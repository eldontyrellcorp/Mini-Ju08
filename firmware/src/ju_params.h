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

/* ---- effects (jupiter8_esp32/src/dsp/fx_params.h): CHORUS & MFX in series, then a delay and a reverb send */
enum {
    MFX_CHO1, MFX_CHO2, MFX_CHO3, MFX_DLY1, MFX_DLY2, MFX_DLY3, MFX_RDLY, MFX_OD, MFX_FUZZ,
    MFX_FATTEN, MFX_BITC, MFX_LOFI, MFX_PHA1, MFX_PHA2, MFX_FILT, MFX_PCH1, MFX_PCH2, MFX_COUNT
};
/* parameter positions in ju_fxpatch_t.m, by effect type */
enum { C1_MODE, C1_NOISE, C1_BAL, C1_LEV };
enum { C2_INT, C2_LO, C2_HI, C2_LEV };
enum { C3_MODE, C3_LO, C3_HI, C3_LEV };
enum { D1_SYNC, D1_TIME, D1_NOTE, D1_FB, D1_BAL, D1_LO, D1_HI, D1_LEV };
enum { D2_SYNC, D2_TIME, D2_NOTE, D2_FB, D2_DAMP, D2_PAN1, D2_PAN2, D2_LV1, D2_LV2, D2_LO, D2_HI, D2_BAL, D2_LEV };
enum { D3_LSYNC, D3_LTIME, D3_LNOTE, D3_RSYNC, D3_RTIME, D3_RNOTE, D3_FBMODE, D3_FB, D3_DAMP,
       D3_RSYNC2, D3_RATE, D3_RNOTE2, D3_DEPTH, D3_PHASE, D3_LO, D3_HI, D3_BAL, D3_LEV };
enum { RD_SYNC, RD_TIME, RD_NOTE, RD_FB, RD_BAL, RD_LO, RD_HI, RD_LEV };
enum { OD_DIST, OD_TONE, OD_LEV };
enum { FZ_DRIVE, FZ_TONE, FZ_LEV };
enum { FT_ODD, FT_EVEN, FT_LEV };
enum { BC_RATE, BC_BIT, BC_FILT, BC_LO, BC_HI, BC_LEV };
enum { LF_COMP, LF_LOFI, LF_FILT, LF_COFF, LF_LO, LF_HI, LF_BAL, LF_LEV };
enum { P1_SPEED, P1_DEPTH, P1_LO, P1_HI, P1_LEV };
enum { P2_STAGES, P2_MANUAL, P2_SYNC, P2_RATE, P2_NOTE, P2_DEPTH, P2_RESO, P2_MIX, P2_PAN, P2_LO, P2_HI, P2_LEV };
enum { SF_TYPE, SF_SLOPE, SF_CUT, SF_RES, SF_GAIN, SF_MOD, SF_WAVE, SF_SYNC, SF_RATE, SF_NOTE, SF_DEPTH, SF_ATK, SF_LEV };
enum { PS_CRS, PS_FINE, PS_SYNC, PS_TIME, PS_NOTE, PS_FB, PS_LO, PS_HI, PS_BAL, PS_LEV };
enum { V2_1CRS, V2_1FIN, V2_1SYNC, V2_1TIME, V2_1NOTE, V2_1FB, V2_1PAN, V2_1LEV,
       V2_2CRS, V2_2FIN, V2_2SYNC, V2_2TIME, V2_2NOTE, V2_2FB, V2_2PAN, V2_2LEV, V2_LO, V2_HI, V2_BAL, V2_LEV };
/* delay section */
enum { DL_SEND, DL_MODE, DL_SYNC, DL_TIME, DL_NOTE, DL_FB, DL_DAMP, DL_PAN1, DL_PAN2, DL_LV1, DL_LV2, DL_LEV, DL_RSEND, DL_COUNT };
enum { DLM_2TAP, DLM_PING };
/* reverb section */
enum { RV_SEND, RV_MODE, RV_TYPE, RV_PDLY, RV_TIME, RV_LEV,
       RS_LDAMP, RS_HDAMP, RS_TIME, RS_DENS, RS_PDLY, RS_MRATE, RS_MDEPTH, RS_PCH1, RS_PCH2, RS_FB1, RS_FB2, RS_LV1, RS_LV2, RS_LEV, RV_COUNT };
enum { RVM_STD, RVM_SHIMMER };
enum { RVT_ROOM1, RVT_ROOM2, RVT_HALL1, RVT_HALL2, RVT_PLATE };
#define JU_MFX_MAXP 20

typedef struct {
    uint8_t mfxType;
    uint8_t mfxOn, dlyOn, revOn;
    int16_t m[JU_MFX_MAXP];
    int16_t d[DL_COUNT];
    int16_t r[RV_COUNT];
} ju_fxpatch_t;

enum { FXK_NUM, FXK_ONOFF, FXK_LABELS, FXK_MS, FXK_PCT, FXK_DB, FXK_HZ100, FXK_PAN, FXK_CENTS, FXK_SEMI, FXK_SEC10, FXK_DEG };
typedef struct {
    const char *name;
    int16_t lo, hi, def;
    uint8_t kind;
    const char *const *labels;
} ju_fxparam_t;

typedef struct {
    char name[12];
    uint8_t v[P_COUNT];
    ju_fxpatch_t fx;                /* in Dual and Split, the Upper patch's effects apply to both */
} ju_patch_t;

const char *ju_mfx_name(int type);
int ju_mfx_nparams(int type);
const ju_fxparam_t *ju_mfx_param(int type, int i);
const ju_fxparam_t *ju_delay_param(int i);
const ju_fxparam_t *ju_reverb_param(int i);
void ju_fx_format(const ju_fxparam_t *d, int v, char *out, int n);
void ju_fx_load_mfx_defaults(ju_fxpatch_t *f, int type);
void ju_fx_defaults(ju_fxpatch_t *f);
int32_t ju_note_beats_q16(int idx);                 /* a note value (index 0-21: 64t .. 2 bars) in beats, Q16 */
