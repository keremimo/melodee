/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* KIT CR78 (src/drum_cr78.c) and KIT FM (src/drum_fm.c) on the lanes of src/drum_lanes.c: every drum alone
 * (and CR78's METAL hats) sounds, stays under the part's scale, renders the same twice and ends; the kit on one
 * voice; a lane struck again never steps; a closed hat chokes the open one; a recorded hit's length; a kit
 * switch; the knobs' directions; levels next to the 808; no wrapped sample, no clipping at the corners; demos in
 * build/drum_kits_demo/ (argv[1]). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <sys/stat.h>
static int fails;
static void check(const char *s, int ok) { printf("DRUM KITS: %-84s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }
#define P_808 0u
#define P_CR78 4u
#define P_FM 5u
static const char *const KIT_NAME[6] = {"808", "", "", "", "CR78", "FM"};

static void kit(uint32_t preset)                 /* part 1 on a DRUM preset, nothing sounding */
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
static int32_t run(int32_t *buf, uint32_t nb)     /* nb blocks of the part into buf (0: none); the peak */
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
static uint32_t to_end(uint32_t max)               /* blocks until the kit's voice ends (max: it did not) */
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
    return sqrt(s / n);
}
static double centroid(const int32_t *x, uint32_t n)   /* zero crossings a second: brightness, roughly */
{
    uint32_t z = 0;
    for (uint32_t i = 1; i < n; i++)
        z += (x[i] >= 0) != (x[i - 1] >= 0);
    return (double)z * FS / 2.0 / n;
}

/* two bars at 120 BPM, twice: kick, snare, the hats, an open hat, a tom fill, the clap lane, percussion */
static void demo(const char *dir, const char *name, uint32_t preset, int metal)
{
    char path[512];
    uint32_t nb = 4u * 16u * 5512u / CTL + FS / CTL, k, last = ~0u;
    int32_t o[2 * CTL];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    kit(preset);
    trk[0].p[P_E6] = (int16_t)metal;
    wav_hdr(f, nb * CTL);
    for (k = 0; k < nb; k++) {
        uint32_t s = k * CTL / 5512u;
        if (s != last && s < 64u) {
            uint32_t b = s % 32u, q = b % 16u;
            last = s;
            if (q == 0 || q == 6 || q == 10) trk_note_on(&trk[0], 36, 120);
            if (q == 4 || q == 12) trk_note_on(&trk[0], 38, 110);
            if (q % 2 == 0 && q != 14) trk_note_on(&trk[0], 42, q % 4 ? 80 : 100);
            if (q == 14) trk_note_on(&trk[0], 46, 100);
            if (b >= 28) trk_note_on(&trk[0], b == 28 ? 50 : b == 29 ? 47 : b == 30 ? 45 : 41, 115);
            if (b == 12) trk_note_on(&trk[0], 39, 100);
            if (s >= 32u && (q == 3 || q == 11)) trk_note_on(&trk[0], q == 3 ? 64 : 61, 90);
            if (s >= 32u && q == 7) trk_note_on(&trk[0], 56, 90);
            if (s >= 32u && q == 15) trk_note_on(&trk[0], 75, 90);
        }
        mix_block(o, CTL);
        for (uint32_t i = 0; i < CTL; i++)
            wav_put(f, o[2 * i], o[2 * i + 1]);
    }
    fclose(f);
    printf("DRUM KITS:   demo %s\n", path);
}

int main(int argc, char **argv)
{
    static int32_t a[FS * 2], b[FS * 2];
    static const uint32_t KITS[2] = {P_CR78, P_FM};
    uint32_t s, k, i, j;
    check("KIT 7 is CR78, 8 FM: factory kits CR78 KIT and FM KIT", ENG_DRUM.edit[0].max == DK_FM &&
          !strcmp(N_DRUM_KIT[DK_CR78], "CR78") && !strcmp(N_DRUM_KIT[DK_FM], "FM") && ENG_DRUM.npresets == 6u &&
          !strcmp(ENG_DRUM.presets[P_CR78].name, "CR78 KIT") && ENG_DRUM.presets[P_CR78].e[0] == DK_CR78 &&
          !strcmp(ENG_DRUM.presets[P_FM].name, "FM KIT") && ENG_DRUM.presets[P_FM].e[0] == DK_FM);
    {   /* the names: CR78's own percussion where the 808 has drums the box had not */
        kit(P_CR78);
        check("CR78 names: TAMB on the clap lane, CONGA BONGL BONGH, HATS on KICK's knob",
              !strcmp(drum_sound_name(&trk[0], D9_CP), "TAMB") && !strcmp(drum_lane_name(&trk[0], DV_CLAP), "TAMB") &&
              !strcmp(drum_sound_name(&trk[0], 11), "CONGA") && !strcmp(drum_sound_name(&trk[0], 13), "BONGH") &&
              drum_desc(&trk[0], 6) == &DRUM_CR_HATS && drum_cap(&trk[0]) == 1u);
        kit(P_FM);
        check("FM names: the 808's; one voice", !strcmp(drum_sound_name(&trk[0], D9_CP), "CLAP") &&
              drum_desc(&trk[0], 6) == 0 && drum_cap(&trk[0]) == 1u);
    }
    for (j = 0; j < 2u; j++) {   /* every drum alone: sounds, bounded, deterministic, over */
        for (int metal = 0; metal < (j ? 1 : 2); metal++) {
            int sound = 1, bounded = 1, det = 1, ends = 1, mapped = 1;
            int32_t pk[16];
            for (s = 0; s < 16u; s++) {
                uint32_t note;
                uint64_t h[2] = {1469598103934665603ull, 1469598103934665603ull};
                if (metal && s != D9_CH && s != D9_OH)
                    continue;
                for (uint32_t r = 0; r < 2u; r++) {
                    kit(KITS[j]);
                    trk[0].p[P_E6] = (int16_t)metal;
                    note = drum_sound_note(&trk[0], s);
                    mapped &= drum_sound_of(&trk[0], note) == s;
                    trk_note_on(&trk[0], note, 127);
                    pk[s] = run(a, FS / CTL);
                    for (i = 0; i < FS / CTL * CTL; i++)
                        h[r] = (h[r] ^ (uint32_t)a[i]) * 1099511628211ull;
                    ends &= to_end(5u * FS / CTL) < 5u * FS / CTL;
                }
                det &= h[0] == h[1];
                sound &= pk[s] > 1500;
                bounded &= pk[s] < 60000;
            }
            printf("DRUM KITS:   %s%s peaks alone:", KIT_NAME[KITS[j]], metal ? " METAL" : "");
            for (s = 0; s < 16u; s++)
                if (!metal || s == D9_CH || s == D9_OH)
                    printf(" %s %d", drum_sound_name(&trk[0], s), (int)pk[s]);
            printf("\n");
            char msg[96];
            snprintf(msg, sizeof msg, "%s%s: every drum alone sounds, bounded, deterministic, then its voice ends",
                     KIT_NAME[KITS[j]], metal ? " METAL hats" : "");
            check(msg, sound && bounded && det && ends && mapped);
        }
    }
    for (j = 0; j < 2u; j++) {   /* the kit on one voice; eight lanes ring together */
        uint32_t lanes = 0;
        kit(KITS[j]);
        for (i = 0; i < 8u; i++)
            trk_note_on(&trk[0], DRUM_LANE_NOTE[i], 110);
        run(0, 2);
        for (k = 0; k < DV_NLANE; k++)
            lanes += dlk_part(0)->ch[k].on;
        char msg[96];
        snprintf(msg, sizeof msg, "%s: the kit on one voice, the eight lanes' drums at once", KIT_NAME[KITS[j]]);
        check(msg, voices_on() == 1u && lanes == 8u);
    }
    for (j = 0; j < 2u; j++) {   /* a lane struck again as it rings: no step beyond the drum's own attack */
        static const uint8_t N[12] = {36, 38, 41, 45, 50, 42, 46, 37, 39, 56, 64, 75};
        int ok = 1;
        int32_t ratio = 0;
        for (k = 0; k < 12u; k++) {
            int32_t alone, again;
            kit(KITS[j]);
            trk_note_on(&trk[0], N[k], 127);
            run(a, 40);
            alone = largest_step(a, 40u * CTL);
            kit(KITS[j]);
            trk_note_on(&trk[0], N[k], 127);
            run(a, 25);
            trk_note_on(&trk[0], N[k], 127);
            run(b, 40);
            again = largest_step(b, 40u * CTL);
            ok &= again <= alone + alone / 4 + 1000;
            ratio = again * 100 / (alone + 1) > ratio ? again * 100 / (alone + 1) : ratio;
        }
        {   /* a tom fill on the tom lane: each tom from rest, the last one's sample easing out */
            static const uint8_t FILL[4] = {50, 48, 45, 41};
            int32_t alone, fill = 0;
            kit(KITS[j]);
            trk_note_on(&trk[0], 50, 127);
            run(a, 40);
            alone = largest_step(a, 40u * CTL);
            kit(KITS[j]);
            for (k = 0; k < 4u; k++) {
                trk_note_on(&trk[0], FILL[k], 127);
                run(a, 20);
                fill = largest_step(a, 20u * CTL) > fill ? largest_step(a, 20u * CTL) : fill;
            }
            printf("DRUM KITS:   %s: struck again as they ring, the largest step at most %d %% of the drum's own;"
                   " tom fill %d (TOM H alone %d)\n", KIT_NAME[KITS[j]], (int)ratio, (int)fill, (int)alone);
            ok &= fill <= alone + alone / 4 + 1000;
        }
        char msg[96];
        snprintf(msg, sizeof msg, "%s: a lane struck again steps no harder than its drum alone", KIT_NAME[KITS[j]]);
        check(msg, ok);
    }
    for (j = 0; j < 2u; j++) {   /* the hats: a closed hat chokes the open one */
        double ring, choked;
        kit(KITS[j]);
        trk_note_on(&trk[0], 46, 110);
        run(0, 8);
        run(a, 32);
        ring = rms(a + 24 * CTL, 8 * CTL);
        kit(KITS[j]);
        trk_note_on(&trk[0], 46, 110);
        run(0, 8);
        trk_note_on(&trk[0], 42, 1);              /* (a closed hat at velocity 1: the open hat's fade alone) */
        trk[0].p[P_LN0 + DV_HATC] = 0;
        run(a, 32);
        choked = rms(a + 24 * CTL, 8 * CTL);
        printf("DRUM KITS:   %s: open hat 6 ms after 0.2 s: alone %.0f, choked %.0f\n", KIT_NAME[KITS[j]], ring, choked);
        char msg[96];
        snprintf(msg, sizeof msg, "%s: a closed hat chokes the open hat (gone in 3 ms)", KIT_NAME[KITS[j]]);
        check(msg, ring > 300 && choked < ring / 50 && !dlk_part(0)->ch[DV_HATO].on);
    }
    for (j = 0; j < 2u; j++) {   /* a recorded hit's length; a kit switch */
        double full, cut;
        uint32_t before, after;
        int32_t pk;
        kit(KITS[j]);
        trk_note_on(&trk[0], 46, 110);
        run(a, 140);
        full = rms(a + 130 * CTL, 10 * CTL);
        kit(KITS[j]);
        drum_event_length = FS / 20u;
        trk_note_on(&trk[0], 46, 110);
        drum_event_length = 0;
        run(a, 140);
        cut = rms(a + 130 * CTL, 10 * CTL);
        printf("DRUM KITS:   %s: open hat after 100 ms: rms %.0f, recorded 50 ms long %.0f\n", KIT_NAME[KITS[j]], full, cut);
        kit(P_808);
        for (i = 0; i < 8u; i++)
            trk_note_on(&trk[0], DRUM_LANE_NOTE[i], 110);
        run(0, 2);
        before = voices_on();
        trk[0].p[P_E0] = (int16_t)ENG_DRUM.presets[KITS[j]].e[0];
        run(0, 2);
        after = voices_on();
        trk_note_on(&trk[0], 38, 110);
        pk = run(0, 4);
        char msg[96];
        snprintf(msg, sizeof msg, "%s: a recorded length ends the drum; KIT 808 -> %s while ringing plays on one voice",
                 KIT_NAME[KITS[j]], KIT_NAME[KITS[j]]);
        check(msg, full > 300 && cut < full / 100 && before == 8u && !after && voices_on() == 1u && pk > 1000);
    }
    {   /* CR78 <-> FM while ringing: the other kit's lanes start from rest */
        int32_t pk;
        kit(P_CR78);
        trk_note_on(&trk[0], 36, 110); trk_note_on(&trk[0], 46, 110);
        run(0, 4);
        trk[0].p[P_E0] = DK_FM;
        run(0, 2);
        trk_note_on(&trk[0], 38, 110);
        pk = run(0, 4);
        check("CR78 -> FM while ringing: the CR78's lanes end, the FM's start from rest",
              dlk_part(0)->kit == DK_FM && !dlk_part(0)->ch[DV_KICK].on && !dlk_part(0)->ch[DV_HATO].on &&
              dlk_part(0)->ch[DV_SNARE].on && pk > 1000);
    }
    for (j = 0; j < 2u; j++) {   /* the knobs: TUNE up raises, TONE up brightens, DECY up lengthens the kick */
        double lo, hi, dark, bright, shrt, lng;
        kit(KITS[j]); trk[0].p[P_E1] = 30; trk_note_on(&trk[0], 45, 120); run(a, 40); lo = centroid(a + 4 * CTL, 32 * CTL);
        kit(KITS[j]); trk[0].p[P_E1] = 100; trk_note_on(&trk[0], 45, 120); run(a, 40); hi = centroid(a + 4 * CTL, 32 * CTL);
        kit(KITS[j]); trk[0].p[P_E2] = 10; trk_note_on(&trk[0], 36, 120); run(a, 40); dark = centroid(a, 40 * CTL);
        kit(KITS[j]); trk[0].p[P_E2] = 127; trk_note_on(&trk[0], 36, 120); run(a, 40); bright = centroid(a, 40 * CTL);
        kit(KITS[j]); trk[0].p[P_E3] = 20; trk_note_on(&trk[0], 36, 120); run(a, 400); shrt = rms(a + 300 * CTL, 100 * CTL);
        kit(KITS[j]); trk[0].p[P_E3] = 110; trk_note_on(&trk[0], 36, 120); run(a, 400); lng = rms(a + 300 * CTL, 100 * CTL);
        printf("DRUM KITS:   %s: tom M crossings/s TUNE 30 %.0f, 100 %.0f; kick TONE 10 %.0f, 127 %.0f; kick at 0.3 s DECY 20 %.0f, 110 %.0f\n",
               KIT_NAME[KITS[j]], lo, hi, dark, bright, shrt, lng);
        char msg[96];
        snprintf(msg, sizeof msg, "%s: TUNE raises the toms, TONE brightens the kick, DECY lengthens it", KIT_NAME[KITS[j]]);
        check(msg, hi > lo * 1.5 && bright > dark && lng > shrt * 2);
    }
    {   /* levels next to the 808 */
        static const uint8_t N[3] = {36, 38, 42};
        int32_t pk[3][3];
        int ok = 1;
        for (k = 0; k < 3u; k++)
            for (j = 0; j < 3u; j++) {
                kit(j ? KITS[j - 1] : P_808);
                trk_note_on(&trk[0], N[k], 127);
                pk[j][k] = run(0, FS / CTL / 2);
                ok &= !j || (pk[j][k] > pk[0][k] / 2 && pk[j][k] < pk[0][k] * 2);
            }
        printf("DRUM KITS:   peaks KICK SNARE HAT C: 808 %d %d %d, CR78 %d %d %d, FM %d %d %d\n", (int)pk[0][0],
               (int)pk[0][1], (int)pk[0][2], (int)pk[1][0], (int)pk[1][1], (int)pk[1][2], (int)pk[2][0], (int)pk[2][1],
               (int)pk[2][2]);
        check("kick, snare and closed hat within 0.5 .. 2 x the 808's", ok);
    }
    for (j = 0; j < 2u; j++) {   /* kick, toms and congas together, loud, at DRV 127: never a wrapped sample */
        uint32_t wraps = 0;
        int32_t pk = 0;
        kit(KITS[j]);
        trk[0].p[P_E2] = 110; trk[0].p[P_E7] = 127;
        for (k = 0; k < 8u; k++) {
            trk_note_on(&trk[0], 36, 127); trk_note_on(&trk[0], 41 + (k % 4u) * 2u, 127);   /* (toms 41 43 45 47) */
            trk_note_on(&trk[0], 64, 127);
            run(a, 43);
            for (i = 1; i < 43u * CTL; i++) {      /* (low drums: a jump of 60000 in a sample is a wrap) */
                wraps += abs(a[i] - a[i - 1]) > 60000;
                pk = abs(a[i]) > pk ? abs(a[i]) : pk;
            }
        }
        printf("DRUM KITS:   %s: kick + tom + conga at DRV 127, 8 times: wrapped samples %u, peak %d\n",
               KIT_NAME[KITS[j]], wraps, (int)pk);
        char msg[96];
        snprintf(msg, sizeof msg, "%s: low drums together at DRV 127: no wrapped sample, under the part's scale",
                 KIT_NAME[KITS[j]]);
        check(msg, !wraps && pk < 60000);
    }
    {   /* the corners: TUNE TONE DECY SNAP at 0 / 127, ACC and DRV 127, the 16 drums at once: no clipping */
        int32_t pk = 0;
        for (j = 0; j < 2u; j++)
            for (uint32_t corner = 0; corner < 4u; corner++) {
                kit(KITS[j]);
                for (k = 1; k <= 4u; k++)
                    trk[0].p[P_E0 + k] = corner & 1u ? 127 : 0;
                trk[0].p[P_E5] = 127; trk[0].p[P_E7] = 127; trk[0].p[P_E6] = (int16_t)(corner >> 1);
                for (s = 0; s < 16u; s++)
                    trk_note_on(&trk[0], drum_sound_note(&trk[0], s), 127);
                for (i = 0; i < FS / CTL; i++) {
                    int32_t o[2 * CTL];
                    mix_block(o, CTL);
                    for (k = 0; k < 2u * CTL; k++)
                        pk = abs(o[k]) > pk ? abs(o[k]) : pk;
                }
            }
        printf("DRUM KITS:   the 16 drums at once at the knobs' corners, velocity 127, DRV 127: mix peak %d\n", (int)pk);
        check("no clipping at the corners", pk < 32767);
    }
    if (argc > 1) {
        mkdir(argv[1], 0755);
        demo(argv[1], "cr78_kit", P_CR78, 0);
        demo(argv[1], "cr78_metal", P_CR78, 1);
        demo(argv[1], "fm_kit", P_FM, 0);
    }
    printf("DRUM KITS: %s\n", fails ? "FAILED" : "all checks ok");
    return fails != 0;
}
