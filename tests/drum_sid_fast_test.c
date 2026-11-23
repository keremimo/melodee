/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Original Melodee SID-kit renderer retained from 05a05cf. Compare every PCM
 * sample and all chip state after live controls, fades, retriggers and digi. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static __attribute__((noinline)) void dsk_render_reference(track_t *t, int32_t *out, uint32_t n, const vmod_t *m,
                                                 const int16_t *p)
{
    dsk_part_t *K = dsk_part((uint32_t)(t - trk) % NPART);
    const int16_t (*dac)[64] = SID_WDAC[SID_6581];
    const uint16_t *gt = SID_FC_G[SID_6581];
    int32_t a0[DSK_NCH], a1[DSK_NCH], zero = SID_ZERO[SID_6581], knee = SID_KNEE[SID_6581];
    int32_t ic1, ic2, xlp, xhp, o = 0, fcr, g, kd, dg = 0, rest = p[P_E6] ? DSK_REST : 15, rcp;
    uint32_t i, k, live = 0, route = 0, dpos, dend;
    tsvf_t fc;
    if (!K || !K->ready)
        return;
    for (k = 0; k < DSK_NCH; k++) {
        const dsk_ch_t *c = &K->ch[k];
        int32_t lv;
        if (dsk_done(c) && !c->e.env_prev && !c->fade)
            continue;
        lv = mulq15(c->gain, dsk_level(p, c->lane, c->level)) >> 1;   /* (half scale) */
        a0[k] = mulq15(sid_edac(SID_6581, c->e.env_prev), lv);
        a1[k] = mulq15(sid_edac(SID_6581, c->e.env), lv);
        if (c->fade) {                         /* fading out: fade / len .. (fade - 1) / len this block */
            a0[k] = mulq15(a0[k], c->fade * 32767 / c->fade_len);
            a1[k] = mulq15(a1[k], (c->fade - 1) * 32767 / c->fade_len);
        }
        live |= 1u << k;
        route |= (uint32_t)DSK_DRUMS[c->sound].route << k;
    }
    fcr = clamp(p[P_E2] * 16 + (p[P_E2] >> 3), 0, 2047);   /* TONE: FC; RES: the resonance's 4 bits */
    g = gt[fcr >> 4] + (((gt[(fcr >> 4) + 1] - gt[fcr >> 4]) * (fcr & 15)) >> 4);
    kd = SID_RES_K[SID_6581][clamp(p[P_E7], 0, 127) >> 3];
    tsvf_coef_gk(&fc, g, kd);
    if (K->digi)
        dg = mulq15(K->dgain, dsk_level(p, K->dlane, K->dlevel));
    rcp = 65536 / rest;
    ic1 = K->ic1; ic2 = K->ic2; xlp = K->xlp; xhp = K->xhp;
    dpos = K->dpos;
    dend = (uint32_t)K->dlen << 16;
    for (i = 0; i < n; i++) {
        int32_t f = 0, d = 0, s;
        for (k = 0; k < DSK_NCH; k++) {
            dsk_ch_t *c = &K->ch[k];
            uint32_t q, w, wb;
            int32_t x;
            if (!(live >> k & 1u))
                continue;
            q = c->ph + c->inc;
            if (c->ctrl & DSK_NOISE) {             /* the noise register: clocked by accumulator bit 19 */
                if ((((c->ph ^ 0x08000000u) & 0x0fffffffu) + c->inc) >> 28) {
                    c->noise = sid_noise_advance(c->noise, c->ph, c->inc);
                    c->nb = sid_noise_bits(c->noise);
                }
                w = c->nb;
            } else {
                wb = (c->ctrl >> 4) & 7u;
                wb &= -wb;                         /* (one waveform: the programs combine none) */
                w = wb ? sid_wave_out(wb, 0, q, q, c->pt) : 0u;
            }
            c->ph = q;
            x = ((sid_dac(dac, w) - zero) * (a0[k] + (((a1[k] - a0[k]) * (int32_t)i) >> CTL_LOG2))) >> 15;
            if (route >> k & 1u)
                f += x;
            else
                d += x;
        }
        {                                          /* the filter: the SID engine's TPT state variable, LP */
            int32_t v3 = f - ic2, bp, lp;
            bp = soft_knee((fc.a1 * ic1 + fc.a2 * v3) >> 13, knee);
            lp = soft_knee(ic2 + ((fc.a2 * ic1 + fc.a3 * v3) >> 13), knee);
            ic1 = clamp(2 * bp - ic1, -150000, 150000);
            ic2 = clamp(2 * lp - ic2, -150000, 150000);
            s = lp + d;
        }
        if (K->digi) {                             /* $D418: the sample's step, the DC riding on it */
            uint32_t at = dpos >> 16, nib = (DSK_DIGI[DSK_DIGI_AT[K->dsmp] + (at >> 1)] >> ((at & 1u) << 2)) & 15u;
            int32_t vol = clamp(rest + ((((int32_t)nib - 8) * dg + 16384) >> 15), 0, 15);
            s = (int32_t)(((int64_t)(s * vol + (DSK_DC >> 2) * (vol - rest)) * rcp) >> 16);   /* (the DC at
                                                                       * half of half scale: a digi as loud as a drum) */
            if ((dpos += K->dstep) >= dend)
                K->digi = 0;
        }
        xlp += (int32_t)(((int64_t)(clamp(s, -131071, 131071) - xlp) * SID_XLP_A) >> 15);
        xhp += (int32_t)(((int64_t)((xlp << 8) - xhp) * SID_XHP_A) >> 16);
        o = clamp(xlp - (xhp >> 8), -32767, 32767);
        out[i] += voice_amp(soft_knee(o * 3, 24000), m, i) << 1;   /* (x3: the kits' level from half scale; doubled
                                                                       * after voice_amp, whose Q15 product holds
                                                                       * 16 bits: the knee's top, 40383, x 2 would wrap) */
    }
    K->ic1 = ic1; K->ic2 = ic2; K->xlp = xlp; K->xhp = xhp;
    K->dpos = dpos;
    K->quiet = !live && !K->digi && o > -32 && o < 32 && xlp > -128 && xlp < 128 && ic1 > -256 && ic1 < 256 &&
               ic2 > -256 && ic2 < 256;
}

static uint32_t seed=0x71893456u;
static uint32_t random_u(void){seed=seed*1664525u+1013904223u;return seed;}
int main(void)
{
    uint32_t cases=0;
    for(uint32_t config=0;config<4;config++){
        eng_state_reset();memset(trk,0,sizeof trk);trk_voices_bind();host_tracks_init();host_kit_preset(&trk[0],2);
        track_t *t=&trk[0];dsk_part_t *K=dsk_of(t);
        for(uint32_t b=0;b<20000;b++){
            t->p[P_E6]=(int16_t)(config<2?config:(b/37u)&1u);
            if(config==3 && b%29u==0){
                for(uint32_t k=P_E1;k<=P_E7;k++)if(k!=P_E6)t->p[k]=(int16_t)(random_u()%128u);
                for(uint32_t k=0;k<DV_NLANE;k++)t->p[P_LN0+k]=(int16_t)(random_u()%128u);
            }
            if(b%17u==0){
                note_hit_pitch=(int32_t)(random_u()%49u)-24;note_hit_len=b%3u?0u:CTL*(1u+b%97u);
                dsk_hit(t,DRUM_SOUND_NOTE[(b/17u)%16u],1u+random_u()%127u);
                if(b%119u==0){dsk_hit(t,36,110);dsk_hit(t,38,100);dsk_hit(t,42,90);}
            }
            dsk_tick(t);
            vmod_t m={0};m.amp0=(int32_t)(random_u()%32769u);m.amp1=(int32_t)(random_u()%32769u);
            int32_t x[CTL],y[CTL];for(uint32_t i=0;i<CTL;i++)x[i]=y[i]=(int32_t)(random_u()%127u)-63;
            uint32_t n=b%33u;dsk_part_t original=*K;
            dsk_render_reference(t,x,n,&m,t->p);dsk_part_t expected=*K;
            *K=original;dsk_render(t,y,n,&m,t->p);
            assert(!memcmp(x,y,sizeof x));assert(!memcmp(&expected,K,sizeof expected));cases++;
        }
    }
    printf("SID drum kernel: %u exact PCM/full-chip-state cases, all programs, 0..32 samples, live controls, steals/fades, DIGI and recorded releases\n",cases);
    return 0;
}
