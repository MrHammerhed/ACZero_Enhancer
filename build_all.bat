@echo off
setlocal enabledelayedexpansion
echo ============================================================================
echo  ACZero Enhancer (v0.23.6) — Visual Studio 2026 / 2022 One-Click Build Script
echo  Targets: version.dll (v0.1), enhancer.asi (v0.23.6), acz_hdtex.asi (v0.23.6), memdump.asi (v0.16.5)
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
if not exist "%~dp0mods\enhancer" mkdir "%~dp0mods\enhancer"

if exist "%~dp0version_loader.cpp" (
    echo [*] Building version.dll ^(v0.1^)...
    cl.exe /nologo /O2 /Oi /GL /EHsc /std:c++17 /utf-8 /LD "%~dp0version_loader.cpp" /Fe:"%~dp0build\version.dll" /Fo:"%~dp0build\\" user32.lib /link /OPT:REF /OPT:ICF
    if errorlevel 1 goto :err
    copy /y "%~dp0build\version.dll" "%~dp0version.dll" >nul
)

if exist "%~dp0enhancer.cpp" (
    echo [*] Building enhancer.asi ^(v0.23.6^)...
    cl.exe /nologo /O2 /Oi /GL /EHsc /std:c++17 /utf-8 /LD "%~dp0enhancer.cpp" /Fe:"%~dp0build\enhancer.asi" /Fo:"%~dp0build\\" user32.lib /link /OPT:REF /OPT:ICF
    if errorlevel 1 goto :err
    copy /y "%~dp0build\enhancer.asi" "%~dp0mods\enhancer\enhancer.asi" >nul
)

if exist "%~dp0memdump.cpp" (
    echo [*] Building memdump.asi ^(v0.16.5^)...
    cl.exe /nologo /O2 /Oi /GL /EHsc /std:c++17 /utf-8 /LD "%~dp0memdump.cpp" /Fe:"%~dp0build\memdump.asi" /Fo:"%~dp0build\\" user32.lib ole32.lib windowscodecs.lib psapi.lib /link /OPT:REF /OPT:ICF
    if errorlevel 1 goto :err
    copy /y "%~dp0build\memdump.asi" "%~dp0mods\enhancer\memdump.asi" >nul
)

if exist "%~dp0acz_hdtex.cpp" (
    echo [*] Building acz_hdtex.asi ^(v0.23.6^)...
    cl.exe /nologo /O2 /Oi /GL /EHsc /std:c++17 /utf-8 /LD "%~dp0acz_hdtex.cpp" /Fe:"%~dp0build\acz_hdtex.asi" /Fo:"%~dp0build\\" user32.lib ole32.lib windowscodecs.lib /link /OPT:REF /OPT:ICF
    if errorlevel 1 goto :err
    copy /y "%~dp0build\acz_hdtex.asi" "%~dp0mods\enhancer\acz_hdtex.asi" >nul
)

echo ============================================================================
echo  [SUCCESS] Built version.dll and all mods\enhancer\*.asi targets!
echo ============================================================================
pause
exit /b 0

:err
echo [ERROR] Build failed!
pause
exit /b 1
