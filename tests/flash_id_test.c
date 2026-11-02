/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* persist_boot's JEDEC gate (project.c): which SPI-NOR IDs enable flash writes.
 * Shares persistence_test.c's scaffold (the fake NOR, project.c, reset()); the
 * JEDEC ID read by persist_boot is fl_jedec_id, which each case sets. */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"

/* reset() leaves flash_ok = 1, so each case first forces the sentinel below to
 * prove persist_boot itself decided the value from the JEDEC ID. */
#define SENTINEL 0x5Au

static int gate(const char *what, uint32_t id, int want_ok)
{
    reset();
    fl_jedec_id = id;
    flash_ok = SENTINEL;
    persist_boot();
    int got = flash_ok == 1u;
    int rejected_clean = want_ok || flash_ok == 0u;   /* a rejected part must read exactly 0, not the sentinel */
    return check(what, got == want_ok && rejected_clean);
}

int main(void)
{
    int bad = 0;
    /* accepted: the two 1 MiB parts persist_boot knows (0x85 Puya, 0xB3 UCUN). */
    bad += gate("JEDEC 0x856014 (Puya) enables flash writes", 0x856014u, 1);
    bad += gate("JEDEC 0xB36014 (UCUN) enables flash writes", 0xB36014u, 1);
    /* rejected: unknown or absent parts stay RAM-only (flash_ok == 0). */
    bad += gate("JEDEC 0x000000 (no device) stays RAM-only", 0x000000u, 0);
    bad += gate("JEDEC 0xFFFFFF (bus idle / no device) stays RAM-only", 0xFFFFFFu, 0);
    bad += gate("JEDEC 0x856015 (near miss) stays RAM-only", 0x856015u, 0);
    printf("flash id gate test %s\n", bad ? "FAILED" : "passed");
    return bad ? 1 : 0;
}
