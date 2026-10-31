import CoreVideo
import Foundation
import QuartzCore
import Vision

/// Detects faces in camera frames with Vision, at most a few times per second and on its own queue
/// so capture and encoding are never blocked. Results are normalized rectangles in the frame's
/// coordinate space (origin top-left), i.e. exactly the coordinates of the streamed video.
final class FaceDetector {
    /// Called on the detector's queue with the detected faces (empty when none).
    var onFaces: (([CGRect]) -> Void)?

    private let queue = DispatchQueue(label: "face.detection", qos: .userInitiated)
    private let lock = NSLock()
    private var busy = false
    private var lastRun: CFTimeInterval = 0
    private let interval: CFTimeInterval

    init(interval: CFTimeInterval = 0.15) {
        self.interval = interval
    }

    /// Call for every captured frame; frames arriving while a detection runs (or too soon) are skipped.
    func process(_ pixelBuffer: CVPixelBuffer) {
        let now = CACurrentMediaTime()
        lock.lock()
        guard !busy, now - lastRun >= interval else {
            lock.unlock()
            return
        }
        busy = true
        lastRun = now
        lock.unlock()

        queue.async { [weak self] in
            guard let self else { return }
            let request = VNDetectFaceRectanglesRequest()
            let handler = VNImageRequestHandler(cvPixelBuffer: pixelBuffer, orientation: .up, options: [:])
            var faces: [CGRect] = []
            if (try? handler.perform([request])) != nil {
                faces = (request.results ?? []).map { observation in
                    // Vision uses a bottom-left origin; the stream uses top-left.
                    let box = observation.boundingBox
                    return CGRect(x: box.minX, y: 1 - box.maxY, width: box.width, height: box.height)
                }
            }
            self.onFaces?(faces)

            self.lock.lock()
            self.busy = false
            self.lock.unlock()
        }
    }
}
