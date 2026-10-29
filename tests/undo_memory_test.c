/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>
static void history_audio_priority(void)
{
    ui_power_on();stop_transport();memset(resource,0,sizeof resource);
    note_transaction_ptr=0;recording_undo_ptr=0;resource_host_cache_enabled=1;
    for(uint32_t b=0;b<RESOURCE_BANKS-1u;b++){
        resource[b+1u].ptr=resource_banks[b].start;
        resource[b+1u].size=(uint32_t)(resource_banks[b].end-resource_banks[b].start);
        memset(resource[b+1u].ptr,0xa5,resource[b+1u].size);
    }
    track_t *t=TSEL;track_defaults_steps(t);t->p[P_SLEN]=64;
    t->step[63]=(step_t){{72,64,67,79},4,ST_NOTE,0,80};
    for(uint32_t i=0;i<8u;i++){
        load_begin(t,UNDO_PAT);t->step[63].vel++;load_end(t);undo_seal();
    }
    assert(undo_nlv==8u&&undo_top==8u);
    for(uint32_t i=0;i<8u;i++)undo_step(0);
    assert(t->step[63].vel==80u&&undo_top==0u);
    for(uint32_t i=0;i<8u;i++)undo_step(1);
    assert(t->step[63].vel==88u&&undo_top==8u);
    go_page(GR_ROLL);cursor_set(63);step_history_sync();
    for(uint32_t i=0;i<8u;i++){
        t->step[63].vel++;step_history_end();step_history_sync();
    }
    assert(step_history.pos==8u&&step_history_memory());
    for(uint32_t i=0;i<8u;i++)assert(step_history_apply(0));
    assert(t->step[63].vel==88u);
    for(uint32_t i=0;i<8u;i++)assert(step_history_apply(1));
    assert(t->step[63].vel==96u);
    step_t saved[NSTEP];memcpy(saved,t->step,sizeof saved);
    assert(resource[RES_STEP_UNDO].size&&resource[RES_LOAD_UNDO].size);
    void *audio=resource_get(RES_ENGINE0,RESOURCE_MAIN_CAPACITY);
    assert(audio && !resource[RES_STEP_UNDO].size&&!resource[RES_LOAD_UNDO].size);
    assert(!memcmp(saved,t->step,sizeof saved));
    assert(!step_history_apply(0));undo_step(0);assert(!undo_top);
    /* No room for history: the edit still succeeds, and stale snapshots are
     * never read after the engine has reused their memory. */
    step_history_sync();t->step[63].vel=97;step_history_end();
    assert(!step_history_context()&&t->step[63].vel==97);
    for(uint32_t b=0;b<RESOURCE_BANKS-1u;b++){
        uint8_t *p=resource[b+1u].ptr;
        assert(p[0]==0xa5&&p[resource[b+1u].size-1u]==0xa5);
    }
    memset(resource,0,sizeof resource);undo_clear();step_history_clear();
    resource_host_cache_enabled=0;ui_power_on();
}


static void history_without_cache(void)
{
    ui_power_on();stop_transport();undo_clear();step_history_clear();resource_host_cache_enabled=0;
    track_t *t=TSEL;track_defaults_steps(t);t->step[63]=(step_t){{72},1,ST_NOTE,0,80};
    assert(!resource[RES_LOAD_UNDO].size);
    for(uint32_t i=0;i<8u;i++){load_begin(t,UNDO_PAT);t->step[63].vel++;load_end(t);undo_seal();}
    assert(undo_top==8u&&undo_nlv==8u&&resource[RES_LOAD_UNDO].size==8u*sizeof(undo_t));
    for(uint32_t i=0;i<8u;i++)undo_step(0);assert(t->step[63].vel==80u);
    for(uint32_t i=0;i<8u;i++)undo_step(1);assert(t->step[63].vel==88u);
    undo_clear();assert(!resource[RES_LOAD_UNDO].size&&undo_lv==undo_sram&&undo_nlv==1u);
}
int main(void){assert(NPAT==16u&&NPAT_MIDI==8u&&NSTEP==64u&&RECORD_MAX==1024u);history_without_cache();history_audio_priority();puts("shared undo RAM: eight levels, cache failure, audio reclamation, stale histories and exhaustion passed");return 0;}
