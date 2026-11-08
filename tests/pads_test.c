/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The pads capability (mod_schema.h MOD3_R_PADS, firmware/src/pads.c) against what the firmware knew of DRUM before it
 * (2.0: each KIT value played by its own kit synth, 808 / 909 / SID KIT / CR78 / FM KIT / SAMPLER, the same rows):
 * tests/fixtures/pads-70111e91-fm1.txt (-smk37.txt) was written from 70111e91's own drum_map.c and ui_draw.c (every
 * KIT value 0..16: the pads' notes, names, abbreviations, third labels and knob order, the MPC pads, the lanes' names,
 * abbreviations and notes, each note's lane and pad, a user kit's keys; KIT 9..16 rewritten when SAMPLER's kits took the
 * 808's sixteen pads, PADS_WRITE=1). Here DRUM's pads block, read by the firmware
 * as any synth's, gives the same, line for line; a synth without pads is never a kit; the stored packing (MOD3_R_PACK)
 * is Melodee 1.x's KIT wire; and each mutation of the block (a note's lane, a map's selector, an MPC pad, a name, a
 * label, the record names, the lanes' notes, a flag) is caught. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

static char got[64 * 1024];
static uint32_t gn;
static void out(const char *f, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void out(const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(got + gn, sizeof got - gn, f, ap);
    va_end(ap);
    if (n > 0) gn += (uint32_t)n < sizeof got - gn ? (uint32_t)n : 0u;
}
/* the facts as the firmware has them now, in the fixture's words */
static void facts(void)
{
    track_t *t = &trk[3];
    char lb[MOD_SCH_NAME + 1u];
    uint32_t was_e = t->eng_req;
    int16_t was_k = t->p[P_E0];
    gn = 0;
    got[0] = 0;
    for (int k = 0; k <= 16; k++) {
        t->engine = t->eng_req = (uint8_t)kit_engine(k);     /* (KIT k: the kit synth that plays it, 2.0) */
        t->p[P_E0] = (int16_t)k;
        uint32_t n = pads_count(t);
        out("kit %d pads %u\n", k, n);
        for (uint32_t s = 0; s < n; s++)
            out("pad %u %u %s %s %s %u\n", s, pads_note(t, s), pads_name(t, s), pads_abbr(t, s), pads_par_label(t, s, 2, lb),
                pads_step(t, s, 1));
        for (uint32_t s = 0; s < n; s++) out("down %u %u %u\n", s, pads_step(t, s, -1), pads_step(t, s, 5));
        out("mpc"); for (uint32_t p = 0; p < 16; p++) out(" %u", pads_mpc_note(t, p)); out("\n");
        out("lanes"); for (uint32_t l = 0; l < 8; l++) out(" %s/%s/%u", lane_name(t, l), lane_abbr(t, l), lane_note(t, l)); out("\n");
        out("notes"); for (uint32_t m = 0; m < 128; m++) out(" %u:%u", pads_lane(t, m), pads_pad_of(t, m)); out("\n");
        out("user %d keys", pads_keymap(t));     /* (a user kit: the synth's own keys, every one; else its GM map) */
        for (int ks = -5; ks < 32; ks++)
            out(" %u", (unsigned)(pads_keymap(t) ? (uint32_t)ENGINES[t->eng_req]->keys(t, (uint32_t)ks) & 255u : drum_user_key(t, ks)));
        out("\n");
    }
    t->engine = t->eng_req = (uint8_t)was_e;
    t->p[P_E0] = was_k;
}
static char want[64 * 1024];
static uint32_t wn;
static int fails;
static void check(const char *s, int ok) { printf("pads: %-100s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }
static uint8_t *drum_schema(uint32_t e, uint32_t *len) { return (uint8_t *)(uintptr_t)sch_builtin(e, len); }
static uint32_t block_at(uint32_t e)             /* the pads block's offset in kit synth e's schema */
{
    mod_sch_head h;
    uint32_t id, f, l;
    assert(!sch_head(e, &h) && !sch_role(e, MOD3_R_PADS, 0, &id, &f, &l));
    return h.wid + id;
}
static int same(void) { sch_gen++; facts(); return gn == wn && !memcmp(got, want, wn); }
/* byte at of kit synth e's schema xor'd with x: the facts no longer the fixture's (or the pads gone); restored */
static int caught(uint32_t e, uint32_t at, uint8_t x)
{
    uint32_t len;
    uint8_t *b = drum_schema(e, &len);
    assert(at < len);
    b[at] ^= x;
    int was = !same() || !pads_on(&trk[3]);
    b[at] ^= x;
    return was && same();
}
int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : (BOARD_SMK37 ? "tests/fixtures/pads-70111e91-smk37.txt" : "tests/fixtures/pads-70111e91-fm1.txt"), "rb");
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!f) { printf("pads: no fixture\n"); return 1; }
    wn = (uint32_t)fread(want, 1, sizeof want - 1u, f);
    fclose(f);
    host_tracks_init();
    host_preset(&trk[3], ENGI_808, 0);
    facts();
    if (getenv("PADS_WRITE")) {                 /* (the fixture written from the firmware as it is now: a deliberate change) */
        FILE *w = fopen(argc > 1 ? argv[1] : (BOARD_SMK37 ? "tests/fixtures/pads-70111e91-smk37.txt" : "tests/fixtures/pads-70111e91-fm1.txt"), "wb");
        if (!w || fwrite(got, 1, gn, w) != gn) return 1;
        fclose(w);
        printf("pads: fixture written\n");
        return 0;
    }
    check("the kit synths' pads blocks as the firmware reads any synth's: every KIT's pads, notes, names, labels, knob order, MPC pads, "
          "lanes, each note's lane and pad, a user kit's keys, as 70111e91's drum_map.c had them", gn == wn && !memcmp(got, want, wn));
    if (gn != wn || memcmp(got, want, wn)) {    /* (the first line that differs) */
        uint32_t i = 0, l = 0;
        while (i < gn && i < wn && got[i] == want[i]) { if (got[i] == '\n') l = i + 1u; i++; }
        printf("  first difference at line from byte %u:\n  want %.120s\n  got  %.120s\n", l, want + l, got + l);
    }
    {   /* flags: what the firmware asks of a kit */
        track_t *t = &trk[3], *s = &trk[0], *u = &trk[1];
        static const uint32_t KITS[] = {ENGI_808, ENGI_909, ENGI_SIDKIT, ENGI_CR78, ENGI_FMKIT, ENGI_SAMPLER};
        host_preset(s, ENGI_SID, 0);                     /* (SID: no pads) */
        host_preset(u, ENGI_SAMPLER, 0);                 /* (SAMPLER: KIT 1, its record 0) */
        int ok = pads_on(t) && pads_flag(t, MOD3_PD_NOBEND) && pads_flag(t, MOD3_PD_NOSUS) && pads_flag(t, MOD3_PD_HITS) &&
                 !pads_flag(t, MOD3_PD_RECORD) && pads_rec(t) < 0 && pads_flag(u, MOD3_PD_RECORD) && !pads_on(s) &&
                 !pads_flag(s, MOD3_PD_NOBEND | MOD3_PD_NOSUS | MOD3_PD_HITS) && lane_note(s, 2) == 39u &&
                 !pads_lane(s, 49) && pads_rec(s) < 0 && !pads_eng(ENGI_SID);
        for (uint32_t i = 0; i < sizeof KITS / sizeof *KITS; i++) ok &= pads_eng(KITS[i]) != 0;
        ok &= pads_rec(u) == 0;
        u->p[P_E0] = 16; ok &= pads_rec(u) == 7; u->p[P_E0] = 8; ok &= pads_rec(u) < 0; u->p[P_E0] = DK_USER0;
        check("a kit synth: no bend, no sustain, hits; SAMPLER's records (KIT 1..8: records 0..7), the other kits' none; "
              "a synth without pads never a kit (its lanes the pattern format's notes)", ok);
        ok = pads_rec_value(&ENGINES[ENGI_SAMPLER]->edit[0], 9) == 0 && pads_rec_value(&ENGINES[ENGI_SAMPLER]->edit[0], 16) == 7 &&
             pads_rec_value(&ENGINES[ENGI_SAMPLER]->edit[0], 8) < 0 && pads_rec_value(&ENGINES[ENGI_SID]->edit[0], 9) < 0 &&
             pads_rec_names(&ENGINES[ENGI_SAMPLER]->edit[0]) == 9;
        check("SAMPLER's KIT card's values 9..16 name records 0..7 (another synth's values never)", ok);
    }
    {   /* Melodee 1.x's KIT wire (core.h kit_wire_pack before the schemas): 16 pads x 6 + 7 + 7 + 7 bits */
        uint8_t p[64], q[64], a[MOD_SCH_KIT_PACKED], b[MOD_SCH_KIT_PACKED];
        uint32_t lcg = 77u, ok = 1;
        for (uint32_t r = 0; r < 200u && ok; r++) {
            int8_t *c = (int8_t *)(void *)p;
            for (uint32_t i = 0; i < 16u; i++, c += 4) {
                lcg = lcg * 1664525u + 1013904223u;
                c[0] = (int8_t)((int32_t)(lcg >> 8) % 49 - 24); c[1] = (int8_t)((int32_t)((lcg >> 14) & 127u) - 64);
                c[2] = (int8_t)((int32_t)((lcg >> 21) & 127u) - 64); c[3] = (int8_t)((lcg >> 4) & 127u);
            }
            uint32_t pos = 0;
            memset(a, 0, sizeof a);
            c = (int8_t *)(void *)p;
            for (uint32_t i = 0; i < 16u; i++, c += 4) {
                wire_bits_put(a, &pos, (uint32_t)(c[0] + 24), 6); wire_bits_put(a, &pos, (uint32_t)(c[1] + 64), 7);
                wire_bits_put(a, &pos, (uint32_t)(c[2] + 64), 7); wire_bits_put(a, &pos, (uint32_t)c[3], 7);
            }
            patch_pack(MOD_SCH_STORE_KIT, b, p, sizeof b);
            memset(q, 0, sizeof q);
            patch_unpack(MOD_SCH_STORE_KIT, q, a, sizeof a);
            ok = !memcmp(a, b, sizeof a) && !memcmp(p, q, sizeof p);
        }
        check("a kit synth's stored packing (MOD3_R_PACK) packs and unpacks as Melodee 1.x's KIT wire, bit for bit", ok);
    }
    {   /* mutations of the 808's pads block (the 909's MPC pads, SAMPLER's records): each caught */
        const uint32_t E = ENGI_808;
        uint32_t at = block_at(E), len, m = 0;
        uint8_t *b = drum_schema(E, &len), h[MOD_SCH_PDHEAD], mp[MOD_SCH_PDMAP];
        memcpy(h, b + at, sizeof h);
        mod_sch_head sh;
        assert(!sch_head(E, &sh));
        uint32_t lanes = sh.wid + (h[10] | (uint32_t)h[11] << 8), maps = sh.wid + (h[12] | (uint32_t)h[13] << 8);
        memcpy(mp, b + maps, sizeof mp);
        uint32_t mpc = sh.wid + (mp[8] | (uint32_t)mp[9] << 8), np = sh.wid + (mp[12] | (uint32_t)mp[13] << 8);
        uint32_t names = sh.str + (mp[2] | (uint32_t)mp[3] << 8), labels = sh.wid + (mp[18] | (uint32_t)mp[19] << 8);
        { int c_ = caught(E, lanes + 8u + 41u, 1); printf("  mutation %s: %s\n", "lanes + 8u + 41u, 1", c_ ? "caught" : "MISSED"); m += c_; }         /* a note's lane (41: the low tom's) */
        { int c_ = caught(E, lanes + 2u, 4); printf("  mutation %s: %s\n", "lanes + 2u, 4", c_ ? "caught" : "MISSED"); m += c_; }               /* a lane's note: the pads refused */
        {   /* the 909's own map: an MPC pad of its schema */
            uint32_t a9 = block_at(ENGI_909), l9;
            uint8_t *b9 = drum_schema(ENGI_909, &l9), m9[MOD_SCH_PDMAP];
            mod_sch_head h9;
            assert(!sch_head(ENGI_909, &h9));
            memcpy(m9, b9 + h9.wid + (b9[a9 + 12] | (uint32_t)b9[a9 + 13] << 8), sizeof m9);
            int c_ = caught(ENGI_909, h9.wid + (m9[8] | (uint32_t)m9[9] << 8) + 3u, 1);
            printf("  mutation %s: %s\n", "909 mpc + 3u, 1", c_ ? "caught" : "MISSED"); m += c_;
        }
        { int c_ = caught(E, mpc + 3u, 1); printf("  mutation %s: %s\n", "mpc + 3u, 1", c_ ? "caught" : "MISSED"); m += c_; }                 /* an MPC pad */
        { int c_ = caught(E, np + 60u, 2); printf("  mutation %s: %s\n", "np + 60u, 2", c_ ? "caught" : "MISSED"); m += c_; }                 /* a note's pad */
        { int c_ = caught(E, names + 1u, 0x20); printf("  mutation %s: %s\n", "names + 1u, 0x20", c_ ? "caught" : "MISSED"); m += c_; }            /* a pad's name */
        { int c_ = caught(E, labels + 2u * 6u, 1); printf("  mutation %s: %s\n", "labels + 2u * 6u, 1", c_ ? "caught" : "MISSED"); m += c_; }         /* the snare's third label */
        { int c_ = caught(E, at + 2, 1); printf("  mutation %s: %s\n", "the maps' count 1 -> 0", c_ ? "caught" : "MISSED"); m += c_; }
        {
            uint32_t was = b[at + 1];
            b[at + 1] &= (uint8_t)~MOD3_PD_NOBEND;
            sch_gen++;
            int c1 = !pads_flag(&trk[3], MOD3_PD_NOBEND);
            b[at + 1] = (uint8_t)was;
            sch_gen++;
            printf("  mutation NOBEND flag: %s\n", c1 && pads_flag(&trk[3], MOD3_PD_NOBEND) ? "caught" : "MISSED");
            m += c1 && pads_flag(&trk[3], MOD3_PD_NOBEND);
        }
        {
            uint32_t as = block_at(ENGI_SAMPLER), ls;   /* rec0: SAMPLER's KIT card's record values move */
            uint8_t *bs = drum_schema(ENGI_SAMPLER, &ls);
            uint32_t was = bs[as + 22];
            bs[as + 22] = 10;
            sch_gen++;
            int c1 = pads_rec_value(&ENGINES[ENGI_SAMPLER]->edit[0], 9) < 0;
            bs[as + 22] = (uint8_t)was;
            sch_gen++;
            printf("  mutation rec0: %s\n", c1 && pads_rec_value(&ENGINES[ENGI_SAMPLER]->edit[0], 9) == 0 ? "caught" : "MISSED");
            m += c1 && pads_rec_value(&ENGINES[ENGI_SAMPLER]->edit[0], 9) == 0;
        }
        check("mutations of the kit synths' pads blocks caught (a note's lane, a lane's note, the 909's map, an MPC pad, a note's pad, "
              "a name, a label, the maps' count, a flag, the record values): 10 of 10", m == 10u);
        printf("pads: %u of 10 mutations caught\n", m);
    }
    printf(fails ? "pads: FAILED\n" : "pads: all passed\n");
    return fails != 0;
}
