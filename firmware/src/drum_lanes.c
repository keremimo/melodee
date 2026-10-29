/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* KIT CR78 (drum_cr78.c) and KIT FM (drum_fm.c): a whole kit on the drum track's one voice, as KIT SID plays it.
 * Eight lanes, as the 808's: a lane is one drum at a time, its next hit takes it over. A new drum's start would
 * step from the old one's last sample: that step eases out over ~1 ms under the new hit, so a retrigger never
 * clicks. A closed hat chokes the open one, a recorded hit's length ends its drum; both fade over 2.9 ms.
 * The kits' drums are their own sample loops; the lanes, a hit's level (velocity and ACC at the hit, the lane
 * level and the HIT page's LEVEL live, ramped over a block), DRV and the output stage are the same for both and
 * match the 808's: a CR78 or FM drum sits where the 808's does in a mix. */
#define DLK_EASE 5                               /* a retrigger's step eases out: >> 5 a sample (0.7 ms) */
#define DLK_FADE 128                             /* a choked open hat, a recorded hit's end: samples of fade */
#define DLK_HIT_V 1742                           /* the trigger level a velocity step (the 808's: 13.5 V at 127) */
#define DLK_NONE 0u                              /* a drum's noise: none, high-passed, band-passed */
#define DLK_HP 1u
#define DLK_BP 2u
#define DLK_GAP 441                              /* a clap's bursts: one every 10 ms */

typedef struct {
    uint32_t t;                                  /* samples since the hit */
    int32_t v;                                   /* its trigger level */
    int32_t gain, g;                             /* the hit's level (velocity, ACC: Q15); the lane's gain now */
    int32_t left, fade;                          /* a recorded hit's samples to its end; samples of fade left */
    int32_t last, ease;                          /* the lane's last sample; a retrigger's step, easing out */
    int32_t px;                                  /* the trigger pulse's shaper (CR78) */
    int32_t f[2], q[2], lp[2], bp[2];            /* resonators (CR78) */
    uint32_t ph[2], inc[2];                      /* oscillators: squares (CR78), carrier and modulator (FM) */
    int32_t k[2];                                /* CR78: the networks' levels (Q16); FM: the index (phase per Q15
                                                  * of the modulator), the sweep (Q16) */
    int32_t m1, m2;                              /* FM: the modulator's last two outputs (feedback) */
    int32_t ng, rise, rstep, burst;              /* the noise's level (Q12), attack (Q15) and its step; a clap's next burst */
    int32_t tk, ts;                              /* TONE: a one-pole low-pass (Q14), its state */
    dre_t e[4];                                  /* envelopes: the noise's, then the kit's */
    drf_t nf, sf;                                /* the noise's filter; the squares' (CR78), the output's (FM) */
    int32_t ns[2], ss[2];                        /* their states */
    int32_t nst;                                 /* the noise generator */
    uint8_t sound, rec, on, level;               /* the drum (DRUM_SOUND_NAME order), its recipe, LEVEL */
} dlk_ch_t;
struct dlk_part {                                /* (dlk_part_t: eng_drum.c) */
    dlk_ch_t ch[DV_NLANE];
    uint8_t ready, kit;                          /* the kit the lanes play: another kit's start from rest */
};
_Static_assert(((sizeof(dlk_part_t) + 3u) & ~3u) != ((sizeof(drum909_part_t) + 3u) & ~3u) &&
               ((sizeof(dlk_part_t) + 3u) & ~3u) != ((sizeof(dsk_part_t) + 3u) & ~3u) &&
               ((sizeof(dlk_part_t) + 3u) & ~3u) != ((sizeof(drum_lane_t) * DV_NLANE + 3u) & ~3u),
               "the lane kits' state: its own size in the DRUM pool (resource_get keys on it)");
static dlk_part_t *dlk_part(uint32_t part);   /* engines.c: the part's runtime pool */
static uint32_t dlk_bytes(void) { return sizeof(dlk_part_t); }

/* a hit's knobs: the part's, with the HIT page's edit of the drum's character (as the 808's: SNAP the snare's,
 * else TONE); its DECAY edit goes to the kit apart (DECY moves only some drums) */
static void dlk_values(const int16_t *p, const int8_t *pc, uint32_t sound, int16_t *q)
{
    for (uint32_t k = 0; k < 8u; k++)
        q[k] = p[P_E0 + k];
    if (sound == D9_SD)
        q[4] = (int16_t)clamp(q[4] + pc[2], 0, 127);
    else
        q[2] = (int16_t)clamp(q[2] + pc[2], 0, 127);
}

/* 2^x for a hit's setup: one copy for both kits (it lives in audio SRAM) */
static __attribute__((noinline)) float dlk_exp2(float x) { return fm_exp2f(x); }
/* a state-variable filter's coefficients at hz with quality qf (drf_bp, drf_hp) */
static drf_t dlk_svf(float hz, float qf)
{
    float g = fm_tanf(FM_PI * fm_clampf(hz, 20.0f, 0.45f * FS) / FS), k = 1.0f / qf, a1 = 1.0f / (1.0f + g * (g + k));
    drf_t c = {(int16_t)(a1 * 8192.0f), (int16_t)(g * a1 * 8192.0f), (int16_t)(g * g * a1 * 8192.0f), (int16_t)(k * 8192.0f)};
    return c;
}
static void dlk_set(dre_t *e, uint32_t ms, int32_t dec)   /* from the top, -40 dB after ms (x 2 a DECY of 24) */
{
    e->e = 1 << 24;
    e->k = dr_blk((uint32_t)(((uint64_t)DR_TAU(ms) * dr_pow2(dec * 8)) >> 16));
}

/* the drum's noise at the hit: its filter (DLK_HP, DLK_BP at hz, quality qf), decay, rise (ms) or a clap's bursts */
static void dlk_noise_set(dlk_ch_t *c, uint32_t type, float hz, float qf, uint32_t ms, uint32_t rise, uint32_t bursts,
                          int32_t dec, int32_t level)
{
    c->nf = dlk_svf(hz, type == DLK_BP ? qf : 0.71f);
    dlk_set(&c->e[0], ms, dec);
    c->ng = level;
    c->rstep = rise ? 32767 / (int32_t)DR_MS(rise) + 1 : 32767;
    c->rise = rise || bursts ? 0 : 32767;
    c->burst = 0;
}
/* the drum's noise, one block into y: risen, or a clap's bursts and their tail (as the 808's clap), on its
 * envelope; 0 once it is over */
static __attribute__((noinline)) int dlk_noise(dlk_ch_t *restrict c, int32_t *y, uint32_t n, uint32_t type,
                                               uint32_t bursts)
{
    int32_t e0, de = dre_next(&c->e[0], &e0), more = (int32_t)(bursts - 1u) * DLK_GAP;
    uint32_t i, t0 = c->t;
    for (i = 0; i < n; i++) {
        int32_t nz = (int32_t)(noise32(&c->nst) >> 17) - 16384, w = DRE_AT(e0, de, i), f;
        if (bursts) {
            int last = c->burst > more;
            if (!last && t0 + i == (uint32_t)c->burst) {
                c->rise = 32767;
                c->burst += DLK_GAP;
            }
            c->rise -= (c->rise >> (last ? 8 : 7)) + (c->rise > 0);   /* 2.9 ms, the last 5.8 ms */
            w = c->rise + ((w * 9000) >> 15);
        } else {
            if (c->rise < 32767)
                c->rise = c->rise + c->rstep > 32767 ? 32767 : c->rise + c->rstep;
            w = (w * c->rise) >> 15;
        }
        f = type == DLK_BP ? drf_bp(&c->nf, nz, c->ns) : drf_hp(&c->nf, nz, c->ns);
        y[i] += (((f * w) >> 15) * c->ng) >> 12;
    }
    return c->e[0].e >= DRE_LOW || (bursts && (c->rise > 8 || c->burst <= more));
}

#include "drum_cr78.c"
#include "drum_fm.c"

static dlk_part_t *dlk_of(const track_t *t)
{
    dlk_part_t *K = dlk_part((uint32_t)(t - trk));
    if (K && (!K->ready || K->kit != t->p[P_E0])) {
        __typeof__(&memset) volatile clear = memset;
        clear(K, 0, sizeof *K);
        for (uint32_t k = 0; k < DV_NLANE; k++)
            K->ch[k].nst = 0x2F6E2B1 + (int32_t)(k * 0x9E3779B9u);
        K->kit = (uint8_t)t->p[P_E0];
        K->ready = 1;
    }
    return K;
}

/* a hit on note at velocity vel: its lane's drum */
static void dlk_hit(track_t *t, uint32_t note, uint32_t vel)
{
    dlk_part_t *K = dlk_of(t);
    int32_t st, tune;
    int16_t q[8];
    if (!K)
        return;
    uint32_t lane = DV_TYPE_LANE[drum_gm(t->p, note, &st)], sound = drum_sound_of(t, note) & 15u;
    const int8_t *pc = drum_patch_of(t - trk)->c[sound];
    dlk_ch_t *c = &K->ch[lane];
    if (lane == DV_HATC && K->ch[DV_HATO].on && !K->ch[DV_HATO].fade)
        K->ch[DV_HATO].fade = DLK_FADE;
    dlk_values(t->p, pc, sound, q);
    tune = clamp((st + pc[0] + drum_event_pitch) * 16 + (q[1] - 64) * 3, -576, 576);
    c->gain = dr8_level(vel, q[5]);
    c->level = (uint8_t)(127 - pc[3]);
    c->left = (int32_t)drum_event_length;
    c->v = (int32_t)vel * DLK_HIT_V;
    /* the same drum still ringing (not fading) may be struck again as it rings; else it starts from rest */
    if (K->kit == DK_CR78 ? cr_hit(c, sound, tune, q, pc[1], c->on && !c->fade && c->sound == sound)
                          : fmk_hit(c, sound, tune, q, pc[1])) {
        c->ease = c->last;                       /* the old drum's last sample eases out */
        c->g = -1;                               /* (at its level from the first sample) */
    }
    c->fade = 0;
    c->sound = (uint8_t)sound;
    c->t = 0;
    c->on = 1;
}

/* once per control tick (drum_amp): 0 once every lane has gone quiet */
static int dlk_tick(track_t *t)
{
    dlk_part_t *K = dlk_part((uint32_t)(t - trk));
    if (!K || !K->ready || K->kit != t->p[P_E0])
        return 0;
    for (uint32_t k = 0; k < DV_NLANE; k++)
        if (K->ch[k].on || K->ch[k].ease)
            return 1;
    return 0;
}

/* the kit, one block: each lane's drum at its level, the steps easing out, DRV, the output as the 808's */
static __attribute__((noinline)) void dlk_render(track_t *t, int32_t *out, uint32_t n, const vmod_t *m,
                                                 const int16_t *p)
{
    dlk_part_t *K = dlk_part((uint32_t)(t - trk) % NPART);
    int32_t y[CTL] = {0}, x[CTL], drv = p[P_E7], g = 0, mk = 0;
    uint32_t i, k;
    if (!K || !K->ready || K->kit != p[P_E0])
        return;
    for (k = 0; k < DV_NLANE; k++) {
        dlk_ch_t *c = &K->ch[k];
        int32_t g0, g1, s = 0;
        if (!c->on && !c->ease)
            continue;
        for (i = 0; i < n; i++)
            x[i] = 0;
        if (c->on && !(K->kit == DK_CR78 ? cr_run(c, x, n) : fmk_run(c, x, n)))
            c->on = 0;
        c->t += n;
        if (c->on && c->left > 0 && (c->left -= (int32_t)n) <= 0 && !c->fade)
            c->fade = DLK_FADE;                  /* a recorded hit's end */
        g1 = mulq15(c->gain, dsk_level(p, k, c->level));
        g0 = c->g < 0 ? g1 : c->g;
        c->g = g1;
        for (i = 0; i < n; i++) {
            s = (int32_t)(((int64_t)x[i] * (g0 + (((g1 - g0) * (int32_t)i) >> CTL_LOG2))) >> 15);
            if (c->fade)
                s = c->fade > (int32_t)i ? s * (c->fade - (int32_t)i) / DLK_FADE : 0;
            s += c->ease;
            c->ease -= c->ease >> DLK_EASE;
            y[i] += s;
        }
        if (c->ease > -(1 << DLK_EASE) && c->ease < (1 << DLK_EASE))
            c->ease = 0;
        if (c->fade && (c->fade -= (int32_t)n) <= 0)
            c->fade = 0, c->on = 0;
        c->last = c->on || c->ease ? s : 0;
    }
    if (drv > 0) {                               /* DRV: x1..x4 into the soft clip, the level kept (Q12) */
        g = 4096 + drv * 3 * 4096 / 127;
        mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
    }
    for (i = 0; i < n; i++) {                    /* x1.25 and the knee below, as the 808's lanes leave */
        int32_t s = clamp(y[i], -131071, 131071);  /* (eight lanes: within DRV's 32-bit product) */
        if (g)
            s = (softclip((s * g) >> 12) * mk) >> 15;
        out[i] += voice_amp(soft_knee(s + (s >> 2), 24000), m, i) << 1;
    }
}
