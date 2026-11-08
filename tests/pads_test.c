/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The pads capability (mod_schema.h MOD3_R_PADS, firmware/src/pads.c) against what the firmware knew of DRUM before it:
 * tests/fixtures/pads-70111e91-fm1.txt (-smk37.txt) was written from 70111e91's own drum_map.c and ui_draw.c (every
 * KIT value 0..16: the pads' notes, names, abbreviations, third labels and knob order, the MPC pads, the lanes' names,
 * abbreviations and notes, each note's lane and pad, a user kit's keys). Here DRUM's pads block, read by the firmware
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
    gn = 0;
    got[0] = 0;
    for (int k = 0; k <= 16; k++) {
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
            out(" %u", (unsigned)(pads_keymap(t) ? (uint32_t)ENGINES[ENGI_DRUM]->keys(t, (uint32_t)ks) & 255u : drum_user_key(t, ks)));
        out("\n");
    }
}
static char want[64 * 1024];
static uint32_t wn;
static int fails;
static void check(const char *s, int ok) { printf("pads: %-100s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }
static uint8_t *drum_schema(uint32_t *len) { return (uint8_t *)(uintptr_t)sch_builtin(ENGI_DRUM, len); }
static uint32_t block_at(void)                   /* the pads block's offset in DRUM's schema */
{
    mod_sch_head h;
    uint32_t id, f, l;
    assert(!sch_head(ENGI_DRUM, &h) && !sch_role(ENGI_DRUM, MOD3_R_PADS, 0, &id, &f, &l));
    return h.wid + id;
}
static int same(void) { sch_gen++; facts(); return gn == wn && !memcmp(got, want, wn); }
/* byte at of DRUM's schema xor'd with x: the facts no longer the fixture's (or the pads gone); restored */
static int caught(uint32_t at, uint8_t x)
{
    uint32_t len;
    uint8_t *b = drum_schema(&len);
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
    host_preset(&trk[3], ENGI_DRUM, 0);
    facts();
    check("DRUM's pads block as the firmware reads any synth's: every KIT's pads, notes, names, labels, knob order, MPC pads, "
          "lanes, each note's lane and pad, a user kit's keys, as 70111e91's drum_map.c had them", gn == wn && !memcmp(got, want, wn));
    if (gn != wn || memcmp(got, want, wn)) {    /* (the first line that differs) */
        uint32_t i = 0, l = 0;
        while (i < gn && i < wn && got[i] == want[i]) { if (got[i] == '\n') l = i + 1u; i++; }
        printf("  first difference at line from byte %u:\n  want %.120s\n  got  %.120s\n", l, want + l, got + l);
    }
    {   /* flags: what the firmware asks of a kit */
        track_t *t = &trk[3], *s = &trk[0];
        host_preset(s, ENGI_SID, 0);                     /* (SID: no pads) */
        int ok = pads_on(t) && pads_flag(t, MOD3_PD_NOBEND) && pads_flag(t, MOD3_PD_NOSUS) && pads_flag(t, MOD3_PD_HITS) &&
                 pads_flag(t, MOD3_PD_RECORD) && !pads_on(s) && !pads_flag(s, MOD3_PD_NOBEND | MOD3_PD_NOSUS | MOD3_PD_HITS) &&
                 lane_note(s, 2) == 39u && !pads_lane(s, 49) && pads_rec(s) < 0 && !pads_eng(ENGI_SID) && pads_eng(ENGI_DRUM);
        t->p[P_E0] = 9; ok &= pads_rec(t) == 0; t->p[P_E0] = 16; ok &= pads_rec(t) == 7; t->p[P_E0] = 5; ok &= pads_rec(t) < 0;
        check("a kit: no bend, no sustain, hits, records (KIT 1..8: records 0..7); a synth without pads never a kit (its "
              "lanes the pattern format's notes)", ok);
        t->p[P_E0] = 4;
        ok = pads_rec_value(&ENGINES[ENGI_DRUM]->edit[0], 9) == 0 && pads_rec_value(&ENGINES[ENGI_DRUM]->edit[0], 16) == 7 &&
             pads_rec_value(&ENGINES[ENGI_DRUM]->edit[0], 8) < 0 && pads_rec_value(&ENGINES[ENGI_SID]->edit[0], 9) < 0 &&
             pads_rec_names(&ENGINES[ENGI_DRUM]->edit[0]) == 9;
        check("the KIT card's values 9..16 name records 0..7 (another synth's values never)", ok);
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
        check("DRUM's stored packing (MOD3_R_PACK) packs and unpacks as Melodee 1.x's KIT wire, bit for bit", ok);
    }
    {   /* mutations of DRUM's pads block: each caught */
        uint32_t at = block_at(), len, m = 0;
        uint8_t *b = drum_schema(&len), h[MOD_SCH_PDHEAD], mp[MOD_SCH_PDMAP];
        memcpy(h, b + at, sizeof h);
        mod_sch_head sh;
        assert(!sch_head(ENGI_DRUM, &sh));
        uint32_t lanes = sh.wid + (h[10] | (uint32_t)h[11] << 8), maps = sh.wid + (h[12] | (uint32_t)h[13] << 8);
        uint32_t sel = sh.wid + (h[8] | (uint32_t)h[9] << 8);
        memcpy(mp, b + maps, sizeof mp);
        uint32_t mpc = sh.wid + (mp[8] | (uint32_t)mp[9] << 8), np = sh.wid + (mp[12] | (uint32_t)mp[13] << 8);
        uint32_t names = sh.str + (mp[2] | (uint32_t)mp[3] << 8), labels = sh.wid + (mp[18] | (uint32_t)mp[19] << 8);
        { int c_ = caught(lanes + 8u + 41u, 1); printf("  mutation %s: %s\n", "lanes + 8u + 41u, 1", c_ ? "caught" : "MISSED"); m += c_; }         /* a note's lane (41: the low tom's) */
        { int c_ = caught(lanes + 2u, 4); printf("  mutation %s: %s\n", "lanes + 2u, 4", c_ ? "caught" : "MISSED"); m += c_; }               /* a lane's note: the pads refused */
        { int c_ = caught(sel + 5u, 3); printf("  mutation %s: %s\n", "sel + 5u, 3", c_ ? "caught" : "MISSED"); m += c_; }                 /* KIT 909's map */
        { int c_ = caught(mpc + 3u, 1); printf("  mutation %s: %s\n", "mpc + 3u, 1", c_ ? "caught" : "MISSED"); m += c_; }                 /* an MPC pad */
        { int c_ = caught(np + 60u, 2); printf("  mutation %s: %s\n", "np + 60u, 2", c_ ? "caught" : "MISSED"); m += c_; }                 /* a note's pad */
        { int c_ = caught(names + 1u, 0x20); printf("  mutation %s: %s\n", "names + 1u, 0x20", c_ ? "caught" : "MISSED"); m += c_; }            /* a pad's name */
        { int c_ = caught(labels + 2u * 6u, 1); printf("  mutation %s: %s\n", "labels + 2u * 6u, 1", c_ ? "caught" : "MISSED"); m += c_; }         /* the snare's third label */
        { int c_ = caught(at + 2, 6); printf("  mutation %s: %s\n", "the maps' count 5 -> 3", c_ ? "caught" : "MISSED"); m += c_; }
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
            uint32_t was = b[at + 22];               /* rec0: the KIT card's record values move */
            b[at + 22] = 10;
            sch_gen++;
            int c1 = pads_rec_value(&ENGINES[ENGI_DRUM]->edit[0], 9) < 0;
            b[at + 22] = (uint8_t)was;
            sch_gen++;
            printf("  mutation rec0: %s\n", c1 && pads_rec_value(&ENGINES[ENGI_DRUM]->edit[0], 9) == 0 ? "caught" : "MISSED");
            m += c1 && pads_rec_value(&ENGINES[ENGI_DRUM]->edit[0], 9) == 0;
        }
        check("mutations of DRUM's pads block caught (a note's lane, a lane's note, a KIT's map, an MPC pad, a note's pad, "
              "a name, a label, the maps' count, a flag, the record values): 10 of 10", m == 10u);
        printf("pads: %u of 10 mutations caught\n", m);
    }
    printf(fails ? "pads: FAILED\n" : "pads: all passed\n");
    return fails != 0;
}
