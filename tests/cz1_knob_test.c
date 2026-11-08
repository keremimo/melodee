/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* CZ-1's knobs (its schema's pages, the module's cz_ui.c), through cz_ed_put as the device writes them, rendered:
 *   ALL WORK   on a tone that uses every part of the CZ (1+2', both lines' own values, detune, vibrato,
 *              eight-step envelopes), each panel value at its minimum and maximum changes the sound; the one
 *              exception is the level of END's own step, which the CZ holds at 0
 *   DIM = DEAD on INIT TONE, on factory tones and on the full tone made LINE2 / no vibrato / END at 4, every
 *              value cz_ed_active draws dim (no effect on the tone as it is) renders bit for bit the same at
 *              its minimum and maximum
 * Notes 36 / 55 / 72 / 84 at velocities 30..127, held 1 s, released 1 s: the part's own voices (track_render),
 * before its level and the master, so nothing else carries from one render to the next.
 * Built and run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

static int fails;
static void check(const char *s, int ok) { printf("cz1 knobs: %-66s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }

#define RENDER_N (2u * FS)
static int32_t ref[RENDER_N], out[RENDER_N];

static void reset_all(track_t *t)
{
    for (uint32_t k = 0; k < NVOICE; k++)
        memset(&t->v[k], 0, sizeof t->v[k]);
    t->rr = 0;                                           /* the same voices again: a voice's noise seed is its slot */
    vage = 0;
    eng_state_clear(0);
}

static void render(track_t *t, int32_t *dst)
{
    static const uint8_t N[4] = {36, 55, 72, 84}, V[4] = {30, 70, 100, 127};
    int32_t o[CTL];
    uint32_t n = 0, k, i, held = 1;
    reset_all(t);
    for (k = 0; k < 4u; k++)
        input_on(t, N[k], V[k]);
    while (n < RENDER_N) {
        if (held && n >= RENDER_N / 2u)
            for (held = 0, k = 0; k < 4u; k++)
                input_off(t, N[k]);
        memset(o, 0, sizeof o);
        mod_begin(t);
        track_render(t, o, CTL);
        if (mod.on)
            mod_end(t);
        for (i = 0; i < CTL && n < RENDER_N; i++)
            dst[n++] = o[i];
    }
}

/* the change against ref, dB of its energy (-999: bit for bit the same) */
static double change_db(void)
{
    double d = 0, e = 1;
    for (uint32_t i = 0; i < RENDER_N; i++) {
        double x = (double)out[i] - ref[i];
        d += x * x;
        e += (double)ref[i] * ref[i];
    }
    return d ? 10 * log10(d / e) : -999;
}

/* id at its minimum and maximum (those that change the tone's bytes): the larger change; *edits how many did */
static double knob(track_t *t, const uint8_t *base, uint32_t id, int *edits)
{
    uint8_t raw[CZ_BYTES];
    double best = -999;
    *edits = 0;
    for (int side = 0; side < 2; side++) {
        memcpy(cz_patch_of(0)->raw, base, CZ_BYTES);
        if (!cz_ed_put(cz_patch_of(0)->raw, id, (uint32_t)(side ? sch_pd(ENGI_CZ, id)->max : sch_pd(ENGI_CZ, id)->min), raw))
            continue;
        ++*edits;
        memcpy(cz_patch_of(0)->raw, raw, CZ_BYTES);
        render(t, out);
        double d = change_db();
        best = d > best ? d : best;
    }
    memcpy(cz_patch_of(0)->raw, base, CZ_BYTES);
    return best;
}

static void rich_tone(uint8_t *p)
{
    static const uint8_t R[8] = {85, 80, 75, 70, 75, 80, 85, 70};   /* (every step reached within the 1 s held) */
    static const uint8_t L[3][8] = {{20, 40, 10, 30, 15, 25, 35, 0}, {90, 40, 70, 30, 60, 20, 50, 0},
                                    {99, 70, 85, 60, 75, 50, 65, 0}};
    p[LCZ_LINE] = 2; p[LCZ_MOD] = 0; p[LCZ_OCT] = 1; p[LCZ_SIGN] = 0; p[LCZ_DOCT] = 0; p[LCZ_NOTE] = 3;
    p[LCZ_FINE] = 20; p[LCZ_VWAVE] = 0; p[LCZ_VRATE] = 50; p[LCZ_VDEP] = 40; p[LCZ_VDELAY] = 10;
    for (uint32_t l = 0; l < 2u; l++) {
        uint32_t b = LCZ_LBASE(l);
        p[b + LCZ_W1] = 1; p[b + LCZ_W2] = 3; p[b + LCZ_KW] = 4; p[b + LCZ_KA] = 4; p[b + LCZ_LEVEL] = 11;
        p[b + LCZ_VP] = 6; p[b + LCZ_VW] = 6; p[b + LCZ_VA] = 6;
        p[LCZ_WIN(l)] = 2;
        for (uint32_t e = 0; e < 3u; e++) {
            uint8_t *ep = p + LCZ_EBASE(l, e);
            for (uint32_t j = 0; j < 8u; j++) {
                ep[j] = R[j];
                ep[8u + j] = L[e][j];
            }
            ep[16] = 6;                                  /* SUS at step 7, END at 8: every step used */
            ep[17] = 7;
        }
    }
}

static int end_level(uint32_t id, const uint8_t *p)    /* the level of its envelope's END step (always 0) */
{
    uint32_t k = id - LCZ_EBASE(0, 0), e = LCZ_EBASE(0, 0) + k / 18u * 18u;
    return id >= LCZ_EBASE(0, 0) && id < LCZ_OLD_NP && k % 18u >= 8u && k % 18u < 16u && id - e - 8u == p[e + 17u];
}

int main(void)
{
    track_t *t = &trk[0];
    uint8_t p[LCZ_PACKED], base[CZ_BYTES];
    static const uint8_t PRE[] = {0, 1, 13, 25, 41, 47, 50, 62};
    uint32_t id, k, knobs = 0, live = 0, dead = 0, dim = 0, dim_dead = 0;
    int edits;
    char what[80];

    host_tracks_init();
    host_preset(t, ENGI_CZ, 0);
    for (k = 0; k < 4u; k++)
        t->p[P_RESERVED33 + k] = 0;
    cz_ed_decode(p, cz_patch_of(0)->raw);
    rich_tone(p);
    lcz_sx_encode(p, base, 1);
    memcpy(cz_patch_of(0)->raw, base, CZ_BYTES);
    render(t, ref);
    render(t, out);
    check("renders are deterministic", change_db() == -999);
    for (id = 0; id < LCZ_NP; id++) {
        if (!sch_pd(ENGI_CZ, id) || end_level(id, p))
            continue;
        double d = knob(t, base, id, &edits);
        knobs++;
        live += edits && d > -60;
        if (!edits || d <= -60) {
            printf("cz1 knobs:   no change: id %u %s (%d edits, %.1f dB)\n", id, sch_pd(ENGI_CZ, id)->label, edits, d);
            dead++;
        }
        dim += !cz_ed_active(base, id);
    }
    snprintf(what, sizeof what, "every knob changes the sound of a full tone (%u of %u)", live, knobs);
    check(what, !dead && live == knobs && knobs == 131u);   /* 137 values, 6 of them END's own level */
    check("none of them drawn dim on that tone", !dim);

    for (k = 0; k <= sizeof PRE; k++) {
        uint32_t n = 0, bad = 0;
        if (k < sizeof PRE) {
            host_preset(t, ENGI_CZ, PRE[k]);
            for (uint32_t i = 0; i < 4u; i++)
                t->p[P_RESERVED33 + i] = 0;
            memcpy(base, cz_patch_of(0)->raw, CZ_BYTES);
        } else {                                         /* the full tone as LINE2, no vibrato DEPTH, END at 4 */
            rich_tone(p);
            p[LCZ_LINE] = 1;
            p[LCZ_VDEP] = 0;
            for (uint32_t e = 0; e < 6u; e++)
                p[LCZ_EBASE(e / 3u, e % 3u) + 16u] = 1, p[LCZ_EBASE(e / 3u, e % 3u) + 17u] = 3;
            memcpy(p + LCZ_NP, "  LINE2 SHORT   ", 16);
            lcz_sx_encode(p, base, 1);
            memcpy(cz_patch_of(0)->raw, base, CZ_BYTES);
        }
        render(t, ref);
        for (id = 0; id < LCZ_NP; id++) {
            if (!sch_pd(ENGI_CZ, id) || cz_ed_active(base, id))
                continue;
            n++;
            double d = knob(t, base, id, &edits);
            if (d != -999) {
                printf("cz1 knobs:   %.16s: id %u %s drawn dim but changes the sound (%.1f dB)\n", base + 128, id,
                       sch_pd(ENGI_CZ, id)->label, d);
                bad++;
            }
        }
        snprintf(what, sizeof what, "%.16s: its %u dim knobs leave the sound bit for bit", base + 128, n);
        check(what, !bad && n);
        dim_dead += n;
    }
    printf("cz1 knob test %s (%u dim knobs checked)\n", fails ? "FAILED" : "passed", dim_dead);
    return fails != 0;
}
