/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 john (@johnskiller)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* HYDRA: a wave-scanning synth after the ASM Hydrasynth, written for Saadet (it grew from Melodee's WT engine), as a
 * Melodee synth module built from the SDK alone (mod_sdk.h, mod_schema.h; ABI 3).
 *
 * A voice: OSC 1 and OSC 2 play one wave (SINGLE) or scan a list of eight (SCAN crossfades neighbouring slots, an
 * EMPTY slot is silence), each through two MUTANTs in series (FM-LIN, STACK, SYNC, PW, SQUEEZE, WARP, HARMONIC, each
 * with its dry/wet); OSC 3 plays one wave. RING (two oscillators multiplied) and NOISE (white, pink, brown) join them
 * in the MIXER, where each source has a level and a share sent to FILTER 2 instead of FILTER 1 (ladders, MS-20, LPG,
 * three-pole, vowel). FILTER 2 is a state variable morphing through band or notch, after FILTER 1 (SERIES) or beside
 * it (PARALLEL). ENV 1 (delay, attack, hold, decay, sustain, release) moves both filters. The part's ADSR is the
 * amplitude; its LFO, ENV DEST and matrix modulate as for any synth (SHP moves the scans of OSC 1 and 2).
 *
 * Its eight macros, after the Hydrasynth's: the patch names them and routes each to destinations of its own with
 * depths (hy_macros); E1..E8 are their values, -64..63 (0: the sound as stored), which the knobs, CC 74/71 and the
 * matrix move. The part's memory holds the voices and a copy of the waves the patch uses, sized by the patch (a SCAN
 * oscillator's eight, else one); the block hook refreshes them when the patch or a macro changes. A voice is costed
 * with every macro wherever it costs most (hy_worst), so no turn of a macro can make the block late.
 *
 * Saadet's engine (firmware/src/eng_hydra.c, Saadet d4ae975) on the module's views: the same arithmetic, the same
 * patch (hydra_patch.h), waves (hydra_waves.h), factory sounds (hydra_presets.h) and native message. What differs:
 *  - no WAVE LAB and no user wave bank (Saadet's hydra_bank.c / hydra_lab.c: flash objects of its own): a slot or WAVE
 *    holding a user wave's id (57..) plays silence, as one past Saadet's bank does;
 *  - no voice beyond the cap of eight (Saadet's spare), and budget units are whole (Melodee's VBUDGET 18), not
 *    quarters: a light voice takes 2 units where Saadet's took 5 of 72 steps;
 *  - a part the arena has no room for at its patch's size is silent until there is room (Saadet kept the smaller state
 *    and played each scan's first wave meanwhile);
 *  - a heavier sound loaded over ringing tails ends the surplus as a voice given up for another part does (the views'
 *    stage 4, Melodee's voice_kill), as Saadet's voice_fade did;
 *  - a received native message is offered for a user slot and plays on the selected part only when that part already
 *    plays HYDRA (Saadet also switched the part to HYDRA: the views cannot);
 *  - CPU1: a voice's block goes into the firmware's queue (host batch_push_cost) in the Prophet's cost class, Saadet's
 *    AP_HYDRA cost; the descriptor (148 bytes, the queue's most) names the part, not its memory;
 *  - Saadet's editor commands (editor_hydra.c) are not here: the schema (hydra_schema.h) gives every synth editor the
 *    pages, and its macro pages edit the routing as Saadet's LFO > MACRO pages did.
 *     python3 sdk/mod_build.py hydra.c -o hydra.mmod */
#include "mod_sdk.h"
#include "mod_schema.h"
#include "hydra_math.h"
#include "hydra_waves.h"
#include "hydra_patch.h"
#include "hydra_tables.h"
#if defined(MOD_HOSTED) || defined(MOD_IN_CORE)
#include <string.h>                                    /* (libc: the host's builds, WebAssembly) */
#else
void *memcpy(void *d, const void *s, __SIZE_TYPE__ n);   /* (mod_rt.c: the module carries them) */
void *memset(void *d, int c, __SIZE_TYPE__ n);
int memcmp(const void *a, const void *b, __SIZE_TYPE__ n);
#endif
_Static_assert(HY_NWAVE == HY_WAVE_COUNT, "HYDRA's waves: hydra_patch.h");
_Static_assert(HYW_SINE == 0 && HYW_TRI == 1 && HYW_SAW == 2 && HYW_SQR == 3 && HYW_SOFTSW == 6 && HYW_SYNC_2 == 32 &&
               HYW_SYNC_3 == 33 && HYW_SYNC_5 == 34, "hydra_patch.h HY_INIT names these waves by number");
_Static_assert(MOD3_RATE == 44100u && MOD3_CTL == 32u, "HYDRA's rates assume 44.1 kHz and 32-sample blocks");
#define HY_FS 44100.0f
#define HY_CTL 32.0f

/* ---------------------------------------------------------------------------------------------- the patch --- */
#define HY_POLY 8u
#define HY_SLOTS HY_POLY                               /* (a voice's state by its index: the cap's voices) */
#define HY_ROWS 17u                                    /* the wave copies at most: OSC 1's eight, OSC 2's, OSC 3 */
/* The budget is VBUDGET's 18 units; a unit is HY_BLOCK_US / 18, so a full budget renders within HY_BLOCK_US of the
 * block above the 330 us an idle block takes: 2600 of 2902. */
#define HY_BLOCK_US 2270u
#define HY_STEPS 18u
#define HY_OS_NOTE (65 * 16)                           /* from F4 up, a voice's oscillators run at twice the rate */
#define HY_COUPLE (2.0f * 3.14159265f * 5.0f / HY_FS)  /* a coupling capacitor, ~5 Hz: ring, FM and the filters' drive can
                                                        * lean the wave off centre */
#define HY_AP 11u                                      /* the CPU1 queue's cost class: the Prophet's (AP_PROPHET) */
#define HY_COST 2600u                                  /* a voice's estimated cost in it (Saadet's AP_HYDRA) */

static uint32_t hy_eng = 23;                           /* the engine number this device gave the module (bind) */
static const float *hy_tanh;                           /* the host's P5_TANH (1025 floats) */
static const uint8_t *hy_raw(const mod_part_v3 *t) { return mh->patch(t); }   /* (zeros while switching) */

/* ------------------------------------------------------------------------------------- the part's state --- */
typedef struct {
    uint8_t mode, src, sh;
    float wet, fm, fb, win, hfr, det;
    uint32_t ratio, knee, k1, k2, h;                   /* SYNC Q16 / WARP sections; PW knee and slopes (Q16) */
    int32_t sq;                                        /* SQUEEZE: the stretch (Q16) */
} hy_mut_t;
typedef struct {                                       /* the patch, made ready (per part, at a change) */
    hy_mut_t mut[4];
    float lvl[HYS_N], bal[HYS_N], amp;
    float f1cut, f1res, f1drive, f1env, f1key, f1vel, f2cut, f2res, f2morph, f2env, f2key;
    float e1atk[1], e1sus, e1dec, e1rel, e1sc[2], e1mu;
    int32_t scan[2], semi16[3], cent[3], key[3];
    uint8_t m1[HM_N];                                  /* MUTANT 1 as the macros leave it (ENV 1 moves it a voice) */
    uint16_t e1dly, e1hld;                             /* control ticks */
    uint8_t scanning[2], on[3], src_used[HYS_N], stack[2], ring_a, ring_b, noise, route, f1type, f1post, f2notch;
    uint8_t row0[2], row3;                             /* OSC 1's and OSC 2's first wave copy, OSC 3's */
    uint8_t mix[HYS_N], nmix, bus2, noise_on;          /* the sources heard (their order), FILTER 2's bus fed alone */
    uint8_t f2open;                                    /* FILTER 2 wide open, unmoved: passed by (no filter) */
} hy_base_t;
typedef struct {
    uint32_t sp[2][4];                                 /* STACK: the four detuned copies of OSC 1 and 2 */
    uint32_t ph[3];                                    /* OSC 1, 2, 3 */
    float z[6], f2[2];                                 /* FILTER 1 (ladder stages, SVF, VOWEL's three), FILTER 2 */
    float dodd[2][16], deven[2][8];                    /* the two buses' half-band decimators (2x voices) */
    float out[HYS_N];                                  /* the sources' last samples (FM, ring, feedback) */
    float pink[3], brown, e1, dc;                      /* (dc: this voice's capacitor, so a voice that ends takes its own) */
    int32_t noise;
    uint16_t e1wait;
    uint8_t e1stage, os, dp;                           /* os: oversampled; dp: the decimators' ring position */
    uint8_t thin;                                      /* a tail hy_tails_fit ended: its last block without mutants */
    hy_mut_t mu1;                                      /* MUTANT 1 moved by ENV 1 (CPU0 writes it, then the kernel) */
} hy_voice_t;
typedef struct {
    hy_voice_t voice[HY_SLOTS];
    uint32_t key;                                      /* the patch's hash when the part was made ready */
    uint8_t tag[HY_ROWS], key_e[16], valid;
    hy_base_t b;
    int16_t wave[][HY_PTS];                            /* the wave copies the patch reads (hy_rows) */
} hy_part_t;
/* the wave copies a patch needs: a scanning oscillator its eight slots, a single one its wave, OSC 3 one. The part's
 * memory is sized by them: a SINGLE / SINGLE patch takes 3 rows, not 17. Switching an oscillator between SINGLE and
 * SCAN gives the part another size (the host's: zeroed, so the part is made ready again) */
static uint32_t hy_rows(const uint8_t *r) { return (r[HY_O1 + HO_MODE] ? 8u : 1u) + (r[HY_O2 + HO_MODE] ? 8u : 1u) + 1u; }
#define HY_BYTES_OF(rows) ((uint32_t)(__builtin_offsetof(hy_part_t, wave) + (rows) * HY_PTS * sizeof(int16_t)))
static uint32_t hy_bytes(const uint8_t *r) { return (HY_BYTES_OF(hy_rows(r)) + 3u) & ~3u; }
static uint32_t hy_state(const mod_part_v3 *t) { return t ? hy_bytes(hy_raw(t)) : (HY_BYTES_OF(HY_ROWS) + 3u) & ~3u; }
/* the parts' memory as their last blocks held it (hy_os_room reads the other parts' voices) */
static struct { hy_part_t *P; const mod_part_v3 *t; uint32_t bytes; } hy_reg[MOD3_NPART];
static hy_part_t *hy_part(const mod_part_v3 *t)
{
    uint32_t bytes = hy_bytes(hy_raw(t));
    hy_part_t *P = md_part(t, bytes);
    if (P && t->index < MOD3_NPART) {
        hy_reg[t->index].P = P;
        hy_reg[t->index].t = t;
        hy_reg[t->index].bytes = bytes;
    }
    return P;
}
static inline __attribute__((always_inline)) hy_voice_t *hy_voice(hy_part_t *P, const mod_voice_v3 *v)
{
    return &P->voice[v->index % HY_SLOTS];
}

/* the waves the patch reads, copied into the part (a slot's wave id in tag, 0 none): a factory wave from the image; a
 * user wave's id (Saadet's bank, which a module has none of) or none: silence */
static void hy_want(hy_part_t *P, uint32_t row, uint32_t id)
{
    if (id >= HY_USER0)
        id = 0;
    if (P->tag[row] == id)
        return;
    if (id)
        memcpy(P->wave[row], HY_WAVES[id - 1u], sizeof P->wave[row]);
    else
        memset(P->wave[row], 0, sizeof P->wave[row]);
    P->tag[row] = (uint8_t)id;
}
static void hy_waves(hy_part_t *P, const uint8_t *r)
{
    uint32_t o, s, row = 0;
    for (o = 0; o < 2u; o++) {
        const uint8_t *q = r + (o ? HY_O2 : HY_O1);
        P->b.row0[o] = (uint8_t)row;
        if (P->b.scanning[o]) {
            for (s = 0; s < 8u; s++)
                hy_want(P, row++, q[HO_L0 + s]);
        } else {
            hy_want(P, row++, q[HO_WAVE] + 1u);
        }
    }
    P->b.row3 = (uint8_t)row;
    hy_want(P, row, r[HY_O3_WAVE] + 1u);
}

static inline __attribute__((always_inline)) float hy_clampf(float x, float lo, float hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}
static inline float hy_unit(int32_t raw) { return (float)md_clamp(raw, 0, 127) * (1.0f / 127.0f); }
/* tanh, interpolated in the host's table (Saadet's p5_fast_softclip) */
static inline __attribute__((always_inline)) float hy_softclip(float x)
{
    float a = fm_fabsf(x) * 128.0f;
    if (a >= 1024.0f)
        return x < 0.0f ? -1.0f : 1.0f;
    uint32_t i = (uint32_t)a;
    float y = hy_tanh[i] + (hy_tanh[i + 1u] - hy_tanh[i]) * (a - (float)i);
    return x < 0.0f ? -y : y;
}
/* a PW knee d (0..1 of the cycle) as the phase map's knee and its two slopes, Q16 */
static void hy_knee(hy_mut_t *u, float d)
{
    d = fm_clampf(d, 0.03f, 0.97f);
    u->knee = (uint32_t)(d * 4294967295.0f);
    u->k1 = (uint32_t)(0.5f / d * 65536.0f);
    u->k2 = (uint32_t)(0.5f / (1.0f - d) * 65536.0f);
}
static void hy_mutant(hy_mut_t *u, const uint8_t *q, int32_t macro)
{
    float depth = hy_unit(q[HM_DEPTH] + macro), aux = hy_unit(q[HM_AUX]), ratio = hy_unit(q[HM_RATIO]);
    memset(u, 0, sizeof *u);
    u->mode = q[HM_MODE] < MU_N ? q[HM_MODE] : MU_OFF;
    u->src = q[HM_SRC] < HYS_N ? q[HM_SRC] : HYS_OSC3;
    u->wet = hy_unit(q[HM_WET]);
    switch (u->mode) {
    case MU_FM:                                        /* phase offsets in cycles: up to two, feedback a half */
        u->fm = depth * depth * 2.0f;
        u->fb = aux * aux * 0.5f;
        break;
    case MU_STACK:                                     /* four copies detuned up to +-6 % (about a semitone) */
        u->det = depth * depth * 0.06f;
        break;
    case MU_SYNC:                                      /* 1 .. 16 slave cycles a cycle, a window against clicks */
        u->ratio = (uint32_t)((1.0f + ratio * 7.0f + depth * 8.0f) * 65536.0f);
        u->win = aux;
        break;
    case MU_PW:
        hy_knee(u, 0.5f - depth * 0.47f);
        break;
    case MU_SQZ:
        u->sq = (int32_t)((1.0f + depth * 7.0f) * 65536.0f);
        break;
    case MU_WARP:                                      /* PW within 1, 2, 4 or 8 sections */
        u->sh = (uint8_t)(md_clamp(q[HM_RATIO], 0, 127) >> 5);
        hy_knee(u, 0.5f - depth * 0.45f);
        break;
    case MU_HARM: {                                    /* harmonic 1 .. up to 16, swept */
        float top = 2.0f + ratio * 14.0f, h = 1.0f + depth * (top - 1.0f);
        u->h = (uint32_t)h;
        u->hfr = h - (float)u->h;
        break;
    }
    }
}
static float hy_ftime(uint32_t v) { return (float)TIME_MS_X10[v & 127u] * HY_FS / (10000.0f * HY_CTL); }
/* the patch as its macros leave it: an assignment moves its destination by depth x the macro's value (e, -64..63) of the
 * destination's range at a full turn; several on one destination add up, then the sum is clamped to its range */
static void hy_macros(const uint8_t *r, const int16_t *e, uint8_t *eff)
{
    int32_t add[HY_NTGT + 1u];
    uint32_t k, g;
    memcpy(eff, r, HY_BYTES);
    memset(add, 0, sizeof add);
    for (k = 0; k < HY_MSLOTS; k++) {
        uint32_t a = r[HY_MA + 2u * k];
        int32_t d = (int32_t)r[HY_MA + 2u * k + 1u] - HY_DEPTH0;
        add[HY_SLOT_TGT(a)] += e[HY_SLOT_MACRO(a)] * d * (int32_t)hy_span(HY_SLOT_TGT(a));
    }
    for (g = 1; g <= HY_NTGT; g++)
        if (add[g]) {
            int32_t x = (add[g] + (add[g] < 0 ? -64 * 127 / 2 : 64 * 127 / 2)) / (64 * 127);
            eff[HY_TGT[g]] = (uint8_t)md_clamp((int32_t)r[HY_TGT[g]] + x, 0, (int32_t)hy_span(g));
        }
}
/* the patch where its macros could make a voice cost most: a level, FILTER 2's pass and resonance as high as they reach,
 * its cutoff as low, its envelope amount off centre if it can be (what hy_cost reads) */
static void hy_worst(const uint8_t *r, uint8_t *w)
{
    int32_t reach[HY_NTGT + 1u];
    uint32_t k, g;
    memcpy(w, r, HY_BYTES);
    memset(reach, 0, sizeof reach);
    for (k = 0; k < HY_MSLOTS; k++) {                  /* (a value -64 at a whole depth: the whole range) */
        int32_t d = (int32_t)r[HY_MA + 2u * k + 1u] - HY_DEPTH0;
        g = HY_SLOT_TGT(r[HY_MA + 2u * k]);
        reach[g] += ((d < 0 ? -d : d) * (int32_t)hy_span(g) + 126) / 127;
    }
    for (g = 1; g <= HY_NTGT; g++)
        if (reach[g]) {
            uint32_t b = HY_TGT[g];
            int32_t lo = md_clamp((int32_t)r[b] - reach[g], 0, (int32_t)hy_span(g));
            int32_t hi = md_clamp((int32_t)r[b] + reach[g], 0, (int32_t)hy_span(g));
            w[b] = (uint8_t)(b == HY_F2_CUT ? lo : b == HY_F2_ENV && hi == HY_ENV0 ? lo : hi);
        }
}
static void hy_base(hy_part_t *P, const mod_part_v3 *t, const uint8_t *raw)
{
    hy_base_t *b = &P->b;
    uint8_t r[HY_BYTES];
    uint32_t o, k;
    hy_macros(raw, md_e(t), r);
    memset(b, 0, sizeof *b);
    for (o = 0; o < 2u; o++) {
        const uint8_t *q = r + (o ? HY_O2 : HY_O1);
        b->scanning[o] = q[HO_MODE] != 0;
        b->scan[o] = md_clamp(q[HO_SCAN], 0, 127) << 8;
        b->semi16[o] = (q[HO_SEMI] - HY_SEMI0) * 16;
        b->cent[o] = q[HO_CENT] - HY_CENT0;
        b->key[o] = q[HO_KEY];
    }
    b->semi16[2] = (r[HY_O3_SEMI] - HY_SEMI0) * 16;
    b->cent[2] = r[HY_O3_CENT] - HY_CENT0;
    b->key[2] = r[HY_O3_KEY];
    memcpy(b->m1, r + HY_M1, HM_N);
    for (k = 0; k < 4u; k++) {
        hy_mutant(&b->mut[k], r + HY_M1 + k * HM_N, 0);
        if (b->mut[k].mode == MU_STACK)
            b->stack[k >> 1] = (uint8_t)(k + 1u);
    }
    for (k = 0; k < HYS_N; k++) {
        float l = hy_unit(r[HY_LVL + k]);
        b->lvl[k] = l * l * 0.8f;
        b->bal[k] = hy_unit(r[HY_BAL + k]);
    }
    b->ring_a = r[HY_RING_A] < 3u ? r[HY_RING_A] : 0;
    b->ring_b = r[HY_RING_B] < 3u ? r[HY_RING_B] : 1;
    b->noise = r[HY_NOISE] < 3u ? r[HY_NOISE] : 0;
    b->route = r[HY_ROUTE] != 0;
    for (k = 0; k < 4u; k++)                           /* the FM sources the mutants read */
        if (b->mut[k].mode == MU_FM)
            b->src_used[b->mut[k].src] = 1;
    if (b->lvl[HYS_RING] > 0.0f)
        b->src_used[HYS_RING] = 1;
    if (b->src_used[HYS_RING])
        b->src_used[b->ring_a] = b->src_used[b->ring_b] = 1;
    for (o = 0; o < 3u; o++)
        b->on[o] = b->lvl[o] > 0.0f || b->src_used[o];
    for (k = 0; k < HYS_N; k++)
        if (b->lvl[k] > 0.0f) {
            b->mix[b->nmix++] = (uint8_t)k;
            b->bus2 |= b->bal[k] > 0.0f;
        }
    b->noise_on = b->lvl[HYS_NOISE] > 0.0f || b->src_used[HYS_NOISE];
    b->f1type = r[HY_F1_TYPE] < F1_N ? r[HY_F1_TYPE] : F1_LAD24;
    b->f1cut = (float)md_clamp(r[HY_F1_CUT], 0, 127);
    b->f1res = hy_unit(r[HY_F1_RES]);
    b->f1drive = 1.0f + hy_unit(r[HY_F1_DRIVE]) * 7.0f;
    b->f1post = r[HY_F1_DPOS] != 0;
    b->f1env = (float)(md_clamp(r[HY_F1_ENV], 0, 2 * HY_ENV0) - HY_ENV0) * (96.0f / 63.0f);   /* up to ~7 octaves */
    b->f1key = (float)md_clamp(r[HY_F1_KEY], 0, 200) * 0.01f * (127.0f / 9.0589f / 12.0f);   /* index a semitone */
    b->f1vel = hy_unit(r[HY_F1_VEL]) * 48.0f;
    b->f2notch = r[HY_F2_TYPE] != 0;
    b->f2morph = hy_unit(r[HY_F2_MORPH]);
    b->f2cut = (float)md_clamp(r[HY_F2_CUT], 0, 127);
    b->f2res = hy_unit(r[HY_F2_RES]);
    b->f2env = (float)(md_clamp(r[HY_F2_ENV], 0, 2 * HY_ENV0) - HY_ENV0) * (96.0f / 63.0f);
    b->f2key = (float)md_clamp(r[HY_F2_KEY], 0, 200) * 0.01f * (127.0f / 9.0589f / 12.0f);
    b->f2open = !b->f2notch && !r[HY_F2_MORPH] && r[HY_F2_CUT] >= 127 && !r[HY_F2_RES] &&
                r[HY_F2_ENV] == HY_ENV0 && !r[HY_F2_KEY];
    b->amp = hy_unit(r[HY_AMP]);
    b->e1sc[0] = (float)(md_clamp(r[HY_E1_SC1], 0, 2 * HY_ENV0) - HY_ENV0) * (127.0f * 256.0f / 63.0f);
    b->e1sc[1] = (float)(md_clamp(r[HY_E1_SC2], 0, 2 * HY_ENV0) - HY_ENV0) * (127.0f * 256.0f / 63.0f);
    b->e1mu = (float)(md_clamp(r[HY_E1_MU1], 0, 2 * HY_ENV0) - HY_ENV0) * (127.0f / 63.0f);
    b->e1dly = (uint16_t)(r[HY_E1_DLY] ? fm_minf(hy_ftime(r[HY_E1_DLY]), 65535.0f) : 0.0f);
    b->e1hld = (uint16_t)(r[HY_E1_HLD] ? fm_minf(hy_ftime(r[HY_E1_HLD]), 65535.0f) : 0.0f);
    b->e1atk[0] = (float)ENV_LIN[r[HY_E1_ATK] & 127u] * (1.0f / 16777216.0f);
    b->e1dec = (float)ENV_EXP[r[HY_E1_DEC] & 127u] * (1.0f / 65536.0f);
    b->e1rel = (float)ENV_EXP[r[HY_E1_REL] & 127u] * (1.0f / 65536.0f);
    b->e1sus = hy_unit(r[HY_E1_SUS]);
}

/* What a voice of this patch costs, in microseconds of the 2902 of a block, as measured on an SMK-37 Elite at 360 MHz with
 * CPU1 (Saadet's tests/hydra_test.c and docs/VALIDATION.md): c1 with its oscillators at the sample rate, os what twice
 * the rate adds. Mutants cost more the more copies they read, STACK five, a scan's crossfade half again */
typedef struct { uint16_t c1, os; } hy_cost_t;
static hy_cost_t hy_cost(const uint8_t *r)
{
    static const uint16_t OSC[3][2][2] = {             /* [other mutants][STACK][c1, os] */
        {{0, 0}, {76, 76}}, {{78, 85}, {261, 255}}, {{115, 130}, {390, 380}},
    };
    uint32_t c1 = 155u, os = 78u, k, o, src = 0;       /* one oscillator, the filter, the block's set-up */
    int ring;
    for (k = 0; k < 4u; k++)                           /* (the sources FM reads sound though the mixer has them at 0) */
        if (r[HY_M1 + k * HM_N + HM_MODE] == MU_FM && r[HY_M1 + k * HM_N + HM_SRC] < HYS_N)
            src |= 1u << r[HY_M1 + k * HM_N + HM_SRC];
    ring = r[HY_LVL + HYS_RING] || (src >> HYS_RING & 1u);
    if (ring) {
        src |= 1u << (r[HY_RING_A] < 3u ? r[HY_RING_A] : 0u) | 1u << (r[HY_RING_B] < 3u ? r[HY_RING_B] : 1u);
        c1 += 19u; os += 19u;
    }
    if (r[HY_LVL + HYS_NOISE] || (src >> HYS_NOISE & 1u)) { c1 += 19u; os += 18u; }
    if (r[HY_LVL + HYS_OSC3] || (src >> HYS_OSC3 & 1u)) { c1 += 12u; os += 14u; }
    if (r[HY_LVL + HYS_OSC2] || (src >> HYS_OSC2 & 1u)) { c1 += 25u; os += 24u; }
    for (o = 0; o < 2u; o++) {
        const uint8_t *m = r + HY_M1 + o * 2u * HM_N, *q = r + (o ? HY_O2 : HY_O1);
        uint32_t a = m[HM_MODE], b = m[HM_N + HM_MODE], x, y;
        if (!r[HY_LVL + o] && !(src >> o & 1u))
            continue;
        k = (a && a != MU_STACK) + (b && b != MU_STACK);
        x = OSC[k][a == MU_STACK || b == MU_STACK][0];
        y = OSC[k][a == MU_STACK || b == MU_STACK][1];
        if ((a == MU_STACK || b == MU_STACK) && (a == MU_FM || b == MU_FM)) {
            /* FM in a STACK reads its source for every copy: two converted patches measured 300 and 288 us a share (HIT
             * C2 ! LA, WCOG) where STACK and PW take 261, and HIT's twice the rate 440 where 371 */
            x += 30u;
            y += 70u;
        }
        if (q[HO_MODE]) {                              /* (a scan between two slots reads both) */
            x = x * 3u / 2u + 25u;
            y = y * 3u / 2u + 25u;
        }
        c1 += x; os += y;
    }
    if (r[HY_F2_MORPH] || r[HY_F2_TYPE] || r[HY_F2_CUT] < 127u || r[HY_F2_RES] || r[HY_F2_ENV] != HY_ENV0 || r[HY_F2_KEY])
        c1 += 17u;
    return (hy_cost_t){(uint16_t)c1, (uint16_t)os};
}
/* what a voice may come to cost, whatever its macros do: the budget's figure. Asked at every note (the budget, twice the
 * rate), so kept for each part's patch by its hash as hy_block keeps the patch made ready */
static struct { uint32_t key, set; hy_cost_t c; } hy_most_kept[MOD3_NPART + 1u];
static uint32_t hy_hash(const uint8_t *r)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < HY_BYTES; i++)                     /* (FNV-1a: a copy of the patch would not fit the part) */
        h = (h ^ r[i]) * 16777619u;
    return h;
}
static hy_cost_t hy_cost_most(const uint8_t *r, uint32_t part)
{
    uint8_t w[HY_BYTES];
    uint32_t h = hy_hash(r), k = part < MOD3_NPART ? part : MOD3_NPART;
    if (hy_most_kept[k].set && hy_most_kept[k].key == h)
        return hy_most_kept[k].c;
    hy_worst(r, w);
    hy_most_kept[k].c = hy_cost(w);
    hy_most_kept[k].key = h;
    hy_most_kept[k].set = 1;
    return hy_most_kept[k].c;
}
/* A part's voices on the two cores: the batch gives CPU1 a part's first two voices and then every other one, so n voices
 * take W(n) = n - (n - 1) / 2 voices' time one after another. The table above is a voice's share at six to eight
 * voices, where CPU1 runs five of eight: a voice on its own takes about 1.6 times its share. Few heavy voices are bound
 * by that, not by the budget: the cap, and a voice that cannot pair takes its whole time from the budget */
#define HY_ALONE(c1) ((c1) * 8u / 5u)
/* Without CPU1 (retired, barred until power-off after a crash, or a firmware without it) nothing pairs: every voice runs
 * on CPU0 at its time alone, and its share of the budget is that time */
static int hy_paired(void) { return mh->worker_online && *mh->worker_online; }
/* n voices of a part one after another, in voices alone: W(n) on the two cores, n on CPU0 alone */
static uint32_t hy_serial(uint32_t n) { return hy_paired() ? n - (n - 1u) / 2u : n; }
static uint32_t hy_cap_of(uint32_t c1)
{
    uint32_t n;
    for (n = HY_POLY; n > 1u && hy_serial(n) * HY_ALONE(c1) > HY_BLOCK_US; n--)
        ;
    return n;
}
static uint32_t hy_units_of(hy_cost_t c)
{
    uint32_t cap = hy_cap_of(c.c1), us = cap < 3u || !hy_paired() ? HY_ALONE(c.c1) : c.c1;
    uint32_t u = (us * HY_STEPS + HY_BLOCK_US - 1u) / HY_BLOCK_US;
    if (cap < HY_POLY && HY_STEPS / u > cap)           /* the cores bind before the budget: the cap's voices take all of it,
                                                        * so a stolen voice's fade is not a voice more beside them */
        u = HY_STEPS / cap;
    return u < HY_STEPS ? u : HY_STEPS;
}
/* the voices of a patch that play at once at the most: its budget, and its cap on the cores */
static uint32_t hy_most(const uint8_t *r, uint32_t part)
{
    hy_cost_t c = hy_cost_most(r, part);
    uint32_t n = HY_STEPS / hy_units_of(c), cap = hy_cap_of(c.c1);
    return n < cap ? n : cap;
}
/* May a new voice of part t run at twice the rate? What is left of the block once the part plays all it can at the sample
 * rate (its budget and cap; MONO and LEGATO one voice and the one fading out) goes to the voices that do, in every part.
 * Counted as the cores run them: the part's W(n) one after another, each at its time alone, and what twice the rate adds
 * at its time alone, since it lands on one core */
static int hy_os_room(const mod_part_v3 *t)
{
    const uint8_t *raw = hy_raw(t);
    hy_cost_t c = hy_cost_most(raw, t->index);
    uint32_t n = hy_most(raw, t->index), used, p, i;
    if ((t->p[MOD3_P_VOICE] == 1 || t->p[MOD3_P_VOICE] == 2) && n > 2u)   /* (MONO, LEGATO) */
        n = 2u;
    used = hy_serial(n) * HY_ALONE(c.c1) + HY_ALONE(c.os);
    for (p = 0; p < MOD3_NPART && used <= HY_BLOCK_US; p++) {
        const mod_part_v3 *o = hy_reg[p].t;
        const uint8_t *r = o && o->engine == hy_eng ? hy_raw(o) : 0;
        hy_part_t *P = r && hy_reg[p].bytes == hy_bytes(r) ? hy_reg[p].P : 0;
        uint32_t each = P ? HY_ALONE(hy_cost_most(r, p).os) : 0u;
        for (i = 0; P && i < MOD3_NVOICE; i++)
            if (mod3_voice(o, i)->active && P->voice[i % HY_SLOTS].os)
                used += each;
    }
    return used <= HY_BLOCK_US;
}
/* A heavier sound loaded over notes that were sounding: the load released them, and more tails than the block can carry
 * would render at the new patch's cost. The surplus ends as a voice given up for another part does (stage 4: the host
 * fades it this block), without its mutants. Held notes are never touched */
static void hy_tails_fit(hy_part_t *P, mod_part_v3 *t, const uint8_t *r)
{
    uint32_t most = hy_most(r, t->index), n = 0, i;
    for (i = 0; i < MOD3_NVOICE; i++) {
        const mod_voice_v3 *v = mod3_voice(t, i);
        n += v->active && v->stage != 4u;
    }
    for (i = 0; i < MOD3_NVOICE && n > most; i++) {
        mod_voice_v3 *v = mod3_voice(t, i);
        if (v->active && !v->gate && v->stage != 4u) {
            v->stage = 4;
            hy_voice(P, v)->thin = 1;
            n--;
        }
    }
}
/* the part before its voices: the patch made ready when it or a macro changed, its waves copied */
static void hy_block(mod_part_v3 *t)
{
    hy_part_t *P = hy_part(t);
    const uint8_t *r = hy_raw(t);
    if (!P)
        return;
    uint32_t h = hy_hash(r);
    if (P->valid && P->key == h && !memcmp(P->key_e, md_e(t), sizeof P->key_e))
        return;
    if (!P->valid || P->key != h)
        hy_tails_fit(P, t, r);
    hy_base(P, t, r);
    hy_waves(P, r);                                    /* (after: it sets the base's row numbers) */
    P->key = h;
    memcpy(P->key_e, md_e(t), sizeof P->key_e);
    P->valid = 1;
}

/* ------------------------------------------------------------------------------------------- the voice --- */
static void hy_note_on(mod_part_v3 *t, mod_voice_v3 *v)
{
    hy_part_t *P = hy_part(t);
    hy_voice_t *s;
    uint32_t j;
    if (!P)
        return;
    if (!P->valid)
        hy_block(t);
    s = hy_voice(P, v);
    if (!*mh->was) {                                   /* a free voice: everything from rest, the oscillators a
                                                        * third of a cycle apart (the detuned copies spread) */
        memset(s, 0, sizeof *s);
        s->ph[0] = (uint32_t)v->index * 0x5A17C151u + 0x9E3779B9u;
        s->ph[1] = s->ph[0] + 0x2AAAAAAAu;
        s->ph[2] = s->ph[0] + 0x55555555u;
        for (j = 0; j < 4u; j++) {
            s->sp[0][j] = s->ph[0] + (j + 1u) * 0x33333333u;
            s->sp[1][j] = s->ph[1] + (j + 1u) * 0x33333333u;
        }
        s->noise = (int32_t)(0x5A1793u + v->age * 17u + (uint32_t)v->index * 101u);
        /* the oscillators twice the rate when they play from F4 up (the waves' 64 harmonics fold back above ~345 Hz),
         * or with sync / harmonic mutants from an octave lower */
        const hy_base_t *b = &P->b;
        int32_t top = -(1 << 20), low = HY_OS_NOTE;
        for (j = 0; j < 3u; j++)
            if (b->on[j]) {
                int32_t p16 = 960 + (v->pitch16 - 960) * b->key[j] / 100 + b->semi16[j];
                if (p16 > top) top = p16;
            }
        for (j = 0; j < 4u; j++)
            if (b->mut[j].mode == MU_SYNC || b->mut[j].mode == MU_HARM)
                low = HY_OS_NOTE - 12 * 16;
        s->os = top >= low && hy_os_room(t);
    }
    s->thin = 0;
    s->e1stage = P->b.e1dly ? 1u : 2u;                 /* ENV 1 from where it is (a retrigger: no jump) */
    s->e1wait = P->b.e1dly;
}
static uint32_t hy_cap(const mod_part_v3 *t) { return hy_cap_of(hy_cost_most(hy_raw(t), t->index).c1); }
static uint32_t hy_units(const mod_part_v3 *t) { return hy_units_of(hy_cost_most(hy_raw(t), t->index)); }

/* ENV 1, once a block: delay, attack (linear), hold, decay to sustain, release (exponential) */
static float hy_env1(const hy_base_t *b, hy_voice_t *s, int gate)
{
    if (!gate && s->e1stage)
        s->e1stage = 5;
    switch (s->e1stage) {
    case 1:
        if (s->e1wait) { s->e1wait--; break; }
        s->e1stage = 2;                                /* fall through */
    case 2:
        s->e1 += b->e1atk[0];
        if (s->e1 >= 1.0f) { s->e1 = 1.0f; s->e1stage = 3; s->e1wait = b->e1hld; }
        break;
    case 3:
        if (s->e1wait) { s->e1wait--; break; }
        s->e1stage = 4;                                /* fall through */
    case 4:
        s->e1 += (b->e1sus - s->e1) * b->e1dec;
        break;
    case 5:
        s->e1 -= s->e1 * b->e1rel;
        if (s->e1 < 1e-4f) { s->e1 = 0.0f; s->e1stage = 0; }
        break;
    }
    return s->e1;
}

/* an oscillator for one block, as the producer (CPU0) prepares it: indices, not pointers, so the batch descriptor
 * stays small */
#define HY_NOMUT 255u
#define HY_MU1 4u                                      /* (m1: the voice's own MUTANT 1, moved by ENV 1) */
typedef struct {
    uint8_t a, b, m1, m2;                              /* the rows read; its phase mutants in order (0..3, HY_MU1) */
    int32_t fr;                                        /* a scan's crossfade (Q15) */
    float g, det, swet;                                /* its level (an EMPTY slot fades it), STACK's detune, copies */
    uint32_t inc;
} hy_osc_t;
typedef struct {                                       /* one voice's block, ready to render */
    hy_osc_t o[2];
    uint32_t inc3;
    float gain1;                                       /* FILTER 1: its input gain, then by its type */
    union {
        struct { float g1, k1, rcp1; } lad;            /* the ladders, FAT, three-pole */
        struct { float a1, a2, a3, kd1, lpg; } svf;    /* MS-20, LPG */
        struct { float a1[3], a2[3], a3[3], k; } vow;  /* VOWEL: three band passes */
    } f1;
    float sa1, sa2, sa3, sk, lp, bp, hp;               /* FILTER 2 */
    float amp0, amp1, scale;
    uint8_t f2mod;                                     /* the part's FLT modulation moves FILTER 2 (no pass-by) */
} hy_blk_t;
/* (dk: the voice's dither row; part: its memory is hy_reg's, as hy_render left it. No pointer: one size on every host,
 * and the queue's copy needs no more than a float's alignment) */
typedef struct { uint16_t dk; uint8_t n, vi, part; hy_blk_t c; } hy_job_t;
_Static_assert(sizeof(hy_job_t) <= MOD3_BATCH_BYTES, "HYDRA's CPU1 descriptor");
typedef struct {                                       /* an oscillator, as the kernel reads it */
    const int16_t *a, *b;
    int32_t fr;
    float g;
    uint32_t inc, sinc[4];                             /* its increment, the STACK copies' */
    const hy_mut_t *m1, *m2;
    float swet, srcp;                                  /* STACK's copies, 1 / what they sum to (a divide once a block) */
} hy_oview_t;

static inline __attribute__((always_inline)) int32_t hy_rdi(const int16_t *a, const int16_t *b, int32_t fr, uint32_t q)
{
    uint32_t i = q >> 24, nx = (i + 1u) & (HY_PTS - 1u);
    int32_t t = (int32_t)((q >> 9) & 0x7fffu);
    int32_t x = a[i] + (((a[nx] - a[i]) * t) >> 15);
    if (fr) {
        int32_t y = b[i] + (((b[nx] - b[i]) * t) >> 15);
        x += ((y - x) * fr) >> 15;
    }
    return x;
}
static inline __attribute__((always_inline)) float hy_rd(const int16_t *a, const int16_t *b, int32_t fr, uint32_t q)
{
    return (float)hy_rdi(a, b, fr, q) * (1.0f / 32768.0f);
}
static inline __attribute__((always_inline)) uint32_t hy_pw(const hy_mut_t *u, uint32_t q)
{
    return q < u->knee ? (uint32_t)(((uint64_t)q * u->k1) >> 16)
                       : 0x80000000u + (uint32_t)(((uint64_t)(q - u->knee) * u->k2) >> 16);
}
/* a mutant's phase (and the gain SYNC's window gives) */
static inline __attribute__((always_inline)) uint32_t hy_mphase(const hy_mut_t *u, uint32_t q, const float *src,
                                                                 float self, float *gain)
{
    switch (u->mode) {
    case MU_FM: {
        float c = src[u->src] * u->fm + self * u->fb;
        c -= fm_floorf(c);
        return q + (uint32_t)(c * 4294967040.0f);
    }
    case MU_SYNC:
        if (u->win > 0.0f) {                           /* (a parabola for sin(pi x): no table to read) */
            float x = (float)q * (1.0f / 4294967296.0f);
            *gain = 1.0f - u->win * (1.0f - 4.0f * x * (1.0f - x));
        }
        return (uint32_t)(((uint64_t)q * u->ratio) >> 16);
    case MU_PW:
        return hy_pw(u, q);
    case MU_SQZ: {
        int64_t c = ((int64_t)(int32_t)(q ^ 0x80000000u) * u->sq) >> 16;
        c = c > 0x7FFFFFFF ? 0x7FFFFFFF : c < -0x7FFFFFFF ? -0x7FFFFFFF : c;
        return (uint32_t)(int32_t)c ^ 0x80000000u;
    }
    case MU_WARP:
        if (!u->sh)
            return hy_pw(u, q);
        return (q & ~(0xFFFFFFFFu >> u->sh)) | (hy_pw(u, q << u->sh) >> u->sh);
    case MU_HARM:
        return q * u->h;
    }
    return q;
}
/* a mutant's read: HARMONIC crossfades its harmonic into the next */
static inline __attribute__((always_inline)) float hy_mread(const hy_oview_t *o, const hy_mut_t *u, uint32_t q,
                                                             uint32_t mq)
{
    float x = hy_rd(o->a, o->b, o->fr, mq);
    if (u->mode == MU_HARM && u->hfr > 0.0f)
        x += (hy_rd(o->a, o->b, o->fr, q * (u->h + 1u)) - x) * u->hfr;
    return x;
}
/* phase q through the oscillator's mutants (series, each blended with what reached it by its WET) */
static inline __attribute__((always_inline)) float hy_chain(const hy_oview_t *o, uint32_t q, const float *src, float self)
{
    const hy_mut_t *u1 = o->m1, *u2 = o->m2;
    float g = 1.0f, y, x;
    uint32_t q1, q2;
    if (!u1)
        return hy_rd(o->a, o->b, o->fr, q);
    q1 = hy_mphase(u1, q, src, self, &g);
    y = hy_mread(o, u1, q, q1) * g;
    if (u1->wet < 1.0f) {
        x = hy_rd(o->a, o->b, o->fr, q);
        y = x + (y - x) * u1->wet;
    }
    if (!u2)
        return y;
    g = 1.0f;
    q2 = hy_mphase(u2, q1, src, self, &g);
    x = hy_mread(o, u2, q1, q2) * g;
    return y + (x - y) * u2->wet;
}
/* an oscillator with mutants: its own phase through the chain, then STACK's four copies (one copy of the chain's
 * code serves all five) */
static __attribute__((noinline)) float hy_osc(const hy_oview_t *o, uint32_t *ph, uint32_t *sp, const float *src, float self)
{
    float x = 0.0f, c = 0.0f;
    uint32_t j, n = o->swet > 0.0f ? 5u : 1u;
    if (!o->m1) {                                      /* STACK alone: the copies summed whole (every value is exact in
                                                        * a float, so this is the loop below without its conversions) */
        const int16_t *a = o->a, *b = o->b;
        int32_t fr = o->fr, sum = 0, one;
        uint32_t q = *ph += o->inc;
        one = hy_rdi(a, b, fr, q);
        for (j = 0; j < 4u; j++) {
            q = sp[j] += o->sinc[j];
            sum += hy_rdi(a, b, fr, q);
        }
        x = (float)one * (1.0f / 32768.0f);
        c = (float)sum * (1.0f / 32768.0f);
        return (x + c * o->swet) * o->srcp * o->g;
    }
    for (j = 0; j < n; j++) {
        uint32_t q;
        float y;
        if (!j) {
            *ph += o->inc;
            q = *ph;
        } else {
            sp[j - 1u] += o->sinc[j - 1u];
            q = sp[j - 1u];
        }
        y = hy_chain(o, q, src, self);
        if (!j)
            x = y;
        else
            c += y;
    }
    if (n > 1u)
        x = (x + c * o->swet) * o->srcp;
    return x * o->g;
}

/* FILTER 1: an n-pole ladder (trapezoidal one-poles, the feedback solved without delay, its input saturated);
 * y[0..n-1] its stages' outputs, y[4] the first stage's input (the three-pole modes mix them) */
static inline __attribute__((always_inline)) float hy_ladder(float *z, uint32_t n, float x, const hy_blk_t *c, float *y)
{
    float g = c->f1.lad.g1, sigma, u, d;
    uint32_t j;
    sigma = n == 4u ? z[3] + g * (z[2] + g * (z[1] + g * z[0])) :
            n == 3u ? z[2] + g * (z[1] + g * z[0]) : z[1] + g * z[0];
    sigma *= 1.0f - g;
    u = hy_softclip((x * c->gain1 - c->f1.lad.k1 * sigma) * c->f1.lad.rcp1);
    y[4] = u;
    for (j = 0; j < n; j++) {
        d = g * (u - z[j]);
        y[j] = z[j] + d;
        z[j] = fm_flush(hy_clampf(y[j] + d, -2.0f, 2.0f));
        u = y[j];
    }
    return u;
}
/* a trapezoidal state variable (A. Simper): lp, bp, hp from x; ic: its two integrators */
static inline __attribute__((always_inline)) void hy_svf(float *ic, float x, float a1, float a2, float a3, float *lp,
                                                          float *bp)
{
    float v3 = x - ic[1], v1 = a1 * ic[0] + a2 * v3, v2 = ic[1] + a2 * ic[0] + a3 * v3;
    ic[0] = fm_flush(2.0f * v1 - ic[0]);
    ic[1] = fm_flush(2.0f * v2 - ic[1]);
    *lp = v2;
    *bp = v1;
}
static __attribute__((noinline)) float hy_filter1(hy_voice_t *s, uint32_t type, float x, const hy_blk_t *c)
{
    float y[5], lp, bp;
    switch (type) {
    case F1_LAD12: case F1_FAT12:
        return hy_ladder(s->z, 2, x, c, y);
    case F1_LAD24: case F1_FAT24:
        return hy_ladder(s->z, 4, x, c, y);
    case F1_3LP:
        return hy_ladder(s->z, 3, x, c, y);
    case F1_3BP:
        hy_ladder(s->z, 3, x, c, y);
        return (y[0] - y[1]) * 2.0f;
    case F1_3HP:                                       /* (the binomial mix of the stages: three-pole high pass) */
        hy_ladder(s->z, 3, x, c, y);
        return y[4] - 3.0f * y[0] + 3.0f * y[1] - y[2];
    case F1_MSLP: case F1_MSHP: case F1_LPG: {         /* MS-20: the band integrator saturates (its scream) */
        float in = hy_softclip(x * c->gain1);
        hy_svf(s->z, in, c->f1.svf.a1, c->f1.svf.a2, c->f1.svf.a3, &lp, &bp);
        if (type == F1_LPG)
            return lp * c->f1.svf.lpg;
        s->z[0] = hy_softclip(s->z[0]);
        return hy_softclip(type == F1_MSHP ? in - c->f1.svf.kd1 * bp - lp : lp);   /* (its output stage clips too) */
    }
    case F1_VOWEL: {
        float b0, b1, b2;
        hy_svf(s->z, x, c->f1.vow.a1[0], c->f1.vow.a2[0], c->f1.vow.a3[0], &lp, &b0);
        hy_svf(s->z + 2, x, c->f1.vow.a1[1], c->f1.vow.a2[1], c->f1.vow.a3[1], &lp, &b1);
        hy_svf(s->z + 4, x, c->f1.vow.a1[2], c->f1.vow.a2[2], c->f1.vow.a3[2], &lp, &b2);
        return (b0 + b1 * 0.7f + b2 * 0.45f) * c->f1.vow.k;
    }
    }
    return x;
}

static inline float hy_g(float idx)                    /* a cutoff index (0..127, CUTOFF_HZ) as a TPT one-pole's G */
{
    uint32_t i;
    float g;
    idx = fm_clampf(idx, 0.0f, 126.99f);
    i = (uint32_t)idx;
    g = P5_TPT_G[i];
    return g + (P5_TPT_G[i + 1u] - g) * (idx - (float)i);
}
static void hy_svf_coef(float idx, float k, float *a1, float *a2, float *a3)
{
    float G = hy_g(idx), g = G / (1.0f - G);
    *a1 = 1.0f / (1.0f + g * (g + k));
    *a2 = g * *a1;
    *a3 = g * *a2;
}
/* the vowels' formants (Hz): A E I O U; CUT walks them */
static const uint16_t HY_VOWEL[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410},
                                        {300, 870, 2240}};
static void hy_coef1(hy_blk_t *c, const hy_base_t *b, float idx)
{
    uint32_t type = b->f1type;
    float res = b->f1res;
    c->gain1 = 1.0f;
    if (type == F1_VOWEL) {
        float pos = fm_clampf(idx, 0.0f, 127.0f) * (4.0f / 127.0f), f;
        uint32_t i = (uint32_t)pos, j;
        if (i > 3u) i = 3u;
        pos -= (float)i;
        c->f1.vow.k = 0.12f + (1.0f - res) * 0.5f;
        for (j = 0; j < 3u; j++) {
            f = (float)HY_VOWEL[i][j] + ((float)HY_VOWEL[i + 1u][j] - (float)HY_VOWEL[i][j]) * pos;
            hy_svf_coef(127.0f * fm_log2f(f * (1.0f / 30.0f)) * (1.0f / 9.0589f), c->f1.vow.k, &c->f1.vow.a1[j],
                        &c->f1.vow.a2[j], &c->f1.vow.a3[j]);
        }
        return;
    }
    if (type == F1_MSLP || type == F1_MSHP || type == F1_LPG) {
        c->f1.svf.kd1 = 2.0f - res * 1.96f;
        hy_svf_coef(idx, c->f1.svf.kd1, &c->f1.svf.a1, &c->f1.svf.a2, &c->f1.svf.a3);
        c->f1.svf.lpg = fm_clampf(idx * (1.0f / 100.0f), 0.0f, 1.0f);
        c->f1.svf.lpg *= c->f1.svf.lpg;
        c->gain1 = 1.0f + res * 0.6f;
        return;
    }
    c->f1.lad.g1 = hy_g(idx);
    {
        float g = c->f1.lad.g1, gn = type == F1_LAD12 || type == F1_FAT12 ? g * g : type >= F1_3LP ? g * g * g : g * g * g * g;
        float k = res * (type == F1_LAD12 || type == F1_FAT12 ? 1.9f : type >= F1_3LP ? 7.8f : 4.4f);
        c->f1.lad.k1 = k;
        c->f1.lad.rcp1 = 1.0f / (1.0f + k * gn);
        /* the level resonance takes from the pass band, partly given back (FAT: most of it) */
        c->gain1 = 1.0f + k * (type == F1_FAT12 || type == F1_FAT24 ? 0.75f : type >= F1_3LP ? 0.3f : 0.2f);
    }
}

/* the half-band decimator (27 taps, Kaiser: -43 dB from 26.5 kHz, flat to 17.6 kHz): its odd taps, the centre */
static const float HY_HB[7] = {2.164955030e-03f, -6.402938631e-03f, 1.392820774e-02f, -2.661780621e-02f,
                               4.886781318e-02f, -9.661241257e-02f, 3.148480322e-01f};
static inline __attribute__((always_inline)) float hy_decimate(const float *o, const float *e, uint32_t dp)
{
    float y = 0.4996483f * e[(dp - 6u) & 7u];
    for (uint32_t i = 0; i < 7u; i++)
        y += HY_HB[i] * (o[(dp - 13u + i) & 15u] + o[(dp - i) & 15u]);
    return y;
}
/* an oscillator without mutants: the read alone (no calls) */
static inline __attribute__((always_inline)) float hy_plain(const hy_oview_t *o, uint32_t *ph)
{
    *ph += o->inc;
    return hy_rd(o->a, o->b, o->fr, *ph) * o->g;
}
/* the sources at one (sub)sample: noise, the oscillators, ring; into the MIXER's two buses */
static inline __attribute__((always_inline)) void hy_sources(const hy_base_t *b, hy_voice_t *s, const hy_oview_t *ov,
                                                              const int16_t *w3, uint32_t inc3, float *x1, float *x2)
{
    float *src = s->out, a = 0.0f, d = 0.0f;
    uint32_t k, *ph = s->ph;
    if (b->noise_on) {
        float w = (float)(int32_t)md_noise32(&s->noise) * (1.0f / 2147483648.0f);
        if (b->noise == 1u) {                          /* pink (P. Kellet's three poles), brown (a leaky sum) */
            s->pink[0] = 0.99765f * s->pink[0] + w * 0.0990460f;
            s->pink[1] = 0.96300f * s->pink[1] + w * 0.2965164f;
            s->pink[2] = 0.57000f * s->pink[2] + w * 1.0526913f;
            w = (s->pink[0] + s->pink[1] + s->pink[2] + w * 0.1848f) * 0.25f;
        } else if (b->noise == 2u) {
            s->brown = s->brown * 0.985f + w * 0.12f;
            w = s->brown * 1.6f;
        }
        src[HYS_NOISE] = w;
    }
    if (b->on[2]) {
        ph[2] += inc3;
        src[HYS_OSC3] = hy_rd(w3, w3, 0, ph[2]);
    }
    if (b->on[1])                                      /* (an oscillator's own last sample: its FEEDBACK) */
        src[HYS_OSC2] = ov[1].m1 || ov[1].swet > 0.0f ? hy_osc(&ov[1], &ph[1], s->sp[1], src, src[HYS_OSC2])
                                                       : hy_plain(&ov[1], &ph[1]);
    if (b->on[0])
        src[HYS_OSC1] = ov[0].m1 || ov[0].swet > 0.0f ? hy_osc(&ov[0], &ph[0], s->sp[0], src, src[HYS_OSC1])
                                                       : hy_plain(&ov[0], &ph[0]);
    if (b->src_used[HYS_RING])
        src[HYS_RING] = src[b->ring_a] * src[b->ring_b];
    for (k = 0; k < b->nmix; k++) {                    /* the MIXER: FILTER 1's share and FILTER 2's */
        uint32_t j = b->mix[k];
        float v = src[j] * b->lvl[j], e = v * b->bal[j];
        a += v - e;
        d += e;
    }
    *x1 = a;
    *x2 = d;
}
static const float HY_SPREAD[4] = {-1.0f, -0.5f, 0.5f, 1.0f};
/* x to the nearest whole sample with dither d (the add of 1.5 * 2^23 rounds); 0.0f stays 0 (dsp.c dith_cutf_d) */
static inline __attribute__((always_inline)) int32_t hy_dith_cut(float x, float d)
{
    return x != 0.0f ? (int32_t)(x + d + 12582912.0f) - 12582912 : 0;
}
/* One voice's block (the batch job: CPU1 or CPU0). CPU0 prepares the block (hy_render); this owns the voice's state
 * until the batch joins and reads the part's prepared values, its wave copies, the host's P5_TANH and HY_HB */
static __attribute__((noinline)) void hy_kernel(const void *data, int32_t *out)
{
    const hy_job_t *jb = data;
    hy_part_t *P = hy_reg[jb->part].P;
    const hy_base_t *b = &P->b;
    const hy_blk_t *c = &jb->c;
    hy_voice_t *s = &P->voice[jb->vi];
    hy_oview_t ov[2];
    uint32_t i, o, k, f1 = b->f1type, n = jb->n;
    const float *dither = mh->dither_f + (jb->dk & (MOD3_DITH_N - 1u));
    float dc = s->dc;
    for (o = 0; o < 2u; o++) {
        const hy_osc_t *r = &c->o[o];
        hy_oview_t *w = &ov[o];
        w->a = P->wave[r->a];
        w->b = P->wave[r->b];
        w->fr = r->fr;
        w->g = r->g;
        w->inc = r->inc;
        w->swet = r->swet;
        w->m1 = r->m1 == HY_MU1 ? &s->mu1 : r->m1 < 4u ? &b->mut[r->m1] : 0;
        w->m2 = r->m2 < 4u ? &b->mut[r->m2] : 0;
        w->srcp = 1.0f / (1.0f + r->swet * 2.0f);
        if (r->swet > 0.0f)
            for (k = 0; k < 4u; k++)
                w->sinc[k] = (uint32_t)((float)r->inc * (1.0f + HY_SPREAD[k] * r->det));
    }
    for (i = 0; i < n; i++) {
        float x1, x2, y, lp, bp, amp;
        hy_sources(b, s, ov, P->wave[b->row3], c->inc3, &x1, &x2);
        if (s->os) {                                   /* 2x: this was the even half; the odd, then one of the two */
            uint32_t dp = s->dp = (uint8_t)((s->dp + 1u) & 15u);
            s->deven[0][dp & 7u] = x1;
            s->deven[1][dp & 7u] = x2;
            hy_sources(b, s, ov, P->wave[b->row3], c->inc3, &s->dodd[0][dp], &s->dodd[1][dp]);
            x1 = hy_decimate(s->dodd[0], s->deven[0], dp);
            x2 = b->bus2 ? hy_decimate(s->dodd[1], s->deven[1], dp) : 0.0f;
        }
        if (!b->f1post)
            x1 = hy_softclip(x1 * b->f1drive);
        y = hy_filter1(s, f1, x1, c);
        if (b->f1post)
            y = hy_softclip(y * b->f1drive);
        if (!b->route) {                               /* SERIES: FILTER 2 after FILTER 1 */
            x2 += y;
            y = 0.0f;
        }
        if (b->f2open && !c->f2mod) {
            y += x2;
        } else {
            hy_svf(s->f2, x2, c->sa1, c->sa2, c->sa3, &lp, &bp);
            y += c->lp * lp + c->bp * bp + c->hp * (x2 - c->sk * bp - lp);
        }
        y = hy_clampf(y, -4.0f, 4.0f);
        dc += (y - dc) * HY_COUPLE;
        amp = c->amp0 + (c->amp1 - c->amp0) * (float)i * (1.0f / HY_CTL);
        out[i] += hy_dith_cut((y - dc) * amp * c->scale, dither[i]);
    }
    s->dc = fm_flush(dc);
}

/* CPU0: the voice's block made ready (ENV 1, pitch, the scans, the mutants, the filters' coefficients), then the
 * kernel: in the firmware's queue (CPU1 or CPU0 by cost) or at once */
static void hy_render(mod_part_v3 *t, mod_voice_v3 *v, int32_t *out, uint32_t n, const mod_vmod_v3 *m)
{
    hy_part_t *P = hy_part(t);
    const hy_base_t *b;
    hy_voice_t *s;
    hy_job_t jb;
    hy_blk_t *c = &jb.c;
    uint32_t o;
    float e1, idx;
    if (!P || !n)
        return;
    if (!P->valid)
        hy_block(t);
    b = &P->b;
    jb.part = (uint8_t)(t->index % MOD3_NPART);       /* (hy_part: hy_reg holds P for the kernel) */
    jb.n = (uint8_t)n;
    jb.dk = (uint16_t)m->amp1;
    jb.vi = (uint8_t)(v->index % HY_SLOTS);
    s = &P->voice[jb.vi];
    e1 = hy_env1(b, s, v->gate);
    for (o = 0; o < 2u; o++) {                         /* OSC 1, 2: pitch, the scan, the mutants */
        hy_osc_t *r = &c->o[o];
        const hy_mut_t *u1 = &b->mut[o * 2u], *u2 = &b->mut[o * 2u + 1u], *m1, *m2;
        if (!o && b->e1mu != 0.0f && u1->mode) {      /* ENV 1 -> MUTANT 1's DEPTH: this voice's own */
            hy_mutant(&s->mu1, b->m1, (int32_t)(e1 * b->e1mu));
            u1 = &s->mu1;
        }
        int32_t p16 = 960 + (m->pitch16 - 960) * b->key[o] / 100 + b->semi16[o];
        r->inc = md_cents_inc(p16, b->cent[o], m->fine);
        if (b->scanning[o]) {                          /* SCAN: 0..127 over the eight slots; SHP moves it */
            int32_t pos = md_clamp(b->scan[o] + (m->shape - (64 << 8)) + (int32_t)(e1 * b->e1sc[o]), 0, 127 << 8) * 7;
            int32_t sl = pos / (127 << 8);
            int32_t f = (int32_t)(((pos - sl * (127 << 8)) << 7) / 127);   /* Q15 */
            const uint8_t *q = hy_raw(t) + (o ? HY_O2 : HY_O1) + HO_L0;
            int ea, eb;                                /* EMPTY slots: silence, faded into */
            if (sl >= 7) { sl = 7; f = 0; }
            ea = !q[sl];
            eb = !f || !q[sl + 1];
            r->a = (uint8_t)(b->row0[o] + (uint32_t)(ea && !eb ? sl + 1 : sl));
            r->b = (uint8_t)(b->row0[o] + (uint32_t)(eb ? sl : sl + 1));
            r->fr = ea || eb ? 0 : f;
            r->g = ea ? (eb ? 0.0f : (float)f * (1.0f / 32768.0f)) : eb && f ? 1.0f - (float)f * (1.0f / 32768.0f) : 1.0f;
        } else {
            r->a = r->b = b->row0[o];
            r->fr = 0;
            r->g = 1.0f;
        }
        m1 = u1->mode && u1->mode != MU_STACK ? u1 : u2->mode && u2->mode != MU_STACK ? u2 : 0;
        m2 = m1 == u1 && u2->mode && u2->mode != MU_STACK ? u2 : 0;
        r->m1 = !m1 ? HY_NOMUT : m1 == &s->mu1 ? HY_MU1 : (uint8_t)(m1 - b->mut);
        r->m2 = !m2 ? HY_NOMUT : (uint8_t)(m2 - b->mut);
        r->swet = r->det = 0.0f;
        if (b->stack[o]) {
            const hy_mut_t *st = &b->mut[b->stack[o] - 1u];
            r->swet = st->wet;
            r->det = st->det;
        }
        if (s->thin) {
            r->m1 = r->m2 = HY_NOMUT;
            r->swet = r->det = 0.0f;
        }
    }
    c->inc3 = b->on[2] ? md_cents_inc(960 + (m->pitch16 - 960) * b->key[2] / 100 + b->semi16[2], b->cent[2], m->fine) : 0u;
    /* the filters' cutoffs: the knob, ENV 1, the key (from middle C), velocity, the part's FLT modulation */
    idx = b->f1cut + e1 * b->f1env + (float)(m->pitch16 - 960) * (1.0f / 16.0f) * b->f1key +
          ((float)v->mvel - 64.0f) * (1.0f / 64.0f) * b->f1vel + (float)m->cutoff * (1.0f / 256.0f);
    hy_coef1(c, b, idx);
    c->f2mod = m->cutoff != 0;
    if (!b->f2open || c->f2mod) {                      /* (passed by, the kernel reads none of FILTER 2's) */
        idx = b->f2cut + e1 * b->f2env + (float)(m->pitch16 - 960) * (1.0f / 16.0f) * b->f2key +
              (float)m->cutoff * (1.0f / 256.0f);
        c->sk = 2.0f - b->f2res * 1.9f;
        hy_svf_coef(idx, c->sk, &c->sa1, &c->sa2, &c->sa3);
                                                       /* FILTER 2's morph: LP > BP > HP, or LP > NOTCH > HP
                                                        * (the notch: low and high together) */
        float mo = b->f2morph * 2.0f;
        if (b->f2notch) {
            c->lp = mo <= 1.0f ? 1.0f : 2.0f - mo;
            c->hp = mo <= 1.0f ? mo : 1.0f;
            c->bp = 0.0f;
        } else {
            c->lp = mo <= 1.0f ? 1.0f - mo : 0.0f;
            c->bp = mo <= 1.0f ? mo : 2.0f - mo;
            c->hp = mo <= 1.0f ? 0.0f : mo - 1.0f;
        }
    }
    c->amp0 = (float)m->amp0 * (1.0f / 32768.0f);
    c->amp1 = (float)m->amp1 * (1.0f / 32768.0f);
    c->scale = b->amp * (2.0f * (float)MOD3_VOICE_FS);
    if (s->os) {                                       /* (twice the rate: half the steps) */
        c->o[0].inc >>= 1;
        c->o[1].inc >>= 1;
        c->inc3 >>= 1;
    }
    if (*mh->batch_active) {                           /* (its share of the cores: STACK reads five times, 2x twice) */
        uint32_t reads = 1u + (c->o[0].swet > 0.0f ? 4u : 0u) + (c->o[1].swet > 0.0f ? 4u : 0u);
        mh->batch_push_cost(hy_kernel, &jb, sizeof jb, out, n, HY_AP, (HY_COST + (reads - 1u) * 200u) << s->os);
        return;
    }
    hy_kernel(&jb, out);
}

/* ----------------------------------------------------------------------------------- the panel values --- */
/* a macro's pages (hydra_schema.h): destination j (0..2) of macro m and its depth, ids past the patch's bytes; the
 * macros' values are the part's E1..E8 (MOD3_SK_TRACK ids HYV_VALUE + m) */
#define HY_MDESTS 3u
#define HYV_DEST(m, j) (HY_BYTES + (m) * 2u * HY_MDESTS + (j) * 2u)
#define HYV_DEPTH(m, j) (HYV_DEST(m, j) + 1u)
#define HYV_END HYV_DEST(HY_MACROS, 0)
#define HYV_VALUE HYV_END
/* the slot of macro m's j-th destination, -1 none; the first free slot, -1 none */
static int32_t hy_slot_of(const uint8_t *p, uint32_t m, uint32_t j)
{
    uint32_t k;
    for (k = 0; k < HY_MSLOTS; k++)
        if (HY_SLOT_TGT(p[HY_MA + 2u * k]) && HY_SLOT_MACRO(p[HY_MA + 2u * k]) == m && !j--)
            return (int32_t)k;
    return -1;
}
static int32_t hy_slot_free(const uint8_t *p)
{
    uint32_t k;
    for (k = 0; k < HY_MSLOTS; k++)
        if (!HY_SLOT_TGT(p[HY_MA + 2u * k]))
            return (int32_t)k;
    return -1;
}
static uint32_t hy_slots_used(const uint8_t *p, uint32_t m)
{
    uint32_t n = 0;
    while (n < HY_MSLOTS && hy_slot_of(p, m, n) >= 0)
        n++;
    return n;
}
/* a macro's destination (its HY_TGT number, 0 NONE) and depth (-127..127), none: NONE, 0 */
static int32_t hy_pget(const mod_part_v3 *t, const uint8_t *p, uint32_t id)
{
    (void)t;
    if (id < HY_BYTES || id >= HYV_END)
        return 0;
    int32_t k = hy_slot_of(p, (id - HY_BYTES) / (2u * HY_MDESTS), (id - HY_BYTES) / 2u % HY_MDESTS);
    if (k < 0)
        return 0;
    return (id - HY_BYTES) & 1u ? (int32_t)p[HY_MA + 2u * (uint32_t)k + 1u] - HY_DEPTH0
                                : (int32_t)HY_SLOT_TGT(p[HY_MA + 2u * (uint32_t)k]);
}
/* .. set: a depth of a destination there is; a destination set takes the next free slot (half its range at a full
 * turn), NONE gives it back. 1 changed */
static int hy_pput(mod_part_v3 *t, uint8_t *p, uint32_t id, int32_t v)
{
    uint8_t was[2u * HY_MSLOTS];
    (void)t;
    if (id < HY_BYTES || id >= HYV_END)
        return 0;
    memcpy(was, p + HY_MA, sizeof was);
    uint32_t m = (id - HY_BYTES) / (2u * HY_MDESTS), j = (id - HY_BYTES) / 2u % HY_MDESTS;
    int32_t k = hy_slot_of(p, m, j);
    if ((id - HY_BYTES) & 1u) {
        if (k >= 0)
            p[HY_MA + 2u * (uint32_t)k + 1u] = (uint8_t)(md_clamp(v, -HY_DEPTH0, HY_DEPTH0) + HY_DEPTH0);
    } else {
        v = md_clamp(v, 0, (int32_t)HY_NTGT);
        if (k < 0 && v && j == hy_slots_used(p, m) && (k = hy_slot_free(p)) >= 0)
            p[HY_MA + 2u * (uint32_t)k + 1u] = HY_DEPTH0 + 64u;
        if (k >= 0) {
            p[HY_MA + 2u * (uint32_t)k] = v ? HY_SLOT(m, (uint32_t)v) : 0u;
            if (!v)
                p[HY_MA + 2u * (uint32_t)k + 1u] = HY_DEPTH0;
        }
    }
    return memcmp(was, p + HY_MA, sizeof was) != 0;
}
/* dim what has no effect on the sound as the patch is (still editable); a macro's next destination lights while a slot is
 * free, the ones past it and their depths stay dark; a macro with no destination is dark */
static int hy_pactive(const mod_part_v3 *t, const uint8_t *p, uint32_t id)
{
    (void)t;
    if (id >= HYV_VALUE && id < HYV_VALUE + HY_MACROS)
        return hy_slot_of(p, id - HYV_VALUE, 0) >= 0;
    if (id >= HY_BYTES && id < HYV_END) {
        uint32_t m = (id - HY_BYTES) / (2u * HY_MDESTS), j = (id - HY_BYTES) / 2u % HY_MDESTS;
        if (hy_slot_of(p, m, j) >= 0)
            return 1;
        return !((id - HY_BYTES) & 1u) && j == hy_slots_used(p, m) && hy_slot_free(p) >= 0;
    }
    if (id < HY_O3) {                                  /* OSC 1, 2 */
        uint32_t b = id < HY_O2 ? HY_O1 : HY_O2, f = id - b, o = b == HY_O2;
        int heard = p[HY_LVL + o] || (p[HY_LVL + HYS_RING] && (p[HY_RING_A] == o || p[HY_RING_B] == o));
        if (!heard)
            for (uint32_t k = 0; k < 4u; k++)
                heard |= p[HY_M1 + k * HM_N + HM_MODE] == MU_FM && p[HY_M1 + k * HM_N + HM_SRC] == o;
        if (!heard)
            return 0;
        if (f == HO_WAVE)
            return !p[b + HO_MODE];
        if (f == HO_SCAN || (f >= HO_L0 && f < HO_L0 + 8u))
            return p[b + HO_MODE] != 0;
        return 1;
    }
    if (id >= HY_M1 && id < HY_M4 + HM_N) {            /* a mutant: its fields as its mode reads them */
        uint32_t f = (id - HY_M1) % HM_N, mode = p[id - f + HM_MODE];
        if (f == HM_MODE)
            return 1;
        if (!mode)
            return 0;
        if (f == HM_SRC)
            return mode == MU_FM;
        if (f == HM_RATIO)
            return mode == MU_SYNC || mode == MU_WARP || mode == MU_HARM;
        if (f == HM_AUX)
            return mode == MU_FM || mode == MU_SYNC;
        return 1;
    }
    if (id == HY_RING_A || id == HY_RING_B)
        return p[HY_LVL + HYS_RING] != 0;
    if (id == HY_NOISE)
        return p[HY_LVL + HYS_NOISE] != 0;
    if (id >= HY_BAL && id < HY_BAL + HYS_N)
        return p[HY_LVL + (id - HY_BAL)] != 0;
    if (id == HY_F1_DPOS)
        return p[HY_F1_DRIVE] != 0;
    if (id == HY_E1_MU1)
        return p[HY_M1 + HM_MODE] != 0;
    if (id == HY_E1_SC1)
        return p[HY_O1 + HO_MODE] != 0;
    if (id == HY_E1_SC2)
        return p[HY_O2 + HO_MODE] != 0;
    return 1;
}
/* E1..E8: the macros by the names the patch gives them (AUTO: the first destination's); a mutant's RATIO and AUX say
 * what they do in its mode (the schema's MOD3_SF_DESC parameters) */
static const mod_engine_v3 ENG_HYDRA;
#define X(n, b, l) l,
static const char *const HY_TGT_NAME[] = {HY_TARGETS};
#undef X
static const mod_param_v3 *hy_desc(const mod_part_v3 *t, uint32_t k)
{
    static mod_param_v3 D[MOD3_NPART][HY_MACROS], M;
    const uint8_t *p = hy_raw(t);
    if (k >= MOD3_DESC_ID) {
        uint32_t id = k - MOD3_DESC_ID, f, mode;
        if (id < HY_M1 || id >= HY_M4 + HM_N || ((f = (id - HY_M1) % HM_N) != HM_AUX && f != HM_RATIO))
            return 0;
        mode = p[id - f + HM_MODE];
        M = (mod_param_v3){f == HM_AUX ? (mode == MU_FM ? "FEEDBACK" : mode == MU_SYNC ? "WINDOW" : "AUX")
                                       : (mode == MU_WARP ? "PARTS" : mode == MU_HARM ? "RANGE" : "RATIO"),
                           f == HM_AUX ? MOD3_F_PCT : MOD3_F_INT, 0, 127, f == HM_AUX ? 0 : 32, 0, 0};
        return &M;
    }
    if (k >= HY_MACROS)
        return 0;
    mod_param_v3 *d = &D[t->index % MOD3_NPART][k];
    int32_t s = hy_slot_of(p, k, 0);
    uint32_t nm = p[HY_MNAME + k];
    *d = ENG_HYDRA.edit[k];
    d->label = nm && nm < HY_NNAME ? HY_MACRO_NAMES[nm] : s >= 0 ? HY_TGT_NAME[HY_SLOT_TGT(p[HY_MA + 2u * (uint32_t)s])] :
               d->label;
    return d;
}

/* the text of a wave, a macro's name or destination (the schema's MOD3_SF_FORMAT parameters: more names than a page's
 * column holds), at most seven letters (val: 8 bytes). A user wave's id (Saadet's bank): USER and its number */
static const char *const HY_WAVE_NAME[] = {HY_WAVE_NAME_LIST};
static void hy_pformat(const mod_part_v3 *t, const uint8_t *patch, uint32_t id, int32_t v, char *val, const char **unit)
{
    const char *s = "";
    uint32_t i, w = (uint32_t)(v < 0 ? 0 : v);
    (void)t;
    (void)patch;
    if (id == HY_O1 + HO_WAVE || id == HY_O2 + HO_WAVE || id == HY_O3_WAVE)
        w++;                                           /* (a single oscillator's byte: the wave id - 1) */
    if (id >= HY_BYTES && id < HYV_END)
        s = HY_TGT_NAME[w <= HY_NTGT ? w : 0];
    else if (id >= HY_MNAME && id < HY_MNAME + HY_MACROS)
        s = HY_MACRO_NAMES[w < HY_NNAME ? w : 0];
    else if (!w)
        s = "EMPTY";
    else if (w <= HY_NWAVE)
        s = HY_WAVE_NAME[w - 1u];
    else {
        val[0] = 'U'; val[1] = (char)('0' + (w - HY_NWAVE) / 10u % 10u); val[2] = (char)('0' + (w - HY_NWAVE) % 10u);
        val[3] = 0;
        *unit = 0;
        return;
    }
    for (i = 0; i < 7u && s[i]; i++)
        val[i] = s[i];
    val[i] = 0;
    *unit = 0;
}

/* ----------------------------------------------------------------------------------------------- SysEx --- */
/* the native message (F0 7D 48 59 01, two nibbles a byte, F7: hydra_patch.h), the patch as Saadet sends it; no bank
 * dump (the host sends user slots one by one) */
static uint32_t hy_sysex_dump(const uint8_t *patch, uint32_t what, uint32_t ch, uint8_t *out, uint32_t max)
{
    hy_patch_t p;
    (void)ch;
    if ((what & 255u) == MOD3_DUMP_BANK)
        return 0;
    memcpy(p.raw, patch, HY_BYTES);
    return hy_patch_valid(&p) ? hy_patch_encode(&p, out, max) : 0;
}
/* a native message in (the schema's SYSEX pattern 7D 48 59): offered for a user slot (the USER page asks where), and
 * heard at once on the selected part when it plays HYDRA */
static uint32_t hy_sysex(const uint8_t *f, uint32_t n, uint32_t ev)
{
    hy_patch_t p;
    if (ev != MOD3_SX_END)
        return ev == MOD3_SX_ABORT ? MOD3_SXR_DONE : MOD3_SXR_MORE;
    if (n >= 2u && hy_patch_decode(&p, f + 1, n - 2u)) {
        int k = mh->sx_part(hy_eng, MOD3_SX_SEL);
        if (k >= 0 && mh->part_patch((uint32_t)k, hy_eng))
            (void)mh->patch_put((uint32_t)k, hy_eng, p.raw, HY_BYTES, MOD3_PATCH_SET);
        (void)mh->sx_stage(hy_eng, p.raw, 1, HY_BYTES);
    } else {
        mh->message("INVALID HYDRA");
    }
    return MOD3_SXR_DONE;
}

/* ---------------------------------------------------------------------------------------------- engine --- */
static const mod_engine_v3 ENG_HYDRA = {
    .name = "HYDRA",
    .page_title = {"MACROS 1-4", "MACROS 5-8"},
    .edit = {
        {"M1", MOD3_F_BIPCT, -64, 63, 0, 0, 0}, {"M2", MOD3_F_BIPCT, -64, 63, 0, 0, 0},
        {"M3", MOD3_F_BIPCT, -64, 63, 0, 0, 0}, {"M4", MOD3_F_BIPCT, -64, 63, 0, 0, 0},
        {"M5", MOD3_F_BIPCT, -64, 63, 0, 0, 0}, {"M6", MOD3_F_BIPCT, -64, 63, 0, 0, 0},
        {"M7", MOD3_F_BIPCT, -64, 63, 0, 0, 0}, {"M8", MOD3_F_BIPCT, -64, 63, 0, 0, 0},
    },
    .npresets = 17, .poly = HY_POLY,
    .knob = {MOD3_P_E0, MOD3_P_E0 + 1, MOD3_P_E0 + 2, MOD3_P_E0 + 3},
    .note_on = hy_note_on, .render = hy_render, .block = hy_block, .desc = hy_desc,
    .cap = hy_cap, .units = hy_units, .state = hy_state,
    .pget = hy_pget, .pput = hy_pput, .pformat = hy_pformat, .pactive = hy_pactive,
    .sysex = hy_sysex, .sysex_dump = hy_sysex_dump,
};

/* the host table, its P5_TANH, and the number this device gave the module (its SysEx names it) */
static int hy_bind(const mod_host_v3 *h, uint32_t self)
{
    hy_eng = MOD3_SELF_ENGINE(self);
    if (md_bind(h, self))
        return 1;
    hy_tanh = (const float *)h->table(MOD3_T_P5_TANH);
    return !hy_tanh;
}

#if defined(MOD_HOSTED) && !defined(MOD_IN_CORE)  /* (the schema: the builder's host build writes the SCHM chunk) */
#include "hydra_schema.h"
MOD3_SCHEMA(HY_SCHEMA);
#endif
MOD3_IDENT("Saadet", "hydra", "HYDR");            /* its ID: its author key and "hydra" (mod_abi3.h) */
#ifndef MOD_IN_CORE                               /* (the presets: the file's PRST chunk, never the image's) */
#include "hydra_presets.h"
#else
#define HY_NPRESETS 17u
#endif
_Static_assert(HY_NPRESETS == 17u, "INIT and Saadet's sixteen");
MOD3_EXPORT(mod_hydra) = {
    .magic = MOD3_MAGIC, .abi = MOD3_ABI, .size = sizeof(mod_desc_v3), .host = MOD3_SDK_HOST,
    .eng_size = sizeof(mod_engine_v3), .key = 0, .bind = hy_bind,   /* (MOD3_SDK_HEAD, its own bind) */
    .engine = 23,                                 /* the number it asks for (a device gives the first free one if taken) */
    .name = "HYDRA",
    .npresets = HY_NPRESETS,
    .eng = &ENG_HYDRA,
};
