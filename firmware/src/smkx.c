/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The SMK-37's app area tail. The stock SMK region table ends the app area (PRCT) at 0x9D000, the FM-1's at 0x93000;
 * the stub, the body (0x5000..0x90FFF) and the commit marker (0x91000) sit at the FM-1's places on both, so on the
 * SMK-37 flash 0x93000..0x9CFFF (40 KiB, XIP 0x0208F000) is app area the body never reaches. It holds read-only data
 * the main loop reads (app.ld .smkx from build.py, MELODEE_SMKX): part of the package's flash.bin, SFC-encrypted like
 * the code (tools/fm1pkg_make.py), written by the update loader with the rest of the app area and before the commit
 * marker (firmware/loader/ldr_core.c), read in place through XIP, which decrypts it below the plain window (FL_PLAIN_LO
 * 0x9D000). App area, not user storage: every install writes it, nothing else does.
 * build.py patches smkx_want with the length and CRC-32 of this build's tail. Another build's or a missing tail
 * (a host that wrote only the FM-1's app area): smkx_ok 0, the readers fall back (icons.c: no icon). */
extern const uint8_t _smkx_start[], _smkx_end[];
__attribute__((used)) static const volatile uint32_t smkx_want[3] = {0x54575853u, 0u, 0u};   /* SXWT */

/* once, at power-on, before anything draws: the tail as the app reads it (decrypted) */
static void smkx_check(void)
{
    uint32_t len = smkx_want[1];
    smkx_ok = (uint8_t)(smkx_want[0] == 0x54575853u && len == (uint32_t)(_smkx_end - _smkx_start) &&
                        st_crc32(_smkx_start, len) == smkx_want[2]);
}
