/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* GLO > STORAGE (ui_slots.c draws it): everything the flash pool keeps, as rows, and its deletion. Row 0 the room left,
 * then the projects, the recorded kits, the user sounds' bank records, the synth files, and Melodee's own records (the
 * engine map, the template's end block: never deleted). A row is its kind << 8 | its index; the table is made again
 * whenever a pool record came or went (st_pool_gen) or a synth's file did. Sizes are whole pool sectors (4,056 bytes of
 * payload each), the room the SLOTS card counts. Included after project.c: the stores' own calls do every deletion */
#define STI_MAX (1u + PROJ_SLOTS + 8u + UP_BANKS + NMOD + 1u)
_Static_assert(PROJ_SLOTS <= 32u && UP_BANKS <= 32u && NMOD <= 32u && STI_SYSTEM < 8u, "a row in a byte");
#if MELODEE_FLASH
#define STI_PROJS PROJ_SLOTS
#else
#define STI_PROJS 4u                                    /* (host builds keep slots 1..4 alone: the rest are theirs) */
#endif
#ifndef ST_LIN_PAYLOAD_MAX
#define ST_LIN_PAYLOAD_MAX 4056u                        /* (no flash store built: storage.c's sector payload) */
#endif
static struct {
    uint32_t gen, mods, projs;                          /* what the table was made from */
    uint8_t n, ok;
    uint8_t row[STI_MAX];                               /* each row: its kind << 5 | its index (sti_at widens it) */
    uint8_t sec[STI_MAX];                               /* .. its sectors */
} sti __attribute__((section(".pool")));

#if MELODEE_FLASH
#define STI_PROJ_OBJ(p) st_proj_obj(p)
static uint32_t sti_obj_sectors(uint32_t obj)           /* the sectors object obj's current record takes, 0 none */
{
    st_hdr_t h;
    uint8_t idx[ST_PROJECT_SECTORS];
    if (st_current(obj, &h) < 0)
        return 0;
    uint32_t n = st_rec_sectors(&h, idx);
    return n ? n : (h.len + ST_LIN_PAYLOAD_MAX - 1u) / ST_LIN_PAYLOAD_MAX;   /* (an older form: by its length) */
}
#else
#define STI_PROJ_OBJ(p) (p)
static uint32_t sti_obj_sectors(uint32_t obj) { (void)obj; return 0; }   /* (no flash: the rows, no sizes) */
#endif
static uint32_t sti_mods_sig(void)                      /* the synth files there (a delete, an upload: the table again) */
{
    uint32_t s = 0;
    for (uint32_t k = 0; k < NMOD; k++)
        s = s * 31u + mods[k].len + mods[k].crc;
    return s;
}
static uint32_t sti_gen(void)
{
#if MELODEE_FLASH
    return st_pool_gen * 7u + up_gen * 13u;
#else
    return up_gen * 13u;
#endif
}
static void sti_add(uint32_t kind, uint32_t i, uint32_t sec)
{
    if (sti.n < STI_MAX) {
        sti.row[sti.n] = (uint8_t)(kind << 5 | i);   /* (indices < 32: projects, banks, kits, synth slots) */
        sti.sec[sti.n++] = (uint8_t)(sec > 255u ? 255u : sec);
    }
}
static uint32_t sti_projs(void)                         /* the project slots in use, a bit each */
{
    uint32_t b = 0;
    for (uint32_t p = 0; p < STI_PROJS; p++)
        b |= (uint32_t)(project_used(p) || project_old(p)) << p;
    return b;
}
static void sti_make(void)
{
    uint32_t g = sti_gen(), m = sti_mods_sig(), pj = sti_projs(), s;
    if (sti.ok && sti.gen == g && sti.mods == m && sti.projs == pj)
        return;
    sti.gen = g;
    sti.mods = m;
    sti.projs = pj;
    sti.ok = 1;
    sti.n = 0;
    sti_add(STI_FREE, 0, 0);
    for (uint32_t p = 0; p < STI_PROJS; p++)
        if (project_used(p) || project_old(p))
            sti_add(STI_PROJECT, p, sti_obj_sectors(STI_PROJ_OBJ(p)));
#if MELODEE_FLASH
#ifdef REC_STORE
    for (uint32_t u = 0; u < ST_NREC; u++)
        if ((s = sti_obj_sectors(rec_obj(u))) != 0u)
            sti_add(STI_KIT, u, s);
#endif
#ifndef UP_HOST
    for (uint32_t b = 0; b < UP_BANKS; b++)
        if ((s = sti_obj_sectors(up_obj(b))) != 0u)
            sti_add(STI_SOUNDS, b, s);
#endif
#endif
    for (uint32_t k = 0; k < NMOD; k++) {
        if (!mods[k].len)
            continue;
        s = 0;
#ifdef MODULE_STORE
        for (uint32_t j = 0; j < MOD_RECS; j++)
            s += sti_obj_sectors(mod_obj(k, j));
#endif
        sti_add(STI_SYNTH, k, s);
    }
#if MELODEE_FLASH
    s = sti_obj_sectors(ST_MAPOBJ) + sti_obj_sectors(ST_TPLX);
    if (s)
        sti_add(STI_SYSTEM, 0, s);
#endif
}
static uint32_t sti_count(void) { sti_make(); return sti.n; }
static uint32_t sti_at(uint32_t r) { sti_make(); return r < sti.n ? (uint32_t)(sti.row[r] >> 5) << 8 | (sti.row[r] & 31u) : 0u; }
static uint32_t sti_bytes(uint32_t it)
{
    sti_make();
    if (it >> 8 == STI_FREE) {
        int kb = project_free_pct();
        return kb < 0 ? 0u : (uint32_t)kb * 1024u;
    }
    for (uint32_t r = 1; r < sti.n; r++)
        if (sti_at(r) == it)
            return sti.sec[r] * ST_LIN_PAYLOAD_MAX;
    return 0;
}
static uint32_t sti_total(void)                         /* the pool's sectors, used or not */
{
    uint32_t n = 0;
#if MELODEE_FLASH
    for (uint32_t i = 0; i < ST_LN; i++)
        n += (uint32_t)st_lin_ok(i);
#endif
    return n;
}
#ifdef REC_STORE
static void sti_kit_name(uint32_t u, char *nm)          /* its own name, else "Kit 3" */
{
    if (!rec_name(u, nm, 13u)) {
        str_cpy(nm, "Kit ", 24);
        fmt_int(nm + 4, (int32_t)u + 1);
    }
}
#endif
static void sti_label(uint32_t it, char *tag, char *nm)
{
    uint32_t i = it & 0xFFu;
    tag[0] = nm[0] = 0;
    switch (it >> 8) {
    case STI_FREE:                                      /* FREE  312 KB */
        str_cpy(tag, "FREE", 6);
        sl_kb(nm, sti_bytes(it));
        return;
    case STI_PROJECT:                                   /* "05", its name */
        tag[0] = (char)('0' + (i + 1u) / 10u); tag[1] = (char)('0' + (i + 1u) % 10u); tag[2] = 0;
        if (!project_name(i, nm) || !nm[0])
            str_cpy(nm, project_old(i) ? "Old format" : "Saved", 24);
        return;
#ifdef REC_STORE
    case STI_KIT:
        str_cpy(tag, "KIT", 6);
        sti_kit_name(i, nm);
        return;
#endif
    case STI_SOUNDS:                                    /* "U019", User sounds */
        up_slot_label(tag, i * UP_PER_BANK);
        str_cpy(nm, "User sounds", 24);
        return;
#if MELODEE_MODULES
    case STI_SYNTH:
        syn_label(i, tag, nm);
        return;
#endif
    default:
        str_cpy(tag, "SYS", 6);
        str_cpy(nm, "Melodee", 24);
        return;
    }
}
static void sti_line(uint32_t it, char *b)              /* the card's line (a sheet's too: 19 at most) */
{
    uint32_t i = it & 0xFFu, n = 0;
    switch (it >> 8) {
    case STI_FREE: {                                    /* "Of 364 KB" */
        str_cpy(b, "Of ", 24);
        sl_kb(b + 3, sti_total() * ST_LIN_PAYLOAD_MAX);
        return;
    }
    case STI_PROJECT:
        str_cpy(b, project_old(i) ? "Old project" : "Project", 24);
        return;
#ifdef REC_STORE
    case STI_KIT: {                                     /* "Plays on DRUM" */
        const pads_c_t *c = rec_owner(i);
        if (!c || c->eng >= NENGINES || !rec_tab[i].n) { str_cpy(b, "Its synth is gone", 24); return; }
        str_cpy(b, "Plays on ", 24);
        str_cpy(b + 9, eng_name(c->eng), 11);
        return;
    }
#endif
    case STI_SOUNDS: {                                  /* "7 sounds" */
        for (uint32_t k = i * UP_PER_BANK; k < (i + 1u) * UP_PER_BANK && k < UP_SLOTS; k++)
            n += (uint32_t)(up_used(k) || up_old(k));
        fmt_int(b, (int32_t)n);
        str_cpy(b + str_len(b), n == 1u ? " sound" : " sounds", 8);
        return;
    }
    case STI_SYNTH:
        str_cpy(b, "Synth", 24);
        return;
    default:
        str_cpy(b, "Engine map, template", 24);
        return;
    }
}
static int sti_erasable(uint32_t it) { return it >> 8 != STI_FREE && it >> 8 != STI_SYSTEM; }

/* ---- deletion: a question first (ui_draw.c confirm_text: CF_DEL_ITEM; projects: PROJECT's own CF_ERASE_PROJ) */
static uint32_t sti_parts_of(uint32_t it)               /* the tracks a synth or kit deleted would leave silent */
{
    uint32_t m = 0, i = it & 0xFFu;
#if MELODEE_MODULES
    if (it >> 8 == STI_SYNTH)
        m = syn_parts(i);
#endif
#ifdef REC_STORE
    if (it >> 8 == STI_KIT)
        for (uint32_t p = 0; p < NPART && p < trk_n(); p++)
            if (pads_rec(&trk[p]) == (int32_t)i)
                m |= 1u << p;
#endif
    return m;
}
static void sti_ask(uint32_t it)
{
    if (!sti_erasable(it))
        return;
    if (transport_busy()) {
        ui_message("STOP TO DELETE");
        return;
    }
#if MELODEE_FLASH
    if (st_stream_open) {                               /* (an editor upload or restore on its way) */
        ui_message("BUSY");
        return;
    }
#endif
    if (it >> 8 == STI_PROJECT)
        confirm_open(CF_ERASE_PROJ, it & 0xFFu);
    else
        confirm_open(CF_DEL_ITEM, it);
}
/* CF_DEL_ITEM's question: a "DELETE SYNTH?", b its name and what it leaves */
static void sti_question(uint32_t it, char *a, char *b)
{
    char tag[6], nm[24];
    uint32_t m = sti_parts_of(it);
    sti_label(it, tag, nm);
    str_cpy(a, it >> 8 == STI_SYNTH ? "DELETE SYNTH?" : it >> 8 == STI_KIT ? "DELETE KIT?" : "ERASE USER SOUNDS?", 24);
    str_cpy(b, it >> 8 == STI_SOUNDS ? tag : nm, 32);
    if (it >> 8 == STI_SOUNDS) {                        /* "U019-U036, 7 sounds" */
        uint32_t n = str_len(b), last = ((it & 0xFFu) + 1u) * UP_PER_BANK;
        b[n++] = '-';
        up_slot_label(b + n, (last > UP_SLOTS ? UP_SLOTS : last) - 1u);
        str_cpy(b + str_len(b), ", ", 3);
        sti_line(it, b + str_len(b));
        return;
    }
    if (m) {                                            /* "PPG, track 2 goes silent" */
        uint32_t n = str_len(b), c = 0;
        str_cpy(b + n, m & (m - 1u) ? ", tracks " : ", track ", 32 - n);
        for (uint32_t p = 0; p < NPART; p++)
            if (m >> p & 1u) {
                n = str_len(b);
                if (c++) { b[n++] = ' '; }
                b[n++] = (char)('1' + p);
                b[n] = 0;
            }
        str_cpy(b + str_len(b), " silent", 32 - str_len(b));
    }
}
#ifndef UP_HOST
#if MELODEE_FLASH
/* bank b's record gone once none of its slots holds a sound: its sector back to the pool. 0 done */
static int up_bank_drop(uint32_t b)
{
    for (uint32_t k = b * UP_PER_BANK; k < (b + 1u) * UP_PER_BANK && k < UP_SLOTS; k++)
        if (up_used(k) || up_old(k))
            return 1;
    if (up_cached == b)
        up_cached = 255;                                /* (read again: none there now, an empty bank) */
    return st_exists(up_obj(b)) ? st_project_delete(up_obj(b)) : 0;
}
#endif
#endif
/* the question's yes */
static void sti_delete(uint32_t it)
{
    uint32_t i = it & 0xFFu;
    int rc = 1;
    (void)i;
    if (transport_busy()) {
        ui_message("STOP TO DELETE");
        return;
    }
    switch (it >> 8) {
#ifdef MODULE_STORE
    case STI_SYNTH:
        rc = mod_delete(i);
        if (!rc) { ui_message("SYNTH DELETED"); sti.ok = 0; }
        break;
#endif
#ifdef REC_STORE
    case STI_KIT:
        rc = rec_delete(i);
        if (!rc) ui_message("KIT DELETED");
        break;
#endif
#if MELODEE_FLASH && !defined(UP_HOST)
    case STI_SOUNDS:
        rc = 0;
        for (uint32_t k = i * UP_PER_BANK; !rc && k < (i + 1u) * UP_PER_BANK && k < UP_SLOTS; k++)
            if (up_used(k)) {
                up_ui_named(1u, k, 0);
                rc = up_used(k);                        /* (it said why) */
            }
        if (!rc && up_bank_drop(i))
            rc = 2;
        if (!rc) ui_message("SOUNDS ERASED");
        else if (rc == 2) ui_message("OLD FORMAT: ERASE IN USER");
        return;
#endif
    default:
        break;
    }
    if (rc)
        ui_message("NOT DELETED");
    ui.force = 1;
}
