/* Piston - the rack's plate reverb (see hkh_plate.h). MIT. */
#include "hkh_plate.h"
#include <math.h>
#include <string.h>

/* The voice of the plate. Fixed: the rack has one reverb, and each pad only
 * chooses how much of itself goes in. */
#define PLATE_DECAY 0.70f          /* tank feedback: a ~3 s plate */
#define PLATE_DAMP 0.25f           /* tank one-pole damping, gentle */
#define PLATE_BW 0.9995f           /* input bandwidth, effectively open */
#define PLATE_IN_DIFF1 0.75f
#define PLATE_IN_DIFF2 0.625f
#define PLATE_DECAY_DIFF1 0.70f
#define PLATE_DECAY_DIFF2 0.50f
#define PLATE_PREDELAY 882         /* 20 ms */
#define PLATE_MAP0_BASE 996.0f     /* 672 x 1.4818 */
#define PLATE_MAP1_BASE 1345.0f    /* 908 x 1.4818 */
#define PLATE_EXCURSION 24.0f      /* 16 x 1.4818: the slow chorus that keeps it smooth */
#define PLATE_LFO_W (6.2831853f * 0.5f / 44100.0f)
#define PLATE_WET 0.6f
#define PLATE_QUIET (44100 * 2)

static inline int wrap(int i, int n) { return i >= n ? i - n : (i < 0 ? i + n : i); }

/* A fixed delay as long as its buffer: returns what was written size ago. */
static inline float delay(float *buf, int size, int *ix, float x) {
    float y = buf[*ix];
    buf[*ix] = x;
    *ix = wrap(*ix + 1, size);
    return y;
}

/* Schroeder allpass, delay = the buffer's length: (z^-L - g) / (1 - g z^-L). */
static inline float allpass(float *buf, int size, int *ix, float g, float x) {
    float d = buf[*ix];
    float u = x + g * d;
    buf[*ix] = u;
    *ix = wrap(*ix + 1, size);
    return d - g * u;
}

/* The same allpass with a fractional, modulated delay (linear read). */
static inline float allpass_mod(float *buf, int size, int *ix, float g, float dly, float x) {
    float rp = (float)*ix - dly;
    while (rp < 0.0f) rp += (float)size;
    int i0 = (int)rp;
    float fr = rp - (float)i0;
    int i1 = wrap(i0 + 1, size);
    float d = buf[i0] + (buf[i1] - buf[i0]) * fr;
    float u = x + g * d;
    buf[*ix] = u;
    *ix = wrap(*ix + 1, size);
    return d - g * u;
}

/* A tap t samples behind the line's most recent write. */
static inline float tap(const float *buf, int size, int ix, int t) {
    return buf[wrap(ix - 1 - t, size)];
}

void hkh_plate_init(hkh_plate *p) {
    memset(p, 0, sizeof(*p));
    p->lfo_c = 1.0f;
    p->quiet = PLATE_QUIET;
}

int hkh_plate_idle(const hkh_plate *p) { return p->quiet >= PLATE_QUIET; }

void hkh_plate_tick(hkh_plate *p, float in, float *l, float *r) {
    if (fabsf(in) < 1e-9f && p->quiet >= PLATE_QUIET) return;
    /* pre-delay, bandwidth, four input diffusers */
    float x = delay(p->pre, PLATE_PREDELAY, &p->i_pre, in);
    p->bw += PLATE_BW * (x - p->bw);
    x = allpass(p->in0, HKH_PLATE_IN0, &p->i_in[0], PLATE_IN_DIFF1, p->bw);
    x = allpass(p->in1, HKH_PLATE_IN1, &p->i_in[1], PLATE_IN_DIFF1, x);
    x = allpass(p->in2, HKH_PLATE_IN2, &p->i_in[2], PLATE_IN_DIFF2, x);
    x = allpass(p->in3, HKH_PLATE_IN3, &p->i_in[3], PLATE_IN_DIFF2, x);

    p->lfo_s += PLATE_LFO_W * p->lfo_c;
    p->lfo_c -= PLATE_LFO_W * p->lfo_s;

    /* the two halves, each fed by the other's end (last sample's value) */
    float left_end = p->d1[p->i_d1], right_end = p->d3[p->i_d3];
    float a = allpass_mod(p->map0, HKH_PLATE_MAP0, &p->i_map0, -PLATE_DECAY_DIFF1,
                          PLATE_MAP0_BASE + PLATE_EXCURSION * p->lfo_s, x + PLATE_DECAY * right_end);
    a = delay(p->d0, HKH_PLATE_D0, &p->i_d0, a);
    p->damp0 = a + (p->damp0 - a) * PLATE_DAMP;
    a = allpass(p->ap0, HKH_PLATE_AP0, &p->i_ap0, PLATE_DECAY_DIFF2, p->damp0 * PLATE_DECAY);
    (void)delay(p->d1, HKH_PLATE_D1, &p->i_d1, a);

    float b = allpass_mod(p->map1, HKH_PLATE_MAP1, &p->i_map1, -PLATE_DECAY_DIFF1,
                          PLATE_MAP1_BASE + PLATE_EXCURSION * p->lfo_c, x + PLATE_DECAY * left_end);
    b = delay(p->d2, HKH_PLATE_D2, &p->i_d2, b);
    p->damp1 = b + (p->damp1 - b) * PLATE_DAMP;
    b = allpass(p->ap1, HKH_PLATE_AP1, &p->i_ap1, PLATE_DECAY_DIFF2, p->damp1 * PLATE_DECAY);
    (void)delay(p->d3, HKH_PLATE_D3, &p->i_d3, b);

    /* output taps (the paper's, scaled to 44.1 kHz) */
    float yl = tap(p->d2, HKH_PLATE_D2, p->i_d2, 394) + tap(p->d2, HKH_PLATE_D2, p->i_d2, 4407)
             - tap(p->ap1, HKH_PLATE_AP1, p->i_ap1, 2835) + tap(p->d3, HKH_PLATE_D3, p->i_d3, 2958)
             - tap(p->d0, HKH_PLATE_D0, p->i_d0, 2949) - tap(p->ap0, HKH_PLATE_AP0, p->i_ap0, 277)
             - tap(p->d1, HKH_PLATE_D1, p->i_d1, 1580);
    float yr = tap(p->d0, HKH_PLATE_D0, p->i_d0, 523) + tap(p->d0, HKH_PLATE_D0, p->i_d0, 5375)
             - tap(p->ap0, HKH_PLATE_AP0, p->i_ap0, 1820) + tap(p->d1, HKH_PLATE_D1, p->i_d1, 3961)
             - tap(p->d2, HKH_PLATE_D2, p->i_d2, 3128) - tap(p->ap1, HKH_PLATE_AP1, p->i_ap1, 496)
             - tap(p->d3, HKH_PLATE_D3, p->i_d3, 179);
    yl *= PLATE_WET;
    yr *= PLATE_WET;
    *l += yl;
    *r += yr;

    /* Silent for 2 s with nothing coming in: clear and park. */
    if (fabsf(in) < 1e-9f && fabsf(yl) + fabsf(yr) < 1e-6f) {
        if (++p->quiet >= PLATE_QUIET) hkh_plate_init(p);
    } else {
        p->quiet = 0;
    }
}
