# ahm

Low-resource crosshair overlay for Windows. One installer ships two executables:

| Component | Stack | Behaviour |
|---|---|---|
| `ahm.exe` | Win32, C, no runtime dependencies | A single layered, click-through, topmost window showing a pre-rendered bitmap. It redraws only when a setting changes, so idle CPU use is zero. Runs in the tray (left click opens settings, right click offers *Quit*). A configurable global hotkey (default `Ctrl+Alt+X`) toggles the crosshair. |
| `ahm-settings.exe` | WinUI 3 (Mica, Fluent controls) | The only settings screen, including *Start with Windows*. Edits `ahm.ini` and the overlay updates live. It exits when closed, so the XAML runtime does not stay in memory. |

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
