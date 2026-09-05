# Models: what to download and where to put it

Whisper model weights are **large** and are **never committed** to this repository
(`.gitignore` excludes `*.bin`, `*.gguf` and any `models/` directory).

## The short answer

```powershell
.\scripts\download-model.ps1 -Model small
```

That downloads `ggml-small.bin` into:

```
%LOCALAPPDATA%\WhisperFlowClone\models\ggml-small.bin
```

which is the first directory `WhisperFlowClone.exe` searches. On a typical machine that
resolves to `C:\Users\<you>\AppData\Local\WhisperFlowClone\models\`.

On Linux/WSL (used for CI and for `--wav` accuracy runs):

```bash
./scripts/download-model.sh small      # -> ${XDG_DATA_HOME:-~/.local/share}/WhisperFlowClone/models/
```

## Which model

| `--model-name` | File | Best for |
| :--- | :--- | :--- |
| `tiny` | `ggml-tiny.bin` | fastest, lowest accuracy — quick smoke tests |
| `base` | `ggml-base.bin` | low-end CPUs |
| `small` | `ggml-small.bin` | **default**: best speed/quality balance for dictation |
| `medium` | `ggml-medium.bin` | best accuracy of this list, noticeably slower on CPU |

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
