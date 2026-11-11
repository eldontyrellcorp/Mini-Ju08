# Mini-Ju08

A Jupiter-8 style synthesizer for the **M-VAVE FM-1**, built on the platform code of [Mini-Tone](docs/Mini-Tone-README.md)
(Stephane Schmitz's firmware, itself based on Felucca by Leo Kuroshita, GPL-3.0-only) and on the synth and panel of
`jupiter8_esp32`. It keeps the FM-1's hardware layer, audio output, USB audio and USB/TRS MIDI, and replaces the sequencer,
the tracks, the old engines and the old screens with the Jupiter-8.

**Compatible with both FM-1 hardware versions**: the original board (Puya P25Q80 flash) and the newer one (UC25HQ80 flash). See [Two board versions](#two-board-versions-flash-chip).

## What it does

Two parts of four voices each (or eight voices on one patch), 44.1 kHz. Two VCOs per voice with sync and cross mod, HPF,
12/24 dB ladder VCF, two ADSRs, LFO, Solo / Unison / Poly-1 / Poly-2, Whole / Dual / Split / 2-Part, arpeggiator, polyphonic
portamento, bender and mod wheel, 64 patches numbered 11-88. CHORUS & MFX (17 types), a delay (2-tap or ping-pong) and a
reverb (five types and shimmer) as in the ESP32 version. See `~/jupiter8_esp32/README.md` for the synth's own documentation
(accuracy notes, what is modelled and what is estimated); this port keeps its algorithms and values.

### Differences from the ESP32 version

- **Fixed point.** The FM-1's chip has no FPU, so the engine and the effects are in integers (`ju_dsp.c`, `ju_fx.c`).
- **No second core.** Everything runs in the audio interrupt; the reverb runs at half the sample rate as before.
- **Shorter effect memory** (no PSRAM): MFX lines 0.59 s a channel, delay 0.68 s, reverb as before. The longest delay times of the
  Roland ranges (up to 2.6 s) are cut to what fits.
- **No BLAMP on the triangle** (its corners alias a little at the top of the keyboard); the pulse is built from two sawtooths.
- **The reverb's rooms are 8 % larger** than at 48 kHz (the line lengths are the original's, in samples).
- **Screen** 240 x 240 instead of 320 x 240; the panel is drawn in 20-line bands (no frame buffer).
- **44.1 kHz** instead of 48 kHz.

## Controls

| Control | Does |
|---|---|
| KNOB 1-4 | The four dials of the Q-Link page. On the other pages: KNOB 1 chooses (and carries on into the next page), KNOB 2 sets, KNOB 3 jumps a page, KNOB 4 steps the patch |
| SELECT | The Q-Link group (and back to the Q-Link page) |
| PRESETS | Patch, 11-88 |
| ALGORITHM | Upper / Lower part (the keys play the part being edited) |
| MASTER | Volume |
| HOME | Q-Link page; again: next group |
| ENV / LFO / SCL | Q-Link groups: ENV-1 and ENV-2 / LFO / PLAY (the VCO group: HOME or SELECT) |
| EDIT / GLO / FX / SAVE | EDIT: the sound controls, one section per page (LFO, VCO MOD, VCO-1, VCO-2, MIX HPF VCA, VCF, VCF 2, ENV-1, ENV-2). GLO: the performance pages (assign, key mode, arpeggio, portamento, bender, LFO mod, master, MIDI). Press EDIT or GLO again for the next page. GLO after the last page: **About** (firmware version 1.00 and a QR code of this repository; GLO again to leave). FX: effects. SAVE: patches (again: projects) |
| ARP | Arpeggio on / off |
| SEQ | The sequencer; again: its setup |
| PLAY | Start / stop the sequencer. Held 0.6 s: all notes off |
| REC | Tapped: arm recording. (Held with a knob: that dial back to the saved value) |
| OCT- / OCT+ | Octave of the keys. On the Patches page: back / load or write. Both together: octave 0 |
| OCT- + OCT+ held 5 s | UBOOT (the update mode), as in Mini-Tone |

Q-Link groups: FILTER, FILTER 2, ENV-1, ENV-2, VCO, LFO, EFFECTS, PLAY, four dials each. A red mark under a dial shows a value
that differs from the saved patch. On the Patches page: KNOB 1 moves by one, KNOB 2 by a row; OCT+ loads; select WRITE, OCT+,
pick a slot, OCT+ to save (OCT- cancels). Writes pause the audio briefly; the global settings save themselves two seconds after
the last change, once nothing sounds.

### Sequencer

One pattern for each part, so the two parts can play different sequences: up to 64 steps of a sixteenth note, a note or chord per step, a
rest or a **tie** (the note keeps sounding through the step). 16 patterns to choose from per part, saved in flash two seconds after
a change (when it is stopped and nothing sounds). It plays through the part's voices, so the patch's assign mode and portamento apply.

- **SEQ** opens a **piano roll** (after Sloop's STEP page): the notes as bars, pitch up and the steps along, 16 steps at a time (a 64-step
  pattern is four windows that follow the cursor) and 22 semitones that follow the note under the cursor, with a keyboard strip, beat
  lines and a ruler. A note lasts as many steps as its bar is long (a tie is just a longer bar); the last step of a bar lasts the gate
  only. The step playing is a red line, the cursor a crosshair. Above the roll: STEP, NOTE, LEN (the note's length in steps) and the
  PATTERN. KNOB 1 moves the cursor, KNOB 2 sets the pitch (turning on a rest makes a note), KNOB 3 the length of the note (up to the
  next note; 0 deletes it), KNOB 4 the pattern. **SCL** deletes the note under the cursor.
- **SEQ again** opens the setup: four large values, KNOB 1 the length of the pattern (1-64), KNOB 2 the gate (how long a note lasts in
  its last step, 17 to 100 %), KNOB 3 the tempo, KNOB 4 the pattern. OCT- twice clears the pattern.
- **Erase a step:** **SCL** erases the step under the cursor and the ties hanging on it (so on a tie it shortens the note; on the note it
  removes the whole note). **Undo:** **EDIT** on the sequencer page undoes the last of the 32 most recent changes of recording (each
  recorded note, each tie a held key adds) and of erases, one at a time; the readout shows how many are left. A step erased by mistake is
  undone the same way; changes made with the knobs are not in the history.
- **Quantize** (GLO, the RECORDING page, QNT): while recording in real time, a key snaps to the nearest point of a grid: 1/16 (every step, the
  default), 1/8, 1/4 or 1/2 note. Step entry is not affected (a key writes the cursor step).
- **Metronome and count-in** (GLO, the RECORDING page): MET is OFF, REC (clicks while recording) or PLAY (whenever the sequencer runs); CNT is
  the count-in, 0 to 8 beats; CLK the level. With REC armed and a count-in, **PLAY counts first** (the header shows IN 4, 3, 2, 1) and the
  sequencer starts on the next beat, so recording does not begin directly; the keys sound during the count-in but are not recorded, and PLAY
  cancels it. The first beat of each bar clicks higher. The click is heard in the headphones and speaker, not in the USB audio.
- **Song** (press SEQ twice more: the roll, the setup, the song): a list of up to 32 rows, each naming a pattern for the upper part and one for
  the lower part (`--`: none, the keys have that part) and a number of repeats (1-16). A row lasts its repeats of the longer of its two patterns
  (a shorter one wraps), and both parts change together at the end of the row. KNOB 1 moves along the rows (the empty row after the last makes a
  new one), KNOB 2 sets the upper pattern, KNOB 3 the lower, KNOB 4 the repeats; **SCL** deletes the row, **OCT+** inserts a copy of it, **OCT-**
  switches song mode on and off (also GLO, SEQUENCER page: SNG, and LUP for looping). With song mode on, **PLAY** plays the song from the row
  under the cursor (the header shows SNG and the row); it stops after the last row, or starts again with LUP on. Recording during a song
  overdubs into the pattern the row is playing. The song is saved in flash like the patterns.

- While a song plays, the roll and the step pages show the patterns the song is playing.
- **Step details** (press SEQ again after the roll: two more pages, then the setup and the song). They edit the step under the cursor (KNOB 1
  moves it; SCL erases it):
  - page 1: KNOB 2 **velocity** (ghost, soft, normal, accent: the VCA level, 0.3 / 0.6 / 1 / 1.4), KNOB 3 **probability** (25, 50, 75, 100 % chance
    that the step plays), KNOB 4 **ratchet** (x1 to x4: the notes repeat inside the step, with a short gate);
  - page 2: KNOB 2 **chord** (14 presets: 5th, octave, major, minor, sus2, sus4, dim, aug, maj7, min7, dom7, maj6, add9; a chord played in
    step recording or in real time shows as CUSTOM), KNOB 3 **lane**, KNOB 4 the lane's value at this step.
  - A **chord**: up to four notes, the root and three within 15 semitones above it. In recording, a key pressed while another key of the same step is
    held joins its chord. The roll draws the extra notes in grey; a red, yellow or blue mark under a step says it has a probability, a ratchet or
    a lane value.
  - A **lane** (one per pattern) moves one sound parameter from step to step: cutoff, resonance, HPF, mix, cross-mod, VCO-2 tune, pulse width,
    LFO rate, VCO / VCF LFO depth, VCF envelope depth or VCA level. A step with a value sets the parameter to it (the sound's own value stays as it
    was, and comes back when the sequencer stops); a step without one leaves the sound's own value. KNOB 4 starts a value at the sound's own value.
  - **Swing** (GLO, SEQUENCER page: SWG): every second sixteenth is late by 1, 2 or 3 of its 6 clock ticks (58, 67, 75 %).
  - The velocity of keys is not recorded (the keys have none): set it per step. MIDI velocity is ignored by the sound, as on the Jupiter-8.
- **Tools** (the last page of the SEQ cycle): whole-pattern operations on the pattern being edited. KNOB 1 picks the tool, KNOB 2 sets the
  amount of TRANSPOSE (semitones) and SHIFT (steps, wrapping), **OCT+** does it. COPY and PASTE (one clipboard, any pattern of either part),
  COPY TO NEXT (the pattern goes into the next slot, which becomes the current one, to make a variation), TO OTHER PART (the same slot of the
  other part), TRANSPOSE, SHIFT, DOUBLE (the pattern repeated to twice its length, up to 64), HALVE, and UNDO TOOL (takes back the last
  tool; again: redoes it).
- **MIDI out** (GLO, MIDI page: MOU): the sequencer's notes go out on USB MIDI, on each part's channel (OMNI: 1 for Upper, 2 for Lower), with
  the step's velocity (ghost 36, soft 64, normal 100, accent 127) and every note of a chord. **N+CLK** also sends the clock (24 a quarter note),
  start and stop, when the internal clock is selected. The 5-pin TRS port has no transmit driver in this firmware: it is MIDI in only.
  **Song position pointer** is followed with the MIDI clock selected: a DAW that jumps (then continues) makes the pattern play from that
  sixteenth. (In song mode the song plays from its own row.)
- **Projects** (SAVE twice: the Patches page, then the Projects page): 8 numbered slots P01-P08. A project is the two sounds (with their
  effects), all 32 patterns, the song, and the musical settings (tempo, key mode and split, balance, arpeggio, portamento, the two MIDI
  channels, song mode and loop). Nothing is saved until you ask: KNOB 1 picks the slot, **OCT+** saves into it (asks again before
  overwriting another project), **OCT-** loads it (asks again if the current work is unsaved), **SCL** clears it (asks again). The open
  project is marked in red, with a `*` while the working copy differs from what was saved. Saving and loading need the sequencer
  stopped. A project is stored compactly (a pattern takes about as many bytes as it has steps with something in them); a project that does not fit its slot (32 patterns of 64 steps, every step with everything) is refused with a message. The working copy (sounds as edited, patterns, song, settings) is also kept in flash by itself, so a power cycle brings back
  what you were doing, whatever slot is open.
  **GLO, STORAGE page: AUT** turns this autosave off (default on). Off, nothing is written to flash by itself, so no write can
  interrupt the sound; patterns, song and edited sounds are then kept only by saving a project (or writing a patch). The option itself is saved.
- **PLAY** starts and stops both parts together; a part whose pattern has no note is left to the keys.
- **Keys while it plays** (GLO, the SEQUENCER page, KEY): **PLAY** (the default) the keys sound normally over the running pattern; **TRAN** a key
  transposes the pattern instead (the key is the new root: C4 plays it as written) and does not sound. The keys play the part being edited, so
  with a pattern on one part, ALGORITHM switches to the other part to play on voices of its own.
- **REC** arms recording of the part being edited. Stopped: each key you press sounds and is written to the cursor step, which moves
  on (step entry). Playing: the keys sound and are written to the nearest step; a key held over steps ties them (real time).
- **Tempo and clock:** the tempo is the global BPM (40-295, the same as the effects'). On the Performance page, the SEQUENCER page's SYN chooses
  INT or MIDI: with MIDI, the sequencer follows the clock (24 pulses a quarter note) and Start, Continue and Stop from USB or TRS
  MIDI, and PLAY only arms it.

### MIDI

USB MIDI and TRS MIDI in. Notes, pitch bend, mod wheel (CC 1), sustain (CC 64), all notes off, program change 0-63 to
patches 11-88, and the CC map of the ESP32 version (CC 20-56 and 59-90 for the sound, 91 / 94 / 93 for reverb send, delay send and
CHORUS & MFX on). In 2-Part mode the channels are set on the Performance page (MIDI group: CH1, CH2, OMNI or 1-16).

## Two board versions (flash chip)

Mini-Ju08 runs on **both** versions of the FM-1: the original board and the newer one (recognisable by an M-VAVE logo under the screen).

The newer units carry a different 1 MiB flash chip (UC25HQ80, JEDEC id `0xB36014`) instead of the original Puya P25Q80 (`0x856014`); both use the same
SPI commands. A firmware that only knows the first one treats the storage as absent on the newer boards: nothing is saved (it vanishes at power-off)
and updates over USB are refused. Mini-Ju08 recognises both chips, so patterns, projects, settings and USB updates work on either board.

The About screen (GLO past the last performance page) shows the chip it found: `FLASH 856014` or `FLASH B36014`. `FLASH NONE` in red means no known chip
answered and nothing is saved. If saving ever fails, that line and the "SAVE FAILED" message of the Projects page are the first things to look at.

## Demonstration video

`python3 tools/make_demo.py` renders a Full HD video (about two minutes): a tour of eight patches, then a Berlin-School piece played by the sequencer
in song mode. The sound and the screen come from the firmware's own engine and panel UI (the host build, `host/ju_demo.c`), so it is what the
sources do, not a recording of a device. Needs gcc, ffmpeg, Pillow and numpy; the result goes to `docs/Mini-Ju08-demo.mp4` (not in the repository).

## Building

The toolchain and SDK are those of Mini-Tone (see [BUILDING.md](BUILDING.md): the JieLi toolchain in `~/.jieli/toolchain`, the AC79 SDK
in `~/fw-AC79_AIoT_SDK`).

```
./build.sh                 # build/felucca.fwsc: the package to install
host/build.sh              # the host tests (needs ~/jupiter8_esp32, or JU_REF=...)
FW_MAIN=felucca.c ./build.sh   # Mini-Tone itself
```

The package keeps Mini-Tone's identity string (`FM-1_900`): installing it replaces whatever FM-1 firmware is on the device. Installing
firmware is at your own risk; if an install fails and the FM-1 no longer starts, recovery needs
[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter). A first build (before 2026-11-10) had no update entry: a device running it cannot be updated over USB and needs the chip's boot mode (OCT- + OCT+ held 5 s) and `tools/ju_uboot_flash.sh`, which writes the app area from Linux through
[jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) (backup first; the tool lists this chip family as untested, so its first step only reads),
or FM-1-transporter (see Mini-Tone's README, "If the FM-1 does not start"). The device shows up as "Mini-Ju08" over USB (the update loader as "Felucca Update"); `tools/fm1_install.py` matches both.

## CPU

Static counts of the compiled code (not measurements): about 350 instructions per voice per sample, 8 voices about 2800; the
reverb about 320, a chorus 100-250, the delay about 150. At 44.1 kHz that is up to ~150 million instructions a second with
everything on. The chip's real clock and speed under this load are not known yet: the header shows the render's share of
each half buffer ("CPU"), and after two half buffers over 85 % one voice is faded out, and one more each half while it lasts.
If it is too much on the device: lower `JU_NV` (voices) in `ju_dsp.c`, or use lighter effects.

## Source

| File | |
|---|---|
| `firmware/src/ju08.c` | the top-level unit (include order matters) |
| `ju_dsp.c`, `ju_fx.c` | engine and effects, fixed point, no dependencies (shared with the host tests) |
| `ju_params.h`, `ju_pinfo.c`, `ju_fxdef.c`, `ju_presets.h` | parameters, names, displays, the 64 patches |
| `ju_tables.h` | generated by `tools/gen_ju_tables.py` |
| `ju_ui.c`, `ju_canvas.c` | the panel and its drawing |
| `ju_audio.c`, `ju_midi.c`, `ju_store.c`, `ju_main.c` | the audio and timer interrupts, MIDI and keys, flash, boot and the main loop |
| `usb.c`, `midi_uart.c`, `storage*.c`, `lcd.c`, `libc.c`, `hal/` | Mini-Tone's platform code, unchanged |
| `host/` | the tests and tools against the float original |

## Licence

GPL-3.0-only, as Mini-Tone (see [LICENSING.md](LICENSING.md)). The Jupiter-8 synth and panel are ported from `jupiter8_esp32`, whose
licence is not stated in that tree: it needs the author's confirmation before this is published.
