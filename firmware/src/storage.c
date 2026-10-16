/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B pair (five base plus two extension sectors per bank project; one otherwise). A save goes to the copy that is not
 * the current one: erase the sector, program the payload pages (from offset
 * 256), then the 32-byte header at offset 0 LAST. The header is the commit
 * record (magic, seq, length, payload CRC, header CRC); on load the valid
 * copy with the newest seq wins (including wrap), so a write torn at any point leaves the
 * previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_COMPACT_TAG 0x32504D43u             /* CMP2: preset payload begins at byte 32 */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)
#define ST_BANK_BASE_MAX (5u * ST_SECTOR - ST_PAYLOAD_OFF)
#define ST_BANK_EXT_LO 0xEA000u
#define ST_BANK_EXT_MAX (2u * ST_PAYLOAD_MAX)

/* The Prophet user banks: 0x93000..0x9CFFF. The old banks at 0x89000 are retired and never migrated.
 * The current banks occupy the legacy single-pattern projects' place: those objects are retired (2026-10-10, Kerem: not
 * wanted), their numbers kept, nothing loads or saves them; 0x9D000..0x9EFFF is the final shared preset pair.
 * Bank projects occupy 0xA0000..0xC7FFF, two five-sector copies per slot.
 * Expanded project copies each use two more sectors at 0xEA000..0xF9FFF (unused USR area).
 * User samples are disabled: this area must never accept sample uploads.
 * Flash map: app area 0x4000..0x92FFF; Prophet banks 0x93000..0x9CFFF; shared extension 0x9D000..0x9EFFF; FM6 patch bank
 * copy A 0x9F000 (B 0xFE000); bank projects 0xA0000..0xC7FFF; CZ banks 0xC8000..0xD7FFF; native FM6 0xD8000..;
 * user preset banks 0xDC000..0xDFFFF (upreset.c); OTA staging 0xE0000..0xE4FFF; extensions 0xE5000..0xFAFFF;
 * settings 0xFC000 (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..) */
/* Object numbers 0..19 stay fixed. U33..U64 use E5000..E8FFF;
 * owned FM6 U01..U32 reuse the old bank pair, U33..U64 use E9000/FA000.
 * E0000..E4FFF remains reserved for OTA loader staging. */
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_FM6BANK = OBJ_UPRESET0 + 2, OBJ_BANK0, OBJ_CZBANK0 = OBJ_BANK0 + 4, OBJ_UPRESET_EXT0 = OBJ_CZBANK0 + 8, OBJ_UPFM6_EXT = OBJ_UPRESET_EXT0 + 2, OBJ_NATIVEFM0, OBJ_P5BANK0 = OBJ_NATIVEFM0 + 2, OBJ_SHARED_EXT = OBJ_P5BANK0 + 5, OBJ_COUNT };

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

static uint32_t st_crc32(const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

#if BOARD_SMK37
/* SMK-37. The SMK has less free flash than Melodee uses on the FM-1 (432 KiB of objects): its stock region table
 * keeps BTIF / USRTRIM 0xC0000..0xC1FFF (key calibration), USR 0xF4000..0xFDFFF and key_mac 0xFF000 and a larger
 * app area (PRCT 0x9D000). Every object keeps its number, size and A/B scheme; the SMK holds (fm1_flash_map.h)
 *   store A, FL_DATA 0x9D000..0xBAFFF (the stock VM), 4 KiB A/B neighbours:
 *     0x9D000 settings, 0x9F000 user preset banks 1-2, 0xA3000 FM6 bank (native FM 3),
 *     0xA5000 user preset banks 3-4, 0xA9000 FM6 bank ext (native FM 4), 0xAB000 native FM 1-2,
 *     0xAF000 the store owner tag (st_owner_check), 0xB0000 Prophet user banks 1-5, 0xBA000 spare
 *   store B, FL_UFL 0xC2000..0xE9FFF (USRFLASH below its end marker; Melodee has no user samples):
 *     0xC2000 project banks A, B: two five-sector copies each; 0xD6000 their two-sector extensions;
 *     0xDE000 CZ banks 1-6
 *   0xEA000 the stock end marker's sector: never data (FL_STOCKTAG, only erased as the release below)
 *   store C, FL_GAP 0xEB000..0xF3FFF (unlisted, below USR):
 *     0xEB000 CZ banks 7-8, 0xEF000 the shared preset pair, 0xF1000..0xF3FFF spare
 * Not on the SMK: project slots C and D (14 sectors each; ST_SMK_PROJECTS) and the legacy single-pattern
 * projects (retired everywhere). Their objects read as empty and refuse a save. */
#define ST_SMK_PROJECTS 2u                         /* project slots with flash on the SMK: A, B */
#define ST_SMK_HAS(obj) (!((obj) >= OBJ_PROJECT0 && (obj) < OBJ_PROJECT0 + 4u) && \
                         !((obj) >= OBJ_BANK0 + ST_SMK_PROJECTS && (obj) < OBJ_BANK0 + 4u))
#define ST_SMK_TAG 0xAF000u
#define ST_SMK_P5_LO 0xB0000u
#define ST_SMK_BANK_LO FL_UFL_LO
#define ST_SMK_EXT_LO (ST_SMK_BANK_LO + ST_SMK_PROJECTS * 10u * ST_SECTOR)
#define ST_SMK_CZ_LO (ST_SMK_EXT_LO + ST_SMK_PROJECTS * 4u * ST_SECTOR)
#define ST_SMK_CZ_IN_UFL 6u                        /* CZ banks in store B; the rest in store C */
#define ST_SMK_CZ2_LO FL_GAP_LO
#define ST_SMK_SHARED_LO (ST_SMK_CZ2_LO + (8u - ST_SMK_CZ_IN_UFL) * 2u * ST_SECTOR)
_Static_assert(ST_SMK_CZ_LO + ST_SMK_CZ_IN_UFL * 2u * ST_SECTOR == FL_UFL_HI, "SMK store B: CZ banks 1-6 end at FL_UFL_HI");
_Static_assert(ST_SMK_SHARED_LO + 2u * ST_SECTOR <= FL_GAP_HI, "SMK store C: past FL_GAP");
_Static_assert(ST_SMK_P5_LO + 10u * ST_SECTOR <= FL_DATA_HI && FL_DATA_LO == 0x9D000u, "SMK store A: past FL_DATA");
_Static_assert(OBJ_SHARED_EXT + 1u == OBJ_COUNT && OBJ_P5BANK0 + 5u == OBJ_SHARED_EXT && OBJ_NATIVEFM0 + 2u == OBJ_P5BANK0 &&
               OBJ_UPFM6_EXT + 1u == OBJ_NATIVEFM0 && OBJ_UPRESET_EXT0 + 2u == OBJ_UPFM6_EXT &&
               OBJ_CZBANK0 + 8u == OBJ_UPRESET_EXT0 && OBJ_BANK0 + 4u == OBJ_CZBANK0,
               "SMK store: the object numbers changed, check the layout");
static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1); 0: none on the SMK */
{
    copy &= 1u;
    if (obj >= OBJ_COUNT || !ST_SMK_HAS(obj)) return 0;
    if (obj == OBJ_SHARED_EXT) return ST_SMK_SHARED_LO + copy * ST_SECTOR;
    if (obj >= OBJ_P5BANK0) return ST_SMK_P5_LO + (obj - OBJ_P5BANK0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj >= OBJ_NATIVEFM0) return 0xAB000u + (obj - OBJ_NATIVEFM0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj == OBJ_UPFM6_EXT) return 0xA9000u + copy * ST_SECTOR;
    if (obj >= OBJ_UPRESET_EXT0) return 0xA5000u + (obj - OBJ_UPRESET_EXT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj >= OBJ_CZBANK0 + ST_SMK_CZ_IN_UFL)
        return ST_SMK_CZ2_LO + (obj - OBJ_CZBANK0 - ST_SMK_CZ_IN_UFL) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj >= OBJ_CZBANK0) return ST_SMK_CZ_LO + (obj - OBJ_CZBANK0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj >= OBJ_BANK0) return ST_SMK_BANK_LO + (obj - OBJ_BANK0) * 10u * ST_SECTOR + copy * 5u * ST_SECTOR;
    if (obj == OBJ_SETTINGS) return 0x9D000u + copy * ST_SECTOR;
    if (obj == OBJ_FM6BANK) return 0xA3000u + copy * ST_SECTOR;
    return 0x9F000u + (obj - OBJ_UPRESET0) * 2u * ST_SECTOR + copy * ST_SECTOR;   /* OBJ_UPRESET0, +1 */
}

/* The owner of the store. Several FM-1 firmwares are ported to the SMK-37 with the same board layer and
 * the same store area, each with its own object formats: a firmware must never parse another's data. The
 * sector 0xAF000 (the same in every port) holds "SMK37DATA" + the firmware family (FELUCCA_DATA_FAMILY,
 * build.py; Melodee: MELODEE). Ours: load as ever. Erased (data from before the tag) or another family's:
 * clear Melodee's whole store (A, B and C; the other firmware's projects, its own backups are its business)
 * and take it; FELUCCA_DATA_UNTAGGED_OK=1 would keep untagged data (Melodee never wrote any on an SMK).
 * Calibration, BT pairing, USR and key_mac lie outside the three windows: never touched. */
#ifndef FELUCCA_DATA_FAMILY
#define FELUCCA_DATA_FAMILY "MELODEE"
#endif
#ifndef FELUCCA_DATA_UNTAGGED_OK
#define FELUCCA_DATA_UNTAGGED_OK 0
#endif
#ifndef ST_IDLE
#define ST_IDLE() ((void)0)                        /* (firmware: the watchdog, storage_hw.c) */
#endif
_Static_assert(ST_SMK_TAG >= FL_DATA_LO && ST_SMK_TAG + ST_SECTOR <= FL_DATA_HI, "SMK store owner: outside FL_DATA");
_Static_assert(sizeof FELUCCA_DATA_FAMILY - 1u >= 1u && sizeof FELUCCA_DATA_FAMILY - 1u <= 7u, "FELUCCA_DATA_FAMILY: 1..7 characters");
/* The stock firmware's mapping in USRFLASH (global settings at 0xC2000, magic A3 3A; 8 presets from 0xC3000 every
 * 0x5000, 0x29E3 bytes each, the rest erased; the end marker "usrdata\0" at 0xEAFF8, FL_STOCKTAG): the stock firmware
 * trusts the area while the marker is there and never rebuilds it, so after a return to stock Melodee's store B
 * would be its key, knob and fader mapping (every key one note). So: an intact stock mapping is kept (it reads as
 * empty banks), and the marker is released (its sector erased: the stock firmware rebuilds its factory mapping at
 * its next start) before the first erase in USRFLASH (storage_hw.c st_erase) and at boot when it stands
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
static void st_stock_release(void)              /* erase the marker's sector, once: later calls find no marker */
{
    if (st_stock_tagged())
        st_erase(FL_STOCKTAG_LO);
}
static inline int st_in_usrflash(uint32_t off) { return off >= FL_UFL_LO && off < FL_STOCKTAG_LO; }
static int st_owner_check(void)                    /* 1: another firmware's (or untagged) store was cleared */
{
    static const char FAM[] = FELUCCA_DATA_FAMILY;
    uint8_t tag[16], want[16];
    uint32_t i, erased = 1, ours = 1, s, cleared = 0, keep;
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
            if (s != ST_SMK_TAG) {
                st_erase(s);
                ST_IDLE();
            }
        keep = st_stock_intact();                   /* the stock mapping: kept until Melodee writes there */
        for (s = FL_UFL_LO; !keep && s < FL_UFL_HI; s += ST_SECTOR) {
            st_erase(s);
            ST_IDLE();
        }
        for (s = FL_GAP_LO; s < FL_GAP_HI; s += ST_SECTOR) {
            st_erase(s);
            ST_IDLE();
        }
        cleared = 1;
    }
    st_erase(ST_SMK_TAG);
    st_prog(ST_SMK_TAG, want, 16);
    return (int)cleared;
}
#else
static int st_owner_check(void) { return 0; }
static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
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
#endif

static int st_banked(uint32_t obj) { return obj >= OBJ_BANK0 && obj < OBJ_CZBANK0; }
/* New preset objects start directly after the commit header, leaving the last
 * 256 bytes erased so the SPL/update loader cannot interpret musical data as OTA. */
static uint32_t st_payload_off(uint32_t obj) { return obj >= OBJ_UPRESET_EXT0 ? sizeof(st_hdr_t) : ST_PAYLOAD_OFF; }
static uint32_t st_capacity(uint32_t obj) { return st_banked(obj) ? ST_BANK_BASE_MAX + ST_BANK_EXT_MAX : obj >= OBJ_UPRESET_EXT0 ? ST_SECTOR - 256u - sizeof(st_hdr_t) : ST_PAYLOAD_MAX; }
static int st_preset_object(uint32_t obj)
{
    return (obj>=OBJ_UPRESET0 && obj<OBJ_BANK0) || (obj>=OBJ_CZBANK0 && obj<OBJ_COUNT);
}
static uint32_t st_payload_off_header(uint32_t obj,const st_hdr_t *h)
{
    return st_preset_object(obj) && h->rsv[0]==ST_COMPACT_TAG ? sizeof(st_hdr_t) : st_payload_off(obj);
}
static uint32_t st_extension(uint32_t obj, uint32_t copy)
{
#if BOARD_SMK37
    return ST_SMK_EXT_LO + ((obj - OBJ_BANK0) * 2u + copy) * 2u * ST_SECTOR;
#else
    return ST_BANK_EXT_LO + ((obj - OBJ_BANK0) * 2u + copy) * 2u * ST_SECTOR;
#endif
}
/* Keep existing five-sector copies in place. Each copy has its own two-sector
 * extension in unused flash, so migration never overwrites the current copy.
 * The last 256 bytes of extension sectors stay erased: the SPL scans those
 * tails for update records, and musical data must never look like one. */
static uint32_t st_address_at(uint32_t obj, uint32_t copy, uint32_t off, uint32_t *span,uint32_t payload)
{
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
    if(st_preset_object(obj) && !st_read(st_sector(obj,copy),&h,sizeof h))payload=st_payload_off_header(obj,&h);
    return st_address_at(obj,copy,off,span,payload);
}
static int st_read_range(uint32_t obj, uint32_t copy, uint32_t off, void *dst, uint32_t n)
{
    if (obj >= OBJ_COUNT || copy > 1u || off > st_capacity(obj) || n > st_capacity(obj) - off) return -1;
#if BOARD_SMK37
    if (!ST_SMK_HAS(obj)) return -1;               /* no flash for it on the SMK */
#endif
    uint8_t *d = dst;
    while (n) {
        uint32_t span, addr = st_address(obj, copy, off, &span), k = n < span ? n : span;
        if (st_read(addr, d, k)) return -1;
        d += k; off += k; n -= k;
    }
    return 0;
}

static uint8_t st_buf[256] __attribute__((aligned(4)));

/* the legacy single-pattern projects (OBJ_PROJECT0..+3): retired, their numbers reserved */
static int st_retired(uint32_t obj) { return obj >= OBJ_PROJECT0 && obj < OBJ_PROJECT0 + 4u; }

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (obj >= OBJ_COUNT || copy > 1u || st_retired(obj))
        return -1;
#if BOARD_SMK37
    if (!ST_SMK_HAS(obj))
        return -1;                                 /* no flash for it on the SMK: always empty */
#endif
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > st_capacity(obj) ||
        h->hcrc != st_crc32(h, sizeof *h - 4u) ||
        (h->rsv[0]==ST_COMPACT_TAG && (!st_preset_object(obj) || h->len>ST_SECTOR-256u-sizeof(st_hdr_t))))
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
static int st_current(uint32_t obj, st_hdr_t *h)
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

/* load the whole object into dst; returns its length, or -1 if it does not fit */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    st_hdr_t h;
    int cur;
    if (obj >= OBJ_COUNT || (cur = st_current(obj, &h)) < 0 || h.len > max) return -1;
    if (st_read_range(obj, (uint32_t)cur, 0, dst, h.len)) return -1;
    return (int)h.len;
}

/* a committed record is there, whatever it holds (st_load: -1 for one too big for its buffer too) */
static int st_exists(uint32_t obj)
{
    st_hdr_t h;
    return obj < OBJ_COUNT && !st_retired(obj) && st_current(obj, &h) >= 0 && h.len != 0;
}

static int st_save(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t seq, base, off;
    int cur, rc;
    st_hdr_t h;
    if (obj >= OBJ_COUNT || st_retired(obj) || len > st_capacity(obj))
        return -1;
#if BOARD_SMK37
    if (!ST_SMK_HAS(obj))
        return -1;                                 /* no flash for it on the SMK: SAVE ERROR */
#endif
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    base = st_sector(obj, cur == 0 ? 1u : 0u);       /* write the other copy */
    uint32_t payload=st_payload_off(obj),compact=0;
    if(st_preset_object(obj) && len==3716u){uint32_t magic;memcpy(&magic,src,sizeof magic);compact=magic==0x32425055u;}
    if(compact)payload=sizeof(st_hdr_t);
    uint32_t crc = st_crc32(src, len);
    uint32_t extent = obj >= OBJ_BANK0 && obj < OBJ_CZBANK0 ? 5u * ST_SECTOR : ST_SECTOR;
    for (off = 0; off < extent; off += ST_SECTOR)
        if ((rc = st_erase(base + off)) != 0) return rc;
    if (st_banked(obj)) for (off = 0; off < 2u * ST_SECTOR; off += ST_SECTOR)
        if ((rc = st_erase(st_extension(obj, cur == 0 ? 1u : 0u) + off)) != 0) return rc;
    for (off = 0; off < len; off += sizeof st_buf) {
        uint32_t n = len - off > sizeof st_buf ? sizeof st_buf : len - off;
        memcpy(st_buf, (const uint8_t *)src + off, n);
        uint32_t span, addr = st_address_at(obj, cur == 0 ? 1u : 0u, off, &span,payload);
        if ((rc = st_prog(addr, st_buf, n)) != 0) return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)(cur == 0 ? 1 : 0);
    h.seq = seq + 1u;
    h.len = len;
    h.crc = crc;
    h.rsv[0]=compact?ST_COMPACT_TAG:0xFFFFFFFFu;h.rsv[1]=0xFFFFFFFFu;
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
