#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT_DIR"

# Build wheel using the normal PEP517 path
python -m build --wheel

# Find the produced wheel
WHEEL=$(ls dist/*.whl | tail -n 1)
echo "Post-processing wheel: $WHEEL"

# Optionally, pass PACK_FFMPEG_RUNTIME_DIRS via env var or default probe
if [ -n "${PACK_FFMPEG_RUNTIME_DIRS:-}" ]; then
  # convert ; separated into os.pathsep style for script
  python tools/package_ffmpeg_into_wheel.py "$WHEEL" --dirs "$PACK_FFMPEG_RUNTIME_DIRS"
else
  python tools/package_ffmpeg_into_wheel.py "$WHEEL"
fi

echo "Done. Wheel updated: $WHEEL"
