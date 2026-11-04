# ahm

Tiny crosshair overlay for Windows, shipped as one product: a single installer containing the overlay and a WinUI 3 settings app.

- **`ahm.exe`** - plain Win32 (C, no runtime deps). One layered, click-through, topmost window with a pre-rendered bitmap; it redraws only when a setting changes, so idle CPU is 0. Lives in the tray: left click toggles, right click opens the quick menu. Global hotkey `Ctrl+Alt+X`.
- **`ahm-settings.exe`** - WinUI 3 (Mica, Fluent controls). Opened from the tray's *Settings...*, edits `ahm.ini` and the overlay updates live. It exits when closed, so the XAML runtime never sits in memory.

Games must be windowed or borderless; exclusive fullscreen hides overlays.

## Install

Download `ahm-setup-<version>.exe` from Releases. Per-user install, no admin rights.

## Build

Needs MSVC (or MinGW), the .NET 8 SDK and, for the installer, [Inno Setup](https://jrsoftware.org/isinfo.php).

```
build.bat
iscc /DAppVersion=1.0.0 installer\ahm.iss
```

Output: `dist\` (both exes) and `dist-installer\ahm-setup-1.0.0.exe`. Pushing a `v*` tag builds and publishes the installer through GitHub Actions.
