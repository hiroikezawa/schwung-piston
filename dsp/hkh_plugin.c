/* Piston - Schwung plugin API v2 adapter. MIT.
 *
 * EVERY entry point here runs on the SPI audio callback (plugin_api_v1.h), so:
 * no allocation (instances come from a static pool), no file I/O (samples are
 * loaded by a demoted worker, see hkh_samples.c), no logging, bounded work. */
#include "host/plugin_api_v1.h"
#include "hkh_engine.h"
#include "hkh_samples.h"
#include "hkh_util.h"
#include "hkh_metadata.h"
#include <ctype.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "instance pool needs lock-free atomics");

/* Four chain slots can each hold one; two spare cover a swap in flight. */
#define HKH_INSTANCES 6
#define HKH_DSP_BUILD 203          /* major*10000 + minor*100 + patch: 0.2.3; bump with module.json */

/* The shim parks a slot whose output stays below 5 LSB for ~1 s and renders
 * it only once every ~0.5 s after that (schwung_shim.c, DSP_IDLE_THRESHOLD).
 * There is no opt-out for a sound generator. This module is its own
 * sequencer, so a parked slot would start late -- up to half a second after
 * Play. A block that would otherwise be silent therefore carries a DC offset
 * of 6 LSB (-75 dBFS) so the slot is never judged idle.
 *
 * DC rather than a pulse on purpose: a pulse train is a buzz, and a slot FX
 * with 40 dB of drive would make it audible; DC stays DC through any
 * distortion and is removed by any DC blocker downstream. See README
 * "Why it never sleeps". */
#define HKH_KEEPALIVE_LSB 6
#define HKH_SILENCE_LSB 4

typedef struct {
    atomic_int used;
    hkh_engine engine;
    /* MIDI clock fallback, used only when the host has no get_beat_position. */
    int clk_running, clk_awaiting;
    long clk_ticks;
    float out_l[MOVE_FRAMES_PER_BLOCK], out_r[MOVE_FRAMES_PER_BLOCK];
    hkh_sample_bank bank;       /* user WAVs for the digital kick slots */
} hkh_instance;

static hkh_instance g_pool[HKH_INSTANCES];
static const host_api_v1_t *g_host;
static atomic_uint g_seed = 0x2545F491u;

/* ---- lifecycle ---------------------------------------------------------- */
static void *create_instance(const char *module_dir, const char *defaults) {
    (void)defaults;
    for (int i = 0; i < HKH_INSTANCES; ++i) {
        int expected = 0;
        if (atomic_compare_exchange_strong(&g_pool[i].used, &expected, 1)) {
            hkh_instance *inst = &g_pool[i];
            unsigned seed = atomic_fetch_add(&g_seed, 0x9E3779B9u);
            hkh_engine_init(&inst->engine, seed);
            inst->clk_running = inst->clk_awaiting = 0;
            inst->clk_ticks = 0;
            hkh_engine_set_sample_source(&inst->engine, hkh_samples_get, &inst->bank);
            hkh_samples_start(&inst->bank, HKH_USER_DIR, module_dir);
            return inst;
        }
    }
    return NULL;
}

static void destroy_instance(void *ptr) {
    if (!ptr) return;
    hkh_instance *inst = ptr;
    hkh_samples_stop(&inst->bank);
    atomic_store(&inst->used, 0);
}

/* ---- MIDI --------------------------------------------------------------- */
static void on_clock(hkh_instance *inst, uint8_t status) {
    switch (status) {
    case 0xFA: inst->clk_running = 1; inst->clk_awaiting = 1; inst->clk_ticks = 0; break;
    case 0xFB: inst->clk_running = 1; break;
    case 0xFC: inst->clk_running = 0; break;
    case 0xF8:
        if (!inst->clk_running) { inst->clk_running = 1; inst->clk_awaiting = 1; }
        if (inst->clk_awaiting) { inst->clk_awaiting = 0; inst->clk_ticks = 0; }
        else inst->clk_ticks++;
        break;
    default: break;
    }
}

/* Notes let Move's own sequencer (or a controller) play the two voices:
 * 35/36 kick, 42/44 hat at the current DECAY, 46 an open hat. */
static void on_midi(void *ptr, const uint8_t *msg, int len, int source) {
    (void)source;
    if (!ptr || !msg || len < 1) return;
    hkh_instance *inst = ptr;
    if (msg[0] >= 0xF8) { on_clock(inst, msg[0]); return; }
    if (len < 3 || (msg[1] & 0x80) || (msg[2] & 0x80)) return;
    uint8_t type = msg[0] & 0xF0;
    if (type == 0x90 && msg[2] > 0) {
        float vel = (float)msg[2] / 127.0f;
        switch (msg[1]) {
        case 35: case 36: hkh_engine_trigger_kick(&inst->engine, vel); break;
        case 42: case 44: hkh_engine_trigger_hat(&inst->engine, vel, -1.0f); break;
        case 46: hkh_engine_trigger_hat(&inst->engine, vel, 0.75f); break;
        default: break;
        }
    } else if (type == 0xB0 && (msg[1] == 120 || msg[1] == 123)) {
        hkh_engine_all_sound_off(&inst->engine);
    }
}

/* ---- parameters --------------------------------------------------------- */
static int param_index(const char *key) {
    for (int i = 0; i < P_COUNT; ++i) if (!strcmp(key, hkh_param_keys[i])) return i;
    return -1;
}

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) ++p;
    return p;
}

/* A whole-string number, rejecting NaN/Inf/trailing junk. */
static int parse_float(const char *s, float *out) {
    char *end;
    float v = strtof(s, &end);
    if (end == s || *skip_ws(end) || !isfinite(v)) return 0;
    *out = v;
    return 1;
}

static int parse_int(const char *s, int lo, int hi, int *out) {
    float v;
    if (!parse_float(s, &v) || v != floorf(v) || v < (float)lo || v > (float)hi) return 0;
    *out = (int)v;
    return 1;
}

/* ---- state blob -----------------------------------------------------------
 * {"v":1,"mode":0,"km":1,"ks":0,"hm":1,"kp":17,"hp":68,
 *  "p":[14 numbers],"mo":[8 numbers, -1 = no motion on that step]}
 * Parsed into a scratch copy and applied only if every field is valid, so a
 * truncated or foreign blob can never leave the instance half-restored. The
 * chain host hands this back compacted, but pretty-printed input is accepted.
 * Mute, FILL and motion-record are performance gestures and are not saved. */
#define HKH_STATE_MAX 2048

static const char *find_key(const char *s, const char *key) {
    size_t n = strlen(key);
    for (const char *p = s; (p = strchr(p, '"')) != NULL; ++p) {
        if (!strncmp(p + 1, key, n) && p[n + 1] == '"') {
            const char *q = skip_ws(p + n + 2);
            if (*q == ':') return skip_ws(q + 1);
        }
    }
    return NULL;
}

/* JSON number grammar only: strtod alone would also take "nan", hex, "+1". */
static const char *read_number(const char *p, float *out) {
    const char *start = p;
    if (*p == '-') ++p;
    if (*p == '0') ++p;
    else if (*p >= '1' && *p <= '9') { while (isdigit((unsigned char)*p)) ++p; }
    else return NULL;
    if (*p == '.') {
        ++p;
        if (!isdigit((unsigned char)*p)) return NULL;
        while (isdigit((unsigned char)*p)) ++p;
    }
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '+' || *p == '-') ++p;
        if (!isdigit((unsigned char)*p)) return NULL;
        while (isdigit((unsigned char)*p)) ++p;
    }
    char tmp[32];
    size_t n = (size_t)(p - start);
    if (n == 0 || n >= sizeof(tmp)) return NULL;
    memcpy(tmp, start, n);
    tmp[n] = 0;
    float v = strtof(tmp, NULL);
    if (!isfinite(v)) return NULL;
    *out = v;
    return p;
}

static int read_key_number(const char *s, const char *key, float *out) {
    const char *p = find_key(s, key);
    return p && read_number(p, out) != NULL;
}

static int read_key_array(const char *s, const char *key, float *out, int count) {
    const char *p = find_key(s, key);
    if (!p || *p != '[') return 0;
    p = skip_ws(p + 1);
    for (int i = 0; i < count; ++i) {
        p = read_number(p, &out[i]);
        if (!p) return 0;
        p = skip_ws(p);
        if (i < count - 1) {
            if (*p != ',') return 0;
            p = skip_ws(p + 1);
        }
    }
    return *p == ']';
}

static int in_int_range(float v, int lo, int hi) {
    return v == floorf(v) && v >= (float)lo && v <= (float)hi;
}

static int restore_state(hkh_engine *e, const char *text) {
    if (!text || strlen(text) > HKH_STATE_MAX) return 0;
    const char *p = skip_ws(text);
    if (*p != '{') return 0;
    float v, mode, km, ks, hm, kp, hp, len = 8.0f, prm[P_COUNT], mo[HKH_STEPS];
    if (!read_key_number(p, "v", &v) || v != 1.0f) return 0;
    if (!read_key_number(p, "mode", &mode) || !in_int_range(mode, 0, 1)) return 0;
    if (!read_key_number(p, "km", &km) || !in_int_range(km, 0, KICK_MODEL_COUNT - 1)) return 0;
    if (!read_key_number(p, "ks", &ks) || !in_int_range(ks, 0, HKH_DIGITAL_COUNT - 1)) return 0;
    if (!read_key_number(p, "hm", &hm) || !in_int_range(hm, 0, HAT_MODEL_COUNT - 1)) return 0;
    if (!read_key_number(p, "kp", &kp) || !in_int_range(kp, 0, 65535)) return 0;
    if (!read_key_number(p, "hp", &hp) || !in_int_range(hp, 0, 65535)) return 0;
    if (!read_key_array(p, "p", prm, P_COUNT)) return 0;
    /* "len" and 16 motion values arrived with 16 steps; a state saved before
     * that (8 motion values, no len) still restores, as 8 steps. */
    if (find_key(p, "len") && (!read_key_number(p, "len", &len) || !(len == 8.0f || len == 16.0f))) return 0;
    for (int i = 0; i < HKH_STEPS; ++i) mo[i] = HKH_MOTION_NONE;
    if (!read_key_array(p, "mo", mo, HKH_STEPS) && !read_key_array(p, "mo", mo, 8)) return 0;
    for (int i = 0; i < P_COUNT; ++i) if (prm[i] < 0.0f || prm[i] > 1.0f) return 0;
    for (int i = 0; i < HKH_STEPS; ++i)
        if (!(mo[i] == HKH_MOTION_NONE || (mo[i] >= 0.0f && mo[i] <= 1.0f))) return 0;
    /* All valid: apply. */
    e->mode = (int)mode;
    e->k_model = (int)km;
    e->k_sample = (int)ks;
    e->h_model = (int)hm;
    e->k_pattern = (uint16_t)kp;
    e->h_pattern = (uint16_t)hp;
    e->seq.length = (int)len;
    for (int i = 0; i < P_COUNT; ++i) hkh_engine_set_param(e, i, prm[i]);
    for (int i = 0; i < HKH_STEPS; ++i) e->motion[i] = mo[i];
    return 1;
}

static void set_param(void *ptr, const char *key, const char *val) {
    if (!ptr || !key || !val) return;
    hkh_engine *e = &((hkh_instance *)ptr)->engine;
    if (!strcmp(key, "state")) { (void)restore_state(e, val); return; }

    int idx = param_index(key);
    float f;
    int n;
    if (idx >= 0) { if (parse_float(val, &f)) hkh_engine_set_param(e, idx, f); return; }

    if (!strcmp(key, "mode")) { if (parse_int(val, 0, 1, &n)) e->mode = n; }
    else if (!strcmp(key, "k_model")) { if (parse_int(val, 0, KICK_MODEL_COUNT - 1, &n)) hkh_engine_set_kick_model(e, n, 0); }
    else if (!strcmp(key, "k_sample")) { if (parse_int(val, 0, HKH_DIGITAL_COUNT - 1, &n)) hkh_engine_set_kick_sample(e, n, 0); }
    else if (!strcmp(key, "h_model")) { if (parse_int(val, 0, HAT_MODEL_COUNT - 1, &n)) hkh_engine_set_hat_model(e, n, 0); }
    else if (!strcmp(key, "k_model_pick")) { if (parse_int(val, 0, KICK_MODEL_COUNT - 1, &n)) hkh_engine_set_kick_model(e, n, 1); }
    else if (!strcmp(key, "k_sample_pick")) { if (parse_int(val, 0, HKH_DIGITAL_COUNT - 1, &n)) hkh_engine_set_kick_sample(e, n, 1); }
    else if (!strcmp(key, "h_model_pick")) { if (parse_int(val, 0, HAT_MODEL_COUNT - 1, &n)) hkh_engine_set_hat_model(e, n, 1); }
    else if (!strcmp(key, "k_pattern")) { if (parse_int(val, 0, 65535, &n)) e->k_pattern = (uint16_t)n; }
    else if (!strcmp(key, "h_pattern")) { if (parse_int(val, 0, 65535, &n)) e->h_pattern = (uint16_t)n; }
    else if (!strcmp(key, "length")) { if (parse_int(val, 8, 16, &n) && (n == 8 || n == 16)) hkh_engine_set_length(e, n); }
    else if (!strcmp(key, "k_step")) { if (parse_int(val, 0, HKH_STEPS - 1, &n)) hkh_engine_toggle_step(e, 0, n); }
    else if (!strcmp(key, "h_step")) { if (parse_int(val, 0, HKH_STEPS - 1, &n)) hkh_engine_toggle_step(e, 1, n); }
    else if (!strcmp(key, "k_mute")) { if (parse_int(val, 0, 1, &n)) hkh_engine_set_mute(e, 0, n); }
    else if (!strcmp(key, "h_mute")) { if (parse_int(val, 0, 1, &n)) hkh_engine_set_mute(e, 1, n); }
    else if (!strcmp(key, "k_fill")) { if (parse_int(val, 0, 1, &n)) hkh_engine_set_fill(e, 0, n); }
    else if (!strcmp(key, "h_fill")) { if (parse_int(val, 0, 1, &n)) hkh_engine_set_fill(e, 1, n); }
    else if (!strcmp(key, "h_motion_rec")) { if (parse_int(val, 0, 1, &n)) hkh_engine_set_motion_rec(e, n); }
    else if (!strcmp(key, "k_reset")) hkh_engine_reset(e, 0);
    else if (!strcmp(key, "h_reset")) hkh_engine_reset(e, 1);
    else if (!strcmp(key, "k_shuffle")) hkh_engine_shuffle(e, 0);
    else if (!strcmp(key, "h_shuffle")) hkh_engine_shuffle(e, 1);
    else if (!strcmp(key, "k_random")) hkh_engine_random(e, 0);
    else if (!strcmp(key, "h_random")) hkh_engine_random(e, 1);
    else if (!strcmp(key, "h_offbeat")) hkh_engine_offbeat(e);
}

static int copy_text(char *buf, int size, const char *text) {
    size_t n = strlen(text);
    if (n >= (size_t)size) { buf[0] = 0; return -1; }
    memcpy(buf, text, n + 1);
    return (int)n;
}

static int milli(float v) { return (int)lroundf(hkh_clamp(v, 0.0f, 1.0f) * 1000.0f); }

/* One read gives the UI everything it draws: see ui_core.mjs parseUiState. */
static int write_ui_state(hkh_instance *inst, char *buf, int size) {
    const hkh_engine *e = &inst->engine;
    int n = snprintf(buf, size, "2,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                     e->seq.running, e->seq.cur_step,
                     e->k_pattern, e->h_pattern,
                     hkh_engine_effective_pattern(e, 0), hkh_engine_effective_pattern(e, 1),
                     e->k_mute, e->h_mute, e->k_fill, e->h_fill,
                     e->k_model, e->k_sample, e->h_model, e->mode, e->motion_rec,
                     hkh_samples_user_mask(&inst->bank), e->seq.length);
    for (int i = 0; i < P_COUNT && n > 0 && n < size; ++i)
        n += snprintf(buf + n, size - n, ",%d", milli(e->param[i]));
    for (int i = 0; i < HKH_STEPS && n > 0 && n < size; ++i)
        n += snprintf(buf + n, size - n, ",%d",
                      e->motion[i] >= 0.0f ? milli(e->motion[i]) : -1);
    /* 40: RUMBLE output peak (0-1000), 41: this DSP's build, so the screen
     * can say which binary is actually running. */
    if (n > 0 && n < size)
        n += snprintf(buf + n, size - n, ",%d,%d", milli(e->voices.rumble_meter), HKH_DSP_BUILD);
    if (n < 0 || n >= size) { buf[0] = 0; return -1; }
    return n;
}

static int write_state(const hkh_engine *e, char *buf, int size) {
    int n = snprintf(buf, size, "{\"v\":1,\"mode\":%d,\"km\":%d,\"ks\":%d,\"hm\":%d,"
                     "\"kp\":%d,\"hp\":%d,\"len\":%d,\"p\":[",
                     e->mode, e->k_model, e->k_sample, e->h_model, e->k_pattern, e->h_pattern,
                     e->seq.length);
    for (int i = 0; i < P_COUNT && n > 0 && n < size; ++i)
        n += snprintf(buf + n, size - n, "%s%.4f", i ? "," : "", e->param[i]);
    if (n > 0 && n < size) n += snprintf(buf + n, size - n, "],\"mo\":[");
    for (int i = 0; i < HKH_STEPS && n > 0 && n < size; ++i)
        n += snprintf(buf + n, size - n, "%s%.4f", i ? "," : "",
                      e->motion[i] >= 0.0f ? e->motion[i] : -1.0f);
    if (n > 0 && n < size) n += snprintf(buf + n, size - n, "]}");
    if (n < 0 || n >= size) { buf[0] = 0; return -1; }
    return n;
}

static int get_param(void *ptr, const char *key, char *buf, int size) {
    if (!ptr || !key || !buf || size <= 0) return -1;
    buf[0] = 0;
    hkh_instance *inst = ptr;
    const hkh_engine *e = &inst->engine;
    int idx = param_index(key);
    int n = -1;
    if (idx >= 0) n = snprintf(buf, size, "%.4f", e->param[idx]);
    else if (!strcmp(key, "ui_state")) return write_ui_state(inst, buf, size);
    else if (!strcmp(key, "state")) return write_state(e, buf, size);
    else if (!strcmp(key, "chain_params")) return copy_text(buf, size, hkh_chain_params);
    /* Served, and EMPTY on purpose: no hierarchy is what makes the shadow UI
     * open ui_chain.js (the pad performance surface) instead of a knob grid.
     * An error here would read as a failed read and hold the editor on
     * "Loading..." (component_load_gate.mjs). */
    else if (!strcmp(key, "ui_hierarchy")) return 0;
    else if (!strcmp(key, "mode")) n = snprintf(buf, size, "%d", e->mode);
    else if (!strcmp(key, "k_model")) n = snprintf(buf, size, "%d", e->k_model);
    else if (!strcmp(key, "k_sample")) n = snprintf(buf, size, "%d", e->k_sample);
    else if (!strcmp(key, "h_model")) n = snprintf(buf, size, "%d", e->h_model);
    else if (!strcmp(key, "k_pattern")) n = snprintf(buf, size, "%d", e->k_pattern);
    else if (!strcmp(key, "h_pattern")) n = snprintf(buf, size, "%d", e->h_pattern);
    else if (!strcmp(key, "k_mute")) n = snprintf(buf, size, "%d", e->k_mute);
    else if (!strcmp(key, "h_mute")) n = snprintf(buf, size, "%d", e->h_mute);
    if (n < 0 || n >= size) { buf[0] = 0; return -1; }
    return n;
}

static int get_error(void *ptr, char *buf, int size) {
    (void)ptr;
    if (buf && size > 0) buf[0] = 0;
    return 0;
}

/* ---- audio -------------------------------------------------------------- */
static double transport_beat(hkh_instance *inst) {
    if (g_host && g_host->get_beat_position) return g_host->get_beat_position();
    return inst->clk_running ? (double)inst->clk_ticks / 24.0 : -1.0;
}

static float transport_bpm(void) {
    float bpm = (g_host && g_host->get_bpm) ? g_host->get_bpm() : 120.0f;
    return (bpm >= 20.0f && bpm <= 999.0f) ? bpm : 120.0f;
}

static void render_block(void *ptr, int16_t *out, int frames) {
    if (!out || frames <= 0) return;
    if (!ptr || frames > MOVE_FRAMES_PER_BLOCK) {
        memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }
    hkh_instance *inst = ptr;
    hkh_engine_render(&inst->engine, inst->out_l, inst->out_r, frames,
                      transport_beat(inst), transport_bpm());
    int peak = 0;
    for (int i = 0; i < frames; ++i) {
        float l = hkh_clamp(inst->out_l[i], -1.0f, 1.0f);
        float r = hkh_clamp(inst->out_r[i], -1.0f, 1.0f);
        int16_t sl = (int16_t)lrintf(l * 32767.0f);
        int16_t sr = (int16_t)lrintf(r * 32767.0f);
        out[i * 2] = sl;
        out[i * 2 + 1] = sr;
        int a = abs(sl) > abs(sr) ? abs(sl) : abs(sr);
        if (a > peak) peak = a;
    }
    if (peak <= HKH_SILENCE_LSB)
        for (int i = 0; i < frames * 2; ++i) out[i] = HKH_KEEPALIVE_LSB;
}

static plugin_api_v2_t g_api = {
    .api_version = 2,
    .create_instance = create_instance,
    .destroy_instance = destroy_instance,
    .on_midi = on_midi,
    .set_param = set_param,
    .get_param = get_param,
    .get_error = get_error,
    .render_block = render_block,
};

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    if (host && host->sample_rate && host->sample_rate != MOVE_SAMPLE_RATE) return NULL;
    g_host = host;
    return &g_api;
}
