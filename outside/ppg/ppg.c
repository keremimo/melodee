/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* PPG: a PPG Wave 2.2 group, written for Saadet from the 2.2's owner's manual and its wave EPROMs' layout, as a Melodee
 * synth module built from the SDK alone (mod_sdk.h, mod_schema.h; ABI 3).
 *
 * A voice: OSC and SUB read waves from the part's wave memory, the table's 64 and the upper table's 64, each wave a half
 * cycle of 64 bytes whose second half is the first played backwards and negated. A wave moves in whole waves and is
 * taken up at the next half cycle, so a sweep steps as the 2.2's does. Then the 24 dB low-pass (VCF-CUTOFF, EMPHASIS,
 * ENV 1, keys, touch, velocity, the LFO, the bender) and the loudness (ENV 2, keys, touch, velocity, the tremolo). ENV 1
 * also sweeps the waves; ENV 3, attack and decay, pulls the pitch of OSC, SUB or both and may sweep SUB's waves. The LFO
 * (four shapes, rate, delay) reaches pitch, waves, filter and loudness, its depth the wheel's.
 *
 * The waves are PPG's, read from local images of the EPROMs and the OS at build time (gen_ppg_data.py beside this file:
 * ppg_rom.h, ppg_presets.h, both gitignored) and never kept in this repository. A table's key waves are blended into the
 * slots between them when the table loads, as the 2.2 computes its working memory. Tables 28 (a phasing saw) and 29
 * (pulse widths), which the 2.2 computes, and the standard waves 60..63 are made here; their exact forms, the envelopes'
 * and the LFO's curves, and the four LFO shapes are this file's estimates until they are read from the 2.2's OS. No
 * other implementation's code is used.
 *
 * Saadet's engine (firmware/src/eng_ppg.c, Saadet d4ae975) on the module's views: the same arithmetic. What differs:
 * - Saadet's poly_key (a key on its way in POLY takes its unit of 2.2 voices and ends the keys sounding in it, so the
 *   budget counts the new key in their place) is this module's alloc, which then picks the key's voice as voice.c's
 *   POLY allocation does (the same note, a free voice round the part, the oldest released, the oldest held but the
 *   lowest). ABI 3 has no spare voice: no key beyond KEYB's cap while another fades.
 * - The voice budget is in whole units (Saadet's in quarters): 2 a 2.2 voice, a key's share rounded up.
 * - The common pitch bend is held at 0 (bend_range 0, as Saadet's midi_input.c does for PPG): BD and BI bend it.
 * - The pages' dim hints and the GROUP button as Saadet's; the 2.2's voices' dealing as Saadet's (measured, V3 OS).
 *     python3 gen_ppg_data.py && python3 sdk/mod_build.py ppg/ppg.c -o ppg.mmod */
#include "mod_sdk.h"
#include "mod_schema.h"
#include "ppg_math.h"
#include "ppg_patch.h"
#if !__has_include("ppg_rom.h")
#error "PPG needs the PPG Wave 2.2's waves: run gen_ppg_data.py beside ppg.c (local images, never committed)"
#endif
#include "ppg_rom.h"
#ifdef MOD_IN_CORE                                /* (the presets: the file's PRST chunk, never the image's) */
#undef MOD3_PRESETS_PATCH
#define MOD3_PRESETS_PATCH(n) __attribute__((unused)) static const struct { mod3_prow_t row; uint8_t patch[n]; } ppg_rows_[]
#endif
#if !__has_include("ppg_presets.h")
#error "PPG's presets: run gen_ppg_data.py beside ppg.c"
#endif
#include "ppg_presets.h"                          /* (gen_ppg_data.py: INIT WAVE, then the 2.2's programs) */

#define PPG_POLY 8u                       /* keys at once, at most (KEYB 0) */
#define PPG_HW 8u                         /* the 2.2's eight voices */
#define PPG_ROM_UPPER 28u                 /* the directory's table after 27: the Upper Wavetable */
#define PPG_FS 44100.0f
#define PPG_CTL 32.0f
_Static_assert(MOD3_RATE == 44100u && MOD3_CTL == 32u, "PPG rates assume 44.1 kHz and 32-sample blocks");
_Static_assert(PPG_HW <= MOD3_NVOICE, "a 2.2 voice's key is a part's voice");

typedef struct {
    uint32_t ph[2];                       /* OSC, SUB */
    float bc[2];                          /* their step corrections owed to the next sample (ppg_osc) */
    float z[4], dc, g;                    /* the filter's stages, the coupling, its coefficient last block */
    float e1, e2, e3, amp;                /* ENV 1..3, 0..1 (lv / 255); the amplitude last block */
    float acc;                            /* time towards the next pass of the 2.2's envelopes */
    int16_t lv[3];                        /* ENV 1..3 as the 2.2 keeps them, 0..255 */
    uint8_t wait[3];                      /* passes to wait before a step (ppg_eg_rate) */
    uint8_t s1, s2, s3;                   /* stages: 0 off, 1 attack, 2 decay, 3 sustain, 4 release (ENV 3: 1 up, 2 down) */
    uint8_t cur[2], pend[2];              /* the wave playing and the one taken up at the next half cycle, 0..127; TABLE 31:
                                           * cur is the 128-byte stretch of wave memory playing, 0..63 */
    uint8_t fresh, trig;                  /* trig: a key struck it (TABLE 31 starts its run again) */
    int32_t last_pcm;
} ppg_voice_t;
/* the patch as the voices use it, set once a block before them and read-only while they render */
typedef struct {
    float lfo0, lfo1, depth, lfo_raw;     /* the LFO after its delay, -1..1; the wheel's depth (units); the LFO alone */
    uint8_t ia1, id1, ir1, ia2, id2, ir2, ia3, id3;   /* the envelopes' rate indices (PPG_EG_*), as the 2.2's OS takes them */
    int16_t sus1, sus2;                   /* the sustain levels, 0..252 */
    uint8_t swell1, swell2;               /* ATTACK above 48: the attack runs on after the key, then releases */
    float cut, k, e1f, kf, level;         /* filter units (0..255, 20.25 an octave): CUTOFF, ENV 1 at full, a key's; resonance;
                                           * loudness */
    float e1w, kw, e3pitch, e3sub;        /* waves ENV 1 spans; waves a key; ENV 3: semitones, SUB's waves */
    float bend, bend_st, vib;             /* the bender, -1..1 and in semitones (BI); vibrato in semitones at full depth */
    int32_t sub16, sub_ct;                /* SUB's offset by DETU, 1/16 semitones and cents */
    int8_t semi[PPG_HW];                  /* each voice's SEMIT, semitones */
    uint8_t osc, sub, sw, uw, mp, mw, mf, ml, tw, tf, tl, tm, vf, vf_on, vl, e3to, bd, bi, kl;
    uint8_t smp, loop;                    /* TABLE 31's natural sounds: on; the 128-byte stretch they loop (ENV 1 WAVES) */
} ppg_render_t;
/* The 2.2's eight voices, as its OS deals them (measured on the V3 OS): a key takes a unit of voices from its side of
 * the keyboard, round the side's units in turn, the oldest taken when none is free; a voice taken from a key goes on
 * from where its envelopes are, at the new key. Each voice plays the group its place gives it and its own SEMIT. A
 * part's voice is a key: it renders the 2.2 voices it holds, and ends when it holds none or they have all ended */
typedef struct {
    ppg_voice_t hw[PPG_HW];               /* the 2.2's voices */
    uint8_t own[PPG_HW];                  /* the part's voice (its index + 1) holding each; 0 none */
    uint8_t grp[PPG_HW];                  /* the group each plays (0 A, 1 B), set when a key takes it */
    uint8_t rr[2];                        /* each side's next unit */
    uint8_t want, want_note, want_unit;   /* the unit ppg_poly_key chose for the key on its way (want: 1 chosen) */
    uint8_t hand;                         /* voices ppg_poly_key took from a sounding key: they go on, not afresh */
    uint8_t vrr;                          /* the part's next free voice (ppg_alloc, as voice.c's round) */
    ppg_render_t r[2];                    /* group A's, group B's */
    uint32_t lfo[2], delay_n;             /* each group its own LFO, as the 2.2's */
    float lfo_out[2];
    uint8_t keys, table;                  /* keys held last block; the table in wave[0..63] + 1 (0: none yet: the state
                                           * starts zeroed) */
    uint8_t wave[128][64];                /* the table's 64 waves, then the upper table's 64 */
} ppg_part_t;

static ppg_part_t *ppg_part(const mod_part_v3 *t) { return md_part(t, sizeof(ppg_part_t)); }
static uint32_t ppg_state(const mod_part_v3 *t) { (void)t; return sizeof(ppg_part_t); }
/* the program the part plays (zeros while it switches: KEYB 0, every level 0) */
static const ppg_patch_t *ppg_patch_of(const mod_part_v3 *t) { return (const ppg_patch_t *)(const void *)mh->patch(t); }
static inline __attribute__((always_inline)) mod_voice_v3 *ppg_v(const mod_part_v3 *t, uint32_t i)
{
    return mod3_voice(t, i % MOD3_NVOICE);
}
/* the host's dither of this block, in LSBs: a row of MOD3_CTL values from k (dsp.c dith_row) */
static inline __attribute__((always_inline)) const float *ppg_dith_row(uint32_t k)
{
    return mh->dither_f + (k & (MOD3_DITH_N - 1u));
}
/* x to the nearest whole sample with dither d (the add of 1.5 * 2^23 rounds); 0.0f stays 0 */
static inline __attribute__((always_inline)) int32_t ppg_dith_cut(float x, float d)
{
    return x != 0.0f ? (int32_t)(x + d + 12582912.0f) - 12582912 : 0;
}

/* KEYB's sides, as the OS deals its voices: [KEYB][0 the upper side (A; or the whole keyboard), 1 the lower (B)]: the
 * voices (a mask) and how many a key takes. Where a key takes the whole side, the side is monophonic. Measured: 0 eight
 * voices, one a key; 1 pairs, 2 fours, 3 all eight on one key (an even voice plays A, an odd one B); 4 split, A on the
 * even voices above, B on the odd ones below; 5 A all four even voices on one key, B four; 6 A voices 0 and 2 on one
 * key, B the other six; 7 B voices 1 and 3 on one key, A the other six; 8 A four on one key, B four on one key */
static const struct { uint8_t mask, per; } PPG_SIDE[9][2] = {
    {{0xFF, 1}, {0, 0}}, {{0xFF, 2}, {0, 0}}, {{0xFF, 4}, {0, 0}}, {{0xFF, 8}, {0, 0}},
    {{0x55, 1}, {0xAA, 1}}, {{0x55, 4}, {0xAA, 1}}, {{0x05, 2}, {0xFA, 1}}, {{0xF5, 1}, {0x0A, 2}}, {{0x55, 4}, {0xAA, 4}},
};
/* a note's side: the lower (B) for a key up to SPLIT where KEYB splits (4..8), else the upper */
static uint32_t ppg_side(const ppg_patch_t *p, uint32_t note)
{
    if (p->keyb < 4u) return 0;
    int32_t k = (int32_t)note - 36;                 /* (a key below the 2.2's lowest C counts as that C) */
    return (k < 0 ? 0 : k) < (int32_t)p->split ? 1u : 0u;
}
/* the group voice h plays: KEYB 0 the selected one (the 2.2's A slot, B loaded into it for GROUP B); 1..3 even A, odd
 * B; 4..8 its side's */
static uint32_t ppg_hw_group(const ppg_patch_t *p, uint32_t h)
{
    uint32_t k = p->keyb > 8u ? 0u : p->keyb;
    if (!k) return p->sel & 1u;
    if (k <= 3u) return h & 1u;
    return PPG_SIDE[k][1].mask >> h & 1u;
}
/* unit u of a side: its voices (a mask), the side's voices in order, per at a time */
static uint32_t ppg_unit(uint32_t mask, uint32_t per, uint32_t u)
{
    uint32_t out = 0, n = 0;
    for (uint32_t h = 0; h < PPG_HW; h++)
        if (mask >> h & 1u) { if (n / per == u) out |= 1u << h; n++; }
    return out;
}
static uint32_t ppg_held(const ppg_part_t *P, uint32_t vi)   /* the 2.2 voices the part's voice vi holds */
{
    uint32_t m = 0;
    for (uint32_t h = 0; h < PPG_HW; h++) m |= (uint32_t)(P->own[h] == vi + 1u) << h;
    return m;
}

/* ------------------------------------------------------------ the waves --- */
static uint8_t ppg_u8(float x)
{
    return (uint8_t)md_clamp((int32_t)(128.0f + x * 127.0f + (x < 0.0f ? -0.5f : 0.5f)), 1, 255);
}
static float ppg_saw(float p) { p -= (float)(int32_t)p; if (p < 0.0f) p += 1.0f; return 2.0f * p - 1.0f; }
/* slots 60..63 of every table: triangle, pulse, square, saw, as half cycles (the saw's mirrored half rises on) */
static void ppg_standard(uint8_t *w, uint32_t k)
{
    for (uint32_t i = 0; i < 64u; i++) {
        float p = (float)i * (1.0f / 128.0f);
        w[i] = ppg_u8(k == 0u ? (p < 0.25f ? 4.0f * p : 2.0f - 4.0f * p) : k == 1u ? (i >= 56u ? 1.0f : 0.0f) :
                      k == 2u ? 1.0f : 2.0f * p - 1.0f);
    }
}
/* table 28, a phasing saw: two saws drawn apart by the slot (their sum keeps the half-cycle symmetry); table 29: the
 * square narrowing to a pulse */
static void ppg_computed(uint8_t *w, uint32_t table, uint32_t slot)
{
    for (uint32_t i = 0; i < 64u; i++) {
        float p = (float)i * (1.0f / 128.0f), d = (float)slot * (1.0f / 128.0f);
        w[i] = ppg_u8(table == 28u ? 0.5f * (ppg_saw(p + d) + ppg_saw(p - d)) : i >= slot ? 1.0f : 0.0f);
    }
}
/* a ROM table into 64 slots: each slot between two keys the straight blend of them, by its distance (the key at 60
 * only steers 49..59) */
static void ppg_rom_table(uint8_t (*dst)[64], uint32_t t)
{
    uint32_t a = PPG_KEY_AT[t], b = PPG_KEY_AT[t + 1u];
    for (uint32_t k = a; k + 1u < b; k++) {
        uint32_t s0 = PPG_KEY_SLOT[k], s1 = PPG_KEY_SLOT[k + 1u];
        const uint8_t *w0 = PPG_WAVES[PPG_KEY_WAVE[k]], *w1 = PPG_WAVES[PPG_KEY_WAVE[k + 1u]];
        for (uint32_t s = s0; s < s1 && s < 60u; s++)
            for (uint32_t i = 0; i < 64u; i++)
                dst[s][i] = (uint8_t)(((uint32_t)w0[i] * (s1 - s) + (uint32_t)w1[i] * (s - s0) + (s1 - s0) / 2u) / (s1 - s0));
    }
}
static void ppg_build(uint8_t (*dst)[64], uint32_t table)
{
    if (table < PPG_ROM_UPPER) ppg_rom_table(dst, table);
    else if (table < PG_UPPER) for (uint32_t s = 0; s < 60u; s++) ppg_computed(dst[s], table, s);
    else ppg_rom_table(dst, PPG_ROM_UPPER);
    for (uint32_t k = 0; k < 4u; k++) ppg_standard(dst[60u + k], k);
}
/* a part's wave memory for its table, when it changes (the 2.2 recomputes it on a program change too). WAVETABLE 31, as
 * the OS loads it: the natural sounds over the wave memory from its start (the piano, then the saxophone, 7,936 bytes),
 * the upper table's four standard waves left at the end; leaving 31 rebuilds the upper table */
static void ppg_load_table(ppg_part_t *P, uint32_t table)
{
    if (!P->table || (P->table == PG_SAMPLE + 1u && table != PG_SAMPLE)) ppg_build(&P->wave[64], PG_UPPER);
    if (P->table == table + 1u) return;
#if PPG_HAVE_SAMPLE
    if (table == PG_SAMPLE) {
        _Static_assert(PPG_SAMPLE_BYTES == 124u * 64u, "the natural sounds end at the upper table's standard waves");
        for (uint32_t i = 0; i < PPG_SAMPLE_BYTES; i++) (&P->wave[0][0])[i] = PPG_SAMPLE[i];
        P->table = (uint8_t)(table + 1u);
        return;
    }
#endif
    ppg_build(&P->wave[0], table);
    P->table = (uint8_t)(table + 1u);
}

/* ------------------------------------------------------------ the voice --- */
static inline __attribute__((always_inline)) float ppg_at(const uint8_t *w, uint32_t i)   /* i: 0..127 of a cycle */
{
    return i < 64u ? (float)((int32_t)w[i] - 128) : (float)(128 - (int32_t)w[127u - i]);
}
/* one sample of an oscillator as the 2.2's DAC puts it out: the cycle's 128 points each held for 1 / (128 f), a
 * staircase, its steps made band-limited (a two-sample polynomial BLEP) so the steps' images below Nyquist are heard,
 * as the 2.2's are on low notes, and the rest does not fold back. A step of height h at fraction t of the way to the
 * next sample adds h (1 - t)^2 / 2 to this sample and -h t^2 / 2 to the next (*carry). The pending wave is taken up
 * at a half cycle (points 0 and 64) */
static inline __attribute__((always_inline)) float ppg_osc(uint32_t *ph, const uint8_t **w, const uint8_t *pend, uint32_t inc,
                                                           float *carry)
{
    uint32_t p = *ph, i = p >> 25, q = p + inc;
    float prev = ppg_at(*w, i), y = prev + *carry, next = 0.0f;
    uint32_t steps = ((q >> 25) - i) & 127u;
    if (steps) {
        float rinc = 1.0f / (float)inc;
        uint32_t b = (i + 1u) << 25;                         /* the phase of the next step */
        for (uint32_t k = 1; k <= steps; k++, b += 1u << 25) {
            uint32_t j = (i + k) & 127u;
            if (!(j & 63u)) *w = pend;
            float cur = ppg_at(*w, j), h = cur - prev, t = (float)(uint32_t)(b - p) * rinc, a = 1.0f - t;
            y += h * 0.5f * a * a;
            next -= h * 0.5f * t * t;
            prev = cur;
        }
    }
    *carry = next;
    *ph = q;
    return y;
}
/* TABLE 31, the 2.2's short-loop transient mode (owner's manual 7.3.6; Seib, "Transient Sounds"): the wave memory
 * read straight, 128 bytes a cycle at the oscillator's own clock (no mirrored half), from the start stretch onwards a
 * stretch a cycle while it is short of the loop stretch, which then repeats. A start at or past the loop holds the start:
 * 91 FLUEGEL MIT WAVE SOUND's group A (start 38, loop 23) is a single sax cycle, the "wave sound" its name gives, not a run
 * round the memory through the standard waves (which popped). The same staircase and BLEP as ppg_osc */
static inline __attribute__((always_inline)) float ppg_tosc(uint32_t *ph, uint32_t *unit, uint32_t loop, const uint8_t *ram,
                                                            uint32_t inc, float *carry)
{
    uint32_t p = *ph, i = p >> 25, q = p + inc;
    const uint8_t *w = ram + (*unit << 7);
    float prev = (float)((int32_t)w[i] - 128), y = prev + *carry, next = 0.0f;
    uint32_t steps = ((q >> 25) - i) & 127u;
    if (steps) {
        float rinc = 1.0f / (float)inc;
        uint32_t b = (i + 1u) << 25;
        for (uint32_t k = 1; k <= steps; k++, b += 1u << 25) {
            uint32_t j = (i + k) & 127u;
            if (!j) {                                        /* a cycle done: the next stretch, until the loop's */
                if (*unit < loop) *unit = *unit + 1u;
                w = ram + (*unit << 7);
            }
            float cur = (float)((int32_t)w[j] - 128), h = cur - prev, t = (float)(uint32_t)(b - p) * rinc, a = 1.0f - t;
            y += h * 0.5f * a * a;
            next -= h * 0.5f * t * t;
            prev = cur;
        }
    }
    *carry = next;
    *ph = q;
    return y;
}
static inline __attribute__((always_inline)) float ppg_sat(float x)   /* the resonance limit, as the JUNO-106's */
{
    x = fm_clampf(x, -1.5f, 1.5f);
    return x - x * x * x * (4.0f / 27.0f);
}
static float ppg_coeff(float oct)        /* one-pole G = g / (1 + g), g = tan(pi f / FS), f = 8 Hz * 2^oct */
{
    float f = 8.0f * fm_exp2f(fm_clampf(oct, 0.0f, 11.2f));
    float g = fm_tanf(FM_PI * fm_minf(f, 18000.0f) * (1.0f / PPG_FS));
    return g / (1.0f + g);
}

typedef struct {
    const uint8_t *w0, *p0, *w1, *p1;     /* OSC's wave and its next, SUB's */
    const uint8_t *ram;                   /* TABLE 31: the wave memory, read straight */
    uint32_t smp, loop;                   /* TABLE 31: on; the stretch it loops */
    uint32_t inc0, inc1;
    float sub, g0, dg, a0, da, m0, dm, k, scale;
    const float *dither;
} ppg_kernel_t;
static void ppg_samples(ppg_voice_t *restrict s, int32_t *restrict out, uint32_t n, const ppg_kernel_t *restrict c)
{
    float z0 = s->z[0], z1 = s->z[1], z2 = s->z[2], z3 = s->z[3], dc = s->dc;
    uint32_t ph0 = s->ph[0], ph1 = s->ph[1];
    float bc0 = s->bc[0], bc1 = s->bc[1];
    const uint8_t *w0 = c->w0, *w1 = c->w1;
    uint32_t u0 = s->cur[0], u1 = s->cur[1];
    int32_t last = 0;
    for (uint32_t i = 0; i < n; i++) {
        float time = (float)i * (1.0f / PPG_CTL);
        float x = c->smp ? ppg_tosc(&ph0, &u0, c->loop, c->ram, c->inc0, &bc0) : ppg_osc(&ph0, &w0, c->p0, c->inc0, &bc0);
        if (c->sub != 0.0f)
            x += c->sub * (c->smp ? ppg_tosc(&ph1, &u1, c->loop, c->ram, c->inc1, &bc1) : ppg_osc(&ph1, &w1, c->p1, c->inc1, &bc1));
        x *= 1.0f / 256.0f;                                   /* OSC and SUB together peak at 1 */
        float G = c->g0 + c->dg * time, G2 = G * G, G3 = G2 * G, G4 = G2 * G2;
        float sigma = (1.0f - G) * (G3 * z0 + G2 * z1 + G * z2 + z3);
        float u = (x - c->k * sigma) / (1.0f + c->k * G4);
        u = x - c->k * 0.25f * ppg_sat(4.0f * (G4 * u + sigma));
        float v;
        v = (u - z0) * G; u = v + z0; z0 = u + v;
        v = (u - z1) * G; u = v + z1; z1 = u + v;
        v = (u - z2) * G; u = v + z2; z2 = u + v;
        v = (u - z3) * G; u = v + z3; z3 = u + v;
        dc += (u - dc) * 0.0014237444f;                       /* coupling at 10 Hz (1 - exp(-2 pi 10 / FS)) */
        float amp = (c->a0 + c->da * time) * (c->m0 + c->dm * time);
        last = ppg_dith_cut((u - dc) * amp * c->scale, c->dither[i]);
        out[i] += last;
    }
    s->z[0] = fm_flush(z0); s->z[1] = fm_flush(z1); s->z[2] = fm_flush(z2); s->z[3] = fm_flush(z3);
    s->dc = fm_flush(dc);
    s->ph[0] = ph0; s->ph[1] = ph1; s->last_pcm = last;
    s->bc[0] = fm_flush(bc0); s->bc[1] = fm_flush(bc1);
    if (c->smp) { s->cur[0] = (uint8_t)u0; s->cur[1] = (uint8_t)u1; return; }
    if (w0 == c->p0) s->cur[0] = s->pend[0];
    if (w1 == c->p1) s->cur[1] = s->pend[1];
}

/* The envelopes as the 2.2's OS runs them (its envelope loop, once a pass of its main loop): 8-bit levels; a step
 * moves a fraction (PPG_EG_FRAC / 256) of the distance left, at least one level, then waits PPG_EG_WAIT passes. Attack
 * goes to 255 (index ATTACK / 4, 0 at once), decay to the sustain level (its distance weighed by 255 - sustain, index
 * DECAY / 2), release to 0 (RELEASE / 2). ATTACK above 48 (the OS's 0x60): the attack runs on after the key and goes
 * straight to release, the 2.2's automatic. ENV 3 rises over ATTACK and falls over DECAY (index / 4; the last holds).
 * PPG_TICK, the time a pass takes: the main loop runs free, so it was measured by running the V3 OS (whose tables are
 * the V5's) in a 6809 emulator: 26,634 cycles a pass at rest, 29,762 with all eight voices enveloping, at the 2 MHz E
 * clock its MIDI ACIA implies (control 0xD6: divide by 64 for 31,250 baud), so 13.3 .. 14.9 ms; interrupts not counted */
#define PPG_TICK 0.016f                 /* (30,605 cycles at rest with the timer interrupt's share, 33,700 busy: 15.3 .. 16.9 ms) */
static inline int32_t ppg_eg_rate(uint32_t idx, int32_t dist, uint8_t *wait)
{
    int32_t step = (dist * PPG_EG_FRAC[idx & 31u]) >> 8;
    *wait = PPG_EG_WAIT[idx & 31u];
    return step ? step : 1;
}
static void ppg_adsr_pass(int16_t *lv, uint8_t *st, uint8_t *wait, int gate, uint32_t ia, uint32_t id, int32_t sus,
                          uint32_t ir, int swell)
{
    if (!gate && (*st == 2u || *st == 3u || (*st == 1u && !swell))) { *st = 4; *wait = 0; }
    if (*st == 0u || *st == 3u) return;
    if (*wait) { (*wait)--; return; }
    int32_t L = *lv;
    if (*st == 1u) {
        L = ia ? L + ppg_eg_rate(ia, 255 - L, wait) : 255;
        if (L >= 255) { L = 255; *st = swell ? 4u : 2u; }
    } else if (*st == 2u) {
        L -= ppg_eg_rate(id, ((L - sus) * (255 - sus)) >> 8, wait);
        if (L <= sus) { L = sus; *st = 3; }
    } else {
        L -= ppg_eg_rate(ir, L, wait);
        if (L <= 0) { L = 0; *st = 0; }
    }
    *lv = (int16_t)L;
}
static void ppg_env_tick(ppg_voice_t *s, const ppg_render_t *r, int gate)
{
    if (!gate) {                                    /* a key let go is heard at once, not at the next pass */
        if (s->s1 == 2u || s->s1 == 3u || (s->s1 == 1u && !r->swell1)) { s->s1 = 4; s->wait[0] = 0; }
        if (s->s2 == 2u || s->s2 == 3u || (s->s2 == 1u && !r->swell2)) { s->s2 = 4; s->wait[1] = 0; }
    }
    for (s->acc += PPG_CTL / PPG_FS; s->acc >= PPG_TICK; s->acc -= PPG_TICK) {
        ppg_adsr_pass(&s->lv[0], &s->s1, &s->wait[0], gate, r->ia1, r->id1, r->sus1, r->ir1, r->swell1);
        ppg_adsr_pass(&s->lv[1], &s->s2, &s->wait[1], gate, r->ia2, r->id2, r->sus2, r->ir2, r->swell2);
        if (s->s3 && s->wait[2]) s->wait[2]--;
        else if (s->s3 == 1u) {
            int32_t L = r->ia3 ? s->lv[2] + ppg_eg_rate(r->ia3, 255 - s->lv[2], &s->wait[2]) : 255;
            if (L >= 255) { L = 255; s->s3 = 2; }
            s->lv[2] = (int16_t)L;
        } else if (s->s3 == 2u && r->id3 < 15u) {
            int32_t L = s->lv[2] - ppg_eg_rate(r->id3, s->lv[2], &s->wait[2]);
            if (L <= 0) { L = 0; s->s3 = 0; }
            s->lv[2] = (int16_t)L;
        }
    }
    s->e1 = (float)s->lv[0] * (1.0f / 255.0f);
    s->e2 = (float)s->lv[1] * (1.0f / 255.0f);
    s->e3 = (float)s->lv[2] * (1.0f / 255.0f);
}
/* a 2.2 voice to a key: a silent one starts afresh (its oscillators where they ran to); a sounding one, taken from
 * another key or struck again, rises from where its envelopes are */
static void ppg_hw_on(ppg_voice_t *s, const mod_voice_v3 *v, uint32_t h)
{
    if (!s->s2) {
        uint32_t ph0 = s->ph[0], ph1 = s->ph[1];
        uint8_t *b = (uint8_t *)s;
        for (uint32_t i = 0; i < sizeof *s; i++) b[i] = 0;
        if (!ph0 && !ph1) {                         /* (never played: as if it had run free, each voice elsewhere) */
            ph0 = v->ph[0] + h * 0x9E3779B9u; ph1 = v->ph[1] + h * 0x7F4A7C15u;
        }
        s->ph[0] = ph0; s->ph[1] = ph1;
        s->fresh = 1;
        s->z[0] = 1e-4f;                            /* the circuit's noise starts a resonant filter */
    }
    s->s1 = s->s2 = s->s3 = 1;
    s->wait[0] = s->wait[1] = s->wait[2] = 0;
    s->lv[2] = 0; s->e3 = 0.0f;
    s->acc = PPG_TICK;                              /* the first pass at once: a key is heard in the block it lands */
    s->trig = 1;
}
static uint32_t ppg_units_of(uint32_t mask, uint32_t per) { return per ? (uint32_t)__builtin_popcount(mask) / per : 0u; }
/* the unit a key takes on its side: round the side's units from where they stopped, a silent one first, then one no
 * key holds down, else the next in turn (the oldest) */
static uint32_t ppg_pick(const ppg_part_t *P, const mod_part_v3 *t, uint32_t side, uint32_t mask, uint32_t per)
{
    uint32_t nu = ppg_units_of(mask, per), at = P->rr[side] % nu;
    for (uint32_t pass = 0; pass < 2u; pass++)
        for (uint32_t i = 0; i < nu; i++) {
            uint32_t u = (at + i) % nu, um = ppg_unit(mask, per, u), ok = 1;
            for (uint32_t h = 0; h < PPG_HW && ok; h++) {
                if (!(um >> h & 1u) || !P->own[h]) continue;
                const mod_voice_v3 *w = ppg_v(t, P->own[h] - 1u);
                ok = pass ? !(w->active && w->gate) : !w->active || !P->hw[h].s2;
            }
            if (ok) return u;
        }
    return at;
}
/* a key on its way in POLY (Saadet's poly_key): the unit it will take, and the keys sounding in it end now (their
 * voices go on at the new key, as the 2.2's do), so the budget counts the new key in their place. A key struck again
 * keeps its own */
static void ppg_poly_key(mod_part_v3 *t, ppg_part_t *P, uint32_t note)
{
    P->want = 0;
    for (uint32_t i = 0; i < MOD3_NVOICE; i++) {
        const mod_voice_v3 *w = ppg_v(t, i);
        if (w->active && w->stage != 4u && w->note == note) return;
    }
    const ppg_patch_t *p = ppg_patch_of(t);
    uint32_t k = p->keyb > 8u ? 0u : p->keyb, side = ppg_side(p, note);
    uint32_t mask = PPG_SIDE[k][side].mask, per = PPG_SIDE[k][side].per, pick = ppg_pick(P, t, side, mask, per);
    uint32_t um = ppg_unit(mask, per, pick);
    P->want = 1; P->want_note = (uint8_t)note; P->want_unit = (uint8_t)pick;
    for (uint32_t h = 0; h < PPG_HW; h++) {
        if (!(um >> h & 1u) || !P->own[h]) continue;
        mod_voice_v3 *w = ppg_v(t, P->own[h] - 1u);
        if (!w->active) continue;
        uint32_t wh = ppg_held(P, w->index);
        P->hand |= (uint8_t)(wh & um);
        for (uint32_t x = 0; x < PPG_HW; x++) if ((wh & ~um) >> x & 1u) P->own[x] = 0;   /* (a KEYB changed under it) */
        w->active = w->gate = 0;                    /* (as a voice that ended: voice.c's done) */
        w->stage = 0;
        w->env = 0;
    }
}
/* The POLY voice for a new note (the firmware calls it before the note's voice starts): the 2.2's unit taken first
 * (ppg_poly_key), then the part's voice as voice.c chooses one: the note's own if it sounds, a free one round the
 * part, else the oldest released, else the oldest held but the lowest */
static uint32_t ppg_alloc(mod_part_v3 *t, uint32_t note)
{
    ppg_part_t *P = ppg_part(t);
    uint32_t np = mh->nvoice(t), i, best, low;
    if (P) ppg_poly_key(t, P, note);
    if (!np) return 0;
    for (i = 0; i < np; i++)
        if (ppg_v(t, i)->active && ppg_v(t, i)->note == note) return i;
    for (i = 0; i < np; i++) {
        uint32_t k = ((P ? P->vrr : 0u) + i) % np;
        if (!ppg_v(t, k)->active) {
            if (P) P->vrr = (uint8_t)((k + 1u) % np);
            return k;
        }
    }
    for (i = 0, low = np; i < np; i++)               /* the lowest held note is protected */
        if (ppg_v(t, i)->gate && (low == np || ppg_v(t, i)->note < ppg_v(t, low)->note)) low = i;
    for (i = 0, best = np; i < np; i++) {            /* the oldest released (not one fading out) */
        const mod_voice_v3 *w = ppg_v(t, i);
        if (!w->gate && w->stage != 4u && (best == np || w->age < ppg_v(t, best)->age)) best = i;
    }
    if (best == np)
        for (i = 0; i < np; i++)                     /* the oldest held, not the bass */
            if (i != low && (best == np || ppg_v(t, i)->age < ppg_v(t, best)->age)) best = i;
    return best == np ? 0u : best;
}
static void ppg_note_on(mod_part_v3 *t, mod_voice_v3 *v)
{
    ppg_part_t *P = ppg_part(t);
    if (!P) return;
    const ppg_patch_t *p = ppg_patch_of(t);
    uint32_t vi = v->index, k = p->keyb > 8u ? 0u : p->keyb, side = ppg_side(p, v->note);
    uint32_t mask = PPG_SIDE[k][side].mask, per = PPG_SIDE[k][side].per, held = ppg_held(P, vi);
    uint32_t want = P->want && P->want_note == v->note;
    P->want = 0;
    if (*mh->was && held && (held & ~mask) == 0u && (uint32_t)__builtin_popcount(held) == per) {
        for (uint32_t h = 0; h < PPG_HW; h++) if (held >> h & 1u) ppg_hw_on(&P->hw[h], v, h);   /* struck again */
        P->hand = 0;
        return;
    }
    for (uint32_t h = 0; h < PPG_HW; h++) if (held >> h & 1u) P->own[h] = 0;   /* (a voice reused: what it held is free) */
    uint32_t nu = ppg_units_of(mask, per), pick = want ? P->want_unit % nu : ppg_pick(P, t, side, mask, per);
    P->rr[side] = (uint8_t)((pick + 1u) % nu);
    uint32_t um = ppg_unit(mask, per, pick);
    for (uint32_t h = 0; h < PPG_HW; h++) {
        if (!(um >> h & 1u)) continue;
        const mod_voice_v3 *w = P->own[h] ? ppg_v(t, P->own[h] - 1u) : 0;
        if ((!w || !w->active) && !(P->hand >> h & 1u))
            P->hw[h].s2 = 0;                        /* (no key sounding it: whatever it held has ended or was cut) */
        P->own[h] = (uint8_t)(vi + 1u);
        P->grp[h] = (uint8_t)ppg_hw_group(p, h);
        ppg_hw_on(&P->hw[h], v, h);
    }
    P->hand = 0;
}
/* a key ends when it holds no 2.2 voice (another key took them) or, its key up, all it holds have ended */
static int ppg_done(mod_part_v3 *t, mod_voice_v3 *v)
{
    ppg_part_t *P = ppg_part(t);
    if (!P) return 1;
    uint32_t held = ppg_held(P, v->index);
    if (!held) return 1;
    if (v->gate) return 0;
    for (uint32_t h = 0; h < PPG_HW; h++) if ((held >> h & 1u) && P->hw[h].s2 != 0u) return 0;
    return 1;
}

/* a wave number of the part's 128 (UW starts in the upper half; past the end a sweep runs into the other half) */
static inline uint32_t ppg_wave(uint32_t uw, float x)
{
    int32_t w = (int32_t)(x < 0.0f ? x - 0.5f : x + 0.5f);   /* (rounded; it wraps either way, as the OS's byte does) */
    return (uint32_t)((uw ? 64 : 0) + w) & 127u;
}
static void ppg_render_hw(mod_part_v3 *t, mod_voice_v3 *v, int32_t *out, uint32_t n, const mod_vmod_v3 *m, ppg_part_t *P,
                          uint32_t h)
{
    ppg_voice_t *s = &P->hw[h];
    const ppg_render_t *r = &P->r[P->grp[h] & 1u];
    ppg_env_tick(s, r, v->gate);
    float at = (float)t->at * (1.0f / 127.0f), vel = (float)v->vel * (1.0f / 127.0f);
    float lfo = r->lfo1 * (r->tm ? r->depth * at : r->depth);   /* the OS's LFO byte less its centre: +-112 at full depth */
    /* BD: 1 pitch, 2 filter, 3 waves, 4 SUB's pitch, 5 pitch and filter, 6 pitch and waves, 7 filter and waves */
    float bend_p = r->bd == 1u || r->bd == 5u || r->bd == 6u ? r->bend_st : 0.0f;
    float bend_f = r->bd == 2u || r->bd == 5u || r->bd == 7u ? r->bend : 0.0f;
    float bend_w = r->bd == 3u || r->bd == 6u || r->bd == 7u ? r->bend : 0.0f;
    float bend_s = r->bd == 4u ? r->bend_st : bend_p;
    /* the waves, as the OS sums them: OSC, ENV 1 times its depth (/256), the keys counting DOWN from the lowest key
     * (KW / 4 a key), touch, the LFO and the bender (the last three not yet measured against the OS) */
    float kos = fm_clampf((float)v->note - 35.0f, 1.0f, 96.0f);   /* the OS's key number: the lowest C is 1 */
    float sweep = s->e1 * r->e1w - kos * r->kw + (r->tw ? at * 63.0f : 0.0f) + (r->mw ? lfo : 0.0f) + bend_w * 8.0f;
                                                     /* (MW: the LFO byte's offset, as the OS's 7-bit wave register takes it; the bender +-8) */
    s->pend[0] = (uint8_t)ppg_wave(r->uw, (float)r->osc + sweep);
    /* SUB: 0 PARA (OSC + SUB, sweeping with OSC), 1 FIXED (SUB alone), 2 ENV 3 (OSC + SUB, ENV 3 sweeping it), 3 OFF (as
     * PARA; only its sound is off) */
    float subw = r->sw == 1u ? (float)r->sub : r->sw == 2u ? (float)r->osc + (float)r->sub + s->e3 * r->e3sub :
                 (float)r->osc + (float)r->sub + sweep;
    s->pend[1] = (uint8_t)ppg_wave(r->uw, subw);
    if (r->smp) {                                    /* TABLE 31: OSC (with the waves' other terms, not ENV 1's) is where
                                                      * a struck key starts both oscillators, a 128-byte stretch of 64 */
        if (s->trig) s->cur[0] = s->cur[1] = (uint8_t)(ppg_wave(r->uw, (float)r->osc + sweep - s->e1 * r->e1w) & 63u);
    } else if (s->fresh) { s->cur[0] = s->pend[0]; s->cur[1] = s->pend[1]; }
    s->trig = 0;
    /* the pitches: SEMI, ENV 3 (EO, ES), the vibrato (MO, MS), the bender, SUB's DETU */
    float e3 = s->e3 * r->e3pitch, vib = r->mp ? lfo * (1.66f / 100.0f) : 0.0f;   /* (1.66 cents a unit, the OS's) */
    int32_t semi = r->semi[h];
    int32_t p0 = m->pitch16 + semi * 16 + (int32_t)(((r->e3to & 1u) ? e3 : 0.0f) * 16.0f + ((r->mp & 1u) ? vib : 0.0f) * 16.0f + bend_p * 16.0f);
    int32_t p1 = m->pitch16 + semi * 16 + r->sub16 +
                 (int32_t)(((r->e3to & 2u) ? e3 : 0.0f) * 16.0f + ((r->mp & 2u) ? vib : 0.0f) * 16.0f + bend_s * 16.0f);
    uint32_t inc0 = md_cents_inc(p0, 0, m->fine), inc1 = md_cents_inc(p1, r->sub_ct, m->fine);
    /* the filter, summed as the 2.2's OS sums it, in its 8-bit filter units: CUTOFF, ENV 1 times its depth, the keys
     * counted up from the lowest key (the 2.2 tracks upwards only), touch and velocity at twice their value, the LFO
     * and the bender; held in 0..255. KF 3 tracks 1:1, so 20.25 units are an octave (the SSM2044's CV network on the
     * service schematic agrees: about 2.9 octaves a volt). Where the scale sits is the factory trim's: program 74
     * group B (CUTOFF 16, KF 3) is trimmed so the cutoff equals the note's fundamental on the upper keys (service
     * manual, 3.2), which puts F 64 at the lowest key's C, 65.41 Hz. Then the common modulation, in octaves */
    float keys = kos;
    float F = r->cut + s->e1 * r->e1f + keys * r->kf + (r->tf ? 2.0f * (float)t->at : 0.0f) +
              (r->vf ? 2.0f * (float)v->vel : 0.0f) + (r->mf ? lfo : 0.0f) + bend_f * 64.0f;   /* (MF: the LFO byte added as it is) */
    float oct = 3.0315f + (fm_clampf(F, 0.0f, 255.0f) - 64.0f) * (1.0f / 20.25f) + (float)m->cutoff * (11.1f / (127.0f * 256.0f));
                                                     /* (octaves above 8 Hz: 65.41 Hz is 3.0315) */
    float g1 = ppg_coeff(oct);
    if (s->fresh) { s->g = g1; s->fresh = 0; }
    /* the loudness: ENV 2 times LOUDN., the keys (KL 4 flat), touch, velocity and the tremolo */
    /* KL as the OS scales it: below 4 the high keys quieter, by (4 - KL) / 8 at the top; above 4 the low keys, by
     * (KL - 4) / 6 at the bottom; linear across the 61 keys */
    float pos = fm_clampf((kos - 1.0f) * (1.0f / 60.0f), 0.0f, 1.0f), gain = r->level;
    if (r->kl < 4u) gain *= 1.0f - (float)(4u - r->kl) * 0.125f * pos;
    else if (r->kl > 4u) gain *= 1.0f - (float)(r->kl - 4u) * (1.0f / 6.0f) * (1.0f - pos);
    if (r->tl) gain *= fm_minf(1.0f, ((float)t->at + 8.0f) * (1.0f / 64.0f));   /* the OS's: (touch + 8) x 4 / 256 */
    if (r->vl) gain *= vel;
    if (r->ml) gain *= (157.5f + 61.5f * r->lfo_raw) * (1.0f / 256.0f);   /* the OS's tremolo byte (96..219), no wheel or delay */
    float a1 = s->e2 * gain;
    const ppg_kernel_t c = {
        P->wave[s->cur[0]], P->wave[s->pend[0]], P->wave[s->cur[1]], P->wave[s->pend[1]], &P->wave[0][0], r->smp, r->loop,
        inc0, inc1,
        r->sw == 3u ? 0.0f : 1.0f, s->g, g1 - s->g, s->amp, a1 - s->amp,
        (float)m->amp0 * (1.0f / 32768.0f), (float)(m->amp1 - m->amp0) * (1.0f / 32768.0f), r->k,
        2.0f * (float)MOD3_VOICE_FS, ppg_dith_row((uint32_t)m->amp1)
    };
    s->g = g1; s->amp = a1;
    void (*volatile run)(ppg_voice_t *, int32_t *, uint32_t, const ppg_kernel_t *) = ppg_samples;
    run(s, out, n, &c);
}
static void ppg_render(mod_part_v3 *t, mod_voice_v3 *v, int32_t *out, uint32_t n, const mod_vmod_v3 *m)
{
    ppg_part_t *P = ppg_part(t);
    if (!P) return;
    uint32_t held = ppg_held(P, v->index);
    for (uint32_t h = 0; h < PPG_HW; h++) {
        if (!(held >> h & 1u)) continue;
        if (!v->active && !m->amp1) {               /* the block after the end: fade the last sample out, let it go */
            ppg_voice_t *s = &P->hw[h];
            if (n > 1u) {
                int32_t step = s->last_pcm / (int32_t)(n - 1u);
                for (uint32_t i = 0; i < n - 1u; i++) out[i] += s->last_pcm - step * (int32_t)i;
            }
            s->last_pcm = 0;
            P->own[h] = 0;
            continue;
        }
        ppg_render_hw(t, v, out, n, m, P, h);
    }
}

/* ------------------------------------------------------------- the part --- */
static float ppg_tri(uint32_t ph)
{
    float x = (float)ph * (1.0f / 2147483648.0f);   /* 0..2 */
    return x < 1.0f ? x * 2.0f - 1.0f : 3.0f - x * 2.0f;
}
static float ppg_lfo_shape(uint32_t shape, uint32_t ph)   /* the four WAVESHAPEs, named 1..4 on the pages */
{
    float x = (float)ph * (1.0f / 2147483648.0f) - 1.0f;   /* -1..1 */
    /* the OS's: 1 a triangle, 2 a rising saw, 3 a falling saw, 4 a square between the centre and the top */
    return shape == 0u ? ppg_tri(ph) : shape == 1u ? x : shape == 2u ? -x : (ph & 0x80000000u ? 1.0f : 0.0f);
}
/* the bender in this part's own reach, -1..1 (the common bend is held at 0: ppg_bend_range) */
static float ppg_bend(const mod_part_v3 *t) { return (float)t->bend_raw * (1.0f / 8192.0f); }
static int32_t ppg_bend_range(const mod_part_v3 *t) { (void)t; return 0; }
static void ppg_block_group(mod_part_v3 *t, ppg_part_t *P, uint32_t g, const uint8_t *p)
{
    ppg_render_t *r = &P->r[g];
    /* DELAY fades the depth in from the key, linearly, over about 3.9 x 2^(DELAY / 3) passes (measured on the OS) */
    float delay = (p[PG_DELAY] & 63u) >= 4u ? 3.9f * fm_exp2f((float)((p[PG_DELAY] & 63u) >> 2) * (1.0f / 3.0f)) * PPG_TICK * PPG_FS : 0.0f;
    float fade = delay <= 0.0f || p[PG_TM] ? 1.0f : fm_clampf((float)P->delay_n / delay, 0.0f, 1.0f);
    uint32_t rate = (p[PG_RATE] & 63u) >> 1;                 /* the OS's: 0.501 Hz a step from 0.28 Hz; 0 stands */
    float hz = rate ? 0.28f + 0.501f * (float)rate : 0.0f;
    P->lfo[g] += (uint32_t)(hz * (4294967296.0f / PPG_FS)) * MOD3_CTL;
    r->lfo0 = P->lfo_out[g];
    r->lfo_raw = ppg_lfo_shape(p[PG_SHAPE] & 3u, P->lfo[g]);
    r->lfo1 = P->lfo_out[g] = r->lfo_raw * fade;
    /* depth: the stored WHEEL (or the live wheel, where it is higher) scales the LFO's swing, 3.6 units a step of 32 */
    uint32_t wheel = (p[PG_WHEEL] & 63u) >> 1, live = (uint32_t)t->mw >> 2;
    r->depth = 3.6f * (float)(wheel > live ? wheel : live);
    r->ia1 = (uint8_t)((p[PG_A1] & 63u) >> 2); r->id1 = (uint8_t)((p[PG_D1] & 63u) >> 1); r->ir1 = (uint8_t)((p[PG_R1] & 63u) >> 1);
    r->ia2 = (uint8_t)((p[PG_A2] & 63u) >> 2); r->id2 = (uint8_t)((p[PG_D2] & 63u) >> 1); r->ir2 = (uint8_t)((p[PG_R2] & 63u) >> 1);
    r->sus1 = (int16_t)((p[PG_S1] & 63u) * 4u); r->sus2 = (int16_t)((p[PG_S2] & 63u) * 4u);
    r->swell1 = (uint8_t)(p[PG_A1] > 48u); r->swell2 = (uint8_t)(p[PG_A2] > 48u);
    r->ia3 = (uint8_t)((p[PG_A3] & 63u) >> 2); r->id3 = (uint8_t)((p[PG_D3] & 63u) >> 2);
    int32_t amt = (int32_t)(p[PG_E3AMT] & 63u) - PG_E3AMT0;
    r->e3pitch = (float)amt * 0.062f;                          /* the OS's: linear, +-2 semitones at the ends */
    r->e3sub = (float)((p[PG_E3AMT] & 63u) >> 2) * (63.0f / 15.0f);
    r->e3to = p[PG_E3TO] & 3u;
    r->cut = (float)(p[PG_CUT] & 63u) * 4.0f;                   /* the OS: twice the scaled knob (0..252) */
    /* EMPHASIS reaches the SSM2044 as 4 x its knob; the factory trim sets program 74 group A's 49 just short of
     * self-oscillation (service manual, 3.1). The ladder here oscillates from k about 3.9, so 49 gives 97 % of that and
     * more than about 50 whistles */
    r->k = (float)(p[PG_EMPH] & 63u) * (3.9f * 0.97f / 49.0f);
    r->e1f = (float)(p[PG_E1F] & 63u) * 4.0f * (255.0f / 256.0f);   /* ENV 1 (0..255) times twice the scaled depth / 256 */
    r->kf = (float)(p[PG_KF] & 7u) * (9.0f / 16.0f);           /* the OS: key x KF x 36 x 4 / 256 */
    r->e1w = (float)(p[PG_E1W] & 63u) * (255.0f / 256.0f);      /* the OS's: ENV 1 (0..255) x the depth / 256 */
    r->kw = (float)(p[PG_KW] & 7u) * 0.25f;                     /* the OS's: a wave down every 4 / KW keys */
    r->level = p[PG_TABLE] == PG_SAMPLE && !PPG_HAVE_SAMPLE ? 0.0f : (float)((p[PG_LOUD] & 63u) >> 1) * (0.5f / 31.0f);
                                                     /* (TABLE 31 without the natural sounds' data: silent) */
    /* TABLE 31: ENV 1 WAVES is the stretch the sound ends on and loops, as the OS writes it: twice the knob, and 3 */
    r->smp = (uint8_t)(p[PG_TABLE] == PG_SAMPLE && PPG_HAVE_SAMPLE);
    r->loop = (uint8_t)((2u * (p[PG_E1W] & 63u) + 3u) & 63u);
    r->kl = p[PG_KL] & 7u;
    static const int8_t DETU_CT[8] = {0, 2, 5, 12, 25, 2, 6, 6};   /* the OS's SUB offsets (cents above the semitones) */
    static const int8_t DETU_ST[8] = {0, 0, 0, 0, 0, 7, 12, 24};
    r->sub_ct = DETU_CT[p[PG_DETU] & 7u];
    r->sub16 = DETU_ST[p[PG_DETU] & 7u] * 16;
    for (uint32_t h = 0; h < PPG_HW; h++) r->semi[h] = (int8_t)((int32_t)(p[pg_semi_id(h)] % 49u) - PG_SEMI0);
    static const int8_t BI[4] = {2, 4, 7, 12};
    r->bi = (uint8_t)BI[p[PG_BI] & 3u];
    r->bend = ppg_bend(t);
    r->bend_st = r->bend * (float)r->bi;
    r->bd = p[PG_BD] & 7u;
    r->osc = p[PG_OSC] & 63u; r->sub = p[PG_SUB] & 63u; r->sw = p[PG_SW] & 3u; r->uw = (uint8_t)(p[PG_UW] || p[PG_TABLE] == PG_UPPER);
    r->mp = p[PG_MP] & 3u; r->mw = p[PG_MW] & 1u; r->mf = p[PG_MF] & 1u; r->ml = p[PG_ML] & 1u;
    r->tw = p[PG_TW] & 1u; r->tf = p[PG_TF] & 1u; r->tl = p[PG_TL] & 1u; r->tm = p[PG_TM] & 1u;
    r->vf = p[PG_VF] & 1u; r->vl = p[PG_VL] & 1u;
}
static void ppg_block(mod_part_v3 *t)
{
    ppg_part_t *P = ppg_part(t);
    if (!P) return;
    const ppg_patch_t *pp = ppg_patch_of(t);
    uint32_t table = pp->raw[PG_TABLE];
    if (table > PG_SAMPLE || (table == PG_SAMPLE && !PPG_HAVE_SAMPLE)) table = 0;
    ppg_load_table(P, table);                        /* (one wavetable a program, as the 2.2's) */
    uint32_t keys = 0;
    for (uint32_t i = 0; i < MOD3_NVOICE; i++) keys |= ppg_v(t, i)->gate;
    if (keys && !P->keys) P->delay_n = 0;           /* the delay restarts with a key after all were up */
    P->keys = (uint8_t)!!keys;
    if (P->delay_n < 0x7FFFFFFFu) P->delay_n += MOD3_CTL;
    ppg_block_group(t, P, 0, pp->raw);
    ppg_block_group(t, P, 1, pp->rawb);
}
/* the keys KEYB holds at once: its sides' units (8, 4, 2, 1, 8, 5, 7, 7, 2) */
static uint32_t ppg_cap(const mod_part_v3 *t)
{
    const ppg_patch_t *p = ppg_patch_of(t);
    uint32_t k = p->keyb > 8u ? 0u : p->keyb;
    return ppg_units_of(PPG_SIDE[k][0].mask, PPG_SIDE[k][0].per) + ppg_units_of(PPG_SIDE[k][1].mask, PPG_SIDE[k][1].per);
}
/* A key's budget, from the 2.2 voices the part's keys hold (2 units a voice: two table oscillators and one filter, as a
 * JUNO-106's voice; provisional, not measured on hardware). The firmware counts a part as its keys times this, and a
 * new key as one more: so it is the most of the voices held a key and of those held with what a next key can add (a
 * free unit of a side), shared with it, each rounded up. Then the keys' claim covers what they render, and the claim
 * with one more key what that key can add. Where KEYB's keys cost alike (0..4, 8) it is exact; a split with a
 * monophonic side (5..7) claims more than it renders, so with a full budget its last key may wait for a fade */
static uint32_t ppg_units(const mod_part_v3 *t)
{
    const ppg_patch_t *p = ppg_patch_of(t);
    ppg_part_t *P = ppg_part(t);
    uint32_t k = p->keyb > 8u ? 0u : p->keyb, keys = 0, hw = 0, busy = 0, most = 0;
    for (uint32_t i = 0; i < MOD3_NVOICE; i++) keys += ppg_v(t, i)->active && ppg_v(t, i)->stage != 4u;
    if (P)
        for (uint32_t h = 0; h < PPG_HW; h++) {
            const mod_voice_v3 *w = P->own[h] ? ppg_v(t, P->own[h] - 1u) : 0;
            uint32_t on = w && w->active && w->stage != 4u;
            hw += on;
            busy |= on << h;
        }
    for (uint32_t side = 0; side < 2u; side++) {      /* what a next key can add: a free unit's voices */
        uint32_t mask = PPG_SIDE[k][side].mask, per = PPG_SIDE[k][side].per;
        for (uint32_t u = 0; u < ppg_units_of(mask, per); u++)
            if (!(ppg_unit(mask, per, u) & busy) && per > most) most = per;
    }
    uint32_t after = hw + most < PPG_HW ? hw + most : PPG_HW;
    uint32_t a = keys ? (2u * hw + keys - 1u) / keys : 0u, b = (2u * after + keys) / (keys + 1u);
    return a > b ? a : b;
}

/* ------------------------------------------------------ the panel values --- */
/* Every panel value is a MOD3_SK_CALL parameter, by Saadet's ids (ppg_patch.h PG_*, PGX_*): a group's fields read and
 * edit the group GROUP selects (the 2.2's A/B button); TABLE is the program's, both groups'. SEMI and ENV 3's AMOUNT
 * show signed; the patch keeps them offset */
static const int8_t PPG_SHOWN_MIN[PGX_N] = {[PG_SEMI] = -24, [PG_E3AMT] = -32, [PG_SEMI2 ... PG_SEMI8] = -24,
                                             [PGX_SPLIT] = PG_SPLIT0, [PGX_V1] = -24};
static const int8_t PPG_SHOWN_MAX[PGX_N] = {[PG_SEMI] = 24, [PG_E3AMT] = 31, [PG_SEMI2 ... PG_SEMI8] = 24,
                                             [PGX_SEL] = 1, [PGX_KEYB] = 8, [PGX_SPLIT] = PG_SPLIT0 + 60, [PGX_V1] = 24};
static int32_t ppg_pget(const mod_part_v3 *t, const uint8_t *patch, uint32_t id)
{
    (void)t;
    const ppg_patch_t *pp = (const ppg_patch_t *)(const void *)patch;
    const uint8_t *p = ppg_group_c(pp, pp->sel);
    if (id == PGX_SEL) return pp->sel;
    if (id == PGX_KEYB) return pp->keyb;
    if (id == PGX_SPLIT) return pp->split + PG_SPLIT0;
    if (id == PGX_V1) id = PG_SEMI;
    if (id >= PG_RAW) return 0;
    return pg_is_semi(id) ? (int32_t)p[id] - PG_SEMI0 : id == PG_E3AMT ? (int32_t)p[id] - PG_E3AMT0 : (int32_t)p[id];
}
static int ppg_pput(mod_part_v3 *t, uint8_t *patch, uint32_t id, int32_t value)
{
    (void)t;
    if (id >= PGX_N) return 0;
    ppg_patch_t *pp = (ppg_patch_t *)(void *)patch;
    uint8_t before[PPG_KEYB + 3u];
    for (uint32_t i = 0; i < sizeof before; i++) before[i] = patch[i];
    int32_t lo = PPG_SHOWN_MIN[id], hi = PPG_SHOWN_MAX[id] ? PPG_SHOWN_MAX[id] : id < PG_RAW ? PG_MAX[id] : 0;
    if (id == PGX_SPLIT) hi = PG_SPLIT0 + 60;                 /* (an int8_t holds 96) */
    value = md_clamp(value, lo, hi);
    uint8_t *g = ppg_group(pp, pp->sel);
    if (id == PGX_SEL) pp->sel = (uint8_t)value;
    else if (id == PGX_KEYB) pp->keyb = (uint8_t)value;
    else if (id == PGX_SPLIT) pp->split = (uint8_t)(value - PG_SPLIT0);
    else if (id == PG_SEMI) {                        /* TUNE's SEMI: every voice by as much as voice 1 moves */
        int32_t d = value + PG_SEMI0 - (int32_t)g[PG_SEMI];
        for (uint32_t h = 0; h < 8u; h++) g[pg_semi_id(h)] = (uint8_t)md_clamp((int32_t)g[pg_semi_id(h)] + d, 0, 48);
    } else {
        if (id == PGX_V1) id = PG_SEMI;
        if (pg_is_semi(id)) value += PG_SEMI0;
        if (id == PG_E3AMT) value += PG_E3AMT0;
        if (id == PG_TABLE) pp->raw[id] = pp->rawb[id] = (uint8_t)value;   /* (the program's one wavetable) */
        else g[id] = (uint8_t)value;
    }
    for (uint32_t i = 0; i < sizeof before; i++) if (before[i] != patch[i]) return 1;
    return 0;
}
/* hints, as Saadet's: a control whose source or destination is off is dimmed, still editable */
static int ppg_pactive(const mod_part_v3 *t, const uint8_t *patch, uint32_t id)
{
    (void)t;
    const ppg_patch_t *pp = (const ppg_patch_t *)(const void *)patch;
    const uint8_t *p = ppg_group_c(pp, pp->sel);
    int sub = p[PG_SW] != 3u, lfo = p[PG_MP] || p[PG_MW] || p[PG_MF] || p[PG_ML];
    switch (id) {
    case PG_SUB: case PG_DETU: return sub;
    case PG_A3: case PG_D3: case PG_E3AMT: return p[PG_E3TO] || p[PG_SW] == 2u;
    case PG_RATE: case PG_SHAPE: case PG_DELAY: case PG_WHEEL: return lfo;
    case PG_BI: return p[PG_BD] == 1u || (p[PG_BD] >= 4u && p[PG_BD] <= 6u);
    case PGX_SPLIT: return pp->keyb >= 4u;
    default: return 1;
    }
}

/* ---------------------------------------------------------------- engine --- */
static const mod_engine_v3 ENG_PPG = {
    .name = "PPG", .page_title = {"WAVES", "FILTER"},
    .edit = {{"E1", MOD3_F_INT, -63, 63, 0, 0, 0}, {"E2", MOD3_F_INT, -63, 63, 0, 0, 0},
             {"E3", MOD3_F_INT, -63, 63, 0, 0, 0}, {"E4", MOD3_F_INT, -63, 63, 0, 0, 0},
             {"E5", MOD3_F_INT, -63, 63, 0, 0, 0}, {"E6", MOD3_F_INT, -63, 63, 0, 0, 0},
             {"E7", MOD3_F_INT, -63, 63, 0, 0, 0}, {"E8", MOD3_F_INT, -63, 63, 0, 0, 0}},
    .npresets = PPG_NPRESETS, .poly = PPG_POLY, .ownenv = 1, .caps = MOD3_CAP_WHEEL,
    .knob = {MOD3_P_E0, MOD3_P_E0 + 1, MOD3_P_E0 + 2, MOD3_P_E0 + 3},
    .note_on = ppg_note_on, .render = ppg_render, .done = ppg_done, .block = ppg_block, .alloc = ppg_alloc,
    .units = ppg_units, .cap = ppg_cap, .state = ppg_state, .bend_range = ppg_bend_range,
    .pget = ppg_pget, .pput = ppg_pput, .pactive = ppg_pactive,
};

#if defined(MOD_HOSTED) && !defined(MOD_IN_CORE)  /* (the schema: the builder's host build writes the SCHM chunk) */
#include "ppg_schema.h"
MOD3_SCHEMA(PPG_SCHEMA);
#endif
MOD3_IDENT("Saadet", "ppg-wave", "PPG");          /* its ID: its author key and "ppg-wave" (mod_abi3.h) */
MOD3_EXPORT(mod_ppg) = {
    MOD3_SDK_HEAD,
    .engine = 24,                                 /* the number it asks for (a device gives the first free one if taken) */
    .name = "PPG",
    .npresets = PPG_NPRESETS,
    .eng = &ENG_PPG,
};
