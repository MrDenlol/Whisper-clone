#!/usr/bin/env bash
# Downloads a ggml Whisper model for WhisperFlowClone and verifies its SHA-1.
#
# Source: the official whisper.cpp collection on Hugging Face (MIT).
# Models are never committed to this repository. Default target directory:
#   ${XDG_DATA_HOME:-$HOME/.local/share}/WhisperFlowClone/models
#
# Usage: ./scripts/download_model.sh [tiny|base|small|medium|large-v3|large-v3-turbo] [destination-dir]
set -euo pipefail

MODEL="${1:-small}"
DEST="${2:-${XDG_DATA_HOME:-$HOME/.local/share}/WhisperFlowClone/models}"

# Published by ggml-org/whisper.cpp in models/README.md (v1.9.3).
case "$MODEL" in
  tiny)           SHA1=bd577a113a864445d4c299885e0cb97d4ba92b5f ;;
  base)           SHA1=465707469ff3a37a2b9b8d8f89f2f99de7299dac ;;
  small)          SHA1=55356645c2b361a969dfd0ef2c5a50d530afd8d5 ;;
  medium)         SHA1=fd9727b6e1217c2f614f9b698455c4ffd82463b4 ;;
  large-v3)       SHA1=ad82bf6a9043ceed055076d0fd39f5f186ff8062 ;;
  large-v3-turbo) SHA1=4af2b29d7ec73d781377bfd1758ca957a807e941 ;;
  *) echo "Unknown model '$MODEL'. Use tiny, base, small, medium, large-v3 or large-v3-turbo." >&2; exit 2 ;;
esac

sha1_of() {
  if command -v sha1sum >/dev/null 2>&1; then sha1sum "$1" | cut -d' ' -f1
  else shasum -a 1 "$1" | cut -d' ' -f1; fi
}

mkdir -p "$DEST"
FILE="ggml-$MODEL.bin"
TARGET="$DEST/$FILE"
URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$FILE"

if [[ -s "$TARGET" ]]; then
  if [[ "$(sha1_of "$TARGET")" == "$SHA1" ]]; then
    echo "Already present and valid: $TARGET"
    exit 0
  fi
  echo "Checksum mismatch - re-downloading $TARGET" >&2
  rm -f "$TARGET"
fi

echo "Downloading $FILE"
echo "  from: $URL"
echo "  to:   $TARGET"
curl -fL --retry 3 -o "$TARGET.part" "$URL"

ACTUAL="$(sha1_of "$TARGET.part")"
if [[ "$ACTUAL" != "$SHA1" ]]; then
  rm -f "$TARGET.part"
  echo "SHA-1 mismatch for $FILE: expected $SHA1, got $ACTUAL. File deleted." >&2
  exit 1
fi
mv -f "$TARGET.part" "$TARGET"

SIZE=$(wc -c < "$TARGET")
echo "Done: $TARGET ($(( SIZE / 1048576 )) MiB, SHA-1 OK)"
