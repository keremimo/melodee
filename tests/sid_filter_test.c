/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Original Melodee SID renderer retained from 8e5ce97, including its
 * reSID model/curve provenance documented in firmware/src/eng_sid.c. */
#define MELODEE_SID_FAST 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#undef __attribute__
#include <assert.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
static uint64_t instructions(void) {
#ifdef __APPLE__
 struct rusage_info_v4 r;
 if(!proc_pid_rusage(getpid(),RUSAGE_INFO_V4,(rusage_info_t *)&r))return r.ri_instructions;
#endif
 return 0;
}
static void sid_render_reference(track_t *t,voice_t *v,int32_t *out,uint32_t n,const vmod_t *m,const int16_t *p)
{
    sid_voice_t *s = sid_voice(t, v);
    uint32_t model = sid_model(p), wave = (uint32_t)p[P_E1] & 7u, stack = (uint32_t)p[P_E2] & 7u;
    const sid_stack_t *st = &SID_STACKS[stack];
    const int16_t (*dac)[64] = SID_WDAC[model];
    const uint16_t *gt = SID_FC_G[model];
    uint32_t nosc = st->nosc, heard = st->heard, bits = SID_MODE_BITS[p[P_E6] & 7], wb = SID_WAVE_BITS[wave];
    uint32_t sync = stack == SID_SYNC, ring = stack == SID_RING, blep = wave == SID_SAW || wave == SID_PULSE;
    uint32_t rmask = ring && !(wb & 2u) ? 0x80000000u : 0u;   /* (RING does nothing with the sawtooth on) */
    uint32_t noisy = wb & 8u, pull = !model && (wb & 2u) && (wb & 5u);   /* (6581, the sawtooth combined) */
    const uint8_t *wt = sid_table(t, model, wb);
    uint32_t ph[3], inc[3] = {0, 0, 0}, rcp[3], rsh[3], noise[3], nb[3], hl[3], nh = 0, pt, i, k;
    int32_t full = dac[0][63] + dac[1][63], zero = SID_ZERO[model] * (heard == 7u ? 3 : 1), knee = SID_KNEE[model];
    int32_t shp = m->shape - (64 << 8), norm = heard == 7u ? 10923 : 32767, vel = v->vel * 258;
    int32_t held = s->held, ic1 = s->ic1, ic2 = s->ic2, xlp = s->xlp, xhp = s->xhp, o = 0;
    int32_t fcr, g, kd, a0, a1, gin, gout, sl, sb, sh;
    tsvf_t fc;
    /* the registers this block: frequencies (16 bits), pulse width (12), FC (11), RES (4) */
    for (k = 0; k < 3u; k++) {
        ph[k] = v->ph[k];
        noise[k] = s->noise[k];
        nb[k] = sid_noise_bits(noise[k]);
        if (heard >> k & 1u)
            hl[nh++] = k;                              /* the voices mixed */
    }
    uint32_t edge=blep||sync;
    /* C64: between the player's frames the registers hold what it wrote (a new STACK or edge set-up: at once) */
    uint32_t hold=sid_c64(p) && !s->latch && s->freq_valid && s->filter_valid && s->stack_key==stack && s->edge_key==edge;
    s->latch=0;
    if(!hold && (!s->freq_valid || s->pitch_key!=m->pitch16 || s->fine_key!=m->fine || s->shape_key!=shp ||
       s->tune_key!=tuning_a4 || s->stack_key!=stack || s->edge_key!=edge)){
    for (k = 0; k < nosc; k++) {
        int32_t c = st->cents[k] + (k == 1u && (sync || ring) ? (shp * 75) >> 10 : 0);   /* SHP: +-24 st */
        uint32_t fn = (uint32_t)(((uint64_t)cents_inc(m->pitch16, c, m->fine) * SID_FREQ_R32 + 0x80000000u) >> 32);
        inc[k] = (uint32_t)(((uint64_t)(fn > 0xffffu ? 0xffffu : fn) * SID_FREQ_K16) >> 16);
        rsh[k]=rcp[k]=0;
        if(edge){
            for(rsh[k]=0;inc[k]>>rsh[k]>=65536u;rsh[k]++);
            rcp[k]=inc[k]?0xffffffffu/(inc[k]>>rsh[k]):0u;
        }
        s->inc_cache[k]=inc[k];s->rcp_cache[k]=rcp[k];s->rsh_cache[k]=rsh[k];
    }
    s->pitch_key=m->pitch16;s->fine_key=m->fine;s->shape_key=shp;s->tune_key=tuning_a4;
    s->stack_key=stack;s->edge_key=edge;s->freq_valid=1;
    }else for(k=0;k<nosc;k++){inc[k]=s->inc_cache[k];rcp[k]=s->rcp_cache[k];rsh[k]=s->rsh_cache[k];}
    if (!hold) {
        s->pt_held = (uint32_t)clamp(p[P_E3] * 32 + (sync || ring ? 0 : shp / 32), 0, 4095) << 20;
        fcr = clamp(p[P_E4] * 16 + (p[P_E4] >> 3) + (m->cutoff >> 4), 0, 2047);
        g = gt[fcr >> 4] + (((gt[(fcr >> 4) + 1] - gt[fcr >> 4]) * (fcr & 15)) >> 4);
        kd = SID_RES_K[model][clamp(p[P_E5], 0, 15)];
        if(!s->filter_valid || s->fc_key!=g || s->kd_key!=kd){
            tsvf_coef_gk(&s->filter_cache,g,kd);s->fc_key=g;s->kd_key=kd;s->filter_valid=1;
        }
    }
    pt = s->pt_held;
    kd = s->kd_key;
    fc=s->filter_cache;
    sl = bits & 1u ? 1 : 0;                            /* the chip's sum: LP and HP against BP */
    sb = bits & 2u ? -1 : 0;
    sh = bits & 4u ? 1 : 0;
    gin = 4096 + p[P_E7] * 64;                         /* DRIVE: into the filter x1..x3, out x1..x1/2 */
    gout = (4096 << 12) / (4096 + p[P_E7] * 32);
    a0 = mulq15(mulq15(sid_edac(model, s->env_prev), norm), vel);   /* the envelope DAC over the block */
    a1 = mulq15(mulq15(sid_edac(model, s->env), norm), vel);
    for (i = 0; i < n; i++) {
        int32_t xn = 0, pre = 0, now = 0, x, f;
        uint32_t reset = 0, j;
        ph[0] += inc[0];                               /* (a voice the stack leaves out: inc 0) */
        ph[1] += inc[1];
        ph[2] += inc[2];
        if (sync && ph[0] - 0x80000000u < inc[0]) {   /* voice 1's MSB rose: voice 2 restarts from 0 */
            uint32_t d = sid_frac(ph[0] - 0x80000000u, rcp[0], rsh[0]);
            uint32_t after = (uint32_t)(((uint64_t)inc[1] * d) >> 16), at = ph[1] - after;
            sid_blep(sid_dac(dac, sid_wave12(wave, wt, 0, 0, pt, noise[1])) -
                     sid_dac(dac, sid_wave12(wave, wt, at, at, pt, noise[1])), d, &pre, &now);
            ph[1] = after;
            reset = 2u;
        }
        for (j = 0; j < nh; j++) {
            uint32_t q = ph[k = hl[j]], w;
            if (noisy) {                               /* the noise register: clocked by accumulator bit 19 */
                if (((((q - inc[k]) ^ 0x08000000u) & 0x0fffffffu) + inc[k]) >> 28) {
                    noise[k] = sid_noise_advance(noise[k], q - inc[k], inc[k]);
                    nb[k] = sid_noise_bits(noise[k]);
                }
                w = nb[k];
            } else {
                w = sid_wave_out(wb, wt, q, q ^ (~ph[1] & rmask), pt);
                if (pull)                              /* output bit 11 low pulls the accumulator's MSB low */
                    ph[k] = q &= ((w & 0x800u) << 20) | 0x7fffffffu;
            }
            xn += sid_dac(dac, w);
            if (blep && !(reset >> k & 1u)) {          /* its steps band-limited (not when sync just restarted it) */
                if (q < inc[k] && (wave == SID_SAW || pt))   /* the accumulator wrapped: saw and pulse fall */
                    sid_blep(-full, sid_frac(q, rcp[k], rsh[k]), &pre, &now);
                if (wave == SID_PULSE && pt && q - pt < inc[k])   /* the comparator: the pulse rises */
                    sid_blep(full, sid_frac(q - pt, rcp[k], rsh[k]), &pre, &now);
            }
        }
        x = held + pre;                                /* the sample before, its edges band-limited */
        held = xn - zero + now;
        x = (x * (a0 + (((a1 - a0) * (int32_t)i) >> CTL_LOG2))) >> 15;
        if (bits) {                                    /* the filter: a TPT state variable filter */
            int32_t in = (x * gin) >> 12, v3 = in - ic2, bp, lp, hp;
            bp = soft_knee((fc.a1 * ic1 + fc.a2 * v3) >> 13, knee);
            lp = soft_knee(ic2 + ((fc.a2 * ic1 + fc.a3 * v3) >> 13), knee);
            ic1 = clamp(2 * bp - ic1, -150000, 150000);
            ic2 = clamp(2 * lp - ic2, -150000, 150000);
            hp = in - lp - ((bp * kd) >> 12);
            f = ((lp * sl + bp * sb + hp * sh) * gout) >> 12;
        } else                                         /* OFF: no voice routed to the filter */
            f = (soft_knee((x * gin) >> 12, knee) * gout) >> 12;
        xlp += (int32_t)(((int64_t)(clamp(f, -131071, 131071) - xlp) * SID_XLP_A) >> 15);
        xhp += (int32_t)(((int64_t)((xlp << 8) - xhp) * SID_XHP_A) >> 16);
        o = clamp(xlp - (xhp >> 8), -32767, 32767);
        out[i] += voice_amp(o << 1, m, i);             /* (one voice peaks near half scale) */
    }
    for (k = 0; k < 3u; k++) {
        v->ph[k] = ph[k];
        s->noise[k] = noise[k];
    }
    s->held = held;
    s->ic1 = ic1; s->ic2 = ic2; s->xlp = xlp; s->xhp = xhp;
    /* settled: what is left is a tail below -50 dB or the integrators' rounding residue (a few LSB of DC) */
    s->quiet = o > -32 && o < 32 && xlp > -128 && xlp < 128 && ic1 > -256 && ic1 < 256 && ic2 > -256 && ic2 < 256;
}


static uint32_t seed=0x5288173u;
static uint32_t random_u(void){seed=seed*1664525u+1013904223u;return seed;}
static track_t *setup(void) {
 memset(trk,0,sizeof trk);host_tracks_init();host_preset(&trk[0],3,0);
 track_t *t=&trk[0];voice_t *v=&t->v[0];v->vel=110;v->note=60;v->gate=1;voice_was=0;sid_note_on(t,v);return t;
}
static void parity(void) {
 track_t *t=setup();voice_t *v=&t->v[0];sid_voice_t *s=sid_voice(t,v);
 int32_t a[CTL],b[CTL];vmod_t m={0};uint32_t cases=0;
 for(uint32_t config=0;config<4*8*8*8;config++) {
   t->p[P_E0]=config%4;t->p[P_E1]=(config/4)%8;t->p[P_E2]=(config/32)%8;t->p[P_E6]=(config/256)%8;
   t->p[P_E3]=random_u()%128;t->p[P_E4]=random_u()%128;t->p[P_E5]=random_u()%16;t->p[P_E7]=random_u()%128;
   for(uint32_t n=0;n<=CTL;n++) {
     if(n%7==0){v->ph[0]=random_u();v->ph[1]=random_u();v->ph[2]=random_u();}
     if(n%11==0){v->gate^=1;sid_note_on(t,v);}
     sid_done(t,v);tuning_a4=A4_MIN+random_u()%(A4_MAX-A4_MIN+1u);
     m.pitch16=random_u()%2048;m.fine=(int32_t)(random_u()%201)-100;m.cutoff=(int32_t)(random_u()%65537)-32768;
     m.shape=random_u()%(128u<<8);m.amp0=random_u()%32769;m.amp1=random_u()%32769;
     for(uint32_t k=0;k<CTL;k++)a[k]=b[k]=(int32_t)(random_u()%127)-63;
     voice_t old=*v;sid_voice_t old_s=*s;
     sid_render_reference(t,v,a,n,&m,t->p);voice_t want=*v;sid_voice_t want_s=*s;
     *v=old;*s=old_s;sid_render_into(t,v,b,n,&m,t->p);
     assert(!memcmp(a,b,sizeof a));assert(!memcmp(&want,v,sizeof want));assert(!memcmp(&want_s,s,sizeof want_s));cases++;
   }
 }
 tuning_a4=440;
 printf("SID renderer: %u blocks, all models/waves/stacks/filter modes, 0..32 samples, edits/A4/gates, PCM and complete voice state exact\n",cases);
}
static volatile uint32_t checksum;
static void benchmark(void) {
 puts("model,stack,wave,filter,reference_instructions_per_sample,probe_instructions_per_sample");
 for(uint32_t config=0;config<8*8*2+1;config++) {
   static const uint8_t stacks[]={SID_ONE,SID_DETUNE,SID_SYNC,SID_RING,SID_DETUNE,SID_DETUNE,SID_ONE,SID_SUB};
   static const uint8_t waves[]={SID_PULSE,SID_PULSE,SID_SAW,SID_TRI,SID_NOISE,SID_TRI_SAW,SID_SAW,SID_TRI_SAW_PULSE};
   uint32_t stack=stacks[config/8%8],wave=waves[config/8%8];
   double cost[2];
   for(uint32_t fast=0;fast<2;fast++) {
     track_t *t=setup();voice_t *v=&t->v[0];sid_voice_t *s=sid_voice(t,v);
     t->p[P_E0]=(config/64)%2;t->p[P_E1]=wave;t->p[P_E2]=stack;t->p[P_E6]=config%8;t->p[P_E7]=60;
     s->env=s->env_prev=255;
     vmod_t m={0};m.pitch16=60*16;m.shape=64<<8;m.amp0=m.amp1=32768;
     void (*volatile run)(track_t *,voice_t *,int32_t *,uint32_t,const vmod_t *,const int16_t *)=fast?sid_render_into:sid_render_reference;
     int32_t out[CTL];uint64_t start=instructions();
     for(uint32_t i=0;i<10000;i++){
       if(config==128){t->p[P_E6]=i%8;t->p[P_E7]=i%128;m.pitch16=24*16+(i%1280);m.shape=(i%128)<<8;}
       memset(out,0,sizeof out);run(t,v,out,CTL,&m,t->p);checksum+=(uint32_t)out[i%CTL];}
     cost[fast]=(double)(instructions()-start)/(10000*CTL);
   }
   printf("%u,%u,%u,%u,%.2f,%.2f\n",(config/64)%2,stack,wave,config==128?8:config%8,cost[0],cost[1]);
   if(cost[0]) {
     assert(cost[1]<cost[0]*1.04);
     if(config<128 && (config%8==SID_LP || config%8==SID_BP || config%8==SID_LP_BP))assert(cost[1]<cost[0]*.98);
   }
 }
}
int main(int argc,char **argv){parity();if(argc==1)benchmark();return 0;}
