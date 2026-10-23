/* Piston - PISTON SUB: キックに連動して鳴るサブベース。MIT.
 *
 * KICK の2ページ目（ジョグ）で操作する、キックのトリガーに連動したサブベース
 * エンジン。MIDI ノートや独立したシーケンサーは持たず、キックが発音した
 * 瞬間（fire_kick を通るすべての経路：ステップ、FILL のラチェット、試聴、
 * MIDI）だけを合図に鳴る。
 *
 *   SUB  OSC  A0 = 27.5 Hz の純粋なサイン波。歪ませない（低域の純度を守る）。
 *   BODY OSC  A1 = 55 Hz。MODE で波形が変わる（DEEP: サイン / ANALOG: 三角波 /
 *             DIRTY: 倍音の多いノコギリ波）。DRIVE で tanh 型の飽和、TONE で
 *             明るさ（1 次ローパス）、DC 除去。
 *
 * 音程は A 固定：キック（808 / 909 / IND と、ほとんどのデジタルキック）は
 * A1 = 55 Hz に落ち着くので、サブも A にそろえて余韻がうならないようにする。
 * キック側のピッチ設定とは独立している。
 *
 * 出力はモノラル 1 本で、エンジン側で L / R の両方に同じ量を足す（センター
 * 定位）。SUB が OFF で鳴り終わっていれば hkh_sub_tick は何も計算せず 0 を
 * 返すので、既存のキックの音も CPU 負荷も 0.9.4 と同じになる。
 *
 * 固定サイズの状態だけを持ち、オーディオスレッドでは確保もロックもログも
 * しない。 */
#ifndef HKH_SUB_H
#define HKH_SUB_H

#include "hkh_util.h"

/* パラメータ。KNOB はすべて 0..1 のノブ位置で持ち、実際の単位への変換は
 * hkh_sub.c（DSP）と ui_core.mjs（表示）で同じ式を使う。 */
enum {
    SUB_ON,         /* 0 / 1        サブベースの ON / OFF（初期値 OFF）        */
    SUB_MODE,       /* 0..2         DEEP / ANALOG / DIRTY                     */
    SUB_TRIG,       /* 0..1         FOLLOW / OFFBEAT                          */
    SUB_LEVEL,      /* 0..1         ノブ1 全体の音量（2 乗カーブ）           */
    SUB_BAL,        /* 0..1         ノブ2 SUB/BODY（1 = SUB だけ）            */
    SUB_DECAY,      /* 0..1         ノブ3 50..1500 ms（指数カーブ）           */
    SUB_DELAY,      /* 0..1         ノブ4 FOLLOW の発音遅延 0..100 ms         */
    SUB_DRIVE,      /* 0..1         ノブ5 BODY の倍音と歪み                   */
    SUB_TONE,       /* 0..1         ノブ6 BODY の明るさ 150 Hz..8 kHz         */
    SUB_DUCK,       /* 0..1         ノブ7 キック連動ダッキングの深さ          */
    SUB_COUNT
};
enum { SUB_DEEP, SUB_ANALOG, SUB_DIRTY, SUB_MODE_COUNT };
enum { SUB_FOLLOW, SUB_OFFBEAT };

extern const char *const hkh_sub_keys[SUB_COUNT];
extern const float hkh_sub_defaults[SUB_COUNT];

/* 予約できる発音の数。1/32 の FILL ラチェットと OFFBEAT（半拍後）が重なって
 * も足りる数。あふれたら一番古い予約を上書きする。 */
#define HKH_SUB_EVENTS 8
#define HKH_SUB_DRIVE_TABLE 33

typedef struct {
    int active;            /* 1 = 予約あり */
    int beats;             /* 1 = 拍で数える（OFFBEAT）/ 0 = サンプルで数える */
    double left;           /* 残り：beats なら拍、そうでなければサンプル */
    float vel;             /* キックのベロシティ */
} hkh_sub_event;

typedef struct {
    float prm[SUB_COUNT];          /* 設定値（ノブ位置） */

    /* ブロックごとに目標へ寄せる平滑化済みの値（ジッパーノイズ防止） */
    float level, bal_sub, bal_body, drive, tone_c, duck_depth;
    float level_step, bal_sub_step, bal_body_step, drive_step, duck_step;
    float w[SUB_MODE_COUNT];       /* MODE のクロスフェード重み（合計 1） */

    float ph_sub, ph_body;         /* 位相（0..1 回転） */
    float env;                     /* 振幅エンベロープ 0..1 */
    int stage;                     /* 0 = 休止 / 1 = アタック / 2 = ディケイ / 3 = リリース */
    float env_peak;                /* このヒットのアタックの到達点（ベロシティ） */
    float dec_coef;                /* ディケイの 1 サンプルあたりの係数 */
    float duck;                    /* ダッキング量 0..1（1 = 最も下がる） */
    int duck_t;                    /* 最後のキックからのサンプル数 */
    float tone_y;                  /* TONE の 1 次ローパスの状態 */
    float dc_x, dc_y;              /* DC 除去の状態 */
    double beat_inc;               /* 1 サンプルあたりの拍（テンポから） */
    hkh_sub_event ev[HKH_SUB_EVENTS];
    int n_ev;                      /* 予約の数 */
    float drive_comp[HKH_SUB_DRIVE_TABLE];   /* DRIVE の音量補正表 */
} hkh_sub;

void hkh_sub_init(hkh_sub *s);
/* パラメータを設定する（範囲外はクランプ）。OFF にした瞬間は鳴っている音を
 * 短いリリースで消し、予約も取り消す。 */
void hkh_sub_set(hkh_sub *s, int index, float value);
/* キックが鳴った。ダッキングを始め、TRIGGER に従って発音を予約する。 */
void hkh_sub_kick(hkh_sub *s, float vel);
/* キックのミュート / 全消音：短いリリースで止め、予約を捨てる。 */
void hkh_sub_choke(hkh_sub *s);
/* トランスポートが止まった：拍で数えている予約（OFFBEAT）を捨てる。 */
void hkh_sub_transport_stopped(hkh_sub *s);
/* ブロックの先頭で 1 回：パラメータの平滑化とテンポの反映。 */
void hkh_sub_block(hkh_sub *s, float bpm, int frames);
/* 1 サンプル分のモノラル出力。鳴っていなければ計算せずに 0。
 * vol：外から掛ける音量（キックの VOL への追従）。SUB 専用の天井の前に
 * 掛けるので、VOL を上げても天井を超えない。 */
float hkh_sub_tick(hkh_sub *s, float vol);
/* 鳴っている、または予約がある。 */
int hkh_sub_active(const hkh_sub *s);

/* ノブ位置から実際の単位へ（UI の表示も同じ式）。 */
float hkh_sub_decay_ms(float v);   /* 50 * 30^v */
float hkh_sub_delay_ms(float v);   /* 100 * v */

#endif
