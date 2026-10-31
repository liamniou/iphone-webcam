using System.Collections.Generic;
using System.Linq;

namespace IPhoneWebcam.Desktop;

/// <summary>A camera format the user can choose (what the iPhone streams and the virtual camera offers).</summary>
internal sealed record VideoFormat(string Id, int Width, int Height, int Fps, string Label, string Description)
{
    public static readonly IReadOnlyList<VideoFormat> All = new[]
    {
        new VideoFormat("2160p30", 3840, 2160, 30, "4K · 30 fps", "Sharpest picture; ~30 Mbit/s over Wi-Fi"),
        new VideoFormat("1080p60", 1920, 1080, 60, "1080p · 60 fps", "Smooth motion; ~15 Mbit/s"),
        new VideoFormat("1080p30", 1920, 1080, 30, "1080p · 30 fps", "Recommended: works with every video app; ~7.5 Mbit/s"),
        new VideoFormat("720p60", 1280, 720, 60, "720p · 60 fps", "Smooth motion on slower Wi-Fi; ~6.6 Mbit/s"),
        new VideoFormat("720p30", 1280, 720, 30, "720p · 30 fps", "Lowest bandwidth; ~3.3 Mbit/s"),
    };

    public static VideoFormat Default => All.First(f => f.Id == "1080p30");

    public static VideoFormat FromId(string? id) => All.FirstOrDefault(f => f.Id == id) ?? Default;

    /// <summary>Short label for the title bar badge, e.g. "1080p".</summary>
    public string ShortResolution => Height >= 2160 ? "4K" : $"{Height}p";
}
