/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SID's schema (firmware/modules/sdk/mod_schema.h): no patch (its sound is its E values), MIDI CC 74 / 71 on CUT and
 * RES (E5, E6). The module file carries it as its SCHM chunk; host builds compile it in (eng_sid.c) */
static const mod_sch_role_src SID_SCH_ROLE[] = {
    {MOD3_R_CC_BRIGHT, MOD3_RF_TRACK, MOD3_P_E0 + 4, 0}, {MOD3_R_CC_RESO, MOD3_RF_TRACK, MOD3_P_E0 + 5, 0},
};
static const mod_sch_src SID_SCHEMA = {
    .patch = 0, .wdef = MOD_SCH_NONE, .mmap = MOD_SCH_NONE,
    .role = SID_SCH_ROLE, .nrole = sizeof SID_SCH_ROLE / sizeof *SID_SCH_ROLE,
};
