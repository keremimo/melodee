/* SPDX-License-Identifier: GPL-3.0-only */
/* The one remembered device (firmware/src/ble/ble_store.c; docs/BLE-DEVICES-DESIGN.md §3 and the ruling of 2026-10-09:
 * LAST is a device the FM-1 connected to as a central):
 *   format    70 octets, mark / version / choice, the entry's fields where §3.1 puts them
 *   rules     nothing remembered reads NONE; LAST only with an entry; a new device replaces the entry and its keys, the
 *             same one again keeps its bond; the identity (IRK, identity address) replaces the RPA it was found at;
 *             FORGET; an older, a foreign or a damaged record reads "nothing, NONE"; the name shown or the address
 *   flash     the settings object round trip through storage.c (CRC-checked A / B sectors 0xFC000 / 0xFD000) with the
 *             store at the end of the record; a record from before the store (shorter) reads NONE; nothing is ever
 *             written in 0xE7000-0xE9FFF (the SDK's VM and BTIF)
 * Exit status: the number of failed checks. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define BLE_API static
#include "../firmware/src/ble/ble_store.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("ble_store: %-104s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* ---- a NOR for storage.c (as tests/backup_test.c) */
static uint8_t nor[0x100000];
static uint32_t prog_lo = 0xFFFFFFFFu, prog_hi;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    prog_lo = off < prog_lo ? off : prog_lo;
    prog_hi = off + n > prog_hi ? off + n : prog_hi;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage.c"

/* the settings record's BLE tail as storage/project.c persist_t has it (FELUCCA_BLE): address, trim copy, bond, store */
struct rec_old { uint32_t magic; uint8_t ble_addr[8], ble_rf[100], ble_bond[28]; };
struct rec { uint32_t magic; uint8_t ble_addr[8], ble_rf[100], ble_bond[28], ble_dev[BLE_DEV_STORE_SIZE]; };

static const uint8_t A1[6] = {0xB2, 0xA1, 0x33, 0x44, 0x55, 0xC6}, A2[6] = {1, 2, 3, 4, 5, 0xC7};

static void t_format(void)
{
    struct ble_dev_store s;
    uint8_t raw[BLE_DEV_STORE_SIZE];
    char nm[BLE_NAME_MAX + 1u];
    check("the store is 70 octets: header 4, entry 66 (address 6, info 1, name 16, LTK 16, Rand 8, EDIV 2, IRK 16, 1)",
          sizeof s == 70u && sizeof s.dev == 66u && offsetof(struct ble_dev_store, dev) == 4u &&
          offsetof(struct ble_dev, name) == 7u && offsetof(struct ble_dev, irk) == 49u);
    ble_store_reset(&s);
    check("reset: marked (0xB6, version 1), NONE, nothing remembered",
          s.mark == 0xB6u && s.ver == 1u && s.sel == BLE_SEL_NONE && !ble_store_has_last(&s));
    ble_store_select(&s, BLE_SEL_LAST);
    check("LAST cannot be picked without an entry: NONE stays", s.sel == BLE_SEL_NONE);
    ble_store_set_last(&s, A1, 1, "KeyStep 37", BLE_KIND_MIDI);
    check("a device we connected to becomes LAST: role C, random address, KIND MIDI, used, its name",
          ble_store_has_last(&s) && s.dev.info == (BLE_DEV_USED | BLE_DEV_CENTRAL | BLE_DEV_RANDOM | 1u << 4) &&
          !memcmp(s.dev.addr, A1, 6) && !strcmp(s.dev.name, "KeyStep 37"));
    ble_store_select(&s, BLE_SEL_LAST);
    check("... and can be picked", s.sel == BLE_SEL_LAST);
    {
        uint8_t ltk[16], rnd[8];
        memset(ltk, 0x11, 16);
        memset(rnd, 0x22, 8);
        ble_store_set_bond(&s, ltk, rnd, 0x3344, 0);
    }
    check("its bond: LTK, Rand, EDIV (least significant first), bonded",
          (s.dev.info & BLE_DEV_BONDED) && s.dev.ltk[15] == 0x11u && s.dev.rand[0] == 0x22u && s.dev.ediv[0] == 0x44u &&
          s.dev.ediv[1] == 0x33u);
    check("a Just Works bond: not authenticated, no passkey known to be needed", !s.dev.sec);
    ble_store_set_mitm(&s);
    {
        uint8_t ltk[16], rnd[8];
        memset(ltk, 0x11, 16);
        memset(rnd, 0x22, 8);
        ble_store_set_bond(&s, ltk, rnd, 0x3344, 1);
    }
    check("it needs a passkey (learned), then an authenticated bond: both kept (the last octet, 0 in older records)",
          s.dev.sec == (BLE_DEV_SEC_MITM | BLE_DEV_SEC_AUTH) && offsetof(struct ble_dev, sec) == 65u);
    ble_store_set_last(&s, A1, 1, "KeyStep 37", BLE_KIND_MIDI);
    check("the same device again keeps its bond and its security level", (s.dev.info & BLE_DEV_BONDED) &&
          s.dev.ltk[0] == 0x11u && s.dev.sec == (BLE_DEV_SEC_MITM | BLE_DEV_SEC_AUTH));
    {
        uint8_t irk[16], id[6] = {9, 8, 7, 6, 5, 0x44};
        memset(irk, 0x5A, 16);
        ble_store_set_id(&s, irk, id, 0);
        check("its identity (IRK, identity address) replaces the RPA it was found at; public type",
              (s.dev.info & BLE_DEV_IRK) && !(s.dev.info & BLE_DEV_RANDOM) && !memcmp(s.dev.addr, id, 6) &&
                  s.dev.irk[7] == 0x5Au && (s.dev.info & BLE_DEV_BONDED));
    }
    ble_store_save(&s, raw);
    check("saved: the octets as laid out (mark, version, choice, address at 4)", raw[0] == 0xB6u && raw[1] == 1u &&
          raw[2] == BLE_SEL_LAST && raw[4] == 9u);
    {
        struct ble_dev_store b;
        check("loaded back: the same", ble_store_load(&b, raw) && !memcmp(&b, &s, sizeof b));
    }
    ble_store_set_last(&s, A2, 1, "WIDI Master Long Name X", BLE_KIND_MIDI | BLE_KIND_FM1);
    check("another device replaces the entry: no bond, no IRK, no security level; the name cut to 16; the choice kept",
          !(s.dev.info & (BLE_DEV_BONDED | BLE_DEV_IRK)) && !s.dev.sec && !memcmp(s.dev.addr, A2, 6) &&
              !memcmp(s.dev.name, "WIDI Master Long", 16) && s.sel == BLE_SEL_LAST &&
              (s.dev.info >> BLE_DEV_KIND_SHIFT & 3u) == 3u);
    ble_store_name(&s, nm);
    check("the name shown (16 characters, terminated)", !strcmp(nm, "WIDI Master Long"));
    ble_store_set_last(&s, A2, 1, "", BLE_KIND_MIDI);
    ble_store_name(&s, nm);
    check("no name: the address shown as C7:05:..:02:01", !strcmp(nm, "C7:05:..:02:01"));
    ble_store_forget(&s);
    check("FORGET: nothing remembered, NONE, no keys left", !ble_store_has_last(&s) && s.sel == BLE_SEL_NONE &&
          s.mark == 0xB6u && s.dev.ltk[0] == 0 && s.dev.addr[0] == 0);
    ble_store_name(&s, nm);
    check("... and no name", nm[0] == 0);
}

static void t_load_rules(void)
{
    struct ble_dev_store s;
    uint8_t raw[BLE_DEV_STORE_SIZE];
    memset(raw, 0, sizeof raw);
    check("an older record (zeros: before the store) reads nothing remembered, NONE",
          !ble_store_load(&s, raw) && s.sel == BLE_SEL_NONE && !ble_store_has_last(&s) && s.mark == 0xB6u);
    memset(raw, 0xFF, sizeof raw);
    check("erased flash (0xFF) reads the same", !ble_store_load(&s, raw) && s.sel == BLE_SEL_NONE && !ble_store_has_last(&s));
    ble_store_reset(&s);
    ble_store_set_last(&s, A1, 1, "X", BLE_KIND_MIDI);
    ble_store_select(&s, BLE_SEL_LAST);
    ble_store_save(&s, raw);
    raw[1] = 2;
    check("another version: nothing, NONE", !ble_store_load(&s, raw) && s.sel == BLE_SEL_NONE);
    raw[1] = 1;
    raw[2] = 7;
    check("a choice out of range: nothing, NONE", !ble_store_load(&s, raw) && s.sel == BLE_SEL_NONE);
    raw[2] = BLE_SEL_LAST;
    raw[4 + 6] &= (uint8_t)~BLE_DEV_CENTRAL;
    check("an entry that is not ours as a central (role P: the ruling) is not LAST: NONE",
          ble_store_load(&s, raw) && !ble_store_has_last(&s) && s.sel == BLE_SEL_NONE);
}

static void t_flash(void)
{
    static struct rec r, b;
    static struct rec_old o;
    struct ble_dev_store s, l;
    int n;
    memset(nor, 0xFF, sizeof nor);
    memset(&r, 0, sizeof r);
    r.magic = 0x50455233u;
    memset(r.ble_bond, 0xB5, sizeof r.ble_bond);   /* (the peripheral bond beside it is untouched) */
    ble_store_reset(&s);
    ble_store_set_last(&s, A1, 1, "KeyStep 37", BLE_KIND_MIDI);
    ble_store_select(&s, BLE_SEL_LAST);
    ble_store_save(&s, r.ble_dev);
    check("the settings object saved with the store at the end of the record", st_save(OBJ_SETTINGS, &r, sizeof r) == 0);
    check("... written only in the settings sectors 0xFC000-0xFDFFF (never 0xE7000-0xE9FFF)",
          prog_lo >= 0xFC000u && prog_hi <= 0xFE000u && !(prog_lo < 0xEA000u && prog_hi > 0xE7000u));
    memset(&b, 0, sizeof b);
    n = st_load(OBJ_SETTINGS, &b, sizeof b);
    check("loaded back whole (CRC good): the same octets", n == (int)sizeof r && !memcmp(&b, &r, sizeof r));
    check("... LAST and the choice decoded from it",
          ble_store_load(&l, b.ble_dev) && ble_store_has_last(&l) && l.sel == BLE_SEL_LAST && !strcmp(l.dev.name, "KeyStep 37") &&
              !memcmp(b.ble_bond, r.ble_bond, sizeof r.ble_bond));
    ble_store_forget(&s);
    ble_store_save(&s, r.ble_dev);
    st_save(OBJ_SETTINGS, &r, sizeof r);
    memset(&b, 0, sizeof b);
    n = st_load(OBJ_SETTINGS, &b, sizeof b);
    check("FORGET saved: the next boot has nothing remembered, NONE",
          n == (int)sizeof r && ble_store_load(&l, b.ble_dev) && !ble_store_has_last(&l) && l.sel == BLE_SEL_NONE);
    /* a record from a BLE build before the store: shorter; project.c zeroes the field it lacks (persist_boot) */
    memset(&o, 0, sizeof o);
    o.magic = 0x50455233u;
    st_save(OBJ_SETTINGS, &o, sizeof o);
    memset(&b, 0x77, sizeof b);
    n = st_load(OBJ_SETTINGS, &b, sizeof b);
    if (n == (int)offsetof(struct rec, ble_dev))
        memset(b.ble_dev, 0, sizeof b.ble_dev);
    check("a record from before the store: its length is the store's offset; zeroed, it reads NONE",
          n == (int)offsetof(struct rec, ble_dev) && !ble_store_load(&l, b.ble_dev) && l.sel == BLE_SEL_NONE &&
              !ble_store_has_last(&l));
}

int main(void)
{
    t_format();
    t_load_rules();
    t_flash();
    printf("%s\n", fails ? "ble_store: FAILED" : "ble_store: all passed");
    return fails != 0;
}
