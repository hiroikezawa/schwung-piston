/* Piston - engine: two sequencers, the kick and hat voices and the
 * shared effect busses. Host-independent (no plugin API in here) so the whole
 * signal path is testable natively. MIT. */
#ifndef HKH_ENGINE_H
#define HKH_ENGINE_H

#include <stdint.h>
#include "hkh_seq.h"
#include "hkh_voices.h"

/* The fourteen user-facing continuous parameters, all 0..1. Their ORDER is
 * the knob order: 0-6 are the KICK knobs 1-7, 7-13 the HAT knobs 1-7. Knob 8
 * is reserved in both modes and deliberately has no parameter. */
enum {
    P_K_VOL, P_K_MIX, P_K_DECAY, P_K_DRIVE, P_K_COMP, P_K_RUMBLE, P_K_VERB,
    P_H_VOL, P_H_COLOR, P_H_DECAY, P_H_DRIVE, P_H_COMP, P_H_FILTER, P_H_VERB,
    P_COUNT
};

enum { KICK_808, KICK_909, KICK_IND, KICK_MODEL_COUNT };
enum { HAT_808, HAT_909, HAT_METAL, HAT_IND, HAT_MODEL_COUNT };
#define HKH_DIGITAL_COUNT 4

enum { MODE_KICK = 0, MODE_HAT = 1 };

#define HKH_KICK_DEFAULT_PATTERN 0x1111u /* steps 1, 5 (9, 13) */
#define HKH_HAT_DEFAULT_PATTERN  0x4444u /* steps 3, 7 (11, 15): the offbeat */
#define HKH_HAT_CLOSED_DECAY     0.0f    /* where DECAY starts and RESET returns */
#define HKH_MOTION_NONE          (-1.0f)

extern const char *const hkh_param_keys[P_COUNT];
extern const float hkh_param_defaults[P_COUNT];

typedef hkh_sample_ref (*hkh_sample_source)(void *ctx, int index);

typedef struct {
    float param[P_COUNT];          /* targets as set */
    int k_model, k_sample, h_model, mode;
    uint16_t k_pattern, h_pattern; /* bit i = step i+1; only `length` used */
    int k_mute, h_mute;
    int k_fill, h_fill;            /* momentary, never saved */
    float motion[HKH_STEPS];       /* per-step hat DECAY, or HKH_MOTION_NONE */
    int motion_rec;                /* DECAY knob is being touched in HAT mode */

    /* Heartbeats: a held FILL or a touched knob must be restated by the UI.
     * If the UI goes away mid-hold (slot switch, crash) the DSP lets go by
     * itself instead of leaving a fill or a recording latched forever. */
    int k_fill_age, h_fill_age, rec_age;   /* samples since last restatement */

    hkh_seq seq;
    uint32_t rng;                  /* pattern generator */
    int pending_k, pending_h;      /* auditions / MIDI note triggers */
    float pending_h_decay;         /* <0: use the step/base decay */
    float pending_k_vel, pending_h_vel;
    int k_ratchet_in, h_ratchet_in;      /* samples until a ratchet hit, -1 none */
    float k_ratchet_vel, h_ratchet_vel;
    float h_ratchet_decay;

    hkh_sample_source samples;
    void *samples_ctx;

    unsigned fault_count;          /* non-finite output caught and zeroed */
    unsigned k_hits, h_hits;       /* hits actually fired (diagnostics, tests) */
    float last_h_decay;            /* DECAY the last hat was fired with */
    hkh_voices voices;             /* the sound engines and effect busses */
} hkh_engine;

/* Engines live in caller-owned memory (the plugin keeps a static pool);
 * nothing is allocated, here or anywhere on the audio thread. */
void hkh_engine_init(hkh_engine *e, uint32_t seed);
void hkh_engine_set_sample_source(hkh_engine *e, hkh_sample_source fn, void *ctx);

/* Continuous parameters. */
void hkh_engine_set_param(hkh_engine *e, int index, float value);

/* Discrete selections. `audition` plays the new sound once if the transport
 * is stopped, so a model can be chosen by ear. */
void hkh_engine_set_kick_model(hkh_engine *e, int model, int audition);
void hkh_engine_set_kick_sample(hkh_engine *e, int index, int audition);
void hkh_engine_set_hat_model(hkh_engine *e, int model, int audition);

/* Pad commands. */
void hkh_engine_toggle_step(hkh_engine *e, int hat, int step);
void hkh_engine_set_mute(hkh_engine *e, int hat, int on);
void hkh_engine_reset(hkh_engine *e, int hat);
void hkh_engine_shuffle(hkh_engine *e, int hat);
void hkh_engine_random(hkh_engine *e, int hat);
void hkh_engine_set_fill(hkh_engine *e, int hat, int on);
void hkh_engine_offbeat(hkh_engine *e);
void hkh_engine_set_motion_rec(hkh_engine *e, int on);
void hkh_engine_all_sound_off(hkh_engine *e);   /* MIDI CC 120/123 */

/* Immediate hits (MIDI notes in, auditions). decay < 0 = current hat decay. */
void hkh_engine_trigger_kick(hkh_engine *e, float velocity);
void hkh_engine_trigger_hat(hkh_engine *e, float velocity, float decay);

/* The pattern that is actually sounding (FILL applied). */
uint16_t hkh_engine_effective_pattern(const hkh_engine *e, int hat);

/* 8 or 16 steps, shared by both voices. Going to 16 repeats the 8 you had
 * (so the groove carries on); going back to 8 keeps the first half. */
void hkh_engine_set_length(hkh_engine *e, int steps);

/* beat < 0: transport stopped. Output is float, pre-limited to +-0.71. */
void hkh_engine_render(hkh_engine *e, float *out_l, float *out_r, int frames,
                       double beat, float bpm);

#endif
