/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Retired IDs remain dry and release every old tank, on inserts and shared FX. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>

static void buffers(void)
{
    const uint32_t retired[] = {FX_ECHO};
    const uint32_t sizes[] = {4096, 8192, 16384, 32768, 65536};
    ui_power_on();
    for (uint32_t r = 0; r < NELEM(retired); r++)
        for (uint32_t si = 0; si < FX_STATE_COUNT; si++)
            for (uint32_t b = 0; b < NELEM(sizes); b++) {
                for (uint32_t id = 0; id < RES_COUNT; id++) resource_release(id);
                assert(resource_get(RES_FX0+si, sizes[b]));
                fx_state[si].type = (uint8_t)retired[r]; fx_state[si].mix = 32768;
                int16_t p[4] = {(int16_t)retired[r],127,127,127}; fx_plan_t plan;
                fx_slot_prepare(&plan, si, p);
                assert(!plan.active && !resource[RES_FX0+si].size && !fx_state[si].type && !fx_state[si].mix);
                uint32_t failures = resource_failures;
                for (uint32_t k = 0; k < 32; k++) fx_slot_prepare(&plan, si, p);
                assert(!plan.active && !resource[RES_FX0+si].size && resource_failures == failures);
                /* An old slot cannot retain memory when replaced by a surviving effect. */
                assert(resource_get(RES_FX0+si, sizes[b])); fx_state[si].type = (uint8_t)retired[r];
                p[0] = FX_PHASER; fx_slot_prepare(&plan, si, p);
                assert(plan.active && plan.f->type == FX_PHASER && !resource[RES_FX0+si].size);
                fx_zero(&fx_state[si]);
            }
    puts("Retired FX: all 15 insert/shared slots reclaim 4/8/16/32/64 KiB tanks without allocation retries");
}
static void storage(void)
{
    ui_power_on();
    for (uint32_t tr = 0; tr < NPART; tr++) for (uint32_t s = 0; s < FX_SLOTS; s++) {
        trk[tr].p[P_FXTYPE+s*FX_PARAMS] = FX_ECHO;
        trk[tr].p[P_FXMIX+s*FX_PARAMS] = 127;
    }
    for (uint32_t s = 0; s < FX_SLOTS; s++) song.g[G_MFXTYPE+s*FX_PARAMS] = FX_ECHO;
    project_t q; project_store_t p; project_capture(&q);
    assert(proj_pack(&p,&q) && proj_import_same(&proj_scratch,&p,sizeof p));
    for (uint32_t tr = 0; tr < NPART; tr++) for (uint32_t s = 0; s < FX_SLOTS; s++) assert(!proj_scratch.t[tr].p[P_FXTYPE+s*FX_PARAMS]);
    for (uint32_t s = 0; s < FX_SLOTS; s++) assert(!proj_scratch.g[G_MFXTYPE+s*FX_PARAMS]);
    template_save(); template_load();
    for (uint32_t tr = 0; tr < NPART; tr++) for (uint32_t s = 0; s < FX_SLOTS; s++) assert(!trk[tr].p[P_FXTYPE+s*FX_PARAMS]);
    for (uint32_t s = 0; s < FX_SLOTS; s++) assert(!song.g[G_MFXTYPE+s*FX_PARAMS]);
    const uint32_t kept[] = {FX_MIDIVERB,FX_MIDIDELAY};
    for (uint32_t k = 0; k < NELEM(kept); k++) {
        TSEL->p[P_FXTYPE] = (int16_t)kept[k]; project_capture(&q);
        assert(proj_pack(&p,&q) && proj_import_same(&proj_scratch,&p,sizeof p));
        assert(proj_scratch.t[0].p[P_FXTYPE] == kept[k]);
    }
    puts("Retired FX: old project/template IDs load OFF; ROOM/CHORUS/MIDI insert IDs stay unchanged");
}
static void controls(void)
{
    ui_power_on();
    assert(FX_ECHO == 6 && FX_ROOM == 7 && FX_REVERB == 8 && FX_CHORUS == 9 && FX_MIDIVERB == 10 && FX_MIDIDELAY == 11);
    assert(!N_FX[FX_ECHO][0] && str_eq(N_FX[FX_REVERB],"REVERB") && !N_AUDIO_FX[FX_ECHO][0] && str_eq(N_AUDIO_FX[FX_REVERB],"REVERB"));
    assert(enum_orig(&TP[P_FXTYPE],FX_ECHO)==FX_OFF && enum_orig(&GP[G_MFXTYPE],FX_REVERB)==FX_REVERB);
    assert(enum_step(&TP[P_FXTYPE],FX_OFF,FX_OFF+1)==FX_MIDIVERB);
    assert(enum_step(&TP[P_FXTYPE],FX_MIDIVERB,FX_MIDIVERB+1)==FX_MIDIDELAY);
    assert(enum_orig(&TP[P_FXTYPE],FX_REVERB)==FX_OFF);
    assert(enum_rows(&TP[P_FXTYPE])==3 && enum_rows(&GP[G_MFXTYPE])==14);
    press(B_FX);assert(cur_page()->fam==FAM_FX);
    btn_down(B_FX);frames(800);assert(ui.layer==LAYER_FX);
    key_down(white(0));frame();assert(!gates() && kb_layer);key_up(white(0));frame();assert(!gates());
    btn_up(B_FX);frames(100);assert(!ui.layer);
    printf("Retired FX: picker holes skipped; FX keeps note keys playable; remaining slot state %zu bytes x %u\n",sizeof(fx_state_t),FX_STATE_COUNT);
}
int main(void) { buffers(); storage(); controls(); return 0; }
