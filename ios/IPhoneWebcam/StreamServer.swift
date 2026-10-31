import Foundation
import Network
import UIKit

/// TCP server advertised over Bonjour. Serves one receiver at a time.
///
/// Wire format (iPhone -> PC), repeated:
///   UInt32 big-endian length (of type + payload) | UInt8 type | payload
///   type 1: H.264 Annex B access unit
///   type 2: face detections: UInt8 count, then count x (x, y, w, h) little-endian Float32,
///           normalized to the video frame, origin top-left
/// PC -> iPhone: a single byte 'K' requests a keyframe.
final class StreamServer {
    static let port: NWEndpoint.Port = 5005
    static let serviceType = "_iphonewebcam._tcp"

    var onStatusChange: ((String) -> Void)?
    var onClientChange: ((Bool) -> Void)?
    var onKeyFrameRequested: (() -> Void)?

    private let queue = DispatchQueue(label: "stream.server")
    private var listener: NWListener?
    private var connection: NWConnection?
    private var inFlight = 0
    private var waitingForKeyFrame = true
    private let maxInFlight = 4

    func start() {
        queue.async { self.startListener() }
    }

    func stop() {
        queue.async {
            self.connection?.cancel()
            self.connection = nil
            self.listener?.cancel()
            self.listener = nil
        }
    }

    private func startListener() {
        guard listener == nil else { return }
        do {
            let tcp = NWProtocolTCP.Options()
            tcp.noDelay = true
            let parameters = NWParameters(tls: nil, tcp: tcp)
            parameters.allowLocalEndpointReuse = true
            parameters.includePeerToPeer = false

            let listener = try NWListener(using: parameters, on: Self.port)
            listener.service = NWListener.Service(name: UIDevice.current.name, type: Self.serviceType)
            listener.newConnectionHandler = { [weak self] connection in
                self?.accept(connection)
            }
            listener.stateUpdateHandler = { [weak self] state in
                guard let self else { return }
                switch state {
                case .ready:
                    self.onStatusChange?("Waiting for the PC to connect…")
                case .failed(let error):
                    self.onStatusChange?("Server error: \(error.localizedDescription). Restarting…")
                    self.listener?.cancel()
                    self.listener = nil
                    self.queue.asyncAfter(deadline: .now() + 1) { self.startListener() }
                default:
                    break
                }
            }
            self.listener = listener
            listener.start(queue: queue)
        } catch {
            onStatusChange?("Could not start server: \(error.localizedDescription)")
            queue.asyncAfter(deadline: .now() + 2) { self.startListener() }
        }
    }

    private func accept(_ newConnection: NWConnection) {
        connection?.cancel()
        connection = newConnection
        inFlight = 0
        waitingForKeyFrame = true

        newConnection.stateUpdateHandler = { [weak self, weak newConnection] state in
            guard let self, let newConnection else { return }
            switch state {
            case .ready:
                self.onStatusChange?("Streaming to \(Self.describe(newConnection.endpoint))")
                self.onClientChange?(true)
                self.onKeyFrameRequested?()
            case .failed, .cancelled:
                if self.connection === newConnection {
                    self.connection = nil
                    self.onClientChange?(false)
                    self.onStatusChange?("PC disconnected. Waiting for the PC to connect…")
                }
            default:
                break
            }
        }
        newConnection.start(queue: queue)
        receive(on: newConnection)
    }

    private func receive(on connection: NWConnection) {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 256) { [weak self] data, _, isComplete, error in
            guard let self else { return }
            if let data, data.contains(UInt8(ascii: "K")) {
                self.onKeyFrameRequested?()
            }
            if isComplete || error != nil {
                connection.cancel()
                return
            }
            self.receive(on: connection)
        }
    }

    /// Thread-safe. Drops frames (and waits for the next keyframe) when the network can't keep up.
    func send(frame: Data, isKeyFrame: Bool) {
        queue.async {
            guard let connection = self.connection, case .ready = connection.state else { return }
            if self.waitingForKeyFrame && !isKeyFrame { return }
            if self.inFlight >= self.maxInFlight {
                self.waitingForKeyFrame = true
                self.onKeyFrameRequested?()
                return
            }
            self.waitingForKeyFrame = false

            self.inFlight += 1
            connection.send(content: Self.message(type: 1, payload: frame), completion: .contentProcessed { [weak self] _ in
                self?.inFlight -= 1
            })
        }
    }

    /// Thread-safe. Sends face detections; small, so they are never dropped for bandwidth.
    func send(faces: [CGRect]) {
        var payload = Data(capacity: 1 + faces.count * 16)
        let count = min(faces.count, 8)
        payload.append(UInt8(count))
        for face in faces.prefix(count) {
            for value in [face.minX, face.minY, face.width, face.height] {
                withUnsafeBytes(of: Float32(value).bitPattern.littleEndian) { payload.append(contentsOf: $0) }
            }
        }
        queue.async {
            guard let connection = self.connection, case .ready = connection.state else { return }
            connection.send(content: Self.message(type: 2, payload: payload), completion: .idempotent)
        }
    }

    private static func message(type: UInt8, payload: Data) -> Data {
        let length = UInt32(payload.count + 1)
        var message = Data(capacity: payload.count + 5)
        message.append(UInt8(truncatingIfNeeded: length >> 24))
        message.append(UInt8(truncatingIfNeeded: length >> 16))
        message.append(UInt8(truncatingIfNeeded: length >> 8))
        message.append(UInt8(truncatingIfNeeded: length))
        message.append(type)
        message.append(payload)
        return message
    }

    private static func describe(_ endpoint: NWEndpoint) -> String {
        if case let .hostPort(host, _) = endpoint {
            return "\(host)".components(separatedBy: "%").first ?? "\(host)"
        }
        return "\(endpoint)"
    }
}
