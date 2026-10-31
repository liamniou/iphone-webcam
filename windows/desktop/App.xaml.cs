using System;
using System.Linq;
using System.Threading;
using System.Windows;

namespace IPhoneWebcam.Desktop;

public partial class App : Application
{
    private const string InstanceMutexName = "IPhoneWebcam.Desktop.SingleInstance";
    private const string ShowEventName = "IPhoneWebcam.Desktop.Show";

    private Mutex? _instanceMutex;
    private EventWaitHandle? _showEvent;
    private Settings _settings = new();
    private MainWindow? _window;
    private TrayIcon? _tray;
    private bool _engineRunning;
    private bool _exiting;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        // --dev: a development instance that runs next to the installed app: separate instance lock
        // and settings, and it doesn't register (or feed) the virtual camera.
        bool dev = e.Args.Any(a => string.Equals(a, "--dev", StringComparison.OrdinalIgnoreCase));
        string suffix = dev ? ".Dev" : "";
        if (dev) Settings.UseProfile("IPhoneWebcam-Dev");

        _instanceMutex = new Mutex(true, InstanceMutexName + suffix, out bool firstInstance);
        if (!firstInstance)
        {
            // Already running (probably in the tray): bring that window to the front instead.
            try { EventWaitHandle.OpenExisting(ShowEventName + suffix).Set(); } catch (WaitHandleCannotBeOpenedException) { }
            Shutdown();
            return;
        }
        _showEvent = new EventWaitHandle(false, EventResetMode.AutoReset, ShowEventName + suffix);
        new Thread(() =>
        {
            while (_showEvent.WaitOne() && !_exiting) Dispatcher.BeginInvoke(ShowMainWindow);
        }) { IsBackground = true, Name = "Show listener" }.Start();

        _settings = Settings.Load();

        try
        {
            int hr = Engine.Initialize();
            if (hr < 0) throw new InvalidOperationException(Engine.DescribeError(hr));
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException or InvalidOperationException)
        {
            MessageBox.Show($"Couldn't start the iPhone Webcam engine (IPhoneWebcamEngine.dll).\n\n{ex.Message}",
                            "iPhone Webcam", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
            return;
        }
        _engineRunning = true;

        Engine.SetAutoReconnect(_settings.AutoReconnect);
        Engine.SetHardwareDecode(_settings.HardwareDecode);
        Engine.SetTransform(_settings.Mirror, _settings.Rotate180);
        Engine.SetAutoFraming(_settings.AutoFraming, _settings.Framing / 100f);
        Engine.SetRetouch(_settings.RednessReduction / 100f, _settings.SkinSmoothing / 100f);

        int cameraHr = dev ? unchecked((int)0x80004004) /* E_ABORT */ : Engine.StartVirtualCamera();
        string? cameraError = dev ? "disabled in --dev mode" : cameraHr < 0 ? Engine.DescribeError(cameraHr) : null;

        _window = new MainWindow(_settings, cameraError);
        _tray = new TrayIcon(ShowMainWindow, () => _window.DisconnectFromTray(), ExitApplication);
        _window.StatusUpdated += status => _tray.Update(status);
        _window.HiddenToTray += OnHiddenToTray;
        _window.ExitRequested += ExitApplication;

        if (_settings.ConnectOnStartup && _settings.AutoReconnect && !string.IsNullOrEmpty(_settings.LastAddress))
        {
            Engine.Connect(_settings.LastDeviceName, _settings.LastAddress, _settings.LastPort);
        }

        bool startMinimized = e.Args.Any(a => string.Equals(a, "--minimized", StringComparison.OrdinalIgnoreCase));
        if (startMinimized) _tray.ShowHint("iPhone Webcam", "Running in the tray. The virtual camera is available.");
        else ShowMainWindow();
    }

    private void OnHiddenToTray()
    {
        if (_settings.TrayHintShown) return;
        _settings.TrayHintShown = true;
        _settings.Save();
        _tray?.ShowHint("iPhone Webcam is still running",
                        "The virtual camera stays available. Use the tray icon to open the window or exit, " +
                        "or turn off \"Close to tray\" in the options.");
    }

    private void ShowMainWindow()
    {
        if (_window == null || _exiting) return;
        _window.Show();
        if (_window.WindowState == WindowState.Minimized) _window.WindowState = WindowState.Normal;
        _window.Activate();
    }

    private void ExitApplication()
    {
        if (_exiting) return;
        _exiting = true;
        _window?.CloseForExit();
        _tray?.Dispose();
        _settings.Save();
        ShutdownEngine();
        Shutdown();
    }

    private void ShutdownEngine()
    {
        if (!_engineRunning) return;
        _engineRunning = false;
        Engine.Shutdown(); // also removes the virtual camera
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _tray?.Dispose();
        ShutdownEngine();
        _showEvent?.Set();
        _instanceMutex?.Dispose();
        base.OnExit(e);
    }

    protected override void OnSessionEnding(SessionEndingCancelEventArgs e)
    {
        ExitApplication();
        base.OnSessionEnding(e);
    }
}
