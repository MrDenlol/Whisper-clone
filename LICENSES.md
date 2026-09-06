# Third-party licenses

**Summary for reviewers:** WhisperFlowClone is MIT. The only third-party code compiled into the
binary is **whisper.cpp v1.9.3** and the **ggml** it bundles — both MIT. The release zip adds
the Microsoft Visual C++ runtime DLLs, which Microsoft licenses for redistribution with
applications built by Visual Studio. **There is no GPL, LGPL, AGPL, SSPL, MPL or any other
copyleft component anywhere in the source tree, the build, or the release package.** No Qt,
no cloud/STT API, no telemetry SDK.

Allowed licenses for this project: MIT, BSD-2-Clause, BSD-3-Clause, Apache-2.0, Unlicense,
CC0-1.0, MIT-0/public-domain. Everything else needs an explicit decision before it is added.

## 1. Compiled into `WhisperFlowClone.exe`

| Component | Version | License | Copyright | Source | How it is used |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **WhisperFlowClone** | 0.2.0 | MIT ([LICENSE](./LICENSE)) | (c) 2026 MrDenlol | this repository | `src/`, `include/`, `tests/`, `scripts/`, `packaging/`, `cmake/` |
| **WhisperFlowClone artwork** (`assets/app.ico`, `assets/app.svg`) | — | MIT (project license) | (c) 2026 MrDenlol | this repository | tray/exe icon, resource id 101 via `cmake/app.rc.in`; original artwork, no stock icon |
| **whisper.cpp** | v1.9.3 (`1650f884…d9c0`) | MIT | (c) 2023-2026 The ggml authors | <https://github.com/ggml-org/whisper.cpp> | STT engine; `whisper` target linked **statically** (`BUILD_SHARED_LIBS=OFF`) |
| **ggml / ggml-base / ggml-cpu** (vendored inside whisper.cpp) | as in v1.9.3 | MIT | (c) 2023-2026 The ggml authors | <https://github.com/ggml-org/ggml> | tensor library + CPU backend, linked statically |
| ggml **llamafile SGEMM** (`ggml/src/ggml-cpu/llamafile/sgemm.cpp`) | as in v1.9.3 | MIT | (c) 2024 Mozilla Foundation | part of ggml | compiled only when `GGML_LLAMAFILE=ON`; it is **OFF** in this build (whisper.cpp default) |
| ggml **KleidiAI kernels** (`ggml/src/ggml-cpu/kleidiai/`) | as in v1.9.3 | MIT | (c) 2025-2026 Arm Limited | part of ggml | ARM-only, `GGML_CPU_KLEIDIAI=OFF`; **not compiled** in the x64 build |
| **Windows SDK / Win32 API** (WASAPI, `RegisterHotKey`, `SendInput`, `Shell_NotifyIcon`, GDI) | Windows 10/11 SDK | Microsoft Windows SDK license | Microsoft | Visual Studio | system headers/import libs (`ole32`, `uuid`, `shell32`, `advapi32`); no code redistributed |
| **MSVC C/C++ standard library** (`msvcp140.dll`, `vcruntime140*.dll`) | VS 2022 (14.3x) | Microsoft Visual Studio redistributable terms | Microsoft | Visual Studio | linked dynamically (`/MD`); the DLLs are copied into `dist/` by `InstallRequiredSystemLibraries` |
| **Microsoft OpenMP runtime** (`vcomp140.dll`) | VS 2022 | Microsoft Visual Studio redistributable terms | Microsoft | Visual Studio | ggml CPU threading (`GGML_OPENMP=ON`); copied into `dist/` |

The Microsoft runtime files are the ones listed in Visual Studio's `redist\MSVC\<ver>\x64\Microsoft.VC143.CRT`
and `Microsoft.VC143.OpenMP` folders. Microsoft's Visual Studio license (section
"Distributable Code" / `Redist.txt`) permits redistributing them with applications built by
Visual Studio. They are not open source, but they are not copyleft and impose no obligation
on this project's source.

## 2. Used at build time only (nothing ends up in the binary)

| Tool | License | Notes |
| :--- | :--- | :--- |
| CMake ≥ 3.24 | BSD-3-Clause | `FetchContent` downloads the whisper.cpp tarball and verifies its SHA256 |
| MSVC 19.30+ / Visual Studio 2022 | Microsoft | compiler, linker, `rc.exe` |
| GCC 12 / Clang 15 (Linux CI only) | GPL-3.0-with-GCC-exception / Apache-2.0-with-LLVM-exception | **compiler only**, the GCC runtime exception explicitly allows the resulting binary to be MIT; the Windows release is built with MSVC and never touches GCC |
| GitHub Actions `actions/checkout@v4`, `actions/upload-artifact@v4` | MIT | CI plumbing |
| PowerShell 5.1 / 7 | MIT (pwsh) / Windows component | `scripts/*.ps1` |

## 3. Optional components — OFF by default, never in the release zip

| Component | License | Enabled by | Notes |
| :--- | :--- | :--- | :--- |
| Vulkan-Headers | Apache-2.0 OR MIT | `-DWHISPERFLOW_USE_VULKAN=ON` | from a Vulkan SDK you install yourself; not vendored |
| SPIRV-Headers | MIT (with listed exceptions) | `-DWHISPERFLOW_USE_VULKAN=ON` | needed to compile the ggml Vulkan shaders |
| CUDA toolkit | NVIDIA proprietary EULA | `-DWHISPERFLOW_USE_CUDA=ON` | **not required, not used by the project**; nothing in the repository depends on it |

None of whisper.cpp's other optional pieces are built: `WHISPER_SDL2`, `WHISPER_CURL`,
`WHISPER_COREML`, `WHISPER_OPENVINO`, `GGML_BLAS`, `GGML_METAL` and all examples/tests/server
are forced OFF in `CMakeLists.txt`. In particular the whisper.cpp `examples/` directory
(which carries `json.hpp` (MIT), `miniaudio.h` (MIT-0/public domain), `stb_vorbis.c`
(public domain) and an ffmpeg helper) is **never compiled**.

## 4. Model weights — downloaded by the user, never redistributed

| Artifact | Source | License | Notes |
| :--- | :--- | :--- | :--- |
| `ggml-tiny/base/small/medium.bin` (also `large-v3`, `large-v3-turbo`) | <https://huggingface.co/ggerganov/whisper.cpp> (model card: `license: mit`); conversions of OpenAI Whisper weights, which OpenAI released under MIT | MIT | `scripts/download_model.ps1` / `.sh` download the file and verify the SHA-1 published in whisper.cpp `models/README.md`. `.gitignore` excludes `*.bin` and `models/`; the release zip ships an empty `models/` folder. |

## 5. Explicitly absent

- **No copyleft:** no GPL, LGPL, AGPL, SSPL, MPL, EPL, CDDL code or DLLs. The `License audit`
  job in `.github/workflows/windows-build.yml` greps the tree for copyleft license texts on every
  push, and the whisper.cpp pin (URL + SHA256) is checked against this document.
- **No Qt, wxWidgets, GTK, Electron** — UI is plain Win32 (`Shell_NotifyIcon`, `CreateWindowEx`, GDI).
- **No third-party test framework** — `tests/test_framework.h` is ~60 lines of project code.
- **No JSON/INI library** — `src/Settings.cpp` and `src/AppConfig.cpp` contain a purpose-built parser.
- **No VAD model** (Silero/ONNX) — `src/Vad.cpp` is an energy detector written for this project.
- **No network code** in the binary: no libcurl, no WinHTTP, no telemetry.

## 6. Policy

1. Dependencies are pinned (tag + SHA256 for whisper.cpp in `CMakeLists.txt`, SHA-1 per model in
   `scripts/download_model.ps1`) so the license set of a build is reproducible.
2. A new dependency lands in the same commit as its row in section 1 (or 3) above.
3. Vendoring into `third_party/` is allowed only for the whisper.cpp tarball already listed here
   (offline builds); `third_party/` is otherwise empty on purpose.

## 7. How to verify

```cmd
cmake -S . -B build             :: configure log lists "whisper.cpp: downloading <v1.9.3 url>" and nothing else
cmake --install build --config Release --prefix dist
dir dist                        :: exe + Microsoft runtime DLLs + docs; no other DLLs, no .pdb
dumpbin /dependents dist\WhisperFlowClone.exe
                                :: KERNEL32/USER32/SHELL32/OLE32/ADVAPI32/GDI32 + MSVCP140/VCRUNTIME140*/VCOMP140 only
```
