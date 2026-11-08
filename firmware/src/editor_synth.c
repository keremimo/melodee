/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Editor protocol: the synths, whatever they are (EDITOR_PROTOCOL.md "Synths"): nothing here knows one. A synth's
 * schema (its parameters, pages, roles: mod_schema.h) and its presets (module_presets.h) as their bytes, for the client
 * to read as the device does; a patch got, put, sent or dumped as its synth's SysEx; a SysEx file passed to the synth's
 * module as if it came over MIDI; a parameter by its schema id read or set as the device's pages do.
 *   85 SCHEMA  engine, what (0 SCHM, 1 PRST), offset u21, count u14 -> engine, what, rc, total u21, key u35, offset u21,
 *              count u14, the bytes packed (7 bytes in 8). key: the chunk's CRC (SCHM: its head's; PRST: the module
 *              file's), for a client's cache. rc 1 bad, 2 none
 *   86 PATCH   op, target, engine, index u14 [, payload] -> op, target, engine, index u14, rc [, reply]
 *              targets: 0 a part (index 0..NPART-1), 1 a user slot, 2 a factory preset (GET only)
 *              op 0 GET -> engine (the patch's), length u14, the bytes packed. engine 127: any
 *              op 1 PUT, the bytes packed: a part takes them as a sound of engine (another engine's part: loaded as a new
 *                 sound, undoable), a user slot as engine's stored patch (named by the patch's own name)
 *              op 2 SEND: the patch out over USB as its synth's SysEx (as the STORE page's SEND, the user pages')
 *              op 3 DUMP -> length u14, the frame's bytes between F0 and F7 (its synth's SysEx)
 *              op 4 BANK (target 1; index: an offset), user slots u14... -> length u14, count u14, the count bytes from
 *                 the offset of the frame's between F0 and F7: the slots as engine's synth's bank dump (the bank export
 *                 other tools take: a DX7's 32 voices), made again for each piece; rc 2: its synth has none (the
 *                 client dumps them one by one)
 *              rc 1 bad, 2 not that engine's (or empty, or no synth), 3 busy
 *   87 SYSEX   op 0 begin: length u21 (F0 to F7) -> op, rc; op 1 data: offset u21, the bytes packed -> op, rc;
 *              op 2 end: user slot u14 (UP_SLOTS: the device asks) -> op, rc, engine, staged count: the frame goes to
 *              the synth whose pattern it begins with, as from USB; what the synth offers for user slots goes from the
 *              given slot on. rc 1 bad, 2 busy (a frame or an import waits), 3 no synth for it, 4 saving failed
 *   88 PARAM   op 0 GET: part, ids u14... -> op, part, rc, engine, values v14 (op 2: each with its text when the synth
 *              writes it: MOD3_SF_FORMAT, else empty); op 1 SET: part, id u14, value v14 -> op, part, rc, the value now
 *              (as the device's pages set it: the synth told) */
enum { ED_SCHEMA = 85, ED_PATCH, ED_SYSEX, ED_PARAM };
#define ED_SCHEMA_MAX 840u                        /* a SCHEMA reply's bytes at most (packed: 960) */
#define ED_BANK_SLOTS 64u                         /* a BANK request's slots at most (a synth's bank takes its own count) */
static uint32_t ed_u21(const uint8_t *a) { return a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14; }
static uint32_t ed_14(const uint8_t *a) { return a[0] | (uint32_t)a[1] << 7; }

/* engine e's chunk what: its place (*key: its CRC); total bytes, 0 none */
static uint32_t ed_chunk(uint32_t e, uint32_t what, uint32_t *key)
{
    mod_sch_head h;
    *key = 0;
    if (e >= NENGINES)
        return 0;
    if (!what) {
        if (sch_head(e, &h))
            return 0;
        *key = h.crc;
        return sch_size(e);
    }
#if MELODEE_MODULES
    uint32_t k = mod_pst_of(e);
    if (k < NMOD && mods[k].pst) {
        *key = mods[k].crc;
        return mods[k].pst_len;
    }
#endif
    return 0;
}
static int ed_chunk_rd(uint32_t e, uint32_t what, uint32_t off, uint8_t *dst, uint32_t n)
{
    if (!what)
        return sch_rd(e, off, dst, n);
#if MELODEE_MODULES
    uint32_t k = mod_pst_of(e);
    return k >= NMOD || mod_pread(k, mods[k].pst + off, dst, n);
#else
    return 1;
#endif
}
/* the patch of target t, index k for engine e (127: the one there): into p, its engine into *pe; length, 0 none */
static uint32_t ed_patch_get(uint32_t t, uint32_t e, uint32_t k, uint8_t *p, uint32_t *pe)
{
    uint32_t n;
    memset(p, 0, MOD3_PATCH_MAX);
    if (t == 0u) {
        if (k >= NPART || (e != 127u && patch_eng_of(k) != e) || !patch_len(patch_eng_of(k)))
            return 0;
        *pe = patch_eng_of(k);
        memcpy(p, patch_of(k), patch_len(*pe));
        return patch_len(*pe);
    }
    if (t == 1u) {
        if (k >= UP_SLOTS || !up_used(k))
            return 0;
        *pe = up_engine(k);
        if ((e != 127u && *pe != e) || !native_used(*pe, k))
            return 0;
        n = native_size(*pe);
        memcpy(p, native_raw(*pe, k), n);
        return n;
    }
    if (t == 2u && e < NENGINES && k < eng_npresets(e) && patch_len(e)) {
        *pe = e;
        mod_want(e);
        return preset_patch(e, k, p, patch_sound_len(e)) ? patch_sound_len(e) : 0u;
    }
    return 0;
}
/* part k takes patch p (n bytes) of engine e: an edit of the sound it has, else a new sound loaded (undoable) */
static void ed_patch_part(uint32_t k, uint32_t e, const uint8_t *p, uint32_t n)
{
    track_t *t = &trk[k];
    if (t->eng_req == e && patch_eng_of(k) == e) {
        fm1_irq_off();
        patch_set(k, e, p, n, MOD3_PATCH_SET);
        fm1_irq_on();
        ui.force = 1;
        return;
    }
    mod_want(e);
    load_begin(t, UNDO_SOUND);
    panic_req |= (uint8_t)(1u << k);
    fm1_irq_off();
    t->eng_req = (uint8_t)e;
    for (uint32_t i = P_E0; i < P_COUNT; i++)
        t->p[i] = param_desc_of(e, i)->def;
    patch_set(k, e, p, n, MOD3_PATCH_LOAD);
    t->preset = 0;
    t->user = 0;
    t->user_native = 0;
    fm1_irq_on();
    load_end(t);
    sync_reload = 1;
    ui.force = 1;
}
/* SYSEX: a frame the editor sends in parts into the scratch, then to its synth (sysex_rx.c / sysex_route.c) */
static struct { uint32_t len, got; uint8_t gen, on; } ed_sx;
static int library_send(uint32_t e, uint32_t k);  /* sysex_library.c */
static uint8_t ed_sy_patch[MOD3_PATCH_MAX], ed_sy_frame[320] __attribute__((section(".pool")));   /* (the main loop's
                                                  * stack: a commit under PATCH / SYSEX runs deep) */
static int ed_synth_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint8_t *p = ed_sy_patch;
    uint32_t rc = 0, e, k, t, op, pe = 0, len, key, i;
    switch (cmd) {
    case ED_SCHEMA: {
        uint32_t off, cnt;
        e = ed_arg(a, n, 0);
        t = ed_arg(a, n, 1);
        if (n != 7u || t > 1u)
            rc = 1;
        len = rc ? 0u : ed_chunk(e, t, &key);
        off = n == 7u ? ed_u21(a + 2) : 0u;
        cnt = n == 7u ? ed_14(a + 5) : 0u;
        if (!rc && !len)
            rc = 2;
        if (!rc && (off > len || cnt > ED_SCHEMA_MAX))
            rc = 1;
        ed_echo(a, n, 2); ed_b(rc);
        if (rc)
            return 1;
        if (cnt > len - off)
            cnt = len - off;
        ed_n7(len, 3u); ed_u35(key); ed_n7(off, 3u); ed_u14(cnt);
        while (cnt) {
            uint32_t m = cnt < 7u * 16u ? cnt : 7u * 16u;
            if (ed_chunk_rd(e, t, off, p, m))
                break;
            ed_bk_pack(p, m);
            off += m;
            cnt -= m;
        }
        return 1;
    }
    case ED_PATCH:
        op = ed_arg(a, n, 0);
        t = ed_arg(a, n, 1);
        e = ed_arg(a, n, 2);
        k = n >= 5u ? ed_14(a + 3) : 0u;
        if (n < 5u || op > 4u || t > 2u || (t == 2u && op) || (e >= NENGINES && e != 127u) || (op == 1u && e >= NENGINES) ||
            (op == 4u && (t != 1u || e >= NENGINES)))
            rc = 1;
        else if (op == 0u) {
            len = ed_patch_get(t, e, k, p, &pe);
            rc = n != 5u ? 1u : len ? 0u : 2u;
            ed_echo(a, n, 3); ed_u14(k); ed_b(rc);
            if (!rc) { ed_b(pe); ed_u14(len); ed_bk_pack(p, len); }
            return 1;
        } else if (op == 1u) {
            uint32_t want = t ? native_size(e) : patch_len(e);
            len = ed_unpack7(a + 5, n - 5u, p, MOD3_PATCH_MAX);
            if (!want || len != want || (t == 0u && k >= NPART) || (t == 1u && (k >= UP_SLOTS || !native_limit(e))))
                rc = 1;
            else if (t == 1u ? !patch_stored_ok(patch_store_tag(e), p) : 0)
                rc = 1;
            else if (t == 0u)
                ed_patch_part(k, e, p, len);
            else {
                rc = ed_flash_stop() ? 3u : (uint32_t)native_put(e, k, p);
                if (rc == 3u && !transport_busy())
                    rc = 0;                      /* (no flash: kept in RAM) */
            }
        } else if (op == 4u) {                   /* BANK */
            uint32_t m = (n - 5u) / 2u, total = 0, cnt;
            uint8_t *f = 0;
            if (n < 7u || (n - 5u) % 2u || m > ED_BANK_SLOTS)
                rc = 1;
            for (i = 0; !rc && i < m; i++) {     /* (the module's slots: u16, low byte first) */
                uint32_t s = ed_14(a + 5u + 2u * i);
                rc = s >= UP_SLOTS;
                p[2u * i] = (uint8_t)s;
                p[2u * i + 1u] = (uint8_t)(s >> 8);
            }
            if (!rc && (msx.ready || sx_import.count || (ed_sx.on && ed_sx.gen == proj_wire_gen)))
                rc = 3;                          /* (the scratch holds a frame, an import or an upload) */
            if (!rc) {
                f = proj_wire_borrow();
                total = msx_dump(e, p, MOD3_DUMP_BANK | m << 8, 0, f, WIRE_SCRATCH_BYTES);
                rc = total < 2u ? 2u : k >= total - 2u ? 1u : 0u;
            }
            ed_echo(a, n, 3); ed_u14(k); ed_b(rc);
            if (!rc) {
                cnt = total - 2u - k;
                if (cnt > ed_left() - 4u)
                    cnt = ed_left() - 4u;
                ed_u14(total - 2u); ed_u14(cnt); ed_bytes(f + 1u + k, cnt);
            }
            return 1;
        } else if (n != 5u) {
            rc = 1;
        } else if (op == 2u) {                   /* SEND */
            if (t == 0u)
                rc = k >= NPART || (e != 127u && trk[k].eng_req != e) || store_send(k, 0) ? 2u : 0u;
            else
                rc = k >= UP_SLOTS || library_send(e, k) ? 2u : 0u;
        } else {                                 /* DUMP */
            uint8_t *f = ed_sy_frame;
            len = ed_patch_get(t, e, k, p, &pe);
            len = len ? msx_dump(pe, p, t ? MOD3_DUMP_STORED : MOD3_DUMP_PART, t ? 0u : k % 16u, f, sizeof ed_sy_frame) : 0u;
            rc = len >= 2u ? 0u : 2u;
            ed_echo(a, n, 3); ed_u14(k); ed_b(rc);
            if (!rc) { ed_u14(len - 2u); ed_bytes(f + 1, len - 2u); }
            return 1;
        }
        ed_echo(a, n, 3); ed_u14(k); ed_b(rc);
        return 1;
    case ED_SYSEX:
        op = ed_arg(a, n, 0);
        if (op == 0u) {                          /* begin: the length, F0 to F7 */
            len = n == 4u ? ed_u21(a + 1) : 0u;
            if (len < 3u || len > MOD3_SX_MAX)
                rc = 1;
            else if (msx.eng != 0xFFu || msx.ready || msx.on || sx_import.count)
                rc = 2;
            else {
                ed_sx.len = len;
                ed_sx.got = 0;
                ed_sx.on = 1;
                (void)proj_wire_borrow();
                ed_sx.gen = proj_wire_gen;
            }
        } else if (op == 1u) {                   /* data at offset */
            uint32_t off = n >= 4u ? ed_u21(a + 1) : 0u;
            len = n > 4u ? ed_unpack7(a + 4, n - 4u, p, MOD3_PATCH_MAX) : 0u;
            if (!ed_sx.on || ed_sx.gen != proj_wire_gen || msx.eng != 0xFFu)
                rc = 2;
            else if (!len || off != ed_sx.got || len > ed_sx.len - off)
                rc = 1;
            else {
                memcpy(proj_wire_u.raw + off, p, len);
                ed_sx.got += len;
            }
        } else if (op == 2u) {                   /* end: to its synth */
            uint32_t slot = n == 3u ? ed_14(a + 1) : UP_SLOTS + 1u, staged = 0;
            const uint8_t *f = proj_wire_u.raw;
            e = 0xFFu;
            if (!ed_sx.on || ed_sx.gen != proj_wire_gen || msx.eng != 0xFFu || msx.ready)
                rc = 2;
            else if (slot > UP_SLOTS || ed_sx.got != ed_sx.len || f[0] != 0xF0u || f[ed_sx.len - 1u] != 0xF7u)
                rc = 1;
            for (i = 1; !rc && i + 1u < ed_sx.len; i++)
                if (f[i] > 127u)
                    rc = 1;
            for (i = 0; !rc && i < msx_nroute; i++) {
                uint32_t j, m = msx_route[i].n;
                for (j = 0; j < m && 1u + j < ed_sx.len && f[1u + j] == msx_route[i].b[j]; j++)
                    ;
                if (j == m) { e = msx_route[i].engine; break; }
            }
            if (!rc && e == 0xFFu)
                rc = 3;
            ed_sx.on = 0;
            if (!rc) {                           /* as from USB: the frame waits, its synth takes it now */
                uint32_t f0 = motion_guard();
                msx.on = 0;
                msx.abort = 0;
                msx.src = MSX_USB;
                msx.n = ed_sx.len;
                msx_fed = 0;
                msx.eng = (uint8_t)e;
                msx.ready = 1;
                motion_unguard(f0);
                wire_job = 0;
                msx_service();
                staged = sx_import.count;
                if (staged && slot < UP_SLOTS) {
                    if (slot + staged > UP_SLOTS)
                        rc = 1, sysex_cancel();
                    else if (ed_flash_stop())
                        rc = 4, sysex_cancel();
                    else {
                        sx_import.first = (uint16_t)slot;
                        sysex_commit();
                        rc = sx_import.count ? 4u : 0u;
                        if (rc)
                            sysex_cancel();
                    }
                }
            }
            ed_b(op); ed_b(rc); ed_b(e & 127u); ed_b(staged);
            return 1;
        } else
            rc = 1;
        ed_b(op); ed_b(rc);
        return 1;
    case ED_PARAM: {
        sch_param_t q;
        op = ed_arg(a, n, 0);
        k = ed_arg(a, n, 1);
        e = k < NTRK ? trk[k].eng_req : NENGINES;
        if (op > 2u || k >= NPART || e >= NENGINES || patch_eng_of(k) != e || (op == 1u ? n != 6u : n < 2u || (n & 1u)))
            rc = 1;
        ed_echo(a, n, 2); ed_b(rc);
        if (rc)
            return 1;
        if (op == 1u) {
            uint32_t f0, id = ed_14(a + 2);
            int changed;
            if (sch_param(e, id, &q)) {
                ed_out[ed_n - 1u] = 1;
                return 1;
            }
            f0 = motion_guard();
            changed = sch_put(&trk[k], e, patch_of(k), &q, ed_rv(a + 4));
            if (changed || (q.flags & MOD3_SF_NOTIFY))
                patch_note(k, MOD3_PATCH_EDIT, id);
            motion_unguard(f0);
            ui.force = 1;
            ed_v(sch_val(&trk[k], e, id, 0));
            return 1;
        }
        ed_b(e);
        for (i = 2; i + 1u < n; i += 2) {
            uint32_t id = ed_14(a + i);
            int32_t v = sch_val(&trk[k], e, id, 0);
            ed_v(v);
            if (op == 2u) {
                char s[9] = {0};
                const char *unit = 0;
                const engine_t *g = ENGINES[e];
                if (!sch_param(e, id, &q) && (q.flags & MOD3_SF_FORMAT) && g->pformat && !mod_idle(e))
                    MOD_MAIN_V(e, g->pformat(&trk[k], patch_of(k), id, v, s, &unit));
                ed_str(s, 8);
            }
        }
        return 1;
    }
    }
    return 0;
}
