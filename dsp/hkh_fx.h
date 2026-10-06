/* Piston - one-knob effects. MIT.
 *
 * Each effect is ONE knob on the surface and several parameters inside; the
 * macro curves live here so they can be tuned in one place. Every state is
 * fixed-size (no allocation) and every recursive path is bounded, so no
 * setting can run away. */
#ifndef HKH_FX_H
#define HKH_FX_H

#include "hkh_util.h"

/* DRIVE: 0 = clean (bypassed exactly), 1 = heavy industrial distortion.
 * Pre-gain up to x41, a symmetric tanh that crossfades into an asymmetric
 * one (even harmonics) as it rises, loudness compensation, a lowpass that
 * closes to tame fizz, and a DC blocker for the asymmetry. */
typedef struct {
    float amount;                        /* smoothed */
    float pre_max, lp_max, lp_min;
    /* Gains that shape the waveform are RAMPED per sample across the block:
     * stepping them at block rate is an audible zipper under distortion. */
    float pre, asym, out, wet;
    float pre_step, asym_step, out_step, wet_step;
    hkh_svf_coef lpc;
    hkh_svf lp;
    float dc_x, dc_y;
} hkh_drive;

void hkh_drive_init(hkh_drive *d, float pre_max, float lp_max, float lp_min);
void hkh_drive_block(hkh_drive *d, float amount, int frames);
float hkh_drive_tick(hkh_drive *d, float x);

/* COMP: 0 = off, 1 = heavy. Threshold, ratio, attack, release and makeup all
 * follow the one knob; makeup is automatic and deliberately partial so the
 * knob adds density, not volume. Kick and hat get different time constants
 * (the kick keeps its click; the hat clamps faster). */
typedef struct {
    float amount;
    int hat;
    float env;
    float thr_db, slope, knee, att, rel, makeup_db, max_gain_db, wet;
    float thr_step, slope_step, makeup_step, wet_step;   /* per-sample ramps */
    /* The gain computer (a log and an exp) runs every 4th sample and the
     * gain is interpolated between: the fastest attack is 4 ms, so the 90 us
     * of control latency is inaudible and the comp costs a quarter. */
    float gain, gain_step;
    int phase;
} hkh_comp;

void hkh_comp_init(hkh_comp *c, int hat);
void hkh_comp_block(hkh_comp *c, float amount, int frames);
float hkh_comp_tick(hkh_comp *c, float x);

/* RUMBLE: the techno rumble as one knob. The kick is sent into a small,
 * dark feedback-delay network (a reverb that lives below ~300 Hz), the tail
 * is saturated, lowpassed hard (24 dB), highpassed out of the subsonic, and
 * ducked by the dry kick so it breathes between hits and never smears the
 * punch. Deeper = longer, darker, dirtier. 0 = off (and, once the tail has
 * died, not even computed). */
#define HKH_RUMBLE_LINES 4
#define HKH_RUMBLE_MAX 3600
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
} hkh_rumble;

void hkh_rumble_init(hkh_rumble *r);
void hkh_rumble_block(hkh_rumble *r, float amount, int frames);
float hkh_rumble_tick(hkh_rumble *r, float kick);

/* REVERB: a stereo 8-line FDN room shared by both voices, each with its own
 * send. The kick's send is highpassed at 180 Hz so the reverb never competes
 * with RUMBLE for the low end. The return is band-limited: highpassed at
 * 150 Hz and lowpassed at 3.5 kHz (12 dB/oct), and the loop damping (~2.5 kHz)
 * darkens the tail as it decays, so the hats' fizz is not what rings on. */
#define HKH_REV_LINES 8
#define HKH_REV_MAX 1700
#define HKH_REV_PRE 600
#define HKH_REV_AP 4
#define HKH_REV_AP_MAX 600
typedef struct {
    float line[HKH_REV_LINES][HKH_REV_MAX];
    int len[HKH_REV_LINES], pos[HKH_REV_LINES];
    float damp[HKH_REV_LINES];
    float pre[HKH_REV_PRE];
    int pre_pos;
    float ap[HKH_REV_AP][HKH_REV_AP_MAX];
    int ap_len[HKH_REV_AP], ap_pos[HKH_REV_AP];
    float kick_hp_x, kick_hp_y;
    float ret_lx, ret_ly, ret_rx, ret_ry;
    hkh_svf ret_lp_l, ret_lp_r;
    hkh_svf_coef ret_lp_c;
    int quiet;
} hkh_reverb;

void hkh_reverb_init(hkh_reverb *r);
/* kick_send / hat_send already scaled by their REVERB knobs. */
void hkh_reverb_tick(hkh_reverb *r, float kick_send, float hat_send, float *l, float *rr);

/* Output protection: DC blocker, then a stereo peak limiter (instant attack,
 * 80 ms release) at -3 dBFS, then a hard ceiling. However the knobs are set,
 * nothing leaves the module above -3 dBFS. */
#define HKH_CEILING 0.944f
typedef struct {
    float gain, rel;
    float dl_x, dl_y, dr_x, dr_y;
} hkh_limiter;

void hkh_limiter_init(hkh_limiter *l);
void hkh_limiter_tick(hkh_limiter *l, float *left, float *right);

#endif
