/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The synth modules Melodee makes (firmware/modules/<name>): the device's default engine map and what the firmware
 * shows of these synths before, or without, their files. The firmware reads it by row and never by name: firmware/src
 * knows no synth (tests/no_synth_code.py). A module Melodee does not make is not listed here: its file says who it is
 * (its IDNT chunk, mod_abi3.h), and the device gives it a number of its own (eng_map.c).
 *
 * A row: X(number, uid, name, tag, cost class, store, user namespace, flags)
 *   number      its engine number: the default map's (stores from before the map name it by this alone)
 *   uid         its ID's first 4 bytes (little endian): the ID its file carries, the first 8 bytes of SHA-512 of
 *               "melodee/synth/1", Melodee's release key and its slug (mod_abi3.h; tools/mod_build.py derives it)
 *   name, tag   the lists' name and four-letter tag while its file is not there
 *   cost class  GLO > WEIGHTS' row it counts in (engine_t.wclass), 0 none
 *   store       the Melodee 1.x store format its patch was kept in (module_schema.h MOD_SCH_STORE_*), 0 none
 *   namespace   its Melodee 1.x user-preset namespace (core.h USER_NATIVE_*), 0 none
 *   flags       SHIP_F_*
 * The rows' order is the order the PRESETS browser and the editor list the engines in (the MIDI Track after the
 * shipped ones, any other module after it). */
#pragma once
#define SHIP_F_MODS 1u                           /* in melodee-mods.bin: the web installer writes it, the start asks for
                                                  * setup while it is missing (eng_module_main.c mod_shipped_ok) */
#define SHIP_F_FALLBACK 2u                       /* the engine a sound on a reserved number becomes (eng_sound_idx) */
#define SHIP_F_GM 4u                             /* General MIDI's percussion: the engine pre-1.0 drum tracks and PHYS
                                                  * MODEL 4 sounds became (project.c, upreset.c) */
#define SHIP_ROWS(X)                                                                                                   \
    X(19u, 0xC585ED74u, "PROPHET", "P5", 1u, 3u, 20u, SHIP_F_MODS)                                                     \
    X(12u, 0x3909E71Au, "FM6", "FM6", 3u, 1u, 17u, SHIP_F_MODS | SHIP_F_FALLBACK)                                       \
    X(15u, 0x6ADC0530u, "CZ-1", "CZ-1", 2u, 2u, 18u, SHIP_F_MODS)                                                      \
    X(3u, 0x7F9158D4u, "SID", "SID", 4u, 0u, 0u, SHIP_F_MODS)                                                          \
    X(10u, 0x7D998FFEu, "DRUM", "DRUM", 5u, 4u, 0u, SHIP_F_MODS | SHIP_F_GM)                                          \
    X(6u, 0x2CF92788u, "TRIO", "TRIO", 0u, 0u, 0u, 0u)                                                                 \
    X(7u, 0x5B2DA920u, "WHEEL", "WHEL", 0u, 0u, 0u, 0u)
/* the parts at power-on and in a new song (engine, factory preset): poly, bass, lead, drums. IT'S A PROPHET 5, FM6
 * SOLID BASS, CZ-1 SYNTH.LEAD 1, 808 KIT (until 2026-10-16: PROPHET FUNK BASS II, FM6 PAD, SID INIT SID, 808 KIT) */
#define SHIP_PARTS {{19u, 1u}, {12u, 10u}, {15u, 52u}, {10u, 0u}}
/* the licences of the code inside the shipped modules (SETTINGS > ABOUT's credits: ui_menu.c), X(text, kind) */
#define SHIP_CREDITS(X)                                                                                                \
    X("uPD933 / NATIVE CZ", MC_TITLE) X("Devin Acker / BSD-3-Clause", MC_TEXT)                                       \
    X("https://github.com/mamedev/mame", MC_URL) X("", MC_TEXT)                                                       \
    X("DEXED + MSFA / FM6 PORT", MC_TITLE) X("Pascal Gauthier, Google Inc.", MC_TEXT)                                 \
    X("msfa: Apache-2.0, Dexed: GPL-3.0", MC_TEXT) X("https://github.com/asb2m10/dexed", MC_URL) X("", MC_TEXT)        \
    X("RESID / SID MODEL", MC_TITLE) X("Dag Lem / GPL-2.0-or-later", MC_TEXT)                                         \
    X("https://github.com/libsidplayfp/resid", MC_URL) X("", MC_TEXT)
#define SHIP_LICENCE "DRUM + own samples: GPL-3.0-only"
/* the shipped synths' numbers by name, for their own sources and the host builds' reference engines */
#define ENGI_SID 3u
#define ENGI_TRIO 6u
#define ENGI_WHEEL 7u
#define ENGI_DRUM 10u
#define ENGI_FM6 12u
#define ENGI_CZ 15u
#define ENGI_PROPHET 19u
