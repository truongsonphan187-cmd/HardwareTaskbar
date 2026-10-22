@echo off
setlocal enabledelayedexpansion

echo ======================================================================
echo  Building Hardware Taskbar Monitor (Windows 11 x64 Release)
echo ======================================================================

set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo [ERROR] Visual Studio installation not found via vswhere.
    pause
    exit /b 1
)

echo Found Visual Studio at: !VS_PATH!

set "VCVARS=!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat"
if not exist "!VCVARS!" (
    echo [ERROR] Cannot locate vcvars64.bat at !VCVARS!
    pause
    exit /b 1
)

call "!VCVARS!"

set "CMAKE_EXE=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "!CMAKE_EXE!" (
    where cmake >nul 2>nul
    if %ERRORLEVEL% equ 0 (
        set "CMAKE_EXE=cmake"
    ) else (
        echo [ERROR] CMake not found.
        pause
        exit /b 1
    )
)

echo.
echo [1/2] Configuring CMake...
"!CMAKE_EXE!" -B build -A x64
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [2/2] Compiling Release Build...
"!CMAKE_EXE!" --build build --config Release
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ======================================================================
echo  Build Succeeded!
echo  Executable: build\Release\HardwareTaskbar.exe
echo ======================================================================
endlocal
