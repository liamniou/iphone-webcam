// Command-line front end for IPhoneWebcamEngine.dll (the desktop app uses the same API).

#include <windows.h>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include "../engine/IPhoneWebcamEngine.h"

namespace
{
    std::atomic<bool> g_stop{ false };

    BOOL WINAPI OnConsoleCtrl(DWORD)
    {
        g_stop = true;
        return TRUE;
    }

    void PrintUsage()
    {
        wprintf(L"Usage: IPhoneWebcamCli.exe [--ip <address>] [--port <port>] [--no-camera] [--software-decode]\n"
                L"                           [--mirror] [--rotate180] [--preview-test]\n"
                L"                           [--autoframe] [--framing <0-100>] [--redness <0-100>] [--smooth <0-100>]\n"
                L"  --ip               Connect to this iPhone address instead of the first discovered phone\n"
                L"  --port             TCP port of the iPhone app (default 5005)\n"
                L"  --no-camera        Don't register the virtual camera (receiver/decoder test)\n"
                L"  --software-decode  Decode on the CPU instead of the GPU\n"
                L"  --mirror           Mirror the image horizontally\n"
                L"  --rotate180        Rotate the image by 180 degrees\n"
                L"  --preview-test     Enable the preview pipeline and report preview frames per second\n"
                L"  --autoframe        Keep the face framed (auto zoom and pan)\n"
                L"  --framing          Auto-framing tightness: 0 = wide, 100 = tight (default 50)\n"
                L"  --redness          Reduce facial redness, 0-100 %%\n"
                L"  --smooth           Smooth skin, 0-100 %%\n"
                L"  --format <f>       Output format: 2160p30, 1080p60, 1080p30 (default), 720p60 or 720p30\n"
                L"  --frame-once       Frame the face once after connecting and keep that framing\n"
                L"  --snapshot <file>  Save the processed preview (960x540 BMP) after --seconds and exit\n"
                L"  --seconds <n>      Delay before --snapshot (default 5)\n");
    }

    bool ParseFormat(const std::wstring& text, int32_t& width, int32_t& height, int32_t& fps)
    {
        int lines = 0, rate = 0;
        if (swscanf_s(text.c_str(), L"%dp%d", &lines, &rate) != 2) return false;
        height = lines;
        width = lines * 16 / 9;
        fps = rate;
        return true;
    }

    bool SaveBmp(const std::wstring& path, const std::vector<uint8_t>& bgra, int32_t w, int32_t h)
    {
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file) return false;
        BITMAPINFOHEADER info{ sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
        BITMAPFILEHEADER header{};
        header.bfType = 0x4D42;
        header.bfOffBits = sizeof(header) + sizeof(info);
        header.bfSize = header.bfOffBits + static_cast<DWORD>(bgra.size());
        fwrite(&header, sizeof(header), 1, file);
        fwrite(&info, sizeof(info), 1, file);
        fwrite(bgra.data(), 1, bgra.size(), file);
        fclose(file);
        return true;
    }

    const wchar_t* StateName(int32_t state)
    {
        switch (state)
        {
        case IPWC_STATE_CONNECTING: return L"connecting";
        case IPWC_STATE_STREAMING: return L"streaming";
        case IPWC_STATE_RECONNECTING: return L"reconnecting";
        default: return L"idle";
        }
    }

    const wchar_t* CameraName(int32_t camera)
    {
        switch (camera)
        {
        case IPWC_CAMERA_READY: return L"ready";
        case IPWC_CAMERA_IN_USE: return L"in use";
        default: return L"unavailable";
        }
    }
}

int wmain(int argc, wchar_t** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    std::wstring ip;
    uint16_t port = 5005;
    bool noCamera = false, softwareDecode = false, mirror = false, rotate = false, previewTest = false;
    bool autoFrame = false;
    float framing = 0.5f, redness = 0, smooth = 0;
    std::wstring snapshot;
    int snapshotSeconds = 5;
    int32_t formatWidth = 1920, formatHeight = 1080, formatFps = 30;
    bool frameOnce = false;
    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i];
        if (arg == L"--ip" && i + 1 < argc) ip = argv[++i];
        else if (arg == L"--port" && i + 1 < argc) port = static_cast<uint16_t>(_wtoi(argv[++i]));
        else if (arg == L"--no-camera") noCamera = true;
        else if (arg == L"--software-decode") softwareDecode = true;
        else if (arg == L"--mirror") mirror = true;
        else if (arg == L"--rotate180") rotate = true;
        else if (arg == L"--preview-test") previewTest = true;
        else if (arg == L"--autoframe") autoFrame = true;
        else if (arg == L"--framing" && i + 1 < argc) framing = _wtoi(argv[++i]) / 100.0f;
        else if (arg == L"--redness" && i + 1 < argc) redness = _wtoi(argv[++i]) / 100.0f;
        else if (arg == L"--smooth" && i + 1 < argc) smooth = _wtoi(argv[++i]) / 100.0f;
        else if (arg == L"--format" && i + 1 < argc && ParseFormat(argv[i + 1], formatWidth, formatHeight, formatFps)) ++i;
        else if (arg == L"--frame-once") frameOnce = true;
        else if (arg == L"--snapshot" && i + 1 < argc) { snapshot = argv[++i]; previewTest = true; }
        else if (arg == L"--seconds" && i + 1 < argc) snapshotSeconds = _wtoi(argv[++i]);
        else
        {
            PrintUsage();
            return 1;
        }
    }

    SetConsoleCtrlHandler(OnConsoleCtrl, TRUE);
    HRESULT hr = IPWC_Initialize();
    if (FAILED(hr))
    {
        wprintf(L"Engine initialization failed (0x%08X)\n", hr);
        return 1;
    }
    IPWC_SetHardwareDecode(!softwareDecode);
    IPWC_SetTransform(mirror, rotate);
    IPWC_SetPreviewEnabled(previewTest);
    IPWC_SetAutoFraming(autoFrame, framing);
    IPWC_SetRetouch(redness, smooth);
    hr = IPWC_SetOutputFormat(formatWidth, formatHeight, formatFps);
    if (FAILED(hr))
    {
        wprintf(L"Unsupported format %dx%d@%d\n", formatWidth, formatHeight, formatFps);
        IPWC_Shutdown();
        return 1;
    }
    if (frameOnce) IPWC_FrameOnce();

    if (!noCamera)
    {
        hr = IPWC_StartVirtualCamera();
        if (FAILED(hr))
        {
            wprintf(L"Could not create the virtual camera (0x%08X). Run scripts\\install.ps1 as administrator.\n", hr);
            IPWC_Shutdown();
            return 2;
        }
        wprintf(L"Virtual camera \"iPhone Webcam\" is available.\n");
    }

    if (!ip.empty())
    {
        IPWC_Connect(L"", ip.c_str(), port);
    }
    else
    {
        wprintf(L"Searching for the iPhone app on the local network...\n");
        IPWC_DEVICE device{};
        while (!g_stop && IPWC_GetDevices(&device, 1) == 0) Sleep(250);
        if (!g_stop)
        {
            wprintf(L"Found \"%s\" at %s:%u\n", device.name, device.address, device.port);
            IPWC_Connect(device.name, device.address, device.port);
        }
    }

    std::vector<uint8_t> preview(static_cast<size_t>(IPWC_PREVIEW_WIDTH) * IPWC_PREVIEW_HEIGHT * 4);
    int64_t lastPreview = 0;
    int previewFrames = 0;
    std::wstring lastMessage;
    while (!g_stop)
    {
        for (int i = 0; i < 20 && !g_stop; ++i)
        {
            Sleep(50);
            if (previewTest)
            {
                int64_t id = IPWC_GetPreviewFrame(preview.data(), IPWC_PREVIEW_WIDTH * 4, lastPreview);
                if (id != 0)
                {
                    lastPreview = id;
                    ++previewFrames;
                }
            }
        }

        IPWC_STATUS status{};
        IPWC_GetStatus(&status);
        if (status.state == IPWC_STATE_STREAMING)
        {
            wprintf(L"%-10s %2d fps  %dx%d  %5.1f Mbit/s  %s  camera: %s",
                    StateName(status.state), status.fps, status.width, status.height,
                    status.bitrateKbps / 1000.0, status.hardwareDecode ? L"GPU" : L"CPU", CameraName(status.camera));
            if (previewTest) wprintf(L"  preview: %d fps", previewFrames);
            if (status.faces >= 0) wprintf(L"  faces: %d", status.faces);
            static const wchar_t* framingNames[] = { L"off", L"follow", L"hold", L"pending" };
            if (status.framing >= 0 && status.framing <= 3) wprintf(L"  framing: %s", framingNames[status.framing]);
            if (!status.effectsAvailable) wprintf(L"  (effects unavailable)");
            wprintf(L"\n");
        }
        else if (lastMessage != status.message)
        {
            wprintf(L"%-10s %s\n", StateName(status.state), status.message);
        }
        lastMessage = status.message;
        previewFrames = 0;

        if (!snapshot.empty() && status.state == IPWC_STATE_STREAMING && --snapshotSeconds <= 0 && lastPreview != 0)
        {
            bool saved = SaveBmp(snapshot, preview, IPWC_PREVIEW_WIDTH, IPWC_PREVIEW_HEIGHT);
            wprintf(L"%s %s\n", saved ? L"Saved snapshot" : L"Couldn't save snapshot", snapshot.c_str());
            break;
        }
    }

    wprintf(L"Shutting down...\n");
    IPWC_Shutdown();
    return 0;
}
