#!/usr/bin/env bash
# Downloads a ggml Whisper model for WhisperFlowClone.
#
# Models are never committed to this repository. Default target directory:
#   ${XDG_DATA_HOME:-$HOME/.local/share}/WhisperFlowClone/models
#
# Usage: ./scripts/download-model.sh [tiny|base|small|medium|large-v3] [destination-dir]
set -euo pipefail

MODEL="${1:-small}"
DEST="${2:-${XDG_DATA_HOME:-$HOME/.local/share}/WhisperFlowClone/models}"

case "$MODEL" in
  tiny|base|small|medium|large-v3|large-v3-turbo) ;;
  *) echo "Unknown model '$MODEL'. Use tiny, base, small, medium, large-v3 or large-v3-turbo." >&2; exit 2 ;;
esac

mkdir -p "$DEST"
FILE="ggml-$MODEL.bin"
TARGET="$DEST/$FILE"
URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$FILE"

if [[ -s "$TARGET" ]]; then
  echo "Already present: $TARGET"
  exit 0
fi

echo "Downloading $FILE"
echo "  from: $URL"
echo "  to:   $TARGET"
curl -fL --retry 3 -o "$TARGET" "$URL"

SIZE=$(wc -c < "$TARGET")
if (( SIZE < 1000000 )); then
  rm -f "$TARGET"
  echo "Downloaded file is only $SIZE bytes - the download failed." >&2
  exit 1
fi

echo "Done: $TARGET ($(( SIZE / 1048576 )) MiB)"
