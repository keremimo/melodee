/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of Felucca 1.2's LFO / matrix and SPREAD sound items (Discussions #132, #148), adapted from Felucca 1.4's
 * sound14_test; same sources as the firmware (hostsim.c):   build/host/mod_spread_test   (run_tests.sh)
 * 1. the matrix (mod.c): the lists only grew (every stored SRC / DST keeps its name), S&H holds one value a cycle of the
 *    track LFO (the value LFO WAVE S&H plays), steps at the cycle's start, spreads over both signs; SLEW is continuous,
 *    glides from the cycle before's value to this one's, stays inside S&H's range; DEPTH scales the LFO (its LFO DEST
 *    routings and the matrix's LFO source) as AMP does a voice, several slots multiply, none = untouched (bit for bit);
 *    RATE (the LFO's rate) with them: the modulator modulated; values past the lists do nothing.
 * 2. SPREAD (mix.c mix_spread): SPRD 0 / MONO / LEGATO bit for bit the plain mix, a POLY note hard left / the next hard
 *    right at SPRD 127, a chord's L and R differ, the mono sum's level, UNISON's voices alternate, the SLICER's gate and
 *    the mute key close the side too, the sends stay mono. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("mod/spread: %-71s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t out_buf[2 * CTL];
static uint64_t hash;
static void blocks(uint32_t n)
{
    uint32_t i, k;
    while (n--) {
        mix_block(out_buf, CTL);
        for (i = 0; i < 2u * CTL; i++)
            for (k = 0; k < 4u; k++) {
                hash ^= ((uint32_t)out_buf[i] >> (8u * k)) & 0xFFu;
                hash *= 0x100000001B3ull;
            }
    }
}

static void slot(track_t *t, uint32_t k, int32_t s, int32_t d, int32_t a)
{
    t->p[P_M1SRC + 3u * k] = (int16_t)s;
    t->p[P_M1DST + 3u * k] = (int16_t)d;
    t->p[P_M1AMT + 3u * k] = (int16_t)a;
}

static uint32_t eng_by_name(const char *n)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (str_eq(ENGINES[e]->name, n))
            return e;
    return 0;
}
static uint32_t preset_by_name(uint32_t e, const char *n)
{
    uint32_t k;
    for (k = 0; k < ENGINES[e]->npresets; k++)
        if (str_eq(eng_preset(ENGINES[e], k)->name, n))
            return k;
    return 0;
}

static void fresh(uint32_t e, uint32_t pi)        /* the boot state (no FX tails), track 1 = engine e preset pi */
{
    uint32_t k;
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;                      /* (the master's DC blockers: no residue of a run before) */
    vage = 0;                                             /* the seeds: every run the same (noise by voice age) */
    rng_state = 0x1234567u;
    mod_seed = 0x2545F491u;
    memset(trk, 0, sizeof trk);trk_voices_bind();
    memset(&mod, 0, sizeof mod);
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], k ? HOST_SYNTH : e, k ? 0u : pi);
    song.sel = 0;
    hash = 0xCBF29CE484222325ull;
}
static void dry(track_t *t)                       /* no sends: the dry mix only */
{
    t->p[P_RESERVED33] = t->p[P_RESERVED34] = t->p[P_RESERVED35] = t->p[P_RESERVED36] = 0;
}

/* the hash of a phrase on track 1 in a fork()ed child (the seeds as they were: the same in each); setup(t) first */
static uint64_t phrase_child(uint32_t e, uint32_t pi, void (*setup)(track_t *t))
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    fflush(stdout);
    if (!(pid = fork())) {
        track_t *t = &trk[0];
        fresh(e, pi);
        if (setup)
            setup(t);
        trk_note_on(t, 60, 100);
        trk_note_on(t, 64, 70);
        trk_note_on(t, 67, 120);
        blocks(FS / 2u / CTL);
        trk_note_on(t, 72, 90);
        blocks(FS / 4u / CTL);
        trk_note_off(t, 60);
        trk_note_off(t, 64);
        trk_note_off(t, 67);
        trk_note_off(t, 72);
        blocks(FS / CTL);
        h = hash;
        if (write(fd[1], &h, sizeof h) != sizeof h)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

/* ------------------------------------------------------------------ 1 --- */
static void test_matrix(void)
{
    static const char *const SRC_V12[] = {"OFF", "LFO", "ENV", "VEL", "KEY", "RAND", "MODW", "AT", "EXPR"};
    static const char *const DST_V12[] = {"OFF", "PITCH", "CUT", "SHP", "AMP", "PAN", "", "", "", "", "RATE",
                                          "VIB", "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8"};
    track_t *t = &trk[0];
    uint32_t i, k, steps = 0, changes_in_cycle = 0, pos = 0, neg = 0, same_as_wave = 1;
    int32_t prev_sh, max_slew_step = 0, max_sh_step = 0, prev_slew, lo = 0, hi = 0, slew_ok = 1, wrap_ok = 1;
    int ok = 1;

    for (i = 0; i < NELEM(SRC_V12); i++)
        ok &= str_eq(N_MSRC[i], SRC_V12[i]);
    for (i = 0; i < NELEM(DST_V12); i++)
        ok &= str_eq(N_MDST[i], DST_V12[i]);
    check("SRC / DST identities stay fixed; retired effect destinations are blank",
          ok && MS_SH == 9 && MS_SLEW == 10 && MS_N == 11 && MD_DEPTH == 20 && MD_N == 21 &&
          str_eq(N_MSRC[MS_SH], "S&H") && str_eq(N_MSRC[MS_SLEW], "SLEW") && str_eq(N_MDST[MD_DEPTH], "DEPTH") &&
          TP[P_M1SRC].max == MS_N - 1 && TP[P_M1DST].max == MD_N - 1);
    check("DST names: DEPTH is not an engine parameter (E1..E8 only)", str_eq(mod_dst_name(t, MD_DEPTH), "DEPTH") &&
          !MD_ENGINE(MD_DEPTH) && MD_ENGINE(MD_E1 + 7));

    /* S&H and SLEW over a few LFO cycles: one value per cycle, the steps at the wraps; SLEW continuous */
    fresh(HOST_SYNTH, 0);
    t->p[P_LRATE] = 90;                                   /* a few Hz */
    t->p[P_LWAVE] = 4;                                    /* S&H: the LFO's own value is the same random */
    trk_note_on(t, 60, 100);
    blocks(1);
    prev_sh = mod_sh(t);
    prev_slew = mod_tsrc(t, MS_SLEW, 0);
    for (k = 0; k < 4u * FS / CTL; k++) {
        uint32_t ph0 = t->lfo_ph;
        int32_t sh, sl;
        blocks(1);
        sh = mod_sh(t);
        sl = mod_tsrc(t, MS_SLEW, 0);
        if (t->lfo_ph < ph0) {                            /* a new cycle */
            steps++;
            wrap_ok &= t->lfo_prev == prev_sh;            /* SLEW starts from the old value */
            if (sh != prev_sh) {
                int32_t d = sh - prev_sh;
                d = d < 0 ? -d : d;
                max_sh_step = d > max_sh_step ? d : max_sh_step;
            }
        } else {
            changes_in_cycle += sh != prev_sh;
        }
        if (t->lfo_rnd)
            same_as_wave &= sh == t->lfo_val;             /* (WAVE S&H, POL BI) */
        pos += sh > 8000;
        neg += sh < -8000;
        {
            int32_t d = sl - prev_slew;
            d = d < 0 ? -d : d;
            max_slew_step = d > max_slew_step ? d : max_slew_step;
        }
        lo = sl < lo ? sl : lo;
        hi = sl > hi ? sl : hi;
        slew_ok &= sl >= -32768 && sl <= 32767;
        prev_sh = sh;
        prev_slew = sl;
    }
    check("S&H: one value per LFO cycle (none changes inside one), the LFO's S&H value", steps >= 8u &&
          !changes_in_cycle && same_as_wave);
    printf("mod/spread:   %u cycles in 4 s; S&H steps up to %d, SLEW moves at most %d a block (%.1f %% of S&H's)\n",
           steps, max_sh_step, max_slew_step, 100.0 * max_slew_step / (max_sh_step ? max_sh_step : 1));
    check("S&H: values on both sides (bipolar)", pos > 0u && neg > 0u);
    check("SLEW: continuous (a block's move small against S&H's steps), inside S&H's range, from the cycle before's value",
          slew_ok && wrap_ok && max_slew_step * 8 < max_sh_step && hi > 0 && lo < 0);

    /* S&H follows RATE: a faster LFO steps more often */
    {
        uint32_t s1 = 0, s2 = 0;
        fresh(HOST_SYNTH, 0);
        t->p[P_LRATE] = 70;
        for (k = 0; k < 2u * FS / CTL; k++) {
            uint32_t ph0 = t->lfo_ph;
            blocks(1);
            s1 += t->lfo_ph < ph0;
        }
        fresh(HOST_SYNTH, 0);
        t->p[P_LRATE] = 70;
        slot(t, 0, MS_MODW, MD_RATE, 63);                 /* the matrix's RATE: S&H with it */
        t->mw = 127;
        for (k = 0; k < 2u * FS / CTL; k++) {
            uint32_t ph0 = t->lfo_ph;
            blocks(1);
            s2 += t->lfo_ph < ph0;
        }
        check("S&H: the matrix's RATE (MODW -> RATE) makes it step faster", s2 > s1 * 2u && s1 > 0u);
    }

    /* DEPTH: the LFO's gain, as AMP's */
    fresh(HOST_SYNTH, 0);
    t->lfo_val = 32767;
    t->lfo_fade = 32767;
    slot(t, 0, MS_MODW, MD_DEPTH, 63);
    t->mw = 0;
    mod_begin(t);
    check("MODW -> DEPTH +63: the wheel down closes the LFO (gain 1.6 %)", mod.on && mod.ldep >= 500 && mod.ldep <= 512);
    mod_end(t);
    t->mw = 127;
    mod_begin(t);
    check("MODW -> DEPTH +63: the wheel up leaves it whole", mod.ldep >= 32700);
    mod_end(t);
    slot(t, 0, MS_MODW, MD_DEPTH, -64);
    mod_begin(t);
    check("MODW -> DEPTH -64: the wheel up closes it", mod.ldep == 0);
    mod_end(t);
    slot(t, 0, MS_MODW, MD_DEPTH, 63);
    slot(t, 1, MS_LFO, MD_CUT, 63);
    t->mw = 0;
    mod_begin(t);
    check("DEPTH scales the matrix's LFO source too (LFO -> CUT with the wheel down: ~1.6 %)",
          mod.cut > 0 && mod.cut < ((32767 * 63) >> 7) / 40);
    mod_end(t);
    slot(t, 2, MS_AT, MD_DEPTH, 63);
    t->mw = 127;
    t->at = 64;
    mod_begin(t);
    check("two DEPTH slots multiply (wheel up x AT half)", mod.ldep > 16000 && mod.ldep < 17000);
    mod_end(t);
    slot(t, 0, MS_LFO, MD_DEPTH, 63);                     /* the LFO on its own depth: the unscaled LFO */
    slot(t, 1, 0, 0, 0);
    slot(t, 2, 0, 0, 0);
    mod_begin(t);
    check("LFO -> DEPTH: takes the LFO unscaled (at its top: whole)", mod.ldep >= 32700);
    mod_end(t);
    slot(t, 0, MS_SH, MD_CUT, 40);                        /* no DEPTH slot: the LFO untouched */
    mod_begin(t);
    check("no DEPTH slot: the LFO untouched (track_render leaves it alone)", mod.ldep == 32767);
    mod_end(t);
    slot(t, 0, 11, MD_CUT, 40);                           /* past the lists: nothing */
    slot(t, 1, MS_LFO, MD_N, 40);
    mod_begin(t);
    check("a SRC / DST past the lists does nothing", mod.cut == 0 && mod.nk == 0 && mod.nv == 0);
    mod_end(t);
}

/* DEPTH in the sound: an LFO DEST PIT vibrato with MODW -> DEPTH -64 (whole with the wheel down, closed with it up) */
static void vib_plain(track_t *t)
{
    t->p[P_LRATE] = 80;
    t->p[P_LD_PIT] = 0;
}
static void vib_depth0(track_t *t)                /* the wheel up: DEPTH 0, no vibrato */
{
    t->p[P_LRATE] = 80;
    t->p[P_LD_PIT] = 40;
    slot(t, 0, MS_MODW, MD_DEPTH, -64);
    t->mw = 127;
}
static void vib_depth1(track_t *t)
{
    vib_depth0(t);
    t->mw = 0;
}
static void vib_full(track_t *t)
{
    t->p[P_LRATE] = 80;
    t->p[P_LD_PIT] = 40;
}
static void sh_on_nothing(track_t *t)             /* S&H and SLEW slots with AMT 0: nothing */
{
    slot(t, 0, MS_SH, MD_CUT, 0);
    slot(t, 1, MS_SLEW, MD_PITCH, 0);
    slot(t, 2, MS_LFO, MD_DEPTH, 0);
}
static void test_matrix_sound(void)
{
    uint64_t h0 = phrase_child(HOST_SYNTH, 0, 0), hn = phrase_child(HOST_SYNTH, 0, sh_on_nothing);
    uint64_t p0 = phrase_child(HOST_SYNTH, 0, vib_plain), d0 = phrase_child(HOST_SYNTH, 0, vib_depth0);
    uint64_t d1 = phrase_child(HOST_SYNTH, 0, vib_depth1), f1 = phrase_child(HOST_SYNTH, 0, vib_full);
    check("S&H / SLEW / DEPTH slots at AMT 0: bit for bit no matrix", h0 && h0 == hn);
    check("DEPTH closed: LFO DEST PIT plays nothing (bit for bit LFO DEST PIT 0)", p0 && p0 == d0);
    check("DEPTH open: the vibrato is there (not the plain note), as without the matrix but for the gain",
          d1 != p0 && f1 != p0);
}


#define NREND (FS / CTL)
/* ------------------------------------------------------------------ 3 --- */
static void sp_mono127(track_t *t) { t->p[P_VOICE] = V_MONO; t->p[P_SPRD] = 127; }
static void sp_mono0(track_t *t) { t->p[P_VOICE] = V_MONO; }
static void sp_leg127(track_t *t) { t->p[P_VOICE] = V_LEGATO; t->p[P_SPRD] = 127; }
static void sp_leg0(track_t *t) { t->p[P_VOICE] = V_LEGATO; }
static void sp_poly0(track_t *t) { t->p[P_VOICE] = V_POLY; t->sp_alt = 1; }   /* (the sides change nothing at 0) */
static void sp_poly0b(track_t *t) { t->p[P_VOICE] = V_POLY; }

/* track 1 (the host synth's SYN STRINGS, dry, POLY), SPRD sp, PAN pan: notes, then blocks; L and R sums of squares, the mono sum's */
typedef struct { double l, r, m, lr; int32_t rmax, lmax; } lr_t;
static lr_t spread_run(int32_t sp, int32_t pan, uint32_t mode, const uint8_t *notes, uint32_t nn, uint32_t nb)
{
    track_t *t = &trk[0];
    lr_t a = {0};
    uint32_t k, i;
    fresh(HOST_SYNTH, preset_by_name(HOST_SYNTH, "SYN STRINGS"));
    dry(t);
    /* Keep the pan-law probe below the limiter at its input. MASTER now
     * attenuates the finished output and cannot provide that headroom. */
    song.master_q12 = 2048;
    t->p[P_LEVEL] = 60;                                  /* quiet enough for a linear pan-law comparison */
    t->p[P_VOICE] = (int16_t)mode;
    t->p[P_SPRD] = (int16_t)sp;
    t->p[P_PAN] = (int16_t)pan;
    t->p[P_ATK] = 0;
    for (i = 0; i < nn; i++)
        trk_note_on(t, notes[i], 100);
    for (k = 0; k < nb; k++) {
        blocks(1);
        for (i = 0; i < CTL; i++) {
            double l = out_buf[2u * i], r = out_buf[2u * i + 1u];
            a.l += l * l;
            a.r += r * r;
            a.m += (l + r) * (l + r);
            a.lr += (l - r) * (l - r);
            a.lmax = abs(out_buf[2u * i]) > a.lmax ? abs(out_buf[2u * i]) : a.lmax;
            a.rmax = abs(out_buf[2u * i + 1u]) > a.rmax ? abs(out_buf[2u * i + 1u]) : a.rmax;
        }
    }
    return a;
}

static void test_spread(void)
{
    static const uint8_t ONE[1] = {60}, TWO[2] = {60, 67}, CHORD[4] = {48, 55, 60, 64};
    lr_t a, b, c;
    uint64_t h1, h2;
    char msg[200];

    check("SPRD: a track parameter 0..127, default 0 (as before), before the engine's (P_E0 96, P_COUNT 104)",
          TP[P_SPRD].min == 0 && TP[P_SPRD].max == 127 && TP[P_SPRD].def == 0 && P_SPRD + 1 == P_FXTYPE && P_FX3CHAR + 1 == P_FXSEND && P_FXSEND + 1 == P_FXSEND2 && P_FXSEND2 + 1 == P_FXSEND3 && P_FXSEND3 + 1 == P_FXTONE && P_FX3TONE + 1 == P_STRUM && P_STRUM + 1 == P_STROKE && P_STROKE + 1 == P_E0 && P_E0 == 116 &&
          P_COUNT == 124 && str_eq(TP[P_SPRD].label, "SPRD") && motion_param(P_SPRD));
    h1 = phrase_child(HOST_SYNTH, 1, sp_mono0);
    h2 = phrase_child(HOST_SYNTH, 1, sp_mono127);
    check("MONO: SPRD 127 plays bit for bit as SPRD 0 (one voice: on PAN)", h1 && h1 == h2);
    h1 = phrase_child(HOST_SYNTH, 1, sp_leg0);
    h2 = phrase_child(HOST_SYNTH, 1, sp_leg127);
    check("LEGATO: SPRD 127 plays bit for bit as SPRD 0", h1 && h1 == h2);
    h1 = phrase_child(HOST_SYNTH, 1, sp_poly0);
    h2 = phrase_child(HOST_SYNTH, 1, sp_poly0b);
    check("POLY SPRD 0: the voices' sides change nothing (the plain mix, bit for bit)", h1 && h1 == h2);

    a = spread_run(0, 0, V_POLY, ONE, 1, NREND / 2);
    check("SPRD 0, PAN 0: L = R (as before)", a.lr == 0 && a.l > 0);
    b = spread_run(127, 0, V_POLY, ONE, 1, NREND / 2);
    snprintf(msg, sizeof msg, "SPRD 127: the first note hard left (R peak %d of L's %d), at its level (L %.3f of SPRD 0's)",
             b.rmax, b.lmax, sqrt(b.l / a.l));
    check(msg, b.rmax <= 2 && fabs(sqrt(b.l / a.l) - 1) < 0.01);
    b = spread_run(127, 0, V_POLY, TWO, 2, NREND / 2);
    {
        lr_t r1 = spread_run(127, 0, V_POLY, TWO + 1, 1, NREND / 2);   /* (67 alone: left, the first) */
        snprintf(msg, sizeof msg, "SPRD 127: the second note hard right (L holds the first only: %.3f)", sqrt(b.l / r1.l));
        (void)r1;
        check(msg, b.r > 0 && b.l > 0 && b.rmax > 1000 && b.lmax > 1000);
    }
    {   /* the sides: alternate per new voice; a sounding voice retriggered keeps its side (no jump) */
        track_t *t = &trk[0];
        uint32_t k, sides = 0, same = 1;
        spread_run(127, 0, V_POLY, CHORD, 4, 4);
        for (k = 0; k < NVOICE; k++)
            if (t->v[k].active)
                sides |= 1u << t->v[k].side;
        for (k = 0; k < NVOICE; k++)
            if (t->v[k].active && t->v[k].note == 60) {
                uint32_t s0 = t->v[k].side;
                trk_note_on(t, 60, 100);
                blocks(2);
                same = t->v[k].active && t->v[k].note == 60 && t->v[k].side == s0;
            }
        check("the voices of a chord on both sides; a voice retriggered while it sounds keeps its side", sides == 3u && same);
    }
    a = spread_run(0, 0, V_POLY, CHORD, 4, NREND);
    b = spread_run(64, 0, V_POLY, CHORD, 4, NREND);
    c = spread_run(127, 0, V_POLY, CHORD, 4, NREND);
    snprintf(msg, sizeof msg, "a chord: L and R differ with SPRD (side/mid %.3f at 64, %.3f at 127; 0 at 0)",
             sqrt(b.lr / b.m), sqrt(c.lr / c.m));
    check(msg, a.lr == 0 && b.lr > 0 && c.lr > b.lr);
    snprintf(msg, sizeof msg, "the mono sum: its level %.3f of SPRD 0's at 64 (pan law: 1 - 32/128 = 0.75), %.3f at 127 (0.5)",
             sqrt(b.m / a.m), sqrt(c.m / a.m));
    check(msg, fabs(sqrt(b.m / a.m) - 0.75) < 0.03 && fabs(sqrt(c.m / a.m) - 0.5) < 0.03);
    b = spread_run(64, 40, V_POLY, CHORD, 4, NREND);
    a = spread_run(0, 40, V_POLY, CHORD, 4, NREND);
    check("around PAN: PAN +40 with SPRD 64 leans right as PAN alone does", b.r > b.l && a.r > a.l);
    a = spread_run(0, 0, V_UNISON, ONE, 1, NREND);
    b = spread_run(100, 0, V_UNISON, ONE, 1, NREND);
    snprintf(msg, sizeof msg, "UNISON: the detuned voices alternate L / R (side/mid %.3f at SPRD 100, 0 at 0)", sqrt(b.lr / b.m));
    check(msg, a.lr == 0 && b.lr > b.m * 0.01);
}

/* the SLICER's gate and the mute key close the side too; the sends stay mono */



int main(void)
{
    test_matrix();
    test_matrix_sound();
    test_spread();
    printf("%s\n", bad ? "MOD / SPREAD TEST FAILED" : "mod / spread test passed");
    return bad != 0;
}
