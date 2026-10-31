using System.Windows;
using System.Windows.Controls;

namespace IPhoneWebcam.Desktop.Pages;

public partial class AboutPage : Page
{
    public AboutPage()
    {
        InitializeComponent();
        VersionText.Text = $"Version {AppController.Version}";
    }

    private void OnSourceClick(object sender, RoutedEventArgs e) => App.OpenUrl(App.RepositoryUrl);
    private void OnHelpClick(object sender, RoutedEventArgs e) => App.OpenUrl(App.ReadmeUrl);
    private void OnIssueClick(object sender, RoutedEventArgs e) => App.OpenUrl(App.RepositoryUrl + "/issues");
    private void OnLicenseClick(object sender, RoutedEventArgs e) => App.OpenUrl(App.RepositoryUrl + "/blob/main/LICENSE");
    private void OnWpfUiClick(object sender, RoutedEventArgs e) => App.OpenUrl("https://github.com/lepoco/wpfui");
}
