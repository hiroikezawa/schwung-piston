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
    c->since = 1 << 30;
    c->shape = 1.0f;
    hkh_comp_block(c, 0.0f, 1);
    c->wet += c->wet_step;
    c->wet_step = 0.0f;
}

void hkh_comp_block(hkh_comp *c, float amount, int frames) {
    c->amount = smooth_knob(c->amount, amount);
    float a = c->amount;
    /* The tail's time constant: 0 -> barely touched, 1 -> chopped short.
     * Geometric, so the knob is even to the ear. */
    float hold_ms, tau_ms;
    if (c->hat) { hold_ms = 2.0f;  tau_ms = 125.0f * powf(6.0f / 125.0f, a); }
    else        { hold_ms = 12.0f; tau_ms = 250.0f * powf(17.0f / 250.0f, a); }
    c->hold = (int)(hold_ms * 0.001f * HKH_SR);
    c->decay = expf(-1.0f / (tau_ms * 0.001f * HKH_SR));
    c->wet_step = ramp(c->wet, hkh_clamp(a * 4.0f, 0.0f, 1.0f), frames);
}

void hkh_comp_trigger(hkh_comp *c) { c->since = 0; }

float hkh_comp_tick(hkh_comp *c, float x) {
    c->wet += c->wet_step;
    if (c->since < (1 << 30)) ++c->since;
    if (c->since <= c->hold) {
        /* back to unity within the hold (it was mid-tail on a retrigger) */
        c->shape += (1.0f - c->shape) * 0.05f;
    } else {
        c->shape *= c->decay;
    }
    if (c->wet <= 0.0f && c->wet_step <= 0.0f) { c->wet = 0.0f; return x; }
    return x + (x * c->shape - x) * c->wet;
}

/* ---- RUMBLE -------------------------------------------------------------- */
#define RUMBLE_IDLE (1 << 20)

const char *const hkh_rb_keys[RB_COUNT] = {"rb_delay", "rb_fb", "rb_def", "rb_damp", "rb_duck"};
/* A new Set: 1/16, three clear repeats, mostly the repeats, dark, ducked
 * hard (measured, see tests/test_engine.c test_rumble_*). */
const float hkh_rb_defaults[RB_COUNT] = {RB_DIV_8, 0.60f, 0.75f, 0.70f, 0.80f};
/* A Set from before 0.11.0: every scale below is exactly 1 here and the
 * rhythmic weight exactly 0, so the output is the 0.9.4 RUMBLE. */
const float hkh_rb_legacy[RB_COUNT] = {RB_DIV_8, 0.5f, 0.0f, 0.5f, 0.5f};

float hkh_rb_clamp(int idx, float v) {
    if (idx < 0 || idx >= RB_COUNT) return 0.0f;
    if (!isfinite(v)) return hkh_rb_defaults[idx];
    /* DELAY is fixed at 1/8 (0.11.1): the kick and one repeat on the
     * offbeat. The key and its slot in the state stay, so a Set saved with
     * 1/16 or 1/32 still opens -- at 1/8. */
    if (idx == RB_DELAY) return (float)RB_DIV_8;
    return hkh_clamp(v, 0.0f, 1.0f);
}

int hkh_rumble_delay_samples(int division, float bpm) {
    static const float beats[RB_DIV_COUNT] = {0.5f, 0.25f, 0.125f};
    if (division < 0 || division >= RB_DIV_COUNT) division = RB_DIV_16;
    float b = (bpm >= 20.0f && bpm <= 999.0f) ? bpm : 120.0f;
    long n = lroundf(beats[division] * 60.0f / b * HKH_SR);
    if (n < 64) n = 64;
    if (n > HKH_RB_MAX - 1) n = HKH_RB_MAX - 1;
    return (int)n;
}

#define RB_XFADE_MS 20.0f          /* a new delay time crossfades in this long */
#define RB_WIN_ATTACK_MS 3.0f      /* the send window opens this softly */
#define RB_DUCK_ATTACK_MS 1.0f     /* the beat duck closes this fast */
#define RB_LOOP_HP_HZ 30.0f        /* every repeat loses its subsonic */
#define RB_T_MAX (1 << 30)

/* Smoothed toward the knob, landing EXACTLY on it (DEFINITION 0 must be a
 * true 0 for the 0.9.4 path to be sample-identical). */
static float glide_exact(float cur, float target) {
    cur += (target - cur) * KNOB_SMOOTH;
    return fabsf(target - cur) < 1e-4f ? target : cur;
}

/* A per-sample ramp to `target` that lands ON it: summing a step 128 times
 * leaves a float a hair off, and the weights must be exactly 1 and 0 at
 * DEFINITION 0 for the legacy output to be the 0.9.4 one. */
static float weight_step(float *w, float target, int frames) {
    if (fabsf(target - *w) < 1e-5f) { *w = target; return 0.0f; }
    return ramp(*w, target, frames);
}

void hkh_rumble_init(hkh_rumble *r) {
    memset(r, 0, sizeof(*r));
    static const int lens[HKH_RUMBLE_LINES] = {1693, 2203, 2819, 3491};
    for (int i = 0; i < HKH_RUMBLE_LINES; ++i) r->len[i] = lens[i];
    hkh_svf_set(&r->pre_c, 280.0f, 0.707f);
    r->quiet = RUMBLE_IDLE;
    r->sat = 2.0f;
    for (int i = 0; i < RB_COUNT; ++i) r->prm[i] = hkh_rb_defaults[i];
    r->def = r->prm[RB_DEF];
    r->fbk = r->prm[RB_FB];
    r->dmp = r->prm[RB_DAMP];
    r->dck = r->prm[RB_DUCK];
    r->t = RB_T_MAX;
    r->bpm = 120.0f;
    hkh_rumble_block(r, 0.0f, 120.0f, 1);
    r->wd += r->wd_step; r->wd_step = 0.0f;
    r->wr += r->wr_step; r->wr_step = 0.0f;
}

void hkh_rumble_set(hkh_rumble *r, int idx, float v) {
    if (idx < 0 || idx >= RB_COUNT) return;
    r->prm[idx] = hkh_rb_clamp(idx, v);
}

void hkh_rumble_trigger(hkh_rumble *r) { r->t = 0; }

void hkh_rumble_block(hkh_rumble *r, float amount, float bpm, int frames) {
    r->amount = smooth_knob(r->amount, amount);
    float a = r->amount;
    r->def = glide_exact(r->def, r->prm[RB_DEF]);
    r->fbk = glide_exact(r->fbk, r->prm[RB_FB]);
    r->dmp = glide_exact(r->dmp, r->prm[RB_DAMP]);
    r->dck = glide_exact(r->dck, r->prm[RB_DUCK]);
    float def = r->def;
    /* DAMPING moves every lowpass of the rumble together: 0.57x .. 1.74x of
     * the 0.9.4 cutoffs (1x at the legacy midpoint). */
    float dscale = expf(1.1090355f * (0.5f - r->dmp));     /* 2^(1.6 (0.5 - DAMP)) */

    r->send_step = ramp(r->send, sqrtf(a), frames);    /* it arrives early on the knob */
    /* The diffuse loop: 0.72 .. 0.94 from RUMBLE, scaled by FEEDBACK
     * (0.75x .. 1.25x) and shortened by DEFINITION (down to 0.7x) so it
     * thickens the repeats instead of filling the gaps between them. Capped
     * below 1: it can never self-oscillate. */
    r->fb = fminf(0.97f, (0.72f + 0.22f * a) * (1.0f + 0.5f * (r->fbk - 0.5f))) * (1.0f - 0.3f * def);
    r->sat_step = ramp(r->sat, 2.0f + 8.0f * a, frames);
    r->level_step = ramp(r->level, 1.6f * powf(a, 0.6f), frames);
    /* Ducked enough to leave the punch clear, not so much that the rumble
     * only exists in the last few ms before the next kick. DUCK: 0 = never
     * ducked, the legacy midpoint = 0.9.4 (0.55 .. 0.8 from RUMBLE), 1 =
     * silence under the kick. */
    float dbase = 0.55f + 0.25f * a;
    r->duck_depth = r->dck <= 0.5f ? dbase * (2.0f * r->dck)
                                   : dbase + (1.0f - dbase) * (2.0f * r->dck - 1.0f);
    hkh_svf_set(&r->out_c, (190.0f - 70.0f * a) * dscale, 0.707f);   /* deeper = darker */
    /* The sub alone is inaudible on Move's speaker: the tail is driven AFTER
     * the lowpass so it grows harmonics, and those are kept up to ~1 kHz --
     * the classic "distorted reverb" rumble that reads on any speaker. */
    r->grit_amt = 0.6f + 0.6f * a;
    /* 24 dB/oct and low: enough upper harmonics for a small speaker to show
     * the rumble, none of the fizz that made it read as a bright reverb. */
    hkh_svf_set(&r->grit_c, (900.0f - 200.0f * a) * dscale, 0.6f);

    /* DEFINITION: the rhythmic weight is a sine; the diffuse one falls
     * faster (cos^1.5, -12 dB at 75 %) so the default has clear gaps, and
     * keeps a floor of -22 dB at 100 % as the body under the repeats. */
    float c = cosf(def * 1.5707963f);
    r->wd_step = weight_step(&r->wd, 1.0f - 0.92f * (1.0f - c * sqrtf(c)), frames);
    r->wr_step = weight_step(&r->wr, sinf(def * 1.5707963f), frames);

    /* The rhythmic delay. A new time (tempo, DELAY) is never glided -- that
     * bends the pitch of every repeat in the line -- but crossfaded from the
     * old read position to the new one. One crossfade at a time; a tempo
     * ramp is followed in steps of whatever it moved meanwhile. */
    r->bpm = (bpm >= 20.0f && bpm <= 999.0f) ? bpm : 120.0f;
    int target = hkh_rumble_delay_samples((int)r->prm[RB_DELAY], r->bpm);
    if (r->rlen <= 0) r->rlen = target;
    else if (target != r->rlen && r->xf_len == 0) {
        r->rlen_old = r->rlen;
        r->rlen = target;
        r->xf = 0;
        r->xf_len = (int)(RB_XFADE_MS * 0.001f * HKH_SR);
    }
    float D = (float)r->rlen;
    /* The send window, as a share of the delay time: DEFINITION 1 = 0.6 (a
     * 0.4 gap of silence before the next repeat), 0.5 = 0.925 (the repeats
     * touch), 0 = 1.25 (they overlap). The last 35 % of it is the fade. */
    float win = D * (1.25f - 0.65f * def);
    r->win_end = (int)win;
    r->win_hold = (int)(win * 0.65f);
    /* The beat duck: full under the kick's attack, gone by 0.55 .. 0.85 of a
     * repeat (longer with DUCK), so the first repeat always arrives unducked. */
    r->dk_hold = (int)((0.04f + 0.10f * r->dck) * D);
    r->dk_end = (int)((0.55f + 0.30f * r->dck) * D);
    /* Loop gain from FEEDBACK: 0.12 (one repeat, maybe two) .. 0.92. */
    r->rfb = 0.20f + 0.75f * r->fbk;
    /* How much a new kick clears the old repeats: DUCK, scaled by DEFINITION
     * (0 at DEFINITION 0: the diffuse rumble keeps its long overlap). */
    r->clear = fminf(1.0f, 1.25f * r->dck) * r->def;
    /* What goes in is lowpassed (DAMPING 0: 1.8 kHz, hard; 1: 180 Hz,
     * round), and every pass through the loop darkens it further. Both
     * Butterworth or below: no resonance, no pitch of their own. */
    hkh_svf_set(&r->rpre_c, 1800.0f * powf(0.1f, r->dmp), 0.70710678f);
    hkh_svf_set(&r->rloop_c, 2000.0f * powf(0.06f, r->dmp), 0.5f);
    /* The repeats' level against the diffuse rumble. From 75 % on the gaps
     * between them grow silent, so they are lifted (up to +2.6 dB at 100 %)
     * to keep the knob's travel within ~6 dB of loudness (tests). */
    r->rgain = 0.85f * (1.0f + 1.4f * fmaxf(0.0f, def - 0.75f));

    if (a > 0.001f) r->quiet = 0;
    for (int i = 0; i < HKH_RUMBLE_LINES; ++i)
        if (!isfinite(r->damp[i])) { memset(r->buf, 0, sizeof(r->buf)); memset(r->damp, 0, sizeof(r->damp)); break; }
    if (!isfinite(r->rhp_x) || !isfinite(r->rhp_y)) {
        memset(r->rbuf, 0, sizeof(r->rbuf));
        r->rhp_x = r->rhp_y = 0.0f;
    }
    hkh_svf_sanitize(&r->pre);
    hkh_svf_sanitize(&r->out1);
    hkh_svf_sanitize(&r->out2);
    hkh_svf_sanitize(&r->grit);
    hkh_svf_sanitize(&r->grit2);
    hkh_svf_sanitize(&r->rpre1);
    hkh_svf_sanitize(&r->rpre2);
    hkh_svf_sanitize(&r->rloop);
    hkh_svf_sanitize(&r->rout1);
    hkh_svf_sanitize(&r->rout2);
    hkh_svf_sanitize(&r->rgrit1);
    hkh_svf_sanitize(&r->rgrit2);
}

/* 1 -> 0 along a raised cosine as t goes from `hold` to `end`. */
static float fall(int t, int hold, int end) {
    if (t < hold) return 1.0f;
    if (t >= end) return 0.0f;
    float u = (float)(t - hold) / (float)(end - hold);
    return 0.5f + 0.5f * hkh_sin_turns(0.25f + 0.5f * u);
}

static float rumble_rhythmic(hkh_rumble *r, float kick) {
    float x = kick * r->send;
    x = hkh_svf_lp(&r->rpre2, &r->rpre_c, hkh_svf_lp(&r->rpre1, &r->rpre_c, x));
    int att = (int)(RB_WIN_ATTACK_MS * 0.001f * HKH_SR);
    float w = r->t < att ? (float)r->t / (float)att : fall(r->t, r->win_hold, r->win_end);
    x *= w;

    int rp = r->rpos - r->rlen;
    if (rp < 0) rp += HKH_RB_MAX;
    float y = r->rbuf[rp];
    if (r->xf_len > 0) {
        int op = r->rpos - r->rlen_old;
        if (op < 0) op += HKH_RB_MAX;
        float g = (float)r->xf / (float)r->xf_len;
        y = r->rbuf[op] + (y - r->rbuf[op]) * g;
        if (++r->xf >= r->xf_len) r->xf = r->xf_len = 0;
    }
    /* the loop: ~30 Hz highpass, DAMPING lowpass, gain, soft bound */
    float hp = y - r->rhp_x + (1.0f - 6.2831853f * RB_LOOP_HP_HZ / HKH_SR) * r->rhp_y;
    r->rhp_x = y;
    r->rhp_y = hp;
    float fbv = hkh_svf_lp(&r->rloop, &r->rloop_c, hp) * r->rfb;
    /* A new kick thins out what the last one left in the loop: while its own
     * window is open, the old repeats are fed back at (1 - clear). Its 4th
     * repeat (under the new kick, ducked) then has no 5th to land on the new
     * kick's 1st, so the bar stays "zu-bo-bo-bo" instead of a smear. */
    if (r->t < r->win_end) fbv *= 1.0f - r->clear;
    float wv = hkh_tanh((x + fbv) * 0.5f) * 2.0f;          /* |w| < 2: bounded */
    if (fabsf(wv) < 1e-15f) wv = 0.0f;                     /* no denormals in the line */
    r->rbuf[r->rpos] = wv;
    if (++r->rpos >= HKH_RB_MAX) r->rpos = 0;
    return y;                                             /* the repeats only */
}

float hkh_rumble_tick(hkh_rumble *r, float kick) {
    /* The duck follower runs even when idle so it is ready on wake-up. */
    float ak = fabsf(kick);
    r->duck_env = ak > r->duck_env ? r->duck_env + 0.9f * (ak - r->duck_env)
                                   : r->duck_env * 0.99985f;       /* ~150 ms */
    r->send += r->send_step;
    r->sat += r->sat_step;
    r->level += r->level_step;
    r->wd += r->wd_step;
    r->wr += r->wr_step;
    if (r->t < RB_T_MAX) ++r->t;
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
    float wet = sum * 0.25f * r->wd;
    float y = hkh_tanh(r->sat * wet) / r->sat * 1.6f;
    y = hkh_svf_lp(&r->out1, &r->out_c, y);
    y = hkh_svf_lp(&r->out2, &r->out_c, y);
    float g = hkh_svf_lp(&r->grit, &r->grit_c, hkh_tanh(y * 10.0f) * 0.5f);
    g = hkh_svf_lp(&r->grit2, &r->grit_c, g);
    y += g * r->grit_amt;
    /* The repeats: the same filters, a gentle drive instead of the heavy one
     * (that one levels every repeat to the same loudness), and grit from a
     * full-wave rectifier -- even harmonics in proportion to the level (its
     * DC goes in the highpass below), so a repeat 5 dB down is 5 dB down on
     * the speaker too. */
    float rx = rumble_rhythmic(r, kick) * r->rgain;
    float ry = hkh_tanh(rx * 1.5f) * (1.0f / 1.5f);
    ry = hkh_svf_lp(&r->rout1, &r->out_c, ry);
    ry = hkh_svf_lp(&r->rout2, &r->out_c, ry);
    float rg = hkh_svf_lp(&r->rgrit1, &r->grit_c, fabsf(ry) * 1.2f);   /* full-wave: 2f, 4f, ... */
    rg = hkh_svf_lp(&r->rgrit2, &r->grit_c, rg);
    y += (ry + rg * r->grit_amt) * r->wr;
    wet += rx * r->wr;                              /* for the idle test only */
    float hp = y - r->hp_x + 0.9957f * r->hp_y;     /* ~30 Hz highpass */
    r->hp_x = y;
    r->hp_y = hp;
    /* DUCK: DEFINITION 0 follows the kick's level (0.9.4), 1 follows the
     * beat -- full at the kick, gone before the first repeat. */
    float foll = fminf(1.0f, r->duck_env * 2.5f);
    int dat = (int)(RB_DUCK_ATTACK_MS * 0.001f * HKH_SR);
    float beat = r->t < dat ? (float)r->t / (float)dat : fall(r->t, r->dk_hold, r->dk_end);
    float duck = 1.0f - r->duck_depth * (foll + (beat - foll) * r->def);
    float out = hp * duck * r->level;
    if (fabsf(wet) < 1e-5f && fabsf(out) < 1e-5f) { if (r->quiet < RUMBLE_IDLE) ++r->quiet; }
    else r->quiet = 0;
    return out;
}

/* ---- REVERB -------------------------------------------------------------- */
#define REV_IDLE (44100 * 3)
#define REV_PREDELAY 530              /* 12 ms */
#define REV_KICK_LP_HZ 600.0f         /* kick send lowpass, 24 dB/oct (2 kHz until 0.12.3:
                                         * IND's 0.5-2 kHz grit made a high room) */

void hkh_reverb_init(hkh_reverb *r) {
    memset(r, 0, sizeof(*r));
    static const int lens[HKH_REV_LINES] = {1117, 1187, 1277, 1361, 1423, 1493, 1559, 1619};
    static const int aps[HKH_REV_AP] = {225, 341, 441, 556};
    for (int i = 0; i < HKH_REV_LINES; ++i) r->len[i] = lens[i];
    for (int i = 0; i < HKH_REV_AP; ++i) r->ap_len[i] = aps[i];
    hkh_svf_set(&r->kick_lp_c, REV_KICK_LP_HZ, 0.70710678f);   /* x2: 24 dB/oct */
    r->quiet = REV_IDLE;
}

void hkh_reverb_tick(hkh_reverb *r, float kick_send, float hat_send, float *l, float *rr) {
    float khp = kick_send - r->kick_hp_x + 0.9747f * r->kick_hp_y;   /* 180 Hz HP */
    r->kick_hp_x = kick_send;
    r->kick_hp_y = khp;
    khp = hkh_svf_lp(&r->kick_lp[1], &r->kick_lp_c, hkh_svf_lp(&r->kick_lp[0], &r->kick_lp_c, khp));
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
        r->damp[i] += 0.45f * (h[i] * g - r->damp[i]);
        r->line[i][r->pos[i]] = x * 0.35f + r->damp[i];
        if (++r->pos[i] >= r->len[i]) r->pos[i] = 0;
    }
    float L = (o[0] - o[2] + o[4] - o[6]) * 0.3f;
    float R = (o[1] - o[3] + o[5] - o[7]) * 0.3f;
    float yl = L - r->ret_lx + 0.9789f * r->ret_ly;               /* 150 Hz HP */
    float yr = R - r->ret_rx + 0.9789f * r->ret_ry;
    r->ret_lx = L; r->ret_ly = yl;
    r->ret_rx = R; r->ret_ry = yr;
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
