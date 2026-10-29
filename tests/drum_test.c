/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Synthesized 808 engine behavior, shared voice budget and output bounds. */
#define main hostsim_main
#include "hostsim.c"
#undef main
static int fails;
static uint32_t voices_on(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}

static int32_t blocks_peak(uint32_t nb)          /* nb blocks of the mix, its peak */
{
    int32_t o[2 * CTL], pk = 0;
    uint32_t i, k;
    for (i = 0; i < nb; i++) {
        mix_block(o, CTL);
        for (k = 0; k < 2u * CTL; k++)
            pk = abs(o[k]) > pk ? abs(o[k]) : pk;
    }
    return pk;
}

/* part 1 on the DRUM engine's first kit, nothing sounding (host_tracks_init keeps the voices) */
static void kit_fresh(void)
{
    host_tracks_init();
    memset(trk[0].v, 0, NVOICE * sizeof *trk[0].v);
    eng_state_reset();
    host_preset(&trk[0], ENGI_DRUM, 0);
}

/* one lane struck alone through the engine: the peak of its first nb blocks */
static int32_t lane_alone(uint32_t note, uint32_t nb)
{
    kit_fresh();
    trk_note_on(&trk[0], note, 110);
    return blocks_peak(nb);
}

/* A tom fill must reuse its lane before asking the shared budget for a voice. */
static void lane_budget(void)
{
    static const uint8_t KIT[6] = {36, 38, 39, 42, 45, 56};
    static const uint8_t TOM[4] = {41, 43, 45, 47};
    uint32_t i, p, owner, kills, bad = 0;
    host_tracks_init();
    eng_state_reset();
    for (p = 0; p < NPART; p++)
        memset(trk[p].v, 0, NVOICE * sizeof *trk[p].v);
    host_preset(&trk[0], ENGI_DRUM, 0);
    host_preset(&trk[1], HOST_SYNTH, 0);
    trk[1].p[P_VOICE] = V_POLY;
    trk[1].p[P_SUS] = 127;
    trk_note_on(&trk[1], 48, 100);
    trk_note_on(&trk[1], 60, 100);
    for (i = 0; i < NELEM(KIT); i++)
        trk_note_on(&trk[0], KIT[i], 110);
    blocks_peak(2);
    owner = drum_kit_part(0)[DV_TOM].owner;
    kills = voice_kills;
    bad += voices_busy() != NVOICE || !owner;
    for (i = 0; i < 16u; i++) {
        trk_note_on(&trk[0], TOM[i % NELEM(TOM)], 110);
        bad += drum_kit_part(0)[DV_TOM].owner != owner || voices_busy() != NVOICE || voice_kills != kills;
        blocks_peak(1);
        bad += voices_on(&trk[1]) != 2u || voices_on(&trk[0]) != NELEM(KIT);
    }
    printf("drum_test: tom fill at the 8-voice budget: same lane voice, both synth notes and other drums kept: %s\n",
           bad ? "FAIL" : "ok");
    fails += bad != 0;
    /* A new track can still take a drum voice: no permanent reservation for the kit. */
    host_preset(&trk[2], HOST_SYNTH, 0);
    trk[2].p[P_VOICE] = V_MONO;
    trk_note_off(&trk[0], TOM[3]);
    trk_note_on(&trk[2], 72, 100);
    bad = trk[0].v[owner - 1u].stage != 4u;
    /* Retrigger before the stolen voice's fade block must acquire room again. */
    trk_note_on(&trk[0], TOM[0], 110);
    bad += voices_busy() != NVOICE || drum_kit_part(0)[DV_TOM].owner != owner;
    blocks_peak(2);
    bad += voices_busy() != NVOICE || !trk[2].v[0].active || trk[2].v[0].note != 72;
    printf("drum_test: a synth starts over the kit, a stolen drum retriggers before its fade: budget kept: %s\n",
           bad ? "FAIL" : "ok");
    fails += bad != 0;
    for (p = 0; p < NPART; p++)
        memset(trk[p].v, 0, NVOICE * sizeof *trk[p].v);
}

static void engine(void)
{
    /* the 8 lanes: KICK SNARE CLAP HAT CL HAT OP TOM RIM BELL (the closed hat first: it would choke the open one) */
    static const uint8_t KIT[8] = {36, 38, 39, 42, 46, 45, 37, 56};
    uint32_t i, k, bad = 0, nv, live = 0, own = 0, quiet = 0;
    int32_t pk;
    char why[256] = "";
    for (i = 0; i < 8u; i++)                             /* each lane alone sounds */
        quiet += lane_alone(KIT[i], 8) < 200;
    kit_fresh();
    for (i = 0; i < 8u; i++)
        trk_note_on(&trk[0], KIT[i], 110);
    pk = blocks_peak(2);
    nv = voices_on(&trk[0]);
    for (k = 0; k < DV_NLANE; k++) {
        const drum_lane_t *L = &drum_kit_part(0)[k];
        live += L->r.on;
        own += L->owner && trk[0].v[L->owner - 1u].active && (uint32_t)trk[0].v[L->owner - 1u].s[0] == k;
    }
    if (quiet || nv != 8u || live != 8u || own != 8u || pk < 1000) {
        snprintf(why, sizeof why, " (%u lanes silent alone, %u voices, %u lanes live, %u owned, peak %d)", quiet, nv, live,
                 own, pk);
        bad++;
    }
    printf("drum_test: engine: the 8 lanes struck at once ring together (8 voices, each lane live on its own voice)%s: %s\n",
           why, why[0] ? "FAIL" : "ok");
    /* the same lane from two notes (low floor tom, then low tom): the second cuts the first */
    kit_fresh();
    trk_note_on(&trk[0], 41, 110);
    blocks_peak(4);
    trk_note_on(&trk[0], 43, 110);
    blocks_peak(2);
    k = voices_on(&trk[0]) == 1u && drum_kit_part(0)[DV_TOM].owner &&
        trk[0].v[drum_kit_part(0)[DV_TOM].owner - 1u].note == 43 && drum_kit_part(0)[DV_TOM].st == DRUM_GM[43 - 35][1];
    printf("drum_test: engine: a lane is mono, notes 41 then 43 (both TOM): one voice, the second hit: %s\n",
           k ? "ok" : "FAIL");
    bad += !k;
    /* a released key does not end the hit (ADSR at SUS 0, REL 0); the voice frees itself once the drum has rung out */
    kit_fresh();
    trk[0].p[P_SUS] = 0;
    trk[0].p[P_REL] = 0;
    trk[0].p[P_DEC] = 0;
    trk_note_on(&trk[0], 49, 110);                       /* crash */
    blocks_peak(1);
    trk_note_off(&trk[0], 49);
    pk = blocks_peak(FS / 2u / CTL);
    k = voices_on(&trk[0]) == 1u && drum_kit_part(0)[DV_BELL].r.on && blocks_peak(4) > 50;
    for (i = 0; i < 20u * FS / CTL && voices_on(&trk[0]); i++)
        blocks_peak(1);
    printf("drum_test: engine: a crash 0.5 s after its key-off still rings (ADSR SUS 0 REL 0), its voice free after "
           "%.1f s: %s\n", (double)i * CTL / FS + 0.5, k && !voices_on(&trk[0]) ? "ok" : "FAIL");
    bad += !k || voices_on(&trk[0]);
    /* VOICE MONO, GLIDE: the kit still plays POLY */
    kit_fresh();
    trk[0].p[P_VOICE] = V_MONO;
    trk[0].p[P_GLIDE] = 60;
    trk_note_on(&trk[0], 36, 110);
    trk_note_on(&trk[0], 38, 110);
    blocks_peak(2);
    k = voices_on(&trk[0]) == 2u && drum_kit_part(0)[DV_KICK].r.on && drum_kit_part(0)[DV_SNARE].r.on;
    printf("drum_test: engine: VOICE MONO and GLIDE set: kick and snare still sound together: %s\n", k ? "ok" : "FAIL");
    bad += !k;
    fails += bad != 0;
}

/* ----------------------------------------------------------- KIT 808 --- */
/* the mix's RMS (dB re full scale) over nb blocks */
static double blocks_rms(uint32_t nb)
{
    int32_t o[2 * CTL];
    double s = 0;
    uint32_t i, k;
    for (i = 0; i < nb; i++) {
        mix_block(o, CTL);
        for (k = 0; k < 2u * CTL; k++)
            s += (double)o[k] * o[k];
    }
    return 10 * log10(s / (2.0 * CTL * nb) + 1e-9) - 20 * log10(32768.0);
}
static double note_rms(int16_t kit, uint32_t note, uint32_t vel)   /* one hit through the engine: its first 0.2 s */
{
    kit_fresh();
    trk[0].p[P_E0] = kit;
    trk_note_on(&trk[0], note, vel);
    return blocks_rms(FS / 5u / CTL);
}

/* All 16 808 instruments, velocity, hat choke, release and parameter corners. */
static void kit_808(void)
{
    static const uint8_t KIT[8] = {36, 38, 39, 42, 46, 45, 37, 56};
    static const uint8_t ALL[DR_N] = {36, 38, 41, 45, 50, 64, 63, 62, 37, 75, 39, 70, 56, 49, 46, 42};   /* DR_* order */
    uint32_t i, k, bad = 0, n;
    char why[512] = "";
    for (i = 0; i < DR_N; i++) {                         /* each instrument: its circuit, it sounds, it ends */
        double r;
        kit_fresh();
        trk[0].p[P_E0] = DK_808;
        trk_note_on(&trk[0], ALL[i], 110);
        k = DV_TYPE_LANE[DRUM_GM[ALL[i] - 35][0]];
        n = drum_kit_part(0)[k].r.ins == i;
        r = blocks_rms(FS / 5u / CTL);
        trk_note_off(&trk[0], ALL[i]);
        for (k = 0; k < 8u * FS / CTL && voices_on(&trk[0]); k++)
            blocks_peak(1);
        if (!n || r < -60 || voices_on(&trk[0])) {
            snprintf(why + strlen(why), sizeof why - strlen(why), " note %u (%s%.0f dB%s);", ALL[i],
                     n ? "" : "not its circuit, ", r, voices_on(&trk[0]) ? ", rings on" : "");
            bad++;
        }
    }
    printf("drum_test: KIT 808: the 16 instruments from their GM notes sound and free their voices%s: %s\n", why,
           bad ? "FAIL" : "ok");
    fails += bad != 0;
    {   /* velocity, the trigger level: 127 louder than 64 (the circuits compress it: brighter more than louder) */
        static const uint8_t V[4] = {36, 38, 39, 56};
        for (i = 0, k = 1; i < 4u; i++) {
            double hi = note_rms(DK_808, V[i], 127), lo = note_rms(DK_808, V[i], 64);
            printf("drum_test: KIT 808: note %u at velocity 127 %+.1f dB over 64\n", V[i], hi - lo);
            k &= hi > lo + 2;
        }
        printf("drum_test: KIT 808: velocity 127 at least 2 dB over 64: %s\n", k ? "ok" : "FAIL");
        fails += !k;
    }
    {   /* the choke: a closed hat 100 ms into the open one */
        drum_lane_t *o;
        kit_fresh();
        trk[0].p[P_E0] = DK_808;
        trk_note_on(&trk[0], 46, 110);
        blocks_peak(FS / 10u / CTL);
        o = &drum_kit_part(0)[DV_HATO];
        k = o->r.on;
        trk_note_on(&trk[0], 42, 110);
        blocks_peak(FS / 20u / CTL);
        k = k && !o->r.on;
        printf("drum_test: KIT 808: a closed hat 100 ms into the open one ends it within 50 ms: %s\n", k ? "ok" : "FAIL");
        fails += !k;
    }
    {   /* Old custom-kit values still select the surviving 808 circuit. */
        kit_fresh();
        trk[0].p[P_E0] = 0;
        trk_note_on(&trk[0], 36, 110);
        k = drum_kit_part(0)[DV_KICK].r.ins == DR_BD && blocks_peak(4) > 200;
        printf("drum_test: retired custom kit plays 808: %s\n", k ? "ok" : "FAIL");
        fails += !k || ENG_DRUM.npresets != 6 || ENG_DRUM.edit[0].min != DK_808;
    }
    {   /* the corners: TUNE TONE DECY SNAP at 0 / 127, ACC 127, velocity 127, the whole kit at once: no clipping */
        int32_t pk = 0, q;
        uint32_t c;
        for (c = 0; c < 16u; c++) {
            kit_fresh();
            trk[0].p[P_E0] = DK_808;
            trk[0].p[P_E1] = c & 1u ? 127 : 0;
            trk[0].p[P_E2] = c & 2u ? 127 : 0;
            trk[0].p[P_E3] = c & 4u ? 127 : 0;
            trk[0].p[P_E4] = c & 8u ? 127 : 0;
            trk[0].p[P_E5] = 127;
            for (i = 0; i < 8u; i++)
                trk_note_on(&trk[0], KIT[i], 127);
            q = blocks_peak(FS / CTL);
            pk = q > pk ? q : pk;
        }
        k = pk < 32767;
        printf("drum_test: KIT 808 at the knobs' corners, the 8 lanes at velocity 127: peak %d (< full scale): %s\n", pk,
               k ? "ok" : "FAIL");
        fails += !k;
    }
}


int main(void) { lane_budget(); engine(); kit_808();
    printf("drum_test: %s\n", fails ? "FAILED" : "all checks ok"); return fails != 0; }
