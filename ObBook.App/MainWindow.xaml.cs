using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using DragDropEffects = System.Windows.DragDropEffects;
using MouseEventArgs = System.Windows.Input.MouseEventArgs;

namespace ObBook.App
{
    public partial class MainWindow : Window
    {
        private readonly ObBook.Engine _engine = new ObBook.Engine();
        private Point _treeDragStart;
        private bool _isUpdatingSource;

        public MainWindow()
        {
            InitializeComponent();

            TxtOblivionPath.Text =
                Environment.GetEnvironmentVariable("OBLIVION_PATH")
                ?? TryDetectOblivionPathFromRegistry()
                ?? "";

            TxtSource.Text =
                "<FONT face=1>\r\n" +
                "<DIV align=\"center\">OBLIVION BOOK CREATOR</DIV>\r\n" +
                "<BR>\r\n" +
                "<IMG src=\"book/fancy_font/a_70x61.dds\" width=70 height=61>.\r\n" +
                "<BR>\r\n" +
                "Use straight quotes and forward slashes.\r\n";

            CompileAndRefresh(updateSource: false);
        }

        private void BtnCompile_Click(object sender, RoutedEventArgs e)
        {
            CompileAndRefresh(updateSource: true);
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

        private void BtnBrowse_Click(object sender, RoutedEventArgs e)
        {
            using (var dlg = new System.Windows.Forms.FolderBrowserDialog())
            {
                dlg.Description = "Select Oblivion root folder or Data folder";
                dlg.ShowNewFolderButton = false;

                if (Directory.Exists(TxtOblivionPath.Text))
                    dlg.SelectedPath = TxtOblivionPath.Text;

                if (dlg.ShowDialog() == System.Windows.Forms.DialogResult.OK)
                {
                    TxtOblivionPath.Text = dlg.SelectedPath;
                    CompileAndRefresh(updateSource: false);
                }
            }
        }

        private void BtnAutoDetect_Click(object sender, RoutedEventArgs e)
        {
            var detected = TryDetectOblivionPathFromRegistry();
            if (string.IsNullOrWhiteSpace(detected))
            {
                MessageBox.Show(this, "Could not auto-detect Oblivion install path from registry.", "Auto Detect", MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            TxtOblivionPath.Text = detected;
            CompileAndRefresh(updateSource: false);
        }

        private void BtnScanAssets_Click(object sender, RoutedEventArgs e)
        {
            CompileAndRefresh(updateSource: false);
        }

        private void CompileAndRefresh(bool updateSource)
        {
            try
            {
                _engine.SetOblivionDirectory(TxtOblivionPath.Text ?? "");
                _engine.SetSourceText(TxtSource.Text ?? "");
                _engine.Compile();

                if (updateSource)
                {
                    _isUpdatingSource = true;
                    TxtSource.Text = _engine.NormalizedText;
                    _isUpdatingSource = false;
                }

                LstDiags.Items.Clear();
                var diags = _engine.GetDiagnostics();
                foreach (var d in diags)
                {
                    LstDiags.Items.Add($"{d.SeverityLevel,-7} off={d.Offset} len={d.Length} :: {d.Message}");
                }

                RefreshAssetTree();

                ImgPreview.Source = _engine.RenderPreviewPage(1000, 700, 96f);
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "Compile/Scan failed", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void RefreshAssetTree()
        {
            TreeAssets.Items.Clear();

            var rootPath = new TreeViewItem
            {
                Header = string.IsNullOrWhiteSpace(_engine.ResolvedDataDirectory)
                    ? "Data directory: (not resolved)"
                    : $"Data directory: {_engine.ResolvedDataDirectory}",
                IsExpanded = true,
                IsEnabled = false
            };
            TreeAssets.Items.Add(rootPath);

            var fonts = (_engine.GetBookFontAssets() ?? new System.Collections.Generic.List<string>())
                .Select(ExtractAssetPath)
                .Where(s => !string.IsNullOrWhiteSpace(s))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(s => s, StringComparer.OrdinalIgnoreCase)
                .ToList();

            var textures = (_engine.GetBookTextureAssets() ?? new System.Collections.Generic.List<string>())
                .Select(ExtractAssetPath)
                .Where(s => !string.IsNullOrWhiteSpace(s))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(s => s, StringComparer.OrdinalIgnoreCase)
                .ToList();

            TreeAssets.Items.Add(BuildCategoryNode("Fonts", fonts));
            TreeAssets.Items.Add(BuildCategoryNode("Textures", textures));
        }

        private TreeViewItem BuildCategoryNode(string name, List<string> paths)
        {
            var root = new TreeViewItem
            {
                Header = $"{name} ({paths.Count})",
                IsExpanded = true
            };

            foreach (var path in paths)
            {
                root.Items.Add(new TreeViewItem
                {
                    Header = path,
                    Tag = path
                });
            }

            return root;
        }

        private static string ExtractAssetPath(string raw)
        {
            if (string.IsNullOrWhiteSpace(raw)) return "";
            var idx = raw.IndexOf(" [", StringComparison.Ordinal);
            return idx > 0 ? raw.Substring(0, idx) : raw;
        }

        private static string TryDetectOblivionPathFromRegistry()
        {
            var keys = new[]
            {
                @"SOFTWARE\Bethesda Softworks\Oblivion",
                @"SOFTWARE\WOW6432Node\Bethesda Softworks\Oblivion"
            };

            foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
            {
                foreach (var hive in new[] { RegistryHive.LocalMachine, RegistryHive.CurrentUser })
                {
                    try
                    {
                        using (var baseKey = RegistryKey.OpenBaseKey(hive, view))
                        {
                            foreach (var keyPath in keys)
                            {
                                using (var key = baseKey.OpenSubKey(keyPath))
                                {
                                    var value = key?.GetValue("Installed Path") as string;
                                    if (!string.IsNullOrWhiteSpace(value) && Directory.Exists(value))
                                        return value;
                                }
                            }
                        }
                    }
                    catch
                    {
                        // best-effort autodetection
                    }
                }
            }

            return null;
        }

        private void TreeAssets_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            _treeDragStart = e.GetPosition(TreeAssets);
        }

        private void TreeAssets_PreviewMouseMove(object sender, MouseEventArgs e)
        {
            if (e.LeftButton == MouseButtonState.Pressed)
            {
                var pos = e.GetPosition(TreeAssets);
                if (Math.Abs(pos.X - _treeDragStart.X) > SystemParameters.MinimumHorizontalDragDistance ||
                    Math.Abs(pos.Y - _treeDragStart.Y) > SystemParameters.MinimumVerticalDragDistance)
                {
                    if (TreeAssets.SelectedItem is TreeViewItem item && item.Tag is string path && !string.IsNullOrWhiteSpace(path))
                    {
                        System.Windows.DragDrop.DoDragDrop(TreeAssets, new DataObject(DataFormats.Text, path), DragDropEffects.Copy);
                    }
                }
            }
            else
            {
                _treeDragStart = e.GetPosition(TreeAssets);
            }
        }

        private void TxtSource_PreviewDragOver(object sender, System.Windows.DragEventArgs e)
        {
            e.Effects = e.Data.GetDataPresent(DataFormats.Text) ? DragDropEffects.Copy : DragDropEffects.None;
            e.Handled = true;
        }


        private void TxtSource_TextChanged(object sender, TextChangedEventArgs e)
        {
            if (_isUpdatingSource) return;

            try
            {
                _engine.SetOblivionDirectory(TxtOblivionPath.Text ?? "");
                _engine.SetSourceText(TxtSource.Text ?? "");
                _engine.Compile();
                ImgPreview.Source = _engine.RenderPreviewPage(1000, 700, 96f);
            }
            catch
            {
                // keep typing responsive; compile button surfaces full errors.
            }
        }

        private void TxtSource_Drop(object sender, System.Windows.DragEventArgs e)
        {
            if (!e.Data.GetDataPresent(DataFormats.Text)) return;
            var payload = e.Data.GetData(DataFormats.Text) as string;
            if (string.IsNullOrWhiteSpace(payload)) return;

            var caret = TxtSource.CaretIndex;
            TxtSource.Text = TxtSource.Text.Insert(caret, payload);
            TxtSource.CaretIndex = caret + payload.Length;
            TxtSource.Focus();
        }
    }
}
