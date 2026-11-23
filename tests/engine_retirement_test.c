/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"
int main(void)
{
    int bad=0; reset(); up_boot();
    bad+=check("2.0 handles: none 0, the MIDI Track 1, the manifest's synths from 2, the user kinds past the handles",
               ENG_H_NONE==0 && ENGI_MIDI==1 && ENG_H_SHIP0==2 && NENGINES<=32u && USER_GENERAL>=NENGINES && USER_NATIVE_P5>=NENGINES &&
               eng_map[ENGI_MIDI].uid==SHIP_ID_MIDI && eng_map[ENGI_PROPHET].uid==SHIP[0].uid && eng_map[ENGI_808].uid==SHIP[4].uid && eng_map[ENGI_SAMPLER].uid==SHIP[9].uid);
    uint8_t entry[EMAP_ENTRY]={0};entry[0]=9;mod_put64(entry+4,0x1122334455667788ULL);memcpy(entry+12,"GONE",4);
    uint32_t from,e=eng_map_get(entry,&from);
    bad+=check("a synth a store names that is not here: a lent handle, its name, silent, not offered",
               from==9 && e<NENGINES && e>=ENG_H_FREE0 && eng_silent(e) && !eng_ok(e) && !strcmp(eng_name(e),"GONE") && !eng_state_size(e));
    set_engine_of(TSEL,e);
    TSEL->preset=3;
    for(uint32_t k=0;k<8;k++)TSEL->p[P_E0+k]=(int16_t)(17+k);
    project_capture(&proj_scratch);
    static project_t saved;
    saved=proj_scratch;
    bad+=check("its project keeps the synth, preset and edit values",!project_restore_runtime(&saved) && TSEL->eng_req==e && TSEL->preset==3 && !memcmp(TSEL->p+P_E0,saved.t[song.sel].p+P_E0,16));
    bad+=check("its user preset stays its",!up_store(0,"GONE") && up_engine(0)==e);
    int16_t values[P_COUNT];up_values(up_rec(0),values);
    bad+=check("its user preset keeps the edit values",!memcmp(values+P_E0,TSEL->p+P_E0,16));
    for(uint32_t k=0;k<eng_nshown;k++)bad+=check("browser lists only available engines",eng_ok(eng_vis(k)) && eng_vis(k)!=e);
    printf("Engine identity test %s\n",bad?"FAILED":"passed");return !!bad;
}
