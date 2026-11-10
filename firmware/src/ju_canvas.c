/* SPDX-License-Identifier: GPL-3.0-only */
/* The panel's drawing surface: a band of the 240 x 240 screen. The whole screen is never held in RAM (that would
 * be 115 KB): ju_ui.c draws the page once per band (clipped to it) and the bands that changed go to the panel.
 * Colours are RGB565 already byte-swapped for the ST7789 (CV_RGB), so a band goes out as it is.
 * Fonts after jupiter8_esp32/src/ui/canvas.cpp: 5x7 (advance 6), 3x5 "tiny" (advance 4), seven-segment digits. */
#define CV_W 240
#define CV_H 240
#define CV_BAND 20
#define CV_NBANDS (CV_H / CV_BAND)
#define CV_RGB(r, g, b) ((uint16_t)((((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)) >> 8) | \
                                    (((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)) << 8)))

static uint16_t cv_buf[2][CV_W * CV_BAND] __attribute__((aligned(4), section(".pool")));
static uint16_t *cv_fb;                       /* the band being drawn */
static int cv_y0;                             /* its first screen line */

static inline uint16_t cv_rgb(uint32_t r, uint32_t g, uint32_t b) { return CV_RGB(r, g, b); }

static void cv_rect(int x, int y, int w, int h, uint16_t c)
{
    int j, i;
    y -= cv_y0;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > CV_W) w = CV_W - x;
    if (y + h > CV_BAND) h = CV_BAND - y;
    if (w <= 0 || h <= 0) return;
    for (j = 0; j < h; j++) {
        uint16_t *p = cv_fb + (y + j) * CV_W + x;
        for (i = 0; i < w; i++) p[i] = c;
    }
}
static void cv_frame(int x, int y, int w, int h, uint16_t c)
{
    cv_rect(x, y, w, 1, c); cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c); cv_rect(x + w - 1, y, 1, h, c);
}
static inline void cv_px(int x, int y, uint16_t c)
{
    y -= cv_y0;
    if (x >= 0 && y >= 0 && x < CV_W && y < CV_BAND) cv_fb[y * CV_W + x] = c;
}
/* does a box of height h at y touch the band? (text and glyphs skip themselves when it does not) */
static inline int cv_sees(int y, int h) { return y + h > cv_y0 && y < cv_y0 + CV_BAND; }

static const uint8_t kFont5x7[96][5] = {
    {0,0,0,0,0},{0,0,0x5F,0,0},{0,7,0,7,0},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},
    {0x23,0x13,8,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0,5,3,0,0},{0,0x1C,0x22,0x41,0},{0,0x41,0x22,0x1C,0},
    {0x14,8,0x3E,8,0x14},{8,8,0x3E,8,8},{0,0x50,0x30,0,0},{8,8,8,8,8},{0,0x60,0x60,0,0},{0x20,0x10,8,4,2},
    {0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{1,0x71,9,5,3},
    {0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1E},{0,0x36,0x36,0,0},{0,0x56,0x36,0,0},{8,0x14,0x22,0x41,0},
    {0x14,0x14,0x14,0x14,0x14},{0,0x41,0x22,0x14,8},{2,1,0x51,9,6},{0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},{0x7F,9,9,9,1},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,8,8,8,0x7F},{0,0x41,0x7F,0x41,0},
    {0x20,0x40,0x41,0x3F,1},{0x7F,8,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},{0x7F,2,0xC,2,0x7F},{0x7F,4,8,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E},{0x7F,9,9,9,6},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,9,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {1,1,0x7F,1,1},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},{0,0x7F,0x41,0x41,0},{2,4,8,0x10,0x20},{0,0x41,0x41,0x7F,0},{4,2,1,2,4},
    {0x40,0x40,0x40,0x40,0x40},{0,1,2,4,0},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{8,0x7E,9,1,2},{0xC,0x52,0x52,0x52,0x3E},{0x7F,8,4,4,0x78},
    {0,0x44,0x7D,0x40,0},{0x20,0x40,0x44,0x3D,0},{0x7F,0x10,0x28,0x44,0},{0,0x41,0x7F,0x40,0},{0x7C,4,0x18,4,0x78},
    {0x7C,8,4,4,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,8},{8,0x14,0x14,0x18,0x7C},{0x7C,8,4,4,8},
    {0x48,0x54,0x54,0x54,0x20},{4,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0xC,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},
    {0,8,0x36,0x41,0},{0,0,0x7F,0,0},{0,0x41,0x36,8,0},{8,4,8,0x10,8},{0,0,0,0,0},
};



/* 3x5 caps and digits; rows top to bottom, 3 LSBs per row, bit 2 = left column */
static const uint8_t *cv_tiny_glyph(char c)
{
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t dig[10][5] = {
        {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,3,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,2,2},{7,5,7,5,7},{7,5,7,1,7}};
    static const uint8_t let[26][5] = {
        {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},
        {5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{6,5,5,5,5},{2,5,5,5,2},{6,5,6,4,4},{2,5,5,6,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
        {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},{7,1,2,4,7}};
    static const uint8_t minus[5] = {0,0,7,0,0}, plus[5] = {0,2,7,2,0}, apos[5] = {2,2,0,0,0}, amp[5] = {2,5,2,5,3}, dot[5] = {0,0,0,0,2};
    if (c >= '0' && c <= '9') return dig[c - '0'];
    if (c >= 'A' && c <= 'Z') return let[c - 'A'];
    if (c >= 'a' && c <= 'z') return let[c - 'a'];
    switch (c) { case '-': return minus; case '+': return plus; case '\'': return apos; case '&': return amp; case '.': return dot; default: return blank; }
}

static int cv_text(int x, int y, const char *s, uint16_t c, int scale)
{
    int skip = !cv_sees(y, 7 * scale);
    for (; *s; s++) {
        if (!skip) {
            unsigned ch = (unsigned char)*s;
            const uint8_t *g;
            int i, j;
            if (ch < 32 || ch > 127) ch = '?';
            g = kFont5x7[ch - 32];
            for (i = 0; i < 5; i++) {
                uint8_t col = g[i];
                for (j = 0; j < 7; j++) if (col & (1 << j)) cv_rect(x + i * scale, y + j * scale, scale, scale, c);
            }
        }
        x += 6 * scale;
    }
    return x;
}
static int cv_text_w(const char *s, int scale) { return (int)str_len(s) * 6 * scale - scale; }

static int cv_tiny(int x, int y, const char *s, uint16_t c)
{
    int skip = !cv_sees(y, 5);
    for (; *s; s++) {
        if (!skip) {
            const uint8_t *g = cv_tiny_glyph(*s);
            int i, j;
            for (j = 0; j < 5; j++)
                for (i = 0; i < 3; i++)
                    if (g[j] & (4 >> i)) cv_px(x + i, y + j, c);
        }
        x += 4;
    }
    return x;
}
static int cv_tiny_w(const char *s) { int n = (int)str_len(s); return n ? n * 4 - 1 : 0; }
/* the tiny text cut to a width (the characters that fit) */
static void cv_tiny_fit(int x, int y, const char *s, uint16_t c, int w)
{
    char b[16];
    int n = (w + 1) / 4, k = (int)str_len(s);
    if (n < 1) return;
    if (n > k) n = k;
    if (n > 15) n = 15;
    memcpy(b, s, (unsigned)n);
    b[n] = 0;
    cv_tiny(x + (w - cv_tiny_w(b)) / 2, y, b, c);
}

/*   a
 *  f b
 *   g
 *  e c
 *   d    */
static void cv_seg7(int x, int y, char d, uint16_t on, uint16_t off)
{
    static const uint8_t map[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    uint8_t m = (d >= '0' && d <= '9') ? map[d - '0'] : (d == 'U' ? 0x3E : d == 'L' ? 0x38 : 0);
    const int w = 11, h = 19;
    if (!cv_sees(y, h)) return;
    cv_rect(x + 2, y, w - 4, 2, (m & 1) ? on : off);
    cv_rect(x + w - 2, y + 2, 2, h / 2 - 2, (m & 2) ? on : off);
    cv_rect(x + w - 2, y + h / 2 + 1, 2, h / 2 - 2, (m & 4) ? on : off);
    cv_rect(x + 2, y + h - 2, w - 4, 2, (m & 8) ? on : off);
    cv_rect(x, y + h / 2 + 1, 2, h / 2 - 2, (m & 16) ? on : off);
    cv_rect(x, y + 2, 2, h / 2 - 2, (m & 32) ? on : off);
    cv_rect(x + 2, y + h / 2 - 1, w - 4, 2, (m & 64) ? on : off);
}

/* ---- going to the panel: each band is drawn by draw(), hashed, and sent only when it changed */
static uint32_t cv_hash[CV_NBANDS];
static uint8_t cv_force;                                /* send every band (a new picture over a stale panel) */

static uint32_t cv_band_hash(const uint16_t *p)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < CV_W * CV_BAND; i += 2u) h = (h ^ (*(const uint32_t *)(p + i))) * 16777619u;
    return h;
}
/* draw the page into the bands and push the ones that changed; returns how many went out */
static int cv_present(void (*draw)(void))
{
    static int which;                                    /* the buffer the next band goes into: kept across frames */
    int b, sent = 0;
    /* The last band of a frame is still being sent when the function returns. A new frame draws its first band at once, so
     * it must not start in the buffer that transfer reads (that garbled the bottom of the screen after a page change):
     * wait for the transfer first, then the two buffers alternate with every band sent. */
    lcd_sync();
    for (b = 0; b < CV_NBANDS; b++) {
        uint32_t h;
        cv_fb = cv_buf[which];
        cv_y0 = b * CV_BAND;
        cv_rect(0, cv_y0, CV_W, CV_BAND, CV_RGB(10, 10, 12));
        draw();
        h = cv_band_hash(cv_fb);
        if (h == cv_hash[b] && !cv_force) continue;
        cv_hash[b] = h;
        lcd_blit(0, (uint32_t)cv_y0, CV_W, CV_BAND, cv_fb);      /* waits for the previous band's transfer, then starts this one */
        which ^= 1;
        sent++;
    }
    cv_force = 0;
    return sent;
}
