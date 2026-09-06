# Models: what to download and where to put it

Whisper model weights are **large** and are **never committed** to this repository
(`.gitignore` excludes `*.bin`, `*.gguf` and any `models/` directory).

## The short answer

```powershell
.\scripts\download_model.ps1 -Model small
```

That downloads `ggml-small.bin` from the official whisper.cpp collection on Hugging Face,
verifies its SHA-1 against the value published in whisper.cpp's `models/README.md`
(a mismatch deletes the file and fails), and stores it in:

```
%LOCALAPPDATA%\WhisperFlowClone\models\ggml-small.bin
```

which is the first directory `WhisperFlowClone.exe` searches. On a typical machine that
resolves to `C:\Users\<you>\AppData\Local\WhisperFlowClone\models\`.

On Linux/WSL (used for CI and for `--wav` accuracy runs):

```bash
./scripts/download_model.sh small      # -> ${XDG_DATA_HOME:-~/.local/share}/WhisperFlowClone/models/
```

## Which model

| `--model-name` | File | Size | SHA-1 (upstream) | Best for |
| :--- | :--- | :--- | :--- | :--- |
| `tiny` | `ggml-tiny.bin` | 75 MiB | `bd577a113a864445d4c299885e0cb97d4ba92b5f` | fastest, lowest accuracy — quick smoke tests |
| `base` | `ggml-base.bin` | 142 MiB | `465707469ff3a37a2b9b8d8f89f2f99de7299dac` | low-end CPUs |
| `small` | `ggml-small.bin` | 466 MiB | `55356645c2b361a969dfd0ef2c5a50d530afd8d5` | **default**: best speed/quality balance for dictation |
| `medium` | `ggml-medium.bin` | 1.5 GiB | `fd9727b6e1217c2f614f9b698455c4ffd82463b4` | best accuracy of this list, noticeably slower on CPU |

The script also accepts `large-v3` / `large-v3-turbo` (2.9 / 1.5 GiB); the app can load them via
`--model <path>` but they are not in the tray menu.

Portable layout (model next to the exe, e.g. inside the release folder):

```powershell
.\scripts\download_model.ps1 -Model small -Destination .\models
```

`small` is the default in `AppConfig`; change it with `--model-name` or `model_name` in
`config.ini`. Multilingual models handle Russian and English; the `*.en` variants are
English-only and the app forces `language = en` for them automatically.

## Manual download

From <https://huggingface.co/ggerganov/whisper.cpp/tree/main>:

```powershell
$dir = "$env:LOCALAPPDATA\WhisperFlowClone\models"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
Invoke-WebRequest -Uri "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin" `
                  -OutFile "$dir\ggml-small.bin" -UseBasicParsing
```

Or with the upstream helper script from a whisper.cpp checkout:

```cmd
third_party\whisper.cpp\models\download-ggml-model.cmd small
```

## Search order

`WhisperFlowClone --list-models` prints the resolved list. The order is:

1. `--model <path>` or `model_path` from `config.ini` — an explicit file always wins.
2. `%LOCALAPPDATA%\WhisperFlowClone\models\ggml-<size>.bin`
3. `<exe dir>\models\ggml-<size>.bin`, then `<exe dir>\ggml-<size>.bin`
4. `.\models\ggml-<size>.bin`, then `.\ggml-<size>.bin`

Directories are de-duplicated, so a portable build sitting in its own folder does not list
the same path twice.

If nothing is found the app exits with code `3` and prints the exact paths it probed.

## Exit codes

| Code | Meaning |
| :--- | :--- |
| `0` | success |
| `2` | bad arguments or unknown language code |
| `3` | no model file found |
| `4` | model failed to load, or transcription failed |
| `5` | no audio was captured / WAV input could not be read |

## License of the weights

The weights are downloaded from the `ggerganov/whisper.cpp` collection and are not
redistributed by this repository. Check the model card and the upstream OpenAI Whisper
terms before shipping a bundle that includes weights — see [LICENSES.md](../LICENSES.md).
