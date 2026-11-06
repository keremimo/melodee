/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee Studio's projects (EDITOR_PROTOCOL.md "Studio session"): PROJECTS (42) lists slots 1..32, loads, saves,
 * saves as, renames, duplicates and deletes them, and says which slot the music came from and whether it changed
 * since; STORAGE (43) maps the shared pool. A load or a flash write while the transport runs stops it first and it
 * stays stopped (Kerem 2026-11-06: Studio's button says "Stops FM-1 playback"). Besides the stores it needs only ed_b
 * and ed_flash_stop: tests/backup_test.c builds it on its own, against simulated flash */
#include "editor_wire.h"
enum { ED_PROJECTS = 42, ED_STORAGE };
#if MELODEE_FLASH
#define ED_PJ_SLOTS PROJ_SLOTS
#else
#define ED_PJ_SLOTS 4u                                    /* (host builds without flash: the four slots in RAM) */
#endif
/* a slot: its number, state (0 empty, 1 a project, 2 a record this firmware does not read), stored bytes (3 x 7 bits),
 * name */
static void ed_pj_item(uint32_t s)
{
    char nm[PROJ_NAME_LEN + 1u];
    uint32_t u = (uint32_t)project_used(s), len = 0;
#if MELODEE_FLASH
    st_hdr_t h;
    if (flash_ok && st_current(st_proj_obj(s), &h) >= 0)
        len = h.len;
#else
    len = u ? BANK_STORE_SIZE : 0u;
#endif
    project_name(s, nm);
    ed_b(s); ed_b(u ? 1u : project_old(s) ? 2u : 0u); ed_n7(len, 3u); ed_str(nm, PROJ_NAME_LEN);
}
#if MELODEE_FLASH
/* each linear sector's owner: 0 free, 1..32 a project, 33..40 a user kit, 41 another record (the preset banks), 127 not
 * the pool's (st_lin_ok). Nonzero: a read failed */
static int ed_st_map(uint8_t *own)
{
    uint32_t i, o, used[ST_LW], who;
    st_hdr_t h;
    int c;
    st_read_failed = 0;
    for (i = 0; i < ST_LN; i++)
        own[i] = st_lin_ok(i) ? 0u : 127u;
    for (o = 0; o < ST_KFILE0 + ST_NKIT; o++) {          /* as st_pool_used: every current record */
#if !MELODEE_USER_KITS
        if (o >= ST_KFILE0) break;
#endif
        if (!(o < OBJ_COUNT ? st_listed(o) && !st_retired(o) : st_pfile(o)) || (c = st_current(o, &h)) < 0)
            continue;
        memset(used, 0, sizeof used);
        st_project_used(used, o, (uint32_t)c, &h);
        who = o >= ST_KFILE0 ? 33u + o - ST_KFILE0 : o >= ST_PFILE0 ? 1u + o - ST_PFILE0 : o - OBJ_BANK0 < 4u ? 1u + o - OBJ_BANK0 : 41u;
        for (i = 0; i < ST_LN; i++)
            if ((used[i / 32u] >> (i % 32u)) & 1u)
                own[i] = (uint8_t)who;
    }
    return st_read_failed;
}
#endif

static int ed_projects_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (cmd == ED_STORAGE) {                              /* -> rc, sectors, free, payload per sector, [the map] */
        uint8_t own[ST_LN];
        uint32_t rc = 4, f = 0, i;
        if (n)
            return 0;
#if MELODEE_FLASH
        if (flash_ok && !ed_st_map(own))
            rc = 0;
#endif
        for (i = 0; !rc && i < ST_LN; i++)
            f += !own[i];
        ed_b(rc); ed_u14(ST_LN); ed_u14(f); ed_u14(ST_LIN_PAYLOAD_MAX);
        if (!rc)
            ed_bytes(own, ST_LN);
        return 1;
    }
    if (cmd != ED_PROJECTS)
        return 0;
    /* [op: 0 list first count, 1 load s, 2 save s, 3 save as s name, 4 rename s name, 5 duplicate s to, 6 delete s]
     * -> op, rc, the music's slot (127 none), then: none: dirty, the music's name; 0: first, count, total, slots;
     * 1..6: the slot written (5: to) */
    char nm[PROJ_NAME_LEN + 1u];
    uint32_t op = ed_arg(a, n, 0), s = ed_arg(a, n, 1), to = s, rc = 0, c;
    if (op == 0u ? n != 3u :
        op == 3u || op == 4u ? s >= ED_PJ_SLOTS || n - 2u > PROJ_NAME_LEN || !ed_text(nm, a + 2, n - 2u) :
        op == 5u ? n != 3u || s >= ED_PJ_SLOTS || (to = a[2]) >= ED_PJ_SLOTS :
        op <= 6u ? n != 2u || s >= ED_PJ_SLOTS : n != 0u)
        rc = 1;
    else if ((op == 1u || op == 4u || op == 5u) && !project_used(s))
        rc = 1;                                           /* (an empty slot: nothing stops) */
    else if (op && op != 127u && ed_flash_stop())
        rc = 3;                                           /* (the transport did not stop in 100 ms) */
    else if (op == 1u) {
        c = proj_cur;
        proj_cur = PROJ_NO_SLOT;
        project_load(s);
        rc = proj_cur == s ? 0u : 2u;
        if (rc)
            proj_cur = (uint8_t)c;
    } else if (op == 2u || op == 3u)
        rc = project_save_as(s, op == 3u ? nm : 0) ? 2u : 0u;
    else if (op == 4u || op == 5u)
        rc = (uint32_t)project_copy(s, to, op == 4u ? nm : 0);   /* 1: an empty slot */
    else if (op == 6u)
        rc = project_erase(s) ? 2u : 0u;
    ed_echo(a, n, 1u); ed_b(rc); ed_b(proj_cur < PROJ_SLOTS ? proj_cur : 127u);
    if (op == 127u && !rc) {                              /* (dirty packs the music: a backup's runtime copy goes stale) */
        ed_b(project_dirty());
        ed_str(proj_name, PROJ_NAME_LEN);
    } else if (!op && !rc) {
        c = ed_page(s, a[2], ED_PJ_SLOTS);                /* (all 32 fit one reply) */
        ed_u14(s); ed_u14(c); ed_u14(ED_PJ_SLOTS);
        for (; c; c--, s++)
            ed_pj_item(s);
    } else if (op && to < ED_PJ_SLOTS)
        ed_pj_item(to);
    return 1;
}
