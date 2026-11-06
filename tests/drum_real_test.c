/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* KIT STUDIO, BOOM, TIGHT (src/drum_real.c): the player's decoder sample for sample against the encoder; every drum alone
 * sounds, stays under the part's scale, renders the same twice and ends; the kit on one voice; a drum struck again
 * rings under its next hit without a step; overlaps; the hat choke and takes; KICK SWAP; a recorded hit's length; a kit
 * switch; no resources, no sound; the knobs' directions; levels next to the 808; no clipping at the corners; demos in
 * build/drum_real_demo/ (argv[1]). */
#define DRK_TEST
static int res_absent_for_test;
#define RES_OK() (!res_absent_for_test)
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <sys/stat.h>
static int fails;
static void check(const char *s, int ok) { printf("DRUM REAL: %-84s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }
enum { P_808 = 0, P_STUDIO = 6, P_BOOM = 7, P_TIGHT = 8 };
static const uint32_t KITS[3] = {P_STUDIO, P_BOOM, P_TIGHT};
static const char *const KIT_NAME[3] = {"STUDIO", "BOOM", "TIGHT"};

static void kit(uint32_t preset)
{
    host_tracks_init();
    for (uint32_t p = 0; p < NPART; p++)
        memset(trk[p].v, 0, NVOICE * sizeof *trk[p].v);
    eng_state_reset();
    host_preset(&trk[0], ENGI_DRUM, preset);
}
static uint32_t voices_on(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < NVOICE; i++)
        n += trk[0].v[i].active;
    return n;
}
static int32_t run(int32_t *buf, uint32_t nb)
{
    int32_t o[CTL], pk = 0;
    for (uint32_t i = 0; i < nb; i++) {
        track_render(&trk[0], o, CTL);
        for (uint32_t k = 0; k < CTL; k++) {
            if (buf)
                buf[i * CTL + k] = o[k];
            pk = abs(o[k]) > pk ? abs(o[k]) : pk;
        }
    }
    return pk;
}
static uint32_t to_end(uint32_t max)
{
    uint32_t i;
    for (i = 0; i < max && voices_on(); i++)
        run(0, 1);
    return i;
}
static int32_t largest_step(const int32_t *x, uint32_t n)
{
    int32_t pk = 0;
    for (uint32_t i = 1; i < n; i++)
        pk = abs(x[i] - x[i - 1]) > pk ? abs(x[i] - x[i - 1]) : pk;
    return pk;
}
static double rms(const int32_t *x, uint32_t n)
{
    double s = 0;
    for (uint32_t i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (n ? n : 1));
}
static double hf_share(const int32_t *x, uint32_t n)   /* the first difference's energy over the signal's */
{
    double d = 0, s = 0;
    for (uint32_t i = 1; i < n; i++) {
        d += (double)(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
        s += (double)x[i] * x[i];
    }
    return d / (s + 1);
}
static uint32_t crc32_of(const int16_t *x, uint32_t n)
{
    uint32_t c = ~0u;
    const uint8_t *p = (const uint8_t *)x;
    for (uint32_t i = 0; i < n * 2u; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
/* a groove in sixteenths, then the fill: kick, snare, hats, ghost notes, the crash on the one */
static void groove(const char *dir, const char *name, uint32_t preset, double bpm, uint32_t swing, int style)
{
    char path[256];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    kit(preset);
    double step = FS * 60.0 / bpm / 4.0;
    uint32_t bars = 8, nb = (uint32_t)((bars * 16 + 8) * step / CTL), last = ~0u;
    wav_hdr(f, nb * CTL);
    for (uint32_t k = 0; k < nb; k++) {
        double at = k * CTL / step;
        uint32_t s = (uint32_t)at;
        if (s & 1u) {                            /* swing: the off sixteenth late */
            if (at - s < swing / 100.0)
                s = last;
        }
        if (s != last && s < bars * 16u) {
            uint32_t q = s % 16u, bar = s / 16u, fill = bar % 4u == 3u && q >= 8u;
            last = s;
            if (s % 64u == 0u && s)
                trk_note_on(&trk[0], 49, 110);
            if (!fill) {
                if (style == 0) {                /* a rock backbeat, the ride in the back half */
                    if (q == 0 || q == 7 || q == 8 || q == 10) trk_note_on(&trk[0], 36, q == 0 ? 124 : 104);
                    if (q == 4 || q == 12) trk_note_on(&trk[0], 38, 122);
                    if (bar < 4u) {
                        if (q % 2u == 0u && q != 14) trk_note_on(&trk[0], 42, q % 4u ? 78 : 104);
                        if (q == 14) trk_note_on(&trk[0], 46, 100);
                    } else if (q % 2u == 0u)
                        trk_note_on(&trk[0], 51, q % 4u ? 80 : 104);
                } else if (style == 1) {         /* boom bap */
                    if (q == 0 || q == 3 || q == 10) trk_note_on(&trk[0], 36, q ? 108 : 124);
                    if (q == 4 || q == 12) trk_note_on(&trk[0], 38, 120);
                    if (q == 9 && bar % 2u) trk_note_on(&trk[0], 38, 58);
                    if (q % 2u == 0u) trk_note_on(&trk[0], 42, q % 4u ? 70 : 96);
                    if (q == 15) trk_note_on(&trk[0], 42, 54);
                    if (q == 12 && bar % 2u) trk_note_on(&trk[0], 39, 96);
                } else {                         /* funk: sixteenths on the hat, ghosts, a rim */
                    if (q == 0 || q == 6 || q == 10) trk_note_on(&trk[0], 36, q ? 106 : 122);
                    if (q == 4 || q == 12) trk_note_on(&trk[0], 38, 122);
                    if (q == 7 || q == 9 || q == 15) trk_note_on(&trk[0], 38, 46);
                    trk_note_on(&trk[0], q == 2 ? 46 : 42, q % 4u == 0u ? 106 : q % 2u ? 62 : 84);
                    if (q == 11) trk_note_on(&trk[0], 37, 90);
                }
            } else {                             /* the fill */
                static const uint8_t F[8] = {38, 38, 50, 50, 47, 47, 41, 41};
                trk_note_on(&trk[0], F[q - 8u], q & 1u ? 96 : 118);
                if (q % 4u == 0u) trk_note_on(&trk[0], 36, 110);
            }
        }
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        for (uint32_t i = 0; i < CTL; i++) {
            wav_put(f, o[2 * i], o[2 * i + 1]);
        }
    }
    fclose(f);
    printf("DRUM REAL:   demo %s\n", path);
}
static void single(const char *dir, uint32_t preset, const char *name)   /* every drum alone, a half second apart */
{
    char path[256];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s_each.wav", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    kit(preset);
    uint32_t each = FS / 2u / CTL, nd = drum_sound_count(&trk[0]);
    wav_hdr(f, each * (nd + 3u) * CTL);
    for (uint32_t k = 0; k < each * (nd + 3u); k++) {
        if (k % each == 0u && k / each < nd)
            trk_note_on(&trk[0], drum_sound_note(&trk[0], k / each), 124);
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        for (uint32_t i = 0; i < CTL; i++) {
            wav_put(f, o[2 * i], o[2 * i + 1]);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    static int32_t a[FS * 3], b[FS * 3];
    uint32_t j, s, nd;
    check("KIT 9 STUDIO, 10 BOOM, 11 TIGHT: factory kits appended", ENG_DRUM.edit[0].max == DK_TIGHT &&
          !strcmp(N_DRUM_KIT[DK_STUDIO], "STUDIO") && !strcmp(N_DRUM_KIT[DK_BOOM], "BOOM") && !strcmp(N_DRUM_KIT[DK_TIGHT], "TIGHT") &&
          ENG_DRUM.npresets == 9u && !strcmp(ENG_DRUM.presets[P_STUDIO].name, "STUDIO KIT") &&
          ENG_DRUM.presets[P_TIGHT].e[0] == DK_TIGHT);
    {   /* the player's decoder against the encoder's, stream by stream, through the real channel state */
        static int16_t d[65536];
        uint32_t bad = 0;
        for (j = 0; j < NELEM(DRK_SAMPLES); j++) {
            drk_ch_t c;
            memset(&c, 0, sizeof c);
            drk_stream_start(&c, &DRK_SAMPLES[j]);
            for (s = 0; s < DRK_SAMPLES[j].n; s++)
                d[s] = (int16_t)drk_next(&c);
            if (crc32_of(d, DRK_SAMPLES[j].n) != DRK_CRC[j]) {
                printf("DRUM REAL:   %s decodes differently\n", DRK_NAME[j]);
                bad++;
            }
        }
        check("the decoder gives the encoder's samples, all streams", !bad);
    }
    for (j = 0; j < 3u; j++) {                   /* every drum alone */
        uint32_t silent = 0, over = 0, differs = 0, longer = 0;
        kit(KITS[j]);
        nd = drum_sound_count(&trk[0]);
        for (s = 0; s < nd; s++) {
            int32_t pk;
            kit(KITS[j]);
            trk_note_on(&trk[0], drum_sound_note(&trk[0], s), 127);
            pk = run(a, FS / 2u / CTL);
            kit(KITS[j]);
            trk_note_on(&trk[0], drum_sound_note(&trk[0], s), 127);
            run(b, FS / 2u / CTL);
            silent += pk < 2000;
            over += pk > 62000;
            differs += memcmp(a, b, (FS / 2u / CTL) * CTL * sizeof a[0]) != 0;
            longer += to_end(FS * 12u / CTL) >= FS * 12u / CTL;
            if (voices_on()) printf("DRUM REAL:   %s %s rings on\n", KIT_NAME[j], drum_sound_name(&trk[0], s));
        }
        char line[80];
        snprintf(line, sizeof line, "%s: 11 drums sound, under scale, the same twice, and end", KIT_NAME[j]);
        check(line, nd == D9_NUM && !silent && !over && !differs && !longer);
    }
    {   /* one voice, however many hits */
        kit(P_STUDIO);
        for (s = 0; s < D9_NUM; s++)
            trk_note_on(&trk[0], drum_sound_note(&trk[0], s), 100);
        check("the kit plays on one voice", voices_on() == 1u);
    }
    {   /* a drum struck again: no step; it rings under the new hit */
        kit(P_STUDIO);
        trk_note_on(&trk[0], 38, 127);
        run(0, 40);
        drum_flash[0] = 0;
        trk_note_on(&trk[0], 38, 127);
        run(a, 6);
        uint32_t on = 0;
        for (s = 0; s < DRK_NCH; s++)
            on += drk_part(0)->ch[s].on;
        int32_t st = largest_step(a, 8);
        printf("DRUM REAL:   struck again: %u channels sound, largest step in the first samples %d\n", on, (int)st);
        check("a snare struck again rings under its next hit, without a click", on == 2u && st < 30000);
    }
    {   /* toms overlap; crash under ride */
        kit(P_STUDIO);
        trk_note_on(&trk[0], 41, 110); run(0, 8);
        trk_note_on(&trk[0], 45, 110); run(0, 8);
        trk_note_on(&trk[0], 49, 110); run(0, 8);
        trk_note_on(&trk[0], 51, 110); run(0, 8);
        uint32_t on = 0;
        for (s = 0; s < DRK_NCH; s++)
            on += drk_part(0)->ch[s].on;
        check("two toms, a crash and a ride overlap", on == 4u);
    }
    {   /* more hits than channels: the oldest gives way without a step */
        kit(P_STUDIO);
        for (s = 0; s < 12u; s++) { trk_note_on(&trk[0], 41 + s % 8u, 110); run(0, 2); }
        int32_t pk = run(a, 8);
        check("more hits than channels: the oldest gives way, nothing wraps", pk < 90000);
    }
    {   /* the hat */
        kit(P_STUDIO);
        trk_note_on(&trk[0], 46, 120); run(0, 40);
        uint32_t ring = 0;
        for (s = 0; s < DRK_NCH; s++) ring += drk_part(0)->ch[s].on;
        trk_note_on(&trk[0], 42, 100); run(0, 8);
        uint32_t on = 0;
        for (s = 0; s < DRK_NCH; s++) on += drk_part(0)->ch[s].on && drk_part(0)->ch[s].sound == D9_OH;
        check("a closed hat chokes the open one", ring == 1u && !on);
        kit(P_STUDIO);
        trk_note_on(&trk[0], 42, 110); run(a, FS / 10u / CTL); to_end(FS / CTL);
        trk_note_on(&trk[0], 42, 110); run(b, FS / 10u / CTL);
        check("closed hats alternate between two takes", memcmp(a, b, (FS / 10u / CTL) * CTL * sizeof a[0]) != 0);
    }
    {   /* KICK SWAP */
        kit(P_STUDIO); trk_note_on(&trk[0], 36, 120); run(a, FS / 4u / CTL);
        kit(P_STUDIO); trk[0].p[P_E6] = 1; trk_note_on(&trk[0], 36, 120); run(b, FS / 4u / CTL);
        check("KICK SWAP plays the other kit's kick", drk_part(0)->ch[0].sample == 2u && memcmp(a, b, (FS / 4u / CTL) * CTL * sizeof a[0]) != 0);
        check("KICK reads KIT / SWAP on these kits", drum_desc(&trk[0], 6u) == &DRUM_REAL_KICK && drum_desc(&trk[0], 7u) == 0);
    }
    {   /* a recorded hit's length */
        kit(P_STUDIO);
        drum_event_length = FS / 20u;
        trk_note_on(&trk[0], 49, 120);
        drum_event_length = 0;
        uint32_t blocks = to_end(FS * 3u / CTL);
        check("a recorded hit's length ends a crash", blocks < FS / 8u / CTL);
    }
    {   /* a kit switch while ringing */
        kit(P_STUDIO);
        trk_note_on(&trk[0], 49, 120);
        run(0, 16);
        trk[0].p[P_E0] = DK_BOOM;
        eng_state_prepare(&trk[0]);
        run(0, 2);
        check("STUDIO -> BOOM while ringing: the old kit's hit ends", !voices_on());
        trk_note_on(&trk[0], 36, 120);
        check("the new kit starts from rest", run(0, 40) > 2000 && drk_part(0)->kit == DK_BOOM);
    }
    {   /* no resources: no sound, nothing read */
        res_absent_for_test = 1;
        kit(P_STUDIO);
        trk_note_on(&trk[0], 38, 120);
        int32_t pk = run(0, 64);
        res_absent_for_test = 0;
        check("no resource region: silence", pk == 0);
    }
    {   /* the knobs */
        double k0, k1;
        int32_t pk0, pk1;
        #define HIT(note, knob, val, buf) (kit(P_STUDIO), trk[0].p[knob] = (val), trk_note_on(&trk[0], note, 120), run(buf, FS / 2u / CTL))
        HIT(38, P_E2, 20, a); k0 = hf_share(a, FS / 5u);
        HIT(38, P_E2, 110, a); k1 = hf_share(a, FS / 5u);
        check("TONE: more of it, more top", k1 > k0 * 1.3);
        HIT(49, P_E3, 30, a); k0 = rms(a + FS / 4u, FS / 4u);
        HIT(49, P_E3, 110, a); k1 = rms(a + FS / 4u, FS / 4u);
        check("DECY: more of it, a longer crash", k1 > k0 * 1.5);
        HIT(38, P_E4, 10, a); pk0 = 0; for (s = 0; s < 400u; s++) pk0 = abs(a[s]) > pk0 ? abs(a[s]) : pk0;
        HIT(38, P_E4, 120, a); pk1 = 0; for (s = 0; s < 400u; s++) pk1 = abs(a[s]) > pk1 ? abs(a[s]) : pk1;
        check("SNAP: more of it, a harder front", pk1 > pk0);
        kit(P_STUDIO); trk[0].p[P_E1] = 100; trk_note_on(&trk[0], 45, 120); run(a, FS / 4u / CTL);
        kit(P_STUDIO); trk[0].p[P_E1] = 30; trk_note_on(&trk[0], 45, 120); run(b, FS / 4u / CTL);
        uint32_t za = 0, zb = 0;
        for (s = 1; s < FS / 4u; s++) { za += (a[s - 1] < 0) != (a[s] < 0); zb += (b[s - 1] < 0) != (b[s] < 0); }
        check("TUNE: more of it, a higher tom", za > zb);
        kit(P_STUDIO); trk_note_on(&trk[0], 38, 127); run(a, FS / 8u / CTL); k1 = hf_share(a, FS / 8u);
        kit(P_STUDIO); trk_note_on(&trk[0], 38, 50); run(a, FS / 8u / CTL); k0 = hf_share(a, FS / 8u);
        check("velocity: a soft hit is darker", k0 < k1);
    }
    {   /* levels */
        static const uint8_t NOTES[5] = {36, 38, 42, 45, 39};
        printf("DRUM REAL:   peak / rms of the first 100 ms at velocity 120: KICK SNARE HATC TOM CLAP\n");
        static const uint32_t ALL[4] = {P_808, P_STUDIO, P_BOOM, P_TIGHT};
        static const char *const N[4] = {"808", "STUDIO", "BOOM", "TIGHT"};
        for (j = 0; j < 4u; j++) {
            printf("DRUM REAL:   %-7s", N[j]);
            for (s = 0; s < 5u; s++) {
                kit(ALL[j]);
                trk_note_on(&trk[0], NOTES[s], 120);
                int32_t pk = run(a, FS / 10u / CTL);
                printf(" %6d/%-6.0f", (int)pk, rms(a, FS / 10u));
            }
            printf("\n");
        }
    }
    {   /* the corners */
        int32_t pk = 0;
        for (j = 0; j < 3u; j++)
            for (uint32_t corner = 0; corner < 4u; corner++) {
                kit(KITS[j]);
                for (uint32_t k = 1; k <= 4u; k++)
                    trk[0].p[P_E0 + k] = corner & 1u ? 127 : 0;
                trk[0].p[P_E5] = 127; trk[0].p[P_E7] = 127; trk[0].p[P_E6] = (int16_t)(corner >> 1);
                for (s = 0; s < D9_NUM; s++)
                    trk_note_on(&trk[0], drum_sound_note(&trk[0], s), 127);
                for (uint32_t i = 0; i < FS / CTL; i++) {
                    int32_t o[2 * CTL];
                    mix_block(o, CTL);
                    for (uint32_t k = 0; k < 2u * CTL; k++)
                        pk = abs(o[k]) > pk ? abs(o[k]) : pk;
                }
            }
        printf("DRUM REAL:   the drums at once at the knobs' corners, velocity 127, DRV 127: mix peak %d\n", (int)pk);
        check("no clipping at the corners", pk < 32767);
    }
    if (argc > 1) {
        mkdir(argv[1], 0755);
        groove(argv[1], "studio_rock", P_STUDIO, 112, 0, 0);
        groove(argv[1], "boom_hiphop", P_BOOM, 88, 16, 1);
        groove(argv[1], "tight_funk", P_TIGHT, 104, 8, 2);
        groove(argv[1], "reference_808", P_808, 112, 0, 0);
        single(argv[1], P_STUDIO, "studio");
        single(argv[1], P_BOOM, "boom");
        single(argv[1], P_TIGHT, "tight");
    }
    printf("DRUM REAL: %s\n", fails ? "FAILED" : "all checks ok");
    return fails != 0;
}
