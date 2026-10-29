/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* KIT CR78 (drum_lanes.c's lanes): a late-1970s preset rhythm box after the CompuRhythm CR-78's voice list, built
 * from the 808's circuit pieces (drum_808.c): bridged-T networks kicked by the trigger pulse, filtered noise, a
 * pair of square oscillators through a band-pass. Softer and shorter than the 808: a round low kick, a papery
 * snare, a hi-hat of noise alone, the percussion the box was bought for (tambourine, cowbell, conga, bongos,
 * claves, maracas) and its METAL BEAT, which HATS METAL plays on the two hat lanes. The values are designed after
 * the box's sound, not measured from a unit. TOMS: the conga's network tuned down (the box had none); CLAP: its
 * tambourine (the box had no clap either). TUNE every drum; TONE a low-pass on each (the kick's its tone); DECY
 * the kick's, the open hat's (or metal's), the cymbal's decay; SNAP the snare's noise. A network's level does not
 * follow its pitch (as the 808's toms). */
#define CR_METAL 16u                             /* the recipes after the 16 drums: METAL short, long */
typedef struct {
    uint16_t hz[2], ms[2];                       /* networks kicked by the trigger: Hz (0: none), ring (-40 dB ms) */
    uint16_t g[2];                               /* their levels at 100 Hz (Q16) */
    uint8_t drop;                                /* the pitch at the hit above the rest, % (falling as it rings) */
    uint8_t nf;                                  /* noise: DLK_NONE, DLK_HP, DLK_BP */
    uint16_t nhz, nq;                            /* its filter: Hz, Q x 100 */
    uint16_t nms;                                /* its decay (-40 dB ms) */
    uint8_t nrise;                               /* ms to its top */
    uint8_t dk;                                  /* DECY sets this drum's decay */
    int16_t ng;                                  /* the noise's level (Q12) */
    uint16_t sq[2], shz;                         /* two squares (Hz, 0: none), their band-pass (Hz, Q 3) */
    uint16_t sms;                                /* their decay */
    int16_t sg;                                  /* their level (Q12) */
    uint16_t lp;                                 /* TONE 64: the low-pass's Hz (16 kHz and up: none) */
    uint16_t out;                                /* the drum's level (Q12): the kit's balance */
} cr_sound_t;
static const cr_sound_t CR_SOUNDS[CR_METAL + 2u] = {
    /* KICK: one network, low and soft, no sigh */
    {{62, 0}, {260, 0}, {7200, 0}, 0, DLK_NONE, 0, 0, 0, 0, 1, 0, {0, 0}, 0, 0, 0, 900, 4096},
    /* SNARE: two networks under a long high-passed noise: the noise carries it */
    {{190, 330}, {70, 40}, {2800, 3000}, 0, DLK_HP, 1800, 71, 170, 0, 0, 2600, {0, 0}, 0, 0, 0, 12000, 4518},
    /* TOM L, M, H: the conga's network tuned down, falling a little */
    {{120, 0}, {240, 0}, {5500, 0}, 8, DLK_NONE, 0, 0, 0, 0, 0, 0, {0, 0}, 0, 0, 0, 3000, 4253},
    {{165, 0}, {200, 0}, {5500, 0}, 8, DLK_NONE, 0, 0, 0, 0, 0, 0, {0, 0}, 0, 0, 0, 3500, 4369},
    {{225, 0}, {170, 0}, {5500, 0}, 8, DLK_NONE, 0, 0, 0, 0, 0, 0, {0, 0}, 0, 0, 0, 4000, 4303},
    /* RIM: two short networks */
    {{520, 1650}, {18, 10}, {27000, 43000}, 0, DLK_NONE, 0, 0, 0, 0, 0, 0, {0, 0}, 0, 0, 0, 12000, 1341},
    /* TAMB: band-passed noise rising in 2 ms, the jingles' squares */
    {{0, 0}, {0, 0}, {0, 0}, 0, DLK_BP, 7500, 150, 220, 2, 0, 2200, {5100, 7300}, 6500, 180, 900, 16000, 5589},
    /* HAT C, HAT O: high-passed noise alone */
    {{0, 0}, {0, 0}, {0, 0}, 0, DLK_HP, 7000, 71, 45, 0, 0, 3000, {0, 0}, 0, 0, 0, 16000, 7524},
    {{0, 0}, {0, 0}, {0, 0}, 0, DLK_HP, 7000, 71, 320, 0, 1, 3000, {0, 0}, 0, 0, 0, 16000, 6595},
    /* CYMB: a band of noise, long */
    {{0, 0}, {0, 0}, {0, 0}, 0, DLK_BP, 8500, 90, 1100, 0, 1, 2600, {0, 0}, 0, 0, 0, 16000, 7510},
    /* BELL: the cowbell, two squares through a narrow band-pass */
    {{0, 0}, {0, 0}, {0, 0}, 0, DLK_NONE, 0, 0, 0, 0, 0, 0, {587, 845}, 900, 220, 2600, 8000, 3422},
    /* CONGA, BONGO L, BONGO H: short networks, the conga falling a little */
    {{200, 0}, {150, 0}, {8800, 0}, 4, DLK_NONE, 0, 0, 0, 0, 0, 0, {0, 0}, 0, 0, 0, 5000, 2581},
    {{300, 0}, {90, 0}, {12000, 0}, 0, DLK_NONE, 0, 0, 0, 0, 0, 0, {0, 0}, 0, 0, 0, 6000, 1839},
    {{420, 0}, {70, 0}, {16800, 0}, 0, DLK_NONE, 0, 0, 0, 0, 0, 0, {0, 0}, 0, 0, 0, 7000, 1615},
    /* CLAVE: a high, short network */
    {{2450, 0}, {35, 0}, {12250, 0}, 0, DLK_NONE, 0, 0, 0, 0, 0, 0, {0, 0}, 0, 0, 0, 16000, 3519},
    /* MARAC: high-passed noise rising in 12 ms */
    {{0, 0}, {0, 0}, {0, 0}, 0, DLK_HP, 6000, 71, 70, 12, 0, 8940, {0, 0}, 0, 0, 0, 16000, 4096},
    /* METAL BEAT short, long (HATS METAL): two squares ringing through a band-pass, a little noise */
    {{0, 0}, {0, 0}, {0, 0}, 0, DLK_HP, 9000, 71, 30, 0, 0, 700, {1190, 1771}, 3600, 60, 2600, 16000, 5627},
    {{0, 0}, {0, 0}, {0, 0}, 0, DLK_HP, 9000, 71, 200, 0, 1, 600, {1190, 1771}, 3600, 300, 2400, 16000, 5045},
};
/* HATS: the hat lanes' drums */
static const char *const N_DRUM_HATS[] = {"HAT", "METAL"};
static const param_desc_t DRUM_CR_HATS = {"HATS", F_ENUM, 0, 1, 0, N_DRUM_HATS, 0};

/* the trigger pulse through the 808's pulse shaper (dr_pulse) */
static inline __attribute__((always_inline)) int32_t cr_pulse(dlk_ch_t *c, uint32_t t)
{
    int32_t p = t < DR_P1 ? c->v : 0, s;
    c->px += ((p - c->px) * 6650) >> 15;
    s = p - c->px + ((p * 1442) >> 15);
    if (s < 0)
        s = s * 727 / (727 - (s >> 4));
    return s;
}

/* the lane struck: its drum's recipe at the hit's pitch and the knobs (q: P_E0..P_E7, dk: the HIT page's decay);
 * warm: the same drum rings (not fading): its networks are struck again as they ring. 1: from rest */
static int cr_hit(dlk_ch_t *c, uint32_t sound, int32_t tune, const int16_t *q, int32_t dk, int warm)
{
    uint32_t rec = (sound == D9_CH || sound == D9_OH) && q[6] ? CR_METAL + (sound == D9_OH) : sound;
    const cr_sound_t *s = &CR_SOUNDS[rec];
    int32_t dec = dk + (s->dk ? q[3] - 64 : 0);
    float r = dlk_exp2((float)tune / 192.0f), lp = (float)s->lp * dlk_exp2((float)(q[2] - 64) / 21.0f);
    warm &= c->rec == rec;
    if (!warm) {
        c->px = c->lp[0] = c->bp[0] = c->lp[1] = c->bp[1] = 0;
        c->ns[0] = c->ns[1] = c->ss[0] = c->ss[1] = c->ts = 0;
    }
    c->rec = (uint8_t)rec;
    c->gain = (int32_t)(((int64_t)c->gain * s->out) >> 12);
    for (uint32_t k = 0; k < 2u; k++)
        if (s->hz[k]) {
            c->f[k] = dr_tuned(DR_F(s->hz[k]), tune);
            c->q[k] = dr_damp(c->f[k], (uint32_t)(((uint64_t)DR_TAU(s->ms[k]) * dr_pow2(dec * 8)) >> 16));
            c->k[k] = (int32_t)(((uint64_t)s->g[k] * 100u / s->hz[k] * dr_pow2(-tune)) >> 16);
        }
    dlk_set(&c->e[1], s->ms[0] / 2u + 1u, dec);  /* (the pitch's fall) */
    if (s->nf)
        dlk_noise_set(c, s->nf, (float)s->nhz * r, (float)s->nq / 100.0f, s->nms, s->nrise, 0, dec,
                      sound == D9_SD ? s->ng * q[4] / 64 : s->ng);
    if (s->sq[0]) {
        c->sf = dlk_svf((float)s->shz * r, 3.0f);
        dlk_set(&c->e[2], s->sms, dec);
        for (uint32_t k = 0; k < 2u; k++)       /* (free-running, as the 808's) */
            c->inc[k] = (uint32_t)((float)s->sq[k] * r * 97391.55f);
    }
    c->tk = lp >= 16000.0f ? 16384 : (int32_t)clamp(dr_lp1((uint32_t)fm_clampf(lp, 40.0f, 15999.0f)), 1, 16384);
    return !warm;
}

/* the lane's drum, one block into y; 0 once it has rung out */
static __attribute__((noinline)) int cr_run(dlk_ch_t *restrict c, int32_t *y, uint32_t n)
{
    const cr_sound_t *s = &CR_SOUNDS[c->rec];
    uint32_t i, t0 = c->t;
    int live = t0 < DR_PEND;
    if (s->hz[0]) {                              /* the networks: struck by the pulse, ringing on */
        int32_t d0, f0 = c->f[0], pk = 0;
        dre_next(&c->e[1], &d0);
        if (s->drop)
            f0 += (int32_t)(((int64_t)f0 * (((d0 >> 9) * s->drop) / 100)) >> 15);
        for (i = 0; i < n; i++) {
            int32_t x = t0 + i < DR_PEND ? cr_pulse(c, t0 + i) * 16 : 0, r;
            r = (int32_t)(((int64_t)dr_res(&c->lp[0], &c->bp[0], f0, c->q[0], x) * c->k[0]) >> 16);
            if (s->hz[1])
                r += (int32_t)(((int64_t)dr_res(&c->lp[1], &c->bp[1], c->f[1], c->q[1], x) * c->k[1]) >> 16);
            r = clamp(r, -65535, 65535);
            y[i] += r;
            pk = r > pk ? r : -r > pk ? -r : pk;
        }
        live |= pk >= DR_QUIET;
    }
    if (s->nf)
        live |= dlk_noise(c, y, n, s->nf, 0);
    if (s->sq[0]) {                              /* the squares */
        int32_t e0, de = dre_next(&c->e[2], &e0);
        for (i = 0; i < n; i++) {
            int32_t x = (c->ph[0] < DR_DUTY ? 8192 : -8192) + (c->ph[1] < DR_DUTY ? 8192 : -8192);
            c->ph[0] += c->inc[0];
            c->ph[1] += c->inc[1];
            y[i] += (((drf_bp(&c->sf, x, c->ss) * DRE_AT(e0, de, i)) >> 15) * s->sg) >> 12;
        }
        live |= c->e[2].e >= DRE_LOW;
    }
    if (c->tk < 16384)                           /* TONE */
        for (i = 0; i < n; i++)
            y[i] = c->ts += (int32_t)(((int64_t)(y[i] - c->ts) * c->tk) >> 14);
    return live && t0 < DR_LIFE;
}
