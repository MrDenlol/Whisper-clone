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
| Latency: energy VAD trims silence + shrinks whisper's audio context for short clips | ✅ done |
| Model discovery + config file + model downloader script | ✅ done |
| Unit tests (81 cases) + CI on Windows and Linux | ✅ done |
| Tray icon, settings.json UI, phrase history, autostart, hidden window (no console) | ✅ done |

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

A small always-on-top, non-activating overlay shows `Listening…` / `Transcribing…`
around the actual dictation, so it never steals focus from the text editor.

Settings precedence:

1. Portable `settings.json` next to the executable (wins when it exists)
2. `%APPDATA%\WhisperFlowClone\settings.json` (portable layout in other OS builds)

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
| **small** | ~460 MB | **Recommended (and the default).** Good Russian accuracy with a live CPU response — the sweet spot for dictation and for the hackathon demo on a laptop. |
| medium | ~1.4 GB | Noticeably better Russian, but slow on CPU. Use it when recording a demo/evaluations or when a GPU is available. |
| base / tiny | ~140 / ~75 MB | Only for very weak hardware or quick smoke tests — Russian quality drops fast below small. |

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
├── assets/          # icons and visual assets
├── cmake/           # custom CMake modules
├── dictionary.json  # editable spoken-punctuation dictionary (ru), optional
├── docs/            # STATUS.md, MODELS.md
├── include/         # public headers: AudioCapture, Transcriber, TextNormalizer, AppConfig, ...
├── scripts/         # model downloaders (PowerShell / bash)
├── src/             # implementation
├── tests/           # dependency-free unit tests + tests/utterances_ru.txt (manual quality run)
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
