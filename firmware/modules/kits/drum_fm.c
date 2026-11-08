/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* KIT FM (drum_lanes.c's lanes): two-operator FM drums, as the FM drum machines and the DX7's percussion patches
 * build them. A modulator (its feedback, its index falling on its own envelope) into a sine carrier, both swept
 * down in pitch at the hit, and a filtered noise where a drum wants air (the snare, the clap's bursts, the
 * maracas). The hats and the cymbal are an inharmonic pair at a high index through a high-pass: metal without
 * noise. TUNE every drum; TONE the index (a quarter to four times: dark to bright); DECY the kick's, the open
 * hat's, the cymbal's and the bell's decay; SNAP the snare's noise; KICK ROUND the kick with half its sweep and
 * less index. Operators at the sine table (sine_i), all fixed point but the hit's setup. */
typedef struct {
    uint16_t hz;                                 /* the carrier (Hz, 0: none) */
    uint16_t ratio;                              /* the modulator's frequency over the carrier's (Q8) */
    uint8_t index, fb;                           /* the modulation index at the hit (radians x 16), the feedback */
    uint16_t cms, ims;                           /* the carrier's decay, the index's (-40 dB ms) */
    uint8_t sweep, sms;                          /* the pitch at the hit (semitones above), its fall (-40 dB ms) */
    uint8_t nf, bursts;                          /* noise: DLK_NONE, DLK_HP, DLK_BP; a clap's bursts before its tail */
    uint16_t nhz, nms;                           /* its filter's Hz, its decay */
    uint8_t nrise, dk;                           /* ms to its top; DECY sets this drum's decay */
    int16_t ng, g;                               /* the noise's level, the pair's (Q12) */
    uint16_t hp;                                 /* the output's high-pass (Hz, 0: none) */
    uint16_t out;                                /* the drum's level (Q12): the kit's balance */
} fmk_sound_t;
#define FMK_R(x) ((uint16_t)((x) * 256 + 0.5))
static const fmk_sound_t FMK_SOUNDS[16] = {
    /* KICK: a sine sweeping two octaves down in 45 ms, a short index for its click */
    {52, FMK_R(1.0), 24, 0, 450, 25, 24, 45, DLK_NONE, 0, 0, 0, 0, 1, 0, 4096, 0, 4096},
    /* SNARE: an inharmonic body falling a fifth, under high-passed noise */
    {185, FMK_R(1.56), 32, 0, 140, 40, 7, 25, DLK_HP, 0, 1500, 180, 0, 0, 2400, 2600, 0, 3642},
    /* TOM L, M, H: falling a fifth, a soft index */
    {95, FMK_R(1.0), 19, 0, 380, 60, 7, 120, DLK_NONE, 0, 0, 0, 0, 0, 0, 3600, 0, 3684},
    {130, FMK_R(1.0), 19, 0, 320, 60, 7, 110, DLK_NONE, 0, 0, 0, 0, 0, 0, 3600, 0, 3656},
    {175, FMK_R(1.0), 19, 0, 270, 60, 7, 100, DLK_NONE, 0, 0, 0, 0, 0, 0, 3600, 0, 3640},
    /* RIM: a short wood-and-metal tick */
    {460, FMK_R(3.46), 40, 0, 30, 12, 0, 0, DLK_NONE, 0, 0, 0, 0, 0, 0, 3600, 0, 3165},
    /* CLAP: band-passed noise, three bursts 10 ms apart, a fourth and the tail; no operators */
    {0, 0, 0, 0, 0, 0, 0, 0, DLK_BP, 4, 1200, 160, 0, 0, 10190, 0, 0, 4096},
    /* HAT C, HAT O: c : m = 1 : 1.414 at a high index with feedback, high-passed */
    {3020, FMK_R(1.414), 128, 5, 45, 45, 0, 0, DLK_NONE, 0, 0, 0, 0, 0, 0, 3000, 6000, 3310},
    {3020, FMK_R(1.414), 128, 5, 350, 300, 0, 0, DLK_NONE, 0, 0, 0, 0, 1, 0, 2600, 6000, 3022},
    /* CYMB: a wider pair, the index falling slower than the level, a little noise on top */
    {2100, FMK_R(2.31), 160, 6, 1300, 900, 0, 0, DLK_HP, 0, 7000, 1000, 0, 1, 500, 2400, 4000, 2830},
    /* BELL: the DX bell, c : m = 1 : 3.5 */
    {830, FMK_R(3.5), 48, 0, 900, 500, 0, 0, DLK_NONE, 0, 0, 0, 0, 1, 0, 2600, 0, 4316},
    /* CONGA L, M, H: a soft index, a short sweep */
    {210, FMK_R(1.0), 16, 0, 200, 25, 3, 30, DLK_NONE, 0, 0, 0, 0, 0, 0, 3600, 0, 3315},
    {300, FMK_R(1.0), 16, 0, 170, 25, 3, 30, DLK_NONE, 0, 0, 0, 0, 0, 0, 3600, 0, 3285},
    {420, FMK_R(1.0), 16, 0, 140, 25, 3, 30, DLK_NONE, 0, 0, 0, 0, 0, 0, 3600, 0, 3277},
    /* CLAVE: a high sine, a touch of index */
    {2450, FMK_R(1.0), 10, 0, 40, 10, 0, 0, DLK_NONE, 0, 0, 0, 0, 0, 0, 3000, 0, 3375},
    /* MARAC: high-passed noise rising in 8 ms; no operators */
    {0, 0, 0, 0, 0, 0, 0, 0, DLK_HP, 0, 7000, 60, 8, 0, 8010, 0, 0, 4096},
};
#define FMK_PHASE 20861                          /* phase (2^32 a turn) of a 1-radian index per Q15 of the modulator */
#define FMK_FB 1300                              /* phase per feedback step and Q15 of the modulator's last two */

/* the lane struck: its drum at the hit's pitch and the knobs (q: P_E0..P_E7, dk: the HIT page's decay); always
 * from rest (a ringing operator pair struck again is a new phase): 1 */
static int fmk_hit(dlk_ch_t *c, uint32_t sound, int32_t tune, const int16_t *q, int32_t dk)
{
    const fmk_sound_t *s = &FMK_SOUNDS[sound];
    int32_t dec = dk + (s->dk ? q[3] - 64 : 0), round = sound == D9_BD && q[6];
    float r = dlk_exp2((float)tune / 192.0f), tone = dlk_exp2((float)(q[2] - 64) / 32.0f);
    c->rec = (uint8_t)sound;
    c->gain = (int32_t)(((int64_t)c->gain * s->out) >> 12);
    c->ph[0] = c->ph[1] = 0;
    c->m1 = c->m2 = 0;
    c->ns[0] = c->ns[1] = c->ss[0] = c->ss[1] = 0;
    c->inc[0] = (uint32_t)((float)s->hz * r * 97391.55f);
    c->inc[1] = (uint32_t)(((uint64_t)c->inc[0] * s->ratio) >> 8);
    c->k[0] = (int32_t)((float)s->index / 16.0f * (float)FMK_PHASE * tone * (round ? 0.4f : 1.0f));
    c->k[1] = (int32_t)((dlk_exp2((float)(round ? s->sweep / 2u : s->sweep) / 12.0f) - 1.0f) * 65536.0f);
    dlk_set(&c->e[1], s->cms, dec);
    dlk_set(&c->e[2], s->ims, dec);
    dlk_set(&c->e[3], s->sms ? s->sms : 1u, 0);
    if (s->nf)
        dlk_noise_set(c, s->nf, (float)s->nhz * r, 1.5f, s->nms, s->nrise, s->bursts, dec,
                      sound == D9_SD ? s->ng * q[4] / 64 : s->ng);
    if (s->hp)
        c->sf = dlk_svf((float)s->hp * r, 0.71f);
    return 1;
}

/* the lane's drum, one block into y; 0 once it has rung out */
static __attribute__((noinline)) int fmk_run(dlk_ch_t *restrict c, int32_t *y, uint32_t n)
{
    const fmk_sound_t *s = &FMK_SOUNDS[c->rec];
    uint32_t i;
    int live = 0;
    if (s->hz) {                                 /* the pair: the sweep and the index ramped over the block */
        int32_t a0, da = dre_next(&c->e[1], &a0), i0, di = dre_next(&c->e[2], &i0), p0, fbk = s->fb * FMK_FB;
        dre_next(&c->e[3], &p0);
        uint32_t sw = 65536u + (uint32_t)(((int64_t)c->k[1] * p0) >> 24);
        uint32_t ic = (uint32_t)(((uint64_t)c->inc[0] * sw) >> 16), im = (uint32_t)(((uint64_t)c->inc[1] * sw) >> 16);
        int32_t ka = (int32_t)(((int64_t)c->k[0] * i0) >> 24), kb = (int32_t)(((int64_t)c->k[0] * (i0 + di)) >> 24);
        for (i = 0; i < n; i++) {
            int32_t m = sine_i(c->ph[1] + (uint32_t)((c->m1 + c->m2) * fbk)), o;
            c->m2 = c->m1;
            c->m1 = m;
            c->ph[1] += im;
            o = sine_i(c->ph[0] + (uint32_t)((int64_t)m * (ka + (((kb - ka) * (int32_t)i) >> CTL_LOG2))));
            c->ph[0] += ic;
            y[i] += (((o * DRE_AT(a0, da, i)) >> 15) * s->g) >> 12;
        }
        live |= c->e[1].e >= DRE_LOW;
    }
    if (s->nf)
        live |= dlk_noise(c, y, n, s->nf, s->bursts);
    if (s->hp)                                   /* the metal's high-pass */
        for (i = 0; i < n; i++)
            y[i] = drf_hp(&c->sf, y[i], c->ss);
    return live && c->t < DR_LIFE;
}
