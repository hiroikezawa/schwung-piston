#!/usr/bin/env python3
"""Measure the RUMBLE demo WAVs (scripts/render_demo.sh -> build/demo).

For each rumble_<bpm>bpm_def<NNN>.wav: per 16th of the beat, averaged over
beats 4-11, the peak (dBFS) and the quietest 5 ms in that 16th's last 40 %
(the gap before the next hit). Slot 1 is the kick, 2-4 the repeats.
"""
import math
import pathlib
import re
import struct
import sys
import wave

demo = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/demo")
for path in sorted(demo.glob("rumble_*bpm_def*.wav")):
    bpm = float(re.search(r"_(\d+)bpm", path.name).group(1))
    with wave.open(str(path)) as w:
        raw = w.readframes(w.getnframes())
        sr = w.getframerate()
    x = [s / 32768.0 for s in struct.unpack("<%dh" % (len(raw) // 2), raw)[0::2]]
    spb = sr * 60.0 / bpm
    d = spb / 4.0
    peak = [0.0] * 4
    gap = [0.0] * 4
    beats = range(4, 12)
    for b in beats:
        for q in range(4):
            s0 = int(b * spb + q * d)
            seg = x[s0:int(s0 + d)]
            peak[q] += 20 * math.log10(max(abs(v) for v in seg) + 1e-9) / len(beats)
            lo = 1e9
            for i in range(int(s0 + 0.6 * d), int(s0 + d) - 220, 55):
                r = math.sqrt(sum(v * v for v in x[i:i + 220]) / 220)
                lo = min(lo, 20 * math.log10(r + 1e-9))
            gap[q] += lo / len(beats)
    top = 20 * math.log10(max(abs(v) for v in x) + 1e-9)
    print(f"{path.name:32s} peak {top:5.1f} dBFS | " +
          "  ".join(f"[{q + 1}] {peak[q]:5.1f} gap {gap[q]:5.1f}" for q in range(4)))
