# Mini-Ju08: porting the Jupiter-8 synth (jupiter8_esp32) to the M-VAVE FM-1

**Status (see README.md for the user view):** steps 1-4 below are done and tested on the host; the arpeggiator, stereo layers, all effects
and the panel are in. What remains is everything that needs the device: the real CPU cost, listening, and the web editor / update entry
(`FELUCCA_OTA=0` in `ju08.c`: Mini-Tone's OTA and editor are not included). The scope decisions below were the plan; "milestone 1" was extended to the full port on request.

Base: Mini-Tone 1.0.5 (GPL-3.0, itself from Felucca). Source of the synth and UI: `~/jupiter8_esp32` (ESP32-S3).

## Decisions (agreed)

- **Scope:** slim synth. The Jupiter-8 owns the whole firmware. Mini-Tone's sequencer, tracks, songs, projects, old engines and UI are dropped.
- **Milestone 1:** core voice only, one part with 8 voices: 2 VCO (sync, cross mod), HPF, 12/24 dB VCF, 2 ADSR, LFO, Solo/Unison/Poly-1/Poly-2,
  64 presets, Q-Link style UI. Then in order: 2 parts + Whole/Dual/Split, arpeggiator, portamento/bender, chorus, delay, reverb, MFX.
- **Testing:** host tests only for now (no FM-1 on hand). So: never touch loader/OTA/boot code, keep it byte-identical.

## Target constraints (measured, not guessed)

| | FM-1 (pi32v2) | ESP32-S3 (current Jupiter-8) |
|---|---|---|
| FP | **soft float** (every `*`/`+` on a float is a library call) | hardware FPU |
| Cores | 1, shared with USB/MIDI/key-scan ISRs | 2 |
| RAM | 72 KiB data + 360 KiB pool, flash XIP 581 KB (baseline image: 572 KB, nearly full) | 512 KB + 8 MB PSRAM |
| Screen | 240x240 ST7789 | 320x240 |
| Controls | KNOB 1-4, SELECT/MASTER/PRESETS/ALGORITHM knobs, 16 page buttons, 17 keys | 4 encoders with switches, 2 buttons |
| Sample rate | 44.1 kHz, I2S half buffer of 128 frames | 48 kHz |

Consequences:
1. `engine.cpp`, `fx.cpp`, `params.cpp` must be rewritten in **fixed point** (Q15/Q31, like Mini-Tone's engines). The float code stays as the reference.
2. The code is C (Mini-Tone is one C compilation unit, `felucca.c`); the Jupiter-8 sources are C++. Port to C, keep the algorithms.
3. Delays and reverbs have to fit about 300 KiB total: shorter lines and 16-bit/8-bit storage, no 2.6 s MFX or 1.3 s delay.

## Architecture

Keep (reuse unchanged): `hal/`, `crt0.S`, `app.ld`, `loader/`, `libc.c`, `lcd.c`, `gfx.c` (text/graphics, adapted), the audio ISR in `audio.c`, `usb.c` (USB audio + MIDI),
`midi_uart.c`, `storage*.c`, `ota*.c`, `master.c`, `editor`/web protocol (later, if wanted), `console.c`.

Replace: `core.h` types, `engines.c`, `eng_*.c`, `voice.c`, `fx.c`, `seq.c`, `song_chain.c`, `project.c`, `upreset.c`, `midi_control.c`, `panel.c` and all `ui*.c`.

New:
- `ju_dsp.c`: fixed-point voice, filter, envelopes, LFO, assigner (from `engine.cpp`)
- `ju_params.c`, `ju_presets.c`: parameter table and the 64 patches (from `params.cpp`, `presets.cpp`)
- `ju_midi.c`: MIDI in (USB + TRS) to the assigner, the CC map from the Jupiter-8 README
- `ju_ui.c`: Q-Link pages on 240x240 with the FM-1 knobs and buttons
- `ju_fx.c` (later milestones)
- `host/`: PC build of the DSP with the same sources as the firmware, plus tests that compare the fixed-point render against the float reference

## Steps

1. Slim build: new top-level unit that still builds `.fwsc`, boots, shows a screen, plays a test tone through the audio ISR, answers MIDI.
2. Fixed-point voice + host harness (render to WAV, SNR against the float engine on every preset).
3. Wire MIDI in to the voice, 8 voices, measure cost per voice from the audio ISR's CPU meter (`felucca_dbg.cpu_q8`) and the static `tests/target_budget.py` method.
4. Q-Link UI, presets, patch storage in flash.
5. Later milestones as listed above.

## Open risks

- Cost of a 4-pole OTA ladder with tanh per stage in fixed point at 44.1 kHz and 8 voices. Plan B: tanh only on the first and last stage, or 4 voices.
- Flash is nearly full today, but it frees up once the 20 old engines are removed.
- No on-device measurement is possible yet, so CPU numbers come from instruction counts only (the same way `tests/target_budget.py` does it).
- Jupiter-8 license: `jupiter8_esp32` has no license file here; the combination with GPL-3.0 Mini-Tone code needs the author's confirmation.
