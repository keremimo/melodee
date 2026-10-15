/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"
int main(void)
{
    int bad=0; reset(); up_boot();
    bad+=check("engine and native collection IDs remain unchanged",NENGINES==22 && ENGI_WT==21 && USER_GENERAL==16 && USER_NATIVE_FM==17 && USER_NATIVE_CZ==18 && ENGI_PROPHET==19 && USER_NATIVE_P5==20);
    const uint8_t retired[]={2,5,6,7,9,11};
    for(uint32_t j=0;j<NELEM(retired);j++) {
        uint32_t e=retired[j];
        bad+=check("retired engine absent from browser and DSP resources",!eng_ok(e) && !ENGINES[e]->npresets && !eng_state_size(e));
        favorite_set(e,3,1);
        set_engine_of(TSEL,e);
        TSEL->preset=3;
        for(uint32_t k=0;k<8;k++)TSEL->p[P_E0+k]=(int16_t)(17+k);
        project_capture(&proj_scratch);
        static project_t saved;
        saved=proj_scratch;
        bad+=check("old project keeps retired identity, preset and edit values",!project_restore_runtime(&saved) && TSEL->eng_req==e && TSEL->preset==3 && !memcmp(TSEL->p+P_E0,saved.t[song.sel].p+P_E0,16));
        bad+=check("retired general user patch remains identified by its original engine",!up_store(j,"RETIRED") && up_engine(j)==e);
        int16_t values[P_COUNT];up_values(up_rec(j),values);
        bad+=check("retired user patch retains all native edit values",!memcmp(values+P_E0,TSEL->p+P_E0,16));
        bad+=check("retirement does not clear existing favorite identifiers",favorite_has(e,3));
    }
    for(uint32_t k=0;k<NENG_SHOWN;k++)bad+=check("browser lists only available engines",eng_ok(eng_vis(k)));
    printf("Engine retirement test %s\n",bad?"FAILED":"passed");return !!bad;
}
