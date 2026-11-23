/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Exercise actual snapshot capture/restore with both parameter namespaces and
 * every native-patch kind. The one native snapshot must not overwrite itself. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>

static void take_parameter(uint32_t scope,uint32_t id)
{
    page_t page={"TEST",FAM_SEQ,(uint8_t)scope,GR_NONE,{(uint8_t)id,255,255,255}};
    page_over=&page;momentary_take(0);page_over=0;
}
static void parameter_namespaces(void)
{
    ui_power_on();ui.home=0;song.sel=2;
    fm1_in.buttons|=1u<<panel.btn[B_LFO];
    int16_t level=trk[2].p[P_LEVEL],attack=trk[2].p[P_ATK],swing=song.g[G_SWING];
    take_parameter(SC_TRACK,P_LEVEL);trk[2].p[P_LEVEL]=1;
    take_parameter(SC_TRACK,P_ATK);trk[2].p[P_ATK]=2;
    take_parameter(SC_GLOBAL,G_SWING);song.g[G_SWING]=3;
    take_parameter(SC_TRACK,P_LEVEL); /* repeated edits retain the first value */
    assert(momentary.count==3 && momentary.track==2);
    momentary_restore();
    assert(trk[2].p[P_LEVEL]==level && trk[2].p[P_ATK]==attack && song.g[G_SWING]==swing);
    assert(!momentary.active && !momentary.count);
}
/* a synth's own page (SC_MOD: its patch, taken whole with the voice settings) edited momentarily: everything back */
static void native_snapshot(uint32_t engine,uint32_t id)
{
    ui_power_on();set_engine_of(TSEL,engine);ui.home=0;
    fm1_in.buttons|=1u<<panel.btn[B_LFO];
    uint8_t before[MOD3_PATCH_MAX];memcpy(before,patch_of(song.sel),sizeof before);
    int16_t voice=TSEL->p[P_VOICE],prio=TSEL->p[P_PRIO],detune=TSEL->p[P_DETUNE],alloc=TSEL->p[P_ALLOC];
    take_parameter(SC_MOD,id);assert(momentary.native==1u);
    for(uint32_t i=0;i<patch_len(engine);i+=7u)patch_of(song.sel)[i]^=1u;   /* (its own bytes, the settings past them too) */
    TSEL->p[P_VOICE]=0;TSEL->p[P_PRIO]=1;TSEL->p[P_DETUNE]=7;TSEL->p[P_ALLOC]=1;
    momentary_restore();
    assert(!memcmp(before,patch_of(song.sel),sizeof before));
    assert(TSEL->p[P_VOICE]==voice && TSEL->p[P_PRIO]==prio && TSEL->p[P_DETUNE]==detune && TSEL->p[P_ALLOC]==alloc);
}
int main(void)
{
    parameter_namespaces();
    native_snapshot(ENGI_FM6,FP_PR1);
    native_snapshot(ENGI_CZ,23u);                       /* (its WAVE: a panel id) */
    native_snapshot(ENGI_PROPHET,P5_FREQ_A);
    puts("Momentary snapshots: parameter namespaces, repeated edits and all native patches restore exactly");
    return 0;
}
