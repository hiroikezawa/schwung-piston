/* Piston - the 0.9.4 RUMBLE, verbatim but renamed (ref_*), as the reference
 * an older Set must still sound like: tests/test_engine.c compares it sample
 * for sample with hkh_rumble at the legacy RUMBLE page values. Taken from
 * git 9def036 (dsp/hkh_fx.c); do not "fix" it. */
#ifndef REF_RUMBLE_V094_H
#define REF_RUMBLE_V094_H
#include "../dsp/hkh_fx.h"
#include <math.h>
#include <string.h>

typedef struct {
    float buf[HKH_RUMBLE_LINES][HKH_RUMBLE_MAX];
    int len[HKH_RUMBLE_LINES], pos[HKH_RUMBLE_LINES];
    float damp[HKH_RUMBLE_LINES];
    float amount;
    float fb, send, sat, level, duck_depth;
    float sat_step, level_step, send_step;                /* per-sample ramps */
    hkh_svf_coef pre_c, out_c, grit_c;
    hkh_svf pre, out1, out2, grit, grit2;
    float grit_amt;
    float hp_x, hp_y, duck_env;
    int quiet;                 /* samples the tail has been inaudible */
} ref_rumble;

static void ref_rumble_block(ref_rumble *r, float amount, int frames);

#define REF_KNOB_SMOOTH 0.25f
static float ref_smooth_knob(float cur, float target) {
    return cur + (hkh_clamp(target, 0.0f, 1.0f) - cur) * REF_KNOB_SMOOTH;
}
static float ref_ramp(float cur, float target, int frames) {
    return (target - cur) / (float)(frames > 0 ? frames : 1);
}



/* ---- RUMBLE -------------------------------------------------------------- */
#define REF_RUMBLE_IDLE (1 << 20)

static void ref_rumble_init(ref_rumble *r) {
    memset(r, 0, sizeof(*r));
    static const int lens[HKH_RUMBLE_LINES] = {1693, 2203, 2819, 3491};
    for (int i = 0; i < HKH_RUMBLE_LINES; ++i) r->len[i] = lens[i];
    hkh_svf_set(&r->pre_c, 280.0f, 0.707f);
    r->quiet = REF_RUMBLE_IDLE;
    r->sat = 2.0f;
    ref_rumble_block(r, 0.0f, 1);
}

static void ref_rumble_block(ref_rumble *r, float amount, int frames) {
    r->amount = ref_smooth_knob(r->amount, amount);
    float a = r->amount;
    r->send_step = ref_ramp(r->send, sqrtf(a), frames);    /* it arrives early on the knob */
    r->fb = 0.72f + 0.22f * a;                 /* <= 0.94: can never self-oscillate */
    r->sat_step = ref_ramp(r->sat, 2.0f + 8.0f * a, frames);
    r->level_step = ref_ramp(r->level, 1.6f * powf(a, 0.6f), frames);
    /* Ducked enough to leave the punch clear, not so much that the rumble
     * only exists in the last few ms before the next kick. */
    r->duck_depth = 0.55f + 0.25f * a;
    hkh_svf_set(&r->out_c, 190.0f - 70.0f * a, 0.707f);   /* deeper = darker */
    /* The sub alone is inaudible on Move's speaker: the tail is driven AFTER
     * the lowpass so it grows harmonics, and those are kept up to ~1 kHz --
     * the classic "distorted reverb" rumble that reads on any speaker. */
    r->grit_amt = 0.6f + 0.6f * a;
    /* 24 dB/oct and low: enough upper harmonics for a small speaker to show
     * the rumble, none of the fizz that made it read as a bright reverb. */
    hkh_svf_set(&r->grit_c, 900.0f - 200.0f * a, 0.6f);
    if (a > 0.001f) r->quiet = 0;
    for (int i = 0; i < HKH_RUMBLE_LINES; ++i)
        if (!isfinite(r->damp[i])) { memset(r->buf, 0, sizeof(r->buf)); memset(r->damp, 0, sizeof(r->damp)); break; }
    hkh_svf_sanitize(&r->pre);
    hkh_svf_sanitize(&r->out1);
    hkh_svf_sanitize(&r->out2);
    hkh_svf_sanitize(&r->grit);
    hkh_svf_sanitize(&r->grit2);
}

static float ref_rumble_tick(ref_rumble *r, float kick) {
    /* The duck follower runs even when idle so it is ready on wake-up. */
    float ak = fabsf(kick);
    r->duck_env = ak > r->duck_env ? r->duck_env + 0.9f * (ak - r->duck_env)
                                   : r->duck_env * 0.99985f;       /* ~150 ms */
    r->send += r->send_step;
    r->sat += r->sat_step;
    r->level += r->level_step;
    if (r->quiet >= REF_RUMBLE_IDLE && r->amount < 0.001f) return 0.0f;

    float in = hkh_svf_lp(&r->pre, &r->pre_c, kick * r->send) * 0.5f;
    float o[HKH_RUMBLE_LINES], sum = 0.0f;
    for (int i = 0; i < HKH_RUMBLE_LINES; ++i) { o[i] = r->buf[i][r->pos[i]]; sum += o[i]; }
    for (int i = 0; i < HKH_RUMBLE_LINES; ++i) {
        float fbv = o[i] - 0.5f * sum;               /* Householder: orthogonal */
        r->damp[i] += 0.08f * (fbv - r->damp[i]);    /* ~560 Hz: dark loop */
        float w = in + r->fb * r->damp[i];
        r->buf[i][r->pos[i]] = hkh_tanh(w * 0.35f) * (1.0f / 0.35f);  /* bounded */
        if (++r->pos[i] >= r->len[i]) r->pos[i] = 0;
    }
    float wet = sum * 0.25f;
    float y = hkh_tanh(r->sat * wet) / r->sat * 1.6f;
    y = hkh_svf_lp(&r->out1, &r->out_c, y);
    y = hkh_svf_lp(&r->out2, &r->out_c, y);
    float g = hkh_svf_lp(&r->grit, &r->grit_c, hkh_tanh(y * 10.0f) * 0.5f);
    g = hkh_svf_lp(&r->grit2, &r->grit_c, g);
    y += g * r->grit_amt;
    float hp = y - r->hp_x + 0.9957f * r->hp_y;     /* ~30 Hz highpass */
    r->hp_x = y;
    r->hp_y = hp;
    float duck = 1.0f - r->duck_depth * fminf(1.0f, r->duck_env * 2.5f);
    float out = hp * duck * r->level;
    if (fabsf(wet) < 1e-5f && fabsf(out) < 1e-5f) { if (r->quiet < REF_RUMBLE_IDLE) ++r->quiet; }
    else r->quiet = 0;
    return out;
}

#endif
