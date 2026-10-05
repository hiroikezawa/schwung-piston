#!/usr/bin/env python3
"""dlopen the real dsp.so through the v2 ABI with a fake host transport."""
import ctypes as c
import json
import pathlib
import random
import sys

LIB = sys.argv[1]
BLOCK = 128

LOGFN = c.CFUNCTYPE(None, c.c_char_p)
SENDFN = c.CFUNCTYPE(c.c_int, c.POINTER(c.c_uint8), c.c_int)
INTFN = c.CFUNCTYPE(c.c_int)
BPMFN = c.CFUNCTYPE(c.c_float)
BEATFN = c.CFUNCTYPE(c.c_double)


class Host(c.Structure):
    _fields_ = [("api_version", c.c_uint32), ("sample_rate", c.c_int),
                ("frames_per_block", c.c_int), ("mapped_memory", c.c_void_p),
                ("audio_out_offset", c.c_int), ("audio_in_offset", c.c_int),
                ("log", c.c_void_p), ("midi_send_internal", c.c_void_p),
                ("midi_send_external", c.c_void_p), ("get_clock_status", c.c_void_p),
                ("mod_emit_value", c.c_void_p), ("mod_clear_source", c.c_void_p),
                ("mod_host_ctx", c.c_void_p), ("get_bpm", BPMFN),
                ("midi_inject_to_move", c.c_void_p), ("slot_recv_channel", c.c_void_p),
                ("get_beat_position", BEATFN), ("reserved", c.c_void_p * 8)]


assert Host.get_beat_position.offset == 112 and Host.reserved.offset == 120


class API(c.Structure):
    _fields_ = [("version", c.c_uint32)] + [(k, c.c_void_p) for k in
               ("create", "destroy", "midi", "set", "get", "error", "render")]


transport = {"beat": -1.0, "bpm": 120.0}
bpm_cb = BPMFN(lambda: transport["bpm"])
beat_cb = BEATFN(lambda: transport["beat"])
host = Host(api_version=1, sample_rate=44100, frames_per_block=BLOCK,
            get_bpm=bpm_cb, get_beat_position=beat_cb)

lib = c.CDLL(LIB)
lib.move_plugin_init_v2.restype = c.POINTER(API)
lib.move_plugin_init_v2.argtypes = [c.c_void_p]
bad = Host(api_version=1, sample_rate=48000, frames_per_block=BLOCK)
assert not lib.move_plugin_init_v2(c.byref(bad)), "must refuse a 48 kHz host"
api = lib.move_plugin_init_v2(c.byref(host)).contents
assert api.version == 2

create = c.CFUNCTYPE(c.c_void_p, c.c_char_p, c.c_char_p)(api.create)
destroy = c.CFUNCTYPE(None, c.c_void_p)(api.destroy)
midi = c.CFUNCTYPE(None, c.c_void_p, c.POINTER(c.c_uint8), c.c_int, c.c_int)(api.midi)
set_param = c.CFUNCTYPE(None, c.c_void_p, c.c_char_p, c.c_char_p)(api.set)
get_param = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_char_p, c.c_char_p, c.c_int)(api.get)
render = c.CFUNCTYPE(None, c.c_void_p, c.POINTER(c.c_int16), c.c_int)(api.render)

buf = (c.c_int16 * (BLOCK * 2))()
p = create(b".", None)
assert p


def read(key, size=8192, inst=None):
    out = c.create_string_buffer(size)
    n = get_param(inst or p, key.encode(), out, size)
    return None if n < 0 else out.value.decode()


def write(key, value, inst=None):
    set_param(inst or p, key.encode(), str(value).encode())


def block(inst=None):
    render(inst or p, buf, BLOCK)
    if transport["beat"] >= 0:
        transport["beat"] += BLOCK / (44100 * 60 / transport["bpm"])
    return list(buf)


def ui_state(inst=None):
    f = read("ui_state", inst=inst).split(",")
    assert f[0] == "2" and len(f) == 18 + 14 + 16 + 2, f
    return [int(x) for x in f]


# --- contract with the shadow UI -------------------------------------------
assert read("ui_hierarchy") == "", "must be SERVED and EMPTY (opens ui_chain.js)"
meta = json.loads(read("chain_params"))
manifest = json.loads((pathlib.Path(__file__).parent.parent / "module.json").read_text())
assert meta == manifest["capabilities"]["chain_params"]
assert len(meta) == 14
assert read("no_such_key") is None

# --- silence keeps the slot awake, and nothing else -------------------------
for i in range(50):
    out = block()
    assert all(x == 6 for x in out), "silence is a 6 LSB DC offset (-75 dBFS), nothing else"

# --- the default groove -------------------------------------------------------
s = ui_state()
assert s[1] == 0 and s[2] == -1                 # stopped, no step
assert s[3] == 0x1111 and s[4] == 0x4444        # X---X--- / --X---X-
assert s[17] == 8
assert s[14] == 0 and s[11] == 1 and s[13] == 1  # KICK mode, 909 kick, 909 hat
transport["beat"] = 0.0
energy = 0
for _ in range(400):
    out = block()
    energy += sum(x * x for x in out)
s = ui_state()
assert s[1] == 1 and 0 <= s[2] < 8
assert energy > 1e6, "running transport must sound"

# --- pad commands -------------------------------------------------------------
write("k_step", 2)
assert ui_state()[3] == 0x1115
write("k_step", 2)
write("h_step", 0)
assert ui_state()[4] == 0x4445
write("h_offbeat", 1)
assert ui_state()[4] == 0x4444
write("k_mute", 1)
assert ui_state()[7] == 1 and ui_state()[3] == 0x1111
write("k_mute", 0)
write("mode", 1)
assert ui_state()[14] == 1
write("k_model_pick", 2)
write("k_sample_pick", 3)
write("h_model_pick", 2)
s = ui_state()
assert (s[11], s[12], s[13]) == (2, 3, 2)
for bad_val in ["3", "-1", "1.5", "nan", "x", ""]:
    write("k_model_pick", bad_val)
    write("h_model", bad_val if bad_val != "3" else "4")
s = ui_state()
assert (s[11], s[13]) == (2, 2), "invalid selections must be ignored"

for _ in range(20):
    before = ui_state()[3]
    write("k_random", 1)
    assert ui_state()[3] != before
    before = ui_state()[4]
    write("h_shuffle", 1)
    assert ui_state()[4] != before
write("k_reset", 1)
write("h_reset", 1)
write("k_fill", 1)
assert ui_state()[5] == 0xF1 and ui_state()[3] == 0x1111 and ui_state()[9] == 1
write("k_fill", 0)
assert ui_state()[5] == 0x1111
write("length", 16)
assert ui_state()[17] == 16
write("k_fill", 1)
assert ui_state()[5] == 0xF111
write("k_fill", 0)
for bad in ["12", "0", "x"]:
    write("length", bad)
    assert ui_state()[17] == 16
write("length", 8)

# --- continuous params ----------------------------------------------------------
for key in [m["key"] for m in meta]:
    write(key, 0.37)
    assert abs(float(read(key)) - 0.37) < 1e-4
    write(key, 5)
    assert float(read(key)) == 1.0, "clamped"
    for junk in ["nan", "inf", "-inf", "0.5x", "", " "]:
        write(key, junk)
        assert float(read(key)) == 1.0
    write(key, next(m["default"] for m in meta if m["key"] == key))

# --- state round trip ------------------------------------------------------------
write("k_step", 5)
write("h_decay", 0.61)
state = read("state")
doc = json.loads(state)
assert doc["v"] == 1 and doc["kp"] == 0x1131 and doc["mode"] == 1 and doc["len"] == 8
other = create(b".", None)
assert other and other != p
write("state", json.dumps(doc, indent=2), inst=other)     # pretty-printed is fine
assert json.loads(read("state", inst=other)) == doc
before = read("state", inst=other)
for invalid in ["{}", "{", "", "[]", state[:-2], state.replace('"v":1', '"v":2'),
                state.replace('"kp":4401', '"kp":70000'), state.replace('"len":8', '"len":12'), state.replace('"km":2', '"km":3'),
                state.replace('"p":[', '"p":[9,'), state.replace('"mo":[', '"mo":[2,')]:
    write("state", invalid, inst=other)
    assert read("state", inst=other) == before, invalid
rng = random.Random(7)
for _ in range(3000):
    junk = "".join(rng.choice('{}[]:,."0123456789e+-vkpmo ') for _ in range(rng.randrange(120)))
    write("state", junk, inst=other)
    assert read("state", inst=other) == before
old = '{"v":1,"mode":0,"km":1,"ks":0,"hm":1,"kp":17,"hp":68,"p":[%s],"mo":[0.5,-1,-1,-1,-1,-1,-1,-1]}' % ",".join(["0.5"] * 14)
write("state", old, inst=other)
d = json.loads(read("state", inst=other))
assert d["kp"] == 17 and d["len"] == 8 and d["mo"][0] == 0.5 and d["mo"][8] == -1, d
destroy(other)

# --- MIDI notes -----------------------------------------------------------------
transport["beat"] = -1.0
block()
write("k_mute", 0)
write("h_mute", 0)
for _ in range(200):
    block()


def note(n, v=110):
    midi(p, (c.c_uint8 * 3)(0x90, n, v), 3, 0)


note(36)
e = sum(x * x for x in block())
assert e > 1e5, "note 36 plays the kick"
midi(p, (c.c_uint8 * 3)(0x90, 200, 100), 3, 0)   # data byte out of range
midi(p, (c.c_uint8 * 3)(0x90, 36, 100), 2, 0)    # short message
midi(p, (c.c_uint8 * 1)(0xFE), 1, 0)             # active sensing: ignored

# --- bounds -------------------------------------------------------------------
for key in ["state", "ui_state", "chain_params", "k_vol"]:
    small = c.create_string_buffer(3)
    assert get_param(p, key.encode(), small, 3) == -1 and small.value == b""
for frames in [1, 17, 128]:
    guarded = (c.c_int16 * (2 * frames + 2))()
    guarded[0] = 1234
    guarded[-1] = 5678
    render(None, c.cast(c.byref(guarded, 2), c.POINTER(c.c_int16)), frames)
    assert guarded[0] == 1234 and guarded[-1] == 5678
    assert not any(guarded[1:-1])

# --- DECAY motion, end to end through the plugin API -------------------------
m = create(b".", None)
assert m
write("mode", 1, inst=m)
transport["beat"] = 0.0
block(m)


def advance_to(step, inst):
    for _ in range(2000):
        if ui_state(inst)[2] == step:
            return
        block(inst)
    raise AssertionError("never reached step %d" % step)


advance_to(2, m)                                 # hat step 3: touch, tight
write("h_decay", 0.2, inst=m)
write("h_motion_rec", 1, inst=m)
assert ui_state(m)[15] == 1
advance_to(3, m)
write("h_motion_rec", 0, inst=m)                 # release
advance_to(6, m)                                 # hat step 7: touch, open
write("h_motion_rec", 1, inst=m)
write("h_decay", 0.8, inst=m)
advance_to(7, m)
write("h_motion_rec", 0, inst=m)
motion = ui_state(m)[32:48]
assert motion[2] == 200 and motion[3] == 200, motion   # step 3 (and 4, passed while held)
assert motion[6] == 800 and motion[7] == 800, motion
assert motion[0] == -1 and motion[5] == -1, motion     # never touched
write("h_decay", 0.5, inst=m)                          # released: base only
assert ui_state(m)[32:48] == motion
doc = json.loads(read("state", inst=m))
assert abs(doc["mo"][2] - 0.2) < 1e-3 and abs(doc["mo"][6] - 0.8) < 1e-3 and doc["mo"][0] == -1
# The motion survives a save/restore into a fresh instance...
n2 = create(b".", None)
write("state", json.dumps(doc), inst=n2)
assert ui_state(n2)[32:48] == motion
destroy(n2)
# ...and RESET is the way home: no motion, closed decay, offbeat pattern.
write("h_step", 0, inst=m)
write("h_reset", 1, inst=m)
s_ = ui_state(m)
assert s_[32:48] == [-1] * 16 and s_[4] == 0x4444 and s_[18 + 9] == 250 and s_[15] == 0
transport["beat"] = -1.0
destroy(m)

# --- a module-local WAV replaces a digital slot, loaded off-thread ----------
import math
import os
import tempfile
import time
import wave
tmp = tempfile.mkdtemp()
os.makedirs(os.path.join(tmp, "samples"))
with wave.open(os.path.join(tmp, "samples", "kick3.wav"), "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(22050)
    w.writeframes(b"".join(int(12000 * math.sin(2 * math.pi * 60 * i / 22050)).to_bytes(2, "little", signed=True)
                           for i in range(11025)))
q = create(tmp.encode(), None)
assert q
for _ in range(200):
    if ui_state(q)[16]:
        break
    time.sleep(0.01)
assert ui_state(q)[16] == 1 << 2, "kick3.wav loaded into slot 3 only"
destroy(q)

# --- pool ---------------------------------------------------------------------
handles = [p] + [create(b".", None) for _ in range(5)]
assert all(handles) and len(set(handles)) == 6
assert not create(b".", None), "pool is fixed; the 7th is refused, not allocated"
for h in handles:
    destroy(h)
assert create(b".", None)
print("PASS: v2 ABI, UI contract, keepalive, pad commands, state, MIDI, bounds, pool")
