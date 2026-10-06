/* Piston - UI tests: drives ui_core.mjs against a fake Schwung host
 * and a small mock of the DSP's command contract. Run with node. */
import assert from "node:assert/strict";
import fs from "node:fs";
import {
    createUi, padAt, noteAt, computeLeds, parseUiState, defaultState, knobStep, C,
    MODE_HAT, PARAM_KEYS, stepPad, padStep,
} from "../ui_core.mjs";

/* ---- a mock DSP that speaks the same keys as dsp/hkh_plugin.c ------------- */
function mockDsp() {
    const d = {
        running: 0, step: -1, kp: 0x1111, hp: 0x4444, kmute: 0, hmute: 0, kfill: 0, hfill: 0,
        kmodel: 1, ksample: 0, hmodel: 1, mode: 0, rec: 0, len: 8,
        params: Object.fromEntries(PARAM_KEYS.map((k, i) =>
            [k, [0.8, 0.25, 0.5, 0.15, 0.25, 0, 0, 0.7, 0.5, 0.25, 0.1, 0.2, 0.5, 0][i]])),
        motion: new Array(16).fill(-1),
    };
    d.set = (key, value) => {
        const v = Number(value);
        if (key in d.params) { d.params[key] = v; if (key === "h_decay" && d.rec && d.running) d.motion[d.step] = v; return; }
        switch (key) {
        case "k_step": d.kp ^= 1 << v; break;
        case "h_step": d.hp ^= 1 << v; break;
        case "k_mute": d.kmute = v; break;
        case "h_mute": d.hmute = v; break;
        case "k_fill": d.kfill = v; break;
        case "h_fill": d.hfill = v; break;
        case "k_reset": d.kp = 0x1111; d.kfill = 0; break;
        case "h_reset": d.hp = 0x4444; d.hfill = 0; d.rec = 0; d.motion.fill(-1); d.params.h_decay = 0.25; break;
        case "h_offbeat": d.hp = 0x4444; break;
        case "mode": d.mode = v; break;
        case "length": d.len = v; break;
        case "k_model_pick": d.kmodel = v; break;
        case "k_sample_pick": d.ksample = v; break;
        case "h_model_pick": d.hmodel = v; break;
        case "h_motion_rec": d.rec = v; break;
        default: break;
        }
    };
    d.uiState = () => [2, d.running, d.step, d.kp, d.hp, d.kfill ? 0xF1 : d.kp, d.hfill ? 0xFF : d.hp,
        d.kmute, d.hmute, d.kfill, d.hfill, d.kmodel, d.ksample, d.hmodel, d.mode, d.rec, 0, d.len,
        ...PARAM_KEYS.map((k) => Math.round(d.params[k] * 1000)),
        ...d.motion.map((m) => (m < 0 ? -1 : Math.round(m * 1000))), 250, 206].join(",");
    return d;
}

function rig() {
    const dsp = mockDsp();
    const r = {
        dsp, sets: [], leds: {}, ledLog: [], padBlock: [], on: true, t: 1000,
        snapshot: Object.fromEntries(Array.from({ length: 32 }, (_, i) => [String(68 + i), 45])),
        ledFail: new Set(), printed: [],
    };
    r.host = {
        setParam: (k, v) => { r.sets.push([k, v]); dsp.set(k, v); return true; },
        getParam: (k) => (k === "ui_state" ? dsp.uiState() : null),
        padBlock: (on) => r.padBlock.push(on),
        sendLed: (note, color) => {
            if (r.ledFail.has(note)) return false;
            r.leds[note] = color; r.ledLog.push([note, color]); return true;
        },
        padSnapshot: () => ({ ...r.snapshot }),
        displayOn: () => r.on,
        now: () => r.t,
        gfx: {
            clear: () => { r.printed = []; },
            print: (x, y, text) => { r.printed.push(text); assert(x >= 0 && x < 128 && y >= 0 && y < 64); },
            fillRect: (x, y, w, h) => assert(x >= 0 && y >= 0 && x + w <= 128 && y + h <= 64, `rect ${x},${y},${w},${h}`),
            drawRect: (x, y, w, h) => assert(x >= 0 && y >= 0 && x + w <= 128 && y + h <= 64),
            textWidth: (t) => t.length * 5,
        },
    };
    r.ui = createUi(r.host);
    r.ui.init();
    r.tick = (n = 1) => { for (let i = 0; i < n; i++) { r.t += 16; r.ui.tick(); } };
    r.press = (note, vel = 100) => r.ui.onMidi([0x90, note, vel]);
    r.release = (note) => r.ui.onMidi([0x80, note, 0]);
    r.tap = (note) => { r.press(note); r.release(note); };
    r.turn = (knob, delta) => r.ui.onMidi([0xB0, 71 + knob, delta > 0 ? delta : 128 + delta]);
    r.jog = (delta) => r.ui.onMidi([0xB0, 14, delta > 0 ? delta : 128 + delta]);
    r.click = () => { r.ui.onMidi([0xB0, 3, 127]); r.ui.onMidi([0xB0, 3, 0]); };
    r.touch = (knob, down) => r.ui.onMidi(down ? [0x90, knob, 127] : [0x90, knob, 0]);
    r.last = () => r.sets[r.sets.length - 1];
    r.keys = () => r.sets.map((s) => s[0]);
    r.clear = () => { r.sets.length = 0; };
    r.tick();
    return r;
}

/* ---- the grid ------------------------------------------------------------- */
assert.deepEqual(padAt(92), { row: 0, col: 0 });   /* top-left = ROW 1 PAD 1 */
assert.deepEqual(padAt(99), { row: 0, col: 7 });
assert.deepEqual(padAt(84), { row: 1, col: 0 });
assert.deepEqual(padAt(76), { row: 2, col: 0 });
assert.deepEqual(padAt(68), { row: 3, col: 0 });   /* bottom-left = ROW 4 PAD 1 */
assert.deepEqual(padAt(75), { row: 3, col: 7 });
assert.equal(padAt(67), null);
assert.equal(padAt(100), null);
for (let n = 68; n <= 99; n++) { const p = padAt(n); assert.equal(noteAt(p.row, p.col), n); }

/* ---- parseUiState ---------------------------------------------------------- */
assert(defaultState());
assert.equal(parseUiState(null), null);
assert.equal(parseUiState(""), null);
assert.equal(parseUiState("1" + defaultStateText().slice(1)), null);
assert.equal(parseUiState(defaultStateText() + ",5"), null);
assert.equal(parseUiState(defaultStateText().replace(",4369,", ",x,")), null);
function defaultStateText() { return mockDsp().uiState(); }
const ds = parseUiState(defaultStateText());
assert.equal(ds.kp, 0x1111);
assert.equal(ds.hp, 0x4444);
assert.equal(ds.len, 8);
assert.equal(ds.params.h_decay, 0.25);
assert.deepEqual(ds.motion, new Array(16).fill(null));
/* Step pads: 8 steps on the bottom row; 16 as row 3 = 1-8, row 4 = 9-16. */
for (let i = 0; i < 8; i++) { assert.equal(stepPad(i, 8), noteAt(3, i)); assert.equal(padStep(3, i, 8), i); }
assert.equal(padStep(2, 0, 8), -1);
for (let i = 0; i < 16; i++) {
    const p = padAt(stepPad(i, 16));
    assert.equal(padStep(p.row, p.col, 16), i);
}
assert.deepEqual(padAt(stepPad(0, 16)), { row: 2, col: 0 });
assert.deepEqual(padAt(stepPad(8, 16)), { row: 3, col: 0 });

/* ---- one voice per surface; steps on the bottom ---------------------------- */
{
    const r = rig();
    r.tick();
    assert.equal(r.leds[noteAt(0, 7)], C.KICK, "top-right: KICK");
    assert.equal(r.leds[noteAt(3, 0)], C.KICK, "kick step 1 on the bottom row");
    assert.equal(r.leds[noteAt(3, 2)], C.OFF);
    for (let c = 0; c < 8; c++) assert.equal(r.leds[noteAt(2, c)], C.OFF, "row 3 unused at 8 steps");
    assert.equal(r.leds[noteAt(1, 0)], C.ANALOG_DIM);
    assert.equal(r.leds[noteAt(1, 1)], C.DIGITAL_DIM);
    for (let c = 2; c < 8; c++) assert.equal(r.leds[noteAt(1, c)], C.OFF);
    for (let c = 5; c < 7; c++) assert.equal(r.leds[noteAt(0, c)], C.OFF);
    r.clear();
    r.tap(noteAt(3, 2));
    r.tap(noteAt(2, 2));                           /* row 3: nothing at 8 steps */
    assert.deepEqual(r.sets, [["k_step", "2"]]);
    /* Switch: the same pads now belong to the hat. */
    r.clear();
    r.tap(noteAt(0, 7));
    assert.deepEqual(r.sets, [["mode", "1"]]);
    r.tick();
    assert.equal(r.leds[noteAt(0, 7)], C.HAT);
    assert.equal(r.leds[noteAt(3, 2)], C.HAT, "hat step 3");
    assert.equal(r.leds[noteAt(3, 0)], C.OFF, "no kick on the hat surface");
    assert.equal(r.leds[noteAt(1, 0)], C.MODEL_DIM);
    assert.equal(r.leds[noteAt(1, 1)], C.OFFBEAT_DIM);
    r.clear();
    r.tap(noteAt(3, 5));
    r.tap(noteAt(0, 0)); r.tap(noteAt(0, 1)); r.tap(noteAt(0, 2)); r.tap(noteAt(0, 3));
    r.tap(noteAt(1, 1));
    r.tap(noteAt(0, 5)); r.tap(noteAt(0, 6)); r.tap(noteAt(1, 4));   /* empty pads */
    assert.deepEqual(r.sets, [["h_step", "5"], ["h_mute", "1"], ["h_reset", "1"],
        ["h_shuffle", "1"], ["h_random", "1"], ["h_offbeat", "1"]]);
    r.clear();
    r.tap(noteAt(0, 7));
    r.tap(noteAt(0, 0));
    assert.deepEqual(r.sets, [["mode", "0"], ["k_mute", "1"]], "row 1 follows the voice");
}

/* ---- selectors: hold row 2, the first step pads choose -------------------- */
{
    const r = rig();
    r.clear();
    r.press(noteAt(1, 0));                         /* ANALOG held */
    r.tick();
    assert.equal(r.leds[noteAt(3, 1)], C.WHITE, "current model white");
    assert.equal(r.leds[noteAt(3, 0)], C.ANALOG);
    assert.equal(r.leds[noteAt(3, 4)], C.OFF);
    r.tap(noteAt(3, 2));
    r.tap(noteAt(3, 5));                           /* beyond the 3 models: ignored */
    r.release(noteAt(1, 0));
    r.press(noteAt(1, 1));                         /* DIGITAL held */
    r.tap(noteAt(3, 3));
    r.release(noteAt(1, 1));
    r.tap(noteAt(3, 3));                           /* a step again */
    assert.deepEqual(r.sets, [["k_model_pick", "2"], ["k_sample_pick", "3"], ["k_step", "3"]]);
    r.tap(noteAt(0, 7));
    r.clear();
    r.press(noteAt(1, 0));                         /* MODEL held (hat) */
    r.tap(noteAt(3, 3));
    r.release(noteAt(1, 0));
    assert.deepEqual(r.sets, [["h_model_pick", "3"]]);
}

/* ---- the jog: page 2 switches 8 / 16 steps (default 8) ---------------------- */
{
    const r = rig();
    assert.equal(r.ui.state.len, 8);
    r.clear();
    r.click();                                     /* page 1: click does nothing */
    assert.deepEqual(r.sets, []);
    r.jog(1);
    r.tick();
    assert(r.printed.includes("STEPS (KICK+HAT)"));
    r.click();
    assert.deepEqual(r.sets, [["length", "16"]]);
    r.jog(-1);
    r.tick(4);
    assert(!r.printed.includes("STEPS (KICK+HAT)"), "back to the play page");
    assert.equal(r.ui.state.len, 16);
    /* 16 steps: row 3 = 1-8, row 4 = 9-16. */
    assert.equal(r.leds[noteAt(2, 0)], C.KICK, "step 1 top");
    assert.equal(r.leds[noteAt(3, 0)], C.KICK, "step 9 bottom");
    r.clear();
    r.tap(noteAt(2, 3));
    r.tap(noteAt(3, 7));
    assert.deepEqual(r.sets, [["k_step", "3"], ["k_step", "15"]]);
    r.jog(1); r.click();
    assert.equal(r.last()[1], "8");
}

/* ---- FILL is momentary, restated while held, released with the screen ------ */
{
    const r = rig();
    r.clear();
    r.press(noteAt(0, 4));
    assert.deepEqual(r.last(), ["k_fill", "1"]);
    r.tick(12);
    assert(r.sets.filter((x) => x[0] === "k_fill" && x[1] === "1").length >= 2, "heartbeat");
    r.release(noteAt(0, 4));
    assert.deepEqual(r.last(), ["k_fill", "0"]);
    /* Switching voice while holding FILL lets it go. */
    r.press(noteAt(0, 4));
    r.tap(noteAt(0, 7));
    assert(r.sets.some((x, i) => x[0] === "k_fill" && x[1] === "0" && i > 0));
    r.clear();
    r.tick(20);
    assert(!r.keys().includes("k_fill"));
}

/* ---- touching HAT DECAY records motion, and only DECAY, and only in HAT ---- */
{
    const r = rig();
    r.clear();
    r.touch(2, true); r.touch(2, false);           /* KICK: no recording */
    assert(!r.keys().includes("h_motion_rec"));
    r.tap(noteAt(0, 7));
    r.clear();
    r.touch(2, true);
    assert.deepEqual(r.last(), ["h_motion_rec", "1"]);
    r.turn(2, 10);
    r.touch(2, false);
    const k = r.keys();
    assert.equal(k[k.length - 2], "h_decay");
    assert.deepEqual(r.last(), ["h_motion_rec", "0"]);
    r.touch(2, true);
    r.tap(noteAt(0, 7));                           /* switch away mid-touch */
    assert(!r.ui.recording);
}

/* ---- LEDs: playhead, open hat, fill ------------------------------------------ */
{
    const s = defaultState();
    const ui = { held: new Set(), sel: null };
    s.running = true; s.step = 4;
    let l = computeLeds(s, ui);
    assert.equal(Object.keys(l).length, 32);
    assert.equal(l[noteAt(3, 4)], C.WHITE, "playhead on an active step");
    s.mode = MODE_HAT; s.motion[6] = 0.8;
    l = computeLeds(s, ui);
    assert.equal(l[noteAt(3, 6)], C.OPEN, "recorded open hat is blue");
    assert.equal(l[noteAt(3, 4)], C.LIGHT, "playhead on an empty step");
    s.hfill = true; s.hpe = 0xFF;
    l = computeLeds(s, ui);
    assert.equal(l[noteAt(3, 0)], C.FILL);
}

/* ---- the screen goes away: pads and LEDs go back to Move -------------------- */
{
    const r = rig();
    r.tick();
    r.press(noteAt(0, 4));
    r.on = false;
    r.ledLog.length = 0;
    r.clear();
    r.tick();
    assert.deepEqual(r.last(), ["k_fill", "0"]);
    assert.equal(r.padBlock[r.padBlock.length - 1], 0);
    assert.equal(r.ledLog.length, 32);
    r.on = true;
    r.ledLog.length = 0;
    r.tick();
    assert.equal(r.ledLog.length, 32);
    assert.equal(r.ui.handleBack(), false);
}

/* ---- Move overwrote a pad: we put ours back ---------------------------------- */
{
    const r = rig();
    r.tick(2);
    r.ledLog.length = 0;
    r.snapshot[String(noteAt(3, 0))] = 99;
    r.tick();
    assert(r.ledLog.some(([note, color]) => note === noteAt(3, 0) && color === C.KICK));
}

/* ---- the screen ------------------------------------------------------------- */
{
    const r = rig();
    r.tick();
    assert(r.printed.includes("KICK"));
    r.tap(noteAt(0, 7));
    r.tick();
    assert(r.printed.includes("HAT"));
    r.press(noteAt(1, 0));
    r.tick();
    assert(r.printed.includes("HAT MODEL") && r.printed.includes("METAL"));
    r.release(noteAt(1, 0));
    r.touch(2, true);
    r.tick(2);
    assert(r.printed.includes("REC"));
    r.touch(2, false);
    r.tap(noteAt(0, 7));
    r.touch(5, true);
    r.tick(4);
    assert(r.printed.some((t) => t.startsWith("out -12dB") && t.includes("dsp 0.2.6")), r.printed.join("|"));
    r.touch(5, false);
}

/* ---- knob acceleration ------------------------------------------------------- */
assert.equal(knobStep(1000), 0.005);
assert.equal(knobStep(10), 0.04);
assert(knobStep(80) > 0.005 && knobStep(80) < 0.04);

/* ---- the files the host will load ------------------------------------------- */
const chain = fs.readFileSync(new URL("../ui_chain.js", import.meta.url), "utf8");
assert(chain.includes("globalThis.chain_ui"));
assert(chain.includes("from './ui_core.mjs'"));
assert(!/globalThis\.(init|tick)\s*=/.test(chain), "must not override the shadow UI's own init/tick");
const manifest = JSON.parse(fs.readFileSync(new URL("../module.json", import.meta.url), "utf8"));
assert.equal(manifest.ui_chain, "ui_chain.js");
assert.equal(manifest.capabilities.ui_hierarchy, undefined, "a hierarchy would replace the pad UI");

console.log("PASS: one voice per surface, selectors, jog 8/16 steps, FILL, DECAY touch, LEDs, screen hand-back");
