/* SPDX-License-Identifier: GPL-3.0-only */
/* Jupiter-8 parameter model (from jupiter8_esp32/src/dsp/params.h: same ids, same order, same selector values,
 * so presets, saved patches and the CC map carry over). Plain C, shared by the firmware and the host tests. */
#pragma once
#include <stdint.h>

enum {
    P_LFO_RATE, P_LFO_DELAY, P_LFO_WAVE,
    P_VM_LFO, P_VM_ENV, P_VM_DEST, P_VM_PW, P_VM_PWMODE,
    P_V1_XMOD, P_V1_RANGE, P_V1_WAVE,
    P_V2_SYNC, P_V2_RANGE, P_V2_LOW, P_V2_TUNE, P_V2_WAVE,
    P_MIX, P_HPF,
    P_VCF_CUT, P_VCF_RES, P_VCF_SLOPE, P_VCF_ENV, P_VCF_ENVSEL, P_VCF_LFO, P_VCF_KEY,
    P_VCA_LEVEL, P_VCA_LFO,
    P_E1_A, P_E1_D, P_E1_S, P_E1_R, P_E1_KEY, P_E1_POL,
    P_E2_A, P_E2_D, P_E2_S, P_E2_R, P_E2_KEY,
    P_ASSIGN,
    P_COUNT
};

enum {
    G_VOLUME, G_BAL_U, G_BAL_L, G_TUNE,
    G_KEYMODE, G_PANEL, G_SPLIT, G_HOLD_U, G_HOLD_L,
    G_ARP_ON, G_ARP_MODE, G_ARP_RANGE, G_ARP_RATE,
    G_PORTA_MODE, G_PORTA_TIME,
    G_BEND_VCO, G_BEND_V1, G_BEND_V2, G_BEND_VCF, G_BEND_VCFSW,
    G_MOD_RISE, G_MOD_VCO, G_MOD_VCOSW, G_MOD_VCF, G_MOD_VCFSW,
    G_VINTAGE,
    G_TEMPO,
    G_CH_U, G_CH_L,     /* MIDI channel per part: 0 = OMNI, 1-16 */
    G_COUNT
};

enum { LFO_SINE, LFO_SAW, LFO_SQUARE, LFO_RANDOM };
enum { DEST_VCO1, DEST_BOTH, DEST_VCO2 };
enum { PW_LFO, PW_MAN, PW_ENV1 };
enum { V1_TRI, V1_SAW, V1_PULSE, V1_SQUARE };
enum { V2_SINE, V2_SAW, V2_PULSE, V2_NOISE };
enum { AS_SOLO, AS_UNISON, AS_POLY1, AS_POLY2 };
enum { KM_WHOLE, KM_DUAL, KM_SPLIT, KM_2PART };
enum { ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM };
enum { PM_OFF, PM_ON, PM_UPPER };

typedef struct {
    char name[12];
    uint8_t v[P_COUNT];
} ju_patch_t;
