/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The user's drum kits (src/kit_store.c, src/drum_real.c) on a simulated NOR flash: STUDIO's own streams written as a
 * user kit record through the pool play sample for sample as KIT STUDIO does (only the storage differs), across
 * sector edges and looped tails; an empty slot, a deleted kit and a rewritten slot are silent until the next hit;
 * the resources' absence does not matter; a record the player could not trust is refused and the old kit stays;
 * the boot scan finds the kits again; the KIT card shows a kit's name; a torn write leaves the slot as it was. */
static unsigned char nor[0x100000];
#define DRK_XIP(off) ((const uint8_t *)nor + (off))
#define DRK_TEST
static int res_absent_for_test;
#define RES_OK() (!res_absent_for_test)
#define main hostsim_main
#include "hostsim.c"
#undef main
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
#include "../firmware/src/kit_store.c"

static int fails;
static void check(const char *s, int ok) { printf("USER KIT: %-86s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }
enum { P_STUDIO = 6, P_BOOM = 7 };

/* a built-in stream's slices: as many as its samples take */
static uint32_t slices_of(const drk_sample_t *s)
{
    uint32_t k = s->at, idx = 0;
    while (idx < s->n) idx += DRK_NS[DRK_SLICES[2u * k++] >> 30];
    return k - s->at;
}
/* KIT (built-in index) as a user kit record into rec: its takes (the swapped kick left out), the same map; length */
static uint8_t rec[DRK_KIT_MAX + 64];
static uint32_t make_record(uint32_t kit, const char *name)
{
    drk_kit_t H;
    uint8_t to[64];
    uint32_t n = 0, at = DRK_KIT_BYTES / 8u;
    memset(&H, 0, sizeof H);
    memset(to, 0xFF, sizeof to);
    memset(rec, 0, sizeof rec);
    H.magic = DRK_KIT_MAGIC; H.bytes = DRK_KIT_BYTES; H.ndrum = DRK_NDRUM;
    memcpy(H.name, name, strlen(name));
    for (uint32_t d = 0; d < DRK_NDRUM; d++) {
        drk_map_t m = DRK_MAP[kit][d];
        if (d == D9_BD) m.alt = 0xFFu;
        uint8_t use[2] = {m.sample, m.alt};
        for (uint32_t j = 0; j < 2u; j++) {
            uint32_t o = use[j];
            if (o == 0xFFu || to[o] != 0xFFu) continue;
            drk_sample_t s = DRK_SAMPLES[o];
            uint32_t k = slices_of(&s);
            memcpy(rec + at * 8u, DRK_SLICES + 2u * s.at, k * 8u);
            if (s.loop_at) memcpy(H.loops[n], DRK_LOOPS[s.st], sizeof H.loops[n]);
            s.at = (uint16_t)at; s.st = (uint8_t)n;
            H.smp[n] = s;
            to[o] = (uint8_t)n++;
            at += k;
        }
        m.sample = to[m.sample];
        if (m.alt != 0xFFu) m.alt = to[m.alt];
        H.map[d] = m;
    }
    H.nsmp = (uint8_t)n;
    memcpy(rec, &H, sizeof H);
    return at * 8u;
}
static int put(uint32_t slot, const uint8_t *src, uint32_t len)
{
    st_stream_t w;
    int rc = kit_put_begin(&w, slot, len);
    for (uint32_t off = 0; !rc && off < len; off += 256u)
        rc = kit_put_write(&w, src + off, len - off < 256u ? len - off : 256u);
    return rc ? rc : kit_put_commit(&w, slot);
}

static void kit(uint32_t preset, int user_slot)
{
    host_tracks_init();
    for (uint32_t p = 0; p < NPART; p++)
        memset(trk[p].v, 0, NVOICE * sizeof *trk[p].v);
    eng_state_reset();
    host_preset(&trk[0], ENGI_DRUM, preset);
    if (user_slot >= 0) trk[0].p[P_E0] = (int16_t)(DK_USER0 + user_slot);
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
/* a groove over every drum: overlaps, the hat choke and takes, a crash and a ride ringing into their loops */
static void groove(int32_t *buf)
{
    static const uint8_t notes[] = {36, 42, 38, 42, 36, 46, 38, 42, 41, 45, 48, 37, 39, 49, 51, 42, 36, 38};
    uint32_t at = 0;
    for (uint32_t i = 0; i < sizeof notes; i++) {
        trk_note_on(&trk[0], notes[i], (uint8_t)(70 + (i * 13u) % 57u));
        run(buf + at, 40u);
        at += 40u * CTL;
    }
    run(buf + at, (uint32_t)(FS * 3u / CTL));           /* the cymbals' tails, looped */
}
#define GROOVE (18u * 40u * CTL + FS * 3u)

int main(void)
{
    static int32_t a[GROOVE], b[GROOVE];
    memset(nor, 0xFF, sizeof nor);
    check("KIT 12..19 are the user's: eight slots after TIGHT, names KIT 1..8 until written",
          DK_USER0 == DK_TIGHT + 1 && DK_KIT_MAX == DK_USER0 + 7 && ENG_DRUM.edit[0].max == DK_KIT_MAX &&
          !strcmp(N_DRUM_KIT[DK_USER0], "KIT 1") && !strcmp(N_DRUM_KIT[DK_KIT_MAX], "KIT 8"));

    uint32_t len = make_record(0, "ROCK");
    check("STUDIO's takes as a record fit seven sectors and cross sector edges", len <= DRK_KIT_MAX && len > 6u * DRK_SECBYTES);
    check("the record is written, checked and published into slot 3", put(2, rec, len) == 0 && drk_user[2].n == 7u &&
          kit_len[2] == len);

    kit(P_STUDIO, -1); groove(a);
    kit(P_STUDIO, 2); groove(b);
    uint32_t differ = 0, loud = 0;
    for (uint32_t i = 0; i < GROOVE; i++) { differ += a[i] != b[i]; loud += b[i] != 0; }
    check("KIT 3 plays sample for sample as KIT STUDIO (the same streams from the pool)", !differ && loud > GROOVE / 4u);

    res_absent_for_test = 1;
    kit(P_STUDIO, 2); groove(b);
    differ = 0;
    for (uint32_t i = 0; i < GROOVE; i++) differ += a[i] != b[i];
    check("a user kit plays without the resource region", !differ);
    kit(P_STUDIO, -1); trk_note_on(&trk[0], 38, 120);
    check("... the built-in kits do not", run(0, 40u) == 0);
    res_absent_for_test = 0;

    kit(P_STUDIO, 5); trk_note_on(&trk[0], 38, 120);
    check("an empty slot is silent", run(0, 40u) == 0);

    {   /* the card's name */
        const char *unit; char v[8];
        param_format(&ENG_DRUM.edit[0], DK_USER0 + 2, v, &unit);
        char e[8];
        param_format(&ENG_DRUM.edit[0], DK_USER0 + 5, e, &unit);
        check("the KIT card shows the kit's own name, EMPTY for none", !strcmp(v, "ROCK") && !strcmp(e, "EMPTY"));
    }

    {   /* rewritten while it rings: the next block is silent, the next hit is the new kit's */
        kit(P_STUDIO, 2); trk_note_on(&trk[0], 49, 127); run(0, 20u);
        uint32_t blen = make_record(1, "BOOM");
        int rc = put(2, rec, blen);
        int32_t pk = run(0, 4u);
        trk_note_on(&trk[0], 36, 120); run(a, 200u);
        kit(P_BOOM, -1); trk_note_on(&trk[0], 36, 120); run(b, 200u);
        check("a slot rewritten while it rings: silent at once, then the new kit plays",
              !rc && pk == 0 && !memcmp(a, b, 200u * CTL * sizeof a[0]));
        /* the old record's sectors are free again: a project-sized save may take them */
        check("the storage counts the kit and nothing else of it", st_project_free() == (int)ST_U_N - (int)((blen + DRK_SECBYTES - 1u) / DRK_SECBYTES));
    }

    {   /* refused records */
        uint32_t ok_len = make_record(0, "ROCK");
        uint8_t keep = rec[DRK_KIT_BYTES];
        rec[DRK_KIT_BYTES] ^= 0xC0;                      /* the first slice's mode: the stream walks past its end */
        uint32_t bad = 0;
        uint32_t tail = ok_len - 8u;
        (void)keep;
        memcpy(rec + 4, "\x00\x00", 2);                  /* bytes 0: not a header */
        bad += put(3, rec, ok_len) == -2;
        make_record(0, "ROCK"); memcpy(((drk_kit_t *)(void *)rec)->name, "TOOLONG", 7);
        bad += put(3, rec, ok_len) == -2;
        make_record(0, "ROCK"); ((drk_kit_t *)(void *)rec)->map[3].sample = 10;
        bad += put(3, rec, ok_len) == -2;
        make_record(0, "ROCK");
        bad += put(3, rec, tail) == -2;                  /* cut short: the last take runs past the end */
        check("headers, names, maps and streams the player cannot trust are refused", bad == 4u && !drk_user[3].n);
        kit(P_STUDIO, 3); trk_note_on(&trk[0], 38, 120);
        check("... and leave the slot empty (silent)", run(0, 40u) == 0);
        make_record(0, "ROCK");
        fail_after = 30;
        int rc = put(2, rec, ok_len);
        fail_after = -1;
        kit(P_BOOM, -1); trk_note_on(&trk[0], 36, 120); run(a, 100u);
        kit(P_STUDIO, 2); trk_note_on(&trk[0], 36, 120); run(b, 100u);
        check("a torn write leaves the slot's kit as it was", rc != 0 && drk_user[2].n && !memcmp(a, b, 100u * CTL * sizeof a[0]));
    }

    {   /* boot */
        memset(drk_user, 0, sizeof drk_user); memset(kit_len, 0, sizeof kit_len);
        kit_scan();
        check("the boot scan finds the kits again", drk_user[2].n && kit_len[2] && !drk_user[3].n);
    }

    {   /* delete while it rings */
        kit(P_STUDIO, 2); trk_note_on(&trk[0], 49, 127); run(0, 20u);
        int rc = kit_delete(2);
        int32_t pk = run(0, 4u);
        trk_note_on(&trk[0], 38, 120);
        check("a kit deleted while it rings: silent at once and after, the slot empty, its sectors free",
              !rc && pk == 0 && run(0, 40u) == 0 && !drk_user[2].n && !kit_len[2] && st_project_free() == (int)ST_U_N);
    }
    if (fails) { printf("USER KIT: %d FAILED\n", fails); return 1; }
    printf("USER KIT: all checks ok\n");
    return 0;
}
