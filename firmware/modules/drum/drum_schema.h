/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* DRUM's schema (firmware/modules/sdk/mod_schema.h): its patch (drum_patch.h: the kit's offsets, zero the factory
 * kit; stored as Melodee 1.x's KIT format), MIDI CC 74 on TUNE (E3). Its pads' pages are the firmware's until phase 5
 * (the pads capability). The module file carries it as its SCHM chunk; host builds compile it in (eng_drum.c) */
static const mod_sch_role_src DRUM_SCH_ROLE[] = {
    {MOD3_R_CC_BRIGHT, MOD3_RF_TRACK, MOD3_P_E0 + 2, 0},
};
static const mod_sch_src DRUM_SCHEMA = {
    .patch = sizeof(drum_patch_t), .store = MOD_SCH_STORE_KIT, .wdef = MOD_SCH_NONE, .mmap = MOD_SCH_NONE,
    .role = DRUM_SCH_ROLE, .nrole = sizeof DRUM_SCH_ROLE / sizeof *DRUM_SCH_ROLE,
};
