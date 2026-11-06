/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#pragma once
#include <stdint.h>
#if defined(PLAT_SMK37) && PLAT_SMK37 && !defined(MELODEE_SMK37)
#define MELODEE_SMK37 1    /* tools/build_base.py --board smk37: instrument.c gets it from the generated fm1.toml's o2_cflags */
#endif
#ifdef MELODEE_SMK37
void ml_input_smk(void);
#endif
uint32_t ml_ms(void);
uint32_t ml_ticks(void);
uint32_t ml_lock(void);
void ml_unlock(uint32_t token);
void ml_watchdog(void);
void ml_delay(uint32_t ms);
void ml_reboot(void);
void ml_input(uint32_t *buttons,uint32_t *keys,int32_t *enc);
void ml_lights(const uint8_t *bright,const uint8_t *dim,const uint8_t *masks);
void ml_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t color);
void ml_blit(uint32_t x,uint32_t y,uint32_t w,uint32_t h,const uint16_t *pixels);
void ml_sync(void);
void ml_lcd_power(uint32_t state);
uint32_t ml_master(void);
int ml_midi_in(uint32_t *packet,int *port);
uint32_t ml_midi_t24(void);
void ml_midi_out(uint32_t packet);
uint32_t ml_midi_room(void);
uint32_t ml_sysex_queued(void);
int ml_cache_ready(void);
int ml_midi_ready(void);
int ml_usb_lost(void);
void ml_usb_host(int on);
int ml_usb_host_state(void);
int ml_usb_host_msg(void);
void ml_usb_full(int on);
void ml_usb_play_gain(uint32_t q12);
uint32_t ml_flash_id(void);
void ml_midi_thru(uint32_t packet,int port);
int ml_sysex_send(const uint8_t *frame,uint32_t n);
int ml_read(uint32_t off,void *dst,uint32_t n);
int ml_erase(uint32_t off);
int ml_write(uint32_t off,const void *src,uint32_t n);
const uint8_t *ml_flash(uint32_t off);
int ml_flash_ok(void);
uint32_t ml_battery(void);
uint32_t ml_xruns(void);
void mlb_init(void);
void mlb_render(int32_t *lr,uint32_t n,uint32_t gain);
void mlb_frame(void);
void mlb_input_only(void);
void mlb_panic(void);
void mlb_overload(void);
void mlb_say(const char *a,const char *b);
int mlb_sysex(const uint8_t *p,uint32_t n);
int mlb_expect(const char *what,int *value);

void mlb_sysex_byte(uint8_t byte);

int ml_fault_frame(const uint32_t *frame);
void ml_worker_stop(void);
void ml_worker_crash(void);
int ml_worker_allowed(void);
#ifdef MELODEE_SMK37
int ml_boot_failed(void);
#endif
int ml_power_init(void);

int32_t mlb_sys_h(int face);
int32_t mlb_sys_w(int face,const char *s);
void mlb_sys_text(uint16_t *px,uint32_t w,uint32_t h,int32_t x,int32_t y,int face,const char *s,uint16_t fg);
int ml_ble_connected(void);
int ml_ble_state(void);
int ml_ble_linked(void);
int ml_ble_available(void);
int ml_ble_enabled(void);
void ml_ble_enable(int on);
void ml_ble_relink(int on);                     /* look for the last device (CONNECT OUT), or stop */
int ml_ble_device_connect(int i);               /* the scan's device i: 0 = connecting */
void ml_ble_device_drop(void);
int ml_ble_rf_ok(const uint8_t *copy);          /* a checked calibration copy (ble_vm.c) */
enum { ML_BLE_OFF, ML_BLE_STARTING, ML_BLE_READY };   /* plat.h BLE_* */
enum { ML_BLE_DEV_OFF, ML_BLE_DEV_SCANNING, ML_BLE_DEV_CONNECTING, ML_BLE_DEV_SEARCHING, ML_BLE_DEV_READY };   /* BLE_DEV_* */
void ml_ble_reconnect(void);
int ml_ble_device_state(void);
int32_t ml_ble_pairing_code(void);
int ml_ble_device_count(void);
int ml_ble_device_pick(void);
int ml_ble_device_found(int i,char *name,int *rssi);
const char *ml_ble_device_name(void);
void ml_ble_device_choose(int steps);
const char *ml_ble_device_action(void);
const char *ml_ble_page(int devices,int pairing);
void ml_ble_drop_host(void);
void ml_ble_forget_arm(int on);
int ml_ble_forget_state(void);
void ml_ble_forget_confirm(void);

void ml_panel_set(const uint8_t *buttons,const uint8_t *enc,const int8_t *direction);
void mlb_panel_calibrated(const uint8_t *buttons,const uint8_t *enc,const int8_t *direction);

void ml_usb_state(uint32_t *values);

#ifdef MELODEE_SMK37
/* the SMK-37's own controls, from the main loop: the 37 keys by keybed position (0 = C3), their last velocities, the
 * wheels, the pedal, and KNOB BANK, FADER BANK, <, > as bits 0..3 of extra */
typedef struct { uint64_t keys, retrig; uint8_t vel[37]; int32_t bend, mod; uint8_t pedal; uint32_t extra;
                 uint32_t pads, pads_hit; uint8_t pad_vel[16], pad_press[16], fader[4]; } ml_smk_t;
void ml_smk_read(ml_smk_t *s);
void ml_smk_lights(uint32_t extra, const uint32_t *pad_rgb);
void ml_master_set(uint32_t q12);
int32_t ml_k5(int lend);   /* K5 lent (1): its detents since the last call, and it turns no MASTER; 0: MASTER's */
void ml_smk_config(uint32_t vel, uint32_t trs_off);
#endif

void mlb_deadline(uint32_t us,uint32_t budget);
