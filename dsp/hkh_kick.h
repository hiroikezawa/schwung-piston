/* Piston - analog kick engine: 808 / 909 / INDUSTRIAL. MIT.
 *
 * Not circuit emulations. Each model is its own voice topology, chosen for
 * the character it has to deliver, and they differ from the first sample:
 *
 *   808  sine body with a slow, shallow pitch glide, a short hold before a
 *        long exponential decay (the "boom"), gentle round saturation and a
 *        soft filtered-noise beater tick.
 *   909  triangle->sine shaped body driven hard at the attack (harder, more
 *        odd harmonics that relax into a clean tail), a steep fast pitch
 *        sweep, and a two-part attack: filtered noise plus a pulse "tok".
 *   IND  a phase-modulated body (inharmonic ratio 1.414) whose FM index and
 *        wavefold both decay, so it starts as a clanging "GON/GAN" and ends
 *        as a sub; a very steep two-stage pitch dive; a square pulse and
 *        noise attack that also excite two high-Q inharmonic resonators (the
 *        metallic ring); and an asymmetric saturator INSIDE the voice.
 */
#ifndef HKH_KICK_H
#define HKH_KICK_H

#include <stdint.h>
#include "hkh_util.h"

typedef struct {
    int active, model;
    int age;                 /* samples since trigger */
    float vel;
    float phase, mphase;     /* body / FM modulator phase, 0..1 */
    float amp, amp_coef;     /* body amplitude envelope */
    float attack, attack_inc;
    int hold;
    float p1, p1c, p2, p2c;  /* pitch envelope components */
    float senv, senv_c;      /* shaping / FM / fold envelope */
    float genv, genv_c;      /* second (slower) character envelope */
    float cenv, cenv_c;      /* noise-click envelope */
    int pulse_len;
    float choke, choke_step; /* retrigger/mute fade, 1 -> 0 */
    float decay;             /* DECAY latched for this hit, eased per block */
    float shape_norm;        /* 1 / tanh(body drive), refreshed per block */
    float neg_drive, neg_norm; /* IND: the negative half's saturator */
    hkh_svf click_f, ring1, ring2;
} hkh_kick_voice;

typedef struct {
    hkh_kick_voice v[2];
    int next;
    uint32_t noise;
    hkh_svf_coef click_c[3], ring1_c, ring2_c;
    float ring_decay;        /* DECAY the ring coefficients were set for */
} hkh_akick;

void hkh_akick_init(hkh_akick *k, uint32_t seed);
void hkh_akick_trigger(hkh_akick *k, int model, float velocity, float decay);
/* Once per block: DECAY (0..1) as the knob currently stands. A ringing hit
 * glides toward it, so turning DECAY reshapes the tail that is playing. */
void hkh_akick_block(hkh_akick *k, float decay);
float hkh_akick_tick(hkh_akick *k);
void hkh_akick_choke(hkh_akick *k, float ms);
int hkh_akick_active(const hkh_akick *k);

/* DECAY knob -> T60 seconds for a model (exposed for tests). */
float hkh_akick_t60(int model, float decay);

#endif
