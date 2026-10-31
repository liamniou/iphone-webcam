using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace IPhoneWebcam.Desktop.Pages;

public partial class DevicesPage : Page
{
    private readonly bool _initializing;

    public DevicesPage()
    {
        InitializeComponent();
        _initializing = true;
        Settings settings = App.Controller.Settings;
        AutoReconnectSwitch.IsChecked = settings.AutoReconnect;
        if (string.IsNullOrEmpty(settings.LastDeviceName)) AddressBox.Text = settings.LastAddress;
        _initializing = false;

        Loaded += (_, _) =>
        {
            App.Controller.Updated += Refresh;
            Refresh();
        };
        Unloaded += (_, _) => App.Controller.Updated -= Refresh;
    }

    private void Refresh()
    {
        EngineStatus s = App.Controller.Status;
        bool active = s.State != ConnectionState.Idle;
        string phone = !string.IsNullOrEmpty(s.DeviceName) ? s.DeviceName : s.Address;
        CurrentDeviceTitle.Text = active ? phone : "No iPhone connected";
        CurrentDeviceText.Text = s.State switch
        {
            ConnectionState.Streaming => $"Streaming from {s.Address}",
            ConnectionState.Connecting or ConnectionState.Reconnecting => s.Message,
            _ => "Select an iPhone in the main window, or connect by IP address below.",
        };
        DisconnectButton.IsEnabled = active;

        int count = App.Controller.Devices.Count;
        DiscoveryText.Text = count switch
        {
            0 => "No iPhones found yet. Open the iPhone Webcam app; both devices must be on the same Wi-Fi.",
            1 => "1 iPhone found on your network.",
            _ => $"{count} iPhones found on your network.",
        };
    }

    private void Connect()
    {
        string? error = App.Controller.ConnectByAddress(AddressBox.Text);
        AddressError.Message = error ?? "";
        AddressError.IsOpen = error != null;
    }

    private void OnConnectClick(object sender, RoutedEventArgs e) => Connect();

    private void OnAddressKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) Connect();
    }

    private void OnDisconnectClick(object sender, RoutedEventArgs e) => App.Controller.Disconnect();

    private void OnRefreshClick(object sender, RoutedEventArgs e) => App.Controller.RefreshDevices();

    private void OnAutoReconnectChanged(object sender, RoutedEventArgs e)
    {
        if (_initializing) return;
        App.Controller.SetAutoReconnect(AutoReconnectSwitch.IsChecked == true);
    }

    private void OnInstructionsClick(object sender, RoutedEventArgs e) => App.OpenUrl(App.ReadmeUrl + "#install-on-iphone");
}
