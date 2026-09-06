# Contributing

**Build (Windows, Visual Studio 2022, CMake 3.24+)** — CMake downloads whisper.cpp v1.9.3 itself (pinned tag + SHA256, no submodules):

```cmd
cmake -S . -B build -DWHISPERFLOW_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The same commands work on Linux (GCC 12+/Clang 15+) for everything except the microphone/tray/hotkey code, which is Windows-only (`#if defined(_WIN32)` / `WHISPERFLOW_HAS_MICROPHONE`). A release folder + zip is produced by `scripts\package_release.ps1`.

**Standard and warnings.** C++20, no compiler extensions. Our own targets build with `/W4 /WX /permissive-` (MSVC) and `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang); a warning is a build failure. whisper.cpp/ggml headers are included as `SYSTEM`, so keep the fix on our side, not by relaxing the flags.

**Where code goes.**
- `include/` — one public header per component (`AudioCapture.h`, `Transcriber.h`, ...), namespace `whisperflow`. Prefer `std::function` callbacks and pimpl over inheritance/virtuals; keep Win32 headers out of public headers where possible.
- `src/` — implementations. Platform-independent logic goes into the `whisperflow_core` library so it is testable on Linux CI; Win32-only files (`AudioCapture`, `Hotkey`, `Overlay`, `TrayIcon`) are added to the executable only under `if(WIN32)` in `CMakeLists.txt`.
- `tests/` — dependency-free unit tests (`WF_TEST(...)` from `tests/test_framework.h`), one `test_<component>.cpp` per component, registered in `tests/CMakeLists.txt`.
- `scripts/` — PowerShell (`*.ps1`) first, bash mirror where it makes sense; `docs/` — user-facing documentation; `third_party/` — only for a vendored offline copy of whisper.cpp.

**Dependencies.** Permissive licenses only (MIT/BSD/Apache-2.0/Unlicense/CC0). No GPL/LGPL/AGPL, no Qt, no cloud APIs. Every new dependency gets a pinned version + checksum in `CMakeLists.txt` and a row in `LICENSES.md` in the same commit — the `License audit` CI job enforces the basics.

**Before opening a PR.** Build + tests green locally, no new warnings, no absolute paths or secrets, models/`build/`/`dist/` stay out of git (see `.gitignore`). CI runs Windows (MSVC), Linux (GCC) and the license audit on every push; all three must be green.
