/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Record kit's take (DRUM's: modules/drum/kit_take.c, drum_enc.c through the firmware's data records, rec_store.c;
 * usb_audio_stream.c kt_hear) on a simulated NOR flash: the
 * capture keeps ~3 ms before the trigger and ends at silence or a full buffer; the device encoder decodes back
 * through the player within its SNR and writes what tools/drum_codec.c writes for the same samples; a kick goes in at
 * 22.05 kHz, a snare at 44.1 kHz, a cymbal still ringing when the buffer filled loops; a pad rewritten leaves the
 * others' slices as they were; a quiet take is refused; all eleven pads fill the kit within its cap; cleared pads go
 * and the last one takes the kit with it. Demos of the kit into argv[1]. */
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
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/rec_store.c"            /* (DRUM's take, encoder and record check: built in) */

static int fails;
static void check(const char *s, int ok) { printf("KIT TAKE: %-86s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }
static uint32_t lcg = 12345u;
static int32_t noise(void) { lcg = lcg * 1664525u + 1013904223u; return (int32_t)(lcg >> 16) - 32768; }
#define CAP 48000u
static int16_t take[CAP + 64u], src[FS * 4u];

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
static uint32_t record(const int16_t *x, uint32_t n)
{
    static int16_t silence[FS];
    memset(take, 0, sizeof take);
    kt.buf = take; kt.cap = CAP; kt.pw = kt.n = 0; kt.state = KT_ARMED;
    play(silence, 3000u);
    play(x, n);
    for (uint32_t k = 0; k < 40u && kt.state != KT_DONE; k++) play(silence, FS / 20u);
    return kt.n;
}
static const drk_kit_t *head(uint32_t u)
{
    const uint32_t *secs[DRK_KSEC];
    return kt_secs(u, secs) ? (const drk_kit_t *)(const void *)secs[0] : 0;
}
/* contiguous slices (words) laid out as the player finds them in flash: per sector 476, the guard's place, 31 more */
static uint32_t phys[8][DRK_SLOTS * 2u];
static void phys_secs(const uint32_t *words, uint32_t bytes, drk_part_t *P)
{
    memset(phys, 0xFF, sizeof phys);
    for (uint32_t k = 0; k < bytes / 8u && k / DRK_SECSLICES < 8u; k++)
        memcpy(&phys[k / DRK_SECSLICES][DRK_SLOT(k % DRK_SECSLICES)], words + 2u * k, 8u);
    for (uint32_t k = 0; k < DRK_KSEC; k++) P->sec[k] = phys[k];
}
/* pad d of slot u decoded through the player's channel */
static uint32_t decode(uint32_t u, uint32_t d, int16_t *y)
{
    const uint32_t *secs[DRK_KSEC];
    static drk_part_t P;
    uint32_t nsec = kt_secs(u, secs);
    const drk_kit_t *H = (const drk_kit_t *)(const void *)secs[0];
    const drk_sample_t *s = &H->smp[H->map[d].sample];
    drk_ch_t c;
    memset(&c, 0, sizeof c);
    for (uint32_t k = 0; k < nsec; k++) P.sec[k] = secs[k];
    c.secs = P.sec;
    drk_stream_start(&c, s);
    for (uint32_t i = 0; i < s->n; i++) y[i] = (int16_t)drk_next(&c);
    return s->n;
}
static void kit_on(uint32_t u)
{
    host_tracks_init();
    for (uint32_t p = 0; p < NPART; p++) memset(trk[p].v, 0, NVOICE * sizeof *trk[p].v);
    eng_state_reset();
    host_preset(&trk[0], ENGI_DRUM, 0);   /* (808 KIT: on both boards) */
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

int main(int argc, char **argv)
{
    static int16_t y[FS * 2u], z[FS * 2u];
    static uint32_t words[FS];
    memset(nor, 0xFF, sizeof nor);
    rec_scan();                                  /* (the records' boot scan: the host's record calls DRUM writes through) */
    check("KIT 1..8 are 9..16, straight after FM", DK_USER0 == DK_FM + 1 && DK_KIT_MAX == 16 &&
          ENG_DRUM.edit[0].max == DK_KIT_MAX && !strcmp(N_DRUM_KIT[DK_USER0], "KIT 1") && !strcmp(N_DRUM_KIT[DK_KIT_MAX], "KIT 8"));

    {   /* the capture */
        uint32_t n = hit(0, src, 1.0f), got = record(src, n);
        uint32_t lead = 0;
        while (lead < KT_PRE && abs(take[lead]) < KT_ON) lead++;
        check("a take starts at the first hit with ~3 ms in front, ends at 150 ms of silence",
              kt.state == KT_DONE && lead == KT_PRE - 1u && got >= n && got < n + KT_PRE + KT_END + CTL);
        uint32_t m = hit(3, src, 1.0f);
        got = record(src, m);
        check("a long one ends when the buffer is full", kt.state == KT_DONE && got == CAP);
        {   /* the player closes its stream right after the sound: no silence arrives, the stopped stream is it */
            static int16_t silence[FS];
            static int32_t out[2u * CTL];
            uint32_t blocks = 0;
            memset(take, 0, sizeof take);
            kt.buf = take; kt.cap = CAP; kt.pw = kt.n = 0; kt.state = KT_ARMED;
            play(silence, 3000u); play(src, FS / 5u);
            uint32_t at = kt.n;
            ua.play_alt = 0;
            while (kt.state == KT_REC && blocks < 1000u) { memset(out, 0, sizeof out); ua_audio(out, 0, CTL, 4096u); blocks++; }
            check("a take whose stream stops ends 150 ms later, nothing added", kt.state == KT_DONE && kt.n == at &&
                  blocks * CTL >= KT_END && blocks * CTL < KT_END + CTL);
        }
        kt.state = KT_OFF;
    }

    {   /* the encoder */
        uint32_t n = hit(1, src, 0.8f), ok = 1;
        for (uint32_t i = n; i < n + 64u; i++) src[i] = 0;
        dke_out_t o;
        int32_t snrs[3] = {30, 18, 6};
        double got[3];
        for (uint32_t q = 0; q < 3u; q++) {
            uint32_t bytes = dke_encode(src, n, snrs[q], 0, words, &o), at = 0, k;
            drk_ch_t c; static drk_part_t P;
            phys_secs(words, bytes, &P); k = 0;
            drk_sample_t s = {0, (uint16_t)n, 0, 0, 32768, 0, 0};
            memset(&c, 0, sizeof c); c.secs = P.sec;
            drk_stream_start(&c, &s);
            for (uint32_t i = 0; i < n; i++) y[i] = (int16_t)drk_next(&c);
            got[q] = snr_db(src, y, n);
            ok &= got[q] > (snrs[q] < 18 ? snrs[q] : 18) - 3.0 && bytes == o.bytes;   /* (four bits a sample at most) */
            (void)at;
        }
        printf("KIT TAKE:   snare at 30/18/6 dB: %.1f / %.1f / %.1f dB\n", got[0], got[1], got[2]);
        check("the device encoder decodes back through the player near its SNR, fewer bits for less", ok &&
              got[0] >= got[1] && got[1] > got[2]);
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
            drk_ch_t c; static drk_part_t P;
            phys_secs(words, bytes, &P);
            drk_sample_t s = {0, (uint16_t)m, 0, 0, 32768, 0, 0};
            memset(&c, 0, sizeof c); c.secs = P.sec;
            drk_stream_start(&c, &s);
            for (uint32_t i = 0; i < m; i++) y[i] = (int16_t)drk_next(&c);
            double mine = snr_db(src, y, m), tool = snr_db(src, z, m);
            printf("KIT TAKE:   kind %u at 18 dB: %u bytes %.1f dB, the tool %u bytes %.1f dB\n", kind, bytes, mine, tb, tool);
            near &= !rc && tn == m && mine > tool - 1.5 && bytes * 10u <= tb * 11u;
        }
        check("... within 1.5 dB of tools/drum_codec.c's full search, at most 10 % more bytes", near);
    }

    {   /* takes into slot 1 */
        uint32_t n = hit(0, src, 0.6f);
        record(src, n);
        uint32_t r = kt_keep(0, D9_BD, take, kt.n, CAP);
        const drk_kit_t *H = head(0);
        check("a kick is kept at 22.05 kHz, the kit named KIT 1", r == KM_KEPT && H && H->nsmp == 1u &&
              H->smp[H->map[D9_BD].sample].inc == 16384u && !strcmp(H->name, "KIT 1"));
        uint32_t kn = decode(0, D9_BD, y);
        check("... about half its samples, trimmed to the hit", kn > n / 2u - 300u && kn < n / 2u + 400u);
        n = hit(1, src, 0.5f);
        record(src, n);
        r = kt_keep(0, D9_SD, take, kt.n, CAP);
        H = head(0);
        uint32_t kn2 = decode(0, D9_BD, z);
        check("a snare at 44.1 kHz beside it, the kick's take as it was", r == KM_KEPT && H->nsmp == 2u &&
              H->smp[H->map[D9_SD].sample].inc == 32768u && kn2 == kn && !memcmp(y, z, kn * 2u));
        n = hit(3, src, 0.7f);
        record(src, n);
        r = kt_keep(0, D9_CR, take, kt.n, CAP);
        H = head(0);
        const drk_sample_t *c = &H->smp[H->map[D9_CR].sample];
        check("a crash still ringing at the buffer's end loops its tail and darkens", r == KM_KEPT && c->loop_at &&
              c->loop_at > c->n / 2u && c->dark == 2u);
        kit_on(0);
        trk_note_on(&trk[0], 49, 120); run(0, FS / CTL);
        int32_t late = run(0, (FS * 2u) / CTL / 4u);
        check("... and the player keeps it sounding past the take", late > 300);
        kit_on(0);
        trk_note_on(&trk[0], 36, 110);
        int32_t pk = run(0, FS / 4u / CTL);
        kit_on(0);
        trk_note_on(&trk[0], 42, 110);
        check("the track plays the kick; an empty pad is silent", pk > 3000 && run(0, FS / 10u / CTL) == 0);
        memset(take, 0, sizeof take);
        for (uint32_t i = 0; i < 2000u; i++) take[i] = (int16_t)(noise() / 400);
        check("a take that never reached -40 dB is refused", kt_keep(0, D9_RS, take, 2000u, CAP) == KM_QUIET && head(0)->nsmp == 3u);
    }

    {   /* every pad, long takes: the kit stays within its cap */
        uint32_t ok = 1;
        for (uint32_t d = 0; d < DRK_NDRUM; d++) {
            uint32_t n = hit(d % 4u == 3u ? 3u : d % 4u, src, 0.9f);
            record(src, n);
            uint32_t r = kt_keep(1, d, take, kt.n, CAP);
            if (r != KM_KEPT) printf("KIT TAKE:   pad %u: %u\n", d, r);
            ok &= r == KM_KEPT;
        }
        const drk_kit_t *H = head(1);
        check("eleven pads of long takes all fit the kit's 28,392 bytes", ok && H && H->nsmp == DRK_NDRUM && rec_len[1] <= DRK_KIT_MAX);
        printf("KIT TAKE:   eleven pads: %u bytes in %u sectors\n", rec_len[1], drk_user[1].n);
        if (argc > 1) {
            static int32_t out[FS * 6u];
            static const uint8_t pat[16] = {36, 42, 38, 42, 36, 46, 38, 42, 41, 45, 48, 37, 39, 49, 51, 42};
            char path[256];
            mkdir(argv[1], 0755);
            kit_on(1);
            for (uint32_t i = 0; i < 16u; i++) { trk_note_on(&trk[0], pat[i], 110); run(out + i * (FS / 8u / CTL) * CTL, FS / 8u / CTL); }
            run(out + 16u * (FS / 8u / CTL) * CTL, FS * 3u / CTL);
            snprintf(path, sizeof path, "%s/user_kit.wav", argv[1]);
            FILE *f = fopen(path, "wb");
            if (f) { wav_hdr(f, (16u * (FS / 8u / CTL) + FS * 3u / CTL) * CTL); for (uint32_t i = 0; i < (16u * (FS / 8u / CTL) + FS * 3u / CTL) * CTL; i++) { int16_t v = (int16_t)clamp(out[i] >> 8, -32768, 32767); fwrite(&v, 2, 1, f); } fclose(f); printf("KIT TAKE:   demo %s\n", path); }
        }
    }

    {   /* clearing */
        int rc = kt_write(0, D9_SD, 0, 0, 0, 0);
        const drk_kit_t *H = head(0);
        uint32_t kn = decode(0, D9_BD, z);
        check("a cleared pad goes, the others stay", !rc && H->nsmp == 2u && H->map[D9_SD].sample == 0xFFu && kn && kn == decode(0, D9_BD, y));
        rc = kt_write(0, D9_BD, 0, 0, 0, 0) | kt_write(0, D9_CR, 0, 0, 0, 0);
        check("the last pad cleared takes the kit with it", !rc && !drk_user[0].n && !rec_len[0]);
    }
    if (fails) { printf("KIT TAKE: %d FAILED\n", fails); return 1; }
    printf("KIT TAKE: all checks ok\n");
    return 0;
}
