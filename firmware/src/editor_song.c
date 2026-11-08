/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee Studio's song (EDITOR_PROTOCOL.md "Studio song"): SONG2 (47) reads the whole song (64 sections x 8 tracks, the
 * block sounds, each section's length), replaces it, and inserts, duplicates, deletes and moves sections as the song
 * page does; SONG_SOUND (48) lists the song's sounds and sets a block's, as the song sheet's Set / Remove preset;
 * PUNCH (49) holds a punch-in effect and reads and writes the sections' punch events; WATCH bit 5 pushes
 * SONG_CHANGED (58) when the song changes on the device. Song writes are the song page's undoable edits
 * (song_edit_begin: EDIT undoes them on the device) and keep the punch events and block sounds (FBKN / FBKO) of the
 * sections they keep. Included by editor.c */
enum { ED_SONG2 = 47, ED_SONG_SOUND, ED_PUNCH, ED_SONG_CHANGED = 58 };
#define ED_PUNCH_KEY (1u << 31)                           /* PUNCH: the editor's held effect (the keys: bits 0..26) */
#define ED_SONG_ROW (8u + 2u * NTRK)                      /* SONG2: a section's bytes */
static int layer_allowed(void);                           /* ui_layer.c: no menu, dialog or NAME (the layers play) */
/* the requests' lengths by op (0: SONG2 2 and PUNCH 4 count their sections) */
static const uint8_t ED_SONG2_N[7] = {3, 1, 0, 2, 3, 3, 4}, ED_SOUND_N[4] = {3, 6, 5, 4}, ED_PUNCH_N[5] = {1, 2, 1, 1, 0};

/* the song's head: sections, loop, BPM (14 bits), the song's sounds (SONG2; the whole of SONG_CHANGED, sent when the
 * song's signature changes: the UI pass's song_edit_sync keeps se.signature current) */
static void ed_song_head(void)
{
    ed_b(chain_config.count); ed_b(chain_config.rsv[CHAIN_LOOP]); ed_u14(song.g[G_BPM]); ed_b(song_sound_count);
}
/* a paged reply's first, count (of want, cut to what fits at `size` bytes an item), total; returns the count */
static uint32_t ed_song_page(uint32_t first, uint32_t want, uint32_t total, uint32_t size)
{
    uint32_t c = ed_page(first, want, total), room = (ed_left() - 6u) / size;
    c = c < room ? c : room;
    ed_u14(first); ed_u14(c); ed_u14(total);
    return c;
}
static void ed_punch_release(void)                        /* the editor's effect off (a key's stays) */
{
    if (punch.keybit == ED_PUNCH_KEY) { punch.req = -1; punch.keybit = 0; }
}

/* (a[1..3] are read before the length is known: frames sit in buffers of 64 bytes or more, and a value is used only
 * once the length says it was sent) */
static int ed_song_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t op = ed_arg(a, n, 0), x = a[1], y = a[2], z = a[3];
    uint32_t rows = chain_config.count, s0 = se.signature, rc = 0, r, k, c;
    if (cmd == ED_SONG2) {
        /* [op: 0 sections first count, 1 the banks' timing, 2 write rows loop (repeats, NTRK cells) x rows, 3 insert at x,
         * 4 duplicate x..y, 5 delete x..y, 6 move x..y before z] -> op, rc, the head; 0: first, count, total, sections;
         * 1: global swing, per track its banks' (steps, division, swing) */
        if (n && (op > 6u || n != (op == 2u ? 3u + x * (NTRK + 1u) : ED_SONG2_N[op])))
            rc = 1;
        else if (op == 2u) {
            if (x > CHAIN_ROWS || y > 1u)
                rc = 1;
            for (r = 0; !rc && r < x; r++) {
                const uint8_t *p = a + 3u + r * (NTRK + 1u);
                if (!p[0] || p[0] > 16u)
                    rc = 1;
                for (k = 0; k < NTRK; k++)                /* a pattern the track has, a rest, or goes on after a block */
                    if (p[1u + k] >= trk_npat(k) && p[1u + k] != CHAIN_REST &&
                        (p[1u + k] != CHAIN_HOLD || !r || (p - (NTRK + 1u))[1u + k] == CHAIN_REST))
                        rc = 1;
            }
        } else if (op >= 4u && n && (x > y || y >= rows || (op == 6u && z > rows)))
            rc = 1;                                       /* (the page refuses a full song or a move onto itself: rc 2) */
        if (!rc && op >= 2u && n) {
            if (chain_busy())
                rc = 3;                                   /* (the song plays: STOP TO EDIT) */
            else {
                song_edit_sync();
                c = se.edits;
                if (op == 2u) {
                    if (song_edit_begin()) {              /* the cells and repeats; the punch events and sounds stay */
                        for (r = 0; r < x; r++) {
                            const uint8_t *p = a + 3u + r * (NTRK + 1u);
                            chain_config.row[r].repeat = p[0];
                            for (k = 0; k < NTRK; k++)
                                if ((chain_patterns[r][k] = p[1u + k]) == CHAIN_REST)
                                    chain_config.sound[r][k] = 0;
                            chain_config.row[r].slot = chain_patterns[r][0];
                        }
                        chain_config.count = (uint8_t)x; chain_config.rsv[CHAIN_LOOP] = (uint8_t)y;
                        song_rows_clear();
                        song_edit_end();
                    }
                } else {                                  /* as the song page: its range, its cursor */
                    se.range = op != 3u; se.anchor = se.first = (uint8_t)x; se.end = se.last = (uint8_t)y;
                    ui.song_row = (uint8_t)(op == 6u ? z : x);
                    if (op == 5u)
                        song_sections_delete();
                    else if (op == 6u) {
                        se.pending = SONG_SECTIONS_MOVE;
                        song_apply_destination();
                    } else
                        song_insert(op == 4u);
                    song_edit_cancel();                   /* (refused: no range or move left pending on the page) */
                }
                if (c == se.edits)
                    rc = 2;                               /* (no edit memory: nothing changed) */
            }
        }
        ed_echo(a, n, 1u); ed_b(rc); ed_song_head();
        if (!rc && !op)                                   /* each: repeats, steps, division, length (units), cells */
            for (r = x, c = ed_song_page(x, y, chain_config.count, ED_SONG_ROW); c; c--, r++) {
                k = chain_patterns[r][0];
                ed_b(chain_config.row[r].repeat); ed_b(song_punch_steps_at(r));
                ed_b(k < NPAT ? (uint32_t)pattern_timing(0, k, 1) : 2u);
                ed_u35((uint32_t)chain_row_samples(r));
                for (k = 0; k < NTRK; k++) {
                    ed_b(chain_patterns[r][k]); ed_b(chain_config.sound[r][k]);
                }
            }
        if (!rc && op == 1u) {
            ed_b(song.g[G_SWING]);
            for (k = 0; k < NTRK; k++) {
                ed_b(trk_npat(k));
                for (r = 0; r < trk_npat(k); r++)
                    for (c = 0; c < 3u; c++)
                        ed_b(pattern_timing(k, r, c));
            }
        }
    } else if (cmd == ED_SONG_SOUND) {
        /* [op: 0 sounds first count, 1 row k engine preset lo hi, 2 row k slot lo hi, 3 row k id (0: the track's own)]
         * -> op, row, k (0: first, count), rc, the song's sounds; 0: first, count, total, the sounds */
        uint32_t p = op == 1u ? a[4] | (uint32_t)a[5] << 7 : z | (uint32_t)a[4] << 7;   /* 1: the preset; 2: the slot */
        if (op > 3u || n != ED_SOUND_N[op] || (op && (x >= rows || y >= NTRK || (y >= NPART && (op < 3u || z)) ||
            (op == 3u && z > song_sound_count) || (op == 1u && (z >= NENGINES || (p && p >= ENGINES[z]->npresets))))))
            rc = 1;
        else if (op == 3u)
            rc = chain_busy() ? 3u : song_block_sound(x, y, z) ? 0u : 2u;
        else if (op) {                                    /* an audition on the track, kept, then the track's own back */
            if (transport_busy() || song_picker.active)
                rc = 3;                                   /* (as the sheet: STOP TO EDIT; the device's Set preset open) */
            else {
                momentary_restore();
                rc = 2;
                if (song_preview_begin(y, x, 0)) {
                    song_picker.active = 1;
                    if (op == 1u)
                        engine_load_of(&trk[y], z, p);
                    rc = op == 2u && up_load_to(p, y) ? 1u : song_preset_keep() ? 2u : 0u;
                    song_preset_cancel();
                }
            }
        }
        ed_echo(a, n, 3u); ed_b(rc); ed_b(song_sound_count);
        if (!rc && !op)                                   /* each: id, engine, preset (2 x 7 bits), name */
            for (r = x, c = ed_song_page(x, y, song_sound_count, 20u); c; c--, r++) {
                const song_sound_t *s = song_sound_pool() + r;
                ed_b(r + 1u); ed_b(s->engine); ed_b(s->preset); ed_b(s->preset >> 7); ed_str(s->name, 15);
            }
    } else if (cmd == ED_PUNCH) {
        /* [op: 0 names, 1 hold fx, 2 release, 3 the song's events, 4 write x first, y sections, y x 4 x (fx, start, length)]
         * -> op, rc, held (127 none), the editor's (0/1), the song's (127 none), the effects; 0: names; 3, 4: the events */
        if (n && (op > 4u || n != (op == 4u ? 3u + 12u * y : ED_PUNCH_N[op])))
            rc = 1;
        else if (op == 1u) {
            if (x >= PUNCH_NFX)
                rc = 1;
            else if (!layer_allowed())
                rc = 3;                                   /* (a menu, a dialog or NAME on the device: no layer plays) */
            else {
                punch.req = (int8_t)x;
                punch.keybit = ED_PUNCH_KEY;
            }
        } else if (op == 2u)
            ed_punch_release();
        else if (op == 4u) {
            if (!y || x + y > rows)
                rc = 1;
            for (r = 0; !rc && r < 4u * y; r++)
                if (!chain_fx_valid((const chain_fx_t *)(a + 3u + 3u * r)))
                    rc = 1;
            if (!rc)
                rc = chain_busy() ? 3u : !song_edit_begin() ? 2u : 0u;
            if (!rc) {
                memcpy(chain_config.fx[x], a + 3, 12u * y);
                song_edit_end();
            }
        }
        ed_echo(a, n, 1u); ed_b(rc);                      /* (-1, none, leaves as 127: 7 bits) */
        ed_b((uint32_t)punch.req); ed_b(punch.keybit == ED_PUNCH_KEY); ed_b((uint32_t)punch_song_req); ed_b(PUNCH_NFX);
        for (r = 0; !op && r < PUNCH_NFX; r++)
            ed_str(PUNCH_NAME[r], 12);
        if (op == 3u || op == 4u) {
            ed_b(chain_config.count);
            ed_bytes(&chain_config.fx[0][0].fx, chain_config.count * 12u);
        }
    } else
        return 0;
    if (se.signature != s0)
        ed_w.psig[2] = se.signature;                      /* (the editor's own change: no SONG_CHANGED) */
    return 1;
}
