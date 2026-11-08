/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The user's drum kits (DRUM's data records: src/rec_store.c keeps them, modules/kits/kit_store.c checks them,
 * modules/kits/drum_real.c plays them) on a simulated NOR flash: a kit of eleven synthetic takes
 * (src/drum_enc.c, a looped crash among them) written as a pool record spanning seven sectors decodes, take by take,
 * sample for sample as the same slices read from one block of RAM (the sector edges cost nothing), and the track
 * plays it the same twice and without the resource region; an empty slot, a deleted kit and a rewritten slot are
 * silent until the next hit; a record the player could not trust is refused and the old kit stays; the boot scan finds
 * the kits again; the KIT card shows a kit's name; a torn write leaves the slot as it was. */
static unsigned char nor[0x100000];
#define REC_BASE ((const uint8_t *)nor)        /* (the data records' flash: the player reads them in place) */
static int res_absent_for_test;
#define RES_OK() (!res_absent_for_test)
#define main hostsim_main
#include "hostsim.c"
#undef main
static void fm1_wdt_feed(void) {}
static uint8_t flash_ok = 1;
static int fail_after = -1;                      /* torn write: stop after N programs */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (fail_after == 0) return -9;
    if (fail_after > 0) fail_after--;
    for (uint32_t i = 0; i < n; i++) nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/rec_store.c"            /* (DRUM's own record helpers and encoder: built in) */

static int fails;
static void check(const char *s, int ok) { printf("USER KIT: %-86s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }

/* a kit of eleven synthetic takes (seed: which), its record in rec, about target bytes (the crash takes the rest);
 * its length */
static uint8_t rec[DRK_KIT_MAX + 64];
static uint32_t lcg;
static int32_t noise(void) { lcg = lcg * 1664525u + 1013904223u; return (int32_t)(lcg >> 16) - 32768; }
static uint32_t take_of(uint32_t seed, uint32_t d, int16_t *x, uint32_t n)
{
    float ph = 0.0f;
    lcg = 777u + seed * 31u + d;
    for (uint32_t i = 0; i < n + 64u; i++) {
        float t = (float)i / (float)FS, v = d < D9_RS ? sinf(ph += 6.2831853f * (60.0f + 40.0f * (float)d + 80.0f * dlk_exp2(-t * 30.0f)) / (float)FS)
                                                     : (float)noise() / 32768.0f;
        x[i] = i < n ? (int16_t)(v * 26000.0f * dlk_exp2(-t * (d == D9_CR ? 0.7f : 9.0f + (float)seed))) : 0;
    }
    return n;
}
static uint32_t make_record(uint32_t seed, const char *name, uint32_t target)
{
    static int16_t x[FS * 2u + 64u];
    drk_kit_t H;
    uint32_t at = DRK_KIT_BYTES / 8u;
    memset(&H, 0, sizeof H);
    memset(rec, 0, sizeof rec);
    H.magic = DRK_KIT_MAGIC; H.bytes = DRK_KIT_BYTES; H.ndrum = DRK_NDRUM; H.nsmp = DRK_NDRUM;
    memcpy(H.name, name, strlen(name));
    for (uint32_t d = 0; d < DRK_NDRUM; d++) {
        uint32_t n = d == D9_CR ? FS * 2u : FS / (d < D9_RS ? 6u : 12u), loop;
        dke_out_t o;
        if (d == D9_CR) {                            /* (the crash last in D9 order but two: sized to the room left) */
            uint32_t room = target > at * 8u + 5000u ? target - at * 8u - 3000u : 2000u;
            for (uint32_t bytes; take_of(seed, d, x, n), (bytes = dke_encode(x, n, 24, 0, 0, &o)) > room;)
                n = (uint32_t)((uint64_t)n * room / bytes) * 15u / 16u;
        }
        take_of(seed, d, x, n);
        loop = d == D9_CR ? n * 7u / 10u : 0u;
        uint32_t bytes = dke_encode(x, n, 24, loop, (uint32_t *)(void *)(rec + at * 8u), &o);
        drk_sample_t s = {(uint16_t)at, (uint16_t)n, (uint16_t)o.loop_at, (uint16_t)o.loop_slice, 32768, (uint8_t)(loop ? 2 : 0), (uint8_t)d};
        if (loop) memcpy(H.loops[d], o.loop, sizeof H.loops[d]);
        H.smp[d] = s;
        H.map[d] = (drk_map_t){(uint8_t)d, 0xFFu, 0, 0, 4096, (uint16_t)(d == D9_CR ? 2400 : 600), 4000};
        at += bytes / 8u;
    }
    memcpy(rec, &H, sizeof H);
    if (at * 8u > DRK_KIT_MAX) { printf("USER KIT: test record too long (%u)\n", at * 8u); exit(1); }
    return at * 8u;
}
static int put(uint32_t slot, const uint8_t *src, uint32_t len)
{
    st_stream_t w;
    int rc = rec_put_begin(&w, slot, len);
    for (uint32_t off = 0; !rc && off < len; off += 256u)
        rc = rec_put_write(&w, src + off, len - off < 256u ? len - off : 256u);
    return rc ? rc : rec_put_commit(&w, slot);
}
static void kit(int user_slot)
{
    host_tracks_init();
    for (uint32_t p = 0; p < NPART; p++)
        memset(trk[p].v, 0, NVOICE * sizeof *trk[p].v);
    eng_state_reset();
    host_preset(&trk[0], user_slot < 0 ? ENGI_808 : ENGI_SAMPLER, 0);
    trk[0].p[P_E0] = (int16_t)(user_slot < 0 ? DK_808 : DK_USER0 + user_slot);
}
static int32_t run(int32_t *buf, uint32_t nb)
{
    int32_t o[CTL], pk = 0;
    for (uint32_t i = 0; i < nb; i++) {
        track_render(&trk[0], o, CTL);
        for (uint32_t k = 0; k < CTL; k++) {
            if (buf) buf[i * CTL + k] = o[k];
            pk = abs(o[k]) > pk ? abs(o[k]) : pk;
        }
    }
    return pk;
}
/* every drum, overlapping, then the crash ringing on into its loop */
static void groove(int32_t *buf)
{
    static const uint8_t notes[] = {36, 42, 38, 42, 36, 46, 38, 42, 41, 45, 48, 37, 39, 49, 51, 42, 36, 38};
    uint32_t at = 0;
    for (uint32_t i = 0; i < sizeof notes; i++) {
        trk_note_on(&trk[0], notes[i], (uint8_t)(70 + (i * 13u) % 57u));
        run(buf + at, 40u);
        at += 40u * CTL;
    }
    run(buf + at, (uint32_t)(FS * 3u / CTL));
}
#define GROOVE (18u * 40u * CTL + FS * 3u)
/* slot u's take for drum d: sample by sample through the player's channel, from the flash sectors (ram 0) or from
 * the record as one block of RAM */
static uint32_t decode(uint32_t u, uint32_t d, const uint8_t *ram, int16_t *y)
{
    static drk_part_t P;
    const uint32_t *secs[DRK_KSEC];
    uint32_t n = drk_user[u].n;
    static uint32_t phys[DRK_KSEC][DRK_SLOTS * 2u];      /* (the RAM block laid out as in flash: the guard's place skipped) */
    if (ram) {
        memset(phys, 0xFF, sizeof phys);
        for (uint32_t k = 0; k < rec_len[u] / 8u; k++) memcpy(&phys[k / DRK_SECSLICES][DRK_SLOT(k % DRK_SECSLICES)], ram + 8u * k, 8u);
    }
    for (uint32_t k = 0; k < n; k++)
        secs[k] = ram ? phys[k] : (const uint32_t *)(const void *)DRK_XIP(drk_user[u].sec[k] * 4096u + DRK_SECOFF);
    const drk_kit_t *H = (const drk_kit_t *)(const void *)secs[0];
    const drk_sample_t *s = &H->smp[H->map[d].sample];
    drk_ch_t c;
    memset(&c, 0, sizeof c);
    for (uint32_t k = 0; k < n; k++) P.sec[k] = secs[k];
    c.secs = P.sec;
    drk_stream_start(&c, s);
    uint32_t len = s->n + (s->loop_at ? s->n - s->loop_at : 0u);   /* once past its end into the loop */
    for (uint32_t i = 0; i < len; i++) {
        if (c.idx >= c.n && s->loop_at) {
            for (uint32_t k = 0; k < 4u; k++) { c.h[k] = H->loops[s->st][k]; c.w[k] = H->loops[s->st][4u + k]; }
            drk_seek(&c, (uint32_t)s->at + s->loop_slice); c.nleft = 0; c.idx = s->loop_at;
        }
        y[i] = (int16_t)drk_next(&c);
        c.idx++;
    }
    return len;
}

/* a blank store's free sectors (storage.c): the data flash less OTA staging, the settings pair and the resource region
 * (its whole window: no header in these tests) */
#define T_FREE0 ((int)(ST_LN - 5u - 2u - (FL_RES_HI - FL_RES_LO) / ST_SECTOR))
int main(void)
{
    static int32_t a[GROOVE], b[GROOVE];
    static int16_t y[FS * 4u], z[FS * 4u];
    memset(nor, 0xFF, sizeof nor);
    check("KIT 9..16 are the user's: eight slots after FM, names KIT 1..8 until written",
          DK_USER0 == DK_FM + 1 && DK_KIT_MAX == DK_USER0 + 7 && ENG_SAMPLER.edit[0].max == DK_KIT_MAX && ENG_SAMPLER.edit[0].min == DK_USER0 &&
          !strcmp(N_DRUM_KIT[DK_USER0], "KIT 1") && !strcmp(N_DRUM_KIT[DK_KIT_MAX], "KIT 8"));

    uint32_t len = make_record(0, "ROCK", 27500);       /* (past six 4,056-byte sectors) */
    check("eleven takes as a record fit seven sectors and cross sector edges", len <= DRK_KIT_MAX && len > 6u * DRK_SECBYTES);
    check("the record is written, checked and published into slot 3", put(2, rec, len) == 0 && drk_user[2].n == 7u &&
          rec_len[2] == len);
    {
        uint32_t differ = 0, total = 0;
        for (uint32_t d = 0; d < DRK_NDRUM; d++) {
            uint32_t n = decode(2, d, 0, y);
            decode(2, d, rec, z);
            total += n;
            for (uint32_t i = 0; i < n; i++) differ += y[i] != z[i];
        }
        printf("USER KIT:   %u samples compared\n", total);
        check("every take from the pool's sectors == the same slices in one block (edges and loop included)", !differ);
    }

    kit(2); groove(a);
    kit(2); groove(b);
    uint32_t differ = 0, loud = 0;
    for (uint32_t i = 0; i < GROOVE; i++) { differ += a[i] != b[i]; loud += b[i] != 0; }
    check("the track plays the kit, the same twice", !differ && loud > GROOVE / 4u);
    res_absent_for_test = 1;
    kit(2); groove(b);
    differ = 0;
    for (uint32_t i = 0; i < GROOVE; i++) differ += a[i] != b[i];
    check("... and without the resource region", !differ);
    res_absent_for_test = 0;

    kit(5); trk_note_on(&trk[0], 38, 120);
    check("an empty slot is silent", run(0, 40u) == 0);

    {   /* the card's name */
        const char *unit; char v[8], e[8];
        param_format(&ENG_SAMPLER.edit[0], DK_USER0 + 2, v, &unit);
        param_format(&ENG_SAMPLER.edit[0], DK_USER0 + 5, e, &unit);
        check("the KIT card shows the kit's own name, EMPTY for none", !strcmp(v, "ROCK") && !strcmp(e, "EMPTY"));
    }

    {   /* rewritten while it rings: the next block is silent, the next hit is the new kit's */
        kit(2); trk_note_on(&trk[0], 49, 127); run(0, 20u);
        uint32_t blen = make_record(1, "BOOM", 12000);
        int rc = put(2, rec, blen);
        int32_t pk = run(0, 4u);
        trk_note_on(&trk[0], 36, 120); run(a, 200u);
        put(4, rec, blen);
        kit(4); trk_note_on(&trk[0], 36, 120); run(b, 200u);
        check("a slot rewritten while it rings: silent at once, then the new kit plays",
              !rc && pk == 0 && !memcmp(a, b, 200u * CTL * sizeof a[0]));
        rec_delete(4);
        check("the storage counts the kit and nothing else of it",
              st_project_free() == T_FREE0 - (int)((blen + DRK_SECBYTES - 1u) / DRK_SECBYTES));
    }

    {   /* refused records */
        uint32_t ok_len = make_record(0, "ROCK", 25000), bad = 0;
        memcpy(rec + 4, "\x00\x00", 2);                  /* bytes 0: not a header */
        bad += put(3, rec, ok_len) == -2;
        make_record(0, "ROCK", 25000); memcpy(((drk_kit_t *)(void *)rec)->name, "TOOLONG", 7);
        bad += put(3, rec, ok_len) == -2;
        make_record(0, "ROCK", 25000); ((drk_kit_t *)(void *)rec)->map[3].sample = 11;
        bad += put(3, rec, ok_len) == -2;
        make_record(0, "ROCK", 25000);
        bad += put(3, rec, ok_len - 8u) == -2;           /* cut short: the last take runs past the end */
        check("headers, names, maps and streams the player cannot trust are refused", bad == 4u && !drk_user[3].n);
        kit(3); trk_note_on(&trk[0], 38, 120);
        check("... and leave the slot empty (silent)", run(0, 40u) == 0);
        make_record(0, "ROCK", 25000);
        kit(2); trk_note_on(&trk[0], 36, 120); run(a, 100u);
        fail_after = 30;
        int rc = put(2, rec, ok_len);
        fail_after = -1;
        kit(2); trk_note_on(&trk[0], 36, 120); run(b, 100u);
        check("a torn write leaves the slot's kit as it was", rc != 0 && drk_user[2].n && !memcmp(a, b, 100u * CTL * sizeof a[0]));
    }

    {   /* boot */
        memset(drk_user, 0, sizeof drk_user); memset(rec_len, 0, sizeof rec_len);
        rec_scan();
        check("the boot scan finds the kits again", drk_user[2].n && rec_len[2] && !drk_user[3].n);
    }

    {   /* boot before DRUM is listed (a module written after power-on; nightly dc2209de scanned before mod_boot) */
        const engine_t *drum = ENGINES[ENGI_SAMPLER];
        memset(drk_user, 0, sizeof drk_user); memset(rec_len, 0, sizeof rec_len);
        ENGINES[ENGI_SAMPLER] = &ENG_NONE; sch_gen++;
        rec_scan();
        int waited = !drk_user[2].n && (rec_pend >> 2 & 1u);
        int hidden = rec_hidden(2) && !rec_hidden(3);     /* (Record kit's sheet never takes slot 2 for an empty one) */
        ENGINES[ENGI_SAMPLER] = drum; sch_gen++;
        rec_recheck();                                    /* (eng_module_main.c mod_register: DRUM in SRAM) */
        check("kits found before their synth is listed wait for it, then play (not dropped until a save)",
              waited && drk_user[2].n && rec_len[2] && !drk_user[3].n && !rec_pend);
        check("... and while it waits its slot is a kit, not an empty slot a take may write over",
              hidden && !rec_hidden(2));
    }

    {   /* delete while it rings */
        kit(2); trk_note_on(&trk[0], 49, 127); run(0, 20u);
        uint32_t gen = st_pool_gen;
        int rc = rec_delete(2);
        int32_t pk = run(0, 4u);
        trk_note_on(&trk[0], 38, 120);
        check("a kit deleted while it rings: silent at once and after, the slot empty, its sectors free",
              !rc && pk == 0 && run(0, 40u) == 0 && !drk_user[2].n && !rec_len[2] && st_project_free() == T_FREE0);
        check("... and the room it gave back is counted again (SLOTS card: st_pool_gen moved on)", st_pool_gen != gen);
    }
    if (fails) { printf("USER KIT: %d FAILED\n", fails); return 1; }
    printf("USER KIT: all checks ok\n");
    return 0;
}
