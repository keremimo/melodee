/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* KIT 1..8 (values 9..16): the user's recorded kits (Record kit, kit_take.c), pool records (kit_store.c) played from
 * their flash sectors in place. A whole kit on the drum track's one voice, as the lane kits are, but a channel is a
 * hit, not a lane: toms overlap, a crash rings under a ride, a drum struck again rings out for 15 ms under its next
 * hit. A closed hat chokes the open one (closed hats alternate between two takes where a record holds two).
 * A stream is a 4-tap sign-LMS predictor after QOA with a bit depth per slice: two 32-bit words, [2 mode][4 scale]
 * then residuals, top bits first; drum_enc.c (and tools/drum_codec.c, its reference) encodes. A looped tail (a
 * cymbal recorded past its buffer) restarts its predictor from the record and dies on the hit's envelope, darkening.
 * TUNE every drum (Hermite at the output rate); TONE a one-pole cut below 64, a one-pole boost above, around each
 * drum's pivot; velocity darkens with level; DECY the envelope; SNAP the front of the hit. An empty slot: no sound. */
#define DRK_NCH 8u
#define DRK_RING DR_MS(15)                       /* a drum struck again: the old hit's fade */
#define DRK_CHOKE 128                            /* a choked open hat, a recorded hit's end */
#define DRK_ATT 28672                            /* SNAP's envelope, per block (Q15): ~5 ms */
#define DRK_LPOFF 16384
#define DRK_LPMIN 3000                           /* a tail's darkening stops here: ~1.4 kHz */

typedef struct {
    const uint32_t *src, *end;                   /* the next slice; its sector's end */
    const uint32_t *const *secs;                 /* the kit's sectors (its part's) */
    const drk_sample_t *s;                       /* the take */
    const int32_t *loop;                         /* its predictor at the loop */
    uint32_t hi, lo;                             /* this slice's bits, top first */
    const int8_t *lv;                            /* its levels (Q2) */
    int32_t sc;                                  /* its scale */
    int32_t h[4], w[4];                          /* the predictor */
    int32_t z[4];                                /* the last four samples: the output is between z[1] and z[2] */
    uint32_t ph, inc;                            /* Q16 */
    uint32_t idx, n, t;                          /* samples decoded, in all; output samples since the hit */
    int32_t gain, a;                             /* the hit's level (Q15); last block's amplitude, -1: none yet */
    int32_t left, fade, flen;                    /* a recorded hit's samples to its end; samples of fade left, of all */
    int32_t last, ease;                          /* the channel's last sample; a stolen hit's step, easing out */
    int32_t snap, att;                           /* SNAP (Q15, +- 1); its envelope (Q15) */
    int32_t lp, lk, hs, hk, hv;                  /* TONE: cut (state, Q14), boost (state, Q14, amount Q6) */
    dre_t e;
    uint8_t bits, nleft, map, sec, sound, lane, level, on;
} drk_ch_t;
struct drk_part {                                /* (drk_part_t: eng_drum.c) */
    drk_ch_t ch[DRK_NCH];
    const drk_kit_t *H;                          /* the kit's header (flash); 0: an empty slot */
    const uint32_t *sec[DRK_KSEC];               /* its sectors' payloads (flash) */
    int32_t nst;
    uint32_t gen;                                /* drk_user_gen when the part took its slot */
    uint8_t ready, kit, take;
};
_Static_assert(((sizeof(drk_part_t) + 3u) & ~3u) != ((sizeof(dlk_part_t) + 3u) & ~3u) &&
               ((sizeof(drk_part_t) + 3u) & ~3u) != ((sizeof(drum909_part_t) + 3u) & ~3u) &&
               ((sizeof(drk_part_t) + 3u) & ~3u) != ((sizeof(dsk_part_t) + 3u) & ~3u) &&
               ((sizeof(drk_part_t) + 3u) & ~3u) != ((sizeof(drum_lane_t) * DV_NLANE + 3u) & ~3u),
               "the sampled kits' state: its own size in the DRUM pool (resource_get keys on it)");
static drk_part_t *drk_part(uint32_t part);      /* engines.c: the part's runtime pool */
static uint32_t drk_bytes(void) { return sizeof(drk_part_t); }

/* a user slot's sectors into the part, if the table stands still meanwhile; else (or empty) the part stays silent */
static void drk_user_take(drk_part_t *K, uint32_t u, uint32_t g)
{
    const uint32_t *sec[DRK_KSEC];
    uint32_t n = u < DK_NUSER && !(g & 1u) ? drk_user[u].n : 0u;
    if (n > DRK_KSEC)
        n = 0;
    for (uint32_t k = 0; k < n; k++)
        sec[k] = (const uint32_t *)DRK_XIP(drk_user[u].sec[k] * 4096u + DRK_SECOFF);
    __asm__ volatile("" ::: "memory");
    if (!n || drk_user_gen != g)
        return;
    for (uint32_t k = 0; k < n; k++)
        K->sec[k] = sec[k];
    K->H = (const drk_kit_t *)sec[0];
}

static drk_part_t *drk_of(const track_t *t)
{
    drk_part_t *K = drk_part(trk_ix(t));
    uint32_t g = drk_user_gen;
    if (K && (!K->ready || K->kit != t->p[P_E0] || K->gen != g)) {
        __typeof__(&memset) volatile clear = memset;
        clear(K, 0, sizeof *K);
        K->nst = 0x2F6E2B1;
        K->kit = (uint8_t)t->p[P_E0];
        K->gen = g;
        K->ready = 1;
        drk_user_take(K, K->kit - (uint32_t)DK_USER0, g);
    }
    return K;
}

/* a channel for the hit: a free one, a fading one, the oldest (its step eases out) */
static drk_ch_t *drk_channel(drk_part_t *K)
{
    drk_ch_t *b = 0;
    for (uint32_t k = 0; k < DRK_NCH; k++) {
        drk_ch_t *c = &K->ch[k];
        if (!c->on && !c->ease)
            return c;
        if (!b || (c->fade && !b->fade) || (!!c->fade == !!b->fade && c->t > b->t))
            b = c;
    }
    return b;
}

/* the stream from slice, sector by sector */
static void drk_seek(drk_ch_t *c, uint32_t slice)
{
    uint32_t k = slice / DRK_SECSLICES, j = slice % DRK_SECSLICES;
    c->sec = (uint8_t)k;
    c->src = c->secs[k] + DRK_SLOT(j);
    c->end = c->secs[k] + 2u * (j < DRK_RUN0 ? DRK_RUN0 : DRK_SLOTS);
}

static void drk_stream_start(drk_ch_t *c, const drk_sample_t *s)
{
    for (uint32_t k = 0; k < 4u; k++)
        c->h[k] = c->z[k] = 0;
    c->w[0] = c->w[1] = 0;
    c->w[2] = -(1 << 13);
    c->w[3] = 1 << 14;
    drk_seek(c, s->at);
    c->nleft = 0;
    c->idx = 0;
    c->n = s->n;
}

/* a hit on note at velocity vel */
static void drk_hit(track_t *t, uint32_t note, uint32_t vel)
{
    drk_part_t *K = drk_of(t);
    int16_t q[8];
    if (!K || K->kit < DK_USER0 || K->kit > DK_KIT_MAX || !K->H)
        return;
    uint32_t sound = drum909_ins(note) & 15u, lane = drum_lane(note);
    const int8_t *pc = drum_offs(t)->c[sound];
    const drk_map_t *m = &K->H->map[sound];
    uint32_t take = m->sample;
    if (take == 0xFFu)                           /* a pad with nothing recorded */
        return;
    dlk_values(t->p, pc, sound, q);
    if (m->alt != 0xFFu && (sound == D9_BD ? q[6] != 0 : sound == D9_CH && ((K->take ^= 1u) != 0u)))
        take = m->alt;
    const drk_sample_t *s = &K->H->smp[take];
    for (uint32_t k = 0; k < DRK_NCH; k++) {
        drk_ch_t *o = &K->ch[k];
        if (!o->on || o->fade)
            continue;
        if (o->sound == sound)                   /* the drum struck again: it rings out under the new hit */
            o->fade = o->flen = (int32_t)DRK_RING;
        else if (sound == D9_CH && o->sound == D9_OH)
            o->fade = o->flen = DRK_CHOKE;
    }
    drk_ch_t *c = drk_channel(K);
    c->ease = c->on || c->ease ? c->last : 0;
    int32_t tune = clamp((m->st + pc[0] + drum_event_pitch) * 16 + (q[1] - 64) * 3, -576, 576);
    c->inc = (uint32_t)((float)s->inc * 2.0f * dlk_exp2((float)tune / 192.0f));
    c->ph = 0;
    c->secs = K->sec;
    c->s = s;
    c->loop = K->H->loops[s->st];
    drk_stream_start(c, s);
    /* a harder hit is brighter as well as louder (a recording of one velocity: the player's own touch) */
    int32_t dark = vel < 80u ? (int32_t)(80u - vel) / 2 : 0, v = clamp(q[2] - 64 + m->tone - dark, -64, 63);
    c->lk = v < 0 ? dr_lp1(dr_exp(m->pivot, v, 18)) : DRK_LPOFF;
    c->hk = v > 0 ? dr_lp1(m->pivot) : 0;
    c->hv = v > 0 ? v : 0;
    c->lp = c->hs = 0;
    c->gain = (int32_t)(((int64_t)dr8_level(vel, q[5]) * m->out) >> 12);
    c->level = (uint8_t)(127 - pc[3]);
    c->lane = (uint8_t)lane;
    c->left = (int32_t)drum_event_length;
    c->snap = (q[4] - 64) * 512;
    c->att = 32767;
    dlk_set(&c->e, m->ms, pc[1] + q[3] - 64);
    c->fade = c->flen = 0;
    c->a = -1;
    c->t = 0;
    c->sound = (uint8_t)sound;
    c->on = 1;
}

/* the part may sound: its kit taken under the table's current number */
static int drk_live(const drk_part_t *K, const int16_t *p)
{
    return K && K->ready && K->kit == p[P_E0] && K->H && K->gen == drk_user_gen;
}

static int drk_tick(track_t *t)
{
    drk_part_t *K = drk_part(trk_ix(t));
    if (!drk_live(K, t->p))
        return 0;
    for (uint32_t k = 0; k < DRK_NCH; k++)
        if (K->ch[k].on || K->ch[k].ease)
            return 1;
    return 0;
}

/* the stream's next sample (tools/drum_codec.c's slice(), step for step) */
static inline __attribute__((always_inline)) int32_t drk_next(drk_ch_t *restrict c)
{
    uint32_t hi = c->hi, lo = c->lo, b = c->bits;
    if (!c->nleft) {
        if (c->src == c->end) {                  /* a user kit's second run past the guard, or its next sector */
            const uint32_t *b = c->secs[c->sec];
            if (c->end == b + 2u * DRK_RUN0) {
                c->src = b + 2u * (DRK_RUN0 + 1u);
                c->end = b + 2u * DRK_SLOTS;
            } else {
                c->src = c->secs[++c->sec];
                c->end = c->src + 2u * DRK_RUN0;
            }
        }
        hi = c->src[0];
        lo = c->src[1];
        c->src += 2;
        b = (hi >> 30) + 1u;
        c->bits = (uint8_t)b;
        c->nleft = DRK_NS[b - 1u];
        c->sc = DRK_SF[(hi >> 26) & 15u];
        c->lv = DRK_LV[b - 1u];
        hi = (hi << 6) | (lo >> 26);
        lo <<= 6;
    }
    int32_t r = (c->lv[hi >> (32u - b)] * c->sc) >> 2;
    c->hi = (hi << b) | (lo >> (32u - b));
    c->lo = lo << b;
    c->nleft--;
    int32_t h0 = c->h[0], h1 = c->h[1], h2 = c->h[2], h3 = c->h[3];
    uint32_t pr = (uint32_t)h0 * (uint32_t)c->w[0] + (uint32_t)h1 * (uint32_t)c->w[1] +
                  (uint32_t)h2 * (uint32_t)c->w[2] + (uint32_t)h3 * (uint32_t)c->w[3];
    int32_t s = ((int32_t)pr >> 13) + r, d = r >> 4;
    s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
    c->w[0] += (d ^ (h0 >> 31)) - (h0 >> 31);
    c->w[1] += (d ^ (h1 >> 31)) - (h1 >> 31);
    c->w[2] += (d ^ (h2 >> 31)) - (h2 >> 31);
    c->w[3] += (d ^ (h3 >> 31)) - (h3 >> 31);
    c->h[0] = h1;
    c->h[1] = h2;
    c->h[2] = h3;
    c->h[3] = s;
    return s;
}

/* the channel's stream at its pitch, one block into x; 0 once a one-shot has ended */
static __attribute__((noinline)) int drk_run(drk_ch_t *restrict c, int32_t *x, uint32_t n)
{
    const drk_sample_t *s = c->s;
    uint32_t ph = c->ph, inc = c->inc;
    int live = c->idx < c->n + 4u;
    for (uint32_t i = 0; i < n; i++) {
        ph += inc;
        while (ph >= 65536u) {
            int32_t v = 0;
            ph -= 65536u;
            if (c->idx >= c->n && s->loop_at) {       /* the tail again: the predictor as it stood there */
                const int32_t *st = c->loop;
                for (uint32_t k = 0; k < 4u; k++) {
                    c->h[k] = st[k];
                    c->w[k] = st[4u + k];
                }
                drk_seek(c, (uint32_t)s->at + s->loop_slice);
                c->nleft = 0;
                c->idx = s->loop_at;
            }
            if (c->idx < c->n)
                v = drk_next(c);
            c->idx++;
            c->z[0] = c->z[1];
            c->z[1] = c->z[2];
            c->z[2] = c->z[3];
            c->z[3] = v;
        }
        int32_t f = (int32_t)(ph >> 5), a = c->z[0], b = c->z[1], e = c->z[2], d = c->z[3];
        int32_t c1 = (e - a) >> 1, c2 = a - ((5 * b) >> 1) + 2 * e - (d >> 1), c3 = ((d - a) >> 1) + ((3 * (b - e)) >> 1);
        x[i] = ((((((c3 * f) >> 11) + c2) * f >> 11) + c1) * f >> 11) + b;
    }
    c->ph = ph;
    return live || s->loop_at;
}

static inline __attribute__((always_inline)) void drk_tone(drk_ch_t *restrict c, int32_t *x, uint32_t n)
{
    const drk_sample_t *s = c->s;
    if (s->dark && c->idx >= s->loop_at && c->lk > DRK_LPMIN)
        c->lk -= (c->lk * s->dark) >> 12;
    if (c->lk < DRK_LPOFF) {
        int32_t y = c->lp, k = c->lk;
        for (uint32_t i = 0; i < n; i++) {
            y += ((x[i] - y) * k) >> 14;
            x[i] = y;
        }
        c->lp = y;
    }
    if (c->hv) {
        int32_t y = c->hs, k = c->hk, a = c->hv;
        for (uint32_t i = 0; i < n; i++) {
            y += ((x[i] - y) * k) >> 14;
            x[i] += ((x[i] - y) * a) >> 6;
        }
        c->hs = y;
    }
}

/* the kit, one block: each channel at its level, steps easing out, then the lane kits' output stage */
static __attribute__((noinline)) void drk_render(track_t *t, int32_t *out, uint32_t n, const vmod_t *m, const int16_t *p)
{
    drk_part_t *K = drk_part(trk_ix(t) % NPART);
    int32_t y[CTL] = {0}, x[CTL];
    if (!drk_live(K, p))
        return;
    for (uint32_t k = 0; k < DRK_NCH; k++) {
        drk_ch_t *c = &K->ch[k];
        int32_t s = 0;
        if (!c->on && !c->ease)
            continue;
        if (c->on) {
            if (!drk_run(c, x, n))
                c->on = 0;
            drk_tone(c, x, n);
            int32_t e0, a0, a1;
            dre_next(&c->e, &e0);
            a1 = mulq15(mulq15(c->gain, dsk_level(p, c->lane, c->level)), c->e.e >> 9);
            if (c->snap) {
                a1 = (int32_t)(((int64_t)a1 * (32768 + ((c->snap * c->att) >> 15))) >> 15);
                c->att = (c->att * DRK_ATT) >> 15;
            }
            a0 = c->a < 0 ? a1 : c->a;
            c->a = a1;
            for (uint32_t i = 0; i < n; i++) {
                s = (int32_t)(((int64_t)x[i] * (a0 + (((a1 - a0) * (int32_t)i) >> CTL_LOG2))) >> 15);
                if (c->fade)
                    s = c->fade > (int32_t)i ? (int32_t)(((int64_t)s * (c->fade - (int32_t)i)) / c->flen) : 0;
                y[i] += s;
            }
            c->t += n;
            if (c->left > 0 && (c->left -= (int32_t)n) <= 0 && !c->fade)
                c->fade = c->flen = DRK_CHOKE;   /* a recorded hit's end */
            else if (c->fade && (c->fade -= (int32_t)n) <= 0)
                c->fade = 0, c->on = 0;
            if (c->e.e < DRE_LOW || c->t >= DR_LIFE)
                c->on = 0;
        }
        for (uint32_t i = 0; c->ease && i < n; i++) {
            y[i] += c->ease;
            c->ease -= c->ease >> DLK_EASE;
            if (c->ease > -(1 << DLK_EASE) && c->ease < (1 << DLK_EASE))
                c->ease = 0;
        }
        c->last = c->on ? s : 0;
    }
    dlk_out(y, out, n, m, p);
}

