# PISTON

**English** | [日本語](README.ja.md)

A sound generator for [Schwung](https://github.com/charlesvestal/schwung) on Ableton Move.
Made for live industrial techno, it puts three things in one slot:

- **Hybrid kick**: blends analogue models (808 / 909 / INDUSTRIAL) with digital one-shots at
  any ratio
- **Analogue hi-hat**: four models. One knob moves continuously from closed to open
- **Drum rack**: a one-shot sampler holding up to 32 of your own WAVs. Each pad has its own
  steps, mute and level, pad-only effects (sample reducer, ring modulator, low-pass), and
  send levels to the delay and reverb

Each has an 8/16-step pad sequencer that runs in sync with Move's tempo and transport. The
kick and the hat each have eight presets that remember a whole sound and pattern.

The sound design runs deep, but the controls are kept simple. There are no menus: knob 8
switches between three screens, KICK, HAT and RACK, and each screen has seven sound knobs.
Turning the jog wheel moves to each screen's second and later pages. On the KICK screen,
page 2 is RUMBLE (a rumble made by repeating the kick) and page 3 is KICK FX (a kick-only
tube). On the HAT and RACK screens, page 2 is the effects (HAT FX = hat-only harmonic
enhancement, BUS COMP = a bus compressor over everything). The 8/16-step setting is on page
2 and later too.

Every kick is tuned to C (808 / 909 / INDUSTRIAL and the digital PUNCH / CRUSH / HARD /
GABBER to C2 = 65.4 Hz, SUB to C1 = 32.7 Hz). They fit straight into tracks with C as the
root, such as C Phrygian.

## Installation

### From a release (recommended)

1. Download the latest `piston-module.tar.gz` from [Releases](../../releases).
2. Upload it in Schwung Manager (`http://<Move's IP>:7700`) under Modules → "Install custom".
3. **Power-cycle Move once.**

### Building from source

You need an aarch64 cross compiler (`gcc-aarch64-linux-gnu`) and Python 3.

```bash
scripts/build.sh arm64                               # creates dist/piston-module.tar.gz
python3 scripts/install.py http://<Move's IP>:7700   # upload through Schwung Manager
```

> **Always power-cycle Move after an update.** A DSP (dsp.so) that has been loaded is not
> replaced by reloading the slot. Touching knob 6 on the KICK screen shows the version of the
> DSP actually running at the very bottom of the screen.

## Getting started

Load PISTON as a slot's synth and open that synth: you get its own screen, and the pads turn
into PISTON's control surface. Press Play on Move and the kick and hat sequencers start in
sync from the top of the beat. Leaving the screen (Back, Menu, holding a track) keeps it
playing, and the pads and their colours go back to Move.

## Pads

KICK, HAT and RACK are separate screens. Turning **knob 8** right switches KICK → HAT →
RACK, and left RACK → HAT → KICK (it stops at the ends and never wraps from RACK back to
KICK) (one screen per roughly 120 degrees of turn; pausing for a second resets the turn so
far). The sequencers of screens not shown keep playing.

KICK and HAT screens:

```
row 1  [MUTE][RESET][SHUFFLE][RANDOM][FILL][selector 1][selector 2][ -- / REC ]
        selectors  KICK: ANALOG / DIGITAL    HAT: MODEL / OFFBEAT
        top right  KICK: nothing    HAT: REC (DECAY motion recording on/off)
row 2  presets 1-8 (tap to recall, Shift+tap to save)
row 3  with 16 steps: steps 1-8
row 4  with 8 steps: steps 1-8 / with 16 steps: steps 9-16
```

| Pad | Action |
| --- | --- |
| Steps | 1/16 notes. Each press toggles on/off. The white light is the play position. Switching a step on while stopped auditions it once |
| MUTE | Green = sounding, red = muted. The pattern and sound are not lost. The mute state is saved with the set and comes back when the set is reopened (or Move restarted) (since 0.7.1) |
| RESET | Returns to a safe initial state in one press. Kick: back to beats 1 and 5, FILL off. Hat: back to 3 and 7 (offbeats), all DECAY motion cleared, DECAY back to the standard closed hat, FILL and recording off |
| SHUFFLE | Rearranges the current pattern (the kick's first beat stays put) |
| RANDOM | Makes a new pattern (the kick favours downbeats, the hat offbeats) |
| FILL | A fill only while held (1/16 runs and 1/32 ratchets). Letting go returns to the original pattern |
| ANALOG (while held) | Steps 1 / 2 / 3 = 808 / 909 / INDUSTRIAL |
| DIGITAL (while held) | Steps 1–5 = digital kicks 1–5 (five names do not fit side by side, so the screen's boxes show numbers and the selected kick's name appears on the line above) |
| MODEL (while held) | Steps 1–4 = 808 / 909 / METALLIC / INDUSTRIAL |
| OFFBEAT | Puts the hat on the offbeats (3 and 7) |
| REC (top right of the HAT screen) | DECAY motion recording on/off. Dark red = off (default), red = on, white = recording. While off, touching knob 3 only changes the DECAY value and records nothing. Turning it off while recording stops the recording on the spot. On/off is not saved with the set and goes back to off when the screen is reopened |

While ANALOG, DIGITAL or MODEL is held, the step pads that act as choices light up (the
current one white), and the list also appears on screen. Choosing while stopped auditions
that sound.

**8 / 16 steps**: turn the jog wheel to page 2 and press the jog to switch. Turn the jog back
for the performance screen. It is shared by the kick and the hat and starts at 8. Going 8→16
copies the current 8 steps into the second half so the rhythm carries on; 16→8 keeps the
first half. With 16 steps, SHUFFLE and RANDOM treat each half as its own 8 steps, and FILL
acts on the second 8 steps.

### Presets

Row 2 of the KICK and HAT screens holds eight presets each. Kick and hat presets are stored
separately.

- **Save**: hold Shift and tap a pad. It remembers the current sound (model, digital kick,
  knobs 2–7) and the pattern. For the hat it includes the DECAY motion. Mute, FILL and knob
  1's VOL (level) are not included; switching presets does not change the level.
- **Recall**: tap a pad. It switches at once, even while playing.
- Colours: off = empty, dim = saved, white = the one last recalled (or saved).

Presets are stored independently of sets in `/data/UserData/schwung/piston-presets.json`. The
same eight are available to PISTON in any slot of any set.

The rules for SHUFFLE, RANDOM and FILL can be adjusted in the table at the top of
`dsp/hkh_pattern.c`.

## Knobs

| Knob | KICK | HAT |
| --- | --- | --- |
| 1 | VOLUME | VOLUME |
| 2 | ANALOG / DIGITAL MIX | COLOR / METAL |
| 3 | DECAY | DECAY (closed → open) |
| 4 | DRIVE | DRIVE |
| 5 | COMP | COMP |
| 6 | RUMBLE | FILTER |
| 7 | REVERB | nothing |
| 8 | Screen switch (KICK / HAT / RACK) | Screen switch (KICK / HAT / RACK) |

Touching a knob shows its value large on screen. Knob 8 adds up how far you turn it and
moves one screen per roughly 120 degrees.

- **MIX**: an equal-power crossfade. 0 = analogue only, 50 % = both at −3 dB, 100 % = digital
  only. The digital kicks are matched in perceived level to the analogue ones, so MIX changes
  only the blend, not the level (the lows partly cancel each other, so some combinations get
  a few dB quieter at 50 %).
- **DECAY (kick)**: the length of the analogue side's tail, fitted to a musical range for
  each model (808: about 0.2–2.6 s, 909: 0.1–1.3 s, IND: 0.08–1.8 s). Turning it while a kick
  is ringing changes that tail too.
- **DECAY (hat)**: 0 % = a closed hat with a little tail (the default and RESET's target),
  50 % = open, 100 % = long open. It is not just the level's decay: the more it opens, the
  weaker the attack's "tsk", the more noise "shhh", some lower content appears, and the metal
  ring gets thinner.
- **DRIVE**: from 0 = clean (fully transparent) to strong asymmetric distortion. The level is
  compensated so distortion does not make it too loud, and harsh highs are tamed
  automatically.
- **COMP**: a punch-style compressor. On each hit the attack (about 12 ms for the kick, 2 ms
  for the hat) passes untouched and the body and tail after it are lowered. Higher gives a
  shorter, tighter sound (time for the tail to halve, roughly: kick 170 ms → 12 ms, hat 85 ms
  → 4 ms) without changing the attack's strength. It never lifts the tail. Default 25 %.
- **RUMBLE (kick)**: the amount of rumble. 0 = off; a new set starts at 50 %. It repeats the
  kick's own sound with a 1/8 delay to put a "bo" on the offbeat. Higher is louder, lower and
  dirtier. The detailed settings are on page 2 of the KICK screen ("RUMBLE" below).
- **FILTER (hat)**: darker towards 0, brighter and thinner towards 1.
- **COLOR / METAL (hat)**: higher is more metallic (higher pitch, less noise, sharper ring).
- **REVERB** (KICK only): send level to the kick's room reverb. The send is limited to
  180 Hz–600 Hz (cut at 24 dB/oct above 600 Hz). The lows do not clash with RUMBLE, and the
  click of the kick's attack, the digital kicks' highs and IND's grit never reach the reverb.
  The hat reverb was removed in 0.9.2 (knob 7 on the HAT screen does nothing; even if hat
  reverb was raised in an older set or preset, it does not sound).

The output has DC removal and a −0.5 dBFS limiter, so no setting produces anything louder.
The kick side (kick + RUMBLE) also has its own ceiling, so piling on the kick does not bury
the hat.

## RUMBLE (page 2 of the KICK screen)

**Turn the jog wheel one step** on the KICK screen for the RUMBLE page. It makes the techno
rumble where one "bo" sounds on each kick's offbeat.

```
eighth note:  1     2
kick:         thud
RUMBLE:             bo
```

The repeat interval is **fixed at 1/8** (since 0.11.1; 0.11.0 also had 1/16 and 1/32). The
second repeat overlaps the next kick but disappears under DUCK, so what you hear is "thud-bo"
on every beat.

The "bo" is neither a sound made by a volume gate nor a separate bass voice. **It is the
kick's own sound repeated by a delay.** No sine or other oscillator is added, so it keeps the
pitch of the kick tuned to C and joins the kick naturally.

| Knob | Name | Range | Default | What it does |
| --- | --- | --- | --- | --- |
| 1 | RMB (RUMBLE) | 0–100 % | 50 % | Overall amount of rumble (the same value as knob 6 on page 1) |
| 2 | FB (FEEDBACK) | 0–100 % | 60 % | How the repeats carry on. Low dies after one "bo"; higher trails on into later beats |
| 3 | DEF (DEFINITION) | 0–100 % | 75 % | The outline of the "bo" (explained below) |
| 4 | DAMP (DAMPING) | 0–100 % | 70 % | How much high end is shaved off. Low is hard, high is dark and round |
| 5 | DUCK | 0–100 % | 80 % | How deeply the rumble is lowered at the moment the kick sounds |
| 6, 7 | nothing | | | |
| 8 | Screen switch | | | Same as page 1 |

The top of the screen shows the repeat interval (1/8) and DEFINITION's state (DIFFUSE /
SMOOTH REPEATS / CLEAR REPEATS). Touching a knob shows its value large.

**DEFINITION** is the most important knob. The rumble is made by mixing two paths.

- **The repeat path**: the kick's sound is rounded with a low-pass, then repeated by a 1/8
  delay synced to the tempo. Each kick opens the send "window", which closes before the repeat
  arrives, so the "bo" is a separate lump.
- **The diffuse path**: the RUMBLE of 0.10.0 and earlier itself. A small, dark feedback delay
  network (4 lines) makes a rumbling reverb.

| DEFINITION | Sound | Gap after the "bo" (measured) |
| --- | --- | --- |
| 0 % | The same rumbling reverb as 0.10.0 and earlier | about 11 dB |
| 50 % | A fat bass rolling smoothly, "thud-booo" | about 17 dB |
| 75 % (default) | A clear "thud-bo", with a little reverb underneath | about 21 dB |
| 100 % | Repeats only. A clear space between the "bo" and the next kick | about 38 dB |

Raising DEFINITION not only changes the ratio of repeats to reverb: the send window gets
shorter (widening the space between repeats), the reverb tail gets shorter, and DUCK follows
the beat position instead of the kick's level. The level difference is kept within about
6 dB from one end of the knob to the other (because the gaps between repeats go silent, the
repeats are lifted a little above 75 %).

**DUCK** lowers the rumble at the moment the kick sounds so the attack stays clear. It always
recovers before the "bo" arrives, so it does not shave the "bo". When a new kick sounds it also
weakens what remains in the loop of the previous kick's repeats, so earlier repeats never pile
up over the next beat.

When the tempo changes it crossfades to the new interval over 20 ms (no pitch wobble and no
clicks).

Measurements (909, RUMBLE 50 %, defaults): at 132 / 136 / 140 BPM alike, one "bo" appears on
the offbeat followed by a gap of about 20–26 dB. The "bo" sounds on the offbeat rather than
under the kick (what remains under the kick is more than 8 dB below the "bo"), and its peak is
about 5 dB below the kick. With FEEDBACK 100 %, RUMBLE 100 %, DEFINITION 100 % and DAMPING 0 %,
playing for a minute while switching tempos and models never keeps getting louder, and it dies
away when stopped.

**Older sets and presets** (including ones with 0.10.0's PISTON SUB) open with the RUMBLE page
at "DEFINITION 0 %, the rest at middle values". In this setting **RUMBLE sounds exactly the same
as 0.10.0 and earlier, sample for sample** (confirmed by a comparison test against the old
code). The RUMBLE page's values are saved with the set and in kick presets (pad row 2). Sets
saved with 1/16 or 1/32 in 0.11.0 still open (they become 1/8).

PISTON SUB (a sub-bass played by the kick), added in 0.10.0, is not in 0.11.0, because the
rumble is now made by the kick's own repeats. SUB settings in 0.10.0 sets and presets are
skipped and everything else opens as is.

## Effect pages

Turning the jog wheel on each screen moves to its effect page, and the knobs switch to the
effect (page 3 on the KICK screen, page 2 on the HAT and RACK screens). Turning the jog back
returns to page 1. The pads work as on page 1, and knob 8 still switches KICK / HAT / RACK
(away from page 1 it moves to the neighbouring screen's effect page). Pressing the jog on any
page other than page 1 switches 8/16 steps as before.

Every effect is **off by default**. Sets and presets saved before 0.7.0 sound exactly the same
(confirmed by bit-comparing every output of 3 kicks × 4 hats with 0.6.5). Effect page values
are saved with the set. They are not part of the kick / hat presets (pad row 2).

### KICK FX (a kick-only tube, page 3 of the KICK screen)

| Knob | Name | Range | Default |
| --- | --- | --- | --- |
| 1 | SAT | 0–100 % | 0 % (off) |
| 2 | DRIVE | 0–24 dB | 6 dB |

Fattens the whole kick (sub included) with asymmetric, tube-style saturation. The kick is a
single signal inside PISTON, so there is no band splitting. The sub is distorted too, so even
on small speakers that cannot reproduce lows (such as Move's built-in speaker) the sub's
harmonics are heard and it gains impact. The dry sound passes as is and only the "difference"
the distortion creates is added. Content above 6 kHz is not bent (not as sound design, but to
prevent the aliasing that digital distortion produces).

- **SAT**: how much distortion is mixed in, and how strong the asymmetry (even harmonics) is.
  At 0 % it is fully transparent.
- **DRIVE**: the level going into the tube. Higher lifts the small parts such as the body and
  tail and squashes the large parts, making the kick fat and dense. The peak hardly rises; the
  average level (perceived loudness) does.

How it differs from page 1's DRIVE: page 1's DRIVE is a distortion that gets darker as it
rises, with the level compensated so it does not get louder. KICK FX does not darken; it lifts
the small parts to raise density and perceived loudness.

Measurements (kick only, SAT 100 %, DRIVE 24 dB): average level +7.0 dB on the 808, +5.3 dB on
the 909, +4.9 dB on IND. The sub (below 100 Hz) +7–9 dB. Peaks from 0.59–0.63 to 0.65–0.74
(within the kick's 0.8 ceiling).

### HAT FX (hat-only harmonic enhancement)

| Knob | Name | Range | Default |
| --- | --- | --- | --- |
| 1 | HARM | 0–100 % | 0 % (off) |
| 2 | FREQ | 1.9k / 2.8k / 6.6k / 9.8k | 6.6k |

**Generates and adds** harmonics (2nd, plus 3rd at 1.9k and 2.8k) from the content of the
chosen band. It is not an EQ boost, so the original sound's level does not change.

- 1.9k, 2.8k: the hat's core, the clarity of the attack's "tsk"
- 6.6k, 9.8k: air, sparkle

The hat is a noise-like sound, so the effect is subtle. Measurements (HARM 100 %): +1.2 dB
above 8 kHz at 9.8k, +0.9 dB in the 1–8 kHz band at 2.8k. FREQ moves gradually when turned (it
sweeps), so switching does not click.

### BUS COMP (page 2 of the RACK screen, over all of PISTON)

| Knob | Name | Range | Default |
| --- | --- | --- | --- |
| 1 | THRSH | -36 to 0 dBFS | -12 dB |
| 2 | RATIO | OFF / 2:1 / 4:1 / 10:1 | OFF |
| 3 | ATK | 1–30 ms | 30 ms |
| 4 | REL | 50–600 ms | 150 ms |
| 5 | SCHPF | 20–200 Hz | 90 Hz |
| 6 | MKUP | -12 to +12 dB | 0 dB |
| 7 | PUNCH | OFF / ON | ON |

A stereo-linked glue compressor over **PISTON's whole output**, the sum of kick, hat, reverb and
rack (delay included). It sits before the output limiter.

- **RATIO**: OFF takes the whole compressor out (MAKEUP included) and makes it fully
  transparent. 2:1 is natural glue, 4:1 strong, 10:1 deliberate squashing.
- **SCHPF**: a high-pass on the detector only. It does not touch the sound. It keeps the
  compressor from reacting too much to the kick's lows alone.
- **MKUP**: raises the level lost to compression. Even raised, it never exceeds the output
  limiter (-0.5 dBFS).
- **PUNCH**: while ON, it limits only the values the compressor actually uses, in the direction
  that keeps the kick's attack (attack 25 ms or more, release 100 ms or more, 10:1 acts as 4:1,
  SCHPF 80 Hz or more). The knob values are not rewritten. While it is limiting, a "*" appears
  next to PUNCH's value.
- **GR** (gain reduction, dB) appears at the top right of the screen. 1–3 dB is a good guide.

Measurements (a standard groove, THRSH -24 dB): -6.2 dB at 4:1, -7.3 dB at 10:1 (PUNCH OFF).
With PUNCH ON, 10:1 and 4:1 give the same output.

## Hat DECAY motion

Turn the top-right pad of the HAT screen (REC) **ON**, then on page 1 the DECAY value is recorded
**only while you touch knob 3** (nothing is recorded with REC off or from knob 3 on page 2).
While you touch it, every step the play position passes gets the DECAY of that moment; letting
go stops recording. Recorded steps play with that value every loop, and unrecorded steps follow
the knob.

Example: touch with DECAY small as step 3 plays, then with DECAY large around step 7, and the
same offbeat hat alternates "tsk… shhh".

Steps with a long DECAY light **blue** on the pads, and the screen shows each step's recorded
value as a bar inside its box. The hat's RESET clears them all. **Shift + touching knob 1** on
the HAT screen (any page) clears only the motion: the pattern and the DECAY value stay as they
are. Nothing is recorded while not playing, because there is no play position.

## How it sounds the way it does

The **analogue kicks** are each built differently, not one kick with different settings.

- **808**: a sine body, a gentle and shallow pitch drop, a short hold and then a long decay, a
  soft attack
- **909**: a body shaped from a triangle wave and driven hard at the attack, a steep pitch drop,
  a click made of noise and a pulse
- **INDUSTRIAL**: a body from non-integer-ratio (1.414) phase modulation (the FM depth and the
  wavefold fall away over time), a steep two-stage pitch drop, a square pulse and noise attack
  striking two high-Q metal resonators, and asymmetric saturation inside (a "gong", "clang"
  texture). This grit (phase modulation, wavefold, asymmetric saturation) lasts only for the
  first ~25 ms of the attack and fades over the next ~25 ms, leaving a tail with the same round
  body as the 808. A longer DECAY only stretches the clean low end (since 0.9.4; before that,
  raising DECAY stretched the angular square wave itself, which sounded crackly and distorted)

The **hats** are all analogue-style: six band-limited square waves at non-integer ratios, noise,
two band-passes, a high-pass/low-pass and a VCA. The 808 is the classic metal cluster, the 909
brighter with more noise, METALLIC ring-modulates the square waves against each other through a
ringing band-pass, and INDUSTRIAL ring-modulates noise with the cluster, drives it hard and adds
sample & hold.

**Digital kicks** 1–5 (PUNCH / SUB / CRUSH / HARD / GABBER) are built in.

- **GABBER** (added in 0.9.3): a gabber kick. A body with a deep, fast 909-style pitch drop
  (about 250 Hz → C2), driven hard and asymmetrically and then hard-clipped, squashed almost to a
  square wave. The body's decay is long, so the squashed "doooon" lasts about 0.4 s and is gone at
  0.9 s. DC is removed and only the fizz above 9 kHz is rounded a little. Like the other digital
  kicks, its level over the first 150 ms is matched to the analogue kicks.

**Kick pitch**: every kick starts high, drops quickly and settles on a fixed note. That settling
note is matched to **C** (since 0.11.0; from 0.9.0 to 0.10.0 it was A1, 55 Hz). There is no knob
that changes pitch, and the MIDI note number does not change it either.

| Kick | Settling note | Start |
| --- | --- | --- |
| 808 | C2 (65.4 Hz) | about 135 Hz |
| 909 | C2 (65.4 Hz) | about 261 Hz |
| INDUSTRIAL | C2 (65.4 Hz) | about 550 Hz |
| PUNCH | C2 (65.4 Hz) | about 216 Hz |
| SUB | C1 (32.7 Hz) | about 64 Hz |
| CRUSH | C2 (65.4 Hz) | about 241 Hz |
| HARD | C2 (65.4 Hz) | about 340 Hz |
| GABBER | C2 (65.4 Hz) | about 255 Hz |

Going from A1 to C2, **only the settling note** was raised by three semitones. The start pitch
and the speed of the drop are the same as 0.10.0, so the attack (where a kick's character shows
most) sounds the same; the drop is three semitones narrower. C2 sounds a little louder than
55 Hz, so each analogue kick model's level was lowered by 0.1–0.3 dB to match 0.10.0's low-end
loudness (K-weighted loudness). SUB was already on C1 and is unchanged.

The 808 used to follow the tuning value in Roland's service manual (56 Hz), but from 0.11.0 C
comes first. Blending analogue and digital with MIX settles on the same note for everything but
SUB, so the tail does not beat. Your own WAVs play without changing pitch. Unpitched noise-like
sounds (hats, the rack) are not tuned.

## Drum rack (RACK screen)

You can lay out up to 32 WAVs of your own and play them. All the pads, 8 columns × 4 rows, are
pads 1–32 (1 at top left, 32 at bottom right).

```
row 1  [ 1][ 2][ 3][ 4][ 5][ 6][ 7][ 8]
row 2  [ 9][10][11][12][13][14][15][16]
row 3  [17][18][19][20][21][22][23][24]
row 4  [25][26][27][28][29][30][31][32]
```

| Control | Action |
| --- | --- |
| Hold a pad | Step editing for that pad. For a pad in the top two rows the 16 pads of the bottom two rows become its steps, and for a pad in the bottom two rows the top two rows (with 8 steps, only the nearer row). Tap steps while holding to toggle them. While stopped, pressing auditions it once |
| Hold a pad and turn knob 1 | That pad's level |
| Hold a pad and turn knob 2 (DLY) | How much of that pad goes into the delay (a send). 0–100 %, default 100 % (every pad goes into the delay, as before 0.8.0). At 0 only that pad has no repeats. The dry sound always plays as is. The size and number of repeats are set by knob 7 (DELAY FB), shared by all pads |
| Hold a pad and turn knob 3 (REV) | How much of that pad goes into the reverb. 0–100 %, default 0 % (no reverb) |
| Hold a pad and turn knob 4 (CRS) | That pad's sample reducer mix (an insert). 0 % = none (default), 100 % = the original sound fully replaced by the reduced sound |
| Hold a pad and turn knob 5 (RNG) | That pad's ring modulator mix (an insert). 0 % = none (default), 100 % = the original sound fully replaced by the ring-modulated sound (the original pitch disappears) |
| Hold a pad and press the cycle button | That pad's cycle goes FULL → 3 → 5 → 7 → FULL (see "Polymeter" below) |
| Hold a pad and turn knob 6 (LPF) | That pad's low-pass filter. Fully right (default, shown as "-") is no filter; turning left muffles it more, down to 80 Hz fully left. The screen shows the cutoff frequency (e.g. 800, 2k4 = 2.4 kHz, 12k). While a pad is held, knob 6 is LPF instead of SIDECHAIN |
| Shift + pad | Toggles that pad's mute (a muted pad lights in the dim shade of its own colour) |
| Knob 6 (SIDECHAIN, with no pad held) | Lowers the whole rack's level (delay returns included) every time the kick sounds. 0 = off; higher dips deeper (at 100 % almost silent at the kick). It drops in about 2 ms and recovers in time with the tempo over a dotted eighth (3/16 of a beat) (about 375 ms at 120 BPM). Sounds between kicks "pump" along with it. It does not act while the kick is muted |
| Knob 7 (DELAY FB) | A tempo-synced dotted-eighth delay over the whole rack. 0 = no delay; higher gives more repeats (at 100 % each repeat loses only about 0.45 dB, a very long tail). The same knob sets the return level (audible even at a few %). The repeats are not filtered, so every repeat sounds like the original |

**Polymeter**: each pad can have its own cycle. While a pad is held, the top-right pad of the same
half (top two rows or bottom two rows) — 8 for row 1, 24 for row 3 — becomes the **cycle button**,
and each press goes FULL → 3 → 5 → 7. When pad 8 or 24 itself is held, the pad to its left (7, 23)
is the button. The button's colour is dark green for FULL, orange for 3, purple for 5 and pink for
7, and the top right of the screen shows e.g. `LEN 5`.

With a cycle of 3, 5 or 7, that many of the pad's steps light and repeat every cycle, counted from
where Play was pressed. Steps beyond the cycle are not erased; they just rest, and they play again
when you return to FULL. The cycle is saved with the set, and older sets all open as FULL, so the
sound does not change. 3, 5 and 7 all come back to the top together every 105 steps, and stopping
and starting again returns every pad to the top. The kick and hat have no cycle.

While a pad is held, **knob 7 picks that pad's colour** (13 colours: TEAL, the default, then RED,
ORANGE, YELLOW, LIME, GREEN, CYAN, AZURE, BLUE, VIOLET, PURPLE, MAGENTA, PINK). The held
pad shows the colour while you choose, and column 7 of the
screen shows `CO` with its two-letter name. A pad with a sample lights in the bright shade of its
colour and a muted pad in the dim shade, so both the colour and the mute show at a glance; a hit
still flashes grey. The colours
belong to the samples (Rack/01-32.wav are shared by every set), so they are saved in
`/data/UserData/schwung/piston-rack-colors.json` when you let go of the pad, the same in every set
and slot. Knob 8 does nothing while a pad is held. To fit 8 columns, the column names are two
letters (VO = level, DL = DLY, RV = REV, CR = CRS, RM = RNG, LP = LPF, CO = colour). Use DELAY FB
(knob 7) and the screen switch (knob 8) after letting go of the pad.

**Rack reverb**: a stereo plate reverb just for the rack (separate from the kick / hat reverb).
Written for this module from the plate structure in Jon Dattorro's paper "Effect Design, Part 1"
(1997). The input is smoothed by four diffuser stages and then rings in two slowly wobbling loops,
so it spreads smoothly without a metallic ring. The settings are fixed (20 ms pre-delay, about 3 s
tail, the highs only slightly rounded); each pad only sets how much it sends with REV. Delay repeats
do not go into the reverb (both are fed from the pad's original sound). The reverb return pumps with
the kick through SIDECHAIN too, and goes into page 2's BUS COMP. If nothing is sent, the processing
stops and uses no CPU.

**Sample reducer (CRS)**: lowers the sample rate to 1/8 (about 5.5 kHz). It simply takes one sample
in every eight and holds it until the next, with no smoothing. The high aliasing "fizz" grit is the
effect itself. The settings are fixed.

**Ring modulator (RNG)**: multiplies the sound by a 500 Hz sine. It is replaced by the sum and
difference of the original frequencies and 500 Hz (220 Hz becomes 280 Hz and 720 Hz), giving a
metallic, bell-like ring. The settings are fixed.

**Low-pass (LPF)**: 12 dB/oct, no resonance (a shape with no peak).

**Each pad's signal path**: level (knob 1) → sample reducer → ring modulator → low-pass. These three
are **inserts** and change the pad's sound itself (the higher the amount, the more the original is
replaced by the effect). From there the sound goes straight out to the rack (dry) and is at the same
time **sent to the delay and the reverb** (the dry sound stays, and the delay and reverb sounds are
added; delay repeats do not go into the reverb). A muted pad plays and sends nothing. Finally all pads
and the delay and reverb returns mix on the rack bus, where SIDECHAIN's pumping and page 2's BUS COMP
apply.

The sample reducer and ring modulator are processed per pad, but they are very light, so even with
all 32 pads sounding at once the extra load is about 23 µs per block (around 0.8 % of the budget).

Per-pad effects and send levels are saved with the set. Older sets open with DLY 100 %, everything
else 0 % and no LPF, so the sound does not change (confirmed that 20 s of output playing kick, hat,
rack, delay and SIDECHAIN is bit-identical to 0.7.1).

Colours: the pad's colour = has a sample, its dim shade = muted, off = no sample,
white = the held pad. While playing, a pad flashes whitish the moment it sounds.

Put samples here (file names are two-digit numbers):

```
/data/UserData/UserLibrary/Samples/Piston/Rack/01.wav to 32.wav
```

Any format the digital kicks accept can be read (mixed to mono, converted to 44.1 kHz, up to 1.5 s
each). The file's level is used as is; only peaks above 0.95 are lowered. Loading happens when the
module starts, so restart Move after putting samples in place. The rack's steps, levels, mutes,
per-pad effects and reverb amounts, the delay FB and SIDECHAIN are saved with the set. The rack is not
part of presets.

### Replacing with your own samples

Putting WAVs in the following place replaces digital kicks 1–5 with them (you can upload them from
Schwung Manager's file screen; they survive module updates).

```
/data/UserData/UserLibrary/Samples/Piston/kick1.wav to kick5.wav
```

PCM 8/16/24/32-bit or floating point, any number of channels and any sample rate (8–192 kHz). Mono
mixing, conversion to 44.1 kHz, a 2 s maximum and level matching happen automatically. Loading happens
when the module starts, so restart Move after putting files in place. Replaced slots show as USR1–5 on
screen.

## MIDI

PISTON ignores MIDI notes (since 0.12.6): it plays only from its own sequencers and pads, so a
melody from a MIDI FX in the same slot, Move's track or a controller never fires the kick or the hat.
The notes still reach the slot's audio FX as before. Only MIDI clock and All Sound Off are read.

## Why it never goes idle when silent

Schwung stops processing a slot that has been silent for about a second, and then runs it only every
0.5 s. PISTON sequences itself, so while parked the first sound after Play could be up to 0.5 s late.
A sound generator has no way to switch this off, so only when the output is completely silent it puts
out a tiny DC of 6 LSB (−75 dBFS) to keep from being parked. It is DC rather than a pulse so that even a
distortion effect placed after it does not turn it into an audible sound. Processing is skipped while
nothing is playing, so it costs almost nothing.

## Known limitations

- Leaving the screen with Back, Menu or holding a Move track gives the pads and their colours back to
  Move. But when moving straight to another view (switching slot by tapping a track, Master FX, Global
  Settings and so on), PISTON's colours can stay until Move redraws (switching tracks or the pad mode on
  Move fixes it).
- A short press of the 16 step buttons below the screen is passed to Move by Schwung itself; PISTON
  cannot take them.
- Your own WAVs (digital kicks, rack) are loaded when the module starts.
- The rack level cannot be changed with Move's volume knob (Schwung itself does not pass the volume
  knob to module screens). Hold a pad and turn knob 1.
- One drum rack pad is at most 1.5 s. Longer samples are cut at 1.5 s (with a short fade-out at the end).
- The RUMBLE page and effect page values do not appear in Schwung's list of LFO and automation targets
  (they can only be operated from PISTON's screen; the amount of RUMBLE itself, knob 6 on page 1, does
  appear).

## Development

```
scripts/test.sh            # native build + engine tests (ASan/UBSan) + dlopen smoke + UI tests + ARM64 build and ABI check
scripts/build.sh arm64     # build for Move: dist/piston-module.tar.gz
scripts/render_demo.sh     # writes listening WAVs to build/demo/ (check the sound without a Move)
scripts/analyze_rumble.py build/demo   # measures the RUMBLE comparison WAVs (132/136/140 BPM × DEFINITION 0/50/100 %) per eighth note
scripts/install.py http://<Move's IP>:7700   # install through Schwung Manager
```

| File | Contents |
| --- | --- |
| `dsp/hkh_plugin.c` | The plugin API v2 entry points, state save / restore, preset export / import, state for the screen, idle prevention |
| `dsp/hkh_engine.c` | Parameters, pad commands, the drum rack, the per-block signal path |
| `dsp/hkh_seq.c` | Following the clock for 8/16 steps (the host's beat position) |
| `dsp/hkh_pattern.c` | Rules and tables for SHUFFLE / RANDOM / FILL |
| `dsp/hkh_kick.c` | The 808 / 909 / INDUSTRIAL analogue kicks |
| `dsp/hkh_digital.c` | One-shot playback |
| `dsp/hkh_plate.c` | The rack's plate reverb (Dattorro-style, stereo) |
| `dsp/hkh_samples.c` | Built-in kicks, loading your own WAVs (digital kicks, drum rack) on a worker thread |
| `dsp/hkh_hat.c` | The four analogue hat models |
| `dsp/hkh_fx.c` | DRIVE, COMP, RUMBLE (the repeat path and the diffuse path), REVERB, limiter |
| `ui_core.mjs` | The pad control surface (pure logic, tested with node) |
| `ui_chain.js` | Connects it to Schwung's screen |

The audio path does no memory allocation or locking (instances come from a fixed pool of 6), and its
amount of work is bounded. Files are loaded only by a worker thread lowered to normal priority and
moved to cores 0–2.

## Licence

MIT (`LICENSE`). The only external code is `third_party/schwung/host/plugin_api_v1.h` (the Schwung
plugin API header, MIT, Copyright (c) Charles Vestal), whose notice is collected in
`THIRD_PARTY_LICENSES.md`.
