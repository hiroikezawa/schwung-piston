# PISTON

A Schwung sound generator that does two things for industrial techno, live:
a **hybrid kick** (analog model + digital one-shot, blended) and an **analog
hi-hat** (four models, closed to open on one knob), each with its own 8-step
pad sequencer that follows Move's transport.

The DSP is deep; the surface is not. No menus and no Shift gestures: one
surface per voice, switched by the top-right pad, and a second jog page with
one setting (8 or 16 steps).

## 操作方法（日本語）

**準備**: スロットのシンセにPISTONを読み込み、そのシンセを開くと専用画面になり、PADがPISTONの操作面になります。MoveのPlayで再生すると、キックとハットのシーケンサーがMoveのテンポに同期して動きます。画面を離れても鳴り続けます。

**PAD**（キックとハットは別画面。右上のPADで切り替え：赤=KICK、黄=HAT）

```
1段目  [MUTE][RESET][SHUFFLE][RANDOM][FILL][ -- ][ -- ][KICK/HAT]
2段目  KICK: [ANALOG][DIGITAL]   HAT: [MODEL][OFFBEAT]
3段目  16ステップ時: ステップ1-8
4段目  8ステップ時: ステップ1-8 / 16ステップ時: ステップ9-16
```

| PAD | 動作 |
| --- | --- |
| ステップ | 押すたびにON/OFF。白い光が再生位置。停止中にONにすると1回試聴 |
| MUTE | 緑=発音、赤=ミュート（パターンや音色は消えない） |
| RESET | 安全な初期状態へ。キック=1・5拍、ハット=3・7（裏打ち）に戻し、ハットはDECAYモーションも全消去してクローズドに戻す |
| SHUFFLE | 今のパターンを並べ替え（キックの1拍目は固定） |
| RANDOM | 新しいパターンを生成 |
| FILL | 押している間だけフィル（16分連打＋32分ラチェット）。離すと元のパターン |
| ANALOG（押しながら） | ステップ1/2/3 = 808 / 909 / INDUSTRIAL |
| DIGITAL（押しながら） | ステップ1-4 = デジタルキック1-4 |
| MODEL（押しながら） | ステップ1-4 = 808 / 909 / METALLIC / INDUSTRIAL |
| OFFBEAT | ハットを裏打ち（3・7）にする |

**8 / 16ステップ**: ジョグを回して2ページ目、ジョグを押すと切り替え（キック・ハット共通、既定は8）。8→16は今の8ステップを後半にコピー、16→8は前半を残す。

**ノブ**（ノブに触れると画面に大きく表示）

| ノブ | KICK | HAT |
| --- | --- | --- |
| 1 | VOLUME | VOLUME |
| 2 | ANALOG / DIGITAL MIX | COLOR / METAL |
| 3 | DECAY | DECAY（クローズド→オープン） |
| 4 | DRIVE | DRIVE |
| 5 | COMP | COMP |
| 6 | RUMBLE | FILTER |
| 7 | REVERB | REVERB |
| 8 | 予約（未使用） | 予約（未使用） |

**ハットのDECAYモーション**: HAT画面でノブ3に触れている間だけ、通過したステップにDECAY値を記録します（専用RECボタンなし）。記録したステップは毎ループその値で鳴り、長いDECAYのステップは青く光ります。ハットのRESETで全消去。

**自作サンプル**: `/data/UserData/UserLibrary/Samples/Piston/kick1.wav`〜`kick4.wav` に置くとデジタルキック1-4が置き換わります（Schwung Managerのファイル画面からアップロード可）。

**更新したとき**: インストール後は **Moveの電源を一度入れ直してください**。読み込み済みのDSPはスロットを読み直しても入れ替わりません。KICK画面でノブ6に触れると画面下に動作中のDSPのバージョンが出ます。

## Install

Needs an aarch64 cross compiler (`gcc-aarch64-linux-gnu`) and Python 3.

```bash
scripts/build.sh arm64                         # dist/piston-module.tar.gz
python3 scripts/install.py http://<move-ip>:7700   # upload via Schwung Manager
```

Or upload `dist/piston-module.tar.gz` by hand in Schwung Manager
(Modules -> Install custom). **Restart the Move after installing or
updating**: a dsp.so that is already loaded is not replaced by reloading the
slot.

## Getting it on screen

Load **Piston** as the synth of a chain slot, then open that slot's
synth. The module draws its own performance screen and takes the pads while
that screen is up. Press **Play on Move**: both sequencers start on the
downbeat and follow Move's tempo. Leave the screen (Back, Menu, a track) and
the pads and their colours go back to Move; the sequencers keep playing.

## Pads

Each voice has its own surface; the **top-right pad** switches between KICK
(red) and HAT (yellow). Both sequencers always keep playing.

```
ROW 1   [MUTE] [RESET] [SHUFFLE] [RANDOM] [FILL] [ -- ] [ -- ] [KICK/HAT]
ROW 2   KICK: [ANALOG] [DIGITAL]   HAT: [MODEL] [OFFBEAT]
ROW 3   16 steps: steps 1-8
ROW 4   8 steps: steps 1-8   /   16 steps: steps 9-16
```

- **Steps**: 1/16 notes, tap to toggle. Defaults: kick on 1 and 5, hat on 3
  and 7 (and 9/13, 11/15 at 16 steps). The white step is the playhead. While
  the transport is stopped, turning a step on plays it once.
- **8 / 16 steps**: turn the **jog** to page 2 and **click** to switch; turn
  back for the play page. Shared by both voices, default 8. Going to 16 repeats
  the 8 you had; going back to 8 keeps the first half.
- **MUTE**: green = playing, red = muted. The pattern and sound are kept.
- **RESET**: the safe way home. Kick: pattern back to 1/5, FILL released.
  Hat: pattern back to 3/7, all DECAY motion erased, DECAY back to a normal
  closed hat, FILL released, recording stopped.
- **SHUFFLE** rearranges the pattern (the kick's step 1 stays put);
  **RANDOM** makes a new one. At 16 steps each half is treated as its own 8.
  Rules and tables: `dsp/hkh_pattern.c`.
- **FILL** (hold): a roll with 1/32 ratchets through the last 8 steps; let go
  and the written pattern is back.
- **ANALOG** (hold) + step 1/2/3: 808 / 909 / INDUSTRIAL. **DIGITAL** (hold) +
  step 1-4: digital kick 1-4. **MODEL** (hold) + step 1-4: 808 / 909 /
  METALLIC / INDUSTRIAL. The choices light up (current one white) and the
  screen lists them. Picking while stopped auditions the sound.
- **OFFBEAT**: hat pattern = the offbeats (3 and 7).

## Knobs

| Knob | KICK | HAT |
| --- | --- | --- |
| 1 | VOLUME | VOLUME |
| 2 | ANALOG / DIGITAL MIX | COLOR / METAL |
| 3 | DECAY | DECAY (closed -> open) |
| 4 | DRIVE | DRIVE |
| 5 | COMP | COMP |
| 6 | RUMBLE | FILTER |
| 7 | REVERB | REVERB |
| 8 | reserved | reserved |

Touch a knob to see it big on screen.

- **MIX**: equal-power crossfade, 0 = analog only, 50 % = both at -3 dB,
  100 % = digital only. The digital kicks are loudness-matched to the analog
  ones so MIX blends rather than changes volume. (Two ~50 Hz bodies can
  partly cancel mid-way; some model/sample pairs are a few dB softer at 50 %.)
- **DECAY** (kick): the analog body's decay, mapped per model (808 roughly
  0.2-2.6 s, 909 0.1-1.3 s, IND 0.08-1.8 s). Turning it reshapes the tail that
  is already ringing.
- **DECAY** (hat): 0 very tight, 25 % normal closed, 50 % loose, 75 % open,
  100 % long open. More than the VCA moves: as it opens the "chick" transient
  recedes, sizzle comes up, the highpass drops and the metallic resonances
  narrow.
- **DRIVE**: 0 = clean (bypassed), up to heavy asymmetric distortion, with
  loudness compensation and a lowpass that closes to keep it from fizzing.
- **COMP**: one knob for threshold, ratio, attack, release and makeup; the
  auto-makeup keeps the level roughly steady (within ~3 dB across the range).
- **RUMBLE**: 0 = off. The kick feeds a dark feedback-delay reverb, which is
  saturated, lowpassed (24 dB), and ducked by the kick so it fills the gaps
  without touching the punch. More = longer, lower, dirtier.
- **REVERB**: a room shared by both voices, separate sends. The kick's send is
  highpassed at 180 Hz so it never competes with RUMBLE.

The output is DC-blocked and limited at -3 dBFS whatever the knobs do.

## Hat DECAY motion

In **HAT** mode, **touching knob 3** starts recording; lifting your finger
stops it. While you touch it, the DECAY value is written into every step the
playhead passes (and the current one immediately). Recorded steps then play
back with their own DECAY on every loop; steps you never touched follow the
knob. Example: touch while step 3 plays with DECAY low, later touch around
step 7 with DECAY high, and the same offbeat hat goes "chick ... shhh".

Hat steps with a long recorded DECAY light **blue** on the pads, and the
screen shows each recorded value as a bar inside the hat cells. Hat RESET
erases all of it. Recording needs the transport running (there is no
playhead to write to otherwise).

## Sounds

**Analog kicks** are separate voice designs, not one kick with presets:
808 (sine body, slow shallow glide, hold then long decay, soft tick), 909
(triangle-to-sine body driven hard at the attack, steep sweep, noise + pulse
click), INDUSTRIAL (phase-modulated body at an inharmonic 1.414 ratio with a
decaying FM index and wavefold, a steep two-stage pitch dive, a square-pulse
and noise attack that strikes two high-Q inharmonic resonators, and an
asymmetric saturator inside the voice: "GON / GAN").

**Hats** are all analog-style: six square oscillators at non-integer ratios
(band-limited), noise, two bandpasses, highpass/lowpass, VCA. 808 is the
classic cluster; 909 is brighter with more noise; METALLIC ring-modulates the
squares through ringing bandpasses; INDUSTRIAL ring-modulates noise by the
cluster, saturates hard and sample-and-holds it.

**Digital kicks** 1-4 are built in (PUNCH, SUB, CRUSH, HARD), rendered at
build time by `scripts/gen_digital_kicks.py`, so the module ships no
third-party audio. Replace any of them with your own:

```
/data/UserData/UserLibrary/Samples/Piston/kick1.wav ... kick4.wav
```

(upload them with Schwung Manager's file browser; they survive module
updates). PCM 8/16/24/32-bit or float, any channel count, 8-192 kHz; they are
downmixed, resampled to 44.1 kHz, capped at 2 s, loudness-matched and read
once when the module loads (reload the module after adding files). The screen
shows USR1-4 for a replaced slot.

## MIDI

The slot also answers notes from Move's track or a controller: 35/36 kick,
42/44 hat at the current DECAY, 46 open hat.

## Why it never sleeps

Schwung parks a chain slot whose output has been silent for about a second and
then renders it only twice a second (`DSP_IDLE_THRESHOLD` in the shim). This
module is its own sequencer, so a parked slot would start up to half a second
late after Play. There is no opt-out for a sound generator, so when its output
would be completely silent the module emits a 6 LSB DC offset (-75 dBFS)
instead. DC was chosen over a pulse because it stays inaudible through any
distortion placed after it. While nothing is sounding the engine takes an
idle fast path, so staying awake costs next to nothing.

## Known limitations

- Leaving this screen with Back, Menu or a Move long-press hands the pads and
  their colours back to Move. Jumping straight to another view (a Track tap
  that switches slot, Master FX, Global Settings) gives the pads back but can
  leave this module's colours on them until Move next repaints (change track
  or pad mode on Move).
- User WAVs are read when the module loads; after adding or replacing one,
  reload the module (or the set).
- Verified by native tests and an ARM64 build; the pad/LED/touch behaviour
  follows Schwung's shadow-UI source and has not yet been checked on hardware.

## Building and testing

```
scripts/test.sh            # native build + engine tests (ASan/UBSan) + dlopen smoke + UI tests
scripts/build.sh arm64     # Move build: dist/piston-module.tar.gz
scripts/render_demo.sh     # audition WAVs into build/demo/
scripts/install.py http://<move-ip>:7700   # upload through Schwung Manager
```

## License

MIT (`LICENSE`). `third_party/schwung/host/plugin_api_v1.h` is Schwung's
plugin API header, MIT, Copyright (c) Charles Vestal
(`third_party/schwung/LICENSE`).

## Files

```
dsp/hkh_plugin.c   plugin API v2 adapter, state, ui_state, keepalive
dsp/hkh_engine.c   parameters, commands, the per-block signal path
dsp/hkh_seq.c      8-step clock follower (host beat position)
dsp/hkh_pattern.c  SHUFFLE / RANDOM / FILL rules and tables
dsp/hkh_kick.c     808 / 909 / INDUSTRIAL analog kicks
dsp/hkh_digital.c  one-shot player
dsp/hkh_samples.c  built-in kicks, user WAV loader (worker thread)
dsp/hkh_hat.c      four analog hat models
dsp/hkh_fx.c       DRIVE, COMP, RUMBLE, REVERB, limiter
ui_core.mjs        the pad surface (pure logic, tested under node)
ui_chain.js        binds it to the shadow UI
```

Everything on the audio path is allocation-free (a fixed pool of six
instances), lock-free and bounded; files are read only by a worker thread that
drops to SCHED_OTHER on cores 0-2 first.
