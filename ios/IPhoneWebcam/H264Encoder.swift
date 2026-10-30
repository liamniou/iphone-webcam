import CoreMedia
import Foundation
import VideoToolbox

/// Hardware H.264 encoder (VideoToolbox) that outputs Annex B access units.
/// Keyframes are prefixed with SPS/PPS so a receiver can start decoding at any keyframe.
/// The session is sized from the incoming frames and recreated if the resolution changes.
final class H264Encoder {
    /// Called on VideoToolbox's callback thread with (annexBData, isKeyFrame).
    var onEncodedFrame: ((Data, Bool) -> Void)?
    /// Called when a session is (re)created, with the encoded width and height.
    var onResolutionChange: ((Int32, Int32) -> Void)?

    private let fps: Int
    private let bitsPerPixel: Double
    private var width: Int32 = 0
    private var height: Int32 = 0
    private var session: VTCompressionSession?
    private let lock = NSLock()
    private var forceKeyFrame = true

    /// `bitsPerPixel` × width × height × fps gives the average bitrate (0.12 ≈ 30 Mbit/s at 4K30).
    init(fps: Int, bitsPerPixel: Double = 0.12) {
        self.fps = fps
        self.bitsPerPixel = bitsPerPixel
    }

    deinit {
        invalidate()
    }

    func requestKeyFrame() {
        lock.lock()
        forceKeyFrame = true
        lock.unlock()
    }

    func invalidate() {
        lock.lock()
        defer { lock.unlock() }
        invalidateLocked()
    }

    private func invalidateLocked() {
        if let session {
            VTCompressionSessionCompleteFrames(session, untilPresentationTimeStamp: .invalid)
            VTCompressionSessionInvalidate(session)
        }
        session = nil
    }

    private func makeSession(width: Int32, height: Int32) -> VTCompressionSession? {
        let bitrate = Int(Double(width) * Double(height) * Double(fps) * bitsPerPixel)
        var newSession: VTCompressionSession?
        let status = VTCompressionSessionCreate(
            allocator: kCFAllocatorDefault,
            width: width,
            height: height,
            codecType: kCMVideoCodecType_H264,
            encoderSpecification: nil,
            imageBufferAttributes: nil,
            compressedDataAllocator: nil,
            outputCallback: nil,
            refcon: nil,
            compressionSessionOut: &newSession)
        guard status == noErr, let newSession else {
            print("VTCompressionSessionCreate failed: \(status)")
            return nil
        }

        let properties: [CFString: Any] = [
            kVTCompressionPropertyKey_RealTime: kCFBooleanTrue!,
            kVTCompressionPropertyKey_ProfileLevel: kVTProfileLevel_H264_High_AutoLevel,
            kVTCompressionPropertyKey_AllowFrameReordering: kCFBooleanFalse!,
            kVTCompressionPropertyKey_AverageBitRate: bitrate,
            kVTCompressionPropertyKey_DataRateLimits: [bitrate * 3 / 2 / 8, 1] as CFArray,
            kVTCompressionPropertyKey_ExpectedFrameRate: fps,
            kVTCompressionPropertyKey_MaxKeyFrameInterval: fps * 2,
            kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration: 2,
        ]
        for (key, value) in properties {
            VTSessionSetProperty(newSession, key: key, value: value as CFTypeRef)
        }
        VTCompressionSessionPrepareToEncodeFrames(newSession)
        return newSession
    }

    /// Must be called from a single (capture) queue.
    func encode(_ sampleBuffer: CMSampleBuffer) {
        guard let imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        let frameWidth = Int32(CVPixelBufferGetWidth(imageBuffer))
        let frameHeight = Int32(CVPixelBufferGetHeight(imageBuffer))

        lock.lock()
        var resized = false
        if session == nil || frameWidth != width || frameHeight != height {
            invalidateLocked()
            width = frameWidth
            height = frameHeight
            session = makeSession(width: frameWidth, height: frameHeight)
            forceKeyFrame = true
            resized = session != nil
        }
        let session = self.session
        let keyFrame = forceKeyFrame
        forceKeyFrame = false
        lock.unlock()
        guard let session else { return }
        if resized {
            onResolutionChange?(frameWidth, frameHeight)
        }

        let frameProperties: CFDictionary? = keyFrame
            ? [kVTEncodeFrameOptionKey_ForceKeyFrame: kCFBooleanTrue!] as CFDictionary
            : nil

        VTCompressionSessionEncodeFrame(
            session,
            imageBuffer: imageBuffer,
            presentationTimeStamp: CMSampleBufferGetPresentationTimeStamp(sampleBuffer),
            duration: .invalid,
            frameProperties: frameProperties,
            infoFlagsOut: nil
        ) { [weak self] status, _, encoded in
            guard status == noErr, let encoded, let self else { return }
            if let result = Self.annexB(from: encoded) {
                self.onEncodedFrame?(result.data, result.isKeyFrame)
            }
        }
    }

    private static let startCode: [UInt8] = [0, 0, 0, 1]

    /// Converts a VideoToolbox sample (AVCC, 4-byte length prefixes) into an Annex B access unit.
    private static func annexB(from sampleBuffer: CMSampleBuffer) -> (data: Data, isKeyFrame: Bool)? {
        guard let dataBuffer = CMSampleBufferGetDataBuffer(sampleBuffer) else { return nil }

        var isKeyFrame = true
        if let attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, createIfNecessary: false) as? [[CFString: Any]],
           let first = attachments.first,
           let notSync = first[kCMSampleAttachmentKey_NotSync] as? Bool {
            isKeyFrame = !notSync
        }

        var output = Data()
        if isKeyFrame, let format = CMSampleBufferGetFormatDescription(sampleBuffer) {
            var count = 0
            CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                format, parameterSetIndex: 0, parameterSetPointerOut: nil, parameterSetSizeOut: nil,
                parameterSetCountOut: &count, nalUnitHeaderLengthOut: nil)
            for index in 0..<count {
                var pointer: UnsafePointer<UInt8>?
                var size = 0
                let status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                    format, parameterSetIndex: index, parameterSetPointerOut: &pointer,
                    parameterSetSizeOut: &size, parameterSetCountOut: nil, nalUnitHeaderLengthOut: nil)
                if status == noErr, let pointer {
                    output.append(contentsOf: startCode)
                    output.append(pointer, count: size)
                }
            }
        }

        let totalLength = CMBlockBufferGetDataLength(dataBuffer)
        var bytes = [UInt8](repeating: 0, count: totalLength)
        guard CMBlockBufferCopyDataBytes(dataBuffer, atOffset: 0, dataLength: totalLength, destination: &bytes) == kCMBlockBufferNoErr else {
            return nil
        }

        var offset = 0
        while offset + 4 <= totalLength {
            let nalLength = Int(bytes[offset]) << 24 | Int(bytes[offset + 1]) << 16 | Int(bytes[offset + 2]) << 8 | Int(bytes[offset + 3])
            offset += 4
            guard nalLength > 0, offset + nalLength <= totalLength else { break }
            output.append(contentsOf: startCode)
            output.append(contentsOf: bytes[offset..<offset + nalLength])
            offset += nalLength
        }
        return output.isEmpty ? nil : (output, isKeyFrame)
    }
}
