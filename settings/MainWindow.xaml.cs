using System.Runtime.InteropServices;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
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
    static readonly string Ini = System.IO.Path.Combine(AppContext.BaseDirectory, "ahm.ini");

    static readonly Color[] Palette =
    {
        Color.FromArgb(255, 255, 40, 40), Color.FromArgb(255, 40, 255, 60), Color.FromArgb(255, 0, 230, 255),
        Color.FromArgb(255, 255, 235, 0), Color.FromArgb(255, 255, 0, 220), Color.FromArgb(255, 255, 140, 0),
        Color.FromArgb(255, 255, 255, 255),
    };

    [DllImport("kernel32", CharSet = CharSet.Unicode)]
    static extern int GetPrivateProfileInt(string sec, string key, int def, string file);
    [DllImport("kernel32", CharSet = CharSet.Unicode)]
    static extern bool WritePrivateProfileString(string sec, string key, string val, string file);
    [DllImport("user32", CharSet = CharSet.Unicode)]
    static extern IntPtr FindWindow(string cls, string? title);
    [DllImport("user32")]
    static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32")]
    static extern uint GetDpiForWindow(IntPtr hwnd);

    bool _loading = true;
    Color _color = Palette[0];
    readonly List<Button> _swatches = new();

    public MainWindow()
    {
        InitializeComponent();
        Title = "ahm settings";   // the tray looks this window up by title

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(TitleBar);

        double scale = GetDpiForWindow(WinRT.Interop.WindowNative.GetWindowHandle(this)) / 96.0;
        AppWindow.Resize(new SizeInt32((int)(900 * scale), (int)(680 * scale)));
        if (AppWindow.Presenter is OverlappedPresenter p)
        {
            p.IsResizable = false;
            p.IsMaximizable = false;
        }

        foreach (Color c in Palette)
        {
            var b = new Button
            {
                Width = 28, Height = 28, MinWidth = 0, Padding = new Thickness(0),
                CornerRadius = new CornerRadius(14), BorderThickness = new Thickness(2),
                Background = new SolidColorBrush(c), Tag = c,
            };
            b.Click += (_, _) => { _color = c; Apply(); };
            _swatches.Add(b);
            Swatches.Children.Add(b);
        }

        Load();
        _loading = false;
        Refresh();
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
        _color = Color.FromArgb(255, (byte)(c & 0xFF), (byte)((c >> 8) & 0xFF), (byte)((c >> 16) & 0xFF));
        Picker.Color = _color;

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
        if (shapes == 0) { CrossBox.IsChecked = true; shapes = 1; }

        Put("Shapes", shapes);
        Put("Color", _color.R | (_color.G << 8) | (_color.B << 16));
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
                run.SetValue("ahm", $"\"{System.IO.Path.Combine(AppContext.BaseDirectory, "ahm.exe")}\"");
            else
                run.DeleteValue("ahm", throwOnMissingValue: false);
        }

        IntPtr overlay = FindWindow("ahm_overlay", null);
        if (overlay != IntPtr.Zero) PostMessage(overlay, WM_RELOAD, IntPtr.Zero, IntPtr.Zero);
        Refresh();
    }

    // ---- UI refresh (labels, swatch selection, preview) -------------------

    void Refresh()
    {
        SizeVal.Text = $"{(int)SizeSlider.Value} px";
        ThickVal.Text = $"{(int)ThickSlider.Value} px";
        GapVal.Text = $"{(int)GapSlider.Value} px";
        OpacityVal.Text = $"{(int)OpacitySlider.Value}%";

        var on = (Brush)Application.Current.Resources["TextFillColorPrimaryBrush"];
        foreach (Button b in _swatches)
            b.BorderBrush = (Color)b.Tag == _color ? on : new SolidColorBrush(Colors.Transparent);

        DrawPreview();
    }

    void DrawPreview()
    {
        Preview.Children.Clear();

        int size = (int)SizeSlider.Value, t = (int)ThickSlider.Value, gap = (int)GapSlider.Value;
        int shapes = (CrossBox.IsChecked == true ? 1 : 0) | (DotBox.IsChecked == true ? 2 : 0) | (CircleBox.IsChecked == true ? 4 : 0);
        double w = Preview.ActualWidth > 0 ? Preview.ActualWidth : 280, h = 320;
        double cx = w / 2, cy = h / 2;
        double s = Math.Min(4.0, 140.0 / size);   // px -> preview dip

        Preview.Opacity = OpacitySlider.Value / 100.0;

        void Rect(Brush b, double x, double y, double rw, double rh)
        {
            var r = new Rectangle { Width = Math.Max(rw, 0), Height = Math.Max(rh, 0), Fill = b };
            Canvas.SetLeft(r, x);
            Canvas.SetTop(r, y);
            Preview.Children.Add(r);
        }
        void Ellipse(Brush b, double r, double stroke)
        {
            var e = new Ellipse { Width = 2 * r, Height = 2 * r };
            if (stroke > 0) { e.Stroke = b; e.StrokeThickness = stroke; } else e.Fill = b;
            Canvas.SetLeft(e, cx - r);
            Canvas.SetTop(e, cy - r);
            Preview.Children.Add(e);
        }
        void Layer(Brush b, double grow)
        {
            if ((shapes & 1) != 0)
            {
                double half = (t / 2.0 + grow) * s, outer = (size + grow) * s, inner = (t / 2.0 + gap - grow) * s;
                if (gap == 0)
                {
                    Rect(b, cx - outer, cy - half, 2 * outer, 2 * half);
                    Rect(b, cx - half, cy - outer, 2 * half, 2 * outer);
                }
                else
                {
                    Rect(b, cx + inner, cy - half, outer - inner, 2 * half);
                    Rect(b, cx - outer, cy - half, outer - inner, 2 * half);
                    Rect(b, cx - half, cy + inner, 2 * half, outer - inner);
                    Rect(b, cx - half, cy - outer, 2 * half, outer - inner);
                }
            }
            if ((shapes & 4) != 0) Ellipse(b, (size - t / 2.0) * s, (t + 2 * grow) * s);
            if ((shapes & 2) != 0) Ellipse(b, ((t < 2 ? 1.5 : t * 0.75 + 0.5) + grow) * s, 0);
        }

        if (OutlineSwitch.IsOn) Layer(new SolidColorBrush(Colors.Black), 1);
        Layer(new SolidColorBrush(_color), 0);
    }

    void OnPreviewSize(object sender, SizeChangedEventArgs e)
    {
        Preview.Clip = new RectangleGeometry { Rect = new Windows.Foundation.Rect(0, 0, e.NewSize.Width, e.NewSize.Height) };
        DrawPreview();
    }

    // ---- handlers ---------------------------------------------------------

    void OnChanged(object sender, RoutedEventArgs e) => Apply();
    void OnSlider(object sender, RangeBaseValueChangedEventArgs e) => Apply();
    void OnNumber(NumberBox sender, NumberBoxValueChangedEventArgs e) => Apply();

    void OnColorChanged(ColorPicker sender, ColorChangedEventArgs e)
    {
        if (_loading) return;
        _color = e.NewColor;
        Apply();
    }

    void OnReset(object sender, RoutedEventArgs e)
    {
        _loading = true;
        CrossBox.IsChecked = DotBox.IsChecked = true;
        CircleBox.IsChecked = false;
        _color = Palette[0];
        Picker.Color = _color;
        SizeSlider.Value = 12;
        ThickSlider.Value = 2;
        GapSlider.Value = 4;
        OpacitySlider.Value = 100;
        OutlineSwitch.IsOn = true;
        OffXBox.Value = 0;
        OffYBox.Value = 0;
        _loading = false;
        Apply();
    }
}
