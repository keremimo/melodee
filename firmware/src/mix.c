/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "performance_options.h"
/* Engine output -> optional stereo insert -> track level -> master.
 * The bypass retains the original level/pan/spread path exactly.
 * From the track level on the bus is Q15 << MIX_FRAC (dsp.c): mix_l / mix_r, the stems, the master and mix_block's
 * output. The FX sends and the FX themselves stay Q15 (fx.c), their return goes back onto the bus. */
#include "fx.c"
#define MIX_TRACK_MAX (524287 * MIX_ONE)         /* a track after its level: +24 dB over full scale */
#define MIX_BUS_MAX ((1 << 29) - 1)              /* the master's input: four tracks at MIX_TRACK_MAX (x - dc fits) */
static int32_t part_storage[CTL],mix_l[CTL],mix_r[CTL];
static int32_t *part_buf=part_storage;
static uint8_t fx_send_on;
static uint32_t fx_extra_gain[FX_SLOTS-1u];
static __attribute__((noinline,section(".audio_text"))) void mix_extra_sends(uint32_t n)
{
    for(uint32_t i=0;i<n;i++){
        int32_t l=mix_l[i]-fx_bus_sum_l[i],r=mix_r[i]-fx_bus_sum_r[i];
        for(uint32_t slot=0;slot<FX_SLOTS-1u;slot++)if(fx_extra_gain[slot]){
            fx_send_extra_l[slot][i]+=fx_scale(l,fx_extra_gain[slot]);fx_send_extra_r[slot][i]+=fx_scale(r,fx_extra_gain[slot]);
        }
    }
}
/* l, r on the bus; send (fx_send_gain) takes them down to the FX's Q15 */
static inline __attribute__((always_inline)) void mix_add(int32_t l,int32_t r,uint32_t i,uint32_t send)
{
    mix_l[i]+=l;mix_r[i]+=r;if(send){fx_send_l[i]+=fx_scale(l,send);fx_send_r[i]+=fx_scale(r,send);}
}
/* x (the bus) x g / 4096: the pan laws' Q12 gains */
static inline __attribute__((always_inline)) int32_t mix_q12(int32_t x, int32_t g)
{
    return (int32_t)(((int64_t)x * g) >> 12);
}
/* the FX return (Q15, fx.c) at ret (Q16) onto the bus */
static inline __attribute__((always_inline)) int32_t mix_fx_return(int32_t wet, uint32_t ret)
{
    return fx_scale(clamp(wet, -(MIX_BUS_MAX >> MIX_FRAC), MIX_BUS_MAX >> MIX_FRAC) * MIX_ONE, ret);
}
static volatile uint32_t kb_mask, kb_layer;
static volatile uint8_t kb_lock;
#define LIM_T (26000 * MIX_ONE)                  /* -2 dBFS: only overs, a chord does not duck a held note (#108) */
static int32_t lim_env = LIM_T;
static int32_t dc_l, dc_r, dce_l, dce_r;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input subtracted from it (the bus's MIX_FRAC low bits
 * are the state's fraction). The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next
 * one, so no part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (A rounded step of (x - dc) / 4096 would stop moving at |x - dc| < 2048 and leave an offset at the output after
 * silence.) |x| <= MIX_BUS_MAX: x - dc fits */
static inline __attribute__((always_inline)) int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = x - *dc + *err, d = e >> 12;
    *err = e - d * 4096;
    *dc += d;
    return x - *dc;
}

/* linear up to 24576 (-2.5 dBFS), above it 4096 tanh(d / 4096): slope 1 at the knee, ceiling 28524 (-1.2 dBFS),
 * on the bus (x MIX_ONE). Only what the limiter's attack lets through bends (a held -2 dBFS peak loses 0.02 dB); a
 * full tanh here bent every sample and turned chords into intermodulation (-26 dB on a Prophet chord, -52 dB now) */
#define KNEE (24576 * MIX_ONE)
static inline __attribute__((always_inline)) int32_t master_knee(int32_t y)
{
    int32_t a = y < 0 ? -y : y;
    if (a <= KNEE)
        return y;
    a -= KNEE;
    /* softclip's input d / 4096 in Q15 (flat from 65536 on), its output / 8 back on the bus */
    a = KNEE + softclip((a < 8192 * MIX_ONE ? a : 8192 * MIX_ONE) >> (MIX_FRAC - 3)) * (MIX_ONE >> 3);
    return y < 0 ? -a : a;
}

static __attribute__((noinline,section(".dsp_text"))) void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a;
    *l = dc_block(clamp(*l, -MIX_BUS_MAX, MIX_BUS_MAX), &dc_l, &dce_l);
    *r = dc_block(clamp(*r, -MIX_BUS_MAX, MIX_BUS_MAX), &dc_r, &dce_r);
#ifdef MELODEE_MASTER_RAW
    if (MELODEE_MASTER_RAW) return;                     /* (web/core/melodee_core.c: an export without the limiter) */
#endif
    al = *l < 0 ? -*l : *l;
    ar = *r < 0 ? -*r : *r;
    a = al > ar ? al : ar;
    if (a > lim_env)
        lim_env += (a - lim_env) >> 2;
    else if (lim_env > LIM_T)
        lim_env -= ((lim_env - LIM_T) >> 12) + MIX_ONE;
    if (lim_env > LIM_T) {
        int32_t g = (int32_t)(((uint32_t)(LIM_T >> MIX_FRAC) << 15) / (uint32_t)(lim_env >> MIX_FRAC));   /* <= 32768 */
        *l = (int32_t)(((int64_t)*l * g) >> 15);
        *r = (int32_t)(((int64_t)*r * g) >> 15);
    }
    *l = master_knee(*l);
    *r = master_knee(*r);
}

/* length of one division (N_DIV order) in samples at the song tempo */
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* original IDs stay fixed; slow rates append */
static uint8_t midi_sync_active;                 /* armed by an incoming Start/Continue/Stop, never a clock pulse */
static uint32_t midi_beat_samples;                    /* zero until an external clock has a measured tempo */
static uint32_t beat_samples(void)
{
    return midi_sync_active && midi_beat_samples ? midi_beat_samples : (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM];
}
static uint32_t division_samples(uint32_t quarter, uint32_t div)
{
    return div < 6u ? quarter / DIV_DEN[div] : div < 10u ? quarter << (div - 5u) : quarter / DIV_DEN[div % 6u];
}
static uint32_t div_samples(uint32_t div) { return division_samples(beat_samples(), div); }

#include "punch.c"

/* The sequencer runs on musical units on every clock, so tracks, recordings, ARPs and the clock out cannot slide against
 * each other: 24 clocks/beat and every division divide exactly. A tempo only sets how fast units pass (int_clock_units,
 * or an external clock's pulses) and the display; it never moves an already recorded step. */
#define MIDI_BEAT_UNITS 24576u
static uint32_t seq_beat_samples(void)
{
    return MIDI_BEAT_UNITS;
}
/* CLK INT: audio samples to units at exactly the BPM, the remainder carried. CTL samples at a time stay in 32 bits. */
static uint32_t int_clock_rem;
static uint32_t int_clock_units(uint32_t n)
{
    uint32_t per = (uint32_t)FS * 60u, units = 0;
    while (n) {
        uint32_t chunk = n < CTL ? n : CTL, ticks = chunk * MIDI_BEAT_UNITS * (uint32_t)song.g[G_BPM] + int_clock_rem;
        int_clock_rem = ticks % per;
        units += ticks / per;
        n -= chunk;
    }
    return units;
}
static uint32_t seq_div_samples(uint32_t div)
{
    return division_samples(seq_beat_samples(), div);
}


static void events_block(uint32_t n);
#if MELODEE_USB_AUDIO
static int ua_capture_active(void);
static uint8_t track_capture_on, master_capture_on;   /* track_capture holds the stems, or the stereo mix at unity */
static int32_t track_capture[CTL * NPART];
#endif

typedef struct {
    int16_t level, pan;
    uint8_t valid;
    int32_t lvl, gl, gr;
} mix_cache_t;
static mix_cache_t mix_cache[NPART];
static __attribute__((noinline)) void mix_cache_update(const track_t *t, mix_cache_t *mc)
{
    mc->level=t->p[P_LEVEL];mc->pan=t->p[P_PAN];mc->valid=1;
    mc->lvl=LEVEL_Q12[mc->level & 127];
    mc->gl=4096-(mc->pan>0?mc->pan*64:0);mc->gr=4096+(mc->pan<0?mc->pan*64:0);
}
/* b (the part's sum, +-884000) x lvl (Q12, up to +7.5 dB) / 4096 onto the bus, rounded: the MIX_FRAC bits below Q15
 * stay (no dither: the cut is far below the DAC's). b = 0 or lvl = 0 stays 0 */
static inline __attribute__((always_inline)) int32_t mix_level(int32_t b, int32_t lvl)
{
    b = clamp(b, -884000, 884000);
    return clamp((int32_t)(((int64_t)b * lvl + (1 << (11 - MIX_FRAC))) >> (12 - MIX_FRAC)), -MIX_TRACK_MAX, MIX_TRACK_MAX);
}
/* a track's meter stays Q15 (trk_peak_take); the loops below keep it on the bus scale while they run */
#define PEAK_IN(t) ((t)->peak * MIX_ONE)
#define PEAK_OUT(t, pk) ((t)->peak = (pk) >> MIX_FRAC)
static __attribute__((noinline)) void mix_spread(track_t *t, int32_t *b, uint32_t n)
{
    uint32_t send=fx_send_on?fx_send_gain(t):0u;
    int32_t *d=spread_buf;
    int32_t lvl=LEVEL_Q12[t->p[P_LEVEL] & 127],pan=t->p[P_PAN],s=(t->p[P_SPRD]+1)>>1;
    int32_t pa=clamp(pan-s,-64,63),pb=clamp(pan+s,-64,63);
    int32_t gla=4096-(pa>0?pa*64:0),gra=4096+(pa<0?pa*64:0);
    int32_t glb=4096-(pb>0?pb*64:0),grb=4096+(pb<0?pb*64:0);
    int32_t glm=(gla+glb)>>1,grm=(gra+grb)>>1,gld=(gla-glb)>>1,grd=(gra-grb)>>1,pk=PEAK_IN(t);
    for(uint32_t i=0;i<n;i++){
        int32_t x=mix_level(b[i],lvl);
        int32_t y=mix_level((d[i]<<1)-b[i],lvl);
        int32_t a=x<0?-x:x;
#if MELODEE_USB_AUDIO
        if(track_capture_on)track_capture[i*NPART+(uint32_t)(t-trk)]=x;
#endif
        if(a>pk)pk=a;
        mix_add(mix_q12(x,glm)+mix_q12(y,gld),mix_q12(x,grm)+mix_q12(y,grd),i,send);
    }
    PEAK_OUT(t,pk);
}
#if MELODEE_MODULES
/* host 5 (mod_abi3.h stereo, eng_module.c MOD_HOST3): mix_part names the part it renders; its module's post, on this
 * core after the part's voices, may take the part's left side (spread_buf, the batch's or the part's own) and leave
 * L + R in its block. A render outside mix_part's gets 0 and stays mono */
static const track_t *mix_st_part;
static uint8_t mix_st_on;
static int32_t *mix_h_stereo(const track_t *t)
{
    if(t!=mix_st_part)return 0;
    mix_st_on=1;
    return spread_buf;
}
/* .. such a part, b its L + R, spread_buf its L: each side levelled on its own (its own dither place, as SPREAD's two),
 * then PAN as a balance, the mono law's gains one a side (centre: L and R as they are; right: the left side fades).
 * The FX sends take the sides post balance, as a mono part's post pan; its meter the louder side, its stem (USB
 * capture) their middle */
static __attribute__((noinline)) void mix_stereo(track_t *t,const int32_t *b,uint32_t n)
{
    mix_cache_t *mc=&mix_cache[(uint32_t)(t-trk)];
    if(!mc->valid || mc->level!=t->p[P_LEVEL] || mc->pan!=t->p[P_PAN])mix_cache_update(t,mc);
    uint32_t send=fx_send_on?fx_send_gain(t):0u;
    const int32_t *d=spread_buf;
    int32_t lvl=mc->lvl,gl=mc->gl,gr=mc->gr,pk=PEAK_IN(t);
    for(uint32_t i=0;i<n;i++){
        int32_t l=mix_level(d[i],lvl),r=mix_level(b[i]-d[i],lvl);
        int32_t al=l<0?-l:l,ar=r<0?-r:r;
#if MELODEE_USB_AUDIO
        if(track_capture_on)track_capture[i*NPART+(uint32_t)(t-trk)]=(l+r)>>1;
#endif
        if(al>pk)pk=al;
        if(ar>pk)pk=ar;
        mix_add(mix_q12(l,gl),mix_q12(r,gr),i,send);
    }
    PEAK_OUT(t,pk);
}
#endif
#if MELODEE_MIX_FAST
#define MIX_MONO_GENERAL mix_mono_general
#else
#define MIX_MONO_GENERAL mix_mono_samples
#endif
static __attribute__((noinline,section(".dsp_text"))) void MIX_MONO_GENERAL(track_t *t,int32_t *b,uint32_t n,const mix_cache_t *mc)
{
    uint32_t send=fx_send_on?fx_send_gain(t):0u;
    int32_t lvl=mc->lvl,gl=mc->gl,gr=mc->gr,pk=PEAK_IN(t);
    for(uint32_t i=0;i<n;i++){
        int32_t x=mix_level(b[i],lvl),a=x<0?-x:x;
#if MELODEE_USB_AUDIO
        if(track_capture_on)track_capture[i*NPART+(uint32_t)(t-trk)]=x;
#endif
        if(a>pk)pk=a;
        mix_add(mix_q12(x,gl),mix_q12(x,gr),i,send);
    }
    PEAK_OUT(t,pk);
}
#undef MIX_MONO_GENERAL
#if MELODEE_MIX_FAST
/* Centered dry tracks need neither pan multiplications nor send-buffer
 * reads/writes. Keep this loop separate from the centered-send path. */
static __attribute__((noinline,section(".dsp_text"))) void mix_mono_dry(track_t *t,int32_t *b,uint32_t n,const mix_cache_t *mc)
{
    int32_t lvl=mc->lvl,pk=PEAK_IN(t);
    for(uint32_t i=0;i<n;i++){
        int32_t x=mix_level(b[i],lvl),a=x<0?-x:x;
#if MELODEE_USB_AUDIO
        if(track_capture_on)track_capture[i*NPART+(uint32_t)(t-trk)]=x;
#endif
        if(a>pk)pk=a;
        mix_l[i]+=x;mix_r[i]+=x;
    }
    PEAK_OUT(t,pk);
}
static __attribute__((noinline,section(".dsp_text"))) void mix_mono_samples(track_t *t,int32_t *b,uint32_t n,const mix_cache_t *mc)
{
    uint32_t send=fx_send_on?fx_send_gain(t):0u;
    if(mc->pan){mix_mono_general(t,b,n,mc);return;}
    if(!send){mix_mono_dry(t,b,n,mc);return;}
    /* centred: unity pan is x itself (mix_q12(x, 4096) == x for any x on the bus).
     * Equal channels also share the send's scaling work.
     * Keep each loop separate to avoid register pressure in the general path. */
    int32_t lvl=mc->lvl,pk=PEAK_IN(t);
    for(uint32_t i=0;i<n;i++){
        int32_t x=mix_level(b[i],lvl),a=x<0?-x:x;
#if MELODEE_USB_AUDIO
        if(track_capture_on)track_capture[i*NPART+(uint32_t)(t-trk)]=x;
#endif
        if(a>pk)pk=a;
        mix_add(x,x,i,send);
    }
    PEAK_OUT(t,pk);
}
#endif

static int32_t solo_gain[NPART]={32768,32768,32768,32768};
static void mix_solo(track_t *t,int32_t *b,uint32_t n)
{
    uint32_t ti=(uint32_t)(t-trk);
    /* muted: the notes still sounding go too, not only the new ones (voice.c) */
    int32_t from=solo_gain[ti],to=!t->p[P_MUTE] && (!track_solo || ((track_solo>>ti)&1u))?32768:0;
    if(from==32768 && to==32768)return;
    for(uint32_t i=0;i<n;i++){
        from += clamp(to-from,-256,256); /* 128 samples, same ramp for GLO solo and MUTE */
        b[i]=mulq16(b[i],(uint32_t)from<<1);
        if(spread_on)spread_buf[i]=mulq16(spread_buf[i],(uint32_t)from<<1);
    }
    solo_gain[ti]=from;
}
#if MELODEE_BATCH_AUDIO
static int32_t batch_pcm[NPART][CTL],batch_spread[NPART][CTL];
static uint32_t batch_rendered[NPART];static uint8_t batch_spread_on[NPART],batch_ready;
static __typeof__(mod) batch_mod[NPART];
#if MELODEE_DUAL_CORE
static uint8_t batch_posted[NPART];
#endif
static int16_t batch_params[NPART][P_COUNT];
static __attribute__((noinline)) void mix_prepare_batch(uint32_t n,uint32_t nt)   /* nt: the tracks in use */
{
    audio_batch_active=1;
#if MELODEE_DUAL_CORE
    memset(batch_posted,0,sizeof batch_posted);
    /* Keep the measured CPU0 service reserve for native FM6 workloads (engine_t.reserve: FM6's voices sounding or
     * running on). Per-voice controls and ghosts themselves are costed queue jobs. */
    audio_batch_reserve=0;
    for(uint32_t p=0;p<nt && !audio_batch_reserve;p++){
        const engine_t *g=eng_of(trk[p].engine);
        if(!g->reserve)continue;
#if MELODEE_MODULES
        MOD_AUDIO_V(trk[p].engine,audio_batch_reserve=g->reserve(resource[RES_ENGINE0+p].ptr,resource[RES_ENGINE0+p].size)!=0);
#else
        audio_batch_reserve=g->reserve(resource[RES_ENGINE0+p].ptr,resource[RES_ENGINE0+p].size)!=0;
#endif
    }
#endif
    for(uint32_t p=0;p<nt;p++){
        track_t *t=&trk[p];
        if(t->engine==ENGI_MIDI){batch_rendered[p]=0;continue;}
        mod_begin(t);batch_mod[p]=mod;memcpy(batch_params[p],t->p,sizeof t->p);
        spread_on=t->p[P_SPRD]>0 && (trk_vmode(t)==V_POLY || trk_vmode(t)==V_UNISON);
        batch_spread_on[p]=spread_on;spread_buf=batch_spread[p];
        batch_rendered[p]=track_render_audio(t,batch_pcm[p],n,0);
        if(mod.on)mod_end(t);
    }
    audio_batch_flush();audio_batch_active=0;batch_ready=1;
}
#endif
static __attribute__((noinline)) void mix_part(track_t *t,uint32_t n)
{
    if(t->engine==ENGI_MIDI){t->peak=0;return;}
    uint32_t extra=0;
    if(fx_send_on && fx_bus_separate)for(uint32_t slot=0;slot<FX_SLOTS-1u;slot++){
        uint32_t amount=(uint32_t)t->p[P_FXSEND2+slot];fx_extra_gain[slot]=(amount+(amount==127u))*(512u>>MIX_FRAC);extra|=amount;
    }
    /* Reuse the return workspace until the worker starts. The difference is
     * this track's exact post-fader/pan/solo contribution; CHAIN's sample loops
     * stay unchanged and zero extra sends need no extra mixing pass. */
    if(extra){memcpy(fx_bus_sum_l,mix_l,n*sizeof *mix_l);memcpy(fx_bus_sum_r,mix_r,n*sizeof *mix_r);}
    int rendered;
    uint32_t st=0;
#if MELODEE_MODULES
    mix_st_part=t;mix_st_on=0;
#endif
#if MELODEE_BATCH_AUDIO
    if(batch_ready){
        uint32_t p=(uint32_t)(t-trk);mod=batch_mod[p];
        part_buf=batch_pcm[p];
        if(mod.on)for(uint32_t k=0;k<mod.nk;k++)t->p[mod.kid[k]]=batch_params[p][mod.kid[k]];
        spread_on=batch_spread_on[p];spread_buf=batch_spread[p];rendered=(int)batch_rendered[p];
        if(rendered){
            if(spread_on)spread_sum(part_buf);
            const engine_t *e=ENGINES[t->engine];
#if MELODEE_DUAL_CORE
            if(!batch_posted[p] && e->post)
#else
            if(e->post)
#endif
#if MELODEE_MODULES
                MOD_AUDIO_V(t->engine,e->post(t,part_buf,n,(uint32_t)rendered));
#else
                e->post(t,part_buf,n,(uint32_t)rendered);
#endif
        }
    }else
#endif
    {
        part_buf=part_storage;spread_buf=spread_storage;mod_begin(t);
        spread_on=t->p[P_SPRD]>0 && (trk_vmode(t)==V_POLY || trk_vmode(t)==V_UNISON);
        rendered=(int)track_render_audio(t,part_buf,n,0);
    }
#if MELODEE_MODULES
    mix_st_part=0;
    st=rendered && mix_st_on;
    if(st)spread_on=1;                                  /* (mix_solo's ramp on both sides) */
#endif
#if defined(MELODEE_FX_PROFILE) && MELODEE_FX_PROFILE && defined(FM1_IRQ_TARGET)
    if(fx_profile_control>>30u && fx_profile_track((uint32_t)(t-trk))){for(uint32_t i=0;i<n;i++)part_buf[i]=((i&8u)?-8000:8000)+(int32_t)i*97;rendered=1;spread_on=0;st=0;}
#endif
    AP_START(mix_start);
    if(rendered){
        mix_solo(t,part_buf,n);
#if MELODEE_MODULES
        if(st)mix_stereo(t,part_buf,n);
        else
#endif
        if(spread_on)mix_spread(t,part_buf,n);
        else{
            mix_cache_t *mc=&mix_cache[(uint32_t)(t-trk)];
            if(!mc->valid || mc->level!=t->p[P_LEVEL] || mc->pan!=t->p[P_PAN])mix_cache_update(t,mc);
            void (*volatile mix)(track_t *,int32_t *,uint32_t,const mix_cache_t *)=mix_mono_samples;
            mix(t,part_buf,n,mc);
        }
    }
    AP_END(AP_MIX,mix_start);
    if(mod.on)mod_end(t);
    if(extra){void (*volatile run)(uint32_t)=mix_extra_sends;run(n);}
}
static __attribute__((noinline,section(".dsp_text"))) void master_samples(int32_t *out,uint32_t n)
{
    int32_t gain=fx_bus_mix,target=fx_bus_target;
    int32_t master=(int32_t)song.master_q12;
    /* With no wet result the sample pass only rewrites the dry mix. Advance
     * its monotone return ramp once; keep per-sample rounding when audible. */
    if(!fx_bus_result)gain+=clamp(target-gain,-(int32_t)n*128,(int32_t)n*128);
    else for(uint32_t i=0;i<n;i++){
        if(gain!=target)gain+=clamp(target-gain,-128,128);
        int32_t l=mix_l[i],r=mix_r[i];
        {uint32_t ret=(uint32_t)gain*2u;
            l+=mix_fx_return(fx_left[i],ret);
            r+=mix_fx_return(fx_right[i],ret);}
        mix_l[i]=l;mix_r[i]=r;
    }
    punch_process(mix_l,mix_r,n);
    /* Attenuate the finished output: punch history, freeze and the DC blocker
     * keep running at unity, so even an already sounding tail follows MASTER. */
    for(uint32_t i=0;i<n;i++){int32_t l=mix_l[i],r=mix_r[i];master_out(&l,&r);
#if MELODEE_USB_AUDIO
        if(master_capture_on){track_capture[2u*i]=l;track_capture[2u*i+1u]=r;}   /* (USB stereo capture: before MASTER, #103) */
#endif
        if(master==4096){out[2u*i]=l;out[2u*i+1u]=r;continue;}   /* (full MASTER: nothing to cut) */
        out[2u*i]=(int32_t)(((int64_t)l*master+2048)>>12);out[2u*i+1u]=(int32_t)(((int64_t)r*master+2048)>>12);}
    fx_bus_mix=gain;
}
static __attribute__((noinline)) void mix_block(int32_t *out,uint32_t n)
{
#if defined(MELODEE_FX_PROFILE) && MELODEE_FX_PROFILE && defined(FM1_IRQ_TARGET)
    fx_profile_poll();
#endif
#if MELODEE_USB_AUDIO
    {int cap=ua_capture_active();track_capture_on=(uint8_t)(cap==4);master_capture_on=(uint8_t)(cap==2);}
    if(track_capture_on)memset(track_capture,0,n*NPART*sizeof *track_capture);
#endif
    memset(mix_l,0,n*sizeof *mix_l);memset(mix_r,0,n*sizeof *mix_r);
    dither_block();                                     /* (before any engine renders: dsp.c) */
    AP_START(events_start);events_block(n);AP_END(AP_EVENTS,events_start);
    fx_bus_select();fx_send_on=(uint8_t)fx_bus_active();if(fx_send_on){memset(fx_send_l,0,n*sizeof *fx_send_l);memset(fx_send_r,0,n*sizeof *fx_send_r);
        if(fx_bus_separate){memset(fx_send_extra_l,0,sizeof fx_send_extra_l);memset(fx_send_extra_r,0,sizeof fx_send_extra_r);}}
    uint32_t nt=trk_parts();                            /* (the tracks not in use, the MIDI-only ones: none of their CPU) */
#if MELODEE_BATCH_AUDIO
    mix_prepare_batch(n,nt);
#endif
    for(uint32_t i=0;i<nt;i++)mix_part(&trk[i],n);
#if MELODEE_BATCH_AUDIO
    batch_ready=0;part_buf=part_storage;spread_buf=spread_storage;
#endif
    fx_bus_start(n);
    /* MASTER is applied once, after punch FX and the limiter/DC blocker (the bus keeps every bit: #100) */
    fx_bus_join();
    void (*volatile run)(int32_t *,uint32_t)=master_samples;
    AP_START(output_start);run(out,n);AP_END(AP_OUTPUT,output_start);
}
#ifndef FM1_IRQ_TARGET
/* (host tests: mix_block's output at the 16-bit scale, Q15, rounded; regress hashes the bus itself) */
static __attribute__((unused)) void mix_block_q15(int32_t *out,uint32_t n)
{
    mix_block(out,n);
    for(uint32_t i=0;i<2u*n;i++)out[i]=(out[i]+(MIX_ONE>>1))>>MIX_FRAC;
}
#endif

/* A track's meter has two readers (0 the screen's meters, 1 the editor's METERS push): each takes the peak (|sample|,
 * Q15, after level, solo and mute) since its own last read. Main loop only */
static int32_t peak_seen[2][NTRK];
static int32_t trk_peak_take(uint32_t k, uint32_t who)
{
    int32_t p = trk[k].peak, r;
    trk[k].peak = 0;
    for (uint32_t c = 0; c < 2u; c++)
        if (p > peak_seen[c][k])
            peak_seen[c][k] = p;
    r = peak_seen[who][k];
    peak_seen[who][k] = 0;
    return r;
}
