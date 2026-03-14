# Build Documentation - Modbus Master (Qt)

This document provides comprehensive build instructions for the Modbus Master Qt GUI application.

## Table of Contents
- [Prerequisites](#prerequisites)
- [Build Systems](#build-systems)
- [Windows Build (QMake)](#windows-build-qmake)
- [Linux Build (CMake)](#linux-build-cmake)
- [Build Commands Reference](#build-commands-reference)
- [Troubleshooting](#troubleshooting)

---

## Prerequisites

### Windows
- **Qt 6.10.2 or later** (includes QtCharts for polling graph feature)
- **MinGW 13.1.0 or later** (bundled with Qt)
- **Git** (optional, for version control)

### Linux
- **Qt 5.15+ or Qt 6.x**
  - Qt5: `sudo apt-get install qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools`
  - Qt6: `sudo apt-get install qt6-base-dev libqt6charts6-dev`
- **CMake 3.16+**: `sudo apt-get install cmake`
- **Build tools**: `sudo apt-get install build-essential`
---

## Build Systems

This project supports two build systems:

1. **QMake** (`.pro` file) - Recommended for Windows with Qt Creator
2. **CMake** (`CMakeLists.txt`) - Recommended for Linux and cross-platform builds

---

## Windows Build (QMake)

### Method 1: Using Qt Creator (GUI)
1. Open Qt Creator
2. File → Open File or Project → Select `modbus_master.pro`
3. Configure the kit (select MinGW 64-bit with Qt 6.10.2+)
4. Click the **Build** button (hammer icon) or press `Ctrl+B`
5. Executable will be in `release/modbus_master.exe`

### Method 2: Command Line
```powershell
# Set environment (adjust paths to your Qt installation)
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.10.2\mingw_64\bin;$env:PATH"

# Clean previous build (optional)
if (Test-Path release) { Remove-Item -Recurse -Force release }
if (Test-Path debug) { Remove-Item -Recurse -Force debug }
Remove-Item -Force Makefile*, *.pro.user -ErrorAction SilentlyContinue

# Generate Makefiles
qmake

# Build
mingw32-make

# Build in parallel (faster)
mingw32-make -j4

# Run the application
.\release\modbus_master.exe
```

### Clean Build
```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.10.2\mingw_64\bin;$env:PATH"
mingw32-make clean
qmake
mingw32-make
```

---

## Linux Build (CMake)

### Method 1: Using Build Script (Recommended)
```bash
# Make the script executable
chmod +x build_linux.sh

# Run the build
./build_linux.sh

# Executable will be at: build_qt/modbus_master
```

### Method 2: Manual CMake Build
```bash
# Clean previous build (optional)
rm -rf build_qt
mkdir -p build_qt
cd build_qt

# Configure with CMake (Qt5)
cmake -DCMAKE_BUILD_TYPE=Release -DUSE_QT=ON ..

# Or configure with Qt6 (if available)
cmake -DCMAKE_BUILD_TYPE=Release -DUSE_QT=ON -DUSE_QT6=ON ..

# Build
make -j$(nproc)

# Run
./modbus_master
```

### Clean Build
```bash
cd build_qt
make clean
# Or completely remove build directory
cd ..
rm -rf build_qt
```

---

## Build Commands Reference

### QMake Commands (Windows)

| Command | Description |
|---------|-------------|
| `qmake` | Generate Makefiles from .pro file |
| `qmake -v` | Show qmake version and Qt version |
| `mingw32-make` | Build the project (Release by default) |
| `mingw32-make clean` | Remove built objects and executable |
| `mingw32-make -j4` | Build using 4 parallel jobs (faster) |
| `mingw32-make debug` | Build debug version |
| `mingw32-make release` | Build release version |

### CMake Commands (Linux)

| Command | Description |
|---------|-------------|
| `cmake ..` | Configure project from parent directory |
| `cmake -DCMAKE_BUILD_TYPE=Release ..` | Configure for Release build |
| `cmake -DUSE_QT6=ON ..` | Use Qt6 instead of Qt5 |
| `make` | Build the project |
| `make -j$(nproc)` | Build using all CPU cores |
| `make clean` | Remove built objects and executable |
| `make VERBOSE=1` | Show detailed build commands |

---

## Project File Structure

```
modbus_master/
├── src/
│   ├── common/          # Shared headers
│   │   ├── alarm_manager.h
│   │   ├── device_manager.h
│   │   ├── modbus.h
│   │   └── ...
│   ├── qt/              # Qt-specific implementation
│   │   ├── main.cpp
│   │   ├── mainwindow.cpp/h
│   │   ├── devicedialog.cpp/h
│   │   ├── alarmdialog.cpp/h
│   │   ├── pollinggraphwidget.h  # Qt6 only (requires QtCharts)
│   │   ├── alarm_manager.c
│   │   ├── modbus_simple.c       # Built-in Modbus implementation
│   │   └── backend_stubs.c
│   └── linux/           # Linux-specific backend (not used in Qt build)
├── build_qt/            # CMake build directory (Linux)
├── release/             # QMake build output (Windows)
├── modbus_master.pro    # QMake project file
├── CMakeLists.txt       # CMake project file
├── build_linux.sh       # Linux build script
└── BUILD.md             # This file
```

---

## Features by Qt Version

| Feature | Qt 5.15 | Qt 6.x |
|---------|---------|--------|
| Main GUI | ✅ | ✅ |
| Device Management | ✅ | ✅ |
| Alarm Configuration | ✅ | ✅ |
| TCP/RTU Gateway | ✅ | ✅ |
| Status Monitoring | ✅ | ✅ |
| **Polling Activity Graph** | ❌ | ✅ |

> **Note**: The Polling Activity Graph feature requires Qt6 with QtCharts module. When building with Qt5, this feature is automatically disabled.

---

## Troubleshooting

### Windows Issues

**Problem**: `qmake: command not found`
```powershell
# Solution: Add Qt to PATH
$env:PATH = "C:\Qt\6.10.2\mingw_64\bin;$env:PATH"
```

**Problem**: `mingw32-make: command not found`
```powershell
# Solution: Add MinGW to PATH
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;$env:PATH"
```

**Problem**: `Qt requires a C++17 compiler`
```powershell
# Solution: Ensure you're using MinGW 11+ from Qt's Tools folder
# Old system MinGW won't work with Qt 6.x
```

**Problem**: Missing DLLs when running
```powershell
# Solution: Use windeployqt to copy Qt DLLs
cd release
C:\Qt\6.10.2\mingw_64\bin\windeployqt.exe modbus_master.exe
```

### Linux Issues

**Problem**: `QtCharts: No such file or directory`
```bash
# This is expected with Qt5 - the polling graph feature requires Qt6
# Either:
# 1. Install Qt6 and rebuild with -DUSE_QT6=ON
# 2. Continue with Qt5 (polling graph will be disabled)
sudo apt-get install qt6-base-dev libqt6charts6-dev
```

**Problem**: `Qt5 not found`
```bash
# Solution: Install Qt5
sudo apt-get install qtbase5-dev
```

**Problem**: CMake version too old
```bash
# Solution: Update CMake from official repository
sudo apt-get install cmake
```

---

## Deployment

### Windows Deployment
```powershell
cd release

# Copy all required Qt DLLs
C:\Qt\6.10.2\mingw_64\bin\windeployqt.exe modbus_master.exe

# Create a zip for distribution
Compress-Archive -Path modbus_master.exe,*.dll -DestinationPath modbus_master_windows.zip
```

### Linux Deployment
```bash
cd build_qt

# Show required libraries
ldd modbus_master

# Create AppImage or package for distribution
# (See AppImage documentation for details)
```

---

## Build Variants

### Debug Build (more logging, no optimization)
**QMake (Windows)**:
```powershell
qmake CONFIG+=debug
mingw32-make
# Output: debug/modbus_master.exe
```

**CMake (Linux)**:
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

### Release Build (optimized, smaller binary)
**QMake (Windows)**:
```powershell
qmake CONFIG+=release  # or just qmake (Release is default)
mingw32-make
# Output: release/modbus_master.exe
```

**CMake (Linux)**:
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

---

## Environment Setup Scripts

### Windows (PowerShell)
Create `setup_env.ps1`:
```powershell
# setup_env.ps1
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.10.2\mingw_64\bin;$env:PATH"
Write-Host "Qt 6.10.2 environment configured"
qmake -v
```

Usage:
```powershell
.\setup_env.ps1
qmake
mingw32-make
```

### Linux (Bash)
Create `setup_env.sh`:
```bash
#!/bin/bash
# setup_env.sh
export Qt6_DIR=/usr/lib/x86_64-linux-gnu/cmake/Qt6
export PATH=/usr/lib/qt6/bin:$PATH
echo "Qt6 environment configured"
qmake -v
```

Usage:
```bash
source setup_env.sh
./build_linux.sh
```

---

## Quick Reference Card

### Clean and Rebuild Everything

**Windows (QMake)**:
```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.10.2\mingw_64\bin;$env:PATH"
Remove-Item -Recurse -Force release, debug, Makefile* -ErrorAction SilentlyContinue
qmake
mingw32-make -j4
```

**Linux (CMake)**:
```bash
rm -rf build_qt && mkdir build_qt && cd build_qt
cmake -DCMAKE_BUILD_TYPE=Release -DUSE_QT=ON ..
make -j$(nproc)
```

---

## Additional Resources

- Qt Documentation: https://doc.qt.io/
- CMake Documentation: https://cmake.org/documentation/
- QMake Manual: https://doc.qt.io/qt-6/qmake-manual.html
---

**Last Updated**: February 6, 2026
