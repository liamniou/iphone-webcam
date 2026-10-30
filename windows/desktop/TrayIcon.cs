using System;
using System.Drawing;
using System.Windows;
using Forms = System.Windows.Forms;

namespace IPhoneWebcam.Desktop;

/// <summary>System tray icon with a small menu; keeps the app reachable while the window is hidden.</summary>
internal sealed class TrayIcon : IDisposable
{
    private readonly Forms.NotifyIcon _icon;
    private readonly Forms.ToolStripMenuItem _disconnectItem;
    private string _lastText = "";

    public TrayIcon(Action show, Action disconnect, Action exit)
    {
        var menu = new Forms.ContextMenuStrip();
        var showItem = new Forms.ToolStripMenuItem("Show iPhone Webcam", null, (_, _) => show());
        showItem.Font = new Font(showItem.Font, System.Drawing.FontStyle.Bold);
        _disconnectItem = new Forms.ToolStripMenuItem("Disconnect", null, (_, _) => disconnect()) { Enabled = false };
        menu.Items.Add(showItem);
        menu.Items.Add(_disconnectItem);
        menu.Items.Add(new Forms.ToolStripSeparator());
        menu.Items.Add(new Forms.ToolStripMenuItem("Exit", null, (_, _) => exit()));

        var iconStream = Application.GetResourceStream(new Uri("pack://application:,,,/Assets/AppIcon.ico"))!.Stream;
        _icon = new Forms.NotifyIcon
        {
            Icon = new Icon(iconStream, Forms.SystemInformation.SmallIconSize),
            Text = "iPhone Webcam",
            ContextMenuStrip = menu,
            Visible = true,
        };
        _icon.MouseClick += (_, e) =>
        {
            if (e.Button == Forms.MouseButtons.Left) show();
        };
    }

    public void Update(EngineStatus status)
    {
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
        _disconnectItem.Enabled = status.State != ConnectionState.Idle;
    }

    public void ShowHint(string title, string text) =>
        _icon.ShowBalloonTip(4000, title, text, Forms.ToolTipIcon.Info);

    public void Dispose()
    {
        _icon.Visible = false;
        _icon.Dispose();
    }
}
