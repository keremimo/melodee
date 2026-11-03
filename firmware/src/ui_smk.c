/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The SMK-37's banks (docs/SMK37_UX.md, sections 3-5): one list of ten, each a set of four parameters of the
 * selected track's engine, reached by K1..K4 (KNOB BANK; PAGE = the page's own four, the FM-1) or by the four faders
 * (FADER BANK), or one parameter of tracks 1..4 (LEVELS, SENDS, PANS: the faders as a mixer, bank_trk). A bank is a
 * page_t (bank_page), so the cards, edit_param, the pickers, the momentary edits and motion capture follow with no
 * path of their own: the knobs' bank is page_over while they edit
 * (ui_input.c page_knob_edit) and while the strip draws (ui_draw.c draw_columns, ui_stage.c stage_columns).
 * The strip shows the knobs' bank, or, for ST_KNOB_MS after a fader moved, the faders' bank with a pickup gauge under
 * each card (the value's bar, a tick where the fader stands; a fader "away" since a bank or track change takes over
 * only once it crosses the value: smk_faders in main.c). The header carries the bank's name as a tag (FADERS while the
 * faders' cards are up), as the HOST tag sits. A BANK button tapped: the next bank the engine has; held: the picker
 * (POP_BANK, ui_popup.c pop_draw): K1 or < > walk it, letting go keeps what is shown. Included by ui_draw.c. */
#if BOARD_SMK37
enum { SB_NONE, SB_TAP, SB_HOLD };
static void smk_fader_momentary(uint32_t id, uint32_t native);   /* main.c */
static const char *const BK_NAME[BK_N] = {"PAGE", "SOUND", "FILTER", "ENV", "LFO", "FX", "ARP", "LEVELS", "SENDS", "PANS"};
/* the track banks: control i reaches track i's id (LEVELS, SENDS, PANS: the faders as a mixer; P_COUNT: not one) */
static uint32_t bank_trk(uint32_t bank)
{
    return bank == BK_LEVELS ? P_LEVEL : bank == BK_SENDS ? P_FXSEND : bank == BK_PANS ? P_PAN : P_COUNT;
}
#define SMK_BANK_HOLD_MS 500u                           /* a BANK button held this long: its picker */
static uint8_t smk_kbank;                               /* K1..K4's bank */
static uint8_t smk_fbank = BK_LEVELS;                   /* the faders' */
static uint8_t smk_fbank_new;                           /* .. changed: every fader is away again (main.c) */
static uint8_t smk_fpick;                               /* bit i: fader i has taken over since (main.c) */
static uint32_t smk_fader_ms;                           /* the last fader move (fm1_ms | 1), 0 none */
static uint8_t smk_last_fader;                          /* the faders moved after the knobs: the strip shows theirs */
static uint8_t smk_strip_last;                          /* the bank the strip showed last (a change: a redraw) */
static uint32_t smk_bank_t0[2];                         /* KNOB BANK, FADER BANK press times (smk_hold) */
static page_t smk_bank_pg;
/* the pads' modes (docs/SMK37_UX.md section 2): what the 16 pads are. AUTO follows the page and the track (ui_input.c
 * pad_mode_auto); a mode picked by hand (a BANK button held + a bottom-row pad, Stage's sheet) stays until AUTO is
 * picked again, and the header carries its name while it differs from AUTO's choice */
enum { PM_AUTO, PM_KEYS, PM_DRUM, PM_STEPS, PM_PATTERNS, PM_N };
static const char *const PM_NAME[PM_N] = {"AUTO", "KEYS", "DRUM", "STEPS", "PATTERNS"};
static uint8_t smk_pmode;                               /* the pick */
static uint8_t smk_pad_now;                             /* the mode this frame (PM_KEYS ..; ui_input.c smk_pads_sync) */
static uint8_t smk_pad_ctx;                             /* .. or what has the pads: SPC_* */
enum { SPC_MODE, SPC_NAME, SPC_LAYER, SPC_BANK };
static uint32_t pad_mode_auto(void);                    /* ui_input.c */
static void pads_set(uint32_t m)
{
    smk_pmode = (uint8_t)(m % PM_N);
    ui_say("PADS ", PM_NAME[smk_pmode]);
    ui.force = 1;
}
/* Stage's sheet: the Pads row (ui_popup.c SHEET_STAGE) */
static void ps_pads_v(char *b) { list_words(b, PM_NAME[smk_pmode % PM_N], 12); }
static void ps_pads_turn(int32_t s) { pads_set((uint32_t)clamp((int32_t)smk_pmode + (s > 0 ? 1 : -1), 0, PM_N - 1)); }
static void ps_pads(void) { pads_set((smk_pmode + 1u) % PM_N); }

/* page t, where the engine has it (0: none; ui_list.c page_titled) */
static const page_t *bank_titled(const char *t)
{
    uint32_t i = page_titled(t);
    return i < NPAGES && page_visible(i) ? &PAGES[i] : 0;
}
/* bank's page for the selected track's engine (0: the engine has nothing for it; LEVELS: tracks 1..4, its callers'
 * own: level_knob, the GLO cards) */
static const page_t *bank_page(uint32_t bank)
{
    static const uint8_t P5_FLT[4] = {P5_CUTOFF, P5_RESONANCE, P5_ENV_FILTER, P5_DECAY_FILTER};
    static const uint8_t P5_AMP[4] = {P5_ATTACK_AMP, P5_DECAY_AMP, P5_SUSTAIN_AMP, P5_RELEASE_AMP};
    static const uint8_t FXK[4] = {P_FXMIX, P_FX2MIX, P_FX3MIX, P_FXSEND};
    uint32_t e = eng_idx(TSEL->eng_req), k, scope = SC_TRACK;
    int p5 = e == ENGI_PROPHET, cz = e == ENGI_CZ, fm = e == ENGI_FM6;
    const uint8_t *ids = 0;
    const char *title = 0;
    if (midi_track_only(TSEL))
        return 0;
    switch (bank) {
    case BK_SOUND: return drum_track(TSEL) ? bank_titled("DRUM SOUND") : stage_page();
    case BK_FILTER:
        if (p5) { ids = P5_FLT; scope = SC_P5; }
        else if (cz) title = "CZ1 WAVE";
        else return 0;
        break;
    case BK_ENV:
        if (p5) { ids = P5_AMP; scope = SC_P5; }
        else title = cz ? "C1 AMP R1-4" : fm ? "EG RATE" : "ENV";
        break;
    case BK_LFO: title = p5 ? "P5 LFO" : cz ? "CZ VIBRATO" : fm ? "FM LFO" : "LFO"; break;
    case BK_FX: ids = FXK; break;
    case BK_ARP: title = "ARP"; break;
    default: return 0;
    }
    if (title)
        return bank_titled(title);
    smk_bank_pg.title = BK_NAME[bank];
    smk_bank_pg.fam = FAM_HOME;
    smk_bank_pg.scope = (uint8_t)scope;
    smk_bank_pg.graph = GR_NONE;
    for (k = 0; k < 4u; k++)
        smk_bank_pg.id[k] = ids[k];
    return &smk_bank_pg;
}
static int bank_has(uint32_t bank) { return bank == BK_PAGE || bank_trk(bank) < P_COUNT || bank_page(bank) != 0; }
/* the next bank the engine has, after b (wrapping) */
static uint32_t bank_next(uint32_t b, int32_t dir)
{
    uint32_t i;
    for (i = 0; i < BK_N; i++) {
        b = (uint32_t)((int32_t)b + dir + BK_N) % BK_N;
        if (bank_has(b))
            break;
    }
    return b;
}
/* the banks apply where the page has a strip of cards: not on a screen of its own, a list, a slot page, MOTION, the
 * menu, a dialog, NAME, a layer (their knobs are their own) */
static int bank_knobs_on(void)
{
    return !ui.menu && !ui.confirm && !name_on() && !new_on() && !ui.layer && !ui.ly && !designer_on() &&
           !sec_map_on() && !own_screen() && !list_on() && !slot_kind() && !motion_page() &&
           pop.on != POP_SHEET && pop.on != POP_LIST && !song_punch_on();
}
static int smk_faders_out(void)
{
    return smk_fader_ms && smk_last_fader && fm1_ms - (smk_fader_ms & ~1u) < ST_KNOB_MS;
}
/* the knobs' bank where the page is the track's sound (Stage, ENV, LFO, SCL, EDIT, ARP, FX): a bank is four of the
 * engine's parameters. The sequencer's, the global settings', MIXER and SAVE keep their own four for the knobs, as
 * the FM-1's, whatever bank is picked (BK_PAGE there, no tag; the pick stays for the sound pages). The faders are
 * independent of the page, so theirs is not gated here (main.c smk_faders) */
static uint32_t knob_bank(void)
{
    uint32_t f = ui.home ? FAM_HOME : PAGES[ui.page].fam;
    return f == FAM_GLO || f == FAM_SEQ || f == FAM_TRK || f == FAM_SAVE ? BK_PAGE : smk_kbank;
}
/* the bank whose cards the strip shows (BK_PAGE: the page's own): the faders' while theirs are up, else the knobs' */
static uint32_t strip_bank(void)
{
    if (!bank_knobs_on())
        return BK_PAGE;
    return smk_faders_out() ? smk_fbank : knob_bank();
}
/* the knobs' bank page while they edit (0: the page's own) */
static const page_t *knob_bank_page(void)
{
    uint32_t b = knob_bank();
    return b != BK_PAGE && bank_trk(b) == P_COUNT && bank_knobs_on() ? bank_page(b) : 0;
}
/* the header's tag: FADERS while the faders' cards are up, the knobs' bank (nothing for PAGE), else the pads' mode
 * while picked by hand and not what AUTO would show */
static const char *smk_head_tag(void)
{
    if (bank_knobs_on()) {
        if (smk_faders_out())
            return "FADERS";
        if (knob_bank() != BK_PAGE)
            return BK_NAME[knob_bank()];
    }
    if (smk_pmode != PM_AUTO && smk_pad_ctx == SPC_MODE && smk_pad_now != pad_mode_auto() && !ui.menu)
        return PM_NAME[smk_pad_now];
    return "";
}
/* each frame before the page: the strip changing what it shows redraws it whole */
static void smk_strip_poll(void)
{
    uint32_t b = strip_bank() | (uint32_t)(smk_faders_out() && bank_knobs_on() ? 16u : 0u);
    if (b != smk_strip_last) {
        smk_strip_last = (uint8_t)b;
        memset(ui.col, 0, sizeof ui.col);
        ui.force = 1;
    }
}

/* ---------------------------------------------------------------- the faders' values --- */
/* a page whose four knobs set values (edit_param's own path: an absolute value lands): not the pages whose knobs
 * move a cursor, a row, a slot or an action (NOTES, the drum grid and hits, MIXER, DRUM SOUND, the racks, MOD, the
 * stores, the tools, SCL's favourite) */
static int fader_page_ok(const page_t *pg)
{
    uint32_t sc = pg->scope, g = pg->graph;
    return sc != SC_STEP && sc != SC_DRUMHIT && sc != SC_TRK && sc != SC_DRUM && sc != SC_P5STORE &&
           g != GR_FXRACK && g != GR_MOD && g != GR_FMSTORE && g != GR_CZTOOLS && g != GR_SCALE;
}
/* fader i's parameter as 0..127 (-1: nothing there) */
static int32_t smk_fader_value(uint32_t i)
{
    int16_t *vp;
    const param_desc_t *d;
    const page_t *pg;
    int32_t v;
    uint32_t tid = bank_trk(smk_fbank);
    if (tid < P_COUNT) {
        uint32_t t = trk_group() + i;
        if (t >= trk_n() || midi_track_only(&trk[t]))
            return -1;
        d = &TP[tid];
        v = trk[t].p[tid];
    } else {
        pg = smk_fbank == BK_PAGE ? (ui.home ? stage_page() : cur_page()) : bank_page(smk_fbank);
        if (!pg || !fader_page_ok(pg))
            return -1;
        d = page_desc(pg, i, &vp);
        if (!d || !vp || d->max == d->min)
            return -1;
        v = *vp;
    }
    return ((int32_t)v - d->min) * 127 / (d->max - d->min);
}
/* fader i at v (0..127): its parameter set, as a MIDI knob's absolute value is (edit_param), through the momentary
 * store with NOTE REPEAT held and motion capture */
static void smk_fader_set(uint32_t i, int32_t v)
{
    const page_t *pg;
    uint32_t tid = bank_trk(smk_fbank);
    if (tid < P_COUNT) {                                /* LEVELS, SENDS, PANS: track i's */
        uint32_t t = trk_group() + i;
        int16_t *vp = &trk[t].p[tid];
        if (t >= trk_n() || midi_track_only(&trk[t]))
            return;
        if (t == song.sel)
            smk_fader_momentary(tid, 0);
        *vp = (int16_t)(TP[tid].min + ((int32_t)TP[tid].max - TP[tid].min) * v / 127);
        if (!(t == song.sel && momentary.active))
            (void)motion_capture(&trk[t], tid, *vp);
        return;
    }
    pg = smk_fbank == BK_PAGE ? (ui.home ? stage_page() : cur_page()) : bank_page(smk_fbank);
    if (!pg || !fader_page_ok(pg))
        return;
    page_over = pg;
    momentary_take(i);
    midi_edit_value = v;
    edit_param(i, 0);
    midi_edit_value = -1;
    page_over = 0;
}
/* fader i read at v this pass (prev: its last reading, moved: it changed): its parameter set once it has taken over.
 * A fader is away after a bank, track or sound change (main.c smk_faders) and whenever its parameter changed under
 * it (a load, a knob, the page changing under PAGE, motion): it takes over crossing the value, or next to it, never
 * jumping to where it stands. The value under each fader is watched here (smk_fader_seen) */
static int32_t smk_fader_seen[4] = {-1, -1, -1, -1};
static void smk_fader_poll(uint32_t i, int32_t v, int32_t prev, int moved)
{
    int32_t cur = bank_trk(smk_fbank) == P_COUNT && !bank_knobs_on() ? -1 : smk_fader_value(i);
    if (cur != smk_fader_seen[i]) {                     /* changed under the fader: away until it crosses it */
        smk_fader_seen[i] = cur;
        smk_fpick &= (uint8_t)~(1u << i);
    }
    if (!moved || cur < 0)
        return;
    if (!((smk_fpick >> i) & 1u)) {
        if ((prev - cur) * (v - cur) > 0 && v - cur != 1 && cur - v != 1)
            return;
        smk_fpick |= (uint8_t)(1u << i);
    }
    smk_fader_set(i, v);
    smk_fader_seen[i] = smk_fader_value(i);
}
/* under each card of the faders' bank: the value's bar in the knob's colour (MID while the fader is away), a tick
 * where the fader stands */
static void smk_fader_gauges(void)
{
    const int32_t w = 42, x0 = COL_W / 2 - w / 2;
    uint32_t c;
    for (c = 0; c < 4u; c++) {
        int32_t v = smk_fader_value(c), f = fm1_in.fader[c];
        int picked = (smk_fpick >> c) & 1u;
        uint16_t lc = picked ? T_KNOB(c) : T_MID;
        if (v < 0)
            continue;
        cv_begin(COL_W, 6, T_BG);
        cv_rrect(x0, 2, w, 2, 1, T_LINE, T_BG);
        cv_rrect(x0, 2, w * v / 127, 2, 1, lc, T_BG);
        cv_rect(x0 + w * f / 127 - 1, 0, 2, 6, picked ? lc : T_TEXT);
        cv_blit((uint32_t)CARD_X(c), (uint32_t)(card_y + COL_H - 6));
    }
}

/* ------------------------------------------------------------------- the picker --- */
/* while a BANK button is held: the eight banks as pick_draw's card over the two left columns, the one chosen marked
 * (pop.col: 0 the knobs', 1 the faders'; pop.sel: the bank shown) */
static void bank_pick_open(uint32_t which)
{
    pop_close();
    pop.on = POP_BANK;
    pop.col = (uint8_t)which;
    pop.sel = which ? smk_fbank : smk_kbank;
    ui.force = 1;
}
static void bank_pick_step(int32_t dir)
{
    pop.sel = (uint8_t)bank_next(pop.sel, dir);
    pop.sig = 0;
}
static void bank_apply(uint32_t which, uint32_t b)
{
    if (which) {
        smk_fbank = (uint8_t)b;
        smk_fbank_new = 1;
        ui_say("FADER BANK ", BK_NAME[b]);
    } else {
        smk_kbank = (uint8_t)b;
        ui_say("KNOB BANK ", BK_NAME[b]);
    }
    ui.force = 1;
}
static void bank_pick_draw(void)
{
    int32_t h = 12 + 5 * 28, now = (int32_t)pop.sel, first = clamp(now - 2, 0, BK_N - 5), i, pw = POP_PICK_W;
    uint32_t sig = pop.sel * 7919u + pop.col * 131u + ux.gen * 977u + ux.pal * 31u + 5u;
    if (!ui.force && sig == pop.sig)
        return;
    pop.sig = sig;
    for (uint32_t band = 0; band < (uint32_t)h; band += CV_MAX / (uint32_t)pw) {
        cv_begin_band((uint32_t)pw, (uint32_t)h, T_BG, band);
        cv_rrect(0, 0, pw, h, 10, T_LINE, T_BG);
        cv_rrect(1, 1, pw - 2, h - 2, 9, T_SURF, T_LINE);
        for (i = 0; i < 5; i++) {
            int32_t b = first + i, y = 6 + i * 28, on = b == now, far = b - now > 1 || now - b > 1;
            uint16_t bg = on ? ux_mix(T_SURF, T_THEME, 22) : T_SURF;
            char w[12];
            list_words(w, BK_NAME[b], sizeof w);
            if (on)
                cv_rrect(4, y, pw - 8, 26, 6, bg, T_SURF);
            cv_free_text(10, ty_y(y + 13, TY_TX), TY_TX, w, !bank_has((uint32_t)b) ? T_DIM : on ? T_THEME : far ? T_MID : T_SEC,
                         bg, pw - 18);
        }
        cv_blit_band((uint32_t)CARD_X(0), Y_LABEL + CARD_H + 2);
    }
}

/* ------------------------------------------------------------------- the buttons --- */
/* a button by its id (the SMK's own have no label): BT_TAP on release before SMK_BANK_HOLD_MS, BT_HOLD once held */
static uint32_t smk_hold(uint32_t *t0, uint32_t id, uint32_t now)
{
    uint32_t tap;
    if ((fm1_in.buttons >> id) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (!(*t0 & 2u) && now - (*t0 & ~3u) > SMK_BANK_HOLD_MS * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return SB_HOLD;
        }
        return SB_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? SB_TAP : SB_NONE;
}
/* KNOB BANK and FADER BANK (unless HARDWARE CALIBRATION gave them an FM-1 label), each pass: tap the next bank, held
 * the picker, < > while held the previous / next; let go with the picker up: keep what it shows. 1 = < > taken */
static int smk_bank_buttons(uint32_t pressed, uint32_t now)
{
    static const uint8_t ID[2] = {SMK_BTN_KNOB_BANK, SMK_BTN_FADER_BANK};
    uint32_t w, arrows = 0;
    for (w = 0; w < 2u; w++) {
        uint32_t id = ID[w], g;
        if (panel_btn_of(id) != NB)
            continue;
        g = smk_hold(&smk_bank_t0[w], id, now);
        if (g == SB_TAP) {
            if (pop.on == POP_BANK && pop.col == w) {   /* (let go of a held one: the pick) */
                bank_apply(w, pop.sel);
                pop_close();
            } else {
                bank_apply(w, bank_next(w ? smk_fbank : smk_kbank, 1));
            }
        } else if (g == SB_HOLD) {
            bank_pick_open(w);
        } else if (!((fm1_in.buttons >> id) & 1u) && pop.on == POP_BANK && pop.col == w) {
            bank_apply(w, pop.sel);                     /* (held past the tap: the pick) */
            pop_close();
        }
        if ((fm1_in.buttons >> id) & 1u) {              /* held: the arrows walk its banks */
            if ((pressed >> SMK_BTN_LEFT) & 1u) { if (pop.on != POP_BANK) bank_pick_open(w); bank_pick_step(-1); }
            if ((pressed >> SMK_BTN_RIGHT) & 1u) { if (pop.on != POP_BANK) bank_pick_open(w); bank_pick_step(1); }
            arrows = 1;
        }
    }
    return (int)arrows;
}
#endif
