/* Piston - KICK TUBE, HAT HARMONICS, BUS COMP. MIT. See hkh_busfx.h. */
#include "hkh_busfx.h"
#include <math.h>
#include <string.h>

const hkh_fx_def hkh_fx_defs[FX_COUNT] = {
    [FX_KT_SAT] = {"kt_sat", 0.0f, 1.0f, 0.0f, 0},
    [FX_KT_DRIVE] = {"kt_drive", 0.0f, 24.0f, 6.0f, 0},
    [FX_HH_AMT] = {"hh_amt", 0.0f, 1.0f, 0.0f, 0},
    [FX_HH_FREQ] = {"hh_freq", 0.0f, 3.0f, 2.0f, 1},
    [FX_BC_THRESH] = {"bc_thresh", -36.0f, 0.0f, -12.0f, 0},
    [FX_BC_RATIO] = {"bc_ratio", 0.0f, 3.0f, 0.0f, 1},
    [FX_BC_ATTACK] = {"bc_attack", 1.0f, 30.0f, 30.0f, 0},
    [FX_BC_RELEASE] = {"bc_release", 50.0f, 600.0f, 150.0f, 1},
    [FX_BC_SCHPF] = {"bc_schpf", 20.0f, 200.0f, 90.0f, 1},
    [FX_BC_MAKEUP] = {"bc_makeup", -12.0f, 12.0f, 0.0f, 0},
    [FX_BC_PUNCH] = {"bc_punch", 0.0f, 1.0f, 1.0f, 1},
};

float hkh_fx_clamp(int idx, float v) {
    const hkh_fx_def *d = &hkh_fx_defs[idx];
    if (!isfinite(v)) return d->def;
    v = hkh_clamp(v, d->min, d->max);
    return d->integer ? floorf(v + 0.5f) : v;
}

static float ramp(float cur, float target, int frames) {
    return (target - cur) / (float)(frames > 0 ? frames : 1);
}
static float db2lin(float db) { return expf(db * 0.11512925f); }
#define BLOCK_GLIDE 0.25f           /* per block: ~12 ms to settle */

/* ---- KICK TUBE --------------------------------------------------------------- */
#define KT_ASYM_MIN 0.05f
#define KT_ASYM_SAT 0.25f
#define KT_ADAA_EPS 1e-3f
#define KT_DC_COEF 0.99886f         /* one-pole DC blocker at 8 Hz */
#define KT_XOVER_HZ 120.0f          /* below this the kick is never bent */
#define KT_BAND_HZ 6000.0f          /* above this nothing is bent (aliasing) */
#define KT_REF 0.4f                 /* band level the tube holds at unity */

/* Antiderivative of hkh_tanh minus its value at 0:
 * d/dv [v^2/18 + 4/3 ln(1 + v^2/3)] = v/9 + 8v/(3v^2 + 9) = hkh_tanh(v). */
static float kt_F(float v) {
    float a = fabsf(v);
    if (a >= 3.0f) return a - 3.0f + 2.34839248f;      /* F(3) = 0.5 + 4/3 ln 4 */
    return v * v * (1.0f / 18.0f) + (4.0f / 3.0f) * log1pf(v * v * (1.0f / 3.0f));
}

static void kt_rest(hkh_ktube *t) {
    memset(&t->hp1, 0, sizeof(t->hp1)); memset(&t->hp2, 0, sizeof(t->hp2));
    memset(&t->lp1, 0, sizeof(t->lp1)); memset(&t->lp2, 0, sizeof(t->lp2));
    t->ay = t->au = t->dc_x = t->dc_y = 0.0f;
}

void hkh_ktube_init(hkh_ktube *t) {
    memset(t, 0, sizeof(*t));
    hkh_svf_set(&t->hp_c, KT_XOVER_HZ, 0.70710678f);   /* x2 = LR4 */
    hkh_svf_set(&t->lp1_c, KT_BAND_HZ, 0.54119610f);   /* 4th-order Butterworth */
    hkh_svf_set(&t->lp2_c, KT_BAND_HZ, 1.30656296f);
    t->drv = 1.0f;
    kt_rest(t);
}

void hkh_ktube_block(hkh_ktube *t, float sat, float drive_db, int frames) {
    t->sat_step = ramp(t->sat, hkh_clamp(sat, 0.0f, 1.0f), frames);
    t->drv_step = ramp(t->drv, db2lin(hkh_clamp(drive_db, 0.0f, 24.0f)), frames);
}

float hkh_ktube_tick(hkh_ktube *t, float x) {
    t->drv += t->drv_step;
    if (t->sat <= 0.0f && t->sat_step <= 0.0f) {
        /* off: rest, so turning it up starts from silence (no click) */
        if (t->ay != 0.0f || t->dc_y != 0.0f) kt_rest(t);
        t->sat = 0.0f;
        return x;
    }
    t->sat += t->sat_step;
    if (t->sat < 0.0f) t->sat = 0.0f;
    float y = hkh_svf_hp(&t->hp1, &t->hp_c, x);
    y = hkh_svf_hp(&t->hp2, &t->hp_c, y);
    y = hkh_svf_lp(&t->lp1, &t->lp1_c, y);
    y = hkh_svf_lp(&t->lp2, &t->lp2_c, y);
    /* The ADAA step is taken in u = DRIVE * y alone and both ends are
     * evaluated at TODAY's asymmetry b: a moving SAT (b glides with it)
     * then never reads as a step of the signal -- silence stays silence. */
    const float b = KT_ASYM_MIN + KT_ASYM_SAT * t->sat;
    const float u = t->drv * y, du = u - t->au;
    const float shaped = fabsf(du) > KT_ADAA_EPS ? (kt_F(u + b) - kt_F(t->au + b)) / du
                                                 : hkh_tanh(0.5f * (u + t->au) + b);
    /* ADAA is the curve at a half-sample-delayed input: subtract that same
     * delayed linear part, so only what the curve MAKES is added */
    /* The curve's output is scaled so a band at KT_REF keeps its level:
     * louder parts are squashed, quieter ones (the body, the tail) come UP
     * -- by up to DRIVE at the bottom. That is what thickens a kick. */
    const float tb = hkh_tanh(b);
    const float norm = KT_REF / (hkh_tanh(t->drv * KT_REF + b) - tb);
    const float diff = (shaped - tb) * norm - 0.5f * (y + t->ay);
    t->au = u; t->ay = y;
    const float hp = KT_DC_COEF * (t->dc_y + diff - t->dc_x);
    t->dc_x = diff; t->dc_y = hp;
    if (!isfinite(hp)) { hkh_ktube_init(t); return x; }
    return x + t->sat * hp;
}

/* ---- HAT HARMONICS ------------------------------------------------------------ */
static const float HH_HZ[4] = {1900.0f, 2800.0f, 6600.0f, 9800.0f};
/* 100 % = the generated band at +6 dB over the band it came from: a hat is
 * noise, so its harmonics land among its own partials and 0.5 (the sine-
 * tuned value) measured +0.1 dB of air -- inaudible. */
#define HH_GAIN 2.0f
#define HH_W3 0.35f
#define HH_ENV_K 0.00565f           /* 1 - exp(-1 / (4 ms * fs)) */

static void hh_coefs(hkh_harm *h) {
    const int third = h->w3 > 0.0f;
    const float src = (third ? 0.13f : 0.24f) * HKH_SR;
    hkh_svf_set(&h->src1_c, src, 0.54119610f);
    hkh_svf_set(&h->src2_c, src, 1.30656296f);
    hkh_svf_set(&h->bp_c, h->fc, 1.0f);
    hkh_svf_set(&h->hp_c, fminf(1.3f * h->fc, 0.45f * HKH_SR), 0.70710678f);
}

void hkh_harm_init(hkh_harm *h) {
    memset(h, 0, sizeof(*h));
    h->fc = HH_HZ[2];
    hh_coefs(h);
}

void hkh_harm_block(hkh_harm *h, float amount, int freq_index, int frames) {
    h->amt_step = ramp(h->amt, HH_GAIN * hkh_clamp(amount, 0.0f, 1.0f), frames);
    if (freq_index < 0) freq_index = 0;
    if (freq_index > 3) freq_index = 3;
    const float fc = HH_HZ[freq_index];
    const float w3 = freq_index <= 1 ? HH_W3 : 0.0f;
    /* glide the band, so a switch sweeps rather than steps */
    if (fabsf(fc - h->fc) > 1.0f || h->w3 != w3) {
        h->fc += (fc - h->fc) * BLOCK_GLIDE;
        if (fabsf(fc - h->fc) <= 1.0f) h->fc = fc;
        h->w3 = w3;
        hh_coefs(h);
    }
}

float hkh_harm_tick(hkh_harm *h, float x) {
    if (h->amt <= 0.0f && h->amt_step <= 0.0f) {
        if (!h->idle) {               /* rest: restart from silence */
            memset(&h->src1, 0, sizeof(h->src1)); memset(&h->src2, 0, sizeof(h->src2));
            memset(&h->bp, 0, sizeof(h->bp)); memset(&h->hp, 0, sizeof(h->hp));
            h->p1 = h->p2 = 0.0f;
            h->idle = 1;
        }
        h->amt = 0.0f;
        return x;
    }
    h->idle = 0;
    h->amt += h->amt_step;
    if (h->amt < 0.0f) h->amt = 0.0f;
    float bl = hkh_svf_lp(&h->src1, &h->src1_c, x);
    bl = hkh_svf_lp(&h->src2, &h->src2_c, bl);
    const float bb = hkh_svf_run(&h->bp, &h->bp_c, bl, 0, 0) * h->bp_c.k;   /* unity-peak band */
    /* amplitude from SMOOTHED power (a peak follower's kinks alias); the
     * floor |b|/2 bounds the ratio on attacks the power lags */
    h->p1 += HH_ENV_K * (bb * bb - h->p1);
    h->p2 += HH_ENV_K * (h->p1 - h->p2);
    const float e = fmaxf(sqrtf(2.0f * h->p2), 0.5f * fabsf(bb)) + 1e-9f;
    const float q = bb / e;
    const float gen = bb * q + h->w3 * bb * (4.0f * q * q - 3.0f);
    const float hh = hkh_svf_hp(&h->hp, &h->hp_c, gen);
    if (!isfinite(hh)) { hkh_harm_init(h); return x; }
    return x + h->amt * hh;
}

/* ---- BUS COMP ------------------------------------------------------------------ */
static const float BC_RATIO[4] = {1.0f, 2.0f, 4.0f, 10.0f};
#define BC_KNEE 6.0f
#define BC_DET_REL 0.99773f         /* exp(-1 / (10 ms * fs)) */
#define BC_PUNCH_ATK 25.0f
#define BC_PUNCH_REL 100.0f
#define BC_PUNCH_HPF 80.0f

hkh_bc_eff hkh_buscomp_effective(const float *fx) {
    hkh_bc_eff e;
    e.atk = fx[FX_BC_ATTACK];
    e.rel = fx[FX_BC_RELEASE];
    e.ratio = (int)fx[FX_BC_RATIO];
    e.hpf = fx[FX_BC_SCHPF];
    e.limited = 0;
    if (fx[FX_BC_PUNCH] >= 0.5f && e.ratio > 0) {
        if (e.atk < BC_PUNCH_ATK) { e.atk = BC_PUNCH_ATK; e.limited = 1; }
        if (e.rel < BC_PUNCH_REL) { e.rel = BC_PUNCH_REL; e.limited = 1; }
        if (e.ratio > 2) { e.ratio = 2; e.limited = 1; }
        if (e.hpf < BC_PUNCH_HPF) { e.hpf = BC_PUNCH_HPF; e.limited = 1; }
    }
    return e;
}

void hkh_buscomp_init(hkh_buscomp *c) {
    memset(c, 0, sizeof(*c));
    c->mk = 1.0f;
    c->atk_ms = 30.0f;
    c->rel_ms = 150.0f;
    c->hpf_hz = 90.0f;
    c->thresh = -12.0f;
    hkh_svf_set(&c->sc_c, c->hpf_hz, 0.70710678f);
}

void hkh_buscomp_block(hkh_buscomp *c, const float *fx, int frames) {
    const hkh_bc_eff e = hkh_buscomp_effective(fx);
    const float on = e.ratio > 0 ? 1.0f : 0.0f;
    c->on_step = ramp(c->on, on, frames);
    c->mk_step = ramp(c->mk, on > 0.0f ? db2lin(fx[FX_BC_MAKEUP]) : 1.0f, frames);
    c->atk_ms += (e.atk - c->atk_ms) * BLOCK_GLIDE;
    c->rel_ms += (e.rel - c->rel_ms) * BLOCK_GLIDE;
    c->a_atk = expf(-1000.0f / (c->atk_ms * HKH_SR));
    c->a_rel = expf(-1000.0f / (c->rel_ms * HKH_SR));
    if (e.ratio > 0) c->slope += ((1.0f - 1.0f / BC_RATIO[e.ratio]) - c->slope) * BLOCK_GLIDE;
    c->thresh += (fx[FX_BC_THRESH] - c->thresh) * BLOCK_GLIDE;
    if (fabsf(e.hpf - c->hpf_hz) > 0.01f) {
        c->hpf_hz += (e.hpf - c->hpf_hz) * BLOCK_GLIDE;
        hkh_svf_set(&c->sc_c, c->hpf_hz, 0.70710678f);
    }
}

void hkh_buscomp_tick(hkh_buscomp *c, float *l, float *r) {
    c->on += c->on_step;
    c->mk += c->mk_step;
    if (c->on < 0.0f) c->on = 0.0f;
    if (c->on <= 0.0f && c->on_step <= 0.0f) {
        /* off: nothing computed, the detector rests */
        c->on = 0.0f; c->mk = 1.0f;
        c->det = c->gr = 0.0f;
        c->scl.ic1 = c->scl.ic2 = c->scr.ic1 = c->scr.ic2 = 0.0f;
        return;
    }
    const float hl = hkh_svf_hp(&c->scl, &c->sc_c, *l);
    const float hr = hkh_svf_hp(&c->scr, &c->sc_c, *r);
    const float pk = fmaxf(fabsf(hl), fabsf(hr));
    c->det = pk > c->det ? pk : c->det * BC_DET_REL;   /* instant rise, 10 ms fall */
    float red = 0.0f;
    if (c->det > 1e-6f) {
        const float over = 8.6858896f * logf(c->det) - c->thresh;   /* logf: Move's glibc */
        if (2.0f * over > BC_KNEE) red = c->slope * over;
        else if (2.0f * over > -BC_KNEE) {
            const float o = over + 0.5f * BC_KNEE;
            red = c->slope * o * o / (2.0f * BC_KNEE);
        }
    }
    const float a = red > c->gr ? c->a_atk : c->a_rel;
    c->gr = a * c->gr + (1.0f - a) * red;
    if (!isfinite(c->gr) || !isfinite(c->det)) { c->gr = c->det = 0.0f; hkh_buscomp_init(c); }
    const float shown = c->on * c->gr;
    if (shown > c->gr_peak) c->gr_peak = shown;
    const float g = 1.0f + c->on * (expf(-0.11512925f * c->gr) * c->mk - 1.0f);
    *l *= g;
    *r *= g;
}
