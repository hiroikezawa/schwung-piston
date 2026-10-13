/* Piston - the second knob page of each part (jog to page 2). MIT.
 *
 *   KICK FX   TUBE      kick only: tube saturation of what is ABOVE 120 Hz,
 *                       added to the untouched kick (the sub is never bent)
 *   HAT FX    HARMONICS hat only: harmonics generated from one band
 *                       (1.9 / 2.8 / 6.6 / 9.8 kHz) and added
 *   RACK FX   BUS COMP  the whole PISTON output (kick + hat + reverb + rack):
 *                       a stereo-linked glue compressor with PUNCH
 *
 * Every one is OFF at its default (SAT 0, HARM 0, RATIO OFF), so a set saved
 * before they existed sounds exactly as it did. Fixed-size state, nothing
 * allocated, every recursive path bounded. */
#ifndef HKH_BUSFX_H
#define HKH_BUSFX_H

#include "hkh_util.h"

/* ---- the parameters (real units; their own list, so the 14-knob state,
 * the presets and the ui_state layout of page 1 are untouched) ---- */
enum {
    FX_KT_SAT,      /* 0..1        kick tube amount (+ asymmetry)         */
    FX_KT_DRIVE,    /* 0..24 dB    level into the tube                    */
    FX_HH_AMT,      /* 0..1        hat harmonics amount                   */
    FX_HH_FREQ,     /* 0..3        1.9k / 2.8k / 6.6k / 9.8k              */
    FX_BC_THRESH,   /* -36..0 dBFS                                        */
    FX_BC_RATIO,    /* 0..3        OFF / 2:1 / 4:1 / 10:1                 */
    FX_BC_ATTACK,   /* 1..30 ms                                           */
    FX_BC_RELEASE,  /* 50..600 ms                                         */
    FX_BC_SCHPF,    /* 20..200 Hz  detector only                          */
    FX_BC_MAKEUP,   /* -12..+12 dB                                        */
    FX_BC_PUNCH,    /* 0..1        off / on                               */
    FX_COUNT
};

typedef struct {
    const char *key;
    float min, max, def;
    int integer;
} hkh_fx_def;
extern const hkh_fx_def hkh_fx_defs[FX_COUNT];
float hkh_fx_clamp(int idx, float v);

/* ---- KICK TUBE ------------------------------------------------------------
 * y = x + SAT * DCblock( f(DRIVE * yb) * N - yb ),  yb = x high-passed at
 * 120 Hz (LR4) and low-passed at 6 kHz (4th order). f is an asymmetric
 * rational tanh; N holds a band at 0.4 at its level, so louder parts are
 * squashed and quieter ones (body, tail) come up -- the thickening. Only the
 * DIFFERENCE is added, so the kick's sub passes untouched and in phase. First-order ADAA
 * keeps the curve's products from aliasing; no oversampling, no latency. */
typedef struct {
    float sat, sat_step, drv, drv_step;     /* per-sample ramps */
    hkh_svf_coef hp_c, lp1_c, lp2_c;
    hkh_svf hp1, hp2, lp1, lp2;
    float au, ay;                           /* ADAA memory: last DRIVE*y, last y */
    float dc_x, dc_y;
} hkh_ktube;
void hkh_ktube_init(hkh_ktube *t);
void hkh_ktube_block(hkh_ktube *t, float sat, float drive_db, int frames);
float hkh_ktube_tick(hkh_ktube *t, float x);

/* ---- HAT HARMONICS ----------------------------------------------------------
 * Band-pass (Q 1) at the chosen frequency, band-limited in front (4th-order
 * low-pass) so what is generated stays under fs/2, then Chebyshev-style
 * generators normalised by the band's own amplitude (smoothed power):
 *   2nd b^2/e  (+ 3rd 4b^3/e^2 - 3b at 1.9k / 2.8k), high-passed at 1.3 x fc.
 * Harmonics are ADDED; nothing is boosted. 100 % = the generated band at
 * +6 dB over the band it came from (a hat is noise: less was inaudible). */
typedef struct {
    float amt, amt_step;
    float fc, w3;                           /* glide per block */
    hkh_svf_coef src1_c, src2_c, bp_c, hp_c;
    hkh_svf src1, src2, bp, hp;
    float p1, p2;
    int idle;
} hkh_harm;
void hkh_harm_init(hkh_harm *h);
void hkh_harm_block(hkh_harm *h, float amount, int freq_index, int frames);
float hkh_harm_tick(hkh_harm *h, float x);

/* ---- BUS COMP ---------------------------------------------------------------
 * Feed-forward, dB domain, stereo-linked. Detector = max(|HPF L|, |HPF R|)
 * (the SC HPF is on the detector only), peak follower (instant rise, 10 ms
 * fall), 6 dB soft knee, smooth branching attack/release on the reduction,
 * one gain for both channels. RATIO OFF glides the gain to unity (MAKEUP is
 * part of the compressor and leaves with it).
 * PUNCH changes only the EFFECTIVE values: attack >= 25 ms, release >= 100
 * ms, 10:1 -> 4:1, SC HPF >= 80 Hz. The knobs keep what the user set. */
typedef struct {
    float on, on_step, mk, mk_step;          /* per-sample ramps */
    float atk_ms, rel_ms, slope, hpf_hz;     /* glide per block */
    float a_atk, a_rel;                      /* their one-pole coefficients */
    float thresh;
    hkh_svf_coef sc_c;
    hkh_svf scl, scr;
    float det, gr;                           /* detector peak, reduction (dB) */
    float gr_peak;                           /* deepest reduction since read */
} hkh_buscomp;
typedef struct { float atk, rel, hpf; int ratio, limited; } hkh_bc_eff;
hkh_bc_eff hkh_buscomp_effective(const float *fx);
void hkh_buscomp_init(hkh_buscomp *c);
void hkh_buscomp_block(hkh_buscomp *c, const float *fx, int frames);
void hkh_buscomp_tick(hkh_buscomp *c, float *l, float *r);

#endif
