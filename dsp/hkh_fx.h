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

/* COMP: 0 = off, 1 = tightest. A punch processor rather than a level
 * compressor: every hit's transient passes untouched for a short HOLD, then
 * the body and tail are pulled down along an exponential whose time constant
 * shrinks as the knob goes up. It works per HIT (an onset detector restarts
 * it), not per level, so a long 808 tail can never keep it clamped into the
 * next kick -- and nothing is ever lifted: more COMP = the same crack, a
 * tighter, shorter sound. Kick and hat get different times.
 *
 * The engine says when a hit starts (hkh_comp_trigger, on every fired kick
 * or hat). It used to infer that from the level -- an onset detector -- and
 * a hit landing inside a long, still-loud tail was not "an onset", so it was
 * pressed down by the previous hit's envelope and all but vanished. */
typedef struct {
    float amount;
    int hat;
    int since;                 /* samples since the last hit */
    int hold;                  /* samples the transient passes untouched */
    float decay;               /* per-sample factor of the tail envelope */
    float shape;               /* the tail envelope, 1 -> 0 */
    float wet, wet_step;
} hkh_comp;

void hkh_comp_init(hkh_comp *c, int hat);
void hkh_comp_block(hkh_comp *c, float amount, int frames);
void hkh_comp_trigger(hkh_comp *c);   /* a hit starts now */
float hkh_comp_tick(hkh_comp *c, float x);

/* RUMBLE: the techno rumble, made from the KICK itself -- never an
 * oscillator. The kick (after its VOLUME) is sent into two delay paths:
 *
 *   RHYTHMIC  a tempo-synced delay (1/8, 1/16 or 1/32). What goes in is the
 *             kick's own audio through a lowpass, read through a WINDOW that
 *             opens at each kick and closes before the next repeat is due, so
 *             every repeat is a separate, rounder copy of the kick's head:
 *             at 1/16 the kick and three repeats fill a beat, "zu-bo-bo-bo".
 *             The loop is darkened (DAMPING) and highpassed on every pass and
 *             bounded by a soft clip; the read is on whole samples (no
 *             interpolation, so no hidden lowpass) and a tempo or DELAY change
 *             crossfades to the new length instead of gliding (no pitch bend).
 *   DIFFUSE   the original RUMBLE (0.1 - 0.9): a small, dark 4-line FDN.
 *
 * Each path is saturated, lowpassed (24 dB) and given speaker-band grit (the
 * sub alone does not exist on Move's speaker) -- the diffuse one exactly as
 * in 0.9.4, the repeats more gently, so each repeat stays quieter than the
 * one before it. DEFINITION blends them -- 0 is the diffuse rumble alone, 1
 * the repeats -- and also shortens the window, shortens the diffuse tail and
 * moves the duck from following the kick's level to following the beat.
 * The sum is highpassed (~30 Hz) and DUCKed by the kick.
 *
 * Old Sets: a state or preset saved before 0.11.0 has no rb_* values and loads
 * HKH_RB_LEGACY (DEFINITION 0, the other three at their midpoints), at which
 * the output is the 0.9.4 RUMBLE sample for sample. */
#define HKH_RUMBLE_LINES 4
#define HKH_RUMBLE_MAX 3600
/* The rhythmic delay line: > a 1/8 at 20.2 BPM (65490 samples at 44.1 kHz);
 * a longer time (slower tempo) is clamped to it. */
#define HKH_RB_MAX 65536

enum { RB_DELAY, RB_FB, RB_DEF, RB_DAMP, RB_DUCK, RB_COUNT };
enum { RB_DIV_8, RB_DIV_16, RB_DIV_32, RB_DIV_COUNT };
extern const char *const hkh_rb_keys[RB_COUNT];
extern const float hkh_rb_defaults[RB_COUNT];   /* a new Set */
extern const float hkh_rb_legacy[RB_COUNT];     /* a Set from before 0.11.0 */
float hkh_rb_clamp(int idx, float v);           /* NaN -> default */

typedef struct {
    /* diffuse (0.9.4) */
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

    /* the knobs (as set) and their smoothed values */
    float prm[RB_COUNT];
    float def, fbk, dmp, dck;
    float wd, wr, wd_step, wr_step;     /* diffuse / rhythmic weights, ramped */

    /* rhythmic */
    float rbuf[HKH_RB_MAX];
    int rpos;
    int rlen, rlen_old;        /* samples: current read length, the one fading out */
    int xf, xf_len;            /* crossfade position / length (0 = none) */
    float bpm;
    int t;                     /* samples since the last kick */
    int win_hold, win_end;     /* send window: open until hold, closed at end */
    int dk_hold, dk_end;       /* beat duck: full until hold, gone at end */
    float rfb;                 /* loop gain */
    float clear;               /* how far a new kick clears the old repeats */
    hkh_svf_coef rpre_c, rloop_c;
    hkh_svf rpre1, rpre2, rloop;
    hkh_svf rout1, rout2, rgrit1, rgrit2;   /* the repeats' own lowpass and grit */
    float rhp_x, rhp_y;
    float rgain;               /* rhythmic level against the diffuse one */
} hkh_rumble;

void hkh_rumble_init(hkh_rumble *r);
/* amount = the RUMBLE knob (0..1); bpm = the host tempo (invalid -> 120). */
void hkh_rumble_block(hkh_rumble *r, float amount, float bpm, int frames);
void hkh_rumble_set(hkh_rumble *r, int idx, float v);
void hkh_rumble_trigger(hkh_rumble *r);   /* a kick starts now */
float hkh_rumble_tick(hkh_rumble *r, float kick);
/* The delay time in samples for a division at a tempo (exposed for tests). */
int hkh_rumble_delay_samples(int division, float bpm);

/* REVERB: a stereo 8-line FDN room shared by both voices, each with its own
 * send. The kick's send is band-limited to 180 Hz - 2 kHz (24 dB/oct
 * lowpass): the reverb never competes with RUMBLE for the low end, and the
 * kick's click and the digital kicks' top never ring in the room. The hat's
 * send is not filtered. The return is highpassed at 150 Hz. */
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
    hkh_svf kick_lp[2];
    hkh_svf_coef kick_lp_c;
    float ret_lx, ret_ly, ret_rx, ret_ry;
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
