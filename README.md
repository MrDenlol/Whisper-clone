# WhisperFlowClone

[![Windows Build](https://github.com/MrDenlol/Whisper-clone/actions/workflows/windows-build.yml/badge.svg)](https://github.com/MrDenlol/Whisper-clone/actions/workflows/windows-build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](./LICENSE)

An open-source **Whisper-Flow clone for Windows**: press, speak, get text.
Speech recognition runs **100% locally** with [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (MIT) — no cloud, no API keys, no telemetry.

---

## Current status

| Stage | State |
| :--- | :--- |
| Microphone capture (WASAPI, 16 kHz mono float) | ✅ done |
| Local STT engine (whisper.cpp v1.9.3, CPU) | ✅ done |
| Global push-to-talk hotkey (hold to talk, release to paste) | ✅ done |
| Paste into the focused window (clipboard + `SendInput`, clipboard restored) | ✅ done |
| Silence / too-short / no-words filter — nothing is pasted for empty speech | ✅ done |
| Single-session guard — a second press cannot start a parallel session | ✅ done |
| Model discovery + config file + model downloader script | ✅ done |
| Unit tests (28 cases) + CI on Windows and Linux | ✅ done |
| Tray icon, per-app settings UI, hidden window (no console) | ⏳ next |

See [docs/STATUS.md](./docs/STATUS.md) for the detailed breakdown and the plan.

---

## Quick start (Windows)

```cmd
:: 1. Build (CMake downloads whisper.cpp v1.9.3 automatically - no submodules to init)
cmake -S . -B build
cmake --build build --config Release

:: 2. Put a model where the app looks for it (models are NOT in git)
.\scripts\download-model.ps1 -Model small

:: 3. Run: Enter = start recording, Enter = stop and transcribe
.\build\bin\WhisperFlowClone.exe
```

Output of a real run:

```
=== WhisperFlowClone - local offline speech-to-text ===
whisper.cpp 1.9.3 | CPU : SSE3 = 1 | AVX = 1 | AVX2 = 1 | FMA = 1 | OPENMP = 1 | ...
Model:      C:\Users\you\AppData\Local\WhisperFlowClone\models\ggml-small.bin
Language:   auto
Model load: 912 ms (warm)
Press [Enter] to START recording...

--- Recognized text ---
Привет, это локальное распознавание речи без облака.
(language: ru)

--- Timing ---
Audio:      4.18 s
Capture:    4230 ms
Model load: 0 ms (warm)
Inference:  1540 ms
Real-time:  0.37x  (inference time / audio time, lower is better)
```

---

## How it behaves

Run `WhisperFlowClone.exe` and leave it running:

1. **Hold `Ctrl + Win + Space`** — recording starts immediately (the model is already warm).
2. **Speak.**
3. **Release** — recording stops, the buffer is transcribed locally, and the text is
   inserted into whatever window has focus.

Details worth knowing:

- **The hotkey is a constant** for now: `kPushToTalk` in [`include/Hotkey.h`](./include/Hotkey.h)
  (`MOD_CONTROL | MOD_WIN` + `VK_SPACE`). It moves into `config.ini` in a later step.
- **Release detection.** `RegisterHotKey` only reports the press, so after `WM_HOTKEY` the
  app polls `GetAsyncKeyState` every 20 ms until the combination is let go.
- **Paste** = copy the transcript to the clipboard → `Ctrl+V` via `SendInput` → put the
  previous clipboard text back after 250 ms. If the paste fails, the transcript **stays on
  the clipboard** and the console says so, so you can press `Ctrl+V` yourself.
- **Empty speech is dropped.** Buffers under 400 ms, below ~-40 dBFS peak, or transcripts
  with no words in them (`"."`, `"..."`, `"[Music]"`) are never pasted — see
  [`include/SpeechGate.h`](./include/SpeechGate.h).
- **No overlapping sessions.** `SessionGuard` refuses a second press while recording or
  transcribing and logs why.
- Inference runs on a worker thread, so the message loop (and release detection) never
  stalls.

Console fallback without the hotkey:

```cmd
.\build\bin\WhisperFlowClone.exe --interactive
```

### Microphone permission

`Settings → Privacy & security → Microphone → Let desktop apps access your microphone → On`.
Full walkthrough, device selection and paste/UIPI caveats: **[docs/MICROPHONE.md](./docs/MICROPHONE.md)**.

---

## Where to put the model

Models are downloaded at runtime and never committed (see [.gitignore](./.gitignore)).
Full details, checksums and manual instructions: **[docs/MODELS.md](./docs/MODELS.md)**.

Default location on Windows:

```
%LOCALAPPDATA%\WhisperFlowClone\models\ggml-small.bin
```

Search order used by the app (first hit wins):

1. `--model <path>` / `model_path` in `config.ini`
2. `%LOCALAPPDATA%\WhisperFlowClone\models\ggml-<size>.bin`
3. `<exe dir>\models\ggml-<size>.bin` and `<exe dir>\ggml-<size>.bin` (portable layout)
4. `.\models\ggml-<size>.bin` and `.\ggml-<size>.bin` (current directory)

Check what is installed with `WhisperFlowClone.exe --list-models`.

---

## Command line

| Option | Meaning |
| :--- | :--- |
| *(none)* | run in the background: hold the push-to-talk hotkey, release to paste |
| `--interactive` | console mode: `Enter` starts recording, `Enter` transcribes |
| `--model <path>` | use this ggml model file |
| `--model-name <name>` | `tiny` \| `base` \| `small` (default) \| `medium` |
| `--language <code>` | `auto` (default), `ru`, `en`, `de`, ... |
| `--threads <n>` | inference threads, `0` = all logical cores (capped at 16) |
| `--wav <file>` | transcribe a WAV file instead of the microphone (accuracy testing) |
| `--save-wav <file>` | also dump the captured audio to a WAV file |
| `--translate` | translate the result to English |
| `--cpu` | ignore a GPU backend even if one was compiled in |
| `--list-models` | show the search paths and installed models |
| `--help` | usage |

Config file `%LOCALAPPDATA%\WhisperFlowClone\config.ini` (all keys optional, command line wins):

```ini
model_name = small
; model_path = D:\models\ggml-small.bin
language   = ru
threads    = 0
use_gpu    = true
translate  = false
```

---

## Build options

| Option | Default | Effect |
| :--- | :--- | :--- |
| `WHISPERFLOW_BUILD_TESTS` | `OFF` | builds `WhisperFlowTests` and registers it with CTest |
| `WHISPERFLOW_NATIVE` | `ON` | tunes ggml kernels for the build machine's CPU (`GGML_NATIVE`) |
| `WHISPERFLOW_USE_VULKAN` | `OFF` | optional Vulkan backend; needs the Vulkan SDK (`glslc` + SPIRV-Headers) |
| `WHISPERFLOW_USE_CUDA` | `OFF` | optional CUDA backend; needs the CUDA toolkit. **Never required.** |
| `WHISPERFLOW_WHISPER_CPP_URL` | v1.9.3 tarball | dependency source, pinned together with `WHISPERFLOW_WHISPER_CPP_SHA256` |

The default build is **CPU-only** and needs nothing but a C++20 compiler and CMake.

```cmd
cmake -S . -B build -DWHISPERFLOW_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### Dependency resolution

`CMakeLists.txt` prefers a vendored copy and falls back to a pinned download:

1. `third_party/whisper.cpp/` — use this for a fully offline build.
2. Otherwise CMake downloads `whisper.cpp v1.9.3` and verifies its SHA256.

No `git submodule update --init` step to forget, no unpinned `main` branch.

---

## Repository structure

```
WhisperFlowClone/
├── assets/          # icons and visual assets
├── cmake/           # custom CMake modules
├── docs/            # STATUS.md, MODELS.md
├── include/         # public headers: AudioCapture, Transcriber, ModelLocator, AppConfig, WavFile
├── scripts/         # model downloaders (PowerShell / bash)
├── src/             # implementation
├── tests/           # dependency-free unit tests
├── third_party/     # optional vendored dependencies (permissive licenses only)
├── .github/         # CI workflows
├── CMakeLists.txt
├── LICENSE          # MIT
├── LICENSES.md      # dependency license matrix
└── NOTICE
```

---

## Requirements

- **OS:** Windows 10 / 11 (x64)
- **Compiler:** Visual Studio 2022, *Desktop development with C++* (MSVC 19.30+)
- **Build system:** CMake 3.24+
- **Standard:** C++20, `/W4 /WX /permissive-`

---

## Privacy

Audio is captured with WASAPI, kept in RAM as 16 kHz mono float, transcribed by a local model
and printed. Nothing is uploaded anywhere: the binary makes no network calls at runtime.

---

## License

This project is released under the [MIT License](./LICENSE).
Third-party components and their licenses: [LICENSES.md](./LICENSES.md).
