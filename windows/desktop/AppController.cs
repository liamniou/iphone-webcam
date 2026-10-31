using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Windows.Threading;

namespace IPhoneWebcam.Desktop;

internal sealed record DeviceItem(string Name, string Address, ushort Port)
{
    public string Endpoint => $"{Address}:{Port}";
}

/// <summary>
/// App-wide state and actions shared by the main window, the settings window and the tray menu:
/// polls the engine, keeps the device list, connects/disconnects and applies settings.
/// </summary>
internal sealed class AppController
{
    private readonly DispatcherTimer _pollTimer;
    private readonly DispatcherTimer _saveTimer;
    private bool _userDisconnected;

    public AppController(Settings settings, string? cameraError, bool devMode)
    {
        Settings = settings;
        CameraError = cameraError;
        DevMode = devMode;

        _pollTimer = new DispatcherTimer(DispatcherPriority.Background) { Interval = TimeSpan.FromMilliseconds(300) };
        _pollTimer.Tick += (_, _) => Poll();

        // Sliders change values many times while dragging; write the settings file once they settle.
        _saveTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(600) };
        _saveTimer.Tick += (_, _) =>
        {
            _saveTimer.Stop();
            Settings.Save();
        };
    }

    public Settings Settings { get; }
    public string? CameraError { get; }
    public bool DevMode { get; }
    public EngineStatus Status { get; private set; }
    public IReadOnlyList<DeviceItem> Devices { get; private set; } = Array.Empty<DeviceItem>();

    /// <summary>Raised on the UI thread after every poll (~3 times per second).</summary>
    public event Action? Updated;

    public static string Version
    {
        get
        {
            string? version = typeof(AppController).Assembly
                .GetCustomAttributes(typeof(System.Reflection.AssemblyInformationalVersionAttribute), false)
                .OfType<System.Reflection.AssemblyInformationalVersionAttribute>()
                .FirstOrDefault()?.InformationalVersion;
            return version?.Split('+')[0] ?? "1.0.0";
        }
    }

    public void Start()
    {
        ApplyAll();
        if (Settings.ConnectOnStartup && Settings.AutoReconnect && !string.IsNullOrEmpty(Settings.LastAddress))
        {
            Engine.Connect(Settings.LastDeviceName, Settings.LastAddress, Settings.LastPort);
        }
        Poll();
        _pollTimer.Start();
    }

    public void Stop()
    {
        _pollTimer.Stop();
        if (_saveTimer.IsEnabled)
        {
            _saveTimer.Stop();
            Settings.Save();
        }
    }

    // ----- Connection --------------------------------------------------------------------------

    public void Connect(DeviceItem device) => Connect(device.Name, device.Address, device.Port);

    public void Connect(string name, string address, ushort port)
    {
        _userDisconnected = false;
        Settings.LastDeviceName = name;
        Settings.LastAddress = address;
        Settings.LastPort = port;
        Settings.ConnectOnStartup = true;
        Settings.Save();
        Engine.Connect(name, address, port);
        Poll();
    }

    /// <summary>Parses "192.168.1.23" or "192.168.1.23:5005" and connects; returns an error message or null.</summary>
    public string? ConnectByAddress(string text)
    {
        text = text.Trim();
        ushort port = Engine.DefaultPort;
        int colon = text.LastIndexOf(':');
        if (colon > 0)
        {
            if (!ushort.TryParse(text[(colon + 1)..], out port) || port == 0) return "Invalid port number.";
            text = text[..colon];
        }
        if (!IPAddress.TryParse(text, out IPAddress? ip) || ip.AddressFamily != AddressFamily.InterNetwork)
        {
            return "Enter an IPv4 address like 192.168.1.23 (shown in the iPhone app).";
        }
        Connect("", ip.ToString(), port);
        return null;
    }

    public void Disconnect()
    {
        _userDisconnected = true;
        Settings.ConnectOnStartup = false;
        Settings.Save();
        Engine.Disconnect();
        Poll();
    }

    public void RefreshDevices()
    {
        Engine.RefreshDevices();
        Devices = Array.Empty<DeviceItem>();
        Updated?.Invoke();
    }

    public bool IsConnectedTo(DeviceItem device) =>
        Status.State != ConnectionState.Idle &&
        (string.IsNullOrEmpty(device.Name) ? Status.Address == device.Address : Status.DeviceName == device.Name);

    // ----- Settings ------------------------------------------------------------------------------

    public void ApplyAll()
    {
        Engine.SetAutoReconnect(Settings.AutoReconnect);
        Engine.SetHardwareDecode(Settings.HardwareDecode);
        ApplyEffects();
    }

    /// <summary>Pushes image/effect settings to the engine and schedules a save.</summary>
    public void ApplyEffects()
    {
        Engine.SetTransform(Settings.Mirror, Settings.Rotate180);
        Engine.SetAutoFraming(Settings.AutoFraming, Settings.Framing / 100f);
        bool retouch = Settings.IsRetouchOn;
        Engine.SetRetouch(retouch ? Settings.RednessReduction / 100f : 0, retouch ? Settings.SkinSmoothing / 100f : 0);
        ScheduleSave();
    }

    public void SetAutoReconnect(bool enabled)
    {
        Settings.AutoReconnect = enabled;
        Engine.SetAutoReconnect(enabled);
        ScheduleSave();
    }

    public void SetHardwareDecode(bool enabled)
    {
        Settings.HardwareDecode = enabled;
        Engine.SetHardwareDecode(enabled);
        ScheduleSave();
    }

    public void ScheduleSave()
    {
        _saveTimer.Stop();
        _saveTimer.Start();
    }

    // ----- Polling -------------------------------------------------------------------------------

    private void Poll()
    {
        Status = Engine.GetStatus();
        var devices = Engine.GetDevices()
            .Select(d => new DeviceItem(d.Name, d.Address, d.Port))
            .OrderBy(d => d.Name, StringComparer.CurrentCultureIgnoreCase)
            .ToList();
        if (!devices.SequenceEqual(Devices)) Devices = devices;

        // First run convenience: connect to the first phone that shows up.
        if (Status.State == ConnectionState.Idle && !_userDisconnected &&
            string.IsNullOrEmpty(Settings.LastAddress) && devices.Count > 0)
        {
            Connect(devices[0]);
            return;
        }
        Updated?.Invoke();
    }
}
