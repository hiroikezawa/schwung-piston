/* Piston - the rack's plate reverb. A stereo plate after the figure-of-eight
 * tank in Jon Dattorro, "Effect Design, Part 1: Reverberator and Other
 * Filters" (JAES 45(9), 1997), written from the paper for this module: input
 * diffusion, two cross-fed halves with a slowly modulated allpass each, and
 * output taps summed from both halves. Smooth and wide, no metallic ring.
 * Fed only by the rack pads' own REV sends. MIT. */
#ifndef HKH_PLATE_H
#define HKH_PLATE_H

/* Buffer sizes at 44.1 kHz (the paper's 29761 Hz lengths x 1.4818), with
 * room for the modulation excursion. */
#define HKH_PLATE_PRE 900
#define HKH_PLATE_IN0 211
#define HKH_PLATE_IN1 159
#define HKH_PLATE_IN2 562
#define HKH_PLATE_IN3 411
#define HKH_PLATE_MAP0 1040
#define HKH_PLATE_MAP1 1390
#define HKH_PLATE_D0 6600
#define HKH_PLATE_AP0 2668
#define HKH_PLATE_D1 5513
#define HKH_PLATE_D2 6249
#define HKH_PLATE_AP1 3936
#define HKH_PLATE_D3 4687

typedef struct {
    float pre[HKH_PLATE_PRE];
    float in0[HKH_PLATE_IN0], in1[HKH_PLATE_IN1], in2[HKH_PLATE_IN2], in3[HKH_PLATE_IN3];
    float map0[HKH_PLATE_MAP0], map1[HKH_PLATE_MAP1];      /* modulated allpasses */
    float d0[HKH_PLATE_D0], ap0[HKH_PLATE_AP0], d1[HKH_PLATE_D1];   /* left half */
    float d2[HKH_PLATE_D2], ap1[HKH_PLATE_AP1], d3[HKH_PLATE_D3];   /* right half */
    int i_pre, i_in[4], i_map0, i_map1, i_d0, i_ap0, i_d1, i_d2, i_ap1, i_d3;   /* write indices */
    float bw;                     /* input bandwidth one-pole state */
    float damp0, damp1;           /* tank damping states */
    float lfo_s, lfo_c;           /* quadrature LFO (sin / cos) */
    int quiet;                    /* samples with no input and no tail */
} hkh_plate;

void hkh_plate_init(hkh_plate *p);
/* in = the summed REV sends (mono). Adds the return to *l / *r. */
void hkh_plate_tick(hkh_plate *p, float in, float *l, float *r);
/* Nothing in, nothing left ringing: the caller may skip it. */
int hkh_plate_idle(const hkh_plate *p);

#endif
