using System;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Windows;

namespace IPhoneWebcam.Desktop;

public partial class App : Application
{
    public const string RepositoryUrl = "https://github.com/liamniou/iphone-webcam";
    public const string ReadmeUrl = RepositoryUrl + "#readme";

    private const string InstanceMutexName = "IPhoneWebcam.Desktop.SingleInstance";
    private const string ShowEventName = "IPhoneWebcam.Desktop.Show";

    private Mutex? _instanceMutex;
    private EventWaitHandle? _showEvent;
    private AppController? _controller;
    private MainWindow? _window;
    private SettingsWindow? _settingsWindow;
    private TrayIcon? _tray;
    private bool _engineRunning;
    private bool _exiting;

    public static new App Current => (App)Application.Current;

    /// <summary>Shared state and actions (valid after startup).</summary>
    internal static AppController Controller => Current._controller!;

    internal MainWindow? MainAppWindow => _window;

    public static void OpenUrl(string url)
    {
        try
        {
            Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
        }
        catch (Exception)
        {
            // No browser available; nothing useful to do.
        }
    }

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

        Settings settings = Settings.Load();
        ThemeService.Apply(settings.Theme);

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

        string? cameraError = null;
        if (dev)
        {
            cameraError = "disabled in --dev mode";
        }
        else
        {
            // The camera offers the chosen format, so set it before registering the camera.
            AppController.ApplyFormat(VideoFormat.FromId(settings.OutputFormat));
            int cameraHr = Engine.StartVirtualCamera();
            if (cameraHr < 0) cameraError = Engine.DescribeError(cameraHr);
        }

        _controller = new AppController(settings, cameraError, dev);
        _window = new MainWindow(_controller);
        MainWindow = _window;
        _window.HiddenToTray += OnHiddenToTray;
        _window.ExitRequested += ExitApplication;
        _window.SettingsRequested += ShowSettings;
        _tray = new TrayIcon(_controller, _window, ToggleMainWindow, () => ShowSettings("General"), ExitApplication);
        if (dev)
        {
            // UI testing hook: signalling this event opens the tray menu (no input simulation needed).
            var trayMenuEvent = new EventWaitHandle(false, EventResetMode.AutoReset, "IPhoneWebcam.Desktop.Dev.TrayMenu");
            new Thread(() =>
            {
                while (trayMenuEvent.WaitOne() && !_exiting) Dispatcher.BeginInvoke(() => _tray?.ShowMenu());
            }) { IsBackground = true, Name = "Dev tray menu listener" }.Start();
        }
        _controller.Start();

        bool startMinimized = e.Args.Any(a => string.Equals(a, "--minimized", StringComparison.OrdinalIgnoreCase));
        if (startMinimized) _tray.ShowHint("iPhone Webcam", "Running in the tray. The virtual camera is available.");
        else ShowMainWindow();
    }

    private void OnHiddenToTray()
    {
        Settings settings = _controller!.Settings;
        if (settings.TrayHintShown) return;
        settings.TrayHintShown = true;
        settings.Save();
        _tray?.ShowHint("iPhone Webcam is still running",
                        "The virtual camera stays available. Use the tray icon to open the window or quit, " +
                        "or turn off \"Close to tray\" in Settings.");
    }

    private void ShowMainWindow()
    {
        if (_window == null || _exiting) return;
        _window.Show();
        if (_window.WindowState == WindowState.Minimized) _window.WindowState = WindowState.Normal;
        _window.Activate();
    }

    private void ToggleMainWindow()
    {
        if (_window == null || _exiting) return;
        if (_window.IsVisible && _window.WindowState != WindowState.Minimized) _window.Hide();
        else ShowMainWindow();
    }

    private void ShowSettings(string page)
    {
        if (_exiting) return;
        if (_settingsWindow == null)
        {
            _settingsWindow = new SettingsWindow();
            if (_window?.IsVisible == true) _settingsWindow.Owner = _window;
            else _settingsWindow.WindowStartupLocation = WindowStartupLocation.CenterScreen;
            _settingsWindow.Closed += (_, _) => _settingsWindow = null;
        }
        _settingsWindow.ShowPage(page);
        _settingsWindow.Show();
        if (_settingsWindow.WindowState == WindowState.Minimized) _settingsWindow.WindowState = WindowState.Normal;
        _settingsWindow.Activate();
    }

    private void ExitApplication()
    {
        if (_exiting) return;
        _exiting = true;
        _settingsWindow?.Close();
        _window?.CloseForExit();
        _tray?.Dispose();
        _controller?.Stop();
        _controller?.Settings.Save();
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
