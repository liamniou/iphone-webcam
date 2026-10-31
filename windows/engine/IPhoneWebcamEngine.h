#pragma once

// C API of IPhoneWebcamEngine.dll, used by the WPF desktop app (P/Invoke) and the CLI.
// All functions are thread-safe. Strings are UTF-16.

#include <windows.h>
#include <stdint.h>

#ifdef IPWC_EXPORTS
#define IPWC_API extern "C" __declspec(dllexport)
#else
#define IPWC_API extern "C" __declspec(dllimport)
#endif

enum IPWC_STATE : int32_t
{
    IPWC_STATE_IDLE = 0,         // no phone selected
    IPWC_STATE_CONNECTING = 1,
    IPWC_STATE_STREAMING = 2,
    IPWC_STATE_RECONNECTING = 3, // connection lost or failed; retrying
};

enum IPWC_CAMERA : int32_t
{
    IPWC_CAMERA_UNAVAILABLE = 0, // not registered (source DLL not installed or not Windows 11)
    IPWC_CAMERA_READY = 1,       // registered, no app is using it
    IPWC_CAMERA_IN_USE = 2,      // an app is reading frames
};

#pragma pack(push, 8)
struct IPWC_STATUS
{
    int32_t state;          // IPWC_STATE
    int32_t camera;         // IPWC_CAMERA
    int32_t width;          // decoded stream size
    int32_t height;
    int32_t fps;            // decoded frames in the last second
    int32_t bitrateKbps;    // received bitrate in the last second
    int32_t hardwareDecode; // 1 when decoding on the GPU
    int32_t reserved;
    wchar_t deviceName[128];
    wchar_t address[64];
    wchar_t decoder[128];   // GPU adapter name or "CPU (software)"
    wchar_t message[256];   // human-readable status / last error
    int32_t faces;            // faces currently detected by the iPhone; -1 if the phone doesn't send face data
    int32_t effectsAvailable; // 1 when auto-framing and retouching are available (GPU effects pipeline ready)
};

struct IPWC_DEVICE
{
    wchar_t name[128];      // Bonjour instance name, e.g. "Lev's iPhone"
    wchar_t address[64];    // IPv4
    uint16_t port;
};
#pragma pack(pop)

constexpr int32_t IPWC_PREVIEW_WIDTH = 960;
constexpr int32_t IPWC_PREVIEW_HEIGHT = 540;

// Starts Media Foundation, networking and phone discovery. Call once before anything else.
IPWC_API HRESULT __stdcall IPWC_Initialize();
// Registers the "iPhone Webcam" virtual camera for this session (removed again on shutdown).
IPWC_API HRESULT __stdcall IPWC_StartVirtualCamera();
IPWC_API void __stdcall IPWC_Shutdown();

// Connects to a phone. name may be empty; when set, the engine follows the phone if its IP changes.
IPWC_API void __stdcall IPWC_Connect(const wchar_t* name, const wchar_t* address, uint16_t port);
IPWC_API void __stdcall IPWC_Disconnect();

IPWC_API void __stdcall IPWC_SetAutoReconnect(BOOL enabled);
IPWC_API void __stdcall IPWC_SetHardwareDecode(BOOL enabled); // applies on the next connection
IPWC_API void __stdcall IPWC_SetTransform(BOOL mirror, BOOL rotate180);
IPWC_API void __stdcall IPWC_SetPreviewEnabled(BOOL enabled);
// Auto-framing: smoothly crops/zooms to keep the detected face(s) framed.
// tightness 0..1: 0 = wide (head and shoulders), 1 = tight (head fills the frame).
IPWC_API void __stdcall IPWC_SetAutoFraming(BOOL enabled, float tightness);
// Face retouching, each 0..1 (0 = off): reduce redness of the skin, smooth the skin.
IPWC_API void __stdcall IPWC_SetRetouch(float redness, float smoothing);

// Copies up to capacity discovered phones; returns the total number found.
IPWC_API int32_t __stdcall IPWC_GetDevices(IPWC_DEVICE* devices, int32_t capacity);
// Clears the device list and restarts discovery.
IPWC_API void __stdcall IPWC_RefreshDevices();

IPWC_API void __stdcall IPWC_GetStatus(IPWC_STATUS* status);

// Copies the latest preview (IPWC_PREVIEW_WIDTH x IPWC_PREVIEW_HEIGHT, BGRA32) if it is newer
// than lastFrameId. Returns the id of the copied frame, or 0 if nothing new was copied.
IPWC_API int64_t __stdcall IPWC_GetPreviewFrame(uint8_t* bgra, int32_t stride, int64_t lastFrameId);
