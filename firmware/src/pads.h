/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* What the firmware keeps for a synth that plays pads (mod_schema.h MOD3_R_PADS: pads.c reads its pads block) and for
 * its data records (rec_store.c), before the engines: a module reads these through its host table (mod_abi3.h host 4),
 * a host build's built-in engine by these names. */
#pragma once
#ifndef MELODEE_RECORDS
#define MELODEE_RECORDS 1                        /* data records (rec_store.c): 8 pool slots a synth's own (recorded kits) */
#endif
/* Record kit (rec_ui.c): the device itself, with USB audio and flash; host builds of the UI never show it */
#if defined(FM1_IRQ_TARGET) && MELODEE_RECORDS && MELODEE_USB_AUDIO && MELODEE_FLASH
#define REC_TAKES 1
#else
#define REC_TAKES 0
#endif
/* a note's own pitch and length as the sequencer starts it (seq.c, recording.c: a pads synth's recorded hit, semitones
 * and samples; 0 / 0 a plain note): host 4 note_pitch / note_len, read in note_on */
static int32_t note_hit_pitch;
static uint32_t note_hit_len;
/* the lanes struck since the UI last looked, a bit per lane (pads.c pads_lane) and track: voice.c sets them as a part
 * with pads starts a note, Stage lights the lanes' names (ui_stage.c) */
static volatile uint8_t pads_flash[NTRK];
/* the data records' places (rec_store.c keeps them in the main loop; host 4 rec, rec_gen, rec_base): odd rec_gen while
 * they change */
static mod_rec_v3 rec_tab[MOD3_NREC];
static volatile uint32_t rec_gen;
#ifndef REC_BASE
#ifdef FM1_IRQ_TARGET
#include "fm1_xip.h"
#define REC_BASE ((const uint8_t *)FM1_XIP(0))   /* (the data flash reads plain: fl_plain_window_init) */
#else
#define REC_BASE ((const uint8_t *)0)            /* (host builds without a flash image: no records play) */
#endif
#endif
/* the host's record calls (host 4): rec_store.c sets the hooks at its boot scan; a build without it (no flash, a host
 * test of a part of the firmware) answers -1 */
static struct {
    int (*begin)(uint32_t slot, uint32_t len);
    int (*write)(const void *src, uint32_t n);
    int (*commit)(uint32_t slot);
    int (*del)(uint32_t slot);
    void (*recheck)(void);                       /* (a module came into SRAM: its stored records checked, listed) */
} rec_hook;
static int mod_h4_rec_begin(uint32_t slot, uint32_t len) { return rec_hook.begin ? rec_hook.begin(slot, len) : -1; }
static int mod_h4_rec_write(const void *src, uint32_t n) { return rec_hook.write ? rec_hook.write(src, n) : -1; }
static int mod_h4_rec_commit(uint32_t slot) { return rec_hook.commit ? rec_hook.commit(slot) : -1; }
static int mod_h4_rec_delete(uint32_t slot) { return rec_hook.del ? rec_hook.del(slot) : -1; }
#ifdef FM1_IRQ_TARGET
static void mod_h4_wdt(void) { fm1_wdt_feed(); }
#else
static void mod_h4_wdt(void) {}                  /* (host builds: no watchdog) */
#endif
