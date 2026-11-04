/*
 * ahm - tiny crosshair overlay.
 *
 * One layered, click-through, topmost window holding a pre-rendered 32-bit
 * premultiplied bitmap. The image is drawn only when a setting changes, so
 * the process is completely idle (blocked in GetMessage) the rest of the time.
 * The tray icon opens the WinUI settings app on left click and offers "Quit"
 * on right click; a configurable global hotkey (default Ctrl+Alt+X) toggles the crosshair.
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
#include <math.h>
#include <wchar.h>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif
#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#define NIN_KEYSELECT (WM_USER + 1)
#endif

#define WM_TRAY (WM_APP + 1)
#define WM_RELOAD (WM_APP + 2)  /* posted by the WinUI settings app after it writes ahm.ini */
#define ID_HOTKEY 1
#define INI_SEC L"ahm"

enum { CMD_QUIT = 1 };

typedef struct {
    int shapes;         /* bit0 cross, bit1 dot, bit2 circle */
    COLORREF color;
    int size, thick, gap, opacity;
    int outline, offx, offy, visible;
    int hkMod, hkVk;    /* toggle hotkey: MOD_* flags + virtual key */
} Cfg;

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

/* ---------------------------------------------------------------- config */

static int GetIniInt(const WCHAR *key, int def)
{
    WCHAR b[32];
    if (!GetPrivateProfileStringW(INI_SEC, key, NULL, b, 32, g_ini)) return def;
    return _wtoi(b);
}

static void LoadCfg(void)
{
    int def = RGB(255, 40, 40);
    g_c.shapes = GetIniInt(L"Shapes", 3);
    g_c.color = (COLORREF)GetIniInt(L"Color", def);
    g_c.size = GetIniInt(L"Size", 12);
    g_c.thick = GetIniInt(L"Thickness", 2);
    g_c.gap = GetIniInt(L"Gap", 4);
    g_c.opacity = GetIniInt(L"Opacity", 100);
    g_c.outline = GetIniInt(L"Outline", 1);
    g_c.offx = GetIniInt(L"OffsetX", 0);
    g_c.offy = GetIniInt(L"OffsetY", 0);
    g_c.visible = GetIniInt(L"Visible", 1);
    g_c.hkMod = GetIniInt(L"HotkeyMod", MOD_CONTROL | MOD_ALT) & 0xF;
    g_c.hkVk = GetIniInt(L"HotkeyVk", 'X');

    if (g_c.hkVk < 1 || g_c.hkVk > 254) { g_c.hkMod = MOD_CONTROL | MOD_ALT; g_c.hkVk = 'X'; }
    if (g_c.shapes < 1 || g_c.shapes > 7) g_c.shapes = 3;
    if (g_c.size < 2) g_c.size = 2; else if (g_c.size > 200) g_c.size = 200;
    if (g_c.thick < 1) g_c.thick = 1; else if (g_c.thick > 20) g_c.thick = 20;
    if (g_c.gap < 0) g_c.gap = 0; else if (g_c.gap > 100) g_c.gap = 100;
    if (g_c.opacity < 5) g_c.opacity = 5; else if (g_c.opacity > 100) g_c.opacity = 100;
    g_c.color &= 0xFFFFFF;
}

/* The hotkey is the only thing the overlay changes itself; everything else
 * is owned by the settings app. */
static void SaveVisible(void)
{
    WCHAR b[4];
    wsprintfW(b, L"%d", g_c.visible);
    WritePrivateProfileStringW(INI_SEC, L"Visible", b, g_ini);
}

/* (Re)registers the toggle hotkey when it changed and tells the settings app
 * whether Windows accepted it (HotkeyError=1 when another app owns it). */
static void RegisterHk(void)
{
    static int curMod = -1, curVk = -1, lastOk = 0;
    int ok;
    if (g_c.hkMod == curMod && g_c.hkVk == curVk && lastOk) return;
    UnregisterHotKey(g_wnd, ID_HOTKEY);
    ok = RegisterHotKey(g_wnd, ID_HOTKEY, g_c.hkMod | MOD_NOREPEAT, g_c.hkVk);
    curMod = g_c.hkMod;
    curVk = g_c.hkVk;
    lastOk = ok;
    WritePrivateProfileStringW(INI_SEC, L"HotkeyError", ok ? L"0" : L"1", g_ini);
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
    int maxR = g_c.size;
    int dotR = (int)(g_c.thick * 0.75f + 2.0f);
    int W;
    POINT pt, src = { 0, 0 };
    SIZE sz;
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 0, AC_SRC_ALPHA };

    if (dotR > maxR) maxR = dotR;
    W = 2 * (maxR + 3) + 1;

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
    int pitch = ((n + 31) / 32) * 4;
    BYTE *mb = (BYTE *)calloc(1, pitch * n);

    bi.bmiHeader.biWidth = n;
    bi.bmiHeader.biHeight = -n;
    color = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    mask = CreateBitmap(n, n, 1, 1, mb);
    free(mb);

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

/* Right click: a single "Quit" entry. All configuration lives in the settings app. */
static void ShowMenu(void)
{
    HMENU m = CreatePopupMenu();
    POINT p;

    AppendMenuW(m, MF_STRING, CMD_QUIT, L"Quit");
    GetCursorPos(&p);
    SetForegroundWindow(g_wnd);
    if (TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RETURNCMD, p.x, p.y, 0, g_wnd, NULL) == CMD_QUIT)
        DestroyWindow(g_wnd);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(m);
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

static void Toggle(void)
{
    g_c.visible ^= 1;
    SaveVisible();
    Rebuild();
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            OpenSettings();
            break;
        case WM_CONTEXTMENU:
            ShowMenu();
            break;
        }
        return 0;
    case WM_HOTKEY:
        Toggle();
        return 0;
    case WM_RELOAD:
        LoadCfg();
        RegisterHk();
        TrayRefreshIcon();
        Rebuild();
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
    typedef BOOL (WINAPI *SetDpiContextFn)(void *);
    HMODULE user32;
    SetDpiContextFn setDpi;
    (void)prev; (void)cmd; (void)show;

    mutex = CreateMutexW(NULL, TRUE, L"Local\\ahm.single.instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    user32 = GetModuleHandleW(L"user32.dll");
    setDpi = user32 ? (SetDpiContextFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext") : NULL;
    if (setDpi) setDpi((void *)(LONG_PTR)-4); /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
    else SetProcessDPIAware();

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
    RegisterHk();
    Rebuild();

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_icon) DestroyIcon(g_icon);
    if (g_bmp) { SelectObject(g_mem, g_oldbmp); DeleteObject(g_bmp); }
    DeleteDC(g_mem);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return (int)msg.wParam;
}
