import AVFoundation
import Combine
import UIKit

/// Owns the capture session, encoder and server, and glues them together.
final class CameraController: NSObject, ObservableObject {
    static let fps = 30

    @Published private(set) var status = "Starting…"
    @Published private(set) var isStreaming = false
    @Published private(set) var ipAddress = ""
    @Published private(set) var usingFrontCamera = false
    @Published private(set) var resolution = ""

    let session = AVCaptureSession()

    private let sessionQueue = DispatchQueue(label: "camera.session")
    private let videoQueue = DispatchQueue(label: "camera.video")
    private let videoOutput = AVCaptureVideoDataOutput()
    private let encoder = H264Encoder(fps: CameraController.fps)
    private let server = StreamServer()
    private let clientLock = NSLock()
    private var clientConnected = false
    private var configured = false
    private var orientation: AVCaptureVideoOrientation = .landscapeRight

    override init() {
        super.init()
        encoder.onEncodedFrame = { [weak self] data, isKeyFrame in
            self?.server.send(frame: data, isKeyFrame: isKeyFrame)
        }
        encoder.onResolutionChange = { [weak self] width, height in
            DispatchQueue.main.async { self?.resolution = "\(width)×\(height) @ \(CameraController.fps) fps" }
        }
        server.onKeyFrameRequested = { [weak self] in
            self?.encoder.requestKeyFrame()
        }
        server.onStatusChange = { [weak self] text in
            DispatchQueue.main.async { self?.status = text }
        }
        server.onClientChange = { [weak self] connected in
            guard let self else { return }
            self.clientLock.lock()
            self.clientConnected = connected
            self.clientLock.unlock()
            DispatchQueue.main.async { self.isStreaming = connected }
        }
    }

    func start() {
        ipAddress = NetworkInfo.wifiIPv4Address() ?? "not on Wi-Fi"
        server.start()

        AVCaptureDevice.requestAccess(for: .video) { [weak self] granted in
            guard let self else { return }
            guard granted else {
                DispatchQueue.main.async { self.status = "Camera access denied. Enable it in Settings." }
                return
            }
            self.sessionQueue.async {
                if !self.configured {
                    self.configureSession()
                }
                if !self.session.isRunning {
                    self.session.startRunning()
                }
            }
        }
    }

    func stop() {
        sessionQueue.async {
            if self.session.isRunning {
                self.session.stopRunning()
            }
        }
    }

    func refreshNetworkInfo() {
        ipAddress = NetworkInfo.wifiIPv4Address() ?? "not on Wi-Fi"
    }

    func switchCamera() {
        sessionQueue.async {
            let front = !self.usingFrontCamera
            self.session.beginConfiguration()
            if self.configureInput(front: front) {
                DispatchQueue.main.async { self.usingFrontCamera = front }
            }
            self.applyOrientation()
            self.session.commitConfiguration()
            self.encoder.requestKeyFrame()
        }
    }

    /// Keeps the streamed image upright for the way the phone is held (either landscape direction).
    func updateOrientation(_ interfaceOrientation: UIInterfaceOrientation) {
        let newOrientation: AVCaptureVideoOrientation
        switch interfaceOrientation {
        case .landscapeLeft: newOrientation = .landscapeLeft
        case .landscapeRight: newOrientation = .landscapeRight
        default: return
        }
        sessionQueue.async {
            self.orientation = newOrientation
            self.applyOrientation()
        }
    }

    private func configureSession() {
        session.beginConfiguration()
        defer { session.commitConfiguration() }

        guard configureInput(front: false) else {
            DispatchQueue.main.async { self.status = "No camera available" }
            return
        }

        videoOutput.videoSettings = [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
        ]
        videoOutput.alwaysDiscardsLateVideoFrames = true
        videoOutput.setSampleBufferDelegate(self, queue: videoQueue)
        if session.canAddOutput(videoOutput) {
            session.addOutput(videoOutput)
        }
        applyOrientation()
        configured = true
    }

    private func configureInput(front: Bool) -> Bool {
        let position: AVCaptureDevice.Position = front ? .front : .back
        guard let device = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: position),
              let input = try? AVCaptureDeviceInput(device: device) else {
            return false
        }
        for existing in session.inputs {
            session.removeInput(existing)
        }
        // Reset to a preset every camera supports so the new input can always be added.
        session.sessionPreset = .high
        guard session.canAddInput(input) else { return false }
        session.addInput(input)

        // 4K when this camera supports it (all back cameras since iPhone 6s, front since iPhone 11),
        // otherwise 1080p; the PC side scales to its 4K output.
        if session.canSetSessionPreset(.hd4K3840x2160) {
            session.sessionPreset = .hd4K3840x2160
        } else if session.canSetSessionPreset(.hd1920x1080) {
            session.sessionPreset = .hd1920x1080
        }

        do {
            try device.lockForConfiguration()
            let frameDuration = CMTime(value: 1, timescale: CMTimeScale(Self.fps))
            let supported = device.activeFormat.videoSupportedFrameRateRanges.contains {
                $0.minFrameDuration <= frameDuration && frameDuration <= $0.maxFrameDuration
            }
            if supported {
                device.activeVideoMinFrameDuration = frameDuration
                device.activeVideoMaxFrameDuration = frameDuration
            }
            if device.isFocusModeSupported(.continuousAutoFocus) {
                device.focusMode = .continuousAutoFocus
            }
            device.unlockForConfiguration()
        } catch {
            print("Could not configure device: \(error)")
        }
        return true
    }

    private func applyOrientation() {
        guard let connection = videoOutput.connection(with: .video) else { return }
        if connection.isVideoOrientationSupported {
            connection.videoOrientation = orientation
        }
        if connection.isVideoStabilizationSupported {
            connection.preferredVideoStabilizationMode = .off // stabilization adds latency
        }
    }
}

extension CameraController: AVCaptureVideoDataOutputSampleBufferDelegate {
    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        clientLock.lock()
        let shouldEncode = clientConnected
        clientLock.unlock()
        if shouldEncode {
            encoder.encode(sampleBuffer)
        }
    }
}
