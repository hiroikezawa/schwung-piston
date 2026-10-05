#!/usr/bin/env bash
# Render audition WAVs with the real engine into build/demo/ (no Move needed).
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
GEN="$MODULE/build/generated"
OUT="$MODULE/build/demo"
mkdir -p "$GEN" "$OUT"
python3 "$MODULE/scripts/gen_digital_kicks.py" "$GEN/hkh_digital_kicks.h"
"${CC:-gcc}" -std=c11 -D_DEFAULT_SOURCE -O2 -fno-math-errno -Wall -Wextra -Werror \
  -I"$GEN" "$MODULE/tests/render_demo.c" $(ls "$MODULE"/dsp/*.c | grep -v hkh_plugin.c) \
  -o "$MODULE/build/render-demo" -lm -pthread
"$MODULE/build/render-demo" "$OUT"
