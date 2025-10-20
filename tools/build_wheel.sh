#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT_DIR"

# Build wheel using the normal PEP517 path
python -m build --wheel

# Find the produced wheel
WHEEL=$(ls dist/*.whl | tail -n 1)
echo "Post-processing wheel: $WHEEL"

# If PACK_FFMPEG_RUNTIME_DIRS is set, use it. Otherwise attempt to find ffmpeg
# with `which ffmpeg` and convert its path from .../bin/ffmpeg to .../lib and pass
# that as the explicit --dirs argument with --noprobe to avoid probing other dirs.
if [ -n "${PACK_FFMPEG_RUNTIME_DIRS:-}" ]; then
  python tools/dependency_inject.py "$WHEEL" --dirs "$PACK_FFMPEG_RUNTIME_DIRS" --noprobe
else
  FFMPEG_PATH=$(which ffmpeg 2>/dev/null || true)
  if [ -n "$FFMPEG_PATH" ]; then
    # Replace trailing /bin/ffmpeg (or \bin\ffmpeg on weird setups) with /lib
    FFMPEG_DIR=$(dirname "$FFMPEG_PATH")
    # If ffmpeg is in .../bin, prefer sibling ../lib; otherwise try replacing bin->lib
    if [ "$(basename "$FFMPEG_DIR")" = "bin" ]; then
      CAND_LIB_DIR=$(cd "$FFMPEG_DIR/.." && pwd)/lib
    else
      CAND_LIB_DIR=$(dirname "$FFMPEG_PATH")
    fi
    if [ -d "$CAND_LIB_DIR" ]; then
      python tools/dependency_inject.py "$WHEEL" --dirs "$CAND_LIB_DIR" --noprobe
    else
      # fallback to noprobe without dirs (script will use built-in candidates)
      python tools/dependency_inject.py "$WHEEL" --noprobe
    fi
  else
    # No ffmpeg found on PATH, run with --noprobe so we don't probe sibling dirs but
    # allow the script to probe its default candidate locations.
    python tools/dependency_inject.py "$WHEEL" --noprobe
  fi
fi

echo "Done. Wheel updated: $WHEEL"
