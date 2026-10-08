/* Piston - analog hi-hat models. MIT. See hkh_hat.h. */
#include "hkh_hat.h"
#include <math.h>
#include <string.h>

enum { H808, H909, HMETAL, HIND };

typedef struct {
    float freqs[6];
    int ringmod;            /* 0 sum, 1 squares in pairs, 2 noise x cluster */
    float noise;            /* noise share of the source at COLOR 0.5 */
    float bp1_hz, bp1_q, bp2_hz, bp2_q, bp2_mix;
    float hp_hz, hp_q, lp_hz;
    float sat;              /* 0 = clean */
    int decimate;           /* sample-and-hold at COLOR 0 (samples) */
    float tick;             /* attack transient amount */
    float gain;
} hat_model;

static const hat_model MODELS[4] = {
    [H808] = {
        .freqs = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f},
        .ringmod = 0, .noise = 0.04f,
        .bp1_hz = 3440.0f, .bp1_q = 1.4f, .bp2_hz = 7100.0f, .bp2_q = 1.4f, .bp2_mix = 0.8f,
        .hp_hz = 6000.0f, .hp_q = 0.7f, .lp_hz = 16000.0f,
        .sat = 0.0f, .decimate = 0, .tick = 0.5f, .gain = 8.9f,
    },
    [H909] = {
        .freqs = {342.0f, 520.0f, 547.0f, 616.0f, 763.0f, 1098.0f},
        .ringmod = 0, .noise = 0.45f,
        .bp1_hz = 9000.0f, .bp1_q = 0.8f, .bp2_hz = 12500.0f, .bp2_q = 1.2f, .bp2_mix = 0.6f,
        .hp_hz = 7500.0f, .hp_q = 1.6f, .lp_hz = 17000.0f,
        .sat = 1.5f, .decimate = 0, .tick = 0.7f, .gain = 1.25f,
    },
    [HMETAL] = {
        .freqs = {440.0f, 636.0f, 712.0f, 1213.0f, 1457.0f, 1918.0f},
        .ringmod = 1, .noise = 0.12f,
        .bp1_hz = 5200.0f, .bp1_q = 5.0f, .bp2_hz = 9100.0f, .bp2_q = 6.0f, .bp2_mix = 0.9f,
        .hp_hz = 3500.0f, .hp_q = 0.7f, .lp_hz = 15000.0f,
        .sat = 2.5f, .decimate = 0, .tick = 0.4f, .gain = 1.7f,
    },
    [HIND] = {
        .freqs = {317.0f, 486.0f, 623.0f, 911.0f, 1307.0f, 1721.0f},
        .ringmod = 2, .noise = 0.6f,
        .bp1_hz = 6000.0f, .bp1_q = 0.9f, .bp2_hz = 11000.0f, .bp2_q = 1.5f, .bp2_mix = 0.5f,
        .hp_hz = 4200.0f, .hp_q = 0.9f, .lp_hz = 14000.0f,
        .sat = 4.0f, .decimate = 3, .tick = 0.9f, .gain = 1.18f,
    },
};

/* 0 = 14 ms (very tight), 0.25 = 44 ms (normal closed), 0.5 = 140 ms (loose),
 * 0.75 = 440 ms (open), 1 = 1.4 s (long open). */
/* The DECAY knob covers the upper half of the hat's range: 0 % starts at a
 * loose closed hat (the old 50 %), 100 % is the long open hat. Everything
 * below works on this mapped value. */
static float decay_span(float knob) { return 0.5f + 0.5f * hkh_clamp(knob, 0.0f, 1.0f); }

float hkh_hat_t60(float decay) {
    decay = decay_span(decay);
    return 0.014f * powf(100.0f, hkh_clamp(decay, 0.0f, 1.0f));
}

void hkh_hat_init(hkh_hat *h, uint32_t seed) {
    memset(h, 0, sizeof(*h));
    h->noise = seed ? seed : 0x2468ACEu;
    h->color = 0.5f;
    h->filter = 0.5f;
}

/* Everything that depends on the knobs, per voice, per block. */
static void voice_coefs(hkh_hat *h, hkh_hat_voice *v) {
    const hat_model *m = &MODELS[v->model];
    float color = h->color, f = h->filter, d = decay_span(v->decay);
    float open = hkh_smoothstep(0.3f, 0.85f, d);

    /* COLOR / METAL: low = darker, noisier, softer resonance; high = the
     * cluster pitched up, noise pulled out, resonances narrower and the
     * bandpasses moved up with it -- more metal. */
    float scale = 0.72f * powf(1.9f, color);
    for (int k = 0; k < 6; ++k) v->inc[k] = m->freqs[k] * scale / HKH_SR;
    v->noise_mix = hkh_clamp(m->noise * (1.6f - 1.2f * color) + 0.2f * open, 0.0f, 0.95f);
    float qm = (0.6f + 0.9f * color) * (m->ringmod ? 1.0f + 0.5f * open : 1.0f);
    float bp_shift = 0.75f + 0.5f * color;
    hkh_svf_set(&v->c_bp1, m->bp1_hz * bp_shift, m->bp1_q * qm);
    hkh_svf_set(&v->c_bp2, m->bp2_hz * bp_shift, m->bp2_q * qm);
    v->bp2_mix = m->bp2_mix;

    /* FILTER: dark (<0.5, lowpass closes) .. neutral .. bright/thin (>0.5,
     * highpass rises). Opening the hat lowers the highpass for body. */
    float hp = m->hp_hz * (f <= 0.5f ? 0.55f + 0.9f * f : 1.0f + 1.4f * (f - 0.5f));
    hp *= 1.0f - 0.25f * open;
    float lp = m->lp_hz * (f <= 0.5f ? 0.3f + 1.4f * f : 1.0f) * (1.0f + 0.15f * open);
    hkh_svf_set(&v->c_hp, hp, m->hp_q);
    hkh_svf_set(&v->c_lp, hkh_clamp(lp, 1000.0f, 19000.0f), 0.707f);

    /* A thinner hat is a quieter hat; give some of it back. */
    v->makeup = f > 0.5f ? 1.0f + 1.2f * (f - 0.5f) : 1.0f;
    v->tick_amt = m->tick * (1.0f - 0.6f * open);
    v->tick_norm = 1.0f / (1.0f + v->tick_amt);
    v->sat = m->sat;
    v->sat_norm = m->sat > 0.0f ? 1.0f / hkh_tanh(m->sat) : 1.0f;
    v->hold_period = m->decimate > 0 ? 1 + (int)lrintf((float)(m->decimate - 1) * (1.0f - color)) : 1;
    v->env_coef = hkh_t60_coef(hkh_hat_t60(v->decay));   /* t60 maps the knob itself */
}

void hkh_hat_trigger(hkh_hat *h, int model, float velocity, float decay, int follows_base) {
    if (model < 0 || model > 3) model = H909;
    /* A new hat chokes the ringing one (closed cuts open, as on the
     * machines), over 2 ms so the cut itself does not click. */
    for (int i = 0; i < 2; ++i)
        if (h->v[i].active && h->v[i].choke_step == 0.0f)
            h->v[i].choke_step = 1.0f / (0.002f * HKH_SR);
    hkh_hat_voice *v = &h->v[h->next];
    h->next ^= 1;
    memset(v, 0, sizeof(*v));
    v->active = 1;
    v->model = model;
    v->vel = hkh_clamp(velocity, 0.0f, 1.0f);
    v->decay = v->fixed_decay = hkh_clamp(decay, 0.0f, 1.0f);
    v->follows_base = follows_base;
    v->env = 1.0f;
    v->tick = 1.0f;
    v->tick_coef = hkh_t60_coef(0.012f);
    v->attack_inc = 1.0f / (0.0002f * HKH_SR);
    v->choke = 1.0f;
    /* Free-running oscillators would be closer to hardware. Starting the whole
     * cluster at phase 0 instead lines up all six rising edges at t = 0 --
     * the crisp "chick" of the attack -- and makes every hit identical, which
     * is what a sequenced hat should be. */
    for (int k = 0; k < 6; ++k) v->ph[k] = 0.0f;
    voice_coefs(h, v);
}

void hkh_hat_block(hkh_hat *h, float color, float filter, float base_decay) {
    h->color += (hkh_clamp(color, 0.0f, 1.0f) - h->color) * 0.3f;
    h->filter += (hkh_clamp(filter, 0.0f, 1.0f) - h->filter) * 0.3f;
    base_decay = hkh_clamp(base_decay, 0.0f, 1.0f);
    for (int i = 0; i < 2; ++i) {
        hkh_hat_voice *v = &h->v[i];
        if (!v->active) continue;
        /* A hit from the knob keeps following the knob (turning DECAY opens
         * the hat that is ringing); a recorded motion step keeps its own. */
        float target = v->follows_base ? base_decay : v->fixed_decay;
        v->decay += (target - v->decay) * 0.35f;
        voice_coefs(h, v);
        hkh_svf_sanitize(&v->bp1);
        hkh_svf_sanitize(&v->bp2);
        hkh_svf_sanitize(&v->hp);
        hkh_svf_sanitize(&v->lp);
    }
}

void hkh_hat_choke(hkh_hat *h, float ms) {
    float step = 1.0f / (hkh_clamp(ms, 0.5f, 100.0f) * 0.001f * HKH_SR);
    for (int i = 0; i < 2; ++i)
        if (h->v[i].active && (h->v[i].choke_step == 0.0f || h->v[i].choke_step < step))
            h->v[i].choke_step = step;
}

int hkh_hat_active(const hkh_hat *h) { return h->v[0].active || h->v[1].active; }

static float voice_tick(hkh_hat *h, hkh_hat_voice *v) {
    const hat_model *m = &MODELS[v->model];
    float sq[6];
    for (int k = 0; k < 6; ++k) {
        sq[k] = hkh_square(v->ph[k], v->inc[k]);
        v->ph[k] = hkh_wrap01(v->ph[k] + v->inc[k]);
    }
    float noise = hkh_noise(&h->noise);
    float metal, src;
    if (m->ringmod == 0) {
        metal = (sq[0] + sq[1] + sq[2] + sq[3] + sq[4] + sq[5]) * (1.0f / 6.0f);
        src = metal * (1.0f - v->noise_mix) + noise * v->noise_mix;
    } else if (m->ringmod == 1) {
        metal = (sq[0] * sq[1] + sq[2] * sq[3] + sq[4] * sq[5]) * (1.0f / 3.0f);
        src = metal * (1.0f - v->noise_mix) + noise * v->noise_mix;
    } else {
        metal = (sq[0] + sq[1] + sq[2] + sq[3] + sq[4] + sq[5]) * (1.0f / 6.0f);
        src = v->noise_mix * noise * (0.4f + 0.6f * fabsf(metal) * 2.0f) +
              (1.0f - v->noise_mix) * metal;
    }
    /* Bandpasses normalised to unity peak (x k), then the tone filters. */
    float x = hkh_svf_run(&v->bp1, &v->c_bp1, src, 0, 0) * v->c_bp1.k
            + hkh_svf_run(&v->bp2, &v->c_bp2, src, 0, 0) * v->c_bp2.k * v->bp2_mix;
    x = hkh_svf_hp(&v->hp, &v->c_hp, x);
    x = hkh_svf_lp(&v->lp, &v->c_lp, x);
    if (v->sat > 0.0f) x = hkh_tanh(v->sat * x * 2.0f) * v->sat_norm * 0.5f;
    if (v->hold_period > 1) {
        if (v->hold_ctr-- <= 0) { v->held = x; v->hold_ctr = v->hold_period - 1; }
        x = v->held;
    }

    float amp;
    if (v->attack < 1.0f) { v->attack += v->attack_inc; if (v->attack > 1.0f) v->attack = 1.0f; }
    amp = (v->env + v->tick_amt * v->tick) * v->tick_norm * v->attack;
    v->env *= v->env_coef;
    v->tick *= v->tick_coef;

    if (v->choke_step > 0.0f) {
        v->choke -= v->choke_step;
        if (v->choke <= 0.0f) { v->active = 0; return 0.0f; }
    }
    if (v->env < 1e-4f && v->tick < 1e-4f) v->active = 0;
    /* Gains match the CLOSED hats' loudness across models; a long open hat
     * finds more coincident edges in the cluster, so its peaks are caught by
     * a soft ceiling rather than being allowed to out-shout the kick. */
    return hkh_tanh(x * amp * m->gain * v->makeup) * v->vel * v->choke;
}

float hkh_hat_tick(hkh_hat *h) {
    float s = 0.0f;
    if (h->v[0].active) s += voice_tick(h, &h->v[0]);
    if (h->v[1].active) s += voice_tick(h, &h->v[1]);
    return s;
}
