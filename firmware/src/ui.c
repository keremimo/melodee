/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MELODEE user interface.
 * Flat: SURF cards and panels on the palette's background, no rules, one type family (Rubik or Melodee Pixel, four roles), tracks
 * named by circled numerals. Four columns <-> KNOB 1..4. Rendering is lazy:
 * every element remembers what it last drew and is redrawn only on change. */
static int live_rec_sel(void);
static int project_save(uint32_t slot);
static void panel_setup(void);
static void project_load(uint32_t slot);
static int project_used(uint32_t slot);
static int project_old(uint32_t slot);
static int template_old(void);
static void settings_old_accept(void);
static int template_used(void);                 /* SLOT TMPL (PROJ_TMPL): project.c */
static void template_save(void);
static void template_load(void);
static uint32_t chain_prepare(void);
static uint32_t chain_prepare_from(uint32_t start);
static int up_used(uint32_t k);              /* user presets: upreset.c */
static int up_load(uint32_t k);
static int up_favorite(uint32_t k);
static int up_favorite_set(uint32_t k,int on);
static void up_favorite_poll(void);
static int shared_preferences_save(void);
static int shared_wipe(void);                     /* confirmed recovery from preset migration failure */
static uint32_t up_count(void);
static uint32_t up_nth(uint32_t n);
static uint32_t up_rank(uint32_t slot);
static uint32_t up_engine(uint32_t k);
static void up_name(uint32_t k, char *b);
static void up_slot_label(char *b, uint32_t k);
static void up_ui(uint32_t op, uint32_t k);
static void up_auto_name(char *b, uint32_t e, uint32_t k);   /* naming (ui_name.c) */
static void up_ui_named(uint32_t op, uint32_t k, const char *name);
static uint32_t native_limit(uint32_t e);
static int native_used(uint32_t e, uint32_t k);
static int native_old(uint32_t e, uint32_t k);
static int up_old(uint32_t k);
static int up_old_bank(uint32_t k);
static void native_name(uint32_t e, uint32_t k, char *out);
static int native_load(uint32_t e, uint32_t k, uint32_t tr);
static const uint8_t *native_raw(uint32_t e, uint32_t k);
static int native_store(uint32_t e,uint32_t k,uint32_t tr,const char *name);
static int native_put(uint32_t e, uint32_t k, const uint8_t *raw);
static uint32_t native_size(uint32_t e);
static int shared_import(uint32_t e,uint32_t first,uint32_t count,const uint8_t *raw,uint32_t stride,uint32_t *saved);
static int general_sysex_send(uint32_t k);
static int general_sysex_save(uint32_t k,const uint8_t *raw);
static void confirm_open(uint32_t kind,uint32_t track);
static uint32_t library_engine(void);
static void preset_old_allow(uint32_t allow);
static void slot_save_sheet(void);              /* ui_slots.c */
static int project_save_as(uint32_t slot, const char *name);
static int project_name(uint32_t slot, char *b);
/* pattern p's number, 1..16, into b (3 bytes); its length */
static uint32_t pat_num(char *b, uint32_t p)
{
    p = p % NPAT + 1u;
    if (p > 9u) { b[0] = (char)('0' + p / 10u); b[1] = (char)('0' + p % 10u); b[2] = 0; return 2; }
    b[0] = (char)('0' + p); b[1] = 0; return 1;
}
static int project_free_pct(void);                 /* project.c: the shared pool's room left in KB (-1 unknown) */
static int project_rename(uint32_t slot, const char *name);
static int project_erase(uint32_t slot);
static void project_cur_name(char *b);
static uint32_t user_of(const track_t *t)    /* user preset slot its sound came from, USER_NONE = none */
{
    return t->user && !t->user_native && up_used(t->user-1u) && up_engine(t->user-1u)==t->eng_req ? t->user-1u : USER_NONE;
}
static uint32_t up_gen;                      /* bumped on every user bank change (redraws) */
static uint8_t browse_loads, browse_mark;    /* sound loads so far; at the browser's opening (ui_browser.c: back) */
static struct { uint8_t active, row, track, return_page, pattern; uint16_t epoch; } song_picker;
static void song_preset_cancel(void);
static void song_preset_finish(int keep);
static void sound_name(const track_t *t, char *b);
#include "favorites.c"
/* MENU settings in favorites.factory[15][30]: bit 0 retired, bit 1 KNOB ACCEL OFF, bits 2..4 retired (OVERCLOCK until
 * 1.3.4 / 1.4: left as saved, never read), bit 5 PICKER OFF, bit 6 FONT PIXEL, bit 7 retired
 * (STAGE EFFECTS OFF in 1.4.0-rc.4: left as saved, never read). */
#define PREF_BITS (favorites.factory[15][30])
#define PREF_ACCEL_OFF 2u
#define PREF_PICK_OFF 32u
#define PREF_PIXEL 64u                         /* Settings > FONT: PIXEL (else RUBIK) */
#define TY_PIXEL ty_pixel()
static uint32_t ty_pixel(void) { return (PREF_BITS & PREF_PIXEL) != 0u || !RES_OK(); }   /* (gfx.c TY_*; Rubik: res.c) */
/* HOME SELECT (the menu): what SELECT turns on HOME, kept in favorites.factory[15][25] (0, any other: BPM) */
enum { HS_BPM, HS_MOD, HS_LEVEL, HS_SWING, HS_TRANS, HS_CHORD, HS_ARATE, HS_PATTERN, HS_OFF, HS_OCT, HS_N };
static const char *const HS_NAME[HS_N] = {"BPM", "MOD WHEEL", "LEVEL", "SWING", "TRANSPOSE", "CHORD", "ARP RATE", "PATTERN",
                                          "OFF", "OCTAVE"};
/* the menu's order: OCTAVE beside TRANSPOSE, OFF last (the saved numbers stay: OCTAVE came after OFF) */
static const uint8_t HS_ORDER[HS_N] = {HS_BPM, HS_MOD, HS_LEVEL, HS_SWING, HS_TRANS, HS_OCT, HS_CHORD, HS_ARATE,
                                       HS_PATTERN, HS_OFF};
static uint32_t hs_step(uint32_t v, int32_t s)  /* KNOB 1 steps, 0 (OCT+): steps and wraps */
{
    uint32_t i = 0;
    while (i + 1u < HS_N && HS_ORDER[i] != v) i++;
    i = s > 0 ? (i + 1u < HS_N ? i + 1u : i) : s < 0 ? (i ? i - 1u : 0u) : (i + 1u) % HS_N;
    return HS_ORDER[i];
}
#define HOME_SELECT (favorites.factory[15][25])
static uint32_t home_select(void) { return HOME_SELECT < HS_N ? HOME_SELECT : HS_BPM; }
/* GLO SELECT (the menu): what SELECT turns with GLO held, on any page (the same choices), kept in bits 1..4 of
 * favorites.factory[15][24] (bit 0: a CZ-1 star; 0, any other: BPM) */
#define GLO_SELECT (favorites.factory[15][24])
static uint32_t glo_select(void) { return ((GLO_SELECT >> 1) & 15u) < HS_N ? (GLO_SELECT >> 1) & 15u : HS_BPM; }
static void glo_select_set(uint32_t v) { GLO_SELECT = (uint8_t)((GLO_SELECT & ~0x1Eu) | (v & 15u) << 1); }
/* UNUSED TRACKS (the menu): HIDE in bit 5 of the same byte. SHOW (0): Stage and MIXER keep a greyed row for each
 * place not in use, saying "Add track"; HIDE: the tracks in use share the rows (one track: the wide card) */
#define TRACKS_HIDE 32u
#define TRS_OUT_OFF 64u                        /* (SMK-37) the menu's TRS MIDI OUT OFF (smk_midi_out.h) */
#define MIDI_THRU_ON 128u                      /* (fm1-base) the menu's MIDI THRU ON (platform/melodee_app.c midi_thru) */
/* the four tracks Stage, MIXER, PATTERNS and SONG show: the selected one's (1..4, or the MIDI-only
 * 5..8); a row k of them is track trk_group() + k */
static uint32_t trk_group(void) { return song.sel / NPART * NPART; }
static uint32_t stage_rows(void)
{
    uint32_t g = trk_group(), n = trk_n() > g ? trk_n() - g : 0u;
    return GLO_SELECT & TRACKS_HIDE ? (n < NPART ? n : NPART) : NPART;
}

static uint8_t sync_reload;                  /* engine / preset / project / user preset loaded: editor RELOAD push */

#define ACC T_THEME                /* values, curves */
#define VAL(c) ((void)(c), T_TEXT)   /* a value (the knob just turned: its card tinted and outlined, ui_draw.c) */
#define RATIO(d, v) ((d)->max > (d)->min ? ((int32_t)(v) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
/* layout (docs/design/README.md "Large type"): the header 0..29; a page's four knobs as a strip of cells 32..79 (57 px
 * at x 2 + 60 c: a knob's label over its value), the panel 84..207 (the page's picture, the graphs), the sound bar
 * 212..239; BG between them. Stage, lists, slots: full-width rows of their own */
#define Y_HEAD 0
#define H_HEAD 30
#define Y_LABEL 32                    /* the strip (the knobs' cells) */
#define Y_SEP_END 80
#define CARD_W 57
#define CARD_H 48
#define CARD_X(c) (2 + 60 * (int32_t)(c))
#define Y_GRAPH 84                    /* the panel */
#define H_GRAPH 124
#define Y_FOOT 212                    /* the sound bar (pages of the old template: their one row) */
#define H_FOOT 28
#define BOX_X 2                       /* panels and rows: x 2 .. 237 */
#define BOX_W 236

#if BOARD_SMK37
typedef uint32_t ui_btnmask_t;   /* bits of panel button ids (SMK-37: 18 buttons) */
#else
typedef uint16_t ui_btnmask_t;   /* bits of panel button ids (FM-1: 14 buttons) */
#endif
static struct {
    uint8_t designer_mode;       /* the sound designer: START, the stages (ui_designer.c) */
    uint8_t home;
    uint8_t home_pg;             /* HOME's page: 0 Stage, 1 the chord view (ui_view.c); HOME tapped on HOME turns it */
    uint8_t pad;                 /* the pad DRUM SOUND / MIX and DRUM HIT show (a kit's: pads.c) */
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t bank;                /* SEQ: 16-step bank (follows the cursor) */
    uint8_t pat_key, pat_copy, pat_track;          /* SEQ + white-key bank gesture */
    uint8_t pat_tap, pat_dlg, pat_did;             /* .. the key last tapped (+1), the one held to its dialog, its
                                                    * length changed there (Cancel: Done) */
    uint32_t pat_ms, pat_tap_ms;                   /* .. when it went down, when the tapped one was let go */
    uint8_t ptc_on, ptc_trk, ptc_src, ptc_dst, ptc_to;   /* PATTERNS' Copy to: track ptc_trk's pattern src to
                                                          * track ptc_to's dst (ui_patterns.c) */
    uint8_t preset_audition;     /* Patterns' temporary preset picker; navigation cancels it */
    uint8_t pt_page;             /* PATTERNS' page of eight (a part's 16: two; 0xFF: the selected track's, ui_patterns.c) */
    uint8_t cursor;              /* SEQ: step being edited (STEP page KNOB 1 moves it) */
    uint16_t note_pick;          /* NOTES: event index + 1; zero chooses the first hit */
    uint8_t erase_gesture, erase_owner; /* an EDIT press captures one track/bank until released */
    uint32_t erase_generation, erase_transport;
    uint8_t note_slot;           /* individual pitch in a manually entered chord */
    uint8_t note_zoom;           /* 16, 8, 4, 2 or 1 steps across the panel */
    uint8_t note_track;
    uint8_t scale_picker_seen;
    uint8_t scale_family;        /* 0 ALL, 1..SCALE_FAMILIES, last FAV */
    uint32_t note_pattern_gen, note_generation;
    recorded_note_t note_identity; /* retain focus when unrelated event storage changes */
    uint8_t entry_open;          /* SEQ: keys held since the first press of this entry */
    uint8_t step_move;           /* a held SELECT modifier gesture: one edit until the modifier is released */
    uint8_t step_oct_used;       /* OCT buttons consumed by EDIT undo or redo */
    ui_btnmask_t step_mods, step_used; /* STEP's ENV/SCL buttons awaiting release, and the gestures consumed */
    uint8_t lane;                /* SEQ > STEP on a DRUM track (the grid): the lane the keys and KNOB 3 / 4 edit */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t import_refresh, new_active;
    uint8_t name_active;
    uint8_t bulk_hold;
    uint8_t note_copy, note_move, note_grid;
    uint8_t menu;                /* 0 off, 1 list, 2 about + credits (HOME held) */
    uint8_t menu_sel;
    uint16_t menu_scroll;        /* continuous ABOUT + CREDITS position, pixels */
    uint32_t seen_pattern_gen;
    uint32_t menu_sig, home_t0;  /* HOME press time (btn_hold) */
    uint8_t force;               /* full redraw pending */
    uint8_t waiting_shown;       /* selected track's persistent header status */
    uint8_t msg_t;               /* transient message frames */
    uint8_t bpm_t;               /* frames the BPM stays highlighted after a SELECT turn */
    uint8_t act;                 /* action pages: the column whose action OCT+ does, + 1; 0 = none (act_col) */
    uint32_t rec_t0;             /* REC press time (transport only) */
    uint32_t seq_t0;             /* SEQ held: direct SONG entry */
    uint32_t save_t0;            /* SAVE press time (btn_hold: held = project quick save) */
    uint32_t edit_t0;            /* EDIT press time (btn_hold: held = UNDO) */
    uint32_t oct_t0;             /* OCT+ press time (btn_hold: held = the page's sheet, ui_popup.c) */
    uint8_t confirm;             /* the OCT- / OCT+ dialog: CF_*, 0 = none */
    uint16_t confirm_trk;         /* the track it clears, the slot it overwrites */
    uint16_t uslot;               /* SAVE > USER: the selected user preset slot */
    uint8_t song_row;            /* SONG: row selected, count selects the next empty row */
    uint8_t uboot;               /* main.c: seconds left before UPDATE MODE (OCT- + OCT+ held), 0 = none */
    uint8_t proj_new;            /* SAVE > PROJECT: KNOB 1 past TMPL, NEW (a new song: ui_new.c); SLOT unchanged */
    uint32_t ly_t0;              /* the layer button's press time | 1, LY_* bits (ui_layer.c layer_gesture) */
    uint8_t ly;                  /* the layer whose button is down (LAYER_*), 0 = none */
    uint8_t lock; /* double-tapped quick layer */
    uint8_t punch_view;
    uint8_t layer;               /* the layer whose map is shown (LAYER_*), 0 = none */
    ui_btnmask_t pg_down;        /* page buttons down (panel ids) that act when let go */
    uint32_t layer_sig;          /* drawn-state cache of the map */
    char msg[32];                /* (a sound's name fits: the toast wraps it, ui_draw.c draw_toast) */
    char msg2[32];               /* a second message, shown when the first is over */
    uint32_t enc_t[NE];
    /* drawn-state cache */
    char col[4][32];
    uint32_t graph_sig, head_sig, foot_sig, frame;
    /* rolling digits (ui_draw.c roll_*): the four card values and the header BPM */
    struct {
        char from[7];            /* the value rolling out; "" = idle */
        uint8_t t0;              /* (ui.frame) of the value's last change */
        int8_t dir;              /* +1: it went up (the old digit leaves upward), -1: down */
        uint8_t sig;             /* a card: what its value is of (label, unit, track, engine, palette) */
    } roll[5];
    int16_t roll_bpm;            /* the BPM last drawn */
} ui;

static int designer_on(void) { return ui.designer_mode != 0; }
static void designer_open(void);
static void designer_draw(void);
static int designer_screen(void);
static int designer_input(uint32_t pressed, uint32_t notes);
static void designer_close(int discard);
static int designer_head_chip(char *b);
static void designer_pages(uint32_t *n, uint32_t *k);
static int designer_tabs_on(void);
static void designer_tabs(void);
static int designer_foot(void);
static void (*designer_quit)(int discard);   /* designer_close while it is open (project.c: a project loading) */

#include "screen.c"

enum { CF_NONE, CF_CLEAR_SEQ, CF_CLEAR_TRK, CF_OVR_PROJ, CF_OVR_USER,
       CF_DEL_ROW, CF_CLEAR_SONG, CF_INIT_SOUND, CF_CLEAR_MOTION, CF_ERASE_USER, CF_TAKE_JAM, CF_NEW_SONG, CF_ERASE_PROJ,
       CF_DEL_PAT, CF_PASTE_PAT, CF_DISCARD_DESIGN, CF_IMPORT, CF_BULK_ERASE, CF_OLD_SETTINGS, CF_OLD_TEMPLATE,
       CF_DEL_TRACK, CF_PAT_KEY, CF_CLEAR_SHARED, CF_BLE_FORGET, CF_MLEARN_FORGET, CF_DEL_KIT,
       CF_DEL_ITEM };   /* ui.confirm: REC held on SEQ / ARP, on TRACKS; SAVE over a used slot; a
                         * pattern over the user's steps; USER ERASE; PATTERNS: Delete pattern, Copy to
                         * over a pattern in use; SEQ + a white key held: its pattern (ui_input.c); CF_DEL_ITEM:
                         * GLO > SYNTHS / STORAGE, a synth, a kit, a bank of sounds (storage_items.c) */
static void sti_question(uint32_t it, char *a, char *b);   /* storage_items.c */
static void sti_delete(uint32_t it);

static const page_t *page_over;   /* a quick layer's own four knobs (ui_layer.c), while it edits or draws them */
static uint32_t pads_hit_selected(void);
static const page_t *cur_page(void) { return page_over ? page_over : &PG(ui.page); }

/* the quick layers (ui_layer.c): a button held, the keys and KNOB 1..4 are its shortcuts, its map over the page */
enum { LAYER_NONE, LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_N };
static void draw_layer(void);
static const char *layer_head(void);
static uint32_t layer_leds(void);
static uint32_t layer_btn(void);

/* FM operator pages belonged to DIGITAL (retired): they never appear. SLICES:
 * a SLICE track's (ui_slice.c) */
static int list_gone(uint32_t i);                        /* ui_list.c: a page in another's list */
/* ENV / LFO on the engines with envelopes and LFOs of their own (Prophet, FM6, CZ-1: engine_t.ownenv, the track's
 * ADSR and ENV DEST do nothing there): their own pages (FAM_EDIT, by title). ENV: only those; LFO: those first, then
 * the track LFO's pages and MOD (the track LFO modulates any engine through LFO DEST and is the matrix's source) */
static int native_page(uint32_t i, uint32_t fam)       /* page i: one of fam's native pages (its schema's flags) */
{
    const page_t *pg = page_at(i);
    uint32_t f = fam == FAM_ENV ? MOD3_PG_ENV : fam == FAM_LFO ? MOD3_PG_LFO : 0u;
    return f && pg->scope == SC_MOD && (sch_pg_flags(pg) & f) != 0u;
}
static int native_has(uint32_t fam)                     /* the selected synth has fam's pages of its own */
{
    for (uint32_t i = 0; i < NPAGES; i++)
        if (PAGE_TAB[i].scope == SC_MOD && native_page(i, fam))
            return 1;
    return 0;
}

static int page_visible(uint32_t i)
{
    if (midi_track_only(TSEL)) {
        if (trk_midi_slot(song.sel) && PG(i).graph == GR_BROWSE) return 0;   /* (tracks 5..8: no other sound) */
        if (PG(i).fam == FAM_ENV || PG(i).fam == FAM_LFO) return 0;
        if (PG(i).scope == SC_TRACK && PG(i).fam == FAM_EDIT) return 0;
        if (PG(i).fam == FAM_FX && PG(i).scope == SC_TRACK && PG(i).graph != GR_FXRACK) return 0;
        if (PG(i).scope == SC_ENGINE && PG(i).id[0] == P_E4) return 0;
    }
    if (PG(i).fam == FAM_FX && PG(i).graph != GR_FXRACK) {   /* a slot's page (its MIX first): while it holds
                                                                     * an effect (the rack shows it either way); NOTE
                                                                     * TAIL none: its row is all of it, KNOB 4 REL */
        int16_t t = (PG(i).scope == SC_GLOBAL ? song.g : TSEL->p)[PG(i).id[0] - 1u];
        return t != FX_OFF && t != FX_MIDIVERB;
    }
    if (list_gone(i))
        return 0;
    if (PG(i).graph == GR_MOTION)                   /* MOTION: once there is motion (Kerem: an empty page; the one
                                                         * shown stays until it is left) */
        return motion_has_lanes(TSEL) || (!ui.home && ui.page == i);
    if (PG(i).fam == FAM_ENV && native_has(FAM_ENV))      /* (the track's ADSR, ENV DEST: nothing there) */
        return 0;
    if(PG(i).scope==SC_PADS || PG(i).scope==SC_PADHIT)return pads_on(TSEL) && (PG(i).id[1]!=4 || pads_npar(TSEL)>3u);
    if (PG(i).scope == SC_TRACK && PG(i).id[0] >= P_LN0 && PG(i).id[0] <= P_LN7)
        return pads_on(TSEL);
    if(PG(i).scope==SC_ENGINE && PG(i).fam==FAM_EDIT && (PG(i).id[0]==P_E0 || PG(i).id[0]==P_E4) && sch_noedit(TSEL->eng_req))return 0;
    if (PG(i).scope == SC_CZ)                       /* (retired PHASE's CZ mode pages) */
        return 0;
    if (PG(i).scope == SC_MOD)
        return sch_page_shown(i);                      /* the synth's own pages (its schema) */
    if (PG(i).graph == GR_SLICES)
        return 0;
    return !(PG(i).fam == FAM_EDIT && PG(i).id[0] >= P_FM1_ATK &&
             PG(i).id[0] <= P_FM4_LEVEL);
}

static uint32_t page_first(uint32_t fam)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PG(i).fam == fam && page_visible(i))
            return i;
    return 0;
}

/* transient message in the top bar, right of the transport and the BPM: a + b */
static void ui_say(const char *a, const char *b)
{
    uint32_t n;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_t = 40;
    ui.msg2[0] = 0;
}

static void ui_message(const char *s) { ui_say(s, ""); }

static int chain_busy(void) { return chain.running || chain.armed; }
/* Main loop only, with interrupts enabled. PLAY may be consumed between reads;
 * take one coherent snapshot before a flash operation or song preparation. */
static int transport_busy(void)
{
    int busy;
    fm1_irq_off();
    busy = song.playing || seq_counting() || chain_busy() || transport_req == 1u;
    fm1_irq_on();
    return busy;
}
static void chain_play_ui(void)
{
    uint32_t row = ui.song_row < chain_config.count ? ui.song_row : 0u;
    uint32_t rc = chain_prepare_from(row);
    if (!rc) {
        ui.force = 1;
    } else if (rc == 67u) {
        ui_message("NO SOUND MEMORY");
    } else if (rc >= 3u) {
        char b[8] = "A EMPTY";
        b[0] = (char)('A' + rc - 3u);
        ui_say("PATTERN ", b);
    } else {
        ui_message(rc == 1u ? "ADD A SONG ROW" : "STOP FIRST");
    }
}

static uint16_t step_midi_held;                      /* physical MIDI keys held in this entry */
static uint32_t step_midi_keys[16][4];               /* channel/source ownership, before scale and chord */
static void seq_midi_reset(void)                    /* a new page/track ends MIDI step entry */
{
    fm1_irq_off();
    step_midi_r = step_midi_w;
    step_midi_overflow = 0;
    fm1_irq_on();
    step_midi_held = 0;
    memset(step_midi_keys, 0, sizeof step_midi_keys);
}
static void undo_seal(void);
static void page_entered(void)
{
    if (song_picker.active && cur_page()->graph != GR_BROWSE) song_preset_cancel();
    ui.preset_audition = 0;
    const page_t *pg = cur_page();
    if (pg->graph == GR_SCALE_PICKER && !ui.scale_picker_seen) {
        ui.scale_family = (uint8_t)(SCALE_FAMILY[clamp(TSEL->p[P_SCALE], 0, SCALE_TOTAL - 1u)] + 1u);
        ui.scale_picker_seen = 1;
    }
    song.seq_mode = !ui.home && pg->fam == FAM_SEQ;
    ui.entry_open = 0;
    ui.note_pick = 0;
    ui.note_slot = 0;
    ui.note_copy = ui.note_move = 0;
    seq_midi_reset();
    ui.hot_t = 0;                                /* clear the previous page's emphasis */
    ui.act = 0;
    ui.proj_new = 0;
    ui.ptc_on = 0;
    ui.pt_page = 0xFF;
    if (pg->graph == GR_BROWSE) {                /* (OCT- goes back to the sound from before: its loads, a level */
        browse_mark = browse_loads;              /* of their own) */
        undo_seal();
    }
    ui.force = 1;
}

static int step_on(const step_t *st) { return st->time == ST_NOTE && (st->n || st->hit); }

static void step_clear(step_t *st)
{
    st->n = 0;
    st->time = ST_REST;
    st->flags = 0;
    st->vel = 0;
    st->hit = st->acc = 0;
    st->probability = 0;
}

/* ------------------------------------------------------- the DRUM grid --- */
/* SEQ > STEP on a DRUM track is the grid: 8 lanes x the 16 steps of a page. The white keys are the
 * steps of the page shown (a tap toggles the selected lane there), black keys 1..8 select the lane (and play
 * it), black key 9 held is ACC (white keys toggle accents, their LEDs show them), black keys 10 / 11 the page
 * down / up. KNOB 1 STEP, 2 LANE, 3 HIT, 4 ACC edit the cursor step. A sound load never converts the
 * steps: the grid shows a step's notes on their lanes (pads.c step_lanes) and an edit makes the lane its
 * own (grid_own). Live recording on a DRUM track writes hits (seq.c rec_note) */
static int notes_have_recording(const track_t *t);
static int grid_on(void) { return !ui.home && !ui.menu && !ui.confirm && (cur_page()->graph == GR_ROLL || cur_page()->graph == GR_PADHIT) && pads_on(TSEL) && (cur_page()->scope==SC_PADHIT || !notes_have_recording(TSEL) || (song.playing && (song.rec & (1u << song.sel)))); }   /* (NOTES, DRUM HIT) */

/* black key place p (seq.c key_place) held, 0 = not */
static int black_held(uint32_t p)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (key_black(k) && key_place(k) == p)
            return (int)(((fm1_in.notes & ~kb_layer) >> k) & 1u);
    return 0;
}

/* lane l of step s from now on is its hit alone: the step's lane notes become hits (nothing sounds different),
 * a note of another pitch on the lane (a low tom 41) becomes the lane's own */
static void grid_own(const track_t *t, step_t *s, uint32_t l)
{
    uint32_t k, j = 0;
    step_to_grid(t, s);
    for (k = 0; k < s->n; k++)
        if (pads_lane(t, s->note[k]) == l)
            s->hit |= (uint8_t)(1u << l);
        else
            s->note[j++] = s->note[k];
    for (k = j; k < 4u; k++)
        s->note[k] = 0;
    s->n = (uint8_t)j;
}

/* lane l of step i: on 1, off 0, toggled 2 */
static void grid_hit(track_t *t, uint32_t i, uint32_t l, uint32_t on)
{
    step_t *s = &t->step[i % NSTEP];
    uint32_t b = 1u << (l % NLANE);
    s->flags &= (uint8_t)~SF_RECORDED;
    if (on == 2u)
        on = !(step_lanes(t, s) & b);
    if (s->time != ST_NOTE) {                    /* a REST or a TIE: an empty step (nothing to turn off) */
        if (!on)
            return;
        step_clear(s);
        s->time = ST_NOTE;
    }
    grid_own(t, s, l % NLANE);
    if (on) {
        s->hit |= (uint8_t)b;
    } else {
        s->hit &= (uint8_t)~b;
        s->acc &= (uint8_t)~b;
        if (!s->n && !s->hit)
            step_clear(s);
    }
}

/* the accent of lane l at step i (on 1, off 0, toggled 2); an accent on an empty lane adds the hit */
static void grid_acc(track_t *t, uint32_t i, uint32_t l, uint32_t on)
{
    step_t *s = &t->step[i % NSTEP];
    uint32_t b = 1u << (l % NLANE);
    if (on == 2u)
        on = !(step_accents(t, s) & b);
    if (on)
        grid_hit(t, i, l, 1);
    if (!(step_lanes(t, s) & b))
        return;
    grid_own(t, s, l % NLANE);
    if (s->flags & SF_ACCENT) {                  /* a step accent: each hit's own from now on */
        s->acc |= s->hit;
        s->flags &= (uint8_t)~SF_ACCENT;
    }
    s->acc = (uint8_t)(on ? s->acc | b : s->acc & ~b);
}

/* SEQ cursor: wraps inside the pattern length, the bank follows, a step entry ends */
static void notes_preview(void);
static uint32_t pads_hit_selected(void);
static void cursor_set(int32_t c)
{
    int32_t len = TSEL->p[P_SLEN] > 0 ? TSEL->p[P_SLEN] : 1;
    ui.cursor = (uint8_t)((c % len + len) % len);
    ui.bank = (uint8_t)(ui.cursor / 16u);
    ui.entry_open = 0;
    ui.note_pick = 0;
    ui.note_slot = 0;
    notes_preview();
}

static void cursor_fix(void)                           /* LEN got shorter: onto the last step */
{
    if (ui.cursor >= (uint32_t)TSEL->p[P_SLEN])
        cursor_set(TSEL->p[P_SLEN] - 1);
}

/* "E4"; a microtonal key's by the semitone it sounds nearest and the cents off it, "E4-14" (8 B at most: "C#-1-14") */
static void note_name(char *b, uint32_t n)
{
    int32_t cents = 0;
    if (micro_active(TSEL))
        n = micro_nearest(TSEL, (int32_t)n, &cents);
    str_cpy(b, N_NOTE[n % 12u], 4);
    fmt_int(b + str_len(b), (int32_t)(n / 12u) - 1);
    if (cents) {
        uint32_t at = str_len(b);
        b[at] = cents < 0 ? '-' : '+';
        fmt_int(b + at + 1, cents < 0 ? -cents : cents);
    }
}

/* A note on a card: its name the value; a name with cents is wider than a card, so they take the label's row ("-14c" over
 * "D#4"). Returns the label */
static const char *note_card(char *val, char *cents, uint32_t n, const char *label)
{
    uint32_t at;
    note_name(val, n);
    at = 1u + (val[1] == '#');                          /* (past the note and its octave: -1 .. 9) */
    at += 1u + (val[at] == '-');
    if (!val[at])
        return label;
    str_cpy(cents, val + at, 4);
    str_cpy(cents + str_len(cents), "c", 2);
    val[at] = 0;
    return cents;
}

/* SELECT: the open section's pages, both ways (wraps; pages the track does not show skipped) */
static void page_scroll(int32_t dir)
{
    uint32_t fam = cur_page()->fam, n, i = ui.page;
    for (n = 0; n < NPAGES; n++) {
        i = (i + (dir > 0 ? 1u : NPAGES - 1u)) % NPAGES;
        if (PG(i).fam == fam && page_visible(i))
            break;
    }
    if (i == ui.page)
        return;
    ui.page = (uint8_t)i;
    ui.fam_last[fam] = ui.page;
    page_entered();
}

/* the pages a family's button visits, in order: ENV / LFO on an engine with its own (native_has) its pages
 * first; n = 0: none */
static uint32_t fam_pages(uint32_t fam, uint8_t *pg)
{
    uint32_t i, n = 0;
    for (i = 0; i < NPAGES; i++)
        if (native_page(i, fam) && page_visible(i))
            pg[n++] = (uint8_t)i;
    for (i = 0; i < NPAGES; i++)
        if (PG(i).fam == fam && page_visible(i) && !native_page(i, fam))
            pg[n++] = (uint8_t)i;
    return n;
}
static int fam_has(uint32_t fam, uint32_t page)         /* page: one the family's button visits */
{
    return page < NPAGES && (native_page(page, fam) || PG(page).fam == fam) && page_visible(page);
}

static void open_family(uint32_t fam)
{
    if (midi_track_only(TSEL) && (fam == FAM_ENV || fam == FAM_LFO)) return;
    if ((fam == FAM_ENV || fam == FAM_LFO) && native_has(fam)) {   /* ENV / LFO of a synth with its own */
        uint8_t pg[NPAGES];
        uint32_t n = fam_pages(fam, pg), k;
        for (k = 0; k < n && pg[k] != ui.page; k++) {}
        if (!n)
            return;
        ui.page = !ui.home && k < n ? pg[(k + 1u) % n]                 /* again: the next one; from elsewhere the */
                  : native_page(ui.fam_last[fam], fam) && fam_has(fam, ui.fam_last[fam]) ? ui.fam_last[fam] : pg[0];
                                                                        /* engine's page last used, else its first */
    } else if (fam == FAM_FX && (ui.home || ui.page != page_first(FAM_FX))) {
        ui.page = (uint8_t)page_first(FAM_FX);        /* FX: the track's rack (from its slots and SHARED too); */
    } else if (!ui.home && cur_page()->fam == fam) {   /* same button again: next page (FX: on from its rack) */
        uint32_t n, i = ui.page;
        for (n = 0; n < NPAGES; n++) {
            i = (i + 1u) % NPAGES;
            if (PG(i).fam == fam && page_visible(i)) break;
        }
        ui.page = (uint8_t)i;
    } else if (fam == FAM_SAVE) {
        uint32_t i;
        for (i = 0; i < NPAGES; i++)
            if (PG(i).graph == GR_USER) break;
        ui.page = (uint8_t)i;                         /* SAVE enters the sound save screen directly */
    } else {
        ui.page = ui.fam_last[fam] && PG(ui.fam_last[fam]).fam == fam && page_visible(ui.fam_last[fam]) ? ui.fam_last[fam]
                                                                          : (uint8_t)page_first(fam);
    }
    ui.fam_last[fam] = ui.page;
    ui.home = 0;
    page_entered();
    if (fam == FAM_SAVE && cur_page()->graph == GR_USER)
        slot_save_sheet();                       /* SAVE: USER; OCT+ then its slot's sheet on Save here (ui_slots.c) */
}

/* GLO always enters the mixer from another family. Subsequent taps visit the
 * global settings, then return to the mixer; recording has no navigation role. */
static void open_global(void)
{
    uint32_t i;
    if (!ui.home && cur_page()->fam == FAM_TRK) {
        ui.page = (uint8_t)page_first(FAM_GLO);
    } else if (!ui.home && cur_page()->fam == FAM_GLO) {
        for (i = ui.page + 1u; i < NPAGES && (PG(i).fam != FAM_GLO || !page_visible(i)); i++) {}
        ui.page = (uint8_t)(i < NPAGES ? i : page_first(FAM_TRK));
    } else {
        ui.page = (uint8_t)page_first(FAM_TRK);
    }
    ui.home = 0;
    page_entered();
}

static void go_home(void)
{
    if (song_picker.active) song_preset_cancel();
    ui.home = 1;
    ui.entry_open = 0;
    ui.hot_t = 0;
    song.seq_mode = 0;
    ui.force = 1;
}

/* ------------------------------------------------------- track setup --- */
static int seq_is_empty(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (t->step[i].n || t->step[i].hit)
            return 0;
    return 1;
}

/* UNDO of loads. A sound load (a factory or user preset, an engine jump, TOOLS INIT, the editor's PRESET / G_ENGSEL /
 * UP_LOAD) changes the sound only; a pattern load changes the steps and the pattern
 * parameters only. Each first copies the track as it was into a level; EDIT held swaps back what the latest level's
 * loads changed (held again: the level before, EDIT + OCT+ redoes), so steps recorded after a sound load, or a sound
 * edited after a pattern load, stay as they are. Loads in a row on one track with nothing changed in between (the
 * PRESETS knob through the list, one pattern after the other, an editor audition) keep the copy from before the first,
 * so the undo goes back past the whole browse. Eight levels use ordinary SRAM or validated cache RAM.
 * The resident/cache profile keeps one SRAM fallback level if cache startup fails.
 * Not snapshotted: power-on, projects (a project load drops them all) */
enum { UNDO_SOUND = 1, UNDO_PAT = 2, UNDO_PATTERN_SOUND = 4 };
#include "step_snapshot.h"
typedef struct {
    uint8_t trk;                 /* track + 1, 0 = nothing to undo */
    uint8_t keep;                /* the track is as the last load left it (after): a next load keeps the copy */
    uint8_t what;                /* UNDO_SOUND | UNDO_PAT: what the loads since the copy changed (undo_step) */
    uint8_t eng, preset, user_native, patn, pattern_sound;
    uint16_t user;
    uint8_t peng;                /* the patch's engine */
    uint8_t patch[MOD3_PATCH_MAX];   /* the track's patch (patch.c) */
    int16_t p[P_COUNT];
    step_snapshot_t step[NSTEP];
    motion_store_t motion_backup; /* one track only, swaps with the shared event pool on undo */
    uint32_t pattern_gen;
    uint32_t after;              /* track_sig right after the last load */
    uint32_t pat;                /* pat_sig[] of the copy */
    uint32_t t_ms;               /* time of the last load (the editor's SETs after it belong to it) */
} undo_t;
#define UNDO_LV_MAX 8u
/* Reserve the engine arena even when cache startup fails. Never borrow a live
 * audio allocation from the UI or use unvalidated cache memory. */
#define UNDO_SRAM_LEVELS 1u
static undo_t undo_sram[UNDO_SRAM_LEVELS] __attribute__((section(".pool")));
static undo_t *undo_lv = undo_sram;
static uint8_t undo_nlv = UNDO_SRAM_LEVELS, undo_top, undo_cnt;   /* levels; undoable 0 .. top-1, redoable top .. cnt-1 */
static void undo_refresh(void)
{
    if(undo_lv!=undo_sram && (resource[RES_LOAD_UNDO].ptr!=undo_lv ||
       resource[RES_LOAD_UNDO].size<(uint32_t)undo_nlv*sizeof(undo_t))){
        undo_lv=undo_sram;undo_nlv=UNDO_SRAM_LEVELS;undo_top=undo_cnt=0;
    }
}
static undo_t *undo_last(void) { undo_refresh();return &undo_lv[undo_top ? undo_top - 1u : 0u]; }
#define undo (*undo_last())      /* the latest level (tests, the layers: undo.keep); none: level 0 */
static void undo_clear(void) {
    resource_release(RES_LOAD_UNDO);undo_lv=undo_sram;undo_nlv=UNDO_SRAM_LEVELS;
    undo_top = undo_cnt = 0; undo_lv[0].trk = undo_lv[0].keep = 0;
}
static void undo_levels(void)    /* bounded history, acquired before a load */
{
    undo_refresh();
    uint32_t levels=undo_top<UNDO_LV_MAX?undo_top+1u:UNDO_LV_MAX;
    if(levels<=undo_nlv)return;
    undo_t *p=resource_resize(RES_LOAD_UNDO,levels*sizeof(undo_t));
    if(!p)return; /* keep the existing depth; no voice/effect is displaced */
    if(undo_lv==undo_sram && undo_top)memcpy(p,undo_sram,undo_top*sizeof(undo_t));
    undo_lv=p;undo_nlv=(uint8_t)levels;
}
static uint8_t undo_depth;       /* loads nest (an engine jump loads its first preset): the outer one counts;
                                  * melodee_init / project_load raise it to take no copy at all */
static uint32_t pat_sig[NTRK];   /* steps_sig of the pattern the last pattern load put into each track: such
                                  * steps, untouched, are replaced by the next pattern without asking */
static uint8_t pat_last[NTRK];   /* that pattern's list index + 1, 0 = none */

static uint32_t steps_sig(const track_t *t) { return fnv(2166136261u, t->step, sizeof t->step); }
static uint32_t track_sig(const track_t *t)      /* the sound (an FM6 track's patch too), the steps */
{
    uint8_t id[5] = {t->eng_req, t->preset, (uint8_t)t->user, (uint8_t)(t->user>>8), t->user_native};
    uint32_t h = fnv(fnv(steps_sig(t), t->p, sizeof t->p), id, 3), k = trk_index(t);
    h = fnv(h, patch_of(k), patch_len(t->eng_req));   /* (a patch the editor sent between two loads) */
    for (uint32_t j = 0; j < motion.count; j++)
        if ((motion.event[j].place >> 6) == k) h = fnv(h, &motion.event[j], sizeof motion.event[j]);
    return h ^ ((motion.on >> k) & 1u);
}

static const param_desc_t *home_param(uint32_t k,int16_t **vp);
static int scale_shared(uint32_t id);
/* Parameter IDs resolve against the captured track; a synth's own page edits its patch, which is taken whole, with
 * the voice settings a patch may bring (mod_abi3.h patch_changed). This state is never serialized. */
static const uint8_t MOMENTARY_VOICE[4] = {P_VOICE, P_PRIO, P_DETUNE, P_ALLOC};
static struct {
    uint8_t active, track, count, native;
    uint8_t id[P_COUNT+G_COUNT]; int16_t value[P_COUNT+G_COUNT];
    uint8_t patch[MOD3_PATCH_MAX];
    int16_t voice[4];
} momentary __attribute__((section(".pool")));
_Static_assert(P_COUNT+G_COUNT<=256u,"momentary IDs fit byte");
static void momentary_restore(void)
{
    if(!momentary.active)return;
    uint32_t tr=momentary.track;fm1_irq_off();
    if(momentary.native){
        memcpy(patch_of(tr),momentary.patch,MOD3_PATCH_MAX);
        for(uint32_t k=0;k<4u;k++)trk[tr].p[MOMENTARY_VOICE[k]]=momentary.voice[k];
        patch_note(tr,MOD3_PATCH_RESTORE,0xFFFFu);
    }
    for(uint32_t i=0;i<momentary.count;i++){uint32_t id=momentary.id[i];*(id<P_COUNT?&trk[tr].p[id]:&song.g[id-P_COUNT])=momentary.value[i];}
    momentary.active=momentary.count=momentary.native=0;fm1_irq_on();ui.force=1;
}
static const page_t *stage_page(void);
static void momentary_take(uint32_t slot)
{
    const page_t *pg=page_over?page_over:ui.home?stage_page():cur_page();int16_t *vp;const param_desc_t *d;   /* (HOME: Stage's knobs; a bank's: ui_smk.c) */
    if(!(fm1_in.buttons&(1u<<panel.btn[B_LFO])) || pg->graph==GR_STORE || pg->graph==GR_TOOLS)return;
    d=page_desc(pg,slot,&vp);
    if(!d || !vp || d->max==d->min)return;
    uint32_t native=pg->scope==SC_MOD;
    if(!native && pg->scope!=SC_TRACK && pg->scope!=SC_ENGINE &&
       !(pg->scope==SC_GLOBAL && (pg->id[slot]<=G_SWING)))return;
    if(!native && pg->scope==SC_TRACK && scale_shared((uint32_t)(vp-TSEL->p)))return;
    if(!momentary.active){momentary.track=song.sel;momentary.active=1;}
    if(native && !momentary.native){
        uint32_t tr=song.sel;fm1_irq_off();momentary.native=(uint8_t)native;
        memcpy(momentary.patch,patch_of(tr),MOD3_PATCH_MAX);
        for(uint32_t k=0;k<4u;k++)momentary.voice[k]=TSEL->p[MOMENTARY_VOICE[k]];
        fm1_irq_on();
    }
    if(!native){uint32_t id=pg->scope==SC_GLOBAL?P_COUNT+(uint32_t)(vp-song.g):(uint32_t)(vp-trk[momentary.track].p); uint32_t i;for(i=0;i<momentary.count && momentary.id[i]!=id;i++);
        if(i==momentary.count && i<P_COUNT+G_COUNT){momentary.id[i]=(uint8_t)id;momentary.value[i]=*vp;momentary.count++;}}
    ui.pg_down&=(ui_btnmask_t)~(1u<<panel.btn[B_LFO]);
}
static void load_begin_guarded(track_t *t, uint32_t what)
{
    undo_t *u;
    uint32_t i = trk_index(t);
    if (undo_depth++)
        return;
    browse_loads++;
    if (song_picker.active) return;
    motion_restore(t);
    undo_levels();
    u = undo_last();
    if (undo_top && u->keep && u->pattern_gen == t->pattern_gen && u->trk == i + 1u && track_sig(t) == u->after) {
        u->what |= (uint8_t)what;                 /* browsing on: the copy from before the first load stays */
        if(!designer_on() && (what & (UNDO_SOUND | UNDO_PAT)))motion_reset(t);
        return;
    }
    u->keep = 0;
    if (undo_top == undo_nlv) {                   /* full: the oldest level goes (no memmove in libc.c) */
        uint32_t k;
        for (k = 0; k + 1u < undo_nlv; k++)
            memcpy(&undo_lv[k], &undo_lv[k + 1u], sizeof *undo_lv);
        undo_top--;
    }
    u = &undo_lv[undo_top++];                     /* a new level (the redoable ones go) */
    undo_cnt = undo_top;
    u->trk = (uint8_t)(i + 1u);
    u->keep = 0;
    u->pattern_gen = t->pattern_gen;
    u->what = (uint8_t)what;
    u->eng = t->eng_req;
    u->preset = t->preset;
    u->user = t->user;
    u->user_native=t->user_native;
    memcpy(u->p, t->p, sizeof u->p);
    step_snapshot_save(u->step, t->step);
    u->peng = patch_eng_of(i);
    memcpy(u->patch, patch_of(i), MOD3_PATCH_MAX);
    u->pat = pat_sig[i];
    u->patn = pat_last[i];
    u->pattern_sound = pattern_sound[i][t->pattern];
    motion_snapshot_track(t, &u->motion_backup);
    if(!designer_on() && (what & (UNDO_SOUND | UNDO_PAT)))motion_reset(t);
}
static void load_begin(track_t *t, uint32_t what)
{
    momentary_restore();
    uint32_t f=motion_guard();load_begin_guarded(t,what);motion_unguard(f);
}

static void load_end_guarded(track_t *t)
{
    undo_refresh();
    if (--undo_depth || !undo_top)
        return;
    if (song_picker.active) return;
    motion_rebase(t);
    undo.after = track_sig(t);
    undo.keep = 1;
    undo.t_ms = fm1_ms;
}
static void load_end(track_t *t)
{
    uint32_t f=motion_guard();load_end_guarded(t);motion_unguard(f);
}

/* the editor's SET right after a load on the selected track (an audition: G_ENGSEL, then the patch's
 * values): part of that load, the copy from before it stays */
static void load_extend_guarded(track_t *t)
{
    undo_refresh();
    if (undo_top && undo.keep && undo.trk == trk_index(t) + 1u && fm1_ms - undo.t_ms < 1500u) {
        undo.after = track_sig(t);
        undo.t_ms = fm1_ms;
    }
}
static void load_extend(track_t *t)
{
    uint32_t f=motion_guard();load_extend_guarded(t);motion_unguard(f);
}

static int param_kept(uint32_t i);
static int note_clear_history(int redo);
static int recording_history(int redo)
{
    uint32_t f=motion_guard();
    int result=recording_undo_apply(redo);
    motion_unguard(f);
    if(!result)return 0;
    if(result==1){
        ui.note_pick=0;ui.note_generation=recording_generation;
        ui.hot_t=0;ui.force=1;
        ui_message(redo?"REDO RECORDING":"UNDO RECORDING");
    }else ui_message(result==2?"FINISH RECORDING":"UNDO MEMORY FULL");
    return 1;
}

/* EDIT held (redo 0) / EDIT + OCT+ (redo 1): what a level's loads changed (what) and its copy change places, so its
 * redo is the same swap. The sound: engine, preset and its parameters (not param_kept); the pattern: the steps and LEN
 * DIV SWING GATE. The mix (LEVEL PAN MUTE) stays: no load changes it */
static void undo_step_guarded(int redo)
{
    undo_refresh();
    if(recording_history(redo))return;
    if(note_clear_history(redo))return;
    track_t *t;
    undo_t *u;
    uint32_t i;
    char b[4] = {'T', 0, 0, 0};
    if (redo ? undo_top >= undo_cnt : !undo_top) {
        ui_message(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO");
        return;
    }
    u = &undo_lv[redo ? undo_top : undo_top - 1u];
    t = &trk[(u->trk - 1u) % NTRK];
    if (u->pattern_gen != t->pattern_gen) { undo_clear(); ui_message(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO"); return; }
    motion_restore(t);
    if (u->what & UNDO_PATTERN_SOUND) {
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (!pattern_sound_reserve(trk_index(t))) { ui_message("NO SOUND MEMORY"); return; }
    }
    if (u->what & (UNDO_SOUND | UNDO_PAT)) {
        motion_store_t current_motion;
        motion_snapshot_track(t, &current_motion);
        if (motion_replace_track(t, &u->motion_backup) != 0) { ui_message("MOTION FULL"); return; }
        u->motion_backup = current_motion;
    }
    /* Already guarded: audio cannot see half a sound. */
    if (u->what & UNDO_SOUND) {
        uint8_t e = t->eng_req, pr = t->preset, un=t->user_native;
        uint16_t us = t->user;
        panic_req |= (uint8_t)(1u << trk_index(t));
        t->eng_req = u->eng;
        t->preset = u->preset;
        t->user = u->user;
        t->user_native=u->user_native;
        u->eng = e;
        u->preset = pr;
        u->user = us;
        u->user_native=un;
        for (i = 0; i < P_COUNT; i++)
            if (!param_kept(i)) {
                int16_t v = t->p[i];
                t->p[i] = u->p[i];
                u->p[i] = v;
            }
    }
    if (u->what & UNDO_PATTERN_SOUND) {
        uint32_t k = trk_index(t); uint8_t id = pattern_sound[k][t->pattern];
        pattern_sound[k][t->pattern] = u->pattern_sound; u->pattern_sound = id;
        if (song.playing) pattern_sound_switch(k, t->pattern);
    }
    if (u->what & UNDO_PAT) {
        uint32_t ps = pat_sig[trk_index(t)];
        uint8_t pn = pat_last[trk_index(t)];
        pat_sig[trk_index(t)] = u->pat;
        u->pat = ps;
        pat_last[trk_index(t)] = u->patn;
        u->patn = pn;
        for (i = P_SLEN; i <= P_SGATE; i++) {
            int16_t v = t->p[i];
            t->p[i] = u->p[i];
            u->p[i] = v;
        }
        for (i = 0; i < NSTEP; i++) {
            step_t s = t->step[i];
            t->step[i] = step_snapshot_get(&u->step[i]);
            step_snapshot_put(&u->step[i], &s);
        }
    }
    if (u->what & UNDO_SOUND) {                 /* the track's patch as it was (edited, a project's), with the sound */
        uint32_t tr = trk_index(t);
        uint8_t v[MOD3_PATCH_MAX], ve = (uint8_t)patch_eng_of(tr);
        memcpy(v, patch_of(tr), MOD3_PATCH_MAX);
        patch_set(tr, u->peng, u->patch, MOD3_PATCH_MAX, MOD3_PATCH_SET);
        memcpy(u->patch, v, MOD3_PATCH_MAX);
        u->peng = ve;
    }
    u->keep = 0;                                /* the next load copies the track as it is now */
    undo_top = (uint8_t)(redo ? undo_top + 1u : undo_top - 1u);
    if (undo_top) undo_lv[undo_top - 1u].keep = 0;
    if (t == TSEL)
        sync_reload = 1;
    b[1] = (char)('1' + trk_index(t));
    ui_say(redo ? "REDO " : "UNDO ", b);
    ui.force = 1;
}
static void undo_step(int redo)
{
    uint32_t f=motion_guard();undo_step_guarded(redo);motion_unguard(f);
}
static void undo_swap(void) { undo_step(0); }
static void undo_seal(void) {
    uint32_t f=motion_guard();undo_refresh();
    if (undo_top) undo_lv[undo_top - 1u].keep = 0;
    motion_unguard(f);
}   /* the next load: a level of its own */

static void track_defaults_steps(track_t *t)
{
    uint32_t i, guard = motion_guard();
    recording_clear(t);
    motion_reset(t);
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
    motion_unguard(guard);
}

/* the track's settings, not the sound's: what a sound load (factory or user preset, an engine jump,
 * TOOLS INIT) leaves alone. The mix (LEVEL, PAN, MUTE: the TRACKS faders), the arpeggiator (ARP, ARP 2),
 * the scale and key map (SCL), the pattern parameters (LEN, DIV, SWING, GATE),
 * which chops whatever the track plays in time with its sequencer */

/* ROOT, SCL, scale QNT, recording QNT and the MPC degree are the project's: kept the same in each track's own slots
 * for compatibility with the project format and editor protocol. TRN stays the track's. */
static int scale_shared(uint32_t id) { return id == P_ROOT || id == P_SCALE || id == P_TUNING || id == P_QUANT || id == P_MPCDEG || id == P_RECQ; }
static void scale_share(const track_t *from)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_ROOT] = from->p[P_ROOT];
        trk[k].p[P_SCALE] = from->p[P_SCALE];
        trk[k].p[P_TUNING] = from->p[P_TUNING];
        trk[k].p[P_QUANT] = from->p[P_QUANT];
        trk[k].p[P_RECQ] = from->p[P_RECQ];
        trk[k].p[P_MPCDEG] = (int16_t)clamp(from->p[P_MPCDEG], 1, (int32_t)scale_count(from));
    }
}

#include "scale_picker.c"


/* engine e's preset pi (wrapped) as a pick needs it, read now (a synth module's: unpacked from its file); 0: e has none */
static preset_entry_t pe_pick __attribute__((section(".pool")));
static const preset_entry_t *preset_pick(uint32_t e, uint32_t pi)
{
    uint32_t np = eng_npresets(e % NENGINES);
    if (!np)
        return 0;
    preset_fetch_into(e, pi % np, &pe_pick, 1);
    return &pe_pick;
}
/* preset pi of the engine the track asked for, as preset_pick read it (x): the sound only (not the steps, not
 * param_kept) */
static void apply_preset_with(track_t *t, uint32_t pi, const preset_entry_t *x)
{
    uint32_t i, np = eng_npresets(t->eng_req % NENGINES);
    load_begin(t, UNDO_SOUND);
    panic_req |= (uint8_t)(1u << trk_index(t));       /* MONO/POLY may change: release what sounds */
    t->user = 0; t->user_native=0;
    if (t == TSEL)
        sync_reload = 1;
    if (!np || !x) {
        load_end(t);
        return;
    }
    pi %= np;
    t->preset = (uint8_t)pi;
    for (i = 0; i < P_E0; i++)                        /* the rest of the sound to its defaults: a preset */
        if (!param_kept(i))                           /* sounds the same after any edit */
            t->p[i] = TP[i].def;
    const preset_t *pr = &x->pr;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)pr->e[i];
    if (midi_track_only(t)) {
        t->p[P_E0] = (int16_t)(trk_index(t) + 1u);
        for (i = 0; i < FX_SLOTS; i++) {
            t->p[P_FXTYPE + i * FX_PARAMS] = FX_OFF;
            t->p[P_FXSEND + i] = 0;
        }
    }
    t->p[P_ATK] = pr->env[0];
    t->p[P_DEC] = pr->env[1];
    t->p[P_SUS] = pr->env[2];
    t->p[P_REL] = pr->env[3];
    t->p[P_ED_FLT] = pr->fenv;
    t->p[P_VOICE] = pr->mono ? V_LEGATO : V_POLY;   /* mono presets keep the legato feel */
    preset_patch_put(t, x);                           /* the synth's own patch (its module's) */
    load_end(t);
}
static void apply_preset_to(track_t *t, uint32_t pi)
{
    mod_want(t->eng_req % NENGINES);                  /* (a synth module: into SRAM first) */
    apply_preset_with(t, pi, preset_pick(t->eng_req, pi));
}

/* the engine's defaults and its preset pi. With the audio IRQ off: the ISR sees the old engine with
 * its values or the new one with its own (voice.c engine_block), never one with the other's */
static void engine_load_of(track_t *t, uint32_t ei, uint32_t pi)
{
    if (t >= &trk[NPART] && t < &trk[NTRK]) { ei = ENGI_MIDI; pi = 0; }   /* (tracks 5..8: MIDI Track only) */
    mod_want(ei % NENGINES);                          /* (a synth module: into SRAM first) */
    const engine_t *e = ENGINES[ei % NENGINES];
    const preset_entry_t *x = preset_pick(ei, pi);    /* (read before the audio is held off) */
    uint32_t i;
    load_begin(t, UNDO_SOUND);
    fm1_irq_off();
    t->eng_req = (uint8_t)(ei % NENGINES);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    apply_preset_with(t, pi, x);
    fm1_irq_on();
    load_end(t);
}
/* the engine's defaults and its first preset (its schema may name another: the Prophet's first factory program) */
static void set_engine_of(track_t *t, uint32_t ei) { engine_load_of(t, ei, sch_first(ei % NENGINES)); }

/* the engine's own starting point, nothing changed: its INIT preset (Prophet, CZ-1, SID, FM6: "INIT .."), else
 * its first (DRUM: the 808 kit) */
static uint32_t preset_init(uint32_t e)
{
    const engine_t *en = ENGINES[e % NENGINES];
    uint32_t k;
#if MELODEE_MODULES
    if ((k = mod_pst_of(e % NENGINES)) < NMOD)        /* (a synth module's: its file says which) */
        return mods[k].pst_init < mods[k].npresets ? mods[k].pst_init : 0u;
#endif
    for (k = 0; k < en->npresets; k++) {
        const char *n = eng_preset(en, k)->name;
        if (n[0] == 'I' && n[1] == 'N' && n[2] == 'I' && n[3] == 'T' && (n[4] == ' ' || !n[4]))
            return k;
    }
    return 0;
}
/* Init sound (the sound's sheet, the browser's ENGINE knob): the engine's INIT, the steps
 * stay */
static void sound_init_of(track_t *t, uint32_t ei) { engine_load_of(t, ei, preset_init(ei)); }

static void apply_preset(uint32_t pi) { apply_preset_to(TSEL, pi); }
static void set_engine(uint32_t ei) { set_engine_of(TSEL, ei); }

static void track_defaults(track_t *t)
{
    uint32_t i;
    t->mpc_octave = 0;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
    track_defaults_steps(t);
}

/* switch engine (its INIT sound: one starts a sound from there, the browser's next turn its presets) and say so */
static void select_engine(uint32_t e)
{
    sound_init_of(TSEL, e);
    ui_say("ENGINE ", eng_name(TSEL->eng_req));
    ui.force = 1;
}

#include "browse.c"                     /* the PRESETS list: LIST, categories, RECENT, browsing */

static int preset_favorite(void)
{
    uint32_t k = user_of(TSEL);
    uint32_t e=TSEL->eng_req;
    return favorite_has(k<USER_NONE ? (TSEL->user_native?user_native_ns(e):USER_GENERAL) : e,k<USER_NONE?k:TSEL->preset);
}
static void preset_mark(int on)
{
    uint32_t k = user_of(TSEL);
    uint32_t e=TSEL->eng_req;
    if (favorite_set(k<USER_NONE ? (TSEL->user_native?user_native_ns(e):USER_GENERAL) : e,k<USER_NONE?k:TSEL->preset,on)) {
        ui.force = 1;
        settings_save();
    }
}

static void preset_hinted(void) { ui.force = 1; }  /* after a sound load (the phrases it suggested: gone, 2026-10-10) */

static void preset_go(uint32_t n)                    /* load list index n into the selected track (the sound only) */
{
    uint32_t k, e = preset_at(n, &k);
    if(e==USER_NATIVE_P5 || e==USER_NATIVE_FM || e==USER_NATIVE_CZ){native_load(user_native_eng(e),k,song.sel);}
    else if (e == USER_GENERAL) {
        up_load(k);
    } else {
        if (e != TSEL->eng_req) {
            set_engine(e);
            ui_say("ENGINE ", eng_name(TSEL->eng_req));
        }
        apply_preset(k);
    }
    recent_loaded();
    preset_hinted();
}

/* The selected track's engine only: its factory presets, then the used user presets
 * made with it (slot order). List index of the current sound; *total the length */

/* Seven display rows. ALL is a carousel; FAV, RECENT and a category use a bounded window.
 * Return total for an empty row; a current sound not in the list shows the start. */

/* Stage's (HOME's) four knobs: the selected track's engine's EDIT values (engine_t .knob), a native sound's own panel
 * values for PROPHET (filter, its envelope amount, the amp release) and CZ-1 (line 1's wave and DCW peak, detune,
 * vibrato): a page of their scope, edited and drawn as its pages are (ui_input.c edit_param, ui_stage.c) */
static page_t stage_pg;
static const page_t *stage_page(void)
{
    uint32_t e = eng_idx(TSEL->eng_req), k, id[4];
    stage_pg.title = "STAGE";
    stage_pg.fam = FAM_HOME;
    stage_pg.graph = GR_NONE;
    stage_pg.scope = sch_home(e, id) ? SC_MOD : SC_ENGINE;   /* (a synth's own: its schema's HOME roles) */
    for (k = 0; k < 4u; k++)
        stage_pg.id[k] = (uint8_t)(stage_pg.scope == SC_MOD ? (id[k] < 0xFFu ? id[k] : 0xFFu) : ENGINES[e]->knob[k]);
    return &stage_pg;
}
/* HOME: what KNOB k edits (0: nothing there) */
static const param_desc_t *home_param(uint32_t k, int16_t **vp)
{
    return page_desc(stage_page(), k & 3u, vp);
}

/* select track i (ALGORITHM, SONG's KNOB 2, the editor; one in use): its sound, pages and pattern from now on */
static void track_select(uint32_t i)
{
    if (i >= trk_n() || i == song.sel)
        return;
    if (song_picker.active) song_preset_cancel();
    ui.preset_audition = 0;
    browse_commit();                             /* (a sound still pending loads into the track it was meant for) */
    momentary_restore();
    fm1_irq_off();
    song.sel = (uint8_t)i;
    cap_select(i);
    fm1_irq_on();
    ui.entry_open = 0;
    ui.note_copy = ui.note_move = 0;
    seq_midi_reset();                            /* (MIDI notes held for the other track enter nothing here) */
    ui.hot_t = 0;
    ui.cursor = 0;
    ui.bank = 0;
    sync_reload = 1;
    ui.force = 1;
}

static int slice_page_ok(void) { return 0; }
static int slice_page_on(void) { return 0; }

/* ---------------------------------------------------- action pages --- */
/* Pages whose purpose is an action (EDIT > SLICES, CZ TOOLS): the knobs pick, OCT+ does it, OCT- cancels
 * the picked action or goes HOME (ui_input.c). The slot pages (PROJECT, USER, the STOREs: ui_slots.c) do their
 * actions from a slot's sheet, through act_do too */
static uint32_t act_cols(void)                   /* the columns that are actions, a bit each; 0 = not such a page */
{
    const page_t *pg = cur_page();
    if (ui.home)
        return 0;
    if (pg->graph == GR_SLICES)
        return slice_page_ok() ? 12u : 0u;       /* SPLIT JOIN (a SLICE track only) */
    if (pg->graph == GR_TOOLS)
        return 15u;                              /* a synth's actions (its schema's TOOLS page: NAME 1>2 2>1 COMP) */
    return 0;
}

/* the action OCT+ does: its column + 1, 0 = none picked yet */
static uint32_t act_col(void)
{
    return ui.act;
}

static const char *act_name(uint32_t c)          /* column c's action (the footer hint) */
{
    if (cur_page()->graph == GR_SLICES)
        return c == 3u ? "JOIN" : "SPLIT";
    {
        int16_t *vp;
        const param_desc_t *d = page_desc(cur_page(), c & 3u, &vp);
        return d && d->label ? d->label : "";    /* (a TOOLS page: its column's) */
    }
}

/* the picked action would do something now (OCT+ blinks) */
static int act_ready(void)
{
    uint32_t c = act_col();
    if (!c--)
        return 0;
    return !chain_busy();                        /* (CZ TOOLS) */
}

#include "seq_edit.c"                             /* SEQ > STEP: a note's length, its move, its deletion */
#include "seq_notes.c"                            /* NOTES: select original hits without snapping them */
static int note_tools_history(int redo);
#include "seq_undo.c"                             /* .. and the eight edits EDIT held undoes */

static void confirm_open(uint32_t kind, uint32_t trk)
{
    ui.confirm = (uint8_t)kind;
    ui.confirm_trk = (uint16_t)trk;
    ui.act = 0;
    ui.force = 1;
}

#include "sysex_library.c"                       /* staged imports and USB library exports */

#include "note_tools.c"
#include "pattern_dialog.c"                         /* SEQ's pattern dialog: delete, half, double (the editor's PATTERN too) */
#include "ui_song.c"
#include "ui_song_sound.c"

#define TL_MAX 4u
/* s wrapped into at most m lines of w px in face f (at spaces, always at a '\n'; a word wider than the line: at the
 * last character that fits), each line up to 31 characters; the number of lines */
static uint32_t text_lines(const char *s, char (*l)[32], uint32_t m, int32_t w, const aafont_t *f)
{
    uint32_t k = 0;
    while (*s && k < m) {
        uint32_t i, cut = 0;
        while (*s == ' ' || *s == '\n')
            s++;
        for (i = 1; s[i - 1] && i < 32u; i++) {
            str_cpy(l[k], s, i + 1u);
            if (text_w(f, l[k]) > w)
                break;
            if (!s[i] || s[i] == ' ' || s[i] == '\n')
                cut = i;
            if (s[i] == '\n')
                break;
        }
        if (!cut) {                                     /* (one long word: as much of it as fits) */
            for (cut = 1; s[cut] && cut < 31u; cut++) {
                str_cpy(l[k], s, cut + 2u);
                if (text_w(f, l[k]) > w)
                    break;
            }
        }
        str_cpy(l[k++], s, cut + 1u);
        s += cut;
    }
    return k;
}
#include "ui_boot.c"                            /* visible progress before boot flash maintenance */
