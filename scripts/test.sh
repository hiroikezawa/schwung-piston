#!/usr/bin/env bash
# Native build + every test: engine (ASan/UBSan), real dlopen smoke, UI.
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
bash "$MODULE/scripts/build.sh" native
OUT="$MODULE/build/native"
"${CC:-gcc}" -std=c11 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Werror -fno-math-errno \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  -I"$MODULE/build/generated" \
  "$MODULE/tests/test_engine.c" $(ls "$MODULE"/dsp/*.c | grep -v hkh_plugin.c) \
  -o "$OUT/test-engine" -lm -pthread
rm -rf "$OUT/tmp" && mkdir -p "$OUT/tmp"
HKH_TEST_DIR="$OUT/tmp" "$OUT/test-engine"
python3 "$MODULE/tests/smoke.py" "$OUT/dsp.so"
if [[ -f "$MODULE/tests/test_ui.mjs" ]]; then
  if command -v node >/dev/null 2>&1; then
    node "$MODULE/tests/test_ui.mjs"
  elif command -v node.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
    node.exe "$(wslpath -w "$MODULE/tests/test_ui.mjs")"
  else
    echo "Node.js is required for the UI test" >&2; exit 1
  fi
fi
# The Move build too, when a cross compiler is here: check_abi.py is the only
# thing that notices a libm symbol bound to a glibc newer than Move's 2.34
# (log10f did exactly that on this toolchain), and it only runs on arm64.
if command -v "${CROSS_PREFIX:-aarch64-linux-gnu-}gcc" >/dev/null 2>&1; then
  bash "$MODULE/scripts/build.sh" arm64
else
  echo "SKIP: no aarch64 cross compiler; ABI not checked" >&2
fi
