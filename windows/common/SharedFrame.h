#pragma once

// Shared-memory contract between the receiver app (writer, user session) and the
// virtual-camera media source (reader, loaded by the Frame Server service in session 0).

#include <windows.h>
#include <sddl.h>
#include <cstdint>
#include <cstdio>

namespace vcam
{
    // CLSID of the media source COM object: {6C7E1E8B-3B0F-4C5A-9F1D-2A6B8E4D9C31}
    inline constexpr CLSID CLSID_IPhoneWebcamSource =
        { 0x6c7e1e8b, 0x3b0f, 0x4c5a, { 0x9f, 0x1d, 0x2a, 0x6b, 0x8e, 0x4d, 0x9c, 0x31 } };
    inline constexpr wchar_t CLSID_IPhoneWebcamSourceString[] = L"{6C7E1E8B-3B0F-4C5A-9F1D-2A6B8E4D9C31}";
    inline constexpr wchar_t FriendlyName[] = L"iPhone Webcam";

    // Shared frame slots are sized for the largest format (4K); the current frame size is in the header.
    inline constexpr uint32_t FrameWidth = 3840;
    inline constexpr uint32_t FrameHeight = 2160;
    inline constexpr uint32_t FrameBytes = FrameWidth * FrameHeight * 3 / 2; // NV12
    inline constexpr uint32_t SlotCount = 2;

    // Formats the user can choose. The virtual camera offers exactly the chosen one.
    struct CameraFormat
    {
        uint32_t width;
        uint32_t height;
        uint32_t fps;
        bool operator==(const CameraFormat&) const = default;
    };
    inline constexpr CameraFormat SupportedFormats[] = {
        { 3840, 2160, 30 }, { 1920, 1080, 60 }, { 1920, 1080, 30 }, { 1280, 720, 60 }, { 1280, 720, 30 },
    };
    inline constexpr CameraFormat DefaultFormat = { 1920, 1080, 30 };

    inline bool IsSupportedFormat(const CameraFormat& format)
    {
        for (const auto& f : SupportedFormats)
            if (f == format) return true;
        return false;
    }

    // The chosen format is stored in %ProgramData%\IPhoneWebcam\camera-format.txt ("1920 1080 60"),
    // written by the app that registered the camera and read by the media source (Frame Server,
    // LOCAL SERVICE) whenever an app opens the camera.
    inline bool CameraFormatPath(wchar_t (&path)[MAX_PATH], bool createFolder)
    {
        wchar_t folder[MAX_PATH];
        DWORD length = ExpandEnvironmentStringsW(L"%ProgramData%\\IPhoneWebcam", folder, MAX_PATH);
        if (length == 0 || length > MAX_PATH || folder[0] == L'%') return false;
        if (createFolder)
        {
            // Explicit access: any signed-in user may change the format, the Frame Server (LOCAL SERVICE) reads it.
            PSECURITY_DESCRIPTOR sd = nullptr;
            if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    L"D:(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)(A;OICI;GA;;;AU)(A;OICI;GR;;;LS)", SDDL_REVISION_1, &sd, nullptr))
            {
                SECURITY_ATTRIBUTES sa{ sizeof(sa), sd, FALSE };
                CreateDirectoryW(folder, &sa);
                LocalFree(sd);
            }
        }
        return swprintf_s(path, L"%s\\camera-format.txt", folder) > 0;
    }

    inline CameraFormat ReadCameraFormat()
    {
        wchar_t path[MAX_PATH];
        if (!CameraFormatPath(path, false)) return DefaultFormat;
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) return DefaultFormat;
        char text[64] = {};
        DWORD read = 0;
        ReadFile(file, text, sizeof(text) - 1, &read, nullptr);
        CloseHandle(file);
        CameraFormat format{};
        if (sscanf_s(text, "%u %u %u", &format.width, &format.height, &format.fps) != 3 || !IsSupportedFormat(format))
        {
            return DefaultFormat;
        }
        return format;
    }

    inline bool WriteCameraFormat(const CameraFormat& format)
    {
        wchar_t path[MAX_PATH];
        if (!CameraFormatPath(path, true)) return false;
        char text[64];
        int length = sprintf_s(text, "%u %u %u\r\n", format.width, format.height, format.fps);
        HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        BOOL ok = WriteFile(file, text, static_cast<DWORD>(length), &written, nullptr);
        CloseHandle(file);
        return ok && written == static_cast<DWORD>(length);
    }

    inline constexpr wchar_t SharedMemoryName[] = L"Global\\IPhoneWebcam_Frames_v5";
    inline constexpr uint32_t SharedMagic = 0x4D435069; // "iPCM"

    inline constexpr LONG64 PackSize(uint32_t width, uint32_t height) { return (LONG64(width) << 32) | height; }
    inline constexpr uint32_t PackedWidth(LONG64 size) { return uint32_t(uint64_t(size) >> 32); }
    inline constexpr uint32_t PackedHeight(LONG64 size) { return uint32_t(uint64_t(size)); }

    struct SharedHeader
    {
        uint32_t magic;
        uint32_t reserved0;
        // Size of the frame in each slot (PackSize, <= FrameWidth x FrameHeight). Stored per slot and
        // written before the slot is published so a reader never pairs a size with another frame.
        volatile LONG64 slotSize[SlotCount];
        uint32_t frameBytes;      // slot size
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
