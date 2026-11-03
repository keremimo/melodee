/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#ifndef MELODEE_MIDI_KNOBS_H
#define MELODEE_MIDI_KNOBS_H
enum { MK_OFF, MK_GENERAL, MK_MINILAB, MK_PROFILES };
enum { MK_ABSOLUTE, MK_REL1, MK_REL2, MK_REL3, MK_MODES };
/* Device preferences, independent of projects. Opt in to avoid stealing synth CCs. */
static volatile uint8_t settings_midi_knobs, settings_midi_knob_mode;
/* MIDI LEARN's table, saved as the favorites' retired SAMPLE row (settings_persist.c, midi_knobs.c): the seven knobs by
 * role, the fourteen buttons by matrix id. 21..27 the buttons' note channels, 28..31 the tag */
#define MLEARN_ROW 4u
#define MLEARN_KNOBS 7u
#define MLEARN_BUTTONS 14u
#define MLEARN_N (MLEARN_KNOBS + MLEARN_BUTTONS)
static uint8_t mlearn_row[32];
#endif
