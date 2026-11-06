/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* firmware/src/smk_midi_out.h on fm1-base: the TRS jack's MIDI OUT, the same admission over the base's ring
 * (device/midi_trs_out.c, its byte a tick and its quiet UART). The jack gets only what the instrument sends it
 * (app.h APP_TRS_OWN): USB MIDI out's stream (midi_out_event, midi_sync_send: smo_put) and a MIDI Track's notes on
 * their own books (midi_track.c: smo_start, smo_end), so a release always has its room (ml_trs_send's keep) and TRS
 * MIDI OUT set OFF ends what the jack sounds first (smo_stop). */
#pragma once
#include <stdint.h>

#ifndef SMO_SECTION
#define SMO_SECTION
#endif
#define SMO_RING 256u             /* (the base's: midi_trs_out.c TRS_RING) */

static uint8_t trs_out = 1;       /* the menu's TRS MIDI OUT (ui.c TRS_OUT_OFF; smk_controls.h smo_sync) */
static struct {
    uint32_t owed;                /* what smo_start began and smo_end has not ended */
    uint32_t drops;
    uint16_t live;                /* channels smo_put sent a note or a pedal down on: what smo_stop ends */
} smo SMO_SECTION;

static int smo_send(uint32_t pkt, uint32_t keep)       /* one packet, whole or dropped; keep: bytes left free */
{
    if (!trs_out)
        return 0;
    if (!ml_trs_send(pkt, keep)) {
        smo.drops++;
        return 0;
    }
    return 1;
}

#define SMO_END 3u                                     /* the bytes of what ends a note or a pedal */
static void smo_put(uint32_t pkt)
{
    uint32_t cin = pkt & 15u;
    if (smo_send(pkt, SMO_END * smo.owed) &&
        (cin == 9u || (cin == 11u && ((pkt >> 16) & 255u) == 64u && pkt >> 24 >= 64u)))
        smo.live |= (uint16_t)(1u << ((pkt >> 8) & 15u));
}

/* A MIDI Track's note or pedal: admitted only with room left for its ending and every other owed one. */
static int smo_start(uint32_t pkt)
{
    if (!smo_send(pkt, SMO_END * (smo.owed + 1u)))
        return 0;
    smo.owed++;
    return 1;
}

static void smo_end(uint32_t pkt)                      /* ... and that ending: its room is there */
{
    smo.owed--;
    (void)smo_send(pkt, 0);
}

/* TRS MIDI OUT going OFF: each channel smo_put played on gets pedal up, then All Notes Off. Both or neither; 0: no
 * room yet, the caller comes back. A jack the base gave up on (its UART quiet) has nothing to end. */
static int smo_stop(void)
{
    while (smo.live && !ml_trs_dead()) {
        uint32_t ch = (uint32_t)__builtin_ctz(smo.live);
        if (ml_trs_room() < SMO_END * (smo.owed + 2u))
            return 0;
        (void)smo_send(0x0Bu | (0xB0u | ch) << 8 | 64u << 16, 0);
        (void)smo_send(0x0Bu | (0xB0u | ch) << 8 | 123u << 16, 0);
        smo.live &= (uint16_t)~(1u << ch);
    }
    smo.live = 0;
    return 1;
}

static void smo_on(uint32_t on)                        /* the menu: TRS MIDI OUT ON / OFF (ON tries the UART again) */
{
    trs_out = (uint8_t)(on != 0u);
    ml_trs_on((int)trs_out);
}
