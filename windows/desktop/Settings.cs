using System;
using System.IO;
using System.Text.Json;

namespace IPhoneWebcam.Desktop;

/// <summary>User settings, stored in %APPDATA%\IPhoneWebcam\settings.json.</summary>
internal sealed class Settings
{
    public string LastDeviceName { get; set; } = "";
    public string LastAddress { get; set; } = "";
    public ushort LastPort { get; set; } = Engine.DefaultPort;
    /// <summary>Reconnect to the last phone on startup. Cleared when the user disconnects manually.</summary>
    public bool ConnectOnStartup { get; set; } = true;
    public bool AutoReconnect { get; set; } = true;
    public bool HardwareDecode { get; set; } = true;
    public bool Mirror { get; set; }
    public bool Rotate180 { get; set; }
    public bool TrayHintShown { get; set; }

    private static readonly string FilePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "IPhoneWebcam", "settings.json");

    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    public static Settings Load()
    {
        try
        {
            if (File.Exists(FilePath))
            {
                return JsonSerializer.Deserialize<Settings>(File.ReadAllText(FilePath)) ?? new Settings();
            }
        }
        catch (Exception)
        {
            // Corrupt or unreadable settings: start with defaults.
        }
        return new Settings();
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(this, JsonOptions));
        }
        catch (Exception)
        {
            // Settings are a convenience; never crash because they can't be written.
        }
    }
}
