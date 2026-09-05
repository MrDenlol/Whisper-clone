# WhisperFlowClone

[![Windows Build](https://github.com/MrDenlol/Whisper-clone/actions/workflows/windows-build.yml/badge.svg)](https://github.com/MrDenlol/Whisper-clone/actions/workflows/windows-build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](./LICENSE)

An open-source, ultra-fast **Whisper-Flow clone for Windows**. 
Designed for high performance, zero-latency dictation, and complete privacy by running 100% locally on your machine.

---

##  Key Features

- **Global Hotkey:** Instant start/stop dictation from any application.
- **Audio Pipeline:** WASAPI low-latency microphone audio capture.
- **Local STT:** Fast, local AI speech recognition (no cloud, no API keys required).
- **Auto-Paste:** Direct text insertion into the currently focused window via Win32 API.
- **100% Private & Offline:** Zero network requests, zero telemetry.

---

## 🔒 Privacy & Local Processing Notice

WhisperFlowClone operates **entirely offline on Windows**. Audio data recorded from your microphone is processed locally in RAM and fed directly into the local inference engine. No audio samples or transcriptions ever leave your computer.

---

## 🛠️ Repository Structure

```
WhisperFlowClone/
├── assets/          # Icons and visual assets
├── cmake/           # Custom CMake modules and helper scripts
├── include/         # Public header files (.h)
├── src/             # Source files (.cpp)
├── third_party/     # Vendor dependencies (permissive licenses only)
├── .github/         # CI/CD workflows
├── CMakeLists.txt   # Main CMake build configuration
├── LICENSE          # MIT License
├── LICENSES.md      # Matrix of dependencies and licenses
└── NOTICE           # Project notices and compliance info
```

---

## ⚙️ Building from Source

### Prerequisites

- **OS:** Windows 10 / Windows 11 (x64)
- **Compiler:** Visual Studio 2022 with **Desktop development with C++** workload (MSVC 19.30+)
- **Build System:** CMake 3.24 or higher
- **C++ Standard:** C++20

### Quick Start

1. **Clone the repository:**
   ```cmd
   git clone https://github.com/MrDenlol/Whisper-clone.git
   cd Whisper-clone
   ```

2. **Configure with CMake:**
   ```cmd
   cmake -S . -B build -DWHISPERFLOW_BUILD_TESTS=OFF
   ```

3. **Build Release executable:**
   ```cmd
   cmake --build build --config Release
   ```

4. **Run the executable:**
   ```cmd
   .\build\Release\WhisperFlowClone.exe
   ```

---

## 📜 License

This project is released under the [MIT License](./LICENSE).  
For third-party component licenses and compliance policies, see [LICENSES.md](./LICENSES.md).
