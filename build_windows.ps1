# ModbusHub - Windows Build Script
# Usage: .\build_windows.ps1 [clean|build|run|all|rebuild]

param(
    [Parameter(Position=0)]
    [ValidateSet("clean", "build", "run", "all", "rebuild", "package", "installer")]
    [string]$Action = "all"
)

# Configuration
# ---------------------------------------------------------------------------
# For fully static builds (no Qt DLL runtime required), set $UseStaticQt = $true
# and point $StaticQtPath at your static Qt installation.
#
# How to obtain a static Qt build:
#   1. Download qt-everywhere source, e.g. https://download.qt.io/official_releases/qt/
#   2. Configure: .\configure -static -release -prefix C:\Qt\6.10.2\mingw_64_static ...
#   3. cmake --build . --parallel && cmake --install .
# ---------------------------------------------------------------------------
# Paths can be overridden via environment variables so the script works on
# any developer machine without editing.  Examples:
#   $env:QT_DIR    = "D:\Qt\6.10.2\mingw_64"
#   $env:MINGW_DIR = "D:\Qt\Tools\mingw1310_64"
$UseStaticQt   = $false
$StaticQtPath  = if ($env:QT_STATIC_DIR) { $env:QT_STATIC_DIR } else { "C:\Qt\6.10.2\mingw_64_static" }
$DynamicQtPath = if ($env:QT_DIR)        { $env:QT_DIR }        else { "C:\Qt\6.10.2\mingw_64" }
$QtPath        = if ($UseStaticQt) { $StaticQtPath } else { $DynamicQtPath }
$MinGW         = if ($env:MINGW_DIR)     { $env:MINGW_DIR }     else { "C:\Qt\Tools\mingw1310_64" }
$BuildDir      = "build_qt_win"
$ProjectRoot   = $PSScriptRoot
$AppName       = "modbus_master"

if (-not (Test-Path $QtPath)) {
    Write-Host "Qt not found at $QtPath" -ForegroundColor Red
    Write-Host "Set `$env:QT_DIR to your Qt install directory and re-run." -ForegroundColor Yellow
    exit 1
}
if (-not (Test-Path $MinGW)) {
    Write-Host "MinGW not found at $MinGW" -ForegroundColor Red
    Write-Host "Set `$env:MINGW_DIR to your MinGW install directory and re-run." -ForegroundColor Yellow
    exit 1
}

# Set up environment
$env:PATH = "$QtPath\bin;$MinGW\bin;$env:PATH"
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = "1"

function Write-Status($message) {
    Write-Host "`n=== $message ===" -ForegroundColor Cyan
}

function Clean {
    Write-Status "Cleaning build directory"
    if (Test-Path "$ProjectRoot\$BuildDir") {
        Remove-Item -Recurse -Force "$ProjectRoot\$BuildDir"
        Write-Host "Cleaned $BuildDir" -ForegroundColor Green
    } else {
        Write-Host "Nothing to clean" -ForegroundColor Yellow
    }
}

function Build {
    Write-Status "Building ModbusHub"
    
    # Create build directory
    if (-not (Test-Path "$ProjectRoot\$BuildDir")) {
        New-Item -ItemType Directory -Path "$ProjectRoot\$BuildDir" | Out-Null
    }
    
    Push-Location "$ProjectRoot\$BuildDir"
    
    # Configure with CMake if needed
    if (-not (Test-Path "CMakeCache.txt")) {
        Write-Host "Configuring with CMake..." -ForegroundColor Yellow
        $staticFlag = if ($UseStaticQt) { "-DUSE_STATIC_QT=ON" } else { "-DUSE_STATIC_QT=OFF" }
        cmake .. -G "MinGW Makefiles" `
            -DUSE_QT6=ON `
            $staticFlag `
            -DBUILD_SHARED_LIBS=OFF `
            -DCMAKE_PREFIX_PATH="$QtPath" `
            -DCMAKE_C_COMPILER="$MinGW\bin\gcc.exe" `
            -DCMAKE_CXX_COMPILER="$MinGW\bin\g++.exe" `
            -DCMAKE_BUILD_TYPE=Release
        
        if ($LASTEXITCODE -ne 0) {
            Write-Host "CMake configuration failed!" -ForegroundColor Red
            Pop-Location
            return $false
        }
    }
    
    # Build
    Write-Host "Building..." -ForegroundColor Yellow
    mingw32-make -j4
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build failed!" -ForegroundColor Red
        Pop-Location
        return $false
    }
    
    # Deploy Qt dependencies (only needed for dynamic builds)
    if (-not $UseStaticQt) {
        Write-Host "Deploying Qt dependencies (dynamic)..." -ForegroundColor Yellow
        & "$QtPath\bin\windeployqt6.exe" --release --compiler-runtime "$AppName.exe" 2>$null
        # Copy MinGW runtime DLLs (so target machine needs no MinGW installed)
        $mingwDlls = @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
        foreach ($dll in $mingwDlls) {
            $src = "$MinGW\bin\$dll"
            if ((Test-Path $src) -and -not (Test-Path ".\$dll")) {
                Copy-Item $src "." -Force
                Write-Host "  Copied $dll" -ForegroundColor DarkGray
            }
        }
    } else {
        Write-Host "Static build: no Qt DLLs to deploy." -ForegroundColor Cyan
    }
    
    Pop-Location
    
    Write-Host "Build successful!" -ForegroundColor Green
    return $true
}

function Package {
    Write-Status "Packaging distributable (no Qt required on target)"

    $distDir = "$ProjectRoot\dist\ModbusHub"
    $zipPath = "$ProjectRoot\dist\ModbusHub_portable.zip"

    if (Test-Path "$ProjectRoot\dist") {
        # Use cmd rd which works even when PowerShell Remove-Item hits locked files
        cmd /c "rd /s /q `"$ProjectRoot\dist`"" 2>$null
        Start-Sleep -Milliseconds 500
    }
    New-Item -ItemType Directory -Path $distDir | Out-Null

    # Copy everything from build dir except cmake/make artifacts
    Get-ChildItem "$ProjectRoot\$BuildDir" | Where-Object {
        $_.Name -notmatch "^(CMake|Makefile|cmake_install|CMakeFiles|modbus_master_autogen)"
    } | Copy-Item -Destination $distDir -Recurse -Force

    # Copy INI config if present
    if (Test-Path "$ProjectRoot\modbus_master.ini") {
        Copy-Item "$ProjectRoot\modbus_master.ini" $distDir -Force
    }

    # Zip it
    Compress-Archive -Path "$distDir\*" -DestinationPath $zipPath -Force
    $sizeMB = [math]::Round((Get-Item $zipPath).Length / 1MB, 1)
    Write-Host "Package ready: dist\ModbusHub_portable.zip  ($sizeMB MB)" -ForegroundColor Green
    Write-Host "Folder:        dist\ModbusHub\" -ForegroundColor Green
    Write-Host "Copy the folder (or zip) to any Windows machine - no Qt install needed." -ForegroundColor Cyan
}

function Installer {
    Write-Status "Building Windows installer (Inno Setup)"

    # Find iscc.exe (Inno Setup compiler) - compatible with PowerShell 5.1
    $isccCandidates = @(
        "C:\Program Files (x86)\Inno Setup 6\iscc.exe",
        "C:\Program Files\Inno Setup 6\iscc.exe"
    )
    $isccCmd = Get-Command iscc -ErrorAction SilentlyContinue
    if ($isccCmd) { $isccCandidates += $isccCmd.Source }
    $iscc = $isccCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1

    if (-not $iscc) {
        Write-Host ""
        Write-Host "Inno Setup 6 is not installed." -ForegroundColor Red
        Write-Host "Download and install it from: https://jrsoftware.org/isdl.php" -ForegroundColor Yellow
        Write-Host "Then re-run: .\build_windows.bat installer" -ForegroundColor Yellow
        return
    }

    # Make sure the project is built first
    if (-not (Test-Path "$ProjectRoot\$BuildDir\$AppName.exe")) {
        Write-Host "Executable not found - building first..." -ForegroundColor Yellow
        if (-not (Build)) { return }
    }

    # Ensure output directory exists
    if (-not (Test-Path "$ProjectRoot\dist")) {
        New-Item -ItemType Directory -Path "$ProjectRoot\dist" | Out-Null
    }

    Write-Host "Running Inno Setup compiler..." -ForegroundColor Yellow
    & $iscc "$ProjectRoot\installer.iss"

    if ($LASTEXITCODE -eq 0) {
        $setupExe = Get-ChildItem "$ProjectRoot\dist" -Filter "ModbusHub_Setup_*.exe" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        Write-Host "`nInstaller ready: dist\$($setupExe.Name)" -ForegroundColor Green
        Write-Host "Run it to install ModbusHub with Start Menu + Desktop shortcuts." -ForegroundColor Cyan
    } else {
        Write-Host "Inno Setup compilation failed!" -ForegroundColor Red
    }
}

function Run {
    Write-Status "Running ModbusHub"
    
    $exePath = "$ProjectRoot\$BuildDir\$AppName.exe"
    
    if (-not (Test-Path $exePath)) {
        Write-Host "Executable not found. Building first..." -ForegroundColor Yellow
        if (-not (Build)) { return }
    }
    
    # Kill existing instance
    Get-Process $AppName -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 300
    
    Write-Host "Starting application..." -ForegroundColor Green
    Push-Location "$ProjectRoot\$BuildDir"
    Start-Process ".\$AppName.exe"
    Pop-Location
}

# Main
Write-Host "`nModbusHub Build System (Windows)" -ForegroundColor Magenta
Write-Host "Qt: $QtPath"
Write-Host "Action: $Action`n"

switch ($Action) {
    "clean"     { Clean }
    "build"     { Build | Out-Null }
    "run"       { Run }
    "rebuild"   { Clean; Build | Out-Null }
    "package"   { if (Build) { Package } }
    "installer" { if (Build) { Installer } }
    "all"       { if (Build) { Package; Run } }
}
