/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The Melodee 1.x -> 2.0 converter (the web installer's, Melodee Studio's): the last Melodee 1.x's own virtual FM-1
 * (its web/core/melodee_core.c, every 1.x reader and its synths) with this file built into it. A 1.x backup goes in
 * through its own backup protocol (as into an FM-1 running 1.x), its readers bring every record to the form 1.x
 * writes last, and this file writes those as Melodee 2.0's records (FBKS projects, PER6 settings with the TPLM
 * template, UPB3 user preset banks, KIT2 kits). Firmware 2.0 reads 2.0's records only: everything 1.x knows stays here.
 * Built against the frozen 1.x tree (web/convert/LEGACY_REF), never against firmware/src:
 *   web/convert/build.sh  ->  build/convert/melodee_legacy.wasm, build/convert/legacy_native */
#include "melodee_core.c"

#include <stdarg.h>
#include <stddef.h>
#include "bk_client.h"

/* ---- the conversion's report: what the 2.0 records could not keep as 1.x had it (the installer shows it) */
static char lg_rep[8192];
static uint32_t lg_rep_n;
static void lg_note(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (lg_rep_n < sizeof lg_rep - 1u) {
        int k = vsnprintf(lg_rep + lg_rep_n, sizeof lg_rep - lg_rep_n, fmt, ap);
        if (k > 0) lg_rep_n += (uint32_t)k < sizeof lg_rep - lg_rep_n ? (uint32_t)k : sizeof lg_rep - 1u - lg_rep_n;
    }
    va_end(ap);
}

/* ---- Melodee 2.0.0's records (frozen here as 2.0.0 writes them: firmware/modules/shipped.h, core.h, pattern_store.c,
 * motion.c of that release). A record names synths by handles, each with an entry (handle, 0, 0, 0, ID, name[16]) in
 * its end block; a handle no entry names is none (silent). Handles are below V2_NENG, never 0 */
#define V2_NENG 30u
#define V2_FBKS 0x534B4246u                     /* "FBKS": FBKP's layout, the end block always */
#define V2_MO_TAG(h) (4u + (h) - 2u)            /* a synth's own parameters' Motion tag (motion.c MO_TAG_MOD + h - ENG_H_SHIP0) */
#define V2_ID_MIDI 0x21EB3550E8D4BF9EULL         /* the MIDI Track */
enum { KF_808, KF_909, KF_SID, KF_CR78, KF_FM, KF_SAMPLER, KF_N };   /* DRUM's kits: synths of their own in 2.0 */
static const struct { uint64_t uid; const char *name; } V2_KIT[KF_N] = {
    {0x153965FA94BE8E69ULL, "808"}, {0x98357802396AB4EEULL, "909"}, {0xAE957CEEE32C014EULL, "SID KIT"},
    {0x023E195C232B0352ULL, "CR78"}, {0x30F273CD0F5E6077ULL, "FM KIT"}, {0xA7C6C5DCAB17D386ULL, "SAMPLER"}};
/* DRUM's KIT (its E0: 0..4 the 808 and the kits before it, 5 909, 6 SID, 7 CR78, 8 FM, 9..16 the user's KIT 1..8) */
static uint32_t lg_kit_family(int32_t kit)
{
    return kit <= 4 ? KF_808 : kit == 5 ? KF_909 : kit == 6 ? KF_SID : kit == 7 ? KF_CR78 : kit == 8 ? KF_FM : KF_SAMPLER;
}
/* the KIT a kit synth takes (its own value: the 808's is 4, 1.x's kits before it played as it; SAMPLER KIT 1..8) */
static int32_t lg_kit_value(int32_t kit) { return kit < 4 ? 4 : kit > 16 ? 16 : kit; }
/* DRUM's preset (808 KIT, 909 KIT, SID KIT, SID DIGI, CR78 KIT, FM KIT) as the kit synth's: SID DIGI its second, a user
 * kit SAMPLER's KIT n; any other byte (the power-on sound's) as it was */
static uint32_t lg_kit_preset(uint32_t f, uint32_t preset, int32_t kit)
{
    if (preset >= 6u) return preset;
    return f == KF_SID ? (preset == 3u) : f == KF_SAMPLER ? (uint32_t)clamp(kit - 9, 0, 7) : 0u;
}
typedef struct { uint64_t uid; char name[MOD3_NAME_MAX]; } lg_who_t;
static lg_who_t lg_who[V2_NENG];                       /* the record's handles: who each names (uid 0: free) */
static uint8_t lg_h1[NENGINES], lg_hk[KF_N];           /* a 1.x number's handle, a kit's (0: none yet) */
static void lg_handles_reset(void) { memset(lg_who, 0, sizeof lg_who); memset(lg_h1, 0, sizeof lg_h1); memset(lg_hk, 0, sizeof lg_hk); }
static uint32_t lg_handle(uint64_t uid, const char *name, uint32_t nlen)   /* 0: none left */
{
    uint32_t h;
    for (h = 2; h < V2_NENG; h++) if (lg_who[h].uid == uid) return h;
    for (h = 2; h < V2_NENG && lg_who[h].uid; h++) ;
    if (h == V2_NENG) return 0;
    lg_who[h].uid = uid;
    for (uint32_t i = 0; i < MOD3_NAME_MAX && i < nlen && name[i]; i++) lg_who[h].name[i] = name[i];
    return h;
}
static uint32_t lg_h_of(uint32_t e)                     /* a 1.x number (as this device has it) -> its handle, 0 none */
{
    if (e >= NENGINES) return 0;
    if (!lg_h1[e]) {
        if (e == ENGI_MIDI) lg_h1[e] = (uint8_t)lg_handle(V2_ID_MIDI, "MIDI Track", 10u);
        else if (eng_map[e].uid) lg_h1[e] = (uint8_t)lg_handle(eng_map[e].uid, eng_map[e].name, MOD3_NAME_MAX);
    }
    return lg_h1[e];
}
static uint32_t lg_h_kit(uint32_t f)
{
    if (!lg_hk[f]) lg_hk[f] = (uint8_t)lg_handle(V2_KIT[f].uid, V2_KIT[f].name, 16u);
    return lg_hk[f];
}
static uint32_t lg_drum(void) { return ship_flag(SHIP_F_GM); }   /* 1.x's DRUM (the manifest's General MIDI kit synth) */
/* a sound's synth (1.x number e, its KIT kit) -> its handle in the 2.0 record; *f its kit (KF_N: not DRUM's) */
static uint32_t lg_sound(uint32_t e, int32_t kit, uint32_t *f, const char *what)
{
    uint32_t h;
    *f = KF_N;
    if (e == lg_drum()) {
        *f = lg_kit_family(kit);
        h = lg_h_kit(*f);
    } else h = lg_h_of(e);
    if (!h && e < NENGINES)
        lg_note("  %s: synth %u has no ID (retired): silent\n", what, (unsigned)e);
    return h;
}

/* ---- a project: the record 1.x's capture wrote (bank_pack, runtime: FBKP / FBKQ / FBKR) -> FBKS, in place */
static uint32_t lg_fbks(uint8_t *raw)
{
    uint8_t *fun = raw + 8u, *tail = raw + BANK_ACTIVE_OFF, blk[BANK_TAIL_SIZE];
    uint32_t magic, np = fun[66], stride = np + 2u + NSTEP * 8u, base[PROJ_TRK], f, pos = 0, ne = 0, npat = 0, old_len = 0;
    char what[48];
    memcpy(&magic, raw, 4);
    if (magic != BANKP_MAGIC && magic != BANKQ_MAGIC && magic != BANKR_MAGIC) return 1;
    if (np != P_COUNT) return 1;
    lg_handles_reset();
    for (uint32_t t = 0; t < PROJ_TRK; t++) {         /* the parts */
        uint8_t *at = fun + 68u + t * stride;
        int32_t kit = (int32_t)at[P_E0] - 64;
        snprintf(what, sizeof what, "track %u", (unsigned)t + 1u);
        uint32_t e = at[np], h = lg_sound(e, kit, &f, what);
        base[t] = f;
        at[np] = (uint8_t)h;
        if (f < KF_N) { at[np + 1u] = (uint8_t)lg_kit_preset(f, at[np + 1u], kit); at[P_E0] = (uint8_t)(lg_kit_value(kit) + 64); }
    }
    uint8_t pool_f[CHAIN_SOUNDS];                      /* each sound's kit (KF_N: not DRUM's) */
    memset(pool_f, KF_N, sizeof pool_f);
    for (uint32_t j = 0; bank_sounds && j < bank_sound_count; j++) {   /* the song's and patterns' sounds */
        song_sound_t s;
        uint8_t *at = tail + bank_sound_off() + BANKO_HEAD_BYTES + j * sizeof s;
        memcpy(&s, at, sizeof s);
        snprintf(what, sizeof what, "sound %.16s", s.name);
        int32_t kit = (int32_t)s.p[P_E0] - 64;
        s.engine = (uint8_t)lg_sound(s.engine, kit, &f, what);
        pool_f[j] = (uint8_t)f;
        if (f < KF_N) { s.preset = (uint8_t)lg_kit_preset(f, s.preset, kit); s.p[P_E0] = (uint8_t)(lg_kit_value(kit) + 64); }
        memcpy(at, &s, sizeof s);
    }
    {                                                   /* the Motion: synths' own parameters by the 2.0 tags */
        uint8_t *m = fun + 68u + PROJ_TRK * stride + sizeof proj_scratch.chain, *tags = raw + BL.motion;
        uint32_t cnt = m[0] < MOTION_MAX ? m[0] : MOTION_MAX, n = 0, gone = 0;
        for (uint32_t i = 0; i < cnt; i++) {
            uint8_t *ev = m + 4u + 4u * i;
            uint32_t k = ev[0] >> 6, id = ev[1], keep = 1, fk = k < PROJ_TRK ? base[k] : KF_N;
            int16_t v = (int16_t)(ev[2] | ev[3] << 8);
            if (bank_patterns && k < PROJ_TRK && tags[i] < NPAT) {   /* (its pattern's own sound, when it has one) */
                uint32_t j = tail[bank_pattern_off() + k * NPAT + tags[i]];
                if (j && j <= bank_sound_count) fk = pool_f[j - 1u];
            }
            if (motion_native_event(id, v)) {
                uint32_t e = motion_tag_engine((uint32_t)v >> 8), h;
                h = e == lg_drum() ? (fk < KF_N ? lg_h_kit(fk) : 0u) : lg_h_of(e);
                if (!h) keep = 0;
                else v = (int16_t)(V2_MO_TAG(h) << 8 | ((uint32_t)v & 255u));
            } else if (id == P_E0 && fk < KF_N) {
                if (lg_kit_family(v) != fk) keep = 0;  /* (KIT jumping to another kit: another synth now) */
                else v = (int16_t)lg_kit_value(v);
            }
            if (!keep) { gone++; continue; }
            ev[2] = (uint8_t)v; ev[3] = (uint8_t)((uint16_t)v >> 8);
            if (n != i) { memcpy(m + 4u + 4u * n, ev, 4u); tags[n] = tags[i]; }
            n++;
        }
        for (uint32_t i = n; i < cnt; i++) { memset(m + 4u + 4u * i, 0, 4u); tags[i] = 0; }
        m[0] = (uint8_t)n;
        if (gone) lg_note("  Motion: %u event(s) left out (a synth 2.0 has not got, or KIT moving to another kit)\n", (unsigned)gone);
    }
    if (magic == BANKR_MAGIC) {                         /* its end block's patches, kept */
        const uint8_t *t = tail + BANK_TAIL_SIZE - 4u - BANKR_TRAIL;
        if (!(old_len = bank_ext_head(t, BANK_TAIL_SIZE - 4u - BANKR_TRAIL))) return 2;
    }
    {
        uint8_t pat[BANK_TAIL_SIZE];
        uint32_t pn = 0;
        if (old_len) {
            const uint8_t *t = tail + BANK_TAIL_SIZE - 4u - BANKR_TRAIL, *b;
            uint32_t plen = t[0] | (uint32_t)t[1] << 8, q = t[2] * EMAP_ENTRY;
            b = t - plen;
            for (uint32_t i = 0; i < t[3]; i++) {
                uint32_t len = b[q + 2u] | (uint32_t)b[q + 3u] << 8;
                memcpy(pat + pn, b + q, 4u + len);
                pat[pn + 1u] = (uint8_t)lg_h_of(b[q + 1u]);
                if (!pat[pn + 1u]) lg_note("  track %u: a patch of a synth with no ID left out\n", (unsigned)b[q] + 1u);
                else { pn += 4u + len; npat++; }
                q += 4u + len;
            }
        }
        for (uint32_t h = 2; h < V2_NENG; h++)
            if (lg_who[h].uid) {
                memset(blk + pos, 0, EMAP_ENTRY);
                blk[pos] = (uint8_t)h;
                mod_put64(blk + pos + 4u, lg_who[h].uid);
                memcpy(blk + pos + 12u, lg_who[h].name, MOD3_NAME_MAX);
                pos += EMAP_ENTRY; ne++;
            }
        memcpy(blk + pos, pat, pn); pos += pn;
    }
    blk[pos] = (uint8_t)pos; blk[pos + 1u] = (uint8_t)(pos >> 8); blk[pos + 2u] = (uint8_t)ne; blk[pos + 3u] = (uint8_t)npat;
    blk[pos + 4u] = magic == BANKQ_MAGIC || (magic == BANKR_MAGIC && tail[BANK_TAIL_SIZE - 4u - BANKR_TRAIL + 4u]);
    blk[pos + 5u] = blk[pos + 6u] = blk[pos + 7u] = 0;
    memcpy(blk + pos + 8u, &(uint32_t){BANKR_TAG}, 4);
    {
        uint32_t len = pos + BANKR_TRAIL, end = BANK_TAIL_SIZE - 4u;
        for (uint32_t i = end - len; len > old_len && i < end - old_len; i++)
            if (tail[i]) { lg_note("  the project is too full for its synths' entries\n"); return 3; }
        memset(tail + end - (len > old_len ? len : old_len), 0, len > old_len ? len : old_len);
        memcpy(tail + end - len, blk, len);
    }
    memset(fun + 8u + 2u * G_ENGSEL, 0, 2u);            /* (a handle: 2.0 never stores it) */
    magic = V2_FBKS; memcpy(raw, &magic, 4);
    {
        uint32_t sum = proj_hash(fun, PROJ_STORE_SIZE - 4u);
        memcpy(fun + PROJ_STORE_SIZE - 4u, &sum, 4);
    }
    bank_checksum(raw);
    return 0;
}
/* ---- the settings record and the template after it: PER5 (+ any 1.x template) -> PER6 (+ TPLM) */
#define V2_PERSIST 0x50455236u                   /* "PER6": PER5's layout, the stars by synth ID */
#define V2_TMPL 0x4D4C5054u                      /* "TPLM": TPLL, then the synths' entries (nid, 3 zero, 4 entries) */
#define V2_STAR_ROW 9u                           /* the stars: rows 9..13, entries (ID, n, n bytes of stars) */
typedef struct {
    uint8_t body[offsetof(tmpl_t, size)];
    uint8_t nid, rsv[3];
    uint8_t id[PROJ_TRK][EMAP_ENTRY];
    uint32_t size, magic;
} v2_tmpl_t;
_Static_assert(sizeof(v2_tmpl_t) == 3160u, "TPLM: 2.0.0's template");
/* the factory stars 1.x kept by engine number -> rows 9..13 by synth ID (favorites.c's entries) */
static void lg_stars(persist_t *p)
{
    static const uint8_t own[] = {ENGI_PROPHET, ENGI_FM6, ENGI_CZ, ENGI_SID, 6u, 7u};   /* (+ TRIO, WHEEL) */
    uint8_t *b = p->favorites.factory[V2_STAR_ROW], st[KF_N + 6u][32];
    uint64_t uid[KF_N + 6u];
    uint32_t np[KF_N + 6u], n = 0, at = 0, mp;
    memset(st, 0, sizeof st);
    memcpy(&mp, p->favorites.factory[7] + 28, 4);
    for (uint32_t i = 0; i < sizeof own; i++) {
        uint32_t e = own[i];
        if ((e == 6u || e == 7u) && mp == MPARAM_TAG) continue;   /* (rows 6, 7: MIDI LEARN's parameters) */
        if (!eng_map[e].uid) continue;
        uid[n] = eng_map[e].uid; np[n] = eng_npresets(e) < 256u ? eng_npresets(e) : 255u;
        for (uint32_t k = 0; k < np[n]; k++) if (favorite_has(e, k)) st[n][k / 8u] |= (uint8_t)(1u << (k % 8u));
        n++;
    }
    for (uint32_t k = 0; k < 6u; k++)                  /* DRUM's six presets: each kit synth's own */
        if (favorite_has(lg_drum(), k)) {
            static const uint8_t f[6] = {KF_808, KF_909, KF_SID, KF_SID, KF_CR78, KF_FM};
            uint32_t j = 0;
            while (j < n && uid[j] != V2_KIT[f[k]].uid) j++;
            if (j == n) { uid[n] = V2_KIT[f[k]].uid; np[n] = f[k] == KF_SID ? 2u : 1u; n++; }
            st[j][0] |= (uint8_t)(1u << (k == 3u));
        }
    /* 1.x's star bytes, cleared (2.0 keeps none there): rows 0, 2, 3, 1 up to its settings, 8's and 14's first
     * bytes, row 15's (CZ-1 factory and native slots), rows 9..13 (written below) */
    memset(p->favorites.factory[0], 0, 32); memset(p->favorites.factory[2], 0, 64);
    memset(p->favorites.factory[1], 0, 20); memset(p->favorites.factory[8], 0, 4);
    memset(p->favorites.factory[14], 0, 16); memset(p->favorites.factory[15], 0, 24);
    p->favorites.factory[15][24] &= 0xFEu;
    memset(b, 0, 5u * 32u);
    for (uint32_t j = 0; j < n; j++) {
        uint32_t m = (np[j] + 7u) / 8u, any = 0;
        for (uint32_t i = 0; i < m; i++) any |= st[j][i];
        if (!any) continue;
        if (at + 9u + m > 5u * 32u) { lg_note("  settings: no room for every synth's stars\n"); break; }
        mod_put64(b + at, uid[j]); b[at + 8u] = (uint8_t)m; memcpy(b + at + 9u, st[j], m);
        at += 9u + m;
    }
}
/* the template (1.x's RAM: TPLL, whatever form it was read from) -> TPLM */
static void lg_tmpl(v2_tmpl_t *o)
{
    tmpl_t t = tmpl;
    lg_handles_reset();
    for (uint32_t k = 0; k < PROJ_TRK; k++) {
        char what[24];
        uint32_t f, e = t.t[k].engine;
        int32_t kit = t.t[k].p[P_E0];
        snprintf(what, sizeof what, "template track %u", (unsigned)k + 1u);
        t.t[k].engine = (uint8_t)lg_sound(e, kit, &f, what);
        if (f < KF_N) { t.t[k].preset = (uint8_t)lg_kit_preset(f, t.t[k].preset, kit); t.t[k].p[P_E0] = (int16_t)lg_kit_value(kit); }
        else if (e < NENGINES && e != ENGI_MIDI && t.t[k].engine && patch_store_tag(e) == MOD_SCH_STORE_NONE)
            lg_note("  template track %u: its %.16s patch is not in a backup: its INIT\n", (unsigned)k + 1u, eng_map[e].name);
    }
    t.g[G_ENGSEL] = 0;
    memset(o, 0, sizeof *o);
    memcpy(o->body, &t, sizeof o->body);
    for (uint32_t h = 2; h < V2_NENG; h++)
        if (lg_who[h].uid && o->nid < PROJ_TRK) {
            uint8_t *b = o->id[o->nid++];
            b[0] = (uint8_t)h; mod_put64(b + 4, lg_who[h].uid); memcpy(b + 12, lg_who[h].name, MOD3_NAME_MAX);
        }
    o->size = sizeof *o; o->magic = V2_TMPL;
}
/* a 1.x settings object (id 1) -> PER6 (+ TPLM) in out: its length, 0 bad */
static uint32_t lg_settings(const uint8_t *in, uint32_t n, uint8_t *out)
{
    static uint8_t raw[sizeof set_rec + 64u];
    uint32_t usb, rc, k;
    persist_t p;
    if (n > sizeof(persist_t) + 4u && !memcmp(in + n - 4u, "BLE1", 4)) {   /* (a raw record's radio keys after it: never
                                                    * in a 2.0 backup) */
        uint32_t t = n - sizeof(persist_t) - 4u;
        while (t && !tmpl_param_count(t)) t--;
        n = sizeof(persist_t) + t;
    }
    if (n < sizeof(persist_t) || n > sizeof raw) return 0;
    memcpy(raw, in, n);
    memcpy(&usb, raw + offsetof(persist_t, ext.usb_off), 4);
    k = usb & 3u;                                       /* (1.x's PUT refuses the voice budget's tag its own export writes) */
    memcpy(raw + offsetof(persist_t, ext.usb_off), &k, 4);
    if ((rc = lg_bk_put(1, raw, n)) != 0) { lg_note("  settings: 1.x refused them (%#x)\n", (unsigned)rc); return 0; }
    p = set_rec.p;                                      /* (as 1.x read them: settings_import) */
    settings_export(&p);
    p.ext.usb_off = usb;
    lg_stars(&p);
    p.magic = V2_PERSIST;
    memcpy(out, &p, sizeof p);
    if (!template_used()) return sizeof p;
    lg_tmpl((v2_tmpl_t *)(out + sizeof p));
    return sizeof p + sizeof(v2_tmpl_t);
}
/* ---- the user preset banks (ids 6..28): as 1.x reads them (up_cache_bank: its records, this device's numbers) -> UPB3,
 * every record's values in today's P_COUNT; after the records the synths they name by ID ("UIDS", n, 0 x 3, n IDs of 8
 * bytes), a record's engine byte its synth's place there + 1. At most V2_UP_IDS synths a bank */
#define V2_UPB3 0x33425055u
#define V2_UP_IDS_MAGIC 0x53444955u
#define V2_UP_IDS 10u
static uint32_t lg_bank(uint32_t b, uint8_t *out)   /* bank b -> UPB3 in out: its length (0: an empty bank) */
{
    up_bank_t *B = up_cache_bank(b);
    uint32_t used = 0, n = 0;
    uint64_t ids[V2_UP_IDS];
    struct { uint32_t magic; uint16_t rsize, nslot; } head = {V2_UPB3, 206u, UP_PER_BANK};
    uint8_t *disk = out + sizeof head, *m;
    memset(out, 0, sizeof head + UP_PER_BANK * 206u + 8u + V2_UP_IDS * 8u);
    memcpy(out, &head, sizeof head);
    for (uint32_t j = 0; j < UP_PER_BANK; j++) {
        up_rec_t r = B->r[j];
        uint8_t *d = disk + j * 206u;
        char what[40];
        if ((r.used & ~UP_STAR) != UP_USED) continue;
        snprintf(what, sizeof what, "user preset %u %.12s", (unsigned)(b * UP_PER_BANK + j + 1u), r.name);
        if (!up_valid(&r) && r.ver != UP_VER_CZ) { lg_note("  %s: unreadable, left out\n", what); continue; }
        uint32_t e = r.engine, f, h;
        lg_handles_reset();
        if (up_native(&r) || r.ver == UP_VER_CZ) {      /* (its patch as it is; UP_VER_CZ: the native CZ-1 form since) */
            if (r.ver == UP_VER_CZ) { r.ver = UP_VER_NATIVE_CZ; r.np = 0; memset(r.tone_extra, 0, sizeof r.tone_extra); }
            h = lg_sound(e, 0, &f, what);
        } else {                                        /* (the values by today's ids, the KIT bits after them) */
            int16_t v[P_COUNT], def[P_COUNT];
            uint8_t kb[MOD_SCH_KIT_PACKED];
            int kit_rec = r.ver == UP_VER_KIT || r.ver == UP_VER_KIT_GRID;
            if (kit_rec && !up_kit_wire(&r, kb)) { lg_note("  %s: unreadable, left out\n", what); continue; }
            for (uint32_t i = 0; i < P_COUNT; i++) def[i] = param_desc_of(e % NENGINES, i)->def;
            up_params(&r, v, def);
            h = lg_sound(e, v[P_E0], &f, what);
            if (f < KF_N) v[P_E0] = (int16_t)lg_kit_value(v[P_E0]);
            memset(r.packed, 0, sizeof r.packed);
            for (uint32_t i = 0; i < P_COUNT; i++) r.packed[i] = (uint8_t)(v[i] + 64);
            r.np = P_COUNT;
            if (kit_rec) { memcpy(r.tone_extra, kb, sizeof r.tone_extra); memcpy(r.packed + P_COUNT, kb + sizeof r.tone_extra, 8u); }
        }
        if (!h) { lg_note("  %s: left out (its synth is retired)\n", what); continue; }
        uint64_t uid = lg_who[h].uid;
        uint32_t place = 0;
        while (place < n && ids[place] != uid) place++;
        if (place == n) {
            if (n == V2_UP_IDS) { lg_note("  %s: left out (a bank names %u synths at most)\n", what, (unsigned)V2_UP_IDS); continue; }
            ids[n++] = uid;
        }
        r.engine = (uint8_t)(place + 1u);
        memcpy(d, &r, 160u);
        memcpy(d + 160u, r.tone_extra, 46u);
        used++;
    }
    if (!used) return 0;
    m = disk + UP_PER_BANK * 206u;
    memcpy(m, &(uint32_t){V2_UP_IDS_MAGIC}, 4);
    m[4] = (uint8_t)n;
    for (uint32_t i = 0; i < n; i++) mod_put64(m + 8u + 8u * i, ids[i]);
    return sizeof head + UP_PER_BANK * 206u + 8u + 8u * n;
}
/* ---- the user kits (DRUM KIT 9..16 -> SAMPLER KIT 1..8). 1.x struck the 909's eleven drums by the note (DRUM's
 * drum_map.c drum909_ins); 2.0's SAMPLER strikes pad n on C4 + n and any other note the pad the 808's General MIDI map
 * gives it (kits/drum_map.c drum_user_pad). Drum d's take becomes pad d (lg_kit: the eleven are the sixteen's first
 * eleven), so the lanes' notes and most of a GM beat strike the same take; a note the two maps send apart (the rides,
 * the tambourine, every note from C4) becomes C4 + d (lg_uk_note). Both maps' tables as both releases have them */
enum { LG_PUNCH, LG_ROUND, LG_SNARE, LG_CLAP, LG_HATC, LG_HATO, LG_TOM, LG_CONGA, LG_RIM, LG_CLAVE, LG_BELL, LG_CYM };
static const uint8_t LG_ROLE[47] = {                    /* GM 35..81: its role (drum_map.c DRUM_GM[][0]) */
    LG_PUNCH, LG_PUNCH, LG_RIM, LG_SNARE, LG_CLAP, LG_SNARE, LG_TOM, LG_HATC, LG_TOM, LG_HATC, LG_TOM, LG_HATO,
    LG_TOM, LG_TOM, LG_CYM, LG_TOM, LG_CYM, LG_CYM, LG_BELL, LG_HATC, LG_CYM, LG_BELL, LG_CYM, LG_CLAVE,
    LG_CYM, LG_CONGA, LG_CONGA, LG_CONGA, LG_CONGA, LG_CONGA, LG_TOM, LG_TOM, LG_BELL, LG_BELL, LG_HATC, LG_HATC,
    LG_CLAVE, LG_CLAVE, LG_HATC, LG_HATC, LG_CLAVE, LG_CLAVE, LG_CLAVE, LG_CONGA, LG_CONGA, LG_BELL, LG_BELL};
enum { LG_LKICK, LG_LSNARE, LG_LCLAP, LG_LHATC, LG_LHATO, LG_LTOM, LG_LRIM, LG_LBELL };
static const uint8_t LG_LANE[12] = {LG_LKICK, LG_LKICK, LG_LSNARE, LG_LCLAP, LG_LHATC, LG_LHATO, LG_LTOM, LG_LTOM,
                                    LG_LRIM, LG_LRIM, LG_LBELL, LG_LBELL};   /* a role's lane (DV_TYPE_LANE) */
enum { LG_BD, LG_SD, LG_LT, LG_MT, LG_HT, LG_RS, LG_CP, LG_CH, LG_OH, LG_CR, LG_RD };   /* the 909's (D9_*) */
static uint32_t lg_gm(uint32_t n) { return n >= 35u && n <= 81u ? n : 36u + (n + 120u - 36u) % 12u; }
static uint32_t lg_v1_drum(uint32_t n)                  /* 1.x: drum909_ins */
{
    if (n == 49u || n == 52u || n == 55u || n == 57u) return LG_CR;
    if (n == 51u || n == 53u || n == 59u || n == 56u) return LG_RD;
    switch (LG_LANE[LG_ROLE[lg_gm(n) - 35u]]) {
    case LG_LKICK: return LG_BD; case LG_LSNARE: return LG_SD; case LG_LCLAP: return LG_CP;
    case LG_LHATC: return LG_CH; case LG_LHATO: return LG_OH; case LG_LRIM: return LG_RS;
    default: return n < 45u ? LG_LT : n < 48u ? LG_MT : LG_HT;
    }
}
static uint32_t lg_v2_pad(uint32_t n)                   /* 2.0: drum_user_pad (the 808's instruments, then its pads) */
{
    enum { BD, SD, LT, MT, HT, LC, MC, HC, RS, CL, CP, MA, CB, CY, OH, CH, X = 255 };
    static const uint8_t GM[47] = {BD, BD, RS, SD, CP, SD, LT, CH, LT, CH, MT, OH, MT, HT, CY, HT, CY, CY, CY, MA, CY, CB,
                                   CY, X, CY, HC, MC, HC, MC, LC, X, X, X, X, MA, MA, X, X, X, X, CL, CL, CL, X, X, X, X};
    static const uint8_t OF_ROLE[12] = {BD, BD, SD, CP, CH, OH, MT, MC, RS, CL, CB, CY};
    static const uint8_t PAD[16] = {LG_BD, LG_SD, LG_LT, LG_MT, LG_HT, 11, 12, 13, LG_RS, 14, LG_CP, 15, LG_RD, LG_CR, LG_OH, LG_CH};
    if (n - 60u < 16u) return n - 60u;
    uint32_t g = lg_gm(n), k = GM[g - 35u];
    return PAD[k == X ? OF_ROLE[LG_ROLE[g - 35u]] : k];
}
static uint32_t lg_uk_note(uint32_t n) { uint32_t d = lg_v1_drum(n); return lg_v2_pad(n) == d ? n : 60u + d; }
static int lg_is_uk(uint32_t e, int32_t kit) { return e == lg_drum() && kit >= 9 && kit <= 16; }
static int lg_sound_uk(uint32_t id)                     /* a song sound (1..) is a user kit's */
{
    const song_sound_t *s = song_sound_get(id);
    return s && lg_is_uk(s->engine, (int32_t)s->p[P_E0] - 64);
}
/* the runtime's notes played by a user kit (each pattern's own sound, else its part's) as SAMPLER strikes the same
 * takes: the steps' notes (the lanes' hits strike the same pads) and the recorded notes */
static void lg_uk_notes(void)
{
    uint8_t uk[NTRK][NPAT];
    uint32_t moved = 0;
    for (uint32_t k = 0; k < NTRK; k++) {
        int part = k < NPART && lg_is_uk(trk[k].eng_req, motion_base_value(&trk[k], P_E0));
        for (uint32_t b = 0; b < NPAT; b++) {
            uint32_t id = pattern_sound_id(k, b);
            uk[k][b] = (uint8_t)(id ? lg_sound_uk(id) : part);
        }
        for (uint32_t r = 0; r < chain_config.count; r++) {   /* (a section's own sound for the track) */
            uint32_t id = chain_config.sound[r][k], b = chain_bank(r, k);
            if (id && b < NPAT && lg_sound_uk(id) != uk[k][b]) {
                lg_note("  track %u pattern %u: played by a user kit and by another sound: its notes kept\n",
                        (unsigned)k + 1u, (unsigned)b + 1u);
                uk[k][b] = 2u;
            }
        }
        for (uint32_t b = 0; b < trk_npat(k); b++) {
            if (uk[k][b] != 1u) continue;
            pattern_t p;
            uint32_t n = 0;
            pattern_read(k, b, &p);
            for (uint32_t i = 0; i < NSTEP; i++)
                for (uint32_t j = 0; j < p.step[i].n && j < 4u; j++) {
                    uint32_t v = lg_uk_note(p.step[i].note[j]);
                    n += v != p.step[i].note[j];
                    p.step[i].note[j] = (uint8_t)v;
                }
            if (n && !pattern_write(k, b, &p)) lg_note("  track %u pattern %u: its notes not moved\n", (unsigned)k + 1u, (unsigned)b + 1u);
            else moved += n;
        }
    }
    for (uint32_t i = 0; i < RECORD_MAX; i++) {
        recorded_note_t *r = &recording[i];
        uint32_t o = recorded_owner(r);
        if (!r->vel || o >= NTRK * NPAT || uk[o / NPAT][o % NPAT] != 1u) continue;
        uint32_t v = lg_uk_note(recorded_note(r));
        if (v != recorded_note(r)) { recorded_set_note(r, v); moved++; }
    }
    recording_reindex();
    if (moved) lg_note("  %u user kit note(s) moved to the pads' C4 .. D#5 (the same drums as in 1.x)\n", (unsigned)moved);
}
/* (the agogos and triangles, GM 67, 68, 80, 81, struck 1.x's high tom on the bell lane's DRUM MIX level; C4 + 4 is on
 * the tom lane's) */

/* a user kit: 1.x's KIT1 record (backup objects 92..99: its payload, slice k at byte 8 k) -> 2.0's KIT2 blob (a head,
 * then a PAD2 record a pad: drum d's take on pad d, its map's settings with it). out: at least 16 x the input; its
 * bytes, 0: no take (the kit empty), -1 not a kit 1.x played */
#define V1_KIT_MAGIC 0x3154494Bu                         /* KIT1 (DRUM's drum_kit.h, frozen) */
#define V1_KIT_BYTES 616u
#define V2_PAD_MAGIC 0x32444150u                         /* PAD2 (SAMPLER's kits/drum_kit.h drk_pad_t) */
#define V2_PAD_BYTES 72u
#define V2_BOX_MAGIC 0x3254494Bu                         /* KIT2 (drk_box_t) */
#define V2_BOX_BYTES 88u
static uint32_t lg_r16(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 8; }
static uint32_t lg_r32(const uint8_t *b) { return lg_r16(b) | lg_r16(b + 2) << 16; }
static void lg_w16(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void lg_w32(uint8_t *b, uint32_t v) { lg_w16(b, v); lg_w16(b + 2, v >> 16); }
static int32_t lg_kit(const uint8_t *in, uint32_t n, uint32_t slot, uint8_t *out)
{
    static const uint8_t NS[4] = {58, 29, 19, 14};      /* samples per slice of each mode (drum_kit.h DRK_NS) */
    const uint8_t *map = in + 20u, *smp = in + 130u, *loops = in + 264u;
    uint32_t nsmp = n >= V1_KIT_BYTES ? in[6] : 0u, at = V2_BOX_BYTES, used = 0, spare = 11u;
    if (n < V1_KIT_BYTES || (n & 7u) || lg_r32(in) != V1_KIT_MAGIC || lg_r16(in + 4) != V1_KIT_BYTES || in[7] != 11u ||
        nsmp > 11u)
        return -1;
    memset(out, 0, V2_BOX_BYTES);
    lg_w32(out, V2_BOX_MAGIC); out[4] = (uint8_t)V2_BOX_BYTES; out[6] = 16u;
    {                                                   /* its name, but 1.x's "KIT n" for its own slot: none */
        char def[8];
        snprintf(def, sizeof def, "KIT %u", (unsigned)slot + 1u);
        if (strncmp((const char *)in + 8, def, 12)) memcpy(out + 8, in + 8, 12);
    }
    for (uint32_t pass = 0; pass < 2u; pass++)
        for (uint32_t d = 0; d < 11u; d++) {
            const uint8_t *m = map + 10u * d;
            uint32_t take = m[pass], pad = pass ? spare : d;
            if (take == 0xFFu) continue;
            if (take >= nsmp) return -1;
            if (pass) {                                 /* (an alternate take, 1.x's closed hats': a pad of its own) */
                if (spare >= 16u) { lg_note("  kit %u: an alternate take left out (no pad free)\n", (unsigned)slot + 1u); continue; }
                lg_note("  kit %u: drum %u's alternate take on pad %u (it no longer alternates)\n", (unsigned)slot + 1u, (unsigned)d + 1u, (unsigned)pad + 1u);
                spare++;
            }
            const uint8_t *s = smp + 12u * take;
            uint32_t first = lg_r16(s), ns = lg_r16(s + 2), loop_at = lg_r16(s + 4), loop_slice = lg_r16(s + 6);
            uint32_t k = first, idx = 0, st = s[11];
            if (first < V1_KIT_BYTES / 8u || !ns || st >= nsmp) return -1;
            while (idx < ns) {                          /* (its slices, walked by their modes) */
                if (8u * k + 8u > n) return -1;
                idx += NS[in[8u * k + 3u] >> 6];
                k++;
            }
            uint8_t *h = out + at;
            memset(h, 0, V2_PAD_BYTES);
            lg_w32(h, V2_PAD_MAGIC); lg_w16(h + 4, V2_PAD_BYTES); h[6] = (uint8_t)pad; h[7] = 0;
            lg_w32(h + 8, ns); lg_w32(h + 12, loop_at); lg_w32(h + 16, loop_slice);
            lg_w16(h + 20, lg_r16(s + 8));              /* inc */
            lg_w16(h + 22, 1024u);                      /* gain: unity (Q10), as 1.x played it */
            memcpy(h + 24, m + 4, 6);                   /* out, ms, pivot */
            h[30] = m[2]; h[31] = m[3];                 /* st, tone */
            h[32] = s[10];                              /* dark */
            memcpy(h + 40, loops + 32u * st, 32);       /* the predictor at its loop */
            memcpy(h + V2_PAD_BYTES, in + 8u * first, 8u * (k - first));
            lg_w32(out + 20u + 4u * pad, V2_PAD_BYTES + 8u * (k - first));
            at += V2_PAD_BYTES + 8u * (k - first);
            used++;
        }
    return used ? (int32_t)at : 0;
}

/* a 1.x project object (any form its readers take) -> a 2.0 FBKS record in out (BANK_STORE_SIZE): rc 0 ok */
static uint32_t lg_project(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t rc = lg_bk_put(0, in, n);                  /* (into the runtime, as a backup's restore puts it) */
    if (rc) return rc;
    core_poll(50);
    lg_uk_notes();
    project_capture(&proj_scratch);
    if (!bank_pack(out, &proj_scratch, 1)) return 0x80u;
    return lg_fbks(out) ? 0x81u : 0u;
}

/* ---- the converter's calls (web/convert/convert.mjs; the native tests' lg_convert_dir makes the same): a 1.x backup's
 * objects go in one at a time at cv_in, in this order: the projects (2..5, 64..91, then 0), the preset banks (6..28:
 * every one in with cv_bank_in, then cv_banks, then cv_bank for each id), the kits (92..99), the settings (1). A call's
 * 2.0 object is left at cv_out, its length the call's result. (Not scratch: the backup client's replies land there.) */
#define CV_MAX 0x20000u
static uint8_t cv_inb[CV_MAX], cv_outb[CV_MAX];
_Static_assert(BANK_STORE_SIZE <= CV_MAX && sizeof(persist_t) + sizeof(v2_tmpl_t) <= CV_MAX, "cv_out: a 2.0 object fits");
EXPORT(cv_in) uint8_t *cv_in(void) { return cv_inb; }
EXPORT(cv_in_size) uint32_t cv_in_size(void) { return CV_MAX; }
EXPORT(cv_out) uint8_t *cv_out(void) { return cv_outb; }
EXPORT(cv_report) const char *cv_report(void) { return lg_rep; }
EXPORT(cv_report_len) uint32_t cv_report_len(void) { return lg_rep_n; }
/* project id (n bytes) -> its FBKS record: its length; 0: 1.x refused it */
EXPORT(cv_project) uint32_t cv_project(uint32_t id, uint32_t n)
{
    lg_note("project %u\n", (unsigned)id);
    uint32_t rc = n <= CV_MAX ? lg_project(cv_inb, n, cv_outb) : 0x82u;
    if (rc) lg_note("  1.x refused it (%#x): left out\n", (unsigned)rc);
    return rc ? 0u : BANK_STORE_SIZE;
}
/* preset bank id (n bytes) into the 1.x runtime: rc 0 taken */
EXPORT(cv_bank_in) uint32_t cv_bank_in(uint32_t id, uint32_t n)
{
    uint32_t rc = id >= 6u && id <= 28u && n <= CV_MAX ? lg_bk_put(id, cv_inb, n) : 0x82u;
    if (rc) lg_note("bank %u: 1.x refused it (%#x): left out\n", (unsigned)id, (unsigned)rc);
    return rc;
}
EXPORT(cv_banks) void cv_banks(void) { up_cache_reset(); up_boot(); core_poll(200); }   /* (as 1.x reads them, together) */
/* preset bank id as 1.x read it -> UPB3: its length, 0 an empty bank (or no bank there) */
EXPORT(cv_bank) uint32_t cv_bank(uint32_t id)
{
    uint32_t b = ed_bk_preset_bank(id);
    if (b >= UP_BANKS) return 0;
    lg_note("bank %u\n", (unsigned)id);
    return lg_bank(b, cv_outb);
}
/* kit id (92..99, n bytes) -> its KIT2 box: its length; 0 a kit with no takes; -1 not a kit 1.x played */
EXPORT(cv_kit) int32_t cv_kit(uint32_t id, uint32_t n)
{
    lg_note("kit %u\n", (unsigned)id - 91u);
    int32_t m = id >= 92u && id <= 99u && n <= 65536u ? lg_kit(cv_inb, n, id - 92u, cv_outb) : -1;
    if (m < 0) lg_note("  not a kit Melodee 1.x played: left out\n");
    return m;
}
/* the settings (id 1, n bytes) -> PER6 (+ TPLM): their length; 0 refused */
EXPORT(cv_settings) uint32_t cv_settings(uint32_t n)
{
    lg_note("settings\n");
    return n <= CV_MAX ? lg_settings(cv_inb, n, cv_outb) : 0u;
}

#ifdef LEGACY_NATIVE_MAIN
#define PP_ID(t) ((t)->eng_req == lg_drum() ? V2_KIT[lg_kit_family((t)->p[P_E0])].uid : (t)->eng_req == ENGI_MIDI ? V2_ID_MIDI : eng_map[(t)->eng_req].uid)
#define PP_E0(t, v) ((t)->eng_req == lg_drum() ? lg_kit_value(v) : (v))
#include "preset_play.h"
extern const mod_desc_v3 mod_sid, mod_drum, mod_cz, mod_fm6, mod_p5;   /* (the module objects, linked beside) */
static int lg_boot_modules(const char *dir)
{
    static const char *const stem[] = {"sid", "drum", "cz", "fm6", "prophet"};
    static const mod_desc_v3 *const desc[] = {&mod_sid, &mod_drum, &mod_cz, &mod_fm6, &mod_p5};
    core_boot(0);
    core_poll(100);
    for (uint32_t i = 0; i < 5u; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.mmod", dir, stem[i]);
        FILE *f = fopen(path, "rb");
        if (!f) { printf("legacy: no %s\n", path); return 1; }
        uint32_t n = (uint32_t)fread(scratch, 1, sizeof scratch, f);
        fclose(f);
        int32_t rc = 0;
        if (core_mod_link(desc[i], core_head_uid(scratch, n)) || (rc = core_mod_install(scratch, n)) < 0) { printf("legacy: %s not installed (%d)\n", path, (int)rc); return 1; }
    }
    core_poll(100);
    core_boot(1);
    core_poll(100);
    return 0;
}
static uint8_t lg_buf[1u << 20];
static uint32_t lg_read(const char *path, uint8_t *b, uint32_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t n = (uint32_t)fread(b, 1, max, f);
    fclose(f);
    return n;
}
static void lg_write(const char *dir, const char *name, const void *p, uint32_t n)
{
    char path[600];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(p, 1, n, f); fclose(f); }
}

/* ---- the tests' 1.x projects (tests/convert_test.sh): made here by 1.x's own code, as a device running 1.x would */
static uint32_t fx_lcg;
static uint32_t fx_rnd(uint32_t n) { fx_lcg = fx_lcg * 1664525u + 1013904223u; return (fx_lcg >> 8) % n; }
static void fx_fill(uint32_t k, uint32_t b, uint32_t density, int drum)   /* a scale walk, chords, ties; a drum grid */
{
    static const uint8_t sc[7] = {0, 2, 3, 5, 7, 8, 10};
    pattern_t p;
    pattern_read(k, b, &p);
    for (uint32_t i = 0; i < NSTEP; i++) {
        step_t s = {{0}, 0, ST_REST};
        if (fx_rnd(100) < density) {
            if (drum) {
                static const uint8_t gm[8] = {36, 38, 42, 46, 39, 45, 37, 49};
                s.time = ST_NOTE; s.n = 1; s.note[0] = gm[fx_rnd(8)]; s.vel = (uint8_t)(90u + fx_rnd(30));
            } else {
                s.time = fx_rnd(8) ? ST_NOTE : ST_TIE; s.n = s.time == ST_NOTE ? 1u + (fx_rnd(5) == 0) * 2u : 0;
                for (uint32_t j = 0; j < s.n; j++) s.note[j] = (uint8_t)(36u + 12u * (k % 3u) + sc[(i + 2u * j + b) % 7u] + 12u * fx_rnd(2));
                s.vel = (uint8_t)(80u + fx_rnd(40)); s.flags = fx_rnd(6) ? 0 : SF_ACCENT;
            }
        }
        p.step[i] = s;
    }
    p.timing[0] = 16;
    (void)pattern_write(k, b, &p);
}
static void fx_song(uint32_t rows, uint32_t pats)
{
    chain_config.count = (uint8_t)rows;
    for (uint32_t r = 0; r < rows; r++) {
        chain_config.row[r] = (chain_row_t){(uint8_t)(r % pats), 1};
        for (uint32_t k = 0; k < NTRK; k++) chain_patterns[r][k] = (uint8_t)((r + k) % pats);
        chain_config.row[r].slot = chain_patterns[r][0];
    }
}
static uint32_t fx_snap(uint32_t k, const char *name)   /* track k's sound as it is: a sound of the song's */
{
    song_sound_t s;
    song_sound_capture(&s, &trk[k], name);
    return song_sound_store(&s);
}
static void fx_part(uint32_t k, uint32_t e, uint32_t preset, int32_t kit)
{
    set_engine_of(&trk[k], e);
    apply_preset_to(&trk[k], preset);
    if (kit >= 0) trk[k].p[P_E0] = (int16_t)kit;
    core_poll(5);
}
static void fx_native(uint32_t k, uint32_t bank, uint32_t step, uint32_t slot, uint32_t v)   /* a synth's own byte moved */
{
    uint32_t tag = motion_native_tag(&trk[k]);
    if (tag) (void)motion_set_event_at(&trk[k], bank, step, MO_NATIVE + slot, (int16_t)(tag << 8 | v));
}
/* a user kit's pattern: the lanes' hits, notes 1.x and 2.0 send to the same drum and notes they send apart */
static void fx_fill_uk(uint32_t k, uint32_t b)
{
    static const uint8_t gm[] = {36, 38, 41, 42, 45, 46, 49, 51, 53, 54, 56, 58, 59, 60, 62, 64, 66, 69, 70, 72, 75, 80};
    pattern_t p;
    pattern_read(k, b, &p);
    for (uint32_t i = 0; i < NSTEP; i++) {
        step_t s = {{0}, 0, ST_REST};
        uint32_t r = fx_rnd(100);
        if (r < 35u) {
            s.time = ST_NOTE; s.hit = (uint8_t)(1u << fx_rnd(8) | (fx_rnd(3) ? 0u : 1u << fx_rnd(8))); s.vel = (uint8_t)(90u + fx_rnd(30));
        } else if (r < 70u) {
            s.time = ST_NOTE; s.n = 1u + (fx_rnd(4) == 0); s.vel = (uint8_t)(70u + fx_rnd(50));
            for (uint32_t j = 0; j < s.n; j++) s.note[j] = gm[fx_rnd(sizeof gm)];
        }
        p.step[i] = s;
    }
    p.timing[0] = 16;
    (void)pattern_write(k, b, &p);
}
/* a recorded note on track k's playing pattern: step, its onset in the step (RECORD_UNIT), note, velocity */
static void fx_rec(uint32_t k, uint32_t step, uint32_t on, uint32_t note, uint32_t vel)
{
    uint32_t i = 0;
    while (i < RECORD_MAX && recording[i].vel) i++;
    if (i == RECORD_MAX) return;
    recorded_note_t r = {.on = (uint16_t)on, .duration = 30000, .note7 = (uint8_t)note, .vel = (uint8_t)vel};
    recorded_set_owner(&r, k * NPAT + trk[k].pattern);
    recorded_set_step(&r, step, 0, 0);
    recording[i] = r;
    step_t *s = &trk[k].step[step];
    s->time = ST_NOTE; s->flags |= SF_RECORDED; s->hit = s->acc = 0;
    if (s->n < 4u) s->note[s->n++] = (uint8_t)note;
    if (!s->vel) s->vel = (uint8_t)vel;
}
/* fixture n into the runtime: 0 none left */
static const char *fx_make(uint32_t n)
{
    project_new_blank();
    core_poll(20);
    fx_lcg = 11u + n;
    switch (n) {
    case 0:                                             /* Melodee's own synths, their sounds, their own Motion */
        fx_part(0, ENGI_PROPHET, 3, -1); fx_part(1, ENGI_FM6, 5, -1); fx_part(2, ENGI_CZ, 7, -1); fx_part(3, ENGI_SID, 2, -1);
        for (uint32_t k = 0; k < NPART; k++) for (uint32_t b = 0; b < 4u; b++) fx_fill(k, b, 45, 0);
        fx_song(4, 4);
        {
            uint32_t a, b, c;
            apply_preset_to(&trk[0], 9); a = fx_snap(0, "P5 B"); apply_preset_to(&trk[0], 3);
            apply_preset_to(&trk[2], 11); b = fx_snap(2, "CZ B"); apply_preset_to(&trk[2], 7);
            apply_preset_to(&trk[3], 6); c = fx_snap(3, "SID B"); apply_preset_to(&trk[3], 2);
            chain_config.sound[1][0] = (uint8_t)a; chain_config.sound[2][2] = (uint8_t)b; pattern_sound[3][3] = (uint8_t)c;
        }
        for (uint32_t k = 0; k < 3u; k++) { fx_native(k, k, 4u + k, 3u + k, 30u + 20u * k); fx_native(k, 0, 9, 1, 90); }
        (void)motion_set_event_at(&trk[1], 1, 2, P_E1, 40);
        str_cpy(proj_name, "SYNTHS", sizeof proj_name);
        return "synths";
    case 1:                                             /* DRUM's kits on parts, in sounds, KIT moving */
        fx_part(0, ENGI_DRUM, 0, 4); fx_part(1, ENGI_DRUM, 1, 5); fx_part(2, ENGI_DRUM, 4, 7); fx_part(3, ENGI_DRUM, 2, 6);
        for (uint32_t k = 0; k < NPART; k++) for (uint32_t b = 0; b < 4u; b++) fx_fill(k, b, 40, 1);
        fx_song(4, 4);
        {
            uint32_t a, b;
            trk[3].p[P_E0] = 6; apply_preset_to(&trk[3], 3); a = fx_snap(3, "SID DIGI"); apply_preset_to(&trk[3], 2);
            fx_part(2, ENGI_DRUM, 5, 8); b = fx_snap(2, "FM KIT"); fx_part(2, ENGI_DRUM, 4, 7);
            chain_config.sound[2][3] = (uint8_t)a; pattern_sound[2][1] = (uint8_t)b;
        }
        (void)motion_set_event_at(&trk[0], 0, 8, P_E0, 3);    /* KIT 3: the 808's family still */
        (void)motion_set_event_at(&trk[1], 2, 6, P_E1, 50);   /* the 909's TUNE */
        str_cpy(proj_name, "KITS", sizeof proj_name);
        return "kits";
    case 2:                                             /* MIDI: a part and the MIDI-only tracks */
        song.ntrk = NTRK;
        fx_part(0, ENGI_PROPHET, 1, -1); fx_part(1, ENGI_MIDI, 0, -1); fx_part(2, ENGI_FM6, 2, -1); fx_part(3, ENGI_DRUM, 0, 4);
        for (uint32_t k = 0; k < NTRK; k++) for (uint32_t b = 0; b < 2u; b++) fx_fill(k, b, 35, k == 3u);
        fx_song(2, 2);
        str_cpy(proj_name, "MIDI", sizeof proj_name);
        return "midi";
    case 3:                                             /* KIT moving to another kit: left out (another synth in 2.0) */
        fx_part(0, ENGI_DRUM, 0, 4); fx_part(1, ENGI_PROPHET, 2, -1);
        for (uint32_t b = 0; b < 2u; b++) { fx_fill(0, b, 40, 1); fx_fill(1, b, 40, 0); }
        fx_song(2, 2);
        (void)motion_set_event_at(&trk[0], 1, 4, P_E0, 5);
        str_cpy(proj_name, "KIT JUMP", sizeof proj_name);
        return "kitjump";
    case 4:                                             /* the user kits (OUT/92.bin, 93.bin in first): SAMPLER's */
        if (!rec_tab[0].n || !rec_tab[1].n) return "";  /* (no kits: none) */
        fx_part(0, lg_drum(), 0, 9); fx_part(1, lg_drum(), 0, 10); fx_part(2, lg_drum(), 0, 4); fx_part(3, ENGI_PROPHET, 2, -1);
        for (uint32_t b = 0; b < 3u; b++) { fx_fill_uk(0, b); fx_fill_uk(1, b); fx_fill_uk(2, b); fx_fill(3, b, 40, 0); }
        fx_song(3, 3);
        {                                               /* the 808 track's third pattern: KIT 1's (its notes move) */
            uint32_t a;
            trk[2].p[P_E0] = 9; a = fx_snap(2, "MY KIT"); trk[2].p[P_E0] = 4;
            pattern_sound[2][2] = (uint8_t)a;
        }
        fx_rec(0, 1, 0, 51, 100); fx_rec(0, 5, 20000, 54, 90); fx_rec(0, 9, 40000, 62, 110); fx_rec(0, 13, 0, 36, 120);
        fx_rec(1, 3, 10000, 51, 100); fx_rec(1, 11, 0, 75, 100);
        recording_reindex();
        (void)motion_set_event_at(&trk[0], 1, 8, P_E0, 10);    /* KIT 1 -> KIT 2: SAMPLER's still */
        str_cpy(proj_name, "USER KITS", sizeof proj_name);
        return "userkit";
    }
    return 0;
}
/* DIR/<id>.bin (92..99: a 1.x backup's user kits) into the 1.x runtime as a backup's restore puts them: kits put */
static uint32_t lg_kits_in(const char *dir)
{
    uint32_t any = 0;
    for (uint32_t id = 92; id <= 99u; id++) {
        char path[600];
        snprintf(path, sizeof path, "%s/%u.bin", dir, (unsigned)id);
        uint32_t n = lg_read(path, lg_buf, sizeof lg_buf), rc;
        if (!n) continue;
        if ((rc = lg_bk_put(id, lg_buf, n)) != 0) printf("kit %u: 1.x refused it (%#x)\n", (unsigned)id, (unsigned)rc);
        else any++;
    }
    core_poll(20);
    return any;
}
/* DIR/<id>.bin (92..99) -> OUT/v2-<id>.bin (KIT2); 1: one not a kit */
static int lg_kits_convert(const char *dir, const char *out)
{
    int bad = 0;
    for (uint32_t id = 92; id <= 99u; id++) {
        char path[600], nm[32];
        snprintf(path, sizeof path, "%s/%u.bin", dir, (unsigned)id);
        uint32_t n = lg_read(path, cv_inb, CV_MAX);
        if (!n) continue;
        int32_t m = cv_kit(id, n);
        printf("kit %u: %u B -> %d B\n", (unsigned)id, (unsigned)n, (int)m);
        if (m < 0) { bad = 1; continue; }
        snprintf(nm, sizeof nm, "v2-%u.bin", (unsigned)id);
        lg_write(out, nm, cv_outb, (uint32_t)m);
    }
    return bad;
}
/* tests' fixtures: OUT/v1-<name>.rec (the 1.x record, its capture) and OUT/v2-<name>.rec (converted); the user kits
 * OUT/92.bin, 93.bin (kit_fixture) in first and converted to OUT/v2-92.bin, v2-93.bin */
static int lg_fixtures(const char *out)
{
    static uint8_t v1[BANK_STORE_SIZE], v2[BANK_STORE_SIZE];
    const char *name;
    if (lg_kits_in(out) && lg_kits_convert(out, out)) return 1;
    for (uint32_t n = 0; (name = fx_make(n)) != 0; n++) {
        char nm[64];
        if (!*name) continue;
        core_poll(30);
        project_capture(&proj_scratch);
        if (!bank_pack(v1, &proj_scratch, 1)) { printf("fixture %s: 1.x capture failed\n", name); return 1; }
        snprintf(nm, sizeof nm, "v1-%s.rec", name); lg_write(out, nm, v1, sizeof v1);
        lg_note("fixture %s\n", name);
        uint32_t rc = lg_project(v1, sizeof v1, v2);
        printf("fixture %s: %.4s -> rc %#x\n", name, (const char *)v1, (unsigned)rc);
        if (rc) return 1;
        snprintf(nm, sizeof nm, "v2-%s.rec", name); lg_write(out, nm, v2, sizeof v2);
    }
    printf("%s", lg_rep);
    return 0;
}
/* the tests' 1.x store (OUT/<id>.bin, as a 1.x backup): user presets of every form in several banks, factory and user
 * stars, a voice budget, a template naming DRUM's CR78 */
static int lg_store_fixture(const char *out)
{
    static const struct { uint16_t slot; uint8_t e, preset; int8_t kit; const char *name; } UP[] = {
        {0, ENGI_PROPHET, 4, -1, "MY P5"}, {1, ENGI_FM6, 3, -1, "MY FM6"}, {2, ENGI_CZ, 5, -1, "MY CZ"},
        {3, ENGI_SID, 2, -1, "MY SID"}, {4, 0, 1, 5, "MY 909"}, {20, 0, 3, 6, "MY DIGI"}, {40, 0, 0, 10, "MY KIT 2"},
        {80, ENGI_PROPHET, 7, -1, "P5 B4"}, {150, ENGI_CZ, 9, -1, "CZ B8"}, {300, ENGI_FM6, 6, -1, "FM6 B16"},
        {380, 0, 0, 2, "OLD 808"},
    };
    project_new_blank();
    core_poll(20);
    for (uint32_t i = 0; i < sizeof UP / sizeof *UP; i++) {
        uint32_t e = UP[i].e ? UP[i].e : lg_drum();
        fx_part(0, e, UP[i].preset, UP[i].kit);
        if (up_store_to(UP[i].slot, 0, UP[i].name)) { printf("store: preset %s not stored\n", UP[i].name); return 1; }
        core_poll(10);
    }
    favorite_set(ENGI_PROPHET, 5, 1); favorite_set(ENGI_PROPHET, 200, 1); favorite_set(ENGI_FM6, 2, 1);
    favorite_set(ENGI_CZ, 10, 1); favorite_set(ENGI_SID, 1, 1); favorite_set(lg_drum(), 1, 1); favorite_set(lg_drum(), 3, 1);
    up_favorite_set(2, 1);
    settings_budget = 20;
    fx_part(0, ENGI_PROPHET, 2, -1); fx_part(1, ENGI_CZ, 4, -1); fx_part(2, ENGI_SID, 1, -1); fx_part(3, lg_drum(), 4, 7);
    template_save();
    settings_save();
    core_poll(500);
    if (lg_bk_list()) { printf("store: LIST failed\n"); return 1; }
    for (uint32_t id = 1; id <= 28u; id++) {
        uint32_t rc, n = lg_list[id] > 1u ? lg_bk_get(id, lg_buf, sizeof lg_buf, &rc) : 0u;
        char nm[16];
        if (!n) continue;
        snprintf(nm, sizeof nm, "%u.bin", (unsigned)id); lg_write(out, nm, lg_buf, n);
        printf("store: %u (%u B) %.4s\n", (unsigned)id, (unsigned)n, (const char *)lg_buf);
    }
    return 0;
}
static int lg_render(const char *rec, uint32_t secs, const char *out, const char *kits)
{
    if (kits) lg_kits_in(kits);
    uint32_t n = lg_read(rec, lg_buf, sizeof lg_buf), rc;
    if ((rc = core_load_record(lg_buf, n, 0)) != 0) { printf("legacy: load %u\n", (unsigned)rc); return 1; }
    core_poll(30);
    if ((rc = core_song_prepare(0)) != 0) { printf("legacy: song %u\n", (unsigned)rc); return 1; }
    FILE *f = fopen(out, "wb");
    if (!f) return 2;
    for (uint32_t left = secs * FS; left;) {
        uint32_t k = left > 1024u ? 1024u : left;
        fwrite(core_render(k), sizeof(float), 2u * k, f);
        left -= k;
    }
    fclose(f);
    return 0;
}
/* a 1.x backup (BACKUPDIR/<id>.bin, as tools/prophet_device.py backup writes it) -> OUT/v2-<id>.bin, OUT/report.txt:
 * the calls convert.mjs makes, in its order */
static int lg_convert_dir(const char *in, const char *out)
{
    static const uint8_t pids[] = {2, 3, 4, 5, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84,
                                   85, 86, 87, 88, 89, 90, 91, 0};
    uint32_t bad = 0, any = 0;
    char path[600], nm[32];
    for (uint32_t i = 0; i < sizeof pids; i++) {
        snprintf(path, sizeof path, "%s/%u.bin", in, (unsigned)pids[i]);
        uint32_t n = lg_read(path, cv_inb, CV_MAX), m;
        if (!n) continue;
        m = cv_project(pids[i], n);
        printf("project %u: %u B\n", (unsigned)pids[i], (unsigned)m);
        bad += !m;
        snprintf(nm, sizeof nm, "v2-%u.bin", (unsigned)pids[i]);
        if (m) lg_write(out, nm, cv_outb, m);
    }
    for (uint32_t id = 6; id <= 28u; id++) {             /* the banks: all in first (1.x reads them together), then each */
        snprintf(path, sizeof path, "%s/%u.bin", in, (unsigned)id);
        uint32_t n = lg_read(path, cv_inb, CV_MAX);
        if (!n) continue;
        bad += cv_bank_in(id, n) != 0;
        any = 1;
    }
    if (any) cv_banks();
    for (uint32_t id = 6; any && id <= 28u; id++) {
        uint32_t n = cv_bank(id);
        if (!n) continue;
        snprintf(nm, sizeof nm, "v2-%u.bin", (unsigned)id);
        lg_write(out, nm, cv_outb, n);
        printf("bank %u: %u B\n", (unsigned)id, (unsigned)n);
    }
    bad += lg_kits_convert(in, out);
    snprintf(path, sizeof path, "%s/1.bin", in);
    uint32_t n = lg_read(path, cv_inb, CV_MAX);
    if (n) {
        uint32_t m = cv_settings(n);
        printf("settings: %u B -> %u B\n", (unsigned)n, (unsigned)m);
        if (m) lg_write(out, "v2-1.bin", cv_outb, m); else bad++;
    }
    lg_write(out, "report.txt", lg_rep, lg_rep_n);
    printf("%s", lg_rep);
    return bad != 0;
}
int main(int argc, char **argv)
{
    if (argc < 3 || lg_boot_modules(argv[1])) {
        printf("usage: legacy_native MODDIR convert BACKUPDIR OUT | fixtures OUT | store OUT | render REC S OUT [KITDIR]\n");
        return 2;
    }
    if (argc == 5 && !strcmp(argv[2], "convert")) return lg_convert_dir(argv[3], argv[4]);
    if (argc == 4 && !strcmp(argv[2], "fixtures")) return lg_fixtures(argv[3]);
    if (argc == 4 && !strcmp(argv[2], "store")) return lg_store_fixture(argv[3]);
    if (argc == 5 && !strcmp(argv[2], "presets")) {     /* a 1.x backup's banks in, each user preset played, the stars */
        for (uint32_t id = 6; id <= 28u; id++) {
            char path[600];
            snprintf(path, sizeof path, "%s/%u.bin", argv[3], (unsigned)id);
            uint32_t n = lg_read(path, lg_buf, sizeof lg_buf);
            if (n && lg_bk_put(id, lg_buf, n)) { printf("legacy: put %u refused\n", (unsigned)id); return 1; }
        }
        up_cache_reset(); up_boot(); core_poll(200);
        for (uint32_t k = 0; k < UP_SLOTS; k++)
            if (up_used(k) && pp_state(k, stdout)) printf("preset %u: not loaded\n", (unsigned)k);
        return 0;
    }
    if ((argc == 6 || argc == 7) && !strcmp(argv[2], "render"))   /* (KITDIR: its 92..99.bin in first) */
        return lg_render(argv[3], (uint32_t)atoi(argv[4]), argv[5], argc == 7 ? argv[6] : 0);
    return 2;
}
#endif
