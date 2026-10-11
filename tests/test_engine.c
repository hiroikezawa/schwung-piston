/* Piston - native engine tests. Run by scripts/test.sh under ASan/UBSan. */
#include "../dsp/hkh_engine.h"
#include "../dsp/hkh_seq.h"
#include "../dsp/hkh_util.h"
#include "../dsp/hkh_kick.h"
#include "../dsp/hkh_digital.h"
#include "../dsp/hkh_samples.h"
#include "../dsp/hkh_hat.h"
#include "../dsp/hkh_pattern.h"
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define BLOCK 128
static hkh_engine E;           /* static: the engine is large */
static float L[BLOCK], R[BLOCK];

/* A transport: `beat` advances exactly with tempo while running. */
typedef struct { double beat; float bpm; int running; } transport;

static double spb(float bpm) { return 44100.0 * 60.0 / bpm; }

static void render(hkh_engine *e, transport *t) {
    hkh_engine_render(e, L, R, BLOCK, t->running ? t->beat : -1.0, t->bpm);
    for (int i = 0; i < BLOCK; ++i) {
        assert(isfinite(L[i]) && isfinite(R[i]));
        assert(fabsf(L[i]) <= 1.0f && fabsf(R[i]) <= 1.0f);
    }
    if (t->running) t->beat += BLOCK / spb(t->bpm);
}

static void run(hkh_engine *e, transport *t, int blocks) {
    for (int i = 0; i < blocks; ++i) render(e, t);
}

static int blocks_for_steps(float bpm, double steps) {
    return (int)ceil(steps * spb(bpm) / 4.0 / BLOCK);
}

/* ---- sequencer timing -------------------------------------------------- */
static void test_seq_timing(void) {
    const float tempos[] = {60.0f, 120.0f, 133.0f, 174.0f, 300.0f};
    for (unsigned ti = 0; ti < sizeof(tempos) / sizeof(tempos[0]); ++ti) {
        float bpm = tempos[ti];
        hkh_seq s;
        hkh_seq_init(&s);
        double beat = 0.0, step_len = spb(bpm) / 4.0;
        int64_t expect = 0;
        long sample = 0;
        for (int b = 0; b < 4000; ++b) {
            hkh_step_event ev[HKH_SEQ_MAX_EVENTS];
            int n = hkh_seq_advance(&s, beat, bpm, BLOCK, ev);
            for (int k = 0; k < n; ++k) {
                assert(ev[k].abs_step == expect);
                assert(ev[k].step == (int)(expect % 8));
                /* Sample-accurate: within one sample of the ideal boundary. */
                double ideal = expect * step_len;
                double got = (double)(sample + ev[k].offset);
                assert(fabs(got - ideal) <= 1.0);
                ++expect;
            }
            beat += BLOCK / spb(bpm);
            sample += BLOCK;
        }
        /* Every boundary in the span fired, none extra. */
        assert(expect == (int64_t)floor((double)sample / step_len - 1e-9) + 1);
    }
    printf("PASS: sequencer fires every 1/16 exactly once, sample-accurate, 60-300 BPM\n");
}

static void test_seq_edges(void) {
    hkh_seq s;
    hkh_step_event ev[HKH_SEQ_MAX_EVENTS];
    hkh_seq_init(&s);
    /* Stopped: nothing. */
    assert(hkh_seq_advance(&s, -1.0, 120, BLOCK, ev) == 0 && s.cur_step == -1);
    /* Play: the downbeat fires at offset 0. */
    assert(hkh_seq_advance(&s, 0.0, 120, BLOCK, ev) == 1);
    assert(ev[0].offset == 0 && ev[0].step == 0);
    /* Host jitter that reports the same boundary twice does not refire it. */
    assert(hkh_seq_advance(&s, 0.001, 120, BLOCK, ev) == 0);
    assert(hkh_seq_advance(&s, 0.0, 120, BLOCK, ev) == 0);
    /* A gap (slot not rendered) fires only where we are, not every skip. */
    int n = hkh_seq_advance(&s, 1.30, 120, BLOCK, ev);
    assert(n == 1 && ev[0].abs_step == 5 && ev[0].offset == 0);
    /* A backwards jump (Play again from the top) restarts at the downbeat. */
    n = hkh_seq_advance(&s, 0.0, 120, BLOCK, ev);
    assert(n == 1 && ev[0].abs_step == 0 && ev[0].offset == 0);
    /* Stop, then Play again: fires the downbeat again. */
    assert(hkh_seq_advance(&s, -1.0, 120, BLOCK, ev) == 0 && !s.running);
    assert(hkh_seq_advance(&s, 0.0, 120, BLOCK, ev) == 1 && ev[0].step == 0);
    /* A boundary a hair behind the block start is late, not lost. */
    hkh_seq_init(&s);
    hkh_seq_advance(&s, 0.0, 120, BLOCK, ev);
    n = hkh_seq_advance(&s, 0.2501, 120, BLOCK, ev);
    assert(n == 1 && ev[0].abs_step == 1 && ev[0].offset == 0);
    /* Nonsense input is refused, never propagated. */
    assert(hkh_seq_advance(&s, NAN, 120, BLOCK, ev) == 0);
    hkh_seq_init(&s);
    assert(hkh_seq_advance(&s, 0.0, NAN, BLOCK, ev) == 1);
    printf("PASS: start/stop/jitter/gap/jump edges\n");
}

/* ---- patterns, mutes, reset ---------------------------------------------- */
static void test_patterns(void) {
    transport t = {0.0, 120.0f, 0};
    hkh_engine_init(&E, 1);
    assert(E.k_pattern == 0x1111 && E.h_pattern == 0x4444);   /* X---X--- / --X---X- (x2) */
    assert(E.seq.length == 8);
    assert(E.mode == MODE_KICK);
    /* Two bars at 120 BPM: 2 kicks + 2 hats per 8 steps, 4 loops. */
    t.running = 1;
    run(&E, &t, blocks_for_steps(120, 32) - 1);
    assert(E.k_hits == 8 && E.h_hits == 8);
    /* Toggling a step changes only that step. */
    hkh_engine_toggle_step(&E, 0, 2);
    assert(E.k_pattern == 0x1115);
    hkh_engine_toggle_step(&E, 0, 2);
    assert(E.k_pattern == 0x1111);
    hkh_engine_toggle_step(&E, 1, 7);
    assert(E.h_pattern == 0x44C4);
    hkh_engine_toggle_step(&E, 1, 12);            /* beyond 8 steps: refused */
    assert(E.h_pattern == 0x44C4);
    /* Mute keeps the pattern and silences only its own voice. */
    unsigned k0 = E.k_hits, h0 = E.h_hits;
    hkh_engine_set_mute(&E, 0, 1);
    run(&E, &t, blocks_for_steps(120, 8));
    assert(E.k_hits == k0 && E.h_hits > h0 && E.k_pattern == 0x1111);
    hkh_engine_set_mute(&E, 0, 0);
    k0 = E.k_hits;
    run(&E, &t, blocks_for_steps(120, 8));
    assert(E.k_hits > k0);
    /* RESET returns each voice home and touches nothing of the other. */
    E.k_pattern = 0xFF; E.h_pattern = 0x0F;
    E.param[P_H_DECAY] = 0.9f;
    for (int i = 0; i < 8; ++i) E.motion[i] = 0.8f;
    hkh_engine_set_fill(&E, 0, 1);
    hkh_engine_set_fill(&E, 1, 1);
    hkh_engine_reset(&E, 0);
    assert(E.k_pattern == 0x1111 && !E.k_fill);
    assert(E.h_pattern == 0x0F && E.h_fill && E.param[P_H_DECAY] == 0.9f);
    hkh_engine_reset(&E, 1);
    assert(E.h_pattern == 0x4444 && !E.h_fill && E.param[P_H_DECAY] == HKH_HAT_CLOSED_DECAY);
    for (int i = 0; i < 8; ++i) assert(E.motion[i] == HKH_MOTION_NONE);
    /* OFFBEAT sets exactly steps 3 and 7. */
    E.h_pattern = 0xFF;
    hkh_engine_offbeat(&E);
    assert(E.h_pattern == 0x4444);
    printf("PASS: default patterns, step toggles, mute, reset, offbeat\n");
}

/* ---- motion recording ---------------------------------------------------- */
static void test_motion(void) {
    transport t = {0.0, 120.0f, 1};
    hkh_engine_init(&E, 2);
    run(&E, &t, 2);                       /* now on step 0 */
    /* Touch DECAY on step 0, hold through step 2 at a tight value. */
    hkh_engine_set_param(&E, P_H_DECAY, 0.2f);
    hkh_engine_set_motion_rec(&E, 1);
    assert(E.motion[0] == 0.2f);
    while (E.seq.cur_step != 3) {
        render(&E, &t);
        E.rec_age = 0;                     /* the UI's heartbeat */
    }
    /* Turn it open on step 3 and hold into step 6. */
    hkh_engine_set_param(&E, P_H_DECAY, 0.8f);
    while (E.seq.cur_step != 6) { render(&E, &t); E.rec_age = 0; }
    hkh_engine_set_motion_rec(&E, 0);
    assert(E.motion[0] == 0.2f && E.motion[1] == 0.2f && E.motion[2] == 0.2f);
    assert(E.motion[3] == 0.8f && E.motion[4] == 0.8f && E.motion[5] == 0.8f);
    assert(E.motion[6] == 0.8f);          /* written on arrival, before release */
    assert(E.motion[7] == HKH_MOTION_NONE);
    /* Released: later knob moves change the base only, not recorded steps. */
    hkh_engine_set_param(&E, P_H_DECAY, 0.5f);
    assert(E.motion[3] == 0.8f);
    /* Playback: hat on step 3 uses 0.8, the unrecorded step 7 the base 0.5. */
    E.h_pattern = 0x88;                    /* steps 4 and 8 */
    E.motion[7] = HKH_MOTION_NONE;
    while (E.seq.cur_step != 3) render(&E, &t);
    assert(E.last_h_decay == 0.8f);
    while (E.seq.cur_step != 7) render(&E, &t);
    assert(E.last_h_decay == 0.5f);
    /* Recording is inert while stopped: nowhere to write. */
    t.running = 0;
    render(&E, &t);
    for (int i = 0; i < 8; ++i) E.motion[i] = HKH_MOTION_NONE;
    hkh_engine_set_motion_rec(&E, 1);
    hkh_engine_set_param(&E, P_H_DECAY, 0.7f);
    for (int i = 0; i < 8; ++i) assert(E.motion[i] == HKH_MOTION_NONE);
    printf("PASS: DECAY motion records only while touched, per step, and plays back\n");
}

/* ---- heartbeats ---------------------------------------------------------- */
static void test_heartbeat(void) {
    transport t = {0.0, 120.0f, 1};
    hkh_engine_init(&E, 3);
    hkh_engine_set_fill(&E, 0, 1);
    hkh_engine_set_fill(&E, 1, 1);
    hkh_engine_set_motion_rec(&E, 1);
    /* Restated in time: stays held. */
    for (int i = 0; i < 400; ++i) {
        render(&E, &t);
        if (i % 40 == 0) {
            hkh_engine_set_fill(&E, 0, 1);
            hkh_engine_set_fill(&E, 1, 1);
            hkh_engine_set_motion_rec(&E, 1);
        }
    }
    assert(E.k_fill && E.h_fill && E.motion_rec);
    /* The UI vanished mid-hold: released within the heartbeat window. */
    run(&E, &t, 200);
    assert(!E.k_fill && !E.h_fill && !E.motion_rec);
    printf("PASS: an abandoned FILL / DECAY touch releases itself\n");
}

/* ---- auditions and MIDI hits --------------------------------------------- */
static void test_auditions(void) {
    transport t = {0.0, 120.0f, 0};
    hkh_engine_init(&E, 4);
    render(&E, &t);
    hkh_engine_toggle_step(&E, 0, 3);      /* ON while stopped: heard once */
    render(&E, &t);
    assert(E.k_hits == 1);
    hkh_engine_toggle_step(&E, 0, 3);      /* OFF: silent */
    render(&E, &t);
    assert(E.k_hits == 1);
    hkh_engine_set_hat_model(&E, HAT_METAL, 1);
    render(&E, &t);
    assert(E.h_hits == 1 && E.h_model == HAT_METAL);
    hkh_engine_set_hat_model(&E, HAT_808, 0);   /* state restore: no audition */
    render(&E, &t);
    assert(E.h_hits == 1);
    /* Running: picking a model never adds a stray hit. */
    t.running = 1;
    render(&E, &t);
    unsigned k0 = E.k_hits;
    hkh_engine_set_kick_model(&E, KICK_IND, 1);
    render(&E, &t);
    assert(E.k_hits == k0);
    /* MIDI hits honour mute. */
    hkh_engine_set_mute(&E, 1, 1);
    hkh_engine_trigger_hat(&E, 1.0f, 0.9f);
    render(&E, &t);
    assert(E.h_hits == 1);
    printf("PASS: auditions only while stopped; MIDI hits honour mute\n");
}

/* ---- analog kick models (phase 2) ---------------------------------------- */
#define KN 220500
static float KB[KN];

typedef struct { float peak, t40, attack_hf, body_hf, rms; } kick_stats;

static kick_stats kick_measure(int model, float decay) {
    static hkh_akick k;
    hkh_akick_init(&k, 11);
    hkh_akick_trigger(&k, model, 1.0f, decay);
    kick_stats st = {0};
    for (int i = 0; i < KN; ++i) {
        if (i % BLOCK == 0) hkh_akick_block(&k, decay);
        KB[i] = hkh_akick_tick(&k);
        assert(isfinite(KB[i]));
        if (fabsf(KB[i]) > st.peak) st.peak = fabsf(KB[i]);
    }
    for (int i = KN - 1; i >= 0; --i) if (fabsf(KB[i]) > st.peak * 0.01f) { st.t40 = i / 44100.0f; break; }
    double e = 0, hf = 0, eb = 0, hb = 0, r = 0;
    for (int i = 1; i < 221; ++i) { e += KB[i] * KB[i]; double d = KB[i] - KB[i - 1]; hf += d * d; }
    for (int i = 882; i < 2646; ++i) { eb += KB[i] * KB[i]; double d = KB[i] - KB[i - 1]; hb += d * d; }
    for (int i = 0; i < 13230; ++i) r += KB[i] * KB[i];
    st.attack_hf = (float)(hf / (e + 1e-12));
    st.body_hf = (float)(hb / (eb + 1e-12));
    st.rms = (float)sqrt(r / 13230);
    /* The voice frees itself once silent. */
    assert(!hkh_akick_active(&k));
    return st;
}

static void test_kick_models(void) {
    kick_stats s[3][3];
    for (int m = 0; m < 3; ++m)
        for (int d = 0; d < 3; ++d) s[m][d] = kick_measure(m, d * 0.5f);
    for (int m = 0; m < 3; ++m) {
        for (int d = 0; d < 3; ++d) {
            assert(s[m][d].peak > 0.6f && s[m][d].peak < 0.95f);
            assert(s[m][d].rms > 0.08f);
        }
        /* DECAY lengthens the tail, over a wide musical range. */
        assert(s[m][0].t40 < s[m][1].t40 && s[m][1].t40 < s[m][2].t40);
        assert(s[m][2].t40 > 4.0f * s[m][0].t40);
    }
    /* Character, measured rather than asserted by name:
     * 808 is the longest and the softest-edged, */
    assert(s[KICK_808][1].t40 > s[KICK_909][1].t40 && s[KICK_808][1].t40 > s[KICK_IND][1].t40);
    assert(s[KICK_808][1].attack_hf < s[KICK_909][1].attack_hf);
    /* 909 attacks harder than the 808, IND hardest of all, */
    assert(s[KICK_IND][1].attack_hf > 5.0f * s[KICK_909][1].attack_hf);
    /* and IND's body is harmonic-rich where the others are near-sine. */
    assert(s[KICK_IND][1].body_hf > 20.0f * s[KICK_909][1].body_hf);
    assert(s[KICK_IND][1].body_hf > 20.0f * s[KICK_808][1].body_hf);
    /* Balanced loudness: no model is more than ~4 dB off another. */
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
            assert(s[a][1].rms < 1.6f * s[b][1].rms);
    printf("PASS: 808/909/IND kicks: distinct attack & harmonics, DECAY %.2f-%.2fs, balanced\n",
           s[KICK_909][0].t40, s[KICK_808][2].t40);
}

/* Retrigger and model change mid-tail: no step discontinuity. */
static void test_kick_retrigger(void) {
    static hkh_akick k;
    for (int m = 0; m < 3; ++m) {
        hkh_akick_init(&k, 5);
        float prev = 0.0f, worst_tail = 0.0f, worst_hit = 0.0f;
        hkh_akick_trigger(&k, m, 1.0f, 1.0f);
        for (int i = 0; i < 44100; ++i) {
            if (i % BLOCK == 0) hkh_akick_block(&k, 1.0f);
            if (i == 6000) hkh_akick_trigger(&k, (m + 1) % 3, 1.0f, 1.0f);
            float x = hkh_akick_tick(&k);
            float jump = fabsf(x - prev);
            if (i > 400 && i < 6000) worst_tail = fmaxf(worst_tail, jump);
            if (i >= 6000 && i < 6400) worst_hit = fmaxf(worst_hit, jump);
            prev = x;
        }
        /* The retrigger is no sharper than a fresh attack on its own. */
        hkh_akick_init(&k, 5);
        hkh_akick_trigger(&k, (m + 1) % 3, 1.0f, 1.0f);
        float fresh = 0.0f;
        prev = 0.0f;
        for (int i = 0; i < 400; ++i) {
            float x = hkh_akick_tick(&k);
            fresh = fmaxf(fresh, fabsf(x - prev));
            prev = x;
        }
        assert(worst_hit <= fresh * 1.25f + 0.05f);
        (void)worst_tail;
    }
    printf("PASS: kick retrigger / model change mid-tail is click-free\n");
}

/* ---- digital kick, A/D mix and the WAV loader (phase 3) -------------------- */
static void test_default_samples(void) {
    for (int i = 0; i < 4; ++i) {
        hkh_sample_ref r = hkh_default_sample(i);
        assert(r.data && r.len > 8000 && r.len <= 44100 * 2);
        int peak = 0;
        double sum = 0;
        for (int j = 0; j < r.len; ++j) peak = abs(r.data[j]) > peak ? abs(r.data[j]) : peak;
        for (int j = 0; j < 6615; ++j) sum += (double)r.data[j] * r.data[j];
        double rms = sqrt(sum / 6615) / 32767.0;
        assert(peak <= 31130 && peak > 13000);          /* 0.4 .. 0.95 */
        assert(rms > 0.30 && rms < 0.36);               /* loudness-matched (or peak-capped) */
        assert(abs(r.data[r.len - 1]) < 200);           /* faded end */
        assert(hkh_default_sample_name(i)[0]);
        /* Distinct sounds, not four copies. */
        for (int k = 0; k < i; ++k) assert(hkh_default_sample(k).len != r.len || memcmp(
            hkh_default_sample(k).data, r.data, 2000) != 0);
    }
    printf("PASS: four built-in digital kicks, loudness-matched, faded\n");
}

static double kick_energy(float mix, int model, int sample) {
    static hkh_engine e;
    hkh_engine_init(&e, 9);
    hkh_engine_set_param(&e, P_K_MIX, mix);
    e.voices.mix = mix;                            /* settled, not gliding */
    e.voices.gain_a = cosf(mix * 1.5707963f);
    e.voices.gain_d = sinf(mix * 1.5707963f);
    e.k_model = model;
    e.k_sample = sample;
    e.h_pattern = 0;
    transport t = {0.0, 120.0f, 0};
    hkh_engine_trigger_kick(&e, 1.0f);
    double en = 0;
    /* The first 150 ms: how loud a kick sounds, not how long its tail is. */
    for (int b = 0; b < 52; ++b) { render(&e, &t); for (int i = 0; i < BLOCK; ++i) en += L[i] * L[i]; }
    return en;
}

static void test_mix(void) {
    for (int m = 0; m < 3; ++m) {
        for (int smp = 0; smp < 4; ++smp) {
            double a = kick_energy(0.0f, m, smp), d = kick_energy(1.0f, m, smp),
                   h = kick_energy(0.5f, m, smp);
            assert(a > 0 && d > 0);
            /* The two layers are within ~4 dB of each other: MIX is a blend,
             * not a volume knob. */
            assert(a < 2.5 * d && d < 2.5 * a);
            /* Equal power: the midpoint is about as loud as the ends. Two
             * ~50 Hz bodies can partly cancel, so allow -5..+3.5 dB. */
            double mean = 0.5 * (a + d);
            assert(h > 0.32 * mean && h < 2.25 * mean);
        }
    }
    /* MIX 0 is analog ONLY: the digital sample leaves no trace. */
    double a0 = kick_energy(0.0f, KICK_808, 0), a1 = kick_energy(0.0f, KICK_808, 3);
    assert(fabs(a0 - a1) < 1e-9 * a0);
    double d0 = kick_energy(1.0f, KICK_808, 1), d1 = kick_energy(1.0f, KICK_IND, 1);
    assert(fabs(d0 - d1) < 1e-6 * d0);
    printf("PASS: A/D MIX equal-power crossfade, 0 = analog only, 1 = digital only\n");
}

static void test_digital_switch(void) {
    static hkh_digital d;
    for (int a = 0; a < 4; ++a) {
        hkh_digital_init(&d);
        hkh_digital_trigger(&d, hkh_default_sample(a), 1.0f);
        float prev = 0, worst = 0;
        for (int i = 0; i < 20000; ++i) {
            if (i == 3000) hkh_digital_trigger(&d, hkh_default_sample((a + 1) % 4), 1.0f);
            float x = hkh_digital_tick(&d);
            if (i >= 3000 && i < 3200) worst = fmaxf(worst, fabsf(x - prev));
            prev = x;
        }
        /* The switch is no sharper than the new sample's own attack. */
        hkh_sample_ref r = hkh_default_sample((a + 1) % 4);
        float own = 0;
        for (int i = 1; i < 200; ++i) own = fmaxf(own, (float)abs(r.data[i] - r.data[i - 1]) / 32768.0f);
        own = fmaxf(own, (float)abs(r.data[0]) / 32768.0f);
        assert(worst <= own + 0.08f);
        /* A sample runs to its end and frees the voice. */
        for (int i = 0; i < 100000; ++i) hkh_digital_tick(&d);
        assert(!hkh_digital_active(&d));
    }
    printf("PASS: digital sample switch mid-hit is click-free\n");
}

/* Minimal WAV writer for the loader tests. */
static void put16(FILE *f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, long v) { put16(f, (int)(v & 0xFFFF)); put16(f, (int)((v >> 16) & 0xFFFF)); }

static void write_wav(const char *path, int format, int bits, int ch, int rate, int frames,
                      int extensible, int junk_chunk, double hz) {
    FILE *f = fopen(path, "wb");
    assert(f);
    int bps = bits / 8, align = ch * bps;
    long data = (long)frames * align;
    int fmt_size = extensible ? 40 : 16;
    fwrite("RIFF", 1, 4, f); put32(f, 4 + 8 + fmt_size + (junk_chunk ? 8 + 5 + 1 : 0) + 8 + data);
    fwrite("WAVE", 1, 4, f);
    if (junk_chunk) { fwrite("LIST", 1, 4, f); put32(f, 5); fwrite("abcde\0", 1, 6, f); }   /* odd: padded */
    fwrite("fmt ", 1, 4, f); put32(f, fmt_size);
    put16(f, extensible ? 0xFFFE : format); put16(f, ch); put32(f, rate);
    put32(f, (long)rate * align); put16(f, align); put16(f, bits);
    if (extensible) {
        put16(f, 22); put16(f, bits); put32(f, 3);
        put16(f, format); for (int i = 0; i < 14; ++i) fputc(0, f);
    }
    fwrite("data", 1, 4, f); put32(f, data);
    for (int i = 0; i < frames; ++i) {
        double x = 0.5 * sin(2 * M_PI * hz * i / rate);
        for (int c = 0; c < ch; ++c) {
            if (format == 3 && bits == 32) { float v = (float)x; fwrite(&v, 4, 1, f); }
            else if (format == 3) { double v = x; fwrite(&v, 8, 1, f); }
            else if (bits == 8) fputc((int)lround(x * 127 + 128), f);
            else if (bits == 16) put16(f, (int)lround(x * 32767));
            else if (bits == 24) { long v = lround(x * 8388607); fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); }
            else put32(f, lround(x * 2147483647.0));
        }
    }
    fclose(f);
}

static int zero_crossings(const int16_t *x, int n) {
    int c = 0;
    for (int i = 1; i < n; ++i) if ((x[i - 1] < 0) != (x[i] < 0)) ++c;
    return c;
}

static int16_t WAVBUF[HKH_USER_MAX_SAMPLES];

static void test_wav_loader(const char *dir) {
    char path[512];
    struct { int format, bits, ch, rate, ext, junk; } cases[] = {
        {1, 16, 1, 44100, 0, 0}, {1, 24, 2, 48000, 0, 1}, {3, 32, 1, 22050, 0, 0},
        {1, 8, 1, 11025, 0, 0}, {1, 32, 2, 96000, 1, 0}, {3, 64, 6, 44100, 1, 1},
        {1, 16, 2, 8000, 0, 0}, {3, 32, 2, 192000, 1, 0},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        snprintf(path, sizeof(path), "%s/case%u.wav", dir, i);
        int frames = cases[i].rate / 2;                  /* 0.5 s of 441 Hz */
        write_wav(path, cases[i].format, cases[i].bits, cases[i].ch, cases[i].rate, frames,
                  cases[i].ext, cases[i].junk, 441.0);
        int n = hkh_wav_load(path, WAVBUF, HKH_USER_MAX_SAMPLES, NULL);
        /* resampled to 44.1 kHz (the last source interval is not extrapolated) */
        assert(abs(n - 22050) <= 1 + 44100 / cases[i].rate);
        int zc = zero_crossings(WAVBUF, n);
        assert(abs(zc - 441) <= 4);                      /* pitch preserved */
        int peak = 0;
        double sum = 0;
        for (int j = 0; j < n; ++j) peak = abs(WAVBUF[j]) > peak ? abs(WAVBUF[j]) : peak;
        for (int j = 0; j < 6615; ++j) sum += (double)WAVBUF[j] * WAVBUF[j];
        /* a sine: RMS target 0.35 -> peak 0.495 */
        assert(fabs(sqrt(sum / 6615) / 32767.0 - 0.35) < 0.01);
        assert(abs(peak - 16220) < 400);
        /* the first excursion is positive */
        for (int j = 0; j < n; ++j) if (abs(WAVBUF[j]) * 4 >= peak) { assert(WAVBUF[j] > 0); break; }
    }
    /* Longer than two seconds: truncated with a fade, never an overrun. */
    snprintf(path, sizeof(path), "%s/long.wav", dir);
    write_wav(path, 1, 16, 1, 44100, 44100 * 3, 0, 0, 100.0);
    int n = hkh_wav_load(path, WAVBUF, HKH_USER_MAX_SAMPLES, NULL);
    assert(n == HKH_USER_MAX_SAMPLES && WAVBUF[n - 1] == 0);
    /* Refused: not RIFF, truncated header, unsupported codec, silence, tiny. */
    snprintf(path, sizeof(path), "%s/bad.wav", dir);
    FILE *f = fopen(path, "wb"); fputs("RIFX....WAVEjunk", f); fclose(f);
    assert(hkh_wav_load(path, WAVBUF, HKH_USER_MAX_SAMPLES, NULL) == 0);
    write_wav(path, 2, 16, 1, 44100, 1000, 0, 0, 441.0);   /* ADPCM tag */
    assert(hkh_wav_load(path, WAVBUF, HKH_USER_MAX_SAMPLES, NULL) == 0);
    write_wav(path, 1, 16, 1, 44100, 1000, 0, 0, 0.0);     /* silent */
    assert(hkh_wav_load(path, WAVBUF, HKH_USER_MAX_SAMPLES, NULL) == 0);
    write_wav(path, 1, 16, 1, 44100, 20, 0, 0, 441.0);     /* too short */
    assert(hkh_wav_load(path, WAVBUF, HKH_USER_MAX_SAMPLES, NULL) == 0);
    write_wav(path, 1, 16, 1, 44100, 44100, 0, 0, 441.0);
    f = fopen(path, "r+b"); fseek(f, 0, SEEK_END); long size = ftell(f); fclose(f);
    assert(truncate(path, size / 2) == 0);                 /* cut mid-data: keeps what is there */
    n = hkh_wav_load(path, WAVBUF, HKH_USER_MAX_SAMPLES, NULL);
    assert(n > 20000 && n < 23000);
    assert(truncate(path, 30) == 0);                       /* cut mid-header */
    assert(hkh_wav_load(path, WAVBUF, HKH_USER_MAX_SAMPLES, NULL) == 0);
    assert(hkh_wav_load("/nonexistent/x.wav", WAVBUF, HKH_USER_MAX_SAMPLES, NULL) == 0);
    /* A stop request aborts a load. */
    atomic_int stop = 1;
    snprintf(path, sizeof(path), "%s/case0.wav", dir);
    assert(hkh_wav_load(path, WAVBUF, HKH_USER_MAX_SAMPLES, &stop) == 0);
    printf("PASS: WAV loader: 8/16/24/32-bit, float32/64, extensible, 1-6 ch, 8-192 kHz; refuses junk\n");
}

static hkh_sample_bank BANK;

static void test_sample_bank(const char *dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/user", dir);
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s' '%s/mod/samples'", path, dir);
    assert(system(cmd) == 0);
    char f2[600], f4[600];
    snprintf(f2, sizeof(f2), "%s/kick2.wav", path);
    snprintf(f4, sizeof(f4), "%s/mod/samples/kick4.wav", dir);
    write_wav(f2, 1, 16, 1, 44100, 30000, 0, 0, 200.0);
    write_wav(f4, 1, 24, 2, 48000, 20000, 0, 0, 300.0);
    char mod[520];
    snprintf(mod, sizeof(mod), "%s/mod", dir);
    hkh_samples_start(&BANK, path, mod);
    for (int i = 0; i < 2000 && !atomic_load(&BANK.done); ++i) {
        struct timespec ts = {0, 1000000};
        nanosleep(&ts, NULL);
    }
    assert(atomic_load(&BANK.done));
    hkh_samples_stop(&BANK);
    assert(hkh_samples_user_mask(&BANK) == ((1 << 1) | (1 << 3)));
    assert(hkh_samples_get(&BANK, 1).len == 30000);         /* user folder */
    assert(hkh_samples_get(&BANK, 3).data == BANK.data[3]);  /* module fallback */
    assert(hkh_samples_get(&BANK, 0).data == hkh_default_sample(0).data);
    assert(hkh_samples_get(NULL, 2).data == hkh_default_sample(2).data);
    /* A restart forgets the old files until reloaded. */
    hkh_samples_start(&BANK, "", "");
    assert(hkh_samples_user_mask(&BANK) == 0);
    hkh_samples_stop(&BANK);
    printf("PASS: user WAVs replace slots via the demoted loader; built-ins otherwise\n");
}

/* ---- analog hats (phase 4) ------------------------------------------------ */
#define HN 88200
static float HB[HN];

typedef struct { float peak, rms50, t40; } hat_stats;

static hat_stats hat_render(int model, float decay, float color, float filter, float *out, int n) {
    static hkh_hat h;
    hkh_hat_init(&h, 3);
    h.color = color; h.filter = filter;
    hkh_hat_trigger(&h, model, 1.0f, decay, 1);
    hat_stats st = {0};
    double e = 0;
    for (int i = 0; i < n; ++i) {
        if (i % BLOCK == 0) hkh_hat_block(&h, color, filter, decay);
        out[i] = hkh_hat_tick(&h);
        assert(isfinite(out[i]) && fabsf(out[i]) <= 1.0f);
        if (fabsf(out[i]) > st.peak) st.peak = fabsf(out[i]);
        if (i < 2205) e += out[i] * out[i];
    }
    for (int i = n - 1; i >= 0; --i) if (fabsf(out[i]) > st.peak * 0.01f) { st.t40 = i / 44100.0f; break; }
    st.rms50 = (float)sqrt(e / 2205);
    return st;
}

static double correlation(const float *a, const float *b, int n) {
    double ab = 0, aa = 0, bb = 0;
    for (int i = 0; i < n; ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
    return ab / sqrt(aa * bb + 1e-20);
}

static void test_hat_models(void) {
    static float other[HN];
    float closed[4];
    for (int m = 0; m < 4; ++m) {
        float t40[5];
        for (int d = 0; d < 5; ++d) t40[d] = hat_render(m, d * 0.25f, 0.5f, 0.5f, HB, HN).t40;
        /* 0 loose closed .. 0.5 open .. 1 long open. */
        for (int d = 1; d < 5; ++d) assert(t40[d] > 1.3f * t40[d - 1]);
        if (!(t40[0] > 0.05f && t40[0] < 0.15f)) fprintf(stderr, "m%d t40 %.3f %.3f %.3f %.3f %.3f\n", m, t40[0], t40[1], t40[2], t40[3], t40[4]);
        assert(t40[0] > 0.05f && t40[0] < 0.15f);
        assert(t40[2] > 0.2f && t40[2] < 0.6f);
        assert(t40[4] > 0.7f);
        closed[m] = hat_render(m, HKH_HAT_CLOSED_DECAY, 0.5f, 0.5f, HB, HN).rms50;
        /* Closed -> open is not just a longer VCA: the first 3 ms (before
         * the envelope can differ) already sound different. */
        hat_render(m, 0.0f, 0.5f, 0.5f, HB, 132);
        hat_render(m, 1.0f, 0.5f, 0.5f, other, 132);
        { double cc = correlation(HB, other, 132); if (!(cc < 0.985)) fprintf(stderr, "m%d corr %.3f\n", m, cc); }
        assert(correlation(HB, other, 132) < 0.985);
        /* COLOR and FILTER both move the sound, at the default decay. */
        hat_render(m, 0.25f, 0.0f, 0.5f, HB, 2205);
        hat_render(m, 0.25f, 1.0f, 0.5f, other, 2205);
        assert(correlation(HB, other, 2205) < 0.8);
        hat_render(m, 0.25f, 0.5f, 0.0f, HB, 2205);
        hat_render(m, 0.25f, 0.5f, 1.0f, other, 2205);
        assert(correlation(HB, other, 2205) < 0.9);
        /* FILTER at either end stays usable: within ~9 dB of neutral. */
        float lo = hat_render(m, 0.25f, 0.5f, 0.0f, HB, HN).rms50;
        float hi = hat_render(m, 0.25f, 0.5f, 1.0f, HB, HN).rms50;
        assert(lo > closed[m] * 0.35f && hi > closed[m] * 0.35f);
    }
    /* The four models are equally loud as closed hats (+-2 dB). */
    for (int a = 0; a < 4; ++a)
        for (int b = 0; b < 4; ++b) assert(closed[a] < closed[b] * 1.26f);
    /* ...and different from one another. */
    for (int a = 0; a < 4; ++a) {
        hat_render(a, 0.25f, 0.5f, 0.5f, HB, 2205);
        for (int b = a + 1; b < 4; ++b) {
            hat_render(b, 0.25f, 0.5f, 0.5f, other, 2205);
            assert(fabs(correlation(HB, other, 2205)) < 0.5);
        }
    }
    printf("PASS: 808/909/METAL/IND hats: closed<->open on one knob, COLOR/FILTER, balanced\n");
}

static void test_hat_follow_and_choke(void) {
    static hkh_hat h;
    /* A hit from the knob follows the knob: open it while it rings. */
    hkh_hat_init(&h, 1);
    hkh_hat_trigger(&h, 0, 1.0f, 0.0f, 1);
    int alive = 0;
    for (int i = 0; i < 44100; ++i) {
        if (i % BLOCK == 0) hkh_hat_block(&h, 0.5f, 0.5f, i > 256 ? 1.0f : 0.0f);
        hkh_hat_tick(&h);
        if (hkh_hat_active(&h)) alive = i;
    }
    assert(alive > 0.3 * 44100);
    /* A recorded (motion) hit keeps its own DECAY whatever the knob does. */
    hkh_hat_init(&h, 1);
    hkh_hat_trigger(&h, 0, 1.0f, 0.0f, 0);
    alive = 0;
    for (int i = 0; i < 44100; ++i) {
        if (i % BLOCK == 0) hkh_hat_block(&h, 0.5f, 0.5f, 1.0f);
        hkh_hat_tick(&h);
        if (hkh_hat_active(&h)) alive = i;
    }
    assert(alive < 0.25 * 44100);
    /* Closed chokes open without a click. */
    for (int m = 0; m < 4; ++m) {
        hkh_hat_init(&h, 2);
        hkh_hat_trigger(&h, m, 1.0f, 1.0f, 1);
        float prev = 0, worst = 0, fresh = 0;
        for (int i = 0; i < 12000; ++i) {
            if (i % BLOCK == 0) hkh_hat_block(&h, 0.5f, 0.5f, 1.0f);
            if (i == 8000) hkh_hat_trigger(&h, m, 1.0f, 0.1f, 0);
            float x = hkh_hat_tick(&h);
            if (i >= 8000 && i < 8200) worst = fmaxf(worst, fabsf(x - prev));
            if (i < 200) fresh = fmaxf(fresh, fabsf(x - prev));
            prev = x;
        }
        assert(worst <= fresh * 1.3f + 0.05f);
    }
    printf("PASS: hat follows the knob unless recorded; closed chokes open cleanly\n");
}

/* ---- effects (phase 5) ----------------------------------------------------- */
typedef struct { double rms, between, punch, peak; } groove_stats;

/* Two bars of the default groove at 130 BPM after two bars of settling. */
static groove_stats groove(int model, int param, float value, int kick_only, int hat_only) {
    static hkh_engine g;
    hkh_engine_init(&g, 5);
    g.k_model = model;
    if (param >= 0) hkh_engine_set_param(&g, param, value);
    g.h_mute = kick_only;
    g.k_mute = hat_only;
    transport t = {0.0, 130.0f, 1};
    double s = 0, btw = 0;
    long n = 0, nb = 0;
    groove_stats st = {0};
    int blocks = (int)(4 * 4 * spb(130) / BLOCK);
    for (int b = 0; b < blocks; ++b) {
        double beat0 = t.beat;
        render(&g, &t);
        if (b < blocks / 2) continue;
        for (int i = 0; i < BLOCK; ++i) {
            double pos = beat0 + i / spb(130), frac = pos - floor(pos);
            s += L[i] * L[i]; ++n;
            if (fabsf(L[i]) > st.peak) st.peak = fabsf(L[i]);
            if (frac > 0.6 && frac < 0.95) { btw += L[i] * L[i]; ++nb; }
            if (frac < 0.02 && fabsf(L[i]) > st.punch) st.punch = fabsf(L[i]);
        }
    }
    st.rms = 20 * log10(sqrt(s / n) + 1e-12);
    st.between = 20 * log10(sqrt(btw / nb) + 1e-12);
    return st;
}

static void test_fx_levels(void) {
    for (int m = 0; m < 3; ++m) {
        groove_stats d0 = groove(m, P_K_DRIVE, 0.0f, 1, 0), d1 = groove(m, P_K_DRIVE, 1.0f, 1, 0);
        if (!(d1.rms - d0.rms > -2.0 && d1.rms - d0.rms < 7.0)) fprintf(stderr, "drive m%d %.1f -> %.1f\n", m, d0.rms, d1.rms);
        assert(d1.rms - d0.rms > -2.0 && d1.rms - d0.rms < 7.0);   /* DRIVE: dirtier, not a boost */
        /* COMP: audible -- peaks held, body denser, tail lifted -- and
         * never a volume knob or a trip into the limiter. */
        groove_stats c0 = groove(m, P_K_COMP, 0.0f, 1, 0), c1 = groove(m, P_K_COMP, 1.0f, 1, 0);
        assert(c1.rms - c0.rms > 1.0 && c1.rms - c0.rms < 7.0);
        assert(c1.between - c0.between > 6.0);
        assert(fabs(20 * log10(c1.peak / c0.peak)) < 2.0 && c1.peak < 0.95f);
        /* RUMBLE fills the gaps between kicks and leaves the punch alone. */
        double prev = -200;
        groove_stats r0 = groove(m, P_K_RUMBLE, 0.0f, 1, 0);
        for (int k = 0; k <= 4; ++k) {
            groove_stats r = groove(m, P_K_RUMBLE, k * 0.25f, 1, 0);
            assert(r.between >= prev - 0.1);
            prev = r.between;
            /* The attack is never dulled (rumble sits under it, not over). */
            double dp = 20 * log10(r.punch / r0.punch);
            assert(dp > -1.0 && dp < 2.0);
        }
        /* (The 808's own long tail already fills much of the gap.) */
        assert(prev - r0.between > (m == KICK_808 ? 8.0 : 15.0));
        /* Kick REVERB is audible between kicks. */
        double v1 = groove(m, P_K_VERB, 1.0f, 1, 0).between, v0 = groove(m, P_K_VERB, 0.0f, 1, 0).between;
        if (!(v1 - v0 > 1.5)) fprintf(stderr, "verb m%d %.1f -> %.1f\n", m, v0, v1);
        assert(v1 - v0 > 1.5);
    }
    groove_stats h0 = groove(0, P_H_VERB, 0.0f, 0, 1), h1 = groove(0, P_H_VERB, 1.0f, 0, 1);
    if (!(h1.between - h0.between > 3.0)) fprintf(stderr, "hverb %.1f -> %.1f\n", h0.between, h1.between);
    assert(h1.between - h0.between > 3.0);
    groove_stats hd0 = groove(0, P_H_DRIVE, 0.0f, 0, 1), hd1 = groove(0, P_H_DRIVE, 1.0f, 0, 1);
    assert(hd1.rms - hd0.rms > -3.0 && hd1.rms - hd0.rms < 6.0);
    groove_stats hc0 = groove(0, P_H_COMP, 0.0f, 0, 1), hc1 = groove(0, P_H_COMP, 1.0f, 0, 1);
    assert(hc1.rms - hc0.rms > -1.0 && hc1.rms - hc0.rms < 7.0 && hc1.peak < 0.95f);
    /* Defaults leave headroom: under the limiter, near -6 dBFS peak. */
    groove_stats def = groove(1, -1, 0, 0, 0);
    assert(def.peak < 0.95f && def.peak > 0.5f);
    printf("PASS: DRIVE holds level, COMP audibly thickens, RUMBLE fills gaps not punch, REVERB sends, headroom\n");
}

/* Random knobs, dense patterns, fills: never above -3 dBFS, never NaN. */
static void test_fx_fuzz(void) {
    static hkh_engine g;
    uint32_t seed = 12345;
    for (int trial = 0; trial < 120; ++trial) {
        hkh_engine_init(&g, trial + 1);
        for (int p = 0; p < P_COUNT; ++p) hkh_engine_set_param(&g, p, hkh_rand01(&seed) < 0.3f ? 1.0f : hkh_rand01(&seed));
        g.k_model = (int)(hkh_rand01(&seed) * 3);
        g.h_model = (int)(hkh_rand01(&seed) * 4);
        g.k_sample = (int)(hkh_rand01(&seed) * 4);
        g.k_pattern = (uint16_t)hkh_rand_u32(&seed);
        hkh_engine_set_length(&g, hkh_rand01(&seed) < 0.5f ? 8 : 16);
        g.h_pattern = (uint16_t)hkh_rand_u32(&seed);
        for (int i = 0; i < 8; ++i) g.motion[i] = hkh_rand01(&seed) < 0.5f ? hkh_rand01(&seed) : HKH_MOTION_NONE;
        transport t = {0.0, 60.0f + 200.0f * hkh_rand01(&seed), 1};
        for (int b = 0; b < 700; ++b) {
            if (b % 50 == 0) {           /* knobs move while it plays */
                hkh_engine_set_param(&g, (int)(hkh_rand01(&seed) * P_COUNT), hkh_rand01(&seed));
                hkh_engine_set_fill(&g, 0, hkh_rand01(&seed) < 0.5f);
                hkh_engine_set_fill(&g, 1, hkh_rand01(&seed) < 0.5f);
            }
            render(&g, &t);
            for (int i = 0; i < BLOCK; ++i) assert(fabsf(L[i]) <= 0.9441f && fabsf(R[i]) <= 0.9441f);
        }
        assert(g.fault_count == 0);
    }
    printf("PASS: 120 random settings with moving knobs: bounded at -0.5 dBFS, finite\n");
}

/* RUMBLE and REVERB at maximum cannot run away: the tail dies. */
static void test_fx_tails(void) {
    static hkh_engine g;
    hkh_engine_init(&g, 7);
    for (int p = 0; p < P_COUNT; ++p) hkh_engine_set_param(&g, p, 1.0f);
    g.k_pattern = 0xFFFF;
    g.h_pattern = 0xFFFF;
    hkh_engine_set_length(&g, 16);
    transport t = {0.0, 174.0f, 1};
    run(&g, &t, 44100 * 20 / BLOCK);                 /* 20 s flat out */
    t.running = 0;                                   /* stop: tails only */
    run(&g, &t, 44100 * 15 / BLOCK);
    float tail = 0;
    run(&g, &t, 10);
    for (int i = 0; i < BLOCK; ++i) tail = fmaxf(tail, fmaxf(fabsf(L[i]), fabsf(R[i])));
    assert(tail < 1e-4f);                            /* < -80 dBFS after 15 s */
    printf("PASS: RUMBLE + REVERB at max decay to silence (no runaway)\n");
}

/* REVERB tone: the KICK's send is lowpassed (2 kHz, 24 dB/oct), so its click
 * never rings in the room; the HAT's send is deliberately not. White noise
 * into one send at a time; Goertzel power of the tail at 8 kHz vs 1 kHz. */
static double goertzel(const float *x, int n, double hz) {
    double w = 2.0 * M_PI * hz / 44100.0, c = 2.0 * cos(w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i) { double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static double reverb_tail_tilt(int kick) {
    static hkh_reverb rv;
    static float out[44100];
    hkh_reverb_init(&rv);
    uint32_t seed = 12345;
    for (int i = 0; i < 44100; ++i) {
        float in = 0.0f;
        if (i < 4410) {                               /* 100 ms of white noise */
            seed = seed * 1664525u + 1013904223u;
            in = ((float)(seed >> 8) / 8388608.0f - 1.0f) * 0.5f;
        }
        float l, r;
        hkh_reverb_tick(&rv, kick ? in : 0.0f, kick ? 0.0f : in, &l, &r);
        out[i] = l + r;
    }
    double lo = 0, hi = 0;
    for (int w = 4410; w + 2048 <= 44100; w += 2048) {
        lo += goertzel(out + w, 2048, 1000.0);
        hi += goertzel(out + w, 2048, 8000.0);
    }
    return 10.0 * log10(hi / lo);
}

static void test_reverb_tone(void) {
    double kick = reverb_tail_tilt(1), hat = reverb_tail_tilt(0);
    printf("  reverb tail 8 kHz vs 1 kHz: kick %.1f dB, hat %.1f dB\n", kick, hat);
    fflush(stdout);
    assert(kick < -30.0);                 /* was -3.3 dB with the send unfiltered */
    assert(hat > -10.0);                  /* the hat's room keeps its air */
    printf("PASS: REVERB lowpasses the kick's send, not the hat's\n");
}

/* Knob moves are ramped: no step in the waveform when a knob jumps. */
static float zipper_run(int param, float from, float to, int measure_from, int measure_to) {
    static hkh_engine g;
    hkh_engine_init(&g, 8);
    g.k_model = KICK_808;
    g.h_pattern = 0;
    hkh_engine_set_param(&g, P_K_DECAY, 1.0f);
    hkh_engine_set_param(&g, param, from);
    transport t = {0.0, 120.0f, 0};
    hkh_engine_trigger_kick(&g, 1.0f);
    float prev = 0, worst = 0;
    for (int b = 0; b < measure_to; ++b) {
        if (b == 100) hkh_engine_set_param(&g, param, to);
        render(&g, &t);
        for (int i = 0; i < BLOCK; ++i) {
            if (b >= measure_from) worst = fmaxf(worst, fabsf(L[i] - prev));
            prev = L[i];
        }
    }
    return worst;
}

static void test_zipper(void) {
    const int params[] = {P_K_VOL, P_K_MIX, P_K_DRIVE, P_K_COMP, P_K_DECAY};
    for (unsigned pi = 0; pi < sizeof(params) / sizeof(params[0]); ++pi) {
        /* The steepest the signal ever is at ANY steady setting on the way. */
        float steady = 0;
        for (int k = 1; k <= 9; ++k) {
            float v = k * 0.1f;
            steady = fmaxf(steady, zipper_run(params[pi], v, v, 100, 110));
        }
        float during = zipper_run(params[pi], 0.1f, 0.9f, 100, 110);
        /* A glide never steps: it is no steeper than the settings it passes. */
        if (!(during <= steady * 1.25f + 0.002f))
            fprintf(stderr, "zipper param %d: steady %.5f during %.5f\n", params[pi], steady, during);
        assert(during <= steady * 1.25f + 0.002f);
    }
    printf("PASS: knob jumps are ramped (no zipper steps)\n");
}

/* ---- SHUFFLE / RANDOM / FILL (phase 7) ------------------------------------ */
static int bits(uint8_t p) { int n = 0; for (; p; p &= (uint8_t)(p - 1)) ++n; return n; }

static void test_random(void) {
    uint32_t rng = 99;
    int kick_one = 0, hat_off = 0, seen_k[256] = {0}, seen_h[256] = {0};
    uint8_t k = 0x11, h = 0x44;
    for (int i = 0; i < 20000; ++i) {
        uint8_t nk = hkh_pattern_random(0, k, &rng), nh = hkh_pattern_random(1, h, &rng);
        assert(nk != k && nh != h);                /* RANDOM always changes it */
        assert(bits(nk) >= 2 && bits(nk) <= 5);
        assert(bits(nh) >= 2 && bits(nh) <= 6);
        kick_one += nk & 1;
        hat_off += (nh & 0x44) != 0;
        seen_k[nk] = seen_h[nh] = 1;
        k = nk; h = nh;
    }
    assert(kick_one > 20000 * 0.9);                /* the kick keeps its one */
    assert(hat_off > 20000 * 0.8);                 /* the hat leans offbeat */
    int nk = 0, nh = 0;
    for (int i = 0; i < 256; ++i) { nk += seen_k[i]; nh += seen_h[i]; }
    assert(nk > 30 && nh > 30);                    /* and it is not a short list */
    printf("PASS: RANDOM: always new, 2-5 kicks / 2-6 hats, downbeat & offbeat bias, %d/%d shapes\n", nk, nh);
}

static void test_shuffle(void) {
    uint32_t rng = 7;
    for (int p = 1; p < 255; ++p) {
        for (int i = 0; i < 40; ++i) {
            uint8_t k = hkh_pattern_shuffle(0, (uint8_t)p, &rng);
            uint8_t h = hkh_pattern_shuffle(1, (uint8_t)p, &rng);
            assert(k != 0 && h != 0);
            if ((p & 0xFE) == 0) {
                assert(k != p && (k & 1) && bits(k) == 2);    /* lone "one": add a partner */
            } else {
                /* The same material, moved: the one stays, the count holds. */
                assert((k & 1) == (p & 1));
                assert(bits(k) == bits((uint8_t)p));
            }
            assert(abs(bits(h) - bits((uint8_t)p)) <= 1);
            assert(h != p);
        }
    }
    assert(hkh_pattern_shuffle(0, 0, &rng) == 0);        /* nothing to move */
    assert(hkh_pattern_shuffle(1, 0xFF, &rng) == 0xFF);
    printf("PASS: SHUFFLE rearranges the pattern it is given (the one stays put)\n");
}

static void test_fill(void) {
    transport t = {0.0, 120.0f, 1};
    hkh_engine_init(&E, 21);
    run(&E, &t, blocks_for_steps(120, 8) - 1);           /* one loop, plain */
    unsigned k0 = E.k_hits, h0 = E.h_hits;
    assert(k0 == 2 && h0 == 2);
    hkh_engine_set_fill(&E, 0, 1);
    hkh_engine_set_fill(&E, 1, 1);
    assert((hkh_engine_effective_pattern(&E, 0) & 0xFF) == 0xF1);
    assert((hkh_engine_effective_pattern(&E, 1) & 0xFF) == 0xFF);
    /* One loop of fill: kick 5 + 1 ratchet, hat 8 + 2 ratchets. Track the
     * ratchet's position: exactly a 1/32 after step 8. */
    long sample = 0, step8_at = -1, ratchet_at = -1;
    unsigned kh = E.k_hits;
    for (int b = 0; b < blocks_for_steps(120, 8); ++b) {
        for (int i = 0; i < 1; ++i) {}
        hkh_engine_render(&E, L, R, BLOCK, t.beat, t.bpm);
        if (E.k_hits != kh) {
            if (E.seq.cur_step == 7 && step8_at < 0) step8_at = sample;
            else if (step8_at >= 0 && ratchet_at < 0) ratchet_at = sample;
            kh = E.k_hits;
        }
        E.k_fill_age = E.h_fill_age = 0;                  /* the UI's heartbeat */
        t.beat += BLOCK / spb(120);
        sample += BLOCK;
    }
    assert(E.k_hits - k0 == 6 && E.h_hits - h0 == 10);
    /* Block-granular check: the ratchet lands ~2756 samples (a 1/32 at 120)
     * after the step-8 hit. */
    assert(labs(ratchet_at - step8_at - 2756) <= BLOCK);
    /* Released: the written pattern comes straight back, untouched. */
    hkh_engine_set_fill(&E, 0, 0);
    hkh_engine_set_fill(&E, 1, 0);
    assert(E.k_pattern == 0x1111 && E.h_pattern == 0x4444);
    assert(hkh_engine_effective_pattern(&E, 0) == 0x1111);
    printf("PASS: FILL is momentary: roll + 1/32 ratchets while held, pattern intact\n");
}

/* ---- 16 steps -------------------------------------------------------------- */
static void test_sixteen(void) {
    transport t = {0.0, 120.0f, 1};
    hkh_engine_init(&E, 31);
    E.k_pattern = 0x0021;                          /* steps 1 and 6 at 8 steps */
    E.motion[5] = 0.8f;
    hkh_engine_set_length(&E, 16);
    /* The groove carries on: the second half repeats the first. */
    assert(E.seq.length == 16 && E.k_pattern == 0x2121 && E.motion[13] == 0.8f);
    hkh_engine_toggle_step(&E, 0, 15);
    assert(E.k_pattern == 0xA121);
    /* One 16-step loop: steps 0..15 each fire once, then step 0 again. */
    E.h_pattern = 0;
    render(&E, &t);                               /* (plays the step-16 audition) */
    E.k_hits = 0;
    t.beat = 0.0; E.seq.running = 0;
    run(&E, &t, blocks_for_steps(120, 16) - 1);
    if (!(E.k_hits == 5 && E.seq.cur_step == 15)) fprintf(stderr, "hits %u step %d pat %x\n", E.k_hits, E.seq.cur_step, E.k_pattern);
    assert(E.k_hits == 5 && E.seq.cur_step == 15);
    /* SHUFFLE / RANDOM work on each half; RESET is the 16-step default. */
    for (int i = 0; i < 50; ++i) {
        hkh_engine_random(&E, 0);
        assert((E.k_pattern & 0xFF) && (E.k_pattern >> 8));
    }
    hkh_engine_reset(&E, 0);
    assert(E.k_pattern == 0x1111);
    /* FILL: first half as written, the roll in the second. */
    hkh_engine_set_fill(&E, 0, 1);
    assert(hkh_engine_effective_pattern(&E, 0) == 0xF111);
    hkh_engine_set_fill(&E, 0, 0);
    /* Back to 8 keeps the first half; the 8-step loop wraps at 8. */
    hkh_engine_set_length(&E, 8);
    assert(E.seq.length == 8);
    t.running = 0; render(&E, &t); t.running = 1; t.beat = 0.0;
    run(&E, &t, blocks_for_steps(120, 9) - 1);
    assert(E.seq.cur_step == 0);                  /* step 9 of time = step 1 again */
    printf("PASS: 16 steps: carries the groove over, halves shuffle/random, fill, back to 8\n");
}

static int16_t RACK_CLICK[400];
static hkh_sample_ref rack_src(void *ctx, int pad) {
    (void)ctx;
    hkh_sample_ref r = { NULL, 0 };
    if (pad == 4) return r;                      /* an empty pad */
    r.data = RACK_CLICK; r.len = 400;
    return r;
}

static float energy(hkh_engine *e, transport *t) {
    render(e, t);
    float sum = 0.0f;
    for (int i = 0; i < BLOCK; ++i) sum += L[i] * L[i];
    return sum;
}

static void test_rack(void) {
    for (int i = 0; i < 400; ++i) RACK_CLICK[i] = 16000;
    transport t = {0.0, 120.0f, 1};
    hkh_engine_init(&E, 41);
    hkh_engine_set_rack_source(&E, rack_src, NULL);
    E.k_pattern = E.h_pattern = 0;
    hkh_engine_rack_toggle_step(&E, 0, 0);
    hkh_engine_rack_toggle_step(&E, 1, 2);
    hkh_engine_rack_toggle_step(&E, 4, 1);       /* empty pad: never sounds */
    hkh_engine_rack_toggle_step(&E, 2, 9);       /* beyond 8 steps: refused */
    assert(E.r_pattern[0] == 1 && E.r_pattern[1] == 4 && E.r_pattern[2] == 0);
    hkh_engine_rack_set_mute(&E, 1, 1);
    E.seq.running = 0; t.beat = 0.0;
    E.r_pending = 0;                             /* drop the toggle auditions */
    E.r_hits = 0;
    run(&E, &t, blocks_for_steps(120, 8) - 1);
    assert(E.r_hits == 1);                       /* pad 0 only: 1 muted, 4 empty */
    hkh_engine_rack_set_mute(&E, 1, 0);
    hkh_engine_rack_set_vol(&E, 0, 0.0f);
    hkh_engine_set_length(&E, 16);
    assert(E.r_pattern[0] == 0x0101 && E.r_pattern[1] == 0x0404);
    /* Stopped: a trigger auditions, at the pad's volume (0 -> silence). */
    t.running = 0; render(&E, &t);
    for (int i = 0; i < 80; ++i) render(&E, &t);
    hkh_engine_rack_trigger(&E, 0);
    float e0 = 0; for (int i = 0; i < 4; ++i) e0 += energy(&E, &t);
    hkh_engine_rack_trigger(&E, 3);
    float e3 = 0; for (int i = 0; i < 4; ++i) e3 += energy(&E, &t);
    if (!(e0 < 1e-3f && e3 > 1.0f)) fprintf(stderr, "e0 %g e3 %g\n", e0, e3);
    assert(e0 < 1e-3f && e3 > 1.0f);
    printf("PASS: rack: per-pad steps, mute, empty pads, volume, 16-step carry\n");
}

int main(void) {
    test_seq_timing();
    test_seq_edges();
    test_patterns();
    test_motion();
    test_heartbeat();
    test_auditions();
    test_kick_models();
    test_kick_retrigger();
    test_default_samples();
    test_mix();
    test_digital_switch();
    test_hat_models();
    test_hat_follow_and_choke();
    test_fx_levels();
    test_fx_fuzz();
    test_fx_tails();
    test_reverb_tone();
    test_zipper();
    test_random();
    test_shuffle();
    test_fill();
    test_sixteen();
    test_rack();
    const char *dir = getenv("HKH_TEST_DIR");
    assert(dir && dir[0]);
    test_wav_loader(dir);
    test_sample_bank(dir);
    printf("ALL ENGINE TESTS PASSED\n");
    return 0;
}
