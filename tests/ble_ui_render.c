/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define MELODEE_BLE 1
#define UI_RENDER_NO_MAIN 1
#include "ui_render.c"

int main(void)
{
    const char *out = "build/ble-ui";
    rep = fopen("build/ble-ui/report.txt", "w"); assert(rep);
    static const char *const names[] = {"off", "scan", "connected", "pin_000042", "pin_000000", "pin_999999", "failed", "no_cal", "stage_pin", "presets_pin", "patterns_pin", "scales_pin", "settings_pin"};
    for (uint32_t face=0; face<2; face++) for (uint32_t p=0;p<UI_PAL_ENTRIES;p++)
        for (uint32_t scene=0;scene<13;scene++) {
            char label[80];
            setup(scene==9?S_PRESETS:scene==10?S_PATGRID:scene==11?S_SCALE_PICKER_EDO:scene==12?S_MENU:S_HOME); pal(p); render_pixel=face;
            if (face) PREF_BITS |= PREF_PIXEL; else PREF_BITS &= ~PREF_PIXEL;
            memset(&mb,0,sizeof mb); mb.on=mb.cal=1;
            ble_init((uint8_t[]){1,2,3,4,5,0xC6},1);
            smp_passkey=BLE_NO_PASSKEY; ui.menu=3; ble_row=0;
            if(scene==0)mb.on=0;
            if(scene==1||scene==2){
                mb.count=8;mb.scanning=scene==1;ble_row=scene==2?11:4;mb.ready=scene==2;
                for(uint32_t i=0;i<8;i++){mb.idx[i]=(uint8_t)i;str_cpy(mb.found.e[i].name,"LONG KEYBOARD 123",BLE_NAME_MAX+1);}
            }
            if((scene>=3&&scene<=5)||scene>=8){
                mb.attempt=1;bcen.st=BLE_CS_SETUP;bsmp.init=1;bsmp.st=I_CONFIRM;
                smp_passkey=scene==4?0:scene==5?999999:42;
                if(scene>=8)ui.menu=scene==12?1:0;
            }
#if MELODEE_USB_HOST
            if(scene==2) { uh.on=1; uh.run=0; }
            else { uh.on=uh.run=0; }
#endif
            if(scene==6)str_cpy(mb.failure,"CONNECTION TIMEOUT",sizeof mb.failure);
            if(scene==7)mb.on=mb.cal=0;
            snprintf(label,sizeof label,"%u/%s/%s",face,UI_PALETTES[p].name,names[scene]);cur_name=label;
            ui.force=1;draw(-1);lint();if(p==UI_GRAY_INDEX)mono_check();
            if(p==0)write_ppm(out,face?"PIXEL":"NIGHT",names[scene]);
        }
    fprintf(rep,"%u findings, %u off-gray pixels\n",nfind,mono_bad);fclose(rep);
    printf("BLE UI: both fonts, all palettes, discovery, pairing and header states: %u findings\n",nfind);
    return !!(nfind||mono_bad);
}
