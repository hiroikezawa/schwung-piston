/* Piston - the sound engines the sequencer drives. MIT.
 * The whole signal path after the sequencer:
 *
 *   kick = MIX(analog, digital) -> DRIVE -> COMP -> VOLUME --+--> out
 *                                                            +--> RUMBLE --> out
 *   hat  = model              -> DRIVE -> COMP -> VOLUME ---+--> out
 *   kick * K REVERB + hat * H REVERB -> shared REVERB ------> out
 *   out -> DC block -> -3 dBFS limiter
 */
#ifndef HKH_VOICES_H
#define HKH_VOICES_H

#include <stdint.h>
#include "hkh_kick.h"
#include "hkh_digital.h"
#include "hkh_hat.h"
#include "hkh_fx.h"

typedef struct {
    hkh_akick akick;
    hkh_digital dkick;
    float mix;                  /* smoothed A/D MIX, 0 analog .. 1 digital */
    float gain_a, gain_d;       /* equal-power gains at the end of last block */
    float gain_a_step, gain_d_step;
    hkh_hat hat;
    hkh_drive kdrive, hdrive;
    hkh_comp kcomp, hcomp;
    hkh_rumble rumble;
    hkh_reverb reverb;
    hkh_limiter limiter;
    /* per-sample ramps for the post-effect gains */
    float kvol, kvol_step, hvol, hvol_step;
    float ksend, ksend_step, hsend, hsend_step;
    float rumble_meter;         /* peak of the RUMBLE output, ~0.5 s fall */
} hkh_voices;

#endif
