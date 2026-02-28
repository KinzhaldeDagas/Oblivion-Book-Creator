using System;
using System.Linq;
using System.Windows;
using System.Windows.Input;

namespace ObBook.App
{
    public partial class MainWindow : Window
    {
        private readonly ObBook.Engine _engine = new ObBook.Engine();

        public MainWindow()
        {
            InitializeComponent();

            _engine.SetOblivionDirectory(Environment.GetEnvironmentVariable("OBLIVION_PATH") ?? "");

            TxtSource.Text =
                "<FONT face=1>\r\n" +
                "<DIV align=\"center\">OBLIVION BOOK CREATOR</DIV>\r\n" +
                "<BR>\r\n" +
                "<IMG src=\"book/fancy_font/a_70x61.dds\" width=70 height=61>.\r\n" +
                "<BR>\r\n" +
                "Use straight quotes and forward slashes.\r\n";
        }

        private void BtnCompile_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                _engine.SetSourceText(TxtSource.Text ?? "");
                _engine.Compile();

                // Normalize source (v1) and re-display
                TxtSource.Text = _engine.NormalizedText;

                // Diagnostics
                LstDiags.Items.Clear();
                var diags = _engine.GetDiagnostics();
                foreach (var d in diags)
                {
                    LstDiags.Items.Add($"{d.SeverityLevel,-7} off={d.Offset} len={d.Length} :: {d.Message}");
                }

                // Preview stub
                ImgPreview.Source = _engine.RenderPreviewPage(1000, 700, 96f);
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "Compile failed", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void BtnCopy_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                Clipboard.SetText(_engine.ExportDescText ?? "");
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "Copy failed", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }
    }
}
