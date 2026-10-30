// CaptureTest: opens the "iPhone Webcam" device like any camera app would, reads frames,
// reports the frame rate and saves the last frame as a PNG.

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    void Check(HRESULT hr, const char* what)
    {
        if (FAILED(hr))
        {
            printf("%s failed: 0x%08lX\n", what, hr);
            exit(1);
        }
    }

    uint8_t Clamp(int v) { return static_cast<uint8_t>(std::clamp(v, 0, 255)); }

    void SavePng(const wchar_t* path, const uint8_t* nv12, UINT32 w, UINT32 h)
    {
        std::vector<uint8_t> bgr(static_cast<size_t>(w) * h * 3);
        const uint8_t* uvPlane = nv12 + static_cast<size_t>(w) * h;
        for (UINT32 y = 0; y < h; ++y)
            for (UINT32 x = 0; x < w; ++x)
            {
                int c = nv12[static_cast<size_t>(y) * w + x] - 16;
                const uint8_t* uv = uvPlane + static_cast<size_t>(y / 2) * w + (x & ~1u);
                int d = uv[0] - 128, e = uv[1] - 128;
                uint8_t* p = &bgr[(static_cast<size_t>(y) * w + x) * 3];
                p[2] = Clamp((298 * c + 409 * e + 128) >> 8);
                p[1] = Clamp((298 * c - 100 * d - 208 * e + 128) >> 8);
                p[0] = Clamp((298 * c + 516 * d + 128) >> 8);
            }

        ComPtr<IWICImagingFactory> factory;
        Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "WIC");
        ComPtr<IWICStream> stream;
        Check(factory->CreateStream(&stream), "CreateStream");
        Check(stream->InitializeFromFilename(path, GENERIC_WRITE), "Open PNG");
        ComPtr<IWICBitmapEncoder> encoder;
        Check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "CreateEncoder");
        encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frame;
        encoder->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        frame->SetSize(w, h);
        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
        frame->SetPixelFormat(&format);
        frame->WritePixels(h, w * 3, static_cast<UINT>(bgr.size()), bgr.data());
        frame->Commit();
        encoder->Commit();
    }
}

int wmain(int argc, wchar_t** argv)
{
    const wchar_t* output = argc > 1 ? argv[1] : L"capture.png";
    int frameCount = argc > 2 ? _wtoi(argv[2]) : 90;
    UINT32 requestedWidth = argc > 3 ? static_cast<UINT32>(_wtoi(argv[3])) : 0;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    ComPtr<IMFAttributes> attributes;
    MFCreateAttributes(&attributes, 1);
    attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    Check(MFEnumDeviceSources(attributes.Get(), &devices, &count), "MFEnumDeviceSources");

    ComPtr<IMFActivate> device;
    for (UINT32 i = 0; i < count; ++i)
    {
        wchar_t* name = nullptr;
        UINT32 length = 0;
        devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length);
        wprintf(L"Camera: %s\n", name ? name : L"?");
        if (name && wcsncmp(name, L"iPhone Webcam", 13) == 0) device = devices[i];
        CoTaskMemFree(name);
    }
    for (UINT32 i = 0; i < count; ++i) devices[i]->Release();
    CoTaskMemFree(devices);
    if (!device)
    {
        printf("iPhone Webcam not found\n");
        return 1;
    }

    ComPtr<IMFMediaSource> source;
    Check(device->ActivateObject(IID_PPV_ARGS(&source)), "ActivateObject");
    ComPtr<IMFSourceReader> reader;
    Check(MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader), "CreateSourceReader");

    const DWORD videoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    for (DWORD i = 0;; ++i)
    {
        ComPtr<IMFMediaType> native;
        if (FAILED(reader->GetNativeMediaType(videoStream, i, &native))) break;
        UINT32 nw = 0, nh = 0;
        MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &nw, &nh);
        printf("Offered format %lu: %ux%u\n", i, nw, nh);
        if (requestedWidth != 0 && nw == requestedWidth)
        {
            Check(reader->SetCurrentMediaType(videoStream, nullptr, native.Get()), "SetCurrentMediaType");
        }
    }

    ComPtr<IMFMediaType> type;
    Check(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &type), "GetCurrentMediaType");
    UINT32 w = 0, h = 0;
    MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
    GUID subtype{};
    type->GetGUID(MF_MT_SUBTYPE, &subtype);
    printf("Format: %ux%u %s\n", w, h, subtype == MFVideoFormat_NV12 ? "NV12" : "other");

    ULONGLONG start = GetTickCount64();
    std::vector<uint8_t> last;
    int frames = 0;
    while (frames < frameCount)
    {
        DWORD stream = 0, flags = 0;
        LONGLONG time = 0;
        ComPtr<IMFSample> sample;
        Check(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &stream, &flags, &time, &sample), "ReadSample");
        if (flags & MF_SOURCE_READERF_ERROR) { printf("Stream error\n"); break; }
        if (!sample) continue;
        ++frames;
        ComPtr<IMFMediaBuffer> buffer;
        sample->ConvertToContiguousBuffer(&buffer);
        BYTE* data = nullptr;
        DWORD length = 0;
        buffer->Lock(&data, nullptr, &length);
        last.assign(data, data + length);
        buffer->Unlock();
    }
    double seconds = (GetTickCount64() - start) / 1000.0;
    printf("Read %d frames in %.2f s (%.1f fps)\n", frames, seconds, frames / seconds);

    if (last.size() >= static_cast<size_t>(w) * h * 3 / 2)
    {
        SavePng(output, last.data(), w, h);
        wprintf(L"Saved %s\n", output);
    }
    source->Shutdown();
    MFShutdown();
    return 0;
}
