/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Melodee quick layers: while a page button is held, the keys and KNOB 1..4
 * are its shortcuts and its map shows over the page. One table (LAYERS) drives the gesture, the keys, the knobs,
 * the LEDs and the overlay:
 *   GLO  SET   black keys 1..4 (F#3 G#3 A#3 C#4) T1..T4 MUTE (latched; lit = sounding), F3..B3 SOLO T1..T4 while
 *              held (HOLD cells, a corner triangle), C4 UNMUTE ALL, F4 TAP tempo; KNOB 1..4 T1..T4 LEVEL;
 *              GLO + PLAY: from the top without stopping
 *   SCL  SET   any key: its note name is ROOT; KNOB 1..4 ROOT SCL CHRD VOIC (LY_SCL: the SCL page's first two,
 *              the CHORD page's two; QNT TRN stay on SCL); the LEDs show the root lit and the scale's notes blinking
 * The gesture: let go before HOLD (the menu: 0.3 .. 0.6 s) with nothing else touched: a tap, the button's page.
 * Held past HOLD alone: the map (a peek), letting go does nothing. A key, a knob or a button meanwhile: a combo,
 * the map at once, no tap. Keys pressed with the button down are the layer's (seq.c keyboard_block): silent, no
 * MIDI, never recorded; keys held before stay notes. Only one layer at a time: a second layer button is ignored.
 * PLAY and REC work in every layer (GLO + PLAY: RESTART); SAVE, HOME, SEQ and the other page buttons are swallowed. In SET
 * layers OCT± do not shift the octave: OCT- (on release, not with OCT+) puts back what the layer changed since it
 * opened. A HOLD key's effect lasts until the key is let go, the map with it. No layer in the menu, a dialog,
 * NAME or the UPDATE MODE countdown. A tap opens the button's page without a shortcut hint. */
enum { LK_HOLD, LK_SET };
typedef struct {
    uint8_t btn, kind, fam;            /* the button, HOLD / SET, the family whose first page KNOB 1..4 edit */
    const char *head;                  /* (FAM_HOME: the layer's own knobs) the header over the map */
    khint_t foot[3];                   /* the footer's key hints (a third with no word: none) */
} layer_t;
static const layer_t LAYERS[LAYER_N] = {
    {0, 0, 0, "", {{0, 0}, {0, 0}, {0, 0}}},
    {B_FX, LK_HOLD, FAM_FX, "[FX] HOLD", {{KC_KEYS, "PUNCH"}, {0, 0}, {0, 0}}},
    {B_GLO, LK_SET, FAM_HOME, "[GLO] SET", {{KC_PLAY, "RESTART"}, {KC_OCTDN, "UNDO"}, {KC_GLO, "DONE"}}},
    {B_SCL, LK_SET, FAM_SCL, "[SCL] SET", {{KC_KEYS, "ROOT"}, {KC_OCTDN, "UNDO"}, {KC_SCL, "DONE"}}},
};
/* SCL's knobs: the key and its chord (cur_page() while the layer edits or draws them: page_over) */
static const page_t LY_SCL = {"SCL", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, P_SCALE, P_CHRD, P_VOIC}};
#define LY_OPEN 2u                     /* ui.ly_t0: the map opened (no tap any more) */
#define LY_COMBO 4u                    /* .. by a combo */
#define LY_DEAD 8u                     /* .. pressed where there is no layer: does nothing */

static struct {
    uint8_t l, trk, oct;               /* SET: layer, snapshot track, OCT- / OCT+ pressed (bits) */
    int16_t v[9];                      /* the values when it opened: GLO mutes, levels, BPM; SCL ROOT..TRN, CHRD VOIC */
    uint32_t solo;                     /* GLO: the keys held that solo */
    uint32_t tap[4];                   /* GLO TAP: the last taps (fm1_ms) */
    uint8_t ntap, dt_l, dtap, rp_dirty;
    uint32_t dt_ms;
    struct { uint8_t home, page, act, seq, fam[FAM_COUNT]; } nv;
} lys;

static const uint32_t LY_DTAP_MS=300u;



static int layer_allowed(void) { return !ui.menu && !ui.confirm && !ui.uboot && !name_on() && !new_on() && !pop.on && !smap.on; }
static uint32_t ly_bit(uint32_t l) { return 1u << panel.btn[LAYERS[l].btn]; }
static uint32_t ly_down(uint32_t l) { return l && ((fm1_in.buttons & ly_bit(l)) != 0u || ui.lock == l); }
static uint32_t layer_bits(void)
{
    uint32_t l, m = 0;
    for (l = LAYER_FX; l < LAYER_N; l++)
        m |= ly_bit(l);
    if (step_modifier_context())
        m &= ~step_modifier_mask();                      /* synth STEP: SCL, EDIT edit notes */
    return m;
}
static uint32_t layer_btn(void) { return LAYERS[ui.layer % LAYER_N].btn; }
static const char *layer_head(void)
{
    static const char *const locked[LAYER_N] = {"", "[FX] LOCK", "[GLO] LOCK", "[SCL] LOCK"};
    if (ui.layer == LAYER_FX && punch_memory_failed)    /* (a key that needs the history pressed without it) */
        return "[FX] MEMORY";
    return ui.lock ? locked[ui.layer % LAYER_N] : LAYERS[ui.layer % LAYER_N].head;
}
static uint32_t layer_open(void) { return ui.ly && (ui.ly_t0 & LY_OPEN) && ly_down(ui.ly) && layer_allowed() ? ui.ly : 0u; }
static int layer_set_open(void) { return layer_open() && LAYERS[layer_open()].kind==LK_SET; }
static uint32_t layer_held(void) { return ui.ly && ly_down(ui.ly) && layer_allowed(); }

/* each pass, before the keys: what the ISR (seq.c keyboard_block) gives to a layer. Armed: its button; none
 * armed: any layer button not already down (one pressed meanwhile is armed below; one held over from before
 * is ignored, its keys stay notes); armed but dead (pressed under a dialog, a menu or NAME that has closed
 * since): nothing, its keys stay notes */
static void layer_masks(void)
{
    uint32_t m = !layer_allowed() || (ui.ly && (ui.ly_t0 & LY_DEAD)) ? 0u
               : ui.ly ? ly_bit(ui.ly) : layer_bits() & ~fm1_in.buttons;
    kb_mask = m | (layer_allowed() && !ui.ly ? 1u << panel.btn[B_SEQ] : 0u);

    kb_lock = m && ui.lock ? 1u : 0u;
    punch_panel_mask = (m & ly_bit(LAYER_FX));
    punch_panel_lock = punch_panel_mask && ui.lock == LAYER_FX;
    if (!layer_allowed()) { punch.req=-1; punch.keybit=0; }

}

/* a layer button pressed (its edge) while none is armed: it is now, a dead one where no layer opens */
static void layer_arm(uint32_t pressed, uint32_t now)
{
    uint32_t l;
    if (ui.ly)
        return;
    for (l = LAYER_FX; l < LAYER_N; l++)
        if (pressed & ly_bit(l) & layer_bits()) {
            lys.dtap=l!=LAYER_SCL && lys.dt_l==l && fm1_ms-lys.dt_ms<=LY_DTAP_MS;
            lys.dt_l=0;
            ui.ly = (uint8_t)l;
            ui.ly_t0 = (now & ~15u) | 1u | (layer_allowed() ? 0u : LY_DEAD);
            return;
        }
}

/* the layer opened: SET: what OCT- will put back */
static void layer_opened(uint32_t l)
{
    uint32_t k;
    lys.l = (uint8_t)l;
    lys.trk = song.sel;
    if (l == LAYER_FX) return;
    for (k = 0; k < 4u; k++)
        if (l == LAYER_GLO) {                           /* (the selected track's four) */
            lys.v[k] = trk[trk_group() + k].p[P_MUTE];
            lys.v[4u + k] = trk[trk_group() + k].p[P_LEVEL];
        } else {
            lys.v[k] = TSEL->p[P_ROOT + k];
            lys.v[4u + k] = k < 2u ? TSEL->p[P_CHRD + k] : 0;
        }
    lys.v[8] = song.g[G_BPM];
}


static void layer_let_go(uint32_t quiet)
{
    (void)quiet;

    if (lys.rp_dirty) { lys.rp_dirty=0; settings_save(); }
    ui.ly_t0=0; ui.ly=0; ui.lock=0; kb_lock=0;
}
static void layer_lock_input(uint32_t pressed)
{
    uint32_t keep = 1u << panel.btn[B_PLAY] | 1u << panel.btn[B_REC] | 1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP];
    if (lys.dt_l && (pressed & ~ly_bit(lys.dt_l)))
        lys.dt_l = 0;
    if (!ui.lock)
        return;
    if (pressed & ly_bit(ui.lock))
        ui.lock = 0;
    else if (pressed & ~keep)
        layer_let_go(1);
}

/* the armed button: 0, or the layer whose button was tapped (let go) */
static uint32_t layer_gesture(uint32_t now, uint32_t combo)
{
    uint32_t *t0 = &ui.ly_t0, l = ui.ly;
    if (!l)
        return 0;
    if (!layer_allowed()) {
        *t0 |= LY_DEAD;
        ui.lock = 0;                                    /* (the menu, a dialog, NAME: the lock closes) */
    }
    if (ly_down(l)) {
        if (!(*t0 & (LY_OPEN | LY_DEAD)) &&
            (combo || now - (*t0 & ~15u) >= (uint32_t)HOLD_MS[settings_hold % 4u] * 1000u * FM1_TICKS_PER_US)) {
            *t0 |= LY_OPEN | (combo ? LY_COMBO : 0u);
            layer_opened(l);
        }
        return 0;
    }
    l = *t0 & (LY_OPEN | LY_DEAD) || combo ? 0u : l;   /* (a knob turned as it was let go: a combo, no tap) */
    if (l && lys.dtap) {                                /* #83: a double tap: locked open (no tap) */
        lys.dtap = 0;
        ui.home=lys.nv.home; ui.page=lys.nv.page; ui.act=lys.nv.act;
        memcpy(ui.fam_last,lys.nv.fam,sizeof ui.fam_last); song.seq_mode=lys.nv.seq;
        ui.entry_open=0; ui.msg_t=0; ui.lock=(uint8_t)l;
        ui.ly_t0=(now&~15u)|1u|LY_OPEN; ui.force=1; layer_opened(l);
        return 0;
    }
    layer_let_go(*t0 & LY_OPEN || combo);
    return l;
}

/* the map shows while the button is held open, and after it while its keys are still held */
static void layer_show(void)
{
    uint32_t show = layer_allowed() ? (layer_open() ? layer_open() : kb_layer ? ui.layer : 0u) : 0u;
    if (show != ui.layer) {
        ui.layer = (uint8_t)show;
        ui.force = 1;
    }
}

/* a tap: the button's page; keep the previous navigation state for a double tap */
static void layer_tap(uint32_t l)
{
    lys.dt_l=(uint8_t)l; lys.dt_ms=fm1_ms;
    lys.nv.home=ui.home; lys.nv.page=ui.page; lys.nv.act=ui.act; lys.nv.seq=song.seq_mode;
    memcpy(lys.nv.fam,ui.fam_last,sizeof ui.fam_last);
    page_tap(LAYERS[l].btn);
}

/* GLO TAP: from the third tap the tempo of the taps (the last 4); 2 s without one starts over. INT clock only */
static void glo_tap(void)
{
    uint32_t n = lys.ntap, i;
    if (song.g[G_CLOCK]) {
        ui_message("TAP: CLK IS EXT");
        return;
    }
    if (n && fm1_ms - lys.tap[n - 1u] > 2000u)
        n = 0;
    if (n == 4u) {
        for (i = 0; i < 3u; i++)
            lys.tap[i] = lys.tap[i + 1u];
        n = 3;
    }
    lys.tap[n++] = fm1_ms;
    lys.ntap = (uint8_t)n;
    if (n >= 3u && lys.tap[n - 1u] != lys.tap[0]) {
        song.g[G_BPM] = (int16_t)clamp((int32_t)(60000u * (n - 1u) / (lys.tap[n - 1u] - lys.tap[0])),
                                       GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;
    }
}

/* a key pressed in layer l (k: 0 = F3 .. 26 = G5) */
static void layer_key(uint32_t l, uint32_t k)
{
    uint32_t p = key_place(k), i, g = trk_group();
    if (l == LAYER_GLO) {                               /* (the selected track's four: 1..4, 5..8) */
        if (key_black(k)) {
            if (g + p < trk_n())
                trk[g + p].p[P_MUTE] = (int16_t)!trk[g + p].p[P_MUTE];
        } else if (p < NPART) {
            if (g + p < trk_n())                        /* (a track not in use: nothing) */
                lys.solo |= 1u << k;                    /* (layer_masks: track_solo while held) */
        } else if (p == 4u) {
            for (i = 0; i < NTRK; i++)
                trk[i].p[P_MUTE] = 0;
        } else if (p == 7u) {
            glo_tap();
        }
    } else if (l == LAYER_SCL) {
        TSEL->p[P_ROOT] = (int16_t)((k + 5u) % 12u);    /* the key's note name (F3 = F) */
        scale_share(TSEL);
    }
}
/* each pass: the keys the layer got now; then GLO's solo: the tracks of its keys still held (perform.c) */
static void layer_keys(uint32_t keys)
{
    uint32_t k, l = layer_open() ? layer_open() : ui.layer, s = 0;
    for (k = 0; keys && k < 27u; k++)
        if ((keys >> k) & 1u)
            layer_key(l, k);
    lys.solo &= kb_layer;
    for (k = 0; k < 27u; k++)
        if ((lys.solo >> k) & 1u)
            s |= 1u << (trk_group() + key_place(k));
    track_solo = (uint8_t)s;
}

/* KNOB 1..4 in the open layer: its own (GLO levels), else the four of its page */
static void layer_knob(uint32_t k, int32_t s)
{
    uint32_t l = layer_open();
    if (l == LAYER_FX) {                                /* KNOB 1: along the keys (the one shown) */
        if (!k) ui.punch_view = (uint8_t)punch_fx_of_key((uint32_t)clamp((int32_t)punch_key_of_fx(ui.punch_view) + s, 0, 26));
        return;
    }
    if (l == LAYER_GLO) {                 /* T1..T4 (T5..T8) LEVEL (those in use, with a level), recorded as on MIXER */
        uint32_t t = trk_group() + k;
        int16_t *vp = &trk[t].p[P_LEVEL];
        if (t >= trk_n() || midi_track_only(&trk[t]))
            return;
        *vp = (int16_t)clamp(*vp + accel(EN_K1 + k, s, TP[P_LEVEL].max - TP[P_LEVEL].min), TP[P_LEVEL].min, TP[P_LEVEL].max);
        motion_capture(&trk[t], P_LEVEL, *vp);
    } else if (l) {                                     /* the page's knobs, wherever the page is */
        uint8_t h = ui.home, pg = ui.page;
        ui.home = 0;
        ui.page = (uint8_t)page_first(LAYERS[l].fam);
        page_over = l == LAYER_SCL ? &LY_SCL : 0;
        edit_param(k, s);
        page_over = 0;
        ui.home = h;
        ui.page = pg;
    }
}

/* PLAY with GLO held open: RESTART (from the top, playing on); stopped: PLAY. 1 = done here */
static int layer_play(void)
{
    if (layer_open() != LAYER_GLO || chain_busy())
        return 0;
    if (song.g[G_CLOCK] && song.playing) {
        ui_message("RESTART: CLK IS EXT");
        return 1;
    }
    transport_req = song.playing ? 3u : 1u;
    return 1;
}

/* OCT- / OCT+ in a SET layer: no octave; OCT- let go puts back what the layer changed. Returns the taps left */
static uint32_t layer_oct(uint32_t pressed, uint32_t oct)
{
    uint32_t dn = panel.btn[B_OCTDN], up = panel.btn[B_OCTUP], k, mine;
    if (layer_set_open())
        lys.oct |= (uint8_t)(((pressed >> dn) & 1u) | ((pressed >> up) & 1u) << 1);
    mine = oct & lys.oct;
    if (mine & 1u) {                                    /* OCT-: back to when it opened */
        if (lys.l == LAYER_GLO) {
            for (k = 0; k < 4u; k++) {                  /* (the four of the track selected when it opened) */
                trk[lys.trk / NPART * NPART + k].p[P_MUTE] = lys.v[k];
                trk[lys.trk / NPART * NPART + k].p[P_LEVEL] = lys.v[4u + k];
            }
            song.g[G_BPM] = lys.v[8];
            ui_message("MIX PUT BACK");
        } else {
            for (k = 0; k < 4u; k++)
                trk[lys.trk % NTRK].p[P_ROOT + k] = lys.v[k];
            trk[lys.trk % NTRK].p[P_CHRD] = lys.v[4];
            trk[lys.trk % NTRK].p[P_VOIC] = lys.v[5];
            scale_share(&trk[lys.trk % NTRK]);
            ui_message("SCALE PUT BACK");
        }
    }
    lys.oct &= (uint8_t)(((fm1_in.buttons >> dn) & 1u) | ((fm1_in.buttons >> up) & 1u) << 1);
    return oct & ~mine;
}

/* --------------------------------------------------------- the LEDs --- */
/* lit = in effect now (held, the value, a track sounding), slow blink = can be pressed, dark = nothing there */
static int glo_sounding(uint32_t t) { return !trk[t].p[P_MUTE] && (!track_solo || ((track_solo >> t) & 1u)); }
static uint32_t layer_leds(void)
{
    uint32_t k, m = 0, blink = ((fm1_ms / 250u) & 1u) == 0u, l = ui.layer;
    uint32_t mask = scale_mask(TSEL), root = (uint32_t)TSEL->p[P_ROOT] % 12u;
    if (l == LAYER_FX) return punch_selector_leds(punch.req);
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k), b = (uint32_t)key_black(k), on = 0, e;
        if (l == LAYER_GLO) {
            uint32_t t = trk_group() + p;
            on = b ? t < trk_n() && glo_sounding(t) :
                 p < NPART ? t < trk_n() && (((lys.solo >> k) & 1u) | blink) : p == 4u ? blink :
                 p == 7u && !song.g[G_CLOCK] && blink;
        } else if (l == LAYER_SCL) {                    /* the root lit, the scale's notes blink */
            e = (k + 5u + 12u - root) % 12u;
            on = e == 0u || (((mask >> e) & 1u) && blink);
        }
        m |= on << k;
    }
    return m;
}

/* ------------------------------------------------------ the overlay --- */
/* One template: the header names the button and the kind ("[GLO] SET"); the cards are KNOB 1..4; the panel the
 * map of the keys as cells (the key's note name, an icon, a name); the footer the keycaps. A cell: RAISE
 * (can be pressed), the selection's fill (the value now, SET), the accent (held, HOLD), DIM (cannot now: pressing
 * it says why), KEY (a muted track, as the MUTE badge); HOLD cells in a SET layer: a corner triangle */
#define LY_ROW 24                                        /* (large type) the SCL cells: 22 px, 24 apart */
enum { LS_OFF, LS_SEL, LS_HELD, LS_WAIT, LS_DIM, LS_MUTE };

static uint16_t lc_fill(uint32_t st, uint16_t *ink)
{
    *ink = st == LS_DIM ? T_DIM : T_THEME;
    if (st == LS_MUTE) {
        *ink = T_INK;
        return T_KEY;
    }
    if (st == LS_HELD) {
        *ink = T_BG;
        return T_ACCENT;
    }
    if (st == LS_SEL || st == LS_WAIT) {
        *ink = T_INK;
        return T_THEME;
    }
    return T_RAISE;
}
static void layer_scl(void)                             /* KNOB 2's scales as a list (their names in full), four
                                                         * around the one now selected (filled) */
{
    uint32_t i, sc = (uint32_t)TSEL->p[P_SCALE], max = (uint32_t)TP[P_SCALE].max, first;
    first = sc > 1u ? sc - 1u : 0u;
    if (max >= 3u && first > max - 3u)
        first = max - 3u;
    for (i = first; i < first + 4u && i <= max; i++) {
        int32_t y = 3 + 30 * (int32_t)(i - first);
        uint16_t ink, fill = lc_fill(i == sc ? LS_SEL : LS_OFF, &ink);
        const char *t = SCALE_TITLE[i];
        const aafont_t *f = ty_fit(TY_TX, t, 200);
        if (i == sc)
            cv_rrect(8, y, 224, 28, 8, fill, T_SURF);
        cv_free_text(18, ty_y(y + 14, f), f, t, i == sc ? ink : T_SEC, i == sc ? fill : T_SURF, 200);
    }
}
/* GLO held (docs/design: mock/r6_pages GLO): the keys as a keyboard: the black keys MUTE 1..4 (muted: red), the white
 * keys SOLO 1..4 (held: the track's colour), C unmutes all, the high F taps the tempo (an outside clock: dim). The
 * selected track's four: 5..8 for a MIDI-only one */
static void layer_glo_keys(int32_t h)
{
    static const char *const WL[8] = {"Solo", "Solo", "Solo", "Solo", "Mute", "", "", "Tap"};
    static const uint8_t BX[NPART] = {0, 1, 2, 4};      /* black key t: between white BX and BX + 1 (F# G# A# C#) */
    const int32_t kw = 28, x0 = 1, wy = 50, by = 6, bh = 40;
    uint32_t i, g = trk_group();
    for (i = 0; i < 8u; i++) {                          /* the white keys: the solos' numerals, All (unmute), Tap */
        int32_t x = x0 + (int32_t)i * (kw + 1);
        int on = i < NPART && ((track_solo >> (g + i)) & 1u), none = !WL[i][0] || (i < NPART && g + i >= trk_n()),
            dim = i == 7u && song.g[G_CLOCK];
        uint16_t f = on ? T_TRK(i) : none ? ux_mix(T_PANEL, T_SURF, 50) : T_SURF, ink = on ? T_INK : dim ? T_DIM :
                     i < NPART ? T_TRK(i) : T_TEXT;
        char d[2] = {(char)('1' + g + i), 0};
        cv_rrect(x, wy, kw, h - wy - 6, 4, f, T_PANEL);
        if (!none)
            cv_text_c(x + kw / 2, ty_y(h - 26, i < NPART ? TY_TX : TY_LB), i < NPART ? TY_TX : TY_LB,
                      i < NPART ? d : i == 4u ? "All" : "Tap", ink, f);
    }
    for (i = 0; i < NPART; i++) {                       /* the black keys: the mutes (muted: red) */
        int32_t x = x0 + (int32_t)(BX[i] + 1u) * (kw + 1) - 14;
        int m = trk[g + i].p[P_MUTE] != 0;
        uint16_t f = m ? T_REC : T_LIFT;
        char n[2] = {(char)('1' + g + i), 0};
        if (g + i >= trk_n()) {                         /* (a track not in use: its key blank) */
            cv_rrect(x, by, 26, bh, 4, ux_mix(T_PANEL, T_SURF, 50), T_PANEL);
            continue;
        }
        cv_rrect(x, by, 26, bh, 4, f, T_PANEL);
        cv_icon_mid(x + 5, by + 12, 16, ICON_MUTE, m ? T_TEXT : T_SEC, f);
        cv_text_c(x + 13, ty_y(by + bh - 10, TY_LB), TY_LB, n, m ? T_TEXT : T_SEC, f);
    }
}

static void layer_cards(uint32_t l)
{
    char val[12];
    const char *unit;
    uint32_t c;
    if (l == LAYER_GLO) {                 /* T1..T4 LEVEL (the track's icon; muted: dim); T5..T8 their channel */
        for (c = 0; c < NPART; c++) {
            uint32_t t = trk_group() + c;
            if (t >= trk_n()) {                         /* (a track not in use: an empty card) */
                draw_column(c, "", "", "", T_THEME, -1, ICON_NONE);
                continue;
            }
            if (midi_track_only(&trk[t])) {             /* (no level: the MIDI Track's channel) */
                char ch[8] = "CH ";
                fmt_int(ch + 3, (int32_t)midi_track_channel(&trk[t]) + 1);
                draw_column(c, "MIDI", ch, "", glo_sounding(t) ? VAL(c) : T_DIM, -1, ICON_NONE);
                continue;
            }
            param_format(&TP[P_LEVEL], trk[t].p[P_LEVEL], val, &unit);
            draw_column(c, "LEVEL", val, unit, glo_sounding(t) ? VAL(c) : T_DIM, RATIO(&TP[P_LEVEL], trk[t].p[P_LEVEL]),
                        trk_icon(t, 1));
        }
    } else {                                            /* the page's four (SCL: LY_SCL) */
        uint8_t h = ui.home, pg = ui.page;
        ui.home = 0;
        ui.page = (uint8_t)page_first(LAYERS[l].fam);
        page_over = l == LAYER_SCL ? &LY_SCL : 0;
        draw_columns();
        page_over = 0;
        ui.home = h;
        ui.page = pg;
    }
}

/* FX held (README "FX button"): the 27 keys as the keyboard they are, each tinted in its family's colour (REPEAT,
 * TAPE, FILTER, DIRT, MOTION, SPACE, MIX); the key held white, KNOB 1's key (the one shown, none held) in its colour;
 * without the history's memory the keys that need it grey (the held one outlined). Under it the key's family, its
 * name and a picture of what it does. The header: [FX] HOLD, LOCK or MEMORY. No hint rows */
enum { PF_REPEAT, PF_TAPE, PF_FILTER, PF_DIRT, PF_MOTION, PF_SPACE, PF_MIX };
static const uint8_t PUNCH_FAM[PUNCH_NFX] = {PF_REPEAT, PF_REPEAT, PF_REPEAT, PF_REPEAT, PF_REPEAT, PF_TAPE, PF_TAPE,
    PF_TAPE, PF_FILTER, PF_FILTER, PF_FILTER, PF_DIRT, PF_DIRT, PF_MOTION, PF_SPACE, PF_MOTION, PF_FILTER, PF_FILTER,
    PF_FILTER, PF_TAPE, PF_TAPE, PF_MOTION, PF_MOTION, PF_SPACE, PF_REPEAT, PF_MIX, PF_MIX};
static const char *const PUNCH_FAM_NAME[] = {"REPEAT", "TAPE", "FILTER", "DIRT", "MOTION", "SPACE", "MIX"};
static uint16_t punch_fam_col(uint32_t f)               /* (as the effects' families: params.c FX_FAM_NAME) */
{
    const ui_pal_t *p = &UI_PALETTES[ux.pal];
    return f == PF_DIRT ? p->knob[0][2] : f == PF_SPACE ? p->knob[3][2] : f == PF_MIX ? T_MID :
           p->trk[f == PF_REPEAT ? 0 : f == PF_TAPE ? 1 : f == PF_FILTER ? 2 : 3];
}
static uint16_t punch_color(uint32_t fx) { return punch_fam_col(PUNCH_FAM[fx]); }
static int32_t pk_sin(int32_t turns) { return sine_i((uint32_t)turns << 16); }   /* turns Q16 */
static int32_t pk_pow(int32_t u, uint32_t n) { int32_t v = 65536; while (n--) v = (int32_t)((int64_t)v * u >> 16); return v; }
/* fx's picture in the box x0, y0, w, h: c its colour, gh the dim one (what it was) */
static int32_t pk_y(int32_t fx, uint32_t k, int32_t u)  /* trace k's value at u (0 .. 65536 across), Q15 */
{
    int32_t e, m;
    switch (fx) {
    case PX_REV:                                        /* a hit backwards (dim: as it was) */
        return (int32_t)((int64_t)pk_sin(7 * u) * (k ? (u < 65000 ? pk_pow(u, 3) : 0) : pk_pow(65536 - u, 3) *
               (u < 1638 ? u : 1638) / 1638) >> 16);
    case PX_STOP:                                       /* slowing to a stop */
        return (int32_t)((int64_t)pk_sin(9 * (u - (int32_t)((int64_t)u * u >> 17))) * (65536 - pk_pow(u, 3)) >> 16);
    case PX_HALF: return pk_sin((k ? 3 : 6) * u);
    case PX_OCT: return pk_sin((k ? 6 : 3) * u);
    case PX_RWD: return pk_sin(11 * (u - (int32_t)((int64_t)u * u >> 15)));   /* on, slower, then back faster */
    case PX_WOBBLE: return pk_sin(8 * u + pk_sin(3 * u) * 6 / 5);   /* (its pitch wobbling) */
    case PX_FADE: return (int32_t)((int64_t)pk_sin(9 * u) * (65536 - u) >> 16);
    case PX_FREEZE:                                     /* 186 ms caught, then held */
        e = pk_pow(65536 - (u < 10486 ? u : 10486), 3);
        if (!k)
            return (int32_t)((int64_t)pk_sin(11 * u) * pk_pow(65536 - u, 3) >> 16);
        return (int32_t)((int64_t)pk_sin(11 * u) * (24000 + (pk_sin(29 * u) >> 2)) / 32768 * e >> 16) *
               (u < 1638 ? u : 1638) / 1638;
    case PX_MONO:                                       /* left and right becoming one */
        m = clamp((u - 19661) * 10 / 3, 0, 65536);
        return (int32_t)(((int64_t)pk_sin(5 * u + (k ? 0 : 14600)) * (65536 - m) + (int64_t)pk_sin(5 * u + 7300) * m) >> 16);
    case PX_PAN:                                        /* left (k 1), right (dim): a turn a beat */
        return (k ? 1 : -1) * pk_sin(2 * u + 16384);
    default: return 0;
    }
}
static void punch_pic(int32_t fx, int32_t x0, int32_t y0, int32_t w, int32_t h, uint16_t c, uint16_t gh)
{
    static const uint8_t LOOPS[PUNCH_NFX] = {[PX_LOOP4] = 4, [PX_LOOP8] = 8, [PX_LOOP16] = 16, [PX_LOOP32] = 32,
                                              [PX_STUT] = 24, [PX_LOOP8T] = 12};
    int32_t i, k, cy = (y0 * 2 + h) * 8, a = h * 16 * 21 / 50;   /* (1/16 px: the middle, the reach) */
    if (LOOPS[fx]) {                                    /* the slice it caught, its repeats over a bar */
        int32_t n = LOOPS[fx], gap = n <= 16 ? 2 : 1, cw = (w + gap) / n - gap;
        for (k = 0; k < n; k++) {
            int32_t x = x0 + k * (w + gap) / n;
            if (!k) {
                cv_rrect(x, y0, cw, h, 2, c, T_BG);
                cv_rrect(x + 1, y0 + 1, cw - 2, h - 2, 2, ux_mix(T_BG, c, 32), c);
            } else {
                cv_rrect(x, y0, cw, h, cw > 6 ? 2 : 1, T_SURF, T_BG);
            }
            if (cw >= 11) {                             /* (its snippet: a hit) */
                int32_t py = cy;
                for (i = 0; i < cw - 4; i++) {
                    int32_t u = i * 65536 / (cw - 4), y = cy - (int32_t)((int64_t)pk_sin(3 * u) * a / 32768 *
                              pk_pow(65536 - u, 4) / 65536 * (u < 2185 ? u : 2185) / 2185) * 6 / 7;
                    cv_vspan_aa(x + 2 + i, i ? py : y, y, c);
                    py = y;
                }
            } else {
                cv_rect(x + cw / 2, y0 + h * 3 / 10, 1, h * 2 / 5, c);
            }
        }
        return;
    }
    switch (fx) {
    case PX_LPF: case PX_HPF: case PX_TEL: {            /* the response (dim: where the sweep starts), filled under */
        for (k = fx == PX_TEL; k < 2; k++) {
            int32_t at = !k ? (fx == PX_LPF ? 190 : 20) : fx == PX_LPF ? 62 : fx == PX_HPF ? 150 : 0, py = 0;
            for (i = 0; i < w; i++) {
                int32_t t, d, y;
                if (fx == PX_TEL) {                     /* 500 Hz .. 2.5 kHz */
                    int32_t lo = (78 - i) * 256 / 22, hi = (i - 128) * 256 / 22;
                    lo = lo > 0 ? lo * lo / (256 + lo) : 0;
                    hi = hi > 0 ? hi * hi / (256 + hi) : 0;
                    d = (lo + hi) * 6;
                } else {
                    t = (fx == PX_LPF ? i - at : at - i) * 256 / 22;   /* octaves (Q8) past the cut */
                    d = t > 0 ? t * t / (256 + t) * 6 : 0;   /* 24 dB an octave (dB x 256 / 4) */
                }
                y = y0 * 16 + 48 + clamp(d * (h - 6) * 16 / (30 * 64), 0, (h - 6) * 16);
                if (k) cv_rect(x0 + i, y >> 4, 1, y0 + h - (y >> 4), ux_mix(T_BG, c, 18));
                cv_vspan_aa(x0 + i, i ? py : y, y, k ? c : gh);
                py = y;
            }
        }
        return;
    }
    case PX_KLOW: case PX_KMID: case PX_KHIGH: {        /* LOW MID HIGH: the one killed dashed */
        static const char *const B[3] = {"LOW", "MID", "HIGH"};
        int32_t bw = (w - 12) / 3;
        for (k = 0; k < 3; k++) {
            int32_t x = x0 + k * (bw + 6), off = k == fx - PX_KLOW;
            if (off) {
                cv_dashed(x, y0, bw, h - 22, 4, T_DIM);
            } else {
                cv_rrect(x, y0, bw, h - 22, 4, c, T_BG);
                cv_rrect(x + 1, y0 + 1, bw - 2, h - 24, 3, ux_mix(T_BG, c, 30), c);
            }
            cv_text_c(x + bw / 2, ty_y(y0 + h - 8, TY_LB), TY_LB, B[k], off ? T_DIM : T_MID, T_BG);
        }
        return;
    }
    case PX_CRUSH: case PX_DOWN: {                      /* steps (CRUSH) or holds (ALIAS) on the sine */
        int32_t lv = fx == PX_CRUSH ? 3 : 14, hw = fx == PX_CRUSH ? w / 48 : w / 12, py = 0, pg = 0;
        for (i = 0; i < w; i++) {
            int32_t s = pk_sin(i * 131072 / w), q = pk_sin((fx == PX_CRUSH ? i / hw * hw + hw / 2 : i / hw * hw) * 131072 / w);
            int32_t y = cy - s * a / 32768, v = cy - (q * lv + (q >= 0 ? 16384 : -16384)) / 32768 * a / lv;
            cv_vspan_aa(x0 + i, i ? pg : y, y, gh);
            cv_vspan_aa(x0 + i, i ? py : v, v, c);
            pg = y;
            py = v;
        }
        return;
    }
    case PX_GATE: {                                     /* sixteenths: on half, off half */
        int32_t py = 0;
        for (i = 0; i < w; i++) {
            int32_t seg = i * 8 / w, j = i * 8 % w * 20 / w, y = cy - (pk_sin(j * 65536 * 8 / 100 + seg * 20861) * a / 32768) * 9 / 10;
            cv_vspan_aa(x0 + i, i && j ? py : y, y, j <= 10 ? c : ux_mix(T_BG, c, 22));
            py = y;
        }
        return;
    }
    case PX_ECHO: {                                     /* the hit and its echoes, dotted eighths, over a bar */
        int32_t v = h - 8;
        for (k = 0; k < 6; k++) {
            int32_t x = x0 + 4 + (w - 8) * k * 3 / 16;
            cv_rrect(x - 3, y0 + h - 8 - v, 6, v, 3, k ? ux_mix(T_BG, c, 90 - k * 11) : c, T_BG);
            v = v * 62 / 100;
        }
        for (k = 0; k < 5; k++)
            cv_rect(x0 + 4 + (w - 8) * k / 4, y0 + h - 3, 1, 3, T_DIM);
        return;
    }
    case PX_FLANGE: {                                   /* its comb sweeping (dim: the other end) */
        for (k = 0; k < 2; k++) {
            int32_t py = 0;
            for (i = 0; i < w; i++) {
                int32_t v = (32768 + pk_sin(i * 65536 / w * (k ? 6 : 2) + 16384)) / 2, y = y0 * 16 + 16 + (32768 - v) * (h - 2) / 2048;
                cv_vspan_aa(x0 + i, i ? py : y, y, k ? c : gh);
                py = y;
            }
        }
        return;
    }
    default:
        break;
    }
    if (fx == PX_FREEZE)                                /* (what it caught) */
        cv_rrect(x0, y0, w * 16 / 100, h, 3, ux_mix(T_BG, c, 20), T_BG);
    if (fx == PX_PAN) {
        cv_text_on(x0 + 2, ty_y(y0 + h / 5, TY_LB), TY_LB, "L", c, T_BG);
        cv_text_r(x0 + w - 2, ty_y(y0 + h * 4 / 5, TY_LB), TY_LB, "R", gh, T_BG);
    }
    for (k = fx == PX_STOP || fx == PX_RWD || fx == PX_WOBBLE || fx == PX_FADE; k < 2; k++) {   /* the dim one, then */
        int32_t py = 0, x1 = fx == PX_PAN ? 14 : 0;
        for (i = x1; i < w - x1; i++) {
            int32_t y = cy - pk_y(fx, (uint32_t)k, (i - x1) * 65536 / (w - 2 * x1)) * a / 32768;
            cv_vspan_aa(x0 + i, i > x1 ? py : y, y, k ? c : gh);
            py = y;
        }
    }
}
static void punch_key_draw(uint32_t key, int32_t y, uint32_t nomem, int32_t selected, int32_t held)
{
    int32_t fx = punch_fx_of_key(key), black = key_black(key), x, w = black ? 10 : 14, hk = black ? 37 : 62;
    uint16_t c = punch_fam_col(PUNCH_FAM[fx]), f, o = 0;
    int dry = nomem && punch_needs_ring(fx);
    x = black ? (int32_t)(key_place(key - 1u) + 1u) * 15 - 5 : (int32_t)key_place(key) * 15;
    if (fx == held) f = dry ? T_LIFT : T_ACCENT, o = dry ? T_TEXT : 0;
    else if (dry) f = ux_mix(T_BG, T_DIM, black ? 42 : 26);
    else if (held < 0 && fx == selected) f = c;
    else f = ux_mix(T_BG, c, black ? 58 : 30);
    if (black)
        cv_rect(x - 2, y - 2, w + 4, hk + 4, T_BG);
    if (o) {
        cv_rrect(x, y, w, hk, 3, o, T_BG);
        cv_rrect(x + 2, y + 2, w - 4, hk - 4, 2, f, o);
    } else {
        cv_rrect(x, y, w, hk, black ? 2 : 3, f, T_BG);
    }
}
/* The live layer and SONG use the same keyboard, effect name and illustration.
 * A shorter viewport reserves space for SONG's placement controls. */
static void punch_selector_draw(int32_t fx, int32_t held, uint32_t nomem, uint32_t height)
{
    uint32_t f = PUNCH_FAM[fx], k, dry = nomem && punch_needs_ring(fx) && held >= 0;
    uint16_t c = dry ? T_DIM : punch_fam_col(f);
    int compact = height < 240u - H_HEAD;
    for (uint32_t band = 0; band < height; band += CV_MAX / 240u) {
        cv_begin_band(240, height, T_BG, band);
        for (k = 0; k < 27u; k++)                       /* the white keys, then the black ones over them */
            if (!key_black(k)) punch_key_draw(k, 4, nomem, fx, held);
        for (k = 0; k < 27u; k++)
            if (key_black(k)) punch_key_draw(k, 4, nomem, fx, held);
        cv_text_on(12, ty_y(compact ? 76 : 83, TY_LB), TY_LB, PUNCH_FAM_NAME[f], c, T_BG);
        cv_text_on(12, ty_y(compact ? 98 : 110, TY_VL), TY_VL, PUNCH_NAME[fx], held >= 0 ? c : T_TEXT, T_BG);
        punch_pic(fx, 12, compact ? 116 : fx <= PX_STUT || fx == PX_LOOP8T ? 134 : 132, 216,
                  compact ? (int32_t)height - 120 : fx <= PX_STUT || fx == PX_LOOP8T ? 54 : 66,
                  held >= 0 ? c : ux_mix(T_BG, c, 55), ux_mix(T_BG, c, held >= 0 ? 38 : 25));
        cv_blit_band(0, H_HEAD);
    }
}
static void draw_punch_layer(void)
{
    int32_t held = punch.req, fx = held >= 0 ? held : (int32_t)ui.punch_view % (int32_t)PUNCH_NFX;
    uint32_t nomem = punch_memory_failed, sig = (uint32_t)fx * 131u + (uint32_t)(held + 1) * 7919u + nomem * 40503u + ux.gen;
    if (!ui.force && ui.layer_sig == sig) return;
    ui.layer_sig = sig;
    punch_selector_draw(fx, held, nomem, 240u - H_HEAD);
}
static void draw_layer(void)
{
    uint32_t l = ui.layer % LAYER_N, sig = l * 7919u + ux.gen * 977u, c;
    if(l==LAYER_FX){draw_punch_layer();return;}
    if (l == LAYER_GLO) {                               /* the four levels as strips at the top, each track's colour */
        uint8_t cs = col_style;
        if (ui.force)
            lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
        col_style = CS_CARD;
        for (c = 0; c < NPART; c++)
            card_tc[c] = T_TRK(c);
        layer_cards(l);
        memset(card_tc, 0, sizeof card_tc);
        col_style = cs;
    } else {
        layer_cards(l);
    }
    if (l == LAYER_GLO)
        sig += track_solo * 31u + (uint32_t)song.g[G_CLOCK] * 5u + trk_n() * 40503u + trk_group() * 7u +
               (uint32_t)(trk[trk_group()].p[P_MUTE] | trk[trk_group() + 1u].p[P_MUTE] << 1 |
                          trk[trk_group() + 2u].p[P_MUTE] << 2 | trk[trk_group() + 3u].p[P_MUTE] << 3) * 131u;
    else if (l == LAYER_SCL)
        sig += (uint32_t)TSEL->p[P_SCALE] * 31u;

    if (l == LAYER_GLO) {                               /* (R6: levels at the top, the keys as a keyboard under them) */
        if (ui.force || sig != ui.layer_sig) {
            ui.layer_sig = sig;
            pv_panel(Y_GRAPH, 240 - Y_GRAPH - 2, layer_glo_keys);
        }
        return;
    }
    if (ui.force || sig != ui.layer_sig) {
        ui.layer_sig = sig;
        for (uint32_t band = 0; band < (uint32_t)(H_GRAPH); band += CV_MAX / (240)) {
            cv_begin_band(240,H_GRAPH,T_BG,band);
            cv_rrect(BOX_X, 0, BOX_W, H_GRAPH, 6, T_SURF, T_BG);
            cv_bg = T_SURF;
            layer_scl();
            cv_blit_band(0, Y_GRAPH);
        }
    }
    if (ui.force) {                                     /* the footer: what the keys, knobs and buttons do */
        cv_begin(240, H_FOOT, T_BG);
        cv_key_row(4, 236, (H_FOOT - KC_PH) / 2, LAYERS[l].foot, LAYERS[l].foot[2].act ? 3u : 2u, 7u, T_BG);
        cv_blit(0, Y_FOOT);
        ui.foot_sig = 0;
    }
}
