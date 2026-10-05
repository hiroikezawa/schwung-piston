#!/usr/bin/env python3
"""Embed module.json's chain_params in the DSP so get_param never reads a file.

Also cross-checks the manifest against the engine's own key table and default
table, so the knob metadata the host shows can never drift from what the DSP
actually does with a key.
"""
import json
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
manifest = json.loads((root / "module.json").read_text())
params = manifest["capabilities"]["chain_params"]

engine = (root / "dsp" / "hkh_engine.c").read_text()
keys = re.findall(r'"([kh]_[a-z]+)"', engine.split("hkh_param_keys[P_COUNT] = {", 1)[1].split("};", 1)[0])
assert [p["key"] for p in params] == keys, ("module.json chain_params out of order with hkh_param_keys", keys)
defaults_src = engine.split("hkh_param_defaults[P_COUNT] = {", 1)[1].split("};", 1)[0]
defaults_src = defaults_src.replace("HKH_HAT_CLOSED_DECAY", "0.25f")
defaults = [float(x.strip().rstrip("f")) for x in defaults_src.split(",") if x.strip()]
assert [float(p["default"]) for p in params] == defaults, ("module.json defaults drift", defaults)
for p in params:
    assert p["type"] == "float" and p["min"] == 0 and p["max"] == 1, p

# The screen shows HKH_DSP_BUILD as "dsp X.Y.Z"; it must name this release.
plugin = (root / "dsp" / "hkh_plugin.c").read_text()
build = int(re.search(r"#define HKH_DSP_BUILD (\d+)", plugin).group(1))
major, minor, patch = (int(x) for x in manifest["version"].split("."))
assert build == major * 10000 + minor * 100 + patch, ("HKH_DSP_BUILD does not match module.json version", build, manifest["version"])

literal = json.dumps(json.dumps(params, separators=(",", ":")), ensure_ascii=True)
out = pathlib.Path(sys.argv[1])
out.write_text("/* Generated from module.json by scripts/metadata.py. Do not edit. */\n"
               "static const char hkh_chain_params[] = " + literal + ";\n")
