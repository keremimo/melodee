/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee in the browser: tests/ui_test.c (hostsim.c's engines, voices, mixer and sequencer, then the
 * whole UI on stub hardware) compiled to WebAssembly and run in an AudioWorklet (web/emu/worklet.js).
 * The worklet asks for audio; producing it advances the device's clock one millisecond at a time:
 * ui_input every millisecond, a whole UI frame (ui_input, ui_leds, ui_draw) every 15 ms as main.c's
 * loop, mix_block every CTL frames as the audio ISR. Nothing of the firmware changes for this file.
 * Not here: flash (nothing is saved), USB (no MIDI out, no USB audio), TRS MIDI, the click, the battery.
 *   web/emu/build.sh  ->  build/emu/{index.html, worklet.js, melodee.wasm} */
#include <stdint.h>
#define main melodee_ui_test_main
#include "ui_test.c"
#undef main
#undef __attribute__

/* the key matrix (hal/fm1_input.h): ui_test.c only declares it, so ui_leds' LEDs would land nowhere */
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};

#define EXPORT(name) __attribute__((used, export_name(#name)))

static uint32_t now_ms, fb_gen;
static uint16_t fb_seen[240 * 240];

EXPORT(web_boot) void web_boot(uint32_t master)       /* main.c melodee_init, as the UI tests power on */
{
    fm6_init();
    cz_init();
    ui_power_on();
    song.master_q12 = (master * master) >> 8;
    settings.palette = UI_DEFAULT_INDEX;
    palette_set(settings.palette);
    ui.force = 1;
    screen_clear();
}

/* the panel: matrix ids (PANEL_DEFAULT's), as fm1_in and the scan would have them */
EXPORT(web_keys) void web_keys(uint32_t m)
{
    host_notes |= m & ~fm1_in.notes;
    fm1_in.notes = m;
}
EXPORT(web_buttons) void web_buttons(uint32_t m)
{
    host_pressed |= m & ~fm1_in.buttons;
    fm1_in.buttons = m;
}
EXPORT(web_encoder) void web_encoder(uint32_t e, int32_t n)
{
    if (e < 7u)
        host_enc[e] += n;
}
EXPORT(web_master) void web_master(uint32_t v)        /* the MASTER knob, 0..1023, as main.c reads it */
{
    if (v > 1023u)
        v = 1023u;
    song.master_q12 = (v * v) >> 8;
}
EXPORT(web_midi_in) void web_midi_in(uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_in_q[mi_w++ % MQ] = (st >> 4) | (st & 0xFFu) << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24;
}

/* the LEDs back by id (0..13 buttons, 14..40 keys), lit ones only (not the dim planes) */
static uint32_t led_lit(uint32_t id)
{
    uint32_t c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] == (int8_t)id)
                return (fm1_led[c] >> r) & 1u;
    return 0;
}
EXPORT(web_led_keys) uint32_t web_led_keys(void)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        m |= led_lit(14u + k) << k;
    return m;
}
EXPORT(web_led_buttons) uint32_t web_led_buttons(void)
{
    uint32_t b, m = 0;
    for (b = 0; b < 14u; b++)
        m |= led_lit(b) << b;
    return m;
}
EXPORT(web_fb) uint16_t *web_fb(void) { return host_screen; }   /* RGB565, the panel's byte order */
EXPORT(web_fb_gen) uint32_t web_fb_gen(void) { return fb_gen; }
EXPORT(web_now) uint32_t web_now(void) { return now_ms; }

/* the clock and the sound: stereo float into a ring, drained by web_render */
#define RING 4096u
static float ring[RING * 2u], out_l[1024], out_r[1024];
static uint32_t ring_w, ring_r, owed;

static void tick(void)
{
    now_ms++;
    fm1_ms++;
    host_ticks += 1000u;                               /* (ui_test.c: FM1_TICKS_PER_US 1) */
    ui_input();
    if (now_ms % 15u == 0u) {
        ui_leds();
        ui_draw();
        if (memcmp(fb_seen, host_screen, sizeof fb_seen)) {
            memcpy(fb_seen, host_screen, sizeof fb_seen);
            fb_gen++;
        }
    }
    owed += FS;
    while (owed >= 1000u * CTL) {
        int32_t o[2 * CTL];
        uint32_t i;
        owed -= 1000u * CTL;
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            uint32_t r = (ring_w++ % RING) * 2u;
            ring[r] = (float)o[2u * i] / 65536.0f;       /* Q15, 6 dB down as audio.c sends it to the codec */
            ring[r + 1u] = (float)o[2u * i + 1u] / 65536.0f;
        }
    }
}

EXPORT(web_out_l) float *web_out_l(void) { return out_l; }
EXPORT(web_out_r) float *web_out_r(void) { return out_r; }
EXPORT(web_render) void web_render(uint32_t n)        /* n frames (<= 1024) into out_l / out_r */
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    while (ring_w - ring_r < n)
        tick();
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1u];
    }
}
