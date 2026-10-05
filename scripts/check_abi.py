#!/usr/bin/env python3
"""Move runs glibc 2.34; reject a dsp.so that needs anything newer."""
import re
import subprocess
import sys
path = sys.argv[1]
output = subprocess.check_output(["readelf", "--version-info", path], text=True)
versions = {tuple(map(int, s.split("."))) for s in re.findall(r"GLIBC_([0-9.]+)", output)}
assert versions and max(versions) <= (2, 34), f"Move-incompatible GLIBC: {sorted(versions)}"
header = subprocess.check_output(["readelf", "-h", path], text=True)
assert "AArch64" in header, "not an AArch64 build"
symbols = subprocess.check_output(["readelf", "-Ws", path], text=True)
assert "move_plugin_init_v2" in symbols, "v2 entry point not exported"
print("PASS: AArch64, v2 export, max GLIBC", ".".join(map(str, max(versions))))
