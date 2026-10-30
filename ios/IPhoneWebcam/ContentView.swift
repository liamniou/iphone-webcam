import AVFoundation
import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var camera: CameraController

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            CameraPreview(session: camera.session) { orientation in
                camera.updateOrientation(orientation)
            }
            .ignoresSafeArea()

            VStack {
                HStack(alignment: .top) {
                    VStack(alignment: .leading, spacing: 4) {
                        HStack(spacing: 6) {
                            Circle()
                                .fill(camera.isStreaming ? Color.red : Color.gray)
                                .frame(width: 10, height: 10)
                            Text(camera.isStreaming ? "LIVE" : "IDLE")
                                .font(.caption.bold())
                        }
                        Text(camera.status)
                            .font(.footnote)
                        Text("IP \(camera.ipAddress)   port \(StreamServer.port.rawValue)")
                            .font(.footnote.monospaced())
                        if !camera.resolution.isEmpty {
                            Text(camera.resolution)
                                .font(.footnote.monospaced())
                        }
                    }
                    .padding(10)
                    .background(.black.opacity(0.55), in: RoundedRectangle(cornerRadius: 10))
                    .foregroundStyle(.white)

                    Spacer()

                    Button {
                        camera.switchCamera()
                    } label: {
                        Image(systemName: "arrow.triangle.2.circlepath.camera")
                            .font(.title2)
                            .padding(12)
                            .background(.black.opacity(0.55), in: Circle())
                            .foregroundStyle(.white)
                    }
                    .accessibilityLabel(camera.usingFrontCamera ? "Use back camera" : "Use front camera")
                }
                Spacer()
            }
            .padding()
        }
        .statusBarHidden()
        .persistentSystemOverlays(.hidden)
        .onAppear {
            UIApplication.shared.isIdleTimerDisabled = true
            camera.start()
        }
    }
}

/// Live camera preview that also reports the current interface orientation.
struct CameraPreview: UIViewRepresentable {
    let session: AVCaptureSession
    let onOrientationChange: (UIInterfaceOrientation) -> Void

    func makeUIView(context: Context) -> PreviewView {
        let view = PreviewView()
        view.previewLayer.session = session
        view.previewLayer.videoGravity = .resizeAspect
        view.onOrientationChange = onOrientationChange
        return view
    }

    func updateUIView(_ uiView: PreviewView, context: Context) {
        uiView.onOrientationChange = onOrientationChange
    }
}

final class PreviewView: UIView {
    var onOrientationChange: ((UIInterfaceOrientation) -> Void)?
    private var observer: NSObjectProtocol?

    override class var layerClass: AnyClass { AVCaptureVideoPreviewLayer.self }

    var previewLayer: AVCaptureVideoPreviewLayer {
        // swiftlint:disable:next force_cast
        layer as! AVCaptureVideoPreviewLayer
    }

    override init(frame: CGRect) {
        super.init(frame: frame)
        observer = NotificationCenter.default.addObserver(
            forName: .AVCaptureSessionDidStartRunning, object: nil, queue: .main
        ) { [weak self] _ in
            self?.setNeedsLayout()
        }
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    deinit {
        if let observer {
            NotificationCenter.default.removeObserver(observer)
        }
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        guard let orientation = window?.windowScene?.interfaceOrientation else { return }
        if let connection = previewLayer.connection, connection.isVideoOrientationSupported {
            switch orientation {
            case .landscapeLeft: connection.videoOrientation = .landscapeLeft
            case .landscapeRight: connection.videoOrientation = .landscapeRight
            default: break
            }
        }
        onOrientationChange?(orientation)
    }
}
