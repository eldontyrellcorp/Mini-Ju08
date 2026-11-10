/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI in (USB and TRS: the packets usb.c queues) and the keys, into the Jupiter-8 engine. Runs in the audio ISR at the
 * start of each half buffer, the context of the engine, so no event queue is needed. After jupiter8_esp32/src/app.cpp
 * (the CC map) and midi.cpp. The CC numbers are the ones in jupiter8_esp32's README. */
static volatile int8_t ju_pc_idx[2] = {-1, -1};       /* a program change loaded this patch into a part: the UI follows */
static int8_t ju_octave;                              /* of the keys, -3..+3 (the OCT buttons) */
static uint8_t kb_boot_hold;                          /* under the splash the keys do not sound */

static void ju_midi_cc(uint8_t ch, uint8_t cc, uint8_t value)
{
    const int mask = ju_part_mask(ch);
    int id;
    uint8_t v;
    if (!mask) return;
    if (mask & 1) {                                   /* the effects are shared and stored with part 1: its channel controls them */
        ju_fxpatch_t *fx = &ju_patch[0].fx;
        switch (cc) {
        case 91: fx->r[RV_SEND] = value; fx->revOn = value > 0; return;
        case 94: fx->d[DL_SEND] = value; fx->dlyOn = value > 0; return;
        case 93: fx->mfxOn = value >= 64; return;
        default: break;
        }
    }
    id = ju_cc_to_param(cc);
    if (id < 0) return;
    v = ju_psteps(id) ? (uint8_t)(((int)value * ju_psteps(id)) / 128) : (uint8_t)(value * 2 + (value >> 6));
    if (id >= P_COUNT) { ju_global[id - P_COUNT] = v; return; }
    if (ju_global[G_KEYMODE] == KM_2PART) {
        if (mask & 1) ju_patch[0].v[id] = v;
        if (mask & 2) ju_patch[1].v[id] = v;
    } else {
        ju_patch[ju_global[G_PANEL] & 1].v[id] = v;
    }
}

static void ju_midi_program(uint8_t ch, uint8_t p)
{
    const int mask = ju_part_mask(ch);
    if (p >= 64 || !mask) return;
    if (ju_global[G_KEYMODE] == KM_2PART) {
        if (mask & 1) { ju_load_req[0] = (uint8_t)(p + 1); ju_pc_idx[0] = (int8_t)p; }
        if (mask & 2) { ju_load_req[1] = (uint8_t)(p + 1); ju_pc_idx[1] = (int8_t)p; }
    } else {
        int l = ju_global[G_PANEL] & 1;
        ju_load_req[l] = (uint8_t)(p + 1);
        ju_pc_idx[l] = (int8_t)p;
    }
}

static void ju_midi_event(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    switch (st) {
    case 0x90u:
        if (d2) ju_note_on((uint8_t)ch, (uint8_t)d1); else ju_note_off((uint8_t)ch, (uint8_t)d1);
        break;
    case 0x80u: ju_note_off((uint8_t)ch, (uint8_t)d1); break;
    case 0xB0u:
        if (d1 == 1u) ju_modwheel_set((uint8_t)ch, (uint8_t)d2);
        else if (d1 == 64u) ju_sustain_set((uint8_t)ch, (uint8_t)d2);
        else if (d1 == 120u || d1 == 123u) ju_all_off((uint8_t)ch);
        else ju_midi_cc((uint8_t)ch, (uint8_t)d1, (uint8_t)d2);
        break;
    case 0xC0u: ju_midi_program((uint8_t)ch, (uint8_t)d1); break;
    case 0xE0u: ju_bend((uint8_t)ch, (int)((d2 << 7 | d1)) - 8192); break;
    default: break;
    }
}

static void ju_midi_drain(void)
{
    while (mi_r != mi_w) {
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], status = (pkt >> 8) & 0xFFu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        mi_r++;
        if (status >= 0xF8u) { ju_seq_midi((uint8_t)status); continue; }     /* clock, start, continue, stop: the sequencer (MIDI clock selected) */
        ju_midi_event(status & 0xF0u, status & 0x0Fu, d1, d2);
    }
    if (midi_in_overflow) {                           /* a broken stream: release everything and start again */
        midi_in_overflow = 0;
        mi_r = mi_w;
        ju_all_off(0xFF);
    }
}

/* the 27 keys, F3 to G5, play the part being edited (both parts in the other key modes) */
static uint32_t kb_prev;
static int8_t kb_note[27];
static uint8_t kb_pmask[27];
static void ju_keys_block(void)
{
    uint32_t cur = fm1_in.notes, ch = cur ^ kb_prev, k;
    if (!ch) return;
    for (k = 0; k < 27u; k++) {
        if (!((ch >> k) & 1u)) continue;
        if ((cur >> k) & 1u) {
            int n = 53 + (int)k + 12 * ju_octave;
            if (kb_boot_hold) { kb_note[k] = -1; continue; }
            n = n < 0 ? 0 : n > 127 ? 127 : n;
            kb_note[k] = (int8_t)n;
            kb_pmask[k] = ju_global[G_KEYMODE] == KM_2PART ? (uint8_t)(1u << (ju_global[G_PANEL] & 1u)) : 3u;
            ju_note_on_m(kb_pmask[k], (uint8_t)n);
        } else if (kb_note[k] >= 0) {
            ju_note_off_m(kb_pmask[k], (uint8_t)kb_note[k]);
            kb_note[k] = -1;
        }
    }
    kb_prev = cur;
}
