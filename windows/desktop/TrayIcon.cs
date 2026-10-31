using System;
using System.Drawing;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Interop;
using Wpf.Ui.Controls;
using Forms = System.Windows.Forms;
using MenuItem = System.Windows.Controls.MenuItem;

namespace IPhoneWebcam.Desktop;

/// <summary>
/// System tray icon. The shell icon is a WinForms NotifyIcon (works even before any window has been
/// shown); its menu is a WPF ContextMenu so it gets the Fluent style and icons of the rest of the app.
/// </summary>
internal sealed class TrayIcon : IDisposable
{
    private readonly Forms.NotifyIcon _icon;
    private readonly AppController _controller;
    private readonly Window _anchor;
    private readonly Action _toggleWindow;
    private readonly Action _openSettings;
    private readonly Action _exit;
    private ContextMenu? _menu;
    private string _lastText = "";

    public TrayIcon(AppController controller, Window anchor, Action toggleWindow, Action openSettings, Action exit)
    {
        _controller = controller;
        _anchor = anchor;
        _toggleWindow = toggleWindow;
        _openSettings = openSettings;
        _exit = exit;

        var iconStream = Application.GetResourceStream(new Uri("pack://application:,,,/Assets/AppIcon.ico"))!.Stream;
        _icon = new Forms.NotifyIcon
        {
            Icon = new Icon(iconStream, Forms.SystemInformation.SmallIconSize),
            Text = "iPhone Webcam",
            Visible = true,
        };
        _icon.MouseUp += (_, e) =>
        {
            if (e.Button == Forms.MouseButtons.Left) _toggleWindow();
            else if (e.Button == Forms.MouseButtons.Right) ShowMenu();
        };
        controller.Updated += Update;
    }

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    public void ShowMenu()
    {
        if (_menu?.IsOpen == true) _menu.IsOpen = false;

        bool visible = _anchor.IsVisible && _anchor.WindowState != WindowState.Minimized;
        var menu = new ContextMenu { MinWidth = 240 };
        menu.Items.Add(Item(visible ? "Hide iPhone Webcam" : "Show iPhone Webcam",
                            visible ? SymbolRegular.EyeOff24 : SymbolRegular.Eye24, _toggleWindow));
        menu.Items.Add(new Separator());

        menu.Items.Add(new MenuItem { Header = "Device", IsEnabled = false });
        var devices = _controller.Devices;
        foreach (DeviceItem device in devices)
        {
            bool connected = _controller.IsConnectedTo(device);
            var item = Item(device.Name, connected ? SymbolRegular.Checkmark24 : SymbolRegular.Phone24, () => _controller.Connect(device));
            item.FontWeight = connected ? FontWeights.SemiBold : FontWeights.Normal;
            menu.Items.Add(item);
        }
        if (devices.Count == 0)
        {
            menu.Items.Add(new MenuItem { Header = "No iPhone found", IsEnabled = false, Icon = Symbol(SymbolRegular.Search24) });
        }
        if (_controller.Status.State != ConnectionState.Idle)
        {
            menu.Items.Add(Item("Disconnect", SymbolRegular.PlugDisconnected24, _controller.Disconnect));
        }
        menu.Items.Add(new Separator());

        menu.Items.Add(Item("Settings", SymbolRegular.Settings24, _openSettings));
        menu.Items.Add(Item("Help & advice", SymbolRegular.QuestionCircle24, () => App.OpenUrl(App.ReadmeUrl)));
        menu.Items.Add(Item("Quit iPhone Webcam", SymbolRegular.Dismiss24, _exit));

        // Without a foreground window of our own, the menu wouldn't close when clicking elsewhere.
        SetForegroundWindow(new WindowInteropHelper(_anchor).EnsureHandle());
        menu.Placement = PlacementMode.MousePoint;
        menu.IsOpen = true;
        _menu = menu;
    }

    private static SymbolIcon Symbol(SymbolRegular symbol) => new() { Symbol = symbol, FontSize = 16 };

    private static MenuItem Item(string text, SymbolRegular symbol, Action action)
    {
        var item = new MenuItem { Header = text, Icon = Symbol(symbol) };
        item.Click += (_, _) => action();
        return item;
    }

    private void Update()
    {
        EngineStatus status = _controller.Status;
        string text = status.State switch
        {
            ConnectionState.Streaming => $"iPhone Webcam – streaming {status.Width}×{status.Height} @ {status.Fps} fps",
            ConnectionState.Connecting => "iPhone Webcam – connecting…",
            ConnectionState.Reconnecting => "iPhone Webcam – reconnecting…",
            _ => "iPhone Webcam – not connected",
        };
        if (text.Length > 127) text = text[..127];
        if (text != _lastText)
        {
            _icon.Text = text;
            _lastText = text;
        }
    }

    public void ShowHint(string title, string text) =>
        _icon.ShowBalloonTip(4000, title, text, Forms.ToolTipIcon.Info);

    public void Dispose()
    {
        _controller.Updated -= Update;
        if (_menu != null) _menu.IsOpen = false;
        _icon.Visible = false;
        _icon.Dispose();
    }
}
