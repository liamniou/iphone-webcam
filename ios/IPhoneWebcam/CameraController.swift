import AVFoundation
import Combine
import UIKit

/// Owns the capture session, encoder and server, and glues them together.
final class CameraController: NSObject, ObservableObject {
    @Published private(set) var status = "Starting…"
    @Published private(set) var isStreaming = false
    @Published private(set) var ipAddress = ""
    @Published private(set) var usingFrontCamera = false
    @Published private(set) var resolution = ""

    let session = AVCaptureSession()

    /// Capture format; chosen by the PC app (default until it connects: 1080p at 30 fps).
    private struct Format: Equatable {
        var width: Int32
        var height: Int32
        var fps: Int
    }

    private let sessionQueue = DispatchQueue(label: "camera.session")
    private let videoQueue = DispatchQueue(label: "camera.video")
    private let videoOutput = AVCaptureVideoDataOutput()
    private let encoder = H264Encoder(fps: 30)
    private let faceDetector = FaceDetector()
    private let server = StreamServer()
    private let clientLock = NSLock()
    private var clientConnected = false
    private var configured = false
    private var orientation: AVCaptureVideoOrientation = .landscapeRight
    private var requestedFormat = Format(width: 1920, height: 1080, fps: 30)
    private var currentDevice: AVCaptureDevice?

    override init() {
        super.init()
        encoder.onEncodedFrame = { [weak self] data, isKeyFrame in
            self?.server.send(frame: data, isKeyFrame: isKeyFrame)
        }
        encoder.onResolutionChange = { [weak self] width, height, fps in
            DispatchQueue.main.async { self?.resolution = "\(width)×\(height) @ \(fps) fps" }
        }
        // Face positions let the PC app auto-frame and retouch; detection runs only while streaming.
        faceDetector.onFaces = { [weak self] faces in
            self?.server.send(faces: faces)
        }
        server.onKeyFrameRequested = { [weak self] in
            self?.encoder.requestKeyFrame()
        }
        server.onFormatRequested = { [weak self] width, height, fps in
            self?.setFormat(width: width, height: height, fps: fps)
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

    /// Switches capture to the format requested by the PC (or the closest one this camera supports).
    func setFormat(width: Int, height: Int, fps: Int) {
        guard width >= 320, height >= 240, (1...120).contains(fps) else { return }
        sessionQueue.async {
            let format = Format(width: Int32(width), height: Int32(height), fps: fps)
            guard format != self.requestedFormat else { return }
            self.requestedFormat = format
            guard self.configured, let device = self.currentDevice else { return }
            self.session.beginConfiguration()
            self.applyFormat(to: device)
            self.applyOrientation()
            self.session.commitConfiguration()
            self.encoder.requestKeyFrame()
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
        currentDevice = device
        applyFormat(to: device)
        return true
    }

    /// Picks the device format closest to the requested size and frame rate and makes it active.
    /// Must be called on the session queue inside begin/commitConfiguration.
    private func applyFormat(to device: AVCaptureDevice) {
        let wanted = requestedFormat
        func dimensions(_ format: AVCaptureDevice.Format) -> CMVideoDimensions {
            CMVideoFormatDescriptionGetDimensions(format.formatDescription)
        }
        func maxRate(_ format: AVCaptureDevice.Format) -> Double {
            format.videoSupportedFrameRateRanges.map(\.maxFrameRate).max() ?? 0
        }
        // 8-bit 4:2:0 formats (no HDR/10-bit), 16:9 only.
        let candidates = device.formats.filter { format in
            let subtype = CMFormatDescriptionGetMediaSubType(format.formatDescription)
            let size = dimensions(format)
            return (subtype == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
                    subtype == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) &&
                size.width * 9 == size.height * 16
        }
        // Prefer: the exact size at the wanted rate > the exact size at any rate >
        // the largest smaller size that reaches the wanted rate > the largest smaller size.
        let exact = candidates.filter { dimensions($0).width == wanted.width && dimensions($0).height == wanted.height }
        let smaller = candidates.filter { dimensions($0).height < wanted.height }
            .sorted { dimensions($0).height > dimensions($1).height }
        let choice = exact.first { maxRate($0) >= Double(wanted.fps) }
            ?? exact.max { maxRate($0) < maxRate($1) }
            ?? smaller.first { maxRate($0) >= Double(wanted.fps) }
            ?? smaller.first

        var fps = wanted.fps
        do {
            try device.lockForConfiguration()
            if let choice {
                device.activeFormat = choice // switches the session preset to .inputPriority
                fps = min(wanted.fps, Int(maxRate(choice)))
            } else if session.canSetSessionPreset(.hd1920x1080) {
                session.sessionPreset = .hd1920x1080
                fps = min(wanted.fps, Int(maxRate(device.activeFormat)))
            }
            let frameDuration = CMTime(value: 1, timescale: CMTimeScale(max(fps, 1)))
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
        encoder.setFrameRate(max(fps, 1))
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
            if let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) {
                faceDetector.process(pixelBuffer)
            }
        }
    }
}
