/* Piston - engine. MIT. */
#include "hkh_engine.h"
#include "hkh_util.h"
#include "hkh_samples.h"
#include "hkh_pattern.h"
#include <math.h>
#include <string.h>

const char *const hkh_param_keys[P_COUNT] = {
    "k_vol", "k_mix", "k_decay", "k_drive", "k_comp", "k_rumble", "k_verb",
    "h_vol", "h_color", "h_decay", "h_drive", "h_comp", "h_filter", "h_verb"
};

const float hkh_param_defaults[P_COUNT] = {
    0.80f, 0.25f, 0.50f, 0.15f, 0.25f, 0.00f, 0.00f,
    0.70f, 0.50f, HKH_HAT_CLOSED_DECAY, 0.10f, 0.20f, 0.50f, 0.00f
};

/* A FILL hold or a DECAY touch that the UI stops restating is released after
 * this long. The UI restates every ~150 ms, so this never fires while the
 * finger is actually down. */
#define HKH_HEARTBEAT_SAMPLES 22050

/* ---- voices ------------------------------------------------------------- */
/* One-pole smoothing at block rate: ~20 ms to settle, so a knob or a state
 * restore never steps a gain. */
#define BLOCK_SMOOTH 0.25f

/* VOLUME: squared taper. The module leaves headroom on purpose: it sits in
 * Move's mix with other tracks, and the limiter is a safety, not a loudness
 * tool, so the defaults (kick 0.8, hat 0.7) peak well under it. The hat gets
 * more gain because a closed hat is short and sparse: at equal knob settings
 * the two voices then sit where a techno mix wants them. */
static float kick_vol_gain(float v) { return 1.6f * v * v; }
static float hat_vol_gain(float v) { return 8.0f * v * v; }
/* The hat bus is soft-clipped at this ceiling AFTER its volume: a hat is all
 * crest, so more gain alone only feeds the limiter. Clipping the spikes lets
 * the body come up -- loud next to a compressed, rumbling kick. */
#define HAT_CEILING 0.85f
#define KICK_CEILING 0.8f
/* REVERB sends: squared, so the first half of the knob is subtle. The kick's
 * send is highpassed at 180 Hz inside the reverb (the lows are RUMBLE's), so
 * it needs more level to be heard. */
static float kick_send_gain(float v) { return 3.0f * v * v; }
static float hat_send_gain(float v) { return 0.9f * v * v; }

/* Rack pad volume: squared taper, unity at full. Samples keep their own
 * level (only peaks above 0.95 are scaled down at load). */
static float rack_vol_gain(float v) { return v * v; }
#define RACK_CEILING 0.9f

static void voices_init(hkh_engine *e) {
    hkh_voices *v = &e->voices;
    memset(v, 0, sizeof(*v));
    hkh_akick_init(&v->akick, e->rng ^ 0xA5A5A5A5u);
    hkh_digital_init(&v->dkick);
    v->mix = e->param[P_K_MIX];
    v->gain_a = cosf(v->mix * 1.5707963f);
    v->gain_d = sinf(v->mix * 1.5707963f);
    hkh_hat_init(&v->hat, e->rng ^ 0x5A5A5A5Au);
    hkh_drive_init(&v->kdrive, 40.0f, 16000.0f, 6500.0f);
    hkh_drive_init(&v->hdrive, 20.0f, 19000.0f, 10000.0f);
    hkh_comp_init(&v->kcomp, 0);
    hkh_comp_init(&v->hcomp, 1);
    hkh_rumble_init(&v->rumble);
    hkh_reverb_init(&v->reverb);
    hkh_limiter_init(&v->limiter);
    v->kvol = kick_vol_gain(e->param[P_K_VOL]);
    v->hvol = hat_vol_gain(e->param[P_H_VOL]);
    v->ksend = kick_send_gain(e->param[P_K_VERB]);
    v->hsend = hat_send_gain(e->param[P_H_VERB]);
}

static hkh_sample_ref kick_sample(hkh_engine *e) {
    return e->samples ? e->samples(e->samples_ctx, e->k_sample)
                      : hkh_samples_get(NULL, e->k_sample);
}

static void voices_kick(hkh_engine *e, float vel) {
    /* Both engines fire on every hit; MIX only decides what is heard, so
     * turning MIX during a tail crossfades the tail too. */
    hkh_akick_trigger(&e->voices.akick, e->k_model, vel, e->param[P_K_DECAY]);
    hkh_digital_trigger(&e->voices.dkick, kick_sample(e), vel);
}
static void voices_hat(hkh_engine *e, float vel, float decay, int follows_base) {
    hkh_hat_trigger(&e->voices.hat, e->h_model, vel, decay, follows_base);
}
static void voices_choke(hkh_engine *e, int hat) {
    if (hat) {
        hkh_hat_choke(&e->voices.hat, 10.0f);
    } else {
        hkh_akick_choke(&e->voices.akick, 10.0f);
        hkh_digital_choke(&e->voices.dkick, 10.0f);
    }
}
static void voices_block(hkh_engine *e, int frames) {
    hkh_voices *v = &e->voices;
    hkh_akick_block(&v->akick, e->param[P_K_DECAY]);
    /* Equal-power A/D crossfade (cos/sin): 50 % is both at -3 dB, so the
     * blend is as loud as either end. Gains ramp linearly across the block. */
    v->mix += (e->param[P_K_MIX] - v->mix) * BLOCK_SMOOTH;
    float ga = cosf(v->mix * 1.5707963f), gd = sinf(v->mix * 1.5707963f);
    v->gain_a_step = (ga - v->gain_a) / (float)frames;
    v->gain_d_step = (gd - v->gain_d) / (float)frames;
    hkh_hat_block(&v->hat, e->param[P_H_COLOR], e->param[P_H_FILTER], e->param[P_H_DECAY]);
    hkh_drive_block(&v->kdrive, e->param[P_K_DRIVE], frames);
    hkh_drive_block(&v->hdrive, e->param[P_H_DRIVE], frames);
    hkh_comp_block(&v->kcomp, e->param[P_K_COMP], frames);
    hkh_comp_block(&v->hcomp, e->param[P_H_COMP], frames);
    hkh_rumble_block(&v->rumble, e->param[P_K_RUMBLE], frames);
    /* Gains ramp linearly to a smoothed target across the block. */
    float f = (float)frames;
    v->kvol_step = (kick_vol_gain(e->param[P_K_VOL]) - v->kvol) * BLOCK_SMOOTH / f;
    v->hvol_step = (hat_vol_gain(e->param[P_H_VOL]) - v->hvol) * BLOCK_SMOOTH / f;
    v->ksend_step = (kick_send_gain(e->param[P_K_VERB]) - v->ksend) * BLOCK_SMOOTH / f;
    v->hsend_step = (hat_send_gain(e->param[P_H_VERB]) - v->hsend) * BLOCK_SMOOTH / f;
}
/* True when nothing can make a sound this block: no voice ringing, both
 * send effects parked, the output already settled to zero. The keepalive
 * means the host calls us every block even when stopped, so this is the
 * cost of an idle module. */
static int rack_active(const hkh_engine *e) {
    for (int i = 0; i < HKH_RACK; ++i)
        if (e->rack[i].data && e->rack[i].pos < e->rack[i].len) return 1;
    return 0;
}

static int voices_silent(const hkh_engine *e) {
    const hkh_voices *v = &e->voices;
    return !rack_active(e) && !hkh_akick_active(&v->akick) && !hkh_digital_active(&v->dkick) &&
           !hkh_hat_active(&v->hat) &&
           v->rumble.quiet >= (1 << 20) && v->rumble.amount < 0.001f &&
           v->reverb.quiet >= 44100 * 3 &&
           fabsf(v->limiter.dl_y) < 1e-6f && fabsf(v->limiter.dr_y) < 1e-6f;
}

static void voices_tick(hkh_engine *e, float *l, float *r) {
    hkh_voices *v = &e->voices;
    v->gain_a += v->gain_a_step;
    v->gain_d += v->gain_d_step;
    v->kvol += v->kvol_step;
    v->hvol += v->hvol_step;
    v->ksend += v->ksend_step;
    v->hsend += v->hsend_step;
    float k = v->gain_a * hkh_akick_tick(&v->akick) + v->gain_d * hkh_digital_tick(&v->dkick);
    k = hkh_comp_tick(&v->kcomp, hkh_drive_tick(&v->kdrive, k)) * v->kvol;
    float h = hkh_hat_tick(&v->hat);
    h = hkh_comp_tick(&v->hcomp, hkh_drive_tick(&v->hdrive, h)) * v->hvol;
    h = hkh_tanh(h * (1.0f / HAT_CEILING)) * HAT_CEILING;
    float rumble = hkh_rumble_tick(&v->rumble, k);
    /* Metered for the screen: proof on the device that RUMBLE is producing,
     * separate from whether the speaker can reproduce it. */
    float ar = fabsf(rumble);
    v->rumble_meter = ar > v->rumble_meter ? ar : v->rumble_meter * 0.99995f;
    float rl, rr;
    hkh_reverb_tick(&v->reverb, k * v->ksend, h * v->hsend, &rl, &rr);
    /* The kick bus (kick + rumble) has its own ceiling, below the hat's, so
     * a maxed, compressed, rumbling kick can never bury the hat or pull it
     * down through the output limiter. */
    float kb = hkh_tanh((k + rumble) * (1.0f / KICK_CEILING)) * KICK_CEILING;
    /* The rack: a plain sum of one-shots, soft-clipped as its own bus. */
    float rs = 0.0f;
    for (int i = 0; i < HKH_RACK; ++i) {
        hkh_rack_voice *rv = &e->rack[i];
        if (!rv->data || rv->pos >= rv->len) continue;
        float x = (float)rv->data[rv->pos++] * (1.0f / 32767.0f);
        /* the last 2 ms fade, so a sample that stops off zero cannot click */
        int left = rv->len - rv->pos;
        if (left < 88) x *= (float)left * (1.0f / 88.0f);
        rs += x * rv->vel * e->r_gain[i];
    }
    if (rs != 0.0f) rs = hkh_tanh(rs * (1.0f / RACK_CEILING)) * RACK_CEILING;
    float outl = kb + h + rl + rs, outr = kb + h + rr + rs;
    hkh_limiter_tick(&v->limiter, &outl, &outr);
    *l = outl;
    *r = outr;
}
/* ------------------------------------------------------------------------ */

void hkh_engine_init(hkh_engine *e, uint32_t seed) {
    memset(e, 0, sizeof(*e));
    for (int i = 0; i < P_COUNT; ++i) e->param[i] = hkh_param_defaults[i];
    e->k_model = KICK_909;
    e->k_sample = 0;
    e->h_model = HAT_909;
    e->mode = MODE_KICK;
    e->k_pattern = HKH_KICK_DEFAULT_PATTERN;
    e->h_pattern = HKH_HAT_DEFAULT_PATTERN;
    for (int i = 0; i < HKH_STEPS; ++i) e->motion[i] = HKH_MOTION_NONE;
    for (int i = 0; i < HKH_RACK; ++i) {
        e->r_vol[i] = HKH_RACK_DEFAULT_VOL;
        e->r_gain[i] = rack_vol_gain(HKH_RACK_DEFAULT_VOL);
    }
    e->rng = seed ? seed : 0x9E3779B9u;
    e->pending_h_decay = -1.0f;
    e->k_ratchet_in = e->h_ratchet_in = -1;
    hkh_seq_init(&e->seq);
    voices_init(e);
}

void hkh_engine_set_sample_source(hkh_engine *e, hkh_sample_source fn, void *ctx) {
    e->samples = fn;
    e->samples_ctx = ctx;
}

void hkh_engine_set_rack_source(hkh_engine *e, hkh_sample_source fn, void *ctx) {
    e->rack_samples = fn;
    e->rack_ctx = ctx;
}

static void fire_rack(hkh_engine *e, int pad) {
    if (e->r_mute[pad] || !e->rack_samples) return;
    hkh_sample_ref r = e->rack_samples(e->rack_ctx, pad);
    if (!r.data || r.len <= 0) return;
    ++e->r_hits;
    hkh_rack_voice *v = &e->rack[pad];
    v->data = r.data;
    v->len = r.len;
    v->pos = 0;
    v->vel = 1.0f;
}

void hkh_engine_rack_trigger(hkh_engine *e, int pad) {
    if (pad < 0 || pad >= HKH_RACK) return;
    e->r_pending |= 1u << pad;
}

void hkh_engine_rack_toggle_step(hkh_engine *e, int pad, int step) {
    if (pad < 0 || pad >= HKH_RACK || step < 0 || step >= e->seq.length) return;
    e->r_pattern[pad] ^= (uint16_t)(1u << step);
    if (((e->r_pattern[pad] >> step) & 1u) && !e->seq.running) hkh_engine_rack_trigger(e, pad);
}

void hkh_engine_rack_set_mute(hkh_engine *e, int pad, int on) {
    if (pad < 0 || pad >= HKH_RACK) return;
    e->r_mute[pad] = on ? 1 : 0;
    if (on) e->rack[pad].pos = e->rack[pad].len;     /* choke */
}

void hkh_engine_rack_set_vol(hkh_engine *e, int pad, float vol) {
    if (pad < 0 || pad >= HKH_RACK || !isfinite(vol)) return;
    e->r_vol[pad] = hkh_clamp(vol, 0.0f, 1.0f);
}

void hkh_engine_set_param(hkh_engine *e, int index, float value) {
    if (index < 0 || index >= P_COUNT || !isfinite(value)) return;
    value = hkh_clamp(value, 0.0f, 1.0f);
    e->param[index] = value;
    /* Motion record: a touched DECAY knob writes the step that is playing. */
    if (index == P_H_DECAY && e->motion_rec && e->seq.running && e->seq.cur_step >= 0)
        e->motion[e->seq.cur_step] = value;
}

static int is_stopped(const hkh_engine *e) { return !e->seq.running; }

/* The ONLY two places a hit reaches a voice, so muting, counting and the
 * decay bookkeeping cannot be bypassed by one trigger path. */
static void fire_kick(hkh_engine *e, float vel) {
    if (e->k_mute) return;
    ++e->k_hits;
    voices_kick(e, vel);
}

static void fire_hat(hkh_engine *e, float vel, float decay, int follows_base) {
    if (e->h_mute) return;
    ++e->h_hits;
    e->last_h_decay = decay;
    voices_hat(e, vel, decay, follows_base);
}

void hkh_engine_trigger_kick(hkh_engine *e, float velocity) {
    e->pending_k = 1;
    e->pending_k_vel = hkh_clamp(velocity, 0.0f, 1.0f);
}

void hkh_engine_trigger_hat(hkh_engine *e, float velocity, float decay) {
    e->pending_h = 1;
    e->pending_h_vel = hkh_clamp(velocity, 0.0f, 1.0f);
    e->pending_h_decay = decay < 0.0f ? -1.0f : hkh_clamp(decay, 0.0f, 1.0f);
}

void hkh_engine_set_kick_model(hkh_engine *e, int model, int audition) {
    if (model < 0 || model >= KICK_MODEL_COUNT) return;
    e->k_model = model;
    if (audition && is_stopped(e)) hkh_engine_trigger_kick(e, 1.0f);
}

void hkh_engine_set_kick_sample(hkh_engine *e, int index, int audition) {
    if (index < 0 || index >= HKH_DIGITAL_COUNT) return;
    e->k_sample = index;
    if (audition && is_stopped(e)) hkh_engine_trigger_kick(e, 1.0f);
}

void hkh_engine_set_hat_model(hkh_engine *e, int model, int audition) {
    if (model < 0 || model >= HAT_MODEL_COUNT) return;
    e->h_model = model;
    if (audition && is_stopped(e)) hkh_engine_trigger_hat(e, 1.0f, -1.0f);
}

void hkh_engine_toggle_step(hkh_engine *e, int hat, int step) {
    if (step < 0 || step >= e->seq.length) return;
    uint16_t bit = (uint16_t)(1u << step);
    if (hat) {
        e->h_pattern ^= bit;
        if ((e->h_pattern & bit) && is_stopped(e)) hkh_engine_trigger_hat(e, 1.0f, -1.0f);
    } else {
        e->k_pattern ^= bit;
        if ((e->k_pattern & bit) && is_stopped(e)) hkh_engine_trigger_kick(e, 1.0f);
    }
}

void hkh_engine_set_mute(hkh_engine *e, int hat, int on) {
    on = on ? 1 : 0;
    if (hat) e->h_mute = on; else e->k_mute = on;
    if (on) {
        voices_choke(e, hat);
        if (hat) e->h_ratchet_in = -1; else e->k_ratchet_in = -1;
    }
}

void hkh_engine_all_sound_off(hkh_engine *e) {
    voices_choke(e, 0);
    voices_choke(e, 1);
    for (int i = 0; i < HKH_RACK; ++i) e->rack[i].pos = e->rack[i].len;
    e->r_pending = 0;
    e->k_ratchet_in = e->h_ratchet_in = -1;
    e->pending_k = e->pending_h = 0;
}

void hkh_engine_set_fill(hkh_engine *e, int hat, int on) {
    on = on ? 1 : 0;
    if (hat) { e->h_fill = on; e->h_fill_age = 0; }
    else { e->k_fill = on; e->k_fill_age = 0; }
}

void hkh_engine_set_motion_rec(hkh_engine *e, int on) {
    e->motion_rec = on ? 1 : 0;
    e->rec_age = 0;
    /* The step under the finger is written at once, not a step later. */
    if (e->motion_rec && e->seq.running && e->seq.cur_step >= 0)
        e->motion[e->seq.cur_step] = e->param[P_H_DECAY];
}

/* RESET is the live "get me home" button: one press, a known state. */
void hkh_engine_reset(hkh_engine *e, int hat) {
    if (hat) {
        e->h_pattern = HKH_HAT_DEFAULT_PATTERN;      /* 3/7 in both halves */
        for (int i = 0; i < HKH_STEPS; ++i) e->motion[i] = HKH_MOTION_NONE;
        e->param[P_H_DECAY] = HKH_HAT_CLOSED_DECAY;
        e->motion_rec = 0;
        e->h_fill = 0;
        e->h_ratchet_in = -1;
    } else {
        e->k_pattern = HKH_KICK_DEFAULT_PATTERN;
        e->k_fill = 0;
        e->k_ratchet_in = -1;
    }
}

void hkh_engine_offbeat(hkh_engine *e) {
    e->h_pattern = HKH_HAT_DEFAULT_PATTERN;
}

/* SHUFFLE and RANDOM rewrite the pattern (the algorithms and their tables
 * are in hkh_pattern.c, which think in 8 steps); at 16 steps each half is
 * treated as its own 8. RESET is the way back. */
typedef uint8_t (*half_fn)(int hat, uint8_t current, uint32_t *rng);

static uint16_t per_half(hkh_engine *e, int hat, uint16_t p, half_fn fn) {
    uint8_t lo = fn(hat, (uint8_t)(p & 0xFF), &e->rng);
    if (e->seq.length != 16) return (uint16_t)((p & 0xFF00u) | lo);
    uint8_t hi = fn(hat, (uint8_t)(p >> 8), &e->rng);
    return (uint16_t)(lo | (hi << 8));
}

void hkh_engine_shuffle(hkh_engine *e, int hat) {
    if (hat) e->h_pattern = per_half(e, 1, e->h_pattern, hkh_pattern_shuffle);
    else e->k_pattern = per_half(e, 0, e->k_pattern, hkh_pattern_shuffle);
}

void hkh_engine_random(hkh_engine *e, int hat) {
    if (hat) e->h_pattern = per_half(e, 1, e->h_pattern, hkh_pattern_random);
    else e->k_pattern = per_half(e, 0, e->k_pattern, hkh_pattern_random);
}

void hkh_engine_set_length(hkh_engine *e, int steps) {
    steps = steps == 16 ? 16 : 8;
    if (steps == e->seq.length) return;
    if (steps == 16) {
        /* Carry on with what was playing: the second half repeats the first. */
        e->k_pattern = (uint16_t)((e->k_pattern & 0xFF) | ((e->k_pattern & 0xFF) << 8));
        e->h_pattern = (uint16_t)((e->h_pattern & 0xFF) | ((e->h_pattern & 0xFF) << 8));
        for (int i = 0; i < 8; ++i) e->motion[i + 8] = e->motion[i];
        for (int i = 0; i < HKH_RACK; ++i)
            e->r_pattern[i] = (uint16_t)((e->r_pattern[i] & 0xFF) | ((e->r_pattern[i] & 0xFF) << 8));
    }
    e->seq.length = steps;
}

/* FILL over the whole loop: at 8 steps the 8-step fill; at 16 the first half
 * plays as written and the fill rolls through the second, into the next bar. */
typedef struct { uint16_t steps, ratchet; float vel[HKH_STEPS]; float ratchet_vel; } fill16;

static void make_fill(const hkh_engine *e, int hat, uint16_t p, fill16 *out) {
    hkh_fill f;
    int half = e->seq.length == 16 ? 8 : 0;
    hkh_pattern_fill(hat, (uint8_t)(p >> half), &f);
    for (int i = 0; i < HKH_STEPS; ++i) out->vel[i] = 1.0f;
    for (int i = 0; i < 8; ++i) out->vel[half + i] = f.vel[i];
    out->steps = (uint16_t)((half ? (p & 0xFF) : 0) | (f.steps << half));
    out->ratchet = (uint16_t)(f.ratchet << half);
    out->ratchet_vel = f.ratchet_vel;
}

uint16_t hkh_engine_effective_pattern(const hkh_engine *e, int hat) {
    uint16_t p = hat ? e->h_pattern : e->k_pattern;
    if (!(hat ? e->h_fill : e->k_fill)) return p;
    fill16 f;
    make_fill(e, hat, p, &f);
    return f.steps;
}

static float hat_step_decay(const hkh_engine *e, int step, int *follows_base) {
    float m = e->motion[step];
    *follows_base = !(m >= 0.0f);
    return m >= 0.0f ? m : e->param[P_H_DECAY];
}

static void on_step(hkh_engine *e, int step) {
    if (e->motion_rec) e->motion[step] = e->param[P_H_DECAY];
    int ratchet_in = (int)(e->seq.step_samples * 0.5);      /* a 1/32 later */
    fill16 kf, hf;
    uint16_t kp = e->k_pattern, hp = e->h_pattern;
    if (e->k_fill) { make_fill(e, 0, kp, &kf); kp = kf.steps; }
    if (e->h_fill) { make_fill(e, 1, hp, &hf); hp = hf.steps; }
    if ((kp >> step) & 1u) {
        fire_kick(e, e->k_fill ? kf.vel[step] : 1.0f);
        if (e->k_fill && ((kf.ratchet >> step) & 1u)) {
            e->k_ratchet_in = ratchet_in;
            e->k_ratchet_vel = kf.ratchet_vel;
        }
    }
    if ((hp >> step) & 1u) {
        int follows;
        float d = hat_step_decay(e, step, &follows);
        fire_hat(e, e->h_fill ? hf.vel[step] : 1.0f, d, follows);
        if (e->h_fill && ((hf.ratchet >> step) & 1u)) {
            e->h_ratchet_in = ratchet_in;
            e->h_ratchet_vel = hf.ratchet_vel;
            e->h_ratchet_decay = d;
        }
    }
    for (int i = 0; i < HKH_RACK; ++i)
        if ((e->r_pattern[i] >> step) & 1u) fire_rack(e, i);
}

static void age_heartbeats(hkh_engine *e, int frames) {
    if (e->k_fill && (e->k_fill_age += frames) > HKH_HEARTBEAT_SAMPLES) e->k_fill = 0;
    if (e->h_fill && (e->h_fill_age += frames) > HKH_HEARTBEAT_SAMPLES) e->h_fill = 0;
    if (e->motion_rec && (e->rec_age += frames) > HKH_HEARTBEAT_SAMPLES) e->motion_rec = 0;
}

void hkh_engine_render(hkh_engine *e, float *out_l, float *out_r, int frames,
                       double beat, float bpm) {
    if (!out_l || !out_r || frames <= 0) return;
    age_heartbeats(e, frames);
    hkh_step_event ev[HKH_SEQ_MAX_EVENTS];
    int n = hkh_seq_advance(&e->seq, beat, bpm, frames, ev);
    /* A ratchet never outlives the transport. (Recording needs no such
     * guard: with no step playing there is nowhere to write.) */
    if (!e->seq.running) e->k_ratchet_in = e->h_ratchet_in = -1;
    voices_block(e, frames);

    if (e->pending_k) {
        e->pending_k = 0;
        fire_kick(e, e->pending_k_vel);
    }
    if (e->pending_h) {
        e->pending_h = 0;
        float d = e->pending_h_decay >= 0.0f ? e->pending_h_decay : e->param[P_H_DECAY];
        fire_hat(e, e->pending_h_vel, d, e->pending_h_decay < 0.0f);
    }
    if (e->r_pending) {
        uint32_t m = e->r_pending;
        e->r_pending = 0;
        for (int i = 0; i < HKH_RACK; ++i) if ((m >> i) & 1u) fire_rack(e, i);
    }
    for (int i = 0; i < HKH_RACK; ++i)
        e->r_gain[i] += (rack_vol_gain(e->r_vol[i]) - e->r_gain[i]) * BLOCK_SMOOTH;

    /* Idle fast path: nothing is sounding and nothing will start this block
     * (no step, no ratchet, no audition). Ramps and smoothing still ran in
     * voices_block, so a knob that moved while idle is where it should be. */
    if (n == 0 && e->k_ratchet_in < 0 && e->h_ratchet_in < 0 && voices_silent(e)) {
        hkh_voices *v = &e->voices;
        v->gain_a += v->gain_a_step * (float)frames;
        v->gain_d += v->gain_d_step * (float)frames;
        v->kvol += v->kvol_step * (float)frames;
        v->hvol += v->hvol_step * (float)frames;
        v->ksend += v->ksend_step * (float)frames;
        v->hsend += v->hsend_step * (float)frames;
        v->rumble_meter *= 0.994f;
        memset(out_l, 0, (size_t)frames * sizeof(float));
        memset(out_r, 0, (size_t)frames * sizeof(float));
        return;
    }

    int ei = 0;
    for (int i = 0; i < frames; ++i) {
        while (ei < n && ev[ei].offset <= i) { on_step(e, ev[ei].step); ++ei; }
        if (e->k_ratchet_in >= 0 && e->k_ratchet_in-- == 0)
            fire_kick(e, e->k_ratchet_vel);
        if (e->h_ratchet_in >= 0 && e->h_ratchet_in-- == 0)
            fire_hat(e, e->h_ratchet_vel, e->h_ratchet_decay, 0);
        float l, r;
        voices_tick(e, &l, &r);
        if (!isfinite(l) || !isfinite(r)) { l = r = 0.0f; ++e->fault_count; }
        out_l[i] = l;
        out_r[i] = r;
    }
}
