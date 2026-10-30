#pragma once

// Shared-memory contract between the receiver app (writer, user session) and the
// virtual-camera media source (reader, loaded by the Frame Server service in session 0).

#include <windows.h>
#include <sddl.h>
#include <cstdint>

namespace vcam
{
    // CLSID of the media source COM object: {6C7E1E8B-3B0F-4C5A-9F1D-2A6B8E4D9C31}
    inline constexpr CLSID CLSID_IPhoneWebcamSource =
        { 0x6c7e1e8b, 0x3b0f, 0x4c5a, { 0x9f, 0x1d, 0x2a, 0x6b, 0x8e, 0x4d, 0x9c, 0x31 } };
    inline constexpr wchar_t CLSID_IPhoneWebcamSourceString[] = L"{6C7E1E8B-3B0F-4C5A-9F1D-2A6B8E4D9C31}";
    inline constexpr wchar_t FriendlyName[] = L"iPhone Webcam";

    // Frames are exchanged at the maximum resolution (4K); the media source downscales
    // when an app selects a smaller format.
    inline constexpr uint32_t FrameWidth = 3840;
    inline constexpr uint32_t FrameHeight = 2160;
    inline constexpr uint32_t FrameRate = 30;
    inline constexpr uint32_t FrameBytes = FrameWidth * FrameHeight * 3 / 2; // NV12
    inline constexpr uint32_t SlotCount = 2;

    // Formats offered to apps; each divides the shared frame size by an integer factor.
    struct VideoFormat { uint32_t width; uint32_t height; };
    inline constexpr VideoFormat OfferedFormats[] = { { 3840, 2160 }, { 1920, 1080 }, { 1280, 720 } };

    inline constexpr wchar_t SharedMemoryName[] = L"Global\\IPhoneWebcam_Frames_v3";
    inline constexpr uint32_t SharedMagic = 0x4D435069; // "iPCM"

    struct SharedHeader
    {
        uint32_t magic;
        uint32_t width;
        uint32_t height;
        uint32_t frameBytes;
        volatile LONG latestSlot;      // slot holding the most recent complete frame
        uint32_t reserved;
        volatile LONG64 frameCounter;  // incremented after each complete frame
        volatile LONG64 lastWriteTick; // GetTickCount64() of the last frame (system-wide clock)
        volatile LONG64 lastReadTick;  // GetTickCount64() of the last frame an app pulled from the camera
    };

    inline constexpr size_t SlotOffset(uint32_t slot) { return 4096 + size_t(slot) * FrameBytes; }
    inline constexpr size_t SharedMemorySize = 4096 + size_t(SlotCount) * FrameBytes;

    // Creates or opens the mapping. Creating a Global\ object needs SeCreateGlobalPrivilege
    // (services and elevated admins have it), so a normal user process can usually only
    // open it after the Frame Server has created it.
    inline HANDLE CreateOrOpenSharedMemory()
    {
        // SYSTEM, Administrators, LocalService, interactive and authenticated users: full access.
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;LS)(A;;GA;;;IU)(A;;GA;;;AU)",
                SDDL_REVISION_1, &sd, nullptr))
        {
            return nullptr;
        }
        SECURITY_ATTRIBUTES sa{ sizeof(sa), sd, FALSE };
        HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE,
                                      0, static_cast<DWORD>(SharedMemorySize), SharedMemoryName);
        LocalFree(sd);
        if (!h)
        {
            h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SharedMemoryName);
        }
        return h;
    }
}
