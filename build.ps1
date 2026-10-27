# PowerShell build script for Hardware Taskbar Monitor
$ErrorActionPreference = "Stop"

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " Building Hardware Taskbar Monitor (Windows 11 x64 Release)" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found at $vswhere"
    exit 1
}

$vsPath = & $vswhere -latest -property installationPath
if (-not $vsPath) {
    Write-Error "Visual Studio installation not found."
    exit 1
}
Write-Host "Found Visual Studio at: $vsPath" -ForegroundColor Green

$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    Write-Error "Cannot locate vcvars64.bat at $vcvars"
    exit 1
}

$cmakeExe = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmakeExe)) {
    $cmakeExe = "cmake"
}

Write-Host "`n[1/2] Configuring CMake..." -ForegroundColor Yellow
cmd.exe /c "call `"$vcvars`" >nul && `"$cmakeExe`" -B build -A x64"
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed."
    exit $LASTEXITCODE
}

Write-Host "`n[2/2] Compiling Release Build..." -ForegroundColor Yellow
cmd.exe /c "call `"$vcvars`" >nul && `"$cmakeExe`" --build build --config Release"
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed."
    exit $LASTEXITCODE
}

Write-Host "`n======================================================================" -ForegroundColor Cyan
Write-Host " Build Succeeded!" -ForegroundColor Green
Write-Host " Executable: build\Release\HardwareTaskbar.exe" -ForegroundColor Green
Write-Host "======================================================================" -ForegroundColor Cyan
