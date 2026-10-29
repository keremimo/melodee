/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#ifndef MELODEE_RECORDING_PREFERENCES_H
#define MELODEE_RECORDING_PREFERENCES_H
#define CLICK_LEVEL_DEFAULT 12u   /* CLICK LEVEL in % of DAC full scale: the old MID at the default MASTER (0.5) */
static volatile uint8_t settings_click, settings_click_level = CLICK_LEVEL_DEFAULT, settings_countin;
static uint8_t settings_preview, settings_chord_add, settings_screen;
static uint8_t settings_autosave = 1;
#endif
