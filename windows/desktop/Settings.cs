using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

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
    public bool AutoFraming { get; set; }
    /// <summary>0 = wide, 100 = tight.</summary>
    public int Framing { get; set; } = 50;
    /// <summary>Camera format id, see <see cref="VideoFormat.All"/> (e.g. "1080p30").</summary>
    public string OutputFormat { get; set; } = VideoFormat.Default.Id;
    /// <summary>Retouch on/off; the strengths below are kept when it's switched off.
    /// Null in settings from version 1.1, where retouch was on whenever a strength was above 0.</summary>
    public bool? RetouchEnabled { get; set; }
    /// <summary>0..100 %.</summary>
    public int RednessReduction { get; set; } = 50;
    /// <summary>0..100 %.</summary>
    public int SkinSmoothing { get; set; } = 30;
    /// <summary>Closing the window hides it to the tray (true) or exits the app (false).</summary>
    public bool CloseToTray { get; set; } = true;
    /// <summary>"System", "Light" or "Dark".</summary>
    public string Theme { get; set; } = "System";
    public bool ShowStatusPanel { get; set; } = true;
    public bool TrayHintShown { get; set; }

    [JsonIgnore]
    public bool IsRetouchOn => RetouchEnabled ?? (RednessReduction > 0 || SkinSmoothing > 0);

    private static string FilePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "IPhoneWebcam", "settings.json");

    /// <summary>Use a separate settings file (development instances, see App --dev).</summary>
    public static void UseProfile(string folderName) =>
        FilePath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), folderName, "settings.json");

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
        return new Settings { RetouchEnabled = false };
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
