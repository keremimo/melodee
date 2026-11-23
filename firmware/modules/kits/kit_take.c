/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A take into a kit's pad as it is recorded (the firmware's Record kit sheet drives it from USB audio: SAMPLER's
 * rec_stream): kit_take_core.c encodes it piece by piece and hands it to the host's take calls (mod_abi3.h host 6),
 * into a pad record of its own, so a take never copies the kit's other pads. */
#ifndef KT_ON
#define KT_ON MOD3_TAKE_ON                       /* (the host's take starts above it: tk_end's quiet take) */
#endif
#if MOD_SIDE                                     /* (the host's calls; a host build's built-in engine: its own) */
#define kt_h_write(off, p, n) mod_host->take_write(off, p, n)
#define kt_h_end(u, len) mod_host->take_end(u, len)
#define kt_h_abort() mod_host->take_abort()
#else
#define kt_h_write mod_h6_take_write
#define kt_h_end mod_h6_take_end
#define kt_h_abort mod_h6_take_abort
#endif
#include "drum_enc.c"
#include "kit_take_core.c"
_Static_assert(KM_NONE == MOD3_TK_NONE && KM_KEPT == MOD3_TK_KEPT && KM_QUIET == MOD3_TK_QUIET && KM_ROOM == MOD3_TK_ROOM &&
               KM_FLASH == MOD3_TK_FLASH, "a take's end as the host shows it");

/* SAMPLER's data records (mod_abi3.h host 6): a pad record it plays, a take into kit slot's pad as it comes, whether a
 * kit's pad has a take */
static int drum_rec_check(const uint8_t *secs, uint32_t nsec, uint32_t len) { return drk_pad_check(secs, nsec, len); }
static uint32_t drum_rec_stream(uint32_t u, uint32_t pad, const int16_t *x, uint32_t n, uint32_t op)
{
    if (u >= DK_NUSER || pad >= DRK_NPAD)
        return KM_FLASH;
    switch (op) {
    case MOD3_TS_BEGIN: tk_begin(u * DRK_NPAD + pad, pad, n); return KM_NONE;
    case MOD3_TS_FEED: return tk_feed(x, n);
    case MOD3_TS_END: return tk_end();
    default: tk_abort(); return KM_NONE;
    }
}
static int drum_rec_pad(uint32_t u, uint32_t pad)
{
    return u < DK_NUSER && pad < DRK_NPAD && !(drk_user_gen & 1u) && drk_user[u * DRK_NPAD + pad].n;
}
