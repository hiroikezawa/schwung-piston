/* Piston - one-knob effects. MIT. See hkh_fx.h for the macros. */
#include "hkh_fx.h"
#include <math.h>
#include <string.h>

#define KNOB_SMOOTH 0.25f          /* per block: ~12 ms to settle */

static float smooth_knob(float cur, float target) {
    return cur + (hkh_clamp(target, 0.0f, 1.0f) - cur) * KNOB_SMOOTH;
}

/* ---- DRIVE --------------------------------------------------------------- */
void hkh_drive_init(hkh_drive *d, float pre_max, float lp_max, float lp_min) {
    memset(d, 0, sizeof(*d));
    d->pre_max = pre_max;
    d->lp_max = lp_max;
    d->lp_min = lp_min;
    d->pre = d->out = 1.0f;
    hkh_drive_block(d, 0.0f, 1);
}

static float ramp(float cur, float target, int frames) {
    return (target - cur) / (float)(frames > 0 ? frames : 1);
}

void hkh_drive_block(hkh_drive *d, float amount, int frames) {
    d->amount = smooth_knob(d->amount, amount);
    float a = d->amount;
    d->pre_step = ramp(d->pre, 1.0f + d->pre_max * a * a, frames);
    d->asym_step = ramp(d->asym, a * a, frames);
    d->out_step = ramp(d->out, 1.0f / (1.0f + 3.3f * a), frames);   /* loudness compensation */
    d->wet_step = ramp(d->wet, hkh_clamp(a * 20.0f, 0.0f, 1.0f), frames);  /* 0 = exact bypass */
    hkh_svf_set(&d->lpc, d->lp_max * powf(d->lp_min / d->lp_max, powf(a, 0.7f)), 0.707f);
    hkh_svf_sanitize(&d->lp);
}

float hkh_drive_tick(hkh_drive *d, float x) {
    d->pre += d->pre_step;
    d->asym += d->asym_step;
    d->out += d->out_step;
    d->wet += d->wet_step;
    if (d->wet <= 0.0f && d->wet_step <= 0.0f) { d->wet = 0.0f; return x; }
    float s = x * d->pre;
    float sym = hkh_tanh(s);
    float asy = hkh_tanh(s + 0.4f) - 0.3799490f;           /* tanh(0.4) */
    float y = (sym + (asy - sym) * d->asym) * d->out;
    float dc = y - d->dc_x + 0.9985f * d->dc_y;             /* asymmetry -> DC */
    d->dc_x = y;
    d->dc_y = dc;
    y = hkh_svf_lp(&d->lp, &d->lpc, dc);
    return x + (y - x) * d->wet;
}

/* ---- COMP ---------------------------------------------------------------- */
void hkh_comp_init(hkh_comp *c, int hat) {
    memset(c, 0, sizeof(*c));
    c->hat = hat;
    hkh_comp_block(c, 0.0f, 1);
    c->thr_db += c->thr_step; c->slope += c->slope_step;
    c->makeup_db += c->makeup_step; c->wet += c->wet_step;
    c->thr_step = c->slope_step = c->makeup_step = c->wet_step = 0.0f;
}

/* The static curve: gain change in dB for a detector level, soft knee. */
static float comp_gr(const hkh_comp *c, float level_db) {
    float over = level_db - c->thr_db;
    if (2.0f * over < -c->knee) return 0.0f;
    if (2.0f * fabsf(over) <= c->knee) {
        float t = over + 0.5f * c->knee;
        return -c->slope * t * t / (2.0f * c->knee);
    }
    return -c->slope * over;
}

void hkh_comp_block(hkh_comp *c, float amount, int frames) {
    c->amount = smooth_knob(c->amount, amount);
    float a = c->amount, ratio, att_ms, rel_ms, thr;
    if (c->hat) {
        thr = -6.0f - 24.0f * a;
        ratio = 1.0f + 5.0f * a;
        att_ms = 2.5f - 1.5f * a;
        rel_ms = 30.0f + 70.0f * a;
    } else {
        /* Attack stays >= 4 ms so the kick's click passes; release grows so
         * heavy settings lift the tail (the "pump"). */
        /* Short release: the gain recovers inside the tail, so the knob is
         * heard as the kick's body swelling and sustaining ("fat"), not as
         * a quieter kick. */
        thr = -3.0f - 33.0f * a;
        ratio = 1.0f + 11.0f * a;
        att_ms = 10.0f - 9.5f * a;          /* heavy = the click is squashed too */
        rel_ms = 120.0f - 60.0f * a;
    }
    /* The static curve is evaluated with the TARGET settings for makeup, and
     * the three values the per-sample gain depends on are then ramped. */
    hkh_comp target = *c;
    target.thr_db = thr;
    target.slope = 1.0f - 1.0f / ratio;
    target.knee = c->knee = 6.0f;
    c->att = expf(-1.0f / (att_ms * 0.001f * HKH_SR));
    c->rel = expf(-1.0f / (rel_ms * 0.001f * HKH_SR));
    /* Auto makeup: give back what the curve takes at the level the peak
     * detector actually sits at through a hit (just under the voice's peak,
     * because the release holds it there), so the knob changes density and
     * punch, not volume. Tails below that level come up. */
    float makeup = -comp_gr(&target, c->hat ? -6.0f : -2.0f);
    c->thr_step = ramp(c->thr_db, thr, frames);
    c->slope_step = ramp(c->slope, target.slope, frames);
    c->makeup_step = ramp(c->makeup_db, makeup, frames);
    /* The detector is still catching up during the first milliseconds of a
     * hit, where the curve takes nothing and makeup would be applied raw:
     * +15 dB on the transient, which then drags the output limiter down for
     * the whole hit. Cap the NET gain so the attack is punchier, not louder. */
    c->max_gain_db = c->hat ? 9.0f : 14.0f;
    c->wet_step = ramp(c->wet, hkh_clamp(a * 20.0f, 0.0f, 1.0f), frames);
}

float hkh_comp_tick(hkh_comp *c, float x) {
    c->thr_db += c->thr_step;
    c->slope += c->slope_step;
    c->makeup_db += c->makeup_step;
    c->wet += c->wet_step;
    float ax = fabsf(x);
    int rising = ax > c->env;
    /* Peak detector: instant attack (a kick's first cycle is already its
     * loudest, and makeup must never ride it), smoothed release. */
    c->env = rising ? ax : c->rel * c->env + (1.0f - c->rel) * ax;
    if (c->wet <= 0.0f && c->wet_step <= 0.0f) { c->wet = 0.0f; c->phase = 0; return x; }
    if (rising) c->phase = 0;         /* recompute at once on a rise */
    if (c->phase-- <= 0) {
        c->phase = 3;
        /* 20*log10(x) as 8.6859*ln(x): log10f binds GLIBC_2.43 on current
         * toolchains, which Move's glibc 2.34 does not have (check_abi.py). */
        float gr = comp_gr(c, 8.6858896f * logf(c->env + 1e-6f));
        float target = expf(fminf(gr + c->makeup_db, c->max_gain_db) * 0.11512925f);  /* dB -> gain */
        /* Gain DOWN at once (no overshoot on the attack), back UP smoothly. */
        if (c->gain <= 0.0f || target < c->gain) { c->gain = target; c->gain_step = 0.0f; }
        else c->gain_step = (target - c->gain) * 0.25f;
    }
    c->gain += c->gain_step;
    return x + (x * c->gain - x) * c->wet;
}

/* ---- RUMBLE -------------------------------------------------------------- */
#define RUMBLE_IDLE (1 << 20)

void hkh_rumble_init(hkh_rumble *r) {
    memset(r, 0, sizeof(*r));
    static const int lens[HKH_RUMBLE_LINES] = {1693, 2203, 2819, 3491};
    for (int i = 0; i < HKH_RUMBLE_LINES; ++i) r->len[i] = lens[i];
    hkh_svf_set(&r->pre_c, 280.0f, 0.707f);
    r->quiet = RUMBLE_IDLE;
    r->sat = 2.0f;
    hkh_rumble_block(r, 0.0f, 1);
}

void hkh_rumble_block(hkh_rumble *r, float amount, int frames) {
    r->amount = smooth_knob(r->amount, amount);
    float a = r->amount;
    r->send_step = ramp(r->send, sqrtf(a), frames);    /* it arrives early on the knob */
    r->fb = 0.72f + 0.22f * a;                 /* <= 0.94: can never self-oscillate */
    r->sat_step = ramp(r->sat, 2.0f + 8.0f * a, frames);
    r->level_step = ramp(r->level, 1.6f * powf(a, 0.6f), frames);
    /* Ducked enough to leave the punch clear, not so much that the rumble
     * only exists in the last few ms before the next kick. */
    r->duck_depth = 0.55f + 0.25f * a;
    hkh_svf_set(&r->out_c, 190.0f - 70.0f * a, 0.707f);   /* deeper = darker */
    /* The sub alone is inaudible on Move's speaker: the tail is driven AFTER
     * the lowpass so it grows harmonics, and those are kept up to ~1 kHz --
     * the classic "distorted reverb" rumble that reads on any speaker. */
    r->grit_amt = 0.35f + 0.45f * a;
    /* 24 dB/oct and low: enough upper harmonics for a small speaker to show
     * the rumble, none of the fizz that made it read as a bright reverb. */
    hkh_svf_set(&r->grit_c, 500.0f - 150.0f * a, 0.6f);
    if (a > 0.001f) r->quiet = 0;
    for (int i = 0; i < HKH_RUMBLE_LINES; ++i)
        if (!isfinite(r->damp[i])) { memset(r->buf, 0, sizeof(r->buf)); memset(r->damp, 0, sizeof(r->damp)); break; }
    hkh_svf_sanitize(&r->pre);
    hkh_svf_sanitize(&r->out1);
    hkh_svf_sanitize(&r->out2);
    hkh_svf_sanitize(&r->grit);
    hkh_svf_sanitize(&r->grit2);
}

float hkh_rumble_tick(hkh_rumble *r, float kick) {
    /* The duck follower runs even when idle so it is ready on wake-up. */
    float ak = fabsf(kick);
    r->duck_env = ak > r->duck_env ? r->duck_env + 0.9f * (ak - r->duck_env)
                                   : r->duck_env * 0.99985f;       /* ~150 ms */
    r->send += r->send_step;
    r->sat += r->sat_step;
    r->level += r->level_step;
    if (r->quiet >= RUMBLE_IDLE && r->amount < 0.001f) return 0.0f;

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
    float g = hkh_svf_lp(&r->grit, &r->grit_c, hkh_tanh(y * 6.0f) * 0.35f);
    g = hkh_svf_lp(&r->grit2, &r->grit_c, g);
    y += g * r->grit_amt;
    float hp = y - r->hp_x + 0.9957f * r->hp_y;     /* ~30 Hz highpass */
    r->hp_x = y;
    r->hp_y = hp;
    float duck = 1.0f - r->duck_depth * fminf(1.0f, r->duck_env * 2.5f);
    float out = hp * duck * r->level;
    if (fabsf(wet) < 1e-5f && fabsf(out) < 1e-5f) { if (r->quiet < RUMBLE_IDLE) ++r->quiet; }
    else r->quiet = 0;
    return out;
}

/* ---- REVERB -------------------------------------------------------------- */
#define REV_IDLE (44100 * 3)
#define REV_PREDELAY 530              /* 12 ms */
#define REV_DAMP 0.30f                /* one-pole in the loop: ~2.5 kHz */
#define REV_LP_HZ 3500.0f             /* return lowpass, 12 dB/oct */

void hkh_reverb_init(hkh_reverb *r) {
    memset(r, 0, sizeof(*r));
    static const int lens[HKH_REV_LINES] = {1117, 1187, 1277, 1361, 1423, 1493, 1559, 1619};
    static const int aps[HKH_REV_AP] = {225, 341, 441, 556};
    for (int i = 0; i < HKH_REV_LINES; ++i) r->len[i] = lens[i];
    for (int i = 0; i < HKH_REV_AP; ++i) r->ap_len[i] = aps[i];
    hkh_svf_set(&r->ret_lp_c, REV_LP_HZ, 0.70710678f);
    r->quiet = REV_IDLE;
}

void hkh_reverb_tick(hkh_reverb *r, float kick_send, float hat_send, float *l, float *rr) {
    float khp = kick_send - r->kick_hp_x + 0.9747f * r->kick_hp_y;   /* 180 Hz HP */
    r->kick_hp_x = kick_send;
    r->kick_hp_y = khp;
    float in = khp + hat_send;
    if (fabsf(in) > 1e-6f) r->quiet = 0;
    if (r->quiet >= REV_IDLE) { *l = *rr = 0.0f; return; }

    float x = r->pre[r->pre_pos];
    r->pre[r->pre_pos] = in;
    if (++r->pre_pos >= REV_PREDELAY) r->pre_pos = 0;
    for (int i = 0; i < HKH_REV_AP; ++i) {           /* input diffusion */
        float b = r->ap[i][r->ap_pos[i]];
        float y = b - 0.6f * x;
        r->ap[i][r->ap_pos[i]] = x + 0.6f * y;
        if (++r->ap_pos[i] >= r->ap_len[i]) r->ap_pos[i] = 0;
        x = y;
    }
    float h[HKH_REV_LINES], o[HKH_REV_LINES];
    for (int i = 0; i < HKH_REV_LINES; ++i) h[i] = o[i] = r->line[i][r->pos[i]];
    for (int span = 1; span < HKH_REV_LINES; span <<= 1)          /* Hadamard */
        for (int i = 0; i < HKH_REV_LINES; i += 2 * span)
            for (int j = i; j < i + span; ++j) {
                float a = h[j], b = h[j + span];
                h[j] = a + b;
                h[j + span] = a - b;
            }
    const float g = 0.88f * 0.35355339f;                          /* fb / sqrt(8) */
    for (int i = 0; i < HKH_REV_LINES; ++i) {
        r->damp[i] += REV_DAMP * (h[i] * g - r->damp[i]);
        r->line[i][r->pos[i]] = x * 0.35f + r->damp[i];
        if (++r->pos[i] >= r->len[i]) r->pos[i] = 0;
    }
    float L = (o[0] - o[2] + o[4] - o[6]) * 0.3f;
    float R = (o[1] - o[3] + o[5] - o[7]) * 0.3f;
    float yl = L - r->ret_lx + 0.9789f * r->ret_ly;               /* 150 Hz HP */
    float yr = R - r->ret_rx + 0.9789f * r->ret_ry;
    r->ret_lx = L; r->ret_ly = yl;
    r->ret_rx = R; r->ret_ry = yr;
    yl = hkh_svf_lp(&r->ret_lp_l, &r->ret_lp_c, yl);
    yr = hkh_svf_lp(&r->ret_lp_r, &r->ret_lp_c, yr);
    if (!isfinite(yl) || !isfinite(yr)) {
        hkh_reverb_init(r);
        yl = yr = 0.0f;
    }
    if (fabsf(yl) + fabsf(yr) < 1e-6f && fabsf(in) < 1e-6f) ++r->quiet;
    else r->quiet = 0;
    *l = yl;
    *rr = yr;
}

/* ---- LIMITER ------------------------------------------------------------- */
void hkh_limiter_init(hkh_limiter *l) {
    memset(l, 0, sizeof(*l));
    l->gain = 1.0f;
    l->rel = expf(-1.0f / (0.08f * HKH_SR));
}

void hkh_limiter_tick(hkh_limiter *l, float *left, float *right) {
    float yl = *left - l->dl_x + 0.9995f * l->dl_y;
    float yr = *right - l->dr_x + 0.9995f * l->dr_y;
    l->dl_x = *left; l->dl_y = yl;
    l->dr_x = *right; l->dr_y = yr;
    float pk = fmaxf(fabsf(yl), fabsf(yr));
    float target = pk > HKH_CEILING ? HKH_CEILING / pk : 1.0f;
    l->gain = target < l->gain ? target : target + (l->gain - target) * l->rel;
    *left = hkh_clamp(yl * l->gain, -HKH_CEILING, HKH_CEILING);
    *right = hkh_clamp(yr * l->gain, -HKH_CEILING, HKH_CEILING);
}
