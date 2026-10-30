#include "FrameWriter.h"
#include "IPhoneWebcamEngine.h"

#include <algorithm>
#include <cstring>

namespace
{
    constexpr uint32_t W = vcam::FrameWidth;
    constexpr uint32_t H = vcam::FrameHeight;
    constexpr ULONGLONG CameraIdleMs = 2000;

    inline uint8_t Clamp(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }
}

FrameWriter::FrameWriter()
    : m_private(vcam::FrameBytes),
      m_preview(static_cast<size_t>(IPWC_PREVIEW_WIDTH) * IPWC_PREVIEW_HEIGHT * 4)
{
}

FrameWriter::~FrameWriter()
{
    if (m_view) UnmapViewOfFile(m_view);
    if (m_map) CloseHandle(m_map);
}

bool FrameWriter::TryOpen()
{
    std::lock_guard lock(m_openLock);
    if (m_view) return true;
    ULONGLONG now = GetTickCount64();
    if (m_lastAttempt != 0 && now - m_lastAttempt < 1000) return false;
    m_lastAttempt = now;

    m_map = vcam::CreateOrOpenSharedMemory();
    if (!m_map) return false;
    m_view = static_cast<uint8_t*>(MapViewOfFile(m_map, FILE_MAP_ALL_ACCESS, 0, 0, vcam::SharedMemorySize));
    if (!m_view)
    {
        CloseHandle(m_map);
        m_map = nullptr;
        return false;
    }

    auto header = reinterpret_cast<vcam::SharedHeader*>(m_view);
    if (header->magic != vcam::SharedMagic)
    {
        header->width = W;
        header->height = H;
        header->frameBytes = vcam::FrameBytes;
        header->latestSlot = 0;
        MemoryBarrier();
        header->magic = vcam::SharedMagic;
    }
    return true;
}

bool FrameWriter::IsCameraInUse()
{
    if (!TryOpen()) return false;
    auto header = reinterpret_cast<const vcam::SharedHeader*>(m_view);
    return GetTickCount64() - static_cast<ULONGLONG>(header->lastReadTick) < CameraIdleMs;
}

void FrameWriter::Write(const DecodedFrame& frame)
{
    if (frame.width < 2 || frame.height < 2) return;

    const bool shared = TryOpen();
    vcam::SharedHeader* header = shared ? reinterpret_cast<vcam::SharedHeader*>(m_view) : nullptr;
    LONG slot = 0;
    uint8_t* dstY = m_private.data();
    if (shared)
    {
        slot = (header->latestSlot + 1) % static_cast<LONG>(vcam::SlotCount);
        dstY = m_view + vcam::SlotOffset(slot);
    }
    uint8_t* dstUV = dstY + static_cast<size_t>(W) * H;

    Scale(frame, dstY, dstUV);
    ApplyTransform(dstY, dstUV);

    if (shared)
    {
        MemoryBarrier();
        InterlockedExchange(&header->latestSlot, slot);
        InterlockedIncrement64(&header->frameCounter);
        InterlockedExchange64(&header->lastWriteTick, static_cast<LONG64>(GetTickCount64()));
    }

    if (m_previewEnabled) UpdatePreview(dstY, dstUV);
}

void FrameWriter::Scale(const DecodedFrame& frame, uint8_t* dstY, uint8_t* dstUV)
{
    if (frame.width == W && frame.height == H)
    {
        for (uint32_t y = 0; y < H; ++y)
            memcpy(dstY + static_cast<size_t>(y) * W, frame.y + static_cast<LONG_PTR>(y) * frame.pitch, W);
        for (uint32_t y = 0; y < H / 2; ++y)
            memcpy(dstUV + static_cast<size_t>(y) * W, frame.uv + static_cast<LONG_PTR>(y) * frame.pitch, W);
        return;
    }

    if (frame.width * 2 == W && frame.height * 2 == H)
    {
        // Exact 2x upscale (e.g. 1080p -> 4K): duplicate pixels and rows.
        for (uint32_t y = 0; y < frame.height; ++y)
        {
            const uint8_t* src = frame.y + static_cast<LONG_PTR>(y) * frame.pitch;
            uint8_t* dst = dstY + static_cast<size_t>(y) * 2 * W;
            for (uint32_t x = 0; x < frame.width; ++x) dst[x * 2] = dst[x * 2 + 1] = src[x];
            memcpy(dst + W, dst, W);
        }
        for (uint32_t y = 0; y < frame.height / 2; ++y)
        {
            const uint8_t* src = frame.uv + static_cast<LONG_PTR>(y) * frame.pitch;
            uint8_t* dst = dstUV + static_cast<size_t>(y) * 2 * W;
            for (uint32_t x = 0; x < frame.width / 2; ++x)
            {
                dst[x * 4] = dst[x * 4 + 2] = src[x * 2];
                dst[x * 4 + 1] = dst[x * 4 + 3] = src[x * 2 + 1];
            }
            memcpy(dst + W, dst, W);
        }
        return;
    }

    // Nearest-neighbour scale-to-fit with black bars (even sizes/offsets keep NV12 chroma aligned).
    double scale = std::min(double(W) / frame.width, double(H) / frame.height);
    uint32_t outW = std::max<uint32_t>(2, static_cast<uint32_t>(frame.width * scale) & ~1u);
    uint32_t outH = std::max<uint32_t>(2, static_cast<uint32_t>(frame.height * scale) & ~1u);
    uint32_t offX = ((W - outW) / 2) & ~1u;
    uint32_t offY = ((H - outH) / 2) & ~1u;

    memset(dstY, 16, static_cast<size_t>(W) * H);
    memset(dstUV, 128, static_cast<size_t>(W) * H / 2);

    std::vector<uint32_t> xmap(outW);
    for (uint32_t x = 0; x < outW; ++x) xmap[x] = static_cast<uint32_t>(uint64_t(x) * frame.width / outW);
    for (uint32_t y = 0; y < outH; ++y)
    {
        const uint8_t* src = frame.y + static_cast<LONG_PTR>(uint64_t(y) * frame.height / outH) * frame.pitch;
        uint8_t* dst = dstY + static_cast<size_t>(offY + y) * W + offX;
        for (uint32_t x = 0; x < outW; ++x) dst[x] = src[xmap[x]];
    }

    uint32_t srcCW = frame.width / 2, srcCH = frame.height / 2;
    uint32_t outCW = outW / 2, outCH = outH / 2;
    for (uint32_t y = 0; y < outCH; ++y)
    {
        const uint8_t* src = frame.uv + static_cast<LONG_PTR>(uint64_t(y) * srcCH / outCH) * frame.pitch;
        uint8_t* dst = dstUV + static_cast<size_t>(offY / 2 + y) * W + offX;
        for (uint32_t x = 0; x < outCW; ++x)
        {
            uint32_t sx = static_cast<uint32_t>(uint64_t(x) * srcCW / outCW) * 2;
            dst[x * 2] = src[sx];
            dst[x * 2 + 1] = src[sx + 1];
        }
    }
}

void FrameWriter::ApplyTransform(uint8_t* y, uint8_t* uv)
{
    // Rotate 180 = flip both axes, so mirror + rotate 180 is a vertical flip only.
    const bool flipH = m_mirror != m_rotate180;
    const bool flipV = m_rotate180;

    if (flipH)
    {
        for (uint32_t row = 0; row < H; ++row)
        {
            uint8_t* p = y + static_cast<size_t>(row) * W;
            std::reverse(p, p + W);
        }
        for (uint32_t row = 0; row < H / 2; ++row)
        {
            auto p = reinterpret_cast<uint16_t*>(uv + static_cast<size_t>(row) * W); // keep U,V pairs together
            std::reverse(p, p + W / 2);
        }
    }
    if (flipV)
    {
        for (uint32_t row = 0; row < H / 2; ++row)
            std::swap_ranges(y + static_cast<size_t>(row) * W, y + static_cast<size_t>(row + 1) * W,
                             y + static_cast<size_t>(H - 1 - row) * W);
        for (uint32_t row = 0; row < H / 4; ++row)
            std::swap_ranges(uv + static_cast<size_t>(row) * W, uv + static_cast<size_t>(row + 1) * W,
                             uv + static_cast<size_t>(H / 2 - 1 - row) * W);
    }
}

void FrameWriter::UpdatePreview(const uint8_t* y, const uint8_t* uv)
{
    constexpr uint32_t PW = IPWC_PREVIEW_WIDTH, PH = IPWC_PREVIEW_HEIGHT;
    constexpr uint32_t Step = W / PW; // 4

    std::lock_guard lock(m_previewLock);
    for (uint32_t py = 0; py < PH; ++py)
    {
        const uint8_t* yRow = y + static_cast<size_t>(py * Step + Step / 2) * W;
        const uint8_t* uvRow = uv + static_cast<size_t>(py * Step / 2) * W;
        uint8_t* out = m_preview.data() + static_cast<size_t>(py) * PW * 4;
        for (uint32_t px = 0; px < PW; ++px)
        {
            // BT.709 limited range -> RGB
            int c = (yRow[px * Step + Step / 2] - 16) * 298;
            const uint8_t* chroma = uvRow + static_cast<size_t>(px * Step / 2) * 2;
            int d = chroma[0] - 128, e = chroma[1] - 128;
            out[px * 4 + 0] = Clamp((c + 541 * d + 128) >> 8);
            out[px * 4 + 1] = Clamp((c - 55 * d - 136 * e + 128) >> 8);
            out[px * 4 + 2] = Clamp((c + 459 * e + 128) >> 8);
            out[px * 4 + 3] = 255;
        }
    }
    ++m_previewId;
}

void FrameWriter::ClearPreview()
{
    std::lock_guard lock(m_previewLock);
    for (size_t i = 0; i < m_preview.size(); i += 4)
    {
        m_preview[i] = m_preview[i + 1] = m_preview[i + 2] = 0;
        m_preview[i + 3] = 255;
    }
    ++m_previewId;
}

int64_t FrameWriter::CopyPreview(uint8_t* bgra, int32_t stride, int64_t lastFrameId)
{
    std::lock_guard lock(m_previewLock);
    if (m_previewId == lastFrameId || m_previewId == 0 || !bgra) return 0;
    const size_t rowBytes = static_cast<size_t>(IPWC_PREVIEW_WIDTH) * 4;
    for (int32_t y = 0; y < IPWC_PREVIEW_HEIGHT; ++y)
        memcpy(bgra + static_cast<size_t>(y) * stride, m_preview.data() + y * rowBytes, rowBytes);
    return m_previewId;
}
