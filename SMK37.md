# SLOOP on the M-Vave SMK-37

The M-Vave SMK-37 has the FM-1's chip (JieLi AC791N), so SLOOP runs on it as a target board: `BOARD=smk37`.
Everything SMK-37-specific sits behind `BOARD_SMK37`; the FM-1 build is unchanged, byte for byte.

> Unofficial firmware for the SMK-37, not affiliated with or endorsed by M-Vave. No warranty: install at your own
> risk and read [Safety](#safety) first.

## Models

| Model | Build | Package identity |
|---|---|---|
| SMK-37 Pro | `BOARD=smk37 ./build.sh` | `SMK-37 Pro_900` (`build/sloop-smk37.fwsc`) |
| SMK-37 Elite | `BOARD=smk37 MODEL=elite ./build.sh` | `SMK-37 Elite_900` (`build/sloop-smk37-elite.fwsc`) |
| MKE-P37 | `BOARD=smk37 MODEL=mke ./build.sh` | `MKE-P37_900` (`build/sloop-smk37-mke.fwsc`) |
| Starrykey 37 Play | the SMK-37 Pro build | `SMK-37 Pro_900` |

They are one keyboard under several names. The `MODEL` builds differ only in the name the keyboard reports
(its identity, its update loader's, the update record's), so M-Vave's updater still recognises the unit for the way
back to the stock firmware. A release build: `--release X.Y` (identity `SMK-37 Pro_9XY`). See
[BUILDING.md](BUILDING.md) for the toolchain.

## Install

Connect the keyboard over USB (a data cable) and use this repository's installer, as for the FM-1:

```sh
python3 tools/fm1_install.py build/sloop-smk37.fwsc        # Python 3 with mido and python-rtmidi
```

It finds the keyboard's MIDI port, checks the model and the package, asks the running firmware (the stock one or
SLOOP) to start the update loader and writes the package; then the keyboard restarts and the installed identity is
checked. The web installer (`web/index_pkg.html`, built by `web/make_site.py`) does the same in Chrome or Edge.
Updating later works the same way; projects, presets, samples and settings are kept. A package never goes to the
other device (FM-1 / SMK-37), and a keyboard running another firmware with its own updater is left alone.

Until this project's releases include SMK-37 packages, unofficial builds can be installed with the web installer: <https://genkigenki.github.io/smk37/>.

**Back to the stock firmware:** install the official package for your model with M-Vave's own updater (M-UPGRADE),
or give it to the installer: `python3 tools/fm1_install.py --force SMK-37_Pro_016.fwsc` (the installer reads the
stock package format). The SLOOP build keeps the stock update protocol, so the stock tools see the keyboard as usual.
The user sample slots USR1 / USR2 (0xC2000–0xE9FFF) are where the stock firmware keeps its key, knob and fader
mapping; it trusts that area while its end marker `usrdata` (0xEAFF8) is there. SLOOP erases the marker's sector
(0xEA000, the only sector it may touch there) before it first stores a sample in USRFLASH, and at power-on when the
marker sits over sample data, so the stock firmware rebuilds its factory mapping; an untouched stock mapping (with
your MIDI Suite presets) stays as it is. The web editor's Settings tab has **Prepare the return to M-Vave** to erase
the marker on request.

**If it does not start:** after two failed starts SLOOP opens its USB recovery screen (identity `SMK-37 Pro_000`,
or the model's), where the installer can write a package again; after a third it drops into the chip's mask-ROM
update mode (UBOOT).

## Knobs, faders, wheels

| FM-1 printed | SLOOP function | SMK-37 printed | on the SMK-37 |
|---|---|---|---|
| MASTER (pot, top left) | output volume | K1 | master volume (virtual pot, 24 steps a click; its curve: HOME menu > AUDIO > LIMITER) |
| SELECT (big encoder) | tempo (TRACKS), page ◀▶, visualiser style, menu section | K2 | SELECT: tempo / pages |
| ALGORITHM | select track 1–4 | K3 | track |
| PRESETS | sound (synth) / kit (drums) | K4 | sound / kit, the pad picker shows them |
| KNOB 1 | dial 1 on screen (blue) | K5 | dial 1 |
| KNOB 2 | dial 2 on screen (green) | K6 | dial 2 |
| KNOB 3 | dial 3 on screen (yellow) | K7 | dial 3 |
| KNOB 4 | dial 4 on screen (orange) | K8 | dial 4 |
| — | — | KNOB LAYOUT = SMK | HOME menu > SCREEN > KNOB LAYOUT (K7): K1–K4 the dials 1–4, K5 master volume, K6 SELECT, K7 PRESETS, K8 ALGORITHM (track); FM-1 (default) as above |
| — | — | FADER 1–4 | tracks 1–4 (4 = drums), by FADER BANK: A level, B filter (low-pass ◀ open ▶ high-pass), C reverb, D delay; a fader takes over once moved. [Organ mode](#organ-mode) adds banks E–G |
| — (no wheels used by SLOOP) | — | PITCH wheel | pitch bend, selected synth track: HOME menu > PLAY > BEND UP (K6) / BEND DOWN (K7), OFF, 1, 2, 3, 4, 5, 7, 12 semitones each; default 7 / 7 |
| — | — | MOD wheel | vibrato 10 Hz, up to ±1 semitone (up = more); [organ mode](#organ-mode): the rotor STOP / SLOW / FAST |
| — | — | PEDAL jack | — |

## Buttons

| FM-1 printed | SLOOP tap | SLOOP hold (layer) | SMK-37 printed | on the SMK-37 |
|---|---|---|---|---|
| PLAY | start / stop (LED flashes on every beat) | — | SEQ PLAY | PLAY |
| REC | record / arm | 2 s: clear track | SEQ REC | REC |
| HOME | TRACKS screen, again: visualiser | menu | PATCH | HOME |
| ENV | envelope pages (amp/filter shape) | — | CHORD | ENV |
| LFO | LFO pages (modulation) | — | NOTE REPEAT | LFO |
| FX | effect pages | punch-in FX on pads | FX | FX |
| SEL | key / chord pages | key layer (pads = piano: scale green, root brighter) | SCALE | SEL |
| EDIT | sound pages, drum kit | erase layer | PARA | EDIT |
| ARP | arp pages (MODE on K5: OFF switches the arpeggiator off) | note repeat (rolls) | ARP | ARP; LED lit while the selected track's arpeggiator runs |
| SEQ | step / pattern pages | step layer (steps 1–16) | SEQ | SEQ |
| GLO | global / master / system | mix layer (mute, solo, fill, tap) | GLOBE | GLO |
| SAVE | song screen / save | song layer (slots A–D: used green, free white) | BT | SAVE (SLOOP has no Bluetooth) |
| OCT− | octave down | drums: ghost hits | OCTAVE− | OCT− |
| OCT+ | octave up | drums: hard hits | OCTAVE+ | OCT+ |
| — | — | — | KNOB BANK | pins the pad sound picker open / closes it (lit while pinned or while K4 turns) |
| — | — | — | FADER BANK | the fader bank screen; pressed again, < > or SELECT: the next bank A→B→C→D (A→G in [organ mode](#organ-mode); closes after 4 s by default, HOME menu > FADERS > FADER SCREEN; moving a fader keeps it up; lit while up; closes the pad picker) |
| — | — | — | KNOB BANK + FADER BANK | the pads on the PLAY screen: DRUM KIT (default) ↔ STEPS (as the stock PAD BANK combo) |
| — | — | — | < | page back (as SELECT, never tempo); SEQ held or pads on STEPS: step page back; visualiser: style |
| — | — | — | > | page forward; SEQ held or pads on STEPS: step page forward; visualiser: style |
| — | — | — | < + > | SYNTH ↔ DAW mode ([DAW mode](#daw-mode)) |

## Organ mode

When the selected track plays the organ engine (DRAWBAR, sounds such as SOUL ORGAN or GOSPEL), the faders and the mod
wheel become organ controls for that track.

**FADER BANK** steps through seven banks instead of four, A → G. Banks A–D are the same as before. E–G set the
selected track's organ:

| Bank | FADER 1 | FADER 2 | FADER 3 | FADER 4 |
|---|---|---|---|---|
| E | 16' | 5 1/3' | 8' | 4' |
| F | 2 2/3' | 2' | 1 3/5' | 1 1/3' |
| G | 1' | PERC | DRIVE | ROTOR |

- A drawbar fader runs 0–8, bottom to top. Moving any drawbar fader switches the sound's REG to **CUSTM**: the nine
  bars start from the registration as it sounded (SUB / BODY / TOP folded in), then each fader sets its own bar.
  SUB / BODY / TOP on the knobs still shift the bars on top of that.
- CUSTM bars are kept with the project (and the autosave). User presets keep REG CUSTM but not the bar levels.
- The screen shows each fader's footage, its level and a bar. As with the other banks, a fader only takes over once
  it moves, and the faders wait to move again after a track change.
- Select a track with another engine and the banks go back to A–D (on bank E–G, it returns to A).

**ROTOR pages.** The EDIT family gets two more pages on an organ track, **ROTOR** and **ROTOR 2**, after EDIT 2
(EDIT pressed again, or SELECT / `<` `>`). They set the rotary speaker; at their defaults it sounds exactly as before.

| Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| ROTOR | SLOW: the horn's chorale speed, 0.2–2.0 Hz (0.8) | FAST: the horn's tremolo speed, 3.0–10.0 Hz (6.7) | ACCEL: how fast it changes speed (50 %: horn ~1 s, drum ~4 s; higher is quicker) | DEPTH: the amplitude throb (50 %) |
| ROTOR 2 | DRUM: the drum's speed, 50–120 % of the horn's (87 %) | BAL: + less drum, − less horn (0) | XOVER: where the horn takes over from the drum, 300–1500 Hz (800 Hz) | DOPL: the Doppler pitch wobble (50 %) |
| ROTOR 3 | DRAG: the brake, how fast STOP winds the rotors down: 0 % nearly free-running (the drum coasts for minutes), 100 % a hard brake (48 %: the horn ~2 s, the drum ~7 s) | | | |

The rotor settings belong to the track's part (like a cabinet: they stay when you change organ presets) and are kept
with the project and the autosave, not with user presets.

**MOD wheel = rotor switch**, three positions: bottom **STOP**, middle **SLOW** (chorale), top **FAST** (tremolo).
On STOP the rotors coast down at ROTOR 3 > DRAG's rate, the rotary sound with them, and the effect goes once both
stand still.
The rotors ramp between speeds the way a rotary speaker does: the horn in about 1 s, the drum in about 4 s. The
setting changes only when the wheel moves into another position, so a preset's rotor speed stays put until you
touch the wheel. Each change shows ROTOR: STOP / SLOW / FAST briefly. On the organ the mod wheel adds no vibrato.
HOME menu > FADERS > **ORGAN WHEEL** = VIBRATO gives the mod wheel its usual vibrato on the organ instead (the rotor
then stays on ROTR, the fader and the knob).

## Keys and pads

| FM-1 | SLOOP | SMK-37 | on the SMK-37 |
|---|---|---|---|
| 27 keys F3–G5 | notes; in layers: tiles (steps, FX, mix…) | keys F3–G5 (middle 27) | same, with velocity |
| — | — | keys C3–E3 (lowest 5) | synth tracks: notes; drum track: C3 kick, D3 snare, E3 hat (black keys = left white key) |
| — | — | keys G#5–C6 (highest 5) | synth tracks: notes; drum track: G#5 cowbell, A5 open hat, B5 crash, C6 ride |
| LEDs under 27 keys | what is on / playing | 16 RGB pads | PLAY screen: DRUM KIT (default; a pad a drum sound whatever track is selected, velocity, recorded into the drum track while recording; orange, dim / bright while it sounds) or STEPS (KNOB BANK + FADER BANK; the selected track's 16 steps of the page, top row 1–8: empty dim white, set in the track's colour, playhead white; tap sets / clears; drums: the sound last played). Layers: SEQ / FX / GLO / SAVE tiles, SCALE piano, EDIT / ARP held the white keys |

## Velocity

HOME menu (PATCH held) > PLAY > **KEY VELOCITY**, on K5 (before: MIDI, K8), sets how the 37 keys and the pads play. It applies to the
notes, their MIDI out and the drum levels; OCT− / OCT+ held still play ghost / hard drums. Kept with the settings.

| KEY VELOCITY | the notes play |
|---|---|
| DYNAMIC (default) | as played (the unit's factory velocity curves) |
| SOFT | louder for a light touch (32 → 56, 64 → 96) |
| HARD | loud needs a hard hit (64 → 32, 100 → 78) |
| FIXED 64 | always 64 (drums: soft) |
| FIXED 100 | always 100, as the FM-1's keys (drums: normal) |
| FIXED 127 | always 127 (drums: hard) |

## HOME menu: the SMK-37 settings

HOME menu (PATCH held; sections SCREEN, LIGHTS, AUDIO, PLAY, FADERS, MIDI, DAW, SYSTEM); SELECT walks the sections, the knobs set the rows (K5–K8, or K1–K4 with KNOB LAYOUT = SMK;
the screen names them), OCT+ steps the row under the cursor; SEQ PLAY and SEQ REC still start / stop and arm:

| Section | Row | Knob | Setting |
|---|---|---|---|
| SCREEN | KNOB LAYOUT | K7 | FM-1 (default): K1 MASTER, K2 SELECT, K3 ALGORITHM, K4 PRESETS, K5–K8 the dials. SMK: K1–K4 the dials, K5 MASTER, K6 SELECT, K7 PRESETS, K8 ALGORITHM. The knobs are endless: the new layout works at once. Switched to SMK, the menu's rows move to K1–K4 (K3 switches back) |
| LIGHTS | PAD LIGHT | K8 | how bright the 16 pads are: 100 % (default, as before), 75 %, 50 %, 25 %; right brighter, left dimmer, at once. The bright states follow it, the dim ones keep most of their light, so at 25 % a dim pad is still plain to see and still apart from a lit one (an empty step 40 → 25, a lit one 255 → 90 of 255) |
| AUDIO | LIMITER | K8 | SOFT (default): 9 dB more headroom at the power-on MASTER position, the limiter only near full scale, so a held chord keeps its level; MASTER's top as loud as before (turn it up for loudness). CLASSIC: SLOOP's limiter and MASTER curve |
| PLAY | KEY VELOCITY | K5 | see [Velocity](#velocity) |
| PLAY | BEND UP | K6 | the pitch wheel up, on the internal sounds (the selected synth track): OFF, 1, 2, 3, 4, 5, 7 or 12 semitones, default 7. MIDI IN's pitch bend has its own (GLO > MIDI 2 > BEND) |
| PLAY | BEND DOWN | K7 | the same, down (default 7; e.g. +2 up, −12 down). The earlier BEND RANGE is read as both |
| PLAY | KNOB ACCEL | K8 | SLOOP's (SCREEN on the FM-1): ON (default), a fast turn moves further; OFF, one value a detent |
| FADERS | FADER SCREEN | K5 | how long the FADER BANK screen stays up after the last bank change or fader move: 4 S (default), 10 S, HOLD (until PATCH closes it) |
| FADERS | ORGAN WHEEL | K6 | ROTOR (default): on an organ track the mod wheel is the rotor's STOP / SLOW / FAST switch; VIBRATO: the usual mod-wheel vibrato |
| MIDI | TRS MIDI OUT | K5 | ON (default) / OFF. The 3.5 mm jack sends what goes out over USB MIDI: the keys and pads (and the sequencer with GLO > SYSTEM > MIDI = SEQ, or DAW mode), on each track's MIDI channel, with or without a computer. NOT SENDING: the UART did not answer; set it again to retry |
| MIDI | MODE | K6 | SYNTH (default) / DAW ([DAW mode](#daw-mode); also `<` + `>`) |
| MIDI | CHANNEL | K7 | 1–16 (default 1): DAW mode's keys and every map entry on MAIN |
| MIDI | LOCAL | K8 | DAW mode: OFF (default; SLOOP silent) / ON (the keys play the selected track, NOTE pads the drum kit); also PATCH tapped |
| DAW | CC MAP | K5 | DEFAULT / USER (the map edited on the keyboard or in the web editor) |
| DAW | KNOBS | K6 | ABSOLUTE (0–127) / RELATIVE (64 ± the detents) |
| DAW | EDIT MAP | K7 | OCT+: the map editor (also PATCH held + OCTAVE+) |
| DAW | RESET MAP | K8 | how many entries differ from DEFAULT; OCT+ twice: USER = DEFAULT |

The jack is a MIDI **OUT** (UART1 on PC7, 31250 baud; there is no MIDI IN on the SMK-37). Connect it with a
TRS-to-DIN MIDI adapter. The SMK-37 is listed with a **type A** TRS jack (a community spec sheet; M-Vave's manual
only says "a 3.5 mm to MIDI adapter", and it has not been measured): if a synth does not respond, try a type B
adapter.

## DAW mode

**< + >** (the second pressed while the first is held) switches **SYNTH ↔ DAW**; so does HOME menu > MIDI > MODE.
A full screen says which. In DAW mode the keyboard is a MIDI controller (USB, and the TRS jack with TRS MIDI OUT on):
SLOOP's sequencer stops and the controls leave SLOOP alone. MIDI IN still plays SLOOP (a sound module for the DAW).

- **Keys:** notes on CHANNEL (HOME menu > MIDI, default 1), the lowest key C3 (48) at octave 0, velocity as KEY
  VELOCITY. OCTAVE− / OCTAVE+: the octave (±3; both: 0).
- **K1–K8** (as printed, whatever KNOB LAYOUT), **FADER 1–4**, the **16 pads**, **13 buttons** and the **wheels** send
  what the map says. KNOB BANK / FADER BANK: bank A / B of the knobs / faders (lit on B); both: the pads' bank.
  Knobs ABSOLUTE (0–127, kept per knob) or RELATIVE (64 ± the detents): HOME menu > DAW > KNOBS. `<` `>` send when
  let go. A knob, fader or wheel sends at most every 8 ms (the last value always); a CC TOGGLE flips once a press.
- **PATCH** tapped: **LOCAL** ON / OFF (PATCH lit: ON). ON: the keys also play the selected track, NOTE pads the drum
  kit (GM drum notes). OFF (default): SLOOP stays silent. PATCH held: the menu.
- **Screen:** DAW, CHANNEL, LOCAL; the last control touched (its name and bank, its message, the value, a bar); the
  fader and pad banks, the octave; the eight knobs as bars with their CCs.
- **Pads:** NOTE bank A blue, bank B green (dim, bright while held); CC yellow (dim, bright while held; a CC TOGGLE
  bright while on); OFF faint.

Default map (CC MAP DEFAULT). M-Vave's own DAW presets use Mackie Control on extra USB ports and its CC numbers are
not published, so the map uses free CCs (no bank select, data entry, volume, pan, pedals or channel mode). A pad or
button CC is 127 while held, 0 when let go.

| Control | Bank A | Bank B | Channel |
|---|---|---|---|
| K1–K8 | CC 70–77 | CC 102–109 | MAIN |
| FADER 1–4 | CC 20–23 | CC 24–27 | MAIN |
| PAD 9–16 (bottom row) | notes 36–43 | notes 52–59 | 10 |
| PAD 1–8 (top row) | notes 44–51 | notes 60–67 | 10 |
| `<` `>` | CC 110, 111 | | MAIN |
| SEQ PLAY, SEQ REC | CC 115, 117 | | MAIN |
| SEQ, FX, PARA | CC 112, 113, 114 | | MAIN |
| ARP, NOTE REPEAT | CC 118, 119 | | MAIN |
| SCALE, CHORD, GLOBE, BT | CC 85, 86, 87, 88 | | MAIN |
| Pitch wheel | pitch bend | | MAIN |
| Mod wheel | CC 1 | | MAIN |
| Keys | notes | | MAIN |

MAIN = HOME menu > MIDI > CHANNEL. **Your own map (USER):**

- **On the keyboard:** PATCH held + OCTAVE+ (or HOME menu > DAW > EDIT MAP). Touch a control (turn a knob, move a
  fader or a wheel, hit a pad, press a button), then KNOB 1 its TYPE (OFF, CC, CC TOGGLE, NOTE, MMC, PITCH BEND, as
  the control has them), KNOB 2 its NUMBER (CC, note or MMC command), KNOB 3 its CHANNEL (MAIN or 1–16); the screen
  names the knobs (K5–K7, or K1–K3 with KNOB LAYOUT = SMK). OCTAVE+: the next control; OCTAVE−: done. The banks
  switch as above. Editing puts CC MAP on USER.
- **In the web editor:** the DAW MAP tab (an SMK-37 with this firmware): the settings and the whole map; send, read,
  back to DEFAULT, save / open a file.
- HOME menu > DAW > **CC MAP**: DEFAULT / USER; **RESET MAP**: OCTAVE+ twice, USER = DEFAULT.

Kept in flash (0xB3000 / 0xB4000) with MODE, CHANNEL, LOCAL, CC MAP and KNOBS; without it: SYNTH, 1, OFF, DEFAULT,
ABSOLUTE.

## Hardware calibration

OCTAVE− + OCTAVE+ held at power-on (or HOME menu > SYSTEM > HARDWARE CALIBRATION): press each button and turn each
knob as the screen names it (K2 SELECT, K3 ALGORITHM, K4 PRESETS, K5–K8; with KNOB LAYOUT = SMK: K2–K4, K5 MASTER,
K6 SELECT, K7 PRESETS, K8 ALGORITHM: the same knobs, per printed knob), then the wheels, OCTAVE+ taking each step:

1. PITCH WHEEL, LET IT REST (its noise sets the dead band)
2. PITCH WHEEL, FULL DOWN (hold it there)
3. PITCH WHEEL, FULL UP
4. MOD WHEEL, AT THE BOTTOM
5. MOD WHEEL, FULL UP

The screen shows the wheel's reading. The full travel then gives the full range (BEND UP / DOWN, mod 0–127); at
power-on a pitch wheel resting near its taught rest is re-centred. 30 s without OCTAVE+ (or too little travel)
keeps the wheels as they were. Kept in flash with the settings (0xB1000 / 0xB2000); without it, or after updating,
the wheels calibrate themselves as before.

## The pad sound picker

Turning PRESETS (K4; K7 with KNOB LAYOUT = SMK) shows the sounds of the selected track's kind on the 16 pads and on the screen (8 × 2), in the
PRESETS order, 16 a page, then the user presets; on the drum track the drum kits. A pad loads its sound and plays a
short preview; < > step the pages. It closes by itself a moment after the last turn, unless KNOB BANK pinned it.
FADER BANK closes it (one screen at a time).

## Power-on and special combos (same as SLOOP on FM-1)

| Combo | Does |
|---|---|
| OCTAVE− held at power-on | USB rescue |
| OCTAVE− + OCTAVE+ held at power-on | hardware calibration (re-teach buttons, knobs and the wheels) |
| OCTAVE− + OCTAVE+ held 5 s, stopped | update mode (UBOOT) |
| KNOB BANK + FADER BANK (SMK-37) | the pads: DRUM KIT ↔ STEPS (DAW mode: the pads' bank A / B) |
| < + > (SMK-37) | SYNTH ↔ DAW mode |
| PATCH held + OCTAVE+ (SMK-37, DAW mode) | the DAW map editor |

## Safety

- **Back up the whole 1 MiB flash first**, over the chip's mask-ROM UBOOT mode (e.g. with `jl-uboot-tool`). The stock
  firmware has no way into UBOOT, so you need a USB_KEY dongle (as for the FM-1); with SLOOP installed, hold
  OCTAVE− and OCTAVE+ for 5 seconds while stopped. Keep the dump private: it holds data unique to your unit.
- **Never write 0xC0000–0xC1FFF** (Bluetooth pairing and the factory key / pad calibration, "trimdata") **or key_mac
  (0xFF000–0xFFFFF)**, and leave the stock resources (0xF4000–0xFDFFF) alone: they are unique to each unit and cannot
  be recreated. The firmware and its update loader write only an explicit allow-list of flash ranges (checked at
  compile time and again in the flash driver), but a manual UBOOT write can; restore only the ranges you need.
- `FELUCCA_UART` must stay 0 on the SMK-37 (PH8, the FM-1's MIDI IN, is a key-matrix row there): the build refuses
  otherwise. The SMK-37's TRS MIDI OUT is its own driver (`firmware/hal/smk_uart_tx.h`, `firmware/src/smk_midi_out.h`).
- Power the keyboard from a reliable USB port during an update and do not unplug it while the update screen is up.

## USB audio and remote MASTER

- **USB audio output:** the keyboard is a USB speaker too ("Speakers (Felucca)", 44.1 kHz stereo). The computer's sound is
  mixed after the synth at the MASTER level, times the computer's own volume slider (the keyboard's hardware volume, −60..0
  dB) and HOME menu > USB > USB IN LEVEL (OFF, −18, −12, −6, 0 dB; 0 dB by default). It follows the MASTER knob's
  travel in a straight line (30 % of the knob = 30 % of the level), not the knob's SOFT / CLASSIC curve. It is not sent
  back into the USB recording ("Line (Felucca)").
- **MASTER from the computer:** the Universal Real-Time SysEx Master Volume, `F0 7F 7F 04 01 <lsb> <msb> F7` (any device
  ID; the MSB sets the level, 0..127 = 0..100 % of the knob's travel), sets MASTER and shows it in the top bar. It holds
  until the MASTER knob is turned.
