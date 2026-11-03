/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Mock screens of the SMK-37 ("Melodee Pro / Elite") UX (docs/SMK37_UX.md), drawn on the firmware's own drawing
 * code (tests/ui_render.c's scenes, firmware/src/ui*.c): the real pages as they are, with the parts the design adds
 * drawn over them here (a header tag, the knob or fader bank's cards in the strip, a fader's pickup gauge, the bank
 * picker). Nothing here is firmware behaviour: the firmware draws none of the additions yet.
 *   smk_ux_mocks OUTDIR      -> OUTDIR/ppm/NIGHT_<scene>.ppm (tools/smk_ux_mocks.py: PNGs beside the pad maps)
 * Built by tools/smk_ux_mocks.py as tests/ble_ui_render.c is (ui_render.c with UI_RENDER_NO_MAIN). */
#define UI_RENDER_NO_MAIN 1
#include "ui_render.c"

/* ------------------------------------------------------------ the additions --- */
/* the header again, with a tag left of the page name (the knob bank's name, FADERS while the faders' cards are up,
 * the pads' mode while it is set by hand): draw_head's layout, the tag as the HOST / BLE tags sit */
static void mk_head(const char *tag, const char *chip, int dots)
{
    char b[8];
    int32_t x, cw = text_w(TY_TX, chip) + 4, hw = tag[0] ? text_w(TY_LB, tag) + 10 : 0;
    fmt_int(b, song.g[G_BPM]);
    cv_begin(240, H_HEAD, T_BG);
    cv_icon_mid(5, H_HEAD / 2, 16, song.playing ? ICON_X_PLAY : ICON_X_STOP, song.playing ? T_TEXT : T_MID, T_BG);
    cv_text_on(BPM_X, ty_y(H_HEAD / 2, TY_TX), TY_TX, b, T_TEXT, T_BG);
    x = BPM_X + BPM_W;
    if (song.rec) {
        cv_icon_mid(x, H_HEAD / 2, 16, ((song.rec & ~song.rec_wait) >> song.sel) & 1u ? ICON_X_REC : ICON_X_REC_O, T_REC, T_BG);
        x += 18;
    }
    if (dots && !hw) {                                  /* (a page's dots, as head_pages draws them; a tag: they give way) */
        int32_t i;
        for (i = 0, x += 6; i < 5; i++) {
            cv_rrect(x, H_HEAD / 2 - 3, i == 1 ? 16 : 6, 6, 3, i == 1 ? T_THEME : T_LINE, T_BG);
            x += (i == 1 ? 16 : 6) + 6;
        }
    }
    if (hw)
        cv_text_on(236 - cw - hw + 4, ty_y(H_HEAD / 2, TY_LB), TY_LB, tag, T_THEME, T_BG);
    cv_text_r(236, ty_y(H_HEAD / 2, TY_TX), TY_TX, chip, T_THEME, T_BG);
    cv_blit(0, Y_HEAD);
}

/* the strip: a bank's four cards (draw_column, as a page's), hot the knob turning (-1 none), tc a card colour per
 * cell (0: the knob shades) */
static void mk_cards(const char *const lb[4], const char *const v[4], const char *const u[4], int hot, const uint16_t *tc)
{
    uint32_t c;
    uint8_t cs = col_style;
    ui.hot_col = (uint8_t)(hot < 0 ? 0 : hot);
    ui.hot_t = hot < 0 ? 0 : 30;
    memset(ui.col, 0, sizeof ui.col);                   /* (drawn fresh) */
    col_style = CS_CARD;
    if (tc) memcpy(card_tc, tc, sizeof card_tc);
    for (c = 0; c < 4u; c++)
        draw_column(c, lb[c], v[c], u[c], T_TEXT, -1, ICON_NONE);
    memset(card_tc, 0, sizeof card_tc);
    col_style = cs;
}

/* a fader's pickup gauge under its card (rows COL_H - 6 .. COL_H - 2 of the cell): the value's bar in the knob's
 * colour, a tick where the fader stands; not yet picked up (the fader away from the value): the bar in MID */
static void mk_pickup(uint32_t c, int32_t value, int32_t fader, int picked)
{
    const int32_t w = 42, x0 = COL_W / 2 - w / 2, y = COL_H - 6;
    uint16_t lc = picked ? T_KNOB(c) : T_MID;
    cv_begin(COL_W, 6, T_BG);
    cv_rrect(x0, y - (COL_H - 6) + 2, w, 2, 1, T_LINE, T_BG);
    cv_rrect(x0, y - (COL_H - 6) + 2, w * value / 127, 2, 1, lc, T_BG);
    cv_rect(x0 + w * fader / 127 - 1, 0, 2, 6, picked ? lc : T_TEXT);
    cv_blit((uint32_t)CARD_X(c), (uint32_t)(Y_LABEL + COL_H - 6));
}

/* the bank picker (pick_draw's card: five names around the one chosen) over the two left columns */
static void mk_picker(const char *const *name, int32_t n, int32_t now)
{
    int32_t rows = n < 5 ? n : 5, h = 12 + rows * 28, first = clamp(now - 2, 0, n - rows), i, pw = POP_PICK_W;
    for (uint32_t band = 0; band < (uint32_t)h; band += CV_MAX / (uint32_t)pw) {   /* (in bands, as pick_draw) */
        cv_begin_band((uint32_t)pw, (uint32_t)h, T_BG, band);
        cv_rrect(0, 0, pw, h, 10, T_LINE, T_BG);
        cv_rrect(1, 1, pw - 2, h - 2, 9, T_SURF, T_LINE);
        for (i = 0; i < rows; i++) {
            int32_t y = 6 + i * 28, on = first + i == now, far = first + i - now > 1 || now - first - i > 1;
            uint16_t bg = on ? ux_mix(T_SURF, T_THEME, 22) : T_SURF;
            char b[16];
            list_words(b, name[first + i], sizeof b);
            if (on)
                cv_rrect(4, y, pw - 8, 26, 6, bg, T_SURF);
            cv_free_text(10, ty_y(y + 13, TY_TX), TY_TX, b, on ? T_THEME : far ? T_MID : T_SEC, bg, pw - 18);
        }
        cv_blit_band((uint32_t)CARD_X(0), Y_LABEL + CARD_H + 2);
    }
}

/* the bank names (docs/SMK37_UX.md: one list for the knobs and the faders) */
static const char *const BANK[8] = {"Page", "Sound", "Filter", "Env", "LFO", "FX", "Arp", "Levels"};

/* ------------------------------------------------------------------ scenes --- */
enum { M_STAGE_KEYS, M_STAGE_DRUM, M_NOTES_STEPS, M_DRUM_STEPS, M_PATTERNS, M_SEQ_HELD, M_BANK_FILTER_NOTES,
       M_BANK_ENV_STAGE, M_FADERS_PAGE_ENV, M_FADERS_LEVELS, M_PICK_KNOBS, M_PICK_FADERS, M_LAYER_GLO, M_LAYER_SCL,
       M_PADS_OVERRIDE, M_NAME, M_COUNT };
static const char *const M_NAME_OF[M_COUNT] = {"stage_keys", "stage_drum", "notes_steps", "drum_steps", "patterns",
    "seq_held", "bank_filter_notes", "bank_env_stage", "faders_page_env", "faders_levels", "pick_knobs", "pick_faders",
    "layer_glo", "layer_scl", "pads_override", "name"};

static void mk_scene(int m)
{
    static const char *const U0[4] = {"", "", "", ""};
    switch (m) {
    case M_STAGE_KEYS:                                   /* Stage, T2 Prophet, a chord held: the pads play keys */
    case M_SEQ_HELD:                                     /* .. SEQ held: the pads are the tracks and patterns */
        setup(S_REF_STAGE_HELD); pal(0); draw(S_REF_STAGE_HELD);
        break;
    case M_STAGE_DRUM:                                   /* Stage, the 808 kit selected: the pads are its drums */
        setup(S_STAGE_DRUM); pal(0); draw(S_STAGE_DRUM);
        break;
    case M_PADS_OVERRIDE:                                /* .. the pads set to STEPS by hand: the header says so */
        setup(S_STAGE_DRUM); pal(0); draw(S_STAGE_DRUM);
        mk_head("STEPS", "808 KIT", 0);
        break;
    case M_NOTES_STEPS:                                  /* NOTES playing: the pads are the page's 16 steps */
        setup(S_NOTES_MIXED); song.playing = 1; trk[0].seq_idx = 5; pal(0); draw(S_NOTES_MIXED);
        break;
    case M_DRUM_STEPS:                                   /* the drum grid: the selected lane's steps on the pads */
        setup(S_DRUM); song.playing = 1; pal(0); draw(S_DRUM);
        break;
    case M_PATTERNS:                                     /* PATTERNS: the pads mirror the selected track's row */
        setup(S_PATGRID); song.sel = 1; pal(0); draw(S_PATGRID);
        break;
    case M_BANK_FILTER_NOTES: {                          /* NOTES, knob bank FILTER: the Prophet's filter on K1..K4 */
        static const char *const L[4] = {"CUTOFF", "RESO", "ENV AMT", "DECAY"};
        static const char *const V[4] = {"79", "38", "105", "61"};
        setup(S_NOTES_MIXED);
        set_engine_of(&trk[0], ENGI_PROPHET); apply_preset_to(&trk[0], 33); trk[0].engine = trk[0].eng_req;
        song.playing = 1; trk[0].seq_idx = 5; pal(0); draw(S_NOTES_MIXED);
        mk_head("FILTER", "NOTES", 1);
        mk_cards(L, V, U0, 0, 0);
        break;
    }
    case M_BANK_ENV_STAGE: {                             /* Stage, knob bank ENVELOPE: its cards drop in on a turn */
        static const char *const L[4] = {"ATK", "DEC", "SUS", "REL"};
        static const char *const V[4] = {"12", "48", "80", "40"};
        setup(S_REF_STAGE_CUTOFF); pal(0); draw(S_REF_STAGE_CUTOFF);
        mk_head("ENV", "PROPHET", 0);
        mk_cards(L, V, U0, 3, 0);
        break;
    }
    case M_FADERS_PAGE_ENV: {                            /* ENV, fader bank PAGE: the faders are the page's four; two
                                                         * not yet picked up (their bar in MID, the tick where the
                                                         * fader stands), fader 2 moving */
        static const char *const L[4] = {"ATK", "DEC", "SUS", "REL"};
        static const char *const V[4] = {"12", "48", "80", "40"};
        static const char *const U[4] = {"", "", "%", ""};
        setup(S_ENV); pal(0); draw(S_ENV);
        mk_head("FADERS", "ENV", 1);
        mk_cards(L, V, U, 1, 0);
        mk_pickup(0, 12, 12, 1); mk_pickup(1, 48, 48, 1); mk_pickup(2, 80, 30, 0); mk_pickup(3, 40, 98, 0);
        break;
    }
    case M_FADERS_LEVELS: {                              /* NOTES, fader bank LEVELS: tracks 1..4 in their colours */
        static const char *const L[4] = {"LEVEL", "LEVEL", "LEVEL", "LEVEL"};
        static const char *const V[4] = {"-6", "-14", "-24", "-1"};
        static const char *const U[4] = {"", "", "", ""};
        uint16_t tc[4];
        uint32_t k;
        setup(S_NOTES_MIXED); song.playing = 1; trk[0].seq_idx = 5; pal(0); draw(S_NOTES_MIXED);
        for (k = 0; k < 4u; k++) tc[k] = T_TRK(k);
        mk_head("FADERS", "NOTES", 1);
        mk_cards(L, V, U, 2, tc);
        mk_pickup(0, 100, 100, 1); mk_pickup(1, 84, 84, 1); mk_pickup(2, 64, 64, 1); mk_pickup(3, 110, 20, 0);
        break;
    }
    case M_PICK_KNOBS:                                   /* KNOB BANK held: the picker over the cards (K1 / < > walk it,
                                                         * a top-row pad picks); the cards are the bank chosen */
    case M_PICK_FADERS: {                                /* FADER BANK held: the same for the faders */
        static const char *const L[4] = {"CUTOFF", "RESO", "ENV AMT", "DECAY"};
        static const char *const V[4] = {"79", "38", "105", "61"};
        static const char *const LL[4] = {"LEVEL", "LEVEL", "LEVEL", "LEVEL"};
        static const char *const LV[4] = {"-6", "-14", "-24", "-1"};
        static const char *const LU[4] = {"", "", "", ""};
        uint16_t tc[4];
        uint32_t k;
        setup(S_REF_EDIT_OSC); pal(0); draw(S_REF_EDIT_OSC);
        for (k = 0; k < 4u; k++) tc[k] = T_TRK(k);
        if (m == M_PICK_KNOBS) {
            mk_head("", "KNOB BANK", 0);
            mk_cards(L, V, U0, -1, 0);
            mk_picker(BANK, 8, 2);
        } else {
            mk_head("", "FADER BANK", 0);
            mk_cards(LL, LV, LU, -1, tc);
            mk_picker(BANK, 8, 7);
        }
        break;
    }
    case M_LAYER_GLO:                                    /* GLO held: mutes and solos on the pads */
        setup(S_GLO_ACTIVE); pal(0); draw(S_GLO_ACTIVE);
        break;
    case M_LAYER_SCL:                                    /* SCALE held: the pads as a piano, the scale lit */
        setup(S_SCL_ACTIVE); pal(0); draw(S_SCL_ACTIVE);
        break;
    case M_NAME:                                         /* NAME: the letters on the pads, as now */
        setup(S_NAME_TYPING); pal(0); draw(S_NAME_TYPING);
        break;
    default:
        break;
    }
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "build/smk_ux";
    char path[600];
    int m;
    snprintf(path, sizeof path, "%s/report.txt", out);
    rep = fopen(path, "w");
    if (!rep) { fprintf(stderr, "cannot write %s\n", path); return 1; }
    for (m = 0; m < M_COUNT; m++) {
        cur_name = M_NAME_OF[m];
        mk_scene(m);
        write_ppm(out, "NIGHT", M_NAME_OF[m]);
    }
    fprintf(rep, "%d scenes\n", M_COUNT);
    fclose(rep);
    printf("smk_ux_mocks: %d scenes -> %s/ppm\n", M_COUNT, out);
    return 0;
}
