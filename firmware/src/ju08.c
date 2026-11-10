/* SPDX-License-Identifier: GPL-3.0-only */
/* Mini-Ju08: a Jupiter-8 synthesizer for the M-VAVE FM-1. One compilation unit (the HAL is header-only), as
 * Mini-Tone's felucca.c, which it replaces: it keeps the HAL, the audio output, USB (MIDI and audio), TRS MIDI and
 * flash storage of Mini-Tone and drops the sequencer, the tracks and the old engines and screens. Each file uses
 * what the files above it define, so the order matters. */

#ifndef FELUCCA_FLASH
#define FELUCCA_FLASH 1
#endif
#ifndef FELUCCA_OTA
#define FELUCCA_OTA 1                /* the M-UPGRADE entry: the installer talks to the running firmware (no web editor) */
#endif
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 0
#endif
#ifndef FELUCCA_UAC
#define FELUCCA_UAC 1                /* USB audio input: the master output, stereo, 44.1 or 48 kHz */
#endif
#ifndef FELUCCA_UART
#define FELUCCA_UART 1               /* TRS MIDI IN on UART1 / PH8 */
#endif
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_900"
#endif
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "ju0.1"
#endif

#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"

#define FS 44100
#define CTL 32                       /* the engine's control block (JU_BLOCK) */
#define HALF_FRAMES 128              /* I2S half buffer: 2.9 ms */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")

#define JU_POOL __attribute__((section(".pool")))   /* ju_dsp.c: the bank goes in the pool */
#include "libc.c"
#include "lcd.c"
#include "usb.c"
#if FELUCCA_UART
#include "midi_uart.c"
#endif
#include "ju_main.c"
