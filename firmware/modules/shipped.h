/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The synth modules Melodee makes (firmware/modules/<name>): what the firmware shows of these synths before, or
 * without, their files, and the parts a new song starts with. The firmware reads it by row and never by name:
 * firmware/src knows no synth (tests/no_synth_code.py). A synth is its ID (Melodee 2.0): a row's handle in RAM is
 * ENG_H_SHIP0 + its place here (core.h), given again at every power-on and never stored. A module Melodee does not
 * make is not listed here: its file says who it is (its IDNT chunk, mod_abi3.h).
 *
 * A row: X(uid, name, tag, cost class, store, user namespace, flags)
 *   uid         its ID (its 8 bytes little endian): the first 8 bytes of SHA-512 of "melodee/synth/1", Melodee's
 *               release key and its slug (mod_abi3.h; tools/mod_build.py derives it)
 *   name, tag   the lists' name and four-letter tag while its file is not there
 *   cost class  GLO > WEIGHTS' row it counts in (engine_t.wclass), 0 none
 *   store       the Melodee 1.x store format its patch was kept in (module_schema.h MOD_SCH_STORE_*), 0 none
 *   namespace   its Melodee 1.x user-preset namespace (core.h USER_NATIVE_*), 0 none
 *   flags       SHIP_F_*
 * The rows' order is the order the PRESETS browser and the editor list the engines in (the MIDI Track after them, any
 * other module after it). */
#pragma once
#define SHIP_F_MODS 1u                           /* in melodee-mods.bin: the web installer writes it, the start asks for
                                                  * setup while it is missing (eng_module_main.c mod_shipped_ok) */
#define SHIP_F_GM 4u                             /* the synth that owns the data records by default (rec_store.c) */
#define SHIP_ROWS(X)                                                                                                   \
    X(0x4624E600C585ED74ULL, "PROPHET", "P5", 1u, 3u, USER_NATIVE_P5, SHIP_F_MODS)                                   \
    X(0x918284C33909E71AULL, "FM6", "FM6", 3u, 1u, USER_NATIVE_FM, SHIP_F_MODS)                                      \
    X(0x62DC75AA6ADC0530ULL, "CZ-1", "CZ-1", 2u, 2u, USER_NATIVE_CZ, SHIP_F_MODS)                                    \
    X(0x8757CDD57F9158D4ULL, "SID", "SID", 4u, 0u, 0u, SHIP_F_MODS)                                                   \
    X(0x241AACC57D998FFEULL, "DRUM", "DRUM", 5u, 4u, 0u, SHIP_F_MODS | SHIP_F_GM)
/* the shipped synths' handles in RAM (ENG_H_SHIP0 + their rows: engines.c asserts them), for their own sources and the
 * host builds' reference engines */
#define ENGI_PROPHET 2u
#define ENGI_FM6 3u
#define ENGI_CZ 4u
#define ENGI_SID 5u
#define ENGI_DRUM 6u
#ifndef MOD_HANDLE
#define MOD_HANDLE(row) (row)                    /* a synth's own handle: a host build's built-in one is its row's (a
                                                  * module's, bind's self: mod_engine.h); its own sources call the host
                                                  * with this, never with the row's number */
#endif
#define SHIP_ID_MIDI 0x21EB3550E8D4BF9EULL       /* the MIDI Track's ID (release key, "midi"): built in, handle ENGI_MIDI */
/* the parts at power-on and in a new song (engine, factory preset): poly, bass, lead, drums. IT'S A PROPHET 5, FM6
 * SOLID BASS, CZ-1 SYNTH.LEAD 1, 808 KIT (until 2026-10-16: PROPHET FUNK BASS II, FM6 PAD, SID INIT SID, 808 KIT) */
#define SHIP_PARTS {{ENGI_PROPHET, 1u}, {ENGI_FM6, 10u}, {ENGI_CZ, 52u}, {ENGI_DRUM, 0u}}
/* the licences of the code inside the shipped modules (SETTINGS > ABOUT's credits: ui_menu.c), X(text, kind) */
#define SHIP_CREDITS(X)                                                                                                \
    X("uPD933 / NATIVE CZ", MC_TITLE) X("Devin Acker / BSD-3-Clause", MC_TEXT)                                       \
    X("https://github.com/mamedev/mame", MC_URL) X("", MC_TEXT)                                                       \
    X("DEXED + MSFA / FM6 PORT", MC_TITLE) X("Pascal Gauthier, Google Inc.", MC_TEXT)                                 \
    X("msfa: Apache-2.0, Dexed: GPL-3.0", MC_TEXT) X("https://github.com/asb2m10/dexed", MC_URL) X("", MC_TEXT)        \
    X("RESID / SID MODEL", MC_TITLE) X("Dag Lem / GPL-2.0-or-later", MC_TEXT)                                         \
    X("https://github.com/libsidplayfp/resid", MC_URL) X("", MC_TEXT)
#define SHIP_LICENCE "DRUM + own samples: GPL-3.0-only"
