Put a ggml Whisper model in this folder, for example:

    models\ggml-small.bin      (recommended, ~466 MiB)

Download + verify it with the bundled script (PowerShell, run from the dist folder):

    .\scripts\download_model.ps1 -Model small -Destination .\models

or fetch it manually from https://huggingface.co/ggerganov/whisper.cpp/tree/main

WhisperFlowClone.exe also looks in %LOCALAPPDATA%\WhisperFlowClone\models\ and
next to the executable. Run  WhisperFlowClone.exe --list-models  to see what it found.
Model files are large and are never committed to git.
