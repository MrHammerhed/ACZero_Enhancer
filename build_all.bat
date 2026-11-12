@echo off
setlocal enabledelayedexpansion
echo ============================================================================
echo  ACZero Enhancer (v0.23.5) — Visual Studio 2026 / 2022 One-Click Build Script
echo  Targets: enhancer.dll (v0.23.5), acz_hdtex.dll (v0.23.5), memdump.dll (v0.16.3)
echo ============================================================================

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
    )
)

if "%VCVARS%"=="" (
    for %%V in (2026 18 2022) do (
        for %%E in (Community Professional Enterprise BuildTools) do (
            if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" (
                set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
            )
        )
    )
)

if "%VCVARS%"=="" (
    echo [ERROR] Could not locate Visual Studio 2026 / 2022 C++ Build Tools ^(vcvars64.bat^).
    pause
    exit /b 1
)

echo [*] Initializing x64 C++ Build Environment: "%VCVARS%"
call "%VCVARS%" >nul
if not exist "%~dp0build" mkdir "%~dp0build"
if not exist "%~dp0mods" mkdir "%~dp0mods"

if exist "%~dp0enhancer.cpp" (
    echo [*] Building enhancer.dll ^(v0.23.5^)...
    cl.exe /nologo /O2 /Oi /GL /EHsc /std:c++17 /utf-8 /LD "%~dp0enhancer.cpp" /Fe:"%~dp0build\enhancer.dll" /Fo:"%~dp0build\\" user32.lib /link /OPT:REF /OPT:ICF
    if errorlevel 1 goto :err
    copy /y "%~dp0build\enhancer.dll" "%~dp0mods\enhancer.dll" >nul
)

if exist "%~dp0memdump.cpp" (
    echo [*] Building memdump.dll ^(v0.16.3^)...
    cl.exe /nologo /O2 /Oi /GL /EHsc /std:c++17 /utf-8 /LD "%~dp0memdump.cpp" /Fe:"%~dp0build\memdump.dll" /Fo:"%~dp0build\\" user32.lib ole32.lib windowscodecs.lib psapi.lib /link /OPT:REF /OPT:ICF
    if errorlevel 1 goto :err
    copy /y "%~dp0build\memdump.dll" "%~dp0mods\memdump.dll" >nul
)

if exist "%~dp0acz_hdtex.cpp" (
    echo [*] Building acz_hdtex.dll ^(v0.23.5^)...
    cl.exe /nologo /O2 /Oi /GL /EHsc /std:c++17 /utf-8 /LD "%~dp0acz_hdtex.cpp" /Fe:"%~dp0build\acz_hdtex.dll" /Fo:"%~dp0build\\" user32.lib ole32.lib windowscodecs.lib /link /OPT:REF /OPT:ICF
    if errorlevel 1 goto :err
    copy /y "%~dp0build\acz_hdtex.dll" "%~dp0mods\acz_hdtex.dll" >nul
)

echo ============================================================================
echo  [SUCCESS] All available ACZero Enhancer DLL targets built and copied to mods\!
echo ============================================================================
pause
exit /b 0

:err
echo [ERROR] Build failed!
pause
exit /b 1
