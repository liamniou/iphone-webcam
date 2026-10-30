using System;
using System.Runtime.InteropServices;

namespace IPhoneWebcam.Desktop;

internal enum ConnectionState
{
    Idle = 0,
    Connecting = 1,
    Streaming = 2,
    Reconnecting = 3,
}

internal enum CameraState
{
    Unavailable = 0,
    Ready = 1,
    InUse = 2,
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode, Pack = 8)]
internal struct EngineStatus
{
    public ConnectionState State;
    public CameraState Camera;
    public int Width;
    public int Height;
    public int Fps;
    public int BitrateKbps;
    public int HardwareDecode;
    public int Reserved;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceName;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string Address;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string Decoder;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string Message;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode, Pack = 8)]
internal struct EngineDevice
{
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string Name;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string Address;
    public ushort Port;
}

/// <summary>P/Invoke bindings for IPhoneWebcamEngine.dll (see windows/engine/IPhoneWebcamEngine.h).</summary>
internal static class Engine
{
    private const string Dll = "IPhoneWebcamEngine.dll";

    public const int PreviewWidth = 960;
    public const int PreviewHeight = 540;
    public const ushort DefaultPort = 5005;

    [DllImport(Dll, EntryPoint = "IPWC_Initialize")]
    public static extern int Initialize();

    [DllImport(Dll, EntryPoint = "IPWC_StartVirtualCamera")]
    public static extern int StartVirtualCamera();

    [DllImport(Dll, EntryPoint = "IPWC_Shutdown")]
    public static extern void Shutdown();

    [DllImport(Dll, EntryPoint = "IPWC_Connect", CharSet = CharSet.Unicode)]
    public static extern void Connect(string name, string address, ushort port);

    [DllImport(Dll, EntryPoint = "IPWC_Disconnect")]
    public static extern void Disconnect();

    [DllImport(Dll, EntryPoint = "IPWC_SetAutoReconnect")]
    public static extern void SetAutoReconnect([MarshalAs(UnmanagedType.Bool)] bool enabled);

    [DllImport(Dll, EntryPoint = "IPWC_SetHardwareDecode")]
    public static extern void SetHardwareDecode([MarshalAs(UnmanagedType.Bool)] bool enabled);

    [DllImport(Dll, EntryPoint = "IPWC_SetTransform")]
    public static extern void SetTransform([MarshalAs(UnmanagedType.Bool)] bool mirror, [MarshalAs(UnmanagedType.Bool)] bool rotate180);

    [DllImport(Dll, EntryPoint = "IPWC_SetPreviewEnabled")]
    public static extern void SetPreviewEnabled([MarshalAs(UnmanagedType.Bool)] bool enabled);

    [DllImport(Dll, EntryPoint = "IPWC_GetDevices")]
    private static extern int GetDevices([Out] EngineDevice[] devices, int capacity);

    [DllImport(Dll, EntryPoint = "IPWC_RefreshDevices")]
    public static extern void RefreshDevices();

    [DllImport(Dll, EntryPoint = "IPWC_GetStatus")]
    private static extern void GetStatus(out EngineStatus status);

    [DllImport(Dll, EntryPoint = "IPWC_GetPreviewFrame")]
    public static extern long GetPreviewFrame(IntPtr bgra, int stride, long lastFrameId);

    public static EngineStatus GetStatus()
    {
        GetStatus(out EngineStatus status);
        return status;
    }

    public static EngineDevice[] GetDevices()
    {
        var buffer = new EngineDevice[32];
        int count = GetDevices(buffer, buffer.Length);
        return buffer[..Math.Min(count, buffer.Length)];
    }

    public static string DescribeError(int hresult) =>
        $"0x{hresult:X8}: {Marshal.GetExceptionForHR(hresult)?.Message ?? "unknown error"}";
}
