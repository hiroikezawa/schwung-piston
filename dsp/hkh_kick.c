/* Piston - analog kick engine. MIT. See hkh_kick.h for the models. */
#include "hkh_kick.h"
#include <math.h>
#include <string.h>

enum { M808, M909, MIND };

typedef struct {
    float base_hz;
    float p1_depth, p1_ms, p2_depth, p2_ms;   /* hz = base * (1 + d1*e1 + d2*e2) */
    float t60_min, t60_max;                    /* DECAY knob range, seconds */
    float attack_ms, hold_ms;
    float click_level, click_ms, click_hz, click_q;
    float pulse_level, pulse_ms;
    float shape, shape_end, shape_ms;          /* body drive: start -> end */
    float fm_index, fm_ratio;                  /* IND phase modulation (uses senv) */
    float fold, fold_ms;                       /* IND wavefold (uses genv) */
    float ring_level;                          /* IND resonators */
    float asym;                                /* IND asymmetric saturation bias */
    float gain;
} kick_model;

static const kick_model MODELS[3] = {
    [M808] = {
        .base_hz = 49.0f, .p1_depth = 1.3f, .p1_ms = 30.0f, .p2_depth = 0.15f, .p2_ms = 120.0f,
        .t60_min = 0.22f, .t60_max = 2.6f, .attack_ms = 0.8f, .hold_ms = 7.0f,
        .click_level = 0.22f, .click_ms = 1.2f, .click_hz = 1800.0f, .click_q = 0.7f,
        .pulse_level = 0.0f, .pulse_ms = 0.0f,
        .shape = 1.35f, .shape_end = 1.35f, .shape_ms = 50.0f,
        .gain = 0.80f,
    },
    [M909] = {
        .base_hz = 55.0f, .p1_depth = 3.4f, .p1_ms = 10.0f, .p2_depth = 0.35f, .p2_ms = 55.0f,
        .t60_min = 0.10f, .t60_max = 1.3f, .attack_ms = 0.2f, .hold_ms = 0.0f,
        .click_level = 0.85f, .click_ms = 3.5f, .click_hz = 3400.0f, .click_q = 0.9f,
        .pulse_level = 0.55f, .pulse_ms = 0.6f,
        .shape = 2.4f, .shape_end = 1.1f, .shape_ms = 35.0f,
        .gain = 0.76f,
    },
    [MIND] = {
        .base_hz = 45.0f, .p1_depth = 8.0f, .p1_ms = 6.0f, .p2_depth = 1.0f, .p2_ms = 85.0f,
        .t60_min = 0.08f, .t60_max = 1.8f, .attack_ms = 0.1f, .hold_ms = 3.0f,
        .click_level = 0.55f, .click_ms = 2.5f, .click_hz = 2400.0f, .click_q = 1.2f,
        .pulse_level = 0.6f, .pulse_ms = 0.6f,
        .shape = 1.7f, .shape_end = 1.05f, .shape_ms = 60.0f,
        .fm_index = 3.2f, .fm_ratio = 1.414f,
        .fold = 2.2f, .fold_ms = 70.0f,
        .ring_level = 0.35f, .asym = 0.25f,
        .gain = 0.74f,
    },
};

static float ms_coef(float ms) {           /* e^-1 per `ms` */
    return ms > 0.0f ? expf(-1.0f / (ms * 0.001f * HKH_SR)) : 0.0f;
}

float hkh_akick_t60(int model, float decay) {
    const kick_model *m = &MODELS[model < 0 || model > 2 ? 1 : model];
    decay = hkh_clamp(decay, 0.0f, 1.0f);
    return m->t60_min * powf(m->t60_max / m->t60_min, decay);
}

void hkh_akick_init(hkh_akick *k, uint32_t seed) {
    memset(k, 0, sizeof(*k));
    k->noise = seed ? seed : 0x13579BDu;
    for (int i = 0; i < 3; ++i)
        hkh_svf_set(&k->click_c[i], MODELS[i].click_hz, MODELS[i].click_q);
    k->ring_decay = -1.0f;
    hkh_akick_block(k, 0.5f);
}

void hkh_akick_trigger(hkh_akick *k, int model, float velocity, float decay) {
    if (model < 0 || model > 2) model = M909;
    /* The ringing hit fades out over 3 ms instead of being cut: no click. */
    for (int i = 0; i < 2; ++i)
        if (k->v[i].active && k->v[i].choke_step == 0.0f)
            k->v[i].choke_step = 1.0f / (0.003f * HKH_SR);
    hkh_kick_voice *v = &k->v[k->next];
    k->next ^= 1;
    const kick_model *m = &MODELS[model];
    memset(v, 0, sizeof(*v));
    v->active = 1;
    v->model = model;
    v->vel = hkh_clamp(velocity, 0.0f, 1.0f);
    v->decay = hkh_clamp(decay, 0.0f, 1.0f);
    v->amp = 1.0f;
    v->amp_coef = hkh_t60_coef(hkh_akick_t60(model, v->decay));
    v->attack_inc = m->attack_ms > 0.0f ? 1.0f / (m->attack_ms * 0.001f * HKH_SR) : 1.0f;
    v->hold = (int)(m->hold_ms * 0.001f * HKH_SR);
    v->p1 = 1.0f; v->p1c = ms_coef(m->p1_ms);
    v->p2 = 1.0f; v->p2c = ms_coef(m->p2_ms);
    v->senv = 1.0f; v->senv_c = ms_coef(m->shape_ms);
    v->genv = 1.0f; v->genv_c = ms_coef(m->fold_ms > 0.0f ? m->fold_ms : 50.0f);
    v->cenv = 1.0f; v->cenv_c = ms_coef(m->click_ms);
    v->pulse_len = (int)(m->pulse_ms * 0.001f * HKH_SR);
    v->choke = 1.0f;
    /* Every body starts at a rising zero crossing: the triangle shaper is at
     * zero a quarter cycle in. */
    v->phase = model == M909 ? 0.25f : 0.0f;
    v->shape_norm = 1.0f / hkh_tanh(m->shape);
    v->neg_drive = 1.4f + 4.0f * m->asym;
    v->neg_norm = 1.0f / hkh_tanh(v->neg_drive);
}

void hkh_akick_block(hkh_akick *k, float decay) {
    decay = hkh_clamp(decay, 0.0f, 1.0f);
    /* The IND ring rides DECAY too: a short kick must not leave a gong tail. */
    if (fabsf(decay - k->ring_decay) > 0.002f) {
        k->ring_decay = decay;
        hkh_svf_set(&k->ring1_c, 177.0f, 9.0f + 26.0f * decay);
        hkh_svf_set(&k->ring2_c, 421.0f, 7.0f + 19.0f * decay);
    }
    for (int i = 0; i < 2; ++i) {
        hkh_kick_voice *v = &k->v[i];
        if (!v->active) continue;
        /* ~15 ms glide at block rate: no zipper when DECAY is turned. */
        v->decay += (decay - v->decay) * 0.35f;
        v->amp_coef = hkh_t60_coef(hkh_akick_t60(v->model, v->decay));
        /* The drive envelope moves over tens of ms; its normaliser per
         * block (2.9 ms) is exact enough and saves a divide per sample. */
        const kick_model *m = &MODELS[v->model];
        v->shape_norm = 1.0f / hkh_tanh(m->shape_end + (m->shape - m->shape_end) * v->senv);
        hkh_svf_sanitize(&v->click_f);
        hkh_svf_sanitize(&v->ring1);
        hkh_svf_sanitize(&v->ring2);
    }
}

void hkh_akick_choke(hkh_akick *k, float ms) {
    float step = 1.0f / (hkh_clamp(ms, 0.5f, 100.0f) * 0.001f * HKH_SR);
    for (int i = 0; i < 2; ++i)
        if (k->v[i].active && (k->v[i].choke_step == 0.0f || k->v[i].choke_step < step))
            k->v[i].choke_step = step;
}

int hkh_akick_active(const hkh_akick *k) { return k->v[0].active || k->v[1].active; }

static float voice_tick(hkh_akick *k, hkh_kick_voice *v) {
    const kick_model *m = &MODELS[v->model];

    /* Pitch: two exponential dives summed in ratio. */
    float hz = m->base_hz * (1.0f + m->p1_depth * v->p1 + m->p2_depth * v->p2);
    v->p1 *= v->p1c;
    v->p2 *= v->p2c;
    float inc = hz * (1.0f / HKH_SR);

    /* Amplitude: short attack ramp (the body starts at phase 0, the ramp only
     * rounds the corner), optional hold, exponential decay. */
    float amp;
    if (v->attack < 1.0f) {
        v->attack += v->attack_inc;
        if (v->attack > 1.0f) v->attack = 1.0f;
        amp = v->attack;
    } else if (v->hold > 0) {
        --v->hold;
        amp = v->amp;
    } else {
        v->amp *= v->amp_coef;
        amp = v->amp;
    }

    /* Body. */
    float body;
    if (v->model == MIND) {
        float mod = hkh_sin_turns(v->mphase);
        v->mphase = hkh_wrap01(v->mphase + inc * m->fm_ratio);
        float idx = m->fm_index * v->senv + 0.15f;
        body = hkh_sin_turns(v->phase + idx * mod * (1.0f / HKH_TWO_PI));
        float fold = 1.0f + m->fold * v->genv;
        body = hkh_sin_turns(0.25f * fold * body);
    } else if (v->model == M909) {
        float tri = 4.0f * fabsf(v->phase - 0.5f) - 1.0f;   /* triangle, -1..1 */
        body = -hkh_sin_turns(0.25f * tri);                /* tri -> sine shaper */
    } else {
        body = hkh_sin_turns(v->phase);
    }
    v->phase = hkh_wrap01(v->phase + inc);
    float g = m->shape_end + (m->shape - m->shape_end) * v->senv;
    body = hkh_tanh(g * body) * v->shape_norm;
    v->senv *= v->senv_c;
    v->genv *= v->genv_c;
    float out = body * amp;

    /* Attack transients. */
    float click = 0.0f;
    if (v->cenv > 1e-4f) {
        click = hkh_svf_run(&v->click_f, &k->click_c[v->model], hkh_noise(&k->noise), 0, 0)
                * m->click_level * v->cenv * 2.0f;
        v->cenv *= v->cenv_c;
    }
    float pulse = 0.0f;
    if (v->age < v->pulse_len) {
        float t = (float)v->age / (float)v->pulse_len;
        pulse = v->model == MIND ? (t < 0.5f ? m->pulse_level : -m->pulse_level)
                                 : m->pulse_level * (1.0f - t);
    }
    out += click + pulse;

    if (v->model == M909) {
        /* The whole 909 voice through one soft clip: the click and the body
         * push into each other at the attack, which is part of why it is
         * harder than the 808, and the peak stays bounded. */
        out = hkh_tanh(1.3f * out) * 1.1317264f;          /* 1 / hkh_tanh(1.3) */
    } else if (v->model == MIND) {
        /* The metallic ring: two inharmonic high-Q resonators struck by the
         * transient and lightly by the body itself. */
        float strike = (click + pulse) * 0.6f + body * amp * 0.02f;
        float r = hkh_svf_run(&v->ring1, &k->ring1_c, strike, 0, 0)
                + 0.7f * hkh_svf_run(&v->ring2, &k->ring2_c, strike, 0, 0);
        out += r * m->ring_level;
        /* Asymmetric saturation inside the voice: the two polarities bend
         * with different curvature (even harmonics, grit), each bounded to
         * +-1 so the model cannot out-shout the others. */
        out = out >= 0.0f ? hkh_tanh(1.4f * out) * 1.1010260f        /* 1 / hkh_tanh(1.4) */
                          : hkh_tanh(v->neg_drive * out) * v->neg_norm;
    }

    ++v->age;
    if (v->choke_step > 0.0f) {
        v->choke -= v->choke_step;
        if (v->choke <= 0.0f) { v->active = 0; return 0.0f; }
    }
    if (amp < 1e-4f && v->cenv < 1e-4f && v->age > v->pulse_len && v->attack >= 1.0f &&
        fabsf(v->ring1.ic1) + fabsf(v->ring1.ic2) + fabsf(v->ring2.ic1) + fabsf(v->ring2.ic2) < 1e-5f) {
        v->active = 0;
    }
    return out * m->gain * v->vel * v->choke;
}

float hkh_akick_tick(hkh_akick *k) {
    float s = 0.0f;
    if (k->v[0].active) s += voice_tick(k, &k->v[0]);
    if (k->v[1].active) s += voice_tick(k, &k->v[1]);
    return s;
}
