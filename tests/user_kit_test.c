/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SAMPLER's kits (its data records: src/rec_store.c keeps them, modules/kits/kit_store.c checks them,
 * modules/kits/drum_real.c plays them) on a simulated NOR flash: a kit of sixteen synthetic takes (drum_enc.c, a looped
 * crash among them), each pad a record of its own, decodes take by take sample for sample as the same slices read from
 * one block of RAM (sector edges, the guard and the loop cost nothing), and the track plays it the same twice and
 * without the resource region; an empty kit, a deleted kit and a rewritten pad are silent until the next hit; the
 * records of the other pads keep playing as the table moves under them; a record the player could not trust is
 * refused and the old one stays; the boot scan finds the kits again (waiting for their synth when it is not listed
 * yet); the KIT card shows a kit's name; a torn write leaves the pad as it was. */
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
#include "../firmware/src/rec_store.c"            /* (SAMPLER's record helpers and encoder: built in) */

static int fails;
static void check(const char *s, int ok) { printf("USER KIT: %-86s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }

/* pad p's synthetic take (seed: which kit), its record in rec (a looped crash on pad 9); its length */
static uint8_t rec[DRK_NPAD][FS * 2u];
static uint32_t rlen[DRK_NPAD];
static uint32_t lcg;
static int32_t noise(void) { lcg = lcg * 1664525u + 1013904223u; return (int32_t)(lcg >> 16) - 32768; }
static uint32_t make_pad(uint32_t seed, uint32_t p)
{
    static int16_t x[FS * 2u + 64u];
    drk_pad_t H;
    dke_out_t o;
    uint32_t n = p == 9u ? FS * 2u : FS / (p < 5u ? 6u : 12u), loop = p == 9u ? n * 7u / 10u : 0u;
    float ph = 0.0f;
    lcg = 777u + seed * 31u + p;
    for (uint32_t i = 0; i < n + 64u; i++) {
        float t = (float)i / (float)FS, v = p < 5u ? sinf(ph += 6.2831853f * (60.0f + 40.0f * (float)p + 80.0f * dlk_exp2(-t * 30.0f)) / (float)FS)
                                                   : (float)noise() / 32768.0f;
        x[i] = i < n ? (int16_t)(v * 26000.0f * dlk_exp2(-t * (p == 9u ? 0.7f : 9.0f + (float)seed))) : 0;
    }
    memset(&H, 0, sizeof H);
    memset(rec[p], 0, sizeof rec[p]);
    uint32_t bytes = dke_encode(x, n, 24, loop, (uint32_t *)(void *)(rec[p] + DRK_PAD_BYTES), &o);
    H.magic = DRK_PAD_MAGIC; H.bytes = (uint16_t)DRK_PAD_BYTES; H.pad = (uint8_t)p; H.codec = DRK_CODEC_LMS;
    H.n = n; H.inc = 32768u; H.gain = 1024u; H.out = 4096u; H.ms = (uint16_t)(p == 9u ? 2400u : 0u); H.pivot = 4000u;
    if (loop) { H.loop_at = o.loop_at; H.loop_slice = o.loop_slice; H.dark = 2; memcpy(H.loop, o.loop, sizeof H.loop); }
    memcpy(rec[p], &H, sizeof H);
    return rlen[p] = DRK_PAD_BYTES + bytes;
}
static int put(uint32_t s, const uint8_t *src, uint32_t len)
{
    st_stream_t w;
    int rc = rec_put_begin(&w, s, len);
    for (uint32_t off = 0; !rc && off < len; off += 256u)
        rc = rec_put_write(&w, src + off, len - off < 256u ? len - off : 256u);
    return rc ? rc : rec_put_commit(&w, s);
}
/* kit u: every pad's record (seed), its name; 0 */
static int make_kit(uint32_t u, uint32_t seed, const char *name)
{
    int rc = 0;
    for (uint32_t p = 0; p < DRK_NPAD && !rc; p++) { make_pad(seed, p); rc = put(u * MOD3_RPAD + p, rec[p], rlen[p]); }
    return rc ? rc : rec_put_name(u, name, (uint32_t)strlen(name));
}
static void kit(int u)
{
    host_tracks_init();
    for (uint32_t p = 0; p < NPART; p++)
        memset(trk[p].v, 0, NVOICE * sizeof *trk[p].v);
    eng_state_reset();
    host_preset(&trk[0], u < 0 ? ENGI_808 : ENGI_SAMPLER, 0);
    trk[0].p[P_E0] = (int16_t)(u < 0 ? DK_808 : DK_USER0 + u);
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
/* every pad, overlapping, then the crash ringing on into its loop */
static void groove(int32_t *buf)
{
    static const uint8_t notes[] = {36, 42, 38, 42, 36, 46, 38, 42, 41, 45, 50, 37, 39, 49, 56, 64, 61, 60, 75, 70, 36, 38};
    uint32_t at = 0;
    for (uint32_t i = 0; i < sizeof notes; i++) {
        trk_note_on(&trk[0], notes[i], (uint8_t)(70 + (i * 13u) % 57u));
        run(buf + at, 40u);
        at += 40u * CTL;
    }
    run(buf + at, (uint32_t)(FS * 3u / CTL));
}
#define GROOVE (22u * 40u * CTL + FS * 3u)
/* pad record s's take sample by sample through the player's channel, from the flash sectors (ram 0) or from the record
 * as one block of RAM laid out as in flash (the guard's place skipped), once past its end into its loop */
static uint8_t phys[16][4096];
static uint8_t plist[16];
static uint32_t decode(uint32_t s, const uint8_t *ram, uint32_t len, int16_t *y)
{
    const uint8_t *list = &rec_list[rec_tab[s].at];
    if (ram) {                                   /* (sector numbers 0x10.. outside the store: phys[] at their place) */
        static unsigned char keep[16][4096];
        memset(phys, 0xFF, sizeof phys);
        for (uint32_t k = 0; k < len / 8u; k++)
            memcpy(&phys[k / DRK_SECSLICES][DRK_SECOFF + 4u * DRK_SLOT(k % DRK_SECSLICES)], ram + 8u * k, 8u);
        for (uint32_t k = 0; k < 16u; k++) { memcpy(keep[k], nor + (0x10u + k) * 4096u, 4096); memcpy(nor + (0x10u + k) * 4096u, phys[k], 4096); plist[k] = (uint8_t)(0x10u + k); }
        list = plist;
        (void)keep;
    }
    const drk_pad_t *H = (const drk_pad_t *)(const void *)DRK_SEC(list[0]);
    drk_ch_t c;
    memset(&c, 0, sizeof c);
    c.list = list;
    drk_stream_start(&c, H);
    uint32_t n = H->n + (H->loop_at ? H->n - H->loop_at : 0u);
    for (uint32_t i = 0; i < n; i++) {
        if (c.idx >= c.n && H->loop_at) {
            for (uint32_t k = 0; k < 4u; k++) { c.h[k] = H->loop[k]; c.w[k] = H->loop[4u + k]; }
            drk_seek(&c, DRK_FIRST + H->loop_slice); c.nleft = 0; c.idx = H->loop_at;
        }
        y[i] = (int16_t)drk_next(&c);
        c.idx++;
    }
    if (ram) memset(nor + 0x10000u, 0xFF, 16u * 4096u);
    return n;
}

/* a blank store's free sectors (storage.c): the data flash less OTA staging, the settings pair and the resource region
 * (its whole window: no header in these tests) */
#define T_FREE0 ((int)(ST_LN - 5u - 2u - (FL_RES_HI - FL_RES_LO) / ST_SECTOR))
int main(void)
{
    static int32_t a[GROOVE], b[GROOVE];
    static int16_t y[FS * 4u], z[FS * 4u];
    memset(nor, 0xFF, sizeof nor);
    check("KIT 9..16 are SAMPLER's: eight kits after FM, names KIT 1..8 until named",
          DK_USER0 == DK_FM + 1 && DK_KIT_MAX == DK_USER0 + 7 && ENG_SAMPLER.edit[0].max == DK_KIT_MAX && ENG_SAMPLER.edit[0].min == DK_USER0 &&
          !strcmp(N_DRUM_KIT[DK_USER0], "KIT 1") && !strcmp(N_DRUM_KIT[DK_KIT_MAX], "KIT 8"));

    check("sixteen pad records written, checked and published into KIT 3, its name ROCK", make_kit(2, 0, "ROCK") == 0 &&
          rec_used(2) == 1 && rec_tab[2u * MOD3_RPAD + 9u].n == (rlen[9] + DRK_SECBYTES - 1u) / DRK_SECBYTES);
    {
        uint32_t differ = 0, total = 0;
        for (uint32_t p = 0; p < DRK_NPAD; p++) {
            uint32_t n = decode(2u * MOD3_RPAD + p, 0, 0, y);
            make_pad(0, p);
            decode(2u * MOD3_RPAD + p, rec[p], rlen[p], z);
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
    check("an empty kit is silent", run(0, 40u) == 0);

    {   /* the card's name */
        const char *unit; char v[8], e[8];
        param_format(&ENG_SAMPLER.edit[0], DK_USER0 + 2, v, &unit);
        param_format(&ENG_SAMPLER.edit[0], DK_USER0 + 5, e, &unit);
        check("the KIT card shows the kit's own name, EMPTY for none", !strcmp(v, "ROCK") && !strcmp(e, "EMPTY"));
    }

    {   /* a pad rewritten while it rings: the next block is silent, the next hit is the new take's; the others play on */
        kit(2); trk_note_on(&trk[0], 49, 127); run(0, 20u);
        make_pad(1, 0);
        int rc = put(2u * MOD3_RPAD, rec[0], rlen[0]);
        int32_t pk = run(0, 4u);
        trk_note_on(&trk[0], 36, 120); run(a, 200u);
        put(4u * MOD3_RPAD, rec[0], rlen[0]);
        kit(4); trk_note_on(&trk[0], 36, 120); run(b, 200u);
        check("a pad rewritten while the kit rings: silent at once, then the new take plays",
              !rc && pk == 0 && !memcmp(a, b, 200u * CTL * sizeof a[0]));
        uint32_t before = 0, after = 0;
        make_pad(0, 9);
        decode(2u * MOD3_RPAD + 9u, rec[9], rlen[9], z);
        before = decode(2u * MOD3_RPAD + 9u, 0, 0, y);
        before = !memcmp(y, z, before * 2u);
        rec_delete(4);
        after = decode(2u * MOD3_RPAD + 9u, 0, 0, y);
        after = !memcmp(y, z, after * 2u);
        check("the other pads' sectors stay found as the table moves under them", before && after);
        int used = 0;
        for (uint32_t p = 0; p < DRK_NPAD; p++) used += (int)rec_tab[2u * MOD3_RPAD + p].n;
        check("the storage counts the kit's records and nothing else of it", st_project_free() == T_FREE0 - used - 1);
    }

    {   /* refused records */
        uint32_t bad = 0, s = 3u * MOD3_RPAD + 1u;
        make_pad(0, 1); memcpy(rec[1], "PAD1", 4);
        bad += put(s, rec[1], rlen[1]) == -2;
        make_pad(0, 1); ((drk_pad_t *)(void *)rec[1])->pad = 16;
        bad += put(s, rec[1], rlen[1]) == -2;
        make_pad(0, 1); ((drk_pad_t *)(void *)rec[1])->inc = 40000u;
        bad += put(s, rec[1], rlen[1]) == -2;
        make_pad(0, 1); ((drk_pad_t *)(void *)rec[1])->codec = 1;
        bad += put(s, rec[1], rlen[1]) == -2;
        make_pad(0, 1);
        bad += put(s, rec[1], rlen[1] - 16u) == -2;      /* cut short: the take runs past the end */
        check("headers, pads, rates, codecs and streams the player cannot trust are refused", bad == 5u && !rec_tab[s].n &&
              !rec_pad_stored(s));
        kit(3); trk_note_on(&trk[0], 38, 120);
        check("... and leave the kit empty (silent)", run(0, 40u) == 0 && !rec_used(3));
        make_pad(0, 0);
        kit(2); trk_note_on(&trk[0], 36, 120); run(a, 100u);
        fail_after = 5;
        int rc = put(2u * MOD3_RPAD, rec[0], rlen[0]);
        fail_after = -1;
        kit(2); trk_note_on(&trk[0], 36, 120); run(b, 100u);
        check("a torn write leaves the pad's take as it was", rc != 0 && rec_tab[2u * MOD3_RPAD].n && !memcmp(a, b, 100u * CTL * sizeof a[0]));
    }

    {   /* boot */
        memset(rec_tab, 0, sizeof rec_tab); memset(rec_nm, 0, sizeof rec_nm);
        rec_scan();
        check("the boot scan finds the kits and their names again", rec_tab[2u * MOD3_RPAD + 9u].n && !strcmp(rec_nm[2], "ROCK") &&
              rec_used(2) == 1 && !rec_used(3));
    }

    {   /* boot before SAMPLER is listed (a module written after power-on; nightly dc2209de scanned before mod_boot) */
        const engine_t *smp = ENGINES[ENGI_SAMPLER];
        memset(rec_tab, 0, sizeof rec_tab);
        ENGINES[ENGI_SAMPLER] = &ENG_NONE; sch_gen++;
        rec_scan();
        int waited = !rec_tab[2u * MOD3_RPAD].n && (rec_pend >> 2 & 1u);
        int hidden = rec_hidden(2u * MOD3_RPAD) && rec_used(2) == 2 && !rec_hidden(3u * MOD3_RPAD);
        ENGINES[ENGI_SAMPLER] = smp; sch_gen++;
        rec_recheck();                                    /* (eng_module_main.c mod_register: SAMPLER in SRAM) */
        check("kits found before their synth is listed wait for it, then play (not dropped until a save), named again",
              waited && rec_tab[2u * MOD3_RPAD].n && rec_tab[2u * MOD3_RPAD + 15u].n && !rec_pend && !strcmp(rec_nm[2], "ROCK"));
        check("... and while they wait each pad is a take, not an empty pad a take may write over",
              hidden && !rec_hidden(2u * MOD3_RPAD));
    }

    {   /* a record of another form where a kit's name goes (Melodee 1.x's KIT1 kit): no name, the kit still taken */
        static uint8_t old[600];
        memset(old, 0x11, sizeof old); memcpy(old, "KIT1", 4); memcpy(old + 8, "OLDKT", 5);
        int rc = st_project_save(rec_name_obj(6), old, sizeof old);
        rec_scan();
        check("a 1.x kit record where a name goes names nothing; the kit stays taken (Record kit passes it by)",
              !rc && !rec_nm[6][0] && rec_used(6) == 1);
        st_project_delete(rec_name_obj(6));
        rec_scan();
    }

    {   /* delete while it rings */
        kit(2); trk_note_on(&trk[0], 49, 127); run(0, 20u);
        uint32_t gen = st_pool_gen;
        int rc = rec_delete(2);
        int32_t pk = run(0, 4u);
        trk_note_on(&trk[0], 38, 120);
        check("a kit deleted while it rings: silent at once and after, the kit empty, its sectors free",
              !rc && pk == 0 && run(0, 40u) == 0 && !rec_used(2) && !rec_nm[2][0] && st_project_free() == T_FREE0);
        check("... and the room it gave back is counted again (SLOTS card: st_pool_gen moved on)", st_pool_gen != gen);
    }
    if (fails) { printf("USER KIT: %d FAILED\n", fails); return 1; }
    printf("USER KIT: all checks ok\n");
    return 0;
}
