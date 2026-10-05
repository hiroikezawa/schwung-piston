/* Piston - 8-step clock follower. MIT. */
#include "hkh_seq.h"
#include "hkh_util.h"
#include <math.h>

void hkh_seq_init(hkh_seq *s) {
    s->running = 0;
    s->last_abs = -1;
    s->cur_step = -1;
    s->step_samples = HKH_SR * 60.0 / 120.0 / 4.0;
    s->length = 8;
}

static int step_of(const hkh_seq *s, int64_t abs_step) {
    int len = s->length == 16 ? 16 : 8;
    int64_t m = abs_step % len;
    return (int)(m < 0 ? m + len : m);
}

int hkh_seq_advance(hkh_seq *s, double beat, float bpm, int frames,
                    hkh_step_event *out) {
    if (!(beat >= 0.0) || frames <= 0) {          /* stopped (or NaN) */
        s->running = 0;
        s->cur_step = -1;
        return 0;
    }
    if (!(bpm >= 20.0f && bpm <= 999.0f)) bpm = 120.0f;
    double spb = (double)HKH_SR * 60.0 / (double)bpm;    /* samples per beat */
    s->step_samples = spb / 4.0;

    int n = 0;
    int64_t cur = (int64_t)floor(beat * 4.0 + 1e-9);
    if (!s->running || cur < s->last_abs || cur > s->last_abs + 1) {
        /* New Play, a jump backwards, or a gap: sound where we ARE. */
        s->running = 1;
        s->last_abs = cur;
        s->cur_step = step_of(s, cur);
        out[n].offset = 0;
        out[n].abs_step = cur;
        out[n].step = s->cur_step;
        n++;
    }
    /* Every later boundary that lands inside this block. */
    while (n < HKH_SEQ_MAX_EVENTS) {
        int64_t next = s->last_abs + 1;
        double off = ((double)next / 4.0 - beat) * spb;
        if (off >= (double)frames) break;
        int o = off <= 0.0 ? 0 : (int)off;
        if (o >= frames) break;
        s->last_abs = next;
        s->cur_step = step_of(s, next);
        out[n].offset = o;
        out[n].abs_step = next;
        out[n].step = s->cur_step;
        n++;
    }
    return n;
}
