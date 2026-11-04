/*
 * ahm - tiny crosshair overlay.
 *
 * One layered, click-through, topmost window holding a pre-rendered 32-bit
 * premultiplied bitmap. The image is drawn only when a setting changes, so
 * the process is completely idle (blocked in GetMessage) the rest of the time.
 * Control lives in the notification-area icon (left click toggles, right
 * click opens the menu) and the global hotkey Ctrl+Alt+X.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <math.h>
#include <wchar.h>

#define WM_TRAY (WM_APP + 1)
#define WM_RELOAD (WM_APP + 2)  /* posted by the WinUI settings app after it writes ahm.ini */
#define ID_HOTKEY 1
#define INI_SEC L"ahm"

enum {
    CMD_TOGGLE = 1,
    CMD_SETTINGS = 2,
    CMD_SHAPE = 10,     /* 10..12  cross, dot, circle */
    CMD_COLOR = 20,     /* 20 + palette index */
    CMD_CUSTOM = 40,
    CMD_OUTLINE = 50,
    CMD_AUTOSTART = 60,
    CMD_EXIT = 99,
    CMD_SIZE = 100,
    CMD_THICK = 200,
    CMD_GAP = 300,
    CMD_OPACITY = 400
};

typedef struct {
    int shapes;         /* bit0 cross, bit1 dot, bit2 circle */
    COLORREF color;
    int size, thick, gap, opacity;
    int outline, offx, offy, visible;
} Cfg;

static const struct { const WCHAR *name; COLORREF c; } PAL[] = {
    { L"Red", RGB(255, 40, 40) },     { L"Green", RGB(40, 255, 60) },
    { L"Cyan", RGB(0, 230, 255) },    { L"Yellow", RGB(255, 235, 0) },
    { L"Magenta", RGB(255, 0, 220) }, { L"Orange", RGB(255, 140, 0) },
    { L"White", RGB(255, 255, 255) },
};
#define NPAL ((int)(sizeof PAL / sizeof PAL[0]))

static const int SIZES[] = { 4, 6, 8, 10, 12, 16, 20, 24, 32, 48 };
static const int THICKS[] = { 1, 2, 3, 4, 5, 6 };
static const int GAPS[] = { 0, 2, 4, 6, 8, 12, 16 };
static const int OPACS[] = { 25, 50, 75, 100 };
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static Cfg g_c;
static HWND g_wnd;
static HDC g_mem;
static HBITMAP g_bmp, g_oldbmp;
static DWORD *g_bits;
static int g_w;
static HICON g_icon;
static NOTIFYICONDATAW g_nid;
static UINT g_taskbarMsg;
static WCHAR g_ini[MAX_PATH];
static WCHAR g_exe[MAX_PATH];
static COLORREF g_cust[16];

/* ---------------------------------------------------------------- config */

static void LoadCfg(void)
{
    int def = RGB(255, 40, 40);
    g_c.shapes = GetPrivateProfileIntW(INI_SEC, L"Shapes", 3, g_ini);
    g_c.color = (COLORREF)GetPrivateProfileIntW(INI_SEC, L"Color", def, g_ini);
    g_c.size = GetPrivateProfileIntW(INI_SEC, L"Size", 12, g_ini);
    g_c.thick = GetPrivateProfileIntW(INI_SEC, L"Thickness", 2, g_ini);
    g_c.gap = GetPrivateProfileIntW(INI_SEC, L"Gap", 4, g_ini);
    g_c.opacity = GetPrivateProfileIntW(INI_SEC, L"Opacity", 100, g_ini);
    g_c.outline = GetPrivateProfileIntW(INI_SEC, L"Outline", 1, g_ini);
    g_c.offx = (int)GetPrivateProfileIntW(INI_SEC, L"OffsetX", 0, g_ini);
    g_c.offy = (int)GetPrivateProfileIntW(INI_SEC, L"OffsetY", 0, g_ini);
    g_c.visible = GetPrivateProfileIntW(INI_SEC, L"Visible", 1, g_ini);

    if (g_c.shapes < 1 || g_c.shapes > 7) g_c.shapes = 3;
    if (g_c.size < 2) g_c.size = 2; else if (g_c.size > 200) g_c.size = 200;
    if (g_c.thick < 1) g_c.thick = 1; else if (g_c.thick > 20) g_c.thick = 20;
    if (g_c.gap < 0) g_c.gap = 0; else if (g_c.gap > 100) g_c.gap = 100;
    if (g_c.opacity < 5) g_c.opacity = 5; else if (g_c.opacity > 100) g_c.opacity = 100;
    g_c.color &= 0xFFFFFF;
}

static void PutInt(const WCHAR *key, int v)
{
    WCHAR b[16];
    wsprintfW(b, L"%d", v);
    WritePrivateProfileStringW(INI_SEC, key, b, g_ini);
}

static void SaveCfg(void)
{
    PutInt(L"Shapes", g_c.shapes);
    PutInt(L"Color", (int)g_c.color);
    PutInt(L"Size", g_c.size);
    PutInt(L"Thickness", g_c.thick);
    PutInt(L"Gap", g_c.gap);
    PutInt(L"Opacity", g_c.opacity);
    PutInt(L"Outline", g_c.outline);
    PutInt(L"OffsetX", g_c.offx);
    PutInt(L"OffsetY", g_c.offy);
    PutInt(L"Visible", g_c.visible);
}

/* ------------------------------------------------------------- autostart */

#define RUNKEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

static int AutostartOn(void)
{
    HKEY k;
    LONG r;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, KEY_QUERY_VALUE, &k)) return 0;
    r = RegQueryValueExW(k, L"ahm", NULL, NULL, NULL, NULL);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static void SetAutostart(int on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, KEY_SET_VALUE, &k)) return;
    if (on) {
        WCHAR v[MAX_PATH + 2];
        int n = wsprintfW(v, L"\"%s\"", g_exe);
        RegSetValueExW(k, L"ahm", 0, REG_SZ, (const BYTE *)v, (DWORD)((n + 1) * sizeof(WCHAR)));
    } else {
        RegDeleteValueW(k, L"ahm");
    }
    RegCloseKey(k);
}

/* ------------------------------------------------------------- rendering */

static float Sat(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

/* Coverage 0..1 of the crosshair at offset (x,y) from the centre pixel.
 * `grow` inflates every shape by that many pixels (used for the outline). */
static float CoverAt(int x, int y, int grow)
{
    int t = g_c.thick, S = g_c.size, G = g_c.gap;
    int lo = -(t / 2), hi = lo + t - 1;
    float best = 0.f, d = 0.f;

    if (g_c.shapes & 1) {
        int inH = y >= lo - grow && y <= hi + grow;
        int inV = x >= lo - grow && x <= hi + grow;
        int hx, vy;
        if (G == 0) {
            hx = x >= -S - grow && x <= S + grow;
            vy = y >= -S - grow && y <= S + grow;
        } else {
            hx = (x >= hi + 1 + G - grow && x <= S + grow) || (x <= lo - 1 - G + grow && x >= -S - grow);
            vy = (y >= hi + 1 + G - grow && y <= S + grow) || (y <= lo - 1 - G + grow && y >= -S - grow);
        }
        if ((inH && hx) || (inV && vy)) return 1.f;
    }
    if (g_c.shapes & 6) d = sqrtf((float)(x * x + y * y));
    if (g_c.shapes & 2) {
        float R = t < 2 ? 1.5f : t * 0.75f + 0.5f;
        float c = Sat(0.5f + R + grow - d);
        if (c > best) best = c;
    }
    if (g_c.shapes & 4) {
        float r = S - t * 0.5f;
        float c = Sat(0.5f + t * 0.5f + grow - fabsf(d - r));
        if (c > best) best = c;
    }
    return best;
}

static void Render(void)
{
    int H = g_w / 2, px, py;
    float R = GetRValue(g_c.color), G = GetGValue(g_c.color), B = GetBValue(g_c.color);

    for (py = 0; py < g_w; py++) {
        for (px = 0; px < g_w; px++) {
            int x = px - H, y = py - H;
            float m = CoverAt(x, y, 0);
            float o = g_c.outline ? CoverAt(x, y, 1) : 0.f;
            float a = m + o * (1.f - m);
            g_bits[py * g_w + px] =
                ((DWORD)(a * 255.f + 0.5f) << 24) |
                ((DWORD)(R * m + 0.5f) << 16) |
                ((DWORD)(G * m + 0.5f) << 8) |
                (DWORD)(B * m + 0.5f);
        }
    }
}

static void Rebuild(void)
{
    int W = 2 * (g_c.size + 3) + 1;
    POINT pt, src = { 0, 0 };
    SIZE sz;
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 0, AC_SRC_ALPHA };

    if (W != g_w) {
        BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), 0, 0, 1, 32, BI_RGB } };
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -W;
        if (g_bmp) {
            SelectObject(g_mem, g_oldbmp);
            DeleteObject(g_bmp);
        }
        g_bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&g_bits, NULL, 0);
        g_oldbmp = (HBITMAP)SelectObject(g_mem, g_bmp);
        g_w = W;
    }
    Render();

    pt.x = GetSystemMetrics(SM_CXSCREEN) / 2 - W / 2 + g_c.offx;
    pt.y = GetSystemMetrics(SM_CYSCREEN) / 2 - W / 2 + g_c.offy;
    sz.cx = sz.cy = W;
    bf.SourceConstantAlpha = (BYTE)(g_c.opacity * 255 / 100);
    UpdateLayeredWindow(g_wnd, NULL, &pt, &sz, g_mem, &src, 0, &bf, ULW_ALPHA);

    if (g_c.visible) {
        SetWindowPos(g_wnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        ShowWindow(g_wnd, SW_HIDE);
    }
}

/* ------------------------------------------------------------------ tray */

static HICON MakeIcon(COLORREF col)
{
    int n = GetSystemMetrics(SM_CXSMICON), c = n / 2, x, y;
    int tt = n >= 24 ? 3 : 2, gap = n / 8 + 1;
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), 0, 0, 1, 32, BI_RGB } };
    DWORD *px;
    HBITMAP color, mask;
    ICONINFO ii;
    HICON icon;
    DWORD fg = 0xFF000000u | (GetRValue(col) << 16) | (GetGValue(col) << 8) | GetBValue(col);

    bi.bmiHeader.biWidth = n;
    bi.bmiHeader.biHeight = -n;
    color = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    mask = CreateBitmap(n, n, 1, 1, NULL);

    for (y = 0; y < n; y++) {
        for (x = 0; x < n; x++) {
            int dx = x - c, dy = y - c, k, on = 0, edge = 0;
            for (k = 0; k < 9; k++) {            /* 3x3 neighbourhood for outline */
                int ex = dx + k % 3 - 1, ey = dy + k / 3 - 1;
                int ax = ex < 0 ? -ex : ex, ay = ey < 0 ? -ey : ey;
                int hit = ((ay < tt / 2 + (tt & 1) && ax >= gap) ||
                           (ax < tt / 2 + (tt & 1) && ay >= gap)) && ax < n / 2 - 1 && ay < n / 2 - 1;
                if (k == 4) on = hit; else if (hit) edge = 1;
            }
            px[y * n + x] = on ? fg : edge ? 0xFF000000u : 0;
        }
    }
    ii.fIcon = TRUE;
    ii.xHotspot = ii.yHotspot = 0;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

static void TrayAdd(void)
{
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_wnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_icon;
    lstrcpyW(g_nid.szTip, L"ahm crosshair");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

static void TrayRefreshIcon(void)
{
    HICON old = g_icon;
    g_icon = MakeIcon(g_c.color);
    g_nid.hIcon = g_icon;
    g_nid.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    if (old) DestroyIcon(old);
}

static HMENU NumMenu(int base, const int *v, int n, int cur, const WCHAR *unit)
{
    HMENU m = CreatePopupMenu();
    int i;
    for (i = 0; i < n; i++) {
        WCHAR b[24];
        wsprintfW(b, L"%d%s", v[i], unit);
        AppendMenuW(m, MF_STRING | (v[i] == cur ? MF_CHECKED : 0), base + i, b);
    }
    return m;
}

static void ShowMenu(void)
{
    HMENU m = CreatePopupMenu(), shape = CreatePopupMenu(), col = CreatePopupMenu();
    POINT p;
    int i, custom = 1;

    AppendMenuW(shape, MF_STRING | ((g_c.shapes & 1) ? MF_CHECKED : 0), CMD_SHAPE, L"Cross");
    AppendMenuW(shape, MF_STRING | ((g_c.shapes & 2) ? MF_CHECKED : 0), CMD_SHAPE + 1, L"Dot");
    AppendMenuW(shape, MF_STRING | ((g_c.shapes & 4) ? MF_CHECKED : 0), CMD_SHAPE + 2, L"Circle");

    for (i = 0; i < NPAL; i++) {
        int on = PAL[i].c == g_c.color;
        custom &= !on;
        AppendMenuW(col, MF_STRING | (on ? MF_CHECKED : 0), CMD_COLOR + i, PAL[i].name);
    }
    AppendMenuW(col, MF_STRING | (custom ? MF_CHECKED : 0), CMD_CUSTOM, L"Custom...");

    AppendMenuW(m, MF_STRING | (g_c.visible ? MF_CHECKED : 0), CMD_TOGGLE, L"Show\tCtrl+Alt+X");
    AppendMenuW(m, MF_STRING, CMD_SETTINGS, L"Settings...");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)shape, L"Shape");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)col, L"Color");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)NumMenu(CMD_SIZE, SIZES, COUNT(SIZES), g_c.size, L" px"), L"Size");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)NumMenu(CMD_THICK, THICKS, COUNT(THICKS), g_c.thick, L" px"), L"Thickness");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)NumMenu(CMD_GAP, GAPS, COUNT(GAPS), g_c.gap, L" px"), L"Gap");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)NumMenu(CMD_OPACITY, OPACS, COUNT(OPACS), g_c.opacity, L"%"), L"Opacity");
    AppendMenuW(m, MF_STRING | (g_c.outline ? MF_CHECKED : 0), CMD_OUTLINE, L"Outline");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (AutostartOn() ? MF_CHECKED : 0), CMD_AUTOSTART, L"Start with Windows");
    AppendMenuW(m, MF_STRING, CMD_EXIT, L"Exit");

    GetCursorPos(&p);
    SetForegroundWindow(g_wnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, g_wnd, NULL);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(m);     /* destroys the submenus too */
}

/* -------------------------------------------------------------- commands */

/* The settings UI is a separate WinUI 3 process, started on demand and gone
 * when closed, so the resident overlay never pays for the XAML runtime. */
static void OpenSettings(void)
{
    WCHAR path[MAX_PATH], dir[MAX_PATH];
    WCHAR *slash;
    HWND open = FindWindowW(NULL, L"ahm settings");
    if (open) {
        ShowWindow(open, SW_RESTORE);
        SetForegroundWindow(open);
        return;
    }
    lstrcpyW(dir, g_exe);
    slash = wcsrchr(dir, L'\\');
    if (slash) *slash = 0;
    wsprintfW(path, L"%s\\ahm-settings.exe", dir);
    ShellExecuteW(NULL, L"open", path, NULL, dir, SW_SHOWNORMAL);
}

static void OnCommand(int id)
{
    if (id == CMD_EXIT) {
        DestroyWindow(g_wnd);
        return;
    }
    if (id == CMD_SETTINGS) {
        OpenSettings();
        return;
    }
    if (id == CMD_TOGGLE) {
        g_c.visible ^= 1;
    } else if (id >= CMD_SHAPE && id < CMD_SHAPE + 3) {
        int s = g_c.shapes ^ (1 << (id - CMD_SHAPE));
        if (s) g_c.shapes = s;
    } else if (id >= CMD_COLOR && id < CMD_COLOR + NPAL) {
        g_c.color = PAL[id - CMD_COLOR].c;
    } else if (id == CMD_CUSTOM) {
        CHOOSECOLORW cc = { sizeof cc };
        cc.hwndOwner = g_wnd;
        cc.rgbResult = g_c.color;
        cc.lpCustColors = g_cust;
        cc.Flags = CC_RGBINIT | CC_FULLOPEN;
        if (!ChooseColorW(&cc)) return;
        g_c.color = cc.rgbResult & 0xFFFFFF;
    } else if (id == CMD_OUTLINE) {
        g_c.outline ^= 1;
    } else if (id == CMD_AUTOSTART) {
        SetAutostart(!AutostartOn());
        return;
    } else if (id >= CMD_SIZE && id < CMD_SIZE + COUNT(SIZES)) {
        g_c.size = SIZES[id - CMD_SIZE];
    } else if (id >= CMD_THICK && id < CMD_THICK + COUNT(THICKS)) {
        g_c.thick = THICKS[id - CMD_THICK];
    } else if (id >= CMD_GAP && id < CMD_GAP + COUNT(GAPS)) {
        g_c.gap = GAPS[id - CMD_GAP];
    } else if (id >= CMD_OPACITY && id < CMD_OPACITY + COUNT(OPACS)) {
        g_c.opacity = OPACS[id - CMD_OPACITY];
    } else {
        return;
    }
    SaveCfg();
    TrayRefreshIcon();
    Rebuild();
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP: OnCommand(CMD_TOGGLE); break;
        case WM_CONTEXTMENU: ShowMenu(); break;
        }
        return 0;
    case WM_HOTKEY:
        OnCommand(CMD_TOGGLE);
        return 0;
    case WM_RELOAD:
        LoadCfg();
        TrayRefreshIcon();
        Rebuild();
        return 0;
    case WM_COMMAND:
        OnCommand(LOWORD(wp));
        return 0;
    case WM_DISPLAYCHANGE:
        Rebuild();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_DESTROY:
        UnregisterHotKey(h, ID_HOTKEY);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    default:
        if (msg == g_taskbarMsg && msg) {       /* explorer restarted */
            TrayAdd();
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSW wc = { 0 };
    MSG msg;
    WCHAR *slash;
    HANDLE mutex;
    (void)prev; (void)cmd; (void)show;

    mutex = CreateMutexW(NULL, TRUE, L"Local\\ahm.single.instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    SetProcessDPIAware();

    GetModuleFileNameW(NULL, g_exe, MAX_PATH);
    lstrcpyW(g_ini, g_exe);
    slash = wcsrchr(g_ini, L'\\');
    lstrcpyW(slash ? slash + 1 : g_ini, L"ahm.ini");
    LoadCfg();

    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"ahm_overlay";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            wc.lpszClassName, L"ahm", WS_POPUP, 0, 0, 1, 1,
                            NULL, NULL, inst, NULL);
    if (!g_wnd) return 1;

    g_mem = CreateCompatibleDC(NULL);
    g_taskbarMsg = RegisterWindowMessageW(L"TaskbarCreated");
    g_icon = MakeIcon(g_c.color);
    TrayAdd();
    RegisterHotKey(g_wnd, ID_HOTKEY, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'X');
    Rebuild();

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_icon) DestroyIcon(g_icon);
    if (g_bmp) { SelectObject(g_mem, g_oldbmp); DeleteObject(g_bmp); }
    DeleteDC(g_mem);
    ReleaseMutex(mutex);
    return (int)msg.wParam;
}
