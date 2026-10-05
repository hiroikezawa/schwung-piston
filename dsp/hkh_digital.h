/* Piston - digital kick: a plain one-shot sample player. MIT.
 *
 * Deliberately simple (the spec: "基本的にはOne Shot Sample Player"). Two
 * voices so a retrigger or a sample change fades the ringing hit out over a
 * few milliseconds instead of cutting it (no click), and a short fade at the
 * end of every sample so a file that does not end on zero cannot click. */
#ifndef HKH_DIGITAL_H
#define HKH_DIGITAL_H

#include <stdint.h>

/* A one-shot sample: mono int16 at 44.1 kHz, immutable while referenced. */
typedef struct { const int16_t *data; int len; } hkh_sample_ref;

typedef struct {
    const int16_t *data;
    int len, pos, active;
    float gain, choke, choke_step;
} hkh_dig_voice;

typedef struct {
    hkh_dig_voice v[2];
    int next;
} hkh_digital;

void hkh_digital_init(hkh_digital *d);
void hkh_digital_trigger(hkh_digital *d, hkh_sample_ref s, float velocity);
float hkh_digital_tick(hkh_digital *d);
void hkh_digital_choke(hkh_digital *d, float ms);
int hkh_digital_active(const hkh_digital *d);

#endif
