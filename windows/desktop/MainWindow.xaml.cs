using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Wpf.Ui.Controls;
using TextBlock = System.Windows.Controls.TextBlock;

namespace IPhoneWebcam.Desktop;

public partial class MainWindow : FluentWindow
{
    private readonly AppController _controller;
    private readonly WriteableBitmap _preview;
    private readonly Dictionary<string, TextBlock> _statusValues = new();
    private long _lastPreviewId;
    private bool _renderingHooked;
    private bool _allowClose;
    private bool _initializing = true;
    private bool _updatingDevices;
    private ConnectionState _lastState = (ConnectionState)(-1);

    internal event Action? HiddenToTray;
    internal event Action? ExitRequested;
    internal event Action<string>? SettingsRequested;

    private Settings Settings => _controller.Settings;

    internal MainWindow(AppController controller)
    {
        InitializeComponent();
        _controller = controller;
        ThemeService.Register(this);

        VersionText.Text = AppController.Version + (controller.DevMode ? "  (dev)" : "");
        _preview = new WriteableBitmap(Engine.PreviewWidth, Engine.PreviewHeight, 96, 96, PixelFormats.Bgra32, null);
        PreviewImage.Source = _preview;

        AddStatusTile("phone", SymbolRegular.Phone24, "Phone");
        AddStatusTile("resolution", SymbolRegular.SlideSize24, "Resolution");
        AddStatusTile("fps", SymbolRegular.TopSpeed24, "Frame rate");
        AddStatusTile("bitrate", SymbolRegular.DataUsage24, "Bitrate");
        AddStatusTile("decoder", SymbolRegular.BrainCircuit24, "Decoder");
        AddStatusTile("camera", SymbolRegular.Video24, "Virtual camera");
        AddStatusTile("faces", SymbolRegular.Person24, "Faces");

        AutoFrameSwitch.IsChecked = Settings.AutoFraming;
        FramingSlider.Value = Settings.Framing;
        RetouchSwitch.IsChecked = Settings.IsRetouchOn;
        RednessSlider.Value = Settings.RednessReduction;
        SmoothingSlider.Value = Settings.SkinSmoothing;
        MirrorToggle.IsChecked = Settings.Mirror;
        RotateToggle.IsChecked = Settings.Rotate180;
        StatusPanel.Visibility = Settings.ShowStatusPanel ? Visibility.Visible : Visibility.Collapsed;
        UpdateEffectControls();

        CameraInfoBar.IsOpen = controller.CameraError != null;
        CameraInfoBar.Message = controller.DevMode
            ? "Development instance (--dev): the virtual camera isn't registered."
            : "Other apps can't see the iPhone yet. Reinstall iPhone Webcam (Windows 11 is required). " +
              $"Details: {controller.CameraError}";

        IsVisibleChanged += (_, _) => UpdatePreviewHook();
        StateChanged += (_, _) => UpdatePreviewHook();
        controller.Updated += Refresh;
        _initializing = false;
        Refresh();
    }

    internal void CloseForExit()
    {
        _allowClose = true;
        _controller.Updated -= Refresh;
        UnhookPreview();
        Close();
    }

    protected override void OnClosing(CancelEventArgs e)
    {
        if (!_allowClose)
        {
            e.Cancel = true;
            if (Settings.CloseToTray)
            {
                // Keep the virtual camera running in the tray.
                Hide();
                HiddenToTray?.Invoke();
            }
            else
            {
                // Exit the app; deferred because a window can't be closed from inside OnClosing.
                Dispatcher.BeginInvoke(() => ExitRequested?.Invoke());
            }
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

    private void ClearPreview()
    {
        var black = new byte[Engine.PreviewWidth * Engine.PreviewHeight * 4];
        _preview.WritePixels(new Int32Rect(0, 0, Engine.PreviewWidth, Engine.PreviewHeight), black, Engine.PreviewWidth * 4, 0);
    }

    private void OnPreviewSizeChanged(object sender, SizeChangedEventArgs e) =>
        PreviewHost.Clip = new RectangleGeometry(new Rect(e.NewSize), 8, 8);

    // ----- Status --------------------------------------------------------------------------------

    private void AddStatusTile(string key, SymbolRegular symbol, string label)
    {
        var grid = new Grid();
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition());
        var icon = new SymbolIcon { Symbol = symbol, FontSize = 20, VerticalAlignment = VerticalAlignment.Center };
        var text = new StackPanel { Margin = new Thickness(12, 0, 0, 0) };
        var caption = new TextBlock { Text = label, FontSize = 12 };
        caption.SetResourceReference(TextBlock.ForegroundProperty, "TextFillColorSecondaryBrush");
        var value = new TextBlock { Text = "—", TextTrimming = TextTrimming.CharacterEllipsis, Margin = new Thickness(0, 2, 0, 0) };
        text.Children.Add(caption);
        text.Children.Add(value);
        Grid.SetColumn(text, 1);
        grid.Children.Add(icon);
        grid.Children.Add(text);
        var tile = new Border { Child = grid, Padding = new Thickness(12, 10, 12, 10), Margin = new Thickness(0, 0, 0, 6) };
        tile.SetResourceReference(StyleProperty, "Surface");
        StatusTiles.Children.Add(tile);
        _statusValues[key] = value;
    }

    private void SetStatus(string key, string value, Brush? brush = null)
    {
        TextBlock text = _statusValues[key];
        text.Text = value;
        text.ToolTip = value;
        if (brush != null) text.Foreground = brush;
        else text.ClearValue(TextBlock.ForegroundProperty);
    }

    private Brush ResourceBrush(string key) => (Brush)FindResource(key);

    private void Refresh()
    {
        EngineStatus s = _controller.Status;
        bool streaming = s.State == ConnectionState.Streaming && s.Width > 0;

        // Device card
        (string state, string dotKey) = s.State switch
        {
            ConnectionState.Streaming => ("Streaming", "SystemFillColorSuccessBrush"),
            ConnectionState.Connecting => ("Connecting…", "SystemFillColorCautionBrush"),
            ConnectionState.Reconnecting => ("Reconnecting…", "SystemFillColorCautionBrush"),
            _ => ("Not connected", "TextFillColorTertiaryBrush"),
        };
        string phone = !string.IsNullOrEmpty(s.DeviceName) ? s.DeviceName : s.Address;
        DeviceTitle.Text = string.IsNullOrEmpty(phone) ? "No iPhone" : phone;
        StateText.Text = state;
        StateDot.Fill = ResourceBrush(dotKey);
        MessageText.Text = s.State is ConnectionState.Connecting or ConnectionState.Reconnecting ? s.Message : "";
        MessageText.Visibility = string.IsNullOrEmpty(MessageText.Text) ? Visibility.Collapsed : Visibility.Visible;

        bool active = s.State != ConnectionState.Idle;
        ConnectToggleButton.Icon = new SymbolIcon { Symbol = active ? SymbolRegular.PlugDisconnected24 : SymbolRegular.PlugConnected24 };
        ConnectToggleButton.ToolTip = active ? "Disconnect" : "Connect";
        System.Windows.Automation.AutomationProperties.SetName(ConnectToggleButton, active ? "Disconnect" : "Connect");
        ConnectToggleButton.IsEnabled = active || DeviceCombo.SelectedItem != null || !string.IsNullOrEmpty(Settings.LastAddress);

        UpdateDevices(s);

        // Title bar badges
        (string camera, string cameraKey) = s.Camera switch
        {
            CameraState.InUse => ("Camera in use", "SystemFillColorSuccessBrush"),
            CameraState.Ready => ("Camera ready", "SystemFillColorAttentionBrush"),
            _ => ("Camera unavailable", "SystemFillColorCriticalBrush"),
        };
        CameraPillText.Text = camera;
        CameraDot.Fill = TryBrush(cameraKey) ?? ResourceBrush("AccentFillColorDefaultBrush");
        FormatBadge.Visibility = streaming ? Visibility.Visible : Visibility.Collapsed;
        FormatResolution.Text = ResolutionLabel(s.Width, s.Height);
        FormatFps.Text = $"{s.Fps}FPS";

        // Status panel
        SetStatus("phone", string.IsNullOrEmpty(phone) ? "—" : string.IsNullOrEmpty(s.DeviceName) ? s.Address : $"{s.DeviceName} ({s.Address})");
        SetStatus("resolution", streaming ? $"{s.Width} × {s.Height}" : "—");
        SetStatus("fps", streaming ? $"{s.Fps} fps" : "—");
        SetStatus("bitrate", streaming ? $"{s.BitrateKbps / 1000.0:0.0} Mbit/s" : "—");
        SetStatus("decoder", streaming && !string.IsNullOrEmpty(s.Decoder)
            ? (s.HardwareDecode != 0 ? $"GPU · {s.Decoder}" : s.Decoder) : "—");
        SetStatus("camera", s.Camera switch
        {
            CameraState.InUse => "In use by an app",
            CameraState.Ready => "Ready (not in use)",
            _ => "Not available",
        }, s.Camera == CameraState.Unavailable ? ResourceBrush("SystemFillColorCriticalBrush") : null);
        SetStatus("faces", !streaming ? "—" : s.Faces < 0 ? "Not sent by this iPhone app" : s.Faces == 0 ? "None" : s.Faces.ToString());

        // Effects
        EffectsInfoBar.IsOpen = s.EffectsAvailable == 0;
        AutoFrameSwitch.IsEnabled = RetouchSwitch.IsEnabled = s.EffectsAvailable != 0;
        UpdateEffectControls();
        FaceStatus.Visibility = streaming ? Visibility.Visible : Visibility.Collapsed;
        FaceStatusText.Text = s.Faces < 0 ? "Update the iPhone app to enable face detection"
            : s.Faces == 0 ? "No face detected"
            : s.Faces == 1 ? "Face detected"
            : $"{s.Faces} faces detected";
        FaceStatusIcon.Symbol = s.Faces > 1 ? SymbolRegular.People24 : SymbolRegular.Person24;

        // Preview overlay
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
                ? "Open the iPhone Webcam app on your iPhone. Both devices need to be on the same Wi-Fi."
                : s.Message;
        }
    }

    private Brush? TryBrush(string key) => TryFindResource(key) as Brush;

    private static string ResolutionLabel(int width, int height) => height switch
    {
        >= 2160 => "4K",
        >= 1440 => "1440p",
        >= 1080 => "1080p",
        >= 720 => "720p",
        > 0 => $"{height}p",
        _ => "",
    };

    private void UpdateDevices(EngineStatus s)
    {
        var devices = _controller.Devices;
        var current = DeviceCombo.ItemsSource as IReadOnlyList<DeviceItem>;
        _updatingDevices = true;
        try
        {
            if (current == null || !current.SequenceEqual(devices)) DeviceCombo.ItemsSource = devices;
            DeviceItem? connected = devices.FirstOrDefault(d => _controller.IsConnectedTo(d));
            if (!DeviceCombo.IsDropDownOpen && !Equals(DeviceCombo.SelectedItem, connected))
            {
                DeviceCombo.SelectedItem = connected;
            }
        }
        finally
        {
            _updatingDevices = false;
        }
        DevicePlaceholder.Text = devices.Count == 0 ? "Searching for iPhones…" : "Select a device…";
        DevicePlaceholder.Visibility = DeviceCombo.SelectedItem == null ? Visibility.Visible : Visibility.Collapsed;
    }

    private void UpdateEffectControls()
    {
        FramingSlider.IsEnabled = AutoFrameSwitch.IsChecked == true && AutoFrameSwitch.IsEnabled;
        RednessSlider.IsEnabled = SmoothingSlider.IsEnabled = RetouchSwitch.IsChecked == true && RetouchSwitch.IsEnabled;
        RednessValue.Text = $"{RednessSlider.Value:0} %";
        SmoothingValue.Text = $"{SmoothingSlider.Value:0} %";
    }

    // ----- Actions -----------------------------------------------------------------------------

    private void OnDeviceSelected(object sender, SelectionChangedEventArgs e)
    {
        if (_updatingDevices || _initializing) return;
        if (DeviceCombo.SelectedItem is DeviceItem device && !_controller.IsConnectedTo(device))
        {
            _controller.Connect(device);
        }
    }

    private void OnConnectToggleClick(object sender, RoutedEventArgs e)
    {
        if (_controller.Status.State != ConnectionState.Idle)
        {
            _controller.Disconnect();
        }
        else if (DeviceCombo.SelectedItem is DeviceItem device)
        {
            _controller.Connect(device);
        }
        else if (!string.IsNullOrEmpty(Settings.LastAddress))
        {
            _controller.Connect(Settings.LastDeviceName, Settings.LastAddress, Settings.LastPort);
        }
    }

    private void OnRefreshClick(object sender, RoutedEventArgs e) => _controller.RefreshDevices();

    private void OnConnectByIpClick(object sender, RoutedEventArgs e) => SettingsRequested?.Invoke("Devices");

    private void OnSettingsClick(object sender, RoutedEventArgs e) => SettingsRequested?.Invoke("General");

    private void OnStatusPanelClick(object sender, RoutedEventArgs e)
    {
        Settings.ShowStatusPanel = StatusPanel.Visibility != Visibility.Visible;
        StatusPanel.Visibility = Settings.ShowStatusPanel ? Visibility.Visible : Visibility.Collapsed;
        _controller.ScheduleSave();
    }

    internal void SetStatusPanelVisible(bool visible) =>
        StatusPanel.Visibility = visible ? Visibility.Visible : Visibility.Collapsed;

    private void OnAutoFramingChanged(object sender, RoutedEventArgs e)
    {
        if (_initializing) return;
        Settings.AutoFraming = AutoFrameSwitch.IsChecked == true;
        UpdateEffectControls();
        _controller.ApplyEffects();
    }

    private void OnFramingChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (_initializing) return;
        Settings.Framing = (int)Math.Round(FramingSlider.Value);
        _controller.ApplyEffects();
    }

    private void OnRetouchChanged(object sender, RoutedEventArgs e)
    {
        if (_initializing) return;
        Settings.RetouchEnabled = RetouchSwitch.IsChecked == true;
        UpdateEffectControls();
        _controller.ApplyEffects();
    }

    private void OnRetouchSliderChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (_initializing) return;
        Settings.RednessReduction = (int)Math.Round(RednessSlider.Value);
        Settings.SkinSmoothing = (int)Math.Round(SmoothingSlider.Value);
        UpdateEffectControls();
        _controller.ApplyEffects();
    }

    private void OnTransformChanged(object sender, RoutedEventArgs e)
    {
        if (_initializing) return;
        Settings.Mirror = MirrorToggle.IsChecked == true;
        Settings.Rotate180 = RotateToggle.IsChecked == true;
        _controller.ApplyEffects();
    }
}
