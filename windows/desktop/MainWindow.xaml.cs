using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace IPhoneWebcam.Desktop;

internal sealed record DeviceItem(string Name, string Address, ushort Port)
{
    public string Endpoint => $"{Address}:{Port}";
}

public partial class MainWindow : Window
{
    private readonly Settings _settings;
    private readonly WriteableBitmap _preview;
    private readonly DispatcherTimer _statusTimer;
    private long _lastPreviewId;
    private bool _renderingHooked;
    private bool _userDisconnected;
    private bool _allowClose;
    private ConnectionState _lastState = (ConnectionState)(-1);
    private bool _initializing = true;

    internal event Action<EngineStatus>? StatusUpdated;
    internal event Action? HiddenToTray;

    internal MainWindow(Settings settings, string? cameraError)
    {
        InitializeComponent();
        _settings = settings;

        _preview = new WriteableBitmap(Engine.PreviewWidth, Engine.PreviewHeight, 96, 96, PixelFormats.Bgra32, null);
        PreviewImage.Source = _preview;

        MirrorBox.IsChecked = settings.Mirror;
        RotateBox.IsChecked = settings.Rotate180;
        AutoReconnectBox.IsChecked = settings.AutoReconnect;
        HardwareDecodeBox.IsChecked = settings.HardwareDecode;
        AutostartBox.IsChecked = Autostart.IsEnabled;
        IpBox.Text = string.IsNullOrEmpty(settings.LastDeviceName) ? settings.LastAddress : "";

        if (cameraError != null)
        {
            CameraWarning.Visibility = Visibility.Visible;
            CameraWarningText.Text = "The virtual camera isn't available, so other apps can't see the iPhone yet. " +
                                     "Install it by running windows\\scripts\\install.ps1 as administrator (Windows 11 is required). " +
                                     $"Details: {cameraError}";
        }

        _statusTimer = new DispatcherTimer(DispatcherPriority.Background) { Interval = TimeSpan.FromMilliseconds(300) };
        _statusTimer.Tick += (_, _) => Poll();
        _statusTimer.Start();

        IsVisibleChanged += (_, _) => UpdatePreviewHook();
        StateChanged += (_, _) => UpdatePreviewHook();
        _initializing = false;
        Poll();
    }

    /// <summary>Called by the tray menu.</summary>
    internal void DisconnectFromTray() => Disconnect();

    internal void CloseForExit()
    {
        _allowClose = true;
        _statusTimer.Stop();
        UnhookPreview();
        Close();
    }

    protected override void OnClosing(CancelEventArgs e)
    {
        if (!_allowClose)
        {
            // Closing the window keeps the virtual camera running in the tray.
            e.Cancel = true;
            Hide();
            HiddenToTray?.Invoke();
        }
        base.OnClosing(e);
    }

    // ----- Preview -----------------------------------------------------------------------------

    private void UpdatePreviewHook()
    {
        bool shouldRender = IsVisible && WindowState != WindowState.Minimized;
        if (shouldRender && !_renderingHooked)
        {
            CompositionTarget.Rendering += OnRendering;
            _renderingHooked = true;
            Engine.SetPreviewEnabled(true);
        }
        else if (!shouldRender)
        {
            UnhookPreview();
        }
    }

    private void UnhookPreview()
    {
        if (!_renderingHooked) return;
        CompositionTarget.Rendering -= OnRendering;
        _renderingHooked = false;
        Engine.SetPreviewEnabled(false);
    }

    private void OnRendering(object? sender, EventArgs e)
    {
        _preview.Lock();
        try
        {
            long id = Engine.GetPreviewFrame(_preview.BackBuffer, _preview.BackBufferStride, _lastPreviewId);
            if (id != 0)
            {
                _lastPreviewId = id;
                _preview.AddDirtyRect(new Int32Rect(0, 0, Engine.PreviewWidth, Engine.PreviewHeight));
            }
        }
        finally
        {
            _preview.Unlock();
        }
    }

    // ----- Status & devices --------------------------------------------------------------------

    private void Poll()
    {
        EngineStatus status = Engine.GetStatus();
        UpdateDevices(status);
        UpdateStatus(status);
        StatusUpdated?.Invoke(status);
    }

    private void UpdateDevices(EngineStatus status)
    {
        var devices = Engine.GetDevices()
            .Select(d => new DeviceItem(d.Name, d.Address, d.Port))
            .OrderBy(d => d.Name, StringComparer.CurrentCultureIgnoreCase)
            .ToList();

        var current = DeviceList.Items.Cast<DeviceItem>().ToList();
        if (!current.SequenceEqual(devices))
        {
            var selected = DeviceList.SelectedItem as DeviceItem;
            DeviceList.ItemsSource = devices;
            DeviceList.SelectedItem = devices.FirstOrDefault(d => d.Name == selected?.Name)
                                      ?? devices.FirstOrDefault(d => d.Name == status.DeviceName);
        }
        if (DeviceList.SelectedItem == null && devices.Count > 0) DeviceList.SelectedIndex = 0;
        NoDevicesText.Visibility = devices.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        ConnectButton.IsEnabled = DeviceList.SelectedItem != null;

        // First run convenience: connect to the first phone that shows up.
        if (status.State == ConnectionState.Idle && !_userDisconnected &&
            string.IsNullOrEmpty(_settings.LastAddress) && devices.Count > 0)
        {
            Connect(devices[0].Name, devices[0].Address, devices[0].Port);
        }
    }

    private void UpdateStatus(EngineStatus s)
    {
        bool streaming = s.State == ConnectionState.Streaming && s.Width > 0;

        (string state, Brush dot) = s.State switch
        {
            ConnectionState.Streaming => ("Streaming", (Brush)FindResource("GoodBrush")),
            ConnectionState.Connecting => ("Connecting…", (Brush)FindResource("WarnBrush")),
            ConnectionState.Reconnecting => ("Reconnecting…", (Brush)FindResource("WarnBrush")),
            _ => ("Not connected", (Brush)FindResource("SubtleTextBrush")),
        };
        StateText.Text = state;
        StateDot.Fill = dot;
        MessageText.Text = s.State == ConnectionState.Streaming ? "" : s.Message;
        MessageText.Visibility = string.IsNullOrEmpty(MessageText.Text) ? Visibility.Collapsed : Visibility.Visible;

        string phone = !string.IsNullOrEmpty(s.DeviceName) ? $"{s.DeviceName} ({s.Address})" : s.Address;
        PhoneValue.Text = string.IsNullOrEmpty(phone) ? "—" : phone;
        PhoneValue.ToolTip = PhoneValue.Text;
        ResolutionValue.Text = streaming ? $"{s.Width} × {s.Height}" : "—";
        FpsValue.Text = streaming ? $"{s.Fps} fps" : "—";
        BitrateValue.Text = streaming ? $"{s.BitrateKbps / 1000.0:0.0} Mbit/s" : "—";
        DecoderValue.Text = s.State == ConnectionState.Streaming && !string.IsNullOrEmpty(s.Decoder)
            ? (s.HardwareDecode != 0 ? $"GPU · {s.Decoder}" : s.Decoder)
            : "—";
        DecoderValue.ToolTip = DecoderValue.Text;

        (string camera, Brush cameraBrush) = s.Camera switch
        {
            CameraState.InUse => ("In use by an app", (Brush)FindResource("GoodBrush")),
            CameraState.Ready => ("Ready (not in use)", (Brush)FindResource("TextBrush")),
            _ => ("Not installed", (Brush)FindResource("BadBrush")),
        };
        CameraValue.Text = camera;
        CameraValue.Foreground = cameraBrush;

        DisconnectButton.IsEnabled = s.State != ConnectionState.Idle;
        LiveBadge.Visibility = streaming ? Visibility.Visible : Visibility.Collapsed;
        Overlay.Visibility = streaming ? Visibility.Collapsed : Visibility.Visible;

        if (s.State != _lastState)
        {
            _lastState = s.State;
            if (!streaming) ClearPreview();
        }
        if (!streaming)
        {
            OverlayTitle.Text = s.State switch
            {
                ConnectionState.Connecting => "Connecting…",
                ConnectionState.Reconnecting => "Reconnecting…",
                ConnectionState.Streaming => "Waiting for video…",
                _ => "No iPhone connected",
            };
            OverlayText.Text = s.State == ConnectionState.Idle
                ? "Open the iPhone Webcam app on your iPhone and make sure both devices are on the same Wi-Fi."
                : s.Message;
        }
    }

    private void ClearPreview()
    {
        var black = new byte[Engine.PreviewWidth * Engine.PreviewHeight * 4];
        _preview.WritePixels(new Int32Rect(0, 0, Engine.PreviewWidth, Engine.PreviewHeight), black, Engine.PreviewWidth * 4, 0);
    }

    // ----- Actions -----------------------------------------------------------------------------

    private void Connect(string name, string address, ushort port)
    {
        _userDisconnected = false;
        _settings.LastDeviceName = name;
        _settings.LastAddress = address;
        _settings.LastPort = port;
        _settings.ConnectOnStartup = true;
        _settings.Save();
        Engine.Connect(name, address, port);
        Poll();
    }

    private void Disconnect()
    {
        _userDisconnected = true;
        _settings.ConnectOnStartup = false;
        _settings.Save();
        Engine.Disconnect();
        Poll();
    }

    private void ConnectSelected()
    {
        if (DeviceList.SelectedItem is DeviceItem device) Connect(device.Name, device.Address, device.Port);
    }

    private void OnConnectClick(object sender, RoutedEventArgs e) => ConnectSelected();

    private void OnDeviceDoubleClick(object sender, MouseButtonEventArgs e) => ConnectSelected();

    private void OnDeviceSelectionChanged(object sender, SelectionChangedEventArgs e) =>
        ConnectButton.IsEnabled = DeviceList.SelectedItem != null;

    private void OnDisconnectClick(object sender, RoutedEventArgs e) => Disconnect();

    private void OnRefreshClick(object sender, RoutedEventArgs e)
    {
        Engine.RefreshDevices();
        DeviceList.ItemsSource = new List<DeviceItem>();
    }

    private void OnConnectIpClick(object sender, RoutedEventArgs e) => ConnectToTypedAddress();

    private void OnIpBoxKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) ConnectToTypedAddress();
    }

    private void ConnectToTypedAddress()
    {
        string text = IpBox.Text.Trim();
        ushort port = Engine.DefaultPort;
        int colon = text.LastIndexOf(':');
        if (colon > 0)
        {
            if (!ushort.TryParse(text[(colon + 1)..], out port) || port == 0)
            {
                ShowIpError("Invalid port number.");
                return;
            }
            text = text[..colon];
        }
        if (!IPAddress.TryParse(text, out IPAddress? ip) || ip.AddressFamily != AddressFamily.InterNetwork)
        {
            ShowIpError("Enter an IPv4 address like 192.168.1.23 (shown in the iPhone app).");
            return;
        }
        IpError.Visibility = Visibility.Collapsed;
        Connect("", ip.ToString(), port);
    }

    private void ShowIpError(string message)
    {
        IpError.Text = message;
        IpError.Visibility = Visibility.Visible;
    }

    private void OnTransformChanged(object sender, RoutedEventArgs e)
    {
        if (_initializing) return;
        _settings.Mirror = MirrorBox.IsChecked == true;
        _settings.Rotate180 = RotateBox.IsChecked == true;
        _settings.Save();
        Engine.SetTransform(_settings.Mirror, _settings.Rotate180);
    }

    private void OnAutoReconnectChanged(object sender, RoutedEventArgs e)
    {
        if (_initializing) return;
        _settings.AutoReconnect = AutoReconnectBox.IsChecked == true;
        _settings.Save();
        Engine.SetAutoReconnect(_settings.AutoReconnect);
    }

    private void OnHardwareDecodeChanged(object sender, RoutedEventArgs e)
    {
        if (_initializing) return;
        _settings.HardwareDecode = HardwareDecodeBox.IsChecked == true;
        _settings.Save();
        Engine.SetHardwareDecode(_settings.HardwareDecode);
    }

    private void OnAutostartChanged(object sender, RoutedEventArgs e)
    {
        if (_initializing) return;
        bool wanted = AutostartBox.IsChecked == true;
        if (wanted == Autostart.IsEnabled) return;
        try
        {
            Autostart.Set(wanted);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, $"Couldn't change the startup setting: {ex.Message}", "iPhone Webcam",
                            MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        AutostartBox.IsChecked = Autostart.IsEnabled;
    }
}
