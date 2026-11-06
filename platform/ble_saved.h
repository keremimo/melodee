/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* What Melodee keeps for Bluetooth, byte for byte firmware/src/midi_ble.h's mel_ble_saved_t: the settings record's
 * trailer (project.c), so a unit updated onto fm1-base keeps its address, its computer's bond, the radio's calibration
 * copy and the last device. Shared by the instrument (melodee_app.c, which saves it with its settings) and the platform
 * (app.c app_ble_load / app_ble_save, ble_own.h's calibration and last device). */
#pragma once
#include <stdint.h>
#define ML_BLE_RF_COPY 100u                     /* ble_vm.h BLE_RF_COPY_SIZE */
#define ML_BLE_DEV_STORE 70u                    /* ble_store.h BLE_DEV_STORE_SIZE */
typedef struct {
    uint8_t addr[6], valid, flags;              /* flags: MB_VISIBLE, MB_RADIO */
    uint8_t bond[28], rf[ML_BLE_RF_COPY], dev[ML_BLE_DEV_STORE];   /* bond: B5 01, rand[8], ediv (LE), ltk[16] */
    uint32_t magic;
} ml_ble_saved_t;
_Static_assert(sizeof(ml_ble_saved_t) == 212u, "midi_ble.h mel_ble_saved_t's layout");
#define MEL_BLE_MAGIC 0x31454C42u /* BLE1 */
#define MB_VISIBLE 1u   /* Mode VISIBLE: advertise for a computer or phone, never scan */
#define MB_RADIO 2u     /* the radio as last set: started again at power-on */
ml_ble_saved_t *mlb_ble_saved(void);            /* the instrument's copy (a fresh one if it had none) */
void mlb_ble_dirty(void);                       /* changed: the settings save it (project.c) */
int mlb_ble_at_boot(void);                      /* the radio was on when last set (app.h APP_BLE_AT_BOOT) */
