// FakePhone: stands in for the iPhone app during development. Encodes a moving test pattern
// with the Windows H.264 encoder and serves it using the same wire protocol and Bonjour type.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windns.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mftransform.h>
#include <codecapi.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    UINT32 g_width = 1920;
    UINT32 g_height = 1080;
    constexpr UINT32 Fps = 30;
    constexpr uint16_t Port = 5005;

    void Check(HRESULT hr, const char* what)
    {
        if (FAILED(hr))
        {
            printf("%s failed: 0x%08lX\n", what, hr);
            exit(1);
        }
    }

    ComPtr<IMFTransform> CreateEncoder()
    {
        ComPtr<IMFTransform> encoder;
        Check(CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&encoder)), "Create H.264 encoder");

        ComPtr<IMFMediaType> output;
        Check(MFCreateMediaType(&output), "MFCreateMediaType");
        output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        output->SetUINT32(MF_MT_AVG_BITRATE, 6'000'000);
        MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, g_width, g_height);
        MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, Fps, 1);
        MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        output->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base);
        Check(encoder->SetOutputType(0, output.Get(), 0), "SetOutputType");

        ComPtr<IMFMediaType> input;
        Check(MFCreateMediaType(&input), "MFCreateMediaType");
        input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, g_width, g_height);
        MFSetAttributeRatio(input.Get(), MF_MT_FRAME_RATE, Fps, 1);
        MFSetAttributeRatio(input.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        Check(encoder->SetInputType(0, input.Get(), 0), "SetInputType");

        ComPtr<ICodecAPI> codecApi;
        if (SUCCEEDED(encoder.As(&codecApi)))
        {
            VARIANT value;
            VariantInit(&value);
            value.vt = VT_UI4;
            value.ulVal = Fps * 2;
            codecApi->SetValue(&CODECAPI_AVEncMPVGOPSize, &value);
            value.vt = VT_BOOL;
            value.boolVal = VARIANT_TRUE;
            codecApi->SetValue(&CODECAPI_AVLowLatencyMode, &value);
        }

        encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        return encoder;
    }

    void FillPattern(std::vector<uint8_t>& nv12, uint32_t frame)
    {
        const UINT32 W = g_width, H = g_height;
        for (UINT32 y = 0; y < H; ++y)
            for (UINT32 x = 0; x < W; ++x)
                nv12[y * W + x] = static_cast<uint8_t>(((x + frame * 8) / 80 + y / 80) % 2 ? 180 : 60);
        uint8_t* uv = nv12.data() + W * H;
        for (UINT32 y = 0; y < H / 2; ++y)
            for (UINT32 x = 0; x < W / 2; ++x)
            {
                uv[y * W + x * 2] = static_cast<uint8_t>(x * 255 / (W / 2));
                uv[y * W + x * 2 + 1] = static_cast<uint8_t>(255 - y * 255 / (H / 2));
            }
        // White square that travels left to right, to make motion obvious.
        UINT32 boxX = (frame * 12) % (W - 120);
        for (UINT32 y = H / 2 - 60; y < H / 2 + 60; ++y)
            memset(nv12.data() + y * W + boxX, 235, 120);
    }

    bool SendAll(SOCKET s, const uint8_t* data, size_t length)
    {
        while (length > 0)
        {
            int sent = send(s, reinterpret_cast<const char*>(data), static_cast<int>(length), 0);
            if (sent <= 0) return false;
            data += sent;
            length -= sent;
        }
        return true;
    }

    bool SendFrame(SOCKET s, const uint8_t* data, size_t length)
    {
        uint32_t messageLength = static_cast<uint32_t>(length + 1);
        uint8_t header[5] = {
            static_cast<uint8_t>(messageLength >> 24), static_cast<uint8_t>(messageLength >> 16),
            static_cast<uint8_t>(messageLength >> 8), static_cast<uint8_t>(messageLength), 1 };
        return SendAll(s, header, sizeof(header)) && SendAll(s, data, length);
    }

    bool DrainEncoder(IMFTransform* encoder, SOCKET s, DWORD outputSize, bool providesSamples)
    {
        for (;;)
        {
            MFT_OUTPUT_DATA_BUFFER output{};
            ComPtr<IMFSample> own;
            if (!providesSamples)
            {
                ComPtr<IMFMediaBuffer> buffer;
                MFCreateMemoryBuffer(outputSize, &buffer);
                MFCreateSample(&own);
                own->AddBuffer(buffer.Get());
                output.pSample = own.Get();
            }
            DWORD status = 0;
            HRESULT hr = encoder->ProcessOutput(0, 1, &output, &status);
            if (output.pEvents) output.pEvents->Release();
            ComPtr<IMFSample> sample;
            if (providesSamples) sample.Attach(output.pSample);
            else sample = own;

            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) return true;
            if (FAILED(hr) || !sample) return hr == MF_E_TRANSFORM_STREAM_CHANGE;

            ComPtr<IMFMediaBuffer> buffer;
            sample->ConvertToContiguousBuffer(&buffer);
            BYTE* data = nullptr;
            DWORD length = 0;
            buffer->Lock(&data, nullptr, &length);
            bool ok = SendFrame(s, data, length);
            buffer->Unlock();
            if (!ok) return false;
        }
    }

    void Stream(SOCKET client)
    {
        auto encoder = CreateEncoder();
        MFT_OUTPUT_STREAM_INFO info{};
        encoder->GetOutputStreamInfo(0, &info);
        bool providesSamples = (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
        DWORD outputSize = info.cbSize ? info.cbSize : g_width * g_height;

        std::vector<uint8_t> nv12(g_width * g_height * 3 / 2);
        LONGLONG time = 0;
        ULONGLONG start = GetTickCount64();
        for (uint32_t frame = 0;; ++frame)
        {
            // Poll for keyframe requests without blocking.
            u_long nonBlocking = 1;
            ioctlsocket(client, FIONBIO, &nonBlocking);
            char request = 0;
            if (recv(client, &request, 1, 0) == 1 && request == 'K')
            {
                ComPtr<ICodecAPI> codecApi;
                if (SUCCEEDED(encoder.As(&codecApi)))
                {
                    VARIANT value;
                    VariantInit(&value);
                    value.vt = VT_UI4;
                    value.ulVal = 1;
                    codecApi->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &value);
                }
                printf("Keyframe requested by receiver\n");
            }
            u_long blocking = 0;
            ioctlsocket(client, FIONBIO, &blocking);

            FillPattern(nv12, frame);
            ComPtr<IMFMediaBuffer> buffer;
            MFCreateMemoryBuffer(static_cast<DWORD>(nv12.size()), &buffer);
            BYTE* data = nullptr;
            buffer->Lock(&data, nullptr, nullptr);
            memcpy(data, nv12.data(), nv12.size());
            buffer->Unlock();
            buffer->SetCurrentLength(static_cast<DWORD>(nv12.size()));
            ComPtr<IMFSample> sample;
            MFCreateSample(&sample);
            sample->AddBuffer(buffer.Get());
            sample->SetSampleTime(time);
            sample->SetSampleDuration(10'000'000 / Fps);
            time += 10'000'000 / Fps;

            if (FAILED(encoder->ProcessInput(0, sample.Get(), 0))) break;
            if (!DrainEncoder(encoder.Get(), client, outputSize, providesSamples)) break;

            ULONGLONG due = start + (frame + 1) * 1000ULL / Fps;
            ULONGLONG now = GetTickCount64();
            if (due > now) Sleep(static_cast<DWORD>(due - now));
            if (frame % (Fps * 5) == 0) printf("Sent %u frames\n", frame);
        }
        printf("Receiver disconnected\n");
    }

    VOID WINAPI OnRegistered(DWORD status, PVOID, PDNS_SERVICE_INSTANCE instance)
    {
        printf("mDNS registration: %s\n", status == ERROR_SUCCESS ? "ok" : "failed");
        if (instance) DnsServiceFreeInstance(instance);
    }
}

int wmain(int argc, wchar_t** argv)
{
    for (int i = 1; i + 1 < argc; i += 2)
    {
        if (wcscmp(argv[i], L"--width") == 0) g_width = _wtoi(argv[i + 1]);
        else if (wcscmp(argv[i], L"--height") == 0) g_height = _wtoi(argv[i + 1]);
    }

    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD computerLength = ARRAYSIZE(computer);
    GetComputerNameW(computer, &computerLength);
    std::wstring host = std::wstring(computer) + L".local";
    std::wstring instanceName = L"FakePhone._iphonewebcam._tcp.local";
    PDNS_SERVICE_INSTANCE instance = DnsServiceConstructInstance(
        instanceName.c_str(), host.c_str(), nullptr, nullptr, Port, 0, 0, 0, nullptr, nullptr);
    DNS_SERVICE_REGISTER_REQUEST registerRequest{};
    registerRequest.Version = DNS_QUERY_REQUEST_VERSION1;
    registerRequest.pServiceInstance = instance;
    registerRequest.pRegisterCompletionCallback = OnRegistered;
    DnsServiceRegister(&registerRequest, nullptr);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(Port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(listener, 1) != 0)
    {
        printf("Cannot listen on port %u\n", Port);
        return 1;
    }
    printf("FakePhone streaming %ux%u@%u on 127.0.0.1:%u\n", g_width, g_height, Fps, Port);

    for (;;)
    {
        SOCKET client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) break;
        BOOL noDelay = TRUE;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
        printf("Receiver connected\n");
        Stream(client);
        closesocket(client);
    }
    return 0;
}
