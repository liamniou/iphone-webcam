using System.Collections.Generic;
using System.Windows;
using Wpf.Ui.Appearance;
using Wpf.Ui.Controls;

namespace IPhoneWebcam.Desktop;

/// <summary>Applies the app theme ("System", "Light" or "Dark"); "System" follows Windows live.</summary>
internal static class ThemeService
{
    private static readonly List<Window> Windows = new();
    private static readonly HashSet<Window> Watched = new();
    private static string _theme = "System";

    public static string Current => _theme;

    public static void Apply(string theme)
    {
        _theme = theme is "Light" or "Dark" ? theme : "System";
        switch (_theme)
        {
            case "Light":
                ApplicationThemeManager.Apply(ApplicationTheme.Light, WindowBackdropType.Mica, true);
                break;
            case "Dark":
                ApplicationThemeManager.Apply(ApplicationTheme.Dark, WindowBackdropType.Mica, true);
                break;
            default:
                ApplicationThemeManager.ApplySystemTheme(true);
                break;
        }
        foreach (Window window in Windows) UpdateWatch(window);
    }

    /// <summary>Call once for every window, after it's created.</summary>
    public static void Register(Window window)
    {
        Windows.Add(window);
        window.Closed += (_, _) =>
        {
            Windows.Remove(window);
            Watched.Remove(window);
        };
        UpdateWatch(window);
    }

    private static void UpdateWatch(Window window)
    {
        bool watch = _theme == "System";
        if (watch == Watched.Contains(window)) return;
        if (watch)
        {
            SystemThemeWatcher.Watch(window, WindowBackdropType.Mica, true);
            Watched.Add(window);
        }
        else
        {
            // UnWatch throws for windows that haven't been loaded yet.
            if (window.IsLoaded) SystemThemeWatcher.UnWatch(window);
            Watched.Remove(window);
        }
    }
}
