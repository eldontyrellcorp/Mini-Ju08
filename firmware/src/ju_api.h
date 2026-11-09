/* SPDX-License-Identifier: GPL-3.0-only */
/* The engine's and the parameter tables' public symbols (ju_dsp.c, ju_fxdef.c, ju_pinfo.c), for the host tools that
 * build those files separately. In the firmware everything is one compilation unit and this is not needed. */
#pragma once
#include "ju_params.h"
#define JU_NV 8
#define JU_BLOCK 32
extern ju_patch_t ju_patch[2], ju_bank[64];
extern uint8_t ju_global[G_COUNT], ju_stereo_layers;
extern volatile uint8_t ju_load_req[2], ju_panic_req;
void ju_init(void); void ju_prepare_block(void); void ju_render(int32_t *, int32_t *); void ju_finish(const int32_t *, const int32_t *, int32_t *);
void ju_note_on(uint8_t, uint8_t); void ju_note_off(uint8_t, uint8_t); void ju_note_on_m(int, uint8_t); void ju_note_off_m(int, uint8_t);
int32_t ju_voice_level(int); int ju_part_mask(uint8_t);
const char *ju_pshort(int); const char *ju_plong(int); int ju_psteps(int); const char *ju_plabel(int, int);
int ju_cc_to_param(uint8_t); void ju_format_param(int, uint8_t, char *, int);
