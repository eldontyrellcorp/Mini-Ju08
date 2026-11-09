/* SPDX-License-Identifier: GPL-3.0-only */
/* I2S output (ALNK0 -> the codec), the audio ISR and the TIMER5 ISR, after Mini-Tone's audio.c and main.c.
 * Per half buffer (128 frames, 2.9 ms): MIDI and keys into the engine, then four blocks of JU_BLOCK = 32 samples:
 * engine -> master pot -> USB audio -> 24-bit stereo. The key / LED scan (TIMER5, 10 kHz) outranks the audio ISR and
 * nests in it; USB and TRS polls wait for the first tick after the render. */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];

#define HALF_WORDS (HALF_FRAMES * 2u)
#define DAC_TICKS 544u            /* TIMER4 ticks per I2S frame (24 MHz / 44,117.6 Hz) */
#define CPU_AVG (4096u / HALF_FRAMES)   /* halves the load meter averages: ~93 ms whatever the half */
#define OUT_SHIFT 7               /* Q15 -> 24-bit, -6 dBFS ceiling */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

/* diagnostics, kept across resets: read with `fm1t memr` */
#define DBG_MAGIC 0x44424732u                       /* "DBG2" */
struct felucca_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, page, home;
    uint32_t prev_stage, prev_page, prev_home, prev_rst, prev_frames;
} felucca_dbg __attribute__((section(".noinit")));
static volatile uint32_t audio_halves, audio_max_us;
static volatile uint32_t t5_nested_ticks;
static uint32_t audio_cpu_rem;
static volatile uint32_t ju_cpu_q8;                 /* the render's share of a half buffer, x256 (the header's CPU figure) */
static uint32_t ju_master_q12 = 2048;               /* the MASTER pot, squared (master_poll) */

static void audio_block(int32_t *out, uint32_t n)
{
    int32_t u[JU_BLOCK], l[JU_BLOCK];
    uint32_t i;
    for (i = 0; i < JU_BLOCK; i++) u[i] = l[i] = 0;
    ju_prepare_block();
    ju_render(u, l);
    ju_finish(u, l, out);
    {
        int32_t m = (int32_t)ju_master_q12;
        for (i = 0; i < 2u * n; i++) out[i] = (out[i] * m) >> 12;
    }
#if FELUCCA_UAC
    uac_tap(out, n);                                    /* the USB audio input: the master output */
#endif
    for (i = 0; i < n; i++) {
        out[2u * i] *= 1 << OUT_SHIFT;
        out[2u * i + 1u] *= 1 << OUT_SHIFT;
    }
}

/* Overload: two halves in a row above 85 % (the render plus TIMER5 nested in it): fade one voice over the next
 * block, and one more each half while it lasts */
static volatile uint8_t shed_req;
static uint8_t shed_over;
#define SHED_TICKS ((HALF_FRAMES * 1000000u / FS) * 85u / 100u * FM1_TICKS_PER_US)
static uint32_t shed_count;

static __attribute__((noinline)) uint32_t shed_check(uint32_t all)
{
    shed_over = (uint8_t)(shed_over << 1 | (all > SHED_TICKS));
    if ((shed_over & 3u) == 3u) shed_req = 1;
    return (all - t5_nested_ticks) / FM1_TICKS_PER_US;
}

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0;
    felucca_dbg.in_audio = 1;                   /* first: TIMER5 nests from here on */
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us;
        int32_t *o = &abuf[half * HALF_WORDS];
        if (shed_req) { shed_req = 0; ju_shed_voice(); shed_count++; }
        ju_midi_drain();
        ju_keys_block();
#if FELUCCA_UAC
        uac_render_start();
#endif
        for (b = 0; b < HALF_FRAMES; b += CTL) audio_block(o + 2u * b, CTL);
        fm1_audio_ack_half();
        audio_halves++;
        us = shed_check(fm1_ticks() - t0);
        if (us > audio_max_us) audio_max_us = us;
        {
            uint32_t load = ju_cpu_q8 * (CPU_AVG - 1u) + (us * 256u) / (HALF_FRAMES * 1000000u / FS) + audio_cpu_rem;
            ju_cpu_q8 = load / CPU_AVG;
            audio_cpu_rem = load % CPU_AVG;
        }
        if (fm1_audio_free_half() != half) felucca_dbg.late++;       /* the DMA moved on while we rendered */
        felucca_dbg.halves++;
        felucca_dbg.last_us = us;
        if (us > felucca_dbg.max_us) felucca_dbg.max_us = us;
        felucca_dbg.cpu_q8 = ju_cpu_q8;
    }
    felucca_dbg.in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* codec and ALNK0 bring-up (fm1_audio.h) */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++) abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}

/* TIMER5 (10 kHz): the key / LED scan, the milliseconds, USB (2 kHz) and TRS MIDI (2 kHz); nested in the audio ISR it
 * only scans and keeps the USB audio stream going */
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
    felucca_dbg.timer_irqs++;
    fm1_input_tick();
    {
        static uint32_t last, acc;
        acc += t0 - last;
        last = t0;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
    if (usb_due) owed |= 1u;
#if FELUCCA_UART
    if (sub % 5u == 2u) owed |= 2u;
#endif
    if (++sub == 10u) sub = 0;
    if (felucca_dbg.in_audio) {
        felucca_dbg.nested++;
#if FELUCCA_UAC
        if (usb_due) uac_service();
#endif
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed & 1u) usb_poll();
#if FELUCCA_UART
    if (owed & 2u) uart_midi_poll();
#endif
    owed = 0;
}
extern void isr_timer5(void);
