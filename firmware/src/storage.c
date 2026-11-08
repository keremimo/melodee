/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Settings and presets have A/B pairs. Projects share a 56-sector pool:
 * one current record per slot plus common working space for safe saves.
 * Legacy five-base/two-extension project copies remain readable. A save goes to space that is not
 * the current one: erase, write payload pages, then commit the 32-byte header
 * at offset 0 LAST. The header is the commit
 * record (magic, seq, length, payload CRC, header CRC); on load the valid
 * copy with the newest seq wins (including wrap), so a write torn at any point leaves the
 * previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_COMPACT_TAG 0x32504D43u             /* CMP2: preset payload begins at byte 32 */
#define ST_SETTINGS_TAG 0x31454C42u /* BLE1: settings payload at byte 32, including BLE trailer */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)
#define ST_BANK_BASE_MAX (5u * ST_SECTOR - ST_PAYLOAD_OFF)
#define ST_BANK_EXT_LO 0xEA000u
#define ST_BANK_EXT_MAX (2u * ST_PAYLOAD_MAX)
#define ST_PROJECT_SECTORS 8u                 /* a 27,904-byte record (pattern_store.c) */
#define ST_POOL_PAYLOAD_OFF 32u
#define ST_POOL_PAYLOAD_MAX (ST_SECTOR - 256u - ST_POOL_PAYLOAD_OFF)
#define ST_PROJECT_MAX (ST_PROJECT_SECTORS * (ST_SECTOR - 32u - 8u))   /* (ST_LIN_PAYLOAD_MAX) */
#if BOARD_SMK37
#define ST_POOL_SECTORS 60u                    /* the SMK-37's pool: indices 0..59 (st_pool_at), no former pools */
#define ST_SP_N 0u
#else
#define ST_POOL_SECTORS 56u                    /* the former project pool's sectors: shared indices 0..55 */
#define ST_SP_N 23u                            /* the former preset pool's: 56..78 (ST_SP_AT) */
#endif
#define ST_U_N (ST_POOL_SECTORS + ST_SP_N)     /* one pool for projects and preset banks */
#define ST_UW ((ST_U_N + 31u) / 32u)           /* words of a pool bitmap */
#define ST_POOL_TAG 0xA7000000u                /* a project record's sectors: a bitmap of indices 0..55 (read only) */
#define ST_LIST_TAG 0xA8000000u                /* .. a list: eight 7-bit indices, 0x7F after the last */
#define ST_SMALL_U0 100u                       /* a preset bank's copy in the pool: ST_SMALL_U0 + index */
_Static_assert(ST_PROJECT_SECTORS * 5u <= ST_U_N && ST_U_N < 0x7Fu && ST_SMALL_U0 > ST_SP_N + 1u, "pool numbering");
/* The store since the bridge (2026-11): every record but the settings is a list record (projects, user kits, preset
 * banks alike) under ST_LIN_TAG, its sectors given by linear indices: on the FM-1 every data-flash sector by address
 * (0x93000 + 4 KB * index), on the SMK-37 the pool's own windows. A predicate keeps the fixed places out (OTA
 * staging, the settings pair, the resource region's installed window). Payload in each sector: 32..0xEFF and
 * 0xF08..0xFFF; the 8 bytes at 0xF00 stay erased, so the update loader's record scan (ldr_core.c: +6 reads 0x5441)
 * never takes musical data for an update record. Old forms (fixed pairs, extensions, the 0xA7/0xA8 lists, the bank
 * copies) are still read; the boot converter (st_convert) re-saves them in this form. */
#define ST_GUARD_OFF 0xF00u
#define ST_GUARD_LEN 8u
#define ST_RUN0 (ST_GUARD_OFF - ST_POOL_PAYLOAD_OFF)   /* 3,808: a sector's payload before the guard */
#define ST_LIN_PAYLOAD_MAX (ST_SECTOR - ST_POOL_PAYLOAD_OFF - ST_GUARD_LEN)   /* 4,056 */
#define ST_LIN_TAG 0xA9000000u                 /* a list record: eight 7-bit linear indices, the 4,056-byte geometry */
#define ST_LIN_C0 256u                         /* a list record's copy number: ST_LIN_C0 + its first sector's index */
#ifndef FL_RES_LO                                      /* (hal/fm1_flash_map.h; host tests: the FM-1's) */
#define FL_RES_LO 0xCB000u
#define FL_RES_HI 0xE0000u
#endif
#ifndef FL_RES_RESERVE
#define FL_RES_RESERVE 9u
#define FL_RES_RESERVE_OLD 15u
#endif
/* the resource region's sectors the reserve gave the pool when the factory sounds went into the synth modules (2026-11:
 * FL_RES_RESERVE_OLD -> FL_RES_RESERVE): taken last (st_order), so a firmware from before keeps reading what it can */
#define ST_RES_GIVEN (FL_RES_RESERVE_OLD - FL_RES_RESERVE)
#if BOARD_SMK37
#define ST_LN (ST_U_N + ST_RES_GIVEN)                  /* the pool's windows, then the given sectors */
#else
#define ST_LIN_BASE 0x93000u
#define ST_LN 108u                             /* 0x93000..0xFEFFF */
#endif
#define ST_LW ((ST_LN + 31u) / 32u)            /* words of a linear bitmap */
_Static_assert(ST_LN < 0x7Fu && ST_LIN_C0 > ST_SMALL_U0 + ST_U_N && ST_RUN0 == ST_POOL_PAYLOAD_MAX &&
               ST_LIN_PAYLOAD_MAX == 4056u, "list record numbering");

/* The Prophet user banks: 0x93000..0x9CFFF. The old banks at 0x89000 are retired and never migrated.
 * The current banks occupy the legacy single-pattern projects' place: those objects are retired (2026-10-10, Kerem: not
 * wanted), their numbers kept, nothing loads or saves them; 0x9D000..0x9EFFF is the final shared preset pair.
 * The project pool occupies 0xA0000..0xC7FFF and 0xEA000..0xF9FFF. The preset banks share 23 sectors (ST_SP_AT);
 * 0xCB000..0xDFFFF is the resource region (res.c).
 * Legacy projects used two five-sector bases per slot plus two extensions per copy.
 * User samples are disabled: this area must never accept sample uploads.
 * Flash map: app area 0x4000..0x92FFF; Prophet banks 0x93000..0x9CFFF; shared extension 0x9D000..0x9EFFF; FM6 patch bank
 * copy A 0x9F000 (B 0xFE000); bank projects 0xA0000..0xC7FFF; CZ banks 0xC8000..0xD7FFF; native FM6 0xD8000..;
 * user preset banks 0xDC000..0xDFFFF (upreset.c); OTA staging 0xE0000..0xE4FFF; extensions 0xE5000..0xFAFFF;
 * settings 0xFC000 (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..) */
/* Object numbers 0..19 stay fixed. U33..U64 use E5000..E8FFF;
 * owned FM6 U01..U32 reuse the old bank pair, U33..U64 use E9000/FA000.
 * E0000..E4FFF remains reserved for OTA loader staging. */
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_FM6BANK = OBJ_UPRESET0 + 2, OBJ_BANK0, OBJ_CZBANK0 = OBJ_BANK0 + 4, OBJ_UPRESET_EXT0 = OBJ_CZBANK0 + 8, OBJ_UPFM6_EXT = OBJ_UPRESET_EXT0 + 2, OBJ_NATIVEFM0, OBJ_P5BANK0 = OBJ_NATIVEFM0 + 2, OBJ_SHARED_EXT = OBJ_P5BANK0 + 5, OBJ_COUNT };

/* Projects: 32 slots (project.c PROJ_SLOTS). The first four keep their objects (and legacy A/B copies); slots 5..32
 * are pool records of type ST_PFILE0 + slot. Empty slots own no sector. */
#define ST_NPROJ 32u
#define ST_PFILE0 64u
/* Data records (rec_store.c: a synth module's own, recorded kits): 8 slots, pool records of type ST_RFILE0 + slot, as
 * slots 5..32 are */
#ifndef MELODEE_RECORDS                      /* (pads.h sets it first in the firmware; storage tests alone: on) */
#define MELODEE_RECORDS 1
#endif
#define ST_NREC 8u
#define ST_RFILE0 (ST_PFILE0 + ST_NPROJ)
/* Synth modules (module_store.c): NMOD slots, a module file as built in up to MOD_RECS pool records (ST_PROJECT_MAX
 * bytes each, the last one the rest): slot k's record j is st_mod_obj(k, j). Slots 0..7's first two keep the numbers
 * they had (ST_MFILE0 + k, ST_MXFILE0 + k); the rest come after the device's engine map (eng_map.c) */
#ifndef NMOD
#define NMOD 16u                               /* (core.h; storage tests alone) */
#endif
#ifndef MOD_RECS
#define MOD_RECS 4u
#endif
#define ST_NMOD NMOD
#define ST_MFILE0 (ST_RFILE0 + ST_NREC)
#define ST_MXFILE0 (ST_MFILE0 + 8u)
#define ST_MAPOBJ (ST_MXFILE0 + 8u)            /* the device's engine map: number <-> UID, the data records' owners */
#define ST_MNEW0 (ST_MAPOBJ + 1u)
#define ST_TPLX (ST_MNEW0 + ST_NMOD * MOD_RECS)     /* the template's end block (project.c tmpl_ext): only when needed */
#define ST_OBJ_END (ST_TPLX + 1u)                   /* every object number below this */
static uint32_t st_mod_obj(uint32_t k, uint32_t j)
{
    return k < 8u && j < 2u ? ST_MFILE0 + 8u * j + k : ST_MNEW0 + k * MOD_RECS + j;
}
static uint32_t st_proj_obj(uint32_t slot) { return slot < 4u ? OBJ_BANK0 + slot : ST_PFILE0 + slot; }
/* a record that lives only in the pool, never in a fixed pair: projects 5..32, the user kits, the synth modules, the
 * engine map, the template's end block */
static int st_pfile(uint32_t obj) { return (obj >= ST_PFILE0 + 4u && obj < ST_PFILE0 + ST_NPROJ) || (obj >= ST_RFILE0 && obj < ST_OBJ_END); }
static int st_obj_ok(uint32_t obj) { return obj < OBJ_COUNT || st_pfile(obj); }

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;
_Static_assert(sizeof(st_hdr_t) == 32u, "storage commit record layout");

#ifndef ST_IDLE
#define ST_IDLE() ((void)0)                    /* (firmware: the watchdog, storage_hw.c) */
#endif
static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

/* Allocation must not mistake a failed read for free project space. */
static uint8_t st_read_failed;
/* every pool record published or deleted moves it on: a count of free sectors kept from before is stale (project.c
 * proj_space: the SLOTS card's room left after a kit, a module or a preset bank came or went) */
static uint32_t st_pool_gen;
static int st_fetch(uint32_t off, void *dst, uint32_t n)
{
    int rc = st_read(off, dst, n);
    if (rc) st_read_failed = 1;
    return rc;
}

static uint32_t st_crc32_update(uint32_t c, const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
static uint32_t st_crc32(const void *p,uint32_t n)
{return ~st_crc32_update(0xFFFFFFFFu,p,n);}

#if BOARD_SMK37
/* SMK-37 (hal/fm1_flash_map.h): the stock region table keeps BTIF / USRTRIM 0xC0000..0xC1FFF (the key calibration),
 * USR 0xF4000..0xFDFFF and key_mac 0xFF000, and its app area runs to 0x9D000, so Melodee has 79 free sectors in
 * three windows. The objects, the pool and the formats are the FM-1's; only the places differ:
 *   store A, FL_DATA 0x9D000..0xBAFFF (the stock VM): 0x9D000 the settings pair; 0x9F000..0xAEFFF the resource
 *     region (res.c, 16 sectors, written by the update loader inside its VM window); 0xAF000 the store owner tag
 *     (st_owner_check); 0xB0000..0xBAFFF pool indices 0..10
 *   store B, FL_UFL 0xC2000..0xE9FFF (USRFLASH below its end marker): pool indices 11..50
 *   0xEA000 the stock end marker's sector: never data (FL_STOCKTAG, only erased as the release below)
 *   store C, FL_GAP 0xEB000..0xF3FFF (unlisted, below USR): pool indices 51..59
 * Nothing was ever stored on an SMK in the old layouts: the legacy A/B copies of every object but the settings are
 * absent (st_sector 0, st_head -1), and there is no former preset pool (ST_SP_N 0). */
#define ST_SMK_TAG 0xAF000u
#define ST_SMK_POOL_A 0xB0000u
#define ST_SMK_POOL_A_N 11u
#define ST_SMK_POOL_B_N 40u
_Static_assert(FL_DATA_LO == 0x9D000u && FL_RES_LO == 0x9F000u && FL_RES_HI == ST_SMK_TAG &&
               ST_SMK_TAG + ST_SECTOR == ST_SMK_POOL_A && ST_SMK_POOL_A + ST_SMK_POOL_A_N * ST_SECTOR == FL_DATA_HI &&
               FL_UFL_LO + ST_SMK_POOL_B_N * ST_SECTOR == FL_UFL_HI &&
               FL_GAP_LO + (ST_POOL_SECTORS - ST_SMK_POOL_A_N - ST_SMK_POOL_B_N) * ST_SECTOR == FL_GAP_HI,
               "SMK store: the windows and the pool disagree");
static uint32_t st_pool_at(uint32_t i)                 /* a pool index's sector */
{
    return i < ST_SMK_POOL_A_N ? ST_SMK_POOL_A + i * ST_SECTOR
         : i < ST_SMK_POOL_A_N + ST_SMK_POOL_B_N ? FL_UFL_LO + (i - ST_SMK_POOL_A_N) * ST_SECTOR
         : i < ST_U_N ? FL_GAP_LO + (i - ST_SMK_POOL_A_N - ST_SMK_POOL_B_N) * ST_SECTOR : 0u;
}
#else
/* The 22 preset banks (upreset.c up_obj) share one pool: a record a sector anywhere in it, the newest valid one of
 * each the current; a save takes a sector no current record holds. One spare serves them all (one save at a time),
 * where each A/B pair kept a private one. Copies 0/1 still read the old fixed pairs; copies 2.. are pool sectors.
 * Only sectors the banks used before: the rest of their old place is the resource region (res.c). */
static const uint32_t ST_SP_AT[] = {0x93000u, 0x94000u, 0x95000u, 0x96000u, 0x97000u, 0x98000u, 0x99000u, 0x9A000u,
    0x9B000u, 0x9C000u, 0x9D000u, 0x9E000u, 0x9F000u, 0xC8000u, 0xC9000u, 0xCA000u, 0xE5000u, 0xE6000u, 0xE7000u,
    0xE8000u, 0xE9000u, 0xFA000u, 0xFE000u};
_Static_assert(sizeof ST_SP_AT / sizeof ST_SP_AT[0] == ST_SP_N, "the former preset pool");
static uint32_t st_pool_at(uint32_t i)                 /* a pool index's sector */
{
    return i < 40u ? 0xA0000u + i * ST_SECTOR : i < ST_POOL_SECTORS ? ST_BANK_EXT_LO + (i - 40u) * ST_SECTOR
         : i < ST_U_N ? ST_SP_AT[i - ST_POOL_SECTORS] : 0u;
}
#endif
/* the linear index space of list records (ST_LIN_TAG): a sector's address, and back (ST_LN: none) */
#if BOARD_SMK37
static uint32_t st_lin_at(uint32_t i)
{
    return i < ST_U_N ? st_pool_at(i) : i < ST_LN ? FL_RES_LO + (FL_RES_RESERVE + i - ST_U_N) * ST_SECTOR : 0u;
}
static uint32_t st_lin_of(uint32_t addr)
{
    uint32_t i = 0;
    while (i < ST_LN && st_lin_at(i) != (addr & ~(ST_SECTOR - 1u))) i++;
    return i;
}
#else
static uint32_t st_lin_at(uint32_t i) { return i < ST_LN ? ST_LIN_BASE + i * ST_SECTOR : 0u; }
static uint32_t st_lin_of(uint32_t addr) { return addr >= ST_LIN_BASE && addr < ST_LIN_BASE + ST_LN * ST_SECTOR ? (addr - ST_LIN_BASE) / ST_SECTOR : ST_LN; }
#endif
/* The resource region's installed window ends here (res.c res_window, from the header in flash): the pool takes
 * sectors of the region only above it, so no update loader ever blanks a record (ldr_core.c ldr_res). Until boot
 * has read the header: the whole region, as every firmware before the bridge left it. */
static uint32_t st_res_end = FL_RES_HI;
/* a linear sector a list record may take: never OTA staging, the settings pair or the installed resource window */
static int st_lin_ok(uint32_t i)
{
#if BOARD_SMK37
    return i < ST_U_N || (i < ST_LN && st_lin_at(i) >= st_res_end);   /* (the pool's windows hold nothing else) */
#else
    uint32_t a = st_lin_at(i);
    return a && !(a >= 0xE0000u && a < 0xE5000u) && !(a >= 0xFC000u && a < 0xFE000u) &&
           !(a >= FL_RES_LO && a < st_res_end);
#endif
}
static int st_preset_object(uint32_t obj);
static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* legacy A/B (0/1), or project pool index + 2; 0: no such sector */
{
    if (copy >= ST_LIN_C0) return obj != OBJ_SETTINGS && copy - ST_LIN_C0 < ST_LN ? st_lin_at(copy - ST_LIN_C0) : 0u;
    if (((obj >= OBJ_BANK0 && obj < OBJ_CZBANK0) || st_pfile(obj)) && copy >= 2u) {
        return st_pool_at(copy - 2u);
    }
    if (copy >= ST_SMALL_U0 && st_preset_object(obj)) return st_pool_at(copy - ST_SMALL_U0);
#if BOARD_SMK37
    if (copy >= 2u) return 0u;                          /* (no former preset pool) */
    return obj == OBJ_SETTINGS ? FL_DATA_LO + copy * ST_SECTOR : 0u;   /* no legacy pairs on the SMK */
#else
    if (copy >= 2u && st_preset_object(obj)) return copy - 2u < ST_SP_N ? ST_SP_AT[copy - 2u] : 0u;
    if (obj == OBJ_SHARED_EXT) return 0x9D000u+copy*ST_SECTOR;
    if (obj >= OBJ_P5BANK0) return 0x93000u+(obj-OBJ_P5BANK0)*2u*ST_SECTOR+copy*ST_SECTOR;
    if (obj >= OBJ_NATIVEFM0) return 0xD8000u + (obj - OBJ_NATIVEFM0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj == OBJ_UPFM6_EXT) return copy ? 0xFA000u : 0xE9000u;
    if (obj >= OBJ_UPRESET_EXT0) return 0xE5000u + (obj - OBJ_UPRESET_EXT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj >= OBJ_CZBANK0)
        return 0xC8000u + (obj - OBJ_CZBANK0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj >= OBJ_BANK0)
        return 0xA0000u + (obj - OBJ_BANK0) * 10u * ST_SECTOR + copy * 5u * ST_SECTOR;
    if (obj == OBJ_SETTINGS)
        return 0xFC000u + copy * ST_SECTOR;
    if (obj == OBJ_FM6BANK)
        return copy ? 0xFE000u : 0x9F000u;
    if (obj >= OBJ_UPRESET0)
        return 0xDC000u + (obj - OBJ_UPRESET0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    return 0x97000u + (obj - OBJ_PROJECT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
#endif
}

#if BOARD_SMK37
/* The owner of the store. Several FM-1 firmwares are ported to the SMK-37 with the same board layer and
 * the same store area, each with its own object formats: a firmware must never parse another's data. The
 * sector 0xAF000 (the same in every port) holds "SMK37DATA" + the firmware family (FELUCCA_DATA_FAMILY,
 * build.py; Melodee: MELODEE). Ours: load as ever. Erased (data from before the tag) or another family's:
 * clear Melodee's whole store (A, B and C; the other firmware's projects, its own backups are its business)
 * and take it; FELUCCA_DATA_UNTAGGED_OK=1 would keep untagged data (Melodee never wrote any on an SMK).
 * The resource region (res.c) is not the store: the update loader writes it before the first start, and it
 * carries its own commit record, so the clear leaves it alone. Calibration, BT pairing, USR and key_mac lie
 * outside the three windows: never touched. */
#ifndef FELUCCA_DATA_FAMILY
#define FELUCCA_DATA_FAMILY "MELODEE"
#endif
#ifndef FELUCCA_DATA_UNTAGGED_OK
#define FELUCCA_DATA_UNTAGGED_OK 0
#endif
_Static_assert(ST_SMK_TAG >= FL_DATA_LO && ST_SMK_TAG + ST_SECTOR <= FL_DATA_HI, "SMK store owner: outside FL_DATA");
_Static_assert(sizeof FELUCCA_DATA_FAMILY - 1u >= 1u && sizeof FELUCCA_DATA_FAMILY - 1u <= 7u, "FELUCCA_DATA_FAMILY: 1..7 characters");
/* The stock firmware's mapping in USRFLASH (global settings at 0xC2000, magic A3 3A; 8 presets from 0xC3000 every
 * 0x5000, 0x29E3 bytes each, the rest erased; the end marker "usrdata\0" at 0xEAFF8, FL_STOCKTAG): the stock firmware
 * trusts the area while the marker is there and never rebuilds it, so after a return to stock Melodee's store B
 * would be its key, knob and fader mapping (every key one note). So: an intact stock mapping is kept (it reads as
 * free pool sectors), and the marker is released (its sector erased: the stock firmware rebuilds its factory mapping
 * at its next start) before the first erase in USRFLASH (storage_hw.c st_erase) and at boot when it stands
 * over anything else (st_owner_check: ours, an older build's, another firmware's). */
static int st_stock_tagged(void)
{
    static const uint8_t TAG[8] = {'u', 's', 'r', 'd', 'a', 't', 'a', 0};
    uint8_t t[8];
    uint32_t i;
    if (st_read(FL_STOCKTAG_OFF, t, 8))
        return 0;
    for (i = 0; i < 8u; i++)
        if (t[i] != TAG[i])
            return 0;
    return 1;
}
static int st_stock_intact(void)                /* the marker over the stock layout */
{
    uint8_t b[8];
    uint32_t n, k, i, ff;
    if (!st_stock_tagged() || st_read(FL_UFL_LO, b, 2) || b[0] != 0xA3u || b[1] != 0x3Au)
        return 0;
    for (n = 0; n < 8u; n++)                    /* each preset: its head written, its last two sectors erased */
        for (k = 0; k < 3u; k++) {
            if (st_read(FL_UFL_LO + 0x1000u + n * 0x5000u + (k ? 0x2000u + k * 0x1000u : 0u), b, 8))
                return 0;
            for (ff = 1, i = 0; i < 8u; i++)
                ff &= b[i] == 0xFFu;
            if (ff != (k != 0u))
                return 0;
        }
    return 1;
}
#ifndef ST_ERASE_MARKER
#define ST_ERASE_MARKER() st_erase(FL_STOCKTAG_LO)   /* (host tests; the firmware: storage_hw.c, the raw erase) */
#endif
static void st_stock_release(void)              /* erase the marker's sector, once: later calls find no marker */
{
    if (st_stock_tagged())
        (void)ST_ERASE_MARKER();
}
static inline int st_in_usrflash(uint32_t off) { return off >= FL_UFL_LO && off < FL_STOCKTAG_LO; }
static inline int st_in_res(uint32_t off) { return off >= FL_RES_LO && off < FL_RES_HI; }
static uint8_t st_foreign;                         /* the store could not be cleared: RAM-only this boot (persist_boot) */
static int st_owner_check(void)                    /* 1: another firmware's (or untagged) store was cleared; -1: an erase
                                                    * failed: not tagged as ours, so the next boot clears it again */
{
    static const char FAM[] = FELUCCA_DATA_FAMILY;
    uint8_t tag[16], want[16];
    uint32_t i, erased = 1, ours = 1, s, cleared = 0, keep, bad = 0;
    for (i = 0; i < 16u; i++)
        want[i] = i < 9u ? (uint8_t)"SMK37DATA"[i] : i - 9u < sizeof FAM - 1u ? (uint8_t)FAM[i - 9u] : 0u;
    if (!st_stock_intact())
        st_stock_release();                     /* the marker over anything but the stock mapping: let it rebuild */
    if (st_read(ST_SMK_TAG, tag, 16))
        return 0;
    for (i = 0; i < 16u; i++) {
        erased &= tag[i] == 0xFFu;
        ours &= tag[i] == want[i];
    }
    if (ours)
        return 0;
    if (!erased || !FELUCCA_DATA_UNTAGGED_OK) {     /* not ours: clear the store */
        for (s = FL_DATA_LO; s < FL_DATA_HI; s += ST_SECTOR)
            if (s != ST_SMK_TAG && !st_in_res(s)) {
                bad |= st_erase(s) != 0;
                ST_IDLE();
            }
        keep = st_stock_intact();                   /* the stock mapping: kept until Melodee writes there */
        for (s = FL_UFL_LO; !keep && s < FL_UFL_HI; s += ST_SECTOR) {
            bad |= st_erase(s) != 0;
            ST_IDLE();
        }
        for (s = FL_GAP_LO; s < FL_GAP_HI; s += ST_SECTOR) {
            bad |= st_erase(s) != 0;
            ST_IDLE();
        }
        for (s = FL_RES_LO + FL_RES_RESERVE * ST_SECTOR; s < FL_RES_HI; s += ST_SECTOR) {
            bad |= st_erase(s) != 0;                /* (the resource sectors the pool may take: never another's data) */
            ST_IDLE();
        }
        cleared = 1;
    }
    if (bad || st_erase(ST_SMK_TAG) || st_prog(ST_SMK_TAG, want, 16))
        return -1;                                  /* the tag only over a store that is all ours */
    return (int)cleared;
}
#else
static int st_owner_check(void) { return 0; }
#endif

static int st_banked(uint32_t obj) { return (obj >= OBJ_BANK0 && obj < OBJ_CZBANK0) || st_pfile(obj); }
/* New preset objects start directly after the commit header, leaving the last
 * 256 bytes erased so the SPL/update loader cannot interpret musical data as OTA. */
static uint32_t st_payload_off(uint32_t obj) { return obj >= OBJ_UPRESET_EXT0 ? sizeof(st_hdr_t) : ST_PAYLOAD_OFF; }
static uint32_t st_capacity(uint32_t obj) { return obj == OBJ_SETTINGS ? ST_SECTOR - sizeof(st_hdr_t) : st_banked(obj) ? ST_PROJECT_MAX : obj >= OBJ_UPRESET_EXT0 ? ST_SECTOR - 256u - sizeof(st_hdr_t) : ST_PAYLOAD_MAX; }
static int st_preset_object(uint32_t obj)
{
    return (obj>=OBJ_UPRESET0 && obj<OBJ_BANK0) || (obj>=OBJ_CZBANK0 && obj<OBJ_COUNT);
}
static int st_small(uint32_t obj) { return st_preset_object(obj); }
static int st_listed(uint32_t obj) { return st_banked(obj) || st_small(obj); }   /* saved as a list record */
static int st_copy_ok(uint32_t obj, uint32_t copy)    /* 0/1 the fixed pair; 2.. a pool sector; ST_LIN_C0.. a list record */
{
    if (copy >= ST_LIN_C0) return st_listed(obj) && copy < ST_LIN_C0 + ST_LN;
    return copy < 2u || (st_banked(obj) && copy < ST_U_N + 2u) ||
           (st_small(obj) && (copy < ST_SP_N + 2u || (copy >= ST_SMALL_U0 && copy < ST_SMALL_U0 + ST_U_N)));
}
static uint32_t st_payload_off_header(uint32_t obj,const st_hdr_t *h)
{
    if ((st_banked(obj) && h->slot >= 2u) || h->slot >= ST_LIN_C0) return ST_POOL_PAYLOAD_OFF;
    return (st_preset_object(obj) && h->rsv[0]==ST_COMPACT_TAG) || (obj == OBJ_SETTINGS && h->rsv[0] == ST_SETTINGS_TAG) ? sizeof(st_hdr_t) : st_payload_off(obj);
}
static uint32_t st_extension(uint32_t obj, uint32_t copy)   /* (the FM-1's legacy copies; never reached on the SMK) */
{
    return ST_BANK_EXT_LO + ((obj - OBJ_BANK0) * 2u + copy) * 2u * ST_SECTOR;
}
/* One pool of 79 sectors holds projects and preset banks. A project header's reserved words carry its sectors in
 * order (ST_LIST_TAG: eight 7-bit pool indices; ST_POOL_TAG: the former 56-bit bitmap, still read) and an 8-bit tag;
 * a preset bank is one sector. Payloads start at 32 in every sector; the last 256 bytes stay erased for the SPL.
 * slot >= 2 identifies the first sector (pool index + 2, a bank's ST_SMALL_U0 + index); 0/1 remain legacy A/B. */
static int st_pool_bit(const st_hdr_t *h, uint32_t i)
{return (h->rsv[i / 32u] >> (i % 32u)) & 1u;}
static uint32_t st_list_get(const st_hdr_t *h, uint32_t k)
{
    uint32_t v = 0;
    for (uint32_t b = 0, p = 7u * k; b < 7u; b++, p++) v |= ((h->rsv[p / 32u] >> (p % 32u)) & 1u) << b;
    return v;
}
static void st_list_put(st_hdr_t *h, uint32_t k, uint32_t v)
{
    for (uint32_t b = 0, p = 7u * k; b < 7u; b++, p++) {
        if ((v >> b) & 1u) h->rsv[p / 32u] |= 1u << (p % 32u);
        else h->rsv[p / 32u] &= ~(1u << (p % 32u));
    }
}
static void st_list_init(st_hdr_t *h) { h->rsv[0] = 0xFFFFFFFFu; h->rsv[1] = ST_LIN_TAG | 0x00FFFFFFu; }
static int st_rec_lin(const st_hdr_t *h) { return (h->rsv[1] & 0xFF000000u) == ST_LIN_TAG; }
/* a record's payload bytes per sector: the list form's 4,056, the old pool forms' 3,808 */
static uint32_t st_geom(const st_hdr_t *h) { return st_rec_lin(h) ? ST_LIN_PAYLOAD_MAX : ST_POOL_PAYLOAD_MAX; }
/* a pool record's sectors in order (indices in its tag's space: linear, or the old pool's); their count, 0 when it
 * has no pool shape */
static uint32_t st_rec_sectors(const st_hdr_t *h, uint8_t idx[ST_PROJECT_SECTORS])
{
    uint32_t tag = h->rsv[1] & 0xFF000000u, n = 0;
    if (tag == ST_POOL_TAG) {
        for (uint32_t i = 0; i < ST_POOL_SECTORS; i++) if (st_pool_bit(h, i)) { if (n == ST_PROJECT_SECTORS) return 0; idx[n++] = (uint8_t)i; }
    } else if (tag == ST_LIST_TAG || tag == ST_LIN_TAG) {
        for (uint32_t k = 0; k < ST_PROJECT_SECTORS; k++) {
            uint32_t v = st_list_get(h, k);
            if (v == 0x7Fu) break;
            if (v >= (tag == ST_LIN_TAG ? ST_LN : ST_U_N)) return 0;
            idx[n++] = (uint8_t)v;
        }
    }
    return n;
}
/* the address of a record's sector by its index (st_rec_sectors) */
static uint32_t st_rec_addr(const st_hdr_t *h, uint32_t v) { return st_rec_lin(h) ? st_lin_at(v) : st_pool_at(v); }
static int st_pool_shape(const st_hdr_t *h, uint32_t copy)
{
    uint8_t idx[ST_PROJECT_SECTORS];
    int lin = st_rec_lin(h);
    uint32_t n = lin ? (copy >= ST_LIN_C0 && copy < ST_LIN_C0 + ST_LN ? st_rec_sectors(h, idx) : 0u)
                     : (copy >= 2u && copy < ST_U_N + 2u ? st_rec_sectors(h, idx) : 0u), g = st_geom(h);
    return n && idx[0] + (lin ? ST_LIN_C0 : 2u) == copy && n == (h->len ? (h->len + g - 1u) / g : 1u);
}
/* payload byte off of a pool record: its flash address, *span the bytes from there up to the guard or the sector end */
static uint32_t st_pool_address(const st_hdr_t *h, uint32_t off, uint32_t *span)
{
    uint8_t idx[ST_PROJECT_SECTORS];
    uint32_t n = st_rec_sectors(h, idx), g = st_geom(h), k = off / g, b = off % g;
    if (k >= n) { *span = 0; return 0; }
    uint32_t at = st_rec_addr(h, idx[k]);
    if (b < ST_RUN0) { *span = ST_RUN0 - b; return at + ST_POOL_PAYLOAD_OFF + b; }
    *span = g - b;
    return at + ST_GUARD_OFF + ST_GUARD_LEN + (b - ST_RUN0);
}
static uint32_t st_stream_generation;
static uint32_t st_stream_open;                     /* the generation of a stream begun and not yet published (0: none) */
static uint32_t st_stream_activity;                 /* its begins and writes counted (module_store.c: still being written) */
/* New records use their pool bitmap. Legacy reads retain the five-sector base
 * plus two padded extensions, allowing migration without moving committed data. */
static uint32_t st_address_at(uint32_t obj, uint32_t copy, uint32_t off, uint32_t *span,uint32_t payload)
{
    if ((st_banked(obj) && copy >= 2u) || copy >= ST_LIN_C0) {
        st_hdr_t h;
        if (st_fetch(st_sector(obj, copy), &h, sizeof h) || !st_pool_shape(&h, copy)) { *span = 0; return 0; }
        return st_pool_address(&h, off, span);
    }
    if (!st_banked(obj) || off < ST_BANK_BASE_MAX) {
        *span = (st_banked(obj) ? ST_BANK_BASE_MAX : st_capacity(obj)) - off;
        return st_sector(obj, copy) + payload + off;
    }
    off -= ST_BANK_BASE_MAX;
    *span = ST_PAYLOAD_MAX - off % ST_PAYLOAD_MAX;
    return st_extension(obj, copy) + off / ST_PAYLOAD_MAX * ST_SECTOR + off % ST_PAYLOAD_MAX;
}
/* The commit header carries the offset, so old and new copies can coexist
 * during conversion. Never infer layout from a payload length or uncommitted data. */
static uint32_t st_address(uint32_t obj,uint32_t copy,uint32_t off,uint32_t *span)
{
    st_hdr_t h;uint32_t payload=st_payload_off(obj);
    if((st_preset_object(obj) || obj == OBJ_SETTINGS) && !st_fetch(st_sector(obj,copy),&h,sizeof h))payload=st_payload_off_header(obj,&h);
    return st_address_at(obj,copy,off,span,payload);
}
static int st_read_range(uint32_t obj, uint32_t copy, uint32_t off, void *dst, uint32_t n)
{
    if (!st_obj_ok(obj) || !st_copy_ok(obj, copy) || off > st_capacity(obj) || n > st_capacity(obj) - off) return -1;
    if (st_banked(obj) && copy < 2u && (off > ST_BANK_BASE_MAX + ST_BANK_EXT_MAX || n > ST_BANK_BASE_MAX + ST_BANK_EXT_MAX - off)) return -1;
    uint8_t *d = dst;
    while (n) {
        uint32_t span, addr = st_address(obj, copy, off, &span), k = n < span ? n : span;
        if (!k || st_fetch(addr, d, k)) return -1;
        d += k; off += k; n -= k;
    }
    return 0;
}

#include "scratch.h"
#define st_buf (main_scratch.st)
_Static_assert(sizeof st_buf == 256u, "storage retains bounded page-sized scratch");

/* the legacy single-pattern projects (OBJ_PROJECT0..+3): retired, their numbers reserved */
static int st_retired(uint32_t obj) { return obj >= OBJ_PROJECT0 && obj < OBJ_PROJECT0 + 4u; }

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (!st_obj_ok(obj) || !st_copy_ok(obj, copy) || st_retired(obj) ||
        (copy < 2u && st_pfile(obj)) || !st_sector(obj, copy))
        return -1;
    if (st_fetch(st_sector(obj, copy), h, sizeof *h))
        return -1;
    int pooled = (st_banked(obj) && copy >= 2u) || copy >= ST_LIN_C0;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > st_capacity(obj) ||
        h->hcrc != st_crc32(h, sizeof *h - 4u) ||
        (pooled ? !st_pool_shape(h, copy) : st_banked(obj) && h->len > ST_BANK_BASE_MAX + ST_BANK_EXT_MAX) ||
        (!pooled && h->rsv[0]==ST_COMPACT_TAG && (!st_preset_object(obj) || h->len>ST_SECTOR-256u-sizeof(st_hdr_t))) ||
        (st_small(obj) && copy >= 2u && copy < ST_LIN_C0 && (h->rsv[0] != ST_COMPACT_TAG || h->len > ST_SECTOR - 256u - sizeof(st_hdr_t))) ||
        (!pooled && h->rsv[0] == ST_SETTINGS_TAG && obj != OBJ_SETTINGS) ||
        (obj == OBJ_SETTINGS && h->rsv[0] != ST_SETTINGS_TAG && h->len > ST_PAYLOAD_MAX))
        return -1;
    return 0;
}

static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)   /* streaming CRC, bounded 256-byte scratch, valid: 0 */
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t off = 0; off < h->len; off += sizeof st_buf) {
        uint32_t n = h->len - off > sizeof st_buf ? sizeof st_buf : h->len - off;
        if (st_read_range(obj, copy, off, st_buf, n)) return -1;
        for (uint32_t i = 0; i < n; i++) {
            crc ^= st_buf[i];
            for (uint32_t j = 0; j < 8u; j++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc == h->crc ? 0 : -1;

}

/* the current copy: the newest valid sequence (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is read (it
 * is verified); *h gets its header. */
static int st_legacy_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u))) {
        if (st_body(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

/* Old forms (fixed pairs, extensions, the 0xA7/0xA8 lists, the bank copies) may still be in the store: st_old_scan
 * at boot says so. While they are, st_current looks at them too; afterwards only at list records */
static uint8_t st_old = 1;
/* the copy numbers a pooled object's records can have: the old forms' while st_old (a project's 2..80, the pool
 * index + 2; a preset bank's 2..24, the former preset pool's, then ST_SMALL_U0 + the pool index), then a list
 * record's ST_LIN_C0 + its first sector's linear index */
static uint32_t st_old_cands(uint32_t obj) { return !st_old ? 0u : st_small(obj) ? ST_SP_N + ST_U_N : ST_U_N; }
static uint32_t st_cands(uint32_t obj) { return st_old_cands(obj) + ST_LN; }
static uint32_t st_cand(uint32_t obj, uint32_t i)
{
    uint32_t o = st_old_cands(obj);
    if (i >= o) return ST_LIN_C0 + i - o;
    return !st_small(obj) || i < ST_SP_N ? i + 2u : ST_SMALL_U0 + i - ST_SP_N;
}
#define ST_CANDS_MAX (ST_SP_N + ST_U_N + ST_LN)
static int st_current(uint32_t obj, st_hdr_t *h)
{
    if (!st_listed(obj)) return st_legacy_current(obj, h);
    /* Retry older headers only after a failed payload CRC. Rejecting one
     * candidate in RAM avoids an erase merely to read storage. */
    uint32_t rejected[(ST_CANDS_MAX + 31u) / 32u] = {0}, n = st_cands(obj);
    for (;;) {
        st_hdr_t best = {0}; int current = -1; uint32_t at = 0;
        for (uint32_t i = 0; i < n; i++) {
            st_hdr_t candidate;
            if ((rejected[i / 32u] >> (i % 32u)) & 1u || st_head(obj, st_cand(obj, i), &candidate)) continue;
            if (current < 0 || (candidate.seq != best.seq && candidate.seq - best.seq < 0x80000000u)) { best = candidate; current = (int)st_cand(obj, i); at = i; }
        }
        if (current < 0) return -1;
        if (!st_body(obj, (uint32_t)current, &best)) { *h = best; return current; }
        rejected[at / 32u] |= 1u << (at % 32u);
    }
}
static void st_mark(uint32_t used[ST_LW], uint32_t addr)   /* the linear sector holding addr: in use */
{
    uint32_t i = st_lin_of(addr);
    if (i < ST_LN) used[i / 32u] |= 1u << (i % 32u);
}
/* Protect precisely the current payload of each record, in whatever form, including unreadable musical formats with
 * valid storage CRCs. Old duplicates and abandoned stages are free. */
static void st_project_used(uint32_t used[ST_LW], uint32_t obj, uint32_t copy, const st_hdr_t *h)
{
    if (copy >= ST_LIN_C0 || (st_banked(obj) && copy >= 2u)) {     /* a list record (or an old pool list) */
        uint8_t idx[ST_PROJECT_SECTORS];
        for (uint32_t k = 0, n = st_rec_sectors(h, idx); k < n; k++) st_mark(used, st_rec_addr(h, idx[k]));
        return;
    }
    if (!st_banked(obj)) { st_mark(used, st_sector(obj, copy)); return; }   /* a bank's one sector, old forms */
    uint32_t base = st_sector(obj, copy), ext = st_extension(obj, copy);   /* a legacy A/B project */
    uint32_t bytes = h->len < ST_BANK_BASE_MAX ? h->len : ST_BANK_BASE_MAX;
    for (uint32_t a = base; a < base + ST_PAYLOAD_OFF + bytes; a += ST_SECTOR) st_mark(used, a);
    if (h->len > ST_BANK_BASE_MAX)
        for (uint32_t k = 0, n = (h->len - ST_BANK_BASE_MAX + ST_PAYLOAD_MAX - 1u) / ST_PAYLOAD_MAX; k < n; k++)
            st_mark(used, ext + k * ST_SECTOR);
}
/* every current record (projects, user kits, preset banks) into used, linear sectors; *seq: obj's next sequence (1
 * when it has none). Nonzero: a read failed, which is never evidence of free space */
static int st_pool_used(uint32_t used[ST_LW], uint32_t obj, uint32_t *seq)
{
    uint8_t seen[(ST_OBJ_END + 7u) / 8u];
    uint32_t skip = !st_old;                        /* (list records only: a pool object no sector's header names has none) */
    st_read_failed = 0;
    *seq = 1u;
    for (uint32_t w = 0; w < ST_LW; w++) used[w] = 0;
    memset(seen, 0, sizeof seen);
    for (uint32_t i = 0; skip && i < ST_LN; i++) {  /* every sector's header read once: the objects there are */
        struct { uint32_t magic; uint16_t type, slot; } h;
        uint32_t a = st_lin_at(i);
        if (!a || st_fetch(a, &h, sizeof h)) { skip = 0; break; }
        if (h.magic == ST_MAGIC && h.type < ST_OBJ_END) seen[h.type / 8u] |= (uint8_t)(1u << (h.type % 8u));
    }
    for (uint32_t o = 0; o < ST_OBJ_END; o++) {
        uint32_t live = o < OBJ_COUNT ? st_listed(o) && !st_retired(o) : st_pfile(o);
#if !MELODEE_RECORDS
        live &= o < ST_RFILE0 || o >= ST_MFILE0;
#endif
        if (!live || (skip && o >= ST_PFILE0 && !(seen[o / 8u] >> (o % 8u) & 1u))) continue;
        st_hdr_t current; int copy = st_current(o, &current);
        if (copy < 0) continue;
        st_project_used(used, o, (uint32_t)copy, &current);
        if (o == obj) *seq = current.seq + 1u;
    }
    return st_read_failed;
}
/* the m-th linear sector a new record of obj tries. FM-1: from the top down for every record, so the low end next to
 * the app area stays free longest. SMK-37: projects and kits from store A up, banks from the top (store C), so the
 * stock mapping in USRFLASH (store B) lasts as long as it can */
static uint32_t st_order(uint32_t obj, uint32_t m)
{
#if BOARD_SMK37
    if (m >= ST_U_N) return m;                          /* (the resource sectors given to the pool: last) */
    if (!st_small(obj)) return m;
    return ST_U_N - 1u - m;
#else
    /* top down, the resource sectors given to the pool last: indices hi .. ST_LN-1, then 0 .. lo-1, then lo .. hi-1 */
    const uint32_t lo = (FL_RES_LO + FL_RES_RESERVE * ST_SECTOR - ST_LIN_BASE) / ST_SECTOR, hi = lo + ST_RES_GIVEN;
    (void)obj;
    if (m < ST_LN - hi) return ST_LN - 1u - m;
    m -= ST_LN - hi;
    return m < lo ? lo - 1u - m : hi - 1u - (m - lo);
#endif
}
/* a linear sector a new record may take: allowed and held by no current record */
static int st_pool_free_at(const uint32_t used[ST_LW], uint32_t i) { return st_lin_ok(i) && !((used[i / 32u] >> (i % 32u)) & 1u); }
/* the pool's free sectors (-1: a read failed) */
static int st_project_free(void)
{
    uint32_t used[ST_LW], seq, n = 0;
    if (st_pool_used(used, 0xFFFFu, &seq)) return -1;
    for (uint32_t i = 0; i < ST_LN; i++) n += (uint32_t)st_pool_free_at(used, i);
    return (int)n;
}
/* a record emptied: every committed copy's header erased, the oldest first and the current one last, so a cut
 * leaves it as it was or nothing (an older copy never comes back). 0, or the first error */
static int st_project_delete(uint32_t obj)
{
    uint32_t n = st_cands(obj) + 2u;
    st_pool_gen++;
    for (uint32_t pass = 0; pass < n; pass++) {
        st_hdr_t h, best; uint32_t at = 0; int found = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t c = i < 2u ? i : st_cand(obj, i - 2u);
            if ((i < 2u && (st_pfile(obj) || !st_old)) || st_head(obj, c, &h)) continue;
            if (!found || (best.seq != h.seq && best.seq - h.seq < 0x80000000u)) { best = h; at = c; found = 1; }
        }
        if (!found) return 0;
        int rc = st_erase(st_sector(obj, at));
        if (rc) return rc;
    }
    return -7;
}
/* a new list record's sectors (in st_order), erased, and its address map staged */
static int st_project_allocate(uint32_t obj, uint32_t len, st_hdr_t *h)
{
    uint32_t used[ST_LW];
    memset(h, 0xFF, sizeof *h);
    h->type = (uint16_t)obj; h->len = len; h->seq = 1u;
    if (st_pool_used(used, obj, &h->seq)) return -7;
    st_list_init(h);
    uint32_t need = len ? (len + ST_LIN_PAYLOAD_MAX - 1u) / ST_LIN_PAYLOAD_MAX : 1u, k = 0;
    if (need > ST_PROJECT_SECTORS) return -1;
    for (uint32_t m = 0; m < ST_LN && k < need; m++) if (st_pool_free_at(used, st_order(obj, m))) st_list_put(h, k++, st_order(obj, m));
    if (k < need) return -5;
    h->slot = (uint16_t)(ST_LIN_C0 + st_list_get(h, 0));
    for (uint32_t m = 0; m < need; m++) {
        int rc = st_erase(st_lin_at(st_list_get(h, m))); if (rc) return rc;
    }
    /* Persist the address map for staged range reads. The erased magic and
     * header CRC keep it invisible until the final one-page commit. */
    return st_prog(st_sector(obj, h->slot), h, sizeof *h);
}
static int st_project_save(uint32_t obj, const void *src, uint32_t len);

/* load the whole object into dst; returns its length, or -1 if it does not fit */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    st_hdr_t h;
    int cur;
    if (!st_obj_ok(obj) || (cur = st_current(obj, &h)) < 0 || h.len > max) return -1;
    if (st_read_range(obj, (uint32_t)cur, 0, dst, h.len)) return -1;
    return (int)h.len;
}

/* a committed record to read in pieces (st_read_range) without a buffer for all of it: its copy, *len its bytes; -1
 * none */
static int st_open(uint32_t obj, uint32_t *len)
{
    st_hdr_t h;
    int cur = st_obj_ok(obj) ? st_current(obj, &h) : -1;
    *len = cur >= 0 ? h.len : 0u;
    return cur;
}
/* a committed record is there, whatever it holds (st_load: -1 for one too big for its buffer too) */
static int st_exists(uint32_t obj)
{
    st_hdr_t h;
    return st_obj_ok(obj) && !st_retired(obj) && st_current(obj, &h) >= 0 && h.len != 0;
}

static int st_pair_save(uint32_t obj, const void *src, uint32_t len);
static int st_save(uint32_t obj, const void *src, uint32_t len)
{
#if defined(MELODEE_AUDIO_PROFILE) && MELODEE_AUDIO_PROFILE
    if(AUDIO_LAB_GUARDED())return -9;
#endif
    if (!st_obj_ok(obj) || st_retired(obj) || len > st_capacity(obj))
        return -1;
    if (st_listed(obj)) return st_project_save(obj, src, len);
    return st_pair_save(obj, src, len);
}
/* the fixed A/B pair: settings (and a preset bank's old home, where host tests build pre-pool fixtures) */
static int st_pair_save(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t seq, base, off;
    int cur, rc;
    st_hdr_t h;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    base = st_sector(obj, cur == 0 ? 1u : 0u);       /* write the other copy */
    uint32_t payload=st_payload_off(obj),compact=0;
    if(st_preset_object(obj) && len==3716u){uint32_t magic;memcpy(&magic,src,sizeof magic);compact=magic==0x32425055u;}
    if(compact || (obj == OBJ_SETTINGS && len > ST_PAYLOAD_MAX))payload=sizeof(st_hdr_t);
    uint32_t crc = st_crc32(src, len);
    if ((rc = st_erase(base)) != 0) return rc;
    for (off = 0; off < len;) {
        uint32_t span, addr = st_address_at(obj, cur == 0 ? 1u : 0u, off, &span,payload);
        uint32_t n = len - off, page = 256u - (addr & 255u);
        if (n > sizeof st_buf) n = sizeof st_buf;
        if (n > page) n = page;
        if (n > span) n = span;
        memcpy(st_buf, (const uint8_t *)src + off, n);
        if ((rc = st_prog(addr, st_buf, n)) != 0) return rc;
        off += n;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)(cur == 0 ? 1 : 0);
    h.seq = seq + 1u;
    h.len = len;
    h.crc = crc;
    h.rsv[0]=compact?ST_COMPACT_TAG:obj == OBJ_SETTINGS && payload == sizeof(st_hdr_t) ? ST_SETTINGS_TAG : 0xFFFFFFFFu;h.rsv[1]=0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        uint32_t c = cur == 0 ? 1u : 0u;
        if (st_head(obj, c, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, c, &chk))
            return -7;
    }
    return 0;
}

#include "storage_stream.c"

/* ---- the bridge converter ---- */
/* sector a holds a header the old readers find (a record of an old form, current or not): 1, its object and copy */
static int st_old_head_at(uint32_t a, uint32_t *obj, uint32_t *copy)
{
    st_hdr_t h, c;
    if (st_fetch(a, &h, sizeof h) || h.magic != ST_MAGIC || h.slot >= ST_LIN_C0 || h.type == OBJ_SETTINGS ||
        !st_obj_ok(h.type) || !st_copy_ok(h.type, h.slot) || st_sector(h.type, h.slot) != a || st_head(h.type, h.slot, &c))
        return 0;
    *obj = h.type; *copy = h.slot;
    return 1;
}
/* st_old from the flash: every old form's first sector lies in the linear space (the FM-1's data flash; the
 * SMK-37's pool windows), so one header per sector tells. A failed read counts as old */
static void st_old_scan(void)
{
    uint32_t o, c;
    st_old = 1;                                       /* (st_head while scanning sees every form) */
    st_read_failed = 0;
    int found = 0;
    for (uint32_t i = 0; i < ST_LN && !found; i++) found = st_old_head_at(st_lin_at(i), &o, &c);
    st_old = (uint8_t)(found || st_read_failed);
}
/* obj's current record, if it is in an old form, re-saved as a list record through buf (256 bytes a piece); the old
 * one stays current until the new one commits */
static int st_convert_one(uint32_t obj, void *buf)
{
    st_hdr_t h;
    int copy = st_current(obj, &h);
    if (copy < 0 || (uint32_t)copy >= ST_LIN_C0) return 0;
    st_stream_t w;
    int rc = st_stream_begin(&w, obj, h.len);
    for (uint32_t off = 0; !rc && off < h.len; off += 256u) {
        uint32_t n = h.len - off < 256u ? h.len - off : 256u;
        rc = st_read_range(obj, (uint32_t)copy, off, buf, n) ? -7 : st_stream_write(&w, buf, n);
    }
    return rc ? rc : st_stream_publish(&w);
}
/* The bridge converter (boot, before anything reads a record; res.c begin): every record still in an old form
 * re-saved as a list record, the smallest first (the denser form never takes more sectors than it frees, so later,
 * bigger ones find room), each committed on its own (a cut resumes at the next boot); then every header the old
 * readers would find erased, unless it is still its object's current one (a record that did not fit stays as it
 * was). step(done, total), if any, shows the work. Returns the records left in an old form */
#define ST_CONV_MAX 64u
static int st_convert(void *buf, void (*step)(uint32_t, uint32_t))
{
    uint16_t co[ST_CONV_MAX], cl[ST_CONV_MAX];
    uint32_t n = 0, left = 0;
    st_old = 1;
    for (uint32_t o = 0; o < ST_OBJ_END; o++) {
        uint32_t live = o < OBJ_COUNT ? (uint32_t)st_listed(o) : (uint32_t)st_pfile(o);
        if (!live) continue;
        st_hdr_t h;
        int c = st_current(o, &h);
        if (c >= 0 && (uint32_t)c < ST_LIN_C0 && n < ST_CONV_MAX) { co[n] = (uint16_t)o; cl[n] = (uint16_t)h.len; n++; }
    }
    if (n && step) step(0, n);
    for (uint32_t d = 0; d < n; d++) {
        uint32_t m = d;
        for (uint32_t k = d + 1u; k < n; k++) if (cl[k] < cl[m]) m = k;
        uint16_t t = co[d]; co[d] = co[m]; co[m] = t; t = cl[d]; cl[d] = cl[m]; cl[m] = t;
        if (st_convert_one(co[d], buf)) left++;
        if (step) step(d + 1u, n);
        ST_IDLE();
    }
    for (uint32_t i = 0; i < ST_LN; i++) {
        uint32_t a = st_lin_at(i), o, c;
        if (!st_old_head_at(a, &o, &c)) continue;
        st_hdr_t h;
        int cur = st_current(o, &h);
        if (cur >= 0 && (uint32_t)cur == c) continue;      /* still the current one: it did not fit */
        (void)st_erase(a);
        ST_IDLE();
    }
    st_old_scan();
    return (int)left;
}
/* a current record has a sector in [lo, hi): 1 (-1: a read failed) */
static int st_held(uint32_t lo, uint32_t hi)
{
    uint32_t used[ST_LW], seq;
    if (st_pool_used(used, 0xFFFFu, &seq)) return -1;
    for (uint32_t i = 0; i < ST_LN; i++) {
        uint32_t a = st_lin_at(i);
        if (a >= lo && a < hi && ((used[i / 32u] >> (i % 32u)) & 1u)) return 1;
    }
    return 0;
}
