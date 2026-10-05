/* Piston - small DSP building blocks. MIT.
 *
 * Everything here is header-only, allocation-free and branch-light, because
 * every call site runs on the SPI audio callback (see plugin_api_v1.h). */
#ifndef HKH_UTIL_H
#define HKH_UTIL_H

#include <math.h>
#include <stdint.h>

#define HKH_SR 44100.0f
#define HKH_PI 3.14159265358979f
#define HKH_TWO_PI 6.28318530717959f

static inline float hkh_clamp(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

/* Rational tanh: exact slope at 0, reaches +-1 at |x| = 3 with zero slope. */
static inline float hkh_tanh(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* Per-sample multiplier that falls 60 dB in t60 seconds. */
static inline float hkh_t60_coef(float t60) {
    if (t60 < 0.0005f) t60 = 0.0005f;
    return expf(-6.9077553f / (t60 * HKH_SR));
}

/* xorshift32 white noise in [-1, 1). State must never be 0. */
static inline float hkh_noise(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

static inline uint32_t hkh_rand_u32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}

/* Uniform [0, 1). */
static inline float hkh_rand01(uint32_t *s) {
    return (float)(hkh_rand_u32(s) >> 8) * (1.0f / 16777216.0f);
}

/* ---- Zero-delay-feedback state variable filter (Simper) ------------------
 * Stable under per-block coefficient changes, which is why it is used for
 * every filter whose cutoff is driven by a knob or an envelope. */
typedef struct { float a1, a2, a3, k; } hkh_svf_coef;
typedef struct { float ic1, ic2; } hkh_svf;

static inline void hkh_svf_set(hkh_svf_coef *c, float hz, float q) {
    hz = hkh_clamp(hz, 10.0f, 0.45f * HKH_SR);
    if (q < 0.05f) q = 0.05f;
    float g = tanf(HKH_PI * hz / HKH_SR);
    c->k = 1.0f / q;
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}

/* Returns bandpass; lowpass / highpass through the out pointers (nullable). */
static inline float hkh_svf_run(hkh_svf *s, const hkh_svf_coef *c, float x,
                                float *lp, float *hp) {
    float v3 = x - s->ic2;
    float v1 = c->a1 * s->ic1 + c->a2 * v3;
    float v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    if (lp) *lp = v2;
    if (hp) *hp = x - c->k * v1 - v2;
    return v1;
}

static inline float hkh_svf_lp(hkh_svf *s, const hkh_svf_coef *c, float x) {
    float lp;
    (void)hkh_svf_run(s, c, x, &lp, 0);
    return lp;
}

static inline float hkh_svf_hp(hkh_svf *s, const hkh_svf_coef *c, float x) {
    float hp;
    (void)hkh_svf_run(s, c, x, 0, &hp);
    return hp;
}

/* Guard a recursive state against a NaN/Inf that would otherwise latch. */
static inline void hkh_svf_sanitize(hkh_svf *s) {
    if (!isfinite(s->ic1) || !isfinite(s->ic2) ||
        fabsf(s->ic1) > 1e4f || fabsf(s->ic2) > 1e4f) {
        s->ic1 = 0.0f;
        s->ic2 = 0.0f;
    }
}

/* ---- Band-limited square (polyBLEP) ------------------------------------ */
static inline float hkh_polyblep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

/* phase in [0,1), dt = hz / SR. */
static inline float hkh_square(float phase, float dt) {
    float v = phase < 0.5f ? 1.0f : -1.0f;
    float p2 = phase + 0.5f;
    if (p2 >= 1.0f) p2 -= 1.0f;
    return v + hkh_polyblep(phase, dt) - hkh_polyblep(p2, dt);
}

static inline float hkh_wrap01(float p) {
    if (p >= 1.0f) p -= (float)(int)p;
    return p;
}

/* sin(2*pi*t) for any t, about 1.5e-4 worst error (-76 dB): the kick bodies
 * need three of these per sample, and libm's sinf is the single biggest cost
 * on the Move's cores. Range-reduce to a quarter turn, then an odd
 * polynomial. */
static inline float hkh_sin_turns(float t) {
    float r = t + 0.5f;                       /* floor without a libm call */
    int i = (int)r;
    if (r < (float)i) --i;
    t -= (float)i;                            /* [-0.5, 0.5) */
    if (t > 0.25f) t = 0.5f - t;
    else if (t < -0.25f) t = -0.5f - t;       /* [-0.25, 0.25] */
    float x = HKH_TWO_PI * t, x2 = x * x;
    return x * (1.0f - x2 * (0.16666667f - x2 * (0.0083333310f - x2 * 0.00019840874f)));
}

/* Smoothstep for macro curves. */
static inline float hkh_smoothstep(float e0, float e1, float x) {
    float t = hkh_clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

#endif
