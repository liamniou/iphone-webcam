using System;
using System.Linq;
using System.Windows;
using System.Windows.Controls;

namespace IPhoneWebcam.Desktop.Pages;

public partial class GeneralPage : Page
{
    private readonly bool _initializing;

    public GeneralPage()
    {
        InitializeComponent();
        _initializing = true;
        Settings settings = App.Controller.Settings;
        ThemeCombo.SelectedItem = ThemeCombo.Items.OfType<ComboBoxItem>().FirstOrDefault(i => (string)i.Tag == ThemeService.Current);
        StatusPanelSwitch.IsChecked = settings.ShowStatusPanel;
        StartupSwitch.IsChecked = Autostart.IsEnabled;
        CloseToTraySwitch.IsChecked = settings.CloseToTray;
        _initializing = false;
    }

    private void OnThemeChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_initializing || ThemeCombo.SelectedItem is not ComboBoxItem item) return;
        App.Controller.Settings.Theme = (string)item.Tag;
        ThemeService.Apply(App.Controller.Settings.Theme);
        App.Controller.ScheduleSave();
    }

    private void OnStatusPanelChanged(object sender, RoutedEventArgs e)
    {
        if (_initializing) return;
        App.Controller.Settings.ShowStatusPanel = StatusPanelSwitch.IsChecked == true;
        App.Current.MainAppWindow?.SetStatusPanelVisible(App.Controller.Settings.ShowStatusPanel);
        App.Controller.ScheduleSave();
    }

    private void OnStartupChanged(object sender, RoutedEventArgs e)
    {
        if (_initializing) return;
        bool wanted = StartupSwitch.IsChecked == true;
        if (wanted == Autostart.IsEnabled) return;
        try
        {
            Autostart.Set(wanted);
        }
        catch (Exception ex)
        {
            System.Windows.MessageBox.Show(Window.GetWindow(this), $"Couldn't change the startup setting: {ex.Message}",
                                           "iPhone Webcam", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        StartupSwitch.IsChecked = Autostart.IsEnabled;
    }

    private void OnCloseToTrayChanged(object sender, RoutedEventArgs e)
    {
        if (_initializing) return;
        App.Controller.Settings.CloseToTray = CloseToTraySwitch.IsChecked == true;
        App.Controller.ScheduleSave();
    }
}
