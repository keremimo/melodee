/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The HOME menu's rows (ui_menu.c draws and steps them) and what each holds, for the menu and the editor's SETTINGS
 * (editor_device.c, EDITOR_PROTOCOL.md "Studio device"): a row's kind (menu_kind), how many values (menu_count), the
 * value it has (menu_get), the value KNOB 1 or OCT+ takes it to (menu_step), a value set with what the menu does when it
 * changes it (menu_put), the words for a value (menu_value; shown through menu_words). MIDI LEARN's table edits too (its
 * page and the editor's MLEARN). Included by ui_menu.c, and by editor.c where no menu is built (the virtual FM-1, the
 * editor's host tests) */
#ifndef MENU_ROWS_C
#define MENU_ROWS_C
enum { MI_COLOR, MI_FONT, MI_HOLD, MI_LIGHTS,
#if MELODEE_USB_AUDIO
       MI_USB, MI_USB_INPUT,
#endif
#if MELODEE_USB_VOLUME
       MI_USB_VOLUME,
#endif
#if MELODEE_USB_HOST
       MI_USB_HOST,
#endif
#if MELODEE_BLE
       MI_BLE,
#endif
#if MELODEE_MIDI_THRU
       MI_THRU,
#endif
#if BOARD_SMK37
       MI_TRS, MI_VELOCITY,
#endif
       MI_MIDI_KNOBS, MI_MIDI_MODE, MI_MIDI_LEARN, MI_MIDI_FORGET,
       MI_CLICK, MI_CLICK_LEVEL, MI_COUNTIN, MI_PREVIEW, MI_ADD, MI_AUTOSAVE, MI_PICKER, MI_ACCEL, MI_HOME_SEL, MI_GLO_SEL, MI_TRACKS, MI_SCREEN, MI_PANEL, MI_ABOUT, MI_DESIGNER, MI_BACK,
       MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "FONT", "HOLD",
#if BOARD_SMK37
                                              "PAD LIGHTS",                 /* (the pads' level: ui_input.c smk_pad) */
#else
                                              "LIGHTS",
#endif
#if MELODEE_USB_AUDIO
                                              "USB AUDIO", "USB INPUT",
#endif
#if MELODEE_USB_VOLUME
                                              "USB VOLUME",
#endif
#if MELODEE_USB_HOST
                                              "USB HOST",
#endif
#if MELODEE_BLE
                                              "BLUETOOTH",
#endif
#if MELODEE_MIDI_THRU
                                              "MIDI THRU",
#endif
#if BOARD_SMK37
                                              "TRS MIDI OUT", "VELOCITY",
#endif
                                              "MIDI KNOBS", "MIDI MODE", "MIDI LEARN", "FORGET LEARNED",
                                              "AUDIO CLICK", "CLICK LEVEL", "COUNT-IN", "NOTE PREVIEW", "CHORD ENTRY",
                                              "AUTOSAVE", "PICKER POPUPS", "KNOB ACCEL", "HOME SELECT", "GLO SELECT", "UNUSED TRACKS", "SCREEN OFF", "CALIBRATION", "ABOUT", "SOUND DESIGNER", "BACK"};
static const char *const MI_MIDI_KNOB_NAME[MK_PROFILES] = {"OFF", "GENERAL", "MINILAB"};
#if BOARD_SMK37
static const char *const SMK_VEL_NAME[4] = {"NORMAL", "SOFT", "HARD", "FIXED"};
#endif
static const char *const MI_MIDI_MODE_NAME[MK_MODES] = {"ABSOLUTE", "REL 1", "REL 2", "REL 3"};

/* "AUDIO CLICK" -> "Audio click", "P5 OSC A" -> "Osc A" (acronyms and a letter alone stay capitals) */
static void menu_words(char *d, const char *s, uint32_t n)
{
    static const char *const AC[] = {"USB", "FX", "MIDI", "LFO", "EG", "FM", "OP", "PW", "DCW", "DCA", "DCO", "MPC", "GLO"};
    uint32_t i, w = 0, a;
    str_cpy(d, s, n);
    for (i = 0; d[i]; i++) {
        int first = !i, ac = 0;
        if (d[i] == ' ' || d[i] == '+' || d[i] == '-' || d[i] == '/') { w = i + 1u; continue; }
        if (i == w) {                                   /* a word: an acronym, a letter alone stay */
            for (a = 0; a < NELEM(AC) && !ac; a++) {
                uint32_t l = str_len(AC[a]);
                ac = !memcmp(d + w, AC[a], l) && (!d[w + l] || d[w + l] == ' ' || d[w + l] == '/' || (d[w + l] >= '0' &&
                     d[w + l] <= '9'));
            }
            ac |= !d[i + 1] || d[i + 1] == ' ';
        }
        if (ac) { while (d[i + 1] && d[i + 1] != ' ' && d[i + 1] != '/') i++; continue; }
        if (!first && d[i] >= 'A' && d[i] <= 'Z') d[i] = (char)(d[i] + 32);
    }
}

/* ---- MIDI LEARN's table (midi_knobs.h mlearn_row): what is learned, forget it all, a control's message */
static uint32_t mlearn_count(void)
{
    uint32_t n = 0;
    if (mlearn_valid())
        for (uint32_t k = 0; k < MLEARN_N; k++) n += mlearn_row[k] != 0u;
    return n;
}
static void mlearn_forget(void)
{
    fm1_irq_off();
    memset(mlearn_row, 0, sizeof mlearn_row);
    fm1_in.remote = 0;
    fm1_irq_on();
    settings_save();
}
static void mlearn_bind(uint32_t k, uint32_t src, uint32_t ch)   /* control k (knobs, then buttons) is message src now */
{
    fm1_irq_off();
    if (!mlearn_valid()) {                              /* (Felucca 1.0's stars of a retired engine, if any, go) */
        memset(mlearn_row, 0, sizeof mlearn_row);
        mlearn_row[28] = 'M'; mlearn_row[29] = 'L'; mlearn_row[30] = 1u;
    }
    for (uint32_t i = 0; i < MLEARN_N; i++)             /* the message leaves the control it had */
        if (mlearn_row[i] == src && (!(src & MLEARN_NOTE) || (i >= NE && mlearn_note_ch(i - NE) == ch)))
            mlearn_row[i] = 0;
    mlearn_row[k] = (uint8_t)src;
    if (k >= NE) {
        uint32_t b = k - NE, shift = b & 1u ? 4u : 0u;
        uint8_t *c = &mlearn_row[MLEARN_N + b / 2u];
        *c = (uint8_t)((*c & ~(15u << shift)) | ch << shift);
    }
    fm1_irq_on();
    settings_save();
}

/* ---- the rows. Kinds: a list of values KNOB 1 stops at both ends of (OCT+ steps and wraps), one KNOB 1 wraps (COLOR),
 * a percentage in steps of 5, a page of its own, an action */
enum { MR_ENUM, MR_CYCLE, MR_PERCENT, MR_PAGE, MR_ACTION };
#define MR_PCT 100u                                     /* a percentage's top (its count in MR_N) */
static const uint8_t MR_N[MI_COUNT] = {                 /* the values a row has (0: a page or an action) */
    [MI_COLOR] = NPALETTES, [MI_FONT] = 2, [MI_HOLD] = 4, [MI_LIGHTS] = LIGHTS_N,
#if MELODEE_USB_AUDIO
    [MI_USB] = 4, [MI_USB_INPUT] = 2,
#endif
#if MELODEE_USB_VOLUME
    [MI_USB_VOLUME] = MR_PCT,
#endif
#if MELODEE_USB_HOST
    [MI_USB_HOST] = 2,
#endif
#if MELODEE_MIDI_THRU
    [MI_THRU] = 2,
#endif
#if BOARD_SMK37
    [MI_TRS] = 2, [MI_VELOCITY] = 4,
#endif
    [MI_MIDI_KNOBS] = MK_PROFILES, [MI_MIDI_MODE] = MK_MODES, [MI_CLICK] = 3, [MI_CLICK_LEVEL] = MR_PCT, [MI_COUNTIN] = 3,
    [MI_PREVIEW] = 2, [MI_ADD] = 2, [MI_AUTOSAVE] = 2, [MI_PICKER] = 2, [MI_ACCEL] = 2, [MI_HOME_SEL] = HS_N,
    [MI_GLO_SEL] = HS_N, [MI_TRACKS] = 2, [MI_SCREEN] = 5,
};
static uint32_t menu_kind(uint32_t i)
{
    uint32_t n = MR_N[i];
    return i == MI_COLOR ? MR_CYCLE : n == MR_PCT ? MR_PERCENT : n ? MR_ENUM : i == MI_MIDI_FORGET || i == MI_BACK ? MR_ACTION : MR_PAGE;
}
static uint32_t menu_hs(uint32_t v)                     /* HOME / GLO SELECT: v's place in the menu's order */
{
    uint32_t i = 0;
    while (i + 1u < HS_N && HS_ORDER[i] != v) i++;
    return i;
}
static uint32_t menu_get(uint32_t i)                    /* the row's value (MIDI LEARN: the controls learned) */
{
    switch (i) {
    case MI_COLOR: return settings.palette;
    case MI_FONT: return TY_PIXEL;
    case MI_HOLD: return settings_hold % 4u;
    case MI_LIGHTS: return settings_lights % LIGHTS_N;
#if MELODEE_USB_AUDIO
    case MI_USB: return ua_off_want & 3u;                /* (ua_off_want: UA_OFF_OUT | UA_OFF_IN) */
    case MI_USB_INPUT: return ua_tracks_want;
#endif
#if MELODEE_USB_VOLUME
    case MI_USB_VOLUME: return settings_usb_volume;
#endif
#if MELODEE_USB_HOST
    case MI_USB_HOST: return uh.want;
#endif
#if MELODEE_MIDI_THRU
    case MI_THRU: return !!(GLO_SELECT & MIDI_THRU_ON);
#endif
#if BOARD_SMK37
    case MI_TRS: return !(GLO_SELECT & TRS_OUT_OFF);
    case MI_VELOCITY: return smk_vel_curve % 4u;
#endif
    case MI_MIDI_KNOBS: return settings_midi_knobs % MK_PROFILES;
    case MI_MIDI_MODE: return settings_midi_knob_mode % MK_MODES;
    case MI_MIDI_LEARN: return mlearn_count();
    case MI_CLICK: return settings_click;
    case MI_CLICK_LEVEL: return settings_click_level;
    case MI_COUNTIN: return settings_countin;
    case MI_PREVIEW: return settings_preview;
    case MI_ADD: return settings_chord_add;
    case MI_AUTOSAVE: return settings_autosave;
    case MI_PICKER: return !(PREF_BITS & PREF_PICK_OFF);
    case MI_ACCEL: return !(PREF_BITS & PREF_ACCEL_OFF);
    case MI_HOME_SEL: return menu_hs(home_select());
    case MI_GLO_SEL: return menu_hs(glo_select());
    case MI_TRACKS: return !!(GLO_SELECT & TRACKS_HIDE);
    case MI_SCREEN: return scr_get();
    default: return 0;
    }
}
/* KNOB 1 (s detents) or OCT+ (s 0) on row i: the value it goes to. A list steps one at a time and stops at its ends,
 * OCT+ steps and wraps; COLOR wraps both ways; a percentage steps 5 a detent, OCT+ 5 and from 100 to 0 */
static uint32_t menu_step(uint32_t i, int32_t s)
{
    uint32_t n = MR_N[i], v = menu_get(i);
    if (n == MR_PCT)
        return (uint32_t)(s ? clamp((int32_t)v + s * 5, 0, 100) : v + 5u > 100u ? 0 : (int32_t)v + 5);
    if (i == MI_COLOR)
        return (v + (s < 0 ? n - 1u : 1u)) % n;
    return s > 0 ? (v + 1u < n ? v + 1u : v) : s < 0 ? (v ? v - 1u : 0u) : (v + 1u) % n;
}
static void menu_put(uint32_t i, uint32_t v)            /* row i's value v (the menu's: in range), as the menu sets it */
{
    switch (i) {
    case MI_COLOR: settings.palette = (uint8_t)v; palette_set(v); break;
    case MI_FONT:                                       /* (palette_set: ux.gen, every part's signature, the rolls redraw) */
        PREF_BITS = (uint8_t)(v ? PREF_BITS | PREF_PIXEL : PREF_BITS & ~PREF_PIXEL);
        palette_set(settings.palette);
        break;
    case MI_HOLD: settings_hold = (uint8_t)v; return;    /* (these four: the menu's signature redraws) */
    case MI_LIGHTS: settings_lights = (uint8_t)v; return;
#if MELODEE_USB_AUDIO
    case MI_USB:                                        /* the host gets it once it rests (main.c) */
        ua_off_set(UA_OFF_OUT, !(v & UA_OFF_OUT), fm1_ms);
        ua_off_set(UA_OFF_IN, !(v & UA_OFF_IN), fm1_ms);
        return;
    case MI_USB_INPUT: ua_tracks_set((int)v, fm1_ms); return;
#endif
#if MELODEE_USB_VOLUME
    case MI_USB_VOLUME: settings_usb_volume = (uint8_t)v; break;
#endif
#if MELODEE_USB_HOST
    case MI_USB_HOST: uh.want = (uint8_t)v; break;     /* (not saved: the next boot is a device again) */
#endif
#if MELODEE_MIDI_THRU
    case MI_THRU: GLO_SELECT = (uint8_t)(v ? GLO_SELECT | MIDI_THRU_ON : GLO_SELECT & ~MIDI_THRU_ON); break;
#endif
#if BOARD_SMK37
    case MI_TRS: GLO_SELECT = (uint8_t)(v ? GLO_SELECT & ~TRS_OUT_OFF : GLO_SELECT | TRS_OUT_OFF); break;
    case MI_VELOCITY: smk_vel_curve = (uint8_t)v; break;   /* (keys and pads, smk_input.h) */
#endif
    case MI_MIDI_KNOBS: case MI_MIDI_MODE:
        *(i == MI_MIDI_KNOBS ? &settings_midi_knobs : &settings_midi_knob_mode) = (uint8_t)v;
        memset(midi_panel_delta, 0, sizeof midi_panel_delta);   /* (no old mapping's turns edit rows under the new one) */
        midi_panel_mask = 0;
        break;
    case MI_CLICK: settings_click = (uint8_t)v; break;
    case MI_CLICK_LEVEL: settings_click_level = (uint8_t)v; break;
    case MI_COUNTIN: settings_countin = (uint8_t)v; break;
    case MI_PREVIEW: settings_preview = (uint8_t)v; break;
    case MI_ADD: settings_chord_add = (uint8_t)v; break;
    case MI_AUTOSAVE: settings_autosave = (uint8_t)v; break;
    case MI_PICKER: PREF_BITS = (uint8_t)(v ? PREF_BITS & ~PREF_PICK_OFF : PREF_BITS | PREF_PICK_OFF); break;
    case MI_ACCEL: PREF_BITS = (uint8_t)(v ? PREF_BITS & ~PREF_ACCEL_OFF : PREF_BITS | PREF_ACCEL_OFF); break;
    case MI_HOME_SEL: HOME_SELECT = HS_ORDER[v]; break;
    case MI_GLO_SEL: glo_select_set(HS_ORDER[v]); break;
    case MI_TRACKS: GLO_SELECT = (uint8_t)(v ? GLO_SELECT | TRACKS_HIDE : GLO_SELECT & ~TRACKS_HIDE); break;
    case MI_SCREEN: scr_put(v); break;
    default: return;
    }
    ui.force = 1;
}
/* value v's words, as the menu shows them (through menu_words); 0: none (a page, an action). b: 24 bytes */
static const char *menu_value(uint32_t i, uint32_t v, char *b)
{
    switch (i) {
    case MI_COLOR: return UI_PALETTES[v % NPALETTES].name;
    case MI_FONT: return v ? "PIXEL" : "RUBIK";
    case MI_HOLD:                                       /* "0.4 s" */
        str_cpy(b, "0.4 s", 24);
        b[2] = (char)('0' + HOLD_MS[v % 4u] / 100u);
        return b;
    case MI_LIGHTS: return LIGHTS_NAME[v % LIGHTS_N];
#if MELODEE_USB_AUDIO
    case MI_USB: return (const char *const[]){"In + Out", "In", "Out", "Off"}[v & 3u];
    case MI_USB_INPUT: return v ? "4 TRACKS" : "STEREO";
#endif
#if MELODEE_USB_VOLUME
    case MI_USB_VOLUME:
#endif
    case MI_CLICK_LEVEL:
        fmt_int(b, v);
        str_cpy(b + str_len(b), "%", 4);
        return b;
#if BOARD_SMK37
    case MI_VELOCITY: return SMK_VEL_NAME[v % 4u];
#endif
    case MI_MIDI_KNOBS: return MI_MIDI_KNOB_NAME[v % MK_PROFILES];
    case MI_MIDI_MODE: return MI_MIDI_MODE_NAME[v % MK_MODES];
    case MI_MIDI_LEARN:                                 /* how many controls are learned */
        if (!v) return "NONE";
        fmt_int(b, v);
        str_cpy(b + str_len(b), " LEARNED", 9);
        return b;
    case MI_CLICK: return (const char *const[]){"OFF", "REC", "ON"}[v % 3u];
    case MI_COUNTIN: return (const char *const[]){"OFF", "1 BAR", "2 BARS"}[v % 3u];
    case MI_ADD: return v ? "ADD" : "HOLD";
    case MI_HOME_SEL: case MI_GLO_SEL: return HS_NAME[HS_ORDER[v % HS_N]];
    case MI_TRACKS: return v ? "HIDE" : "SHOW";
    case MI_SCREEN: return (const char *const[]){"NEVER", "5 MIN", "15 MIN", "30 MIN", "60 MIN"}[v % 5u];
    default: return MR_N[i] ? (v ? "ON" : "OFF") : 0;   /* the switches: NOTE PREVIEW, AUTOSAVE, PICKER POPUPS, ... */
    }
}
#endif
