import SwiftUI

@main
struct IPhoneWebcamApp: App {
    @StateObject private var camera = CameraController()
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(camera)
        }
        .onChange(of: scenePhase) { phase in
            switch phase {
            case .active:
                // The phone is acting as a webcam; never let it auto-lock while the app is open.
                UIApplication.shared.isIdleTimerDisabled = true
                camera.refreshNetworkInfo()
                camera.start()
            case .background:
                UIApplication.shared.isIdleTimerDisabled = false
                camera.stop()
            default:
                break
            }
        }
    }
}
