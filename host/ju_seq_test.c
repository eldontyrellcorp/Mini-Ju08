/* The sequencer in the engine (ju_dsp.c): timing against the tempo, rests, ties and the gate, two patterns of different
 * lengths, transposition from the keys, MIDI clock with start / stop, step and real-time recording. Notes are observed
 * through the voices' gates, block by block. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ju_api.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

typedef struct { int blk, note, on, part; } ev_t;
static ev_t ev[4096];
static int nev, blk;
static int prev[JU_NV];                                   /* the note a voice held last block, -1 none */
static void obs_reset(void) { nev = 0; blk = 0; for (int i = 0; i < JU_NV; i++) prev[i] = -1; }
static void block(void)
{
    int32_t u[32] = {0}, l[32] = {0};
    ju_prepare_block(); ju_render(u, l);
    for (int i = 0; i < JU_NV; i++) {
        int now = ju_voice_gate(i) ? ju_voice_note(i) : -1;
        if (now != prev[i]) {
            if (prev[i] >= 0) ev[nev++] = (ev_t){blk, prev[i], 0, i / 4};
            if (now >= 0) ev[nev++] = (ev_t){blk, now, 1, i / 4};
            prev[i] = now;
        }
    }
    blk++;
}
static double ms(int b) { return b * 32 * 1000.0 / 44100.0; }
static int count(int note, int on, int part) { int n = 0; for (int i = 0; i < nev; i++) n += ev[i].note == note && ev[i].on == on && (part < 0 || ev[i].part == part); return n; }
static int nth(int note, int on, int part, int k) { for (int i = 0; i < nev; i++) if (ev[i].note == note && ev[i].on == on && (part < 0 || ev[i].part == part) && k-- == 0) return ev[i].blk; return -1; }
static void setup(void)
{
    ju_init();
    ju_global[G_KEYMODE] = KM_2PART; ju_global[G_TEMPO] = 80;          /* 120 BPM */
    ju_patch[0].v[P_E2_R] = 0; ju_patch[1].v[P_E2_R] = 0;               /* releases end at once: the gate is what is seen */
    obs_reset();
}
static void pat(int p, int len, int gate, const int *notes, const char *types)
{
    ju_pat_t *t = &ju_pat[p][ju_seq_cur[p]];
    t->len = (uint8_t)len; t->gate = (uint8_t)gate;
    for (int i = 0; i < len; i++) { t->note[i] = (uint8_t)notes[i]; t->type[i] = types[i] == 'N' ? SQ_NOTE : types[i] == 'T' ? SQ_TIE : SQ_REST; }
}
static int near(double a, double b, double tol) { return a > b - tol && a < b + tol; }

int main(void)
{
    /* 1. 120 BPM: a sixteenth is 125 ms. N R N T, gate 3 ticks (62.5 ms) */
    setup();
    { int n[4] = {60, 0, 64, 0}; pat(0, 4, 3, n, "NRNT"); }
    ju_seq_req = 1;
    for (int i = 0; i < 2 * 44100 / 32; i++) block();
    CHECK(count(60, 1, 0) == 4 && count(64, 1, 0) == 4);                                  /* 2 s = 4 patterns of 0.5 s */
    CHECK(near(ms(nth(60, 1, 0, 1)) - ms(nth(60, 1, 0, 0)), 500, 2));                    /* one pattern: 500 ms */
    CHECK(near(ms(nth(60, 0, 0, 0)) - ms(nth(60, 1, 0, 0)), 62.5, 2));                    /* the gate: 3 ticks = 62.5 ms */
    CHECK(near(ms(nth(64, 1, 0, 0)) - ms(nth(60, 1, 0, 0)), 250, 2));                    /* the rest: the note on step 2 */
    CHECK(near(ms(nth(64, 0, 0, 0)) - ms(nth(64, 1, 0, 0)), 125 + 62.5, 2));             /* the tie: on through the next step, then the gate */
    CHECK(count(60, 1, 1) == 0);                                                          /* the other part is empty: silent */

    /* 2. tempo 90 BPM and the gate 6 (a whole step): 166.7 ms a step */
    setup(); ju_global[G_TEMPO] = 50;
    { int n[2] = {60, 62}; pat(0, 2, 6, n, "NN"); }
    ju_seq_req = 1;
    for (int i = 0; i < 44100 / 32; i++) block();
    CHECK(near(ms(nth(62, 1, 0, 0)) - ms(nth(60, 1, 0, 0)), 60000.0 / 90 / 4, 2));
    CHECK(near(ms(nth(60, 0, 0, 0)) - ms(nth(60, 1, 0, 0)), 60000.0 / 90 / 4, 2));        /* gate 100 %: off as the next one starts */

    /* 3. two parts, two lengths: 4 steps against 3 */
    setup();
    { int a[4] = {60, 61, 62, 63}, b[3] = {72, 73, 74}; pat(0, 4, 3, a, "NNNN"); pat(1, 3, 3, b, "NNN"); }
    ju_seq_req = 1;
    for (int i = 0; i < 3 * 44100 / 32; i++) block();                                     /* 3 s: 24 steps */
    CHECK(count(60, 1, 0) == 6 && count(72, 1, 1) == 8);
    CHECK(near(ms(nth(72, 1, 1, 1)) - ms(nth(72, 1, 1, 0)), 375, 2));

    /* 4. a key while it plays: the pattern is transposed (the key is the new root, C4 as written), the key does not sound */
    setup();
    { int n[2] = {61, 64}; pat(0, 2, 3, n, "NN"); }
    ju_seq_req = 1;
    for (int i = 0; i < 20; i++) block();
    ju_note_on_m(1, 67);                                                                  /* the key G4: +7 */
    for (int i = 0; i < 2 * 44100 / 32; i++) block();
    CHECK(count(67, 1, -1) == 0);                                                         /* the key itself is not played */
    CHECK(count(61 + 7, 1, 0) > 0 && count(64 + 7, 1, 0) > 0);                            /* the pattern, up a fifth */
    /* an empty part is played normally while the other runs */
    ju_note_on_m(2, 50); for (int i = 0; i < 5; i++) block();
    CHECK(count(50, 1, 1) == 1);

    /* 5. stop releases; start again begins at step 0 */
    ju_seq_req = 2; block(); block();
    CHECK(ju_voice_gate(0) == 0 && ju_voice_gate(1) == 0 && ju_voice_gate(2) == 0 && ju_voice_gate(3) == 0 && !ju_seq_run);
    for (int i = 0; i < 4; i++) ju_note_off_m(2, 50);

    /* 6. MIDI clock: 24 pulses a quarter note, steps every 6; nothing moves without them; start and stop */
    setup(); ju_global[G_SYNC] = 1;
    { int n[4] = {60, 62, 64, 65}; pat(0, 4, 3, n, "NNNN"); }
    ju_seq_midi(0xFA);
    for (int i = 0; i < 100; i++) block();                                                /* no clock: nothing */
    CHECK(count(60, 1, 0) == 0);
    for (int t = 0; t < 12; t++) { ju_seq_midi(0xF8); block(); }                          /* 12 pulses: steps 0 and 1 */
    CHECK(count(60, 1, 0) == 1 && count(62, 1, 0) == 1 && count(64, 1, 0) == 0);
    ju_seq_midi(0xFC); block();
    CHECK(!ju_seq_run && ju_voice_gate(0) == 0);
    for (int t = 0; t < 12; t++) { ju_seq_midi(0xF8); block(); }                          /* stopped: ignored */
    CHECK(count(64, 1, 0) == 0);
    ju_seq_midi(0xFB); ju_seq_midi(0xF8); block();                                        /* continue: the next step */
    CHECK(ju_seq_run);
    ju_global[G_SYNC] = 0; ju_seq_midi(0xFA);                                             /* clock ignored with the internal clock */
    CHECK(ju_seq_run == 1 && 1);                                                          /* (start ignored: it stayed as it was) */

    /* 7. step recording: REC armed, stopped: each key writes the cursor step and goes on; the key sounds */
    setup();
    ju_pat[0][0].len = 4;
    ju_seq_rec = 1; ju_seq_cursor[0] = 0;
    ju_note_on_m(1, 60); block(); ju_note_off_m(1, 60); ju_note_on_m(1, 62); block(); ju_note_off_m(1, 62); ju_note_on_m(1, 64); block();
    CHECK(ju_pat[0][0].note[0] == 60 && ju_pat[0][0].note[1] == 62 && ju_pat[0][0].note[2] == 64);
    CHECK(ju_pat[0][0].type[0] == SQ_NOTE && ju_pat[0][0].type[1] == SQ_NOTE && ju_pat[0][0].type[2] == SQ_NOTE && ju_pat[0][0].type[3] == SQ_REST);
    CHECK(ju_seq_cursor[0] == 3 && ju_seq_dirty[0]);
    CHECK(count(64, 1, 0) == 1);                                                          /* it sounded */
    ju_note_on_m(1, 65); ju_note_off_m(1, 65); ju_note_on_m(1, 67);
    CHECK(ju_pat[0][0].note[3] == 65 && ju_pat[0][0].note[0] == 67 && ju_seq_cursor[0] == 1);   /* wraps at the length */
    ju_note_off_m(1, 64); ju_note_off_m(1, 67);

    /* 8. real-time recording: a key held over two steps and a bit: note, tie, tie */
    setup();
    ju_pat[0][0].len = 8;
    ju_seq_rec = 1; ju_seq_req = 1;
    for (int i = 0; i < 10; i++) block();                                                 /* a few ticks into step 0 */
    while (ju_seq_pos[0] != 1) block();                                                   /* in step 1 from its start */
    ju_note_on_m(1, 70);                                                                  /* early in step 1 */
    for (int i = 0; i < 3 * 44100 / 32 / 8 * 2 + 40; i++) block();                       /* held about 2 steps */
    ju_note_off_m(1, 70);
    CHECK(ju_pat[0][0].type[1] == SQ_NOTE && ju_pat[0][0].note[1] == 70);
    CHECK(ju_pat[0][0].type[2] == SQ_TIE);
    for (int i = 0; i < 2 * 44100 / 32; i++) block();                                     /* and round again: the recorded note plays */
    ju_seq_rec = 0;
    obs_reset();
    for (int i = 0; i < 44100 / 32; i++) block();
    CHECK(count(70, 1, 0) >= 1);

    /* 9. the metronome: PLAY mode clicks every beat while it runs, accent on the bar's first beat; REC mode only while recording; OFF never */
    setup(); ju_global[G_METRO] = 2;
    { int n[4] = {60, 0, 0, 0}; pat(0, 4, 3, n, "NRRR"); }
    ju_seq_req = 1;
    for (int i = 0; i < 2 * 44100 / 32; i++) block();                                     /* 2 s at 120 BPM: 4 beats */
    CHECK(ju_click_count == 4);
    ju_seq_req = 2; block();
    setup(); ju_global[G_METRO] = 1; ju_seq_req = 1;
    for (int i = 0; i < 44100 / 32; i++) block();
    CHECK(ju_click_count == 0);                                                           /* REC mode, not recording */
    ju_seq_req = 2; block(); ju_seq_rec = 1; ju_seq_req = 1;
    for (int i = 0; i < 44100 / 32; i++) block();
    CHECK(ju_click_count == 2);
    ju_seq_req = 2; block();
    setup(); ju_global[G_METRO] = 0; ju_seq_req = 1;
    for (int i = 0; i < 44100 / 32; i++) block();
    CHECK(ju_click_count == 0);
    /* the click itself: a short burst that dies away, and only on the output it is mixed into */
    {
        int32_t o[2 * 64]; int peak0 = 0, peakLate = 0;
        setup(); ju_global[G_CLICKVOL] = 200; ju_click_trig(1);
        for (int k = 0; k < 40; k++) {
            memset(o, 0, sizeof o); ju_click_render(o, 64);
            for (int i = 0; i < 128; i++) { int a = o[i] < 0 ? -o[i] : o[i]; if (k < 4 && a > peak0) peak0 = a; if (k >= 36 && a > peakLate) peakLate = a; }
        }
        CHECK(peak0 > 2000 && peak0 <= 32767 && peakLate == 0);
        memset(o, 0, sizeof o); ju_click_render(o, 64);
        { int any = 0; for (int i = 0; i < 128; i++) any |= o[i]; CHECK(any == 0); }        /* nothing after it ended */
    }

    /* 10. count-in: REC armed, 2 beats: the metronome counts, then the sequencer starts on the next beat; keys are not recorded meanwhile */
    setup(); ju_global[G_COUNTIN] = 2; ju_global[G_METRO] = 0;
    { int n[4] = {60, 62, 64, 65}; pat(0, 4, 3, n, "NNNN"); }
    ju_seq_rec = 1; ju_seq_req = 1;
    block();
    CHECK(!ju_seq_run && ju_seq_count_beats_left() == 2 && ju_click_count == 1 && ju_click_accent == 1);
    ju_note_on_m(1, 90); ju_note_off_m(1, 90);                                            /* a key during the count-in */
    CHECK(ju_pat[0][0].type[0] == SQ_NOTE && ju_pat[0][0].note[0] == 60);                 /* not written */
    int cblk0 = blk;
    for (int i = 0; i < 44100 / 32 / 2 + 20; i++) block();                                /* a little over one beat */
    CHECK(ju_click_count == 2 && ju_click_accent == 0 && !ju_seq_run && ju_seq_count_beats_left() == 1);
    for (int i = 0; i < 44100 / 32 / 2; i++) block();                                     /* the second beat ends: the sequencer runs */
    CHECK(ju_seq_run);
    while (count(60, 1, 0) == 0 && blk < 4000) block();
    CHECK(near(ms(nth(60, 1, 0, 0) - cblk0 + 0), 1000, 4));                               /* step 0 comes two beats (1000 ms) after the start */
    CHECK(count(60, 1, 0) == 1 && ju_click_count == 2);                                   /* (metronome off: only the count-in clicked) */
    /* PLAY during the count-in cancels it */
    setup(); ju_global[G_COUNTIN] = 4; ju_seq_rec = 1; ju_seq_req = 1; block();
    CHECK(ju_seq_count_beats_left() == 4);
    ju_seq_req = 2; block(); block();
    CHECK(ju_seq_count_beats_left() == 0 && !ju_seq_run);
    /* no count-in when REC is not armed, or with 0 beats: it starts directly */
    setup(); ju_global[G_COUNTIN] = 3; ju_seq_rec = 0; ju_seq_req = 1; block();
    CHECK(ju_seq_run && ju_seq_count_beats_left() == 0);
    setup(); ju_global[G_COUNTIN] = 0; ju_seq_rec = 1; ju_seq_req = 1; block();
    CHECK(ju_seq_run);
    /* count-in on the MIDI clock: one beat is 24 pulses */
    setup(); ju_global[G_SYNC] = 1; ju_global[G_COUNTIN] = 1; ju_seq_rec = 1; ju_seq_req = 1; block();
    for (int k = 0; k < 23; k++) { ju_seq_midi(0xF8); block(); }
    CHECK(!ju_seq_run && ju_seq_count_beats_left() == 1);
    ju_seq_midi(0xF8); block(); CHECK(ju_seq_run);

    /* 11. undo: the last recording events, one by one, back to 32 */
    setup(); ju_pat[0][0].len = 8; ju_seq_rec = 1; ju_seq_cursor[0] = 0;
    for (int k = 0; k < 4; k++) { ju_note_on_m(1, 60 + k); block(); ju_note_off_m(1, 60 + k); }
    CHECK(ju_seq_undo_count() == 4 && ju_pat[0][0].type[3] == SQ_NOTE);
    ju_seq_op = 1; block();
    CHECK(ju_pat[0][0].type[3] == SQ_REST && ju_pat[0][0].type[2] == SQ_NOTE && ju_seq_undo_count() == 3 && ju_seq_cursor[0] == 3);
    ju_seq_op = 1; block(); ju_seq_op = 1; block();
    CHECK(ju_pat[0][0].type[0] == SQ_NOTE && ju_pat[0][0].type[1] == SQ_REST && ju_seq_undo_count() == 1);
    ju_seq_op = 1; block(); CHECK(ju_pat[0][0].type[0] == SQ_REST && ju_seq_undo_count() == 0);
    ju_seq_op = 1; block(); CHECK(ju_seq_undo_count() == 0);                              /* nothing more: no harm */
    /* an overwritten step comes back as it was */
    ju_pat[0][0].note[0] = 55; ju_pat[0][0].type[0] = SQ_NOTE; ju_seq_cursor[0] = 0;
    ju_note_on_m(1, 77); block(); ju_note_off_m(1, 77);
    CHECK(ju_pat[0][0].note[0] == 77);
    ju_seq_op = 1; block(); CHECK(ju_pat[0][0].note[0] == 55 && ju_pat[0][0].type[0] == SQ_NOTE);
    /* the history keeps the last 32 */
    ju_seq_cursor[0] = 0;
    for (int k = 0; k < 40; k++) { ju_note_on_m(1, 40 + k % 20); block(); ju_note_off_m(1, 40 + k % 20); }
    CHECK(ju_seq_undo_count() == 32);
    for (int k = 0; k < 40; k++) { ju_seq_op = 1; block(); }
    CHECK(ju_seq_undo_count() == 0);
    /* real time: a held key's ties are events too, undone one by one */
    setup(); ju_pat[0][0].len = 8; ju_seq_rec = 1; ju_seq_req = 1;
    for (int i = 0; i < 10; i++) block();
    while (ju_seq_pos[0] != 1) block();
    ju_note_on_m(1, 70);
    for (int i = 0; i < 3 * 44100 / 32 / 8 * 2 + 40; i++) block();
    ju_note_off_m(1, 70);
    { int ties = 0; for (int i = 0; i < 8; i++) ties += ju_pat[0][0].type[i] == SQ_TIE; CHECK(ties >= 2 && ju_seq_undo_count() == 1 + ties); }
    ju_seq_op = 1; block();
    { int ties = 0; for (int i = 0; i < 8; i++) ties += ju_pat[0][0].type[i] == SQ_TIE; CHECK(ties >= 1); }

    /* 12. erase a step: it and the ties that hang on it; undone together */
    setup(); ju_pat[0][0].len = 8;
    { int n[8] = {60, 0, 0, 62, 0, 0, 0, 0}; pat(0, 8, 3, n, "NTTNTRRR"); }
    ju_seq_op_part = 0; ju_seq_op_step = 1; ju_seq_op = 2; block();                       /* the first tie: it and the one after go, the note stays (shorter) */
    CHECK(ju_pat[0][0].type[0] == SQ_NOTE && ju_pat[0][0].type[1] == SQ_REST && ju_pat[0][0].type[2] == SQ_REST && ju_pat[0][0].type[3] == SQ_NOTE);
    ju_seq_op_step = 3; ju_seq_op = 2; block();                                           /* a note with its tie */
    CHECK(ju_pat[0][0].type[3] == SQ_REST && ju_pat[0][0].type[4] == SQ_REST);
    ju_seq_op_step = 6; ju_seq_op = 2; block();                                           /* a rest: nothing to erase, nothing to undo */
    CHECK(ju_seq_undo_count() == 4);
    ju_seq_op = 1; block();                                                               /* undo: the note and its tie together */
    CHECK(ju_pat[0][0].type[3] == SQ_NOTE && ju_pat[0][0].type[4] == SQ_TIE && ju_pat[0][0].note[3] == 62 && ju_pat[0][0].type[1] == SQ_REST);
    ju_seq_op = 1; block();                                                               /* and the first erase, both steps */
    CHECK(ju_pat[0][0].type[1] == SQ_TIE && ju_pat[0][0].type[2] == SQ_TIE && ju_seq_undo_count() == 0);

    printf("sequencer: %s\n", fails ? "FAILED" : "all checks passed");
    return fails ? 1 : 0;
}
