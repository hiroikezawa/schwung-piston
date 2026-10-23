/* Piston - PISTON SUB: キックに連動して鳴るサブベース（hkh_sub.h 参照）。MIT. */
#include "hkh_sub.h"
#include <math.h>
#include <string.h>

const char *const hkh_sub_keys[SUB_COUNT] = {
    "sub_on", "sub_mode", "sub_trig", "sub_level", "sub_bal",
    "sub_decay", "sub_delay", "sub_drive", "sub_tone", "sub_duck",
};

/* 初期値。SUB は OFF：以前の Set やプリセットの音が変わらないように。
 * DECAY 0.5721 = 350 ms（50 * 30^v）、DELAY 0.25 = 25 ms。 */
const float hkh_sub_defaults[SUB_COUNT] = {
    0.0f, (float)SUB_ANALOG, (float)SUB_FOLLOW, 0.50f, 0.70f,
    0.5721f, 0.25f, 0.15f, 0.40f, 0.50f,
};

/* 音程：A0 と A1。キックが落ち着く A1 = 55 Hz にそろえる。 */
#define SUB_HZ 27.5f
#define BODY_HZ 55.0f

/* エンベロープの内部固定値。アタック 3 ms はクリックを防ぐための最短の
 * 立ち上がり（27.5 Hz の 1 周期は 36 ms なので、低音のアタック感は損なわない）。
 * リリース 12 ms は OFF / ミュート時に音を切らずに消すための長さ。 */
#define SUB_ATTACK_MS 3.0f
#define SUB_RELEASE_MS 12.0f
#define SUB_ENV_FLOOR 1e-4f            /* -80 dB：ここまで下がったら休止 */

/* ダッキング：キックが鳴ったら約 1.5 ms で下げ（クリックしない速さ）、
 * キックのアタックの間（20 ms）は下げたまま保ち、その後 60 ms の時定数で
 * なめらかに戻す。 */
#define SUB_DUCK_HOLD (int)(0.020f * HKH_SR)
#define SUB_DUCK_ATK 0.0149f           /* 1 - exp(-1 / (1.5 ms * SR)) */
#define SUB_DUCK_REL 0.99962f          /* exp(-1 / (60 ms * SR)) */

/* MODE のクロスフェード：約 20 ms の 1 次の平滑化で重みを移す。波形を
 * 突然切り替えないので、切り替えでクリックしない。 */
#define SUB_MODE_SMOOTH 0.00113f       /* 1 - exp(-1 / (20 ms * SR)) */

/* ブロックごとの平滑化（約 20 ms で目標に落ち着く。エンジンと同じ値）。 */
#define SUB_BLOCK_SMOOTH 0.25f

/* SUB 全体の天井：レベルを上げすぎてもここで丸め、出力リミッターへの
 * 負担を減らす。 */
#define SUB_CEILING 0.8f
#define SUB_LEVEL_MAX 1.2f             /* ノブ1 = 100% のときの振幅（2 乗カーブ） */

/* MODE ごとの性格。
 *   body_gain   BODY の量（DEEP は控えめ）
 *   drive_base  DRIVE 0% でもかかる飽和（ANALOG は軽く、DIRTY は強め）
 *   drive_range DRIVE 100% で加わる入力ゲイン */
static const float BODY_GAIN[SUB_MODE_COUNT] = {0.45f, 1.0f, 1.0f};
static const float DRIVE_BASE[SUB_MODE_COUNT] = {0.0f, 0.6f, 1.5f};
static const float DRIVE_RANGE[SUB_MODE_COUNT] = {2.0f, 5.0f, 12.0f};
#define SUB_G_MAX 16.0f                /* 補正表の上限（1 + 1.5 + 12 = 14.5 を含む） */

/* DC 除去：15 Hz の 1 次ハイパス（BODY は 55 Hz なので影響は小さい）。 */
#define SUB_DC_R 0.99786f              /* 1 - 2 pi 15 / SR */

float hkh_sub_decay_ms(float v) { return 50.0f * powf(30.0f, hkh_clamp(v, 0.0f, 1.0f)); }
float hkh_sub_delay_ms(float v) { return 100.0f * hkh_clamp(v, 0.0f, 1.0f); }

/* TONE のカットオフ：150 Hz（暗い）から 8 kHz（明るい）まで指数的に。 */
static float tone_coef(float v) {
    float hz = 150.0f * powf(8000.0f / 150.0f, hkh_clamp(v, 0.0f, 1.0f));
    return 1.0f - expf(-HKH_TWO_PI * hz / HKH_SR);
}

/* DRIVE の音量補正表：入力ゲイン g で tanh(g * sin) にしたときの RMS を、
 * 元のサインの RMS にそろえる倍率。DRIVE を回しても音量がほぼ変わらず、
 * 倍音（歪み）だけが増えるようにする。g = 1..SUB_G_MAX を等間隔に。
 * 初期化で 1 回だけ計算する（33 x 256 回の tanh。オーディオ処理では引くだけ）。 */
static void build_drive_table(hkh_sub *s) {
    for (int k = 0; k < HKH_SUB_DRIVE_TABLE; ++k) {
        float g = 1.0f + (SUB_G_MAX - 1.0f) * (float)k / (float)(HKH_SUB_DRIVE_TABLE - 1);
        double sum = 0.0;
        for (int i = 0; i < 256; ++i) {
            float y = hkh_tanh(g * hkh_sin_turns((float)i / 256.0f));
            sum += (double)y * y;
        }
        float rms = (float)sqrt(sum / 256.0);
        s->drive_comp[k] = rms > 1e-6f ? 0.70710678f / rms : 1.0f;
    }
}

static float drive_comp(const hkh_sub *s, float g) {
    float x = (g - 1.0f) / (SUB_G_MAX - 1.0f) * (float)(HKH_SUB_DRIVE_TABLE - 1);
    x = hkh_clamp(x, 0.0f, (float)(HKH_SUB_DRIVE_TABLE - 1));
    int i = (int)x;
    if (i >= HKH_SUB_DRIVE_TABLE - 1) return s->drive_comp[HKH_SUB_DRIVE_TABLE - 1];
    float f = x - (float)i;
    return s->drive_comp[i] + (s->drive_comp[i + 1] - s->drive_comp[i]) * f;
}

/* ノブ位置から、平滑化の目標値を求める。 */
static float level_target(const hkh_sub *s) {
    float v = s->prm[SUB_LEVEL];
    return SUB_LEVEL_MAX * v * v;
}

void hkh_sub_init(hkh_sub *s) {
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < SUB_COUNT; ++i) s->prm[i] = hkh_sub_defaults[i];
    build_drive_table(s);
    s->level = level_target(s);
    s->bal_sub = sinf(s->prm[SUB_BAL] * 1.5707963f);
    s->bal_body = cosf(s->prm[SUB_BAL] * 1.5707963f);
    s->drive = s->prm[SUB_DRIVE];
    s->tone_c = tone_coef(s->prm[SUB_TONE]);
    s->duck_depth = s->prm[SUB_DUCK];
    s->w[(int)s->prm[SUB_MODE]] = 1.0f;
    s->dec_coef = hkh_t60_coef(hkh_sub_decay_ms(s->prm[SUB_DECAY]) * 0.001f);
    s->beat_inc = 120.0 / 60.0 / HKH_SR;
}

/* 予約をすべて取り消す（beats_only なら拍で数えているものだけ）。 */
static void drop_events(hkh_sub *s, int beats_only) {
    int n = 0;
    for (int i = 0; i < HKH_SUB_EVENTS; ++i) {
        if (s->ev[i].active && (!beats_only || s->ev[i].beats)) s->ev[i].active = 0;
        n += s->ev[i].active;
    }
    s->n_ev = n;
}

void hkh_sub_choke(hkh_sub *s) {
    drop_events(s, 0);
    if (s->stage != 0) s->stage = 3;          /* 現在の音量から 12 ms で消す */
}

void hkh_sub_transport_stopped(hkh_sub *s) { drop_events(s, 1); }

void hkh_sub_set(hkh_sub *s, int index, float value) {
    if (index < 0 || index >= SUB_COUNT || !isfinite(value)) return;
    switch (index) {
    case SUB_ON:
        value = value >= 0.5f ? 1.0f : 0.0f;
        if (value == 0.0f) hkh_sub_choke(s);
        break;
    case SUB_MODE:
        value = hkh_clamp(roundf(value), 0.0f, (float)(SUB_MODE_COUNT - 1));
        break;
    case SUB_TRIG:
        value = hkh_clamp(roundf(value), 0.0f, 1.0f);
        /* 方式を変えたら、古い方式で入れた予約は捨てる */
        if (value != s->prm[SUB_TRIG]) drop_events(s, 0);
        break;
    default:
        value = hkh_clamp(value, 0.0f, 1.0f);
        break;
    }
    s->prm[index] = value;
}

/* 発音を始める。休止中からなら位相を 0（波形のゼロクロス）から始め、毎回
 * 同じ形で鳴るようにする。鳴っている最中の再トリガーは位相をそのまま続け、
 * エンベロープも今の音量からアタックするので、連打してもクリックしない。 */
static void start_note(hkh_sub *s, float vel) {
    if (s->stage == 0) {
        s->ph_sub = s->ph_body = 0.0f;
        s->tone_y = 0.0f;
        s->dc_x = s->dc_y = 0.0f;
        s->env = 0.0f;
    }
    s->env_peak = hkh_clamp(vel, 0.0f, 1.0f);
    s->stage = s->env < s->env_peak ? 1 : 2;
}

static void push_event(hkh_sub *s, int beats, double left, float vel) {
    for (int i = 0; i < HKH_SUB_EVENTS; ++i) {
        if (s->ev[i].active) continue;
        s->ev[i].active = 1;
        s->ev[i].beats = beats;
        s->ev[i].left = left;
        s->ev[i].vel = vel;
        ++s->n_ev;
        return;
    }
    /* 満杯（ありえないほどの連打）：新しいものを捨てる。鳴りかけの予約を
     * 消すより安全。 */
}

void hkh_sub_kick(hkh_sub *s, float vel) {
    if (s->prm[SUB_ON] < 0.5f) return;
    s->duck_t = 0;                            /* ダッキングはキックと同時に始まる */
    if ((int)s->prm[SUB_TRIG] == SUB_OFFBEAT) {
        /* 半拍後。拍で数えるので、待っている間にテンポが変わっても裏拍に落ちる。 */
        push_event(s, 1, 0.5, vel);
        return;
    }
    double d = (double)hkh_sub_delay_ms(s->prm[SUB_DELAY]) * 0.001 * HKH_SR;
    if (d < 1.0) start_note(s, vel);          /* 遅延 0：このサンプルから */
    else push_event(s, 0, d, vel);
}

void hkh_sub_block(hkh_sub *s, float bpm, int frames) {
    if (!(bpm >= 20.0f && bpm <= 999.0f)) bpm = 120.0f;
    s->beat_inc = (double)bpm / 60.0 / HKH_SR;
    s->dec_coef = hkh_t60_coef(hkh_sub_decay_ms(s->prm[SUB_DECAY]) * 0.001f);
    float lt = level_target(s);
    float bs = sinf(s->prm[SUB_BAL] * 1.5707963f), bb = cosf(s->prm[SUB_BAL] * 1.5707963f);
    float dt = s->prm[SUB_DRIVE], kt = s->prm[SUB_DUCK];
    float tc = tone_coef(s->prm[SUB_TONE]);
    if (!hkh_sub_active(s) || frames <= 0) {
        /* 鳴っていない間は目標へそのまま移す（聞こえる段差がないので）。 */
        s->level = lt; s->bal_sub = bs; s->bal_body = bb;
        s->drive = dt; s->duck_depth = kt; s->tone_c = tc;
        s->level_step = s->bal_sub_step = s->bal_body_step = s->drive_step = s->duck_step = 0.0f;
        for (int m = 0; m < SUB_MODE_COUNT; ++m) s->w[m] = m == (int)s->prm[SUB_MODE] ? 1.0f : 0.0f;
        return;
    }
    /* 鳴っている間はブロック内で直線的に寄せる（ジッパーノイズ防止）。 */
    float f = (float)frames;
    s->level_step = (lt - s->level) * SUB_BLOCK_SMOOTH / f;
    s->bal_sub_step = (bs - s->bal_sub) * SUB_BLOCK_SMOOTH / f;
    s->bal_body_step = (bb - s->bal_body) * SUB_BLOCK_SMOOTH / f;
    s->drive_step = (dt - s->drive) * SUB_BLOCK_SMOOTH / f;
    s->duck_step = (kt - s->duck_depth) * SUB_BLOCK_SMOOTH / f;
    s->tone_c += (tc - s->tone_c) * SUB_BLOCK_SMOOTH;
}

int hkh_sub_active(const hkh_sub *s) { return s->stage != 0 || s->n_ev > 0; }

/* 予約を 1 サンプル進め、時間が来たものを鳴らす。 */
static void run_events(hkh_sub *s) {
    for (int i = 0; i < HKH_SUB_EVENTS; ++i) {
        hkh_sub_event *e = &s->ev[i];
        if (!e->active) continue;
        e->left -= e->beats ? s->beat_inc : 1.0;
        if (e->left <= 0.0) {
            e->active = 0;
            --s->n_ev;
            start_note(s, e->vel);
        }
    }
}

float hkh_sub_tick(hkh_sub *s, float vol) {
    if (s->stage == 0 && s->n_ev == 0) return 0.0f;      /* OFF・休止中は何もしない */
    if (s->n_ev > 0) run_events(s);

    /* 平滑化中のパラメータを 1 サンプル進める */
    s->level += s->level_step;
    s->bal_sub += s->bal_sub_step;
    s->bal_body += s->bal_body_step;
    s->drive += s->drive_step;
    s->duck_depth += s->duck_step;

    /* ダッキング（キックからの経過時間で決まる） */
    if (s->duck_t < SUB_DUCK_HOLD) { s->duck += (1.0f - s->duck) * SUB_DUCK_ATK; ++s->duck_t; }
    else s->duck *= SUB_DUCK_REL;

    if (s->stage == 0) return 0.0f;                      /* 予約待ちの間は無音 */

    /* エンベロープ */
    switch (s->stage) {
    case 1:
        s->env += 1.0f / (SUB_ATTACK_MS * 0.001f * HKH_SR);
        if (s->env >= s->env_peak) { s->env = s->env_peak; s->stage = 2; }
        break;
    case 2:
        s->env *= s->dec_coef;
        break;
    default:
        s->env -= 1.0f / (SUB_RELEASE_MS * 0.001f * HKH_SR);
        break;
    }
    if (s->env <= SUB_ENV_FLOOR && s->stage != 1) {
        s->env = 0.0f;
        s->stage = 0;
        return 0.0f;
    }

    /* MODE の重みを目標へ（クロスフェード） */
    int mode = (int)s->prm[SUB_MODE];
    for (int m = 0; m < SUB_MODE_COUNT; ++m)
        s->w[m] += ((m == mode ? 1.0f : 0.0f) - s->w[m]) * SUB_MODE_SMOOTH;

    /* SUB OSC：A0 の純粋なサイン。DRIVE も TONE も通らない。 */
    float sub = hkh_sin_turns(s->ph_sub);
    s->ph_sub = hkh_wrap01(s->ph_sub + SUB_HZ / HKH_SR);

    /* BODY OSC：A1。どの波形も位相 0 でゼロから上がる形にそろえてある。 */
    const float dt = BODY_HZ / HKH_SR;
    float p = s->ph_body, x = 0.0f, g = 0.0f, gain = 0.0f;
    if (s->w[SUB_DEEP] > 1e-4f) x += s->w[SUB_DEEP] * hkh_sin_turns(p);
    if (s->w[SUB_ANALOG] > 1e-4f) {
        float q = hkh_wrap01(p + 0.25f);
        x += s->w[SUB_ANALOG] * (1.0f - 4.0f * fabsf(q - 0.5f));          /* 三角波 */
    }
    if (s->w[SUB_DIRTY] > 1e-4f) {
        float q = hkh_wrap01(p + 0.5f);
        x += s->w[SUB_DIRTY] * (2.0f * q - 1.0f - hkh_polyblep(q, dt));   /* 帯域制限ノコギリ波 */
    }
    for (int m = 0; m < SUB_MODE_COUNT; ++m) {
        g += s->w[m] * (1.0f + DRIVE_BASE[m] + DRIVE_RANGE[m] * s->drive);
        gain += s->w[m] * BODY_GAIN[m];
    }
    s->ph_body = hkh_wrap01(p + dt);

    /* DRIVE：tanh 型のソフトクリップ、音量補正つき。55 Hz の倍音は折り返す
     * 前に十分小さく、後段の TONE でも削られるので、オーバーサンプリングは
     * しない（負荷を増やさないため）。 */
    float y = hkh_tanh(g * x) * drive_comp(s, g);
    /* DC 除去 */
    float hp = y - s->dc_x + SUB_DC_R * s->dc_y;
    s->dc_x = y;
    s->dc_y = hp;
    /* TONE：1 次ローパス */
    s->tone_y += (hp - s->tone_y) * s->tone_c;

    float mix = s->bal_sub * sub + s->bal_body * gain * s->tone_y;
    float out = mix * s->env * (1.0f - s->duck_depth * s->duck) * s->level * vol;
    out = hkh_tanh(out * (1.0f / SUB_CEILING)) * SUB_CEILING;
    if (!isfinite(out)) {           /* 万一の数値異常：鳴っている状態を捨てて黙る */
        drop_events(s, 0);
        s->stage = 0;
        s->env = s->duck = s->tone_y = s->dc_x = s->dc_y = 0.0f;
        s->ph_sub = s->ph_body = 0.0f;
        return 0.0f;
    }
    return out;
}
