/* Piston - pattern generators behind SHUFFLE, RANDOM and FILL. MIT.
 *
 * Kept apart from the engine so the musical rules can be tuned without
 * touching timing or sound: every table these use sits at the top of
 * hkh_pattern.c. A pattern is 8 bits, bit i = step i+1. */
#ifndef HKH_PATTERN_H
#define HKH_PATTERN_H

#include <stdint.h>

typedef struct {
    uint8_t steps;      /* the pattern that plays while FILL is held */
    uint8_t ratchet;    /* steps that also fire a 1/32 later */
    float vel[8];       /* per-step velocity */
    float ratchet_vel;
} hkh_fill;

/* A NEW 8-step pattern, never equal to `current` (hat = 0 kick, 1 hat). */
uint8_t hkh_pattern_random(int hat, uint8_t current, uint32_t *rng);
/* The current pattern REARRANGED (same material, moved), never equal to it
 * unless nothing can move (empty or full). */
uint8_t hkh_pattern_shuffle(int hat, uint8_t current, uint32_t *rng);
/* The momentary variation FILL plays while held. */
void hkh_pattern_fill(int hat, uint8_t current, hkh_fill *out);

#endif
