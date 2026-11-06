/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#pragma once
#include <stdint.h>
#include "bridge.h"
#if !defined(MELODEE_SMK37)
#define APP_NAME "MELODEE"
#elif PLAT_SMK37_MODEL == 1
#define APP_NAME "MELODEE PRO"
#elif PLAT_SMK37_MODEL == 2
#define APP_NAME "MELODEE ELITE"
#else
#define APP_NAME "MELODEE MKE"
#endif
#define APP_DATA_FAMILY "MELODEE"                  /* the SMK-37 store's owner (the base's plat_smk37.c) */
#define APP_BLOCK 128u
/* The USB recording's ring is written when a render ends, so a render past about 60 % of its half runs it dry: repeated
 * frames in the recording, never on the speaker or headphones. A half more in hand: 128 frames, +2.9 ms of recording
 * latency. tools/base_overlay.py keeps the ring in POOL, where the base's FELUCCA_UAC_RING_ATTR would put it. */
#define FM1B_UAC_DEEP 1
#define APP_SPLASH_RGB 0xFBA0u
#define APP_CANVAS_PX (240u*24u)
#define APP_SYSEX
#define APP_HOST_EXPECT
#define APP_BTDEV                                   /* lib/btdev.h: the Bluetooth MIDI devices (ble_devices.h) */
#ifndef PLAT_HOST
#define APP_SYS_TEXT                              /* the base's own screens in Melodee's faces (platform/app.c) */
#endif
/* The base's own store is not used (tools/base_overlay.py: plat_store_* answer none): Melodee's storage.c keeps every
 * object, the panel's calibration in its settings, in the sectors it always had. The map's pair is never written. */
enum { OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u}
/* Bluetooth's address and bond in Melodee's settings (platform/ble_saved.h, app.c), the radio started at power-on when
 * it was left on (Settings > Bluetooth Radio) */
#define APP_BLE_STORE
#define APP_BLE_AT_BOOT (mlb_ble_at_boot())
int mlb_ble_at_boot(void);
#define APP_OVERLOAD                                /* a render over 90 % of its time: shed a voice (melodee_app.c) */
void app_overload(void);
void app_init(void);
void app_render(int32_t *lr,uint32_t n,uint32_t gain);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a,const char *b);
int app_sysex(const uint8_t *p,uint32_t n);
int app_expect(const char *what,int *value);

#ifndef PLAT_HOST
#define APP_CSTART
#define APP_FAULT
void app_cstart(void);
void app_fault(const void *crash);
#endif
