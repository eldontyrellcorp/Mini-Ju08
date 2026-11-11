/* A demonstration, rendered offline with the firmware's own engine and panel UI (the same sources as the device): some sounds, then a
 * Berlin-School piece played by the sequencer in song mode. Writes the audio (raw 16-bit stereo, 44.1 kHz) to a file and the 240 x 240
 * screen at 30 frames a second (raw RGB) to stdout; tools/make_demo.py puts them into a Full HD video.
 *   ju_demo audio.raw captions.txt > frames.rgb */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ju_api.h"
#include "ju_presets.h"
#define FELUCCA_FLASH 0
static unsigned str_len(const char *s) { return (unsigned)strlen(s); }
static void fmt_int(char *b, int32_t v) { sprintf(b, "%d", (int)v); }
static uint16_t shot[240 * 240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t j;
    for (j = 0; j < h; j++) memcpy(&shot[(y + j) * 240 + x], px + j * w, w * 2);
}
#define section(x)
#include "ju_canvas.c"
static int8_t ju_octave;
#include "ju_proj.c"
#include "ju_ui.c"
static void ju_store_current(void) {}
static int ju_store_patch(int idx) { (void)idx; return 0; }
static int ju_store_seq(int part) { (void)part; return 0; }
static int ju_store_song(void) { return 0; }
static int ju_proj_flash_save(int s) { (void)s; return 0; }
static int ju_proj_flash_load(int s) { (void)s; return -1; }
static int ju_proj_flash_clear(int s) { (void)s; return 0; }

#define FS 44100
#define FPT 1470                                    /* samples a video frame lasts (30 frames a second) */
static FILE *aud, *meta;
static uint64_t samples;
static int frame_n;
static ju_in_t pend;                                /* the input of the next frame */
static uint32_t rel_next;                           /* buttons released at the frame after */
static int sweep_every, sweep_dir, sweep_knob, sweep_cnt;     /* a knob turned slowly, now and then */
static double fade_from = -1, fade_len = 1;         /* the master volume goes down from this time (s) */
static int vol0 = 200;

static void emit_frame(void)
{
    ju_in_t in = pend;
    unsigned char rgb[240 * 3];
    int i, y;
    memset(&pend, 0, sizeof pend);
    in.released |= rel_next; rel_next = 0;
    if (sweep_every && ++sweep_cnt >= sweep_every) { sweep_cnt = 0; in.knob[sweep_knob] += sweep_dir; }
    if (fade_from >= 0) {
        double t = (double)samples / FS;
        double g = t < fade_from ? 1.0 : t > fade_from + fade_len ? 0.0 : 1.0 - (t - fade_from) / fade_len;
        ju_global[G_VOLUME] = (uint8_t)(vol0 * g);
    }
    ui.cpu = 0;                                     /* the video does not show a CPU figure (masked) */
    if (ju_song_playing()) ui.song_cur = ju_song_row;     /* (the song page here follows the playing row) */
    ui_update(&in, (uint32_t)((uint64_t)frame_n * 1000 / 30));
    cv_force = 1; cv_present(ui_draw);
    for (y = 0; y < 240; y++) {
        for (i = 0; i < 240; i++) {
            uint16_t c = (uint16_t)((shot[y * 240 + i] >> 8) | (shot[y * 240 + i] << 8));
            rgb[3 * i] = (unsigned char)((c >> 11) << 3); rgb[3 * i + 1] = (unsigned char)(((c >> 5) & 63) << 2); rgb[3 * i + 2] = (unsigned char)((c & 31) << 3);
        }
        fwrite(rgb, 1, sizeof rgb, stdout);
    }
    frame_n++;
}
static void block(void)
{
    int32_t u[32] = {0}, l[32] = {0}, o[64];
    int16_t pcm[64];
    int i;
    ju_prepare_block(); ju_render(u, l); ju_finish(u, l, o);
    for (i = 0; i < 64; i++) { int32_t v = o[i]; pcm[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
    fwrite(pcm, 2, 64, aud);
    samples += 32;
    while (samples >= (uint64_t)frame_n * FPT) emit_frame();
}
static void wait_s(double s)
{
    uint64_t end = samples + (uint64_t)(s * FS);
    while (samples < end) block();
}
static double now_s(void) { return (double)samples / FS; }
static void cap(const char *title, const char *sub) { fprintf(meta, "%.3f\t%s\t%s\n", now_s(), title, sub); }
static void tap(int b) { pend.press |= BTN(b); pend.held |= BTN(b); rel_next |= BTN(b); }
static void non(int mask, int n) { ju_note_on_m(mask, (uint8_t)n); }
static void noff(int mask, int n) { ju_note_off_m(mask, (uint8_t)n); }
static void load(int part, int idx) { ui.patch_idx[part] = idx; ju_load_req[part] = (uint8_t)(idx + 1); wait_s(0.05); }

#define R (-1)
#define T (-2)
#define NN(pc, oct) (12 * ((oct) + 1) + (pc))
enum { C_ = 0, D_ = 2, E_ = 4, F_ = 5, G_ = 7, A_ = 9, B_ = 11 };
static void setpat(int part, int idx, int len, int gate, const int *s)
{
    ju_pat_t *t = &ju_pat[part][idx];
    int i;
    memset(t, 0, sizeof *t);
    for (i = 0; i < JU_SEQ_STEPS; i++) t->note[i] = 60;
    t->len = (uint8_t)len; t->gate = (uint8_t)gate;
    for (i = 0; i < len; i++) { if (s[i] == R) t->type[i] = SQ_REST; else if (s[i] == T) t->type[i] = SQ_TIE; else { t->type[i] = SQ_NOTE; t->note[i] = (uint8_t)s[i]; } }
}
static void fl(int part, int idx, int step, int vel, int prob, int rat) { ju_pat[part][idx].flags[step] = SQF_MAKE(vel, prob, rat); }
static void chord(int part, int idx, int step, int o1, int o2, int o3) { ju_pat[part][idx].chord[step][0] = (uint8_t)(o1 | o2 << 4); ju_pat[part][idx].chord[step][1] = (uint8_t)o3; }
static void lane(int part, int idx, int param, const int *pct16)
{
    ju_pat_t *t = &ju_pat[part][idx];
    int k, i;
    for (k = 0; k < JU_LANE_N; k++) if (ju_lane_param[k] == param) t->lane = (uint8_t)(k + 1);
    for (i = 0; i < 16; i++) t->lv[i] = (uint8_t)(1 + pct16[i] * 127 / 100);
}

/* the four chords of the piece: Am F G Em, as root pitch class and the third above */
static const int kRoot[4] = {A_, F_, G_, E_}, kThird[4] = {3, 4, 4, 3};
static void build_patterns(void)
{
    int c, i;
    for (c = 0; c < 4; c++) {
        int r2 = NN(kRoot[c], 2), r3 = r2 + 12, f5 = r3 + 7, t4 = r3 + 12 + kThird[c], o4 = r3 + 12;
        int arp[16] = {r2, r3, f5, r3, t4, r3, f5, r3, r2, r3, f5, r3, o4, t4, f5, r3};
        int arp2[16] = {r2, f5, r3, t4, o4, t4, r3, f5, r2, f5, r3, t4, o4 + 7, t4, o4, f5};
        int pulse[16] = {r2, R, r2, R, r2, R, r2, R, r2, R, r2, R, r2, R, r3, R};
        int cut_up[16] = {30, 35, 40, 46, 52, 58, 64, 70, 76, 82, 88, 94, 100, 94, 88, 80};
        int cut_zz[16] = {70, 40, 85, 45, 95, 50, 100, 55, 90, 45, 100, 60, 85, 40, 95, 70};
        int pad[16] = {NN(kRoot[c], 3), T, T, T, T, T, T, T, T, T, T, T, T, T, T, T};
        static const int mel[4][16] = {{76, T, T, T, 74, T, 72, T, T, T, 69, T, T, T, R, R}, {72, T, T, T, 69, T, 72, T, 77, T, T, T, 76, T, 72, T},
                                       {74, T, T, T, 71, T, 74, T, 79, T, T, T, 78, T, 74, T}, {71, T, T, T, 76, T, T, T, 79, T, 76, T, T, T, T, T}};
        setpat(0, c, 16, 3, arp);                                      /* the arpeggio, accented on the beats, softer in between */
        for (i = 0; i < 16; i++) fl(0, c, i, i % 4 == 0 ? 1 : (i % 2 ? 2 : 0), 0, 0);
        setpat(0, 4 + c, 16, 4, pulse);                                /* the pulse */
        fl(0, 4 + c, 0, 1, 0, 0);
        setpat(0, 8 + c, 16, 2, arp2);                                 /* the climax: denser, a lane on the cutoff, ratchets and a chance of missing */
        for (i = 0; i < 16; i++) fl(0, 8 + c, i, i % 4 == 0 ? 1 : 0, i % 4 == 3 ? 1 : 0, 0);
        fl(0, 8 + c, 7, 0, 0, 1); fl(0, 8 + c, 15, 1, 0, 2);
        lane(0, 8 + c, P_VCF_CUT, cut_zz);
        if (c == 3) lane(0, 4 + c, P_VCF_CUT, cut_up);
        setpat(1, c, 16, 6, pad);                                      /* the pad: one chord a bar */
        chord(1, c, 0, kThird[c], 7, 12);
        setpat(1, 4 + c, 16, 5, mel[c]);                               /* the melody over it */
        setpat(1, 8 + c, 16, 5, mel[c]);                               /* ... and the same with a fifth under each note and a ratchet at the end */
        for (i = 0; i < 16; i++) if (mel[c][i] >= 0) chord(1, 8 + c, i, 7, 0, 0);
        fl(1, 8 + c, 14, 0, 0, 1);
        if (c == 0) fl(1, 8 + c, 12, 1, 0, 0);
    }
    /* the song */
    ju_song.n = 0;
#define ROW(a, b, nrep) do { ju_song.row[ju_song.n].pat[0] = (a); ju_song.row[ju_song.n].pat[1] = (b); ju_song.row[ju_song.n].rep = (nrep); ju_song.n++; } while (0)
    ROW(4, 0, 2); ROW(5, 1, 2); ROW(6, 2, 1); ROW(7, 3, 1);                                                 /* intro: the pulse under the pad (6 bars) */
    ROW(0, 0, 2); ROW(1, 1, 2); ROW(2, 2, 2); ROW(3, 3, 2);                                                 /* the arpeggio enters (8 bars) */
    ROW(0, 4, 2); ROW(1, 5, 2); ROW(2, 6, 2); ROW(3, 7, 2);                                                 /* a melody over it (8 bars) */
    ROW(8, 8, 2); ROW(9, 9, 2); ROW(10, 10, 2); ROW(11, 11, 2);                                             /* the climax (8 bars) */
    ROW(4, 0, 2); ROW(JU_SONG_NONE, 0, 2);                                                                 /* the pulse alone, then the pad (4 bars) */
    ju_seq_cur[0] = 0; ju_seq_cur[1] = 0;
}

static void phrase(int mask, const int *notes, int n, double on, double gap)
{
    int i;
    for (i = 0; i < n; i++) {
        if (notes[i] == R) { wait_s(on + gap); continue; }
        non(mask, notes[i]); wait_s(on); noff(mask, notes[i]); wait_s(gap);
    }
}
static void chordhold(int mask, const int *n, int k, double hold)
{
    int i;
    for (i = 0; i < k; i++) non(mask, n[i]);
    wait_s(hold);
    for (i = 0; i < k; i++) noff(mask, n[i]);
}
static void patch_caption(int idx, const char *what)
{
    char b[64];
    snprintf(b, sizeof b, "PATCH %d%d  %s", idx / 8 + 1, idx % 8 + 1, ju_bank[idx].name);
    cap(b, what);
}

int main(int argc, char **argv)
{
    int i;
    if (argc < 3) { fprintf(stderr, "usage: ju_demo audio.raw captions.txt > frames.rgb\n"); return 1; }
    aud = fopen(argv[1], "wb"); meta = fopen(argv[2], "w");
    static char obuf[1 << 20];
    setvbuf(stdout, obuf, _IOFBF, sizeof obuf);
    ju_init();
    memcpy(ju_bank, JU_PRESETS, sizeof(ju_bank));
    ju_global[G_CH_U] = 1; ju_global[G_CH_L] = 2; ju_global[G_VOLUME] = (uint8_t)vol0;
    ju_global[G_KEYMODE] = KM_WHOLE; ju_global[G_TEMPO] = 86 /* 126 BPM */;
    ju_patch[0] = ju_bank[8]; ju_patch[1] = ju_bank[0]; ui.patch_idx[0] = 8; ui.patch_idx[1] = 0;
    memcpy(ui.saved_global, ju_global, G_COUNT);
    ui.page = PAGE_QLINK; ui.qgroup = 0; ui.overview = 0;
    wait_s(0.5);

    /* ---- part 1: the sounds */
    cap("SOUNDS", "A few of the 64 patches, played from the keys");
    {
        static const int chAm[4] = {NN(A_, 3), NN(C_, 4), NN(E_, 4), NN(A_, 4)}, chF[4] = {NN(F_, 3), NN(A_, 3), NN(C_, 4), NN(F_, 4)};
        static const int chG[3] = {NN(G_, 3), NN(B_, 3), NN(D_, 4)};
        static const int bass[8] = {NN(A_, 1), NN(A_, 1), NN(C_, 2), NN(A_, 1), NN(E_, 2), NN(D_, 2), NN(A_, 1), NN(G_, 1)};
        static const int acid[12] = {NN(A_, 1), NN(A_, 2), NN(A_, 1), NN(C_, 2), NN(A_, 1), NN(A_, 2), NN(E_, 2), NN(A_, 1), NN(G_, 2), NN(A_, 1), NN(C_, 3), NN(A_, 1)};
        static const int lead[7] = {NN(A_, 4), NN(C_, 5), NN(E_, 5), NN(D_, 5), NN(C_, 5), NN(A_, 4), NN(E_, 5)};
        static const int bell[12] = {NN(A_, 5), NN(E_, 5), NN(C_, 6), NN(E_, 5), NN(A_, 5), NN(E_, 6), NN(C_, 6), NN(E_, 5), NN(G_, 5), NN(D_, 6), NN(B_, 5), NN(D_, 6)};
        static const int c1[4] = {NN(A_, 3), NN(C_, 4), NN(E_, 4), NN(A_, 4)}, c2[4] = {NN(F_, 3), NN(A_, 3), NN(C_, 4), NN(F_, 4)};
        static const int c3[4] = {NN(C_, 4), NN(E_, 4), NN(G_, 4), NN(C_, 5)}, c4[4] = {NN(G_, 3), NN(B_, 3), NN(D_, 4), NN(G_, 4)};
        patch_caption(8, "Polyphonic pad, chorus and reverb"); load(0, 8); ui.qgroup = 3;
        chordhold(1, chAm, 4, 2.0); wait_s(0.6); chordhold(1, chF, 4, 1.6); wait_s(0.9);
        patch_caption(24, "Brass section"); load(0, 24); ui.qgroup = 2;
        for (i = 0; i < 3; i++) { chordhold(1, chG, 3, 0.22); wait_s(0.2); }
        chordhold(1, chG, 3, 1.4); wait_s(0.7);
        patch_caption(4, "Unison bass: eight voices on every key"); load(0, 4); ui.qgroup = 0;
        phrase(1, bass, 8, 0.2, 0.08); phrase(1, bass, 8, 0.2, 0.08); wait_s(0.5);
        patch_caption(3, "Resonant filter, swept by hand"); load(0, 3); ui.qgroup = 0; ui.overview = 0;
        sweep_every = 3; sweep_dir = 1; sweep_knob = 0;
        phrase(1, acid, 12, 0.11, 0.03); sweep_dir = -1; phrase(1, acid, 12, 0.11, 0.03); sweep_every = 0; wait_s(0.5);
        patch_caption(41, "Saw lead with delay"); load(0, 41); ui.qgroup = 5;
        phrase(1, lead, 7, 0.38, 0.02); wait_s(1.2);
        patch_caption(49, "Glass bell"); load(0, 49); ui.qgroup = 4;
        phrase(1, bell, 12, 0.16, 0.04); wait_s(1.6);
        patch_caption(16, "Strings"); load(0, 16); ui.qgroup = 3;
        chordhold(1, c1, 4, 1.2); chordhold(1, c2, 4, 1.2); chordhold(1, c3, 4, 1.2); chordhold(1, c4, 4, 1.8); wait_s(1.0);
        patch_caption(63, "Noise and resonance"); load(0, 63); ui.qgroup = 0;
        non(1, NN(A_, 2)); sweep_every = 5; sweep_dir = 1; sweep_knob = 0; wait_s(3.5); sweep_every = 0; noff(1, NN(A_, 2)); wait_s(1.6);
    }

    /* ---- part 2: the piece */
    cap("BERLIN SCHOOL", "Two parts, 126 BPM: the sequencer in song mode");
    ju_global[G_KEYMODE] = KM_2PART; ju_global[G_PANEL] = 0;
    ju_patch[0] = ju_bank[39]; ju_patch[1] = ju_bank[10]; ui.patch_idx[0] = 39; ui.patch_idx[1] = 10;
    ju_load_req[0] = ju_load_req[1] = 0;
    ju_global[G_SONG] = 1; ju_global[G_SONGLOOP] = 0;
    build_patterns();
    ju_song_sel = 0;
    tap(B_SEQ); wait_s(0.1);                                           /* the roll */
    wait_s(0.6);
    cap("INTRO", "Part 2: a warm pad, one chord a bar. Part 1: a pulse");
    ju_seq_req = 1;                                                    /* PLAY (the button is a tap of the same request) */
    {
        const double bar = 16.0 * 60.0 / 126.0 / 4.0;                  /* 1.905 s */
        double t0 = now_s();
        fade_from = t0 + 30 * bar + 0.0; fade_len = 4 * bar - 0.2;
        wait_s(6 * bar - 0.1);
        cap("THE ARPEGGIO", "A 16-step sequence with accents; the cutoff opened by hand");
        tap(B_HOME); wait_s(0.1); ui.page = PAGE_QLINK; ui.qgroup = 0; ui.overview = 0;
        sweep_every = 7; sweep_dir = 1; sweep_knob = 0;
        wait_s(8 * bar);
        sweep_every = 0;
        cap("THE MELODY", "Part 2 becomes a lead (patch 43); the pad is gone");
        ju_load_req[1] = 43 + 1; ui.patch_idx[1] = 43; ju_global[G_PANEL] = 1;
        ui.page = PAGE_SEQ; ui.seq_sub = 0; wait_s(0.1);
        wait_s(8 * bar - 0.1);
        cap("THE CLIMAX", "Denser sequence: accents, ratchets and a chance of missing a note");
        ju_global[G_PANEL] = 0; ui.seq_sub = 0; wait_s(2 * bar);
        ui.seq_sub = 3; ju_seq_cursor[0] = 7; wait_s(2 * bar);                                  /* a step with a ratchet */
        cap("A LANE ON THE CUTOFF", "One lane per pattern moves a parameter from step to step");
        ui.seq_sub = 4; ju_seq_cursor[0] = 3; wait_s(2 * bar);
        cap("CHORDS", "Up to four notes on a step: here a fifth under every melody note");
        ju_global[G_PANEL] = 1; ju_seq_cursor[1] = 0; wait_s(2 * bar);
        cap("OUTRO", "Song mode follows its rows; the song ends by itself");
        ui.seq_sub = 2; ui.page = PAGE_SEQ;
        ju_load_req[1] = 10 + 1; ui.patch_idx[1] = 10;
        wait_s(4 * bar - 0.1);
    }
    wait_s(1.5);
    cap("MINI-JU08", "Firmware 1.00 for the M-VAVE FM-1");
    ui.page = PAGE_ABOUT; wait_s(6.0);
    fclose(aud); fclose(meta); fflush(stdout);
    fprintf(stderr, "%d frames, %.1f s\n", frame_n, now_s());
    return 0;
}
