/* Piston - the four DIGITAL kick slots. MIT.
 *
 * Every slot always has a sound: a kick rendered into the binary at build
 * time (scripts/gen_digital_kicks.py). A user WAV replaces it when present:
 *
 *   /data/UserData/UserLibrary/Samples/Piston/kick1.wav .. kick4.wav
 *   (then <module dir>/samples/kickN.wav as a fallback)
 *
 * Files are read ONCE per instance by a worker thread that demotes itself to
 * SCHED_OTHER on cores 0-2 before touching the disk -- never on the audio
 * thread. A slot's storage belongs to the instance (static pool, nothing is
 * allocated) and is published by a single release-store of its length, after
 * which it is never written again, so the audio thread needs no lock. */
#ifndef HKH_SAMPLES_H
#define HKH_SAMPLES_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include "hkh_digital.h"

#define HKH_SAMPLE_SLOTS 4
#define HKH_USER_MAX_SAMPLES (44100 * 2)          /* 2 s per slot */
#define HKH_USER_DIR "/data/UserData/UserLibrary/Samples/Piston"

/* The drum rack (jog page / RACK surface): 24 one-shots, read from
 *   <user dir>/Rack/01.wav .. 32.wav
 * by the same worker after the kicks. A missing file is a silent pad. */
#define HKH_RACK_VOICES 32
#define HKH_RACK_MAX_SAMPLES (44100 * 3 / 2)     /* 1.5 s per pad */

typedef struct {
    int16_t data[HKH_SAMPLE_SLOTS][HKH_USER_MAX_SAMPLES];
    atomic_int len[HKH_SAMPLE_SLOTS];   /* 0 = use the built-in kick */
    int16_t rack[HKH_RACK_VOICES][HKH_RACK_MAX_SAMPLES];
    atomic_int rack_len[HKH_RACK_VOICES];   /* 0 = no sample on that pad */
    atomic_int stop;
    atomic_int done;
    int started;
    pthread_t thread;
    char user_dir[192];
    char module_dir[192];
} hkh_sample_bank;

/* Called from create_instance (the audio thread): resets the slots and
 * spawns the loader. Does no I/O itself. */
void hkh_samples_start(hkh_sample_bank *b, const char *user_dir, const char *module_dir);
/* Called from destroy_instance: asks the loader to stop and joins it. The
 * loader checks the flag between 4 KB reads, so this is bounded. */
void hkh_samples_stop(hkh_sample_bank *b);

/* RT-safe. `bank` may be NULL (built-ins only). */
hkh_sample_ref hkh_samples_get(void *bank, int index);
int hkh_samples_user_mask(hkh_sample_bank *b);
/* RT-safe. An empty pad answers len 0. */
hkh_sample_ref hkh_samples_rack_get(void *bank, int index);
unsigned hkh_samples_rack_mask(hkh_sample_bank *b);
hkh_sample_ref hkh_default_sample(int index);
const char *hkh_default_sample_name(int index);

/* The loader's parser, exposed for tests. Reads a RIFF/WAVE file (PCM 8/16/
 * 24/32-bit, float 32/64, WAVE_FORMAT_EXTENSIBLE, any channel count, 8-192
 * kHz), downmixes to mono, resamples to 44.1 kHz, truncates to `max` with a
 * fade, loudness-matches it to the analog kicks (first 150 ms RMS 0.35, peak
 * <= 0.95) with its first big excursion positive. Returns the length, or 0. */
int hkh_wav_load(const char *path, int16_t *dst, int max, atomic_int *stop);
/* The rack's variant: no loudness match and no polarity flip -- a pad keeps
 * its own level and is only scaled DOWN if it peaks above 0.95. */
int hkh_wav_load_natural(const char *path, int16_t *dst, int max, atomic_int *stop);

#endif
