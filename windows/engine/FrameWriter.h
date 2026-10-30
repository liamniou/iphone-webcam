#pragma once

#include "H264Decoder.h"
#include "../common/SharedFrame.h"

#include <atomic>
#include <mutex>
#include <vector>

// Turns decoded frames into the virtual camera's fixed 4K NV12 format (scaling/letterboxing,
// mirror/rotation), publishes them to the shared memory read by the media source and
// produces a small BGRA preview for the UI.
class FrameWriter
{
public:
    FrameWriter();
    ~FrameWriter();

    void SetTransform(bool mirror, bool rotate180) { m_mirror = mirror; m_rotate180 = rotate180; }
    void SetPreviewEnabled(bool enabled) { m_previewEnabled = enabled; }

    void Write(const DecodedFrame& frame);
    void ClearPreview();

    bool IsCameraInUse();
    int64_t CopyPreview(uint8_t* bgra, int32_t stride, int64_t lastFrameId);

private:
    bool TryOpen();
    void Scale(const DecodedFrame& frame, uint8_t* dstY, uint8_t* dstUV);
    void ApplyTransform(uint8_t* y, uint8_t* uv);
    void UpdatePreview(const uint8_t* y, const uint8_t* uv);

    std::mutex m_openLock;
    HANDLE m_map = nullptr;
    uint8_t* volatile m_view = nullptr;
    ULONGLONG m_lastAttempt = 0;
    std::vector<uint8_t> m_private; // used when no app has opened the camera yet
    std::atomic<bool> m_mirror{ false };
    std::atomic<bool> m_rotate180{ false };
    std::atomic<bool> m_previewEnabled{ false };

    std::mutex m_previewLock;
    std::vector<uint8_t> m_preview;
    int64_t m_previewId = 0;
};
