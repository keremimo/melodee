/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the UI (firmware/src/ui*.c with the sound sources of hostsim.c;
 * the display and the HAL are stubs, the buttons and knobs are driven through them):
 *   SOUNDS     a sound load (PRESETS knob / page, Init sound, user preset) changes the sound only: the
 *              steps, LEN DIV SWING GATE, ARP, SCL, the SLICER and the mix stay the track's. Power-on:
 *              every sequencer empty.
 *   PATTERNS   SEQ > PATTERNS: KNOB 1 PAT (factory patterns, then user presets that hold one), OCT+
 *              LOAD; over the user's steps the REPLACE? dialog (OCT- / OCT+); the PRESETS hint.
 *   UNDO       a load keeps a copy of the track; SAVE held swaps back what the loads changed (again:
 *              redo); browsing keeps the copy from before the first load; power-on and projects: none.
 *   REC        a tap on every screen arms/disarms the selected track; PLAY starts recording, without navigation.
 *              Long REC never clears. On STEP, armed and playing: keys do not write the
 *              cursor step. With the ARP on, the arp's notes are recorded, not the keys.
 *   MIDI IN    GLO > SYSTEM ROUT: CH1-4 / SEL, note-offs follow their note-ons.
 *   SAVE       refused while playing ("STOP TO SAVE"); a used slot asks OVERWRITE? (OCT+ / OCT-).
 *   ACTIONS    OCT- / OCT+ Esc / Enter off Stage, on release, never both together; the sheets (USER, PROJECT:
 *              a slot's; the pattern's: Clear pattern, TOOLS gone); the OCT LEDs (OCT- lit, OCT+ blinks).
 *   TRACKS     the PRESETS knob does nothing there; KNOB 1 is MUTE.
 *   GRID       SEQ > STEP on a DRUM track: white keys toggle the selected lane's steps of the page, black
 *              keys 1..8 select the lane (and only they play), 9 held = ACC, 10 / 11 the page; KNOB 1..4 STEP
 *              LANE HIT ACC; the key LEDs (hits, the playhead inverted, the lane, ACC, the page keys); a step's
 *              GM notes shown on their lanes and made the lane's own by an edit; live recording into the grid
 *              (lane keys on the grid, GM keys elsewhere; other GM drums as notes; no TIE holds); 12 BEAT from
 *              PATTERNS fills the grid (undo); sound loads keep it, and other engines play it as GM notes.
 *   ROLL       the rolling digits of the card values and the header BPM: strips only, the last frame static,
 *              direction, retarget, the snaps (fast turn, names, shape, page, track, palette, force).
 * Built and run by tests/run_tests.sh. */
#include <stddef.h>
#include <stdint.h>
static uint32_t host_slots[3u * 0x14000u / 4u];          /* USR1..3 (zero: empty), as the flash at 0xA0000 */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

/* ------------------------------------------------------------ HAL stubs --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL];
static uint8_t fm1_led[FM1_NCOL];
static uint8_t fm1_led_dim[3][FM1_NCOL], fm1_led_dim_mask[3];   /* (hal/fm1_input.h: the dim planes) */
#define FM1_TICKS_PER_US 1u
static uint32_t host_ticks, host_pressed, host_notes;
static int32_t host_enc[7];
static uint32_t fm1_ticks(void) { return host_ticks; }
static uint32_t fm1_input_edges(int x) { uint32_t p = host_pressed; (void)x; host_pressed = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t n = host_notes; host_notes = 0; return n; }
static int32_t fm1_enc_take(uint32_t e)
{
    e %= 7u;
    int32_t s = host_enc[e]; host_enc[e] = 0;
#ifdef UI_ENC_TAKE_HOOK
    UI_ENC_TAKE_HOOK(e);                           /* a scan interrupt can publish another detent immediately */
#endif
    return s;
}
static void fm1_wdt_feed(void) {}
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static uint16_t host_screen[240 * 240];
static int host_blit_trace;                       /* (tests/ui_render.c UI_AUDIT_BLITS: each transfer listed) */
static uint64_t host_sent;                        /* pixels sent to the LCD, blitted or filled (tests/ui_render.c) */
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    if (host_blit_trace) fprintf(stderr, " [fill %u,%u %ux%u]", x, y, w, h);
    if (x < 240u && y < 240u)
        host_sent += (uint64_t)(w < 240u - x ? w : 240u - x) * (h < 240u - y ? h : 240u - y);
#ifdef UI_FILL_HOOK
    UI_FILL_HOOK(x, y, w, h);                     /* (tests/ui_render.c: what a fill covers is gone) */
#endif
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            host_screen[(y + j) * 240u + x + i] = (uint16_t)((c >> 8) | (c << 8));
}
static void lcd_sync(void) {}
#define SCOPE_N 512u                              /* audio.c: the HOME oscilloscope */
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    host_sent += (uint64_t)w * h;
    if (host_blit_trace) fprintf(stderr, " [blit %u,%u %ux%u]", x, y, w, h);
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) host_screen[(y + j) * 240u + x + i] = p[j * w + i];
}
static struct { uint32_t stage; } melodee_dbg;
#define MELODEE_FLASH 0
#ifndef MELODEE_VERSION
#define MELODEE_VERSION "TEST"
#endif
#define UI_TEST_PALETTE 1                         /* + GRAY: every screen drawn in it must stay gray */
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_graph.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/ui_layer.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#include "../firmware/src/fm6_store.c"
#include "demo_steps.h"
/* the host screen blanked: the diff (gfx.c) forgets what it sent, as after a fill */
static void screen_clear(void)
{
    memset(host_screen, 0, sizeof host_screen);
    memset(df_key, 0, sizeof df_key);
#ifdef UI_REF_SCREEN
    memset(ref_screen, 0, sizeof ref_screen);
#endif
}

static int check(const char *what, int ok)
{
    printf("ui: %-74s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* main.c melodee_init */
static void ui_power_on(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(&chain, 0, sizeof chain);
    chain_defaults(&chain_config);
    pattern_init();
    memset(&ui, 0, sizeof ui);
    memset(&step_history, 0, sizeof step_history);
    seq_midi_reset();
    memset(&nm, 0, sizeof nm);                    /* NAME closed, no project name */
    memset(&nw, 0, sizeof nw);                    /* NEW SONG closed */
    memset(&pop, 0, sizeof pop);                  /* no popup (ui_popup.c) */
    smap.on = 0; sec.ok = 0;                      /* no map, the sections built again (ui_sections.c) */
    oct_eat = oct_deferred = 0;
    proj_name[0] = 0;
    proj_cur = PROJ_NO_SLOT;
    memset(&favorites, 0, sizeof favorites);
    memset(&brw, 0, sizeof brw);                  /* the browser: nothing pending, LIST ALL, RECENT empty */
    list_recent = 0;
    recent_n = 0;
    undo_clear();
    memset(pat_last, 0, sizeof pat_last);
    memset(proj_slot, 0, sizeof proj_slot);
    up_cache_reset(); native_cache_reset();
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    undo_depth++;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);
        t->engine = t->eng_req;
        track_defaults_steps(t);
        pat_sig[i] = steps_sig(t);
        pat_last[i] = 0;
    }
    undo_depth--;
    song.sel = 0;
    ui.home = 1;
    ui.force = 1;
    panel = PANEL_DEFAULT;
    settings.palette = UI_GRAY_INDEX;
    palette_set(UI_GRAY_INDEX);
    transport_req = 0;
    panic_req = 0;
    mi_r = mi_w = midi_in_overflow = 0;
    memset(midi_sel_on, 0, sizeof midi_sel_on);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_owners, 0, sizeof midi_owners);
    memset(midi_notes, 0, sizeof midi_notes);
    kb_prev = 0;
    memset(live_held, 0, sizeof live_held);
    memset(live_last, 0, sizeof live_last);
    memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target);
    memset(kb_chn, 0, sizeof kb_chn);                   /* chord.c: no key, MIDI chord or last chord */
    memset(mchord, 0, sizeof mchord);
    memset(chord_last, 0, sizeof chord_last);
    perf_held = perf_act = kb_layer = perf_mask = 0;      /* the FX layer: nothing held */
    perf_kill = 0;
    perf_mask = 1u << panel.btn[B_FX];                  /* (ui_input sets them every frame) */
    kb_mask = layer_bits();
    perf_solo = 0;
    memset(&lys, 0, sizeof lys);
    perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
    settings_hold = HOLD_DEF;
    fm1_in.buttons = fm1_in.notes = 0;
}

/* one UI frame (input, then the drawing on the stub display) */
static void frame(void)
{
    host_ticks += 16000u;
    fm1_ms += 16u;
    ui_input();
    ui_draw();
}
static void press(uint32_t label)                 /* a tap: down, a frame, up, a frame */
{
    fm1_ms += 320u; /* Separate navigation taps; dedicated tests exercise double-taps. */
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    frame();
}
static void hold(uint32_t label)                  /* held 0.8 s, then let go */
{
    uint32_t k;
    fm1_ms += 320u; /* Separate navigation taps; dedicated tests exercise double-taps. */
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    for (k = 0; k < 52u; k++)
        frame();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    frame();
}
static void press(uint32_t label);
static void save_redo(void)                       /* SAVE held + OCT+: redo a load */
{
    fm1_in.buttons |= 1u << panel.btn[B_SAVE];
    frame();
    press(B_OCTUP);
    fm1_in.buttons &= ~(1u << panel.btn[B_SAVE]);
    frame();
}
static void turn(uint32_t role, int32_t s)
{
    host_enc[panel.enc[role]] += s * panel.dir[role];
    frame();
    host_ticks += 200000u;                        /* (no knob acceleration between the turns: */
    fm1_ms += 200u;                               /* each one a slow detent, loaded at once) */
}
static void stop_transport(void) { transport_req = 0; song.playing = 0; }
/* a slot page (ui_slots.c): OCT+ its slot's sheet (unless open), KNOB 2 to the row labelled so, OCT+ does it */
static void sheet_do(const char *label)
{
    uint32_t i, n;
    if (pop.on != POP_SHEET)
        press(B_OCTUP);
    if (pop.on != POP_SHEET)
        return;
    for (i = 0; i < pop.n && !str_eq(pop.rows[i].label, label); i++)
        ;
    for (n = 0; n < 16u && pop.sel < i && pop.sel + 1u < pop.n; n++)
        turn(EN_K2, 1);
    for (n = 0; n < 16u && pop.sel > i; n++)
        turn(EN_K2, -1);
    press(B_OCTUP);
}
static int16_t stored_param(uint32_t slot, uint32_t track, uint32_t id)
{
    project_t p;
    return proj_import(&p, &proj_slot[slot], sizeof proj_slot[slot]) ? p.t[track].p[id] : -32768;
}
static uint8_t stored_note(uint32_t slot, uint32_t track, uint32_t step)
{
    project_t p;
    return proj_import(&p, &proj_slot[slot], sizeof proj_slot[slot]) ? p.t[track].step[step].note[0] : 0;
}
static int msg_is(const char *s) { return ui.msg_t && str_eq(ui.msg, s); }
static void my_steps(track_t *t)                  /* "recorded" steps */
{
    uint32_t i;
    track_defaults_steps(t);
    for (i = 0; i < 32u; i += 3u) {
        t->step[i].note[0] = (uint8_t)(40 + i);
        t->step[i].n = 1;
        t->step[i].time = ST_NOTE;
        t->step[i].vel = 99;
    }
    t->p[P_SLEN] = 32;
}

static void go_page(uint32_t graph)               /* the page that draws graph */
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == graph)
            break;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
}
/* the sound of a track: engine, preset and the parameters that are not the track's own (param_kept) */
static int same_sound(const track_t *a, const track_t *b)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i) && a->p[i] != b->p[i])
            return 0;
    return a->eng_req == b->eng_req && a->preset == b->preset;
}

/* sounds never touch the steps; the arp, the scale, the SLICER and the pattern parameters are the track's */
static int test_sound_loads(void)
{
    int bad = 0;
    uint32_t i, empty = 1;
    track_t *t = &trk[0], before;
    ui_power_on();
    for (i = 0; i < NTRK; i++)
        empty &= (uint32_t)seq_is_empty(&trk[i]);
    bad += check("power-on: the four sounds, every sequencer empty, no undo copy", empty && undo.trk == 0 && undo_depth == 0 &&
                 trk[3].eng_req == ENGI_DRUM && trk[3].preset == 0u && trk[0].preset == TRK_DEF[0][1]);
    set_engine_of(t,3);
    my_steps(t);
    t->p[P_E0 + 1] = 77;                          /* a sound edit */
    t->p[P_AMODE] = 2;                            /* and the track's own settings */
    t->p[P_AOCT] = 3;
    t->p[P_SCALE] = 2;
    t->p[P_TRANS] = 5;
    t->p[P_SLCR] = SL_GATE;
    t->p[P_SLPAT] = 4;
    t->p[P_SDIV] = 1;
    t->p[P_LEVEL] = 90;
    before = *t;
    turn(EN_PRESET, 1);                           /* HOME: the next preset */
    bad += check("PRESETS on HOME loads the sound: steps, LEN, DIV untouched", t->preset == (uint8_t)((before.preset + 1u) % ENGINES[3]->npresets) &&
                 !memcmp(t->step, before.step, sizeof t->step) && t->p[P_SLEN] == 32 && t->p[P_SDIV] == 1);
    bad += check("..ARP, SCL, the SLICER and the mix stay the track's", t->p[P_AMODE] == 2 && t->p[P_AOCT] == 3 && t->p[P_SCALE] == 2 &&
                 t->p[P_TRANS] == 5 && t->p[P_SLCR] == SL_GATE && t->p[P_SLPAT] == 4 && t->p[P_LEVEL] == 90);
    bad += check("..and no warning about the sequence", !msg_is("T1 SEQ REPLACED"));
    for (i = 0; i < ENGINES[3]->npresets; i++)
        if (str_eq(ENGINES[3]->presets[i].name, "ARP BASS"))
            break;
    t->p[P_AMODE] = 0;
    t->preset = (uint8_t)(i - 1u);
    turn(EN_PRESET, 1);
    bad += check("ARP BASS (an arp-named preset) leaves the track arp off", t->preset == i && t->p[P_AMODE] == 0);
    t->p[P_AMODE] = 2;
    before = *t;
    for (i = 0; i < ENGINES[3]->npresets - before.preset + 1u; i++) /* through SID's remaining sounds, into next engine */
        turn(EN_PRESET, 1);
    bad += check("browsing on, into another engine: the steps still untouched", t->eng_req != before.eng_req &&
                 !memcmp(t->step, before.step, sizeof t->step) && t->p[P_AMODE] == 2);
    bad += check("browsing keeps the copy from before the first load", undo.trk == 1u && undo.what == UNDO_SOUND &&
                 undo.p[P_E0 + 1] == before.p[P_E0 + 1] && undo.eng == before.eng_req && undo.preset == before.preset);
    t->step[0].note[0] = 99;                      /* recorded after the loads */
    hold(B_SAVE);
    bad += check("SAVE held: the sound back (UNDO T1); steps recorded since stay", same_sound(t, &before) &&
                 t->step[0].note[0] == 99 && msg_is("UNDO T1"));
    bad += check("SAVE held does not open the SAVE pages", ui.home);
    save_redo();
    bad += check("SAVE + OCT+: the loads again (REDO T1)", t->eng_req != before.eng_req && t->step[0].note[0] == 99 &&
                 msg_is("REDO T1") && ui.home && !song.octave);
    hold(B_SAVE);
    t->p[P_LEVEL] = 50;                           /* a mix change after the undo */
    turn(EN_PRESET, 1);                           /* a load after an undo copies the track as it is now */
    hold(B_SAVE);
    bad += check("a load after the undo: its own copy; the mix (LEVEL) is never swapped", same_sound(t, &before) && t->p[P_LEVEL] == 50);
    press(B_SAVE);
    bad += check("SAVE tap opens the SAVE pages (on release)", !ui.home && cur_page()->fam == FAM_SAVE);
    /* Init sound (set_engine), user preset load: the sound only, undoable; one copy for all tracks */
    ui_power_on();
    my_steps(&trk[1]);
    trk[1].p[P_SLCR] = SL_STUT;
    before = trk[1];
    track_select(1);
    set_engine(trk[1].eng_req);                   /* Init sound */
    bad += check("Init sound: the sound only (steps and SLICER kept), a copy of T2", undo.trk == 2u &&
                 !memcmp(trk[1].step, before.step, sizeof before.step) && trk[1].p[P_SLCR] == SL_STUT);
    track_select(0);
    my_steps(&trk[0]);
    trk[0].p[P_SDIV] = 2;
    up_store(3, "MINE");
    bad += check("a user preset keeps its pattern in the record (format unchanged)", up_used(3) && up_rec(3)->note[0] == 40);
    trk[0].p[P_SDIV] = 0;
    track_defaults_steps(&trk[0]);
    trk[0].step[5].note[0] = 33;
    trk[0].step[5].n = 1;
    before = trk[0];
    up_load(3);
    bad += check("a user preset load: the sound only (steps, DIV kept); the copy is now T1's", trk[0].user == 4u && undo.trk == 1u &&
                 !memcmp(trk[0].step, before.step, sizeof before.step) && trk[0].p[P_SDIV] == 0);
    undo_swap();
    bad += check("undo of a user preset load", trk[0].user == 0 && !memcmp(trk[0].step, before.step, sizeof before.step));
    /* project load: no copy, and the old one is gone */
    project_save(1);
    up_load(3);
    project_load(1);
    bad += check("a project load drops the copy and takes none", undo.trk == 0 && undo_depth == 0);
    ui_power_on();
    bad += check("sample engines retired with reserved IDs",!eng_ok(4) && !eng_ok(8) && !eng_ok(13) && !eng_ok(14));
    host_808_kit(t);
    t->preset = 0;                             /* a project written before the factory removal */
    my_steps(t);
    t->p[P_LEVEL] = 71;
    before = *t;
    project_save(0);
    apply_preset_to(t, 0);
    track_defaults_steps(t);
    project_load(0);
    bad += check("saved 808 DRUM keeps every parameter and step with valid display metadata",
                 t->eng_req == ENGI_DRUM && t->preset == 0u && !memcmp(t->p, before.p, sizeof t->p) &&
                 !memcmp(t->step, before.step, sizeof t->step));
    ui_power_on();
    host_808_kit(&trk[3]);
    my_steps(&trk[3]);
    trk[3].p[P_SDIV] = 3;
    before = trk[3];
    project_save(1);
    project_v6_t legacy = {0};
    project_t decoded;
    proj_import(&decoded, &proj_slot[1], sizeof proj_slot[1]);
    legacy.magic = PROJ_MAGIC_V6; legacy.size = sizeof legacy;
    legacy.parts = 0; legacy.phys = PROJ_PHYS;
    memcpy(legacy.g, decoded.g, sizeof legacy.g);
    for (uint32_t ti = 0; ti < NTRK; ti++) {
        memcpy(legacy.t[ti].p, decoded.t[ti].p, 61u * sizeof(int16_t));
        memcpy(legacy.t[ti].p + 61, decoded.t[ti].p + P_E0, 8u * sizeof(int16_t));
        legacy.t[ti].engine = decoded.t[ti].engine; legacy.t[ti].preset = decoded.t[ti].preset;
        for (uint32_t st = 0; st < NSTEP; st++) memcpy(&legacy.t[ti].step[st], &decoded.t[ti].step[st], sizeof(step10_t));
    }
    legacy.t[3].engine = 0;
    legacy.g[G_DRLVL] = 71; legacy.g[G_DRREV] = 43;
    legacy.g[G_RTYPE] = 10;                               /* (id 24 was the GM drum part's MIDI channel: 10) */
    legacy.sum = proj_hash(&legacy, offsetof(project_v6_t, sum));
    memcpy(&proj_slot[1], &legacy, sizeof legacy);
    project_load(1);
    bad += check("old GM projects use 808 and retain envelope, mix and steps",
                 trk[3].eng_req == ENGI_DRUM && trk[3].p[P_E0] == 4 &&
                 !memcmp(&trk[3].p[P_ATK], &before.p[P_ATK], 4u * sizeof(int16_t)) &&
                 trk[3].p[P_LEVEL] == 71 && trk[3].p[P_REV] == 43 && trk[3].p[P_SDIV] == 3 &&
                 !memcmp(trk[3].step, before.step, sizeof trk[3].step));
    bad += check("  its old drum channel (10, in id 24) loads as REVERB TYPE ROOM", song.g[G_RTYPE] == 0);
    printf("ui: undo level %u bytes\n", (unsigned)sizeof undo);
    return bad;
}

/* the phrases (SEQ > PHRASES, the factory patterns) are gone: no page, a sound load leaves the steps as they are */
static int test_no_phrases(void)
{
    int bad = 0;
    uint32_t i, found = 0;
    track_t *t = &trk[0];
    step_t keep[NSTEP];
    ui_power_on();
    for (i = 0; i < NPAGES; i++)
        found |= str_eq(PAGES[i].title, "PHRASES");
    bad += check("no SEQ > PHRASES page", !found);
    bad += check("the parts power on with empty sequencers", seq_is_empty(&trk[0]) && seq_is_empty(&trk[1]) &&
                 seq_is_empty(&trk[2]) && seq_is_empty(&trk[3]));
    demo_pat16(t, DEMO_ACID);
    memcpy(keep, t->step, sizeof keep);
    set_engine_of(t, ENGI_FM6);
    apply_preset_to(t, 0);
    go_page(GR_BROWSE);
    turn(EN_K2, 1);
    bad += check("a sound load from the browser keeps the steps", !memcmp(keep, t->step, sizeof keep));
    return bad;
}

static int test_rec(void)
{
    static const uint8_t FAMS[3] = {FAM_TRK, FAM_SEQ, FAM_ARP};
    int bad = 0;
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        ui_power_on();
        track_select(2);
        open_family(FAMS[k]);
        press(B_REC);
        bad += check(k == 0 ? "REC on TRACKS: arms the selected track without starting PLAY" :
                     k == 1 ? "REC on SEQ: arms the selected track without starting PLAY" : "REC on ARP: arms the selected track without starting PLAY",
                     song.rec == 4u && transport_req == 0u);
        song.playing = 1;
        transport_req = 0;
        press(B_REC);
        bad += check("REC again disarms (the transport runs on)", song.rec == 0u && transport_req == 0u);
        hold(B_REC);
        bad += check("REC held captures (nothing played: nothing), no arming, no dialog", ui.confirm == CF_NONE && song.rec == 0u &&
                     msg_is("NOTHING TO CAPTURE"));
    }
    ui_power_on();
    press(B_REC);
    bad += check("REC on HOME arms without changing the screen or starting PLAY", ui.home && song.rec == 1u && transport_req == 0u);
    /* STEP page: armed + playing -> the keys record live only */
    ui_power_on();
    open_family(FAM_SEQ);
    bad += check("SEQ opens STEP", cur_page()->scope == SC_STEP);
    press(B_REC);
    song.playing = 1;
    transport_req = 0;
    fm1_in.notes = host_notes = 1u << 7;
    frame();
    fm1_in.notes = 0;
    frame();
    bad += check("STEP, armed and playing: a key does not write the cursor step", !trk[0].step[0].n && ui.cursor == 0);
    song.rec = 0;
    fm1_in.notes = host_notes = 1u << 7;
    frame();
    fm1_in.notes = 0;
    frame();
    bad += check("STEP, not armed: the key writes the cursor step (and moves on)", trk[0].step[0].n == 1 && ui.cursor == 1);
    /* ARP: the arp's notes are recorded, not the key */
    ui_power_on();
    {
        track_t *t = &trk[0];
        uint32_t i, n = 0, notes = 0;
        t->engine = t->eng_req;
        t->p[P_AMODE] = 1;
        t->p[P_AOCT] = 2;
        t->p[P_APROB] = 127;
        song.rec = 1;
        song.playing = 1;
        input_on(t, 60, 100);
        for (i = 0; i < NSTEP; i++)
            n += t->step[i].n;
        bad += check("ARP on: the key held is not recorded", n == 0);
        arp_tick(t, 1);
        for (i = 0; i < NSTEP; i++)
            if (t->step[i].n)
                notes = t->step[i].note[0];
        bad += check("ARP on: the note the arp plays is recorded", notes == 60u);
        t->p[P_AMODE] = 0;
        input_off(t, 60);
    }
    return bad;
}

static uint32_t gated_notes(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++) n += t->v[i].gate != 0;
    return n;
}

static int test_midi(void)
{
    int bad = 0;
    track_t *a, *b;
    ui_power_on();
    song.sel = 2;
    midi_hint = 0;
    midi_event(0x90, 0, 60, 100); midi_event(0x90, 10, 61, 100);
    bad += check("ROUT CH1-4 (default): channel 1 -> part 1, channel 11 -> selected, channel 10 -> the DRUM track 4",
                 song.g[G_ROUTE] == 0 && midi_sel_on[0][60] == 1u && midi_sel_on[10][61] == 3u && midi_track(9) == &trk[3]);
    bad += check("a note into another track tells the UI", midi_hint == 1u);
    frame(); bad += check("MIDI IN -> T1 shown", msg_is("MIDI IN -> T1"));
    song.g[G_ROUTE] = 1;
    midi_event(0x80, 0, 60, 0); midi_event(0x90, 0, 62, 100);
    bad += check("ROUT changes: note-off releases original owner, new note uses selection",
                 !gated_notes(&trk[0]) && midi_sel_on[0][62] == 3u && midi_track(3) == &trk[2]);
    song.sel = 1; midi_event(0x80, 0, 62, 0);
    bad += check("note-off after selection follows the note-on's track", !midi_sel_on[0][62]);
    midi_event(0x80, 9, 61, 0);
    ui_power_on();
    song.g[G_ROUTE] = 1;
    midi_event(0x90, 0, 60, 100);
    song.sel = 1;
    midi_event(0x90, 0, 60, 100);
    bad += check("same MIDI channel/pitch on a new selected track releases its old owner",
                 !gated_notes(&trk[0]) && gated_notes(&trk[1]) && midi_sel_on[0][60] == 2u);
    midi_event(0x80, 0, 60, 0);
    midi_event(0x80, 0, 60, 0);
    bad += check("both note-offs after a MIDI ownership change leave neither track held",
                 !gated_notes(&trk[0]) && !gated_notes(&trk[1]) && !midi_sel_on[0][60]);
    ui_power_on();
    song.sel = 2; song.g[G_ROUTE] = 1;
    midi_event(0x90, 0, 61, 100);
    song.g[G_ROUTE] = 0;
    midi_event(0x90, 0, 61, 100);
    midi_event(0x80, 0, 61, 0);
    bad += check("same channel/pitch across a ROUT change cannot leave its previous part held",
                 !gated_notes(&trk[2]) && !gated_notes(&trk[0]) && !midi_sel_on[0][61]);
    {
        uint32_t hold;
        for (hold = 0; hold < 2u; hold++) {
            ui_power_on();
            song.g[G_ROUTE] = 1;
            trk[0].p[P_AMODE] = 1; trk[0].p[P_AHOLD] = (int16_t)hold;
            midi_event(0x90, 0, 60, 100);
            song.sel = 1;
            midi_event(0x90, 0, 60, 100);
            bad += check("MIDI ownership transfer releases old ARP keys and respects explicit HOLD",
                         !trk[0].arp_phys && trk[0].nheld == hold && midi_sel_on[0][60] == 2u);
        }
    }
    ui_power_on();
    trk[1].p[P_AMODE] = trk[1].p[P_AHOLD] = 1;
    midi_event(0x90, 0, 60, 100);
    midi_event(0x90, 1, 64, 100);
    song.playing = 1;
    {
        uint32_t i, held = 0;
        for (i = 0; i < MQ; i++) midi_in_event(0x643C9909u);
        midi_in_event(0x003C8908u);                      /* note-off lost to a full ring */
        events_block(CTL);
        for (i = 0; i < NTRK * NVOICE; i++) held += trk[i / NVOICE].v[i % NVOICE].gate;
        bad += check("MIDI overflow releases held notes and latched ARP, keeps transport running",
                     !held && !trk[1].nheld && !trk[1].arp_phys && !midi_sel_on[0][60] &&
                     !midi_sel_on[1][64] && !midi_in_overflow && mi_r == mi_w && song.playing);
        midi_in_event(0x643D9009u);
        events_block(CTL);
        bad += check("MIDI accepts a fresh note after overflow recovery", midi_sel_on[0][61] == 1u && trk[0].v[0].gate);
    }
    {
        const page_t *pg = &PAGES[page_first(FAM_GLO) + 1u];
        int16_t *vp;
        const param_desc_t *d = page_desc(pg, 2, &vp);
        char v[8];
        const char *u;
        param_format(d, 1, v, &u);
        bad += check("GLO > SYSTEM K3: ROUT CH1-4 / SEL", str_eq(pg->title, "SYSTEM") && vp == &song.g[G_ROUTE] && str_eq(v, "SEL"));
    }
    return bad;
}

static int test_save(void)
{
    int bad = 0, ok;
    uint32_t i;
    ui_power_on();
    trk[0].p[P_E0] = 5;
    press(B_SAVE);
    bad += check("SAVE opens USER on its slot, no sheet popped up, without writing a slot",
                 !ui.home && cur_page()->graph == GR_USER && !pop.on && !up_used(0));
    press(B_OCTUP);
    bad += check("..OCT+: the slot's sheet, on Save here", pop.on == POP_SHEET && str_eq(pop.rows[pop.sel].label, "Save here"));
    press(B_OCTUP);
    bad += check("..OCT+ again opens NAME (the automatic name), writes nothing yet",
                 name_on() && nm.kind == NK_USER_SAVE && str_eq(nm.s, nm.ph) && nm.cur == nm.len && !up_used(0));
    press(B_OCTUP);
    {
        char nmb[16], au[16];
        up_name(0, nmb);
        up_auto_name(au, trk[0].eng_req, 0);
        bad += check("..OCT+ again stores the edited sound with that name (SAVE, OCT+, OCT+, OCT+)",
                     !name_on() && up_used(0) && up_value(up_rec(0), P_E0) == 5 && ui.act == 0u && str_eq(nmb, au));
    }
    press(B_SAVE);
    bad += check("SAVE again still cycles to PROJECT", cur_page()->graph == GR_SLOTS);
    go_home();
    press(B_SAVE);
    ok = cur_page()->graph == GR_USER && !pop.on;
    press(B_OCTUP);
    bad += check("SAVE from HOME returns directly to USER (OCT+: its sheet, on Save here), even after visiting PROJECT",
                 ok && pop.on == POP_SHEET && str_eq(pop.rows[pop.sel].label, "Save here"));
    press(B_OCTUP);
    bad += check("direct SAVE still asks before overwriting an occupied slot",
                 ui.confirm == CF_OVR_USER && up_value(up_rec(0), P_E0) == 5);
    press(B_OCTDN);
    ui_power_on();
    song.playing = 1;
    project_save(0);
    bad += check("project save while playing: refused, STOP TO SAVE", !project_used(0) && msg_is("STOP TO SAVE"));
    bad += check("user preset save while playing: refused", up_store(0, "X") == 2 && !up_used(0));
    song.playing = 0;
    transport_req = 1;
    project_save(0);
    bad += check("a pending PLAY cannot save a project", !project_used(0) && msg_is("STOP TO SAVE"));
    bad += check("a pending PLAY cannot save a user preset", up_store(0, "X") == 2 && !up_used(0));
    transport_req = 0;
    chain.armed = 1;
    bad += check("a pending SONG cannot save a user preset", up_store(0, "X") == 2 && !up_used(0));
    chain.armed = 0;
    song.playing = 1;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_SLOTS)
            break;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
    ui.msg_t = 0;
    press(B_OCTUP);
    bad += check("PROJECT: OCT+ opens the slot's sheet, saves nothing", pop.on == POP_SHEET && !project_used(0) && !ui.msg_t);
    sheet_do("Save here");
    bad += check("Save here while playing: STOP TO SAVE, no NAME", msg_is("STOP TO SAVE") && !project_used(0) &&
                 song.g[G_SAVE] == 0 && !name_on());
    stop_transport();
    sheet_do("Save here");
    bad += check("stopped, an empty slot: Save here opens NAME (no name yet: PROJECT A)", name_on() && nm.kind == NK_PROJ_SAVE &&
                 nm.len == 0u && str_eq(nm.ph, "PROJECT A") && !project_used(0));
    press(B_OCTUP);
    bad += check("..OCT+ saves it unnamed, SAVE dropped", !name_on() && project_used(0) && msg_is("SAVED (RAM)") &&
                 ui.act == 0u);
    sheet_do("Save here");
    bad += check("a used slot: the OVERWRITE? dialog", ui.confirm == CF_OVR_PROJ && ui.confirm_trk == 0);
    trk[0].p[P_E0] = 5;
    press(B_OCTDN);
    bad += check("OCT- keeps the slot", ui.confirm == CF_NONE && stored_param(0, 0, P_E0) != 5);
    sheet_do("Save here");
    press(B_OCTUP);
    bad += check("OCT+ (YES): NAME, the slot not written yet", name_on() && stored_param(0, 0, P_E0) != 5);
    press(B_OCTUP);
    bad += check("OCT+ overwrites it", ui.confirm == CF_NONE && !name_on() && stored_param(0, 0, P_E0) == 5);
    trk[0].p[P_LEVEL] = 11;
    sheet_do("Load");
    bad += check("Load: the project back", trk[0].p[P_LEVEL] != 11 && trk[0].p[P_LEVEL] == stored_param(0, 0, P_LEVEL) &&
                 msg_is("LOADED") && ui.act == 0u);
    /* user presets */
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_USER)
            break;
    ui.page = (uint8_t)i;
    page_entered();
    up_store(4, "OLD");
    ui.uslot = 4;
    sheet_do("Save here");
    bad += check("USER Save here over a used slot: the OVERWRITE? dialog", ui.confirm == CF_OVR_USER && ui.confirm_trk == 4u);
    press(B_OCTUP);
    bad += check("YES: NAME with the sound's name (not the old slot's)", name_on() && !str_eq(nm.s, "OLD"));
    press(B_OCTUP);
    {
        char nm[16];
        up_name(4, nm);
        bad += check("OCT+ stores the sound there", ui.confirm == CF_NONE && !str_eq(nm, "OLD"));
    }
    song.playing = 1;
    ui.msg_t = 0;
    sheet_do("Erase");
    bad += check("USER Erase while playing: STOP TO SAVE", msg_is("STOP TO SAVE") && up_used(4));
    stop_transport();
    return bad;
}

static void go_title(const char *title)
{
    uint32_t i;
    for (i = 0; i < NPAGES && !str_eq(PAGES[i].title, title); i++)
        ;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
}
static uint32_t oct_leds_seen(int all)            /* over 1 s: the OCT LED bits lit at some point (all: throughout) */
{
    uint32_t k, any = 0, every = 3u;
    for (k = 0; k < 64u; k++) {
        frame();
        any |= oct_leds();
        every &= oct_leds();
    }
    return all ? every : any;
}

/* the action pages: the knobs pick, OCT+ does it, OCT- cancels / goes HOME; the LEDs; chords */
static int test_actions(void)
{
    int bad = 0;
    uint32_t i, found = 0;
    track_t *t = &trk[0];
    ui_power_on();
    press(B_OCTUP);
    bad += check("HOME: OCT+ shifts the octave (lit)", song.octave == 1 && oct_leds() == 2u);
    my_steps(t);
    go_title("PATTERN");                          /* (a page off Stage) */
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];   /* both together (the UPDATE MODE hold): nothing */
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    fm1_in.buttons |= 1u << panel.btn[B_OCTDN];
    host_pressed |= 1u << panel.btn[B_OCTDN];
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTDN]);
    frame();
    bad += check("OCT- + OCT+ together off Stage: no sheet, no HOME, no octave change", !pop.on && !ui.home && song.octave == 1);
    go_home();
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];   /* pressed on HOME, let go on PATTERN: the octave, nothing there */
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    go_title("PATTERN");
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    bad += check("a press that began elsewhere does nothing there", !pop.on && song.octave == 2 && !ui.home);
    press(B_OCTDN);
    bad += check("PATTERN: OCT- goes HOME (the octave stays)", ui.home && song.octave == 2);
    /* TOOLS is gone: Clear pattern in the pattern's sheet, Init sound in the sound's */
    for (i = 0; i < NPAGES; i++)
        found |= str_eq(PAGES[i].title, "TOOLS");
    bad += check("no TOOLS page", !found);
    my_steps(t);
    go_title("PATTERN");
    press(B_OCTUP);
    bad += check("PATTERN: OCT+ the pattern's sheet (Clear pattern, Clear motion)", pop.on == POP_SHEET &&
                 pop.rows == SHEET_PATTERN && !seq_is_empty(t));
    sheet_do("Clear pattern");
    bad += check("Clear pattern first asks", ui.confirm == CF_CLEAR_SEQ && !seq_is_empty(t));
    press(B_OCTUP);
    bad += check("OCT+ clears the pattern", seq_is_empty(t) && msg_is("PATTERN CLEARED"));
    go_title("EDIT 1");
    t->p[P_E0] = (int16_t)(t->p[P_E0] + 1);
    sheet_do("Init sound");
    bad += check("the sound's sheet: Init sound first asks", ui.confirm == CF_INIT_SOUND);
    press(B_OCTUP);
    bad += check("OCT+: SOUND INIT", msg_is("SOUND INIT"));
    press(B_OCTDN);
    bad += check("OCT- on a sound page: HOME", ui.home);
    /* the dialogs and the menu */
    open_family(FAM_SEQ);
    my_steps(t);
    confirm_open(CF_CLEAR_SEQ, song.sel);
    frame();
    bad += check("a dialog: OCT+ blinks, OCT- lit", ui.confirm == CF_CLEAR_SEQ && oct_leds_seen(0) == 3u && oct_leds_seen(1) == 1u);
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    bad += check("..it answers on release, not on the press", ui.confirm == CF_CLEAR_SEQ && !seq_is_empty(t));
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    bad += check("..OCT+ let go: cleared", ui.confirm == CF_NONE && seq_is_empty(t));
    bad += check("back on SEQ (NOTES): OCT- lit (Esc), OCT+ lit (a note, its sheet), not the octave",
                 oct_leds() == 3u && song.octave);
    return bad;
}

static int test_tracks(void)
{
    int bad = 0;
    track_t before;
    ui_power_on();
    open_family(FAM_TRK);
    before = trk[0];
    turn(EN_PRESET, 1);
    bad += check("TRACKS: the PRESETS knob does nothing", trk[0].preset == before.preset && !memcmp(trk[0].p, before.p, sizeof before.p));
    turn(EN_K4, 1);
    bad += check("MIXER KNOB 4 right: MUTE ON (the selected track stays)", trk[0].p[P_MUTE] == 1 && song.sel == 0);
    turn(EN_K1, -3);
    bad += check("KNOB 1 changes LEVEL only (MUTE stays)", trk[0].p[P_MUTE] == 1 && trk[0].p[P_LEVEL] == before.p[P_LEVEL] - 3);
    turn(EN_K2, 2);
    bad += check("KNOB 2 changes PAN", trk[0].p[P_PAN] == before.p[P_PAN] + 2);
    turn(EN_K4, -1);
    bad += check("MIXER KNOB 4 left: MUTE OFF", trk[0].p[P_MUTE] == 0);
    turn(EN_ALGO, 1);
    bad += check("ALGORITHM selects the track", song.sel == 1);
    return bad;
}

/* key k of colour black (1) / white (0) at place p (seq.c key_place) */
static uint32_t key_at(int black, uint32_t p)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (key_black(k) == black && key_place(k) == p)
            return k;
    return 0;
}
static void tap_key(uint32_t k)                   /* a key down for a frame, then up */
{
    fm1_in.notes |= 1u << k;
    host_notes |= 1u << k;
    frame();
    fm1_in.notes &= ~(1u << k);
    frame();
}
static uint32_t lane_steps(const track_t *t, uint32_t l)   /* bit i: lane l strikes at step i (0..31) */
{
    uint32_t i, m = 0;
    for (i = 0; i < 32u; i++)
        m |= ((step_lanes(&t->step[i]) >> l) & 1u) << i;
    return m;
}

static int test_grid(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    uint32_t i, k, leds, ok;
    ui_power_on();
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    open_family(FAM_SEQ);
    frame();
    bad += check("SEQ > STEP on a DRUM track is the grid (keys to the UI), GRID in the footer", grid_on() && song.grid == 1u &&
                 cur_page()->scope == SC_STEP);
    bad += check("the lanes: KICK SNARE CLAP HATCL HATOP TOM RIM BELL on GM 36 38 39 42 46 45 37 56",
                 str_eq(drum_lane_name(t, 0), "KICK") && str_eq(drum_lane_name(t, 7), "BELL") && DRUM_LANE_NOTE[3] == 42 &&
                 DRUM_LANE_NOTE[6] == 37 && drum_lane(41) == DV_TOM && drum_lane(49) == DV_BELL && drum_lane(35) == DV_KICK);
    tap_key(key_at(1, 1));
    bad += check("black key 2: lane 2 (SNARE) selected", ui.lane == 1u);
    tap_key(key_at(0, 4));
    bad += check("white key 5: SNARE on step 5, the cursor there", t->step[4].hit == 1u << DV_SNARE && t->step[4].time == ST_NOTE &&
                 !t->step[4].n && ui.cursor == 4u);
    leds = grid_leds();
    bad += check("LEDs: white key 5 lit, black key 2 lit, ACC and the page keys dark (LEN 16)",
                 (leds >> key_at(0, 4) & 1u) && (leds >> key_at(1, 1) & 1u) && !(leds >> key_at(0, 3) & 1u) &&
                 !(leds >> key_at(1, 0) & 1u) && !(leds >> key_at(1, GK_ACC) & 1u) && !(leds >> key_at(1, GK_PGUP) & 1u));
    song.playing = 1;
    t->seq_idx = 4;
    leds = grid_leds();
    ok = !(leds >> key_at(0, 4) & 1u);
    t->seq_idx = 6;
    leds = grid_leds();
    ok &= (leds >> key_at(0, 6) & 1u) && !(leds >> key_at(0, 4) & 1u) == 0u;
    song.playing = 0;
    bad += check("LEDs: the step playing inverted (a hit goes dark, an empty step lights)", ok);
    tap_key(key_at(0, 4));
    bad += check("white key 5 again: off, an empty step (REST)", !t->step[4].hit && t->step[4].time == ST_REST);
    /* ACC held: the white keys set accents, their LEDs show them */
    fm1_in.notes |= 1u << key_at(1, GK_ACC);
    frame();
    tap_key(key_at(0, 0));
    fm1_in.notes |= 1u << key_at(1, GK_ACC);
    leds = grid_leds();
    ok = t->step[0].hit == 1u << DV_SNARE && t->step[0].acc == 1u << DV_SNARE && (leds >> key_at(0, 0) & 1u) &&
         (leds >> key_at(1, GK_ACC) & 1u);
    tap_key(key_at(0, 0));
    fm1_in.notes |= 1u << key_at(1, GK_ACC);
    leds = grid_leds();
    ok &= t->step[0].hit == 1u << DV_SNARE && !t->step[0].acc && !(leds >> key_at(0, 0) & 1u);
    fm1_in.notes = 0;
    frame();
    leds = grid_leds();
    ok &= (leds >> key_at(0, 0) & 1u) != 0u;
    bad += check("ACC held: a tap adds the hit accented, again drops the accent only; the LEDs show accents", ok);
    /* the knobs: STEP LANE HIT ACC */
    turn(EN_K1, 2);
    turn(EN_K2, 1);
    turn(EN_K3, 1);
    ok = ui.cursor == 2u && ui.lane == 2u && t->step[2].hit == 1u << DV_CLAP;
    turn(EN_K4, 1);
    ok &= t->step[2].acc == 1u << DV_CLAP;
    turn(EN_K3, -1);
    ok &= !t->step[2].hit && !t->step[2].acc && t->step[2].time == ST_REST;
    turn(EN_K4, 1);
    ok &= t->step[2].hit == 1u << DV_CLAP && t->step[2].acc == 1u << DV_CLAP;
    bad += check("KNOB 1 STEP, 2 LANE, 3 HIT on / off, 4 ACC (an accent adds the hit)", ok);
    press(B_EDIT);
    bad += check("EDIT: the cursor step cleared, on to the next", t->step[2].time == ST_REST && !t->step[2].hit && ui.cursor == 3u &&
                 grid_on());
    /* pages: LEN 32, the page keys */
    t->p[P_SLEN] = 32;
    leds = grid_leds();
    tap_key(key_at(1, GK_PGUP));
    ok = ui.bank == 1u && ui.cursor == 19u && (leds >> key_at(1, GK_PGUP) & 1u) && (leds >> key_at(1, GK_PGDN) & 1u);
    tap_key(key_at(0, 15));
    ok &= t->step[31].hit == 1u << DV_CLAP;
    tap_key(key_at(1, GK_PGDN));
    ok &= ui.bank == 0u && ui.cursor == 15u;
    t->p[P_SLEN] = 12;
    frame();
    tap_key(key_at(0, 13));
    ok &= !t->step[13].hit && !t->step[13].n;
    bad += check("pages: black keys 11 / 10 up / down (the cursor along); past LEN a key does nothing", ok);
    t->p[P_SLEN] = 16;
    /* a step's GM notes on their lanes */
    t->step[8] = (step_t){{41, 49, 36, 0}, 3, ST_NOTE, SF_ACCENT, 96, 0, 0};
    ok = step_lanes(&t->step[8]) == ((1u << DV_TOM) | (1u << DV_BELL) | (1u << DV_KICK)) &&
         step_accents(&t->step[8]) == step_lanes(&t->step[8]);
    grid_hit(t, 8, DV_TOM, 0);
    ok &= t->step[8].n == 1 && t->step[8].note[0] == 49 && t->step[8].hit == 1u << DV_KICK && (t->step[8].flags & SF_ACCENT);
    grid_acc(t, 8, DV_KICK, 0);
    ok &= !t->step[8].acc && !(t->step[8].flags & SF_ACCENT) && t->step[8].n == 1 && (step_lanes(&t->step[8]) >> DV_BELL & 1u);
    grid_acc(t, 8, DV_BELL, 1);
    ok &= !t->step[8].n && t->step[8].hit == ((1u << DV_KICK) | (1u << DV_BELL)) && t->step[8].acc == 1u << DV_BELL;
    bad += check("GM notes on the grid: a low tom / crash on their lanes; an edit makes the lane its hit", ok);
    /* the keys in the audio ISR: on the grid only the lane keys play */
    kb_prev = 0;
    fm1_in.notes = (1u << key_at(0, 3)) | (1u << key_at(1, 3)) | (1u << key_at(1, GK_ACC));
    keyboard_block();
    ok = kb_note[key_at(0, 3)] == KB_SILENT && kb_note[key_at(1, 3)] == 42u && kb_note[key_at(1, GK_ACC)] == KB_SILENT;
    fm1_in.notes = 0;
    keyboard_block();
    bad += check("grid keys in the ISR: white keys and ACC silent, black key 4 plays HAT CL (42)", ok);
    /* live recording on the grid: armed and playing, a lane key records its hit at the play head */
    track_defaults_steps(t);
    song.rec = 1;
    song.playing = 1;
    t->seq_idx = 5;
    t->seq_pos = 0;
    fm1_in.notes = 1u << key_at(1, 4);
    keyboard_block();
    fm1_in.notes = 0;
    keyboard_block();
    ok = t->step[5].hit == 1u << DV_HATO && !t->step[5].n && t->rskip_n == 0u;
    t->seq_pos = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), 5) - 10u;   /* late: rounds to step 7 */
    fm1_in.notes = 1u << key_at(1, 0);
    keyboard_block();
    ok &= t->step[5].hit == (1u << DV_HATO) && t->step[6].hit == (1u << DV_KICK) && t->rskip_n == 1u && t->rskip_idx == 6;
    rec_hold(t, 7, 16);
    fm1_in.notes = 0;
    keyboard_block();
    ok &= t->step[7].time == ST_REST && !t->rh_n;
    bad += check("REC on the grid: lane keys round to the nearest step, no TIE holds", ok);
    /* live recording elsewhere (HOME): the GM keys; a lane's note a hit, another GM drum a note */
    go_home();
    frame();
    t->seq_idx = 9;
    t->seq_pos = 0;
    fm1_in.notes = (1u << 7) | (1u << 13) | (1u << 6);   /* C 36 KICK, F# 42 HAT CL, E 35 (a kick 2 st down) */
    keyboard_block();
    fm1_in.notes = 0;
    keyboard_block();
    ok = !song.grid && t->step[9].hit == ((1u << DV_KICK) | (1u << DV_HATC)) && t->step[9].n == 1 && t->step[9].note[0] == 35 &&
         step_lanes(&t->step[9]) == ((1u << DV_KICK) | (1u << DV_HATC));
    song.rec = 0;
    song.playing = 0;
    bad += check("REC on HOME: GM keys quantised into the grid (35 a note on the KICK lane)", ok);
    /* a 16-step BEAT as GM notes into the DRUM track: the grid (step_to_grid) */
    demo_pat16(t, DEMO_BEAT);
    ok = t->p[P_SLEN] == 16 && lane_steps(t, DV_KICK) == ((1u << 0) | (1u << 6) | (1u << 8) | (1u << 11)) &&
         lane_steps(t, DV_SNARE) == ((1u << 4) | (1u << 12)) && lane_steps(t, DV_HATO) == 1u << 14 &&
         lane_steps(t, DV_HATC) == 0xA6AEu && t->step[0].acc == 1u << DV_KICK && t->step[4].acc == 1u << DV_SNARE &&
         t->step[1].acc == 0u && t->step[16].time == ST_REST;
    for (i = 0; i < 16u; i++)
        ok &= t->step[i].n == 0u && !(t->step[i].flags & SF_ACCENT);
    bad += check("BEAT's GM notes into a DRUM track: the grid (hits, the accents on 1 5 9 13)", ok);
    open_family(FAM_SEQ);
    ui.lane = DV_KICK;
    cursor_set(0);
    leds = grid_leds();
    ok = 1;
    for (i = 0; i < 16u; i++)
        ok &= (leds >> key_at(0, i) & 1u) == ((0x0941u >> i) & 1u);
    bad += check("..its KICK lane on the white key LEDs (1 7 9 12)", ok);
    {   /* the sound changes, the grid stays and plays as GM notes on another engine */
        step_t keep[NSTEP];
        memcpy(keep, t->step, sizeof keep);
        go_home();
        turn(EN_PRESET, 1);
        set_engine_of(t, 0);
        t->engine = t->eng_req;
        ok = !memcmp(keep, t->step, sizeof keep) && !grid_on();
        t->seq_hold = 0;
        t->seq_n = 0;
        seq_step(t, &t->step[0], div_samples((uint32_t)t->p[P_SDIV]), 0);
        ok &= t->seq_n == 1u && t->seq_notes[0] == 36u;
        seq_release(t);
        bad += check("a sound load keeps the grid; on ANALOG its hits play as their GM notes", ok);
        hold(B_SAVE);                             /* undo the sound loads: DRUM again */
        set_engine_of(t, ENGI_DRUM);
        t->engine = t->eng_req;
    }
    /* the menu: the keys play again */
    go_page(GR_ROLL);
    frame();
    k = song.grid;
    hold(B_HOME);
    frame();
    bad += check("HOME held (the menu): the keys are no longer the grid's", k == 1u && ui.menu && !song.grid);
    menu_close();
    return bad;
}

static int test_screen(void)
{
    int bad = 0;
    ui_power_on();
    ui_say("STOP TO SAVE", "");
    ui.force = 1;
    ui_draw();
    bad += check("a message keeps the header's transport and BPM (drawn right of x 80)", ui.msg_t != 0);
    ui.uboot = 3;
    ui.menu = 1;
    ui.force = 1;
    ui_draw();
    bad += check("UPDATE MODE countdown drawn over the menu", ui.force == 0);
    ui.uboot = 0;
    ui.menu = 0;
    return bad;
}

/* the sound browser (src/browse.c): a category per factory preset, LIST's ALL FAV RECENT and categories, the slots'
 * categories from their names, knob acceleration (as Felucca 1.4), the pending place of a fast turn (loaded when the
 * knob rests, a key is played or the track changes), RECENT, and undo back to the sound before browsing */
static uint32_t find_preset(uint32_t e, const char *name)
{
    uint32_t k;
    for (k = 0; k < ENGINES[e]->npresets; k++)
        if (str_eq(ENGINES[e]->presets[k].name, name))
            return k;
    return 0xFFFFu;
}
static void spin(uint32_t role, int32_t dir, uint32_t n)   /* a fast turn: a detent a frame (16 ms) */
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        host_enc[panel.enc[role]] += dir * panel.dir[role];
        frame();
    }
}
static void rest(void)                                     /* the knob left alone for a while */
{
    uint32_t i;
    for (i = 0; i < 10u; i++)
        frame();
}
static int test_browser(void)
{
    int bad = 0, ok = 1;
    uint32_t i, e, k, c, total, all, sum, src, first, want, f1, f2, f3;
    track_t before;
    ui_power_on();
    PREF_BITS &= (uint8_t)~PREF_ACCEL_OFF;
    list_set(LM_ALL);
    for (e = 0; e < NENGINES; e++)
        if (PRESET_CAT[e] && PRESET_CAT[e][0]) {
            ok &= str_len(PRESET_CAT[e]) == ENGINES[e]->npresets;
            for (k = 0; k < ENGINES[e]->npresets && PRESET_CAT[e][k]; k++)
                ok &= (c = preset_cat(e, k)) >= 1u && c < CAT_N && CAT_LETTER[c] == PRESET_CAT[e][k];
        }
    for (i = 0; i < NENG_SHOWN; i++)
        ok &= (PRESET_CAT[eng_vis(i)] && PRESET_CAT[eng_vis(i)][0]) || eng_vis(i) == ENGI_DRUM || eng_vis(i) == 11u;
    bad += check("BROWSE: a category letter for each factory preset of every engine shown", ok);
    bad += check("BROWSE: factory categories (Prophet, FM6, CZ-1, the kits)",
                 preset_cat(ENGI_PROPHET, find_preset(ENGI_PROPHET, "Fat Poly Bass")) == CAT_BASS &&
                 preset_cat(ENGI_PROPHET, find_preset(ENGI_PROPHET, "Pluckity Duck")) == CAT_PLUCK &&
                 preset_cat(ENGI_PROPHET, find_preset(ENGI_PROPHET, "Vintage Wurly")) == CAT_KEYS &&
                 preset_cat(ENGI_PROPHET, find_preset(ENGI_PROPHET, "Choral Voices")) == CAT_PAD &&
                 preset_cat(ENGI_FM6, find_preset(ENGI_FM6, "STEEL DRUM")) == CAT_BELL &&
                 preset_cat(ENGI_CZ, find_preset(ENGI_CZ, "METALLIC")) == CAT_BELL &&
                 preset_cat(ENGI_CZ, find_preset(ENGI_CZ, "SAXOPHONE")) == CAT_WIND &&
                 preset_cat(ENGI_DRUM, find_preset(ENGI_DRUM, "909 KIT")) == CAT_DRUM);
    bad += check("BROWSE: a slot's category from its name: a factory sound's of its engine, else its words",
                 cat_guess(ENGI_PROPHET, "Fat Poly Bas") == CAT_BASS &&
                 cat_guess(ENGI_PROPHET, "PICKLE PINCHER") == preset_cat(ENGI_PROPHET, find_preset(ENGI_PROPHET, "Pickle Pincher")) &&
                 cat_guess(ENGI_FM6, "E.PIANO 1") == CAT_KEYS && cat_guess(ENGI_FM6, "SYN-BASS 3") == CAT_BASS &&
                 cat_guess(ENGI_FM6, "PLUCKITY") == CAT_PLUCK && cat_guess(ENGI_FM6, "EPIC LEAD") == CAT_LEAD &&
                 cat_guess(ENGI_FM6, "TUBULAR BEL") == CAT_BELL && cat_guess(ENGI_CZ, "STRINGS 9") == CAT_STRING &&
                 cat_guess(ENGI_DRUM, "ANYTHING") == CAT_DRUM && cat_guess(ENGI_FM6, "MY SOUND") == CAT_OTHER);
    preset_all_pos(&all);
    for (i = 0, sum = 0; i < NELEM(CAT_ORDER); i++) {
        list_set(LM_CAT + i);
        preset_pos(&total);
        sum += total;
        ok = total > 0u;
    }
    bad += check("BROWSE: every sound is in exactly one category (their lists add up to ALL)", sum == all && ok);
    up_store(5, "SUB BASS 2");
    list_set(LM_CAT);                                      /* BASS */
    preset_pos(&total);
    for (i = 0, ok = 0; i < total; i++)
        ok |= preset_at(i, &k) == USER_GENERAL && k == 5u;
    bad += check("BROWSE: a user preset joins the category of its name", ok);
    up_put(5, 0);

    go_page(GR_BROWSE);
    list_set(LM_ALL);
    for (i = 0; i < LM_N + 2u; i++)
        turn(EN_K1, 1);
    bad += check("LIST (KNOB 1): ALL FAV RECENT, the categories, stopping at OTHER", list_mode() == LM_N - 1u &&
                 str_eq(list_name(list_mode()), "OTHER"));
    for (i = 0; i < LM_N; i++)
        turn(EN_K1, -1);
    turn(EN_K1, 1);
    turn(EN_K1, 1);
    turn(EN_K1, 1);
    bad += check("LIST: a category is kept with the settings in its byte, FAV stays favorites.filter",
                 list_mode() == LM_CAT && list_lcat == CAT_BASS && !favorites.filter && !list_recent);
    turn(EN_K2, 1);
    cur_entry(&src, &k);
    bad += check("LIST BASS: KNOB 2 loads the next bass", entry_cat(src, k) == CAT_BASS);

    PREF_BITS &= (uint8_t)~PREF_ACCEL_OFF;
    memset(ui.enc_t, 0, sizeof ui.enc_t);
    fm1_ms = 100000u;
    ok = accel_by(EN_K1, 1, 8u, &f1) == 1 && !f1;
    fm1_ms += 10u; ok &= accel_by(EN_K1, 1, 8u, &f2) == 1 && f2;
    fm1_ms += 10u; ok &= accel_by(EN_K1, 1, 8u, 0) == 1;
    fm1_ms += 10u; ok &= accel_by(EN_K1, 1, 8u, 0) == 8;
    fm1_ms += 20u; ok &= accel_by(EN_K1, 1, 8u, 0) == 5;
    fm1_ms += 30u; ok &= accel_by(EN_K1, 1, 8u, 0) == 3;
    fm1_ms += 50u; ok &= accel_by(EN_K1, 1, 8u, 0) == 1;
    fm1_ms += 6u; ok &= accel_by(EN_K1, 3, 8u, 0) == 24;
    fm1_ms += 6u; ok &= accel_by(EN_K1, 1, 3u, 0) == 3;
    fm1_ms += 6u; ok &= accel_by(EN_K1, 1, 16u, 0) == 16;
    fm1_ms += 20u; ok &= accel_by(EN_K1, 1, 16u, 0) == 10;
    fm1_ms += 6u; ok &= accel_by(EN_K1, -1, 8u, &f3) == -1 && !f3;
    fm1_ms += 300u; ok &= accel_by(EN_K1, -1, 8u, &f1) == -1 && !f1;
    bad += check("ACCEL: a step a detent until the 4th quick one, then x3 under 45 ms, x5 under 25, x8 under 12 (capped, "
                 "doubled over 8); a reversal and a pause start over", ok);
    ok = 1;
    for (i = 0; i < 3u; i++) {
        fm1_ms += 30u; ok &= accel_by(EN_K3, 1, 8u, 0) == 1;
    }
    fm1_ms += 300u; ok &= accel_by(EN_K3, 1, 8u, 0) == 1;
    fm1_ms += 30u; ok &= accel_by(EN_K3, 1, 8u, 0) == 1;
    bad += check("ACCEL: three quick clicks, a pause, another: one step each", ok);
    ok = accel(EN_K2, 5, 20) == 5 && desc_range(&(param_desc_t){"X", F_ENUM, 0, 99, 0, 0, 0}) == 0;
    PREF_BITS |= PREF_ACCEL_OFF;
    fm1_ms += 6u; ok &= accel_by(EN_K1, -1, 8u, 0) == -1;
    fm1_ms += 6u; ok &= accel_by(EN_K1, -1, 8u, &f1) == -1 && !f1;
    PREF_BITS &= (uint8_t)~PREF_ACCEL_OFF;
    bad += check("ACCEL: narrow values and lists of names keep a step a detent; MENU KNOB ACCEL OFF: always", ok);

    ui_power_on();                                       /* the browser's keys: OCT- back, OCT+ keep, both to Stage */
    PREF_BITS |= PREF_ACCEL_OFF;
    list_set(LM_ALL);
    set_engine_of(TSEL, ENGI_PROPHET);
    apply_preset_to(TSEL, 3);
    fm1_ms += 1000u;
    go_page(GR_BROWSE);
    first = list_cur(&total);
    press(B_OCTDN);
    bad += check("BROWSER: OCT- without a load goes to Stage, the sound as it was", ui.home && list_cur(&total) == first);
    go_page(GR_BROWSE);
    turn(EN_K2, 1);
    turn(EN_K2, 1);
    ok = list_cur(&total) == first + 2u;
    press(B_OCTDN);
    bad += check("BROWSER: KNOB 2 browses; OCT- goes back to the sound from before it, to Stage",
                 ok && ui.home && list_cur(&total) == first);
    go_page(GR_BROWSE);
    turn(EN_K2, 1);
    press(B_OCTUP);
    bad += check("BROWSER: OCT+ keeps the sound, to Stage", ui.home && list_cur(&total) == first + 1u);
    PREF_BITS &= (uint8_t)~PREF_ACCEL_OFF;

    ui_power_on();
    PREF_BITS &= (uint8_t)~PREF_ACCEL_OFF;
    list_set(LM_ALL);
    recent_n = 0;
    set_engine_of(TSEL, ENGI_PROPHET);
    my_steps(TSEL);
    ui.home = 1;
    fm1_ms += 1000u;
    rest();
    before = *TSEL;
    first = list_cur(&total);
    spin(EN_PRESET, 1, 12);
    want = first + 3u + 9u * 10u;                          /* three single steps, then 16 ms a detent: x5, doubled */
    bad += check("BROWSE: a fast turn loads its first detent only, the list moves on (x10 at 16 ms a detent)",
                 browse_pending() && preset_pos(&total) == want && list_cur(&total) == first + 1u &&
                 !memcmp(TSEL->step, before.step, sizeof before.step));
    e = browse_shown(&k);
    bad += check("BROWSE: HOME's footer and the PRESETS page show the pending sound",
                 e == preset_at(want, &c) && k == c);
    rest();
    bad += check("BROWSE: it loads when the knob rests (120 ms)", !brw.on && list_cur(&total) == want &&
                 !memcmp(TSEL->step, before.step, sizeof before.step));
    undo_swap();
    bad += check("BROWSE: SAVE held (undo) brings back the sound from before browsing",
                 TSEL->eng_req == before.eng_req && TSEL->preset == before.preset && list_cur(&total) == first);
    undo_swap();
    spin(EN_PRESET, 1, 6);
    want = preset_pos(&total);
    fm1_in.notes |= 1u << 3; host_notes |= 1u << 3; frame();
    fm1_in.notes &= ~(1u << 3); frame();
    bad += check("BROWSE: a key played while browsing loads the pending sound at once", !brw.on && list_cur(&total) == want);
    rest();
    spin(EN_PRESET, -1, 6);
    want = preset_pos(&total);
    turn(EN_ALGO, 1);
    bad += check("BROWSE: changing the track loads the pending sound into the track it was meant for",
                 song.sel == 1u && !brw.on && (song.sel = 0, list_cur(&total) == want));
    rest();
    list_set(LM_RECENT);
    preset_pos(&total);
    cur_entry(&src, &k);
    bad += check("RECENT: the sounds browsed, newest first", total >= 3u && preset_at(0, &c) == src && c == k);
    e = recent[0];
    turn(EN_PRESET, 1);
    bad += check("RECENT: browsing it loads the next one and keeps the order", recent[0] == e && preset_pos(&total) == 1u);
    list_set(LM_ALL);
    return bad;
}

static int test_favorites(void)
{
    int bad = 0;
    uint32_t total, pos;
    track_t before;
    ui_power_on();
    set_engine_of(TSEL,ENGI_PROPHET);
    my_steps(TSEL);
    TSEL->p[P_AMODE] = 2;
    go_page(GR_BROWSE);
    before = *TSEL;
    turn(EN_K3, 1);
    turn(EN_K1, 1);
    pos = preset_pos(&total);
    bad += check("FAV and LIST mark/filter the current sound without loading or touching its steps",
                 preset_favorite() && favorites.filter && total == 1 && pos == 0 &&
                 !memcmp(TSEL, &before, sizeof before));
    bad += check("one favorite occupies one display row, without repeated copies",
                 preset_visible(pos, total, 0) == 0 && preset_visible(pos, total, 1) == total);
    select_engine(ENGI_DRUM);
    before = *TSEL;
    turn(EN_K3, 1);
    bad += check("a DRUM sound can be a favorite on any track", preset_favorite() &&
                 favorite_has(ENGI_DRUM, 0) && !memcmp(TSEL, &before, sizeof before));
    turn(EN_PRESET, 1);
    bad += check("filtered browsing crosses DRUM and synth sounds while retaining the track's pattern and ARP",
                 TSEL->eng_req == ENGI_PROPHET && preset_favorite() && !memcmp(TSEL->step, before.step, sizeof before.step) &&
                 TSEL->p[P_AMODE] == 2);
    turn(EN_K3, -1);
    pos = preset_pos(&total);
    bad += check("unmarking the current sound leaves it loaded even when outside the filtered list",
                 !preset_favorite() && total == 1 && pos == total && TSEL->eng_req == ENGI_PROPHET);
    turn(EN_PRESET, -1);
    bad += check("browsing from a nonfavorite selects the last favorite", TSEL->eng_req == ENGI_DRUM);
    turn(EN_K3, -1);
    before = *TSEL;
    turn(EN_PRESET, 1);
    bad += check("empty favorites explains LIST ALL and retains the current sound and steps",
                 msg_is("NO FAVORITES") && !memcmp(TSEL, &before, sizeof before));
    turn(EN_K1, -1);
    preset_pos(&total);
    bad += check("LIST ALL restores the full browser without an implicit load", !favorites.filter && total > 1 &&
                 !memcmp(TSEL, &before, sizeof before));
    up_store(31, "Favorite");
    up_load(31);
    turn(EN_K3, 1);
    bad += check("user slot 32 has its own favorite reference", favorite_has(USER_GENERAL, 31) && preset_favorite());
    up_store(31, "Renamed");
    bad += check("overwriting or renaming a user slot retains its star", favorite_has(USER_GENERAL, 31));
    up_put(31, 0);
    bad += check("successful erasure removes the user slot star and source label",
                 !favorite_has(USER_GENERAL, 31) && !up_used(31) && !TSEL->user);
    up_store(31, "New sound");
    bad += check("reusing the erased slot does not restore its old star", !favorite_has(USER_GENERAL, 31));
    return bad;
}

static int test_display_preferences(void)
{
    int bad = 0;
    track_t sounds[NTRK];
    uint16_t before[240 * 240];
    ui_power_on();
    memcpy(sounds, trk, sizeof sounds);
    hold(B_HOME);
    ui.menu_sel = MI_COLOR;
    ui.force = 1; ui_draw();
    memcpy(before, host_screen, sizeof before);
    settings.palette = 0; palette_set(0);
    press(B_OCTUP);
    bad += check("COLOR OCT+ previews the next palette (DAY, light) without closing the menu",
                 settings.palette == 1 && T_BG == UI_PALETTES[1].bg && ux.light && ui.menu == 1 && !song.octave &&
                 memcmp(before, host_screen, sizeof before) && !memcmp(sounds, trk, sizeof sounds));
    turn(EN_K1, 1);
    bad += check("COLOR KNOB 1 steps on to CONTRAST, then wraps to NIGHT",
                 settings.palette == 2 && (turn(EN_K1, 1), settings.palette == UI_DEFAULT_INDEX));
    settings.lowcut = 2;
    ui.menu_sel = MI_LOWCUT;
    press(B_OCTUP);
    bad += check("the expanded menu retains all three SPEAKER modes", settings.lowcut == 0 && !fx_lowcut);
    press(B_OCTDN);
    bad += check("OCT- leaves display preferences without changing musical state",
                 !ui.menu && settings.palette == UI_DEFAULT_INDEX && !memcmp(sounds, trk, sizeof sounds));
    return bad;
}

static int test_information(void)
{
    uint16_t header[240 * H_HEAD], battery[240 * H_HEAD];
    track_t sounds[NTRK];
    int bad = 0, quiet = 1;
    uint32_t x, y;
    ui_power_on();
    memcpy(sounds, trk, sizeof sounds);
    hold(B_HOME);
    ui.menu_sel = MI_ABOUT;
    press(B_OCTUP);
    ui_draw();
    bad += check("ABOUT opens one document with scrollable license text and credits", ui.menu == 2 && !ui.menu_scroll && menu_scroll_max() > MENU_DOC_H);
    for (y = 28; y < 102; y++)                          /* (the QR at the top right, by the name) */
        for (x = 158; x < 232; x++)
            if (x < 166 || x >= 224 || y < 36 || y >= 94)
                quiet &= host_screen[y * 240 + x] == swap16(UI_QR_LIGHT);
    bad += check("ABOUT QR keeps its complete white quiet zone", quiet);
    memcpy(header, host_screen, sizeof header);
    turn(EN_PRESET, 3);
    ui_draw();
    bad += check("PRESETS scrolls ABOUT with its header fixed",
                 ui.menu == 2 && ui.menu_scroll > 0 && !memcmp(header, host_screen, sizeof header));
    turn(EN_PRESET, 12);
    ui_draw();
    bad += check("PRESETS continues from ABOUT into CREDITS without changing pages",
                 ui.menu == 2 && ui.menu_scroll > 192 && !memcmp(header, host_screen, sizeof header));
    uint16_t position = ui.menu_scroll;
    press(B_OCTUP);
    bad += check("OCT+ does not switch pages or alter the octave in the document",
                 ui.menu == 2 && ui.menu_scroll == position && song.octave == 0);
    turn(EN_PRESET, 10000);
    turn(EN_PRESET, 10000);
    ui_draw();
    bad += check("the continuous document stops at its final credit",
                 ui.menu == 2 && ui.menu_scroll == menu_scroll_max());
    turn(EN_PRESET, -10000);
    bad += check("scrolling back through CREDITS returns to ABOUT's top", ui.menu == 2 && !ui.menu_scroll);
    press(B_OCTDN);
    bad += check("OCT- returns to the menu without changing sounds or patterns",
                 ui.menu == 1 && !memcmp(sounds, trk, sizeof sounds));
    press(B_OCTUP);
    bad += check("reopening ABOUT starts the continuous document at the top", ui.menu == 2 && !ui.menu_scroll);
    menu_close();
    ui.msg_t = 0;
    usb.config = 1; usb.suspended = 0;
    song.batt_raw = 530;
    ui.force = 1; draw_head(); ui.force = 0;
    memcpy(battery, host_screen, sizeof battery);
    uint32_t signature = ui.head_sig;
    for (x = 0; x < 4; x++) { fm1_ms += 600; draw_head(); }
    bad += check("USB power shows a steady bolt inside the battery without animation",
                 batt_shown() == 4 && ui.head_sig == signature && !memcmp(battery, host_screen, sizeof battery) &&
                 host_screen[6 * 240 + 217] != 0 && host_screen[9 * 240 + 225] != 0);
    usb.suspended = 1;
    draw_head();
    bad += check("USB suspension restores the battery outline and measured level",
                 batt_shown() == 0 && ui.head_sig != signature && host_screen[6 * 240 + 217] != 0);
    usb.config = 0; usb.suspended = 0;
    song.batt_raw = 531; int levels = batt_shown() == 1;
    song.batt_raw = 561; levels &= batt_shown() == 2;
    song.batt_raw = 591; levels &= batt_shown() == 3;
    bad += check("battery operation retains all three ADC level thresholds", levels);
    return bad;
}

/* MONO is grayscale on every screen: every page of every engine, HOME, the menu, ABOUT, the dialogs */
static int screen_gray(void)
{
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        if ((c >> 11) != (c & 31u) || ((c >> 5) & 63u) != (c >> 11) * 2u)
            return 0;
    }
    return 1;
}
static int test_mono_screens(void)
{
    int bad = 0, ok = 1;
    uint32_t e, i, n = 0;
    for (e = 0; e < NENGINES; e++) {
        if (!eng_ok(e))
            continue;                               /* (DIGITAL without MELODEE_FM4: never a track's) */
        for (i = 0; i < NPAGES; i++) {
            ui_power_on(); set_engine_of(TSEL, e); song.playing = 1; song.rec = 1;
            ui.home = 0; ui.page = (uint8_t)i; page_entered();
            if (!page_visible(i)) continue;
            screen_clear();
            ui.force = 1; ui_draw();
            ok &= screen_gray();
            n++;
        }
        ui_power_on(); set_engine_of(TSEL, e); go_home();
        ui.force = 1; ui_draw(); ok &= screen_gray();
    }
    for (i = 0; i < 4u; i++) {
        ui_power_on(); song.rec = 3;
        if (i < 2u) { ui.menu = (uint8_t)(i + 1u); ui.menu_scroll = 300; }
        else if (i == 2u) ui.confirm = CF_OVR_PROJ;
        else ui.uboot = 2;
        ui.force = 1; ui_draw(); ok &= screen_gray();
    }
    bad += check("MONO: every pixel of every page, HOME, menu, ABOUT, dialog and countdown is gray", ok && n > 200u);
    for (e = 0; e < NPALETTES; e++) {               /* the other palettes are not gray: the check sees colour */
        ui_power_on(); palette_set(e); ui.force = 1; ui_draw();
        ok = screen_gray();
        if (e == UI_GRAY_INDEX ? !ok : ok) bad += check("palette colour shows on HOME", 0);
    }
    palette_set(UI_GRAY_INDEX);
    return bad;
}

/* ROLL: the rolling digits of the four cards and the header BPM (ui_draw.c roll_*). The GLOBAL page: BPM (also
 * in the header), SWG, CLK (a name), TUNE (signed). A roll frame differs from the static render only inside the
 * value strip, its last frame is the static render; unchanged digits stay; the direction follows the sign; a
 * change mid-roll retargets; a fast turn, another shape, a page / track / palette change and ui.force snap. */
static uint16_t roll_shot[240 * 240];
static void roll_settle_on(const char *page)       /* that page, nothing rolling, no knob hot */
{
    uint32_t k;
    go_title(page);
    ui.hot_t = 0; ui.bpm_t = 0;
    for (k = 0; k < 12u; k++) frame();
}
static void roll_settle(void) { roll_settle_on("GLOBAL"); }   /* (TUNE on KNOB 4) */
/* host_screen against roll_shot: the pixels that differ inside the box (in = 1) or outside it */
static uint32_t roll_diff(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int in)
{
    uint32_t n = 0;
    int32_t x, y;
    for (y = 0; y < 240; y++)
        for (x = 0; x < 240; x++) {
            int inside = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (inside == in && host_screen[y * 240 + x] != roll_shot[y * 240 + x]) n++;
        }
    return n;
}
/* the mean row (x16) of the ink (not the card surface) in columns x0..x1-1, rows y0..y1-1 of a screen */
static int32_t roll_ink_row(const uint16_t *s, int32_t x0, int32_t x1, int32_t y0, int32_t y1)
{
    int32_t x, y, n = 0, sum = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            if (s[y * 240 + x] != swap16(T_BG) && s[y * 240 + x] != swap16(T_LIFT)) {
                n++;                                      /* (a ring's ground, plain or lifted while its knob turns) */
                sum += y * 16;
            }
    return n ? sum / n : -1;
}
static int roll_static_now(void)                  /* the screen is the static render (a forced redraw changes nothing) */
{
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1;
    ui_draw();
    return roll_diff(0, 0, 0, 0, 0) == 0;
}
/* the pages' knobs are rings (ui_pages.c): column c's cell, its value's row, where its value starts (centred) */
#define RX(c) (1 + 60 * (int32_t)(c))
#define VY (PV_RING_Y + 55)
#define STRIP(c) RX(c), VY, RX(c) + 58, VY + ROLL_H
static int32_t ring_vx(uint32_t c)
{
    int16_t *vp;
    char v[16];
    const char *u;
    const param_desc_t *d = page_desc(cur_page(), c, &vp);
    int32_t uw;
    param_format(d, *vp, v, &u);
    if (str_eq(u, d->label)) u = "";
    uw = u[0] ? text_w(&AF_X, u) + 2 : 0;
    return RX(c) + 29 - (text_w(&AF_M, v) + uw) / 2;
}
static int test_roll(void)
{
    int bad = 0, ok;
    uint32_t k, n, e, id;
    int32_t up, down, st;
    static const struct { const char *a, *b; int d; } DIRS[] = {
        {"120", "121", 1}, {"121", "120", -1}, {"129", "130", 1}, {"-12", "-13", -1}, {"-13", "-12", 1}, {"+5", "+6", 1},
        {"1.25", "1.30", 1}, {"0.9", "0.8", -1}, {"9", "10", 0}, {"0", "-1", 0}, {"+5", "-5", 0}, {"C3", "C4", 0},
        {"1/4", "1/8", 0}, {"OFF", "ON", 0}, {"2X", "4X", 0}, {"120", "120", 0}, {"123456", "123457", 1},
        {"1234567", "1234568", 0}, {"12%", "13%", 0}};

    ok = 1;
    for (k = 0; k < sizeof DIRS / sizeof DIRS[0]; k++)
        if (roll_dir(DIRS[k].a, DIRS[k].b) != DIRS[k].d) { printf("  roll_dir %s -> %s\n", DIRS[k].a, DIRS[k].b); ok = 0; }
    bad += check("roll: numbers of one shape roll by the sign of the change; names, length, sign changes snap", ok);
    ok = 1;                                         /* every page of every engine: names never roll, numbers do */
    n = 0;
    for (e = 0; e < NENGINES; e++)
        for (id = 0; id < NPAGES && eng_ok(e); id++) {
            uint32_t c;
            ui_power_on(); set_engine_of(TSEL, e);
            ui.home = 0; ui.page = (uint8_t)id; page_entered();
            if (!page_visible(id) || PAGES[id].graph == GR_MOD) continue;
            ui.force = 1; ui_draw();
            for (c = 0; c < 4u; c++) {
                int16_t *vp;
                const param_desc_t *d = PAGES[id].scope == SC_GLOBAL || PAGES[id].scope == SC_TRACK ||
                                        PAGES[id].scope == SC_ENGINE ? page_desc(cur_page(), c, &vp) : 0;
                int32_t v, v0;
                if (!d || !d->label || d->label[0] == '-' || ((act_cols() >> c) & 1u)) continue;
                v0 = *vp;
                for (v = d->min; v < d->max && v - d->min < 40; v++) {
                    *vp = (int16_t)v; ui.frame += ROLL_SNAP; draw_columns();
                    *vp = (int16_t)(v + 1); ui.frame += ROLL_SNAP; draw_columns();
                    if (ui.roll[c].from[0] && (d->fmt == F_ENUM || d->fmt == F_NOTE || d->fmt == F_ONOFF)) {
                        printf("  %s/%s %s: %s rolls\n", ENGINES[e]->name, PAGES[id].title, d->label, ui.roll[c].from);
                        ok = 0;
                    }
                    n += ui.roll[c].from[0] != 0;
                }
                *vp = (int16_t)v0;
            }
        }
    ok &= n > 500u;
    bad += check("roll: every page and engine: a name (enum, even ALG 1..8, note, ON/OFF) snaps, numbers roll", ok);

    /* TUNE 0 -> 1 (card 4): a roll of ROLL_FRAMES frames, the last one the static render */
    ui_power_on(); roll_settle();
    turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 1 && str_eq(ui.roll[3].from, "0") && ui.roll[3].dir == 1;
    for (n = 1; ui.roll[3].from[0] && n < 20u; n++) frame();
    ok &= n == ROLL_FRAMES;
    bad += check("roll: a card's value rolls for 9 frames (TUNE 0 -> 1), the value itself at once", ok);
    bad += check("roll: the roll's last frame is pixel-identical to the static render", roll_static_now());

    /* mid-roll: only the value strip differs from the static render */
    ui_power_on(); roll_settle();
    turn(EN_K4, 1); frame(); frame(); frame();
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1; ui_draw();                       /* (snaps: the static render) */
    ok = roll_diff(STRIP(3), 0) == 0 && roll_diff(STRIP(3), 1) > 20u && !ui.roll[3].from[0];
    bad += check("roll: a mid-roll frame differs from the static render inside the value strip only", ok);

    /* BPM 120 -> 121 (SEQ > TEMPO KNOB 1): the header rolls, the digits 1 and 2 stay; the BPM card too (no unit:
     * the label says BPM: set in M, it rolls like any number) */
    ui_power_on(); roll_settle_on("TEMPO");
    turn(EN_K1, 1);
    ok = song.g[G_BPM] == 121 && str_eq(ui.roll[ROLL_BPM].from, "120") && ui.roll[ROLL_BPM].dir == 1 &&
         str_eq(ui.roll[0].from, "120") && ui.roll[0].dir == 1;
    frame(); frame();
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1; ui_draw();
    n = roll_diff(BPM_X, 0, BPM_X + BPM_W, H_HEAD, 1);
    ok &= n > 20u && roll_diff(BPM_X, 0, BPM_X + BPM_W, H_HEAD, 0) == roll_diff(STRIP(0), 1);   /* (+ the card's strip) */
    ok &= roll_diff(BPM_X, 0, BPM_X + text_w(&AF_S, "12"), H_HEAD, 1) == 0;
    ok &= roll_diff(ring_vx(0), VY, ring_vx(0) + text_w(&AF_M, "12"), VY + ROLL_H, 1) == 0;
    /* TUNE 10 -> 11 on a card: the 1 stays */
    ui_power_on(); song.g[G_TUNE] = 10; roll_settle();
    turn(EN_K4, 1); frame(); frame();
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1; ui_draw();
    ok &= song.g[G_TUNE] == 11 && roll_diff(STRIP(3), 1) > 20u && roll_diff(STRIP(3), 0) == 0;
    ok &= roll_diff(ring_vx(3), VY, ring_vx(3) + text_w(&AF_M, "1"), VY + ROLL_H, 1) == 0;
    bad += check("roll: header BPM 120 -> 121 and a card 10 -> 11: only the strip changes, unchanged digits stay put", ok);

    /* the direction: 5 -> 6 and -5 -> -6 at frame 4: the new digit comes from below (up) / above (down) */
    ui_power_on(); song.g[G_TUNE] = 5; roll_settle();
    st = roll_ink_row(host_screen, ring_vx(3), ring_vx(3) + 11, VY, VY + ROLL_H);
    turn(EN_K4, 1); frame(); frame(); frame();
    up = roll_ink_row(host_screen, ring_vx(3), ring_vx(3) + 11, VY, VY + ROLL_H);
    ok = ui.roll[3].dir == 1;
    ui_power_on(); song.g[G_TUNE] = 7; roll_settle();
    turn(EN_K4, -1); frame(); frame(); frame();
    down = roll_ink_row(host_screen, ring_vx(3), ring_vx(3) + 11, VY, VY + ROLL_H);
    ok &= ui.roll[3].dir == -1 && st > 0 && up > st + 16 && down < st - 16;
    ui_power_on(); song.g[G_TUNE] = -5; roll_settle();
    turn(EN_K4, -1);
    ok &= song.g[G_TUNE] == -6 && str_eq(ui.roll[3].from, "-5") && ui.roll[3].dir == -1;
    bad += check("roll: an increase rolls up, a decrease down (-5 -> -6 is down)", ok);

    /* retarget: a change mid-roll restarts from the value it was going to; a fast turn snaps */
    ui_power_on(); song.g[G_TUNE] = 1; roll_settle();
    turn(EN_K4, 1); frame(); frame(); frame();
    turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 3 && str_eq(ui.roll[3].from, "2") && (uint8_t)(ui.frame - ui.roll[3].t0) == 0;
    for (n = 1; ui.roll[3].from[0] && n < 20u; n++) frame();
    ok &= n == ROLL_FRAMES && roll_static_now();
    bad += check("roll: a change mid-roll retargets (from the last value, restarted) and ends static", ok);
    ui_power_on(); song.g[G_TUNE] = 1; roll_settle();
    turn(EN_K4, 1); turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 3 && !ui.roll[3].from[0] && roll_static_now();
    bad += check("roll: a change within 40 ms of the last one snaps (a fast turn)", ok);

    /* snaps: another shape, a name, the page, the track, the palette, ui.force */
    ui_power_on(); roll_settle();
    turn(EN_K4, -1);
    ok = song.g[G_TUNE] == -1 && !ui.roll[3].from[0] && roll_static_now();
    for (k = 0; k < 5u; k++) frame();
    turn(EN_K3, 1);
    ok &= song.g[G_CLOCK] != 0 && !ui.roll[2].from[0] && roll_static_now();
    bad += check("roll: a sign / length change (0 -> -1) and a name (CLK) snap", ok);
    ok = 1;
    for (k = 0; k < 5u; k++) {
        ui_power_on(); roll_settle();
        turn(EN_K4, 1);
        ok &= ui.roll[3].from[0] != 0;
        if (k == 0) go_title("SYSTEM");
        else if (k == 1) turn(EN_ALGO, 1);
        else if (k == 2) { settings.palette = 1; palette_set(1); }          /* (no ui.force) */
        else if (k == 3) set_engine_of(TSEL, eng_step(TSEL->eng_req, 1));   /* (no ui.force) */
        else ui.force = 1;
        frame();
        ok &= !ui.roll[3].from[0] && !ui.roll[ROLL_BPM].from[0];
    }
    ui_power_on(); roll_settle_on("TEMPO");
    turn(EN_K1, 1);
    ok &= ui.roll[ROLL_BPM].from[0] != 0;
    ui.force = 1; frame();
    ok &= !ui.roll[ROLL_BPM].from[0];
    bad += check("roll: a page, track, palette or engine change and ui.force snap", ok);
    palette_set(UI_GRAY_INDEX);

    /* MONO: every roll frame is gray */
    ui_power_on(); roll_settle();
    ok = 1;
    song.g[G_BPM] += 9;                                  /* (the header rolls too) */
    turn(EN_K4, -1);
    for (k = 0; k < ROLL_FRAMES; k++) { ok &= screen_gray(); frame(); }
    bad += check("roll: MONO roll frames are gray", ok);
    ui_power_on();
    return bad;
}

static void screen_save(const char *dir, const char *name)
{
    char path[512];
    uint32_t i;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    f = fopen(path, "wb");
    if (!f) return;
    ui.msg_t = 0; ui.hot_t = 0; ui.force = 1;
    ui_draw();
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);           /* canvas stores panel-order RGB565 */
        uint8_t b[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u), (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}
static void chain_screens(const char *dir)
{
    uint32_t k;
    ui_power_on();
    go_page(GR_SONG);
    screen_save(dir, "song-empty");
    for (k = 0; k < 2u; k++) { my_steps(&trk[0]); project_save(k); }
    chain_config.count = 3;
    chain_config.row[0] = (chain_row_t){0, 2};
    chain_config.row[1] = (chain_row_t){1, 4};
    chain_config.row[2] = (chain_row_t){0, 1};
    ui.song_row = 1;
    screen_save(dir, "song-ready");
    chain_prepare(); events_block(32);
    screen_save(dir, "song-playing");
    seq_stop();
}

/* PATTERNS and SONG (ui_patterns.c): a row's letter is its set of patterns; a pattern's bars follow LEN and DIV */
/* NOTES / the drum grid (ui_popup.c step_enter): OCT+ on an empty place puts a note / the lane's hit there, on one
 * its sheet; OCT+ held: the pattern's sheet */
static int test_step_sheets(void)
{
    int bad = 0;
    track_t *t;
    ui_power_on();
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    go_title("NOTES"); cursor_set(4); frame();
    last_note = 62;
    press(B_OCTUP);
    bad += check("NOTES: OCT+ on an empty step places the last note played, no sheet", t->step[4].n == 1u &&
                 t->step[4].note[0] == 62u && !pop.on && song.octave == 0);
    press(B_OCTUP);
    bad += check("..OCT+ on the note: its sheet (Length, Velocity, Chance, Slide, Delete note)", pop.on == POP_SHEET &&
                 pop.rows == SHEET_NOTE && !memcmp(pop.title, "Note ", 5));
    turn(EN_K1, 1);
    bad += check("..KNOB 1 on Length: 2 steps", step_note_length(t, 4) == 2u && pop.on == POP_SHEET);
    turn(EN_K2, 1); turn(EN_K2, 1); turn(EN_K1, -10);
    bad += check("..Chance: 90 %", step_chance(&t->step[4]) == 90u);
    sheet_do("Slide");
    bad += check("..Slide: on, the sheet closed", (t->step[4].flags & SF_SLIDE) && !pop.on);
    sheet_do("Delete note");
    bad += check("..Delete note: gone", !step_on(&t->step[4]));
    hold(B_OCTUP);
    bad += check("NOTES: OCT+ held: the pattern's sheet", pop.on == POP_SHEET && pop.rows == SHEET_PATTERN);
    press(B_OCTDN);
    ui_power_on();
    track_select(3); frame();                           /* T4 DRUM: the grid */
    t = TSEL;
    track_defaults_steps(t);
    go_title("NOTES"); cursor_set(2); ui.lane = 1; frame();
    press(B_OCTUP);
    bad += check("drum grid: OCT+ on an empty place sets the lane's hit", grid_on() &&
                 ((step_lanes(&seq_steps(t)[2]) >> 1) & 1u) && !pop.on);
    press(B_OCTUP);
    bad += check("..OCT+ on the hit: its sheet (Accent, Chance, Clear hit)", pop.on == POP_SHEET && pop.rows == SHEET_HIT);
    press(B_OCTUP);                                     /* (Accent: on) */
    bad += check("..Accent on", (step_accents(&seq_steps(t)[2]) >> 1) & 1u);
    sheet_do("Clear hit");
    bad += check("..Clear hit: the lane empty there", !((step_lanes(&seq_steps(t)[2]) >> 1) & 1u));
    return bad;
}

/* PATTERNS: OCT- clears what is queued first, then goes to Stage */
static int test_patterns_queue(void)
{
    int bad = 0;
    ui_power_on();
    song.playing = 1;
    go_title("PATTERNS"); frame();
    turn(EN_K1, 1);
    bad += check("PATTERNS playing: KNOB 1 queues track 1's next pattern", trk[0].pattern_next == 1u);
    press(B_OCTDN);
    bad += check("..OCT-: the queue cleared, PATTERNS stays", trk[0].pattern_next == 0xFFu && !ui.home &&
                 msg_is("QUEUE CLEARED"));
    press(B_OCTDN);
    bad += check("..OCT- again: Stage", ui.home);
    stop_transport();
    return bad;
}

/* Init sound: the engine's INIT (FM6: INIT VOICE after F24, dry); the browser's ENGINE knob loads it, its INIT first
 * in the list. SCALES: no picker over its own list. PATTERNS: OCT+ the pattern's sheet; Copy to (the first empty
 * place, any knob moves it, OCT+ copies, over a pattern in use the question first, OCT- leaves); Delete pattern */
static int test_init_scales_patterns(void)
{
    int bad = 0, ok;
    uint32_t i, b, first;
    char nm[16];
    track_t *t;
    ui_power_on();
    set_engine_of(TSEL, ENGI_FM6);
    ok = TSEL->preset == 0u;
    TSEL->p[P_E0] = 5;
    sound_init_of(TSEL, ENGI_FM6);
    fm6_name(nm, fm6_patch[song.sel]);
    for (i = 0; i < 4u; i++)
        ok &= !TSEL->p[P_DIST + i];
    bad += check("INIT: FM6's: INIT VOICE (after F24), the DX7 init voice, dry", ok && TSEL->preset == FM6_NFAC &&
                 str_eq(nm, "INIT VOICE") && !TSEL->p[P_E0]);
    sound_init_of(TSEL, ENGI_PROPHET);
    sound_name(TSEL, nm);
    bad += check("INIT: the Prophet's: INIT PROPHET (not program 1)", TSEL->eng_req == ENGI_PROPHET && !TSEL->preset &&
                 str_eq(nm, "INIT PROPHET"));
    sound_init_of(TSEL, ENGI_CZ);
    sound_name(TSEL, nm);
    bad += check("INIT: the CZ-1's: INIT TONE", TSEL->eng_req == ENGI_CZ && !TSEL->preset && str_eq(nm, "INIT TONE"));
    set_engine_of(TSEL, ENGI_FM6);
    hold(B_OCTUP);
    sheet_do("Init sound");
    press(B_OCTUP);
    bad += check("INIT: the sound's sheet: Init sound, asked, FM6's INIT VOICE", TSEL->preset == FM6_NFAC &&
                 msg_is("SOUND INIT"));
    go_page(GR_BROWSE); frame();
    list_set(LM_ALL);
    turn(EN_K4, -1);
    bad += check("INIT: the browser's ENGINE knob: the Prophet, its INIT", TSEL->eng_req == ENGI_PROPHET && !TSEL->preset);
    turn(EN_K4, 1);
    bad += check("..again: FM6, its INIT VOICE", TSEL->eng_req == ENGI_FM6 && TSEL->preset == FM6_NFAC);
    turn(EN_K2, 1);
    ok = TSEL->eng_req == ENGI_FM6 && !TSEL->preset;
    turn(EN_K2, -1);
    bad += check("..INIT VOICE first in FM6's sounds: KNOB 2 on, F1; back: INIT VOICE", ok && TSEL->eng_req == ENGI_FM6 &&
                 TSEL->preset == FM6_NFAC);
    turn(EN_K2, -1);
    bad += check("..back again: the Prophet's last program", TSEL->eng_req == ENGI_PROPHET &&
                 TSEL->preset == ENGINES[ENGI_PROPHET]->npresets - 1u);

    ui_power_on();
    go_title("SCALES"); frame();
    turn(EN_K2, 1);
    ok = pop.on != POP_PICK;
    turn(EN_K3, 1);
    bad += check("SCALES: KNOB 2 (the scale) and KNOB 3 turning: no picker over the list", ok && pop.on != POP_PICK);

    ui_power_on();
    stop_transport();
    go_title("PATTERNS"); frame();
    t = TSEL;
    my_steps(t);
    press(B_OCTUP);
    bad += check("PATTERNS: OCT+ the pattern's sheet (Copy to, Delete pattern)", pop.on == POP_SHEET &&
                 pop.rows == SHEET_PATTERNS);
    for (first = 1; first < NPAT && !pattern_empty(song.sel, first); first++)
        ;
    sheet_do("Copy to\x85");
    bad += check("..Copy to: the pattern, the first empty place", ptc_on() && ui.ptc_src == t->pattern &&
                 ui.ptc_dst == first && first < NPAT);
    turn(EN_K3, 1);
    ok = ui.ptc_dst == first + 1u && t->pattern == 0u;
    turn(EN_K1, -1);
    bad += check("..any knob moves the place (no pattern switched)", ok && ui.ptc_dst == first && t->pattern == 0u);
    hold(B_OCTUP);
    bad += check("..OCT+ held: no sheet while copying", pop.on != POP_SHEET);
    ok = 1;
    for (i = 0; i < NSTEP; i++)
        ok &= !memcmp(&pattern_at(song.sel, first)->step[i], &t->step[i], sizeof t->step[i]);
    bad += check("..OCT+ copies it there, Copy to ends", ok && !ptc_on() && msg_is("PATTERN COPIED") &&
                 !pattern_empty(song.sel, first));
    sheet_do("Copy to\x85");
    b = ui.ptc_dst;
    turn(EN_K1, -10);
    ok = b != first && ui.ptc_dst == first;
    press(B_OCTUP);
    bad += check("..over a pattern in use: the question first", ok && ui.confirm == CF_PASTE_PAT && ptc_on());
    press(B_OCTUP);
    bad += check("..yes: copied", !ui.confirm && !ptc_on() && msg_is("PATTERN COPIED"));
    sheet_do("Copy to\x85");
    press(B_OCTDN);
    bad += check("..OCT- leaves Copy to, PATTERNS stays", !ptc_on() && !ui.home && str_eq(cur_page()->title, "PATTERNS"));
    sheet_do("Delete pattern");
    bad += check("..Delete pattern asks", ui.confirm == CF_DEL_PAT && !seq_is_empty(t));
    press(B_OCTUP);
    bad += check("..yes: the pattern empty", seq_is_empty(t) && pattern_empty(song.sel, t->pattern) &&
                 msg_is("PATTERN DELETED"));
    undo_step(0);
    ok = !seq_is_empty(t);
    undo_step(1);
    bad += check("..SAVE held undoes it, SAVE + OCT+ redoes it", ok && seq_is_empty(t));
    sheet_do("Delete pattern");
    ok = !ui.confirm && msg_is("NOTHING TO DELETE");
    sheet_do("Copy to\x85");
    bad += check("..an empty pattern: nothing to delete, nothing to copy", ok && !ptc_on() && msg_is("NOTHING TO COPY"));
    return bad;
}

/* MOD as rows: KNOB 2 the route, KNOB 1 its source (its picker), KNOB 3 its destination, KNOB 4 its amount */
static int test_mod_rows(void)
{
    int bad = 0;
    ui_power_on();
    go_title("MOD"); frame();
    turn(EN_K2, 1);
    bad += check("MOD: KNOB 2 the route (2)", mod_ui_slot == 1u);
    turn(EN_K1, 1);
    bad += check("..KNOB 1 its source (LFO), its picker under KNOB 1", TSEL->p[P_M2SRC] == MS_LFO && pop.on == POP_PICK &&
                 pop.col == 0u && !TSEL->p[P_M1SRC]);
    turn(EN_K3, 2);
    bad += check("..KNOB 3 its destination", TSEL->p[P_M2DST] == 2 && !TSEL->p[P_M1DST]);
    turn(EN_K4, 5);
    bad += check("..KNOB 4 its amount", TSEL->p[P_M2AMT] > 0 && !TSEL->p[P_M1AMT]);
    return bad;
}

/* ENV / LFO on the engines with envelopes and LFOs of their own (ui.c native_titles): their pages, the track's ADSR
 * and ENV DEST hidden; LFO goes on to the track LFO. OCT- on a sound page: Stage; on SCALES: SCL */
static int test_native_env_lfo(void)
{
    int bad = 0, o;
    uint32_t i, found = 0;
    ui_power_on();
    set_engine_of(TSEL, ENGI_CZ);
    open_family(FAM_ENV);
    bad += check("ENV on CZ-1: Line 1's pitch envelope, ENV lit", str_eq(cur_page()->title, "C1 PIT R1-4") && cur_fam() == FAM_ENV);
    open_family(FAM_ENV);
    bad += check("..ENV again: its wave (DCW) envelope", str_eq(cur_page()->title, "C1 WAV R1-4"));
    for (i = 0; i < NPAGES; i++)
        found |= PAGES[i].fam == FAM_ENV && page_visible(i);
    bad += check("..the track's ADSR and ENV DEST hidden", !found);
    open_family(FAM_LFO);
    bad += check("LFO on CZ-1: its vibrato, LFO lit", str_eq(cur_page()->title, "CZ VIBRATO") && cur_fam() == FAM_LFO);
    open_family(FAM_LFO);
    bad += check("..LFO again: the track LFO (it modulates the CZ-1 too)", str_eq(cur_page()->title, "LFO"));
    go_home();
    set_engine_of(TSEL, ENGI_PROPHET);
    open_family(FAM_ENV);
    bad += check("ENV on the Prophet: its filter envelope", str_eq(cur_page()->title, "P5 FLT ENV"));
    open_family(FAM_LFO);
    bad += check("LFO on the Prophet: its LFO", str_eq(cur_page()->title, "P5 LFO"));
    go_home();
    set_engine_of(TSEL, ENGI_FM6);
    open_family(FAM_ENV);
    bad += check("ENV on FM6: the operator's EG", str_eq(cur_page()->title, "EG RATE"));
    open_family(FAM_LFO);
    bad += check("LFO on FM6: its LFO", str_eq(cur_page()->title, "FM LFO"));
    go_home();
    set_engine_of(TSEL, 3);
    open_family(FAM_ENV);
    bad += check("ENV on SID: the track's ADSR", str_eq(cur_page()->title, "ENV"));
    o = song.octave;
    press(B_OCTDN);
    bad += check("OCT- on a sound page: Stage, no octave", ui.home && song.octave == o);
    press(B_OCTDN);
    bad += check("OCT- on Stage: an octave down", ui.home && song.octave == o - 1);
    go_title("SCALES");
    press(B_OCTDN);
    bad += check("OCT- on SCALES: back to SCL's list", !ui.home && str_eq(cur_page()->title, "SCL"));
    return bad;
}

/* popups (ui_popup.c): OCT+ on a sound page its sheet (tapped: Enter, held too), never the octave (Stage only); the
 * sheet's rows; a list knob's picker */
static int test_popups(void)
{
    int bad = 0, o;
    ui_power_on();
    ui.home = 0; ui.page = (uint8_t)page_first(FAM_EDIT); page_entered(); frame();
    o = song.octave;
    press(B_OCTUP);
    bad += check("POPUPS: OCT+ tapped on a sound page: the sound's sheet (Enter), no octave", pop.on == POP_SHEET && song.octave == o);
    press(B_OCTDN);
    bad += check("POPUPS: OCT- closes it, the page stays", !pop.on && !ui.home && song.octave == o);
    hold(B_OCTUP);
    bad += check("POPUPS: OCT+ held: the sheet too, no octave", pop.on == POP_SHEET && song.octave == o);
    turn(EN_K2, 1); turn(EN_K2, 1);
    press(B_OCTUP);
    bad += check("POPUPS: KNOB 2 the row, OCT+ does it (Favourite) and closes", !pop.on && preset_favorite());
    hold(B_OCTUP); press(B_OCTDN);
    bad += check("POPUPS: OCT- closes the sheet", !pop.on && !ui.confirm);
    hold(B_OCTUP); press(B_OCTUP);
    bad += check("POPUPS: a destructive row (Init sound) asks first", !pop.on && ui.confirm == CF_INIT_SOUND);
    press(B_OCTDN);
    turn(EN_K1, 1);
    bad += check("POPUPS: a list knob (WAVE) turning: its picker", pop.on == POP_PICK && pop.col == 0u);
    fm1_ms += 2000u; frame();
    bad += check("POPUPS: the picker goes once the knob rests", !pop.on);
    turn(EN_K1, 1); turn(EN_K2, 1);
    bad += check("POPUPS: another knob turning closes it", !pop.on);
    turn(EN_K1, 1); press(B_OCTDN);
    bad += check("POPUPS: OCT- closes the picker (the page stays), no octave", !pop.on && !ui.home && song.octave == o);
    {   /* list pages (ui_list.c): KNOB 2 the row, KNOB 1 its value, OCT+ its list (SCL's Scale: SCALES), OCT- Stage */
        ui_power_on(); go_title("VOICE"); frame();
        o = TSEL->p[P_DETUNE];
        turn(EN_K2, 5); turn(EN_K1, 3);
        bad += check("LISTS: VOICE 1-3 one list; KNOB 2 to row 6 (VOICE 2's DETUNE), KNOB 1 edits it",
                     TSEL->p[P_DETUNE] == o + 3 && !page_visible(page_titled("VOICE 2")));
        turn(EN_K2, -5);
        press(B_OCTUP);
        bad += check("LISTS: OCT+ on a list value (VOICE): its whole list", pop.on == POP_LIST);
        o = TSEL->p[P_VOICE];
        turn(EN_K2, 1); press(B_OCTUP);
        bad += check("LISTS: KNOB 2 the next name, OCT+ takes it", !pop.on && TSEL->p[P_VOICE] == o + 1);
        go_title("SCL"); frame();
        turn(EN_K2, 1); press(B_OCTUP);
        bad += check("LISTS: SCL's Scale row, OCT+: the SCALES page", str_eq(cur_page()->title, "SCALES"));
        go_title("SCL"); frame();
        press(B_OCTDN);
        bad += check("LISTS: OCT- on a list: Stage", ui.home);
    }
    {   /* sections (ui_sections.c): PRESETS the next section and back to the page last used; EDIT: the map */
        uint32_t f;
        ui_power_on(); set_engine_of(TSEL, ENGI_PROPHET); go_title("P5 FLT ENV"); frame();
        f = ui.page;
        turn(EN_PRESET, 1);
        o = str_eq(sec.name[sec_of(ui.page)], "Amp");
        turn(EN_PRESET, -1);
        bad += check("SECTIONS: PRESETS the next section (Filter -> Amp), back: the page last used in it (FLT ENV)",
                     o && ui.page == f);
        press(B_EDIT);
        o = smap.on && smap.row == sec_of(ui.page);
        turn(EN_K2, 1); turn(EN_K1, 0); press(B_OCTUP);
        bad += check("SECTIONS: EDIT on an EDIT page: the map; KNOB 2 a row down, OCT+ opens its page",
                     o && !smap.on && str_eq(sec.name[sec_of(ui.page)], "Amp"));
        press(B_EDIT); press(B_EDIT);
        bad += check("SECTIONS: EDIT again closes the map", !smap.on);
    }
    go_page(GR_SONG); frame();
    hold(B_OCTUP);
    bad += check("POPUPS: SONG, OCT+ held: the song's sheet (no play)", pop.on == POP_SHEET && !song.playing);
    press(B_OCTUP);
    bad += check("POPUPS: Insert section: a row", !pop.on && chain_config.count == 1u);
    return bad;
}
static int test_song_view(void)
{
    static const uint8_t ROWS[6][NTRK] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {1, 1, 1, 1}, {1, 1, 1, 1}, {2, 0, 0, 0}, {0, 0, 0, 0}};
    char got[7];
    uint32_t i;
    int bad = 0;
    ui_power_on();
    for (i = 0; i < 6u; i++)
        got[i] = row_letter(ROWS, i);
    got[6] = 0;
    bad += check("SONG: rows with the same four patterns share a letter (A A B B C A)", str_eq(got, "AABBCA"));
    TSEL->p[P_SLEN] = 32;
    TSEL->p[P_SDIV] = 2;                              /* 1/16 */
    i = pat_bars(song.sel, TSEL->pattern);
    TSEL->p[P_SDIV] = 0;                              /* 1/4: 32 steps, 8 bars */
    bad += check("SONG: a pattern's bars from its LEN and DIV (32 x 1/16: 2, 32 x 1/4: 8)",
                 i == 2u && pat_bars(song.sel, TSEL->pattern) == 8u);
    return bad;
}
static int test_chain(void)
{
    uint32_t i, k, period, last, n = 32u;
    int bad = 0, ok;
    step_t before[NTRK][NSTEP];
    int16_t timing[NTRK][4], sounds[NTRK][P_COUNT];
    ui_power_on();
    bad += check("SONG empty: PLAY does not start", chain_prepare() == 1 && !transport_req);
    for (k = 0; k < 2u; k++) {
        for (i = 0; i < NTRK; i++) {
            pattern_request(&trk[i], k);
            track_defaults_steps(&trk[i]);
            trk[i].p[P_SLEN] = (int16_t)(2u + k);
            trk[i].step[0] = (step_t){{(uint8_t)(60u + 5u * k), (uint8_t)(64u + 5u * k), (uint8_t)(67u + 5u * k)}, 3, ST_NOTE, 0, 96};
        }
        project_save(k);
    }
    for (i = 0; i < NTRK; i++) {
        pattern_request(&trk[i], 2);
        my_steps(&trk[i]); trk[i].p[P_SLEN] = 9;
        memcpy(before[i], trk[i].step, sizeof before[i]);
        memcpy(timing[i], &trk[i].p[P_SLEN], sizeof timing[i]);
        memcpy(sounds[i], trk[i].p, sizeof sounds[i]);
    }
    chain_config.count = 2;
    chain_config.row[0] = (chain_row_t){0, 2}; chain_config.row[1] = (chain_row_t){1, 1};
    memset(chain_patterns[0], 0, NTRK); memset(chain_patterns[1], 1, NTRK);
    chain_patterns[1][3] = NPAT;
    bad += check("SONG invalid bank refuses before changing any track", chain_prepare() == 1 && !chain.armed &&
        !memcmp(trk[0].step, before[0], sizeof before[0]));
    chain_patterns[1][3] = 1;
    project_save(2); chain_defaults(&chain_config); pattern_init(); project_load(2);
    bad += check("SONG bank assignments saved and loaded with all patterns", chain_config.count == 2 &&
        chain_config.row[0].repeat == 2 && chain_patterns[1][3] == 1);
    song.rec = 3;
    bad += check("SONG prepares while stopped, no starts over a pending start", chain_prepare() == 0 && chain_prepare() == 2);
    events_block(n);
    bad += check("SONG starts all tracks at source step 0, recording paused", chain.running && song.playing &&
        !song.rec && trk[0].seq_idx == 0 && seq_steps(&trk[0])[0].n == 3 && trk[0].seq_n == 3 && trk[0].seq_notes[0] == 60);
    period = div_samples((uint32_t)trk[0].p[P_SDIV]);
    events_block(period);
    events_block(period);
    bad += check("SONG first row repeats without a gap", chain.row == 0 && chain.remaining == 1 && trk[0].seq_idx == 0);
    events_block(period);
    events_block(period);
    ok = chain.row == 1 && chain.remaining == 1;
    for (i = 0; i < NTRK; i++) {
        ok &= trk[i].seq_idx == 0 && trk[i].p[P_SLEN] == 3 && seq_steps(&trk[i])[0].note[0] == 65;
        for (k = 0; k < P_COUNT; k++)
            if (k < P_SLEN || k > P_SGATE) ok &= trk[i].p[k] == sounds[i][k];
    }
    bad += check("SONG row change: four tracks together, sounds unchanged", ok);
    open_family(FAM_SEQ);
    for (k = 0; k < NPAGES && cur_page()->scope != SC_STEP; k++) open_family(FAM_SEQ);
    turn(EN_K2, 1); press(B_EDIT); hold(B_REC); hold(B_SAVE);
    bad += check("SONG playing: step edits, clears, recording and undo are blocked", trk[0].step[0].note[0] == 65 && trk[0].step[0].n == 3 && !ui.confirm);
    events_block(period);
    events_block(period);
    events_block(period);
    ok = !song.playing && !chain.running && song.rec == 3;
    for (i = 0; i < NTRK; i++) ok &= !trk[i].seq_n && !memcmp(trk[i].step, before[i], sizeof before[i]) &&
        !memcmp(&trk[i].p[P_SLEN], timing[i], sizeof timing[i]);
    bad += check("SONG end: stops and restores editable patterns, timing and record arms", ok);
    bad += check("SONG source banks stay unchanged", pattern_at(0, 0)->step[0].note[0] == 60 && pattern_at(0, 1)->step[0].n == 3);
    chain_config.row[0].repeat = 1;
    chain_prepare(); events_block(n);
    trk[0].seq_idx = 1;
    last = step_samples(&trk[0], div_samples((uint32_t)trk[0].p[P_SDIV]), 1);
    trk[0].seq_pos = last - n + 19u;
    events_block(n);
    ok = chain.row == 1;
    for (i = 0; i < NTRK; i++) ok &= trk[i].seq_pos == 19u && trk[i].seq_idx == 0;
    bad += check("SONG transition preserves fractional block time on all tracks", ok);
    transport_req = 2; events_block(n);
    bad += check("SONG manual STOP restores the previous pattern", !chain.running && !song.playing && !memcmp(trk[0].step, before[0], sizeof before[0]));
    chain_prepare(); events_block(n);
    project_load(0);
    bad += check("PROJECT load during SONG stops it before loading new timing", !chain.running && !song.playing &&
        trk[0].p[P_SLEN] == 2 && trk[0].step[0].note[0] == 60 && !chain_config.count);
    ui_power_on();
    open_family(FAM_SEQ);
    for (k = 0; k < NPAGES && cur_page()->graph != GR_SONG; k++) open_family(FAM_SEQ);
    press(B_PLAY);
    bad += check("SONG page empty PLAY explains how to start", msg_is("ADD A SONG ROW") && !transport_req);
    turn(EN_K3, 1);
    bad += check("SONG KNOB 3 (pattern) adds the first row with one repeat", chain_config.count == 1 && !chain_config.row[0].slot && chain_config.row[0].repeat == 1);
    turn(EN_K1, 1); turn(EN_K3, 1); turn(EN_K3, 1); turn(EN_K4, 2);
    bad += check("SONG KNOB 1 row, 3 pattern, 4 repeats build a chain", chain_config.count == 2 && ui.song_row == 1 &&
        chain_config.row[1].slot == 1 && chain_config.row[1].repeat == 3);
    turn(EN_K2, 1);
    bad += check("SONG KNOB 2 picks the track (2), the song unchanged", song.sel == 1u && chain_config.count == 2);
    turn(EN_K2, -1);
    turn(EN_K4, 30);
    project_save(0);
    chain_config.count = 1;
    chain_config.row[0] = (chain_row_t){0, 1};
    fm1_in.buttons |= (1u << panel.btn[B_OCTUP]) | (1u << panel.btn[B_OCTDN]);
    host_pressed |= fm1_in.buttons; frame();
    fm1_in.buttons = 0; frame();
    bad += check("SONG OCT chord does not start playback or leave the page", !transport_req && !ui.home);
    press(B_OCTUP);
    bad += check("SONG OCT+ on release starts, pending start blocks SAVE", chain.armed && transport_req == 1);
    project_save(3);
    bad += check("SONG pending start cannot write a project", msg_is("STOP TO SAVE") && !project_used(3));
    press(B_PLAY); events_block(32);
    bad += check("SONG PLAY cancels a pending start", !chain.running && !chain.armed && !song.playing);
    uint32_t old_count = chain_config.count;
    turn(EN_K4, 30);
    bad += check("SONG KNOB 4 (repeats) cannot add or clear rows", chain_config.count == old_count);
    for (k = chain_config.count; k < CHAIN_ROWS; k++) { turn(EN_K1, 1); turn(EN_K3, 1); }
    bad += check("SONG append row stays bounded at 16", chain_config.count == CHAIN_ROWS && chain_valid(&chain_config));
    go_title("SONG"); hold(B_OCTUP); sheet_do("Clear song");
    bad += check("Clear song (the song's sheet) requires explicit confirmation", ui.confirm == CF_CLEAR_SONG &&
                 chain_config.count == CHAIN_ROWS);
    press(B_OCTDN);
    bad += check("cancel preserves the full song", chain_config.count == CHAIN_ROWS);
    hold(B_OCTUP); sheet_do("Clear song"); press(B_OCTUP);
    bad += check("confirmed CLEAR SONG keeps musical steps", !chain_config.count && !ui.song_row);
    return bad;
}

/* Product controls are exercised through the actual button/encoder handlers
 * and canvas, including all pages, both fonts and every shipped palette. */
static int test_product_ux(void)
{
    int bad = 0, ok = 1;
    uint32_t i, p, b;
    for (i = 0; i < NPAGES; i++) {
        ui_power_on(); set_engine_of(TSEL, 1);         /* (DIGITAL; without MELODEE_FM4 FM6: no OP pages) */
        ui.home = 0; ui.page = (uint8_t)i; page_entered();
        if (!page_visible(i))
            continue;
        press(B_REC);
        if (PAGES[i].graph == GR_SONG)
            ok &= song.rec == 0 && !transport_req && msg_is("[SEQ] TO RECORD");
        else ok &= song.rec == 1 && transport_req == 0 && ui.page == i && !ui.home;
    }
    bad += check("REC stays on every page, SONG stopped asks for pattern recording", ok);
    ui_power_on(); hold(B_HOME); press(B_REC);
    bad += check("REC in the MENU does nothing (the menu stays, no arm, no transport start)",
                 ui.menu == 1 && song.rec == 0 && !transport_req);
    ui.menu_sel = MI_ABOUT; press(B_OCTUP); press(B_REC);
    bad += check("REC in ABOUT does nothing (the document and its scroll stay)",
                 ui.menu == 2 && !ui.menu_scroll && song.rec == 0 && !transport_req);
    ui_power_on(); press(B_GLO);
    bad += check("GLO directly opens the four-channel MIXER", cur_page()->graph == GR_TRK);
    int16_t len = TSEL->p[P_SLEN], send = TSEL->p[P_REV];
    turn(EN_K3, 1);
    bad += check("MIXER K3 edits reverb, never sequence length", TSEL->p[P_SLEN] == len && TSEL->p[P_REV] == send + 1);
    press(B_GLO); ok = cur_page()->fam == FAM_GLO && str_eq(cur_page()->title, "GLOBAL");
    press(B_GLO); ok &= str_eq(cur_page()->title, "SYSTEM");
    press(B_GLO); ok &= cur_page()->graph == GR_TRK;
    bad += check("GLO cycles MIXER > GLOBAL > SYSTEM > MIXER", ok);
    go_home(); hold(B_SEQ);
    bad += check("long SEQ goes directly to PATTERNS (the song under it) with no tap on release", cur_page()->graph == GR_PATGRID);
    turn(EN_SELECT, 1);
    bad += check("  SELECT: SONG next to it", cur_page()->graph == GR_SONG);
    song.rec = 1; chain.armed = 1; press(B_REC);
    bad += check("REC cannot write borrowed patterns while SONG is armed", song.rec == 1 && msg_is("STOP TO RECORD"));
    ui_power_on(); open_family(FAM_EDIT);
    ok = 1;
    for (i = 0; i < 8u; i++) { open_family(FAM_EDIT); ok &= page_visible(ui.page) && !(cur_page()->id[0] >= P_FM1_ATK && cur_page()->id[0] <= P_FM4_LEVEL); }
    bad += check("operator envelope pages are hidden on non-DIGITAL instruments", ok);
#if MELODEE_FM4
    set_engine_of(TSEL, 1); open_family(FAM_EDIT); ok = 0;
    for (i = 0; i < 12u; i++) { if (cur_page()->id[0] == P_FM1_ATK) ok = 1; open_family(FAM_EDIT); }
    bad += check("DIGITAL exposes four envelopes and independent operator levels", ok);
    go_title("OP1 ENV"); turn(EN_K1, 4);
    bad += check("operator envelope K1 edits only OP1 attack", TSEL->p[P_FM1_ATK] == 4 && !TSEL->p[P_FM2_ATK]);
    track_select(2); frame();
    bad += check("changing to a non-FM track leaves the stale operator page", page_visible(ui.page) && cur_page()->id[0] == P_E0);
#else
    set_engine_of(TSEL, ENGI_DIGITAL); open_family(FAM_EDIT); ok = TSEL->eng_req == ENGI_FM6;
    for (i = 0; i < 12u; i++) { ok &= !(cur_page()->id[0] >= P_FM1_ATK && cur_page()->id[0] <= P_FM4_LEVEL); open_family(FAM_EDIT); }
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == FAM_EDIT && PAGES[i].id[0] >= P_FM1_ATK && PAGES[i].id[0] <= P_FM4_LEVEL)
            ok &= !page_visible(i);
    bad += check("DIGITAL retired: engine 1 asked for loads FM6; the OP ENV / OP LEVEL pages never show", ok);
#endif
    ui_power_on(); track_defaults_steps(TSEL); go_title("NOTES"); cursor_set(0); frame();
    press(B_OCTUP); press(B_OCTUP); turn(EN_K2, 1); turn(EN_K2, 1); turn(EN_K1, -35);   /* (a note placed; its sheet: Chance) */
    bad += check("CHANCE (the note's sheet) is per step and starts at backward-compatible 100 percent", step_chance(&TSEL->step[0]) == 65 && step_chance(&TSEL->step[1]) == 100);
    turn(EN_K1, -1000); ok = step_chance(&TSEL->step[0]) == 0;
    turn(EN_K1, 1000); ok &= step_chance(&TSEL->step[0]) == 100;
    bad += check("CHANCE controls clamp to 0..100 without changing adjacent steps", ok);
    ui_power_on(); song.rec = 1; seq_start(); go_home();
    int16_t baseline = TSEL->p[P_E0]; turn(EN_K1, 1);
    bad += check("REC plus a sound knob records motion through the UI", motion_count(TSEL) == 1 && motion_enabled(TSEL));
    press(B_PLAY); events_block(32);
    bad += check("stopping returns the original sound after a recorded knob gesture", TSEL->p[P_E0] == baseline);
    go_page(GR_MOTION); turn(EN_K1, -1);
    bad += check("MOTION playback OFF preserves its stored events", !motion_enabled(TSEL) && motion_count(TSEL) == 1);
    sheet_do("Clear all motion");
    bad += check("MOTION: the lane's sheet, Clear all motion always asks confirmation", ui.confirm == CF_CLEAR_MOTION &&
                 motion_count(TSEL) == 1);
    press(B_OCTDN); sheet_do("Clear all motion"); press(B_OCTUP);
    bad += check("confirmed MOTION clear removes events and SAVE hold restores them", motion_count(TSEL) == 0);
    hold(B_SAVE);
    bad += check("motion-clear UNDO restores the recorded events", motion_count(TSEL) == 1);
    press(B_OCTUP);
    bad += check("MOTION: OCT+ on the lane: its sheet (Play, Clear <lane>, Clear all motion)", pop.on == POP_SHEET &&
                 pop.rows == SHEET_MOTION && !memcmp(SHEET_MOTION[1].label, "Clear ", 6));
    press(B_OCTUP);
    bad += check("..Play: on again", motion_enabled(TSEL) && !pop.on);
    sheet_do(mo_clear_label);
    bad += check("..Clear <lane>: its events gone, undoable", motion_count(TSEL) == 0 && msg_is("LANE CLEARED"));
    hold(B_SAVE);
    bad += check("motion-clear UNDO restores the recorded events", motion_count(TSEL) == 1);
    ui_power_on(); ui_leds();
    memset(led_pos, 0xFF, sizeof led_pos);
    led_pos[panel.btn[B_PLAY]] = 1; led_pos[panel.btn[B_REC]] = 2;
    song.playing = song.rec = 1; ok = 1;
    for (i = 0; i < 120u; i++) { fm1_ms += 17; ui_leds(); ok &= (fm1_led[0] & 6u) == 6u; }
    bad += check("PLAY and REC LEDs stay lit across time and audio block phase", ok);
    led_pos_init();
    ok = 1;
    for (p = 0; p < NPALETTES; p++) for (b = 0; b < 1u; b++) {   /* (ENV: its knobs the strip under the curve) */
        uint16_t sound[240 * (240 - PV_SOUND_Y)], columns[240 * PV_STRIP_H];
        ui_power_on(); palette_set(p); settings.zoom = 1;
        open_family(FAM_ENV); frame();
        memcpy(sound, host_screen + PV_SOUND_Y * 240, sizeof sound);
        memcpy(columns, host_screen + PV_STRIP_Y * 240, sizeof columns);
        ui.hot_col = 1; ui.hot_t = 40; ui.force = 1; ui_draw();
        ok &= !memcmp(sound, host_screen + PV_SOUND_Y * 240, sizeof sound);
        int changed = 0;
        for (uint32_t y = 0; y < PV_STRIP_H; y++) for (uint32_t x = 0; x < 240; x++) {
            uint16_t old = columns[y * 240 + x], now = host_screen[(PV_STRIP_Y + y) * 240 + x];
            if (x >= (uint32_t)CARD_X(1) && x < (uint32_t)(CARD_X(1) + CARD_W)) changed |= old != now;   /* (outlined) */
            else ok &= old == now;
        }
        ok &= changed;
    }
    bad += check("all palettes: the knob turning is lifted in place, the others and the page stay", ok);
    bad += check("SAMPLE and SLICE retired from selectors with reserved stored IDs",!eng_ok(4) && !eng_ok(13) && !ENGINES[4]->npresets && !ENGINES[13]->npresets);
    ui_power_on(); return bad;
}

static int test_panel(void)
{
    int bad = 0;
    panel = PANEL_DEFAULT;
    panel.btn[B_PLAY] = panel.btn[B_REC];
    panel_init();
    bad += check("duplicate button mappings recover to the default panel", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.enc[EN_K1] = panel.enc[EN_K2];
    panel_init();
    bad += check("duplicate knob mappings recover to the default panel", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.btn[B_PLAY] = 255;
    panel_init();
    bad += check("out-of-range panel data recovers without an invalid shift", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.dir[EN_K1] = -1;
    panel_init();
    bad += check("a valid reversed knob mapping is retained", panel.dir[EN_K1] == -1);
    return bad;
}

/* ------------------------------------------------- the FX hold layer --- */
static void btn_down(uint32_t label) { fm1_in.buttons |= 1u << panel.btn[label]; host_pressed |= 1u << panel.btn[label]; }
static void btn_up(uint32_t label) { fm1_in.buttons &= ~(1u << panel.btn[label]); }
static void key_down(uint32_t k) { fm1_in.notes |= 1u << k; host_notes |= 1u << k; keyboard_block(); }
static void key_up(uint32_t k) { fm1_in.notes &= ~(1u << k); keyboard_block(); }
static void frames(uint32_t ms) { uint32_t i; for (i = 0; i < ms / 16u; i++) frame(); }
static uint32_t gates(void)                        /* voices of every track with their key down */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NTRK; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].gate != 0;
    return n;
}
static uint32_t white(uint32_t w) { return key_at(0, w); }

static int test_layer(void)
{
    int bad = 0, ok, flash;
    uint32_t i, mo, k;
    /* tap: on release, the page; held briefly: no map ever */
    ui_power_on();
    btn_down(B_FX); frame();
    ok = ui.home;
    frames(64); btn_up(B_FX); frame();
    bad += check("FX tap: nothing on press, the FX page when let go", ok && !ui.home && cur_page()->fam == FAM_FX);
    press(B_FX);
    bad += check("  a second tap: the next page of the family (SLICER)", str_eq(cur_page()->title, "SLICER"));
    ui_power_on();
    btn_down(B_FX);
    for (flash = 0, i = 0; i < 22u; i++) { frame(); flash |= ui.layer; }      /* 0.35 s */
    btn_up(B_FX); frame();
    bad += check("  held 0.35 s (HOLD 0.4): still a tap, the map never shown", !flash && !ui.home);
    /* peek: past HOLD alone, the map; letting go does nothing */
    ui_power_on();
    btn_down(B_FX); frames(368);
    ok = !ui.layer;
    frames(64);
    ok &= ui.layer == LAYER_FX;
    btn_up(B_FX); frame();
    bad += check("FX held past 0.4 s: the map (not before); let go: no page, the map gone",
                 ok && ui.home && !ui.layer);
    /* the HOLD setting: 0.6 s */
    ui_power_on();
    hold(B_HOME); ui.menu_sel = MI_HOLD; turn(EN_K1, 1); turn(EN_K1, 1); ok = settings_hold == 3u; hold(B_HOME);
    btn_down(B_FX); frames(512); flash = ui.layer; btn_up(B_FX); frame();
    bad += check("menu HOLD 0.6 s (KNOB 1): FX held 0.5 s is a tap, no map", ok && !flash && !ui.home && !ui.menu);
    {
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        settings_export(&p); settings_hold = HOLD_DEF;
        bad += check("  HOLD is saved with the settings and read back", settings_import(&p, sizeof p) && settings_hold == 3u);
    }
#if MELODEE_USB_AUDIO
    /* USB AUDIO: KNOB 1 steps IN+OUT IN OUT OFF, into ua_off_want (main.c applies it 0.6 s after the knob rests: a
     * replug, usb_audio_driver_test.c); the settings keep what the host got */
    ui_power_on();
    ua_off = ua_off_want = 0;
    hold(B_HOME); ui.menu_sel = MI_USB; turn(EN_K1, 1); turn(EN_K1, 1);
    ok = ua_off_want == UA_OFF_IN && !ua_off;
    hold(B_HOME);
    ua_off = ua_off_want;                            /* (ua_off_apply) */
    {
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        settings_export(&p); ua_off = ua_off_want = 0;
        bad += check("menu USB AUDIO (KNOB 1 twice: OUT only): wanted at once, the settings keep it",
                     ok && p.ext.usb_off == UA_OFF_IN && settings_import(&p, sizeof p) && ua_off == UA_OFF_IN &&
                     ua_off_want == UA_OFF_IN);
        p.magic = PERSIST_MAGIC4;                    /* Felucca 1.0's record: both devices */
        bad += check("  a PER4 record (Felucca 1.0) reads with both devices on", settings_import(&p, (int)PERSIST_LEN4) == 2 &&
                     !ua_off && !ua_off_want && p.magic == PERSIST_MAGIC && !p.ext.usb_off);
    }
    ua_off = ua_off_want = 0;
#endif
    /* combo: a key with FX: at once, silent, no MIDI, no recording, no step */
    ui_power_on();
    usb.config = 1; mo = mo_w;
    go_page(GR_ROLL); my_steps(TSEL); song.rec = 1; song.playing = 1;
    {
        step_t before[NSTEP];
        uint32_t cur = ui.cursor;
        memcpy(before, TSEL->step, sizeof before);
        btn_down(B_FX); key_down(white(2)); frame();
        ok = ui.layer == LAYER_FX && !gates() && mo_w == mo && (kb_layer >> white(2)) & 1u &&
             (perf_held & PF_BIT(PF_R32));                    /* white key 3 (A3): REPEAT 1/32 */
        for (i = 0; i < 40u; i++) events_block(CTL);
        ok &= !memcmp(before, TSEL->step, sizeof before) && ui.cursor == cur;
        bad += check("FX + a key: the map at once; no voice, no MIDI, no recording, no step written", ok);
        key_up(white(2)); frame();
        bad += check("  the key let go: its effect off, no note-off sent", !perf_held && !kb_layer && mo_w == mo);
        btn_up(B_FX); frame();
        bad += check("  FX let go after a combo: no tap, the map gone", !ui.layer && cur_page()->graph == GR_ROLL);
    }
    song.playing = 0; song.rec = 0;
    /* FX let go first: the effect stays with its key, the map too; a key pressed now is a note */
    ui_power_on();
    btn_down(B_FX); key_down(white(4)); frame();          /* white key 5 (C4): LPF */
    btn_up(B_FX); frame();
    ok = (perf_held & PF_BIT(PF_LPF)) && ui.layer == LAYER_FX;
    key_down(white(0)); frame();
    ok &= gates() > 0 && !((kb_layer >> white(0)) & 1u);
    key_up(white(0)); key_up(white(4)); frame();
    bad += check("FX let go first: the key holds its effect and the map; a new key is a note", ok && !ui.layer && !perf_held);
    /* OCT UP (G4) and OCT DN (A4), the 9th and 10th white keys: held effects, silent; KNOB 4 the shimmer while
     * held, back to 0 with FX; B4 on does nothing */
    ui_power_on();
    usb.config = 1; mo = mo_w;
    btn_down(B_FX); key_down(white(8)); frame();
    ok = ui.layer == LAYER_FX && !gates() && mo_w == mo && perf_held == PF_BIT(PF_OUP);
    perf_begin(CTL);                                  /* audio activates OCT UP, so the card / knob is SHIMR */
    ok &= perf_harm_on();
    turn(EN_K4, 40);
    ok &= perf_k[3] == 40;
    key_up(white(8)); key_down(white(9)); frame();
    ok &= perf_held == PF_BIT(PF_ODN) && !gates() && mo_w == mo;
    key_up(white(9)); key_down(white(10)); frame();
    ok &= perf_held == PF_BIT(PF_FLG) && (kb_layer >> white(10)) & 1u && !gates();
    key_up(white(10)); btn_up(B_FX); frame();
    bad += check("FX + G4 / A4: OCT UP / OCT DN held, silent, no MIDI; KNOB 4 turns; B4 FLANGER; FX let go: K4 back to 0",
                 ok && !perf_held && !ui.layer && !perf_k[3] && mo_w == mo);
    /* REVERB > TYPE (FX family, global): ROOM / SPRING on KNOB 1, kept by a project */
    ui_power_on();
    go_title("REVERB");
    ok = cur_page()->fam == FAM_FX && cur_page()->id[0] == G_RTYPE && cur_page()->id[1] == G_RSIZE &&
         cur_page()->id[2] == G_RDAMP && song.g[G_RTYPE] == 0;
    turn(EN_K1, 1);
    ok &= song.g[G_RTYPE] == 1 && str_eq(GP[G_RTYPE].names[song.g[G_RTYPE]], "SPRING");
    turn(EN_K1, 5);
    ok &= song.g[G_RTYPE] == 2;
    turn(EN_K1, -1);
    song.playing = 0;
    project_save(2);
    turn(EN_K1, -1);
    ok &= song.g[G_RTYPE] == 0;
    project_load(2);
    bad += check("REVERB page: TYPE ROOM -> SPRING on KNOB 1 (SIZE, DAMP beside it); a project keeps SPRING",
                 ok && song.g[G_RTYPE] == 1);
    go_title("CHORUS");
    bad += check("  CHORUS page: CRT CDP", cur_page()->fam == FAM_FX && cur_page()->id[0] == G_CRATE &&
                 cur_page()->id[1] == G_CDEPTH && cur_page()->id[2] == 0xFFu);
    /* chording: a key held before FX stays a note, its note-off arrives */
    ui_power_on();
    mo = mo_w;
    key_down(white(4)); frame(); btn_down(B_FX); frame(); key_up(white(4)); frame(); btn_up(B_FX); frame();
    for (i = 0; i < 4u; i++) events_block(CTL);
    bad += check("a key held before FX stays a note: note-on, note-off, nothing left held", mo_w == mo + 2u && !gates() &&
                 !kb_layer);
    /* knob macros: not the page's, not recorded, back to 0 when FX is let go */
    ui_power_on();
    go_title("ENV"); song.rec = 1; song.playing = 1;
    {
        int16_t atk = TSEL->p[P_ATK];
        uint32_t ev = motion.count;
        btn_down(B_FX); frame();
        turn(EN_K1, -12); turn(EN_K2, 30);
        ok = perf_k[0] == -12 && perf_k[1] == 30 && TSEL->p[P_ATK] == atk && motion.count == ev && ui.layer == LAYER_FX;
        turn(EN_PRESET, 3);
        ok &= user_of(TSEL) == USER_NONE && TSEL->preset == trk[0].preset;
        btn_up(B_FX); frame();
        bad += check("FX + KNOB 1 / 2: the macros, not ATK, not recorded; FX let go: back to 0, no tap",
                     ok && !perf_k[0] && !perf_k[1] && str_eq(cur_page()->title, "ENV"));
    }
    song.rec = 0; song.playing = 0;
    /* buttons in the layer: PLAY works, SAVE, HOME, a page button are swallowed */
    ui_power_on();
    btn_down(B_FX); frame();
    press(B_PLAY);
    ok = transport_req == 1u && ui.layer == LAYER_FX;
    transport_req = 0;
    press(B_SAVE); ok &= ui.home;
    hold(B_SAVE); ok &= !msg_is("UNDO T1") && ui.home;
    press(B_ENV); ok &= ui.home;
    hold(B_HOME); ok &= !ui.menu && ui.home;
    btn_up(B_FX); frame();
    bad += check("in the layer: PLAY plays; SAVE (tap and hold), ENV, HOME (hold) do nothing", ok && ui.home && !ui.menu);
    btn_down(B_FX); frame(); btn_down(B_ENV); frame(); btn_up(B_FX); frame(); btn_up(B_ENV); frame();
    bad += check("  a page button pressed with FX, let go after it: still nothing", ui.home);
    /* release-act: every page button acts when let go */
    {
        static const uint8_t PB[6] = {B_ENV, B_LFO, B_SCL, B_ARP, B_GLO, B_EDIT};
        static const uint8_t PF[6] = {FAM_ENV, FAM_LFO, FAM_SCL, FAM_ARP, FAM_GLO, FAM_EDIT};
        ok = 1;
        for (i = 0; i < 6u; i++) {
            ui_power_on();
            btn_down(PB[i]); frame();
            ok &= ui.home;
            btn_up(PB[i]); frame();
            ok &= !ui.home && (PB[i] == B_GLO ? cur_page()->graph == GR_TRK : cur_page()->fam == PF[i]);
            if (PB[i] == B_SCL || PB[i] == B_GLO || PB[i] == B_EDIT)
                continue;                             /* (a layer of their own: held long is a peek) */
            ui_power_on();
            hold(PB[i]);                              /* (no layer of their own: a long press is a tap too) */
            ok &= !ui.home && cur_page()->fam == PF[i];
        }
        bad += check("ENV LFO SCL ARP GLO EDIT act when let go, not on press (ENV LFO ARP held long: the same)", ok);
    }
    ui_power_on();
    go_page(GR_ROLL); my_steps(TSEL); ui.cursor = 0;
    btn_down(B_EDIT); frame();
    ok = step_on(&TSEL->step[0]) && ui.cursor == 0;
    btn_up(B_EDIT); frame();
    bad += check("EDIT on STEP clears the step when let go (not on press)", ok && !step_on(&TSEL->step[0]) && ui.cursor == 1);
    ui_power_on(); hold(B_HOME);
    ok = ui.menu == 1; hold(B_HOME); ok &= !ui.menu;
    go_home(); hold(B_SEQ); ok &= cur_page()->graph == GR_PATGRID;
    bad += check("HOME (menu) and SEQ (PATTERNS) holds of 0.7 s", ok);
    /* no layer in the menu or a dialog: FX + a key is a note */
    ui_power_on(); hold(B_HOME);
    btn_down(B_FX); frame(); key_down(white(3)); frame();
    ok = !ui.layer && !perf_mask && gates() > 0 && !kb_layer;
    key_up(white(3)); btn_up(B_FX); frame();
    bad += check("in the menu: no layer, FX + a key plays the key; FX let go: no page", ok && ui.menu == 1);
    ui_power_on(); ui.confirm = CF_CLEAR_SEQ;
    btn_down(B_FX); frames(512);
    ok = !ui.layer;
    btn_up(B_FX); frame();
    bad += check("in a dialog: FX held shows no map and taps nothing", ok && ui.confirm == CF_CLEAR_SEQ);
    /* UBOOT: the countdown closes the map and stops every effect; OCT- + OCT+ are still seen */
    ui_power_on();
    btn_down(B_FX); key_down(white(5)); frame();
    btn_down(B_OCTDN); btn_down(B_OCTUP); frame();
    ok = ui.layer == LAYER_FX && (fm1_in.buttons & 3u << panel.btn[B_OCTDN]) != 0u;
    ui.uboot = 3; frame();                          /* (main.c: OCT- + OCT+ held 2 s) */
    ok &= !ui.layer && perf_kill && !perf_mask && perf_begin(CTL) >= 0;
    ok &= (fm1_in.buttons & ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]))) ==
          ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]));
    ui.uboot = 0;
    btn_up(B_OCTDN); btn_up(B_OCTUP); key_up(white(5)); btn_up(B_FX); frame();
    bad += check("UBOOT countdown over the layer: the map closes, effects off; OCT- + OCT+ untouched", ok && !perf_kill && !kb_layer);
    /* the LEDs of the map */
    ui_power_on();
    btn_down(B_FX); key_down(white(1)); frame();
    {
        uint32_t a, b2;
        song.g[G_BPM] = 120;
        fm1_ms = 0; a = layer_leds();
        fm1_ms = 250; b2 = layer_leds();
        k = white(1);
        ok = ((a >> k) & 1u) && ((b2 >> k) & 1u);                    /* held: lit */
        for (i = 0; i < PF_NFX; i++)                                     /* the other effects (F3 .. A4): blink */
            ok &= i == 1u || ((a ^ b2) >> white(i)) & 1u;
        for (i = PF_NFX; i < PF_KEYS; i++)                                    /* B4 .. G5: no effect, dark */
            ok &= !((a | b2) >> white(i) & 1u);
        ok &= ((a ^ b2) >> key_at(1, 0)) & 1u && !((a | b2) >> key_at(1, 4) & 1u);   /* a mute blinks, a spare black dark */
        song.g[G_BPM] = 72;
        fm1_ms = 0; a = layer_leds();
        fm1_ms = 250; b2 = layer_leds();
        ok &= !((a | b2) >> white(0) & 1u) && !((a | b2) >> white(3) & 1u);   /* 1/8, REVERSE: too long at 72 */
        ok &= ((a ^ b2) >> white(2)) & 1u;                            /* 1/32 still blinks */
        song.g[G_BPM] = 120;
    }
    key_up(white(1)); btn_up(B_FX); frame();
    bad += check("map LEDs: held lit, the 12 effects blink, D5 .. G5 and a too-long REPEAT dark", ok);
    usb.config = 0;
    return bad;
}

/* ------------------------------------------------------------ NAME --- */
static void nm_tap(uint32_t k)                    /* a key tapped in NAME: down a frame, up a frame (32 ms) */
{
    fm1_in.notes |= 1u << k; host_notes |= 1u << k; frame();
    fm1_in.notes &= ~(1u << k); frame();
}
static uint32_t nm_black_key(uint32_t f, uint32_t octave)   /* the black key of function f (NB_*: F# G# A# C# D#), octave 0 / 1 */
{
    return key_at(1, f + 5u * octave);
}
static int test_name(void)
{
    int bad = 0, ok;
    uint32_t i, mo;
    char b[16];
    ui_power_on();
    go_page(GR_USER);
    ui.uslot = 2;
    sheet_do("Save here");
    up_auto_name(b, TSEL->eng_req, 2);
    bad += check("NAME: USER SAVE into an empty slot opens NAME, prefilled with the automatic name",
                 name_on() && nm.kind == NK_USER_SAVE && nm.slot == 2u && str_eq(nm.s, b) && nm.cur == nm.len);
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    bad += check("  DELETE removes the character before the cursor, down to empty", nm.len == 0u && nm.cur == 0u && !nm.s[0]);
    /* multi-tap: the same key cycles, another key commits, 0.8 s commits */
    nm_tap(white(0));
    ok = nm.len == 1u && nm.s[0] == 'A' && nm.key == 1u && nm.cur == 0u;
    nm_tap(white(0));
    ok &= nm.s[0] == 'B' && nm.len == 1u;
    nm_tap(white(0));
    bad += check("  a white key types its first letter; tapped again within 0.8 s: the next one, cycling (A B A)",
                 ok && nm.s[0] == 'A' && nm.len == 1u && nm.cur == 0u);
    nm_tap(white(1));
    bad += check("  another key keeps the letter and types its own (A, C)", nm.len == 2u && str_eq(nm.s, "AC") && nm.cur == 1u &&
                 nm.key == 2u);
    frames(800);
    bad += check("  0.8 s without a tap keeps it: the cursor moves on", nm.key == 0u && nm.cur == 2u && str_eq(nm.s, "AC"));
    nm_tap(white(1));
    frames(400);
    nm_tap(white(1));
    bad += check("  a second tap within 0.8 s (0.43 s later) still cycles (C -> D)", str_eq(nm.s, "ACD") && nm.key == 2u);
    frames(800);
    nm_tap(white(4)); nm_tap(white(4)); nm_tap(white(4));   /* IJK: K */
    nm_tap(nm_black_key(NB_SPACE, 1));                       /* SPACE (the upper octave's G#) keeps K first */
    bad += check("  a three-letter key (IJK) cycles to K; SPACE keeps it and adds a space", str_eq(nm.s, "ACDK ") && nm.cur == 5u &&
                 !nm.key);
    nm_tap(nm_black_key(NB_DEL, 1));
    nm_tap(nm_black_key(NB_LEFT, 0));
    nm_tap(nm_black_key(NB_LEFT, 0));
    nm_tap(white(2));                             /* E inserted before D */
    frames(800);
    bad += check("  DELETE, cursor left twice, a letter inserted at the cursor", str_eq(nm.s, "ACEDK") && nm.cur == 3u);
    nm_tap(nm_black_key(NB_RIGHT, 1));
    nm_tap(nm_black_key(NB_RIGHT, 0));
    nm_tap(nm_black_key(NB_RIGHT, 0));
    bad += check("  cursor right stops at the end", nm.cur == 5u);
    /* 123 */
    nm_tap(nm_black_key(NB_MODE, 0));
    nm_tap(white(1)); nm_tap(white(9)); nm_tap(white(15));
    bad += check("  D# switches to 123: one tap, one character (2, 0, +), no cycling", nm.num && str_eq(nm.s, "ACEDK20+") &&
                 nm.cur == 8u && !nm.key);
    nm_tap(nm_black_key(NB_MODE, 1));
    nm_tap(white(15)); nm_tap(white(15)); nm_tap(white(15));
    frames(800);
    bad += check("  back to ABC: the last key (0-.) gives 0, then -, then .", !nm.num && str_eq(nm.s, "ACEDK20+.") && nm.cur == 9u);
    /* the knobs */
    turn(EN_K1, -2);
    turn(EN_K2, 1);
    bad += check("  KNOB 1 moves the cursor, KNOB 2 changes the character there (+ -> SPACE, wrapping)",
                 nm.cur == 7u && str_eq(nm.s, "ACEDK20 ."));
    turn(EN_K2, 5);
    bad += check("  KNOB 2 on: SPACE + 5 = E", nm.s[7] == 'E');
    turn(EN_K1, 9);
    turn(EN_K2, 1);
    bad += check("  KNOB 1 to the end, KNOB 2 adds a character there (A)", str_eq(nm.s, "ACEDK20E.A") && nm.cur == 9u && nm.len == 10u);
    turn(EN_K2, -2);
    bad += check("  .. and turns it back past SPACE to the last symbol (+)", nm.s[9] == '+');
    /* full */
    nm_tap(nm_black_key(NB_RIGHT, 0));
    nm_tap(white(7)); frames(800); nm_tap(white(7)); frames(800);
    ui.msg_t = 0;
    nm_tap(white(7));
    bad += check("  12 characters at most: NAME FULL, nothing typed", nm.len == 12u && msg_is("NAME FULL") && str_eq(nm.s, "ACEDK20E.+PP"));
    /* held DELETE repeats */
    fm1_in.notes |= 1u << nm_black_key(NB_DEL, 0); host_notes |= 1u << nm_black_key(NB_DEL, 0);
    frames(16 * 40);
    fm1_in.notes &= ~(1u << nm_black_key(NB_DEL, 0)); frame();
    bad += check("  DELETE held 0.64 s: one, then it repeats after 0.45 s, every 0.09 s (3 in all)", nm.len == 9u);
    /* the keys never sound, record or send MIDI */
    usb.config = 1; mo = mo_w; song.rec = 1;
    {
        step_t before[NSTEP];
        memcpy(before, TSEL->step, sizeof before);
        key_down(white(3)); key_down(nm_black_key(NB_SPACE, 0)); frame();
        for (i = 0; i < 8u; i++) events_block(CTL);
        ok = !gates() && mo_w == mo && !memcmp(before, TSEL->step, sizeof before) && song.grid == 2u;
        key_up(white(3)); key_up(nm_black_key(NB_SPACE, 0)); frame();
        bad += check("  keys in NAME: no voice, no MIDI, nothing recorded (seq.c: song.grid 2)", ok && mo_w == mo);
    }
    song.rec = 0; usb.config = 0;
    {   /* LEDs: all 27 keys do something; the cycling key blinks */
        uint32_t a, c, t0;
        nm_tap(nm_black_key(NB_DEL, 0)); nm_tap(nm_black_key(NB_DEL, 0));
        nm_tap(white(5));
        t0 = (fm1_ms / 500u + 1u) * 500u;
        fm1_ms = t0; a = name_leds(); fm1_ms = t0 + 250u; c = name_leds();
        nm.t = fm1_ms;                            /* (the letter still cycling) */
        bad += check("  LEDs: every key lit, the cycling key (LM) blinks", ((a | c) == 0x7FFFFFFu) && ((a ^ c) == (1u << white(5))));
    }
    /* cancel */
    {
        uint32_t page = ui.page;
        press(B_OCTDN);
        bad += check("  OCT- cancels: nothing written, back on the USER page", !name_on() && !up_used(2) && ui.page == page && !ui.home);
    }
    /* typing a name and saving it */
    sheet_do("Save here");
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    nm_tap(white(1)); nm_tap(white(0)); nm_tap(white(0)); nm_tap(nm_black_key(NB_RIGHT, 0));    /* C B */
    nm_tap(nm_black_key(NB_SPACE, 0)); nm_tap(nm_black_key(NB_MODE, 0)); nm_tap(white(1));        /* " 2" */
    nm_tap(nm_black_key(NB_SPACE, 0));                                                           /* a trailing space */
    song.playing = 1;
    press(B_OCTUP);
    bad += check("  OCT+ while playing: STOP TO SAVE, NAME stays", name_on() && msg_is("STOP TO SAVE") && !up_used(2));
    stop_transport();
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  OCT+ stopped: saved as typed, the trailing space dropped (CB 2)", !name_on() && up_used(2) && str_eq(b, "CB 2") &&
                 msg_is("SAVED (RAM)"));
    /* SAVE held does nothing in NAME; the FX layer neither */
    sheet_do("Save here"); press(B_OCTUP);        /* (U03 used: OVERWRITE?, YES) */
    bad += check("  overwrite: the dialog, then NAME", name_on() && nm.kind == NK_USER_SAVE);
    ui.msg_t = 0;
    hold(B_SAVE);
    btn_down(B_FX); frames(600); ok = !ui.layer; btn_up(B_FX); frame();
    bad += check("  SAVE held, FX held: nothing in NAME (no undo, no map, no page)", name_on() && ok && cur_page()->graph == GR_USER &&
                 !ui.msg_t);
    press(B_OCTDN);
    /* rename: EDIT on USER */
    ui.uslot = 5;
    press(B_EDIT);
    bad += check("  EDIT on an empty slot: EMPTY SLOT", !name_on() && msg_is("EMPTY SLOT") && cur_page()->graph == GR_USER);
    ui.uslot = 2;
    up_load(2);                                   /* (the sound now comes from U03) */
    trk[0].p[P_E0] = 33;                          /* an edit: a rename must not store it */
    press(B_EDIT);
    bad += check("  EDIT on a used slot: NAME (rename) with its name", name_on() && nm.kind == NK_USER_RENAME && str_eq(nm.s, "CB 2"));
    nm_tap(nm_black_key(NB_DEL, 0));
    nm_tap(white(10)); nm_tap(white(10));          /* VW: W */
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  OCT+: renamed (CB W), the sound stored there unchanged", !name_on() && str_eq(b, "CB W") &&
                 up_value(up_rec(2), P_E0) != 33 && msg_is("RENAMED (RAM)"));
    press(B_EDIT);
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    press(B_OCTUP);
    {
        char au[16];
        up_name(2, b);
        up_auto_name(au, up_rec(2)->engine, 2);
        bad += check("  a name deleted to nothing: the automatic one", str_eq(b, au));
    }
    /* the sound's own name is the prefill */
    trk[0].user = 3;
    ui.uslot = 7;
    sheet_do("Save here");
    up_name(2, b);
    bad += check("  SAVE of a sound that came from U03: prefilled with U03's name", name_on() && str_eq(nm.s, b));
    press(B_OCTDN);
    /* projects */
    go_page(GR_SLOTS);
    song.g[G_SLOT] = 2;
    sheet_do("Save here");
    for (i = 0; i < 4u; i++) { nm_tap(white(7)); nm_tap(white(6)); frames(800); }   /* P N P N .. */
    press(B_OCTUP);
    {
        char pn[16];
        int named = project_name(1, pn);
        bad += check("PROJECT SAVE: NAME, typed, saved: the slot has the name, the project too", named && str_eq(pn, "PNPNPNPN") &&
                     str_eq(proj_name, "PNPNPNPN"));
    }
    proj_name[0] = 0;
    project_load(1);
    bad += check("  loading it brings its name back (the next save's prefill)", str_eq(proj_name, "PNPNPNPN"));
    trk[0].p[P_LEVEL] = 7;
    press(B_EDIT);
    for (i = 0; i < 4u; i++) nm_tap(nm_black_key(NB_DEL, 1));
    press(B_OCTUP);
    {
        char pn[16];
        project_name(1, pn);
        bad += check("  EDIT renames a project (PNPN); its music unchanged", str_eq(pn, "PNPN") && stored_param(1, 0, P_LEVEL) != 7 &&
                     msg_is("RENAMED (RAM)"));
    }
    song.g[G_SLOT] = 4;
    press(B_EDIT);
    bad += check("  EDIT on an empty project slot: EMPTY SLOT", !name_on() && msg_is("EMPTY SLOT"));
    song.g[G_SLOT] = 2;
    press(B_EDIT);
    hold(B_HOME);
    bad += check("  HOME held in NAME: the menu, NAME closed", ui.menu && !name_on());
    hold(B_HOME);
    return bad;
}

/* the EDIT cycle (no ENGINE page: engines are the EDIT layer's) and its memory */
static int engine_cycle(const char *const *want, uint32_t n)   /* EDIT tapped from HOME, then SELECT n - 1 times */
{                                                                 /* (EDIT again: the map, ui_sections.c) */
    uint32_t i;
    int ok = 1;
    go_home(); frame();
    for (i = 0; i < n; i++) {
        if (i) turn(EN_SELECT, 1);
        else press(B_EDIT);
        ok &= str_eq(cur_page()->title, want[i]);
    }
    return ok;
}
/* CZ-1: every value of the native Casio tone on its own page (cz_edit.h), knob edits written into the
 * 144 bytes; an edit leaves the bytes it does not encode verbatim; SUS stays before END; the TOOLS
 * page copies a line, compares with the tone as loaded, names the tone */
static int test_cz1_pages(void)
{
    static const char *const CYC[] = {"CZ TOOLS", "CZ LINE", "CZ DETUNE", "CZ VIBRATO",
        "CZ WINDOW", "CZ1 WAVE", "CZ1 TOUCH",
        "C1 PIT R1-4", "C1 PIT R5-8", "C1 PIT L1-4", "C1 PIT L5-8", "C1 PIT POINT",
        "C1 WAV R1-4", "C1 WAV R5-8", "C1 WAV L1-4", "C1 WAV L5-8", "C1 WAV POINT",
        "C1 AMP R1-4", "C1 AMP R5-8", "C1 AMP L1-4", "C1 AMP L5-8", "C1 AMP POINT",
        "CZ2 WAVE", "CZ2 TOUCH",
        "C2 PIT R1-4", "C2 PIT R5-8", "C2 PIT L1-4", "C2 PIT L5-8", "C2 PIT POINT",
        "C2 WAV R1-4", "C2 WAV R5-8", "C2 WAV L1-4", "C2 WAV L5-8", "C2 WAV POINT",
        "C2 AMP R1-4", "C2 AMP R5-8", "C2 AMP L1-4", "C2 AMP L5-8", "C2 AMP POINT",
        "VOICE", "CZ TOOLS"};
    uint8_t p[LCZ_PACKED], q[LCZ_PACKED], raw[CZ_BYTES], before[CZ_BYTES];
    uint32_t tr, id, v, i, ok = 1, sane = 1, kept = 1;
    int bad = 0;
    ui_power_on();
    stop_transport();
    tr = song.sel % NTRK;
    set_engine_of(TSEL, ENGI_CZ);
    frame();
    bad += check("CZ-1 EDIT cycle: tone pages and VOICE, without bank/patch pages", engine_cycle(CYC, NELEM(CYC)));
    /* every value, every setting: what is put reads back; the others stay (END's level, a SUS past END aside) */
    for (id = 0; id < LCZ_NP; id++) {
        if (!CZ_PD[id].label)
            continue;
        for (v = (uint32_t)CZ_PD[id].min; v <= (uint32_t)CZ_PD[id].max; v++) {
            cz_patch_init(cz_patch[tr].raw);
            cz_ed_decode(p, cz_patch[tr].raw);
            if (!cz_ed_put(tr, id, v, raw)) {           /* no change: already that, or END's level / SUS */
                uint32_t k = id - LCZ_EBASE(0, 0), e = LCZ_EBASE(0, 0) + k / 18u * 18u;
                ok &= p[id] == v || (id >= LCZ_EBASE(0, 0) && id < LCZ_OLD_NP &&
                                     (k % 18u == 16u || (k % 18u >= 8u && k % 18u < 16u && id - e - 8u == p[e + 17u])));
                continue;
            }
            sane &= cz_patch_valid(raw);
            cz_ed_decode(q, raw);
            if (id < LCZ_EBASE(0, 0) || id >= LCZ_OLD_NP || (id - LCZ_EBASE(0, 0)) % 18u != 16u) {   /* (SUS: below) */
                if (id >= LCZ_EBASE(0, 0) && id < LCZ_OLD_NP && (id - LCZ_EBASE(0, 0)) % 18u >= 8u &&
                    (id - LCZ_EBASE(0, 0)) % 18u < 16u) {
                    uint32_t e = LCZ_EBASE(0, 0) + (id - LCZ_EBASE(0, 0)) / 18u * 18u;
                    if (id - e - 8u == q[e + 17u]) continue;   /* END's own level: always 0 */
                }
                ok &= q[id] == v;
            }
            for (i = 0; i < LCZ_PACKED; i++)
                if (i != id && q[i] != p[i] && !(i >= LCZ_EBASE(0, 0) && i < LCZ_OLD_NP &&
                                                 (i - LCZ_EBASE(0, 0)) % 18u >= 8u))
                    ok = 0;
        }
    }
    bad += check("CZ-1 every panel value round-trips through the native bytes, alone", ok);
    bad += check("CZ-1 every edit leaves a valid native tone", sane);
    /* an imported tone: rates off Melodee's 0..99 grid stay exact unless their own value is turned */
    cz_patch_init(cz_patch[tr].raw);
    for (i = 0; i < 8u; i++) cz_patch[tr].raw[CZ_ENV_BASE[0][1] + 2u * i] = (uint8_t)(13u + 7u * i);
    memcpy(before, cz_patch[tr].raw, CZ_BYTES);
    if (cz_ed_put(tr, LCZ_EBASE(0, 0) + 9u, 40u, raw))     /* C1 PIT L2 */
        for (i = 0; i < 8u; i++) kept &= raw[CZ_ENV_BASE[0][1] + 2u * i] == before[CZ_ENV_BASE[0][1] + 2u * i];
    else
        kept = 0;
    for (i = 0; i < 128u; i++)
        if (raw[i] != before[i] && (i < CZ_ENV_BASE[0][0] - 1u || i > CZ_ENV_BASE[0][0] + 15u)) kept = 0;
    bad += check("CZ-1 an edit rewrites its own envelope only; imported rates elsewhere stay verbatim", kept);
    /* the knobs: CZ DETUNE FINE up 5 -> 5; C1 AMP POINT SUS past END -> "-", back down -> END - 1 */
    cz_patch_init(cz_patch[tr].raw);
    cz_track_accept(TSEL);
    go_title("CZ DETUNE");
    turn(EN_K4, 5);
    cz_ed_decode(p, cz_patch[tr].raw);
    bad += check("CZ-1 CZ DETUNE KNOB 4 turns FINE in the track's tone", p[LCZ_FINE] == 5u);
    go_title("C1 AMP POINT");
    cz_ed_decode(p, cz_patch[tr].raw);
    id = LCZ_EBASE(0, 2) + 16u;
    for (i = 0; i < 12u && p[id] != 8u; i++) { turn(EN_K1, 1); cz_ed_decode(p, cz_patch[tr].raw); }
    ok = p[id] == 8u;
    turn(EN_K1, -1);
    cz_ed_decode(p, cz_patch[tr].raw);
    bad += check("CZ-1 SUS turned past END shows -, turned back lands before END",
                 ok && p[id + 1u] && p[id] == p[id + 1u] - 1u);
    /* TOOLS: COMP swaps with the tone before the edits, 1 > 2 copies line 1 over line 2 */
    go_title("CZ TOOLS");
    memcpy(before, cz_patch[tr].raw, CZ_BYTES);
    turn(EN_K4, 1);
    press(B_OCTUP);
    cz_ed_decode(p, cz_patch[tr].raw);
    ok = p[LCZ_FINE] == 0u;
    turn(EN_K4, 1);
    press(B_OCTUP);
    bad += check("CZ-1 TOOLS COMP: the tone as loaded, then the edit again",
                 ok && !memcmp(before, cz_patch[tr].raw, CZ_BYTES));
    cz_patch[tr].raw[14] = 0xA0;                         /* line 1: DBL SINE */
    turn(EN_K2, 1);
    press(B_OCTUP);
    bad += check("CZ-1 TOOLS 1 > 2: line 2 is line 1's block, MOD stays line 1's",
                 !memcmp(cz_patch[tr].raw + 71, cz_patch[tr].raw + 14, 57) && (cz_patch[tr].raw[72] & 0x38u) == 0u);
    turn(EN_K1, 1);
    press(B_OCTUP);
    ok = name_on() && nm.kind == NK_CZ_NAME && str_eq(nm.s, "INIT");
    name_close();
    bad += check("CZ-1 TOOLS NAME opens the tone's 16-character name", ok);
    return bad;
}

/* CZ-1's factory: Casio's 64 preset tones are PRESETS 1..64 (each loads its tone, BANK A..D, PTCH 1..16) and
 * the default BANK A..D (E..H empty); a saved bank wins, "none" in a restore brings the default back */
static int test_cz1_factory(void)
{
    const engine_t *e = ENGINES[ENGI_CZ];
    uint32_t tr, i, ok = 1, n;
    cz_bank_t *b;
    static cz_bank_t mine;
    int bad = 0;
    ui_power_on();
    stop_transport();
    tr = song.sel % NTRK;
    bad += check("CZ-1 PRESETS: INIT TONE, then Casio's 64 CZ-1 tones", e->npresets == 65u && CZ_FACTORY_N == 64u &&
                 str_eq(e->presets[1].name, "BRASS 1") && str_eq(e->presets[64].name, "TYPHOON"));
    for (i = 1; i <= 64u; i++) {
        set_engine_of(TSEL, ENGI_CZ);
        apply_preset_to(TSEL, i);
        ok &= TSEL->preset == i && TSEL->p[P_E0] == (int16_t)((i - 1u) / 16u) && TSEL->p[P_E1] == (int16_t)((i - 1u) % 16u + 1u) &&
              !memcmp(cz_patch[tr].raw, CZ_FACTORY[i - 1u], CZ_BYTES) && cz_patch_valid(cz_patch[tr].raw);
        frame();                                         /* (cz_bank_poll: the tone stays, BANK / PTCH already its) */
        ok &= !memcmp(cz_patch[tr].raw, CZ_FACTORY[i - 1u], CZ_BYTES);
    }
    bad += check("CZ-1 each factory preset loads its native tone and shows its BANK / PTCH", ok);
    bad += check("CZ-1 factory tones keep the CZ-1's own LCD names",
                 !memcmp(CZ_FACTORY[0] + 128, "    BRASS 1     ", 16) && !memcmp(CZ_FACTORY[45] + 128, "AFRO-PERCUSSION ", 16));
    apply_preset_to(TSEL, 0);
    bad += check("CZ-1 INIT TONE is still the init voice", !memcmp(cz_patch[tr].raw + 128, "INIT", 4) && TSEL->p[P_E1] == 0);
    ok = 1;
    for (n = 0; n < 8u; n++) {
        cz_bank_import(n, 0, 0);
        b = cz_bank_load(n);
        ok &= !cz_bank_saved && b->used == (n < 4u ? 0xFFFFu : 0u);
        for (i = 0; n < 4u && i < 16u; i++) ok &= !memcmp(b->tone[i].raw, CZ_FACTORY[n * 16u + i], CZ_BYTES);
    }
    b = cz_bank_load(1);
    bad += check("CZ-1 never-saved BANK A..D hold Casio's A-1 .. H-8, E..H empty", ok && !memcmp(b->name, "CZ-1 C1-D8", 10));
    cz_bank_boot();                                      /* (power-on: persist_boot -> up_boot) */
    set_engine_of(TSEL, ENGI_CZ);
    TSEL->p[P_E0] = 2; TSEL->p[P_E1] = 7; frame();
    bad += check("CZ-1 retired BANK / PTCH values cannot replace the current tone",
                 !memcmp(cz_patch[tr].raw + 128,"INIT",4));
    cz_bank_empty(&mine, 0);
    memcpy(mine.tone[0].raw, CZ_FACTORY[63], CZ_BYTES); mine.used = 1u;
    cz_bank_import(0, (const uint8_t *)&mine, sizeof mine);
    b = cz_bank_load(0);
    ok = cz_bank_saved && b->used == 1u && !memcmp(b->tone[0].raw, CZ_FACTORY[63], CZ_BYTES);
    bad += check("CZ-1 a saved BANK A wins over the factory one", ok);
    cz_bank_import(0, 0, 0);
    bad += check("CZ-1 a restore without BANK A brings Casio's back", cz_bank_load(0)->used == 0xFFFFu && !cz_bank_saved);
    return bad;
}

static int test_edit_cycle(void)
{
    static const char *const CYC_A[] = {"EDIT 1", "EDIT 2", "VOICE", "EDIT 1"};   /* (VOICE: VOICE 1-3's list) */
    static const char *const CYC_F[] = {"EDIT 1", "EDIT 2", "STORE", "ALGO", "FREQ", "OUT", "EG RATE", "EG LVL", "SCALE",
                                        "CURVE", "PITCH EG", "PITCH LV", "FM LFO", "FM BEND", "VOICE", "EDIT 1"};
    static const char *const CYC_D[] = {"EDIT 1", "EDIT 2", "OP1 ENV", "OP2 ENV", "OP3 ENV", "OP4 ENV",
                                        "OP LEVEL", "VOICE", "EDIT 1"};
    int bad = 0, ok;
    uint32_t i;
    ui_power_on();
    btn_down(B_EDIT); frame();
    ok = ui.home;
    btn_up(B_EDIT); frame();
    bad += check("EDIT opens EDIT 1, when let go (not on press)", ok && str_eq(cur_page()->title, "EDIT 1"));
    for (i = 0; i < NPAGES; i++)
        ok &= !str_eq(PAGES[i].title, "ENGINE");
    bad += check("no ENGINE page (engines are the EDIT layer's)", ok);
    set_engine_of(TSEL, 0);
    bad += check("EDIT cycle (ANALOG): EDIT 1 EDIT 2 VOICE VOICE 2 EDIT 1", engine_cycle(CYC_A, NELEM(CYC_A)));
#if MELODEE_LEGACY_EXTRAS                           /* (PHASE: retired) */
    set_engine_of(TSEL, 2);
    TSEL->p[P_E7] = 0;
    bad += check("PHASE LINK keeps the compact EDIT cycle", engine_cycle(CYC_A, NELEM(CYC_A)));
    TSEL->p[P_E7] = 1;
    static const char *const CYC_CZ[] = {"EDIT 1", "EDIT 2", "DCW1 ENV", "DCW2 ENV", "DCA2 ENV", "DCO ENV",
        "CZ LEVEL", "VOICE", "EDIT 1"};
    bad += check("PHASE SPLIT exposes native envelope pages", engine_cycle(CYC_CZ, NELEM(CYC_CZ)));
    go_title("DCW1 ENV"); TSEL->p[P_FM1_ATK] = 0; turn(EN_K1, 1);
    bad += check("PHASE DCW1 knob edits its saved parameter", TSEL->p[P_FM1_ATK] > 0);
#endif
#if MELODEE_FM4
    set_engine_of(TSEL, 1);
    bad += check("EDIT cycle (DIGITAL): EDIT 1 EDIT 2 OP1..OP4 ENV OP LEVEL VOICE VOICE 2 EDIT 1",
                 engine_cycle(CYC_D, NELEM(CYC_D)));
#else
    set_engine_of(TSEL, ENGI_DIGITAL);
    bad += check("EDIT cycle (engine 1 asked for: FM6, DIGITAL retired): EDIT 1 EDIT 2, FM6's 16 pages, VOICE VOICE 2",
                 TSEL->eng_req == ENGI_FM6 && engine_cycle(CYC_F, NELEM(CYC_F)));
    (void)CYC_D;
#endif
    go_title("OP2 ENV"); ui.fam_last[FAM_EDIT] = ui.page;
    set_engine_of(TSEL, 0); frame();
    bad += check("an OP page of a track no longer DIGITAL falls back to EDIT 1", str_eq(cur_page()->title, "EDIT 1"));
    go_title("EDIT 2"); ui.fam_last[FAM_EDIT] = ui.page;
    go_home(); press(B_EDIT);
    bad += check("EDIT remembers its last page (as every family)", str_eq(cur_page()->title, "EDIT 2"));
    return bad;
}

/* SAVE > PROJECT: KNOB 2 BOOT (OFF, A..D: the device's, kept with the settings), SLOT T the template (SAVE keeps the
 * sounds without the patterns, LOAD makes a new project of them on a free slot, no name); power-on (project_boot)
 * loads the BOOT project, else the template; CLK TUNE MIDI ROUT kept as last used (glo_poll, glo_restore) */
static int test_boot_template(void)
{
    int bad = 0, ok;
    uint32_t i;
    char v[16];
    const char *u;
    ui_power_on();
    stop_transport();
    memset(proj_slot, 0, sizeof proj_slot);
    memset(&tmpl, 0, sizeof tmpl);
    settings_boot = 0;
    go_title("PROJECT");
    song.g[G_SLOT] = 1;
    turn(EN_K2, 1);
    sheet_do("Boot");
    bad += check("PROJECT KNOB 2 to B, its sheet's Boot: BOOT B, the device's (song.g untouched), saved", settings_boot == 2u &&
                 song.g[G_BOOT] == 0 && song.g[G_SLOT] == 2);
    turn(EN_K1, 9);
    param_format(&GP[G_SLOT], song.g[G_SLOT], v, &u);
    bad += check("KNOB 1 past D: SLOT TMPL", song.g[G_SLOT] == PROJ_TMPL && str_eq(v, "TMPL"));
    trk[1].p[P_LEVEL] = 77;
    fm6_fn[1][FN_PBUP] = 7;                              /* (track 2's FM6 bend range: kept too) */
    set_engine_of(&trk[2], ENGI_DRUM);
    trk[2].p[P_E0] = 4;                                  /* (KIT 808) */
    sheet_do("Save as template");
    bad += check("SAVE on TMPL: the template, at once (no NAME, no dialog)", template_used() && !name_on() &&
                 ui.confirm == CF_NONE && (msg_is("TEMPLATE SAVED (RAM)") || msg_is("TEMPLATE SAVED")));
    press(B_EDIT);
    bad += check("  EDIT there: no NAME (the template has no name)", !name_on());
    trk[1].p[P_LEVEL] = 20;
    fm6_fn[1][FN_PBUP] = 2;
    set_engine_of(&trk[2], 0);
    trk[0].step[3].time = ST_NOTE; trk[0].step[3].n = 1; trk[0].step[3].note[0] = 60;
    project_save(0);                                     /* (A used: the template's project goes to B) */
    sheet_do("Load template");
    ok = trk[1].p[P_LEVEL] == 77 && trk[2].eng_req == ENGI_DRUM && trk[2].p[P_E0] == 4 && fm6_fn[1][FN_PBUP] == 7u;
    for (i = 0; i < NTRK; i++)
        ok &= seq_is_empty(&trk[i]);
    bad += check("LOAD on TMPL: its sounds, every pattern empty, SLOT on a free slot (B)", ok &&
                 msg_is("TEMPLATE LOADED") && song.g[G_SLOT] == 2 && proj_cur == PROJ_NO_SLOT);
    trk[1].p[P_LEVEL] = 30;
    project_save(1);                                     /* B: LEVEL 30 */
    trk[1].p[P_LEVEL] = 99;
    settings_boot = 2;
    bootguard.failed = 0;
    project_boot();
    bad += check("power-on, BOOT B: project B, SLOT B", trk[1].p[P_LEVEL] == 30 && song.g[G_SLOT] == 2 && proj_cur == 1u);
    settings_boot = 3;                                   /* C: empty */
    project_boot();
    bad += check("  BOOT C empty: the template, SLOT on a free slot (C)", trk[1].p[P_LEVEL] == 77 && song.g[G_SLOT] == 3);
    trk[1].p[P_LEVEL] = 99;
    bootguard.failed = 1;
    settings_boot = 2;
    project_boot();
    bad += check("  a boot that crashed before: the project skipped (BOOT PROJECT SKIPPED)", trk[1].p[P_LEVEL] == 99 &&
                 msg_is("BOOT PROJECT SKIPPED"));
    bootguard.failed = 0;
    {   /* the settings keep BOOT and the kept GLO values; the template follows them in the record */
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        song.g[G_TUNE] = 7; song.g[G_ROUTE] = 1;
        song.playing = 0;
        glo_poll();
        ok = settings_glo[1] == 7 && settings_glo[3] == 1;
        settings_export(&p);
        settings_boot = 0; memset(settings_glo, 0, sizeof settings_glo); song.g[G_TUNE] = 0; song.g[G_ROUTE] = 0;
        ok &= settings_import(&p, sizeof p) == 1 && settings_boot == 2u && settings_glo[1] == 7;
        glo_restore();
        bad += check("BOOT and CLK TUNE MIDI ROUT kept with the settings, restored at power-on", ok &&
                     song.g[G_TUNE] == 7 && song.g[G_ROUTE] == 1);
    }
    settings_boot = 0;
    memset(&tmpl, 0, sizeof tmpl);
    memset(proj_slot, 0, sizeof proj_slot);
    return bad;
}

/* BPM and SWG: SEQ > TEMPO, the project's (saved and loaded with it); SELECT never sets the BPM: it turns the open
 * section's pages both ways (wrapping) and moves the cursor on STEP */
static int test_tempo_select(void)
{
    int bad = 0, ok;
    ui_power_on();
    stop_transport();
    go_title("GLOBAL");
    turn(EN_SELECT, 1);
    ok = song.g[G_BPM] == 120 && str_eq(cur_page()->title, "SYSTEM");
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "GLOBAL");
    turn(EN_SELECT, -1);
    bad += check("SELECT on GLO: GLOBAL -> SYSTEM -> GLOBAL (wraps), back the other way; the BPM untouched",
                 ok && str_eq(cur_page()->title, "SYSTEM") && song.g[G_BPM] == 120);
    go_title("NOTES");
    cursor_set(4);
    turn(EN_SELECT, 1);
    ok = !str_eq(cur_page()->title, "NOTES") && ui.cursor == 4u;
    turn(EN_SELECT, -1);
    turn(EN_K1, 3);
    bad += check("SELECT on NOTES turns the page (and back); KNOB 1 moves through the steps",
                 ok && ui.cursor == 7u && str_eq(cur_page()->title, "NOTES"));
    go_title("TEMPO");
    turn(EN_K1, 4);
    turn(EN_K2, 10);
    ok = song.g[G_BPM] == 124 && song.g[G_SWING] == 10 && cur_page()->fam == FAM_SEQ;
    memset(proj_slot, 0, sizeof proj_slot);
    memset(proj_bank_slot, 0, sizeof proj_bank_slot);
    project_save(2);
    song.g[G_BPM] = 90;
    song.g[G_SWING] = 0;
    project_load(2);
    bad += check("SEQ > TEMPO: BPM and SWG, saved and loaded with the project", ok && song.g[G_BPM] == 124 &&
                 song.g[G_SWING] == 10);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(proj_bank_slot, 0, sizeof proj_bank_slot);
    return bad;
}

/* GLO > SYSTEM KNOB 1: the MIDI column shows USB's or TRS's status (both inputs play), RX for 250 ms after input */
static int test_midi_status(void)
{
    int bad = 0, ok;
    ui_power_on();
    go_title("SYSTEM");
    song.g[G_MIDI] = 0;
    turn(EN_K1, 1);
    ok = song.g[G_MIDI] == 1;
    turn(EN_K1, -1);
    bad += check("SYSTEM KNOB 1: TRS's status, then USB's", ok && song.g[G_MIDI] == 0);
    midi_rx_recent(0);                                   /* (what came before: seen) */
    fm1_ms += 1000;
    ok = !midi_rx_recent(0);
    usb.rx_pkts++;
    ok &= midi_rx_recent(0);
    fm1_ms += 300;
    bad += check("  USB RX for 250 ms after a packet", ok && !midi_rx_recent(0));
    return bad;
}

/* SAVE + REC: the project back to its slot at once ("SAVED B"); playing: stopped first, then saved; a new project:
 * PROJECT on a free slot, SAVE picked; neither button does its own thing (no page, no undo, no arming) */
static int test_quick_save(void)
{
    int bad = 0, i;
    ui_power_on();
    stop_transport();
    memset(proj_slot, 0, sizeof proj_slot);
    memset(proj_bank_slot, 0, sizeof proj_bank_slot);
    project_save(1);
    trk[0].p[P_LEVEL] = 55;
    btn_down(B_SAVE); frame();
    btn_down(B_REC); frame();
    btn_up(B_REC); btn_up(B_SAVE); frame();
    bad += check("SAVE + REC: back into B at once, SAVED B; no SAVE page, no arming", stored_param(1, 0, P_LEVEL) == 55 &&
                 msg_is("SAVED B") && ui.home && !song.rec);
    trk[0].p[P_LEVEL] = 66;
    song.playing = 1;
    btn_down(B_REC); frame();
    btn_down(B_SAVE); frame();
    btn_up(B_SAVE); btn_up(B_REC); frame();
    bad += check("  REC + SAVE while playing: the transport stops first", transport_req == 2u && stored_param(1, 0, P_LEVEL) == 55);
    song.playing = 0;
    transport_req = 0;
    for (i = 0; i < 3; i++)
        frame();
    bad += check("  .. then the save", stored_param(1, 0, P_LEVEL) == 66 && msg_is("SAVED B"));
    project_load(1);
    proj_cur = PROJ_NO_SLOT;
    btn_down(B_SAVE); frame();
    btn_down(B_REC); frame();
    btn_up(B_REC); btn_up(B_SAVE); frame();
    bad += check("  a new project: PROJECT on a free slot (A), SAVE picked", !ui.home && cur_page()->graph == GR_SLOTS &&
                 song.g[G_SLOT] == 1 && ui.act == 4u && msg_is("NEW PROJECT: PICK SLOT"));
    memset(proj_slot, 0, sizeof proj_slot);
    memset(proj_bank_slot, 0, sizeof proj_bank_slot);
    return bad;
}

/* REC + PLAY: armed and playing at once, REC's release no tap (no disarm); REC held: Capture */
static int test_rec_gestures(void)
{
    int bad = 0;
    ui_power_on();
    stop_transport();
    song.rec = 0;
    btn_down(B_REC); frame();
    press(B_PLAY);
    btn_up(B_REC); frame();
    bad += check("REC + PLAY: the track armed, the transport starting, REC's release no disarm",
                 (song.rec & 1u) && transport_req == 1u && msg_is("RECORDING"));
    stop_transport();
    transport_req = 0;
    song.rec = 0;
    hold(B_REC);
    bad += check("REC held: Capture, nothing armed", !song.rec && !transport_req && msg_is("NOTHING TO CAPTURE"));
    go_home();
    press(B_REC);
    bad += check("REC tapped: armed, transport remains stopped", (song.rec & 1u) && transport_req == 0u && ui.home);
    stop_transport();
    transport_req = 0;
    song.rec = 0;
    return bad;
}

/* LIGHTS (menu): the keys that play glow (QNT OFF: the scale's notes; a layout: the keys not silent; a kit: every
 * key), a key sounding is bright (pressed, or its note held by MIDI on the track), the idle buttons glow; OFF: as 1.0 */
static uint32_t key_light(uint32_t id)                   /* 2 bright, 1 dim, 0 dark (led id: key k = 14 + k) */
{
    uint8_t q = led_pos[id];
    if (q == 0xFF)
        return 0;
    return (fm1_led[q >> 3] >> (q & 7u)) & 1u ? 2u : (fm1_led_dim[0][q >> 3] >> (q & 7u)) & 1u ? 1u : 0u;
}
static int test_key_lights(void)
{
    int bad = 0, ok;
    uint32_t k, c = 60u - 53u, cs = 61u - 53u;          /* the keys C4 and C#4 */
    ui_power_on();
    ui_leds();                                           /* (its led_pos_init first: then a map of the test's own, */
    for (k = 0; k < 41u; k++)                            /* the host's FM1_KEYMAP being empty) */
        led_pos[k] = (uint8_t)((k % FM1_NCOL) << 3 | (1u + k / FM1_NCOL));
    settings_lights = LIGHTS_MID;
    TSEL->p[P_QUANT] = Q_OFF;
    TSEL->p[P_ROOT] = 0;
    TSEL->p[P_SCALE] = 1;                                /* (C major) */
    frame(); ui_leds();
    bad += check("LIGHTS MID, QNT OFF C MAJ: C glows, C# dark, the dim plane at 1/4, idle buttons glow",
                 key_light(14u + c) == 1u && !key_light(14u + cs) && fm1_led_dim_mask[0] == 3u &&
                 ((fm1_led_dim[1][led_pos[panel.btn[B_LFO]] >> 3] >> (led_pos[panel.btn[B_LFO]] & 7u)) & 1u));
    fm1_in.notes |= 1u << cs;
    frame(); ui_leds();
    bad += check("  a key held bright, out of the scale too", key_light(14u + cs) == 2u);
    fm1_in.notes = 0;
    midi_event(0x90, 0, 64, 100);
    frame(); ui_leds();
    bad += check("  MIDI holding E4 on the track: its key bright", key_light(14u + 64u - 53u) == 2u);
    midi_event(0x80, 0, 64, 0);
    frame(); ui_leds();
    bad += check("  .. and back to its glow when it is let go", key_light(14u + 64u - 53u) == 1u);
    TSEL->p[P_QUANT] = Q_WHITE;
    frame(); ui_leds();
    for (k = 0, ok = 1; k < 27u; k++)
        ok &= key_light(14u + k) == (key_black(k) ? 0u : 1u);
    bad += check("QNT WHITE: the white keys glow, the black ones (silent) dark", ok);
    set_engine_of(TSEL, ENGI_DRUM);
    frame(); ui_leds();
    for (k = 0, ok = 1; k < 27u; k++)
        ok &= key_light(14u + k) == 1u;
    bad += check("a DRUM kit: every key glows", ok);
    settings_lights = LIGHTS_OFF;
    frame(); ui_leds();
    for (k = 0, ok = 1; k < 27u; k++)
        ok &= !key_light(14u + k);
    for (k = 0; k < FM1_NCOL; k++)
        ok &= !fm1_led_dim[1][k];
    bad += check("LIGHTS OFF: no glow, keys or buttons (as Felucca 1.0)", ok);
    settings_lights = LIGHTS_FULL;
    frame(); ui_leds();
    bad += check("LIGHTS FULL: the dim plane every frame", fm1_led_dim_mask[0] == 0u && key_light(14u) == 1u);
    {   /* kept with the settings */
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        settings_export(&p); settings_lights = LIGHTS_MID;
        bad += check("  LIGHTS kept with the settings", settings_import(&p, sizeof p) && settings_lights == LIGHTS_FULL);
    }
    settings_lights = LIGHTS_MID;
    led_pos_init();
    return bad;
}

/* FM6's pages (params.c fm6_page_desc / fm6_page_put): the PRESETS knob picks the operator of the operator pages, a
 * knob edits that operator in the track's patch, ON switches it (fm6_on), the function settings are the track's and
 * go with the project; STORE writes the patch into bank slot KNOB 1 and PTCH follows it, not while playing; INIT */
static int test_fm6_pages(void)
{
    int bad = 0;
    uint32_t tr;
    uint8_t *op2, crs;
    char a[12], b[12];
    uint8_t pk[FM6_PACKED], v[FP_SIZE + 1u];
    ui_power_on();
    stop_transport();
    set_engine_of(TSEL, ENGI_FM6);
    tr = song.sel;
    ui.home = 1;
    memcpy(v, fm6_patch[tr], FP_SIZE);
    memcpy(v + FP_NAME, "MY VOICE  ", 10);
    fm6_set_patch(tr, v);
    TSEL->p[P_E6] = 0;
    turn(EN_K4, 20);
    bad += check("FM6 home knob 4 changes DTUN and keeps the imported voice",
                 TSEL->p[P_E6] == 20 && !TSEL->p[P_E7] && !memcmp(v, fm6_patch[tr], FP_SIZE));
    turn(EN_K4, -20);
    bad += check("FM6 home knob 4 returns DTUN to neutral without replacing the voice",
                 !TSEL->p[P_E6] && !memcmp(v, fm6_patch[tr], FP_SIZE));
    go_title("FREQ");
    fm6_opsel = 0;
    turn(EN_PRESET, 1);
    bad += check("FM6 FREQ: PRESETS picks the operator (OP2)", fm6_opsel == 1u && TSEL->eng_req == ENGI_FM6);
    op2 = &fm6_patch[tr][4u * FP_OP];                    /* (the patch keeps operator 6 first) */
    crs = op2[FP_FC];
    turn(EN_K1, crs < 20u ? 2 : -2);
    bad += check("  KNOB 1: OP2's CRS in the patch", op2[FP_FC] == (uint8_t)(crs < 20u ? crs + 2u : crs - 2u));
    go_title("OUT");
    turn(EN_K4, -1);
    bad += check("  OUT ON off: OP2's switch (fm6_on bit 4)", !((fm6_on[tr] >> 4) & 1u) && ((fm6_on[tr] >> 5) & 1u));
    turn(EN_K4, 1);
    bad += check("  .. and on again", (fm6_on[tr] & FM6_ON_ALL) == FM6_ON_ALL);
    go_title("FM BEND"); frame();                        /* (FM6's controllers: one list, ui_list.c) */
    fm6_fn_reset();
    turn(EN_K2, 7); turn(EN_K1, 1);                      /* PORTA ENGINE: row 8 */
    turn(EN_K2, -2); turn(EN_K1, 9);                     /* PORTA TIME: row 6 */
    bad += check("FM PORTA ENGINE, TIME: the track's own (another FM6 track keeps Dexed's)",
                 fm6_fn[tr][FN_ENGINE] == FM6_MARK1 + 1u && fm6_fn[tr][FN_PTIME] == 9u &&
                 fm6_fn[(tr + 1u) % NTRK][FN_ENGINE] == FM6_MARK1 && !fm6_fn[(tr + 1u) % NTRK][FN_PTIME]);
    project_save(3);
    fm6_fn_reset();
    project_load(3);
    bad += check("  saved with the project, back with it", fm6_fn[tr][FN_ENGINE] == FM6_MARK1 + 1u &&
                 fm6_fn[tr][FN_PTIME] == 9u);
    {   /* a project saved before (no function settings in its record): Dexed's */
        uint8_t *raw = proj_bank_slot[3];
        memset(raw + BANK_FN_OFF, 0, 4u + NTRK * FM6_NFN);
        bank_checksum(raw);
        project_load(3);
        bad += check("  a project of before: Dexed's", fm6_fn[tr][FN_ENGINE] == FM6_MARK1 && !fm6_fn[tr][FN_PTIME]);
    }
    fm6_fn_reset();
    go_title("STORE");
    turn(EN_K1, 4);
    sheet_do("Save here");
    fm6_name(a, fm6_patch[tr]);
    b[0] = 0;
    if (native_used(ENGI_FM6,4)) {
        memcpy(pk,native_raw(ENGI_FM6,4),FM6_PACKED);
        fm6_unpack(pk, v);
        fm6_name(b, v);
    }
    bad += check("STORE onto F005: native voice in its own preset slot", fm6_bslot == 4u && native_used(ENGI_FM6,4) && str_eq(a, b) &&
                 TSEL->p[P_E7] == 0);
    song.playing = 1;
    ui.msg_t = 0;
    turn(EN_K1, 1);
    sheet_do("Save here");
    bad += check("  STORE while playing: STOP TO SAVE, U06 empty", msg_is("STOP TO SAVE") && !up_used(5) &&
                 TSEL->p[P_E7] == 0);
    stop_transport();
    sheet_do("Init sound");
    fm6_name(a, fm6_patch[tr]);
    fm6_unpack(FM6_INIT, v);
    fm6_name(b, v);
    bad += check("  INIT: the init voice", msg_is("INIT VOICE") && str_eq(a, b));
    set_engine_of(TSEL, 0);
    frame();
    bad += check("an FM6 page of a track no longer FM6 falls back to EDIT 1", str_eq(cur_page()->title, "EDIT 1"));
    return bad;
}

#if MELODEE_SLICE
/* SLICES (EDIT family, a SLICE track: ui_slice.c; the MAN slices of eng_slice.c, ported from hugelton/Felucca#27 by
 * andreahaku): in the EDIT cycle after EDIT 2 on SLICE only; BREAK shows its slices, edits need a user slot; the first
 * edit takes the slices shown (8 equal) as MAN and sets DIV MAN; KNOB 1 the marker (then END), KNOB 2 moves it,
 * KNOB 3 / 4 pick SPLIT / JOIN and OCT+ does it (it stays picked), OCT- drops the pick, then goes HOME; a key picks
 * its slice and the keys of the selected slice are lit; the slot is marked for the store; another engine: EDIT 1 */
static void host_slot_make(uint32_t k)                /* USR k + 1: 2 s at 22.05 kHz, a noise burst every 0.25 s */
{
    smp_user_hdr_t *h = (smp_user_hdr_t *)((uint8_t *)host_slots + k * SMP_USER_SIZE);
    uint8_t *d = (uint8_t *)h + SMP_USER_DATA;
    uint32_t n = 44100u, i, r = 12345u;
    memset(h, 0, SMP_USER_SIZE);
    h->magic = SMP_USER_MAGIC;
    h->version = 1;
    h->nz = 1;
    memcpy(h->name, "TEST", 4);
    h->data_len = n / 2u;
    for (i = 0; i < n / 2u; i++) {                   /* (IMA codes: a burst of random ones, then +-1/8 steps) */
        r = r * 1103515245u + 12345u;
        d[i] = i % 2756u < 400u ? (uint8_t)(r >> 16) : 0x80u;
    }
    h->zone[0].n = n;
    h->zone[0].le = n - 1u;
    h->zone[0].rate = 32768u;                        /* 22050 / 44100, Q16 */
    h->zone[0].root16 = 60 * 16;
    h->zone[0].hi = 127;
}
static int test_slices(void)
{
    static const char *const CYC_S[] = {"EDIT 1", "EDIT 2", "SLICES", "VOICE", "EDIT 1"};
    int bad = 0, ok;
    uint32_t n, j, p0, k, note;
    ui_power_on();
    set_engine_of(TSEL, 13u);                        /* SLICE CHOP: BREAK, 16 */
    bad += check("SLICES: SLICE's EDIT cycle EDIT 1 EDIT 2 SLICES VOICE VOICE 2 EDIT 1", engine_cycle(CYC_S, NELEM(CYC_S)));
    go_page(GR_SLICES); frame();
    n = slice_count();
    p0 = TSEL->p[P_E1];
    turn(EN_K1, 2); turn(EN_K2, 3);
    ok = n == 16u && slice_sel() == 2u && TSEL->p[P_E1] == p0 && msg_is("SRC USR1-3 TO EDIT") && !slice_act_ready(2);
    bad += check("SLICES on BREAK: its 16 slices shown, KNOB 1 picks; edits need USR1-3 (DIV unchanged)", ok);
    TSEL->p[P_E0] = 2;                               /* SRC USR2, empty: BREAK's slices, the slot named */
    frame();
    turn(EN_K2, 1);
    ok = slice_count() == 16u && TSEL->p[P_E1] == p0 && msg_is("USR2 EMPTY");
    TSEL->p[P_E0] = 0;
    bad += check("SLICES on an empty USR2: \"USR2 EMPTY\" (DIV unchanged)", ok);

    host_slot_make(0);
    smp_user_scan(0);
    TSEL->p[P_E0] = 1;                               /* SRC USR1, DIV 8 */
    TSEL->p[P_E1] = 1;
    frame();
    n = slice_count();
    p0 = slice_mark(2);
    turn(EN_K2, 5);
    ok = n == 8u && TSEL->p[P_E1] == SLC_DIV_MAN && msg_is("DIV MAN") && slice_count() == 8u && slice_sel() == 2u &&
         slice_mark(2) > p0 && slc_man_of(slc_get(1)) && (slc_man_save & 1u);
    bad += check("SLICES on USR1: the first KNOB 2 turn takes the 8 slices as MAN (DIV MAN), moves slice 3's start", ok);
    turn(EN_K3, 1);
    ok = ui.act == 3u && act_ready();
    press(B_OCTUP);
    ok &= slice_count() == 9u && slice_sel() == 3u && ui.act == 3u;
    press(B_OCTUP);
    ok &= slice_count() == 10u && slice_sel() == 4u;
    bad += check("SLICES: KNOB 3 picks SPLIT, OCT+ splits the slice (again: it stays picked)", ok);
    turn(EN_K4, 1);
    ok = ui.act == 4u && act_ready();
    press(B_OCTUP);
    ok &= slice_count() == 9u && slice_sel() == 3u;
    turn(EN_K1, -20);
    ok &= slice_sel() == 0u && !act_ready();
    press(B_OCTUP);
    ok &= slice_count() == 9u && msg_is("FIRST SLICE");
    bad += check("SLICES: KNOB 4 picks JOIN, OCT+ joins to the slice before; not the first slice", ok);
    turn(EN_K1, 20);
    p0 = slice_mark(slice_count());
    turn(EN_K2, -4);
    ok = slice_sel() == slice_count() && slice_mark(slice_count()) < p0 && !act_ready();
    bad += check("SLICES: after the last slice the END marker: KNOB 2 trims the tail", ok);
    {   /* DIV set back to a grid: the first touch shows the slot's MAN slices (DIV MAN), it does not replace them */
        slc_man_t keep = *slc_man_of(slc_get(1));
        TSEL->p[P_E1] = 2;                           /* DIV 16 */
        frame();
        turn(EN_K1, -1); turn(EN_K2, 1);
        ok = TSEL->p[P_E1] == SLC_DIV_MAN && msg_is("DIV MAN") && slc_man_of(slc_get(1))->n == keep.n &&
             !memcmp(slc_man_of(slc_get(1))->pos, keep.pos, keep.n * sizeof keep.pos[0]);
        turn(EN_K1, -20); turn(EN_K2, 1);            /* then KNOB 2 edits that table */
        ok &= slc_man_of(slc_get(1))->n == keep.n && memcmp(slc_man_of(slc_get(1))->pos, keep.pos, keep.n * sizeof keep.pos[0]);
        bad += check("SLICES: DIV 16 over a slot's MAN slices: KNOB 2 goes back to them (DIV MAN), keeps them", ok);
    }

    note = 5u;                                       /* (the 6th key: SLICE maps every key) */
    key_down(note); frame(); key_up(note); frame();
    j = slc_note_slice(TSEL->p, kb_map(TSEL, note), slice_count());
    ok = slice_sel() == j && ((slice_leds() >> note) & 1u);
    for (k = 0; k < 27u; k++)
        ok &= ((slice_leds() >> k) & 1u) == (slc_note_slice(TSEL->p, kb_map(TSEL, k), slice_count()) == j);
    bad += check("SLICES: a key picks the slice it plays; the keys of the selected slice are lit", ok);
    btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();
    ok = ui.act == 0u && !ui.home;
    btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();
    ok &= ui.home;
    bad += check("SLICES: OCT- drops the pick, then goes HOME", ok);
    go_page(GR_SLICES); frame();
    set_engine_of(TSEL, 0); frame();
    bad += check("SLICES: on another engine the page is not there (EDIT 1)", str_eq(cur_page()->title, "EDIT 1"));
    {   /* the engine changed (the editor's SET between two ui_input passes) while the page is still the current one:
         * its knobs and actions do nothing to the track or the slot */
        track_t before;
        slc_man_t keep;
        set_engine_of(TSEL, 13u);
        TSEL->p[P_E0] = 1;
        TSEL->p[P_E1] = SLC_DIV_MAN;
        go_page(GR_SLICES); frame();
        keep = *slc_man_of(slc_get(1));
        set_engine(3u);                              /* SID, as ed_service's SET G_ENGSEL */
        TSEL->p[P_E0] = 1;
        before = *TSEL;
        slc_man_save = 0;
        host_enc[panel.enc[EN_K2]] += 2 * panel.dir[EN_K2];
        host_ticks += 16000u; fm1_ms += 16u;
        ui_input();                                  /* (no ui_draw yet) */
        ok = cur_page()->graph == GR_SLICES && !slice_page_ok() && !act_cols();
        ui.act = 3u;
        act_do();
        ui.act = 0;
        ok &= !memcmp(&before, TSEL, sizeof before) && !slc_man_save && slc_man_of(slc_get(1))->n == keep.n &&
              !memcmp(slc_man_of(slc_get(1))->pos, keep.pos, keep.n * sizeof keep.pos[0]);
        bad += check("SLICES: left by an engine change not yet drawn: KNOB 2 and OCT+ change nothing", ok);
        frame();
    }
    {   /* another sample of the same length uploaded into the slot: the waveform is decoded again */
        int8_t lo0[SP_COLS], hi0[SP_COLS];
        uint8_t *d = (uint8_t *)host_slots + SMP_USER_DATA;
        smp_user_hdr_t *h = (smp_user_hdr_t *)host_slots;
        set_engine_of(TSEL, 13u);
        TSEL->p[P_E0] = 1;
        TSEL->p[P_E1] = 2;
        go_page(GR_SLICES); ui.force = 1; frame();
        memcpy(lo0, sp.lo, sizeof lo0);
        memcpy(hi0, sp.hi, sizeof hi0);
        for (k = 0; k < h->data_len; k++)
            d[k] = 0x80u;                            /* silence, the same length */
        h->crc ^= 1u;
        smp_user_scan(0);                            /* (the upload's END) */
        ui.force = 1; frame();
        ok = memcmp(lo0, sp.lo, sizeof lo0) || memcmp(hi0, sp.hi, sizeof hi0);
        for (k = 0; k < SP_COLS; k++)
            ok &= sp.lo[k] >= -1 && sp.hi[k] <= 1;   /* (near silence) */
        bad += check("SLICES: a re-upload of the same length redraws the waveform", ok);
        set_engine_of(TSEL, 0); frame();
    }
    memset(host_slots, 0, sizeof host_slots);        /* (the other tests: empty slots) */
    smp_user_scan(0);
    slc_man_save = 0;
    return bad;
}
#endif

/* ------------------------------------------------ the GLO SCL EDIT layers --- */
static uint32_t black(uint32_t b) { return key_at(1, b); }
static void lay_combo(uint32_t btn, uint32_t k) { btn_down(btn); key_down(k); frame(); }
static void oct_back(void) { btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame(); }
static uint32_t leds_at(uint32_t ms) { fm1_ms = ms; return layer_leds(); }

static int test_quick_layers(void)
{
    static const uint8_t LB[3] = {B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[3] = {LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    static const char *const HINT[3] = {"HOLD [GLO] QUICK", "HOLD [SCL] QUICK", "HOLD [EDIT] QUICK"};
    int bad = 0, ok, flash, hint;
    uint32_t i, k, a, b2, mo;
    track_t before;
    /* tap / peek / combo, the hint once */
    ok = 1; hint = 1;
    for (i = 0; i < 3u; i++) {
        ui_power_on();
        btn_down(LB[i]);
        for (flash = 0, k = 0; k < 20u; k++) { frame(); flash |= ui.layer; }      /* 0.32 s */
        btn_up(LB[i]); frame();
        ok &= !flash && !ui.home && (LB[i] == B_GLO ? cur_page()->graph == GR_TRK :
                                     LB[i] == B_SCL ? cur_page()->fam == FAM_SCL : str_eq(cur_page()->title, "EDIT 1"));
        hint &= msg_is(HINT[i]);
        go_home(); frame();
        btn_down(LB[i]); frames(368);
        ok &= !ui.layer;
        frames(64);
        ok &= ui.layer == LL[i] && ((layer_seen >> LL[i]) & 1u);
        btn_up(LB[i]); frame();
        ok &= ui.home && !ui.layer;
        ui.msg_t = 0;
        press(LB[i]);
        hint &= !msg_is(HINT[i]);
        go_home(); frame();
        btn_down(LB[i]); key_down(white(9)); frame();
        ok &= ui.layer == LL[i] && !gates();
        key_up(white(9)); btn_up(LB[i]); frame();
        ok &= ui.home && !ui.layer;
    }
    bad += check("GLO SCL EDIT: a tap opens the page, held past HOLD the map (no page), a key: the map at once", ok);
    bad += check("  after a tap \"HOLD [BTN] QUICK\" until the layer has been opened once", hint);
    {
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        layer_seen = 0x1Cu;
        settings_export(&p); layer_seen = 0;
        bad += check("  the seen bits are kept with the settings (a spare favorites byte)",
                     settings_import(&p, sizeof p) && layer_seen == 0x1Cu);
    }

    /* GLO: mutes latch (lit = sounding), SOLO while held, UNMUTE ALL, TAP, levels, OCT- */
    ui_power_on();
    usb.config = 1; mo = mo_w;
    lay_combo(B_GLO, black(1));
    ok = trk[1].p[P_MUTE] == 1 && ui.layer == LAYER_GLO && !gates() && mo_w == mo;
    a = leds_at(0); b2 = leds_at(250);
    ok &= ((a & b2) >> black(0)) & 1u && !(((a | b2) >> black(1)) & 1u);           /* T1 sounding lit, T2 muted dark */
    ok &= ((a ^ b2) >> white(0)) & 1u && ((a ^ b2) >> white(7)) & 1u && !(((a | b2) >> white(5)) & 1u);
    key_up(black(1)); btn_up(B_GLO); frame();
    bad += check("GLO + black key 2: T2 MUTE latched (SET), silent, no MIDI; LEDs: sounding lit, muted dark", ok &&
                 trk[1].p[P_MUTE] == 1 && !ui.layer);
    lay_combo(B_GLO, black(1)); key_up(black(1)); frame();
    key_down(white(4)); key_up(white(4)); frame();
    lay_combo(B_GLO, black(2)); key_up(black(2)); frame();
    ok = trk[1].p[P_MUTE] == 0 && trk[2].p[P_MUTE] == 1;
    key_down(white(4)); frame(); key_up(white(4)); frame();
    btn_up(B_GLO); frame();
    bad += check("  again: unmuted; C4: UNMUTE ALL", ok && !trk[2].p[P_MUTE] && !trk[1].p[P_MUTE]);
    lay_combo(B_GLO, white(2));
    perf_begin(CTL);
    ok = perf_solo == 4u && ((perf_act >> PF_M1) & 15u) == 0xBu;   /* T3 solo: T1 T2 T4 muted */
    btn_up(B_GLO); frame();
    perf_begin(CTL);
    ok &= perf_solo == 4u && ui.layer == LAYER_GLO;                 /* GLO let go first: the solo stays with its key */
    key_up(white(2)); frame();
    perf_begin(CTL);
    bad += check("GLO + A3 held: SOLO T3 (the others muted), lasts while the key is held, not P_MUTE",
                 ok && !perf_solo && !((perf_act >> PF_M1) & 15u) && !trk[0].p[P_MUTE] && !ui.layer);
    song.g[G_BPM] = 100;
    btn_down(B_GLO); frame();
    for (i = 0; i < 3u; i++) { key_down(white(7)); frame(); key_up(white(7)); frames(480); }
    ok = song.g[G_BPM] == 120;
    btn_up(B_GLO); frame();
    song.g[G_CLOCK] = 1;
    lay_combo(B_GLO, white(7)); key_up(white(7)); frame();
    ok &= msg_is("TAP: CLK IS EXT") && song.g[G_BPM] == 120;
    btn_up(B_GLO); frame(); song.g[G_CLOCK] = 0;
    bad += check("GLO + F4 x3 at 0.5 s: TAP 120 BPM; with CLK EXT: dimmed, it says why", ok);
    {
        int16_t l2 = trk[2].p[P_LEVEL], atk = TSEL->p[P_ATK];
        go_title("ENV");
        btn_down(B_GLO); frame();
        turn(EN_K3, -10);
        ok = trk[2].p[P_LEVEL] == l2 - 10 && TSEL->p[P_ATK] == atk && ui.layer == LAYER_GLO;
        key_down(black(0)); frame(); key_up(black(0)); frame();
        ok &= trk[0].p[P_MUTE] == 1 && song.octave == 0;
        oct_back();
        ok &= trk[2].p[P_LEVEL] == l2 && !trk[0].p[P_MUTE] && song.octave == 0 && ui.layer == LAYER_GLO;
        btn_up(B_GLO); frame();
        bad += check("GLO KNOB 3: T3 LEVEL from any page; OCT-: mutes and levels as it opened, no octave", ok &&
                     str_eq(cur_page()->title, "ENV"));
    }
    song.playing = 1; transport_req = 0;
    btn_down(B_GLO); frame(); press(B_PLAY);
    ok = transport_req == 3u;
    events_block(CTL);
    ok &= song.playing && transport_req == 0u;
    btn_up(B_GLO); frame();
    stop_transport();
    btn_down(B_GLO); frame(); press(B_PLAY);
    ok &= transport_req == 1u;
    btn_up(B_GLO); frame(); transport_req = 0;
    bad += check("GLO + PLAY: RESTART playing (from the top, not stopped); stopped: PLAY", ok);
    usb.config = 0;

    /* SCL: a key is ROOT, KNOB 2 the scale, the LEDs, OCT- */
    ui_power_on();
    TSEL->p[P_ROOT] = 0; TSEL->p[P_SCALE] = 1;                     /* C MAJ */
    lay_combo(B_SCL, black(4));                                    /* D#4 */
    ok = TSEL->p[P_ROOT] == 3 && ui.layer == LAYER_SCL && !gates();
    key_up(black(4)); frame();
    key_down(white(1)); key_up(white(1)); frame();                  /* G3 */
    ok &= TSEL->p[P_ROOT] == 7;
    turn(EN_K2, 1);
    ok &= TSEL->p[P_SCALE] == 2;
    a = leds_at(0); b2 = leds_at(250);
    ok &= ((a & b2) >> white(1)) & 1u && ((a & b2) >> white(8)) & 1u;   /* G3 G4: the root, lit */
    ok &= ((a ^ b2) >> white(2)) & 1u && ((a ^ b2) >> black(4)) & 1u;   /* A, A# (G minor): blink */
    ok &= !(((a | b2) >> black(0)) & 1u);                               /* F#: not in G minor, dark */
    btn_up(B_SCL); frame();
    bad += check("SCL + D#4, then G3: ROOT D#, G (latched); KNOB 2: SCL; LEDs: root lit, the scale blinks", ok &&
                 TSEL->p[P_ROOT] == 7 && TSEL->p[P_SCALE] == 2 && ui.home);
    btn_down(B_SCL); frame(); key_down(white(4)); key_up(white(4)); frame(); turn(EN_K2, 3);
    ok = TSEL->p[P_ROOT] == 0 && TSEL->p[P_SCALE] == 5;
    oct_back();
    btn_up(B_SCL); frame();
    bad += check("  OCT- in SCL: ROOT and SCL as the layer opened", ok && TSEL->p[P_ROOT] == 7 && TSEL->p[P_SCALE] == 2);

    /* EDIT: the sound's sections on the white keys, FM6's operators on the first six black keys, then INIT FAV UNDO
     * STORE (ui_layer.c, ui_sections.c); KNOB 1 the section, 2 the sound, 3 FAV; the engine only in the browser */
    ui_power_on();
    set_engine_of(TSEL, ENGI_PROPHET); go_home(); frame();
    my_steps(TSEL); song.playing = 1;
    before = *TSEL;
    sec_build();
    for (k = 0; k < 16u && !str_eq(sec.name[ly_white_sec(k) % SEC_MAX], "Filter"); k++)
        ;
    lay_combo(B_EDIT, white(k));                        /* (Prophet: Sound Osc Mixer Filter ..) */
    ok = k < 16u && ui.layer == LAYER_EDIT && !ui.home && cur_page()->fam == FAM_EDIT &&
         str_eq(sec.name[sec_of(ui.page)], "Filter");
    key_up(white(k)); frame();
    ok &= TSEL->eng_req == ENGI_PROPHET && !memcmp(TSEL->step, before.step, sizeof before.step);
    btn_up(B_EDIT); frame();
    bad += check("EDIT + white key n: the sound's n-th section (Prophet: Filter), the sound and steps untouched", ok);
    btn_down(B_EDIT); frame(); turn(EN_K1, 1);
    ok = str_eq(sec.name[sec_of(ui.page)], "Amp");
    btn_up(B_EDIT); frame();
    bad += check("  KNOB 1: the next section", ok);
    set_engine_of(TSEL, ENGI_FM6); go_home(); frame();
    lay_combo(B_EDIT, black(3)); key_up(black(3)); frame();
    ok = !ui.home && cur_page()->scope == SC_FMOP && fm6_opsel == 3u;
    sec_build();
    for (k = 0; k < 16u && !str_eq(sec.name[ly_white_sec(k) % SEC_MAX], "Algo"); k++)
        ;
    key_down(white(k)); key_up(white(k)); frame();
    ok &= str_eq(sec.name[sec_of(ui.page)], "Algo");
    btn_up(B_EDIT); frame();
    bad += check("  FM6: the first six black keys OP1 .. OP6 (OP4: its pages), the white keys the rest (Algo)", ok);
    lay_combo(B_EDIT, black(LY_ACT + 1u)); key_up(black(LY_ACT + 1u)); frame();
    ok = preset_favorite();
    turn(EN_K3, -1);
    ok &= !preset_favorite();
    btn_up(B_EDIT); frame();
    bad += check("  the 8th black key: FAV on; KNOB 3: off", ok);
    song.playing = 0;
    lay_combo(B_EDIT, black(LY_ACT));
    ok = ui.confirm == CF_INIT_SOUND;
    frame();
    ok &= !ui.layer;
    key_up(black(LY_ACT)); btn_up(B_EDIT); frame();
    ok &= ui.confirm == CF_INIT_SOUND;
    press(B_OCTDN);
    bad += check("EDIT + the 7th black key: INITIALIZE SOUND? dialog (closes the layer, no tap)", ok && !ui.confirm);

    /* no layer in the menu or a dialog: GLO + a key plays */
    ui_power_on(); hold(B_HOME);
    btn_down(B_GLO); frame(); key_down(white(3)); frame();
    ok = !ui.layer && gates() > 0 && !kb_layer;
    key_up(white(3)); btn_up(B_GLO); frame();
    bad += check("in the menu: no GLO layer, GLO + a key plays it; GLO let go: no page", ok && ui.menu == 1);
    ui_power_on(); ui.confirm = CF_CLEAR_SEQ;
    btn_down(B_SCL); frames(512);
    ok = !ui.layer;
    btn_up(B_SCL); frame();
    bad += check("in a dialog: SCL held shows no map and taps nothing", ok && ui.confirm == CF_CLEAR_SEQ);
    /* one layer at a time: a second layer button is ignored, its keys stay notes after */
    ui_power_on();
    btn_down(B_GLO); frame(); btn_down(B_EDIT); frame(); key_down(white(1)); frame();
    ok = ui.layer == LAYER_GLO && TSEL->eng_req == trk[0].eng_req && perf_solo == 2u;
    key_up(white(1)); btn_up(B_GLO); frame();
    key_down(white(1)); frame();
    ok &= gates() > 0 && !ui.layer;
    key_up(white(1)); btn_up(B_EDIT); frame();
    bad += check("GLO then EDIT: GLO's layer; EDIT ignored (no layer, no page, its keys notes)", ok && ui.home);
    return bad;
}

/* bug fixes (UI): one regression each */
static int test_bughunt_ui(void)
{
    int bad = 0, ok;
    /* 1: REC in NAME, the menu or a dialog: nothing (PLAY cannot stop the transport there) */
    ui_power_on();
    go_page(GR_USER); ui.uslot = 3;
    sheet_do("Save here");                              /* NAME opens */
    press(B_REC);
    bad += check("REC in NAME does nothing (no arm, no transport start); NAME stays",
                 name_on() && !song.rec && !transport_req && !song.playing);
    ui_power_on(); hold(B_HOME); press(B_REC); press(B_PLAY);
    bad += check("REC in the menu does nothing (then PLAY: nothing either)", ui.menu && !song.rec && !transport_req);
    ui_power_on();
    up_ui(2, 5);
    go_page(GR_USER); ui.uslot = 5; sheet_do("Save here");
    press(B_REC);
    ok = ui.confirm == CF_OVR_USER && !song.rec && !transport_req;
    press(B_OCTUP);
    bad += check("REC in the OVERWRITE? dialog does nothing; OCT+ then opens NAME", ok && name_on());
    {   /* 2: UNDO of a sound load on an FM6 track: the track's own (edited) patch, not PTCH's factory one */
        uint8_t mine[FP_SIZE + 1u], next[FP_SIZE + 1u];
        ui_power_on();
        track_select(1); frame();
        undo_depth++; set_engine_of(TSEL, ENGI_FM6); undo_depth--;   /* (no undo copy of this) */
        frame();
        memcpy(mine, fm6_patch[1], sizeof mine);
        mine[0] ^= 0x15; mine[5] ^= 0x22; mine[40] ^= 0x07;   /* an edited patch (the editor, a project) */
        fm6_set_patch(1, mine);
        memcpy(mine, fm6_patch[1], FP_SIZE);
        turn(EN_PRESET, 1);                             /* browse one sound */
        memcpy(next, fm6_patch[1], FP_SIZE);
        hold(B_SAVE);                                   /* UNDO */
        frame();
        ok = TSEL->eng_req == ENGI_FM6 && !memcmp(mine, fm6_patch[1], FP_SIZE);
        save_redo();                                    /* REDO */
        frame();
        bad += check("UNDO of a sound load on FM6: the track's edited patch back; REDO: the load's",
                     ok && memcmp(mine, next, FP_SIZE) && !memcmp(next, fm6_patch[1], FP_SIZE));
        hold(B_SAVE); frame();                          /* (the edited patch again) */
        btn_down(B_EDIT); frames(500);                  /* EDIT layer: the next sound (KNOB 2), then OCT- */
        turn(EN_K2, 1);
        ok = ui.layer == LAYER_EDIT && memcmp(mine, fm6_patch[1], FP_SIZE);
        oct_back();
        btn_up(B_EDIT); frame();
        bad += check("  the EDIT layer's OCT- after a sound load: FM6 with the track's edited patch",
                     ok && TSEL->eng_req == ENGI_FM6 && !memcmp(mine, fm6_patch[1], FP_SIZE));
    }
    {   /* 2b: a patch the editor sends between two preset loads is a new starting point: UNDO brings it back */
        uint8_t ed[FP_SIZE + 1u];
        ui_power_on();
        track_select(1); frame();
        undo_depth++; set_engine_of(TSEL, ENGI_FM6); undo_depth--;
        frame();
        turn(EN_PRESET, 1); frame();                    /* load 1 */
        memcpy(ed, fm6_patch[1], sizeof ed);
        ed[0] ^= 0x15; ed[40] ^= 0x07;
        fm6_set_patch(1, ed);                           /* the editor's FM6_PUT */
        memcpy(ed, fm6_patch[1], FP_SIZE);
        turn(EN_PRESET, 1); frame();                    /* load 2 */
        hold(B_SAVE); frame();                          /* UNDO */
        bad += check("UNDO after load, editor patch, load: the editor's patch back",
                     !memcmp(ed, fm6_patch[1], FP_SIZE));
    }
    {   /* the ARP button flashes on the beat while an ARP plays (the bar's first beat longer); dark otherwise */
        uint32_t q, b, r[5];
        ui_power_on(); frame();
        b = beat_samples();
        r[0] = arp_led();                               /* no ARP playing */
        trk[2].p[P_AMODE] = 1; trk[2].nheld = 1; trk[2].held[0] = 60;
        beat_n = 1; beat_pos = 0; r[1] = arp_led();
        beat_pos = b / 3u; r[2] = arp_led();
        beat_n = 0; r[3] = arp_led();
        beat_pos = b * 3u / 4u; r[4] = arp_led();
        bad += check("ARP LED: none without an ARP; flashes on the beat, the bar's first beat longer",
                     r[0] == 2u && r[1] == 1u && r[2] == 0u && r[3] == 1u && r[4] == 0u);
        for (q = 0; q < 4u * b / 128u + 4u; q++) events_block(128);
        bad += check("  the beat counter runs (a bar of blocks wraps the beat of the bar)", beat_n < 4u && beat_pos < b);
        trk[2].p[P_AMODE] = 0; trk[2].nheld = 0;
    }
    /* 3: USER ERASE asks first (ERASE U02?): OCT- keeps the preset, OCT+ erases it */
    ui_power_on();
    go_page(GR_USER); ui.uslot = 1;
    up_ui(2, 1);
    sheet_do("Erase");
    ok = ui.confirm == CF_ERASE_USER && ui.confirm_trk == 1u && up_used(1);
    {
        char a[24], b[32];
        confirm_text(a, b);
        ok &= str_eq(a, "ERASE U02?") && b[0];
    }
    press(B_OCTDN);
    ok &= !ui.confirm && up_used(1);
    sheet_do("Erase"); press(B_OCTUP);
    bad += check("USER ERASE: the ERASE U02? dialog; OCT- keeps the preset, OCT+ erases it",
                 ok && !ui.confirm && !up_used(1));
    sheet_do("Erase");
    bad += check("  an empty slot: no dialog, EMPTY SLOT", !ui.confirm && msg_is("EMPTY SLOT"));
    {   /* 5: ALGORITHM does nothing while a layer's button is held (as PRESETS); after it, the track again */
        int16_t r0;
        ui_power_on();
        r0 = trk[0].p[P_ROOT];
        btn_down(B_SCL); frames(500);
        turn(EN_ALGO, 1);
        ok = ui.layer == LAYER_SCL && song.sel == 0u;
        key_down(white(1)); frame(); key_up(white(1)); frame();   /* ROOT on T1 */
        ok &= trk[0].p[P_ROOT] != r0;
        oct_back();
        ok &= trk[0].p[P_ROOT] == r0;
        btn_up(B_SCL); frame();
        btn_down(B_EDIT); turn(EN_ALGO, 1);             /* armed, not open yet: ignored too */
        ok &= song.sel == 0u;
        btn_up(B_EDIT); frame(); go_home(); frame();
        turn(EN_ALGO, 1);
        bad += check("ALGORITHM ignored with a layer's button held (OCT- puts back all); then T2",
                     ok && song.sel == 1u);
    }
    /* 6: the sheets' clearing rows: nothing there says so (not a question, not STOP TO EDIT) */
    ui_power_on();
    chain_defaults(&chain_config);
    pattern_init();                      /* no song rows */
    go_title("SONG"); frame();
    hold(B_OCTUP); sheet_do("Delete section");
    ok = !ui.confirm && msg_is("NOTHING TO DELETE") && text_w(&AF_S, ui.msg) <= 236 - 106;   /* (fits the header) */
    hold(B_OCTUP); sheet_do("Clear song");
    ok &= !ui.confirm && msg_is("NOTHING TO CLEAR");
    track_defaults_steps(TSEL);
    chain_config.count = 1; chain_config.row[0].slot = 0; chain_config.row[0].repeat = 1;
    go_title("PATTERN"); frame();
    sheet_do("Clear pattern");                          /* an empty track, the song with a row */
    ok &= !ui.confirm && msg_is("NOTHING TO CLEAR");
    go_title("SONG"); frame();
    hold(B_OCTUP); sheet_do("Clear song");
    bad += check("the sheets' Delete section / Clear song / Clear pattern with nothing there: NOTHING TO ..; a song: its dialog",
                 ok && ui.confirm == CF_CLEAR_SONG);
    /* 7: REC works in the layers, as PLAY: it arms the track (PLAY starts); the layer stays, no page */
    ok = 1;
    {
        static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
        uint32_t i;
        for (i = 0; i < 4u; i++) {
            ui_power_on();
            btn_down(LB[i]); frame();
            press(B_REC);
            ok &= song.rec == 1u && transport_req == 0u && ui.layer != 0u;
            btn_up(LB[i]); frame();
            ok &= ui.home;
        }
    }
    bad += check("REC in every layer: arms the track without starting PLAY; the layer stays, no page", ok);
    /* 8: the OCT- LED lit in the SET layers (OCT- = UNDO), OCT+ dark; FX (HOLD): the octave as usual */
    ok = 1;
    {
        static const uint8_t LB[3] = {B_GLO, B_SCL, B_EDIT};
        uint32_t i;
        for (i = 0; i < 3u; i++) {
            ui_power_on();
            song.octave = 1;                            /* (outside: OCT+ lit) */
            btn_down(LB[i]); frames(500);
            ok &= layer_set_open() && oct_leds_seen(1) == 1u && oct_leds_seen(0) == 1u;
            btn_up(LB[i]); frame();
            ok &= oct_leds() == 2u;
        }
        ui_power_on();
        btn_down(B_FX); frames(500);
        ok &= ui.layer == LAYER_FX && oct_leds() == 0u;
        btn_up(B_FX); frame();
    }
    bad += check("SET layers: the OCT- LED lit (UNDO), OCT+ dark; after: the octave; FX: the octave", ok);
    /* 9: no "HOLD [EDIT] QUICK" over NAME (EDIT tapped on USER / PROJECT renames) */
    ui_power_on();
    layer_seen = 0;
    up_ui(2, 0);
    go_page(GR_USER); ui.uslot = 0; frame();
    ui.msg_t = 0;
    press(B_EDIT);
    ok = name_on() && !msg_is("HOLD [EDIT] QUICK");
    ui_power_on();
    layer_seen = 0;
    press(B_EDIT);
    bad += check("EDIT tap = RENAME on USER: no layer hint over NAME (elsewhere the hint as before)",
                 ok && msg_is("HOLD [EDIT] QUICK"));
    return bad;
}

/* the chord keys on the device (chord.c): SCL tapped again is the CHORD page, KNOB 1 CHRD, KNOB 2 VOIC, the graph
 * names the last chord (MONO gray); a sound load keeps them; the SCL layer's KNOB 3 / 4 are CHRD / VOIC, OCT-
 * puts them back */
static int test_chord_page(void)
{
    int bad = 0, ok;
    track_t *t;
    ui_power_on();
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    go_home(); frame();
    press(B_SCL); frames(400);
    ok = str_eq(cur_page()->title, "SCALES");
    press(B_SCL); frames(400);
    ok &= str_eq(cur_page()->title, "SCL");
    press(B_SCL); frames(400);
    ok &= str_eq(cur_page()->title, "CHORD") && cur_page()->graph == GR_CHORD && cur_page()->fam == FAM_SCL;
    bad += check("SCL cycles SCALES, SCL, then CHORD: the CHORD page (CHRD VOIC, the chord graph)", ok && cur_page()->id[0] == P_CHRD &&
                 cur_page()->id[1] == P_VOIC);
    screen_clear(); ui.force = 1; ui_draw();
    bad += check("  CHRD OFF: the page says what to do, MONO gray", screen_gray());
    turn(EN_K1, 1); frame();
    turn(EN_K2, 1); frame(); turn(EN_K1, 1); frame();
    bad += check("  (a list) KNOB 1: CHRD DIA3; KNOB 2 the next row, KNOB 1: VOIC OPEN",
                 t->p[P_CHRD] == CH_DIA3 && t->p[P_VOIC] == VC_OPEN);
    key_down(7); frame();                                          /* C4 in C (SCALE CHR: the major of C) */
    ok = gates() == 3u && chord_last[song.sel].root == 60;
    screen_clear(); ui.force = 1; ui_draw();
    ok &= screen_gray();
    key_up(7); frame();
    bad += check("  a key plays the chord, the graph shows it (C), MONO gray", ok && !gates());
    t->p[P_CHRD] = CH_MIN7; t->p[P_VOIC] = VC_BASS;
    apply_preset_to(t, 1);
    bad += check("  a sound load keeps CHRD and VOIC (SCL settings)", t->p[P_CHRD] == CH_MIN7 && t->p[P_VOIC] == VC_BASS);
    set_engine_of(t, ENGI_DRUM); t->engine = t->eng_req; frame();
    screen_clear(); ui.force = 1; ui_draw();
    bad += check("  a kit (DRUM): the page says so, MONO gray", chord_kit(t) && screen_gray());
    ui_power_on();
    t = TSEL;
    t->p[P_CHRD] = CH_OFF; t->p[P_VOIC] = VC_CLOSE;
    go_home(); frame();
    btn_down(B_SCL); frames(800);
    ok = ui.layer == LAYER_SCL;
    turn(EN_K3, 2); frame();
    turn(EN_K4, 1); frame();
    ok &= t->p[P_CHRD] == CH_DIA7 && t->p[P_VOIC] == VC_OPEN && t->p[P_QUANT] == 0 && t->p[P_TRANS] == 0;
    turn(EN_K1, 2); frame();
    ok &= t->p[P_ROOT] == 2;
    oct_back();
    btn_up(B_SCL); frame();
    bad += check("SCL layer: KNOB 1 ROOT, 3 CHRD, 4 VOIC (QNT TRN untouched); OCT- puts them back", ok &&
                 t->p[P_CHRD] == CH_OFF && t->p[P_VOIC] == VC_CLOSE && t->p[P_ROOT] == 0 && ui.home);
    btn_down(B_SCL); frames(800);
    screen_clear(); ui.force = 1; ui_draw();
    ok = screen_gray();
    btn_up(B_SCL); frame();
    bad += check("  its map and cards MONO gray", ok);
    return bad;
}

/* the STEP page's piano roll (ui_graph.c graph_roll), read back from the screen: per column the rows whose centre
 * pixel (the bar's middle, x + 6) is a bar colour, against the notes the step sounds (its own; a TIE: the note
 * before's; a REST: none) */
static int32_t pr_note(const step_t *st, uint32_t j)   /* the step's note j (0..3), then its lane hits (-1: none) */
{
    if (j < 4u)
        return j < st->n ? st->note[j] : -1;
    return (st->hit >> (j - 4u)) & 1u ? DRUM_LANE_NOTE[j - 4u] : -1;
}
static int pr_is_bar(uint32_t x, uint32_t y)
{
    uint16_t c = swap16(host_screen[y * 240u + x]);
    return c == T_THEME || c == T_ACCENT || c == T_TEXT;
}
static int pr_columns_match(const track_t *t, uint32_t *nbars)
{
    uint32_t i, r, j, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u;
    int ok = 1;
    for (i = 0; i < 16u && base + i < len; i++) {
        uint32_t want = 0, got = 0, s = pr_src(t, base + i, len);
        for (j = 0; s < NSTEP && j < 4u + NLANE; j++) {
            int32_t n = pr_note(&t->step[s], j), rr = (int32_t)proll.lo + PR_ROWS - 1 - n;
            if (n >= 0 && rr >= 0 && rr < PR_ROWS) want |= 1u << rr;
        }
        for (r = 0; r < PR_ROWS; r++)
            if (pr_is_bar((uint32_t)(PR_X0 + (int32_t)i * PR_CW + 6), PR_TOP + PR_Y0 + r * PR_RH + 2u)) got |= 1u << r;
        if (got != want) {
            printf("ui:   roll column %u: rows %05x drawn, %05x wanted\n", base + i, got, want);
            ok = 0;
        }
        for (r = 0; r < PR_ROWS; r++) *nbars += (got >> r) & 1u;
    }
    return ok;
}
static int test_piano_roll(void)
{
    int bad = 0, ok;
    uint32_t i, nb = 0, lo0, steps = 0;
    track_t *t;
    ui_power_on();
    t = TSEL;
    demo_pat16(t, DEMO_ACID);                                         /* ACID: A2..A3, ties, accents, slides */
    t->p[P_ROOT] = 9; t->p[P_SCALE] = 2;
    go_page(GR_ROLL); ui.cursor = 0; ui.bank = 0;
    screen_clear(); ui.force = 1; ui_draw();
    ok = pr_columns_match(t, &nb);
    bad += check("piano roll: ACID's bars drawn at their notes' rows, ties carried, rests empty", ok && nb >= 10u);
    bad += check("  the view holds the page's notes (A2..A3 in 14 rows), MONO gray", proll.lo <= 45 && proll.lo + PR_ROWS - 1 >= 57 &&
                 screen_gray());
    t->p[P_VOICE] = V_POLY;                                           /* chords: up to 4 bars in a column */
    for (i = 0; i < 16u; i += 4u) {
        step_t *s = &t->step[i];
        s->time = ST_NOTE; s->n = 4; s->note[0] = 57; s->note[1] = 60; s->note[2] = 64; s->note[3] = 67;
        t->step[i + 1u].time = ST_TIE;
    }
    nb = 0;
    screen_clear(); ui.force = 1; ui_draw();
    bad += check("  4-note chords stacked, their ties carry all four", pr_columns_match(t, &nb));
    for (i = 0; i < NSTEP; i++) {                                     /* LEN 32, page 2 */
        step_clear(&t->step[i]);
        if (i >= 16u && i % 2u == 0u) { t->step[i].time = ST_NOTE; t->step[i].n = 1; t->step[i].note[0] = (uint8_t)(72 + i % 7u); }
    }
    t->p[P_SLEN] = 32; cursor_set(18);
    nb = 0;
    screen_clear(); ui.force = 1; ui_draw();
    bad += check("  LEN 32, the cursor on page 2: its 16 steps drawn", ui.bank == 1u && pr_columns_match(t, &nb) && nb == 8u);
    lo0 = proll.lo;                                                   /* two octaves up: the view follows in steps */
    for (i = 16; i < 32u; i++) if (t->step[i].n) t->step[i].note[0] += 24;
    for (i = 0; i < 20u && proll.lo != lo0 + 24u; i++) { frame(); steps++; }
    bad += check("  notes moved two octaves: the view follows over frames (not at once), then rests",
                 proll.lo == lo0 + 24u && steps >= 3u && steps < 20u);
    {
        uint32_t sig = ui.graph_sig;
        frame(); frame();
        bad += check("  nothing changed: the graph is not drawn again", ui.graph_sig == sig);
    }
    nb = 0;
    bad += check("  after the follow the bars match again", pr_columns_match(t, &nb));
    key_down(7); frames(200);                                         /* a key held: its row lit on the strip */
    {
        int32_t r = (int32_t)proll.lo + PR_ROWS - 1 - (int32_t)kb_chord[7][0];
        uint16_t c = r >= 0 && r < PR_ROWS ? swap16(host_screen[(PR_TOP + PR_Y0 + (uint32_t)r * PR_RH + 1u) * 240u + PR_KX + 7]) : 0;
        bad += check("  a key held: its row on the keyboard strip is the accent", kb_chn[7] && c == T_ACCENT);
    }
    key_up(7); frames(100);
    return bad;
}

/* bug fixes, second round (UI) */
static uint32_t text_in(int32_t x0, int32_t y0, int32_t x1, int32_t y1)   /* pixels in T_TEXT in the box */
{
    uint32_t n = 0;
    int32_t x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += host_screen[y * 240 + x] == swap16(T_TEXT);
    return n;
}
static uint32_t accent_in(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t n = 0;
    int32_t x, y;
    uint16_t a = (uint16_t)((T_ACCENT >> 8) | (T_ACCENT << 8));
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += host_screen[y * 240 + x] == a;
    return n;
}
static int test_bughunt_ui2(void)
{
    int bad = 0;
    uint32_t i;
    {   /* 1. a layer button pressed under a dialog and still held after it closes: dead, its keys stay notes */
        static const uint32_t BTN[2] = {B_FX, B_GLO};
        for (i = 0; i < 2u; i++) {
            uint32_t k = white(0), ok;
            ui_power_on(); TSEL->p[P_VOICE] = V_POLY;
            go_home(); frame();
            confirm_open(CF_CLEAR_SEQ, 0); frame();
            btn_down(BTN[i]); frame(); frames(100);
            btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();   /* the dialog closes, the button still held */
            frames(800);
            ok = !ui.confirm && (ui.ly_t0 & LY_DEAD) && !ui.layer && !kb_mask && !perf_mask;
            key_down(k); frame();
            ok = ok && kb_note[k] != KB_SILENT && gates() == 1u && !((kb_layer >> k) & 1u) && !perf_held && !ui.layer;
            key_up(k); frame();
            btn_up(BTN[i]); frame();
            bad += check(i ? "dead GLO held from a dialog: no map, the key plays a note"
                           : "dead FX held from a dialog: no map, the key plays a note, no effect", ok);
        }
    }
    {   /* 2. STEP entry with CHRD on writes what the key sounds, as live recording does */
        static const uint32_t KEY[2] = {7u, 8u};                      /* C4, C#4 (out of C major) */
        uint32_t v, j, ok = 1;
        for (v = 0; v < 2u; v++)
            for (j = 0; j < 2u; j++) {
                track_t *t;
                step_t e, r;
                ui_power_on(); t = TSEL; t->p[P_VOICE] = v ? V_MONO : V_POLY; t->p[P_CHRD] = CH_DIA3; t->p[P_SCALE] = 1;
                track_defaults_steps(t); go_page(GR_ROLL); cursor_set(0); frame();
                key_down(KEY[j]); frame();
                e = t->step[0];
                key_up(KEY[j]); frame();
                ok &= e.n == (v ? 1u : 3u) && e.note[0] == 60u && (v || (e.note[1] == 64u && e.note[2] == 67u));
                ui_power_on(); t = TSEL; t->p[P_VOICE] = v ? V_MONO : V_POLY; t->p[P_CHRD] = CH_DIA3; t->p[P_SCALE] = 1;
                track_defaults_steps(t); go_home(); frame();
                song.rec = 1; song.playing = 1; t->seq_idx = 3; t->seq_pos = 0;
                fm1_in.notes = 1u << KEY[j]; keyboard_block();
                r = t->step[3];
                fm1_in.notes = 0; keyboard_block();
                song.rec = 0; song.playing = 0;
                ok &= r.n == e.n && !memcmp(r.note, e.note, e.n);
            }
        bad += check("STEP entry with CHRD: POLY the chord, MONO its root, as live recording writes", ok);
    }
    {   /* 3. MIXER: the knob just turned is the control drawn in text (K1 the LEVEL ring, K2 PAN's value, K3 REV's,
         * K4 the MUTE chip) in the selected track's strip (ui_pages.c pv_strip) */
        uint32_t k, a[4][4], ok = 1;
        for (k = 0; k < 4u; k++) {
            int32_t X = CARD_X(song.sel), Y = PV_STRIP_TOP;
            ui_power_on();
            settings.palette = 1; palette_set(1);
            go_page(GR_TRK); frame(); frame();
            X = CARD_X(song.sel);
            turn(EN_K1 + k, 1);                                       /* (K4 right: MUTE on) */
            ui.force = 1; frame();
            a[k][0] = text_in(X + 7, Y + 50, X + 15, Y + 90);         /* the LEVEL ring's left (its arc) */
            a[k][1] = text_in(X + 5, Y + 145, X + 26, Y + 157);       /* PAN's value */
            a[k][2] = text_in(X + 29, Y + 145, X + 50, Y + 157);      /* REV's */
            a[k][3] = text_in(X + 6, Y + 168, X + 48, Y + 184);       /* the MUTE chip */
        }
        for (k = 0; k < 4u; k++) {
            uint32_t j;
            for (j = 0; j < 4u; j++)
                ok &= j == k ? a[k][j] > 0u : a[k][j] == 0u;
        }
        bad += check("MIXER: K1..K4 light the LEVEL ring / PAN / REV / the MUTE chip, nothing else", ok);
    }
    {   /* 4. renaming the slot the music came from renames the music: the next SAVE prefills the new name */
        char pn[16];
        uint32_t ok;
        ui_power_on();
        go_page(GR_SLOTS); song.g[G_SLOT] = 2;
        project_save_as(1, "OLD");
        project_save_as(2, "OTHER");
        project_load(1); frame();
        ok = str_eq(proj_name, "OLD");
        project_rename(2, "ELSE");
        ok &= str_eq(proj_name, "OLD");                               /* another slot: the music's name stays */
        press(B_EDIT);                                                /* rename B on the slot list */
        while (nm.len) { nm_do(NB_DEL); }
        nm_white(13); frames(900);
        str_cpy(nm.s, "NEW", sizeof nm.s); nm.len = nm.cur = 3; nm.key = 0;
        press(B_OCTUP);
        project_name(1, pn);
        ok &= str_eq(pn, "NEW") && str_eq(proj_name, "NEW");
        sheet_do("Save here");                                        /* SAVE to B: OVERWRITE? */
        press(B_OCTUP);                                               /* YES: NAME */
        ok &= name_on() && str_eq(nm.s, "NEW");
        press(B_OCTDN);
        bad += check("rename of the loaded slot: the music's name follows, SAVE prefills it", ok);
    }
    {   /* 5. NAME: PLAY stops a transport started meanwhile (so the name can be saved), never starts it; REC ignored */
        uint32_t ok;
        ui_power_on(); go_page(GR_USER); ui.uslot = 3; sheet_do("Save here");
        ok = name_on();
        press(B_PLAY); frame();
        ok &= !transport_req && !song.playing && name_on();          /* stopped: PLAY starts nothing */
        press(B_REC); frame();
        ok &= !song.rec && !transport_req && name_on();
        song.playing = 1;                                             /* an external MIDI Start / the editor */
        press(B_OCTUP);
        ok &= name_on() && msg_is("STOP TO SAVE");
        press(B_REC); frame();
        ok &= !song.rec && !transport_req;
        press(B_PLAY); frame();
        ok &= transport_req == 2u && name_on();                       /* PLAY stops it, NAME stays */
        stop_transport();
        press(B_OCTUP);
        ok &= !name_on() && up_used(3);
        bad += check("NAME: PLAY stops a running transport (never starts), REC ignored, then OCT+ saves", ok);
    }
    {   /* 6. the piano roll's lowest C name ("C-1") stays inside the panel: nothing drawn left of x 3 */
        uint32_t p, x, y, out = 0;
        uint16_t bg;
        for (p = 0; p < NPALETTES; p++) {
            track_t *t;
            uint8_t lo = 0, hi = 127;
            ui_power_on(); t = TSEL; track_defaults_steps(t); t->p[P_SLEN] = 16;
            settings.palette = (uint8_t)p; palette_set(p);
            t->step[0].time = t->step[5].time = ST_NOTE; t->step[0].n = t->step[5].n = 1;
            t->step[0].note[0] = lo; t->step[5].note[0] = hi;
            go_page(GR_ROLL); cursor_set(0); ui.force = 1; frame(); frame();
            bg = (uint16_t)((T_BG >> 8) | (T_BG << 8));
            for (y = PR_TOP; y < PR_TOP + PR_H; y++)
                for (x = 0; x < 3u; x++)
                    out += host_screen[y * 240u + x] != bg;
        }
        bad += check("piano roll: the C-1 label stays inside the panel (every palette)", !out);
    }
    {   /* 7. EDIT: only the 7th black key inits (the six before it: operators or nothing) */
        uint32_t p, ok = 1;
        for (p = 0; p < LY_ACT + 1u; p++) {
            ui_power_on(); set_engine_of(TSEL, ENGI_PROPHET); go_home(); frame();
            btn_down(B_EDIT); key_down(black(p)); frame(); frames(50);
            ok &= (ui.confirm == CF_INIT_SOUND) == (p == LY_ACT);
            key_up(black(p)); btn_up(B_EDIT); frame();
            if (ui.confirm) press(B_OCTDN);
        }
        bad += check("EDIT: INIT on the 7th black key only", ok);
    }
    return bad;
}

#ifndef UI_TEST_NO_MAIN
/* L is a sparse face (tools/gen_aa_font.py L_CHARS): every string drawn in it has all its glyphs */
static int test_large_face(void)
{
    static const char *const FIXED[] = {"MELODEE", "0123456789", "C#4 / .."};   /* main.c, ui_menu.c; ui_draw.c draw_uboot */
    uint32_t i, missing = 0;
    const char *s;
    for (i = 0; i < NB + NE + sizeof FIXED / sizeof FIXED[0]; i++)                              /* ui_input.c setup_show: the control names */
        for (s = i < NB ? B_NAME[i] : i < NB + NE ? E_NAME[i - NB] : FIXED[i - NB - NE]; *s; s++)
            missing += glyph_at(&AF_L, (uint8_t)*s) < 0;
    return check("the L face holds every glyph of the strings drawn in it", !missing);
}

#if MELODEE_FM4
/* The DIGITAL algorithm charts (ui_graph.c FM_CELL / FM_MOD) against the DSP: src/eng_digital.c's switch (alg)
 * in digital_render_legacy and digital_render_custom, written out here as "source>destination" routes and the
 * operators mixed to the output. Op 4's feedback is on every algorithm (drawn always). Also: the carriers sit
 * on the bottom row and only they, a modulator one row above what it modulates, no two operators in a cell,
 * every operator heard, and the eight charts distinct. */
static int test_fm_charts(void)
{
    static const char *const DSP[8][2] = {
        {"4>3 3>2 2>1", "1"},          /* o3 = op(o4), o2 = op(o3), s = op(o2) */
        {"3>2 4>2 2>1", "1"},          /* o2 = op((o3 + o4) / 2), s = op(o2) */
        {"3>2 2>1 4>1", "1"},          /* o2 = op(o3), s = op((o2 + o4) / 2) */
        {"4>3 2>1 3>1", "1"},          /* o3 = op(o4), s = op((o2 + o3) / 2) */
        {"4>3 2>1", "13"},             /* o3 = op(o4), o1 = op(o2), s = (o1 + o3) / 2 */
        {"4>1 4>2 4>3", "123"},        /* o1, o2, o3 = op(o4), s = (o1 + o2 + o3) / 3 */
        {"4>3", "123"},                /* o3 = op(o4), s = (o1 + o2 + o3) / 3 */
        {"", "1234"}};                 /* s = (o1 + o2 + o3 + o4) / 4 */
    uint32_t a, b, k, j, routes = 1, layout = 1, distinct = 1;
    for (a = 0; a < 8u; a++) {
        uint32_t want = 0, car = 0, ok = 1;
        const char *s;
        for (s = DSP[a][0]; *s; s++)
            if (*s == '>') want |= 1u << (4u * (uint32_t)(s[1] - '1') + (uint32_t)(s[-1] - '1'));
        for (s = DSP[a][1]; *s; s++) car |= 1u << (*s - '1');
        if (FM_MOD[a] != want) { printf("ui: algorithm %u: routes %04x, the DSP %04x\n", a + 1u, FM_MOD[a], want); routes = 0; }
        for (k = 0; k < 4u; k++) {
            uint32_t row = FM_CELL[a][k] >> 4, outs = 0;
            if ((row == 0u) != ((car >> k) & 1u)) ok = 0;
            for (j = 0; j < 4u; j++) {
                if (j != k && FM_CELL[a][j] == FM_CELL[a][k]) ok = 0;
                if (FM_MOD[a] >> (4u * j + k) & 1u) { outs++; if (row != (FM_CELL[a][j] >> 4) + 1u) ok = 0; }
            }
            if (!outs && !((car >> k) & 1u)) ok = 0;
        }
        if (!ok) { printf("ui: algorithm %u: the chart's layout is off\n", a + 1u); layout = 0; }
        for (b = 0; b < a; b++)
            if (FM_MOD[a] == FM_MOD[b] && !memcmp(FM_CELL[a], FM_CELL[b], 4)) distinct = 0;
    }
    return check("DIGITAL charts: the 8 algorithms' routes and carriers equal eng_digital.c's", routes) +
           check("DIGITAL charts: carriers on the bottom row, modulators a row up, one per cell, distinct", layout && distinct);
}
#endif

/* The FM6 algorithm charts (ui_graph.c graph_fm6: FM6_CELL, its routes fm6_routes and feedback operator
 * fm6_fb_op read from fm6_core.c FM6_ALG) against the 32 algorithms of 6-operator FM written out here as
 * "source>destination" routes, the carriers and the feedback destination. MARK I's feedback return comes
 * from OP4 / OP5 on algorithms 4 / 6, while MODERN and OPL return OP6. Also against the core itself: the carriers
 * fm6_carriers (fm6_car_ops), the feedback flags. The layout: the carriers on the bottom row and only they, a modulator one
 * row above everything it modulates, one operator per cell, columns 0..5, at most 4 rows. (The charts as drawn:
 * tests/ui_render.c's lint, fm6_lint.) */
static int test_fm6_charts(void)
{
    static const struct { const char *r, *car; char fb; } ALGS[32] = {
        {"2>1 4>3 5>4 6>5", "13", '6'}, {"2>1 4>3 5>4 6>5", "13", '2'}, {"2>1 3>2 5>4 6>5", "14", '6'},
        {"2>1 3>2 5>4 6>5", "14", '6'}, {"2>1 4>3 6>5", "135", '6'}, {"2>1 4>3 6>5", "135", '6'},
        {"2>1 4>3 5>3 6>5", "13", '6'}, {"2>1 4>3 5>3 6>5", "13", '4'}, {"2>1 4>3 5>3 6>5", "13", '2'},
        {"2>1 3>2 5>4 6>4", "14", '3'}, {"2>1 3>2 5>4 6>4", "14", '6'}, {"2>1 4>3 5>3 6>3", "13", '2'},
        {"2>1 4>3 5>3 6>3", "13", '6'}, {"2>1 4>3 5>4 6>4", "13", '6'}, {"2>1 4>3 5>4 6>4", "13", '2'},
        {"2>1 3>1 5>1 4>3 6>5", "1", '6'}, {"2>1 3>1 5>1 4>3 6>5", "1", '2'}, {"2>1 3>1 4>1 5>4 6>5", "1", '3'},
        {"2>1 3>2 6>4 6>5", "145", '6'}, {"3>1 3>2 5>4 6>4", "124", '3'}, {"3>1 3>2 6>4 6>5", "1245", '3'},
        {"2>1 6>3 6>4 6>5", "1345", '6'}, {"3>2 6>4 6>5", "1245", '6'}, {"6>3 6>4 6>5", "12345", '6'},
        {"6>4 6>5", "12345", '6'}, {"3>2 5>4 6>4", "124", '6'}, {"3>2 5>4 6>4", "124", '3'},
        {"2>1 4>3 5>4", "136", '5'}, {"4>3 6>5", "1235", '6'}, {"4>3 5>4", "1236", '5'},
        {"6>5", "12345", '6'}, {"", "123456", '6'}};
    uint32_t a, k, j, routes = 1, layout = 1, core = 1, feedback = 1;
    for (a = 0; a < 32u; a++) {
        uint8_t m[6], want[6] = {0, 0, 0, 0, 0, 0};
        uint32_t car = 0, ok = 1, fbs = 0;
        const char *s;
        for (s = ALGS[a].r; *s; s++)
            if (*s == '>') want[s[1] - '1'] |= (uint8_t)(1u << (s[-1] - '1'));
        for (s = ALGS[a].car; *s; s++) car |= 1u << (*s - '1');
        fm6_routes(a, m);
        if (memcmp(m, want, 6) || fm6_fb_op(a) != (uint32_t)(ALGS[a].fb - '1')) {
            printf("ui: FM6 algorithm %u: the chart's routes or feedback differ from the written ones\n", a + 1u);
            routes = 0;
        }
        for (k = 0; k < 6u; k++)                                    /* the core: who writes the output, who has FB */
            fbs += (FM6_ALG[a][k] & 0xC0u) == 0xC0u;
        if (fm6_car_ops(a) != car || fbs != 1u) {
            printf("ui: FM6 algorithm %u: fm6_core.c's carriers / feedback differ\n", a + 1u);
            core = 0;
        }
        feedback &= fm6_fb_source(a, FM6_MARK1) == (a == 3u ? 3u : a == 5u ? 4u : (uint32_t)(ALGS[a].fb - '1'));
        feedback &= fm6_fb_source(a, FM6_MODERN) == (uint32_t)(ALGS[a].fb - '1');
        feedback &= fm6_fb_source(a, FM6_OPL) == (uint32_t)(ALGS[a].fb - '1');
        for (k = 0; k < 6u; k++) {
            uint32_t row = FM6_CELL[a][k] >> 4, outs = 0;
            if ((row == 0u) != ((car >> k) & 1u) || (FM6_CELL[a][k] & 0x0Fu) > 5u || row > 3u) ok = 0;
            for (j = 0; j < 6u; j++) {
                if (j != k && FM6_CELL[a][j] == FM6_CELL[a][k]) ok = 0;
                if (m[j] >> k & 1u) { outs++; if (FM6_CELL[a][k] >> 4 != (FM6_CELL[a][j] >> 4) + 1u) ok = 0; }
            }
            if (!outs && !((car >> k) & 1u)) ok = 0;               /* every operator heard */
        }
        if (!ok) { printf("ui: FM6 algorithm %u: the chart's layout is off\n", a + 1u); layout = 0; }
    }
    return check("FM6 charts: the 32 algorithms' routes, carriers and feedback operator equal fm6_core.c's", routes && core) +
           check("FM6 charts: carriers on the bottom row, modulators a row up, one per cell, at most 4 rows", layout) +
           check("FM6 charts: MARK I returns OP4 / OP5 in 4 / 6; MODERN and OPL return the feedback operator", feedback);
}

#if !MELODEE_FM4
/* DIGITAL retired (src/fm4_convert.c): engine 1 is on no track and in no list; its sounds arrive as FM6 with a patch
 * of their own: a user preset of it, its preset numbers (set_engine_of / apply_preset_to), a track that got engine 1
 * any other way (the main loop's net); the favourites of its presets move to FM6's; the power-on pad is FM6 PAD */
static int test_fm4_retired(void)
{
    int bad = 0, ok = 1;
    uint32_t i, k, e, total, seen = 0, all = 0;
    for(uint32_t eng=0;eng<NENGINES;eng++) if(eng_ok(eng)&&eng!=0)all |= 1u<<eng;
    int16_t p[P_COUNT];
    uint8_t v[FP_SIZE + 1u];
    ui_power_on();
    bad += check("power-on: track 2 is FM6 PAD (TRK_DEF, was DIGITAL PAD)", trk[1].eng_req == ENGI_FM6 &&
                 str_eq(ENGINES[ENGI_FM6]->presets[trk[1].preset].name, "PAD"));
    preset_all_pos(&total);
    for (i = 0; i < total; i++) {
        e = preset_all_at(i, &k);
        if (e < NENGINES)
            seen |= 1u << e;
    }
    bad += check("PRESETS: the list holds every engine's presets but DIGITAL's", seen == all && NENG_SHOWN == NENGINES - 9u - 6u * !MELODEE_LEGACY_EXTRAS);
    go_page(GR_BROWSE);
    set_engine_of(TSEL, 0);
    for (i = 0, seen = 0; i < NENG_SHOWN; i++) {
        turn(EN_K4, 1);
        seen |= 1u << TSEL->eng_req;
    }
    bad += check("PRESETS KNOB 4: the engines in order, DIGITAL skipped, back to the first",
                 seen == all && TSEL->eng_req == ENGI_PROPHET && eng_step(0, 1) == ENGI_FM6 && eng_step(ENGI_FM6, 1) == (MELODEE_LEGACY_EXTRAS ? 2u : ENGI_CZ) &&
                 eng_step(ENGI_FM6, -1) == ENGI_PROPHET && eng_step(0, -1) == ENGI_DRUM);
    {   /* the display order (engines.c ENGINE_ORDER): every engine one can pick once; the PRESETS list follows it */
        static const char *const ORDER[] = {"PROPHET", "FM6",
#if MELODEE_LEGACY_EXTRAS
                                            "PHASE",
#endif
                                            "CZ-1", "SID",
#if MELODEE_LEGACY_EXTRAS
                                            "VOICE", "TRIO", "WHEEL", "PHYS", "NOISE",
#endif
                                            "DRUM"};
        uint32_t last = 0xFFu, r = 0, n = 0;
        ok = NENG_SHOWN == NELEM(ORDER);
        for (i = 0; ok && i < NENG_SHOWN; i++)
            ok &= str_eq(ENGINES[eng_vis(i)]->name, ORDER[i]) && eng_rank(eng_vis(i)) == i && eng_ok(eng_vis(i));
        preset_all_pos(&total);
        for (i = 0; i < total; i++) {                  /* the list's engines, each once, in that order */
            e = preset_all_at(i, &k);
            if (e >= NENGINES || e == last)
                continue;
            ok &= n < NENG_SHOWN && e == eng_vis(n);
            n++;
            last = e;
            r++;
        }
        bad += check("engines shown PROPHET FM6 CZ-1 SID ... DRUM (ENGINE_ORDER); PRESETS lists them so",
                     ok && r == NENG_SHOWN);
    }
    /* a user preset stored with engine 1: kept as it is, it loads as FM6 with the converted patch */
    for (i = 0; i < P_COUNT; i++)
        p[i] = param_desc_of(ENGI_DIGITAL, i)->def;
    fm4_preset_values(p, 5);                           /* DIGITAL PAD */
    {
        up_rec_t r;
        memset(&r, 0, sizeof r);
        r.used = UP_USED;
        r.ver = UP_VER;
        r.engine = ENGI_DIGITAL;
        r.np = P_COUNT;
        memcpy(r.name, "OLD PAD", 7);
        for (i = 0; i < P_COUNT; i++)
            up_set_value(&r, i, p[i]);
        up_put(7, &r);
    }
    fm4_convert(p, v);
    up_load(7);
    bad += check("a DIGITAL user preset loads as FM6: the converted patch and preset = FM6 PAD",
                 TSEL->eng_req == ENGI_FM6 && TSEL->preset == 4u && TSEL->p[P_E7] == 0 && !TSEL->p[P_E0] &&
                 !memcmp(fm6_patch[song.sel], v, FP_SIZE) && TSEL->user == 8u);
    frame();
    bad += check("  .. and the main loop keeps that patch after load", !memcmp(fm6_patch[song.sel], v, FP_SIZE));
    eng_list_pos(&total);
    bad += check("  the record stays DIGITAL in the bank, listed with FM6's sounds (EDIT KNOB 2)",
                 up_rec(7)->engine == ENGI_DIGITAL && up_engine(7) == ENGI_FM6 &&
                 total == ENGINES[ENGI_FM6]->npresets + 1u);
    up_store(8, "AGAIN");
    bad += check("  saved again: an FM6 user preset (unused macro stays zero)", up_rec(8)->engine == ENGI_FM6 &&
                 up_value(up_rec(8), P_E7) == 0);
    /* preset numbers of engine 1 */
    set_engine_of(TSEL, ENGI_DIGITAL);
    bad += check("engine 1 asked for: DIGITAL E.PIANO converted (FM6, preset TINE EP, the patch named E.PIANO)",
                 TSEL->eng_req == ENGI_FM6 && TSEL->preset == 0u && !memcmp(fm6_patch[song.sel] + FP_NAME, "E.PIANO", 7));
    TSEL->eng_req = ENGI_DIGITAL;                      /* (the ISR's view: no other path does this) */
    apply_preset_to(TSEL, 1);
    bad += check("a DIGITAL preset number (apply_preset_to): its sound converted (BELL)",
                 TSEL->eng_req == ENGI_FM6 && TSEL->preset == 1u && !memcmp(fm6_patch[song.sel] + FP_NAME, "BELL", 4));
    for (i = 0; i < P_COUNT; i++)
        trk[2].p[i] = param_desc_of(ENGI_DIGITAL, i)->def;
    fm4_preset_values(trk[2].p, 2);
    trk[2].eng_req = ENGI_DIGITAL;
    frame();
    bad += check("a track left on engine 1 any other way: the main loop converts it (BASS -> FM BASS)",
                 trk[2].eng_req == ENGI_FM6 && trk[2].preset == 2u);
    {   /* favourites of DIGITAL presets (settings PER4) -> the FM6 presets that cover them */
        persist_t pe;
        memset(&pe, 0, sizeof pe);
        pe.magic = PERSIST_MAGIC;
        pe.panel = PANEL_DEFAULT;
        pe.favorites.factory[ENGI_DIGITAL][0] = 1u << 5 | 1u << 7;   /* PAD, FUNK KEY */
        settings_import(&pe, (int)sizeof pe);
        bad += check("favourites: DIGITAL PAD / FUNK KEY -> FM6 PAD / PLUCK, engine 1's cleared",
                     favorite_has(ENGI_FM6, 4) && favorite_has(ENGI_FM6, 7) && !favorites.factory[ENGI_DIGITAL][0]);
        memset(&favorites, 0, sizeof favorites);
    }
    return bad;
}
#endif

static int test_home_notes(void)
{
    uint32_t saved[4], root = 99, i, held = 0;
    int bad = 0;
    const char *q;
    ui_power_on();
    bad += check("HOME has no invented notes before the first key", !graph_notes());
    key_down(7); /* C4 */
    bad += check("panel C4 enters HOME after mapping", live_last[0][1] == (1u << 28));
    key_up(7);
    frame();
    bad += check("a panel tap between frames remains visible after release", graph_notes() &&
                 live_last[0][1] == (1u << 28) && !live_held[0][1]);
    ui_power_on();
    TSEL->p[P_QUANT] = Q_MPC;
    midi_event(0x90, 0, 21, 100);
    bad += check("MPC's mapped C4 is displayed, not source MIDI note 21", live_last[0][1] == (1u << 28) && !live_last[0][0]);
    midi_event(0x80, 0, 21, 0);
    ui_power_on();
    midi_event(0x90, 0, 60, 100);
    midi_event(0x90, 1, 60, 100);
    midi_event(0x80, 0, 60, 0);
    bad += check("same pitch on another track remains held in HOME", !live_held[0][1] && live_held[1][1] == (1u << 28));
    midi_event(0xB0, 1, 120, 0);
    bad += check("CC120 clears held HOME notes only on its track", !live_held[1][1] && live_last[1][1] == (1u << 28));
    midi_event(0x90, 1, 64, 100); midi_event(0x80, 1, 64, 0);
    bad += check("each track keeps its own last notes (Stage shows the selected one's)",
                 live_last[0][1] == (1u << 28) && live_last[1][2] == (1u << 0) && !live_last[1][1]);
    ui_power_on();
    midi_event(0x90, 0, 60, 100);
    midi_event(0x90, 10, 60, 100);
    midi_event(0x80, 0, 60, 0);
    bad += check("overlapping MIDI owners retain the HOME pitch until last release", live_held[0][1] == (1u << 28));
    midi_event(0xB0, 10, 64, 127); midi_event(0x80, 10, 60, 0);
    bad += check("sustain keeps the displayed pitch held", live_held[0][1] == (1u << 28));
    midi_event(0xB0, 10, 64, 0);
    bad += check("pedal-up releases emphasis but retains the last note", !live_held[0][1] && live_last[0][1] == (1u << 28));
    ui_power_on(); set_engine_of(TSEL, ENGI_FM6); events_block(32);
    midi_event(0x90, 0, 60, 100); midi_event(0x90, 0, 64, 100); midi_event(0x90, 0, 67, 100);
    q = chord_of((1u << 0) | (1u << 4) | (1u << 7), 0, &root);
    bad += check("FM6 notes are visible and a major triad is recognized", live_last[0][1] == (1u << 28) &&
                 live_last[0][2] == ((1u << 0) | (1u << 3)) && q && !q[0] && root == 0);
    /* C4 is bit 28 of word 1; E4/G4 are bits 0/3 of word 2. */
    for (i = 0; i < 4u; i++) saved[i] = live_last[0][i];
    input_on(&trk[3], 36, 100); input_off(&trk[3], 36);
    bad += check("drum hits do not replace HOME's last synth chord", !memcmp(saved, live_last[0], sizeof saved) && !live_last[3][1]);
    q = chord_of((1u << 0) | (1u << 4) | (1u << 7), 4, &root);
    bad += check("an inverted major triad resolves its root for slash bass", q && !q[0] && root == 0);
    q = chord_of((1u << 9) | (1u << 0) | (1u << 4) | (1u << 7), 9, &root);
    bad += check("minor seventh chord name retained from next", q && !strcmp(q, "m7") && root == 9);
    for (i = 0; i < 4u; i++) midi_forget_track(i);
    for (i = 0; i < NTRK * 4u; i++) held |= ((uint32_t *)live_held)[i];
    bad += check("panic clears every held bit without erasing the readout", !held && !memcmp(saved, live_last[0], sizeof saved));
    ui_power_on();
    return bad;
}

/* Stage (HOME, ui_stage.c): the selected track's engine's own four knobs (PROPHET and CZ-1: their native panel values,
 * edited as their pages edit them), no footer; SEQ > PATTERNS: KNOB k queues track k's pattern; the lights' grammar:
 * dim something there, bright happening now, breathing waiting (the third plane); the DRUM lanes' flashes */
static uint32_t plane_led(uint32_t plane, uint32_t id)          /* led id in dim plane `plane` (0..2) */
{
    uint8_t q = led_pos[id];
    return q != 0xFF && ((fm1_led_dim[plane][q >> 3] >> (q & 7u)) & 1u);
}
static int test_stage(void)
{
    int bad = 0;
    int16_t *vp;
    uint32_t k, before;
    ui_power_on();
    go_home();
    frame();
    bad += check("Stage: ANALOG's knobs are its EDIT values (CUT RES ATK REL)", stage_page()->scope == SC_ENGINE &&
                 stage_page()->id[0] == P_E4 && stage_page()->id[3] == P_REL && home_param(0, &vp) && vp == &TSEL->p[P_E4]);
    set_engine_of(TSEL, ENGI_PROPHET);
    go_home();
    frame();
    before = p5_patch_of(TSEL)->raw[P5_CUTOFF];
    turn(EN_K1, before < P5_PANEL[P5_CUTOFF].max ? 1 : -1);
    bad += check("Stage: PROPHET's KNOB 1 is the program's own CUTOFF", stage_page()->scope == SC_P5 &&
                 p5_patch_of(TSEL)->raw[P5_CUTOFF] != before && home_param(2, &vp) && str_eq(home_param(2, &vp)->label, "ENV AMT"));
    set_engine_of(TSEL, ENGI_CZ);
    apply_preset(3);
    go_home();
    frame();
    {
        const param_desc_t *d = home_param(2, &vp);
        int16_t was = vp ? *vp : -1;
        uint8_t raw[CZ_BYTES];
        memcpy(raw, cz_patch[song.sel].raw, CZ_BYTES);
        turn(EN_K3, was < d->max ? 1 : -1);
        d = home_param(2, &vp);
        bad += check("Stage: CZ-1's KNOB 3 detunes the native tone (DETUNE: its FINE)", stage_page()->scope == SC_CZ1 &&
                     d && vp && *vp != was && memcmp(raw, cz_patch[song.sel].raw, CZ_BYTES) &&
                     str_eq(stage_label(2, d), "DETUNE"));
    }
    ui_power_on();
    stop_transport();
    go_page(GR_PATGRID);
    turn(EN_K2, 1);
    bad += check("PATTERNS stopped: KNOB 2 switches track 2 to pattern 2 at once", trk[1].pattern == 1u && trk[0].pattern == 0u);
    song.playing = 1;
    turn(EN_K3, 1);
    bad += check("  playing: KNOB 3 queues track 3's pattern 2 for its bar", trk[2].pattern == 0u && trk[2].pattern_next == 1u);
    turn(EN_K3, -1);
    bad += check("  .. back to the one playing: nothing waits", trk[2].pattern_next == 0xFFu);
    {   /* the lights: SEQ held, the pattern keys; REC armed while stopped */
        static const uint8_t PK[NPAT] = {0, 2, 4, 6, 7, 9, 11, 12};
        ui_power_on();
        ui_leds();
        for (k = 0; k < 41u; k++)
            led_pos[k] = (uint8_t)((k % FM1_NCOL) << 3 | (1u + k / FM1_NCOL));
        stop_transport();
        pattern_switch(TSEL, 2); my_steps(TSEL); pattern_switch(TSEL, 0); my_steps(TSEL);
        song.playing = 1;
        TSEL->pattern_next = 5;
        btn_down(B_SEQ);
        frame();
        fm1_ms = 0;                                       /* (the breath at its brightest) */
        ui_leds();
        bad += check("SEQ held: the pattern playing bright, one holding notes dim, the one waiting breathing, empty dark",
                     key_light(14u + PK[0]) == 2u && plane_led(1, 14u + PK[2]) && !key_light(14u + PK[2]) &&
                     plane_led(2, 14u + PK[5]) && !key_light(14u + PK[5]) && !plane_led(1, 14u + PK[5]) &&
                     !key_light(14u + PK[3]) && !plane_led(1, 14u + PK[3]) && !plane_led(2, 14u + PK[3]));
        fm1_ms = 500;                                     /* (the breath's dark step) */
        ui_leds();
        bad += check("  .. the breath goes dark between its swells", !plane_led(2, 14u + PK[5]));
        btn_up(B_SEQ);
        frame();
        stop_transport();
        song.rec = 1;
        fm1_ms = 0;
        ui_leds();
        bad += check("REC armed, stopped: REC breathes (waiting), not lit", plane_led(2, panel.btn[B_REC]) &&
                     !((fm1_led[led_pos[panel.btn[B_REC]] >> 3] >> (led_pos[panel.btn[B_REC]] & 7u)) & 1u));
        song.playing = 1;
        ui_leds();
        bad += check("  .. recording: REC lit", !plane_led(2, panel.btn[B_REC]) &&
                     ((fm1_led[led_pos[panel.btn[B_REC]] >> 3] >> (led_pos[panel.btn[B_REC]] & 7u)) & 1u));
        song.rec = 0;
        led_pos_init();
    }
    ui_power_on();
    track_select(3);
    set_engine_of(TSEL, ENGI_DRUM);
    events_block(4);
    drum_flash[3] = 0;
    input_on(TSEL, 38, 100); input_off(TSEL, 38);
    events_block(2);
    bad += check("a DRUM hit flags its lane for Stage (SNARE)", (drum_flash[3] >> 1) & 1u);
    go_home();
    frame();
    bad += check("  .. Stage takes it and lights the lane's name", !drum_flash[3] && stage.hit_t[1]);
    ui_power_on();
    return bad;
}

/* Phase 4: Capture (REC held: what the selected track played in its last bars, unarmed, into its pattern), the jam log
 * (the patterns played since PLAY, a row per track 1 loop) and TAKE JAM, autosave (stopped and untouched) */
static uint32_t cap_events(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < RECORD_MAX; i++)
        if (recording[i].vel && (recording[i].owner & 31u) == recording_owner(t) && recording_present(&recording[i])) n++;
    return n;
}
static int test_capture(void)
{
    int bad = 0;
    uint32_t i, steps;
    ui_power_on();
    recording_reset();
    track_defaults_steps(TSEL);
    TSEL->p[P_SLEN] = 16;
    transport_req = 1;
    events_block(1);                                    /* (PLAY: the sequencer from step 0) */
    for (i = 0; i < 20u; i++) {                         /* a bar and a quarter: notes on steps 2, 6 .. of bar 1 and bar 2 */
        while (cap_step[0] != (uint16_t)i) events_block(1);
        if (i % 4u == 2u) input_on(TSEL, 60 + i, 100);
        events_block(2);
        if (i % 4u == 2u) input_off(TSEL, 60 + i);
    }
    steps = TSEL->p[P_SLEN];
    hold(B_REC);
    bad += check("Capture: an empty pattern takes the bars played (2 bars: LEN 32), every note, nothing armed",
                 TSEL->p[P_SLEN] == 32 && steps == 16 && cap_events(TSEL) == 5u && !song.rec && msg_is("CAPTURED 2 BARS"));
    bad += check("  .. on their steps (2 6 10 14 18), as recorded notes", (TSEL->step[2].flags & SF_RECORDED) &&
                 TSEL->step[2].note[0] == 62 && TSEL->step[18].note[0] == 78 && TSEL->step[14].n == 1u);
    hold(B_SAVE);
    bad += check("  SAVE held undoes it (LEN and steps back)", TSEL->p[P_SLEN] == 16 && !TSEL->step[2].n);
    song.rec = 1;
    hold(B_REC);
    bad += check("Capture while the track records: refused", msg_is("RECORDING"));
    song.rec = 0;
    stop_transport();
    {   /* the jam log: track 1's loop with pattern 1, then 2 twice, then 1 */
        ui_power_on();
        stop_transport();
        for (i = 0; i < NTRK; i++) trk[i].p[P_SLEN] = 4;
        transport_req = 1;
        events_block(1);
        while (jam.n < 1u) events_block(1);
        pattern_request(&trk[0], 1);
        while (jam.n < 2u) events_block(1);
        while (jam.rep[1] < 2u) events_block(1);
        pattern_request(&trk[0], 0);
        while (jam.n < 3u) events_block(1);
        stop_transport();
        bad += check("jam log: a row per change, repeats counted (1, 2 x2, 1)", jam.n == 3u && jam.pat[0][0] == 0u &&
                     jam.pat[1][0] == 1u && jam.rep[1] == 2u && jam.pat[2][0] == 0u);
        go_page(GR_SONG);
        hold(B_REC);
        bad += check("SONG: REC held takes the jam as the song (empty song: no dialog)", chain_config.count == 3u &&
                     chain_patterns[1][0] == 1u && chain_config.row[1].repeat == 2u && !ui.confirm);
        hold(B_REC);
        bad += check("  over a song with rows: the dialog first", ui.confirm == CF_TAKE_JAM);
        press(B_OCTDN);
        go_home();
    }
    {   /* autosave: a project with a slot, changed, stopped, untouched 5 s: saved once; unchanged: no write */
        uint32_t writes;
        ui_power_on();
        stop_transport();
        project_save(1);
        TSEL->p[P_LEVEL] = 77;
        writes = stored_param(1, 0, P_LEVEL) == 77;
        for (i = 0; i < 400u; i++) frame();             /* 6.4 s */
        bad += check("autosave: changed, stopped, untouched: back to its slot (B)", !writes && stored_param(1, 0, P_LEVEL) == 77);
        TSEL->p[P_LEVEL] = 66;
        for (i = 0; i < 100u; i++) frame();
        bad += check("  .. not before AUTOSAVE_MS of rest", stored_param(1, 0, P_LEVEL) == 77);
        turn(EN_K1, 1);
        for (i = 0; i < 200u; i++) frame();
        bad += check("  .. and any touch starts the rest again", stored_param(1, 0, P_LEVEL) == 77);
        memset(proj_slot, 0, sizeof proj_slot);
        memset(proj_bank_slot, 0, sizeof proj_bank_slot);
    }
    ui_power_on();
    return bad;
}

/* undo levels in the cache RAM's UI part (resources.c ui_cache): loads on two tracks, SAVE held twice undoes both
 * (newest first), SAVE + OCT+ redoes the older; without cache RAM one level */
static int test_undo_levels(void)
{
    int bad = 0;
    uint8_t p0, p1;
    resource_host_cache_enabled = 1;
    ui_power_on();
    stop_transport();
    p0 = trk[0].preset;
    turn(EN_PRESET, 1);                                 /* T1: a sound load */
    track_select(1);
    p1 = trk[1].preset;
    turn(EN_PRESET, 1);                                 /* T2: another (a level of its own) */
    hold(B_SAVE);
    hold(B_SAVE);
    bad += check("cache RAM: undo levels; SAVE held twice undoes T2's load, then T1's", undo_nlv == UNDO_LV_MAX &&
                 trk[0].preset == p0 && trk[1].preset == p1 && msg_is("UNDO T1"));
    save_redo();
    bad += check("  SAVE + OCT+ redoes the older first (T1), T2 stays undone", trk[0].preset != p0 && trk[1].preset == p1 &&
                 msg_is("REDO T1"));
    turn(EN_PRESET, 1);                                 /* a new load: the redo levels go */
    save_redo();
    bad += check("  a new load drops what was left to redo", msg_is("NOTHING TO REDO"));
    undo_lv = &undo_one;                                /* (back to the one level the other tests expect) */
    undo_nlv = 1;
    resource_host_cache_enabled = 0;
    ui_power_on();
    return bad;
}

/* NEW SONG: SAVE > PROJECT, KNOB 1 past TMPL (NEW), KNOB 3 picks it, OCT+ (notes unsaved: the dialog first); KEY: KNOB 1
 * ROOT, 2 SCALE, 3 TEMPO, OCT+; ROLES: KNOB k track k's, OCT+ creates it: the power-on sounds (no template), BASS on
 * track 2 (a BASS sound), the key on every track, the tempo, every pattern empty, no slot */
static int test_new_song(void)
{
    int bad = 0;
    uint32_t i, src, k, empty = 1;
    ui_power_on();
    stop_transport();
    my_steps(&trk[0]);
    go_page(GR_SLOTS);
    for (i = 0; i < 6u; i++) turn(EN_K1, 1);
    bad += check("PROJECT: KNOB 1 past TMPL shows NEW (SLOT itself stays)", ui.proj_new && song.g[G_SLOT] == PROJ_TMPL);
    turn(EN_K3, 1);
    press(B_OCTUP);
    bad += check("  OCT+ on NEW with notes not saved: the dialog first", ui.confirm == CF_NEW_SONG);
    press(B_OCTUP);
    bad += check("  .. then the KEY screen", new_on() && nw.on == 1);
    turn(EN_K1, 1); turn(EN_K1, 1);                     /* D */
    turn(EN_K3, 1);
    press(B_OCTUP);
    bad += check("  OCT+: the ROLES screen", nw.on == 2);
    turn(EN_K2, 1); turn(EN_K2, 1);                     /* track 2: BASS */
    press(B_OCTUP);
    for (k = 0; k < NTRK; k++) empty &= seq_is_empty(&trk[k]);
    song.sel = 1; cur_entry(&src, &k); song.sel = 0;
    bad += check("  OCT+ creates it: HOME, every pattern empty, D on every track, the tempo, track 2 a BASS sound, no slot",
                 !new_on() && ui.home && empty && trk[0].p[P_ROOT] == 2 && trk[3].p[P_ROOT] == 2 && song.g[G_BPM] == nw.bpm &&
                 entry_cat(src, k) == CAT_BASS && proj_cur == PROJ_NO_SLOT && msg_is("NEW SONG"));
    go_page(GR_SLOTS);
    for (i = 0; i < 6u; i++) turn(EN_K1, 1);
    turn(EN_K3, 1);
    press(B_OCTUP);
    bad += check("  a new song with nothing in it: no dialog", !ui.confirm && new_on());
    press(B_OCTDN);
    bad += check("  OCT- on KEY cancels", !new_on());
    ui_power_on();
    return bad;
}

static int test_prophet_pages(void)
{
    ui_power_on();set_engine_of(TSEL,ENGI_PROPHET);int bad=0,visible=0;
    bad+=check("selecting Prophet starts on its first factory program",!TSEL->user&&TSEL->preset==1u&&!memcmp(p5_patch_of(TSEL),&P5_FACTORY[0],sizeof(p5_patch_t)));
    uint32_t count,slot,source=preset_all_at(200u,&slot);
    bad+=check("all 201 Prophet factory entries precede empty user slots in both browsers",eng_list_pos(&count)==1u&&count==201u&&source==ENGI_PROPHET&&slot==200u);
    int factory=ENGINES[ENGI_PROPHET]->npresets==P5_FACTORY_N+1u;
    for(uint32_t k=1;k<=P5_FACTORY_N;k++){
        apply_preset_to(TSEL,k);char name[21];p5_patch_name(name,&P5_FACTORY[k-1u]);
        factory &= TSEL->preset==k&&!TSEL->user&&!memcmp(p5_patch_of(TSEL),&P5_FACTORY[k-1u],sizeof(p5_patch_t))&&
                   !strcmp(ENGINES[ENGI_PROPHET]->presets[k].name,name)&&!TSEL->p[P_DIST]&&!TSEL->p[P_REV];
    }
    bad+=check("all 200 Sequential programs are dry factory presets with exact native bytes, whatever the user bank holds",factory);
    apply_preset_to(TSEL,0);
    for(uint32_t i=0;i<NPAGES;i++)if(PAGES[i].scope==SC_P5||PAGES[i].scope==SC_P5STORE)visible+=page_visible(i);
    bad+=check("Prophet exposes all sixteen native editing and store pages",visible==16);
    go_title("P5 OSC A");p5_patch_t before=*p5_patch_of(TSEL);turn(EN_K1,1);before.raw[P5_FREQ_A]++;
    bad+=check("native oscillator knob changes its field and preserves opaque bytes",!memcmp(&before,p5_patch_of(TSEL),sizeof before));
    go_title("P5 STORE");p5_store_slot=128;turn(EN_K2,1);press(B_OCTUP);
    bad+=check("native STORE: OCT+ the slot's sheet, no NAME yet",!name_on()&&pop.on==POP_SHEET&&p5_store_slot==128);
    sheet_do("Save here");bad+=check("empty P128 opens native slot naming without overwrite",name_on()&&nm.slot==127&&name_limit()==20);
    nm.len=nm.cur=0;nm.s[0]=0;for(uint32_t k=0;k<20;k++){nm_insert((char)('A'+k));nm.cur++;}
    bad+=check("native NAME holds twenty characters and refuses a twenty-first",nm.len==20&&!nm_insert('Z')&&nm.s[20]==0);
    name_close();go_title("P5 STORE");p5_patch_of(TSEL)->raw[97]=255;sheet_do("Init sound");
    bad+=check("native INIT resets the full patch with sound undo available",p5_patch_of(TSEL)->raw[97]==0&&undo.keep);
    undo_swap();bad+=check("undo after native INIT restores opaque bytes",p5_patch_of(TSEL)->raw[97]==255);
    return bad;
}
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int bad = test_prophet_pages();
    bad += test_large_face();
    bad += test_home_notes();
    bad += test_stage();
    bad += test_capture();
    bad += test_undo_levels();
    bad += test_new_song();
    bad += test_sound_loads();
    bad += test_no_phrases();
    bad += test_rec();
    bad += test_midi();
    bad += test_save();
    bad += test_actions();
    bad += test_tracks();
    bad += test_grid();
    bad += test_screen();
    bad += test_favorites();
    bad += test_browser();
    bad += test_display_preferences();
    bad += test_information();
    bad += test_chain();
    bad += test_product_ux();
    bad += test_mono_screens();
    bad += test_roll();
    bad += test_panel();
    bad += test_layer();
    bad += test_name();
    bad += test_edit_cycle();
    bad += test_cz1_pages();
    bad += test_cz1_factory();
    bad += test_fm6_pages();
    bad += test_boot_template();
    bad += test_key_lights();
    bad += test_rec_gestures();
    bad += test_quick_save();
    bad += test_midi_status();
    bad += test_tempo_select();
#if MELODEE_SLICE
#if SMP_USER_SLOTS
    bad += test_slices();
#else
    bad += check("SAMPLE and SLICE unavailable; user sample storage retired", SMP_USER_SLOTS == 0 && !eng_ok(4) && !eng_ok(13) && !eng_ok(14));
#endif
#endif
    bad += test_quick_layers();
    bad += test_chord_page();
    bad += test_song_view();
    bad += test_popups();
    bad += test_native_env_lfo();
    bad += test_step_sheets();
    bad += test_mod_rows();
    bad += test_patterns_queue();
    bad += test_init_scales_patterns();
    bad += test_bughunt_ui();
    bad += test_bughunt_ui2();
    bad += test_piano_roll();
    bad += test_fm6_charts();
#if MELODEE_FM4
    bad += test_fm_charts();                        /* (DIGITAL's charts: built with MELODEE_FM4=1 only) */
#else
    bad += test_fm4_retired();
#endif

    if (getenv("UI_SCREEN_DIR")) chain_screens(getenv("UI_SCREEN_DIR"));
    printf(bad ? "ui: %d FAILED\n" : "ui: all passed\n", bad);
    return bad ? 1 : 0;
}
#endif
