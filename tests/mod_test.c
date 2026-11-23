/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the modulation matrix (firmware/src/mod.c), same sources as the firmware (through hostsim.c):
 *   build/host/mod_test [DEMO_DIR]      (run_tests.sh builds and runs it; demos in build/mod_demo/)
 * 1. off: slots that do nothing (SRC, DST or AMT at 0) render bit-identically to no matrix at all; the
 *    stored parameters are never left modulated after a block.
 * 2. the math: every source on each kind of destination (per voice: PITCH CUT SHP AMP; per block: PAN, the
 *    sends, RATE, VIB, an engine parameter), bipolar sources and amounts, sums, clamping to the ranges,
 *    per-voice sources on per-block destinations (the latest note-on), RAND without the shared rng.
 * 3. MIDI: CC1 / CC11 / CC121 and channel aftertouch over USB (packets) and TRS (bytes, running status) reach
 *    the matrix of the channel's track (ch 1..4 the parts, others the selected track).
 * 4. cost: instructions per sample (kernel-counted) of a Prophet preset and of a 4-part mix with every
 *    slot of every part active, against the same with the matrix off; fails above MOD_COST_MAX.
 * 5. demos: LFO -> CUT, VEL -> AMP, MODW -> VIB as WAVs in DEMO_DIR. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define MOD_MIX_IPS_MAX 256.0     /* fixed matrix work with the four shipped synths (FM6, CZ-1, PROPHET, SID: ~165 measured,
                                   * nightly 045e599a 161; 96 held the retired PHASE / PHYS / TRIO mix): bounded
                                   * independently of the dry mixer, with room for the process counter's noise under
                                   * parallel jobs */
#define MOD_COST_MAX 5.0          /* % more instructions per sample with every slot active, at most */

static int bad;
static void check(const char *what, int ok)
{
    printf("mod: %-74s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t out_buf[2 * CTL];
static uint64_t hash;
static void blocks(uint32_t n)
{
    uint32_t i, k;
    while (n--) {
        mix_block_q15(out_buf, CTL);
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
static uint32_t edit_by_label(uint32_t e, const char *l, uint32_t fallback)   /* E index 0..7 */
{
    uint32_t k;
    for (k = 0; k < 8u; k++)
        if (str_eq(ENGINES[e]->edit[k].label, l))
            return k;
    return fallback;
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
    memset(trk, 0, sizeof trk);trk_voices_bind();
    memset(&mod, 0, sizeof mod);
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], k ? 0u : e, k ? 0u : pi);
    song.sel = 0;
    hash = 0xCBF29CE484222325ull;
}

/* a phrase on track 1: a chord, a note, releases; the hash of the output */
static uint64_t phrase(void)
{
    track_t *t = &trk[0];
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
    return hash;
}

/* ------------------------------------------------------------------ 1 --- */
/* the phrase's hash in a fork()ed child (the seeds as they were: the same in each); setup k:
 * 0 no matrix, 1 slots that do nothing, 2 an active matrix. The child answers 0 when p[] was left changed */
static uint64_t phrase_child(uint32_t k)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    fflush(stdout);
    if (!(pid = fork())) {
        int16_t keep[P_COUNT];
        fresh(HOST_SYNTH, 0);
        if (k == 1u) {
            slot(&trk[0], 0, MS_LFO, MD_CUT, 0);   /* AMT 0 */
            slot(&trk[0], 1, MS_OFF, MD_PAN, 40);  /* SRC OFF */
            slot(&trk[0], 2, MS_VEL, MD_OFF, -30); /* DST OFF */
        } else if (k == 2u) {
            slot(&trk[0], 0, MS_LFO, MD_CUT, 40);
            slot(&trk[0], 1, MS_MODW, MD_PAN, 63);
            slot(&trk[0], 2, MS_LFO, MD_E1 + 4, -50);
            slot(&trk[0], 3, MS_ENV, MD_RESERVED9, 30);
            trk[0].mw = 100;
        }
        memcpy(keep, trk[0].p, sizeof keep);
        h = phrase();
        if (memcmp(keep, trk[0].p, sizeof keep) || mod.on)
            h = 0;
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

static void test_off(void)
{
    uint64_t h0 = phrase_child(0), h1 = phrase_child(1), h2 = phrase_child(2);
    check("slots that do nothing: bit-identical to the matrix off", h0 && h0 == h1);
    check("an active matrix changes the sound, and leaves no modulated value in p[]", h2 && h2 != h0);
}

/* ------------------------------------------------------------------ 2 --- */
static void lfo_at(track_t *t, int32_t v)          /* the LFO value the next block's matrix sees */
{
    t->lfo_val = v;
    t->lfo_fade = 32767;
}

static void test_math(void)
{
    track_t *t = &trk[0];
    int32_t lfo;
    vmod_t m;
    voice_t *v;
    uint32_t e_trio, k_pw;

    /* per-track sources on per-voice destinations */
    fresh(HOST_SYNTH, 0);
    lfo_at(t, 32767);
    lfo = mulq15(32767, 32767);
    slot(t, 0, MS_LFO, MD_CUT, 63);
    mod_begin(t);
    check("LFO -> CUT +63: +63 steps at the top of the LFO (as ENV DEST)", mod.on && mod.cut == (lfo * 63) >> 7 && mod.cut / 256 == 62);
    mod_end(t);
    slot(t, 0, MS_LFO, MD_PITCH, 63);
    mod_begin(t);
    check("LFO -> PITCH +63: +11.8 semitones (as LFO DEST PIT)", mod.pit == (lfo * 63 * 3) >> 15 && mod.pit == 188);
    mod_end(t);
    lfo_at(t, -32768);
    slot(t, 0, MS_LFO, MD_SHP, 63);
    mod_begin(t);
    check("LFO bipolar: the bottom of the LFO pulls SHP down", mod.shp < -16000);
    mod_end(t);
    slot(t, 0, MS_LFO, MD_SHP, -64);
    mod_begin(t);
    check("a negative AMT inverts it", mod.shp > 16000);
    mod_end(t);
    lfo_at(t, 0);
    slot(t, 0, MS_MODW, MD_AMP, 64 - 1);
    t->mw = 0;
    mod_begin(t);
    check("MODW -> AMP +63: the wheel down closes the amp (gain 1.6 %)", mod.amp && mod.gain >= 500 && mod.gain <= 512);
    mod_end(t);
    t->mw = 127;
    mod_begin(t);
    check("MODW -> AMP +63: the wheel up opens it fully", mod.gain >= 32760);
    mod_end(t);
    slot(t, 0, MS_AT, MD_AMP, -64);
    t->at = 127;
    mod_begin(t);
    check("AT -> AMP -64: full pressure closes it", mod.gain < 200);
    mod_end(t);
    slot(t, 0, MS_EXPR, MD_AMP, 63);
    mod_begin(t);
    check("EXPR before any CC11 is full (the MIDI default): AMP open", mod.gain >= 32760);
    mod_end(t);
    slot(t, 0, MS_LFO, MD_AMP, 64 - 1);
    mod_begin(t);
    check("LFO -> AMP: bipolar as 0..1 (half way at an LFO of 0)", mod.gain > 16000 && mod.gain < 17000);
    mod_end(t);

    /* per-voice sources on per-voice destinations (mod_voice) */
    fresh(HOST_SYNTH, 0);
    t->p[P_VOICE] = V_POLY;
    slot(t, 0, MS_KEY, MD_CUT, 63);
    slot(t, 1, MS_VEL, MD_AMP, 63);
    slot(t, 2, MS_ENV, MD_SHP, 40);
    slot(t, 3, MS_RAND, MD_PITCH, 20);
    {
        uint32_t rs = rng_state;
        trk_note_on(t, 72, 127);
        trk_note_on(t, 48, 1);
        check("RAND per note-on: its own generator (the shared rng untouched)", rng_state == rs && t->v[0].mrnd != t->v[1].mrnd);
    }
    mod_begin(t);
    memset(&m, 0, sizeof m);
    m.envq15 = 16384;
    m.shape = 64 << 8;
    m.pitch16 = 1000;
    m.inc = pitch_inc(1000);
    m.amp1 = 20000;
    v = &t->v[0];
    mod_voice(t, v, &m, 0);
    check("KEY -> CUT +63: an octave above C4 = +12 steps (key tracking 1:1)", m.cutoff == (12 * 512 * 63) >> 7 && m.cutoff / 256 == 11);
    check("VEL -> AMP +63: velocity 127 keeps the amplitude", m.amp1 == mulq15(20000, mulq15(32767, mod_gain(MS_VEL, 127 * 258, 63))) && m.amp1 > 19900 && v->env_out == m.amp1);
    check("ENV -> SHP +40: half the envelope = +20 steps", m.shape == (64 << 8) + ((16384 * 40) >> 7));
    check("RAND -> PITCH: the voice's own random offset", m.pitch16 == 1000 + ((v->mrnd * 20 * 3) >> 15) && m.inc == pitch_inc(m.pitch16));
    memset(&m, 0, sizeof m);
    m.amp1 = 20000;
    mod_voice(t, &t->v[1], &m, 0);
    check("KEY below C4 negative, VEL 1 -> AMP almost closed", m.cutoff == -((12 * 512 * 63) >> 7) && m.amp1 < 600);
    mod_end(t);

    /* clamping of the per-voice sums */
    fresh(HOST_SYNTH, 0);
    lfo_at(t, 32767);
    for (k_pw = 0; k_pw < 4u; k_pw++)
        slot(t, k_pw, MS_LFO, k_pw < 2u ? MD_CUT : MD_PITCH, 63);
    trk_note_on(t, 120, 100);
    lfo_at(t, 32767);                              /* (a note-on restarts the LFO and its FADE) */
    mod_begin(t);
    memset(&m, 0, sizeof m);
    m.cutoff = 100 << 8;
    m.pitch16 = 1920;
    mod_voice(t, &t->v[0], &m, 0);
    check("CUT sums clamp to what the fixed routings reach (+150), PITCH to 2047", m.cutoff == 150 * 256 && m.pitch16 == 2047);
    mod_end(t);

    /* per-block destinations: AMT 64 = the whole range, clamped, restored */
    fresh(HOST_SYNTH, 0);
    t->p[P_PAN] = 0;
    t->mw = 127;
    slot(t, 0, MS_MODW, MD_PAN, 16);
    mod_begin(t);
    check("MODW -> PAN +16: a quarter of the range (+31)", t->p[P_PAN] == ((((127 * 258) * 16) >> 6) * 127 >> 15) && t->p[P_PAN] == 31);
    mod_end(t);
    check("PAN restored after the block", t->p[P_PAN] == 0);
    slot(t, 1, MS_MODW, MD_PAN, 16);
    mod_begin(t);
    check("two slots on PAN add up (+62)", t->p[P_PAN] == 62);
    mod_end(t);
    slot(t, 1, MS_MODW, MD_PAN, 63);
    mod_begin(t);
    check("PAN clamps to +63", t->p[P_PAN] == 63);
    mod_end(t);
    slot(t, 1, MS_OFF, MD_OFF, 0);
    slot(t, 0, MS_AT, MD_RESERVED9, -64);
    t->at = 127;
    t->p[P_RESERVED36] = 50;
    mod_begin(t);
    check("retired REV destination is ignored", t->p[P_RESERVED36] == 50);
    mod_end(t);
    lfo_at(t, -32768);
    slot(t, 0, MS_LFO, MD_RESERVED6, 32);
    slot(t, 1, MS_LFO, MD_RESERVED7, -32);
    slot(t, 2, MS_LFO, MD_RATE, 32);
    slot(t, 3, MS_LFO, MD_VIB, 32);
    t->p[P_RESERVED33] = t->p[P_RESERVED34] = 64;
    t->p[P_LRATE] = 100;
    t->p[P_LD_PIT] = 0;
    lfo = mulq15(-32768, 32767);
    mod_begin(t);
    {
        int32_t up = (((lfo * 32) >> 6) * 127) >> 15, dn = (((lfo * -32) >> 6) * 127) >> 15;
        check("retired DIST / CHO ignored; RATE, VIB retain bipolar modulation",
              up < -30 && dn > 30 && t->p[P_RESERVED33] == 64 && t->p[P_RESERVED34] == 64 && t->p[P_LRATE] == 100 + up &&
              t->p[P_LD_PIT] == up);
    }
    mod_end(t);
    check("every per-block value restored", t->p[P_RESERVED33] == 64 && t->p[P_RESERVED34] == 64 && t->p[P_LRATE] == 100 && t->p[P_LD_PIT] == 0);

    /* an engine parameter: its descriptor's range (the Prophet's PWA, RES: -63..63) */
    e_trio = ENGI_PROPHET;
    k_pw = edit_by_label(e_trio, "PWA", 4);
    fresh(e_trio, 0);
    lfo_at(t, 32767);
    t->p[P_E0 + k_pw] = 50;
    slot(t, 0, MS_LFO, MD_E1 + (int32_t)k_pw, 40);
    mod_begin(t);
    check("LFO -> Prophet PWA: clamped to its range (-63..63)", t->p[P_E0 + k_pw] == 63);
    mod_end(t);
    t->p[P_E0 + 1] = 0;                            /* RES: -63..63 */
    slot(t, 0, MS_MODW, MD_E1 + 1, -32);
    t->mw = 127;
    mod_begin(t);
    check("MODW -> E2 -32: half its range down (the Prophet's RES -63..63)",
          t->p[P_E0 + 1] == (((((127 * 258) * -32) >> 6) * (ENGINES[e_trio]->edit[1].max - ENGINES[e_trio]->edit[1].min)) >> 15) &&
          t->p[P_E0 + 1] < -10);
    mod_end(t);
    check("the engine parameters restored", t->p[P_E0 + k_pw] == 50 && t->p[P_E0 + 1] == 0);
    check("DST names: E1..E8 by the engine's labels", str_eq(mod_dst_name(t, MD_E1 + (int32_t)k_pw), "PWA") &&
                                                          str_eq(mod_dst_name(t, MD_CUT), "CUT"));

    /* per-voice sources on a per-block destination: the latest note-on */
    fresh(HOST_SYNTH, 0);
    slot(t, 0, MS_VEL, MD_PAN, 63);
    trk_note_on(t, 60, 20);
    mod_begin(t);
    {
        int32_t p1 = t->p[P_PAN];
        mod_end(t);
        trk_note_on(t, 64, 127);
        mod_begin(t);
        check("VEL -> PAN: the latest note-on's velocity", p1 == ((((20 * 258) * 63) >> 6) * 127 >> 15) && t->p[P_PAN] == 63);
        mod_end(t);
    }
    slot(t, 0, MS_KEY, MD_RESERVED8, 63);
    t->p[P_RESERVED35] = 64;
    mod_begin(t);
    check("retired DLY destination is ignored", t->p[P_RESERVED35] == 64);
    mod_end(t);
    slot(t, 0, MS_ENV, MD_PAN, 63);
    blocks(FS / 10u / CTL);
    mod_begin(t);
    check("ENV -> PAN: the latest note's envelope (sounding: > 0)", t->p[P_PAN] > 20 && t->m_env > 0);
    mod_end(t);
}

/* ------------------------------------------------------------------ 3 --- */
static void pkt(uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_in_q[mi_w++ % MQ] = (st >> 4) | st << 8 | d1 << 16 | d2 << 24;
}
/* fn() in a fork()ed child (the tests' state stays as it was); its result */
static int in_child(int (*fn)(void))
{
    int st = 0;
    pid_t pid;
    fflush(stdout);
    if (!(pid = fork()))
        _exit(fn() ? 0 : 1);
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}
static int pan_e2e(void)                       /* MODW -> PAN moves the pan the mix uses */
{
    int64_t l = 0, r = 0;
    uint32_t i, k;
    fresh(HOST_SYNTH, 0);
    slot(&trk[0], 0, MS_MODW, MD_PAN, 63);
    trk[0].p[P_RESERVED34] = trk[0].p[P_RESERVED35] = trk[0].p[P_RESERVED36] = 0;   /* (the buses are in the middle) */
    pkt(0xB0, 1, 127);
    trk_note_on(&trk[0], 60, 100);
    blocks(FS / 10u / CTL);
    for (k = 0; k < FS / 10u / CTL; k++) {
        blocks(1);
        for (i = 0; i < CTL; i++) {
            l += labs(out_buf[2u * i]);
            r += labs(out_buf[2u * i + 1u]);
        }
    }
    return r > 20 * l && l > 0 && trk[0].p[P_PAN] == 0;
}

static void test_midi(void)
{
    static const uint8_t TRS[] = {0xB0, 1, 0x40, 1, 0x7F, 0xF8, 11, 0x20, 0xD2, 0x33, 0x34, 0x90, 60, 0, 0xB4, 1, 9};
    uint32_t i;
    fresh(HOST_SYNTH, 0);
    song.sel = 1;
    pkt(0xB0, 1, 100);                           /* ch 1 CC1 */
    pkt(0xB2, 11, 40);                           /* ch 3 CC11 */
    pkt(0xD3, 77, 0);                            /* ch 4 aftertouch */
    pkt(0xB6, 1, 55);                            /* ch 7: filtered in CH1-4 */
    pkt(0xB0, 7, 10);                            /* other CCs: ignored */
    blocks(1);
    check("USB: CC1 ch 1 -> part 1, CC11 ch 3 -> part 3, AT ch 4 -> part 4",
          trk[0].mw == 100 && trk[2].ex_off == 127 - 40 && trk[3].at == 77 && trk[0].at == 0 && trk[0].ex_off == 0);
    check("USB: CC1 on ch 7 is filtered in CH1-4", trk[1].mw == 0 && trk[2].mw == 0);
    pkt(0xB0, 121, 0);                           /* reset all controllers */
    blocks(1);
    check("CC121 resets MODW / AT / EXPR of the channel's track", trk[0].mw == 0 && trk[1].mw == 0);
    fresh(HOST_SYNTH, 0);
    song.sel = 1;
    for (i = 0; i < sizeof TRS; i++)
        um_byte(TRS[i]);
    blocks(1);
    check("TRS: CC1, running status, realtime between, CC11, AT (1 data byte, running status) -> parts",
          trk[0].mw == 0x7F && trk[0].ex_off == 127 - 0x20 && trk[2].at == 0x34 && trk[0].at == 0);
    check("TRS: ch 5 is filtered in CH1-4", trk[1].mw == 0);
    song.g[G_ROUTE] = 1;
    um_byte(0xB4); um_byte(1); um_byte(9); blocks(1);
    check("TRS: SEL sends ch 5 CC1 to the selected track (2)", trk[1].mw == 9);
    check("MODW -> PAN +63 through MIDI: the part is on the right (dry mixer)", in_child(pan_e2e));
}
/* ------------------------------------------------------------------ 4 --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

/* instructions per sample: parts[k] = {engine, preset, notes}; on: every slot of every sounding part active
 * (LFO -> CUT, VEL -> AMP, MODW -> VIB, LFO -> E3: per voice and per block) */
static double cost(const uint8_t parts[NPART][3], int on)
{
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    uint32_t p, i, rep;
    double best = 1e30;
    fresh(HOST_SYNTH, 0);
    for (p = 0; p < NPART; p++) {
        track_t *t = &trk[p];
        host_preset(t, parts[p][0], parts[p][1]);
        t->p[P_VOICE] = V_POLY;
        t->p[P_SUS] = 127;
        t->p[P_AMODE] = 0;
        t->mw = 90;
        if (on && parts[p][2]) {
            slot(t, 0, MS_LFO, MD_CUT, 30);
            slot(t, 1, MS_VEL, MD_AMP, 40);
            slot(t, 2, MS_MODW, MD_VIB, 8);
            slot(t, 3, MS_LFO, MD_E1 + 2, 20);
        }
        for (i = 0; i < parts[p][2]; i++)
            trk_note_on(t, NOTES[i] + 12u * p, 100);
    }
    blocks(FS / 2u / CTL);
    for (rep = 0; rep < 3u; rep++) {
        uint64_t i0 = instr_now();
        double ipc;
        blocks(FS / CTL);
        ipc = (double)(instr_now() - i0) / FS;
        best = ipc < best ? ipc : best;
    }
    return best;
}

static void test_cost(void)
{
    uint32_t e_phys = ENGI_PROPHET, pp = 1;          /* (the heaviest synth: the Prophet; PHYS retired) */
    uint8_t phys[NPART][3] = {{(uint8_t)e_phys, (uint8_t)pp, 8}}, idle[NPART][3] = {{0}};
    uint8_t mix[NPART][3] = {{ENGI_FM6, 0, 8}, {ENGI_CZ, 0, 8}, {(uint8_t)e_phys, (uint8_t)pp, 4}, {ENGI_SID, 0, 4}};
    double c_idle, c0, c1, m0, m1;
    if (!instr_now()) {
        printf("mod: cost: no instruction counter on this host (proc_pid_rusage), skipped\n");
        return;
    }
    c_idle = cost(idle, 0);
    c0 = cost(phys, 0);
    c1 = cost(phys, 1);
    m0 = cost(mix, 0);
    m1 = cost(mix, 1);
    printf("mod: cost: PROPHET %s (plays 4 voices): %.0f -> %.0f instructions / sample with 4 slots (+%.0f, +%.1f %% of "
           "the part over the idle mix %.0f)\n", eng_preset(ENGINES[e_phys], pp)->name, c0, c1, c1 - c0,
           (c1 - c0) * 100 / (c0 - c_idle), c_idle);
    printf("mod: cost: 4 parts (FM6 8 + CZ-1 8 + PROPHET 4 + SID 4 asked, the budget plays 8), every slot of "
           "every part active: %.0f -> %.0f (+%.1f %%)\n", m0, m1, (m1 - m0) * 100 / m0);
    check("cost: PROPHET <=5%; four-track matrix overhead <=256 instructions/sample",
          (c1 - c0) * 100 / (c0 - c_idle) <= MOD_COST_MAX && m1 - m0 <= MOD_MIX_IPS_MAX);
}

/* ------------------------------------------------------------------ 5 --- */
static FILE *demo_open(const char *dir, const char *name, uint32_t frames)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    f = fopen(path, "wb");
    if (f)
        wav_hdr(f, frames);
    return f;
}
static void demo_blocks(FILE *f, uint32_t n)
{
    uint32_t i;
    while (n--) {
        blocks(1);
        for (i = 0; i < CTL; i++)
            wav_put(f, out_buf[2u * i], out_buf[2u * i + 1u]);
    }
}

static int demos(const char *dir)
{
    uint32_t e, k, n = 0;
    FILE *f;
    track_t *t = &trk[0];
    /* LFO -> CUT: the host synth (CZ-1), a held chord, a slow sine LFO opens and closes the filter */
    fresh(HOST_SYNTH, 0);
    t->p[P_LRATE] = 30;
    slot(t, 0, MS_LFO, MD_CUT, 48);
    if ((f = demo_open(dir, "lfo_cut", 6u * FS / CTL * CTL))) {
        trk_note_on(t, 48, 100);
        trk_note_on(t, 55, 100);
        trk_note_on(t, 60, 100);
        demo_blocks(f, 5u * FS / CTL);
        trk_note_off(t, 48);
        trk_note_off(t, 55);
        trk_note_off(t, 60);
        demo_blocks(f, FS / CTL);
        fclose(f);
        n++;
    }
    /* VEL -> AMP: FM6's TINE EP, the same note at velocities 10 .. 127, twice as much dynamics */
    fresh(ENGI_FM6, 0);
    slot(t, 0, MS_VEL, MD_AMP, 63);
    slot(t, 1, MS_VEL, MD_CUT, 30);
    if ((f = demo_open(dir, "vel_amp", 16u * (FS / 4u / CTL) * CTL + FS / CTL * CTL))) {
        for (k = 0; k < 16u; k++) {
            trk_note_on(t, 60u + (k & 3u) * 4u, 10u + k * 117u / 15u);
            demo_blocks(f, FS / 4u / CTL);
            trk_note_off(t, 60u + (k & 3u) * 4u);
        }
        demo_blocks(f, FS / CTL);
        fclose(f);
        n++;
    }
    /* MODW -> CUT: SID LEAD 6581, the mod wheel (CC1) opens its filter over 4 s */
    e = eng_by_name("SID");
    fresh(e, preset_by_name(e, "LEAD 6581"));
    t->p[P_LRATE] = 70;
    t->p[P_LD_PIT] = 0;
    t->p[P_LFADE] = 0;
    slot(t, 0, MS_MODW, MD_CUT, 12);
    if ((f = demo_open(dir, "modw_vib", 5u * FS / CTL * CTL))) {
        trk_note_on(t, 69, 100);
        for (k = 0; k < 4u * FS / CTL; k++) {
            if (k % 32u == 0u)
                pkt(0xB0, 1, k * 127u / (4u * FS / CTL));
            demo_blocks(f, 1);
        }
        trk_note_off(t, 69);
        demo_blocks(f, FS / CTL);
        fclose(f);
        n++;
    }
    printf("mod: %u demos in %s (lfo_cut, vel_amp, modw_vib)\n", n, dir);
    return n == 3u;
}

int main(int argc, char **argv)
{
    test_off();
    test_math();
    test_midi();
    test_cost();
    if (argc > 1)
        check("demos written", demos(argv[1]));
    printf("%s\n", bad ? "MOD MATRIX TEST FAILED" : "mod matrix test passed");
    return bad != 0;
}
