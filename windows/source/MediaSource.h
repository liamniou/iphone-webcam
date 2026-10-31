#pragma once

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <ks.h>
#include <ksproxy.h>
#include <ksmedia.h>
#include <wrl.h>
#include <mutex>

#include "../common/SharedFrame.h"

using Microsoft::WRL::ChainInterfaces;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

// Reads frames published by the receiver app through shared memory.
class SharedFrameReader
{
public:
    ~SharedFrameReader();
    // Fills an NV12 destination of the selected format; returns false (and draws a placeholder)
    // when no live frame is available.
    bool Fill(BYTE* dst, LONG pitch, UINT32 width, UINT32 height);

private:
    bool EnsureOpen();
    static void FillPlaceholder(BYTE* dst, LONG pitch, UINT32 width, UINT32 height);
    static void Downscale(const BYTE* src, UINT32 srcW, UINT32 srcH, BYTE* dst, LONG pitch, UINT32 width, UINT32 height, UINT32 factor);
    static void ScaleBilinear(const BYTE* src, UINT32 srcW, UINT32 srcH, BYTE* dst, LONG pitch, UINT32 width, UINT32 height);

    HANDLE m_map = nullptr;
    BYTE* m_view = nullptr;
    ULONGLONG m_lastOpenAttempt = 0;
};

class MediaSource;

class MediaStream : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
                                        ChainInterfaces<IMFMediaStream2, IMFMediaStream, IMFMediaEventGenerator>>
{
public:
    HRESULT RuntimeClassInitialize(IMFMediaSource* source, IMFStreamDescriptor* descriptor);

    // IMFMediaEventGenerator
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
    IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
    IFACEMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* value) override;

    // IMFMediaStream
    IFACEMETHODIMP GetMediaSource(IMFMediaSource** source) override;
    IFACEMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** descriptor) override;
    IFACEMETHODIMP RequestSample(IUnknown* token) override;

    // IMFMediaStream2
    IFACEMETHODIMP SetStreamState(MF_STREAM_STATE state) override;
    IFACEMETHODIMP GetStreamState(MF_STREAM_STATE* state) override;

    HRESULT Start(IMFMediaType* mediaType);
    HRESULT Stop();
    HRESULT Shutdown();
    HRESULT SetAllocator(IUnknown* allocator);

private:
    HRESULT CheckShutdown() const { return m_isShutdown ? MF_E_SHUTDOWN : S_OK; }
    HRESULT CreateSample(IMFSample** sample);

    std::mutex m_lock;
    std::mutex m_sampleLock;
    bool m_isShutdown = false;
    MF_STREAM_STATE m_state = MF_STREAM_STATE_STOPPED;
    ComPtr<IMFMediaSource> m_source;
    ComPtr<IMFStreamDescriptor> m_descriptor;
    ComPtr<IMFMediaEventQueue> m_eventQueue;
    ComPtr<IMFMediaType> m_mediaType;
    UINT32 m_width = vcam::DefaultFormat.width;
    UINT32 m_height = vcam::DefaultFormat.height;
    LONGLONG m_frameDuration = 10'000'000LL / vcam::DefaultFormat.fps; // 100 ns units
    ComPtr<IMFVideoSampleAllocatorEx> m_allocator;
    bool m_allocatorInitialized = false;
    SharedFrameReader m_reader;
    LONGLONG m_nextSampleTime = 0;
    HANDLE m_timer = nullptr; // high-resolution waitable timer for frame pacing

public:
    ~MediaStream()
    {
        if (m_timer) CloseHandle(m_timer);
    }
};

class MediaSource : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
                                        ChainInterfaces<IMFMediaSourceEx, IMFMediaSource, IMFMediaEventGenerator>,
                                        IMFGetService,
                                        IKsControl,
                                        IMFSampleAllocatorControl>
{
public:
    ~MediaSource();
    HRESULT RuntimeClassInitialize(IMFAttributes* activateAttributes);

    // IMFMediaEventGenerator
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
    IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
    IFACEMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* value) override;

    // IMFMediaSource
    IFACEMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) override;
    IFACEMETHODIMP GetCharacteristics(DWORD* characteristics) override;
    IFACEMETHODIMP Pause() override;
    IFACEMETHODIMP Shutdown() override;
    IFACEMETHODIMP Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat, const PROPVARIANT* startPosition) override;
    IFACEMETHODIMP Stop() override;

    // IMFMediaSourceEx
    IFACEMETHODIMP GetSourceAttributes(IMFAttributes** attributes) override;
    IFACEMETHODIMP GetStreamAttributes(DWORD streamId, IMFAttributes** attributes) override;
    IFACEMETHODIMP SetD3DManager(IUnknown* manager) override;

    // IMFGetService
    IFACEMETHODIMP GetService(REFGUID service, REFIID riid, LPVOID* object) override;

    // IKsControl
    IFACEMETHODIMP KsProperty(PKSPROPERTY property, ULONG propertyLength, LPVOID data, ULONG dataLength, ULONG* bytesReturned) override;
    IFACEMETHODIMP KsMethod(PKSMETHOD method, ULONG methodLength, LPVOID data, ULONG dataLength, ULONG* bytesReturned) override;
    IFACEMETHODIMP KsEvent(PKSEVENT event, ULONG eventLength, LPVOID data, ULONG dataLength, ULONG* bytesReturned) override;

    // IMFSampleAllocatorControl
    IFACEMETHODIMP SetDefaultAllocator(DWORD outputStreamId, IUnknown* allocator) override;
    IFACEMETHODIMP GetAllocatorUsage(DWORD outputStreamId, DWORD* inputStreamId, MFSampleAllocatorUsage* usage) override;

private:
    HRESULT CheckShutdown() const { return m_isShutdown ? MF_E_SHUTDOWN : S_OK; }
    HRESULT BuildDescriptors(const vcam::CameraFormat& format);

    std::mutex m_lock;
    bool m_isShutdown = false;
    bool m_mfStarted = false;
    bool m_streamActive = false;
    vcam::CameraFormat m_format = vcam::DefaultFormat;
    ComPtr<IMFMediaEventQueue> m_eventQueue;
    ComPtr<IMFAttributes> m_attributes;
    ComPtr<IMFPresentationDescriptor> m_presentationDescriptor;
    ComPtr<IMFStreamDescriptor> m_streamDescriptor;
    ComPtr<MediaStream> m_stream;
};
