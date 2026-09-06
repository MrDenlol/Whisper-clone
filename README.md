# WhisperFlowClone

[![Windows Build](https://github.com/MrDenlol/Whisper-clone/actions/workflows/windows-build.yml/badge.svg)](https://github.com/MrDenlol/Whisper-clone/actions/workflows/windows-build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](./LICENSE)

An open-source **Whisper-Flow clone for Windows**: press, speak, get text.
Speech recognition runs **100% locally** with [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (MIT) — no cloud, no API keys, no telemetry.

---

## 2-minute walkthrough for the jury

Everything below is what the shipped binary does today; nothing here is planned-but-missing.

```powershell
# 0. Unpack the release zip (WhisperFlowClone-<version>-win-x64.zip) anywhere, e.g. C:\WFC
#    It already contains WhisperFlowClone.exe + the MSVC runtime DLLs. No installer, no admin rights.
cd C:\WFC

# 1. Download the model (~466 MiB, SHA-1 verified, from the official whisper.cpp collection).
#    Models are never in git. Default location: %LOCALAPPDATA%\WhisperFlowClone\models\ggml-small.bin
.\scripts\download_model.ps1 -Model small
#    (for a fully portable folder use:  .\scripts\download_model.ps1 -Model small -Destination .\models)

# 2. Check the app sees it
.\WhisperFlowClone.exe --list-models

# 3. Start the tray app (no console window; a double-click on the exe does the same)
.\WhisperFlowClone.exe --tray
```

4. A **WhisperFlowClone icon appears in the system tray**; its tooltip reads
   `WhisperFlowClone - Waiting for hotkey...`. Model loading (`small`, ~1 s warm-up) happens
   before the icon appears.
5. Click into any text field (Notepad, browser, Telegram, IDE).
6. **Hold `Ctrl + Shift + Space`.** A small dark rounded pill with white text `Listening...`
   appears at the bottom-centre of the primary screen, above the taskbar. It never takes focus.
7. **Speak** a sentence in Russian (default language) — e.g. «Привет, это локальное
   распознавание речи без облака».
8. **Release the keys.** The pill switches to `Transcribing...`; after roughly 0.5–2 s on a
   laptop CPU (model `small`, 3–5 s phrase) the text is pasted into the focused field via
   `Ctrl+V`, the pill disappears and the tray tooltip changes to `WhisperFlowClone - Inserted`.
   Your previous clipboard text is restored 250 ms later.

What to expect while testing:

| Action | Result |
| :--- | :--- |
| Hold the hotkey silently, release | nothing is pasted; tray status `Skipped (...)` with the reason (too short / too quiet / no words) |
| Press the hotkey again while a phrase is still being transcribed | ignored; tray status `Busy: ...` — sessions never overlap |
| Say «запятая», «точка», «вопросительный знак» | inserted as `,` `.` `?` ([dictionary.json](./dictionary.json), editable) |
| Hotkey already taken by another app | an error message box at start-up; change `hotkey` in `settings.json` |
| No internet after the model is downloaded | works identically — the binary makes no network calls at runtime |

**Tray menu (right- or left-click the icon)**, top to bottom, exactly as built in
[`src/TrayIcon.cpp`](./src/TrayIcon.cpp):

```
Status: Waiting for hotkey...            (read-only line, mirrors the tooltip)
──────────────────────────────
Repeat last insertion                    pastes the most recent accepted phrase again
──────────────────────────────
Language: ru (default)          ● 
Language: auto
Language: en
Language: other (set in settings.json)   greyed out unless settings.json has another code
──────────────────────────────
Model: tiny
Model: base
Model: small                    ●        switching reloads the model in the background,
Model: medium                            the pill shows "Loading medium..." meanwhile
──────────────────────────────
Open models folder                       opens %LOCALAPPDATA%\WhisperFlowClone\models in Explorer
Open settings file                       opens settings.json in the default editor (creates it if missing)
Start with Windows              ☐        HKCU\...\CurrentVersion\Run  ->  "<exe>" --tray
──────────────────────────────
Exit
```

Every menu change is written back to `settings.json` immediately.

**Overlay** ([`src/Overlay.cpp`](./src/Overlay.cpp)): a borderless, always-on-top,
non-activating (`WS_EX_NOACTIVATE`) window of about 120×34 px with a 1 px grey border,
dark grey fill `RGB(32,32,32)` and white text in the default GUI font. It shows one of
`Listening...`, `Transcribing...`, `Loading <model>...` and is hidden the rest of the time.

**Where files live at runtime** (nothing is written anywhere else):

| File | Location |
| :--- | :--- |
| model `ggml-<size>.bin` | `%LOCALAPPDATA%\WhisperFlowClone\models\` **or** `<exe dir>\models\` (portable) |
| `settings.json`, `phrase_history.json` | `<exe dir>\` if a `settings.json` already exists there (portable), otherwise `%APPDATA%\WhisperFlowClone\` |
| `dictionary.json` | `<exe dir>\` first, then next to `settings.json`, then built-in defaults |

Troubleshooting in one line each: no text after release → check `Settings → Privacy →
Microphone → Let desktop apps access your microphone` ([docs/MICROPHONE.md](./docs/MICROPHONE.md));
`--list-models` shows nothing → the download went to a different folder, pass `--model <path>`;
paste does not land in an **elevated** window → UIPI, run the app elevated too.

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
| Latency: energy VAD trims silence + shrinks whisper's audio context for short clips | ✅ done |
| Model discovery + config file + SHA-1-verified model downloader script | ✅ done |
| Unit tests (81 cases) + CI on Windows and Linux + license audit | ✅ done |
| Tray icon, settings.json, phrase history, autostart, overlay, no console window | ✅ done |
| Release layout: `cmake --install … --component whisperflow` → `dist/` with exe + runtime DLLs, zip without PDB | ✅ done |

**Not implemented** (and not claimed anywhere else in this file): a settings window/GUI,
microphone device selection (always the default capture device), streaming/partial results
while the key is held, GPU builds in the release zip (CPU-only), an installer, signed binaries.
See [docs/STATUS.md](./docs/STATUS.md) for the detailed breakdown and the known risks.

---

## Build from source (Windows)

```cmd
:: 1. Build (CMake downloads whisper.cpp v1.9.3 automatically - pinned tag + SHA256, no submodules)
cmake -S . -B build
cmake --build build --config Release

:: 2. Put a model where the app looks for it (models are NOT in git)
.\scripts\download_model.ps1 -Model small

:: 3. Run: background hotkey mode with a log in this console
.\build\bin\WhisperFlowClone.exe
```

Console output of a real run (`--interactive` mode, Enter starts / Enter stops):

```
=== WhisperFlowClone - local offline speech-to-text ===
whisper.cpp 1.9.3 | CPU : SSE3 = 1 | AVX = 1 | AVX2 = 1 | FMA = 1 | OPENMP = 1 | ...
Model:      C:\Users\you\AppData\Local\WhisperFlowClone\models\ggml-small.bin
Language:   ru
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

### Release folder / zip for distribution

```powershell
.\scripts\package_release.ps1            # configure + build Release + cmake --install -> dist\ + zip
.\scripts\package_release.ps1 -Portable  # additionally creates dist\settings.json (portable layout)

:: or by hand, after the build above:
cmake --install build --config Release --prefix dist --component whisperflow
```

`dist\` contains `WhisperFlowClone.exe`, the MSVC runtime DLLs (`vcruntime140.dll`,
`vcruntime140_1.dll`, `msvcp140.dll`, `vcomp140.dll` for OpenMP), `settings.example.json`,
`dictionary.json`, `LICENSE`, `LICENSES.md`, `NOTICE`, `README.md`, `scripts\download_model.ps1`
and an empty `models\` folder. **No `.pdb`**, no `.lib`/`.obj`, and no model weights. whisper.cpp
and ggml are linked statically. The CI workflow uploads the same `dist/` as the
`WhisperFlowClone-windows-x64` artifact on every push.

---

## How it behaves

Run `WhisperFlowClone.exe` and leave it running:

1. **Hold `Ctrl + Shift + Space`** — recording starts immediately (the model is already warm).
2. **Speak.**
3. **Release** — recording stops, the buffer is transcribed locally, and the text is
   inserted into whatever window has focus.

Details worth knowing:

- **The hotkey is configurable**: `--hotkey ctrl+alt+space`, or `hotkey = ...` in the config
  file. Default is `ctrl+shift+space`. Combinations with the **Win** key are a bad default:
  Windows reserves `Win+Space` for the keyboard layout switch (and plenty of other `Win+X`
  shortcuts), so `RegisterHotKey` fails with `ERROR_HOTKEY_ALREADY_REGISTERED (1409)`. If you
  still want one, pass it explicitly and expect that failure on a stock Windows install.
  Parsing lives in [`include/HotkeySpec.h`](./include/HotkeySpec.h), which is plain C++ and
  unit tested; only the mapping to virtual key codes is in
  [`src/Hotkey.cpp`](./src/Hotkey.cpp).
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

The executable is built as a Windows-subsystem binary so that `--tray` and autostart never
open a console window. When started from `cmd.exe`/PowerShell it attaches to that console, so
`--help`, `--list-models`, `--interactive` and the log lines are visible there. When started
by **double-click** (or from `Start with Windows`) there is no console, and with no arguments
the app behaves as `--tray`.

### Tray mode (background, no console)

```cmd
:: Keep the tray build running (used by Start with Windows as well)
.\build\bin\WhisperFlowClone.exe --tray
```

In tray mode the app has **no console** and uses `settings.json` as the source of
truth. It shows a tray icon with:

- **Status** (recorded / transcribed / error)
- **Repeat last insertion** — re-pastes the most recent accepted phrase
- **Language** — `ru` (default) / `auto` / `en` (other codes stay editable in `settings.json`)
- **Model** — `tiny` / `base` / `small` / `medium`; switching model reloads it
  without restarting the application
- **Open models folder / Open settings file**
- **Start with Windows** (HKCU `…\CurrentVersion\Run`, command `"<exe>" --tray`)
- **Exit**

A small always-on-top, non-activating overlay shows `Listening...` / `Transcribing...`
around the actual dictation (and `Loading <model>...` while switching models), so it never
steals focus from the text editor. If the hotkey or the tray icon cannot be set up, a message
box explains why and the process exits with code 6.

Settings precedence:

1. Portable `settings.json` next to the executable (wins when it exists)
2. `%APPDATA%\WhisperFlowClone\settings.json` otherwise (created by `Open settings file`)

Phrase history lands next to `settings.json` as `phrase_history.json`, is capped
(`max_history_entries`, default 200), deduplicates consecutive repeats and is local-only.

Example file: [`settings.example.json`](./settings.example.json).

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

### Which model to pick

| Model | Download | When to use |
| :--- | :--- | :--- |
| **small** | 466 MiB | **Recommended (and the default).** Good Russian accuracy with a live CPU response — the sweet spot for dictation and for the hackathon demo on a laptop. |
| medium | 1.5 GiB | Noticeably better Russian, but slow on CPU. Use it when recording a demo/evaluations or when a GPU is available. |
| base / tiny | 142 / 75 MiB | Only for very weak hardware or quick smoke tests — Russian quality drops fast below small. |

A GPU is **optional**: the CPU path (with VAD trimming + audio-context shrinking) already
keeps `small` responsive for 2–5 s phrases. `use_gpu` / `--cpu` only matter if a GPU backend
was compiled in.

---


## Punctuation and text quality

Two layers keep the inserted text readable without a cloud API:

**1. Decoder prompt (whisper).** Russian is the default language, and for `ru` the app feeds
whisper an initial prompt that asks for literal transcription with proper Russian punctuation
and capital letters and forbids describing non-speech sounds (no more `(кашель)`, `(музыка)`,
`(аплодисменты)` in the output). `auto`/`en` get a neutral English wording so language
detection is not biased. Along with the prompt the decoder runs with `suppress_blank`,
`suppress_nst` and `no_speech_thold = 0.6`.

Override the prompt when needed:

```cmd
WhisperFlowClone.exe --initial-prompt "Расставь запятые по правилам русского языка."
```

or in `settings.json` / `config.ini`:

```json
{ "initial_prompt": "Расставь запятые по правилам русского языка." }
```

An empty value keeps the built-in per-language default.

**2. Text normalization before insertion** ([`src/TextNormalizer.cpp`](./src/TextNormalizer.cpp)).
The transcript is cleaned up before it is printed/pasted/stored in the phrase history:

- whitespace is trimmed and runs of spaces are collapsed;
- a stray space before `,.;:!?) ] }` is removed (`Привет , как дела ?` → `Привет, как дела?`);
- nothing is inserted and no characters are reordered, so `python3 script.py`,
  `https://example.com/docs?lang=ru` and `localhost:3000` pass through untouched.

**3. Offline spoken-punctuation dictionary** ([`dictionary.json`](./dictionary.json)).
Phrases like «запятая» → `,` or «точка с запятой» → `;` are replaced before insertion.
The file is plain JSON and safe to edit:

```json
{
  "запятая": ",",
  "точка с запятой": ";"
}
```

Lookup order: `dictionary.json` next to the executable first, then next to `settings.json`
(`%APPDATA%\WhisperFlowClone\dictionary.json`), then the built-in defaults. A missing or
corrupt file never breaks dictation — the defaults take over.

The normalization runs in every mode: `--wav`, `--interactive`, the background hotkey and
`--tray`. Manual quality checklists for Russian live in
[`tests/utterances_ru.txt`](./tests/utterances_ru.txt); the sine-wave unit test only verifies
that the decoder pipeline runs, **not** ASR quality.

---

## Command line

| Option | Meaning |
| :--- | :--- |
| *(none)* | run in the background: hold the push-to-talk hotkey, release to paste |
| `--tray` | Windows tray build: no console, `settings.json`, phrase history, autostart |
| `--interactive` | console mode: `Enter` starts recording, `Enter` transcribes |
| `--model <path>` | use this ggml model file |
| `--model-name <name>` | `tiny` \| `base` \| `small` (default) \| `medium` |
| `--language <code>` | `ru` (default), `auto`, `en`, `de`, ... |
| `--initial-prompt <text>` | override the whisper quality prompt (default: built-in, per language) |
| `--threads <n>` | inference threads, `0` = all logical cores (capped at 16) |
| `--wav <file>` | transcribe a WAV file instead of the microphone (accuracy testing) |
| `--save-wav <file>` | also dump the captured audio to a WAV file |
| `--translate` | translate the result to English |
| `--no-vad` | do not trim leading/trailing silence before recognition |
| `--no-shrink-context` | keep whisper's full 30 s audio context (slower on short clips) |
| `--cpu` | ignore a GPU backend even if one was compiled in |
| `--list-models` | show the search paths and installed models |
| `--help` | usage |

Config file `%LOCALAPPDATA%\WhisperFlowClone\config.ini` (all keys optional, command line wins;
legacy CLI mode):

```ini
model_name = small
; model_path = D:\models\ggml-small.bin
language   = ru
; initial_prompt = your own quality prompt (empty = built-in per-language default)
threads    = 0
use_gpu    = true
translate  = false
vad            = true   ; trim leading/trailing silence before recognition
shrink_context = true   ; size whisper's audio context to the clip (faster short clips)
```

Tray mode uses `settings.json` instead. Put `settings.example.json` next to the
executable (or in `%APPDATA%\WhisperFlowClone`) and edit it; the tray menu updates
the same values:

```json
{
  "model_name": "small",
  "language": "ru",
  "initial_prompt": "",
  "hotkey": "ctrl+shift+space",
  "threads": 0,
  "use_gpu": true,
  "translate": false,
  "vad": true,
  "shrink_context": true,
  "start_with_windows": false,
  "max_history_entries": 200
}
```

### Latency

Two compounding optimisations shrink the "release key → text in the field" delay, both **on by
default**:

- **Energy VAD** ([`src/Vad.cpp`](./src/Vad.cpp)) — a dependency-free, MIT detector that drops the
  dead air you always capture while reaching for and releasing the key. No ONNX/Silero, no extra
  model file, no extra license.
- **Audio-context shrinking** — whisper's encoder normally always processes a full 30 s window;
  for a 2–5 s phrase the context is sized to the real clip length instead.

For a typical 4.5 s hold (0.8 s silence + 2.5 s speech + 1.2 s silence) this cuts the encoder's
work to **~10 %** of the naive "transcribe everything with the full window" path. Each utterance
logs the full breakdown:

```
[Timing] capture 4230 ms | vad_trim 2 ms (kept 2.74/4.50 s, cut 1760 ms) | encode 180 ms | decode 320 ms | inject 45 ms | inference 500 ms (0.18x realtime)
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
├── assets/          # app.ico / app.svg (own MIT artwork, embedded as resource 101)
├── cmake/           # app.rc.in (resource script template)
├── dictionary.json  # editable spoken-punctuation dictionary (ru), optional
├── docs/            # STATUS.md, MODELS.md, MICROPHONE.md
├── include/         # public headers: AudioCapture, Transcriber, TextNormalizer, AppConfig, ...
├── packaging/       # files copied into dist/ by cmake --install (models/README.txt)
├── scripts/         # download_model.ps1/.sh (SHA-1 verified), package_release.ps1
├── src/             # implementation
├── tests/           # dependency-free unit tests + tests/utterances_ru.txt (manual quality run)
├── third_party/     # optional vendored whisper.cpp for offline builds (permissive licenses only)
├── .github/         # CI: Windows (MSVC) + Linux (GCC) builds, tests, license audit, dist artifact
├── CMakeLists.txt
├── CONTRIBUTING.md  # how to build, standard, where code goes
├── LICENSE          # MIT
├── LICENSES.md      # dependency license matrix
└── NOTICE
```

---

## Requirements

- **Run:** Windows 10 / 11 (x64), a microphone, ~1 GB RAM free for `small`. No admin rights,
  no installer; the release zip ships the MSVC runtime DLLs.
- **Build:** Visual Studio 2022, *Desktop development with C++* (MSVC 19.30+), CMake 3.24+
- **Standard:** C++20, `/W4 /WX /permissive-` (see [CONTRIBUTING.md](./CONTRIBUTING.md))

---

## Privacy

Audio is captured with WASAPI, kept in RAM as 16 kHz mono float, transcribed by a local model
and printed. Nothing is uploaded anywhere: the binary makes no network calls at runtime.

---

## License

This project is released under the [MIT License](./LICENSE).
Third-party components and their licenses: [LICENSES.md](./LICENSES.md).
