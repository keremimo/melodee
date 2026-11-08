/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A synth module's patch and parameters, inside its file: the SCHM chunk (the SDK's description of it, which modules
 * write theirs with: firmware/modules/sdk/mod_schema.h). The firmware reads it (schema.c); host builds whose engines
 * are built in write their chunks at first use from the same descriptions (MELODEE_SCH_BUILTIN) */
#pragma once
#ifndef MELODEE_SCH_BUILTIN
#define MELODEE_SCH_BUILTIN (MELODEE_P5_BUILTIN || MELODEE_CZ_BUILTIN || MELODEE_FM6_BUILTIN || MELODEE_DRUM_BUILTIN || \
                             MELODEE_SID_BUILTIN || MELODEE_LEGACY_EXTRAS)
#endif
#if !MELODEE_SCH_BUILTIN
#define MOD_SCH_NO_WRITER
#endif
#include "../modules/sdk/mod_schema.h"
