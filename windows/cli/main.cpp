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
                L"  --ip               Connect to this iPhone address instead of the first discovered phone\n"
                L"  --port             TCP port of the iPhone app (default 5005)\n"
                L"  --no-camera        Don't register the virtual camera (receiver/decoder test)\n"
                L"  --software-decode  Decode on the CPU instead of the GPU\n"
                L"  --mirror           Mirror the image horizontally\n"
                L"  --rotate180        Rotate the image by 180 degrees\n"
                L"  --preview-test     Enable the preview pipeline and report preview frames per second\n");
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
            wprintf(L"\n");
        }
        else if (lastMessage != status.message)
        {
            wprintf(L"%-10s %s\n", StateName(status.state), status.message);
        }
        lastMessage = status.message;
        previewFrames = 0;
    }

    wprintf(L"Shutting down...\n");
    IPWC_Shutdown();
    return 0;
}
