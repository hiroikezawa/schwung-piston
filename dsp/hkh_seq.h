/* Piston - 8-step, 1/16-note clock follower. MIT.
 *
 * The sequencer owns no clock of its own. Every block it is handed the host
 * transport position in beats (host->get_beat_position(), where 0.0 is the
 * downbeat of Move's Play) and the tempo, and answers WHERE in the block each
 * 1/16 boundary falls. That keeps it drift-free by construction and makes the
 * timing unit-testable without a host. */
#ifndef HKH_SEQ_H
#define HKH_SEQ_H

#include <stdint.h>

#define HKH_STEPS 16            /* the most a pattern can hold */
#define HKH_SEQ_MAX_EVENTS 4

typedef struct {
    int offset;        /* sample offset inside the block, 0..frames-1 */
    int64_t abs_step;  /* 1/16 index since transport start */
    int step;          /* abs_step mod length */
} hkh_step_event;

typedef struct {
    int running;
    int64_t last_abs;  /* last 1/16 boundary that fired */
    int cur_step;      /* 0..length-1 while running, -1 when stopped */
    int length;        /* 8 or 16 steps */
    double step_samples; /* length of one 1/16 at the current tempo */
} hkh_seq;

void hkh_seq_init(hkh_seq *s);

/* beat < 0 means the transport is stopped. Returns the number of events
 * written (at most HKH_SEQ_MAX_EVENTS), in ascending offset order.
 *
 * Rules, each pinned by tests/test_engine.c:
 *  - a start (or a backwards jump, i.e. a new Play) fires the current step at
 *    offset 0, so the downbeat is never lost to a block boundary;
 *  - a boundary is fired exactly once, even if host jitter reports it again;
 *  - a forward gap (the slot was not rendered for a while) fires only the
 *    current step, late, rather than replaying every skipped step. */
int hkh_seq_advance(hkh_seq *s, double beat, float bpm, int frames,
                    hkh_step_event *out);

#endif
