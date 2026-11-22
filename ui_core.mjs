/*
 * Piston - pad performance surface.
 *
 * Pure logic: every host service (param channel, LEDs, pad block, display) is
 * injected, so tests/test_ui.mjs drives exactly this file under node. The
 * wiring to Schwung's real globals is ui_chain.js.
 *
 * The DSP is the single source of truth. Pads send commands; the screen and
 * the LEDs are drawn from one `ui_state` read (a CSV the DSP composes), plus
 * optimistic local updates so a press lights up in the same frame.
 */

/* ---- the surface --------------------------------------------------------- */

/* Move's pads: 68-99, rows ascending from the BOTTOM (68-75 is the bottom
 * row). ROW 1 in the spec is the top row. */
export const ROW_BASE = [92, 84, 76, 68];

export function padAt(note) {
    if (note < 68 || note > 99) return null;
    const i = note - 68;
    return { row: 3 - Math.floor(i / 8), col: i % 8 };
}

export function noteAt(row, col) { return ROW_BASE[row] + col; }

/* Three surfaces -- KICK, HAT, RACK -- and the top-right pad steps through
 * them in that order. On KICK and HAT:
 *   row 1: five performance pads, the voice's two selectors, the switch
 *   row 2: eight presets for this voice (tap recalls, Shift+tap saves)
 *   rows 3-4: its steps (8 steps: row 4 only; 16: row 3 = 1-8, row 4 = 9-16)
 * RACK is drawn by rackLeds below. */
export const TOP_ROWS = [
    ["mute", "reset", "shuffle", "random", "fill", "analog", "digital", null],   /* KICK */
    ["mute", "reset", "shuffle", "random", "fill", "model", "offbeat", "rec"],   /* HAT */
];
export const PRESETS = 8;

/* The rack: 24 pads in a 6 x 4 block (pad 1 top left, 24 bottom right of
 * the block). Columns 7-8 are not part of it; the top-right pad is the
 * surface switch as everywhere. Holding a pad in the top two rows turns the
 * bottom two into its steps, and the other way round. */
export const RACK_COLS = 8;
export const RACK_PADS = 32;
export function rackPadAt(row, col) {
    return col < RACK_COLS ? row * RACK_COLS + col : -1;
}
/* The step area for a held rack pad, and which step a pad there is. */
export function rackStepRows(pad) { return pad < 16 ? [2, 3] : [0, 1]; }
export function rackStepPad(pad, i, len) {
    const rows = rackStepRows(pad);
    if (i >= len) return -1;
    return noteAt(rows[i < 8 ? 0 : 1], i % 8);
}
export function rackPadStep(pad, row, col, len) {
    const rows = rackStepRows(pad);
    if (row === rows[0]) return col;
    if (row === rows[1] && len === 16) return 8 + col;
    return -1;
}

/* A held pad's period button: the top-right pad of its own half (its left
 * neighbour when that is the held pad). Each press: whole > 3 > 5 > 7. */
export const RACK_PERIODS = [0, 3, 5, 7];
export function rackPeriodPad(held) {
    const corner = held < 16 ? 7 : 23;
    return held === corner ? corner - 1 : corner;
}
/* How many steps pad plays, and the step it is on (-1 stopped). */
export function rackLen(s, pad) { return s.rpl[pad] || s.len; }
export function rackStepOf(s, pad) {
    if (s.step < 0) return -1;
    return s.rpl[pad] ? s.abs % s.rpl[pad] : s.step;
}

/* Which pad holds step i (0-based), and which step a pad holds (-1 none). */
export function stepPad(i, len) {
    if (len === 16) return noteAt(i < 8 ? 2 : 3, i % 8);
    return i < 8 ? noteAt(3, i) : -1;
}
export function padStep(row, col, len) {
    if (len === 16) return row === 2 ? col : row === 3 ? 8 + col : -1;
    return row === 3 ? col : -1;
}

export const MODE_KICK = 0;
export const MODE_HAT = 1;
export const MODE_RACK = 2;
export const MODES = 3;

/* Knob 1-8 in each mode. DECAY is knob 3 in both. Knob 8 is reserved. */
export const KNOBS = [
    ["k_vol", "k_mix", "k_decay", "k_drive", "k_comp", "k_rumble", "k_verb", null],
    ["h_vol", "h_color", "h_decay", "h_drive", "h_comp", "h_filter", null, null],   /* no hat reverb */
];
export const KNOB_LABELS = [
    ["VOL", "MIX", "DEC", "DRV", "CMP", "RMB", "VRB", "PAGE"],
    ["VOL", "MTL", "DEC", "DRV", "CMP", "FLT", "-", "PAGE"],
];
export const KNOB_NAMES = [
    ["VOLUME", "ANALOG/DIGITAL", "DECAY", "DRIVE", "COMP", "RUMBLE", "REVERB", "PAGE"],
    ["VOLUME", "COLOR/METAL", "DECAY", "DRIVE", "COMP", "FILTER", "-", "PAGE"],
];
/* The DSP's 14 params in its order; h_verb stays a (now silent) param so
 * saved states keep their shape. */
export const PARAM_KEYS = [...KNOBS[0].slice(0, 7), ...KNOBS[1].slice(0, 6), "h_verb"];

/* Page 2 (turn the jog): each part's effect page, in real units -- the
 * order of dsp/hkh_busfx.h. KICK FX = TUBE on the kick only, HAT FX =
 * HARMONICS on the hat only, RACK FX = BUS COMP on the whole output. */
const pct = (v) => `${Math.round(v * 100)}%`;
const db1 = (v) => `${v > 0 ? "+" : ""}${v.toFixed(1)}`;
export const FX_KEYS = ["kt_sat", "kt_drive", "hh_amt", "hh_freq", "bc_thresh", "bc_ratio",
    "bc_attack", "bc_release", "bc_schpf", "bc_makeup", "bc_punch"];
export const FX_DEFS = {
    kt_sat: { min: 0, max: 1, res: 0.01, def: 0, label: "SAT", name: "TUBE SAT", show: pct },
    kt_drive: { min: 0, max: 24, res: 0.5, def: 6, label: "DRIVE", name: "TUBE DRIVE", show: (v) => `${v.toFixed(1)}dB` },
    hh_amt: { min: 0, max: 1, res: 0.01, def: 0, label: "HARM", name: "HARMONICS", show: pct },
    hh_freq: { options: ["1.9k", "2.8k", "6.6k", "9.8k"], def: 2, label: "FREQ", name: "HARM FREQ" },
    bc_thresh: { min: -36, max: 0, res: 0.5, def: -12, label: "THRSH", name: "THRESHOLD", show: (v) => `${v.toFixed(1)}dB`, cell: (v) => v.toFixed(1) },
    bc_ratio: { options: ["OFF", "2:1", "4:1", "10:1"], def: 0, label: "RATIO", name: "RATIO" },
    bc_attack: { min: 1, max: 30, res: 0.5, def: 30, label: "ATK", name: "ATTACK", show: (v) => `${v.toFixed(1)}ms`, cell: (v) => v.toFixed(1) },
    bc_release: { min: 50, max: 600, res: 10, def: 150, label: "REL", name: "RELEASE", show: (v) => `${Math.round(v)}ms`, cell: (v) => String(Math.round(v)) },
    bc_schpf: { min: 20, max: 200, res: 5, def: 90, label: "SCHPF", name: "SC HPF", show: (v) => `${Math.round(v)}Hz`, cell: (v) => String(Math.round(v)) },
    bc_makeup: { min: -12, max: 12, res: 0.5, def: 0, label: "MKUP", name: "MAKEUP", show: (v) => `${db1(v)}dB`, cell: db1 },
    bc_punch: { options: ["OFF", "ON"], def: 1, label: "PUNCH", name: "PUNCH" },
};
export const FX_KNOBS = [
    ["kt_sat", "kt_drive", null, null, null, null, null, null],
    ["hh_amt", "hh_freq", null, null, null, null, null, null],
    ["bc_thresh", "bc_ratio", "bc_attack", "bc_release", "bc_schpf", "bc_makeup", "bc_punch", null],
];
export const FX_TITLES = ["KICK FX", "HAT FX", "BUS COMP"];
export function fxText(key, v) {
    const d = FX_DEFS[key];
    return d.options ? d.options[Math.round(v)] || "?" : d.show(v);
}
/* The same value for a 32-pixel cell of the knob overview: at most five
 * characters, so neighbours never run into each other. The unit is left
 * out where the full text would not fit; the touched-knob view shows it. */
export function fxCellText(key, v) {
    const d = FX_DEFS[key];
    return d.cell && !d.options ? d.cell(v) : fxText(key, v);
}
/* Where a value sits on its knob's travel, 0..1 (for the bar). */
export function fxPos(key, v) {
    const d = FX_DEFS[key];
    return d.options ? Math.round(v) / (d.options.length - 1) : (v - d.min) / (d.max - d.min);
}
const ENUM_TICKS = 10;         /* encoder counts per choice on an option knob */

/* KICK page 2: RUMBLE. The rb_* keys and their order are dsp/hkh_fx.h's
 * (RB_*); knob 1 is the RUMBLE amount itself, k_rumble -- the same value as
 * page 1's knob 6. All 0..1 except DELAY, a division index that the DSP
 * holds at 1/8 (0.11.1), so it has no knob. */
export const RB_KEYS = ["rb_delay", "rb_fb", "rb_def", "rb_damp", "rb_duck"];
export const RB_DIVS = ["1/8", "1/16", "1/32"];
export const RB_DEFS = {
    k_rumble: { min: 0, max: 1, res: 0.001, label: "RMB", name: "RUMBLE", show: pct },
    rb_delay: { options: RB_DIVS, label: "DLY", name: "DELAY" },
    rb_fb: { min: 0, max: 1, res: 0.001, label: "FB", name: "FEEDBACK", show: pct },
    rb_def: { min: 0, max: 1, res: 0.001, label: "DEF", name: "DEFINITION", show: pct },
    rb_damp: { min: 0, max: 1, res: 0.001, label: "DAMP", name: "DAMPING", show: pct },
    rb_duck: { min: 0, max: 1, res: 0.001, label: "DUCK", name: "DUCK", show: pct },
};
export const RB_KNOBS = ["k_rumble", "rb_fb", "rb_def", "rb_damp", "rb_duck", null, null, null];
/* Short names in page 1's layout (knob 8 still turns the part). */
export const RB_LABELS = ["RMB", "FB", "DEF", "DAMP", "DUCK", "-", "-", "PAGE"];
export const RB_TITLE = "RUMBLE";
export function rbText(key, v) {
    const d = RB_DEFS[key];
    return d.options ? d.options[Math.round(v)] || "?" : d.show(v);
}
/* Where a value sits on its knob's travel, 0..1 (for the bar). */
export function rbPos(key, v) {
    const d = RB_DEFS[key];
    return d.options ? Math.round(v) / (d.options.length - 1) : v;
}

export const KICK_MODELS = ["808", "909", "IND"];
/* The four built-in digital kicks (scripts/gen_digital_kicks.py); a slot
 * holding the user's own WAV shows as USR1-5 instead. */
export const DIGITAL_NAMES = ["PUNCH", "SUB", "CRUSH", "HARD", "GABBER"];
export function digitalName(s, i) {
    if (i < 0 || i >= DIGITAL_NAMES.length) return "?";
    return ((s.userMask >> i) & 1) ? `USR${i + 1}` : DIGITAL_NAMES[i];
}
export const HAT_MODELS = ["808", "909", "METAL", "INDUS"];

/* What each held select pad offers on the step row, and what it sets. */
const PICKERS = {
    analog: { title: "ANALOG KICK", names: KICK_MODELS, field: "kmodel", color: 2, key: "k_model_pick" },
    digital: { title: "DIGITAL KICK", names: DIGITAL_NAMES, field: "ksample", color: 14, key: "k_sample_pick" },
    model: { title: "HAT MODEL", names: HAT_MODELS, field: "hmodel", color: 9, key: "h_model_pick" },
};

const KNOB_CC_FIRST = 71;
const JOG_TURN = 14;
const JOG_CLICK = 3;
/* Jog pages: 0 = play. KICK: 1 = RUMBLE, 2 = KICK FX (TUBE); HAT and RACK:
 * 1 = that part's effects. Every page past 0: jog click = 8 / 16 steps. */
export function pagesFor(mode) { return mode === MODE_KICK ? 3 : 2; }
export function fxPageFor(mode) { return mode === MODE_KICK ? 2 : 1; }
const SHIFT_CC = 49;
const PAGE_TURN_MS = 1000;     /* knob 8: a pause this long forgets a part-turn */
export const PAGE_TICKS = 40;  /* knob 8: encoder counts per page (~120 degrees) */
export const PRESET_FILE = "/data/UserData/schwung/piston-presets.json";
const DECAY_KNOB = 2;          /* knob 3 */

/* Move palette indices (src/shared/constants.mjs names in comments). */
export const C = {
    OFF: 0,
    WHITE: 120,           /* White */
    LIGHT: 118,           /* LightGrey */
    KICK: 1,              /* BrightRed */
    KICK_DIM: 65,         /* DeepRed */
    KICK_FAINT: 66,       /* VeryDarkRed */
    HAT: 7,               /* VividYellow */
    HAT_DIM: 73,          /* DarkYellow */
    HAT_FAINT: 74,        /* VeryDarkYellow */
    OPEN: 16,             /* AzureBlue: an open (long DECAY) hat step */
    OPEN_DIM: 95,         /* DarkAzure */
    LIVE: 83,             /* DarkGreen: voice on air */
    MUTED: 1,             /* BrightRed */
    FILL: 3, FILL_DIM: 72,          /* BrightOrange / DarkOrange */
    SHUFFLE: 19, SHUFFLE_DIM: 101,  /* Violet */
    RANDOM: 17, RANDOM_DIM: 97,     /* RoyalBlue */
    ANALOG: 2, ANALOG_DIM: 67,      /* OrangeRed */
    DIGITAL: 14, DIGITAL_DIM: 91,   /* CyanTeal */
    MODEL: 9, MODEL_DIM: 81,        /* BrightLime */
    OFFBEAT: 23, OFFBEAT_DIM: 109,  /* NeonPink */
    RACK: 14, RACK_DIM: 91,         /* CyanTeal: the rack switch, pads with a sample */
    RACK_STEP: 16,                  /* AzureBlue: a held pad's steps */
    /* a held pad's period button: whole / 3 / 5 / 7 */
    RACK_PERIOD: { 0: 83, 3: 3, 5: 19, 7: 23 },
};

/* Rack pad colours, picked with knob 7 while a pad is held. Index 0 is the
 * default (C.RACK); each entry is [with steps, without steps]. */
export const RACK_COLORS = [
    { name: "TEAL", short: "TE", on: 14, dim: 91 },
    { name: "RED", short: "RE", on: 1, dim: 65 },
    { name: "ORANGE", short: "OR", on: 3, dim: 69 },
    { name: "YELLOW", short: "YE", on: 7, dim: 77 },
    { name: "LIME", short: "LI", on: 9, dim: 81 },
    { name: "GREEN", short: "GR", on: 11, dim: 85 },
    { name: "CYAN", short: "CY", on: 15, dim: 93 },
    { name: "AZURE", short: "AZ", on: 16, dim: 95 },
    { name: "BLUE", short: "BL", on: 17, dim: 97 },
    { name: "VIOLET", short: "VI", on: 19, dim: 101 },
    { name: "PURPLE", short: "PU", on: 22, dim: 107 },
    { name: "MAGENTA", short: "MG", on: 23, dim: 109 },
    { name: "PINK", short: "PK", on: 25, dim: 113 },
];
/* Colours belong to the samples (Rack/01-32.wav are shared by every Set),
 * so they live beside them in one file, not in a Set's state. */
export const RACK_COLOR_FILE = "/data/UserData/schwung/piston-rack-colors.json";
export function parseRackColors(text) {
    const out = new Array(RACK_PADS).fill(0);
    try {
        const a = JSON.parse(text).colors;
        if (Array.isArray(a))
            for (let i = 0; i < RACK_PADS; i++)
                if (Number.isInteger(a[i]) && a[i] >= 0 && a[i] < RACK_COLORS.length) out[i] = a[i];
    } catch (e) { /* missing or broken file: all default */ }
    return out;
}
export function serializeRackColors(c) { return JSON.stringify({ colors: c }); }

/* A held rack pad's knobs 1-6: its volume, its four sends and its LPF. */
const RACK_PAD_KEYS = ["r_vol", "r_dsend", "r_rsend", "r_csend", "r_msend", "r_lpf"];
const RACK_PAD_NAMES = ["VO", "DL", "RV", "CR", "RM", "LP"];   /* eight columns of 16 px: three letters collide */

/* The LPF knob's cutoff (as dsp/hkh_engine.c hkh_rack_lpf_hz), shortened to
 * three characters for its column: "-" open, "800", "2k4", "12k". */
export function lpfLabel(v) {
    if (v >= 1) return "-";
    const hz = 80 * Math.pow((0.45 * 44100) / 80, Math.max(0, v));
    if (hz < 1000) return String(Math.round(hz));
    if (hz < 10000) { const t = Math.round(hz / 100); return `${Math.floor(t / 10)}k${t % 10}`; }
    return `${Math.round(hz / 1000)}k`;
}

/* ---- ui_state ------------------------------------------------------------ */

/* The DSP's one-line state; see write_ui_state in dsp/hkh_plugin.c. */
export const UI_STATE_FIELDS = 18 + 14 + 16 + 2 + 2 + 32 + 32 + 1 + 1   // + the rack sidechain (0.6.3)
    + 11 + 2                                                             // + page-2 effects, GR, PUNCH limit (0.7.0)
    + 32 + 32                                                            // + rack DLY / REV sends (0.8.0)
    + 32 + 32                                                            // + rack CRUSH / RING sends (0.8.1)
    + 32                                                                 // + rack LPF (0.8.2)
    + 5                                                                  // + RUMBLE page (0.11.0)
    + 32 + 1;                                                            // + rack periods, 1/16 count (0.12.0)

export function parseUiState(text) {
    if (typeof text !== "string" || text.length === 0) return null;
    const f = text.split(",");
    if (f.length !== UI_STATE_FIELDS || f[0] !== "3") return null;
    const n = f.map((x) => Number(x));
    if (n.some((x) => !Number.isInteger(x))) return null;
    const params = {};
    PARAM_KEYS.forEach((k, i) => { params[k] = n[18 + i] / 1000; });
    const motion = [];
    for (let i = 0; i < 16; i++) {
        const m = n[32 + i];
        motion.push(m < 0 ? null : m / 1000);
    }
    return {
        running: n[1] === 1, step: n[2],
        kp: n[3], hp: n[4], kpe: n[5], hpe: n[6],
        kmute: n[7] === 1, hmute: n[8] === 1, kfill: n[9] === 1, hfill: n[10] === 1,
        kmodel: n[11], ksample: n[12], hmodel: n[13], mode: n[14],
        rec: n[15] === 1, userMask: n[16], len: n[17] === 16 ? 16 : 8, params, motion,
        rumbleOut: n[48] / 1000, build: n[49],
        rackMask: n[50], rackMute: n[51],
        rp: n.slice(52, 84), rv: n.slice(84, 116).map((x) => x / 1000), rfb: n[116] / 1000,
        rsc: n[117] / 1000,
        fx: Object.fromEntries(FX_KEYS.map((k, i) => [k, n[118 + i] / 10])),
        bcGr: n[129] / 10, bcLim: n[130] === 1,
        rds: n.slice(131, 163).map((x) => x / 1000), rrs: n.slice(163, 195).map((x) => x / 1000),
        rcs: n.slice(195, 227).map((x) => x / 1000), rms: n.slice(227, 259).map((x) => x / 1000),
        rlp: n.slice(259, 291).map((x) => x / 1000),
        /* RUMBLE page: DELAY as its index, the rest knob positions */
        rb: Object.fromEntries(RB_KEYS.map((k, i) => [k, i === 0 ? n[291] : n[291 + i] / 1000])),
        rpl: n.slice(296, 328), abs: n[328],
    };
}

export function defaultState() {
    return parseUiState("3,0,-1,4369,17476,4369,17476,0,0,0,0,1,0,1,0,0,0,8," +
        "800,250,500,150,250,500,0,700,500,0,100,200,500,0," +
        Array(16).fill(-1).join(",") + ",0,0,0,0," +
        Array(32).fill(0).join(",") + "," + Array(32).fill(800).join(",") + ",0,0," +
        FX_KEYS.map((k) => Math.round(FX_DEFS[k].def * 10)).join(",") + ",0,0," +
        Array(32).fill(1000).join(",") + "," + Array(96).fill(0).join(",") + "," + Array(32).fill(1000).join(",") +
        ",1,600,750,700,800," + Array(32).fill(0).join(",") + ",0");
}

/* ---- LEDs (pure) --------------------------------------------------------- */

const bit = (p, i) => ((p >> i) & 1) === 1;

/* The colour for a hat step: long DECAY hats are drawn in the "open" blue so
 * a recorded chick -> shhh motion is visible on the pads. */
function hatStepColor(s, i, focus, faint) {
    const d = s.motion[i] !== null ? s.motion[i] : s.params.h_decay;
    const open = d >= 0.4;      /* 40 % on the knob and up reads as open */
    if (faint) return open ? C.OPEN_DIM : C.HAT_FAINT;
    if (open) return focus ? C.OPEN : C.OPEN_DIM;
    return focus ? C.HAT : C.HAT_DIM;
}

/**
 * All 32 pad colours, keyed by note, for the voice on screen (s.mode).
 * ui: { held: Set(note), sel: "analog"|"digital"|"model"|null,
 *       presets: [[8 strings|null] kick, [8] hat], preset: [kick, hat] last used,
 *       rackHeld: pad index or -1 }
 */
export function computeLeds(s, ui) {
    const out = {};
    for (let note = 68; note <= 99; note++) out[note] = C.OFF;
    const put = (row, col, color) => { out[noteAt(row, col)] = color; };
    if (s.mode === MODE_RACK) return rackLeds(s, ui, out, put);
    const pressed = (row, col) => ui.held.has(noteAt(row, col));
    const hat = s.mode === MODE_HAT;
    const mute = hat ? s.hmute : s.kmute, fill = hat ? s.hfill : s.kfill;

    /* Row 1: the performance pads, then the switch (red KICK / yellow HAT). */
    put(0, 0, mute ? C.MUTED : C.LIVE);
    put(0, 1, pressed(0, 1) ? C.WHITE : C.LIGHT);
    put(0, 2, pressed(0, 2) ? C.SHUFFLE : C.SHUFFLE_DIM);
    put(0, 3, pressed(0, 3) ? C.RANDOM : C.RANDOM_DIM);
    put(0, 4, fill ? C.FILL : C.FILL_DIM);

    /* The voice's two selectors. */
    if (hat) {
        put(0, 5, ui.sel === "model" ? C.MODEL : C.MODEL_DIM);
        put(0, 6, pressed(0, 6) ? C.OFFBEAT : C.OFFBEAT_DIM);
        /* REC: armed = DECAY touch records motion; white while recording */
        put(0, 7, s.rec ? C.WHITE : ui.recArm ? C.MUTED : C.KICK_FAINT);
    } else {
        put(0, 5, ui.sel === "analog" ? C.ANALOG : C.ANALOG_DIM);
        put(0, 6, ui.sel === "digital" ? C.DIGITAL : C.DIGITAL_DIM);
    }

    /* Row 2: this voice's presets. White = the one last recalled or saved. */
    const v = hat ? 1 : 0;
    const slots = (ui.presets && ui.presets[v]) || [];
    for (let i = 0; i < PRESETS; i++) {
        if (pressed(1, i)) put(1, i, C.WHITE);
        else if (!slots[i]) put(1, i, C.OFF);
        else if (ui.preset && ui.preset[v] === i) put(1, i, C.WHITE);
        else put(1, i, hat ? C.HAT_DIM : C.KICK_DIM);
    }

    /* Rows 3-4: the steps, or while a selector is held, its choices on the
     * first step pads (current one white). */
    const pick = ui.sel && PICKERS[ui.sel];
    if (pick && (ui.sel === "model") === hat) {
        const cur = s[pick.field];
        for (let i = 0; i < pick.names.length; i++)
            out[stepPad(i, s.len)] = i === cur ? C.WHITE : pick.color;
        return out;
    }
    const p = hat ? (s.hfill ? s.hpe : s.hp) : (s.kfill ? s.kpe : s.kp);
    for (let i = 0; i < s.len; i++) {
        const on = bit(p, i);
        let color;
        if (s.running && s.step === i) color = on ? C.WHITE : C.LIGHT;
        else if (!on) color = C.OFF;
        else if (fill) color = C.FILL;
        else if (hat) color = hatStepColor(s, i, true, s.hmute);
        else color = s.kmute ? C.KICK_FAINT : C.KICK;
        out[stepPad(i, s.len)] = color;
    }
    return out;
}

function rackLeds(s, ui, out, put) {
    const held = ui.rackHeld;
    const hit = (pad) => s.running && s.step >= 0 && ((s.rp[pad] >> rackStepOf(s, pad)) & 1) === 1;
    for (let pad = 0; pad < RACK_PADS; pad++) {
        const row = Math.floor(pad / RACK_COLS), col = pad % RACK_COLS;
        const loaded = ((s.rackMask >> pad) & 1) === 1;
        const muted = ((s.rackMute >> pad) & 1) === 1;
        let color;
        const pc = RACK_COLORS[(ui.rackColors && ui.rackColors[pad]) || 0];
        /* the held pad is white, or its colour while knob 7 is choosing it */
        if (pad === held) color = ui.rackColorShow ? pc.on : C.WHITE;
        /* muted = its own colour, dim; otherwise bright whether or not it has steps */
        else if (muted) color = pc.dim;
        else if (!loaded) color = C.OFF;
        else if (hit(pad)) color = C.LIGHT;
        else color = pc.on;
        put(row, col, color);
    }
    if (held >= 0) {
        /* The held pad's steps cover the other half. */
        for (const row of rackStepRows(held)) for (let col = 0; col < 8; col++) put(row, col, C.OFF);
        const cur = rackStepOf(s, held), n = rackLen(s, held);
        for (let i = 0; i < n; i++) {
            const on = ((s.rp[held] >> i) & 1) === 1;
            let color = on ? C.RACK_STEP : C.OFF;
            if (s.running && cur === i) color = on ? C.WHITE : C.LIGHT;
            out[rackStepPad(held, i, s.len)] = color;
        }
        const pb = rackPeriodPad(held);
        put(Math.floor(pb / RACK_COLS), pb % RACK_COLS, C.RACK_PERIOD[s.rpl[held]]);
    }
    return out;
}

/* ---- presets ------------------------------------------------------------- */

/* The preset file: {"kick":[8 x string|null],"hat":[8 x string|null]}.
 * Anything unreadable starts empty rather than refusing to run. */
export function parsePresets(text) {
    const empty = () => Array(PRESETS).fill(null);
    const out = [empty(), empty()];
    if (typeof text !== "string" || !text) return out;
    let j;
    try { j = JSON.parse(text); } catch (e) { return out; }
    ["kick", "hat"].forEach((k, v) => {
        const a = j && Array.isArray(j[k]) ? j[k] : [];
        for (let i = 0; i < PRESETS; i++) out[v][i] = typeof a[i] === "string" && a[i] ? a[i] : null;
    });
    return out;
}
export function serializePresets(p) {
    return JSON.stringify({ kick: p[0], hat: p[1] });
}

/* ---- encoder decoding ---------------------------------------------------- */

export function decodeDelta(v) {
    if (v >= 1 && v <= 63) return v;
    if (v >= 65 && v <= 127) return v - 128;
    return 0;
}

/* Slow turn: 0.5 % per detent (fine); fast spin: up to 4 %. */
export function knobStep(elapsedMs) {
    if (!(elapsedMs > 0) || elapsedMs >= 140) return 0.005;
    if (elapsedMs <= 20) return 0.04;
    return 0.005 + (0.035 * (140 - elapsedMs)) / 120;
}

/* A held rack pad's volume (knob 1) moves at a third of the other knobs. */
const RACK_VOL_SCALE = 1 / 3;

/* ---- the controller ------------------------------------------------------- */

const READ_EVERY_TICKS = 3;        /* ~20 Hz: the playhead LED */
const HEARTBEAT_MS = 150;          /* restate held FILL / DECAY touch (DSP lets go at 500) */
const TOUCH_SHOW_MS = 900;         /* the big knob readout lingers after release */
const REASSERT_EVERY_TICKS = 6;    /* one row of our LEDs re-sent round-robin */

/**
 * host = {
 *   setParam(key, value) -> bool, getParam(key) -> string|null,
 *   padBlock(on), sendLed(note, color) -> bool, padSnapshot() -> {note: color}|null,
 *   displayOn() -> bool, now() -> ms,
 *   readFile(path) -> string|null, writeFile(path, text) -> bool,
 *   gfx: { clear(), print(x, y, text, color), fillRect(x, y, w, h, c),
 *          drawRect(x, y, w, h, c), textWidth(text) }
 * }
 */
export function createUi(host) {
    let s = defaultState();
    const ui = { held: new Set(), sel: null, presets: null, preset: [-1, -1], rackHeld: -1, recArm: false,
                 rackColors: null, rackColorShow: false, rackColorDirty: false };
    let shiftHeld = false;
    let lastPageTurn = -1e9;
    let pageAcc = 0;
    let flash = "";                    /* a one-line confirmation, e.g. SAVED 3 */
    let flashUntil = 0;
    const selHeld = { analog: false, digital: false, model: false };
    let fillHeld = [false, false];
    let recHeld = false;
    let touched = -1;
    let touchShowUntil = 0;
    let lastTouchKnob = -1;
    const pending = new Map();         /* continuous param writes, flushed per tick */
    const lastTurn = new Array(8).fill(0);
    let ledCache = {};
    let moveSnap = null;               /* Move's own pad colours, last seen */
    let tickCount = 0;
    let readDue = true;
    let wasOn = false;
    let lastHeartbeat = 0;
    let reassertRow = 0;
    let dirty = true;
    let page = 0;
    const enumAcc = new Array(8).fill(0);   /* option knobs: counts toward the next choice */

    const set = (key, value) => host.setParam(key, String(value));

    function refresh() {
        const next = parseUiState(host.getParam("ui_state"));
        if (next) { s = next; dirty = true; }
        readDue = false;
    }

    function flushPending() {
        for (const [key, value] of pending) {
            const rk = /^(r_vol|r_dsend|r_rsend|r_csend|r_msend|r_lpf):(\d+)$/.exec(key);
            if (rk) set(rk[1], `${rk[2]}:${value.toFixed(4)}`);
            else set(key, value.toFixed(4));
        }
        pending.clear();
    }

    /* Let go of everything a finger was holding. Called whenever our screen
     * goes away, so no FILL or recording outlives the surface that owned it
     * (the DSP's own heartbeat is the backstop if even this is missed). */
    function releaseAll() {
        flushPending();
        if (fillHeld[0]) set("k_fill", 0);
        if (fillHeld[1]) set("h_fill", 0);
        if (recHeld) set("h_motion_rec", 0);
        fillHeld = [false, false];
        recHeld = false;
        selHeld.analog = selHeld.digital = selHeld.model = false;
        ui.sel = null;
        ui.held.clear();
        ui.rackHeld = -1;
        shiftHeld = false;
        touched = -1;
    }

    /* Shift: the shim does not forward CC 49 to a module's screen, so the
     * host's own shift flag is asked; the CC is still honoured if it comes. */
    function isShift() {
        return shiftHeld || (host.shiftHeld ? host.shiftHeld() : false);
    }

    function loadPresets() {
        if (ui.presets) return;
        ui.presets = parsePresets(host.readFile ? host.readFile(PRESET_FILE) : null);
    }

    function loadRackColors() {
        if (ui.rackColors) return;
        ui.rackColors = parseRackColors(host.readFile ? host.readFile(RACK_COLOR_FILE) : null);
    }
    /* Written once the pad is let go, not on every detent. */
    function saveRackColors() {
        if (!ui.rackColorDirty || ui.rackHeld >= 0) return;
        ui.rackColorDirty = false;
        if (host.writeFile) host.writeFile(RACK_COLOR_FILE, serializeRackColors(ui.rackColors));
    }

    function say(text) {
        flash = text;
        flashUntil = host.now() + 1200;
        dirty = true;
    }

    function onPreset(slot) {
        loadPresets();
        const hat = s.mode === MODE_HAT, v = hat ? 1 : 0;
        const key = hat ? "h_preset" : "k_preset";
        if (isShift()) {
            const text = host.getParam(key);
            if (typeof text !== "string" || !text) { say("SAVE FAILED"); return; }
            const next = ui.presets.map((a) => a.slice());
            next[v][slot] = text;
            if (host.writeFile && host.writeFile(PRESET_FILE, serializePresets(next))) {
                ui.presets = next;
                ui.preset[v] = slot;
                say(`SAVED ${hat ? "HAT" : "KICK"} ${slot + 1}`);
            } else {
                say("SAVE FAILED");
            }
            return;
        }
        const text = ui.presets[v][slot];
        if (!text) { say(`EMPTY ${slot + 1}  (SHIFT+PAD SAVES)`); return; }
        set(key, text);
        ui.preset[v] = slot;
        readDue = true;
        say(`${hat ? "HAT" : "KICK"} PRESET ${slot + 1}`);
    }

    function nextMode(dir = 1) {
        const hat = s.mode === MODE_HAT;
        if (s.mode !== MODE_RACK) {
            const prefix = hat ? "h_" : "k_";
            if (fillHeld[hat ? 1 : 0]) { set(prefix + "fill", 0); fillHeld[hat ? 1 : 0] = false; }
            if (hat) stopRecording();
        }
        selHeld.analog = selHeld.digital = selHeld.model = false;
        updateSel();
        ui.rackHeld = -1;
        /* KICK | HAT | RACK in a line: the ends stop, nothing wraps. */
        const next = Math.min(MODES - 1, Math.max(0, s.mode + (dir < 0 ? -1 : 1)));
        if (next === s.mode) return;
        set("mode", next);
        s.mode = next;
        /* Off the play page, land on the next part's effects page (only the
         * KICK has a RUMBLE page). */
        if (page > 0) page = fxPageFor(next);
    }

    /* ---- the rack surface ---- */
    function onRackPad(row, col, down) {
        const held = ui.rackHeld;
        if (held >= 0) {
            /* A pad is held: the other half is its steps. */
            const step = rackPadStep(held, row, col, s.len);
            if (step >= 0 || rackStepRows(held).includes(row)) {
                if (down && step >= 0 && step < rackLen(s, held)) {
                    set("r_step", `${held}:${step}`);
                    s.rp[held] ^= (1 << step);
                    readDue = true;
                }
                return;
            }
            const button = rackPeriodPad(held);
            if (rackPadAt(row, col) === button) {
                if (down) {
                    const i = RACK_PERIODS.indexOf(s.rpl[held]);
                    const next = RACK_PERIODS[(i + 1) % RACK_PERIODS.length];
                    set("r_period", `${held}:${next}`);
                    s.rpl[held] = next;
                    readDue = true;
                }
                return;
            }
            if (!down && rackPadAt(row, col) === held) {
                ui.rackHeld = -1;
                ui.rackColorShow = false;
                /* another pad of the same half may still be down */
                for (const n of ui.held) {
                    const q = padAt(n);
                    const p = q ? rackPadAt(q.row, q.col) : -1;
                    if (p >= 0 && p !== button) { ui.rackHeld = p; break; }
                }
            }
            return;
        }
        const pad = rackPadAt(row, col);
        if (pad < 0 || !down) return;
        if (isShift()) {
            /* Shift + pad: mute / unmute, no hold, no audition. */
            const on = ((s.rackMute >> pad) & 1) ? 0 : 1;
            set("r_mute", `${pad}:${on}`);
            s.rackMute ^= (1 << pad);
            readDue = true;
            return;
        }
        ui.rackHeld = pad;
        ui.rackColorShow = false;
        if (!s.running) set("r_trig", pad);
    }

    /* ---- pads ---- */
    function onPad(note, velocity) {
        const pos0 = padAt(note);
        if (pos0 && s.mode === MODE_RACK) {
            const down0 = velocity > 0;
            if (down0) ui.held.add(note); else ui.held.delete(note);
            dirty = true;
            onRackPad(pos0.row, pos0.col, down0);
            return;
        }
        return onVoicePad(note, velocity);
    }

    /* Hand the pads back to Move with Move's own colours. */
    function restoreLeds() {
        const snap = host.padSnapshot ? host.padSnapshot() : null;
        for (let note = 68; note <= 99; note++) {
            const c = snap && snap[String(note)] !== undefined ? snap[String(note)] : 0;
            host.sendLed(note, c);
        }
        ledCache = {};
        moveSnap = null;
    }

    function stopRecording() {
        if (!recHeld) return;
        flushPending();                 /* the last turn lands INSIDE the recording */
        set("h_motion_rec", 0);
        recHeld = false;
        s.rec = false;
    }

    function updateSel() {
        /* Whichever select pad is held picks; the kick pair is exclusive. */
        ui.sel = selHeld.model ? "model" : selHeld.analog ? "analog" : selHeld.digital ? "digital" : null;
    }

    function onVoicePad(note, velocity) {
        const pos = padAt(note);
        if (!pos) return;
        const down = velocity > 0;
        if (down) ui.held.add(note); else ui.held.delete(note);
        dirty = true;
        const { row, col } = pos;
        const hat = s.mode === MODE_HAT;
        const prefix = hat ? "h_" : "k_";

        if (row === 0) {
            const role = TOP_ROWS[hat ? 1 : 0][col];
            if (role === "analog" || role === "digital" || role === "model") {
                if (down && role !== "model") {
                    /* Kick pickers are exclusive: the newest press wins. */
                    selHeld.analog = role === "analog";
                    selHeld.digital = role === "digital";
                } else {
                    selHeld[role] = down;
                }
                updateSel();
                return;
            }
            if (role === "fill") {
                fillHeld[hat ? 1 : 0] = down;
                set(prefix + "fill", down ? 1 : 0);
                if (hat) s.hfill = down; else s.kfill = down;
                readDue = true;
                return;
            }
            if (!down) return;
            switch (role) {
            case "rec":
                /* arm / disarm DECAY motion recording; disarming ends a take */
                ui.recArm = !ui.recArm;
                if (!ui.recArm) stopRecording();
                say(ui.recArm ? "REC ARMED (TOUCH DECAY)" : "REC OFF");
                break;
            case "mute": {
                const on = hat ? !s.hmute : !s.kmute;
                set(prefix + "mute", on ? 1 : 0);
                if (hat) s.hmute = on; else s.kmute = on;
                break;
            }
            case "reset":
                /* RESET is the safe way home: it also ends a held FILL and,
                 * on the hat, a DECAY recording. */
                set(prefix + "reset", 1);
                fillHeld[hat ? 1 : 0] = false;
                if (hat) recHeld = false;
                readDue = true;
                break;
            case "shuffle": set(prefix + "shuffle", 1); readDue = true; break;
            case "random": set(prefix + "random", 1); readDue = true; break;
            case "offbeat":
                set("h_offbeat", 1);
                s.hp = 0x4444;
                readDue = true;
                break;
            case "toggle":
                /* The next surface. Anything held on this one is let go. */
                nextMode();
                break;
            default: break;                  /* unassigned */
            }
            return;
        }

        if (row === 1) {
            if (down) onPreset(col);
            return;
        }

        /* Rows 3-4: steps, or a held selector's choices. */
        if (!down) return;
        const step = padStep(row, col, s.len);
        if (step < 0) return;
        if (ui.sel) {
            const pick = PICKERS[ui.sel];
            if (step < pick.names.length) { set(pick.key, step); s[pick.field] = step; }
            return;
        }
        set(prefix + "step", step);
        if (hat) s.hp ^= (1 << step); else s.kp ^= (1 << step);
        readDue = true;
    }

    /* ---- jog: page 1 plays, page 2 = this part's effects (+ the length) ---- */
    function onJog(cc, value) {
        if (cc === JOG_TURN) {
            const d = decodeDelta(value);
            const was = page;
            if (d > 0 && page < pagesFor(s.mode) - 1) page++;
            else if (d < 0 && page > 0) page--;
            if (page !== was) { touched = -1; touchShowUntil = 0; }
            if (page > 0) stopRecording();
            dirty = true;
        } else if (cc === JOG_CLICK && value > 0 && page > 0) {
            const next = s.len === 16 ? 8 : 16;
            set("length", next);
            s.len = next;
            readDue = true;
            dirty = true;
        }
    }

    /* ---- knobs ---- */
    function onKnobTurn(knob, value) {
        if (page === 0 && s.mode === MODE_RACK && ui.rackHeld >= 0) {
            /* A held rack pad: knob 7 picks its colour, knob 8 does nothing. */
            if (knob === 7) return;
            if (knob === 6) {
                const d = decodeDelta(value);
                if (d === 0) return;
                if (Math.sign(d) !== Math.sign(enumAcc[knob])) enumAcc[knob] = 0;
                enumAcc[knob] += d;
                if (Math.abs(enumAcc[knob]) < ENUM_TICKS) return;
                loadRackColors();
                const n = RACK_COLORS.length, pad = ui.rackHeld;
                ui.rackColors[pad] = (ui.rackColors[pad] + Math.sign(enumAcc[knob]) + n) % n;
                enumAcc[knob] = 0;
                ui.rackColorShow = true;
                ui.rackColorDirty = true;
                dirty = true;
                return;
            }
        }
        if (knob === 7) {
            /* Knob 8 turns the page: KICK > HAT > RACK, one page per turn
             * gesture (a spin does not race through all three). */
            const d = decodeDelta(value), now = host.now();
            if (d === 0) return;
            /* About a third of a turn per page: counts accumulate (both
             * ways), and a pause or a change of direction starts over. */
            if (now - lastPageTurn >= PAGE_TURN_MS || Math.sign(d) !== Math.sign(pageAcc)) pageAcc = 0;
            lastPageTurn = now;
            pageAcc += d;
            const atEnd = (pageAcc > 0 && s.mode === MODES - 1) || (pageAcc < 0 && s.mode === 0);
            if (atEnd) { pageAcc = 0; return; }       /* no winding up past an end */
            while (Math.abs(pageAcc) >= PAGE_TICKS) {
                nextMode(pageAcc > 0 ? 1 : -1);
                pageAcc -= Math.sign(pageAcc) * PAGE_TICKS;
            }
            dirty = true;
            return;
        }
        if (page > 0) { onFxKnob(knob, value); return; }
        if (s.mode === MODE_RACK) {
            const pad = ui.rackHeld, d = decodeDelta(value);
            if (knob === 6 && d !== 0) {
                /* Knob 7: the rack delay's FB (0 = no delay). */
                const now = host.now();
                const step = knobStep(now - lastTurn[knob]);
                lastTurn[knob] = now;
                s.rfb = Math.min(1, Math.max(0, s.rfb + d * step));
                pending.set("r_fb", s.rfb);
                say(s.rfb > 0 ? `DELAY FB ${Math.round(s.rfb * 100)}%` : "DELAY OFF");
                return;
            }
            if (knob === 5 && d !== 0 && pad < 0) {
                /* Knob 6: the rack sidechain -- how far each kick ducks it
                 * (while a pad is held, knob 6 is that pad's LPF). */
                const now = host.now();
                const step = knobStep(now - lastTurn[knob]);
                lastTurn[knob] = now;
                s.rsc = Math.min(1, Math.max(0, (s.rsc || 0) + d * step));
                pending.set("r_sc", s.rsc);
                say(s.rsc > 0 ? `SIDECHAIN ${Math.round(s.rsc * 100)}%` : "SIDECHAIN OFF");
                return;
            }
            /* Hold a rack pad: knob 1 = its volume, 2-5 = its DLY, REV,
             * CRUSH and RING sends, 6 = its LPF. */
            if (pad < 0 || d === 0) return;
            const arr = [s.rv, s.rds, s.rrs, s.rcs, s.rms, s.rlp][knob];
            if (!arr) return;
            const now = host.now();
            const step = knobStep(now - lastTurn[knob]) * (knob === 0 ? RACK_VOL_SCALE : 1);
            lastTurn[knob] = now;
            const v = Math.min(1, Math.max(0, arr[pad] + d * step));
            arr[pad] = v;
            pending.set(`${RACK_PAD_KEYS[knob]}:${pad}`, v);
            dirty = true;
            return;
        }
        const key = KNOBS[s.mode][knob];
        const d = decodeDelta(value);
        if (!key || d === 0) return;
        const now = host.now();
        const step = knobStep(now - lastTurn[knob]);
        lastTurn[knob] = now;
        const v = Math.min(1, Math.max(0, s.params[key] + d * step));
        s.params[key] = v;
        pending.set(key, v);
        lastTouchKnob = knob;
        touchShowUntil = now + TOUCH_SHOW_MS;
        dirty = true;
    }

    /* The knobs of the page showing: RUMBLE, or the part's effects. */
    function pageKnobs() {
        if (s.mode === MODE_KICK && page === 1) return { keys: RB_KNOBS, defs: RB_DEFS, vals: s.rb };
        return { keys: FX_KNOBS[s.mode] || [], defs: FX_DEFS, vals: s.fx };
    }
    /* k_rumble lives with the page-1 params; everything else in its page. */
    const getV = (vals, key) => (key === "k_rumble" ? s.params.k_rumble : vals[key]);
    const putV = (vals, key, v) => { if (key === "k_rumble") s.params.k_rumble = v; else vals[key] = v; };

    /* Pages 2 (and KICK's 3): the page's knobs, in real units. */
    function onFxKnob(knob, value) {
        const { keys, defs, vals } = pageKnobs();
        const key = keys[knob];
        const d = decodeDelta(value);
        if (d === 0) return;
        const now = host.now();
        if (key) {
            const def = defs[key];
            let v = getV(vals, key);
            if (def.options) {
                if (Math.sign(d) !== Math.sign(enumAcc[knob])) enumAcc[knob] = 0;
                enumAcc[knob] += d;
                if (Math.abs(enumAcc[knob]) < ENUM_TICKS) return;
                v = Math.min(def.options.length - 1, Math.max(0, Math.round(v) + Math.sign(enumAcc[knob])));
                enumAcc[knob] = 0;
            } else {
                const step = knobStep(now - lastTurn[knob]);
                v += d * step * (def.max - def.min);
                v = Math.min(def.max, Math.max(def.min, Math.round(v / def.res) * def.res));
            }
            lastTurn[knob] = now;
            if (v === getV(vals, key)) return;
            putV(vals, key, v);
            pending.set(key, v);
        }
        lastTouchKnob = knob;
        touchShowUntil = now + TOUCH_SHOW_MS;
        dirty = true;
    }

    function onKnobTouch(knob, down) {
        if (knob === 7) return;                              /* knob 8 turns pages */
        if (knob === 0 && down && s.mode === MODE_HAT && isShift()) {
            /* Shift + knob 1 touch (HAT, any page): clear the DECAY motion.
             * Pattern and the DECAY base stay. */
            set("h_motion_clear", 1);
            s.motion = s.motion.map(() => null);
            readDue = true;
            say("MOTION CLEAR");
            return;
        }
        if (page > 0) {
            /* pages 2-3: a touch only shows the knob (no DECAY recording here) */
            const now = host.now();
            if (down) { touched = knob; lastTouchKnob = knob; }
            else if (touched === knob) touched = -1;
            touchShowUntil = now + TOUCH_SHOW_MS;
            dirty = true;
            return;
        }
        if (s.mode === MODE_RACK) return;
        const now = host.now();
        if (down) {
            touched = knob;
            lastTouchKnob = knob;
            touchShowUntil = now + TOUCH_SHOW_MS;
            if (s.mode === MODE_HAT && knob === DECAY_KNOB && !recHeld && ui.recArm) {
                /* With REC armed (top-right pad), touch IS the record button:
                 * DECAY motion is captured for exactly as long as the finger
                 * is on the knob. Disarmed, DECAY just sets the base. */
                recHeld = true;
                lastHeartbeat = now;
                set("h_motion_rec", 1);
                s.rec = true;
            }
        } else {
            if (touched === knob) touched = -1;
            touchShowUntil = now + TOUCH_SHOW_MS;
            if (knob === DECAY_KNOB) stopRecording();
        }
        dirty = true;
    }

    function onMidi(data) {
        if (!data || data.length < 3) return;
        const status = data[0] & 0xF0, d1 = data[1], d2 = data[2];
        if (status === 0x90 || status === 0x80) {
            const down = status === 0x90 && d2 > 0;
            if (d1 >= 68 && d1 <= 99) { onPad(d1, down ? d2 : 0); return; }
            if (d1 <= 7) { onKnobTouch(d1, down); return; }
            return;
        }
        if (status === 0xB0 && d1 === SHIFT_CC) { shiftHeld = d2 > 0; return; }
        if (status === 0xB0 && d1 >= KNOB_CC_FIRST && d1 < KNOB_CC_FIRST + 8) {
            onKnobTurn(d1 - KNOB_CC_FIRST, d2);
        } else if (status === 0xB0 && (d1 === JOG_TURN || d1 === JOG_CLICK)) {
            onJog(d1, d2);
        }
    }

    /* ---- LEDs ---- */
    function paintLeds() {
        const want = computeLeds(s, ui);
        /* Move repaints its own pads (a playing drum track flashes them).
         * Whatever Move changed since last frame is ours to put back now. */
        const snap = host.padSnapshot ? host.padSnapshot() : null;
        if (snap) {
            if (moveSnap) {
                for (let note = 68; note <= 99; note++) {
                    if (snap[String(note)] !== moveSnap[String(note)]) delete ledCache[note];
                }
            }
            moveSnap = snap;
        }
        /* And a slow round-robin backstop for anything the snapshot missed. */
        if (tickCount % REASSERT_EVERY_TICKS === 0) {
            for (let col = 0; col < 8; col++) delete ledCache[noteAt(reassertRow, col)];
            reassertRow = (reassertRow + 1) % 4;
        }
        for (let note = 68; note <= 99; note++) {
            const color = want[note];
            if (ledCache[note] === color) continue;
            /* Cache only what was actually queued, so a dropped packet is
             * retried next frame instead of being believed. */
            if (host.sendLed(note, color)) ledCache[note] = color;
        }
    }

    /* ---- screen ---- */
    function draw() {
        const g = host.gfx;
        const tw = (t) => (g.textWidth ? g.textWidth(t) : t.length * 6);
        g.clear();
        const hat = s.mode === MODE_HAT;

        /* Header: the knob target, inverted, so it reads from across a room. */
        g.fillRect(0, 0, 128, 11, 1);
        const rack = s.mode === MODE_RACK;
        const rbPage = s.mode === MODE_KICK && page === 1;
        if (rbPage) {
            g.print(2, 2, RB_TITLE, 0);
            const div = RB_DIVS[s.rb.rb_delay] || "?";
            g.print(126 - tw(div), 2, div, 0);
        } else if (page > 0) {
            g.print(2, 2, FX_TITLES[s.mode] || "?", 0);
            const gr = s.mode === MODE_RACK ? (s.fx.bc_ratio > 0 ? `GR ${s.bcGr > 0 ? "-" : ""}${s.bcGr.toFixed(1)}` : "OFF") : "";
            if (gr) g.print(126 - tw(gr), 2, gr, 0);
        } else g.print(2, 2, rack ? "RACK" : hat ? "HAT" : "KICK", 0);
        let info = rack ? (ui.rackHeld >= 0 ? `PAD ${ui.rackHeld + 1}`
                          : s.rfb > 0 ? `DLY ${Math.round(s.rfb * 100)}%` : `${s.len} STEPS`)
                 : hat ? HAT_MODELS[s.hmodel] || "?"
                       : `${KICK_MODELS[s.kmodel] || "?"}+${digitalName(s, s.ksample)}`;
        if (s.rec && (tickCount >> 4) % 2 === 0) info = "REC " + info;
        if (page === 0) g.print(126 - tw(info), 2, info, 0);

        if (rbPage) { drawRb(g, tw); dirty = false; return; }
        if (page > 0) { drawFx(g, tw); dirty = false; return; }

        if (rack) { drawRack(g, tw); dirty = false; return; }

        /* This voice's steps: filled = on, frame = off, underline = playhead.
         * 16 steps are drawn as two rows of 8, as on the pads. */
        const p = hat ? (s.hfill ? s.hpe : s.hp) : (s.kfill ? s.kpe : s.kp);
        const rows16 = s.len === 16;
        for (let i = 0; i < s.len; i++) {
            const x = 9 + (i % 8) * 13, y = rows16 ? (i < 8 ? 13 : 24) : 18;
            const h = rows16 ? 8 : 10;
            if (bit(p, i)) g.fillRect(x, y, 11, h, 1);
            else g.drawRect(x, y, 11, h, 1);
            if (s.running && s.step === i) g.fillRect(x, y + h + 1, 11, 1, 1);
            if (hat && s.motion[i] !== null) {
                /* recorded DECAY as a tick height inside the cell */
                const mh = 1 + Math.round(s.motion[i] * (h - 3));
                g.fillRect(x + 4, y + h - 1 - mh, 3, mh, bit(p, i) ? 0 : 1);
            }
        }
        if (hat ? s.hmute : s.kmute) { g.fillRect(114, 13, 14, 10, 1); g.print(117, 14, "M", 0); }

        const now = host.now();
        const bigKnob = touched >= 0 ? touched : (now < touchShowUntil ? lastTouchKnob : -1);
        if (now < flashUntil) {
            g.fillRect(0, 40, 128, 16, 1);
            g.print(Math.max(1, Math.floor((128 - tw(flash)) / 2)), 44, flash, 0);
        } else if (ui.sel) {
            /* A picker is held: the choices, big, the current one inverted. */
            const pick = PICKERS[ui.sel];
            const names = ui.sel === "digital" ? DIGITAL_NAMES.map((_, i) => digitalName(s, i)) : pick.names;
            const cur = s[pick.field];
            const w = Math.floor(128 / names.length);
            /* A box too narrow for its names (five digital kicks: 25 px)
             * numbers the boxes and names the current one in the title. */
            const numbered = names.some((n) => tw(n) > w - 3);
            g.print(0, 38, numbered ? `${pick.title}: ${names[cur] || "?"}` : pick.title, 1);
            names.forEach((name, i) => {
                const x = i * w, label = numbered ? String(i + 1) : name;
                if (i === cur) g.fillRect(x, 48, w - 1, 13, 1);
                else g.drawRect(x, 48, w - 1, 13, 1);
                g.print(x + Math.max(1, Math.floor((w - 1 - tw(label)) / 2)), 51, label, i === cur ? 0 : 1);
            });
        } else if (bigKnob >= 0) {
            /* The touched knob, big: name, value, bar. */
            const key = KNOBS[s.mode][bigKnob];
            g.print(0, 38, `${bigKnob + 1} ${KNOB_NAMES[s.mode][bigKnob]}`, 1);
            if (s.rec && hat && bigKnob === DECAY_KNOB) {
                g.fillRect(70, 37, 19, 9, 1);
                g.print(72, 38, "REC", 0);
            }
            if (key) {
                const v = s.params[key];
                const pct = `${Math.round(v * 100)}%`;
                g.print(126 - tw(pct), 38, pct, 1);
                const rumble = key === "k_rumble";
                g.drawRect(0, 48, 128, rumble ? 7 : 12, 1);
                g.fillRect(2, 50, Math.round(124 * v), rumble ? 3 : 8, 1);
            } else {
                g.print(0, 50, "not assigned", 1);
            }
            if (key === "k_rumble") {
                /* What RUMBLE is really putting out, and which DSP is running. */
                const db = s.rumbleOut > 0.00001 ? Math.round(20 * Math.log10(s.rumbleOut)) : -99;
                const b = s.build;
                g.print(0, 57, `out ${db}dB  dsp ${Math.floor(b / 10000)}.${Math.floor(b / 100) % 100}.${b % 100}`, 1);
            }
        } else {
            /* All eight knobs, two rows of four. */
            for (let k = 0; k < 8; k++) {
                const x = (k % 4) * 32, y = k < 4 ? 37 : 51;
                g.print(x + 1, y, KNOB_LABELS[s.mode][k], 1);
                const key = KNOBS[s.mode][k];
                if (key) {
                    g.drawRect(x + 1, y + 9, 29, 4, 1);
                    g.fillRect(x + 1, y + 9, Math.round(29 * s.params[key]), 4, 1);
                }
            }
        }
        dirty = false;
    }

    /* The RUMBLE page, laid out like page 1: what the repeats are doing on
     * top, the eight knobs (name and bar) below. A touched knob shows big:
     * name, value, bar. */
    function drawRb(g, tw) {
        const rb = s.rb;
        const div = RB_DIVS[rb.rb_delay] || "?";
        g.print(0, 15, `DELAY ${div}`, 1);
        const st = `${s.len} STEPS`;
        g.print(126 - tw(st), 15, st, 1);
        /* DEFINITION as words: where between the diffuse and the repeats */
        const def = rb.rb_def;
        g.print(0, 25, def < 0.25 ? "DIFFUSE" : def < 0.6 ? "SMOOTH REPEATS" : "CLEAR REPEATS", 1);
        if (s.params.k_rumble <= 0) { const off = "OFF"; g.print(126 - tw(off), 25, off, 1); }

        const now = host.now();
        const bigKnob = touched >= 0 ? touched : (now < touchShowUntil ? lastTouchKnob : -1);
        if (now < flashUntil) {
            g.fillRect(0, 40, 128, 16, 1);
            g.print(Math.max(1, Math.floor((128 - tw(flash)) / 2)), 44, flash, 0);
        } else if (bigKnob >= 0 && bigKnob < 7) {
            const key = RB_KNOBS[bigKnob];
            if (key) {
                const v = getV(rb, key), txt = rbText(key, v);
                g.print(0, 38, `${bigKnob + 1} ${RB_DEFS[key].name}`, 1);
                g.print(126 - tw(txt), 38, txt, 1);
                g.drawRect(0, 48, 128, 12, 1);
                g.fillRect(2, 50, Math.round(124 * Math.min(1, Math.max(0, rbPos(key, v)))), 8, 1);
            } else {
                g.print(0, 38, `${bigKnob + 1} not assigned`, 1);
            }
        } else {
            for (let k = 0; k < 8; k++) {
                const x = (k % 4) * 32, y = k < 4 ? 37 : 51;
                g.print(x + 1, y, RB_LABELS[k], 1);
                const key = RB_KNOBS[k];
                if (key) {
                    g.drawRect(x + 1, y + 9, 29, 4, 1);
                    g.fillRect(x + 1, y + 9, Math.round(29 * rbPos(key, getV(rb, key))), 4, 1);
                }
            }
        }
    }

    /* Page 2 (KICK: page 3): the part's effect knobs, and the shared 8 / 16 steps (jog click). */
    function drawFx(g, tw) {
        const keys = FX_KNOBS[s.mode] || [];
        const now = host.now();
        const bigKnob = touched >= 0 ? touched : (now < touchShowUntil ? lastTouchKnob : -1);
        if (bigKnob >= 0 && bigKnob < 7) {
            const key = keys[bigKnob];
            if (key) {
                const v = s.fx[key], txt = fxText(key, v);
                g.print(0, 16, `${bigKnob + 1} ${FX_DEFS[key].name}`, 1);
                g.print(126 - tw(txt), 16, txt, 1);
                g.drawRect(0, 28, 128, 12, 1);
                g.fillRect(2, 30, Math.round(124 * Math.min(1, Math.max(0, fxPos(key, v)))), 8, 1);
                if (key === "bc_punch" && s.bcLim) g.print(0, 44, "PUNCH IS LIMITING", 1);
            } else {
                g.print(0, 16, `${bigKnob + 1} not assigned`, 1);
            }
        } else {
            /* the knobs, two rows of four: label, value */
            for (let k = 0; k < 7; k++) {
                const key = keys[k];
                if (!key) continue;
                const x = (k % 4) * 32, y = k < 4 ? 15 : 35;
                /* the label inverted in its own 31-pixel box: the columns
                 * read as columns even where two long labels sit side by side */
                g.fillRect(x, y - 1, 31, 9, 1);
                g.print(x + 1, y, FX_DEFS[key].label, 0);
                let txt = fxCellText(key, s.fx[key]);
                if (key === "bc_punch" && s.bcLim) txt += "*";
                g.print(x + 1, y + 9, txt, 1);
            }
        }
        /* bottom line: the shared step length, switched with a jog click */
        g.print(0, 56, `STEPS ${s.len}  click: 8/16`, 1);
    }

    /* The rack: the 6 x 4 block as on the pads, or a held pad's detail. */
    function drawRack(g, tw) {
        const held = ui.rackHeld;
        if (held < 0) {
            for (let pad = 0; pad < RACK_PADS; pad++) {
                const x = (pad % RACK_COLS) * 16, y = 13 + Math.floor(pad / RACK_COLS) * 12;
                const loaded = ((s.rackMask >> pad) & 1) === 1;
                const muted = ((s.rackMute >> pad) & 1) === 1;
                const hit = s.running && s.step >= 0 && ((s.rp[pad] >> rackStepOf(s, pad)) & 1) === 1;
                const label = muted ? "M" : String(pad + 1);
                if (hit && !muted) {
                    g.fillRect(x, y, 15, 11, 1);
                    g.print(x + Math.floor((15 - tw(label)) / 2), y + 2, label, 0);
                } else {
                    if (loaded) g.drawRect(x, y, 15, 11, 1);
                    g.print(x + Math.floor((15 - tw(label)) / 2), y + 2, loaded || muted ? label : "-", 1);
                }
            }
            if (host.now() < flashUntil) {
                g.fillRect(0, 40, 128, 16, 1);
                g.print(Math.max(1, Math.floor((128 - tw(flash)) / 2)), 44, flash, 0);
            }
            return;
        }
        const loaded = ((s.rackMask >> held) & 1) === 1;
        const muted = ((s.rackMute >> held) & 1) === 1;
        g.print(0, 14, loaded ? `Rack/${String(held + 1).padStart(2, "0")}.wav` : "NO SAMPLE", 1);
        if (muted) { g.fillRect(114, 13, 14, 10, 1); g.print(117, 14, "M", 0); }
        const lenText = s.rpl[held] ? `LEN ${s.rpl[held]}` : "LEN FULL";
        g.print(112 - tw(lenText), 14, lenText, 1);
        const p = s.rp[held], cur = rackStepOf(s, held), steps = rackLen(s, held);
        for (let i = 0; i < steps; i++) {
            const x = 9 + (i % 8) * 13, y = s.len === 16 ? (i < 8 ? 23 : 31) : 26;
            if ((p >> i) & 1) g.fillRect(x, y, 11, 6, 1); else g.drawRect(x, y, 11, 6, 1);
            if (s.running && cur === i) g.fillRect(x, y + 7, 11, 1, 1);
        }
        /* knobs 1-6: VOL, the four sends and the LPF: name, value, bar */
        const vals = [s.rv[held], s.rds[held], s.rrs[held], s.rcs[held], s.rms[held], s.rlp[held]];
        vals.forEach((v, k) => {
            const x = k * 16, name = RACK_PAD_NAMES[k];
            const num = k === 5 ? lpfLabel(v) : String(Math.round(v * 100));
            g.print(Math.max(x, x + Math.floor((15 - tw(name)) / 2)), 40, name, 1);
            g.print(Math.max(x, x + Math.floor((15 - tw(num)) / 2)), 49, num, 1);
            g.drawRect(x, 58, 15, 5, 1);
            g.fillRect(x + 1, 59, Math.round(13 * v), 3, 1);
        });
        /* knob 7: the pad's colour; knob 8 does nothing here */
        const col = RACK_COLORS[(ui.rackColors && ui.rackColors[held]) || 0].short;
        g.print(96 + Math.floor((15 - tw("CO")) / 2), 40, "CO", 1);
        g.print(96 + Math.floor((15 - tw(col)) / 2), 49, col, 1);
    }

    /* ---- lifecycle ---- */
    function init() {
        refresh();
        /* the saved presets light row 2 from the first frame, not only after
         * the first preset pad is pressed */
        loadPresets();
        loadRackColors();
        ledCache = {};
        wasOn = false;
        dirty = true;
    }

    function tick() {
        tickCount++;
        saveRackColors();
        const on = host.displayOn();
        if (!on) {
            if (wasOn) {
                /* The screen was dismissed with this view still loaded (Menu,
                 * a Move long-press): give the pads back to Move now. */
                releaseAll();
                restoreLeds();
                host.padBlock(0);
            }
            wasOn = false;
            return;
        }
        /* Restated EVERY frame: the shim drops the flag by itself on the
         * display edge, so remembering it would latch dead pads. */
        host.padBlock(1);
        if (!wasOn) { ledCache = {}; moveSnap = null; readDue = true; }
        wasOn = true;

        flushPending();
        const now = host.now();
        if (now - lastHeartbeat >= HEARTBEAT_MS) {
            lastHeartbeat = now;
            if (fillHeld[0]) set("k_fill", 1);
            if (fillHeld[1]) set("h_fill", 1);
            if (recHeld) set("h_motion_rec", 1);
        }
        if (readDue || tickCount % READ_EVERY_TICKS === 0) refresh();
        paintLeds();
        if (dirty || s.rec || s.running || now < touchShowUntil + 50 || now < flashUntil + 50) draw();
    }

    /* Back leaves the module: tidy up, then let the host navigate. */
    function handleBack() {
        releaseAll();
        restoreLeds();
        host.padBlock(0);
        return false;
    }

    return {
        init, tick, onMidi, handleBack,
        /* for tests */
        get state() { return s; },
        get uiState() { return ui; },
        get recording() { return recHeld; },
    };
}
