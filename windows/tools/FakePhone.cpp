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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    UINT32 g_width = 1920;
    UINT32 g_height = 1080;
    bool g_drawFace = true;  // draw the synthetic face and the reddish test patch
    bool g_sendFaces = true; // send face metadata (message type 2) like the iPhone app
    float g_faceSize = 0.30f; // face height as a fraction of the frame height
    bool g_stillFace = false; // keep the face at one (off-centre) position
    UINT32 g_fps = 30;
    bool g_followFormat = true; // obey format commands from the PC like the iPhone app
    constexpr uint16_t Port = 5005;

    bool SendAll(SOCKET s, const uint8_t* data, size_t length);

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
        MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, g_fps, 1);
        MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        output->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base);
        Check(encoder->SetOutputType(0, output.Get(), 0), "SetOutputType");

        ComPtr<IMFMediaType> input;
        Check(MFCreateMediaType(&input), "MFCreateMediaType");
        input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, g_width, g_height);
        MFSetAttributeRatio(input.Get(), MF_MT_FRAME_RATE, g_fps, 1);
        MFSetAttributeRatio(input.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        Check(encoder->SetInputType(0, input.Get(), 0), "SetInputType");

        ComPtr<ICodecAPI> codecApi;
        if (SUCCEEDED(encoder.As(&codecApi)))
        {
            VARIANT value;
            VariantInit(&value);
            value.vt = VT_UI4;
            value.ulVal = g_fps * 2;
            codecApi->SetValue(&CODECAPI_AVEncMPVGOPSize, &value);
            value.vt = VT_BOOL;
            value.boolVal = VARIANT_TRUE;
            codecApi->SetValue(&CODECAPI_AVLowLatencyMode, &value);
        }

        encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        return encoder;
    }

    struct FaceBox { float x, y, w, h; }; // normalized, top-left origin

    // Where the synthetic face is at a given frame (slow Lissajous drift around the frame).
    FaceBox FacePosition(uint32_t frame)
    {
        double t = g_stillFace ? 3.0 : frame / static_cast<double>(g_fps);
        float h = g_faceSize;
        float w = h * g_height / g_width * 0.78f; // a bit narrower than tall, in pixels
        float cx = 0.5f + 0.28f * static_cast<float>(std::sin(t * 0.35));
        float cy = 0.48f + 0.14f * static_cast<float>(std::sin(t * 0.55 + 1.0));
        return { cx - w / 2, cy - h / 2, w, h };
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

        if (!g_drawFace) return;

        // Reddish "skin" square in the bottom-left corner: must NOT change when a face is detected,
        // because retouching is limited to the face area.
        const uint8_t skinY = 150, skinCb = 108, redCr = 168;
        UINT32 patch = H / 6;
        for (UINT32 y = H - patch - H / 20; y < H - H / 20; ++y)
            memset(nv12.data() + y * W + W / 30, skinY, patch);
        for (UINT32 y = (H - patch - H / 20) / 2; y < (H - H / 20) / 2; ++y)
            for (UINT32 x = W / 60; x < (W / 30 + patch) / 2; ++x)
            {
                uv[y * W + x * 2] = skinCb;
                uv[y * W + x * 2 + 1] = redCr;
            }

        // The "face": a reddish skin ellipse with fine texture (for smoothing), eyes and a mouth.
        FaceBox f = FacePosition(frame);
        float cx = (f.x + f.w / 2) * W, cy = (f.y + f.h / 2) * H, rx = f.w / 2 * W, ry = f.h / 2 * H;
        for (int y = std::max(0, static_cast<int>(cy - ry)); y < std::min<int>(H, static_cast<int>(cy + ry)); ++y)
            for (int x = std::max(0, static_cast<int>(cx - rx)); x < std::min<int>(W, static_cast<int>(cx + rx)); ++x)
            {
                float dx = (x - cx) / rx, dy = (y - cy) / ry;
                if (dx * dx + dy * dy > 1) continue;
                uint32_t hash = (static_cast<uint32_t>(x) * 73856093u) ^ (static_cast<uint32_t>(y) * 19349663u);
                int noise = static_cast<int>((hash >> 7) % 25) - 12; // skin texture / blemishes
                bool eye = ((dx + 0.35f) * (dx + 0.35f) + (dy + 0.25f) * (dy + 0.25f) < 0.012f) ||
                           ((dx - 0.35f) * (dx - 0.35f) + (dy + 0.25f) * (dy + 0.25f) < 0.012f);
                bool mouth = std::abs(dy - 0.45f) < 0.04f && std::abs(dx) < 0.3f;
                nv12[y * W + x] = static_cast<uint8_t>(eye ? 30 : mouth ? 90 : skinY + noise);
            }
        for (int y = std::max(0, static_cast<int>(cy - ry)) / 2; y < std::min<int>(H, static_cast<int>(cy + ry)) / 2; ++y)
            for (int x = std::max(0, static_cast<int>(cx - rx)) / 2; x < std::min<int>(W, static_cast<int>(cx + rx)) / 2; ++x)
            {
                float dx = (x * 2 - cx) / rx, dy = (y * 2 - cy) / ry;
                if (dx * dx + dy * dy > 1) continue;
                uv[y * W + x * 2] = skinCb;
                uv[y * W + x * 2 + 1] = redCr;
            }
    }

    bool SendFaces(SOCKET s, uint32_t frame)
    {
        FaceBox f = FacePosition(frame);
        uint8_t payload[1 + 16];
        payload[0] = 1;
        memcpy(payload + 1, &f, sizeof(f)); // little-endian float32 x, y, w, h
        uint32_t messageLength = sizeof(payload) + 1;
        uint8_t header[5] = { 0, 0, 0, static_cast<uint8_t>(messageLength), 2 };
        return SendAll(s, header, sizeof(header)) && SendAll(s, payload, sizeof(payload));
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

            // Like the iPhone app: face positions of a frame are sent after that frame's video,
            // about 6 times per second. (The encoder delays frames, so use the output's timestamp.)
            LONGLONG time = 0;
            sample->GetSampleTime(&time);
            uint32_t frameIndex = static_cast<uint32_t>((time + 10'000'000 / g_fps / 2) / (10'000'000 / g_fps));
            if (g_drawFace && g_sendFaces && frameIndex % 5 == 0 && !SendFaces(s, frameIndex)) return false;
        }
    }

    void Stream(SOCKET client)
    {
        std::vector<uint8_t> commands;
        for (bool restart = true; restart;)
        {
            restart = false;
            auto encoder = CreateEncoder();
            MFT_OUTPUT_STREAM_INFO info{};
            encoder->GetOutputStreamInfo(0, &info);
            bool providesSamples = (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
            DWORD outputSize = info.cbSize ? info.cbSize : g_width * g_height;
            printf("Streaming %ux%u@%u\n", g_width, g_height, g_fps);

            std::vector<uint8_t> nv12(g_width * g_height * 3 / 2);
            LONGLONG time = 0;
            ULONGLONG start = GetTickCount64();
            for (uint32_t frame = 0;; ++frame)
            {
                // Read commands from the PC without blocking: 'K' = keyframe,
                // 'F' u16be width u16be height u8 fps = change format.
                u_long nonBlocking = 1;
                ioctlsocket(client, FIONBIO, &nonBlocking);
                char buffer[64];
                int received;
                while ((received = recv(client, buffer, sizeof(buffer), 0)) > 0) commands.insert(commands.end(), buffer, buffer + received);
                u_long blocking = 0;
                ioctlsocket(client, FIONBIO, &blocking);

                bool keyFrame = false;
                while (!commands.empty())
                {
                    if (commands[0] == 'K')
                    {
                        keyFrame = true;
                        commands.erase(commands.begin());
                    }
                    else if (commands[0] == 'F')
                    {
                        if (commands.size() < 6) break;
                        UINT32 width = (commands[1] << 8) | commands[2], height = (commands[3] << 8) | commands[4], fps = commands[5];
                        commands.erase(commands.begin(), commands.begin() + 6);
                        printf("Format requested: %ux%u@%u%s\n", width, height, fps, g_followFormat ? "" : " (ignored)");
                        if (g_followFormat && width >= 64 && height >= 64 && fps >= 1 && fps <= 60 &&
                            (width != g_width || height != g_height || fps != g_fps))
                        {
                            g_width = width & ~1u;
                            g_height = height & ~1u;
                            g_fps = fps;
                            restart = true;
                        }
                    }
                    else
                    {
                        commands.erase(commands.begin());
                    }
                }
                if (restart) break;

                if (keyFrame)
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

                FillPattern(nv12, frame);
                ComPtr<IMFMediaBuffer> mediaBuffer;
                MFCreateMemoryBuffer(static_cast<DWORD>(nv12.size()), &mediaBuffer);
                BYTE* data = nullptr;
                mediaBuffer->Lock(&data, nullptr, nullptr);
                memcpy(data, nv12.data(), nv12.size());
                mediaBuffer->Unlock();
                mediaBuffer->SetCurrentLength(static_cast<DWORD>(nv12.size()));
                ComPtr<IMFSample> sample;
                MFCreateSample(&sample);
                sample->AddBuffer(mediaBuffer.Get());
                sample->SetSampleTime(time);
                sample->SetSampleDuration(10'000'000 / g_fps);
                time += 10'000'000 / g_fps;

                if (FAILED(encoder->ProcessInput(0, sample.Get(), 0))) return;
                if (!DrainEncoder(encoder.Get(), client, outputSize, providesSamples))
                {
                    printf("Receiver disconnected\n");
                    return;
                }

                ULONGLONG due = start + (frame + 1) * 1000ULL / g_fps;
                ULONGLONG now = GetTickCount64();
                if (due > now) Sleep(static_cast<DWORD>(due - now));
                if (frame % (g_fps * 5) == 0) printf("Sent %u frames\n", frame);
            }
        }
    }

    VOID WINAPI OnRegistered(DWORD status, PVOID, PDNS_SERVICE_INSTANCE instance)
    {
        printf("mDNS registration: %s\n", status == ERROR_SUCCESS ? "ok" : "failed");
        if (instance) DnsServiceFreeInstance(instance);
    }
}

int wmain(int argc, wchar_t** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (wcscmp(argv[i], L"--width") == 0 && i + 1 < argc) g_width = _wtoi(argv[++i]);
        else if (wcscmp(argv[i], L"--height") == 0 && i + 1 < argc) g_height = _wtoi(argv[++i]);
        else if (wcscmp(argv[i], L"--no-faces") == 0) g_sendFaces = false;
        else if (wcscmp(argv[i], L"--no-face") == 0) g_drawFace = false;
        else if (wcscmp(argv[i], L"--fixed-format") == 0) g_followFormat = false;
        else if (wcscmp(argv[i], L"--fps") == 0 && i + 1 < argc) g_fps = _wtoi(argv[++i]);
        else if (wcscmp(argv[i], L"--face-size") == 0 && i + 1 < argc) g_faceSize = static_cast<float>(_wtof(argv[++i]));
        else if (wcscmp(argv[i], L"--still-face") == 0) g_stillFace = true;
        else
        {
            printf("Usage: FakePhone.exe [--width N] [--height N] [--fps N] [--fixed-format (ignore format requests, like old iPhone apps)]\n"
                   "                     [--face-size 0.05-0.6] [--still-face] [--no-faces (don't send face metadata)] [--no-face (plain pattern)]\n");
            return 1;
        }
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
    printf("FakePhone streaming %ux%u@%u on 127.0.0.1:%u\n", g_width, g_height, g_fps, Port);

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
