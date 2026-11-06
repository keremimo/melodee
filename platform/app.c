/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Platform half of the bridge (from Saadet's, Kerem Kilic's). The instrument is a separate translation unit,
 * keeping its retained formats/types apart from the base's internal names. */
#include "bridge.h"
#include "store_map.h"
#ifdef PLAT_HOST
#include "ceiling.h"
#endif
void app_init(void){mlb_init();}
static void (*volatile ml_render_fn)(int32_t *,uint32_t,uint32_t)=mlb_render;
void app_render(int32_t *lr,uint32_t n,uint32_t gain){
    ml_render_fn(lr,n,gain);
#ifdef PLAT_HOST
    ml_ceiling(lr,n);
#endif
}
void app_panic(void){mlb_panic();}
void app_ui_init(void){mlb_frame();}
void app_frame(void){mlb_frame();}
void app_input_only(void){mlb_input_only();}
void app_say(const char *a,const char *b){mlb_say(a,b);}
int app_sysex(const uint8_t *p,uint32_t n){return mlb_sysex(p,n);}
int app_expect(const char *s,int *v){return mlb_expect(s,v);}
void app_overload(void){mlb_overload();}
uint32_t ml_ms(void){return plat_ms();}
uint32_t ml_ticks(void){
#ifdef PLAT_HOST
    return plat_ms()*24000u;
#else
    return fm1_ticks();
#endif
}
uint32_t ml_lock(void){return plat_lock();}
void ml_unlock(uint32_t f){plat_unlock(f);}
void ml_watchdog(void){
#ifndef PLAT_HOST
    fm1_wdt_feed();
#endif
}
void ml_delay(uint32_t ms){
#ifndef PLAT_HOST
    fm1_delay_ms(ms);
#else
    (void)ms;
#endif
}
void ml_reboot(void){
#ifndef PLAT_HOST
    radio_clean_exit();fm1_reboot();
#endif
}
void ml_input(uint32_t *buttons,uint32_t *keys,int32_t *enc){
    static const uint8_t bm[14]={2,3,4,5,6,7,8,9,10,11,12,13,0,1};
    static const uint8_t em[7]={0,1,6,2,3,4,5};
    uint32_t b;*buttons=0;*keys=plat_keys();
#if defined(PLAT_HOST)||defined(MELODEE_SMK37)
    /* The SMK-37: plat_buttons and plat_enc read the 18 buttons and 8 encoders by the base's panel map (K5 is its
     * virtual MASTER), and the UI gets the FM-1's 14 functions and 7 roles in the FM-1's default ids */
    b=plat_buttons();
#else
    *buttons=fm1_in.buttons;b=remote.btn;
#endif
    for(uint32_t i=0;i<14u;i++)if(b&(1u<<i))*buttons|=1u<<bm[i];
    #if defined(PLAT_HOST)||defined(MELODEE_SMK37)
    for(uint32_t i=0;i<7u;i++)enc[em[i]]=plat_enc((int)i);
#else
    uint32_t f=plat_lock();
    for(uint32_t i=0;i<7u;i++)enc[i]=fm1_enc_take(i);
    for(uint32_t i=0;i<7u;i++){enc[em[i]]+=remote.enc[i];remote.enc[i]=0;}
    plat_unlock(f);
#endif
}
#ifdef MELODEE_SMK37
void ml_smk_read(ml_smk_t *s){
    s->keys=plat_keys37();s->retrig=plat_keys37_retrig();
    for(int i=0;i<37;i++)s->vel[i]=(uint8_t)plat_key_vel(i);
    s->bend=plat_wheel(PLAT_WHEEL_PITCH);s->mod=plat_wheel(PLAT_WHEEL_MOD);s->pedal=(uint8_t)plat_pedal();
    s->extra=plat_buttons()>>B_KBANK;
    s->pads=plat_pads();s->pads_hit=plat_pads_pressed();
    for(int i=0;i<16;i++){s->pad_vel[i]=(uint8_t)plat_pad_vel(i);s->pad_press[i]=(uint8_t)plat_pad_press(i);}
    for(int i=0;i<4;i++)s->fader[i]=(uint8_t)plat_fader(i);
}
void ml_master_set(uint32_t q12){plat_master_set(q12);}
int32_t ml_k5(int lend){return plat_k5_lend(lend);}
void ml_smk_config(uint32_t vel,uint32_t trs_off){
    static uint32_t was_vel=255u,was_off=255u;
    if(vel!=was_vel){was_vel=vel;plat_vel_curve((int)vel);}
    if(trs_off!=was_off){was_off=trs_off;plat_trs_out(!trs_off);}
}
/* the LEDs of the SMK's own buttons, and PLAY's red, in plat_lights' bits (ui_smk.c smk_led_extra) */
static uint32_t ml_smk_lit;
void ml_smk_lights(uint32_t extra,const uint32_t *pad_rgb){
    ml_smk_lit=(extra&15u)<<B_KBANK|((extra>>4)&1u)<<B_PLAY;
    for(int i=0;i<16;i++)plat_pad_rgb(i,pad_rgb[i]);
}
#ifndef PLAT_HOST
/* The UI draws the FM-1's LED cells (instrument.c FM1_KEYMAP): where each function's is, (column << 3) | row, in plat.h's
 * button order. tests/build_targets_test.py checks this against FM1_KEYMAP */
static const uint8_t ml_btn_cell[14]={49,33,17,1,74,66,57,41,25,9,82,58,4,12};
_Static_assert(ML_SMK_DATA_LO==FL_DATA_LO&&ML_SMK_DATA_HI==FL_DATA_HI&&ML_SMK_GLOB_LO==FL_GLOB_LO&&ML_SMK_GLOB_HI==FL_GLOB_HI&&
               ML_SMK_LOW_LO==FL_XDATA0_LO&&FL_XDATA0_HI==FL_AUTO_LO&&FL_AUTO_HI==FL_XDATA1_LO&&ML_SMK_LOW_HI==FL_XDATA1_HI&&
               ML_FLASH_ID==FL_JEDEC_ID,"store_map.h is not this board's flash map");
#endif
#endif
void ml_lights(const uint8_t *bright,const uint8_t *dim,const uint8_t *masks){
#if defined(MELODEE_SMK37)&&!defined(PLAT_HOST)
    /* the UI's three dim planes, at the board's levels: a plane at full duty lit, 1/2 and 1/4 mid, 1/8 the glow */
    uint32_t lit=0,mid=0,glow=0;
    for(uint32_t i=0;i<14u;i++){
        uint32_t c=ml_btn_cell[i]>>3,bit=1u<<(ml_btn_cell[i]&7u);
        if(bright[c]&bit)lit|=1u<<i;
        for(uint32_t p=0;p<3u;p++)if(masks[p]!=255u&&(dim[p*11u+c]&bit)){
            if(!masks[p])lit|=1u<<i;
            else if(masks[p]<=3u)mid|=1u<<i;
            else glow|=1u<<i;
        }
    }
    /* SEQ PLAY's own LED is its green (the base's LED_PLAY_GREEN); B_PLAY would light its red */
    {uint32_t play=1u<<B_PLAY;
     if(lit&play)lit=(lit&~play)|LED_PLAY_GREEN;
     if(mid&play)mid=(mid&~play)|LED_PLAY_GREEN;
     if(glow&play)glow=(glow&~play)|LED_PLAY_GREEN;}
    lit|=ml_smk_lit;
    plat_lights(lit,0,glow,0,2);plat_lights_mid(mid,0);
#elif !defined(PLAT_HOST)
    for(uint32_t i=0;i<11u;i++)fm1_led[i]=bright[i];
    for(uint32_t i=0;i<33u;i++)ml_led_planes[i/11u][i%11u]=dim[i];
    for(uint32_t i=0;i<3u;i++)ml_led_masks[i]=masks[i];
#else
    (void)bright;(void)dim;(void)masks;
#endif
}
void ml_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t c){lcd_fill(x,y,w,h,c);}
void ml_blit(uint32_t x,uint32_t y,uint32_t w,uint32_t h,const uint16_t *p){lcd_blit(x,y,w,h,p);}
void ml_sync(void){lcd_sync();}
void ml_lcd_power(uint32_t s){
#ifndef PLAT_HOST
    lcd_sync();
    if(s==0){lcd_cmd(0x28);fm1_lcd_backlight(0);}
    else if(s==1)lcd_cmd(0x11);
    else{lcd_cmd(0x29);fm1_lcd_backlight(1);}
#else
    (void)s;
#endif
}
uint32_t ml_master(void){return plat_master();}
/* port: Melodee's sources (firmware/src/usb.c midi_enqueue): 1 USB (a computer, or the USB host's device), 2 the TRS
 * jack, 3 Bluetooth (either link) */
int ml_midi_in(uint32_t *p,int *port){
    int from;
    if(!plat_midi_in(p,&from))return 0;
    *port=from==PORT_TRS?2:from==PORT_BLE?3:1;
    *p&=~0xF0u;return 1;
}
uint32_t ml_midi_t24(void){return plat_midi_in_t24();}
void ml_midi_out(uint32_t p){plat_midi_send(p,TO_USB|TO_BLE);}
uint32_t ml_midi_room(void){return plat_midi_out_room(TO_USB);}
uint32_t ml_sysex_queued(void){return plat_sysex_queued();}
/* the cache RAM's test passed, all 7 ways Melodee's (no .cache_ram objects: app.ld) */
int ml_cache_ready(void){
    const struct plat_cache_ram *c=plat_cache_ram();
    return c&&c->status==PLAT_CR_OK&&c->bytes>=0x7000u;
}
int ml_midi_ready(void){
#ifdef PLAT_HOST
    return 1;
#else
    return plat_usb_session()!=0u||plat_usb_host_state()==USBH_CONNECTED;
#endif
}
/* 1 once when the computer's session ends (a replug, the host gone) or the USB host's device goes: Melodee releases
 * what that port held (seq.c midi_usb_lost) */
int ml_usb_lost(void){
    static uint32_t was;
    uint32_t now=plat_usb_session();
    int lost=was&&now!=was;
    was=now;
    return lost|plat_usb_lost();
}
void ml_usb_host(int on){plat_usb_host(on);}
int ml_usb_host_state(void){return plat_usb_host_state();}
/* the base's USBH_MSG_* as Melodee's UH_MSG_* (melodee_app.c) */
int ml_usb_host_msg(void){
    int m=plat_usb_host_msg();
    return m==USBH_MSG_MIDI?1:m==USBH_MSG_LOST?2:m==USBH_MSG_NOMIDI?3:m==USBH_MSG_LOW?4:0;
}
void ml_usb_full(int on){plat_usb_audio_full(on);}
void ml_usb_play_gain(uint32_t q12){plat_usb_play_gain(q12);}
/* MIDI THRU: a message from port (ml_midi_in's) out where it did not come from. TO_BLE is both links, so BLE in goes
 * to USB only (it would echo to its own link); TRS has no out. Continuous streams leave a busy ring's last slots to
 * the notes and the keys. */
void ml_midi_thru(uint32_t p,int port){
    uint32_t to=port==PORT_USB?TO_BLE:port==PORT_TRS?TO_USB|TO_BLE:TO_USB,st=(p>>8)&0xF0u,cc=(p>>16)&127u;
    int stream=st==0xA0u||st==0xD0u||st==0xE0u||(st==0xB0u&&cc!=64u&&cc!=120u&&cc!=123u);
    if(stream&&plat_midi_out_room(to)<16u)return;
    plat_midi_send(p,to);
}
int ml_sysex_send(const uint8_t *p,uint32_t n){
#ifdef PLAT_HOST
    plat_sysex_send(p,n);return 0;
#else
    return ota_wire_send(p,n);
#endif
}
#include "ble_devices.h"
#include "ble_saved.h"
#if FM1B_BLE && !defined(PLAT_HOST)   /* (platform/ble_own.h, in the device's bt_own.c) */
extern void mlb_ble_relink(int on);
extern int mlb_ble_rf_ok(const uint8_t *copy);
void ml_ble_relink(int on){mlb_ble_relink(on);}
int ml_ble_rf_ok(const uint8_t *copy){return mlb_ble_rf_ok(copy);}
#else
void ml_ble_relink(int on){(void)on;}
int ml_ble_rf_ok(const uint8_t *copy){(void)copy;return 0;}
#endif
int ml_ble_device_connect(int i){return plat_ble_connect(i);}
void ml_ble_device_drop(void){plat_ble_disconnect();}
#ifdef APP_BLE_STORE
/* The address and the computer's bond: Melodee's settings trailer (ble_saved.h), as its own stack kept them */
int app_ble_load(struct plat_ble_rec *r){
    const ml_ble_saved_t *s=mlb_ble_saved();
    if(!s->valid)return 0;
    for(int i=0;i<6;i++)r->addr[i]=s->addr[i];
    r->bonded=(uint8_t)(s->bond[0]==0xB5u&&s->bond[1]==1u);
    if(r->bonded){
        for(int i=0;i<8;i++)r->bond_rand[i]=s->bond[2+i];
        r->bond_ediv=(uint16_t)(s->bond[10]|s->bond[11]<<8);
        for(int i=0;i<16;i++)r->bond_ltk[i]=s->bond[12+i];
    }
    return 1;
}
int app_ble_save(const struct plat_ble_rec *r){
    ml_ble_saved_t *s=mlb_ble_saved();
    for(int i=0;i<6;i++)s->addr[i]=r->addr[i];
    s->valid=1;
    for(int i=0;i<28;i++)s->bond[i]=0;
    if(r->bonded){
        s->bond[0]=0xB5u;s->bond[1]=1u;
        for(int i=0;i<8;i++)s->bond[2+i]=r->bond_rand[i];
        s->bond[10]=(uint8_t)r->bond_ediv;s->bond[11]=(uint8_t)(r->bond_ediv>>8);
        for(int i=0;i<16;i++)s->bond[12+i]=r->bond_ltk[i];
    }
    mlb_ble_dirty();
    return 0;
}
#endif
#ifdef APP_SYS_TEXT
/* The base's own screens (boot, crash, safe mode, calibration, the splash) on its canvas, in Melodee's faces. */
static int32_t app_sys_h(int face){return mlb_sys_h(face);}
static int32_t app_sys_w(int face,const char *s){return mlb_sys_w(face,s);}
static void app_sys_text(int32_t x,int32_t y,int face,const char *s,uint16_t c){mlb_sys_text(cv_px,cv_w,cv_h,x,y,face,s,c);}
#endif
static int ml_data_ok(uint32_t off,uint32_t n){return ML_DATA_OK(off,n);}
#ifdef PLAT_HOST
static uint8_t ml_nor[0x100000];
static uint8_t ml_nor_ready;
static void ml_nor_init(void){if(!ml_nor_ready){memset(ml_nor,255,sizeof ml_nor);ml_nor_ready=1;}}
#endif
uint32_t ml_flash_id(void){
#ifdef PLAT_HOST
    return 0x856014u;
#else
    return flash_ok?flash_jedec:0u;
#endif
}
int ml_flash_ok(void){
#ifdef PLAT_HOST
    ml_nor_init();return 1;
#else
    return flash_ok!=0;
#endif
}
const uint8_t *ml_flash(uint32_t off){
    if(!ml_data_ok(off,1u))return 0;
#ifdef PLAT_HOST
    ml_nor_init();return ml_nor+off;
#else
    return flash_ok?(const uint8_t *)FL_XIP(off):0;
#endif
}
int ml_read(uint32_t off,void *dst,uint32_t n){
    if(!ml_data_ok(off,n))return -1;
#ifdef PLAT_HOST
    ml_nor_init();memcpy(dst,ml_nor+off,n);return 0;
#else
    return flash_ok?st_read(off,dst,n):-1;
#endif
}
int ml_erase(uint32_t off){
    if((off&4095u)||!ml_data_ok(off,4096u))return -1;
#ifdef PLAT_HOST
    ml_nor_init();memset(ml_nor+off,255,4096);return 0;
#else
    uint32_t took;audio_silence();return flash_ok?fl_erase4k(off,&took):-1;
#endif
}
int ml_write(uint32_t off,const void *src,uint32_t n){
    if(!ml_data_ok(off,n))return -1;
#ifdef PLAT_HOST
    ml_nor_init();const uint8_t *p=src;for(uint32_t i=0;i<n;i++)ml_nor[off+i]&=p[i];return 0;
#else
    return flash_ok?fl_write(off,src,n):-1;
#endif
}
uint32_t ml_xruns(void){return plat_xruns();}
uint32_t ml_battery(void){
#ifdef PLAT_HOST
    return 600;
#else
    static uint32_t last,at;
    if((uint32_t)(fm1_ms-at)<1000u)return last;
#ifdef MELODEE_SMK37
    if(fm1_in.battery)last=fm1_in.battery; /* mux C's divider, from the board's scan: not yet measured on a unit */
    at=fm1_ms;return last;
#else
    uint32_t f=plat_lock();
    if(!fm1_an.con&&!fm1_an.pend){int32_t v=fm1_adc_read(FM1_ADC_BATT);if(v>0)last=(uint32_t)v;at=fm1_ms;}
    plat_unlock(f);return last;
#endif
#endif
}

#ifndef PLAT_HOST
#define FM1_POWER_EXTERNAL_HAL 1
#include "../firmware/hal/fm1_power.h"
int ml_power_init(void){fm1_power_ready=(uint8_t)fm1_power_init();return fm1_power_ready;}
void app_cstart(void){ml_worker_stop();}
void app_fault(const void *crash){(void)crash;ml_worker_crash();}
int ml_worker_allowed(void){return fm1_power_ready&&!bootguard.failed;}
#endif
#ifdef MELODEE_SMK37
#ifdef PLAT_HOST
int ml_boot_failed(void){return 0;}
#else
int ml_boot_failed(void){return bootguard.failed!=0u;}
#endif
#endif

void ml_panel_set(const uint8_t *buttons,const uint8_t *enc,const int8_t *direction){
#if !defined(PLAT_HOST)&&!defined(MELODEE_SMK37) /* (the SMK's map is the base's alone: 18 buttons, its calibration) */
    for(uint32_t i=0;i<14u;i++)panel.btn[i]=buttons[i];
    for(uint32_t i=0;i<7u;i++){panel.enc[i]=enc[i];panel.dir[i]=direction[i];}
#else
    (void)buttons;(void)enc;(void)direction;
#endif
}

void ml_usb_state(uint32_t *v){
#ifdef PLAT_HOST
    v[0]=1;v[1]=0;v[2]=1;v[3]=0;v[4]=0;
#else
    v[0]=usb.config;v[1]=usb.resets;v[2]=usb.up;v[3]=usb.suspended;v[4]=usb.rx_pkts;
#endif
}
