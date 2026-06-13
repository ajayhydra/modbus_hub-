# ModbusHub

Cross-platform Qt 6 desktop tool for Modbus engineers. Speaks **Modbus TCP** and **RTU**, with a custom in-tree C protocol stack (no libmodbus). Three jobs:

1. **Master** — connect to one TCP/RTU slave and read/write registers and coils interactively.
2. **Gateway** — transparent **TCP ↔ RTU** bridge in either direction.
3. **Scanner** — discover Modbus TCP devices across an IP range.

Runs on Windows, Linux, and macOS.

---

## Features

| Tab | What it does |
|-----|--------------|
| **Master** | One slave, TCP or RTU. FCs `0x01`–`0x06`, `0x0F`, `0x10`. Renders UInt16/Int16/UInt32/Int32/Float32/Float64 with word order (ABCD/CDAB/BADC/DCBA) and Dec/Hex/Oct. Optional auto-poll. |
| **Gateway** | RTU→TCP or TCP→RTU forwarding with a live frame log. |
| **Scanner** | Probe an IP range on a port (default `502`); double-click a hit to load it into Master. |
| **Logs** | Filterable log (Master/Gateway/Scanner/System), 1,000-entry cap, text/CSV export. |
| **Settings** | Light/Dark theme + built-in HTML manual. |

---

## Install (Windows)

- **Installer:** run `dist\ModbusHub_Setup_2.0.0.exe`.
- **Portable:** unzip `dist\ModbusHub_portable.zip` and run `modbus_master.exe`.

Both bundle the Qt runtime. On Linux/macOS, build from source (below).

---

## Usage

A full guide + Modbus protocol reference is built in: **Settings → Open User Manual**.

> Addresses are **0-based** — one less than most datasheets. Register **40001 → enter `0`**.

- **Master** — pick *TCP* (IP + port `502`) or *RTU* (COM port, baud, parity) and a Slave ID (`1`–`247`) → **Connect**. To read: choose a read FC, set Address + Quantity + Format, click **Read**. To write: choose a write FC, set Address + space-separated Value(s), click **Write**. Tick **Auto-Poll** for repeated reads.
- **Gateway** — choose direction, set the TCP listen port and the matching COM/baud/parity, click **Start Gateway**. *(Don't share a COM port with the Master tab.)*
- **Scanner** — set Start/End IP and port → **Scan Network**; double-click a result to send it to Master.

---

## Build

Needs Qt 6 (or Qt 5) with **Core, Widgets, Network** — nothing else.

```bash
# any platform
cmake -S . -B build -DUSE_QT6=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Convenience wrappers:

```bat
build_windows.bat [build|rebuild|package|installer|run]   :: → build_qt_win\modbus_master.exe
```
```bash
./make_linux.sh  [build|rebuild|run|all|clean]            # → build_linux/modbus_master
./build_macos.sh [build|rebuild|run|all|clean]
```

- Windows wrapper hardcodes the Qt/MinGW paths — edit `build_windows.ps1` if yours differ.
- `installer` compiles `installer.iss` (Inno Setup 6); run `build`/`package` first so `windeployqt` has populated the DLLs.
- CMake options: `USE_QT6` (default ON; OFF = Qt 5), `USE_STATIC_QT` (default OFF).

---

## Layout

```
src/ui/        Qt C++ frontend — mainwindow + GatewayThread / ScanWorkerThread
src/backend/   C99: modbus.c (custom TCP+RTU stack), platform.c, config, loggers
```

UI runs on the main thread; the gateway bridge and IP scan run on `QThread` workers that report back via queued signals. TCP uses MBAP over sockets; RTU uses native serial (Win32 / POSIX `termios`).

> Windows: COM ports above COM9 need the `\\.\COMxx` prefix — handled in `modbus.c`; keep it if you refactor.

---

## Runtime files

- `modbus_master.ini` — settings, loaded from the working dir (repo copy is a template).
- `logs/` — created at runtime for CSV data logging.

---

MIT — see [LICENSE.txt](LICENSE.txt).
