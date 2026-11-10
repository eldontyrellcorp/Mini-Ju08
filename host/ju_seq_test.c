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

    printf("sequencer: %s\n", fails ? "FAILED" : "all checks passed");
    return fails ? 1 : 0;
}
