// IPhoneWebcamEngine: connection management, decoding, virtual camera and preview behind a C API.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "IPhoneWebcamEngine.h"
#include "DeviceBrowser.h"
#include "Effects.h"
#include "FrameWriter.h"
#include "GpuDevice.h"
#include "H264Decoder.h"
#include "VideoEffects.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr uint8_t MessageVideo = 1;
    constexpr uint8_t MessageFaces = 2;
    constexpr uint8_t RequestKeyFrame = 'K';
    constexpr uint32_t MaxMessageSize = 16 * 1024 * 1024;
    constexpr auto ReconnectDelay = std::chrono::seconds(2);
    constexpr ULONGLONG FaceDataMaxAgeMs = 1000;
    constexpr uint8_t MaxFacesPerMessage = 8;

    template <size_t N>
    void CopyString(wchar_t (&dst)[N], const std::wstring& src)
    {
        wcsncpy_s(dst, src.c_str(), _TRUNCATE);
    }

    // Runs a function on a temporary MTA thread (the virtual camera object must live in the MTA,
    // while the WPF caller is an STA thread).
    template <typename F>
    void RunOnMta(F&& function)
    {
        std::thread([&] {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            function();
            CoUninitialize();
        }).join();
    }

    bool ReadExact(SOCKET s, uint8_t* buffer, size_t length)
    {
        while (length > 0)
        {
            int received = recv(s, reinterpret_cast<char*>(buffer), static_cast<int>(std::min<size_t>(length, 1 << 20)), 0);
            if (received <= 0) return false;
            buffer += received;
            length -= received;
        }
        return true;
    }

    SOCKET ConnectSocket(const std::wstring& address, uint16_t port)
    {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (InetPtonW(AF_INET, address.c_str(), &addr.sin_addr) != 1) return INVALID_SOCKET;

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return s;

        u_long nonBlocking = 1;
        ioctlsocket(s, FIONBIO, &nonBlocking);
        connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        fd_set writeSet, errorSet;
        FD_ZERO(&writeSet); FD_SET(s, &writeSet);
        FD_ZERO(&errorSet); FD_SET(s, &errorSet);
        timeval timeout{ 3, 0 };
        if (select(0, nullptr, &writeSet, &errorSet, &timeout) <= 0 || FD_ISSET(s, &errorSet))
        {
            closesocket(s);
            return INVALID_SOCKET;
        }
        nonBlocking = 0;
        ioctlsocket(s, FIONBIO, &nonBlocking);

        BOOL noDelay = TRUE;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
        DWORD recvTimeout = 5000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeout), sizeof(recvTimeout));
        int recvBuffer = 8 * 1024 * 1024;
        setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuffer), sizeof(recvBuffer));
        return s;
    }
}

class Engine
{
public:
    HRESULT Initialize();
    HRESULT StartVirtualCamera();
    void Shutdown();

    void Connect(const std::wstring& name, const std::wstring& address, uint16_t port);
    void Disconnect();
    void SetAutoReconnect(bool enabled) { m_autoReconnect = enabled; }
    void SetHardwareDecode(bool enabled) { m_hardwareDecode = enabled; }
    void SetTransform(bool mirror, bool rotate180)
    {
        m_mirror = mirror;
        m_rotate180 = rotate180;
        m_writer.SetTransform(mirror, rotate180); // used by the CPU fallback path
    }
    void SetPreviewEnabled(bool enabled) { m_writer.SetPreviewEnabled(enabled); }
    void SetAutoFraming(bool enabled, float tightness)
    {
        m_autoFraming = enabled;
        m_tightness = std::clamp(tightness, 0.0f, 1.0f);
    }
    void SetRetouch(float redness, float smoothing)
    {
        m_redness = std::clamp(redness, 0.0f, 1.0f);
        m_smoothing = std::clamp(smoothing, 0.0f, 1.0f);
    }

    int32_t GetDevices(IPWC_DEVICE* devices, int32_t capacity);
    void RefreshDevices() { m_browser.Refresh(); }
    void GetStatus(IPWC_STATUS* status);
    int64_t GetPreview(uint8_t* bgra, int32_t stride, int64_t last) { return m_writer.CopyPreview(bgra, stride, last); }

private:
    void WorkerLoop();
    std::wstring Stream(SOCKET s);
    void ResetStatsLocked();

    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_initialized = false;
    bool m_stopping = false;
    std::thread m_worker;

    bool m_hasTarget = false;
    std::wstring m_targetName;
    std::wstring m_targetAddress;
    uint16_t m_targetPort = 0;
    uint64_t m_generation = 0;
    SOCKET m_socket = INVALID_SOCKET;

    std::atomic<bool> m_autoReconnect{ true };
    std::atomic<bool> m_hardwareDecode{ true };
    std::atomic<bool> m_mirror{ false };
    std::atomic<bool> m_rotate180{ false };
    std::atomic<bool> m_autoFraming{ false };
    std::atomic<float> m_tightness{ 0.5f };
    std::atomic<float> m_redness{ 0.0f };
    std::atomic<float> m_smoothing{ 0.0f };

    IPWC_STATE m_state = IPWC_STATE_IDLE;
    std::wstring m_message = L"Not connected";
    std::wstring m_decoder;
    bool m_hardwareActive = false;
    int32_t m_width = 0, m_height = 0, m_fps = 0, m_kbps = 0;
    int32_t m_faces = -1;

    ComPtr<IMFVirtualCamera> m_camera;
    DeviceBrowser m_browser;
    FrameWriter m_writer;
    std::unique_ptr<GpuDevice> m_gpu; // null when no usable GPU
    VideoEffects m_effects;
    FaceTracker m_tracker;
};

HRESULT Engine::Initialize()
{
    std::lock_guard lock(m_lock);
    if (m_initialized) return S_OK;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return HRESULT_FROM_WIN32(WSAGetLastError());
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return hr;

    // GPU decoding and effects are optional: without them the engine decodes on the CPU and
    // skips auto-framing/retouching.
    auto gpu = std::make_unique<GpuDevice>();
    if (SUCCEEDED(gpu->Create()))
    {
        m_gpu = std::move(gpu);
        if (FAILED(m_effects.Initialize(m_gpu.get(), vcam::FrameWidth, vcam::FrameHeight)))
        {
            OutputDebugStringW(L"IPhoneWebcamEngine: effects pipeline unavailable\n");
        }
    }

    m_browser.Start();
    m_stopping = false;
    m_worker = std::thread([this] { WorkerLoop(); });
    m_initialized = true;
    return S_OK;
}

HRESULT Engine::StartVirtualCamera()
{
    std::lock_guard lock(m_lock);
    if (!m_initialized) return E_UNEXPECTED;
    if (m_camera) return S_OK;

    HRESULT hr = E_FAIL;
    RunOnMta([&] {
        ComPtr<IMFVirtualCamera> camera;
        hr = MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource,
                                   MFVirtualCameraLifetime_Session,
                                   MFVirtualCameraAccess_CurrentUser,
                                   vcam::FriendlyName,
                                   vcam::CLSID_IPhoneWebcamSourceString,
                                   nullptr, 0, &camera);
        if (SUCCEEDED(hr)) hr = camera->Start(nullptr);
        if (SUCCEEDED(hr)) m_camera = camera;
    });
    m_writer.SetPublishEnabled(SUCCEEDED(hr));
    return hr;
}

void Engine::Shutdown()
{
    {
        std::lock_guard lock(m_lock);
        if (!m_initialized) return;
        m_stopping = true;
        m_hasTarget = false;
        ++m_generation;
        if (m_socket != INVALID_SOCKET)
        {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
    m_browser.Stop();

    if (m_camera)
    {
        RunOnMta([&] {
            m_camera->Remove();
            m_camera->Shutdown();
            m_camera.Reset();
        });
    }

    std::lock_guard lock(m_lock);
    MFShutdown();
    WSACleanup();
    m_initialized = false;
}

void Engine::Connect(const std::wstring& name, const std::wstring& address, uint16_t port)
{
    {
        std::lock_guard lock(m_lock);
        m_hasTarget = true;
        m_targetName = name;
        m_targetAddress = address;
        m_targetPort = port;
        ++m_generation;
        if (m_socket != INVALID_SOCKET)
        {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }
    m_cv.notify_all();
}

void Engine::Disconnect()
{
    {
        std::lock_guard lock(m_lock);
        m_hasTarget = false;
        ++m_generation;
        if (m_socket != INVALID_SOCKET)
        {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
        m_state = IPWC_STATE_IDLE;
        m_message = L"Disconnected";
    }
    m_cv.notify_all();
}

int32_t Engine::GetDevices(IPWC_DEVICE* devices, int32_t capacity)
{
    auto list = m_browser.Snapshot();
    for (int32_t i = 0; i < capacity && i < static_cast<int32_t>(list.size()); ++i)
    {
        IPWC_DEVICE device{};
        CopyString(device.name, list[i].name);
        CopyString(device.address, list[i].address);
        device.port = list[i].port;
        devices[i] = device;
    }
    return static_cast<int32_t>(list.size());
}

void Engine::GetStatus(IPWC_STATUS* status)
{
    IPWC_STATUS result{};
    {
        std::lock_guard lock(m_lock);
        result.state = m_state;
        result.width = m_width;
        result.height = m_height;
        result.fps = m_fps;
        result.bitrateKbps = m_kbps;
        result.hardwareDecode = m_hardwareActive ? 1 : 0;
        CopyString(result.deviceName, m_hasTarget ? m_targetName : std::wstring());
        CopyString(result.address, m_hasTarget ? m_targetAddress : std::wstring());
        CopyString(result.decoder, m_decoder);
        CopyString(result.message, m_message);
        result.camera = !m_camera ? IPWC_CAMERA_UNAVAILABLE : IPWC_CAMERA_READY;
        result.faces = m_faces;
        result.effectsAvailable = m_effects.IsReady() ? 1 : 0;
    }
    if (result.camera == IPWC_CAMERA_READY && m_writer.IsCameraInUse()) result.camera = IPWC_CAMERA_IN_USE;
    *status = result;
}

void Engine::ResetStatsLocked()
{
    m_width = m_height = m_fps = m_kbps = 0;
    m_faces = -1;
}

void Engine::WorkerLoop()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::unique_lock lock(m_lock);
    while (!m_stopping)
    {
        if (!m_hasTarget)
        {
            m_state = IPWC_STATE_IDLE;
            m_cv.wait(lock, [this] { return m_stopping || m_hasTarget; });
            continue;
        }

        const uint64_t generation = m_generation;
        if (!m_targetName.empty())
        {
            // Follow the phone if its IP address changed (e.g. new DHCP lease).
            lock.unlock();
            auto device = m_browser.FindByName(m_targetName);
            lock.lock();
            if (generation != m_generation) continue;
            if (device)
            {
                m_targetAddress = device->address;
                m_targetPort = device->port;
            }
        }
        const std::wstring address = m_targetAddress;
        const uint16_t port = m_targetPort;
        m_state = IPWC_STATE_CONNECTING;
        m_message = L"Connecting to " + address + L"…";

        lock.unlock();
        SOCKET s = ConnectSocket(address, port);
        lock.lock();

        if (generation != m_generation || m_stopping)
        {
            if (s != INVALID_SOCKET) closesocket(s);
            continue;
        }

        std::wstring reason;
        if (s == INVALID_SOCKET)
        {
            reason = L"Can't reach " + address + L":" + std::to_wstring(port) + L". Is the app open on the iPhone?";
        }
        else
        {
            m_socket = s;
            m_state = IPWC_STATE_STREAMING;
            m_message = L"Connected";
            lock.unlock();
            reason = Stream(s);
            lock.lock();
            if (m_socket == s)
            {
                closesocket(s);
                m_socket = INVALID_SOCKET;
            }
            ResetStatsLocked();
            m_writer.ClearPreview();
            if (generation != m_generation) continue; // user disconnected or picked another phone
        }

        if (!m_autoReconnect)
        {
            m_hasTarget = false;
            m_state = IPWC_STATE_IDLE;
            m_message = reason;
            continue;
        }
        m_state = IPWC_STATE_RECONNECTING;
        m_message = reason + L" Retrying…";
        m_cv.wait_for(lock, ReconnectDelay, [&] { return m_stopping || generation != m_generation; });
    }
    lock.unlock();
    CoUninitialize();
}

std::wstring Engine::Stream(SOCKET s)
{
    H264Decoder decoder;
    HRESULT hr = decoder.Initialize(m_gpu.get(), m_hardwareDecode);
    if (FAILED(hr)) return L"Couldn't create the H.264 decoder.";
    {
        std::lock_guard lock(m_lock);
        m_hardwareActive = decoder.IsHardware();
        m_decoder = decoder.IsHardware() ? m_gpu->AdapterName() : L"CPU (software)";
    }

    std::vector<uint8_t> message;
    uint32_t frames = 0;
    uint64_t bytes = 0;
    UINT32 width = 0, height = 0;
    ULONGLONG lastReport = GetTickCount64();
    ULONGLONG lastKeyFrameRequest = 0;
    LONGLONG timestamp = 0;

    // Face detections sent by the phone (normalized frame coordinates), plus their estimated
    // velocity so masks and framing can be extrapolated between detections (~6 per second).
    std::vector<NormRect> faces;
    std::vector<NormRect> previousFaces;
    std::vector<std::pair<float, float>> faceVelocity;
    bool phoneSendsFaces = false;
    ULONGLONG lastFacesTick = 0;
    std::vector<NormRect> predicted;
    m_tracker.Reset();
    auto lastFrameTime = std::chrono::steady_clock::now();

    auto writeFallback = [&](const DecodedFrame& frame) {
        if (frame.texture) decoder.ReadBack(frame, [&](const DecodedFrame& cpu) { m_writer.Write(cpu); });
        else m_writer.Write(frame);
    };

    auto onFrame = [&](const DecodedFrame& frame) {
        ++frames;
        width = frame.width;
        height = frame.height;

        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - lastFrameTime).count();
        lastFrameTime = now;

        if (!m_effects.IsReady())
        {
            writeFallback(frame);
            return;
        }

        ULONGLONG age = GetTickCount64() - lastFacesTick;
        bool fresh = phoneSendsFaces && age < FaceDataMaxAgeMs;
        predicted.clear();
        if (fresh)
        {
            float seconds = std::min(age, 300ULL) / 1000.0f;
            for (size_t i = 0; i < faces.size(); ++i)
            {
                NormRect f = faces[i];
                f.x += faceVelocity[i].first * seconds;
                f.y += faceVelocity[i].second * seconds;
                predicted.push_back(f);
            }
        }

        FrameParams params;
        params.crop = m_tracker.Update(predicted, fresh, m_autoFraming, m_tightness, dt);
        params.effects.mirror = m_mirror;
        params.effects.rotate180 = m_rotate180;
        params.effects.redness = m_redness;
        params.effects.smoothing = m_smoothing;
        params.faces = predicted;
        params.hasFaceInfo = phoneSendsFaces;

        HRESULT result = m_effects.Process(frame, params, [&](const uint8_t* y, UINT yPitch, const uint8_t* uv, UINT uvPitch) {
            m_writer.WriteOutput(y, yPitch, uv, uvPitch);
        });
        if (FAILED(result)) writeFallback(frame);
    };

    for (;;)
    {
        uint8_t header[5];
        if (!ReadExact(s, header, sizeof(header))) return L"Connection to the iPhone was lost.";
        uint32_t length = (uint32_t(header[0]) << 24) | (uint32_t(header[1]) << 16) | (uint32_t(header[2]) << 8) | header[3];
        if (length < 1 || length > MaxMessageSize) return L"Received invalid data from the iPhone.";
        message.resize(length - 1);
        if (!ReadExact(s, message.data(), message.size())) return L"Connection to the iPhone was lost.";
        bytes += length + 4;

        if (header[4] == MessageVideo && !message.empty())
        {
            hr = decoder.Decode(message.data(), message.size(), timestamp, onFrame);
            timestamp += 333'333;
            if (FAILED(hr) && GetTickCount64() - lastKeyFrameRequest > 1000)
            {
                lastKeyFrameRequest = GetTickCount64();
                send(s, reinterpret_cast<const char*>(&RequestKeyFrame), 1, 0);
            }
        }
        else if (header[4] == MessageFaces && !message.empty())
        {
            // u8 count, then count x (x, y, w, h) as little-endian float32, normalized, top-left origin.
            size_t count = std::min<size_t>(message[0], MaxFacesPerMessage);
            if (message.size() >= 1 + count * 16)
            {
                faces.clear();
                for (size_t i = 0; i < count; ++i)
                {
                    float v[4];
                    memcpy(v, message.data() + 1 + i * 16, sizeof(v));
                    // Vision boxes can extend past the frame edge: clamp the edges, not origin and size,
                    // so faces at the border don't get shifted.
                    float x0 = std::clamp(v[0], 0.0f, 1.0f), y0 = std::clamp(v[1], 0.0f, 1.0f);
                    float x1 = std::clamp(v[0] + v[2], 0.0f, 1.0f), y1 = std::clamp(v[1] + v[3], 0.0f, 1.0f);
                    if (!std::isfinite(v[0] + v[1] + v[2] + v[3])) continue;
                    NormRect r{ x0, y0, x1 - x0, y1 - y0 };
                    if (r.w > 0.005f && r.h > 0.005f) faces.push_back(r);
                }
                // Largest faces first, so the effects pipeline keeps the most important ones.
                std::sort(faces.begin(), faces.end(), [](const NormRect& a, const NormRect& b) { return a.w * a.h > b.w * b.h; });

                // Velocity per face from the nearest face in the previous detection.
                ULONGLONG tick = GetTickCount64();
                float interval = (tick - lastFacesTick) / 1000.0f;
                faceVelocity.assign(faces.size(), { 0.0f, 0.0f });
                if (phoneSendsFaces && interval > 0.02f && interval < 0.6f)
                {
                    for (size_t i = 0; i < faces.size(); ++i)
                    {
                        float best = faces[i].w; // only match faces that moved less than their width
                        for (const NormRect& p : previousFaces)
                        {
                            float dx = faces[i].cx() - p.cx(), dy = faces[i].cy() - p.cy();
                            float distance = std::sqrt(dx * dx + dy * dy);
                            if (distance < best)
                            {
                                best = distance;
                                faceVelocity[i] = { (faces[i].x - p.x) / interval, (faces[i].y - p.y) / interval };
                            }
                        }
                    }
                }
                previousFaces = faces;
                phoneSendsFaces = true;
                lastFacesTick = tick;
            }
        }

        ULONGLONG now = GetTickCount64();
        if (now - lastReport >= 1000)
        {
            std::lock_guard lock(m_lock);
            double seconds = (now - lastReport) / 1000.0;
            m_fps = static_cast<int32_t>(frames / seconds + 0.5);
            m_kbps = static_cast<int32_t>(bytes * 8 / 1000 / seconds);
            m_width = static_cast<int32_t>(width);
            m_height = static_cast<int32_t>(height);
            m_faces = !phoneSendsFaces ? -1 : (now - lastFacesTick < FaceDataMaxAgeMs ? static_cast<int32_t>(faces.size()) : 0);
            m_message = L"Streaming";
            frames = 0;
            bytes = 0;
            lastReport = now;
        }
    }
}

// ---------------------------------------------------------------------------
// C API
// ---------------------------------------------------------------------------

namespace
{
    // Intentionally never destroyed: DNS-SD callbacks may still arrive while the process exits.
    Engine& TheEngine()
    {
        static Engine* engine = new Engine();
        return *engine;
    }
}

IPWC_API HRESULT __stdcall IPWC_Initialize() { return TheEngine().Initialize(); }
IPWC_API HRESULT __stdcall IPWC_StartVirtualCamera() { return TheEngine().StartVirtualCamera(); }
IPWC_API void __stdcall IPWC_Shutdown() { TheEngine().Shutdown(); }

IPWC_API void __stdcall IPWC_Connect(const wchar_t* name, const wchar_t* address, uint16_t port)
{
    TheEngine().Connect(name ? name : L"", address ? address : L"", port);
}

IPWC_API void __stdcall IPWC_Disconnect() { TheEngine().Disconnect(); }
IPWC_API void __stdcall IPWC_SetAutoReconnect(BOOL enabled) { TheEngine().SetAutoReconnect(enabled != FALSE); }
IPWC_API void __stdcall IPWC_SetHardwareDecode(BOOL enabled) { TheEngine().SetHardwareDecode(enabled != FALSE); }
IPWC_API void __stdcall IPWC_SetTransform(BOOL mirror, BOOL rotate180) { TheEngine().SetTransform(mirror != FALSE, rotate180 != FALSE); }
IPWC_API void __stdcall IPWC_SetPreviewEnabled(BOOL enabled) { TheEngine().SetPreviewEnabled(enabled != FALSE); }
IPWC_API void __stdcall IPWC_SetAutoFraming(BOOL enabled, float tightness) { TheEngine().SetAutoFraming(enabled != FALSE, tightness); }
IPWC_API void __stdcall IPWC_SetRetouch(float redness, float smoothing) { TheEngine().SetRetouch(redness, smoothing); }

IPWC_API int32_t __stdcall IPWC_GetDevices(IPWC_DEVICE* devices, int32_t capacity)
{
    return TheEngine().GetDevices(devices, devices ? capacity : 0);
}

IPWC_API void __stdcall IPWC_RefreshDevices() { TheEngine().RefreshDevices(); }

IPWC_API void __stdcall IPWC_GetStatus(IPWC_STATUS* status)
{
    if (status) TheEngine().GetStatus(status);
}

IPWC_API int64_t __stdcall IPWC_GetPreviewFrame(uint8_t* bgra, int32_t stride, int64_t lastFrameId)
{
    return TheEngine().GetPreview(bgra, stride, lastFrameId);
}
