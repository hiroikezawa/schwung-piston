/* Piston - SHUFFLE / RANDOM / FILL. MIT. Tune the tables below. */
#include "hkh_pattern.h"
#include "hkh_util.h"

#define B(s) ((uint8_t)(1u << ((s) - 1)))          /* step number (1-8) -> bit */

/* ---- RANDOM ---------------------------------------------------------------
 * Half the time a curated industrial pattern, half the time a fresh draw from
 * per-step probabilities, so RANDOM is both reliable and surprising. */
static const uint8_t KICK_CLASSICS[] = {
    B(1) | B(5),                         /* X---X--- four on the floor     */
    B(1) | B(4) | B(7),                  /* X--X--X- tresillo              */
    B(1) | B(3) | B(7),                  /* X-X---X-                       */
    B(1) | B(5) | B(7),                  /* X---X-X-                       */
    B(1) | B(4) | B(5),                  /* X--XX--- stumble               */
    B(1) | B(2) | B(5),                  /* XX--X--- double                */
    B(1) | B(5) | B(8),                  /* X---X--X push into the next    */
    B(1) | B(3) | B(5) | B(7),           /* X-X-X-X- eighths, hard         */
};
static const uint8_t HAT_CLASSICS[] = {
    B(3) | B(7),                         /* --X---X- offbeat               */
    B(2) | B(3) | B(6) | B(7),           /* -XX--XX-                       */
    B(1) | B(3) | B(5) | B(7),           /* X-X-X-X- eighths               */
    B(2) | B(4) | B(6) | B(8),           /* -X-X-X-X                       */
    B(3) | B(5) | B(7),                  /* --X-X-X-                       */
    B(3) | B(4) | B(7) | B(8),           /* --XX--XX gallop                */
    B(2) | B(6) | B(8),                  /* -X---X-X broken                */
};
/* Per-step probability of a hit, step 1..8. The kick leans on the beats,
 * the hat on the offbeats. */
static const float KICK_PROB[8] = {0.95f, 0.10f, 0.30f, 0.22f, 0.80f, 0.12f, 0.35f, 0.20f};
static const float HAT_PROB[8]  = {0.10f, 0.35f, 0.90f, 0.35f, 0.12f, 0.40f, 0.90f, 0.45f};
static const int KICK_HITS_MIN = 2, KICK_HITS_MAX = 5;
static const int HAT_HITS_MIN = 2, HAT_HITS_MAX = 6;

/* ---- FILL -----------------------------------------------------------------
 * Kick: the first beat as written, then a 16th roll into the next bar with a
 * 1/32 ratchet on the last step. Hat: every 16th, accents on the offbeats,
 * ratchets on the last 16th of each beat. */
static const float KICK_FILL_VEL[8] = {1.0f, 1.0f, 1.0f, 1.0f, 0.80f, 0.85f, 0.92f, 1.0f};
static const uint8_t KICK_FILL_RATCHET = B(8);
static const float HAT_FILL_VEL[8] = {0.60f, 0.65f, 1.0f, 0.65f, 0.60f, 0.65f, 1.0f, 0.70f};
static const uint8_t HAT_FILL_RATCHET = B(4) | B(8);

/* ------------------------------------------------------------------------ */

static int popcount8(uint8_t p) {
    int n = 0;
    for (; p; p &= (uint8_t)(p - 1)) ++n;
    return n;
}

static uint8_t rotate(uint8_t p, int by) {
    by = ((by % 8) + 8) % 8;
    return (uint8_t)((p << by) | (p >> (8 - by)));
}

static int rand_int(uint32_t *rng, int n) {           /* 0 .. n-1 */
    return (int)(hkh_rand01(rng) * (float)n) % n;
}

uint8_t hkh_pattern_random(int hat, uint8_t current, uint32_t *rng) {
    const uint8_t *classics = hat ? HAT_CLASSICS : KICK_CLASSICS;
    int n_classics = hat ? (int)sizeof(HAT_CLASSICS) : (int)sizeof(KICK_CLASSICS);
    const float *prob = hat ? HAT_PROB : KICK_PROB;
    int lo = hat ? HAT_HITS_MIN : KICK_HITS_MIN, hi = hat ? HAT_HITS_MAX : KICK_HITS_MAX;
    for (int attempt = 0; attempt < 64; ++attempt) {
        uint8_t p = 0;
        if (hkh_rand01(rng) < 0.5f) {
            p = classics[rand_int(rng, n_classics)];
        } else {
            for (int s = 0; s < 8; ++s) if (hkh_rand01(rng) < prob[s]) p |= (uint8_t)(1u << s);
            int n = popcount8(p);
            if (n < lo || n > hi) continue;
        }
        if (p != current) return p;
    }
    /* Astronomically unlikely; still never answer "no change". */
    return current == classics[0] ? classics[1] : classics[0];
}

/* The kick keeps its downbeat (step 1) where it is: moving the one is not a
 * shuffle, it is a different song. */
static uint8_t kick_transform(uint8_t p, uint32_t *rng) {
    uint8_t anchor = p & B(1);
    uint8_t rest = p & (uint8_t)~B(1);
    switch (rand_int(rng, 4)) {
    case 0: {                                 /* rotate the rest within 2..8 */
        int by = 1 + rand_int(rng, 3);
        if (rand_int(rng, 2)) by = 7 - by;
        uint8_t out = 0;
        for (int s = 1; s < 8; ++s)
            if (rest & (1u << s)) out |= (uint8_t)(1u << (1 + (s - 1 + by) % 7));
        return anchor | out;
    }
    case 1: {                                 /* nudge one hit to a free neighbour */
        int n = popcount8(rest);
        if (!n) return p;
        int pick = rand_int(rng, n), s = 1;
        for (; s < 8; ++s) if ((rest & (1u << s)) && pick-- == 0) break;
        int to = s + (rand_int(rng, 2) ? 1 : -1);
        if (to < 1 || to > 7 || (p & (1u << to))) return p;
        return (uint8_t)((p & ~(1u << s)) | (1u << to));
    }
    case 2: {                                 /* swap what follows each beat */
        uint8_t first = p & (B(2) | B(3) | B(4)), second = p & (B(6) | B(7) | B(8));
        return (uint8_t)((p & (B(1) | B(5))) | (first << 4) | (second >> 4));
    }
    default: {                                /* syncopate beat 2 */
        uint8_t to = rand_int(rng, 2) ? B(4) : B(6);
        if ((p & B(5)) && !(p & to)) return (uint8_t)((p & ~B(5)) | to);
        return p;
    }
    }
}

static uint8_t hat_transform(uint8_t p, uint32_t *rng) {
    switch (rand_int(rng, 4)) {
    case 0:                                   /* rotate */
        return rotate(p, rand_int(rng, 2) ? 1 : 7);
    case 1: {                                 /* nudge one hit */
        int n = popcount8(p);
        if (!n) return p;
        int pick = rand_int(rng, n), s = 0;
        for (; s < 8; ++s) if ((p & (1u << s)) && pick-- == 0) break;
        int to = (s + (rand_int(rng, 2) ? 1 : 7)) % 8;
        if (p & (1u << to)) return p;
        return (uint8_t)((p & ~(1u << s)) | (1u << to));
    }
    case 2: {                                 /* reverse */
        uint8_t out = 0;
        for (int s = 0; s < 8; ++s) if (p & (1u << s)) out |= (uint8_t)(1u << (7 - s));
        return out;
    }
    default: {                                /* thicken or thin by one */
        int n = popcount8(p);
        if (n <= 2 || (n < 6 && rand_int(rng, 2))) {
            int s = rand_int(rng, 8);
            for (int k = 0; k < 8; ++k, s = (s + 1) % 8) if (!(p & (1u << s))) return (uint8_t)(p | (1u << s));
            return p;
        }
        int s = rand_int(rng, 8);
        for (int k = 0; k < 8; ++k, s = (s + 1) % 8) if (p & (1u << s)) return (uint8_t)(p & ~(1u << s));
        return p;
    }
    }
}

uint8_t hkh_pattern_shuffle(int hat, uint8_t current, uint32_t *rng) {
    if (current == 0 || current == 0xFF) return current;     /* nothing to move */
    if (!hat && (current & (uint8_t)~B(1)) == 0) {
        /* A lone downbeat has nothing to rearrange: give it a partner. */
        static const uint8_t PARTNER[] = {B(4), B(5), B(7)};
        return current | PARTNER[rand_int(rng, 3)];
    }
    for (int attempt = 0; attempt < 32; ++attempt) {
        uint8_t p = hat ? hat_transform(current, rng) : kick_transform(current, rng);
        if (p != current && p != 0) return p;
    }
    return hat ? rotate(current, 1) : current;
}

void hkh_pattern_fill(int hat, uint8_t current, hkh_fill *out) {
    if (hat) {
        out->steps = 0xFF;
        out->ratchet = HAT_FILL_RATCHET;
        for (int i = 0; i < 8; ++i) out->vel[i] = HAT_FILL_VEL[i];
        out->ratchet_vel = 0.55f;
    } else {
        out->steps = (uint8_t)((current & 0x0F) | 0xF0);
        out->ratchet = KICK_FILL_RATCHET;
        for (int i = 0; i < 8; ++i) out->vel[i] = KICK_FILL_VEL[i];
        out->ratchet_vel = 0.8f;
    }
}
