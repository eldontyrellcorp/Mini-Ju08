/* SPDX-License-Identifier: GPL-3.0-only */
/* Boot and main loop of Mini-Ju08, after Mini-Ju08's parent main.c: WDT first, the boot-loop guard, the fault handler,
 * then LCD, input (TIMER5, 10 kHz) and audio (ALNK0). The loop reads the panel, runs the UI and draws; the sound is
 * all in the audio ISR. */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
static uint8_t flash_ok;                                /* the 1 MiB NOR answered with its id (ju_boot_flash) */

#include "ju_dsp.c"
#include "ju_fxdef.c"
#include "ju_pinfo.c"
#include "ju_presets.h"
#include "ju_canvas.c"
#include "ju_midi.c"
#include "ju_audio.c"
#include "ju_ui.c"

/* the panel: which matrix button / encoder carries which printed label (measured on hardware by Mini-Tone) */
static const uint8_t PANEL_BTN[NB] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1};   /* FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+ */
enum { EN_SELECT = 0, EN_ALGO = 1, EN_PRESET = 6, EN_K1 = 2, EN_K2 = 3, EN_K3 = 4, EN_K4 = 5 };

#if FELUCCA_FLASH
#include "fm1_flash.h"
#include "storage_hw.c"
#include "storage.c"
#endif
#include "ju_store.c"

static void timer5_start(void) { fm1_timer5_start(isr_timer5, 4); }   /* above ALNK0 (3): the scan nests into the render */

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++) b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void crash_line(int row, const char *s)         /* one band of the crash screen, outside the page machinery */
{
    cv_fb = cv_buf[0];
    cv_y0 = row * CV_BAND;
    cv_rect(0, cv_y0, CV_W, CV_BAND, CV_RGB(120, 0, 0));
    cv_text(8, cv_y0 + 6, s, CV_RGB(255, 255, 255), 1);
    lcd_blit(0, (uint32_t)cv_y0, CV_W, CV_BAND, cv_fb);
    lcd_sync();
}
static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    crash_line(0, "MINI-JU08 CRASH");
    hexs(b, c->vec); crash_line(2, b);
    hexs(b, c->pc); crash_line(3, b);
    hexs(b, c->emu); crash_line(4, b);
    hexs(b, c->dbg); crash_line(5, b);
    hexs(b, c->rets); crash_line(6, b);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

static void ju_boot_flash(void)
{
#if FELUCCA_FLASH
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;     /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (flash_ok) fl_plain_window_init();
#endif
}

/* the MASTER pot (squared), read before the audio starts so the first block plays at its level */
static int32_t master_knob = 512 * 16;
static void master_poll(void)
{
    int32_t a = fm1_adc_read(FM1_ADC_MASTER);
    if (a >= 0) {
        uint32_t k;
        master_knob += (a * 16 - master_knob) / 8;
        k = (uint32_t)(master_knob / 16);
        ju_master_q12 = (k * k) >> 8;
    }
}
static void master_boot(void)
{
    uint32_t i;
    (void)fm1_adc_read(FM1_ADC_MASTER);
    for (i = 0; i < 3u; i++) {
        int32_t a = fm1_adc_read(FM1_ADC_MASTER);
        if (a >= 0) { master_knob = a * 16; ju_master_q12 = ((uint32_t)a * (uint32_t)a) >> 8; return; }
    }
}

/* the panel into the UI's input: knobs, the three big encoders, the button edges */
static void ju_gather(ju_in_t *in)
{
    uint32_t released, pr = fm1_input_edges(&released), b;
    in->knob[0] = fm1_enc_take(EN_K1);
    in->knob[1] = fm1_enc_take(EN_K2);
    in->knob[2] = fm1_enc_take(EN_K3);
    in->knob[3] = fm1_enc_take(EN_K4);
    in->sel = fm1_enc_take(EN_SELECT);
    in->algo = fm1_enc_take(EN_ALGO);
    in->preset = fm1_enc_take(EN_PRESET);
    in->press = in->held = 0;
    for (b = 0; b < NB; b++) {
        if ((pr >> PANEL_BTN[b]) & 1u) in->press |= BTN(b);
        if ((fm1_in.buttons >> PANEL_BTN[b]) & 1u) in->held |= BTN(b);
    }
}

static void fm1_main(void)
{
    uint32_t ms, last_draw = 0, i;
    ju_in_t in;
    ju_boot_flash();
    fm1_input_init();
    fm1_adc_init();
    lcd_init();
    memset(cv_hash, 0xFF, sizeof cv_hash);
    felucca_dbg.boots++;
    ju_init();
    ui.patch_idx[0] = 8;                                /* first start: part 1 on 21 (Jupiter Pad), part 2 on 11 (Deep Sub) */
    ui.patch_idx[1] = 0;
    ju_store_boot();
    ju_patch[0] = ju_bank[ui.patch_idx[0]];
    ju_patch[1] = ju_bank[ui.patch_idx[1]];
    memcpy(ui.saved_global, ju_global, G_COUNT);
    master_boot();
    kb_boot_hold = 1;
    audio_init();
    usb_start();
#if FELUCCA_UART
    uart_midi_init();
#endif
    timer5_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    for (ms = 0; ms < 400u; ms += 10u) {               /* the splash; the pot follows */
        fm1_delay_ms(10);
        master_poll();
    }
    kb_boot_hold = 0;
    cv_force = 1;

    for (;;) {
        uint32_t now = fm1_ms;
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) { bootguard.pending = 0; bootguard.failed = 0; }
        master_poll();
        {   /* OCT- + OCT+ held 5 s: UBOOT (the update mode), as Mini-Tone */
            static uint32_t t0;
            uint32_t both = (1u << PANEL_BTN[B_OCTDN]) | (1u << PANEL_BTN[B_OCTUP]);
            if ((fm1_in.buttons & both) != both) t0 = fm1_ms;
            else if (fm1_ms - t0 > 5000u) {
                fm1_audio_stop();
                cv_fb = cv_buf[0]; cv_y0 = 100;
                cv_rect(0, 100, CV_W, CV_BAND, CV_RGB(10, 10, 12));
                cv_text(90, 106, "UBOOT", CV_RGB(255, 196, 40), 2);
                lcd_blit(0, 100, CV_W, CV_BAND, cv_fb);
                lcd_sync();
                usb_detach();
                fm1_delay_ms(30);
                bootguard.pending = 0;
                fm1_enter_uboot();
            }
        }
        if (usb.uboot_req) {                            /* SysEx F0 22 24 35 7D F7 from the host */
            fm1_audio_stop();
            usb_detach();
            fm1_delay_ms(30);
            bootguard.pending = 0;
            fm1_enter_uboot();
        }
        ju_gather(&in);
        for (i = 0; i < 2u; i++)                        /* a MIDI program change moved a part to another patch */
            if (ju_pc_idx[i] >= 0) { ui.patch_idx[i] = ju_pc_idx[i]; ju_pc_idx[i] = -1; }
        ui_update(&in, now);
        ui.cpu = (uint8_t)(ju_cpu_q8 * 100u / 256u);
        if (now - last_draw >= 40u || in.press || in.knob[0] || in.knob[1] || in.knob[2] || in.knob[3] || in.sel || in.algo || in.preset) {
            cv_present(ui_draw);
            last_draw = now;
        }
        felucca_dbg.ui_frames++;
        while (fm1_ms - now < 15u) {                    /* ~60 UI frames a second at most */
            fm1_wdt_feed();
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) { bootguard.magic = BOOTGUARD_MAGIC; bootguard.failed = 0; bootguard.pending = 0; }
    if (bootguard.pending) bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) { bootguard.failed = 0; bootguard.pending = 0; fm1_enter_uboot(); }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++) *d = 0;
    for (d = _pool_start; d < _pool_end; d++) *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++) *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++) *d = *s;       /* flash driver code that must run from RAM */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
