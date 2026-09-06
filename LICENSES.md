# Open Source Licenses Overview

This project strictly adheres to permissive open-source licenses compatible with commercial and hackathon evaluation requirements (**MIT, BSD-2-Clause, BSD-3-Clause, Apache-2.0, Unlicense, CC0**). Copyleft licenses (GPL, LGPL, AGPL, SSPL) and restrictive frameworks (e.g., Qt) are strictly prohibited in this codebase.

## Component License Matrix

| Component / Dependency | License | Source / Repository | How it is used | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **WhisperFlowClone** (main codebase) | MIT | [Repository root](./LICENSE) | Everything in `src/`, `include/`, `tests/`, `scripts/` | Project core implementation |
| **whisper.cpp** v1.9.3 | MIT | [ggml-org/whisper.cpp](https://github.com/ggml-org/whisper.cpp) | Fetched by CMake at configure time (pinned tag + SHA256), linked statically | `LICENSE`: "MIT License / Copyright (c) 2023-2026 The ggml authors" |
| **ggml** (vendored inside whisper.cpp) | MIT | [ggml-org/ggml](https://github.com/ggml-org/ggml) | Built as part of whisper.cpp (`ggml`, `ggml-base`, `ggml-cpu` targets), linked statically | Covered by the same MIT `LICENSE` as whisper.cpp; upstream ggml is MIT as well |
| **WhisperFlowClone assets** (`assets/app.ico`, `assets/app.svg`, `assets/app.rc`) | MIT | This repository (`assets/`) | Own icon resource embedded via `assets/app.rc` (id 101) | Original project artwork, released under the project MIT `LICENSE`; no third-party icon is bundled |

### Optional components (OFF by default, never part of a default build)

| Component | License | Enabled by | Notes |
| :--- | :--- | :--- | :--- |
| **Vulkan-Headers** | `Apache-2.0 OR MIT` | `-DWHISPERFLOW_USE_VULKAN=ON` | Provided by the Vulkan SDK you install yourself; not vendored in this repository. Its `LICENSE.md` states: "Files in this repository fall under one of these licenses: Apache-2.0, MIT". |
| **SPIRV-Headers** | MIT (with the exceptions listed in its own `LICENSE`) | `-DWHISPERFLOW_USE_VULKAN=ON` | Provided by the Vulkan SDK; required to compile the ggml Vulkan shaders. |
| **CUDA toolkit** | NVIDIA proprietary EULA | `-DWHISPERFLOW_USE_CUDA=ON` | **Not required and not recommended for this project.** Disabled by default; nothing in the repository depends on it. |

### Model weights (downloaded at runtime, never redistributed)

Whisper models (`ggml-tiny/base/small/medium.bin`) are **not** part of this repository — see [.gitignore](./.gitignore) and [docs/MODELS.md](./docs/MODELS.md). They are downloaded by `scripts/download-model.ps1` from the `ggerganov/whisper.cpp` collection on Hugging Face. Review the model card and the upstream OpenAI Whisper terms before redistributing an application bundle that ships weights; this repository redistributes none.

## License Policy

- All third-party dependencies incorporated into `third_party/` or fetched by CMake must use one of the approved permissive licenses:
  - **MIT**
  - **BSD 2-Clause / 3-Clause**
  - **Apache 2.0**
  - **Unlicense**
  - **CC0-1.0**
- Any library under GPL, LGPL, AGPL, SSPL, or commercial copyleft licenses is **forbidden**.
- Dependencies are pinned (tag + SHA256 in `CMakeLists.txt`) so that the license set of a build is reproducible.
- When a new dependency is added, add a row to the matrix above in the same commit.

## Verification

The build never pulls a dependency that is not listed here:

```cmd
cmake -S . -B build            :: prints "whisper.cpp: downloading <url>" - whisper.cpp + ggml only
cmake -S . -B build -DWHISPERFLOW_USE_VULKAN=ON   :: adds the Vulkan SDK components listed above
```

No Qt, no GPL/LGPL/AGPL/SSPL component, no cloud STT service.
