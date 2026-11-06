/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The outgoing MIDI-device link's policy is the Bluetooth bridge's (platform/ble_own.h, on the radio's main-loop poll):
 * these are the app's handles on it. The incoming phone/computer link remains host-initiated. */
#if FM1B_BLE_CENTRAL && !defined(PLAT_HOST)
extern void mlb_ble_relink(int on);
extern void mlb_ble_hold(int on);
extern const char *mlb_ble_message(void);
extern const char *mlb_ble_device_name(void);
extern int mlb_ble_forget_device(void);
static void ml_bt_resume(void){mlb_ble_relink(1);}
static void ml_bt_pause(void){mlb_ble_relink(0);}
static int ml_bt_forget(void){return mlb_ble_forget_device();}
static void ml_bt_hold(int on){mlb_ble_hold(on);}
static const char *ml_bt_message(void){return mlb_ble_message();}
static const char *ml_bt_name(void){
    const char *name=mlb_ble_device_name();
    return name?name:btdev_name();
}
#else
static void ml_bt_resume(void){}
static void ml_bt_pause(void){}
static int ml_bt_forget(void){return 1;}
static void ml_bt_hold(int on){(void)on;}
static const char *ml_bt_message(void){return "";}
static const char *ml_bt_name(void){return btdev_name();}
#endif
