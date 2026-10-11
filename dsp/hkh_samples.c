/* Piston - digital kick slots and their off-thread WAV loader. MIT. */
#define _GNU_SOURCE
#include "hkh_samples.h"
#include "hkh_digital_kicks.h"   /* generated: hkh_dk_data / hkh_dk_len / hkh_dk_name */
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Loudness target for every digital slot: RMS of the first 150 ms. The
 * built-ins are rendered to the same rule (scripts/gen_digital_kicks.py). */
#define HKH_TARGET_RMS 0.35f
#define HKH_PEAK_CAP 0.95f
#define HKH_HEAD_SAMPLES 6615

hkh_sample_ref hkh_default_sample(int index) {
    if (index < 0 || index >= HKH_SAMPLE_SLOTS) index = 0;
    hkh_sample_ref r = { hkh_dk_data[index], hkh_dk_len[index] };
    return r;
}

const char *hkh_default_sample_name(int index) {
    return (index >= 0 && index < HKH_SAMPLE_SLOTS) ? hkh_dk_name[index] : "";
}

hkh_sample_ref hkh_samples_get(void *bank, int index) {
    if (index < 0 || index >= HKH_SAMPLE_SLOTS) index = 0;
    hkh_sample_bank *b = bank;
    int n = b ? atomic_load_explicit(&b->len[index], memory_order_acquire) : 0;
    if (n > 0) {
        hkh_sample_ref r = { b->data[index], n };
        return r;
    }
    return hkh_default_sample(index);
}

int hkh_samples_user_mask(hkh_sample_bank *b) {
    int mask = 0;
    for (int i = 0; b && i < HKH_SAMPLE_SLOTS; ++i)
        if (atomic_load_explicit(&b->len[i], memory_order_acquire) > 0) mask |= 1 << i;
    return mask;
}

/* ---- WAV parsing ----------------------------------------------------------- */
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static float decode(const uint8_t *p, int format, int bits) {
    if (format == 3) {
        if (bits == 32) { float f; memcpy(&f, p, 4); return isfinite(f) ? f : 0.0f; }
        double d; memcpy(&d, p, 8); return isfinite(d) ? (float)d : 0.0f;
    }
    switch (bits) {
    case 8: return ((float)p[0] - 128.0f) * (1.0f / 128.0f);
    case 16: return (float)(int16_t)rd16(p) * (1.0f / 32768.0f);
    case 24: {
        int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;
        return (float)v * (1.0f / 8388608.0f);
    }
    default: return (float)(int32_t)rd32(p) * (1.0f / 2147483648.0f);
    }
}

static int16_t to16(float x) {
    x = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
    return (int16_t)lrintf(x * 32767.0f);
}

static int wav_load(const char *path, int16_t *dst, int max, atomic_int *stop, int match);

int hkh_wav_load(const char *path, int16_t *dst, int max, atomic_int *stop) {
    return wav_load(path, dst, max, stop, 1);
}

int hkh_wav_load_natural(const char *path, int16_t *dst, int max, atomic_int *stop) {
    return wav_load(path, dst, max, stop, 0);
}

hkh_sample_ref hkh_samples_rack_get(void *bank, int index) {
    hkh_sample_ref r = { NULL, 0 };
    hkh_sample_bank *b = bank;
    if (!b || index < 0 || index >= HKH_RACK_VOICES) return r;
    int n = atomic_load_explicit(&b->rack_len[index], memory_order_acquire);
    if (n > 0) { r.data = b->rack[index]; r.len = n; }
    return r;
}

int hkh_samples_rack_mask(hkh_sample_bank *b) {
    int mask = 0;
    for (int i = 0; b && i < HKH_RACK_VOICES; ++i)
        if (atomic_load_explicit(&b->rack_len[i], memory_order_acquire) > 0) mask |= 1 << i;
    return mask;
}

static int wav_load(const char *path, int16_t *dst, int max, atomic_int *stop, int match) {
    if (!path || !dst || max < 64) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int n_out = 0, ok = 0;
    uint8_t hdr[12];
    int format = 0, channels = 0, bits = 0, align = 0;
    uint32_t rate = 0;
    long data_left = -1;
    int have_fmt = 0;
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) goto done;
    for (int guard = 0; guard < 64 && data_left < 0; ++guard) {
        uint8_t ch[8];
        if (fread(ch, 1, 8, f) != 8) goto done;
        uint32_t sz = rd32(ch + 4);
        long skip = (long)sz + (sz & 1);
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fm[40];
            if (sz < 16) goto done;
            uint32_t n = sz < sizeof(fm) ? sz : (uint32_t)sizeof(fm);
            if (fread(fm, 1, n, f) != n) goto done;
            format = rd16(fm);
            channels = rd16(fm + 2);
            rate = rd32(fm + 4);
            align = rd16(fm + 12);
            bits = rd16(fm + 14);
            if (format == 0xFFFE) format = n >= 26 ? rd16(fm + 24) : 0;  /* EXTENSIBLE */
            have_fmt = 1;
            skip -= (long)n;
            if (skip > 0 && fseek(f, skip, SEEK_CUR)) goto done;
        } else if (!memcmp(ch, "data", 4)) {
            data_left = (long)sz;
        } else if (fseek(f, skip, SEEK_CUR)) {
            goto done;
        }
    }
    if (!have_fmt || data_left <= 0) goto done;
    if (channels < 1 || channels > 8 || rate < 8000 || rate > 192000) goto done;
    if (!((format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
          (format == 3 && (bits == 32 || bits == 64)))) goto done;
    int bps = bits / 8;
    if (align != channels * bps) goto done;

    /* Stream: 4 KB at a time, downmix, linear-interpolate to 44.1 kHz. */
    uint8_t chunk[4096];
    int frames_per_chunk = (int)(sizeof(chunk) / (size_t)align);
    double step = (double)rate / 44100.0, next = 0.0;
    float prev = 0.0f;
    long n_in = 0;
    while (data_left > 0 && n_out < max) {
        if (stop && atomic_load_explicit(stop, memory_order_relaxed)) goto done;
        long want = (long)frames_per_chunk * align;
        if (want > data_left) want = data_left - data_left % align;
        if (want <= 0) break;
        size_t got = fread(chunk, 1, (size_t)want, f);
        int frames = (int)(got / (size_t)align);
        if (frames <= 0) break;
        data_left -= (long)got;
        for (int fr = 0; fr < frames && n_out < max; ++fr) {
            const uint8_t *p = chunk + (size_t)fr * (size_t)align;
            float x = 0.0f;
            for (int c = 0; c < channels; ++c) x += decode(p + c * bps, format, bits);
            x /= (float)channels;
            while (n_out < max && next <= (double)n_in) {
                float y = n_in == 0 ? x : prev + (x - prev) * (float)(next - (double)(n_in - 1));
                dst[n_out++] = to16(y);
                next += step;
            }
            prev = x;
            ++n_in;
        }
        if (got < (size_t)want) break;
    }
    if (n_out < 64) goto done;

    /* Loudness-match to the analog kicks (see HKH_TARGET_RMS), and make the
     * first big excursion positive like theirs, so a layered attack adds up
     * instead of cancelling. */
    int peak = 0, first = 0;
    double sum = 0.0;
    int head = n_out < HKH_HEAD_SAMPLES ? n_out : HKH_HEAD_SAMPLES;
    for (int i = 0; i < n_out; ++i) {
        int a = dst[i] < 0 ? -dst[i] : dst[i];
        if (a > peak) peak = a;
        if (i < head) sum += (double)dst[i] * (double)dst[i];
    }
    if (peak == 0 || sum <= 0.0) goto done;
    for (int i = 0; i < n_out; ++i)
        if (abs(dst[i]) * 4 >= peak) { first = dst[i]; break; }
    float rms = (float)sqrt(sum / head) / 32767.0f;
    float cap = HKH_PEAK_CAP * 32767.0f / (float)peak;
    float scale = match ? fminf(HKH_TARGET_RMS / rms, cap) : fminf(1.0f, cap);
    if (match && first < 0) scale = -scale;
    for (int i = 0; i < n_out; ++i) dst[i] = to16((float)dst[i] * scale * (1.0f / 32767.0f));
    if (n_out == max) {                 /* truncated: fade the cut */
        int fade = n_out < 220 ? n_out : 220;
        for (int i = 0; i < fade; ++i)
            dst[n_out - fade + i] = (int16_t)((float)dst[n_out - fade + i] * (float)(fade - 1 - i) / (float)fade);
    }
    ok = 1;
done:
    fclose(f);
    return ok ? n_out : 0;
}

/* ---- the loader thread ----------------------------------------------------- */
static void *loader_main(void *arg) {
    hkh_sample_bank *b = arg;
    /* FIRST: never touch the disk at the audio thread's FIFO 70, and keep
     * core 3 (SPI) free. The attr below already asks for SCHED_OTHER; this is
     * the belt to that brace for hosts that refused the explicit attr. */
    struct sched_param sp = { .sched_priority = 0 };
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set); CPU_SET(1, &set); CPU_SET(2, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);

    for (int i = 0; i < HKH_SAMPLE_SLOTS; ++i) {
        if (atomic_load(&b->stop)) break;
        char path[256];
        int n = 0;
        if (b->user_dir[0]) {
            snprintf(path, sizeof(path), "%s/kick%d.wav", b->user_dir, i + 1);
            n = hkh_wav_load(path, b->data[i], HKH_USER_MAX_SAMPLES, &b->stop);
        }
        if (n <= 0 && b->module_dir[0] && !atomic_load(&b->stop)) {
            snprintf(path, sizeof(path), "%s/samples/kick%d.wav", b->module_dir, i + 1);
            n = hkh_wav_load(path, b->data[i], HKH_USER_MAX_SAMPLES, &b->stop);
        }
        if (n > 0) atomic_store_explicit(&b->len[i], n, memory_order_release);
    }
    for (int i = 0; i < HKH_RACK_VOICES && b->user_dir[0]; ++i) {
        if (atomic_load(&b->stop)) break;
        char path[256];
        snprintf(path, sizeof(path), "%s/Rack/%02d.wav", b->user_dir, i + 1);
        int n = hkh_wav_load_natural(path, b->rack[i], HKH_RACK_MAX_SAMPLES, &b->stop);
        if (n > 0) atomic_store_explicit(&b->rack_len[i], n, memory_order_release);
    }
    atomic_store(&b->done, 1);
    return NULL;
}

static void copy_dir(char *dst, size_t size, const char *src) {
    dst[0] = 0;
    if (!src) return;
    size_t n = strlen(src);
    if (n >= size) return;             /* too long: skip rather than truncate */
    memcpy(dst, src, n + 1);
}

void hkh_samples_start(hkh_sample_bank *b, const char *user_dir, const char *module_dir) {
    for (int i = 0; i < HKH_SAMPLE_SLOTS; ++i) atomic_store(&b->len[i], 0);
    for (int i = 0; i < HKH_RACK_VOICES; ++i) atomic_store(&b->rack_len[i], 0);
    atomic_store(&b->stop, 0);
    atomic_store(&b->done, 0);
    b->started = 0;
    copy_dir(b->user_dir, sizeof(b->user_dir), user_dir);
    copy_dir(b->module_dir, sizeof(b->module_dir), module_dir);
    if (!b->user_dir[0] && !b->module_dir[0]) { atomic_store(&b->done, 1); return; }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
    struct sched_param sp = { .sched_priority = 0 };
    pthread_attr_setschedparam(&attr, &sp);
    if (pthread_create(&b->thread, &attr, loader_main, b) == 0) {
        b->started = 1;
    } else {
        pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);
        if (pthread_create(&b->thread, &attr, loader_main, b) == 0) b->started = 1;
        else atomic_store(&b->done, 1);   /* built-ins only; still a working module */
    }
    pthread_attr_destroy(&attr);
}

void hkh_samples_stop(hkh_sample_bank *b) {
    if (!b->started) return;
    atomic_store(&b->stop, 1);
    pthread_join(b->thread, NULL);
    b->started = 0;
}
