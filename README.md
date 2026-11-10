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
| Overflow, instability, stuck voices | 800 random patches, notes, effects and controllers under UBSan |
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
| ENV / LFO / SEQ / SCL | Q-Link groups: ENV-1 and ENV-2 / LFO / VCO / PLAY |
| EDIT / GLO / FX / SAVE | EDIT: the sound controls, one section per page (LFO, VCO MOD, VCO-1, VCO-2, MIX HPF VCA, VCF, VCF 2, ENV-1, ENV-2). GLO: the performance pages (assign, key mode, arpeggio, portamento, bender, LFO mod, master, MIDI). Press EDIT or GLO again for the next page. FX: effects. SAVE: patches |
| ARP | Arpeggio on / off |
| PLAY | All notes off |
| REC held + a knob | That dial back to the saved patch's value |
| OCT- / OCT+ | Octave of the keys. On the Patches page: back / load or write. Both together: octave 0 |
| OCT- + OCT+ held 5 s | UBOOT (the update mode), as in Mini-Tone |

Q-Link groups: FILTER, FILTER 2, ENV-1, ENV-2, VCO, LFO, EFFECTS, PLAY, four dials each. A red mark under a dial shows a value
that differs from the saved patch. On the Patches page: KNOB 1 moves by one, KNOB 2 by a row; OCT+ loads; select WRITE, OCT+,
pick a slot, OCT+ to save (OCT- cancels). Writes pause the audio briefly; the global settings save themselves two seconds after
the last change, once nothing sounds.

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
