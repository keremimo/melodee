/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#ifndef MELODEE_MIDI_KNOBS_H
#define MELODEE_MIDI_KNOBS_H
enum { MK_OFF, MK_GENERAL, MK_MINILAB, MK_PROFILES };
enum { MK_ABSOLUTE, MK_REL1, MK_REL2, MK_REL3, MK_MODES };
/* Device preferences, independent of projects. Opt in to avoid stealing synth CCs. */
static volatile uint8_t settings_midi_knobs, settings_midi_knob_mode;
/* MIDI LEARN's table, saved as the favorites' retired SAMPLE row (settings_persist.c, midi_knobs.c): the seven knobs by
 * role, the fourteen buttons by matrix id (the SMK-37: by FM-1 function, midi_knobs.c MLEARN_BIT). 21..27 the
 * buttons' note channels, 28..31 the tag */
#define MLEARN_ROW 4u
#define MLEARN_KNOBS 7u
#define MLEARN_BUTTONS 14u
#define MLEARN_N (MLEARN_KNOBS + MLEARN_BUTTONS)
static uint8_t mlearn_row[32];
/* MIDI LEARN's parameters: a CC (on its channel, or any) to a track's parameter, on every page. Saved as the retired rows
 * 5..7 of the favorites (VOICE, TRIO, WHEEL: settings_persist.c), one table: 23 x (CC (0: none), channel (0..15, 127
 * any), track, parameter (P_*)), each byte 7 bits, then the tag 'M', 'P', 1, 0 (else the rows' old stars, kept as they
 * are). In the audio pool (zeroed at boot, before the settings load). midi_knobs.c mparam_receive, menu_rows.c */
#define MPARAM_ROW0 5u
#define MPARAM_N 23u
#define MPARAM_ANY 127u
#define MPARAM_TAG 0x0001504Du                          /* 'M', 'P', 1, 0 (little-endian) */
static uint32_t mparam_row[MPARAM_N + 1u] __attribute__((section(".pool")));
/* a parameter learnable: a track's own (not reserved, not the project's scale: ROOT, SCL, TUNING, scale QNT, the MPC
 * degree, recording QNT) */
#define MPARAM_TARGET(id) ((id) < P_COUNT && !param_reserved(id) && (id) != P_ROOT && (id) != P_SCALE && (id) != P_TUNING && \
                           (id) != P_QUANT && (id) != P_MPCDEG && (id) != P_RECQ)
#endif
