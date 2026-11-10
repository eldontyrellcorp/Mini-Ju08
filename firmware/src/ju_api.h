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

/* sequencer */
extern ju_pat_t ju_pat[2][JU_SEQ_PATS];
extern uint8_t ju_seq_cur[2], ju_seq_pos[2], ju_seq_cursor[2];
extern volatile uint8_t ju_seq_run, ju_seq_rec, ju_seq_req, ju_seq_dirty[2];
int ju_seq_active(int); void ju_seq_defaults(void); void ju_seq_midi(uint8_t);
int ju_voice_gate(int); int ju_voice_note(int); void ju_all_off(uint8_t);
extern volatile uint32_t ju_click_count; extern uint8_t ju_click_accent; extern volatile uint8_t ju_seq_op, ju_seq_op_part, ju_seq_op_step;
void ju_click_render(int32_t *, uint32_t); int ju_seq_count_beats_left(void); int ju_seq_undo_count(void);
void ju_click_trig(int);
int ju_seq_tick_phase(void);
extern ju_song_t ju_song; extern volatile uint8_t ju_song_dirty, ju_song_row; extern uint8_t ju_song_sel;
int ju_seq_playing_pat(int); int ju_song_playing(void);
