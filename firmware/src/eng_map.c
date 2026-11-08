/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The device's engine map: an engine number <-> a synth module's UID (mod_abi3.h mod_ident_v3: CRC-32 of
 * "<vendor>/<name>") and its name. Records keep their 8-bit engine numbers; the map says who each number is.
 *  - The defaults are the manifest's (../modules/shipped.h): the synths Melodee makes keep their numbers (a store
 *    from before the map names them by these alone, so it reads as it always did).
 *  - A module the device has not seen gets the number it asks for when that one is free (ENGI_MOD0..), else the first
 *    free one (module_store.c mod_catalog). Numbers below ENGI_MOD0 are Melodee's own history: only their UIDs.
 *  - A store that names a module this device does not have (a project, a user-preset bank, a .mel from another device:
 *    their map entries) gives it a number too: its sounds keep their bytes and play silent, its name shows (eng_name).
 *  - The data records' owners (rec_store.c): the UID of the module that wrote each slot (0: the manifest's General MIDI
 *    kit synth, which wrote every record before the map).
 * Kept in the pool record ST_MAPOBJ (only what differs from the defaults; none: no record), saved from the main loop
 * when the transport is stopped (eng_map_service). Built for size (tools/size_fns.py). */
typedef struct { uint32_t uid; char name[MOD3_NAME_MAX]; } eng_id_t;
#define SHIP_MAPROW(n, u, nm, tg, wc, st, ns, f) [n] = {u, nm},
static eng_id_t eng_map[NENGINES] = {SHIP_ROWS(SHIP_MAPROW)};
#undef SHIP_MAPROW
static uint32_t rec_uid[MOD3_NREC];                   /* the data records' owners (0: the default) */
static uint8_t eng_map_dirty;                       /* the map changed: saved when the transport stops */
#define EMAP_MAGIC 0x31504D45u                      /* "EMP1": the map record */
#define EMAP_ENTRY 24u                              /* a stored entry: number, 0, 0, 0, uid, name[16] */
/* a store's entries (projects, user-preset banks): at most this many numbers that differ from the defaults */
#define EMAP_STORE_MAX (NENGINES - ENGI_MOD0 + 8u)

/* the defaults again (module_store.c mod_boot: a power-on, before the record and the slots) */
static void eng_map_defaults(void)
{
    memset(eng_map, 0, sizeof eng_map);
    for (uint32_t k = 0; k < NSHIP; k++) {
        eng_map[SHIP[k].num].uid = SHIP[k].uid;
        for (uint32_t i = 0; i < MOD3_NAME_MAX && SHIP[k].name[i]; i++)
            eng_map[SHIP[k].num].name[i] = SHIP[k].name[i];
    }
    memset(rec_uid, 0, sizeof rec_uid);
    eng_map_dirty = 0;
}
static uint32_t eng_map_default(uint32_t e)         /* the manifest's UID for e, 0: none */
{
    const ship_t *r = e < NENGINES ? ship_of(e) : 0;
    return r ? r->uid : 0u;
}
static uint32_t eng_map_find(uint32_t uid)          /* the number uid has here, NENGINES: none */
{
    for (uint32_t e = 0; uid && e < NENGINES; e++)
        if (eng_map[e].uid == uid)
            return e;
    return NENGINES;
}
/* a number for uid (name: its name, want: the number it asks for): the one it has, else want when that is free and a
 * module's (ENGI_MOD0..), else the first free one; NENGINES: none left. A new entry is saved later (eng_map_service) */
static uint32_t eng_map_take(uint32_t uid, const char *name, uint32_t want)
{
    uint32_t e = eng_map_find(uid);
    if (e < NENGINES || !uid)
        return e;
    if (!(want >= ENGI_MOD0 && want < NENGINES && !eng_map[want].uid))
        for (want = ENGI_MOD0; want < NENGINES && eng_map[want].uid; want++)
            ;
    if (want >= NENGINES)
        return NENGINES;
    eng_map[want].uid = uid;
    memset(eng_map[want].name, 0, MOD3_NAME_MAX);
    for (uint32_t i = 0; i < MOD3_NAME_MAX && name[i]; i++)
        eng_map[want].name[i] = name[i];
    eng_map_dirty = 1;
    return want;
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
/* e's entry differs from the defaults (a store that names e carries it) */
static int eng_map_own(uint32_t e)
{
    return e < NENGINES && eng_map[e].uid && eng_map[e].uid != eng_map_default(e);
}
/* the data record slot u's owner's UID */
static uint32_t rec_owner_uid(uint32_t u)
{
    uint32_t e = ship_flag(SHIP_F_GM);
    return u < MOD3_NREC && rec_uid[u] ? rec_uid[u] : e < NENGINES ? eng_map_default(e) : 0u;
}

/* ---- a store's entries: n numbers' {number, uid, name} as EMAP_ENTRY bytes each (the map record's layout) */
static void eng_map_put(uint8_t *b, uint32_t e)
{
    memset(b, 0, EMAP_ENTRY);
    b[0] = (uint8_t)e;
    for (uint32_t i = 0; i < 4u; i++)
        b[4u + i] = (uint8_t)(eng_map[e].uid >> (8u * i));
    memcpy(b + 8, eng_map[e].name, MOD3_NAME_MAX);
}
/* one entry read back: the number it has here (taken when new), NENGINES: a bad entry or none left */
static uint32_t eng_map_get(const uint8_t *b, uint32_t *stored)
{
    uint32_t uid = b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
    char nm[MOD3_NAME_MAX + 1u];
    memcpy(nm, b + 8, MOD3_NAME_MAX);
    nm[MOD3_NAME_MAX] = 0;
    *stored = b[0];
    return b[0] < NENGINES && uid && !b[1] && !b[2] && !b[3] ? eng_map_take(uid, nm, b[0]) : NENGINES;
}
/* the remap a store's entries make (identity for the numbers they leave out): eng_in() during its load. Kept as
 * stored ^ here, so the zeros it starts with are the identity */
static uint8_t eng_remap[NENGINES];
static void eng_remap_reset(void) { memset(eng_remap, 0, sizeof eng_remap); }
/* n entries at b: remap their numbers; 0, or 1 (a bad entry: the store is refused) */
static int eng_remap_read(const uint8_t *b, uint32_t n)
{
    eng_remap_reset();
    for (uint32_t k = 0; k < n; k++, b += EMAP_ENTRY) {
        uint32_t from, to = eng_map_get(b, &from);
        if (from >= NENGINES)
            return 1;
        eng_remap[from] = (uint8_t)(from ^ (to < NENGINES ? to : ENGI_ANALOG));   /* (none left: silent, as retired) */
    }
    return 0;
}
static uint32_t eng_in(uint32_t e) { return e < NENGINES ? e ^ eng_remap[e] : e; }

static int (*eng_map_saver)(void);                 /* module_store.c mod_map_save: the map record (0: no store) */
/* main loop: a changed map saved once nothing plays (the record is one sector) */
static void eng_map_service(int idle)
{
    if (eng_map_dirty && idle && eng_map_saver)
        (void)eng_map_saver();
}
/* a module that is gone from the device keeps its number while a store may name it: its entry stays (deleting a
 * module never renumbers anything) */
