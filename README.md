# SysPulse-TUI 📊⚡

A lightweight, btop-inspired Linux terminal system and hardware monitor built with a **C-first systems architecture** and **pragmatic modern C++20 ergonomics**.

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![Language: C / C++20](https://img.shields.io/badge/Language-C%20%2F%20C%2B%2B20-orange.svg)
![UI: FTXUI](https://img.shields.io/badge/UI-FTXUI%20v5.0-green.svg)
![Platform: Linux](https://img.shields.io/badge/Platform-Linux-lightgrey.svg)

---

## 🏛️ Architecture Philosophy: C-First + Pragmatic C++20 QoL

SysPulse-TUI adheres to a strict division of responsibility between low-level OS mechanics and high-level user interface presentation:

### 1. Pure C / POSIX Systems Layer (OS Level & Virtual Filesystem)
All performance-critical kernel interactions, parsing, and synchronization use native C and POSIX primitives:
- **POSIX Threads & Mutexes (`<pthread.h>`)**: Worker thread spawning via `pthread_create`, non-blocking tick updates, and synchronization using `pthread_mutex_t` (`pthread_mutex_lock` / `pthread_mutex_unlock`).
- **Low-Overhead C Virtual Filesystem I/O (`<stdio.h>`, `<dirent.h>`)**: Zero-heap parsing of `/proc/stat`, `/proc/uptime`, `/proc/loadavg`, `/proc/meminfo`, `/proc/diskstats`, `/proc/net/dev`, and `/proc/[pid]/stat` using `fopen`, `fgets`, `fscanf`, and `sscanf`.
- **Hardware Sensor Traversal (`<dirent.h>`)**: Directory walking of `/sys/class/thermal` and `/sys/class/hwmon` with `opendir`, `readdir`, and `closedir`.
- **POSIX Signals & Syscalls (`<signal.h>`, `<unistd.h>`, `<time.h>`)**: Precise delta timing with `clock_gettime(CLOCK_MONOTONIC)`, process signaling via `kill(pid, sig)`, and memory page size resolution via `sysconf(_SC_PAGESIZE)`.
- **POD Structs**: Fixed-size character arrays (`char name[64]`, `char device[32]`) eliminate allocations during 1000ms polling intervals.

### 2. Pragmatic C++20 QoL Layer (UI & Containers)
C++20 is leveraged exclusively where it offers massive developer ergonomics:
- **Reactive Terminal UI (`ArthurSonzogni/FTXUI v5.0.0`)**: 24-bit TrueColor rendering, Braille historical load graphs, reactive layout panes, and keyboard event dispatching.
- **Dynamic Containers & Algorithms**: `std::deque<int>` for rolling historical graph windows, `std::vector` for process tables, and `std::sort` for multi-column sorting.

---

## ✨ Features

- **Header Bar**: Live system uptime (`HH:MM:SS`), 1m/5m/15m load averages, and status / POSIX signal feedback.
- **CPU & Thermals Panel**:
  - Live Unicode Braille historical load graph.
  - Per-core utilization gauge bars with 3-tier color gradients (Green `< 50%`, Yellow `< 80%`, Red `≥ 80%`).
  - Hardware thermal zone badges reading millidegree Celsius sensors from `hwmon` and `thermal_zone`.
- **Memory & Storage I/O Panel**:
  - Exact `GiB / GiB` gauges for RAM (Total, Used, Available, Cached) and Swap space.
  - Real-time disk read & write MB/s throughput meters parsing `/proc/diskstats` for physical block devices (`nvme*`, `sd*`, `vd*`).
- **Network Throughput Panel**:
  - Live Download (Rx) and Upload (Tx) gauges in Mbps per active non-loopback network interface from `/proc/net/dev`.
- **Interactive Process Manager**:
  - Full-screen scrollable task manager displaying `PID`, `COMMAND`, `STAT`, `CPU%`, and `RSS (MiB)`.
  - In-place signal dispatching (`SIGTERM` and `SIGKILL`).
  - Instant dynamic multi-column sorting.

---

## ⌨️ Keybindings

| Key | Action |
| :--- | :--- |
| `▲` / `▼` or `k` / `j` | Move process selection cursor up / down |
| `PageUp` / `PageDown` | Jump cursor 10 rows |
| `s` | Cycle sort column (`CPU%` ➔ `MEM` ➔ `PID` ➔ `NAME`) |
| `t` | Dispatch `SIGTERM` (Signal 15) to selected PID |
| `x` or `F9` | Dispatch `SIGKILL` (Signal 9) to selected PID |
| `q` or `Escape` | Clean exit (stops worker thread, destroys mutex, restores terminal) |

---

## 📁 Project Structure

```
SysPulse-TUI/
├── CMakeLists.txt              # CMake configuration (C++20, FetchContent FTXUI)
├── Makefile                    # Make wrapper (all, build, run, clean, test)
├── README.md                   # Project documentation
├── include/
│   ├── core/
│   │   ├── engine.h            # Engine lifecycle and worker thread API
│   │   └── system_state.h      # C-style POD structs & shared state
│   ├── collectors/
│   │   └── collectors.h        # /proc and /sys collector declarations
│   └── ui/
│       └── dashboard.h         # FTXUI dashboard component builder
└── src/
    ├── main.cpp                # CLI entry point, CatchEvent loop, --test flag
    ├── core/
    │   └── engine.cpp          # POSIX pthread engine & monotonic delta loop
    ├── collectors/
    │   ├── cpu_mem.cpp         # Pure C parser for /proc/stat, meminfo, uptime, loadavg
    │   ├── io_net.cpp          # Pure C parser for /proc/diskstats and /proc/net/dev
    │   ├── thermals.cpp        # POSIX opendir/readdir traversal of /sys/class/hwmon & thermal
    │   └── process.cpp         # Pure C /proc scanner, stat parser & POSIX kill()
    └── ui/
        └── dashboard.cpp       # 24-bit TrueColor FTXUI btop-style dashboard
```

---

## 🚀 Building & Running

### Prerequisites
- Linux OS (Kernel 3.0+)
- GCC 10+ or Clang 11+ with C++20 support
- CMake 3.20+
- GNU Make
- Git

### Build & Run
```bash
# Clone the repository
git clone https://github.com/taybro-o/SysPulse-TUI.git
cd SysPulse-TUI

# Build the project
make build

# Launch the interactive TUI
./syspulse
# or: make run
```

### Headless Verification Self-Test
SysPulse-TUI includes a headless self-test mode designed for CI/CD environments and automated verification without requiring a terminal TTY:

```bash
make test
# or: ./syspulse --test
```

This runs two collection passes, verifies exact monotonic timing deltas, and prints parsed metrics for CPU, RAM, Disk I/O, Network throughput, Thermals, and top active processes to `stdout`.

---

## 📜 License
This project is open-source and available under the [MIT License](LICENSE).
