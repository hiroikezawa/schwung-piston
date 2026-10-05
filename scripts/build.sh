#!/usr/bin/env bash
# Build the Piston module.
#   scripts/build.sh native   desktop dsp.so for the tests (build/native)
#   scripts/build.sh arm64    Move dsp.so + install tarball (dist/)
set -euo pipefail
ID=piston
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
# Schwung's plugin API header ships in third_party/ (MIT, see its LICENSE),
# so the module builds on its own, outside the Schwung tree.
API="$MODULE/third_party/schwung"
case "${1:-arm64}" in
  native) CC="${CC:-gcc}"; OUT="$MODULE/build/native" ;;
  arm64) CC="${CROSS_PREFIX:-aarch64-linux-gnu-}gcc"; OUT="$MODULE/dist/$ID" ;;
  *) echo "Usage: $0 [native|arm64]" >&2; exit 2 ;;
esac
GEN="$MODULE/build/generated"
mkdir -p "$OUT" "$GEN"
python3 "$MODULE/scripts/metadata.py" "$GEN/hkh_metadata.h"
if [[ -f "$MODULE/scripts/gen_digital_kicks.py" ]]; then
  python3 "$MODULE/scripts/gen_digital_kicks.py" "$GEN/hkh_digital_kicks.h"
fi
# -fno-math-errno keeps sqrtf/fabsf inline on ARM (newer Ubuntu otherwise binds
# sqrtf@GLIBC_2.43, which Move's glibc 2.34 does not have). No -ffast-math:
# the finite() guards on the audio path must stay real.
"$CC" -std=c11 -O2 -fno-math-errno -Wall -Wextra -Werror -fPIC -shared -Wl,-z,defs \
  -I"$API" -I"$GEN" "$MODULE"/dsp/*.c -o "$OUT/dsp.so" -lm -pthread
cp "$MODULE/module.json" "$OUT/"
if [[ "${1:-arm64}" == arm64 ]]; then
  python3 "$MODULE/scripts/check_abi.py" "$OUT/dsp.so"
  for f in ui_chain.js ui_core.mjs help.json README.md; do
    [[ -f "$MODULE/$f" ]] && cp "$MODULE/$f" "$OUT/"
  done
  cp "$MODULE/LICENSE" "$OUT/"
  tar -czf "$MODULE/dist/$ID-module.tar.gz" -C "$MODULE/dist" "$ID"
fi
file "$OUT/dsp.so"
