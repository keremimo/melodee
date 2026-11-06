/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The HOME menu's rows as JSON, for Melodee Studio (tools/studio_bundle.py: settings.json), read off the real menu
 * (ui_menu.c) through tests/ui_test.c's harness, in the FM-1's default build (USB audio, USB host, Bluetooth, MIDI
 * THRU; build with -DMELODEE_USB_AUDIO=1 -DMELODEE_USB_HOST=1 -Itests/ble_fake). Each row: its name as the firmware
 * has it and as the menu shows it, then its values as KNOB 1 steps through them from the left end, read from what the
 * menu draws (gfx.c's text hook; a row draws its value, at the right, then its name at x 12: ui_list.c row_draw). kind: "enum" (KNOB 1 stops at both ends),
 * "toggle" (OFF / ON), "percent" (min..max in steps), "cycle" (KNOB 1 wraps: the palettes), "page" (opens a page of
 * its own), "action" (OCT+ does it). ui_set: the editor's UI_SET id that reaches the row (editor_preferences.c); when:
 * the build flag the row needs. A row with no value that is not in NAV, or a name the menu does not draw, fails the
 * dump: the table here cannot fall behind the menu silently.
 *   build/host/settingsdump > settings.json */
#define MELODEE_BLE 1
#define UI_TEST_NO_MAIN 1
static void sd_text(int x0, const char *s);
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) sd_text(x0, s)
#define GFX_HOOK_BLIT(x, y, r0) ((void)0)                  /* (gfx.c defines these with GFX_HOOK_TEXT) */
#define GFX_HOOK_BEGIN() ((void)0)
#define GFX_HOOK_PIXELS(n) ((void)0)
#include "ui_test.c"

#define SD_MAX 512u
#define SD_TURNS 64u
static char sd_seen[SD_MAX][32];
static int sd_x[SD_MAX];
static uint32_t sd_n;
static void sd_text(int x0, const char *s)
{
    if (sd_n < SD_MAX) {
        sd_x[sd_n] = x0;
        strncpy(sd_seen[sd_n], s, sizeof sd_seen[0] - 1u);
        sd_seen[sd_n][sizeof sd_seen[0] - 1u] = 0;
        sd_n++;
    }
}

/* the rows without a value KNOB 1 steps through: what OCT+ (or KNOB 1 right) does there */
static const struct { uint8_t row; const char *kind, *does; } NAV[] = {
#if MELODEE_BLE
    {MI_BLE, "page", "Bluetooth: mode, links, bonds (ble_page_open)"},
#endif
    {MI_MIDI_LEARN, "page", "MIDI LEARN: the learned controls (mlearn_page_open); shows how many are learned"},
    {MI_MIDI_FORGET, "action", "forgets every learned control, after a question"},
    {MI_PANEL, "page", "HARDWARE CALIBRATION (panel_setup)"},
    {MI_ABOUT, "page", "ABOUT: version, licences, credits"},
    {MI_DESIGNER, "page", "SOUND DESIGNER (designer_open)"},
    {MI_BACK, "action", "closes the menu"},
};
static const struct { uint8_t row, id; } UI_SET_ID[] = {
    {MI_COLOR, 0}, {MI_CLICK, 4}, {MI_CLICK_LEVEL, 5}, {MI_COUNTIN, 6}, {MI_PREVIEW, 7}, {MI_ADD, 8}, {MI_SCREEN, 10},
};
static const struct { uint8_t row; const char *when; } WHEN[] = {
#if MELODEE_USB_AUDIO
    {MI_USB, "MELODEE_USB_AUDIO"}, {MI_USB_INPUT, "MELODEE_USB_AUDIO"},
#endif
#if MELODEE_USB_VOLUME
    {MI_USB_VOLUME, "MELODEE_USB_VOLUME"},
#endif
#if MELODEE_USB_HOST
    {MI_USB_HOST, "MELODEE_USB_HOST"},
#endif
#if MELODEE_BLE
    {MI_BLE, "MELODEE_BLE"},
#endif
#if MELODEE_MIDI_THRU
    {MI_THRU, "MELODEE_MIDI_THRU"},
#endif
};

/* ui_test.c's frame, turn and hold with the BLE fake's TIMER4 (24 ticks a microsecond, read without advancing)
 * kept on the harness's clock: holds and knob acceleration see the time ui_test.c means */
static void sd_frame(void) { fk.tick_per_read = 0; fk.ticks = (host_ticks + 16000u) * FM1_TICKS_PER_US; frame(); }
static void sd_turn(int32_t s)
{
    host_enc[panel.enc[EN_K1]] += s * panel.dir[EN_K1];
    sd_frame();
    host_ticks += 200000u; fm1_ms += 200u;            /* (a slow detent: no acceleration) */
}
static void sd_hold(uint32_t label)
{
    fm1_ms += 320u;
    fm1_in.buttons |= 1u << panel.btn[label]; host_pressed |= 1u << panel.btn[label];
    for (uint32_t k = 0; k < 52u; k++) sd_frame();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    sd_frame();
}

static char shown[MI_COUNT][24];
/* the menu drawn whole; row i's value as drawn (the text drawn before its name, a ">" passed over, if it is not a
 * name: right of SD_NAME_X), "" none; 0 if the row's name was not drawn */
#define SD_NAME_X 40
static const char *drawn(uint32_t i)
{
    static char v[32];
    int32_t k;
    sd_n = 0; ui.force = 1; sd_frame();
    for (k = (int32_t)sd_n - 1; k >= 0 && strcmp(sd_seen[k], shown[i]); k--)
        ;
    if (k < 0) return 0;
    v[0] = 0;
    if (--k >= 0 && !strcmp(sd_seen[k], ">")) k--;
    if (k >= 0 && sd_x[k] > SD_NAME_X) strcpy(v, sd_seen[k]);
    return v;
}
static void js_str(const char *s)
{
    putchar('"');
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\') putchar('\\');
        putchar(*s);
    }
    putchar('"');
}
/* KNOB 1 one detent; 1 if the menu is still the list */
static int knob(int32_t s) { sd_turn(s); if (ui.menu != 1) { ui.menu = 1; ui.force = 1; sd_frame(); return 0; } return 1; }

int main(void)
{
    static char vals[SD_TURNS + 1u][32];
    uint32_t i, j, n, first = 1;
    setvbuf(stdout, NULL, _IONBF, 0);
    ui_power_on();
    settings.palette = UI_DEFAULT_INDEX; palette_set(UI_DEFAULT_INDEX);   /* (ui_test.c's power-on: GRAY) */
    sd_hold(B_HOME);
    if (ui.menu != 1) { fprintf(stderr, "settingsdump: HOME held did not open the menu\n"); return 1; }
    for (i = 0; i < MI_COUNT; i++) menu_words(shown[i], MI_NAME[i], sizeof shown[i]);
    printf("{\"build\":\"FM-1 default (USB audio, USB host, Bluetooth, MIDI THRU)\",\"rows\":[");
    for (i = 0; i < MI_COUNT; i++) {
        const char *v, *kind = 0, *does = 0;
        char v0[32];
        int wraps = 0, page = 0;
        ui.menu = 1; ui.menu_sel = (uint8_t)i;
        if (!(v = drawn(i))) { fprintf(stderr, "settingsdump: row %s not drawn\n", shown[i]); return 1; }
        strcpy(v0, v);
        /* to the left end (or round to where it started: it wraps) */
        for (n = 0; n < SD_TURNS; n++) {
            char before[32];
            strcpy(before, drawn(i));
            if (!knob(-1)) { page = 1; break; }
            v = drawn(i);
            if (!strcmp(v, before)) break;
            if (!strcmp(v, v0)) { wraps = 1; break; }
        }
        /* then right, every value once */
        n = 0;
        strcpy(vals[n++], drawn(i));
        while (!page && n <= SD_TURNS) {
            if (!knob(1)) { page = 1; break; }
            v = drawn(i);
            if (!strcmp(v, vals[n - 1u]) || (wraps && !strcmp(v, vals[0]))) break;
            strcpy(vals[n++], v);
        }
        /* back to the value it had */
        for (j = 0; j < n && !page && strcmp(drawn(i), v0); j++)
            knob(wraps ? 1 : -1);
        if (!page && strcmp(drawn(i), v0))
            for (j = 0; j < n && strcmp(drawn(i), v0); j++) knob(1);
        for (j = 0; j < sizeof NAV / sizeof NAV[0]; j++)
            if (NAV[j].row == i) { kind = NAV[j].kind; does = NAV[j].does; }
        if (page && !kind) { fprintf(stderr, "settingsdump: KNOB 1 left the list on %s (not in NAV)\n", shown[i]); return 1; }
        if (!kind && n == 1u && !vals[0][0]) { fprintf(stderr, "settingsdump: %s has no value and is not in NAV\n", shown[i]); return 1; }
        printf("%s\n {\"index\":%u,\"key\":", first ? "" : ",", (unsigned)i);
        first = 0;
        js_str(MI_NAME[i]);
        printf(",\"label\":");
        js_str(shown[i]);
        if (kind) {
            printf(",\"kind\":\"%s\",\"does\":", kind);
            js_str(does);
        } else {
            int pct = 1;
            for (j = 0; j < n; j++) pct &= vals[j][0] && vals[j][strlen(vals[j]) - 1u] == '%';
            if (pct && n > 2u) {
                int lo = atoi(vals[0]), hi = atoi(vals[n - 1u]), step = atoi(vals[1]) - lo;
                printf(",\"kind\":\"percent\",\"min\":%d,\"max\":%d,\"step\":%d", lo, hi, step);
            } else {
                int toggle = n == 2u && !strcmp(vals[0], "OFF") && !strcmp(vals[1], "ON");
                printf(",\"kind\":\"%s\",\"values\":[", wraps ? "cycle" : toggle ? "toggle" : "enum");
                for (j = 0; j < n; j++) { if (j) putchar(','); js_str(vals[j]); }
                putchar(']');
            }
        }
        for (j = 0; j < sizeof UI_SET_ID / sizeof UI_SET_ID[0]; j++)
            if (UI_SET_ID[j].row == i) printf(",\"ui_set\":%u", UI_SET_ID[j].id);
        for (j = 0; j < sizeof WHEN / sizeof WHEN[0]; j++)
            if (WHEN[j].row == i) { printf(",\"when\":"); js_str(WHEN[j].when); }
        putchar('}');
    }
    printf("]}\n");
    return 0;
}
