#include "MediaSource.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace
{
    constexpr DWORD StreamId = 0;
    constexpr ULONGLONG StaleFrameMs = 2000;

    HRESULT CreateVideoMediaType(UINT32 width, UINT32 height, UINT32 fps, IMFMediaType** result)
    {
        ComPtr<IMFMediaType> type;
        HRESULT hr = MFCreateMediaType(&type);
        if (FAILED(hr)) return hr;

        const UINT32 sampleSize = width * height * 3 / 2;
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width, height);
        MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, fps, 1);
        MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        type->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
        type->SetUINT32(MF_MT_DEFAULT_STRIDE, width);
        type->SetUINT32(MF_MT_SAMPLE_SIZE, sampleSize);
        type->SetUINT32(MF_MT_AVG_BITRATE, sampleSize * 8 * fps);

        *result = type.Detach();
        return S_OK;
    }
}

// ---------------------------------------------------------------------------
// SharedFrameReader
// ---------------------------------------------------------------------------

SharedFrameReader::~SharedFrameReader()
{
    if (m_view) UnmapViewOfFile(m_view);
    if (m_map) CloseHandle(m_map);
}

bool SharedFrameReader::EnsureOpen()
{
    if (m_view) return true;

    ULONGLONG now = GetTickCount64();
    if (m_lastOpenAttempt != 0 && now - m_lastOpenAttempt < 1000) return false;
    m_lastOpenAttempt = now;

    m_map = vcam::CreateOrOpenSharedMemory();
    if (!m_map) return false;

    m_view = static_cast<BYTE*>(MapViewOfFile(m_map, FILE_MAP_ALL_ACCESS, 0, 0, vcam::SharedMemorySize));
    if (!m_view)
    {
        CloseHandle(m_map);
        m_map = nullptr;
        return false;
    }

    auto header = reinterpret_cast<vcam::SharedHeader*>(m_view);
    if (header->magic != vcam::SharedMagic)
    {
        for (auto& size : header->slotSize) size = vcam::PackSize(vcam::FrameWidth, vcam::FrameHeight);
        header->frameBytes = vcam::FrameBytes;
        header->latestSlot = 0;
        header->frameCounter = 0;
        header->lastWriteTick = 0;
        MemoryBarrier();
        header->magic = vcam::SharedMagic;
    }
    return true;
}

void SharedFrameReader::FillPlaceholder(BYTE* dst, LONG pitch, UINT32 width, UINT32 height)
{
    // Dark gray frame with a slowly moving lighter bar so it's obvious the camera is alive.
    UINT32 barX = static_cast<UINT32>((GetTickCount64() / 8) % width);
    for (UINT32 y = 0; y < height; ++y)
    {
        BYTE* row = dst + static_cast<LONG_PTR>(y) * pitch;
        memset(row, 40, width);
        if (y > height / 2 - 4 && y < height / 2 + 4)
        {
            UINT32 barWidth = std::min<UINT32>(width / 8, width - barX);
            memset(row + barX, 120, barWidth);
        }
    }
    BYTE* uv = dst + static_cast<LONG_PTR>(height) * pitch;
    for (UINT32 y = 0; y < height / 2; ++y)
    {
        memset(uv + static_cast<LONG_PTR>(y) * pitch, 128, width);
    }
}

void SharedFrameReader::Downscale(const BYTE* src, UINT32 srcW, UINT32 srcH, BYTE* dst, LONG pitch,
                                  UINT32 width, UINT32 height, UINT32 factor)
{
    // Box filter: each output sample is the average of a factor x factor block.
    const UINT32 area = factor * factor;
    for (UINT32 y = 0; y < height; ++y)
    {
        BYTE* out = dst + static_cast<LONG_PTR>(y) * pitch;
        const BYTE* rows = src + static_cast<size_t>(y) * factor * srcW;
        for (UINT32 x = 0; x < width; ++x)
        {
            UINT32 sum = 0;
            for (UINT32 dy = 0; dy < factor; ++dy)
            {
                const BYTE* p = rows + static_cast<size_t>(dy) * srcW + static_cast<size_t>(x) * factor;
                for (UINT32 dx = 0; dx < factor; ++dx) sum += p[dx];
            }
            out[x] = static_cast<BYTE>(sum / area);
        }
    }

    const BYTE* srcUV = src + static_cast<size_t>(srcW) * srcH;
    BYTE* dstUV = dst + static_cast<LONG_PTR>(height) * pitch;
    for (UINT32 y = 0; y < height / 2; ++y)
    {
        BYTE* out = dstUV + static_cast<LONG_PTR>(y) * pitch;
        const BYTE* rows = srcUV + static_cast<size_t>(y) * factor * srcW;
        for (UINT32 x = 0; x < width / 2; ++x)
        {
            UINT32 sumU = 0, sumV = 0;
            for (UINT32 dy = 0; dy < factor; ++dy)
            {
                const BYTE* p = rows + static_cast<size_t>(dy) * srcW + static_cast<size_t>(x) * factor * 2;
                for (UINT32 dx = 0; dx < factor; ++dx)
                {
                    sumU += p[dx * 2];
                    sumV += p[dx * 2 + 1];
                }
            }
            out[x * 2] = static_cast<BYTE>(sumU / area);
            out[x * 2 + 1] = static_cast<BYTE>(sumV / area);
        }
    }
}

void SharedFrameReader::ScaleBilinear(const BYTE* src, UINT32 srcW, UINT32 srcH, BYTE* dst, LONG pitch,
                                      UINT32 width, UINT32 height)
{
    // Used only while an app keeps an older format after the user changed it (rare, so simple).
    auto plane = [](const BYTE* s, UINT32 sw, UINT32 sh, BYTE* d, LONG dpitch, UINT32 dw, UINT32 dh, UINT32 channels) {
        for (UINT32 y = 0; y < dh; ++y)
        {
            float fy = std::max(0.0f, (y + 0.5f) * sh / dh - 0.5f);
            UINT32 y0 = std::min(static_cast<UINT32>(fy), sh - 1), y1 = std::min(y0 + 1, sh - 1);
            float wy = fy - y0;
            const BYTE* r0 = s + static_cast<size_t>(y0) * sw * channels;
            const BYTE* r1 = s + static_cast<size_t>(y1) * sw * channels;
            BYTE* out = d + static_cast<LONG_PTR>(y) * dpitch;
            for (UINT32 x = 0; x < dw; ++x)
            {
                float fx = std::max(0.0f, (x + 0.5f) * sw / dw - 0.5f);
                UINT32 x0 = std::min(static_cast<UINT32>(fx), sw - 1), x1 = std::min(x0 + 1, sw - 1);
                float wx = fx - x0;
                for (UINT32 c = 0; c < channels; ++c)
                {
                    float top = r0[x0 * channels + c] * (1 - wx) + r0[x1 * channels + c] * wx;
                    float bottom = r1[x0 * channels + c] * (1 - wx) + r1[x1 * channels + c] * wx;
                    out[x * channels + c] = static_cast<BYTE>(top * (1 - wy) + bottom * wy + 0.5f);
                }
            }
        }
    };
    plane(src, srcW, srcH, dst, pitch, width, height, 1);
    plane(src + static_cast<size_t>(srcW) * srcH, srcW / 2, srcH / 2,
          dst + static_cast<LONG_PTR>(height) * pitch, pitch, width / 2, height / 2, 2);
}

bool SharedFrameReader::Fill(BYTE* dst, LONG pitch, UINT32 width, UINT32 height)
{
    if (width >= 2 && height >= 2 && EnsureOpen())
    {
        auto header = reinterpret_cast<vcam::SharedHeader*>(m_view);
        InterlockedExchange64(&header->lastReadTick, static_cast<LONG64>(GetTickCount64()));
        ULONGLONG lastWrite = static_cast<ULONGLONG>(header->lastWriteTick);
        if (header->magic == vcam::SharedMagic && header->frameCounter > 0 &&
            GetTickCount64() - lastWrite < StaleFrameMs)
        {
            const LONG slot = header->latestSlot;
            const LONG64 size = slot >= 0 && slot < static_cast<LONG>(vcam::SlotCount)
                ? InterlockedCompareExchange64(&header->slotSize[slot], 0, 0) : 0;
            const UINT32 srcW = vcam::PackedWidth(size), srcH = vcam::PackedHeight(size);
            if (srcW >= 2 && srcH >= 2 && srcW <= vcam::FrameWidth && srcH <= vcam::FrameHeight)
            {
                const BYTE* src = m_view + vcam::SlotOffset(slot);
                const UINT32 factor = srcW / width;
                if (srcW == width && srcH == height)
                {
                    for (UINT32 y = 0; y < height * 3 / 2; ++y)
                    {
                        memcpy(dst + static_cast<LONG_PTR>(y) * pitch, src + static_cast<size_t>(y) * width, width);
                    }
                }
                else if (factor > 1 && width * factor == srcW && height * factor == srcH)
                {
                    Downscale(src, srcW, srcH, dst, pitch, width, height, factor);
                }
                else
                {
                    ScaleBilinear(src, srcW, srcH, dst, pitch, width, height);
                }
                return true;
            }
        }
    }
    FillPlaceholder(dst, pitch, width, height);
    return false;
}

// ---------------------------------------------------------------------------
// MediaStream
// ---------------------------------------------------------------------------

HRESULT MediaStream::RuntimeClassInitialize(IMFMediaSource* source, IMFStreamDescriptor* descriptor)
{
    m_source = source;
    m_descriptor = descriptor;

    HRESULT hr = MFCreateEventQueue(&m_eventQueue);
    if (FAILED(hr)) return hr;

    ComPtr<IMFMediaTypeHandler> handler;
    hr = m_descriptor->GetMediaTypeHandler(&handler);
    if (FAILED(hr)) return hr;
    return handler->GetCurrentMediaType(&m_mediaType);
}

IFACEMETHODIMP MediaStream::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state)
{
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    return m_eventQueue->BeginGetEvent(callback, state);
}

IFACEMETHODIMP MediaStream::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event)
{
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    return m_eventQueue->EndGetEvent(result, event);
}

IFACEMETHODIMP MediaStream::GetEvent(DWORD flags, IMFMediaEvent** event)
{
    ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard lock(m_lock);
        HRESULT hr = CheckShutdown();
        if (FAILED(hr)) return hr;
        queue = m_eventQueue;
    }
    return queue->GetEvent(flags, event); // may block; must not hold the lock
}

IFACEMETHODIMP MediaStream::QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* value)
{
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    return m_eventQueue->QueueEventParamVar(type, extendedType, status, value);
}

IFACEMETHODIMP MediaStream::GetMediaSource(IMFMediaSource** source)
{
    if (!source) return E_POINTER;
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    return m_source.CopyTo(source);
}

IFACEMETHODIMP MediaStream::GetStreamDescriptor(IMFStreamDescriptor** descriptor)
{
    if (!descriptor) return E_POINTER;
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    return m_descriptor.CopyTo(descriptor);
}

HRESULT MediaStream::CreateSample(IMFSample** result)
{
    ComPtr<IMFSample> sample;
    ComPtr<IMFVideoSampleAllocatorEx> allocator;
    UINT32 width, height;
    {
        std::lock_guard lock(m_lock);
        if (m_allocatorInitialized) allocator = m_allocator;
        width = m_width;
        height = m_height;
    }
    const DWORD frameBytes = width * height * 3 / 2;

    HRESULT hr;
    if (allocator)
    {
        hr = allocator->AllocateSample(&sample);
        if (FAILED(hr)) return hr;
    }
    else
    {
        hr = MFCreateSample(&sample);
        if (FAILED(hr)) return hr;
        ComPtr<IMFMediaBuffer> buffer;
        hr = MFCreate2DMediaBuffer(width, height, MFVideoFormat_NV12.Data1, FALSE, &buffer);
        if (FAILED(hr)) return hr;
        hr = sample->AddBuffer(buffer.Get());
        if (FAILED(hr)) return hr;
    }

    ComPtr<IMFMediaBuffer> buffer;
    hr = sample->GetBufferByIndex(0, &buffer);
    if (FAILED(hr)) return hr;

    ComPtr<IMF2DBuffer2> buffer2d;
    if (SUCCEEDED(buffer.As(&buffer2d)))
    {
        BYTE* scanline0 = nullptr;
        BYTE* bufferStart = nullptr;
        LONG pitch = 0;
        DWORD length = 0;
        hr = buffer2d->Lock2DSize(MF2DBuffer_LockFlags_Write, &scanline0, &pitch, &bufferStart, &length);
        if (FAILED(hr)) return hr;
        m_reader.Fill(scanline0, pitch, width, height);
        buffer2d->Unlock2D();
    }
    else
    {
        BYTE* data = nullptr;
        DWORD maxLength = 0;
        hr = buffer->Lock(&data, &maxLength, nullptr);
        if (FAILED(hr)) return hr;
        if (maxLength >= frameBytes)
        {
            m_reader.Fill(data, static_cast<LONG>(width), width, height);
        }
        buffer->Unlock();
        buffer->SetCurrentLength(frameBytes);
    }

    *result = sample.Detach();
    return S_OK;
}

IFACEMETHODIMP MediaStream::RequestSample(IUnknown* token)
{
    {
        std::lock_guard lock(m_lock);
        HRESULT hr = CheckShutdown();
        if (FAILED(hr)) return hr;
        if (m_state != MF_STREAM_STATE_RUNNING) return MF_E_MEDIA_SOURCE_WRONGSTATE;
    }

    std::lock_guard sampleLock(m_sampleLock);

    // Pace delivery to the nominal frame rate in case the consumer pulls faster. Frames are scheduled
    // on an absolute timeline (so oversleeping doesn't accumulate) and waited for with a
    // high-resolution timer (plain Sleep() has ~15.6 ms granularity, too coarse for 60 fps).
    LONGLONG frameDuration;
    {
        std::lock_guard lock(m_lock);
        frameDuration = m_frameDuration;
    }
    LONGLONG now = MFGetSystemTime();
    if (m_nextSampleTime == 0 || now - m_nextSampleTime > 2 * frameDuration || m_nextSampleTime - now > 2 * frameDuration)
    {
        m_nextSampleTime = now;
    }
    if (m_nextSampleTime > now)
    {
        if (!m_timer) m_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        LARGE_INTEGER due;
        due.QuadPart = -(m_nextSampleTime - now); // relative, 100 ns units
        if (m_timer && SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(m_timer, 1000);
        else Sleep(static_cast<DWORD>((m_nextSampleTime - now) / 10'000));
        now = MFGetSystemTime();
    }
    m_nextSampleTime += frameDuration;

    ComPtr<IMFSample> sample;
    HRESULT hr = CreateSample(&sample);
    if (FAILED(hr)) return hr;

    sample->SetSampleTime(now);
    sample->SetSampleDuration(frameDuration);
    if (token) sample->SetUnknown(MFSampleExtension_Token, token);

    std::lock_guard lock(m_lock);
    hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    return m_eventQueue->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
}

IFACEMETHODIMP MediaStream::SetStreamState(MF_STREAM_STATE state)
{
    MF_STREAM_STATE current;
    {
        std::lock_guard lock(m_lock);
        HRESULT hr = CheckShutdown();
        if (FAILED(hr)) return hr;
        current = m_state;
    }
    if (state == current) return S_OK;

    switch (state)
    {
    case MF_STREAM_STATE_RUNNING:
    {
        ComPtr<IMFMediaTypeHandler> handler;
        ComPtr<IMFMediaType> type;
        if (SUCCEEDED(m_descriptor->GetMediaTypeHandler(&handler))) handler->GetCurrentMediaType(&type);
        return Start(type.Get());
    }
    case MF_STREAM_STATE_STOPPED:
        return Stop();
    case MF_STREAM_STATE_PAUSED:
    {
        std::lock_guard lock(m_lock);
        m_state = MF_STREAM_STATE_PAUSED;
        return S_OK;
    }
    default:
        return MF_E_INVALID_STATE_TRANSITION;
    }
}

IFACEMETHODIMP MediaStream::GetStreamState(MF_STREAM_STATE* state)
{
    if (!state) return E_POINTER;
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    *state = m_state;
    return S_OK;
}

HRESULT MediaStream::Start(IMFMediaType* mediaType)
{
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;

    if (mediaType)
    {
        UINT32 width = 0, height = 0;
        hr = MFGetAttributeSize(mediaType, MF_MT_FRAME_SIZE, &width, &height);
        if (FAILED(hr)) return hr;
        if (!m_mediaType || width != m_width || height != m_height)
        {
            if (m_allocator) m_allocator->UninitializeSampleAllocator();
            m_allocatorInitialized = false;
        }
        m_mediaType = mediaType;
        m_width = width;
        m_height = height;
        UINT32 numerator = 0, denominator = 0;
        if (SUCCEEDED(MFGetAttributeRatio(mediaType, MF_MT_FRAME_RATE, &numerator, &denominator)) && numerator > 0 && denominator > 0)
        {
            m_frameDuration = 10'000'000LL * denominator / numerator;
        }
    }

    if (m_allocator && !m_allocatorInitialized)
    {
        hr = m_allocator->InitializeSampleAllocatorEx(2, 10, nullptr, m_mediaType.Get());
        m_allocatorInitialized = SUCCEEDED(hr);
    }

    m_state = MF_STREAM_STATE_RUNNING;
    PROPVARIANT time;
    PropVariantInit(&time);
    time.vt = VT_I8;
    time.hVal.QuadPart = MFGetSystemTime();
    return m_eventQueue->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, &time);
}

HRESULT MediaStream::Stop()
{
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;

    m_state = MF_STREAM_STATE_STOPPED;
    return m_eventQueue->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
}

HRESULT MediaStream::Shutdown()
{
    std::lock_guard lock(m_lock);
    if (m_isShutdown) return S_OK;
    m_isShutdown = true;
    m_state = MF_STREAM_STATE_STOPPED;
    if (m_eventQueue) m_eventQueue->Shutdown();
    if (m_allocator) m_allocator->UninitializeSampleAllocator();
    m_allocator.Reset();
    m_allocatorInitialized = false;
    m_source.Reset();
    return S_OK;
}

HRESULT MediaStream::SetAllocator(IUnknown* allocator)
{
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;

    if (m_allocator) m_allocator->UninitializeSampleAllocator();
    m_allocator.Reset();
    m_allocatorInitialized = false;
    if (allocator)
    {
        hr = allocator->QueryInterface(IID_PPV_ARGS(&m_allocator));
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}

// ---------------------------------------------------------------------------
// MediaSource
// ---------------------------------------------------------------------------

MediaSource::~MediaSource()
{
    Shutdown();
    if (m_mfStarted) MFShutdown();
}

HRESULT MediaSource::RuntimeClassInitialize(IMFAttributes* activateAttributes)
{
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) return hr;
    m_mfStarted = true;

    hr = MFCreateEventQueue(&m_eventQueue);
    if (FAILED(hr)) return hr;

    hr = MFCreateAttributes(&m_attributes, 4);
    if (FAILED(hr)) return hr;
    if (activateAttributes) activateAttributes->CopyAllItems(m_attributes.Get());

    return BuildDescriptors(vcam::ReadCameraFormat());
}

HRESULT MediaSource::BuildDescriptors(const vcam::CameraFormat& format)
{
    // The camera offers exactly the format chosen in the app.
    ComPtr<IMFMediaType> type;
    HRESULT hr = CreateVideoMediaType(format.width, format.height, format.fps, &type);
    if (FAILED(hr)) return hr;

    IMFMediaType* types[] = { type.Get() };
    ComPtr<IMFStreamDescriptor> streamDescriptor;
    hr = MFCreateStreamDescriptor(StreamId, ARRAYSIZE(types), types, &streamDescriptor);
    if (FAILED(hr)) return hr;

    ComPtr<IMFMediaTypeHandler> handler;
    hr = streamDescriptor->GetMediaTypeHandler(&handler);
    if (FAILED(hr)) return hr;
    hr = handler->SetCurrentMediaType(type.Get());
    if (FAILED(hr)) return hr;

    streamDescriptor->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
    streamDescriptor->SetUINT32(MF_DEVICESTREAM_STREAM_ID, StreamId);
    streamDescriptor->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
    streamDescriptor->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);

    IMFStreamDescriptor* descriptors[] = { streamDescriptor.Get() };
    ComPtr<IMFPresentationDescriptor> presentationDescriptor;
    hr = MFCreatePresentationDescriptor(ARRAYSIZE(descriptors), descriptors, &presentationDescriptor);
    if (FAILED(hr)) return hr;
    hr = presentationDescriptor->SelectStream(0);
    if (FAILED(hr)) return hr;

    ComPtr<MediaStream> stream;
    hr = Microsoft::WRL::MakeAndInitialize<MediaStream>(&stream, this, streamDescriptor.Get());
    if (FAILED(hr)) return hr;

    if (m_stream) m_stream->Shutdown();
    m_stream = stream;
    m_streamDescriptor = streamDescriptor;
    m_presentationDescriptor = presentationDescriptor;
    m_format = format;
    return S_OK;
}

IFACEMETHODIMP MediaSource::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state)
{
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    return m_eventQueue->BeginGetEvent(callback, state);
}

IFACEMETHODIMP MediaSource::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event)
{
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    return m_eventQueue->EndGetEvent(result, event);
}

IFACEMETHODIMP MediaSource::GetEvent(DWORD flags, IMFMediaEvent** event)
{
    ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard lock(m_lock);
        HRESULT hr = CheckShutdown();
        if (FAILED(hr)) return hr;
        queue = m_eventQueue;
    }
    return queue->GetEvent(flags, event);
}

IFACEMETHODIMP MediaSource::QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* value)
{
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    return m_eventQueue->QueueEventParamVar(type, extendedType, status, value);
}

IFACEMETHODIMP MediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor)
{
    if (!descriptor) return E_POINTER;
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;

    // A new client is opening the camera: pick up a format change made in the app meanwhile
    // (only while nobody is streaming, so running clients keep a consistent stream).
    if (!m_streamActive)
    {
        vcam::CameraFormat format = vcam::ReadCameraFormat();
        if (!(format == m_format)) BuildDescriptors(format);
    }
    return m_presentationDescriptor->Clone(descriptor);
}

IFACEMETHODIMP MediaSource::GetCharacteristics(DWORD* characteristics)
{
    if (!characteristics) return E_POINTER;
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    *characteristics = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
}

IFACEMETHODIMP MediaSource::Pause()
{
    return MF_E_INVALID_STATE_TRANSITION;
}

IFACEMETHODIMP MediaSource::Shutdown()
{
    ComPtr<MediaStream> stream;
    {
        std::lock_guard lock(m_lock);
        if (m_isShutdown) return S_OK;
        m_isShutdown = true;
        if (m_eventQueue) m_eventQueue->Shutdown();
        stream = std::move(m_stream);
    }
    if (stream) stream->Shutdown();
    return S_OK;
}

IFACEMETHODIMP MediaSource::Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat, const PROPVARIANT* startPosition)
{
    UNREFERENCED_PARAMETER(startPosition);
    if (!descriptor) return E_INVALIDARG;
    if (timeFormat && *timeFormat != GUID_NULL) return MF_E_UNSUPPORTED_TIME_FORMAT;

    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;

    DWORD count = 0;
    hr = descriptor->GetStreamDescriptorCount(&count);
    if (FAILED(hr)) return hr;

    for (DWORD i = 0; i < count; ++i)
    {
        BOOL selected = FALSE;
        ComPtr<IMFStreamDescriptor> streamDescriptor;
        hr = descriptor->GetStreamDescriptorByIndex(i, &selected, &streamDescriptor);
        if (FAILED(hr)) return hr;

        DWORD id = 0;
        hr = streamDescriptor->GetStreamIdentifier(&id);
        if (FAILED(hr)) return hr;
        if (id != StreamId) return MF_E_INVALIDSTREAMNUMBER;

        if (selected)
        {
            ComPtr<IMFMediaTypeHandler> handler;
            ComPtr<IMFMediaType> currentType;
            hr = streamDescriptor->GetMediaTypeHandler(&handler);
            if (SUCCEEDED(hr)) hr = handler->GetCurrentMediaType(&currentType);
            if (FAILED(hr)) return hr;

            ComPtr<IUnknown> unknown;
            m_stream.As(&unknown);
            hr = m_eventQueue->QueueEventParamUnk(m_streamActive ? MEUpdatedStream : MENewStream,
                                                  GUID_NULL, S_OK, unknown.Get());
            if (FAILED(hr)) return hr;
            hr = m_stream->Start(currentType.Get());
            if (FAILED(hr)) return hr;
            m_streamActive = true;
        }
        else if (m_streamActive)
        {
            m_stream->Stop();
            m_streamActive = false;
        }
    }

    PROPVARIANT time;
    PropVariantInit(&time);
    time.vt = VT_I8;
    time.hVal.QuadPart = MFGetSystemTime();
    return m_eventQueue->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &time);
}

IFACEMETHODIMP MediaSource::Stop()
{
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;

    if (m_streamActive)
    {
        m_stream->Stop();
        m_streamActive = false;
    }
    return m_eventQueue->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
}

IFACEMETHODIMP MediaSource::GetSourceAttributes(IMFAttributes** attributes)
{
    if (!attributes) return E_POINTER;
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    return m_attributes.CopyTo(attributes);
}

IFACEMETHODIMP MediaSource::GetStreamAttributes(DWORD streamId, IMFAttributes** attributes)
{
    if (!attributes) return E_POINTER;
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    if (streamId != StreamId) return MF_E_INVALIDSTREAMNUMBER;
    return m_streamDescriptor.CopyTo(attributes);
}

IFACEMETHODIMP MediaSource::SetD3DManager(IUnknown* manager)
{
    UNREFERENCED_PARAMETER(manager);
    std::lock_guard lock(m_lock);
    return CheckShutdown();
}

IFACEMETHODIMP MediaSource::GetService(REFGUID service, REFIID riid, LPVOID* object)
{
    UNREFERENCED_PARAMETER(service);
    UNREFERENCED_PARAMETER(riid);
    if (!object) return E_POINTER;
    *object = nullptr;
    return MF_E_UNSUPPORTED_SERVICE;
}

IFACEMETHODIMP MediaSource::KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG*)
{
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP MediaSource::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG*)
{
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP MediaSource::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG*)
{
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP MediaSource::SetDefaultAllocator(DWORD outputStreamId, IUnknown* allocator)
{
    ComPtr<MediaStream> stream;
    {
        std::lock_guard lock(m_lock);
        HRESULT hr = CheckShutdown();
        if (FAILED(hr)) return hr;
        if (outputStreamId != StreamId) return MF_E_INVALIDSTREAMNUMBER;
        stream = m_stream;
    }
    return stream->SetAllocator(allocator);
}

IFACEMETHODIMP MediaSource::GetAllocatorUsage(DWORD outputStreamId, DWORD* inputStreamId, MFSampleAllocatorUsage* usage)
{
    if (!inputStreamId || !usage) return E_POINTER;
    std::lock_guard lock(m_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    if (outputStreamId != StreamId) return MF_E_INVALIDSTREAMNUMBER;
    *inputStreamId = outputStreamId;
    *usage = MFSampleAllocatorUsage_UsesProvidedAllocator;
    return S_OK;
}
