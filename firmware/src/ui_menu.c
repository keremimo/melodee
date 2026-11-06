/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Melodee menu (HOME held): COLOR, FONT (the type: RUBIK or PIXEL, ui.c TY_*), HOLD (the layer threshold), LIGHTS (the keys' and buttons'
 * glow: ui_input.c ui_leds), KNOB ACCEL (ui_input.c accel_by), USB AUDIO (IN+OUT, IN, OUT or OFF; usb.c ua_off_set),
 * USB INPUT (stereo master or four track stems; usb.c ua_tracks_set),
 * USB HOST (the USB-C port hosts a USB-MIDI controller instead of talking to a computer; not saved; usb_host.c),
 * MIDI THRU (fm1-base: what comes in goes on out where it did not come from; platform/melodee_app.c midi_thru),
 * TRS MIDI OUT (SMK-37: the jack sends what USB MIDI sends; smk_midi_out.h),
 * AUTOSAVE (automatic project saves after five seconds stopped and untouched), HOME SELECT (what SELECT turns on HOME:
 * ui.c HS_*), GLO SELECT (what SELECT turns with GLO held, on any page: the same choices), UNUSED TRACKS (SHOW:
 * Stage and MIXER keep a greyed "Add track" row for each place not in use; HIDE: the tracks in use only, ui.c
 * stage_rows),
 * CALIBRATION (the setup screen: HARDWARE CALIBRATION), ABOUT.
 * PRESETS scrolls from ABOUT through all credits. ui.menu: 1 list, 2 information, 3 Bluetooth, 4 MIDI LEARN. */
/* ------------------------------------------------------------ menu --- */
#define MENU_HEAD H_HEAD                 /* the menu's own title strip */
#include "menu_rows.c"                 /* the rows, their values and names (the editor's SETTINGS too) */

/* https://keremimo.github.io/melodee, QR version 3, correction M (segno; checked with a decoder).
 * One row per word, leftmost module in bit 0; the constant matrix needs no encoder or RAM. */
static const uint32_t ABOUT_QR[29] = {
    0x1FC6EB7Fu, 0x104EF341u, 0x175C795Du, 0x1747C65Du, 0x174CB15Du, 0x10417241u,
    0x1FD5557Fu, 0x001C5A00u, 0x1D23F9F9u, 0x0D8F5EA5u, 0x0557D275u, 0x137256B4u,
    0x10C9E87Cu, 0x1FC7940Du, 0x143AD76Du, 0x15706E91u, 0x021C8767u, 0x0D1C8893u,
    0x1309E463u, 0x07A1260Bu, 0x0FFBAC53u, 0x0316B300u, 0x035BAB7Fu, 0x011ABF41u,
    0x1BF9275Du, 0x102DF35Du, 0x1DB1E85Du, 0x16A01241u, 0x00D06B7Fu,
};

static void draw_about_qr(int32_t x, int32_t y)
{
    uint32_t row, col;
    cv_rect(x, y, 74, 74, UI_QR_LIGHT);             /* fixed light / dark, independent of the palette */
    for (row = 0; row < 29u; row++)
        for (col = 0; col < 29u; col++)
            if ((ABOUT_QR[row] >> col) & 1u)
                cv_rect(x + 8 + (int32_t)col * 2, y + 8 + (int32_t)row * 2, 2, 2, UI_QR_DARK);
}

/* All notices are in flash. Wrap at the font's actual width, without dropping URL
 * characters; the same layout computes the scroll limit and draws clipped bands.
 * Mixed case here (long text); S throughout, the title in L. */
#define MENU_DOC_H (240 - MENU_HEAD)                  /* the document: screen rows 30..240 */
enum { MC_TEXT, MC_TITLE, MC_URL };
typedef struct { const char *text; uint8_t kind; } menu_credit_t;
static const menu_credit_t MENU_CREDITS[] = {
#if MELODEE_BLE
    {"OPTIMIST / BLE MIDI", MC_TITLE},
    {"w0ts/optimist contributors / GPL-3.0-only", MC_TEXT},
    {"https://github.com/w0ts/optimist", MC_URL},
    {"", MC_TEXT},
#endif
    {"FELUCCA / UPSTREAM", MC_TITLE},                  /* Melodee is a modified Felucca */
    {"H\xFCgelton Instruments, Leo Kuroshita / GPL-3.0", MC_TEXT},
    {"https://github.com/hugelton/Felucca", MC_URL},
    {"", MC_TEXT},
    {"H\xFCgelton Instruments / OWN WORK", MC_TITLE},
    {"Leo Kuroshita", MC_TEXT},
    {"CrispyZebra (PHASE waveforms): GPL-3.0", MC_TEXT},
    {"https://github.com/hugelton", MC_URL},
    {"", MC_TEXT},
    {"KLATTSCH / VOICE REFERENCE", MC_TITLE},
    {"Tony Gies / MIT", MC_TEXT},
    {"https://github.com/tgies/klattsch", MC_URL},
    {"", MC_TEXT},
    {"DAISYSP / PHYS PORT", MC_TITLE},
    {"Electrosmith, Emilie Gillet", MC_TEXT},
    {"MIT", MC_TEXT},
    {"https://github.com/electro-smith/DaisySP", MC_URL},
    {"", MC_TEXT},
    {"uPD933 / NATIVE CZ", MC_TITLE},
    {"Devin Acker / BSD-3-Clause", MC_TEXT},
    {"https://github.com/mamedev/mame", MC_URL},
    {"", MC_TEXT},
    {"DEXED + MSFA / FM6 PORT", MC_TITLE},
    {"Pascal Gauthier, Google Inc.", MC_TEXT},
    {"msfa: Apache-2.0, Dexed: GPL-3.0", MC_TEXT},
    {"https://github.com/asb2m10/dexed", MC_URL},
    {"", MC_TEXT},
    {"RESID / SID MODEL", MC_TITLE},
    {"Dag Lem / GPL-2.0-or-later", MC_TEXT},
    {"https://github.com/libsidplayfp/resid", MC_URL},
    {"", MC_TEXT},
    {"RINGS / PHYS PORT", MC_TITLE},
    {"Emilie Gillet / MIT", MC_TEXT},
    {"https://github.com/pichenettes/eurorack", MC_URL},
    {"", MC_TEXT},
    {"RUBIK / FONT", MC_TITLE},
    {"The Rubik Project Authors", MC_TEXT},
    {"SIL Open Font License 1.1", MC_TEXT},
    {"https://github.com/googlefonts/rubik", MC_URL},
    {"", MC_TEXT},
    {"VSCO-2 CE + VCSL / SAMPLES", MC_TITLE},
    {"Versilian Studios / CC0 1.0", MC_TEXT},
    {"https://github.com/sgossner/VSCO-2-CE", MC_URL},
    {"https://github.com/sgossner/VCSL", MC_URL},
    {"", MC_TEXT},
    {"VOICE / RESEARCH", MC_TITLE},
    {"Dennis H. Klatt (1980)", MC_TEXT},
    {"https://doi.org/10.1121/1.383940", MC_URL},
    {"Hillenbrand, Getty, Clark and Wheeler (1995)", MC_TEXT},
    {"https://doi.org/10.1121/1.411872", MC_URL},
};

static int32_t menu_text(const char *s, int32_t y, uint16_t col, int draw)
{
    while (*s) {
        char line[64];
        uint32_t n = 0, space = 0, take, prev = 0;
        int32_t pen = 0;                                 /* 1/16 px, as text_w adds it up */
        while (s[n] && n < sizeof line - 1u) {
            uint32_t c = (uint8_t)s[n];
            line[n] = s[n];
            line[n + 1u] = 0;
            pen += (prev ? kern(TY_TX, prev, c) : 0) + gadv(TY_TX, &TY_TX->g[glyph(TY_TX, c)]);
            prev = c;
            if ((pen + 8) >> 4 > 226)
                break;
            if (s[n] == ' ')
                space = n;
            else if (s[n] == '/' && n)                  /* (a URL: a line may end after a slash) */
                space = n + 1u;
            n++;
        }
        take = s[n] && space ? space : n;
        if (!take) take = 1;                         /* progress even for an unusually wide glyph */
        line[take] = 0;
        if (draw && y + TY_TX->h > 0 && y < MENU_DOC_H)
            cv_text(6, y, TY_TX, line, col);
        y += 26;
        s += take;
        while (*s == ' ') s++;
    }
    return y;
}

static int32_t menu_document(int32_t y, int draw)
{
    uint32_t i;
    if (draw) {                                       /* (docs/design: mock/r6_pages About, the large type) */
        cv_text(6, y + 2, TY_HR, "Melodee", T_THEME);
        cv_text(6, y + 60, TY_TX, MELODEE_VERSION, T_TEXT);
        cv_text(6, y + 86, TY_LB, __DATE__, T_MID);
        draw_about_qr(158, y + 66);
    }
    y = menu_text("Multi-engine synthesizer", y + 146, T_SEC, draw);
    y = menu_text("\xA9 2026 Kerem Kilic, Ellic Studio", y + 6, T_TEXT, draw);
    y = menu_text("Based on Felucca by Leo Kuroshita, H\xFCgelton Instruments", y, T_TEXT, draw);
    y = menu_text("GPL-3.0-only", y, T_TEXT, draw);
    y = menu_text("keremimo.github.io/melodee", y, T_MID, draw);
    y = menu_text("CREDITS", y + 14, T_THEME, draw) + 8;
    for (i = 0; i < NELEM(MENU_CREDITS); i++) {
        const menu_credit_t *c = &MENU_CREDITS[i];
        const char *text = c->text;
        /* Display host/path once; the full source URL stays in the flash table. */
        if (c->kind == MC_URL && str_len(text) >= 8u && !memcmp(text, "https://", 8))
            text += 8;
        if (!text[0]) y += 8;
        else y = menu_text(text, y, c->kind == MC_TITLE ? T_THEME : c->kind == MC_URL ? T_MID : T_TEXT, draw);
    }
    y = menu_text("LICENSES + SOURCE", y + 12, T_THEME, draw);
    y = menu_text("DRUM + own samples: GPL-3.0-only", y, T_TEXT, draw);
    y = menu_text("Fonts and icons: their licences travel with the source and the release files.", y, T_TEXT, draw);
    y = menu_text("NO WARRANTY", y, T_TEXT, draw);
    y = menu_text("Full notices in source:", y + 8, T_TEXT, draw);
    y = menu_text("github.com/keremimo/melodee", y, T_MID, draw);
    return y + 4;
}
static int32_t menu_scroll_max(void)
{
    int32_t h = menu_document(0, 0);
    return h > MENU_DOC_H ? h - MENU_DOC_H : 0;
}

/* the menu's header (0..24): its title, the REC mark, the way back */
static void enc_drop(void);
#if MELODEE_BLE
#include "ui_ble.c"
#endif
static void menu_head(void)
{
    if (head_shared_status()
#if MELODEE_BLE
        || mb.on || ui.menu == 3
#endif
        ) { draw_head(); return; }
    cv_begin(240, MENU_HEAD, T_BG);
    cv_text(8, ty_y(MENU_HEAD / 2, TY_TX), TY_TX, ui.menu == 2 ? "About" : ui.menu == 4 ? "Touch a control" : "Settings", T_THEME);
    if (ui.menu == 2) {                               /* the way back at the right, the REC mark before it (then */
        cv_icon_mid(218, MENU_HEAD / 2, 16, ICON_X_UNDO, T_MID, T_BG);   /* (OCT-) */
        draw_rec_mark(196, T_BG);
    } else {
        draw_rec_mark(218, T_BG);
    }
    cv_blit(0, Y_HEAD);
}

/* the list (large type, C): six rows, 32 px, 34 apart from y 32 (ui_list.c row_draw): the name and its value at the
 * right in words ("Night", "0.4 s"); the selected row tinted in the track's colour, its value in it; COLOR: the
 * palettes as swatches, the one chosen outlined. Where the rows are in the list: a bar at the right edge. Drawn in two
 * bands (the canvas holds 124 rows), split between two rows */
#define MENU_VISIBLE 6u
#define MENU_Y0 32
#define MENU_ROW 34
#define MENU_RH 32
#define MENU_SPLIT (MENU_Y0 + 3 * MENU_ROW)
#include "ui_midi_learn.c"
static void draw_menu(void)
{
#if MELODEE_BLE
    if (ui.menu == 3) { draw_ble_page(); return; }
#endif
    if (ui.menu == 4) { draw_mlearn_page(); return; }
    uint32_t i, pass, sig = ui.menu * 7u + ui.menu_sel * 131u + settings.palette * 1009u + settings.lowcut * 7919u +
                            settings_hold * 3511u + settings_lights * 6151u + settings_autosave * 15485863u +
                            settings_midi_knobs * 997u + settings_midi_knob_mode * 10007u +
#if BOARD_SMK37
                            smk_vel_curve * 28657u +
#endif
#if MELODEE_USB_AUDIO
                            ua_off_want * 92821u + ua_tracks_want * 324161u +
#endif
                            song.rec * 65537u + song.sel * 13u +
                            (ui.menu == 2 ? ui.menu_scroll * 48611u : 0u);
    if (!ui.force && sig == ui.menu_sig) {
        if (head_shared_status()) draw_head();
        return;
    }
    ui.menu_sig = sig;
    menu_head();
    if (ui.menu >= 2) {
        int32_t scroll = ui.menu_scroll, max = menu_scroll_max();
        int32_t thumb = MENU_DOC_H * MENU_DOC_H / (max + MENU_DOC_H);
        if (thumb < 12) thumb = 12;
        for (pass = 0; pass < 2u; pass++) {
            for (uint32_t band = 0; band < (uint32_t)(pass ? MENU_DOC_H - 124u : 124u); band += CV_MAX / (240)) {
                cv_begin_band(240,pass ? MENU_DOC_H - 124u : 124u,T_BG,band);
                cv_oy = pass ? -124 : 0;
                cv_scroll = 1;
                menu_document(-scroll, 1);
                cv_scroll = 0;
                cv_rrect(235, 2, 3, MENU_DOC_H - 4, 1, T_LINE, T_BG);           /* the scroll bar */
                cv_rrect(235, 2 + (max ? scroll * (MENU_DOC_H - 4 - thumb) / max : 0), 3, thumb, 1, T_THEME, T_LINE);
                cv_oy = 0;
                cv_blit_band(0, MENU_HEAD + pass * 124u);
            }
        }
        return;
    }
    for (pass = 0; pass < 2u; pass++) {
        int32_t top = pass ? MENU_SPLIT : MENU_HEAD;
        for (uint32_t band = 0; band < (uint32_t)((uint32_t)(pass ? 240 - MENU_SPLIT : MENU_SPLIT - MENU_HEAD)); band += CV_MAX / (240)) {
            cv_begin_band(240,(uint32_t)(pass ? 240 - MENU_SPLIT : MENU_SPLIT - MENU_HEAD),T_BG,band);
            cv_oy = -top;                                 /* drawn in screen rows */
            uint32_t first=ui.menu_sel>=MENU_VISIBLE?ui.menu_sel-MENU_VISIBLE+1u:0u;
            for (i = first; i < first+MENU_VISIBLE && i<MI_COUNT; i++) {
                int32_t y = MENU_Y0 + (int32_t)(i-first) * MENU_ROW;
                int sel = i == ui.menu_sel;
                const char *value;
                char b[24], w[24], v[24];
                if (y + MENU_RH <= top || y >= top + (int32_t)cv_h)
                    continue;
                menu_words(w, MI_NAME[i], sizeof w);
                value = menu_value(i, menu_get(i), b);      /* (menu_rows.c) */
                v[0] = 0;
                if (value)
                    menu_words(v, value, sizeof v);
                if (i == MI_COLOR) {                        /* the palette's four track colours as swatches */
                    uint32_t k;
                    int32_t x;
                    uint16_t bg = sel ? ux_mix(T_BG, T_KNOB(i), 18) : T_BG;
                    x = row_draw(y, w, v, T_TEXT, sel, 0, i) - 10 - 16 * 4;
                    for (k = 0; k < 4u; k++)
                        cv_rrect(x + 16 * (int32_t)k + 2, y + MENU_RH / 2 - 5, 10, 10, 3, T_TRK(k), bg);
                } else {
                    row_draw(y, w, v, T_TEXT, sel, 0, i);
                }
            }
            {   /* where the rows are in the list: a bar at the right edge */
                int32_t th = MENU_VISIBLE * MENU_ROW * (int32_t)MENU_VISIBLE / (int32_t)MI_COUNT;
                int32_t ty = MENU_Y0 + (MI_COUNT > MENU_VISIBLE ? (MENU_VISIBLE * MENU_ROW - th) * (int32_t)first /
                                        (int32_t)(MI_COUNT - MENU_VISIBLE) : 0);
                cv_rect(238, MENU_Y0, 2, MENU_VISIBLE * MENU_ROW, T_LINE);
                cv_rect(238, ty, 2, th, T_THEME);
            }
            cv_oy = 0;
            cv_blit_band(0, (uint32_t)top);
        }
    }
}
static void enc_drop(void)                             /* knob turns nobody takes */
{
    uint32_t k;
    for (k = 0; k < NE; k++)
        panel_enc(k);
}

static void menu_close(void)
{
#if MELODEE_BLE
    mel_ble_scan(0);
#endif
    settings_save();                                   /* palette / panel table, if changed */
    ui.menu = 0;
    mlearn_on = 0;                                     /* (MIDI LEARN's page ends with the menu) */
    ui.force = 1;
    go_home();
}

/* menu: PRESETS moves, OCT+ confirms, OCT- cancels (ABOUT -> list -> close) */
static void menu_input(uint32_t oct)                  /* oct: ui_input.c oct_taps */
{
#if MELODEE_BLE
    if (ui.menu == 3) { ble_page_input(oct); return; }
#endif
    int32_t s;
    uint32_t ok = (oct >> 1) & 1u, back = oct & 1u;
    if (back) {
        if (ui.menu >= 2)
            ui.menu = 1, ui.force = 1;
        else
            menu_close();
        return;
    }
    if ((s = panel_enc(EN_K2)) != 0 && ui.menu == 1)  /* KNOB 2 the row (the standard; PRESETS too) */
        ui.menu_sel = (uint8_t)clamp((int32_t)ui.menu_sel + s, 0, (int32_t)MI_COUNT - 1);
    if ((s = panel_enc(EN_PRESET)) != 0 && ui.menu == 1)
        ui.menu_sel = (uint8_t)((ui.menu_sel + (s > 0 ? 1u : MI_COUNT - 1u)) % MI_COUNT);
    else if (s != 0 && ui.menu >= 2) {             /* bounded document scrolling */
        ui.menu_scroll = (uint16_t)clamp((int32_t)ui.menu_scroll + clamp(s, -128, 128) * 18, 0, menu_scroll_max());
    }
    s = panel_enc(EN_K1);
    if ((s > 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_MIDI_LEARN) {   /* KNOB 1 right or OCT+: its page */
        mlearn_page_open();
        return;
    }
    if (ok && ui.menu == 1 && ui.menu_sel == MI_MIDI_FORGET) {
        if (mlearn_count()) {
            ui.menu = 0;
            confirm_open(CF_MLEARN_FORGET, 0);
        }
        return;
    }
    /* a row's value: KNOB 1 steps it, OCT+ steps and wraps (COLOR's OCT+: below); saved when the menu closes */
    if (ui.menu == 1 && menu_kind(ui.menu_sel) <= MR_PERCENT && (s || (ok && ui.menu_sel != MI_COLOR))) {
        menu_put(ui.menu_sel, menu_step(ui.menu_sel, s));
        if (ui.menu_sel != MI_COLOR)
            ok = 0;
    }
    if (ok && ui.menu == 1) {
        switch (ui.menu_sel) {
        case MI_COLOR:                                 /* OCT+ steps through the palettes too */
            menu_put(MI_COLOR, menu_step(MI_COLOR, 0));
            break;
#if MELODEE_BLE
        case MI_BLE: ble_page_open(); break;
#endif
        case MI_PANEL:
            panel_setup();
            ui.force = 1;
            break;
        case MI_DESIGNER:
            menu_close();designer_open();
            break;
        case MI_ABOUT:
            ui.menu = 2;
            ui.menu_scroll = 0;
            ui.force = 1;
            break;
        default:
            menu_close();
            break;
        }
    }
    enc_drop();                                        /* swallow the rest while the menu is up */
}
