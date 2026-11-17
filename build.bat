@echo off
rem Builds dist\ahm.exe (C, MSVC or MinGW) and dist\ahm-settings.exe (WinUI 3, needs the .NET 8 SDK).
setlocal
cd /d "%~dp0"
if not exist dist mkdir dist

where cl >nul 2>nul
if not errorlevel 1 goto :havecl
set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSW%" goto :havecl
"%VSW%" -latest -products * -property installationPath > "%TEMP%\ahm-vsp.txt"
set /p VSP=<"%TEMP%\ahm-vsp.txt"
del "%TEMP%\ahm-vsp.txt"
if not defined VSP goto :havecl
if exist "%VSP%\VC\Auxiliary\Build\vcvars64.bat" call "%VSP%\VC\Auxiliary\Build\vcvars64.bat" >nul
:havecl

where cl >nul 2>nul
if not errorlevel 1 (
    cl /nologo /O2 /Os /GS- /W3 /MT ahm.c /Fo:dist\ /Fe:dist\ahm.exe /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF user32.lib gdi32.lib shell32.lib || exit /b 1
) else (
    where gcc >nul 2>nul
    if errorlevel 1 (
        echo No C compiler found. Install Visual Studio Build Tools ^(C++ workload^) or MinGW-w64.
        exit /b 1
    )
    gcc -Os -s -mwindows ahm.c -o dist\ahm.exe -luser32 -lgdi32 -lshell32 || exit /b 1
)

where dotnet >nul 2>nul
if errorlevel 1 (
    echo dotnet SDK not found; skipping WinUI settings app.
    exit /b 0
)
dotnet publish settings\ahm-settings.csproj -c Release -r win-x64 -p:Platform=x64 -o dist || exit /b 1
