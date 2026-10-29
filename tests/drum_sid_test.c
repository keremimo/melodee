/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* KIT SID (src/drum_sid.c): one 6581 on one voice for the whole drum track, its three voices taken by the
 * hits (a lane's own, the hats' shared one, a silent one, the oldest), frame programs stepped at PAL frames,
 * DIGI's KICK and SNARE on the volume register (7882 Hz, no SID voice) and its effect on the voices ringing
 * meanwhile, the filter for the routed drums only, a recorded hit's length, a kit switch, levels next to the
 * 808, no clipping; demos in build/sid_kit_demo/ (argv[1]). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <sys/stat.h>
static int fails;
static void check(const char *s, int ok) { printf("SID KIT: %-86s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }

static void kit(uint32_t preset, int digi)       /* part 1 on a DRUM preset, nothing sounding */
{
    host_tracks_init();
    for (uint32_t p = 0; p < NPART; p++)
        memset(trk[p].v, 0, NVOICE * sizeof *trk[p].v);
    eng_state_reset();
    host_preset(&trk[0], ENGI_DRUM, preset);
    if (preset == 2u)
        trk[0].p[P_E6] = (int16_t)digi;
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
static double rms(const int32_t *x, uint32_t n)
{
    double s = 0;
    for (uint32_t i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / n);
}
static uint32_t ch_with(uint32_t sound)            /* the SID voice playing sound (DSK_NCH: none) */
{
    for (uint32_t k = 0; k < DSK_NCH; k++)
        if (!dsk_done(&dsk_part(0)->ch[k]) && dsk_part(0)->ch[k].sound == sound)
            return k;
    return DSK_NCH;
}

/* two bars at 120 BPM, twice: kick, snare, closed hats on the 8ths, an open hat, a tom fill, a clap */
static void demo(const char *dir, const char *name, int digi)
{
    static const uint8_t KICK = 36, SNARE = 38, HC = 42, HO = 46;
    char path[512];
    uint32_t nb = 4u * 16u * 5512u / CTL + FS / CTL, k, last = ~0u;
    int32_t o[2 * CTL];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    kit(2, digi);
    wav_hdr(f, nb * CTL);
    for (k = 0; k < nb; k++) {
        uint32_t s = k * CTL / 5512u;
        if (s != last && s < 64u) {
            uint32_t b = s % 32u, q = b % 16u;
            last = s;
            if (q == 0 || q == 6 || q == 10) trk_note_on(&trk[0], KICK, 120);
            if (q == 4 || q == 12) trk_note_on(&trk[0], SNARE, 110);
            if (q % 2 == 0 && q != 14) trk_note_on(&trk[0], HC, q % 4 ? 80 : 100);
            if (q == 14) trk_note_on(&trk[0], HO, 100);
            if (b >= 28) trk_note_on(&trk[0], b == 28 ? 50 : b == 29 ? 47 : b == 30 ? 45 : 41, 115);
            if (b == 12) trk_note_on(&trk[0], 39, 100);
        }
        mix_block(o, CTL);
        for (uint32_t i = 0; i < CTL; i++)
            wav_put(f, o[2 * i], o[2 * i + 1]);
    }
    fclose(f);
    printf("SID KIT:   demo %s\n", path);
}

int main(int argc, char **argv)
{
    static int32_t a[FS * 2], b[FS * 2], c[FS * 2];
    uint32_t s, k, i;
    {   /* the table: one waveform a step, steps and loops in range */
        int ok = 1;
        for (s = 0; s < 16u; s++) {
            const dsk_drum_t *d = &DSK_DRUMS[s];
            ok &= d->nstep >= 1u && d->nstep <= DSK_STEPS && (d->loop == D_HOLD || d->loop < d->nstep) && d->gate;
            for (k = 0; k < d->nstep; k++) {
                uint32_t w = d->s[k].ctrl >> 4;
                ok &= w == 1u || w == 4u || w == 8u;
            }
        }
        check("16 frame programs: TRI, PULSE or NOISE a step, steps and loops in range, a gate", ok);
        check("KIT 6 is SID: factory kits SID KIT (DIGI OFF) and SID DIGI (DIGI ON)", ENG_DRUM.edit[0].max >= DK_SID &&
              !strcmp(N_DRUM_KIT[DK_SID], "SID") && ENG_DRUM.npresets >= 4u && !strcmp(ENG_DRUM.presets[2].name, "SID KIT") &&
              ENG_DRUM.presets[2].e[0] == DK_SID && !ENG_DRUM.presets[2].e[6] && !strcmp(ENG_DRUM.presets[3].name, "SID DIGI") &&
              ENG_DRUM.presets[3].e[0] == DK_SID && ENG_DRUM.presets[3].e[6] == 1);
    }
    {   /* every drum alone, DIGI OFF: sounds, bounded, deterministic, over */
        int sound = 1, bounded = 1, det = 1, ends = 1, mapped = 1;
        int32_t pk[16];
        for (s = 0; s < 16u; s++) {
            uint32_t note = drum_sound_note(&trk[0], s);
            uint64_t h[2] = {1469598103934665603ull, 1469598103934665603ull};
            for (uint32_t r = 0; r < 2u; r++) {
                kit(2, 0);
                mapped &= drum_sound_of(&trk[0], note) == s;
                trk_note_on(&trk[0], note, 127);
                pk[s] = run(a, FS / CTL);
                for (i = 0; i < FS / CTL * CTL; i++)
                    h[r] = (h[r] ^ (uint32_t)a[i]) * 1099511628211ull;
                ends &= to_end(4u * FS / CTL) < 4u * FS / CTL;
            }
            det &= h[0] == h[1];
            sound &= pk[s] > 1500;
            bounded &= pk[s] < 60000;          /* (the part's scale: the mix halves it) */
        }
        printf("SID KIT:   peaks alone:");
        for (s = 0; s < 16u; s++)
            printf(" %s %d", drum_sound_name(&trk[0], s), (int)pk[s]);
        printf("\n");
        check("every drum alone sounds, under full scale, deterministic, then its voice ends", sound && bounded && det && ends && mapped);
    }
    {   /* one voice for the kit; three SID voices */
        uint32_t one, held;
        kit(2, 0);
        trk_note_on(&trk[0], 36, 110); trk_note_on(&trk[0], 38, 110); trk_note_on(&trk[0], 42, 110);
        trk_note_on(&trk[0], 45, 110);
        run(0, 1);
        one = voices_on();
        kit(2, 0);
        trk_note_on(&trk[0], 41, 110); run(0, 1);   /* TOM L, HAT C, CLAP, then RIM: the tom's voice */
        trk_note_on(&trk[0], 42, 110); run(0, 1);
        trk_note_on(&trk[0], 39, 110); run(0, 1);
        trk_note_on(&trk[0], 37, 110); run(0, DSK_STEAL + 2u);   /* (the voice's fade, then the rim) */
        held = (ch_with(D9_CH) < DSK_NCH) + (ch_with(D9_CP) < DSK_NCH) + (ch_with(D9_RS) < DSK_NCH);
        check("the kit plays on one voice; a fourth drum takes the oldest hit's SID voice", one == 1u && held == 3u &&
              ch_with(D9_LT) == DSK_NCH && ch_with(D9_RS) == 0u);
    }
    {   /* the hats one lane: a closed hat chokes the open one (released at the fastest rate, 6 ms) */
        uint32_t ho, rel, gone;
        kit(2, 0);
        trk_note_on(&trk[0], 46, 110); run(0, 4);
        ho = ch_with(D9_OH);
        trk_note_on(&trk[0], 42, 110); run(0, 1);
        rel = ho < DSK_NCH && dsk_part(0)->ch[ho].fade;
        run(0, DSK_FADE + 1u);
        gone = ch_with(D9_OH) == DSK_NCH;
        check("a closed hat chokes the open hat: faded out in 5.8 ms, the closed hat on a free voice",
              rel && gone && ch_with(D9_CH) < DSK_NCH && ch_with(D9_CH) != ho);
    }
    {   /* a tom fill, four notes of the TOM lane: each tom released fast, the next on a free voice, no cut */
        static const uint8_t FILL[4] = {50, 48, 45, 41};
        int32_t first = 0, later = 0;
        kit(2, 0);
        for (k = 0; k < 4u; k++) {
            int32_t pk = 0;
            trk_note_on(&trk[0], FILL[k], 110);
            run(a, 86);                            /* 16ths at 120 BPM */
            for (i = 1; i < 86u * CTL; i++)
                pk = abs(a[i] - a[i - 1]) > pk ? abs(a[i] - a[i - 1]) : pk;
            if (!k) first = pk; else later = pk > later ? pk : later;
        }
        printf("SID KIT:   tom fill 50 48 45 41: largest step, first tom %d, the next three %d\n", (int)first, (int)later);
        check("a tom fill: the toms after the first step no harder than a tom alone", later < first + first / 4);
        {   /* and no DC step: the low tom's first 4 ms after the next tom's hit stay near the faded level */
            int32_t lo = 0, hi = 0;
            kit(2, 0);
            trk_note_on(&trk[0], 41, 120); run(a, 86);
            trk_note_on(&trk[0], 50, 120); run(b, 8);
            for (i = 0; i < 8u * CTL; i++) { lo = b[i] < lo ? b[i] : lo; hi = b[i] > hi ? b[i] : hi; }
            printf("SID KIT:   low tom cut by a high one: the next 5.8 ms span %d .. %d\n", (int)lo, (int)hi);
        }
    }
    {   /* a hit on a voice still sounding: faded out over a block first (the hard restart), no step */
        int32_t step = 0, before = 0;
        kit(2, 0);
        trk_note_on(&trk[0], 64, 127); run(a, 5);   /* CONGL: a smooth triangle, loud */
        for (i = 1; i < 5u * CTL; i++)
            before = abs(a[i] - a[i - 1]) > before ? abs(a[i] - a[i - 1]) : before;
        trk_note_on(&trk[0], 64, 127); run(a, 3);
        for (i = 1; i < 3u * CTL; i++)
            step = abs(a[i] - a[i - 1]) > step ? abs(a[i] - a[i - 1]) : step;
        printf("SID KIT:   CONGL struck again while ringing: largest step %d (while ringing %d)\n", (int)step, (int)before);
        check("a ringing voice struck again fades out, then restarts: no click", step < 3 * before + 600);
    }
    {   /* the frame programs: the kick's frequency changes only at PAL frames from the hit */
        uint32_t last = 0, steps = 0, lo = ~0u, hi = 0, prev, f;
        kit(2, 0);
        trk_note_on(&trk[0], 36, 110);
        prev = dsk_part(0)->ch[0].freq;
        for (i = 1; i <= 250u; i++) {
            run(0, 1);
            if ((f = dsk_part(0)->ch[0].freq) == prev)
                continue;
            if (steps++) {
                lo = i - last < lo ? i - last : lo;
                hi = i - last > hi ? i - last : hi;
            }
            last = i; prev = f;
        }
        printf("SID KIT:   kick: %u frequency writes in 250 blocks, every %u..%u blocks\n", steps, lo, hi);
        check("a drum's program steps at PAL frames (19656 cycles = 27.5 blocks)", steps >= 5u && lo == 27u && hi == 28u);
    }
    {   /* DIGI */
        uint32_t busy = 0;
        int32_t pk;
        kit(2, 1);
        trk_note_on(&trk[0], 36, 127);
        run(0, 1);
        for (k = 0; k < DSK_NCH; k++)
            busy += !dsk_done(&dsk_part(0)->ch[k]);
        check("DIGI ON: KICK on $D418 at 7882 Hz, no SID voice", dsk_part(0)->digi && !busy &&
              dsk_part(0)->dstep == DSK_DIGI_STEP);
        pk = run(0, FS / CTL / 2);
        check("DIGI ON: the kick sounds and its voice ends", pk > 1500 && to_end(2u * FS / CTL) < 2u * FS / CTL);
        kit(2, 1);
        trk_note_on(&trk[0], 38, 127);
        check("DIGI ON: the snare sounds, under full scale", (pk = run(0, FS / CTL / 2)) > 1500 && pk < 60000);
    }
    {   /* the volume register is the chip's: a digi kick changes an open hat ringing meanwhile; a SID kick does not */
        uint32_t n = FS / 2u / CTL, len = n * CTL;
        double hat, r_digi, r_sid;
        for (int digi = 1; digi >= 0; digi--) {
            double r = 0;
            kit(2, digi); trk_note_on(&trk[0], 46, 110); run(a, n);
            kit(2, digi); trk_note_on(&trk[0], 36, 110); run(b, n);
            kit(2, digi); trk_note_on(&trk[0], 46, 110); trk_note_on(&trk[0], 36, 110); run(c, n);
            for (i = 0; i < len; i++)
                c[i] -= a[i] + b[i];
            r = rms(c, len);
            hat = rms(a, len);
            if (digi) r_digi = r / hat; else r_sid = r / hat;
        }
        printf("SID KIT:   open hat + kick minus each alone: DIGI %.3f, SID voices %.4f of the hat\n", r_digi, r_sid);
        check("a digi kick modulates the open hat (one volume register); SID voices add", r_digi > 0.05 && r_sid < 0.01);
    }
    {   /* TONE: the routed drums through the filter, the hats past it */
        double open, dark;
        int same;
        kit(2, 0); trk[0].p[P_E2] = 127; trk_note_on(&trk[0], 38, 110); run(a, 8000u / CTL); open = rms(a, 8000);
        kit(2, 0); trk[0].p[P_E2] = 10; trk_note_on(&trk[0], 38, 110); run(a, 8000u / CTL); dark = rms(a, 8000);
        kit(2, 0); trk[0].p[P_E2] = 127; trk_note_on(&trk[0], 46, 110); run(a, 8000u / CTL);
        kit(2, 0); trk[0].p[P_E2] = 10; trk_note_on(&trk[0], 46, 110); run(b, 8000u / CTL);
        same = !memcmp(a, b, 8000u / CTL * CTL * sizeof a[0]);
        printf("SID KIT:   snare rms TONE 127 %.0f, TONE 10 %.0f\n", open, dark);
        check("TONE darkens the snare (filtered), the open hat bypasses the filter", dark < 0.7 * open && same);
    }
    {   /* a recorded hit's length closes its gate */
        uint32_t n = FS * 6u / 10u / CTL, from = FS * 15u / 100u;
        double full, cut;
        kit(2, 0); trk_note_on(&trk[0], 46, 110); run(a, n); full = rms(a + from, n * CTL - from);
        kit(2, 0); drum_event_length = FS / 20u; trk_note_on(&trk[0], 46, 110); drum_event_length = 0;
        run(a, n); cut = rms(a + from, n * CTL - from);
        printf("SID KIT:   open hat after 150 ms: rms %.0f, recorded 50 ms long %.0f\n", full, cut);
        check("a recorded open hat of 50 ms releases early", cut < 0.5 * full);
    }
    {   /* a kit switch: the 808's voices end, the SID kit takes over */
        uint32_t before, after, one;
        int32_t pk;
        kit(0, 0);
        for (i = 0; i < 8u; i++)
            trk_note_on(&trk[0], DRUM_LANE_NOTE[i], 110);
        run(0, 2);
        before = voices_on();
        trk[0].p[P_E0] = DK_SID;
        run(0, 2);
        after = voices_on();
        trk_note_on(&trk[0], 38, 110);
        pk = run(0, 4);
        one = voices_on();
        check("KIT 808 -> SID while ringing: the 808's voices end, SID plays on one", before == 8u && !after &&
              one == 1u && pk > 1000);
    }
    {   /* kick, tom and closed hat together, loud, DIGI off and on: never a wrapped sample (a sign-flip spike) */
        static const uint8_t TOMS[4] = {50, 48, 45, 41};
        uint32_t wraps[2] = {0, 0};
        int32_t pk = 0;
        for (int digi = 0; digi < 2; digi++) {
            kit(2, digi);
            trk[0].p[P_E2] = 78; trk[0].p[P_E4] = 81; trk[0].p[P_E5] = 37;
            for (k = 0; k < 8u; k++) {
                trk_note_on(&trk[0], 36, 127); trk_note_on(&trk[0], 42, 127); trk_note_on(&trk[0], TOMS[k % 4u], 127);
                run(a, 86);
                for (i = 0; i < 86u * CTL; i++) {
                    wraps[digi] += i && abs(a[i] - a[i - 1]) > 60000;
                    pk = abs(a[i]) > pk ? abs(a[i]) : pk;
                }
            }
        }
        printf("SID KIT:   kick + tom + hat, 8 times: wrapped samples DIGI off %u, on %u; peak %d\n", wraps[0], wraps[1], (int)pk);
        check("kick, tom and hat together: no sample wraps (voice_amp's 16 bits), under the part's scale",
              !wraps[0] && !wraps[1] && pk < 60000);
    }
    {   /* levels next to the 808; the kit at its corners does not clip */
        int32_t p8[3], ps[3], pd[2], pk = 0;
        static const uint8_t N[3] = {36, 38, 42};
        for (k = 0; k < 3u; k++) {
            kit(0, 0); trk_note_on(&trk[0], N[k], 127); p8[k] = run(0, FS / CTL / 2);
            kit(2, 0); trk_note_on(&trk[0], N[k], 127); ps[k] = run(0, FS / CTL / 2);
            if (k < 2u) { kit(2, 1); trk_note_on(&trk[0], N[k], 127); pd[k] = run(0, FS / CTL / 2); }
        }
        printf("SID KIT:   peaks KICK SNARE HAT C: 808 %d %d %d, SID %d %d %d, DIGI %d %d\n", (int)p8[0], (int)p8[1],
               (int)p8[2], (int)ps[0], (int)ps[1], (int)ps[2], (int)pd[0], (int)pd[1]);
        for (uint32_t corner = 0; corner < 4u; corner++) {
            kit(2, (int)(corner & 1u));
            for (k = 1; k <= 4u; k++)
                trk[0].p[P_E0 + k] = corner & 2u ? 127 : 0;
            trk[0].p[P_E5] = 127; trk[0].p[P_E7] = 127;
            for (s = 0; s < 16u; s++)
                trk_note_on(&trk[0], drum_sound_note(&trk[0], s), 127);
            for (i = 0; i < FS / CTL; i++) {           /* (the mix, as the 808's corners are checked) */
                int32_t o[2 * CTL];
                mix_block(o, CTL);
                for (k = 0; k < 2u * CTL; k++)
                    pk = abs(o[k]) > pk ? abs(o[k]) : pk;
            }
        }
        printf("SID KIT:   the 16 drums at once at the knobs' corners, velocity 127, RES 127: mix peak %d\n", (int)pk);
        check("kick and snare within 0.3 .. 2 x the 808's; no clipping at the corners", ps[0] > p8[0] * 3 / 10 &&
              ps[0] < p8[0] * 2 && ps[1] > p8[1] * 3 / 10 && ps[1] < p8[1] * 2 && pk < 32767);
    }
    if (argc > 1) {
        mkdir(argv[1], 0755);
        demo(argv[1], "sid_kit", 0);
        demo(argv[1], "sid_kit_digi", 1);
    }
    printf("SID KIT: %s\n", fails ? "FAILED" : "all checks ok");
    return fails != 0;
}
