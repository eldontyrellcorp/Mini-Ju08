# Mini-Ju08

A Jupiter-8 style synthesizer for the **M-VAVE FM-1**, built on the platform code of [Mini-Tone](docs/Mini-Tone-README.md)
(Stephane Schmitz's firmware, itself based on Felucca by Leo Kuroshita, GPL-3.0-only) and on the synth and panel of
`jupiter8_esp32`. It keeps the FM-1's hardware layer, audio output, USB audio and USB/TRS MIDI, and replaces the sequencer,
the tracks, the old engines and the old screens with the Jupiter-8.

## Status: not yet run on a real FM-1

| Checked | How |
|---|---|
| The voice engine against the float original, all 64 patches | `host/build.sh`: levels within 0.4 dB, waveforms 17-33 dB SNR (four pitch-sweeping patches 5-11 dB) |
| Arpeggiator and Dual / Split stereo layers | same, 19-30 dB |
| All 17 CHORUS & MFX types, both delay modes, five reverbs and shimmer | block by block against the float original, 14-70 dB SNR, levels within 0.2 dB |
| The whole chain on the 56 patches that use effects | 24-32 dB SNR, levels within 0.05 dB |
| Parameter names, ranges, defaults and every displayed value | 39,000 values compared with the original (one differs: the original's float rounding) |
| Panel logic (every button, knob and mode) and every screen | scripted tests and PNG renders of all pages (`host/out/ui/`) |
| The sequencer: tempo, rests, ties, gate, two lengths, transposition, MIDI clock, both recording modes | `host/ju_seq_test.c`, timing checked in milliseconds |
| Overflow, instability, stuck voices | 500 random patches, notes, effects, controllers and sequences under UBSan |
| The firmware builds, links and passes the project's image checks | `./build.sh` |

**Not done:** running on hardware, listening tests, the CPU cost on the chip (see below), the web editor, comparison with a real Jupiter-8.
The update entry (M-UPGRADE, Mini-Tone's `ota.c`) is in, so `tools/fm1_install.py` can update a running Mini-Ju08 over USB; it has not been tried on a device either.

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
| EDIT / GLO / FX / SAVE | EDIT: the sound controls, one section per page (LFO, VCO MOD, VCO-1, VCO-2, MIX HPF VCA, VCF, VCF 2, ENV-1, ENV-2). GLO: the performance pages (assign, key mode, arpeggio, portamento, bender, LFO mod, master, MIDI). Press EDIT or GLO again for the next page. FX: effects. SAVE: patches |
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

One pattern for each part, so the two parts can play different sequences: up to 64 steps of a sixteenth note, one note per step, a
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
- **PLAY** starts and stops both parts together; a part whose pattern has no note is left to the keys.
- **While it plays, a key transposes** the pattern (the key is the new root: C4 plays it as written) and does not sound.
- **REC** arms recording of the part being edited. Stopped: each key you press sounds and is written to the cursor step, which moves
  on (step entry). Playing: the keys sound and are written to the nearest step; a key held over steps ties them (real time).
- **Tempo and clock:** the tempo is the global BPM (40-295, the same as the effects'). On the Performance page, SEQ CLOCK chooses
  INT or MIDI: with MIDI, the sequencer follows the clock (24 pulses a quarter note) and Start, Continue and Stop from USB or TRS
  MIDI, and PLAY only arms it.

### MIDI

USB MIDI and TRS MIDI in. Notes, pitch bend, mod wheel (CC 1), sustain (CC 64), all notes off, program change 0-63 to
patches 11-88, and the CC map of the ESP32 version (CC 20-56 and 59-90 for the sound, 91 / 94 / 93 for reverb send, delay send and
CHORUS & MFX on). In 2-Part mode the channels are set on the Performance page (MIDI group: CH1, CH2, OMNI or 1-16).

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
