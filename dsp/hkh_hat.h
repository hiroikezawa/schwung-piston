/* Piston - analog-modelled hi-hat: 808 / 909 / METALLIC / INDUSTRIAL.
 * MIT.
 *
 * All four are built from the same analog-drum-machine parts -- six square
 * oscillators at non-integer ratios, noise, two bandpasses, a highpass, a
 * VCA -- wired differently:
 *
 *   808    six squares summed, no ring modulation, almost no noise: the
 *          classic cluster through two bandpasses and a steep highpass.
 *   909    brighter, more inharmonic cluster mixed half-and-half with noise,
 *          a resonant highpass and mild saturation: "tss".
 *   METAL  the squares ring-modulated in pairs (sum/difference partials),
 *          high-Q bandpasses that ring, and harder saturation: rough metal.
 *   IND    noise ring-modulated by the cluster, heavy saturation and a
 *          sample-and-hold decimator: a mechanical, gritty hat.
 *
 * DECAY is one knob from a very tight closed hat to a long open one, and it
 * moves more than the VCA: as it opens, the "chick" transient recedes, noise
 * (sizzle) comes up, the highpass drops for more body, and the resonances of
 * the metallic models get narrower -- so it turns INTO an open hat rather
 * than a closed hat played slowly.
 */
#ifndef HKH_HAT_H
#define HKH_HAT_H

#include <stdint.h>
#include "hkh_util.h"

typedef struct {
    int active, model;
    float vel;
    float ph[6], inc[6];
    float env, env_coef;         /* main (DECAY) envelope */
    float tick, tick_coef;       /* attack transient envelope */
    float attack, attack_inc;
    float decay, fixed_decay;    /* current, and the hit's own if not following */
    int follows_base;            /* no motion on this step: DECAY knob drives it */
    float choke, choke_step;
    /* per-block coefficients */
    hkh_svf_coef c_bp1, c_bp2, c_hp, c_lp;
    float noise_mix, bp2_mix, tick_amt, tick_norm, sat, sat_norm, makeup;
    int hold_period;
    /* state */
    hkh_svf bp1, bp2, hp, lp;
    float held;
    int hold_ctr;
} hkh_hat_voice;

typedef struct {
    hkh_hat_voice v[2];
    int next;
    uint32_t noise;
    float color, filter;         /* smoothed knobs */
} hkh_hat;

void hkh_hat_init(hkh_hat *h, uint32_t seed);
/* decay: the DECAY this hit was given (a recorded motion value, or the knob).
 * follows_base: 1 if it came from the knob, so turning DECAY reshapes it. */
void hkh_hat_trigger(hkh_hat *h, int model, float velocity, float decay, int follows_base);
/* Once per block with the knobs as they stand. */
void hkh_hat_block(hkh_hat *h, float color, float filter, float base_decay);
float hkh_hat_tick(hkh_hat *h);
void hkh_hat_choke(hkh_hat *h, float ms);
int hkh_hat_active(const hkh_hat *h);

float hkh_hat_t60(float decay);          /* DECAY -> seconds (for tests) */

#endif
