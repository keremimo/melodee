/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee Studio's session (EDITOR_PROTOCOL.md "Studio session"): CAPS (39) says what this firmware supports, TRACKS
 * (40) lists and changes all eight tracks, TRANSPORT (41) plays and stops; WATCH bits 2..4 add the eight tracks to
 * RELOAD / STEP_CHANGED / TRACK_CHANGED, the pushes TRACKS_CHANGED (56) and TRANSPORT_CHANGED (57), and METERS (55)
 * about 15 times a second. Included by editor.c (PROJECTS and STORAGE: editor_projects.c) */
#if BOARD_SMK37
#define ED_BOARD 1u
#else
#define ED_BOARD 0u
#endif
#if MELODEE_BLE
#define ED_BT 1u                                          /* BLE (52) v1 */
#else
#define ED_BT 0u
#endif
#define ED_METER_MS 66u                                   /* METERS: ~15 Hz */
/* CAPS: its revision, then per feature a tag, the length of its value, the value */
static const uint8_t ED_CAPS_TLV[] = {
    1,
    'B', 1, ED_BOARD,                                     /* the board: 0 FM-1, 1 SMK-37 */
    'L', 4, 1024u & 127u, 1024u >> 7, 640u & 127u, 640u >> 7,   /* the longest reply and request, F0..F7 */
    'T', 3, 1, NTRK, NPART,                               /* TRACKS v1: the places, the parts that sound */
    'X', 1, 1,                                            /* TRANSPORT v1 */
    'P', 3, 1, PROJ_SLOTS, PROJ_NAME_LEN,                 /* PROJECTS v1: slots, name length */
    'G', 1, 1,                                            /* STORAGE v1 */
    'K', 1, 1,                                            /* BACKUP_LIST 1 (an inventory), BACKUP_GET while playing */
    'W', 2, 127, 1,                                       /* the WATCH bits known (14 bits: bit 7 in a second byte) */
    'M', 2, 1, ED_METER_MS,                               /* METERS v1, ms between pushes */
    'S', 6, 1, 1, 1, CHAIN_ROWS, CHAIN_SOUNDS, PUNCH_NFX,  /* SONG2, SONG_SOUND, PUNCH v1; sections, sounds, effects */
    'D', 4, 2, NSTEP, RECORD_MAX & 127u, RECORD_MAX >> 7,  /* the patterns (44, 45, 64's bank, 73's 3..5) v1, v2 73's copy to
                                                            * another track; steps, notes */
    'C', 4, 1, 1, ED_BT, 1,                               /* the FM-1 page (editor_device.c): SETTINGS, MLEARN, BLE (0: none),
                                                            * DRUM v1 */
};

static uint32_t ed_sig(uint32_t at) { return fnv(2166136261u, ed_out + at, ed_n - at); }   /* the reply's bytes from at */
/* ---- TRACKS: after op and rc (and as TRACKS_CHANGED): NTRK, the tracks in use, the selected one, the solo and the
 * armed tracks (14 bits each); per place its sound: engine, preset (2 x 7 bits), the user slot it came from + 1 (14
 * bits, 0 none); per place level v14, mute; the reply then the sounds' names. Returns what TRACKS_CHANGED follows: the
 * bytes before the levels (level and mute: CHANGED / TRACK_CHANGED) */
static uint32_t ed_tracks_body(uint32_t names)
{
    uint32_t k, at = ed_n, s;
    char nm[20];
    ed_b(NTRK); ed_b(trk_n()); ed_b(song.sel); ed_u14(track_solo); ed_u14(song.rec);
    for (k = 0; k < NTRK; k++) {
        ed_sound(&trk[k]);
        ed_b(trk[k].preset >> 7);
        ed_u14(trk[k].user);
    }
    s = ed_sig(at);
    for (k = 0; k < NTRK; k++) {
        ed_v(trk[k].p[P_LEVEL]);
        ed_b(trk[k].p[P_MUTE] != 0);
    }
    for (k = 0; names && k < NTRK; k++) {
        sound_name(&trk[k], nm);
        ed_str(nm, 16);
    }
    return s;
}

/* ---- TRANSPORT: state (1 playing, 2 the song, 4 counting in, 8 a track armed), clock (0 INT, 1 USB, 2 TRS), song loop,
 * the song's row, rows, repeats left, track 1's step. Returns what TRANSPORT_CHANGED follows: all but the step */
static uint32_t ed_tp_state(void)
{
    return (song.playing ? 1u : 0u) | (chain_busy() ? 2u : 0u) | (seq_counting() ? 4u : 0u) | (song.rec ? 8u : 0u);
}
static uint32_t ed_tp_body(void)
{
    uint32_t at = ed_n, s;
    ed_b(ed_tp_state()); ed_b(song.g[G_CLOCK]); ed_b(chain_config.rsv[CHAIN_LOOP]);
    ed_b(chain.row); ed_b(chain_config.count); ed_b(chain.remaining);
    s = ed_sig(at);
    ed_b(trk[0].seq_idx);
    return s;
}

static uint32_t ed_db8(int32_t a)                         /* |sample| (Q15) as 8 log2: 120 full scale, 0 silence */
{
    uint32_t lg = 0;
    if (a < 1)
        return 0;
    if (a > 0xFFFFF)
        a = 0xFFFFF;
    while ((a >> lg) > 1)
        lg++;
    lg = lg * 8u + ((((uint32_t)a << 3) >> lg) & 7u);
    return lg > 127u ? 127u : lg;
}

/* ed_sync's last part (while watching): TRACKS_CHANGED, TRANSPORT_CHANGED (bit 3), SONG_CHANGED (bit 5), PATTERN_CHANGED
 * (bit 6), METERS (bit 4) */
static uint32_t ed_session_push(uint32_t n, uint32_t now)
{
    uint32_t s, k;
    if ((ed_w.bits & 8u) && n < ED_PUSH_MAX && ed_room()) {   /* (built to compare: sent when it differs) */
        ed_begin(ED_TRACKS_CHANGED);
        if ((s = ed_tracks_body(0)) != ed_w.psig[0]) {
            ed_w.psig[0] = s;
            ed_send();
            n++;
        }
    }
    if ((ed_w.bits & 8u) && n < ED_PUSH_MAX && ed_room()) {
        ed_begin(ED_TRANSPORT_CHANGED);
        if ((s = ed_tp_body()) != ed_w.psig[1]) {
            ed_w.psig[1] = s;
            ed_send();
            n++;
        }
    }
    if ((ed_w.bits & 32u) && se.signature != ed_w.psig[2] && n < ED_PUSH_MAX && ed_room()) {
        ed_w.psig[2] = se.signature;
        ed_begin(ED_SONG_CHANGED);
        ed_song_head();
        ed_send();
        n++;
    }
    if ((ed_w.bits & 64u) && n < ED_PUSH_MAX && ed_room() && (s = ed_pat_sig()) != ed_w.psig[3]) {
        ed_w.psig[3] = s;                                 /* (bit 6: the banks playing and queued; Studio reads again) */
        ed_begin(ED_PATTERN_CHANGED);
        ed_pat_banks();
        ed_send();
        n++;
    }
    if ((ed_w.bits & 16u) && now - ed_w.m_ms >= ED_METER_MS && n < ED_PUSH_MAX && ed_room()) {
        ed_w.m_ms = now;                                  /* state, the song's row, the parts' peaks, every step */
        ed_begin(ED_METERS);
        ed_b(ed_tp_state()); ed_b(chain.row);
        ed_b(NPART);
        for (k = 0; k < NPART; k++)
            ed_b(ed_db8(trk_peak_take(k, 1)));
        ed_b(NTRK);
        for (k = 0; k < NTRK; k++)
            ed_b(trk[k].seq_idx);
        ed_send();
        n++;
    }
    return ed_device_push(n);                             /* (bit 7: DEVICE_CHANGED) */
}

static int ed_session_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t op = ed_arg(a, n, 0), k = ed_arg(a, n, 1), rc = 0, t0 = fm1_ms;
    if (cmd == ED_CAPS) {                                 /* -> the list */
        if (n)
            return 0;
        ed_bytes(ED_CAPS_TLV, sizeof ED_CAPS_TLV);
#if MELODEE_BLE_EDITOR
        ed_b('E'); ed_b(2); ed_b(ed_link); ed_b(1);     /* the link this came on (0 USB, 1 Bluetooth), Bluetooth's command
                                                          * list v1 (EDITOR_PROTOCOL.md "Studio over Bluetooth") */
#endif
        return 1;
    }
    if (cmd == ED_TRACKS) {           /* [op: 1 select k, 2 solo lo hi, 3 arm k on, 4 add, 5 remove k] -> op, rc, the tracks */
        if (n && !(n == (op == 4u ? 1u : op == 1u || op == 5u ? 2u : 3u) && op && op <= 5u))
            rc = 1;
        else if (op == 2u) {                              /* (bits past the tracks in use: dropped) */
            track_solo_latch = (uint8_t)((a[1] | (uint32_t)a[2] << 7) & ((1u << trk_n()) - 1u));
            track_solo = track_solo_latch;
        } else if (op == 4u) {
            if (trk_n() >= NTRK)
                rc = 1;
            else
                track_create_to(trk_n());                 /* (selected, its power-on sound; no browser opens) */
        } else if (n) {                                   /* 1, 3, 5: a track in use */
            if (k >= trk_n() || (op == 3u && a[2] > 1u) || (op == 5u && trk_n() < 2u))
                rc = 1;
            else if (op == 1u)
                track_select(k);
            else if (op == 5u)                            /* stopped only, as Stage's sheet */
                rc = transport_busy() ? 3u : (uint32_t)track_remove(k);
            else if (a[2] && chain_busy())
                rc = 3;                                   /* (a song plays: STOP TO RECORD) */
            else {
                fm1_irq_off();                            /* as REC: one track armed at a time, latched */
                rec_set(&trk[k], a[2]);
                if (a[2])
                    song.rec_latch |= (uint8_t)(1u << k);
                fm1_irq_on();
            }
        }
        if (n && !rc) {
            ui.force = 1;
            if (ed_w.on && op != 2u && op != 3u)
                ed_shadow();                              /* another track selected: the editor re-reads it, no RELOAD */
        }
        ed_echo(a, n, 1u); ed_b(rc);
        ed_w.psig[0] = ed_tracks_body(1);                 /* (the editor's own change: no push) */
        return 1;
    }
    if (cmd == ED_TRANSPORT) {        /* [op: 1 play, 2 play the song from row k, 3 stop, 4 song loop k] -> op, rc, the state */
        if (n && !(n == (op == 1u || op == 3u ? 1u : 2u) && op && op <= 4u && !(op == 4u && k > 1u)))
            rc = 1;
        else if (op == 4u) {
            if (chain_busy())
                rc = 3;                                   /* (the song plays: STOP TO EDIT) */
            else
                song_loop_set((int)k);
        } else if (n && song.g[G_CLOCK])
            rc = 2;                                       /* an external clock (CLK USB / TRS) runs the transport */
        else if (op == 3u)
            transport_req = 2;
        else if (op == 1u) {
            if (!transport_busy())
                transport_req = 1;                        /* as PLAY on Stage: the patterns */
        } else if (op == 2u) {
            if (k >= chain_config.count)
                rc = 1;
            else if (ed_flash_stop())
                rc = 3;
            else {
                uint32_t r = chain_prepare_from(k);
                rc = r == 1u ? 1u : r ? 4u : 0u;
            }
        }
        while (n && transport_req && fm1_ms - t0 < 100u)  /* the audio ISR takes the request at its next block */
            fm1_wdt_feed();
        ed_echo(a, n, 1u); ed_b(rc);
        ed_w.psig[1] = ed_tp_body();
        return 1;
    }
    return 0;
}
