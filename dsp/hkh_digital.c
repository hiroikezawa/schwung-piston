/* Piston - digital one-shot player. MIT. */
#include "hkh_digital.h"
#include "hkh_util.h"
#include <string.h>

#define END_FADE 128   /* ~3 ms */

void hkh_digital_init(hkh_digital *d) { memset(d, 0, sizeof(*d)); }

void hkh_digital_choke(hkh_digital *d, float ms) {
    float step = 1.0f / (hkh_clamp(ms, 0.5f, 100.0f) * 0.001f * HKH_SR);
    for (int i = 0; i < 2; ++i)
        if (d->v[i].active && (d->v[i].choke_step == 0.0f || d->v[i].choke_step < step))
            d->v[i].choke_step = step;
}

void hkh_digital_trigger(hkh_digital *d, hkh_sample_ref s, float velocity) {
    hkh_digital_choke(d, 3.0f);
    if (!s.data || s.len <= 0) return;
    hkh_dig_voice *v = &d->v[d->next];
    d->next ^= 1;
    v->data = s.data;
    v->len = s.len;
    v->pos = 0;
    v->active = 1;
    v->gain = hkh_clamp(velocity, 0.0f, 1.0f) * (1.0f / 32768.0f);
    v->choke = 1.0f;
    v->choke_step = 0.0f;
}

float hkh_digital_tick(hkh_digital *d) {
    float out = 0.0f;
    for (int i = 0; i < 2; ++i) {
        hkh_dig_voice *v = &d->v[i];
        if (!v->active) continue;
        float x = (float)v->data[v->pos] * v->gain;
        int left = v->len - v->pos;
        if (left < END_FADE) x *= (float)left / (float)END_FADE;
        if (v->choke_step > 0.0f) {
            v->choke -= v->choke_step;
            if (v->choke <= 0.0f) { v->active = 0; continue; }
            x *= v->choke;
        }
        out += x;
        if (++v->pos >= v->len) v->active = 0;
    }
    return out;
}

int hkh_digital_active(const hkh_digital *d) { return d->v[0].active || d->v[1].active; }
