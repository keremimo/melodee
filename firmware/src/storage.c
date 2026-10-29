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
#define ST_PROJECT_MAX (ST_PROJECT_SECTORS * ST_POOL_PAYLOAD_MAX)
#define ST_POOL_SECTORS 56u                    /* the former project pool's sectors: shared indices 0..55 */
#define ST_SP_N 23u                            /* the former preset pool's: 56..78 (ST_SP_AT) */
#define ST_U_N (ST_POOL_SECTORS + ST_SP_N)     /* one pool for projects and preset banks */
#define ST_UW ((ST_U_N + 31u) / 32u)           /* words of a pool bitmap */
#define ST_POOL_TAG 0xA7000000u                /* a project record's sectors: a bitmap of indices 0..55 (read only) */
#define ST_LIST_TAG 0xA8000000u                /* .. a list: eight 7-bit indices, 0x7F after the last */
#define ST_SMALL_U0 100u                       /* a preset bank's copy in the pool: ST_SMALL_U0 + index */
_Static_assert(ST_PROJECT_SECTORS * 5u <= ST_U_N && ST_U_N < 0x7Fu && ST_SMALL_U0 > ST_SP_N + 1u, "pool numbering");

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
static uint32_t st_proj_obj(uint32_t slot) { return slot < 4u ? OBJ_BANK0 + slot : ST_PFILE0 + slot; }
static int st_pfile(uint32_t obj) { return obj >= ST_PFILE0 + 4u && obj < ST_PFILE0 + ST_NPROJ; }
static int st_obj_ok(uint32_t obj) { return obj < OBJ_COUNT || st_pfile(obj); }

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;
_Static_assert(sizeof(st_hdr_t) == 32u, "storage commit record layout");

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

/* Allocation must not mistake a failed read for free project space. */
static uint8_t st_read_failed;
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
static int st_preset_object(uint32_t obj);
static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* legacy A/B (0/1), or project pool index + 2 */
{
    if (((obj >= OBJ_BANK0 && obj < OBJ_CZBANK0) || st_pfile(obj)) && copy >= 2u) {
        return st_pool_at(copy - 2u);
    }
    if (copy >= ST_SMALL_U0 && st_preset_object(obj)) return st_pool_at(copy - ST_SMALL_U0);
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
}

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
static int st_copy_ok(uint32_t obj, uint32_t copy)    /* 0/1 the fixed pair; 2.. a pool sector */
{
    return copy < 2u || (st_banked(obj) && copy < ST_U_N + 2u) ||
           (st_small(obj) && (copy < ST_SP_N + 2u || (copy >= ST_SMALL_U0 && copy < ST_SMALL_U0 + ST_U_N)));
}
static uint32_t st_payload_off_header(uint32_t obj,const st_hdr_t *h)
{
    if (st_banked(obj) && h->slot >= 2u) return ST_POOL_PAYLOAD_OFF;
    return (st_preset_object(obj) && h->rsv[0]==ST_COMPACT_TAG) || (obj == OBJ_SETTINGS && h->rsv[0] == ST_SETTINGS_TAG) ? sizeof(st_hdr_t) : st_payload_off(obj);
}
static uint32_t st_extension(uint32_t obj, uint32_t copy)
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
static void st_list_init(st_hdr_t *h) { h->rsv[0] = 0xFFFFFFFFu; h->rsv[1] = ST_LIST_TAG | 0x00FFFFFFu; }
/* a pool record's sectors in order (pool indices); their count, 0 when it has no pool shape */
static uint32_t st_rec_sectors(const st_hdr_t *h, uint8_t idx[ST_PROJECT_SECTORS])
{
    uint32_t tag = h->rsv[1] & 0xFF000000u, n = 0;
    if (tag == ST_POOL_TAG) {
        for (uint32_t i = 0; i < ST_POOL_SECTORS; i++) if (st_pool_bit(h, i)) { if (n == ST_PROJECT_SECTORS) return 0; idx[n++] = (uint8_t)i; }
    } else if (tag == ST_LIST_TAG) {
        for (uint32_t k = 0; k < ST_PROJECT_SECTORS; k++) {
            uint32_t v = st_list_get(h, k);
            if (v == 0x7Fu) break;
            if (v >= ST_U_N) return 0;
            idx[n++] = (uint8_t)v;
        }
    }
    return n;
}
static int st_pool_shape(const st_hdr_t *h, uint32_t copy)
{
    uint8_t idx[ST_PROJECT_SECTORS];
    uint32_t n = copy >= 2u && copy < ST_U_N + 2u ? st_rec_sectors(h, idx) : 0u;
    return n && idx[0] + 2u == copy && n == (h->len ? (h->len + ST_POOL_PAYLOAD_MAX - 1u) / ST_POOL_PAYLOAD_MAX : 1u);
}
static uint32_t st_pool_address(const st_hdr_t *h, uint32_t off, uint32_t *span)
{
    uint8_t idx[ST_PROJECT_SECTORS];
    uint32_t n = st_rec_sectors(h, idx), k = off / ST_POOL_PAYLOAD_MAX;
    if (k >= n) { *span = 0; return 0; }
    *span = ST_POOL_PAYLOAD_MAX - off % ST_POOL_PAYLOAD_MAX;
    return st_pool_at(idx[k]) + ST_POOL_PAYLOAD_OFF + off % ST_POOL_PAYLOAD_MAX;
}
static uint32_t st_stream_generation;
/* New records use their pool bitmap. Legacy reads retain the five-sector base
 * plus two padded extensions, allowing migration without moving committed data. */
static uint32_t st_address_at(uint32_t obj, uint32_t copy, uint32_t off, uint32_t *span,uint32_t payload)
{
    if (st_banked(obj) && copy >= 2u) {
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
        (copy < 2u && st_pfile(obj)))
        return -1;
    if (st_fetch(st_sector(obj, copy), h, sizeof *h))
        return -1;
    int pooled = st_banked(obj) && copy >= 2u;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > st_capacity(obj) ||
        h->hcrc != st_crc32(h, sizeof *h - 4u) ||
        (st_banked(obj) && (copy >= 2u ? !st_pool_shape(h, copy) : h->len > ST_BANK_BASE_MAX + ST_BANK_EXT_MAX)) ||
        (!pooled && h->rsv[0]==ST_COMPACT_TAG && (!st_preset_object(obj) || h->len>ST_SECTOR-256u-sizeof(st_hdr_t))) ||
        (st_small(obj) && copy >= 2u && (h->rsv[0] != ST_COMPACT_TAG || h->len > ST_SECTOR - 256u - sizeof(st_hdr_t))) ||
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

/* the copy numbers a pooled object's records can have: a project's 2..80 (the pool index + 2), a preset bank's
 * 2..24 (the former preset pool's) then ST_SMALL_U0 + the pool index */
static uint32_t st_cands(uint32_t obj) { return st_small(obj) ? ST_SP_N + ST_U_N : ST_U_N; }
static uint32_t st_cand(uint32_t obj, uint32_t i)
{
    return !st_small(obj) || i < ST_SP_N ? i + 2u : ST_SMALL_U0 + i - ST_SP_N;
}
static int st_current(uint32_t obj, st_hdr_t *h)
{
    if (!st_banked(obj) && !st_small(obj)) return st_legacy_current(obj, h);
    /* Retry older headers only after a failed payload CRC. Rejecting one
     * candidate in RAM avoids an erase merely to read storage. */
    uint32_t rejected[4] = {0, 0, 0, 0}, n = st_cands(obj);
    st_hdr_t legacy; int old = st_pfile(obj) ? -1 : st_legacy_current(obj, &legacy);
    for (;;) {
        st_hdr_t best = {0}; int current = -1; uint32_t at = 0;
        for (uint32_t i = 0; i < n; i++) {
            st_hdr_t candidate;
            if ((rejected[i / 32u] >> (i % 32u)) & 1u || st_head(obj, st_cand(obj, i), &candidate)) continue;
            if (current < 0 || (candidate.seq != best.seq && candidate.seq - best.seq < 0x80000000u)) { best = candidate; current = (int)st_cand(obj, i); at = i; }
        }
        if (old >= 0 && (current < 0 || best.seq == legacy.seq || legacy.seq - best.seq < 0x80000000u)) { *h = legacy; return old; }
        if (current < 0) return -1;
        if (!st_body(obj, (uint32_t)current, &best)) { *h = best; return current; }
        rejected[at / 32u] |= 1u << (at % 32u);
    }
}
/* the pool index holding a preset bank's copy (-1: a fixed pair outside the pool, in the resource region) */
static int st_small_index(uint32_t obj, uint32_t copy)
{
    if (copy >= ST_SMALL_U0) return (int)(copy - ST_SMALL_U0);
    if (copy >= 2u) return (int)(ST_POOL_SECTORS + copy - 2u);
    uint32_t at = st_sector(obj, copy);
    for (uint32_t i = 0; i < ST_U_N; i++) if (st_pool_at(i) == at) return (int)i;
    return -1;
}
/* Protect precisely the current payload of each slot, including unreadable musical
 * formats with valid storage CRCs. Old duplicates and abandoned stages are free. */
static void st_project_used(uint32_t used[ST_UW], uint32_t obj, uint32_t copy, const st_hdr_t *h)
{
    if (copy >= 2u) {
        uint8_t idx[ST_PROJECT_SECTORS];
        for (uint32_t k = 0, n = st_rec_sectors(h, idx); k < n; k++) used[idx[k] / 32u] |= 1u << (idx[k] % 32u);
        return;
    }
    for (uint32_t i = 0; i < ST_POOL_SECTORS; i++) {
        uint32_t addr = st_pool_at(i), base = st_sector(obj, copy), ext = st_extension(obj, copy);
        uint32_t bytes = h->len < ST_BANK_BASE_MAX ? h->len : ST_BANK_BASE_MAX;
        int live = addr >= base && addr < base + ST_PAYLOAD_OFF + bytes;
        if (h->len > ST_BANK_BASE_MAX) live |= addr >= ext && addr < ext + ((h->len - ST_BANK_BASE_MAX + ST_PAYLOAD_MAX - 1u) / ST_PAYLOAD_MAX) * ST_SECTOR;
        if (live) used[i / 32u] |= 1u << (i % 32u);
    }
}
/* every current record in the pool, projects' and preset banks', into used; *seq: obj's next sequence (1 when it
 * has none); *outside: preset banks whose current record is outside the pool. Nonzero: a read failed, which is
 * never evidence of free space */
static int st_pool_used(uint32_t used[ST_UW], uint32_t obj, uint32_t *seq, uint32_t *outside)
{
    st_read_failed = 0;
    *seq = 1u;
    if (outside) *outside = 0;
    for (uint32_t w = 0; w < ST_UW; w++) used[w] = 0;
    for (uint32_t s = 0; s < ST_NPROJ; s++) {
        uint32_t o = st_proj_obj(s);
        st_hdr_t current; int copy = st_current(o, &current);
        if (copy >= 0) {
            st_project_used(used, o, (uint32_t)copy, &current);
            if (o == obj) *seq = current.seq + 1u;
        }
    }
    for (uint32_t o = 0; o < OBJ_COUNT; o++) {
        if (!st_small(o)) continue;
        st_hdr_t current; int copy = st_current(o, &current);
        if (copy < 0) continue;
        int i = st_small_index(o, (uint32_t)copy);
        if (i >= 0) used[(uint32_t)i / 32u] |= 1u << ((uint32_t)i % 32u);
        else if (outside) (*outside)++;
        if (o == obj) *seq = current.seq + 1u;
    }
    return st_read_failed;
}
static int st_pool_free_at(const uint32_t used[ST_UW], uint32_t i) { return !((used[i / 32u] >> (i % 32u)) & 1u); }
/* the pool's free sectors (-1: a read failed) */
static int st_project_free(void)
{
    uint32_t used[ST_UW], seq, n = 0;
    if (st_pool_used(used, 0xFFFFu, &seq, 0)) return -1;
    for (uint32_t i = 0; i < ST_U_N; i++) n += (uint32_t)st_pool_free_at(used, i);
    return (int)n;
}
/* a project slot emptied: every committed copy's header erased, the oldest first and the current one last, so a
 * cut leaves the project as it was or nothing (an older copy never comes back). 0, or the first error */
static int st_project_delete(uint32_t obj)
{
    for (uint32_t pass = 0; pass < ST_U_N + 2u; pass++) {
        st_hdr_t h, best; uint32_t at = 0; int found = 0;
        for (uint32_t c = st_pfile(obj) ? 2u : 0u; c < ST_U_N + 2u; c++) {
            if (st_head(obj, c, &h)) continue;
            if (!found || (best.seq != h.seq && best.seq - h.seq < 0x80000000u)) { best = h; at = c; found = 1; }
        }
        if (!found) return 0;
        int rc = st_erase(st_sector(obj, at));
        if (rc) return rc;
    }
    return -7;
}
static int st_project_allocate(uint32_t obj, uint32_t len, st_hdr_t *h)
{
    uint32_t used[ST_UW];
    memset(h, 0xFF, sizeof *h);
    h->type = (uint16_t)obj; h->len = len; h->seq = 1u;
    if (st_pool_used(used, obj, &h->seq, 0)) return -7;
    st_list_init(h);
    uint32_t need = len ? (len + ST_POOL_PAYLOAD_MAX - 1u) / ST_POOL_PAYLOAD_MAX : 1u, k = 0;
    if (need > ST_PROJECT_SECTORS) return -1;
    for (uint32_t i = 0; i < ST_U_N && k < need; i++) if (st_pool_free_at(used, i)) st_list_put(h, k++, i);
    if (k < need) return -5;
    h->slot = (uint16_t)(st_list_get(h, 0) + 2u);
    for (uint32_t m = 0; m < need; m++) {
        int rc = st_erase(st_pool_at(st_list_get(h, m))); if (rc) return rc;
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

/* a committed record is there, whatever it holds (st_load: -1 for one too big for its buffer too) */
static int st_exists(uint32_t obj)
{
    st_hdr_t h;
    return st_obj_ok(obj) && !st_retired(obj) && st_current(obj, &h) >= 0 && h.len != 0;
}

/* a preset bank into a free pool sector: erase, payload at 32, the commit record last, read back */
static int st_small_save(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t used[ST_UW], seq, i, at;
    if (len > ST_SECTOR - 256u - sizeof(st_hdr_t)) return -1;
    if (st_pool_used(used, obj, &seq, 0)) return -7;
    for (i = ST_U_N; i-- && !st_pool_free_at(used, i);)   /* from the top: banks gather where they were, projects */
        ;                                                 /* grow from the bottom */
    if (i >= ST_U_N) return -5;                          /* STORAGE FULL */
    at = st_pool_at(i);
    int rc = st_erase(at);
    if (rc) return rc;
    for (uint32_t off = 0; off < len;) {
        uint32_t n = len - off > sizeof st_buf ? sizeof st_buf : len - off, page = 256u - ((at + 32u + off) & 255u);
        if (n > page) n = page;
        memcpy(st_buf, (const uint8_t *)src + off, n);
        if ((rc = st_prog(at + 32u + off, st_buf, n)) != 0) return rc;
        off += n;
    }
    st_hdr_t h;
    h.magic = ST_MAGIC; h.type = (uint16_t)obj; h.slot = (uint16_t)(ST_SMALL_U0 + i); h.seq = seq; h.len = len;
    h.crc = st_crc32(src, len); h.rsv[0] = ST_COMPACT_TAG; h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(at, &h, sizeof h)) != 0) return rc;
    st_hdr_t chk;
    if (st_head(obj, ST_SMALL_U0 + i, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, ST_SMALL_U0 + i, &chk)) return -7;
    return 0;
}
/* Banks still in their old fixed pairs outside the pool (where the resource region now goes) copied into it, as
 * saves are (the old copy current until the new one commits). Boot and res.c, before anything writes there.
 * 0 when every bank's current record is in the pool */
static int st_small_migrate(void *buf, uint32_t max)
{
    int err = 0;
    for (uint32_t o = 0; o < OBJ_COUNT; o++) {
        if (!st_small(o)) continue;
        st_hdr_t h; int copy = st_current(o, &h);
        if (copy < 0 || st_small_index(o, (uint32_t)copy) >= 0) continue;
        int rc = h.len > max || st_read_range(o, (uint32_t)copy, 0, buf, h.len) ? -7 : st_small_save(o, buf, h.len);
        if (rc) err = rc;                    /* (a 3,816-byte pre-UPB2 bank stays put until up_boot converts it) */
    }
    return err;
}
static int st_pair_save(uint32_t obj, const void *src, uint32_t len);
static int st_save(uint32_t obj, const void *src, uint32_t len)
{
#if defined(MELODEE_AUDIO_PROFILE) && MELODEE_AUDIO_PROFILE
    if(AUDIO_LAB_GUARDED())return -9;
#endif
    if (!st_obj_ok(obj) || st_retired(obj) || len > st_capacity(obj))
        return -1;
    if (st_banked(obj)) return st_project_save(obj, src, len);
    if (st_small(obj)) return st_small_save(obj, src, len);
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
