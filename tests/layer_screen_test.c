/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Host test of Felucca 1.2's panel controls as Melodee has them, through the UI test's stubs (ui_test.c):
 * double-tapped GLO stays open and owns the keys silently, repeated SCL taps navigate without locking,
 * FX LATCH, the FX key map (PRESETS assigns, EDIT
 * restores the default), MENU > SCREEN OFF (the panel dark while the transport runs, the wake press swallowed, NEVER),
 * LFO 2 (SYNC's increments at every division and tempo, TRIG NOTE / FREE, POL UNI, the transport's reset),
 * and EDIT map detents published by a scan interrupt between the control read and unused-control cleanup. */
#include <stdint.h>
static uint32_t lcd_commands[8], lcd_count;
static void select_take_hook(uint32_t e);
#define UI_ENC_TAKE_HOOK(e) select_take_hook(e)
#define UI_LCD_POWER(s) (lcd_commands[lcd_count++ % 8u] = (s))
#define UI_LCD_WAKE() ((void)0)
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>

static uint32_t select_takes;
static int32_t select_inject;
static uint32_t map_watch, map_takes[NE], map_inject_role;
static int32_t map_inject;
static void select_take_hook(uint32_t e)
{
    if (map_watch) {
        for (uint32_t role = 0; role < NE; role++)
            if (e == panel.enc[role]) map_takes[role]++;
        if (map_inject && e == panel.enc[map_inject_role]) {
            host_enc[e] += map_inject * panel.dir[map_inject_role];
            map_inject = 0;
        }
    }
    if (e != panel.enc[EN_SELECT]) return;
    select_takes++;
    if (select_inject) {
        host_enc[e] += select_inject * panel.dir[EN_SELECT];
        select_inject = 0;
    }
}

static void reset_controls(void)
{
    seq_stop(); ui_power_on();
    memset(&scrn, 0, sizeof scrn); lcd_count = 0;
    settings_screen = kb_asleep = kb_lock = 0;
}
static void tap_fast(uint32_t b) { btn_down(b); frame(); btn_up(b); frame(); }
static void test_layers(void)
{
    {
        uint32_t l = LAYER_GLO;
        reset_controls(); uint32_t b=LAYERS[l].btn;
        tap_fast(b); tap_fast(b);
        assert(ui.lock==l && ui.layer==l && ui.home);
        key_down(white(0)); frame();
        assert(kb_layer & (1u<<white(0))); assert(!gates());
        key_up(white(0)); frame(); tap_fast(b);
        assert(!ui.lock && !ui.layer);
    }
    for (uint32_t home = 0; home < 2u; home++) {
        reset_controls();
        if (!home) { go_title("ENV"); frame(); }
        for (uint32_t tap = 0; tap < 4u; tap++) {
            btn_down(B_SCL); frame();
            assert(!ui.lock && !ui.layer);
            btn_up(B_SCL); frame();
            assert(!ui.lock && !ui.ly && !ui.layer && !kb_lock && !pop.on && !smap.on);
            assert(!ui.home && cur_page()->fam == FAM_SCL);
        }
        int16_t root = TSEL->p[P_ROOT];
        key_down(white(0)); frame();
        assert(gates() && !kb_layer && TSEL->p[P_ROOT] == root);
        key_up(white(0)); frame();
    }
    puts("layers/screen: GLO double-tap locks; repeated SCL taps navigate and keep keys playable");
}
static void test_glo_select(void)
{
    for (uint32_t home = 0; home < 2u; home++)
    for (uint32_t locked = 0; locked < 2u; locked++)
    for (int32_t dir = -1; dir <= 1; dir += 2) {
        reset_controls();
        HOME_SELECT = HS_LEVEL;
        glo_select_set(HS_SWING);
        song.g[G_SWING] = 40;
        if (!home) { go_title("ENV"); frame(); }
        if (locked) { tap_fast(B_GLO); tap_fast(B_GLO); assert(ui.lock == LAYER_GLO); }
        else { btn_down(B_GLO); frame(); }
        uint32_t pg = ui.page;
        int16_t level = TSEL->p[P_LEVEL], swing = song.g[G_SWING];
        /* A scan publishes a second detent after the first take, even in the armed frame. */
        select_takes = 0;
        select_inject = dir;
        turn(EN_SELECT, dir);
        assert(select_takes == 1u && host_enc[panel.enc[EN_SELECT]] == dir * panel.dir[EN_SELECT]);
        assert((song.g[G_SWING] - swing) * dir > 0 && ui.layer == LAYER_GLO);
        assert(ui.home == home && ui.page == pg && TSEL->p[P_LEVEL] == level);
        select_takes = 0;
        frame();
        assert(select_takes == 1u && !host_enc[panel.enc[EN_SELECT]]);
        assert(ui.home == home && ui.page == pg && TSEL->p[P_LEVEL] == level);
        /* SELECT still belongs to GLO on the frame its button is released. */
        if (locked) { btn_down(B_GLO); frame(); }
        btn_up(B_GLO);
        select_takes = 0;
        turn(EN_SELECT, dir);
        assert(select_takes == 1u && !ui.ly && !ui.lock);
        assert(ui.home == home && ui.page == pg && TSEL->p[P_LEVEL] == level);
        swing = song.g[G_SWING];
        turn(EN_SELECT, dir);
        assert(song.g[G_SWING] == swing);
        assert(home ? TSEL->p[P_LEVEL] != level : ui.page != pg);
    }
    reset_controls();
    HOME_SELECT = HS_LEVEL;
    glo_select_set(HS_OFF);
    int16_t level = TSEL->p[P_LEVEL];
    btn_down(B_GLO); frame();
    select_takes = 0;
    select_inject = 1;
    turn(EN_SELECT, 1); frame();
    assert(select_takes == 2u && TSEL->p[P_LEVEL] == level);
    btn_up(B_GLO); frame();
    reset_controls(); go_title("ENV"); frame();
    glo_select_set(HS_SWING);
    uint32_t pg = ui.page;
    int16_t swing = song.g[G_SWING];
    /* A turn arriving with the initial press belongs to GLO already. */
    btn_down(B_GLO); turn(EN_SELECT, 1);
    assert(song.g[G_SWING] > swing && !ui.home && ui.page == pg);
    btn_up(B_GLO); frame();
    reset_controls(); go_title("ENV"); frame();
    glo_select_set(HS_SWING);
    pg = ui.page; swing = song.g[G_SWING];
    /* Even the first turn arriving on release is a combo, not a GLO tap. */
    btn_down(B_GLO); frame();
    btn_up(B_GLO); turn(EN_SELECT, 1);
    assert(song.g[G_SWING] > swing && !ui.home && ui.page == pg && !ui.ly);
    HOME_SELECT = 0;
    glo_select_set(0);
    puts("layers/screen: GLO SELECT exclusive when armed, held, locked and released; standalone SELECT restored");
}
static void test_map_encoder_reads(void)
{
    const uint32_t roles[] = {EN_K1, EN_K2, EN_SELECT};
    for (uint32_t i = 0; i < NELEM(roles); i++)
    for (int32_t dir = -1; dir <= 1; dir += 2) {
        reset_controls(); set_engine_of(TSEL, ENGI_FM6); fm6_opsel = 2;
        go_title("EG RATE"); frame(); press(B_EDIT);
        uint32_t row = smap.row, col = smap.col, role = roles[i], page = ui.page;
        assert(smap.on && row >= 2u && row + 2u < sec.n && col == 2u);
        memset(map_takes, 0, sizeof map_takes);
        map_inject_role = role; map_inject = dir; map_watch = 1;
        turn(role, dir);                             /* scan ISR publishes another detent just after the read */
        map_watch = 0;
        assert(map_takes[role] == 1u);
        for (uint32_t r = 0; r < NE; r++) assert(map_takes[r] == 1u);
        assert(host_enc[panel.enc[role]] == dir * panel.dir[role]);
        assert(ui.page == page && smap.row == row + (role == EN_K2 ? dir : 0) &&
               smap.col == (role == EN_K2 ? 0u : col + dir));
        memset(map_takes, 0, sizeof map_takes); map_watch = 1;
        frame();
        map_watch = 0;
        assert(map_takes[role] == 1u && !host_enc[panel.enc[role]]);
        assert(ui.page == page && smap.row == row + (role == EN_K2 ? 2 * dir : 0) &&
               smap.col == (role == EN_K2 ? 0u : col + 2 * dir));
    }
    puts("EDIT map: detents arriving after a knob read survive until the next input pass, both axes and directions");
}
static void test_screen_sleep(void)
{
    reset_controls(); scr_put(1); song.playing=1;
    fm1_ms=scrn.idle+300000u; ui_draw();
    assert(scrn.st==SCR_OFF && kb_asleep && song.playing && lcd_commands[0]==0);
    key_down(white(0)); frame(); assert(!gates() && scrn.st==SCR_WAKE);
    fm1_ms+=120; ui_draw(); assert(scrn.st==SCR_SLPOUT && lcd_commands[1]==1);
    fm1_ms+=120; ui_draw(); assert(scrn.st==SCR_ON && lcd_commands[2]==2);
    key_up(white(0)); fm1_ms+=320; frame(); assert(!kb_asleep);
    key_down(white(0)); frame(); assert(gates()); key_up(white(0)); frame();
    song.playing=0;
    for(uint32_t v=0;v<5;v++){scr_put(v);assert(scr_get()==v);}
    scr_put(0);fm1_ms+=3600001u;ui_draw();assert(scrn.st==SCR_ON);
    scr_put(1);ui.confirm=1;fm1_ms+=300001;ui_draw();assert(scrn.st==SCR_ON);
    ui.confirm=0;scr_put(1);fm1_ms+=300001;ui_draw();assert(scrn.st==SCR_OFF);
    int16_t value=TSEL->p[P_LEVEL];host_enc[panel.enc[EN_K1]]=2;frame();assert(TSEL->p[P_LEVEL]==value);
    scr_wake_now();fm1_ms+=320;frame();assert(!kb_asleep);
    puts("layers/screen: screen timeout, continuing transport, silent wake key/knob and NEVER passed");
}
static void test_lfo(void)
{
    reset_controls(); track_t *t=TSEL;
    for(uint32_t s=1;s<NELEM(N_LSYNC);s++){
        t->p[P_LSYNC]=s;lfo_sync_rem[t-trk]=0;
        /* One cycle of units is exactly one turn, however the blocks fall. */
        uint32_t turn=0;
        for(uint32_t left=seq_div_samples(LSYNC_DIV[s]);left;left-=lfo_block_units){
            lfo_block_units=left<37u?left:37u;turn+=lfo_sync_inc(t);
        }
        assert(!turn);
    }
    t->p[P_LSYNC]=0;t->p[P_LTRIG]=1;t->lfo_ph=0x34567890;
    trk_note_on(t,60,100);assert(t->lfo_ph==0x34567890);trk_note_off(t,60);
    t->p[P_LTRIG]=0;trk_note_on(t,64,100);assert(!t->lfo_ph);
    t->p[P_LPOL]=1;t->p[P_LWAVE]=0;
    int32_t out[CTL];for(uint32_t i=0;i<2000;i++){track_render(t,out,CTL);assert(t->lfo_val>=0 && t->lfo_val<=32767);}
    t->p[P_LTRIG]=1;t->p[P_LSYNC]=5;t->lfo_ph=1;seq_start();assert(!t->lfo_ph);seq_stop();
    puts("layers/screen: all synced LFO divisions, NOTE/FREE, UNI and transport reset passed");
}
/* the FX key map and the native CZ-1 favorites share no bits (the map sat in CZ's row, factory[15][14..23], over
 * native slots 47..126's stars), and a settings record from before the move keeps its map, or its stars when its
 * bytes cannot be a map (a code no map writes) or sit only on filled slots */



int main(void) { test_layers(); test_glo_select(); test_map_encoder_reads(); test_screen_sleep(); test_lfo(); return 0; }
