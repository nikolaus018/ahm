using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.Win32;
using Windows.Graphics;
using Windows.UI;

namespace AhmSettings;

/// <summary>
/// Edits ahm.ini next to the exe and nudges the resident overlay (WM_APP+2)
/// to reload it, so every change shows up live.
/// </summary>
public sealed partial class MainWindow : Window
{
    const string Section = "ahm";
    const uint WM_RELOAD = 0x8000 + 2;
    const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    static readonly string Ini = Path.Combine(AppContext.BaseDirectory, "ahm.ini");

    [DllImport("kernel32", CharSet = CharSet.Unicode)]
    static extern int GetPrivateProfileInt(string sec, string key, int def, string file);
    [DllImport("kernel32", CharSet = CharSet.Unicode)]
    static extern bool WritePrivateProfileString(string sec, string key, string val, string file);
    [DllImport("user32", CharSet = CharSet.Unicode)]
    static extern IntPtr FindWindow(string cls, string? title);
    [DllImport("user32")]
    static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);

    bool _loading = true;

    public MainWindow()
    {
        InitializeComponent();
        Title = "ahm settings";
        AppWindow.Resize(new SizeInt32(640, 860));
        Load();
        _loading = false;
    }

    static int Get(string key, int def) => GetPrivateProfileInt(Section, key, def, Ini);
    static void Put(string key, int v) => WritePrivateProfileString(Section, key, v.ToString(), Ini);

    void Load()
    {
        int shapes = Get("Shapes", 3);
        CrossBox.IsChecked = (shapes & 1) != 0;
        DotBox.IsChecked = (shapes & 2) != 0;
        CircleBox.IsChecked = (shapes & 4) != 0;

        int c = Get("Color", 0x2828FF);   // COLORREF: 0x00BBGGRR
        Picker.Color = Color.FromArgb(255, (byte)(c & 0xFF), (byte)((c >> 8) & 0xFF), (byte)((c >> 16) & 0xFF));

        SizeSlider.Value = Get("Size", 12);
        ThickSlider.Value = Get("Thickness", 2);
        GapSlider.Value = Get("Gap", 4);
        OpacitySlider.Value = Get("Opacity", 100);
        OutlineSwitch.IsOn = Get("Outline", 1) != 0;
        VisibleSwitch.IsOn = Get("Visible", 1) != 0;
        using (var run = Registry.CurrentUser.OpenSubKey(RunKey))
            StartupSwitch.IsOn = run?.GetValue("ahm") != null;
        OffXBox.Value = Get("OffsetX", 0);
        OffYBox.Value = Get("OffsetY", 0);
    }

    void Apply()
    {
        if (_loading) return;

        int shapes = (CrossBox.IsChecked == true ? 1 : 0) | (DotBox.IsChecked == true ? 2 : 0) | (CircleBox.IsChecked == true ? 4 : 0);
        if (shapes == 0) { shapes = 1; CrossBox.IsChecked = true; return; }   // re-enters Apply

        Color col = Picker.Color;
        Put("Shapes", shapes);
        Put("Color", col.R | (col.G << 8) | (col.B << 16));
        Put("Size", (int)SizeSlider.Value);
        Put("Thickness", (int)ThickSlider.Value);
        Put("Gap", (int)GapSlider.Value);
        Put("Opacity", (int)OpacitySlider.Value);
        Put("Outline", OutlineSwitch.IsOn ? 1 : 0);
        Put("Visible", VisibleSwitch.IsOn ? 1 : 0);
        if (!double.IsNaN(OffXBox.Value)) Put("OffsetX", (int)OffXBox.Value);
        if (!double.IsNaN(OffYBox.Value)) Put("OffsetY", (int)OffYBox.Value);

        using (var run = Registry.CurrentUser.CreateSubKey(RunKey))
        {
            if (StartupSwitch.IsOn)
                run.SetValue("ahm", $"\"{Path.Combine(AppContext.BaseDirectory, "ahm.exe")}\"");
            else
                run.DeleteValue("ahm", throwOnMissingValue: false);
        }

        IntPtr overlay = FindWindow("ahm_overlay", null);
        if (overlay != IntPtr.Zero) PostMessage(overlay, WM_RELOAD, IntPtr.Zero, IntPtr.Zero);
    }

    void OnChanged(object sender, RoutedEventArgs e) => Apply();
    void OnSlider(object sender, RangeBaseValueChangedEventArgs e) => Apply();
    void OnNumber(NumberBox sender, NumberBoxValueChangedEventArgs e) => Apply();
    void OnColorChanged(ColorPicker sender, ColorChangedEventArgs e) => Apply();
}
