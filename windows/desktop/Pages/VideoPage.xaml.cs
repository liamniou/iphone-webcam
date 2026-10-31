using System.Windows;
using System.Windows.Controls;

namespace IPhoneWebcam.Desktop.Pages;

public partial class VideoPage : Page
{
    private readonly bool _initializing;

    public VideoPage()
    {
        InitializeComponent();
        _initializing = true;
        GpuSwitch.IsChecked = App.Controller.Settings.HardwareDecode;
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
        (CameraStateText.Text, CameraText.Text) = s.Camera switch
        {
            CameraState.InUse => ("In use", "“iPhone Webcam” is installed and an app is using it right now."),
            CameraState.Ready => ("Ready", "“iPhone Webcam” is installed and available to all apps while this app runs."),
            _ => ("Not available", App.Controller.CameraError is { } error
                ? $"The camera couldn't be registered: {error}. Reinstall iPhone Webcam (Windows 11 is required)."
                : "The camera couldn't be registered. Reinstall iPhone Webcam (Windows 11 is required)."),
        };

        DecoderText.Text = s.State == ConnectionState.Streaming && !string.IsNullOrEmpty(s.Decoder)
            ? $"Recommended for 4K. Currently decoding with {(s.HardwareDecode != 0 ? "the GPU: " + s.Decoder : "the CPU")}."
            : "Recommended for 4K: much lower CPU use. Takes effect on the next connection.";

        bool effects = s.EffectsAvailable != 0;
        EffectsState.Text = effects ? "Available" : "Unavailable";
        EffectsText.Text = effects
            ? "Processed on the GPU. Turn them on in the main window."
            : "These need a DirectX 11 graphics card, which wasn't found.";
    }

    private void OnGpuChanged(object sender, RoutedEventArgs e)
    {
        if (_initializing) return;
        App.Controller.SetHardwareDecode(GpuSwitch.IsChecked == true);
    }
}
