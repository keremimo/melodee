/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Record kit's take (SAMPLER's: modules/kits/kit_take.c, kit_take_core.c, drum_enc.c through the firmware's take calls,
 * rec_store.c; usb_audio_stream.c kt_hear) on a simulated NOR flash: the capture keeps ~3 ms before the trigger, fills
 * a ring the main loop empties and ends at silence (or when the synth falls a whole ring behind); a take streamed in
 * pieces of any size is byte for byte the whole-buffer encoding; the store is made ready before the take (dirty free
 * sectors erased then) and nothing is erased while it is recorded; the device encoder decodes back through the
 * player near its SNR and near tools/drum_codec.c's full search; a kick goes in at 22.05 kHz; a take longer than a
 * project's eight sectors (its index blocks) and one that fills the whole store are kept and play; a quiet take, an
 * aborted one and a failed write leave nothing; a pad recorded again frees its old sectors; a kit's sixteen pads, its
 * blob out and in again; the player holds a long take past the drum kits' 6 s. Demos into argv[1]. */
static unsigned char nor[0x100000];
#define REC_BASE ((const uint8_t *)nor)        /* (the data records' flash: the player reads them in place) */
#ifndef MELODEE_USB_AUDIO
#define MELODEE_USB_AUDIO 1
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <sys/stat.h>
static void fm1_wdt_feed(void) {}
static uint8_t flash_ok = 1;
static uint32_t erases;                          /* sector erases counted */
static uint32_t progs;                           /* programs counted */
static int prog_fail = -1;                       /* a program fails after this many (-1 never) */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { erases++; memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (prog_fail == 0) return -9;
    if (prog_fail > 0) prog_fail--;
    progs++;
    for (uint32_t i = 0; i < n; i++) nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/rec_store.c"            /* (SAMPLER's take, encoder and record check: built in) */

static int fails;
static void check(const char *s, int ok) { printf("KIT TAKE: %-86s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }
static uint32_t lcg = 12345u;
static int32_t noise(void) { lcg = lcg * 1664525u + 1013904223u; return (int32_t)(lcg >> 16) - 32768; }
#define CAP 48000u
#define LONG (FS * 24u)
static int16_t ring[CAP], src[LONG + 64u], y[LONG], z[LONG];
static uint32_t words[LONG / 2u];

/* test hits, 44.1 kHz: a kick (a falling sine), a snare (noise and a tone), a hat (short noise), a crash (long noise) */
static uint32_t hit(uint32_t kind, int16_t *x, float level)
{
    uint32_t n = kind == 0 ? FS / 2u : kind == 1 ? FS / 4u : kind == 2 ? FS / 16u : FS * 3u;
    float ph = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        float t = (float)i / (float)FS, v;
        if (kind == 0) { ph += 6.2831853f * (50.0f + 70.0f * dlk_exp2(-t * 30.0f)) / (float)FS; v = sinf(ph) * dlk_exp2(-t * 9.0f); }
        else if (kind == 1) v = ((float)noise() / 32768.0f * 0.7f + 0.3f * sinf(6.2831853f * 200.0f * t)) * dlk_exp2(-t * 25.0f);
        else if (kind == 2) v = (float)noise() / 32768.0f * dlk_exp2(-t * 90.0f);
        else v = (float)noise() / 32768.0f * dlk_exp2(-t * 0.8f);
        x[i] = (int16_t)(v * level * 30000.0f);
    }
    for (uint32_t i = n; i < n + 64u; i++) x[i] = 0;
    return n;
}
/* n samples of a steady sound (a chord and some noise): a long take that never goes quiet */
static uint32_t steady(int16_t *x, uint32_t n, float level)
{
    for (uint32_t i = 0; i < n; i++) {
        float t = (float)i / (float)FS;
        float v = 0.3f * sinf(6.2831853f * 220.0f * t) + 0.2f * sinf(6.2831853f * 277.2f * t) + 0.15f * sinf(6.2831853f * 329.6f * t) +
                  0.1f * (float)noise() / 32768.0f;
        x[i] = (int16_t)(v * level * 32000.0f);
    }
    for (uint32_t i = n; i < n + 64u; i++) x[i] = 0;
    return n;
}
/* the computer's playback, block by block, into kt_hear */
static void play(const int16_t *x, uint32_t n)
{
    static uint32_t pr;
    for (uint32_t at = 0; at < n; at += CTL) {
        for (uint32_t i = 0; i < CTL; i++) {
            uint32_t pi = ((pr + i) & (UA_RING - 1u)) * 2u;
            int16_t s = at + i < n ? x[at + i] : 0;
            ua.play[pi] = ua.play[pi + 1u] = s;
        }
        if (kt.state) kt_hear(pr, CTL);
        pr += CTL;
    }
}
/* SAMPLER's take calls, as Record kit makes them */
static uint32_t stream(uint32_t u, uint32_t pad, const int16_t *x, uint32_t n, uint32_t op)
{
    return ENGINES[ENGI_SAMPLER]->rec_stream(u, pad, x, n, op);
}
/* a whole take into kit u's pad: its room in *room, the samples in pieces of 1..max, ended. MOD3_TK_*; *fed the
 * samples it took, *during the flash's erases and programs while they were fed */
static uint32_t take(uint32_t u, uint32_t pad, const int16_t *x, uint32_t n, uint32_t max, int *room, uint32_t *fed,
                     uint32_t *during)
{
    uint32_t at = 0, st = MOD3_TK_NONE, e0, p0;
    *room = rec_take_begin(u * MOD3_RPAD + pad);
    if (*room < 0) return MOD3_TK_ROOM;
    stream(u, pad, 0, (uint32_t)*room, MOD3_TS_BEGIN);
    e0 = erases; p0 = progs;
    while (at < n && st == MOD3_TK_NONE) {
        uint32_t k = 1u + (uint32_t)(noise() & 0x7FFF) % max;
        if (k > n - at) k = n - at;
        st = stream(u, pad, x + at, k, MOD3_TS_FEED);
        at += k;
    }
    if (fed) *fed = at;
    if (during) *during = erases - e0 + progs - p0;
    st = stream(u, pad, 0, at, MOD3_TS_END);
    return st;
}
static const drk_pad_t *pad_head(uint32_t s)
{
    return rec_tab[s].n ? (const drk_pad_t *)(const void *)DRK_SEC(rec_list[rec_tab[s].at]) : 0;
}
/* pad record s decoded through the player's channel, its n samples */
static uint32_t decode(uint32_t s, int16_t *out)
{
    const drk_pad_t *H = pad_head(s);
    drk_ch_t c;
    if (!H) return 0;
    memset(&c, 0, sizeof c);
    c.list = &rec_list[rec_tab[s].at];
    drk_stream_start(&c, H);
    for (uint32_t i = 0; i < H->n; i++) out[i] = (int16_t)drk_next(&c);
    return H->n;
}
/* words (contiguous slices) decoded as the player would, n samples */
static void decode_words(const uint32_t *w, uint32_t n, int16_t *out)
{
    uint32_t h[4] = {0}, idx = 0, k = 0;
    int32_t hh[4] = {0, 0, 0, 0}, wt[4] = {0, 0, -(1 << 13), 1 << 14};
    (void)h;
    while (idx < n) {
        uint64_t c = ((uint64_t)w[2u * k] << 32) | w[2u * k + 1u];
        uint32_t m = (uint32_t)(c >> 62), sf = (uint32_t)(c >> 58) & 15u, b = m + 1u, ns = DRK_NS[m];
        k++;
        for (uint32_t i = 0; i < ns && idx < n; i++) {
            uint32_t bk = (uint32_t)(c >> (58u - b * (i + 1u))) & ((1u << b) - 1u);
            int32_t r = (DRK_LV[m][bk] * (int32_t)DRK_SF[sf]) >> 2, d = r >> 4;
            uint32_t p = 0;
            for (uint32_t j = 0; j < 4u; j++) p += (uint32_t)hh[j] * (uint32_t)wt[j];
            int32_t s = ((int32_t)p >> 13) + r;
            s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
            for (uint32_t j = 0; j < 4u; j++) wt[j] += hh[j] < 0 ? -d : d;
            hh[0] = hh[1]; hh[1] = hh[2]; hh[2] = hh[3]; hh[3] = s;
            out[idx++] = (int16_t)s;
        }
    }
}
/* pad record s's slices as contiguous words (the payload from byte 72), their bytes */
static uint32_t slices_of(uint32_t s, uint32_t *w)
{
    st_hdr_t h;
    int copy = st_current(rec_pad_obj(s), &h);
    if (copy < 0 || st_read_range(rec_pad_obj(s), (uint32_t)copy, DRK_PAD_BYTES, w, h.len - DRK_PAD_BYTES)) return 0;
    return h.len - DRK_PAD_BYTES;
}
static void kit_on(uint32_t u)
{
    host_tracks_init();
    for (uint32_t p = 0; p < NPART; p++) memset(trk[p].v, 0, NVOICE * sizeof *trk[p].v);
    eng_state_reset();
    host_preset(&trk[0], ENGI_SAMPLER, 0);
    trk[0].p[P_E0] = (int16_t)(DK_USER0 + u);
}
static int32_t run(int32_t *buf, uint32_t nb)
{
    int32_t o[CTL], pk = 0;
    for (uint32_t i = 0; i < nb; i++) {
        track_render(&trk[0], o, CTL);
        for (uint32_t k = 0; k < CTL; k++) { if (buf) buf[i * CTL + k] = o[k]; pk = abs(o[k]) > pk ? abs(o[k]) : pk; }
    }
    return pk;
}
static double snr_db(const int16_t *a, const int16_t *b, uint32_t n)
{
    double s = 0, e = 0;
    for (uint32_t i = 0; i < n; i++) { s += (double)a[i] * a[i]; e += (double)(a[i] - b[i]) * (a[i] - b[i]); }
    return e ? 10.0 * log10(s / e) : 99.0;
}
static uint32_t pool_free(void) { int f = st_project_free(); return f < 0 ? 0u : (uint32_t)f; }

int main(int argc, char **argv)
{
    static uint8_t stage[FS * 16u];
    memset(nor, 0xFF, sizeof nor);
    rec_scan();                                  /* (the records' boot scan: the host's record calls SAMPLER writes through) */
    rec_take_stage(stage, sizeof stage);         /* (Record kit's: the RAM a take is encoded into) */
    check("KIT 1..8 are 9..16, straight after FM; sixteen pads a kit", DK_USER0 == DK_FM + 1 && DK_KIT_MAX == 16 &&
          ENG_SAMPLER.edit[0].max == DK_KIT_MAX && !strcmp(N_DRUM_KIT[DK_USER0], "KIT 1") && DRK_NPAD == 16u &&
          MOD3_RPAD == 16u);

    {   /* the capture: a ring the main loop empties */
        static int16_t silence[FS];
        uint32_t n = hit(0, src, 1.0f), lead = 0;
        kt.buf = ring; kt.cap = CAP; kt.pw = kt.n = kt.r = 0; kt.over = 0; kt.state = KT_ARMED;
        play(silence, 3000u);
        play(src, n);
        for (uint32_t k = 0; k < 40u && kt.state != KT_DONE; k++) play(silence, FS / 20u);
        while (lead < KT_PRE && abs(ring[lead]) < KT_ON) lead++;
        check("a take starts at the first hit with ~3 ms in front, ends at 150 ms of silence",
              kt.state == KT_DONE && !kt.over && lead == KT_PRE - 1u && kt.n >= n && kt.n < n + KT_PRE + KT_END + CTL);
        uint32_t m = steady(src, FS * 3u, 0.5f);
        kt.pw = kt.n = kt.r = 0; kt.over = 0; kt.state = KT_ARMED;
        play(src, m);
        check("a take no one empties ends a ring later (the synth fell behind)", kt.state == KT_DONE && kt.over && kt.n == CAP);
        kt.pw = kt.n = kt.r = 0; kt.ri = 0; kt.over = 0; kt.state = KT_ARMED;
        for (uint32_t at = 0; at < m; at += 4096u) {   /* (the main loop's share: what came, fed on) */
            play(src + at, m - at < 4096u ? m - at : 4096u);
            kt.r = kt.n;
        }
        check("... and one the main loop keeps up with goes on past the ring", kt.state == KT_REC && !kt.over && kt.n > CAP);
        kt.state = KT_OFF;
    }

    {   /* the encoder */
        uint32_t n = hit(1, src, 0.8f), ok = 1;
        dke_out_t o;
        int32_t snrs[3] = {30, 18, 6};
        double got[3];
        for (uint32_t q = 0; q < 3u; q++) {
            uint32_t bytes = dke_encode(src, n, snrs[q], 0, words, &o);
            decode_words(words, n, y);
            got[q] = snr_db(src, y, n);
            ok &= got[q] > (snrs[q] < 18 ? snrs[q] : 18) - 3.0 && bytes == o.bytes;   /* (four bits a sample at most) */
        }
        printf("KIT TAKE:   snare at 30/18/6 dB: %.1f / %.1f / %.1f dB\n", got[0], got[1], got[2]);
        check("the device encoder decodes back near its SNR, fewer bits for less", ok && got[0] >= got[1] && got[1] > got[2]);
        /* the tool's full search on the same samples (no feedback, no emphasis): the fast one stays near it */
        uint32_t near = 1;
        for (uint32_t kind = 0; kind < 4u; kind++) {
            uint32_t m = hit(kind, src, 0.8f);
            if (m > FS) m = FS;
            for (uint32_t i = m; i < m + 64u; i++) src[i] = 0;
            FILE *f = fopen("build/host/kit_take_in.s16", "wb");
            fwrite(src, 2, m, f); fclose(f);
            int rc = system("cc -O1 -o build/host/drum_codec tools/drum_codec.c -lm 2>/dev/null && build/host/drum_codec "
                            "build/host/kit_take_in.s16 build/host/kit_take_tool.bin build/host/kit_take_dec.s16 18 0 0 > /dev/null");
            f = fopen("build/host/kit_take_dec.s16", "rb");
            uint32_t tn = f ? (uint32_t)fread(z, 2, m, f) : 0;
            if (f) fclose(f);
            f = fopen("build/host/kit_take_tool.bin", "rb");
            fseek(f, 0, SEEK_END); uint32_t tb = (uint32_t)ftell(f); fclose(f);
            uint32_t bytes = dke_encode(src, m, 18, 0, words, &o);
            decode_words(words, m, y);
            double mine = snr_db(src, y, m), tool = snr_db(src, z, m);
            printf("KIT TAKE:   kind %u at 18 dB: %u bytes %.1f dB, the tool %u bytes %.1f dB\n", kind, bytes, mine, tb, tool);
            near &= !rc && tn == m && mine > tool - 1.5 && bytes * 10u <= tb * 11u;
        }
        check("... within 1.5 dB of tools/drum_codec.c's full search, at most 10 % more bytes", near);
    }

    {   /* a take streamed in: the whole-buffer encoding, byte for byte; nothing erased while it is recorded */
        uint32_t n = hit(1, src, 0.5f), fed, during, f0 = pool_free(), dirty = 0;
        int room;
        for (uint32_t i = 0; i < ST_LN; i++)     /* (free sectors left dirty: a deleted project's) */
            if (st_lin_ok(i) && i % 3u == 0u) { memset(nor + st_lin_at(i) + 64u, 0x5A, 64); dirty++; }
        uint32_t e0 = erases, r = take(0, 1, src, n, 700u, &room, &fed, &during);
        const drk_pad_t *H = pad_head(1);
        dke_out_t o;
        uint32_t bytes = dke_encode(src, n, TK_SNR, 0, words, &o), sb = slices_of(1, words + bytes / 4u);
        uint32_t pk = 0;
        for (uint32_t i = 0; i < n; i++) pk = (uint32_t)abs(src[i]) > pk ? (uint32_t)abs(src[i]) : pk;
        check("a snare kept on pad 2 of KIT 1 at 44.1 kHz, normalized by its peak", r == MOD3_TK_KEPT && H && H->pad == 1u &&
              H->inc == 32768u && H->gain == 32000u * 1024u / pk && !H->ms && H->n <= n);
        check("its slices are the whole take's encoding byte for byte (fed in pieces of 1..700)", sb == bytes &&
              !memcmp(words, words + bytes / 4u, bytes));
        check("nothing erased or programmed while it was recorded (USB audio's service never held), the store written after",
              !during && erases - e0 >= rec_tab[1].n);
        (void)dirty;
        uint32_t kn = decode(1, y);
        check("... it decodes through the player near its SNR", kn == H->n && snr_db(src, y, kn) > 20.0);
        check("the store less its sectors", pool_free() == f0 - rec_tab[1].n);
        (void)room;
    }

    {   /* a kick at 22.05 kHz; the pad recorded again frees its old sectors */
        uint32_t n = hit(0, src, 0.6f), fed, during, r, f0;
        int room;
        r = take(0, 0, src, n, 2000u, &room, &fed, &during);
        const drk_pad_t *H = pad_head(0);
        check("a kick is kept at 22.05 kHz, about half its samples", r == MOD3_TK_KEPT && H && H->inc == 16384u &&
              H->n > n / 2u - 3000u && H->n <= n / 2u + 2u);
        f0 = pool_free();
        n = hit(0, src, 0.9f);
        r = take(0, 0, src, n / 2u, 2000u, &room, &fed, &during);
        check("the kick recorded again: one record, the old sectors free", r == MOD3_TK_KEPT && pool_free() + rec_tab[0].n >= f0 &&
              pad_head(1) != 0);
    }

    {   /* a quiet take, an aborted one, a failed write: nothing */
        uint32_t f0 = pool_free(), fed, during, r;
        int room;
        for (uint32_t i = 0; i < 4000u; i++) src[i] = (int16_t)(noise() / 400);
        r = take(0, 5, src, 4000u, 500u, &room, &fed, &during);
        check("a take that never reached -40 dB is refused, nothing stored", r == MOD3_TK_QUIET && !rec_tab[5].n &&
              !rec_pad_stored(5) && pool_free() == f0);
        uint32_t n = hit(1, src, 0.7f);
        room = rec_take_begin(5);
        stream(0, 5, 0, (uint32_t)room, MOD3_TS_BEGIN);
        stream(0, 5, src, n / 2u, MOD3_TS_FEED);
        stream(0, 5, 0, 0, MOD3_TS_ABORT);
        check("an aborted take leaves nothing", !rec_pad_stored(5) && pool_free() == f0);
        prog_fail = 40;
        r = take(0, 5, src, n, 3000u, &room, &fed, &during);
        prog_fail = -1;
        check("a write that fails leaves nothing (Not saved)", r == MOD3_TK_FLASH && !rec_pad_stored(5) && pool_free() == f0);
    }

    {   /* a long take: past a project's eight sectors (its index blocks); then one that fills the whole store */
        uint32_t n = steady(src, FS * 8u, 0.7f), fed, during, r;
        int room;
        r = take(2, 9, src, n, 4096u, &room, &fed, &during);
        uint32_t s = 2u * MOD3_RPAD + 9u, kn = decode(s, y);
        dke_out_t o;
        uint32_t bytes = dke_encode(src, n, TK_SNR, 0, words, &o);
        decode_words(words, n, z);
        printf("KIT TAKE:   8 s: %u bytes in %u sectors, %u erases or programs while recorded\n", bytes + DRK_PAD_BYTES, rec_tab[s].n, during);
        check("an 8 s take is kept: more than eight sectors, every sample as the whole encoding decodes",
              r == MOD3_TK_KEPT && rec_tab[s].n > 8u && kn == n && !memcmp(y, z, n * 2u) && !during);
        rec_scan();
        check("... found again at boot (its index blocks)", rec_tab[s].n > 8u && decode(s, z) == n && !memcmp(y, z, n * 2u));
        uint32_t f0 = pool_free(), m = steady(src, LONG, 0.7f);
        r = take(3, 15, src, m, 4096u, &room, &fed, &during);
        s = 3u * MOD3_RPAD + 15u;
        kn = decode(s, y);
        printf("KIT TAKE:   the store's last %u sectors: %.1f s of %.1f s fed, %u erases or programs while recorded\n", f0, kn / (double)FS,
               fed / (double)FS, during);
        check("a take longer than the store fills every free sector, ends there and is kept", r == MOD3_TK_KEPT &&
              rec_tab[s].n == f0 && pool_free() == 0u && kn > (f0 * 4056u / 30000u) * FS / 4u && fed < m && !during);
        bytes = dke_encode(src, kn, TK_SNR, 0, words, &o);
        decode_words(words, kn, z);
        check("... what it holds decodes as the whole encoding of its first samples", !memcmp(y, z, kn * 2u));
        r = take(3, 14, src, FS, 4096u, &room, &fed, &during);
        check("a take into a full store: No room", r == MOD3_TK_ROOM && !rec_pad_stored(3u * MOD3_RPAD + 14u));
        rec_delete_pad(s);
        rec_take_stage(stage, 65536u);           /* (Record kit's RAM smaller than the store's room) */
        r = take(3, 15, src, m, 4096u, &room, &fed, &during);
        kn = decode(s, y);
        printf("KIT TAKE:   a 64 KB stage: %.1f s kept of %.1f s fed\n", kn / (double)FS, fed / (double)FS);
        check("a take longer than its RAM ends when the RAM is full and is kept", r == MOD3_TK_KEPT && room == 65536 &&
              kn > FS * 2u && fed < m && !during);
        rec_take_stage(stage, sizeof stage);
        rec_delete_pad(s);
        check("deleted: the store free again", pool_free() == f0);
        kit_on(2);
        trk_note_on(&trk[0], 49, 120);                 /* (CYMB: pad 10, the 8 s take) */
        run(0, FS * 13u / 2u / CTL);
        int32_t late = run(0, FS / 4u / CTL);
        check("the player holds a long take past the kits' 6 s", late > 1000);
        rec_delete_pad(2u * MOD3_RPAD + 9u);
    }

    {   /* sixteen pads, the kit's blob out and in */
        uint32_t ok = 1, fed, during;
        int room;
        for (uint32_t p = 0; p < DRK_NPAD; p++) {
            uint32_t n = hit(p % 4u == 3u ? 2u : p % 4u, src, 0.9f);
            ok &= take(4, p, src, n, 3000u, &room, &fed, &during) == MOD3_TK_KEPT;
        }
        check("sixteen pads of KIT 5, each a record of its own", ok && rec_used(4) == 1);
        static uint8_t blob[200000], back[200000];
        uint32_t len = rec_box_open(4);
        check("... its blob: the head, then each pad's record", len > REC_BOX_HEAD && len < sizeof blob &&
              !rec_box_read(0, blob, len) && rec_r32(blob) == REC_BOX_MAGIC);
        static st_stream_t w;
        int rc = rec_box_put_begin(&w, 5, len);
        if (!rc) rc = rec_box_put_write(blob, len);
        if (!rc) rc = rec_box_put_end();
        uint32_t len2 = rec_box_open(5);
        if (rc || len2 != len) printf("KIT TAKE:   blob in: rc %d, %u of %u bytes\n", rc, len2, len);
        check("... into KIT 6: the same sixteen pads", !rc && len2 == len && !rec_box_read(0, back, len2) && !memcmp(back, blob, len));
        if (argc > 1) {
            static int32_t out[FS * 6u];
            static const uint8_t pat[16] = {36, 42, 38, 42, 36, 46, 38, 42, 41, 45, 50, 37, 39, 49, 56, 64};
            char path[256];
            mkdir(argv[1], 0755);
            kit_on(4);
            for (uint32_t i = 0; i < 16u; i++) { trk_note_on(&trk[0], pat[i], 110); run(out + i * (FS / 8u / CTL) * CTL, FS / 8u / CTL); }
            run(out + 16u * (FS / 8u / CTL) * CTL, FS * 3u / CTL);
            snprintf(path, sizeof path, "%s/user_kit.wav", argv[1]);
            FILE *f = fopen(path, "wb");
            if (f) { wav_hdr(f, (16u * (FS / 8u / CTL) + FS * 3u / CTL) * CTL); for (uint32_t i = 0; i < (16u * (FS / 8u / CTL) + FS * 3u / CTL) * CTL; i++) { int16_t v = (int16_t)clamp(out[i] >> 8, -32768, 32767); fwrite(&v, 2, 1, f); } fclose(f); printf("KIT TAKE:   demo %s\n", path); }
        }
        rc = rec_delete(4);
        check("the kit deleted: every pad gone", !rc && !rec_used(4));
    }
    if (fails) { printf("KIT TAKE: %d FAILED\n", fails); return 1; }
    printf("KIT TAKE: all checks ok\n");
    return 0;
}
