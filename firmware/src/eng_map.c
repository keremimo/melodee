/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The device's engine map: an engine handle <-> a synth's ID (mod_abi3.h mod_ident_v3: 8 bytes made from its author's
 * key and its slug, little endian as a uint64_t here) and its name. Handles are this device's, in RAM (core.h), never a
 * synth's identity: a store keeps the handles its sounds had where it was written and an entry for each one it names
 * (its ID and name), and reads them back as this device numbers those synths; a byte no entry names is none.
 *  - At power-on: the MIDI Track at ENGI_MIDI, the manifest's synths (../modules/shipped.h) at ENG_H_SHIP0 + their rows.
 *  - An installed module gets the first free handle after them (module_store.c mod_catalog) and keeps it while it is
 *    installed.
 *  - A store that names a synth this device does not have (a project, a user-preset bank, a .mel from another device)
 *    is lent a handle: its sounds keep their bytes and play silent, its name shows (eng_name), and the store written
 *    again names it as it did. A lent handle nothing in RAM names any more is given back when another is needed.
 *  - The data records' owners (rec_store.c): the ID of the synth that wrote each slot, kept in the pool record ST_MAPOBJ,
 *    saved from the main loop when the transport is stopped (a record written with no module running, a backup's, is
 *    the manifest's SHIP_F_RECS synth's).
 * Built for size (tools/size_fns.py). */
typedef struct { uint64_t uid; char name[MOD3_NAME_MAX]; } eng_id_t;
#define SHIP_MAPROW(u, nm, tg, wc, st, ns, f) {u, nm},
/* (RAM: the defaults until mod_boot; the rows follow the MIDI Track, at ENG_H_SHIP0 on) */
static eng_id_t eng_map[NENGINES] = {[ENGI_MIDI] = {SHIP_ID_MIDI, "MIDI Track"}, SHIP_ROWS(SHIP_MAPROW)};
#undef SHIP_MAPROW
_Static_assert(ENG_H_SHIP0 == ENGI_MIDI + 1u, "eng_map: the rows follow the MIDI Track's entry");
static uint64_t rec_uid[MOD3_NREC];                   /* the data records' owners (0: the default) */
static uint8_t eng_map_dirty;                       /* the owners changed: saved when the transport stops */
#define EMAP_MAGIC 0x324E574Fu                      /* "OWN2": the owners record, every owner named (2.0) */
#define EMAP_ENTRY 28u                              /* a stored entry: number, 0, 0, 0, ID (8 bytes), name[16] */
static uint64_t mod_le64(const uint8_t *b)
{
    uint64_t v = 0;
    for (uint32_t i = 8; i--;)
        v = v << 8 | b[i];
    return v;
}
static void mod_put64(uint8_t *b, uint64_t v) { for (uint32_t i = 0; i < 8u; i++) b[i] = (uint8_t)(v >> (8u * i)); }

/* the defaults again (module_store.c mod_boot: a power-on, before the owners and the slots) */
static void eng_map_defaults(void)
{
    memset(eng_map, 0, sizeof eng_map);
    eng_map[ENGI_MIDI].uid = SHIP_ID_MIDI;
    memcpy(eng_map[ENGI_MIDI].name, "MIDI Track", 10);
    for (uint32_t k = 0; k < NSHIP; k++) {
        eng_map[ENG_H_SHIP0 + k].uid = SHIP[k].uid;
        for (uint32_t i = 0; i < MOD3_NAME_MAX && SHIP[k].name[i]; i++)
            eng_map[ENG_H_SHIP0 + k].name[i] = SHIP[k].name[i];
    }
    memset(rec_uid, 0, sizeof rec_uid);
    eng_map_dirty = 0;
}
static uint64_t eng_map_default(uint32_t e)         /* the ID handle e has at every power-on, 0: none */
{
    const ship_t *r = e < NENGINES ? ship_of(e) : 0;
    return e == ENGI_MIDI ? SHIP_ID_MIDI : r ? r->uid : 0u;
}
static uint32_t eng_map_find(uint64_t uid)          /* the handle uid has here, NENGINES: none */
{
    for (uint32_t e = 0; uid && e < NENGINES; e++)
        if (eng_map[e].uid == uid)
            return e;
    return NENGINES;
}
/* project.c: e is lent (a synth a store named, not installed here) and nothing in RAM names it: 1 */
static int (*eng_map_spare)(uint32_t e);
static uint32_t eng_map_reading;                     /* the handles the store being read has been given (eng_map_get):
                                                     * not given back meanwhile, though nothing names them yet */
_Static_assert(NENGINES <= 32u, "eng_map_reading: a bit a handle");
#define ENG_H_FREE0 (ENG_H_SHIP0 + (uint32_t)NSHIP)  /* the first handle a module or a lent ID may have */
static uint32_t eng_map_free(void)                  /* the first free handle, NENGINES: none */
{
    uint32_t e;
    for (e = ENG_H_FREE0; e < NENGINES && eng_map[e].uid; e++)
        ;
    return e;
}
/* a handle for uid (name: its name): the one it has, else the first free one, else a lent one nothing names any more;
 * NENGINES: none left */
static uint32_t eng_map_take(uint64_t uid, const char *name)
{
    uint32_t e = eng_map_find(uid);
    if (e < NENGINES && !eng_map[e].name[0])    /* (lent with no name, a preset bank's IDs: a store's entry names it) */
        for (uint32_t i = 0; i < MOD3_NAME_MAX && name[i]; i++)
            eng_map[e].name[i] = name[i];
    if (e < NENGINES || !uid)
        return e;
    if ((e = eng_map_free()) >= NENGINES && eng_map_spare) {
        for (uint32_t k = ENG_H_FREE0; k < NENGINES; k++)   /* (none free: the lent ones nothing names, given back) */
            if (!(eng_map_reading >> k & 1u) && eng_map_spare(k))
                memset(&eng_map[k], 0, sizeof eng_map[k]);
        e = eng_map_free();
    }
    if (e >= NENGINES)
        return NENGINES;
    eng_map[e].uid = uid;
    memset(eng_map[e].name, 0, MOD3_NAME_MAX);
    for (uint32_t i = 0; i < MOD3_NAME_MAX && name[i]; i++)
        eng_map[e].name[i] = name[i];
    return e;
}
/* e's name from the map (a module this device has not got: what the store that named it called it), 0: none */
static const char *eng_map_name(uint32_t e)
{
    static char nm[MOD3_NAME_MAX + 1u];
    if (e >= NENGINES || !eng_map[e].uid || !eng_map[e].name[0])
        return 0;
    memcpy(nm, eng_map[e].name, MOD3_NAME_MAX);
    nm[MOD3_NAME_MAX] = 0;
    return nm;
}
/* e names a synth: a store that names e carries its entry */
static int eng_map_own(uint32_t e)
{
    return e != ENG_H_NONE && e < NENGINES && eng_map[e].uid;
}
/* the data record slot u's owner's ID */
static uint64_t rec_owner_uid(uint32_t u)
{
    uint32_t e = ship_flag(SHIP_F_RECS);
    return u < MOD3_NREC && rec_uid[u] ? rec_uid[u] : e < NENGINES ? eng_map_default(e) : 0u;
}

/* ---- a store's entries: n numbers' {number, 0, 0, 0, ID, name} as EMAP_ENTRY bytes each */
static void eng_map_put(uint8_t *b, uint32_t e)
{
    memset(b, 0, EMAP_ENTRY);
    b[0] = (uint8_t)e;
    mod_put64(b + 4, eng_map[e].uid);
    memcpy(b + 12, eng_map[e].name, MOD3_NAME_MAX);
}
/* one entry read back: the number it has here (taken when new), NENGINES: a bad entry or none left */
static uint32_t eng_map_get(const uint8_t *b, uint32_t *stored)
{
    uint64_t uid = mod_le64(b + 4);
    char nm[MOD3_NAME_MAX + 1u];
    memcpy(nm, b + 12, MOD3_NAME_MAX);
    nm[MOD3_NAME_MAX] = 0;
    *stored = b[0];
    uint32_t e = b[0] < NENGINES && uid && !b[1] && !b[2] && !b[3] ? eng_map_take(uid, nm) : NENGINES;
    if (e < NENGINES)
        eng_map_reading |= 1u << e;
    return e;
}
/* the remap a store's entries make: eng_in() during its load. eng_remap[stored] = the handle here + 1; 0: the store
 * names no synth there (none) */
static uint8_t eng_remap[NENGINES];
static void eng_remap_reset(void) { memset(eng_remap, 0, sizeof eng_remap); eng_map_reading = 0; }
/* n entries at b: remap their handles; 0, or 1 (a bad entry: the store is refused) */
static int eng_remap_read(const uint8_t *b, uint32_t n)
{
    eng_remap_reset();
    for (uint32_t k = 0; k < n; k++, b += EMAP_ENTRY) {
        uint32_t from, to = eng_map_get(b, &from);
        if (from >= NENGINES || from == ENG_H_NONE)
            return 1;
        eng_remap[from] = (uint8_t)(to < NENGINES ? to + 1u : 0u);   /* (no handle left: none, silent) */
    }
    return 0;
}
/* n IDs at b (8 bytes each: a preset bank's, upreset.c) for the places 1..n: remap them (an ID not installed here lent a
 * handle with no name; 0 or none left: none) */
static void eng_remap_ids(const uint8_t *b, uint32_t n)
{
    eng_remap_reset();
    for (uint32_t k = 0; k < n && k + 1u < NENGINES; k++, b += 8) {
        uint64_t uid = mod_le64(b);
        uint32_t to = uid ? eng_map_take(uid, "") : NENGINES;
        if (to < NENGINES)
            eng_map_reading |= 1u << to;
        eng_remap[k + 1u] = (uint8_t)(to < NENGINES ? to + 1u : 0u);
    }
}
static uint32_t eng_in(uint32_t e) { return e < NENGINES && eng_remap[e] ? eng_remap[e] - 1u : ENG_H_NONE; }
#ifndef FM1_IRQ_TARGET
/* host tests: a record written and read back in one run, without its store's entries: its handles are this device's */
static void eng_remap_same(void)
{
    for (uint32_t e = 0; e < NENGINES; e++)
        eng_remap[e] = (uint8_t)(e + 1u);
}
#endif

static int (*eng_map_saver)(void);                 /* module_store.c mod_map_save: the owners record (0: no store) */
/* main loop: changed owners saved once nothing plays (the record is one sector) */
static void eng_map_service(int idle)
{
    if (eng_map_dirty && idle && eng_map_saver)
        (void)eng_map_saver();
}
