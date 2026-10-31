using System;
using System.Windows;
using Wpf.Ui.Controls;

namespace IPhoneWebcam.Desktop;

public partial class SettingsWindow : FluentWindow
{
    public SettingsWindow()
    {
        InitializeComponent();
        ThemeService.Register(this);
    }

    // Ctrl+1..4 switch pages (keyboard access; the navigation items aren't reachable via UI Automation).
    protected override void OnPreviewKeyDown(System.Windows.Input.KeyEventArgs e)
    {
        if (System.Windows.Input.Keyboard.Modifiers == System.Windows.Input.ModifierKeys.Control)
        {
            string? page = e.Key switch
            {
                System.Windows.Input.Key.D1 => "General",
                System.Windows.Input.Key.D2 => "Devices",
                System.Windows.Input.Key.D3 => "Video",
                System.Windows.Input.Key.D4 => "About",
                _ => null,
            };
            if (page != null)
            {
                ShowPage(page);
                e.Handled = true;
            }
        }
        base.OnPreviewKeyDown(e);
    }

    /// <summary>Shows a page by name: "General", "Devices", "Video" or "About".</summary>
    public void ShowPage(string name)
    {
        Type page = name switch
        {
            "Devices" => typeof(Pages.DevicesPage),
            "Video" => typeof(Pages.VideoPage),
            "About" => typeof(Pages.AboutPage),
            _ => typeof(Pages.GeneralPage),
        };
        if (IsLoaded) Navigation.Navigate(page);
        else Loaded += (_, _) => Navigation.Navigate(page);
    }
}
