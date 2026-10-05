/* Piston - render audition WAVs with the real engine, so the sound
 * can be judged on a computer before anything is installed on the Move.
 *
 *   scripts/render_demo.sh  ->  WAV files in build/demo (44.1 kHz, 16-bit stereo)
 */
#include "../dsp/hkh_engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK 128
#define MAX_SECONDS 20
static hkh_engine E;
static int16_t PCM[44100 * MAX_SECONDS * 2];
static long pcm_frames;
static const char *OUT_DIR;

static void put16(FILE *f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, long v) { put16(f, (int)(v & 0xFFFF)); put16(f, (int)((v >> 16) & 0xFFFF)); }

static void save(const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.wav", OUT_DIR, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    long bytes = pcm_frames * 4;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 44100); put32(f, 44100 * 4);
    put16(f, 4); put16(f, 16); fwrite("data", 1, 4, f); put32(f, bytes);
    fwrite(PCM, 4, (size_t)pcm_frames, f);
    fclose(f);
    printf("  %s (%.1f s)\n", path, pcm_frames / 44100.0);
    pcm_frames = 0;
}

typedef struct { double beat; float bpm; int running; } transport;

static void render(transport *t, int blocks) {
    float l[BLOCK], r[BLOCK];
    for (int b = 0; b < blocks && pcm_frames + BLOCK <= 44100L * MAX_SECONDS; ++b) {
        hkh_engine_render(&E, l, r, BLOCK, t->running ? t->beat : -1.0, t->bpm);
        for (int i = 0; i < BLOCK; ++i) {
            PCM[pcm_frames * 2] = (int16_t)lrintf(l[i] * 32767.0f);
            PCM[pcm_frames * 2 + 1] = (int16_t)lrintf(r[i] * 32767.0f);
            ++pcm_frames;
        }
        if (t->running) t->beat += BLOCK * t->bpm / 60.0 / 44100.0;
        E.k_fill_age = E.h_fill_age = E.rec_age = 0;     /* as a held pad would */
    }
}

static int secs(double s) { return (int)(s * 44100 / BLOCK); }
static int bars(double n, float bpm) { return (int)(n * 4 * 60.0 / bpm * 44100 / BLOCK); }

static void fresh(void) {
    hkh_engine_init(&E, 1234);
    E.h_pattern = 0; E.k_pattern = 0;
}

int main(int argc, char **argv) {
    OUT_DIR = argc > 1 ? argv[1] : ".";
    transport stopped = {0, 130, 0};

    /* Single hits: each analog kick at DECAY 0.2 / 0.5 / 0.9. */
    const char *kname[] = {"kick_808", "kick_909", "kick_industrial"};
    for (int m = 0; m < 3; ++m) {
        fresh();
        E.k_model = m;
        hkh_engine_set_param(&E, P_K_MIX, 0.0f);
        hkh_engine_set_param(&E, P_K_DRIVE, 0.0f);
        hkh_engine_set_param(&E, P_K_COMP, 0.0f);
        for (int d = 0; d < 3; ++d) {
            hkh_engine_set_param(&E, P_K_DECAY, d == 0 ? 0.2f : d == 1 ? 0.5f : 0.9f);
            render(&stopped, 4);
            hkh_engine_trigger_kick(&E, 1.0f);
            render(&stopped, secs(d == 2 ? 2.5 : 1.0));
        }
        save(kname[m]);
    }
    /* The four built-in digital kicks. */
    fresh();
    hkh_engine_set_param(&E, P_K_MIX, 1.0f);
    hkh_engine_set_param(&E, P_K_DRIVE, 0.0f);
    hkh_engine_set_param(&E, P_K_COMP, 0.0f);
    E.voices.mix = 1.0f; E.voices.gain_a = 0.0f; E.voices.gain_d = 1.0f;
    for (int s = 0; s < 4; ++s) {
        E.k_sample = s;
        hkh_engine_trigger_kick(&E, 1.0f);
        render(&stopped, secs(1.4));
    }
    save("kick_digital_punch_sub_crush_hard");
    /* Hats: each model, DECAY swept closed -> open in five hits. */
    const char *hname[] = {"hat_808", "hat_909", "hat_metallic", "hat_industrial"};
    for (int m = 0; m < 4; ++m) {
        fresh();
        E.h_model = m;
        hkh_engine_set_param(&E, P_H_DRIVE, 0.0f);
        hkh_engine_set_param(&E, P_H_COMP, 0.0f);
        for (int d = 0; d < 5; ++d) {
            hkh_engine_trigger_hat(&E, 1.0f, d * 0.25f);
            render(&stopped, secs(d < 3 ? 0.35 : 1.3));
        }
        save(hname[m]);
    }

    /* Grooves at 130 BPM. */
    transport run = {0, 130, 1};
    hkh_engine_init(&E, 99);                      /* the defaults, as installed */
    render(&run, bars(4, 130));
    save("groove_default");

    /* Industrial: IND kick layered with HARD, driven, compressed, rumble. */
    hkh_engine_init(&E, 99);
    E.k_model = KICK_IND; E.k_sample = 3; E.h_model = HAT_IND;
    hkh_engine_set_param(&E, P_K_MIX, 0.35f);
    hkh_engine_set_param(&E, P_K_DECAY, 0.45f);
    hkh_engine_set_param(&E, P_K_DRIVE, 0.55f);
    hkh_engine_set_param(&E, P_K_COMP, 0.5f);
    hkh_engine_set_param(&E, P_K_RUMBLE, 0.75f);
    hkh_engine_set_param(&E, P_K_VERB, 0.25f);
    hkh_engine_set_param(&E, P_H_DRIVE, 0.4f);
    hkh_engine_set_param(&E, P_H_COLOR, 0.65f);
    hkh_engine_set_param(&E, P_H_VERB, 0.3f);
    E.h_pattern = 0x66;                           /* -XX--XX- */
    render(&run, bars(3, 130));
    hkh_engine_set_fill(&E, 0, 1);                /* bar 4: both FILLs held */
    hkh_engine_set_fill(&E, 1, 1);
    render(&run, bars(1, 130));
    hkh_engine_set_fill(&E, 0, 0);
    hkh_engine_set_fill(&E, 1, 0);
    render(&run, bars(2, 130));
    save("groove_industrial_rumble_fill");

    /* DECAY motion: the same offbeat hat, "chick" on 3 and "shhh" on 7. */
    hkh_engine_init(&E, 7);
    E.h_model = HAT_808;
    render(&run, bars(2, 130));                   /* plain closed offbeats */
    E.motion[2] = 0.18f;
    E.motion[6] = 0.80f;
    render(&run, bars(4, 130));
    save("groove_decay_motion");

    /* RUMBLE sweep: 0 -> 1 over eight bars on a 909. */
    hkh_engine_init(&E, 5);
    for (int b = 0; b < 8; ++b) {
        hkh_engine_set_param(&E, P_K_RUMBLE, b / 7.0f);
        render(&run, bars(1, 130));
    }
    save("rumble_sweep_909");
    return 0;
}
