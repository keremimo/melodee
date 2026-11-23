/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee's adapter to Charles Vestal's unchanged fm1-base btdev helper.
 * The helper and JieLi MIDI client retain their upstream notices. */
#include "btdev.h"
#include "ble_reconnect.h"
static uint8_t ml_radio_on=1,ml_forget,ml_forget_resume;
static uint32_t ml_forget_at;
int ml_ble_connected(void){return plat_ble_connected();}
int ml_ble_state(void){return plat_ble_state();}
int ml_ble_linked(void){return plat_ble_linked();}
int ml_ble_available(void){return FM1B_BLE;}
int ml_ble_enabled(void){return FM1B_BLE&&ml_radio_on;}
int ml_ble_device_state(void){return plat_ble_device();}
int32_t ml_ble_pairing_code(void){
#if FM1B_BLE && !defined(PLAT_HOST)
    extern int32_t ble_midi_pairing_code(void);
    return ble_midi_pairing_code();
#else
    return -1;
#endif
}
int ml_ble_device_count(void){return btdev_count();}
int ml_ble_device_pick(void){return btdev_pick();}
int ml_ble_device_found(int i,char *name,int *rssi){return btdev_found(i,name,rssi);}
const char *ml_ble_device_name(void){return ml_bt_name();}
void ml_ble_enable(int on){
    if(ml_forget==2)return;
    if(on)ml_bt_resume();else ml_bt_pause();
    ml_radio_on=(uint8_t)!!on;plat_ble_enable(on);
}
void ml_ble_drop_host(void){
#if FM1B_BLE && !defined(PLAT_HOST)
    extern void ble_midi_drop_host(void);
    if(plat_ble_state()==BLE_READY)ble_midi_drop_host();
#elif FM1B_BLE
    ble_conn=ble_link=0;
#endif
}
void ml_ble_reconnect(void){
    if(ml_forget==2)return;
    ml_ble_enable(1);
#if FM1B_BLE && !defined(PLAT_HOST)
    extern void ble_midi_reconnect(void);
    if(plat_ble_state()==BLE_READY)ble_midi_reconnect();
#elif FM1B_BLE
    ml_ble_drop_host();
#endif
}
void ml_ble_device_choose(int steps){
    if(ml_forget!=2&&!btdev_busy())btdev_choose(btdev_pick()+(steps>0?1:-1));
}
const char *ml_ble_device_action(void){
    if(ml_forget==2||plat_ble_state()!=BLE_READY)return "RADIO NOT READY";
    if(btdev_busy())ml_bt_pause();else ml_bt_resume();
    return btdev_word(btdev_save());
}
void ml_ble_forget_arm(int on){
    if(ml_forget==2)return;
    ml_forget=(uint8_t)(on&&plat_ble_state()==BLE_READY);ml_forget_at=plat_ms();
}
int ml_ble_forget_state(void){return ml_forget;}
void ml_ble_forget_confirm(void){
    if(ml_forget!=1||plat_ms()-ml_forget_at>=15000u)return;
    ml_forget=2;ml_forget_at=plat_ms();ml_forget_resume=ml_radio_on;
    ml_bt_pause();
    plat_ble_enable(0); /* let the link finish before removing the bonds */
}
const char *ml_ble_page(int devices,int pairing){
    const char *message="";
    if(ml_forget==1&&(!pairing||plat_ms()-ml_forget_at>=15000u))ml_forget=0;
    if(ml_forget==2){
        int ready=0,ok=0;
#if FM1B_BLE && !defined(PLAT_HOST)
        extern int ble_midi_can_forget(void),ble_midi_forget_all(void);
        ready=ble_midi_can_forget();
#else
        ready=1;
#endif
        if(ready&&plat_ms()-ml_forget_at>=250u){
#if FM1B_BLE && !defined(PLAT_HOST)
            ok=ble_midi_forget_all();
#else
            ok=1;
#endif
            if(ok)ok=ml_bt_forget();
            ml_forget=0;plat_ble_enable(ml_forget_resume);
            message=ok?"PAIRINGS CLEARED":"FORGET FAILED";
        }else if(plat_ms()-ml_forget_at>=6000u){
            ml_forget=0;plat_ble_enable(ml_forget_resume);message="FORGET FAILED";
        }
    }
    btdev_page(devices&&ml_radio_on&&ml_forget!=2);
    ml_bt_hold(pairing);                          /* GLOBAL > BLE open: no rounds looking for the last device */
    if(!message[0])message=ml_bt_message();
    if(!btdev_busy())btdev_choose(btdev_pick()); /* a new scan can have fewer devices */
    for(int event;(event=btdev_event())!=BTDEV_NONE;){
        if(!message[0])message=btdev_word(event);
    }
    return message;
}
