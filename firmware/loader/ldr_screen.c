/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * From felucca-bsp loader/ldr_screen.c (hugelton/felucca-bsp, pinned by tools/build_base.py), in Melodee's update loader */
/* The update loader's screen: "UPDATING", "DO NOT UNPLUG" and "DO NOT POWER OFF" (smaller), a bar that fills as the
 * app area is written (ldr_progress) and its percentage under it, in the installer page's colours. Until the host
 * starts the install, a short piece of the bar sweeps across it (the loader waits, alive). Mid-install, a host silent
 * for 2 s (ldr_screen_tick, ldr_screen_heard: a cable that dropped, a host gone) turns the lines to "USB LOST",
 * "RECONNECT THE CABLE", "DO NOT POWER OFF" (a paused install is not a safe one to cut), the bar and the percentage
 * kept; the host's next frame turns them back (the loader resumes: what is written is skipped). At the end
 * (ldr_screen_done): "DONE", "RESTARTING", the only lines without the power warning. Drawn only when something
 * changes: a state, a bar pixel, a percent, a sweep step (before the install only). The panel is started as the app's lcd.c does (fm1-base device/lcd.c: reset, sleep out, RGB565,
 * IPS inversion), polled, at Felucca's SPI1 rate (BAUD 4).
 * Part of loader.c's single unit; every buffer is in RAM (the SPI DMA reads RAM only). */
#include "fm1_lcd_hw.h"                          /* (Melodee: hal/, both boards' pins: the SMK-37's panel is the same ST7789V) */

#define SCR_BG 0xFFBDu                              /* #faf4e8 cream */
#define SCR_FG 0x49C5u                              /* #48392f brown */
#define SCR_BAR 0xBA43u                             /* #b84a1e coral */
#define SCR_TRACK 0xEEF9u                           /* #e8dcc8, the empty bar */
#define SCR_BIG_Y 60u
#define SCR_SMALL_Y 94u
#define SCR_POWER_Y 114u
#define SCR_BAR_X 30u
#define SCR_BAR_Y 150u
#define SCR_BAR_W 180u
#define SCR_BAR_H 12u
#define SCR_PC_Y 172u
#define SCR_ROWS 21u                                /* the tallest line: 5x7 glyphs at 3x */
#define SCR_LOST_POLLS 4000u                        /* 2 s of loader.c's 0.5 ms USB polls without a frame */
#define SCR_SWEEP_POLLS 200u                        /* a sweep step every 100 ms of polls, before the install */
#define SCR_SWEEP_W 24u
#define SCR_SWEEP_STEP 6u

static uint16_t scr_strip[240u * SCR_ROWS];         /* one line of text, byte-swapped (the panel takes MSB first) */
static uint8_t scr_small[8];
static uint32_t scr_shown;                          /* bar pixels drawn */
static uint32_t scr_pc;                             /* the percentage drawn */
static uint32_t scr_quiet;                          /* polls since the host's last frame */
static uint32_t scr_wait, scr_sweep;                /* polls waiting; the sweep's x in the bar + 1 (0: none) */
static uint8_t scr_on, scr_armed;                   /* armed: an install has started */
static uint8_t scr_state;                           /* 0 updating, 1 USB lost, 2 done */

/* 5x7 glyphs, a row a byte (bit 4 = left) */
static const struct { char c; uint8_t rows[7]; } SCR_FONT[] = {
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
};

static void scr_cmd(uint8_t c)
{
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void scr_data(const void *p, uint32_t n)     /* n > 0 */
{
    fm1_lcd_send_data(p, n);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void scr_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    scr_small[0] = (uint8_t)(x0 >> 8); scr_small[1] = (uint8_t)x0;
    scr_small[2] = (uint8_t)(x1 >> 8); scr_small[3] = (uint8_t)x1;
    scr_cmd(0x2A);
    scr_data(scr_small, 4);
    scr_small[4] = (uint8_t)(y0 >> 8); scr_small[5] = (uint8_t)y0;
    scr_small[6] = (uint8_t)(y1 >> 8); scr_small[7] = (uint8_t)y1;
    scr_cmd(0x2B);
    scr_data(scr_small + 4, 4);
    scr_cmd(0x2C);
}

static uint16_t scr_swap(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

static void scr_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, n = w * h, k = 240u * SCR_ROWS;
    for (i = 0; i < k && i < n; i++)
        scr_strip[i] = scr_swap(c);
    scr_window(x, y, x + w - 1u, y + h - 1u);
    for (; n; n -= k) {
        if (k > n)
            k = n;
        scr_data(scr_strip, k * 2u);
    }
}

/* one line, centred in a box of cols characters (or its own length if longer), the box cleared: a shorter line put
 * over a longer one of the same box leaves nothing behind; scale <= 3 */
static void scr_text(uint32_t y, const char *s, uint32_t scale, uint32_t cols)
{
    uint32_t len = 0, i, r, b, sx, sy, x0, w, pad;
    while (s[len])
        len++;
    if (cols < len)
        cols = len;
    w = cols * 6u * scale - scale;
    x0 = (240u - w) / 2u;
    pad = (cols - len) * 6u * scale / 2u;
    for (i = 0; i < w * 7u * scale; i++)
        scr_strip[i] = scr_swap(SCR_BG);
    for (i = 0; i < len; i++) {
        const uint8_t *g = 0;
        uint32_t f;
        for (f = 0; f < sizeof SCR_FONT / sizeof SCR_FONT[0]; f++)
            if (SCR_FONT[f].c == s[i])
                g = SCR_FONT[f].rows;
        if (!g)
            continue;                               /* a space */
        for (r = 0; r < 7u; r++)
            for (b = 0; b < 5u; b++)
                if (g[r] & (0x10u >> b))
                    for (sy = 0; sy < scale; sy++)
                        for (sx = 0; sx < scale; sx++)
                            scr_strip[(r * scale + sy) * w + pad + (i * 6u + b) * scale + sx] = scr_swap(SCR_FG);
    }
    scr_window(x0, y, x0 + w - 1u, y + 7u * scale - 1u);
    scr_data(scr_strip, w * 7u * scale * 2u);
}

/* the big line, the small one, and the power warning under it but at the end: 19 small characters fit (226 px) */
static void scr_lines(const char *big, const char *small, uint32_t warn)
{
    scr_text(SCR_BIG_Y, big, 3, 8);                 /* "UPDATING", "USB LOST", "DONE" */
    scr_text(SCR_SMALL_Y, small, 2, 19);            /* "DO NOT UNPLUG", "RECONNECT THE CABLE", "RESTARTING" */
    scr_text(SCR_POWER_Y, warn ? "DO NOT POWER OFF" : "", 2, 19);
}

/* the waiting sweep: a piece of the empty bar, one step on */
static void scr_sweep_step(void)
{
    if (scr_sweep)
        scr_fill(SCR_BAR_X + scr_sweep - 1u, SCR_BAR_Y, SCR_SWEEP_W, SCR_BAR_H, SCR_TRACK);
    scr_sweep = scr_sweep && scr_sweep + SCR_SWEEP_STEP <= SCR_BAR_W - SCR_SWEEP_W + 1u ? scr_sweep + SCR_SWEEP_STEP : 1u;
    scr_fill(SCR_BAR_X + scr_sweep - 1u, SCR_BAR_Y, SCR_SWEEP_W, SCR_BAR_H, SCR_BAR);
}

static void scr_percent(uint32_t pc)                /* "0%" .. "100%" */
{
    char t[5], *p = t;
    if (pc >= 100u)
        *p++ = '1';
    if (pc >= 10u)
        *p++ = (char)('0' + pc / 10u % 10u);
    *p++ = (char)('0' + pc % 10u);
    *p++ = '%';
    *p = 0;
    scr_text(SCR_PC_Y, t, 2, 4);
}

static void ldr_screen_init(void)
{
    fm1_lcd_hw_init();                              /* pins, backlight on, SPI1 */
    fm1_delay_ms(20);
    scr_cmd(0x01);                                  /* SWRESET */
    fm1_delay_ms(150);
    scr_cmd(0x11);                                  /* SLPOUT */
    fm1_delay_ms(120);
    scr_cmd(0x3A);                                  /* COLMOD: RGB565 */
    scr_small[0] = 0x55;
    scr_data(scr_small, 1);
    scr_cmd(0x36);                                  /* MADCTL: top-left origin, RGB order */
    scr_small[0] = 0x00;
    scr_data(scr_small, 1);
    scr_cmd(0x21);                                  /* INVON: IPS panel */
    scr_cmd(0x13);                                  /* NORON */
    scr_fill(0, 0, 240, 240, SCR_BG);
    scr_lines("UPDATING", "DO NOT UNPLUG", 1);
    scr_fill(SCR_BAR_X, SCR_BAR_Y, SCR_BAR_W, SCR_BAR_H, SCR_TRACK);
    scr_percent(0);
    scr_cmd(0x29);                                  /* DISPON */
    scr_on = 1;
}

/* the bar and the percentage only grow: a resumed install starts below what is shown and keeps it */
static void ldr_screen_progress(uint32_t done, uint32_t total)
{
    uint32_t px, pc;
    if (!scr_on || !total)
        return;
    if (!scr_armed && scr_sweep)                    /* the install starts: the sweep goes */
        scr_fill(SCR_BAR_X + scr_sweep - 1u, SCR_BAR_Y, SCR_SWEEP_W, SCR_BAR_H, SCR_TRACK);
    scr_armed = 1;
    px = done >= total ? SCR_BAR_W : done * SCR_BAR_W / total;     /* < 2^32: total <= 2 MiB */
    pc = done >= total ? 100u : done * 100u / total;
    if (px > scr_shown) {
        scr_fill(SCR_BAR_X + scr_shown, SCR_BAR_Y, px - scr_shown, SCR_BAR_H, SCR_BAR);
        scr_shown = px;
    }
    if (pc > scr_pc) {
        scr_percent(pc);
        scr_pc = pc;
    }
}

/* a frame from the host (cheap: called for each one) */
static void ldr_screen_heard(void)
{
    scr_quiet = 0;
    if (scr_state == 1u) {
        scr_state = 0;
        scr_lines("UPDATING", "DO NOT UNPLUG", 1);
    }
}

/* one USB poll (0.5 ms) without a frame: before the install the sweep steps; an install whose host stays silent 2 s is
 * shown lost, once */
static void ldr_screen_tick(void)
{
    if (!scr_on)
        return;
    if (!scr_armed) {
        if (++scr_wait >= SCR_SWEEP_POLLS) {
            scr_wait = 0;
            scr_sweep_step();
        }
        return;
    }
    if (scr_state || ++scr_quiet < SCR_LOST_POLLS)
        return;
    scr_state = 1;
    scr_lines("USB LOST", "RECONNECT THE CABLE", 1);
}

/* written and verified: the app starts next */
static void ldr_screen_done(void)
{
    if (!scr_on)
        return;
    ldr_screen_progress(1, 1);
    scr_state = 2;
    scr_lines("DONE", "RESTARTING", 0);
}
