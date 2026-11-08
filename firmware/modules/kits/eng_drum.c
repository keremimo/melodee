/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DRUM: synthesized TR-808 circuits, eight mono lanes per part, with GM note mapping.
 * Retriggers reuse a lane's voice; closed hats choke open hats. Key release does not end a hit.
 * KIT retains stored value 4 for existing patches. All older custom kits now use the 808.
 * Parameters and output processing remain live while the hit rings. No recorded material.
 * KIT SID (drum_sid.c): one Commodore 6581 for the part, on one voice; its drums are C64 frame programs,
 * DIGI ON plays KICK and SNARE on the volume register, TONE and RES are its filter's.
 * KIT CR78 (drum_cr78.c) and KIT FM (drum_fm.c): eight lanes on one voice (drum_lanes.c); CR78's KICK is HATS.
 * KIT 1..8 (values 9..16): the user's recorded kits (its data records: kit_store.c checks them, kit_take.c makes a
 * take a pad's for the firmware's Record kit), played by drum_real.c on one voice. Its pads (the firmware's lanes,
 * pages and keys) are its schema's (drum_schema.h). */
#include "x0x/fastmath.h"
#include "drum_map.c"                    /* its lanes, the GM map, the kits' numbers, the MPC pads */
#include "drum_kit.h"                    /* the user kits' records */
#include "drum_patch.h"                  /* the part's patch: the kit's offsets (where the firmware keeps it) */
#include "drum_808.c"
#include "x0x/drum909.c"               /* (another kit's module: folded away, its calls with it) */

typedef struct {
    dr8_t r;
    uint8_t owner;               /* the voice playing the lane: index + 1, 0 = none */
    uint8_t role;                /* the drum struck (DVT_*) */
    uint8_t sound;
    int8_t st;                   /* its semitones from the designed pitch (the GM map) */
} drum_lane_t;

static drum_lane_t *drum_kit_part(uint32_t part);  /* engines.c eng_state: the part's DV_NLANE lanes */

/* Allocate a kit only for its selected model; changing KIT invalidates old owners. */
typedef struct {
    drum909_t d;
    uint8_t owner[D9_NUM], ready;
    float *nz;
} drum909_part_t;
static drum909_part_t *drum909_part(uint32_t part);
typedef struct dsk_part dsk_part_t;
static uint32_t dsk_bytes(void);
typedef struct dlk_part dlk_part_t;
static uint32_t dlk_bytes(void);
typedef struct drk_part drk_part_t;
static uint32_t drk_bytes(void);
static uint32_t drum_bytes(const track_t *t) { return drum_is909(t) ? sizeof(drum909_part_t) : drum_issid(t) ? dsk_bytes() : drum_islane(t) ? dlk_bytes() : drum_isuser(t) ? drk_bytes() : sizeof(drum_lane_t)*DV_NLANE; }
static drum909_part_t *drum909_of(const track_t *t)
{
    drum909_part_t *k = drum909_part(trk_ix(t));
    if (k && !k->ready) { drum909_init(&k->d); k->ready=1; }
    return k;
}
static void drum808_values(const track_t *t,uint32_t sound,int16_t *p)
{
    memcpy(p,t->p,sizeof t->p);const int8_t *c=drum_offs(t)->c[sound];
    p[P_E3]=(int16_t)clamp(p[P_E3]+c[1],0,127);
    if(sound==D9_SD)p[P_E4]=(int16_t)clamp(p[P_E4]+c[2],0,127);
}
static void drum909_values(track_t *t,drum909_part_t *k,uint32_t ins,int32_t pitch,uint32_t vel)
{
    drum909_t *d=&k->d;const int8_t *c=drum_offs(t)->c[ins];
    for(int j=0;j<d9_nspec[ins];j++){
        const d9_pspec_t *s=&d9_specs[ins][j];int32_t value=s->ui.def;
        if(s->field==D9_F_ATTACK || s->field==D9_F_SNAPPY || (ins>=D9_RS && s->field==D9_F_TUNE))value+=c[2]+(s->field==D9_F_SNAPPY?0:t->p[P_E2]-64);
        if(s->field==D9_F_SNAPPY)value+=t->p[P_E4]-64;
        if(s->field==D9_F_TONE)value+=t->p[P_E2]-64;
        d9_set_panel(d,(int)ins,j,value,1);
    }
    float ratio=fm_exp2f((float)(c[0]+pitch)/12.0f+(float)(t->p[P_E1]-64)*3.0f/192.0f);
    float decay=fm_exp2f((float)(c[1]+t->p[P_E3]-64)/24.0f);
    if(ins<=D9_HT){
        static const float dec[5]={1380.0f,320.0f,1700.0f,1050.0f,1100.0f};
        d->bt[ins].decay=dec[ins]*decay;
        if(ins==D9_SD){d->bt[ins].tune*=ratio;d->bt[ins].tune2=325.0f*ratio;d->bt[ins].noise_decay=1200.0f*fm_exp2f((float)(t->p[P_E2]-64)/24.0f)*decay;}
        else if(ins!=D9_BD)d->bt[ins].tune*=ratio;
    }else if(ins==D9_RS){d->rim.tune*=ratio;d->rim.tune2=220.0f*ratio;d->rim.decay=40.0f*decay;d9_rim_retune(&d->rim);}
    else if(ins==D9_CP){d->clap.tune*=ratio;d->clap.tail_decay*=decay;d9_clap_retune(&d->clap);}
    else {d->smp[ins-D9_CH].pitch*=ratio;d->smp[ins-D9_CH].decay*=decay;d9_smp_rate(&d->smp[ins-D9_CH]);}
    /* No sample runs between panel fields. Prepare only their final shape
     * and final tuned filters; histories and trigger ordering stay intact. */
    if(ins<=D9_HT)d9_shape_prep(&d->bt[ins].shape,d->bt[ins].drive,d->bt[ins].dist_type);
    else if(ins==D9_RS)d9_shape_prep(&d->rim.shape,d->rim.drive,d->rim.dist_type);
    else if(ins==D9_CP)d9_shape_prep(&d->clap.shape,d->clap.drive,d->clap.dist_type);
    else d9_shape_prep(&d->smp[ins-D9_CH].shape,d->smp[ins-D9_CH].drive,d->smp[ins-D9_CH].dist_type);
    d->accent=2.0f*(float)dr8_level(vel,t->p[P_E5])/(float)dr8_level(vel,100);
    if(ins==D9_BD && t->p[P_E6])d->bt[0].attack*=0.5f;
    drum909_trigger(d,(int)ins,(float)vel/127.0f);
    if(ins==D9_SD){d->bt[1].decay=340.0f*decay;d9_env_exp(&d->bt[1].amp,0.00001f,d->bt[1].decay*D9_MS);d->bt[1].mute=d9_ceil((d->bt[1].noise_decay+d->bt[1].decay+60.0f)*D9_MS);}
    if(ins==D9_RS){d->rim.decay=200.0f*decay;d9_env_exp(&d->rim.amp,0.00001f,d->rim.decay*D9_MS);d->rim.mute=d9_ceil((d->rim.decay+20.0f)*D9_MS);}
    if(ins==D9_BD){d->bt[0].bd_base*=ratio;d->bt[0].bd_df*=ratio*(t->p[P_E6]?0.5f:1.0f);d->bt[0].pitch.v*=ratio;}
}

static int drum909_live(const drum909_part_t *k, uint32_t ins)
{
    if(ins<=D9_SD)return k->d.bt[ins].mute>0;
    if(ins<=D9_HT)return k->d.tom[ins-D9_LT].mute>0;
    if(ins==D9_RS)return k->d.rim.mute>0;
    if(ins==D9_CP)return k->d.clap.mute>0;
    return k->d.smp[ins-D9_CH].mute>0;
}
static void drum_block(track_t *t)
{
    if(drum_is909(t)) { drum909_part_t *k=drum909_of(t); if(k)k->nz=d9_noise_block(&k->d,CTL); }
}

static const char *const N_DRUM_KIT[] = {"808", "808", "808", "808", "808", "909", "SID", "CR78", "FM"
#if MELODEE_USER_KITS                          /* (the card shows a user kit's own name: drk_user_name) */
    , "KIT 1", "KIT 2", "KIT 3", "KIT 4", "KIT 5", "KIT 6", "KIT 7", "KIT 8"
#endif
};
static const char *const N_DRUM_KICK[] = {"PUNCH", "ROUND"};

#include "drum_sid.c"                          /* KIT SID */
#include "drum_lanes.c"                        /* KIT CR78, KIT FM */
#if MELODEE_USER_KITS
#include "drum_real.c"                         /* KIT 1..8: the user's recorded kits */
#include "kit_store.c"                         /* .. their records' check (rec_check) */
#include "kit_take.c"                          /* .. a take made a pad's (rec_take) */
#else
static uint32_t drk_bytes(void) { return 0; }
#endif

static drum_lane_t *drum_kit_of(const track_t *t)
{
    return trk_ix(t) < NPART ? drum_kit_part(trk_ix(t)) : 0;
}

static int drum_live(const drum_lane_t *L) { return L->r.on; }

static void drum_choke(drum_lane_t *K)                   /* a closed hat chokes the open one */
{
    drum_lane_t *o = &K[DV_HATO];
    if (o->r.on && o->r.ins == DR_OH)
        o->r.choke = 1;
}

/* the lane voice v plays, 0 when it plays none (any more) */
static drum_lane_t *drum_lane_of(track_t *t, const voice_t *v)
{
    if (DRUM_KV(t->p) >= DK_909 || v->s[2] >= DK_909) return 0;
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = voice_ix(t, v);
    if (!K || i >= NVOICE)
        return 0;
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    return L->owner == i + 1u ? L : 0;
}

/* Different GM pitches on a lane still share its one sounding voice. */
static voice_t *drum_reuse(track_t *t, uint32_t note)
{
    if(drum_isone(t)) return trk_voice(t, 0)->active ? trk_voice(t, 0) : 0;   /* (the kit's one voice) */
    if(drum_is909(t)) {
        drum909_part_t *k=drum909_of(t); uint32_t ins=drum909_ins(note), o=k?k->owner[ins]:0;
        return o && o<=NVOICE && trk_voice(t, o-1)->active && trk_voice(t, o-1)->s[2]==DK_909 ? trk_voice(t, o-1):0;
    }
    drum_lane_t *K = drum_kit_of(t);
    uint32_t lane = drum_lane(note), owner = K ? K[lane].owner : 0;
    voice_t *v;
    if (!owner || owner > NVOICE)
        return 0;
    v = trk_voice(t, owner - 1u);
    return v->active && (uint32_t)v->s[0] == lane ? v : 0;
}

static void drum_note_on(track_t *t, voice_t *v)
{
    v->s[3]=(int32_t)drum_event_length; v->s[4]=drum_event_pitch; v->s[5]=drum_event_length!=0;
    if(drum_issid(t)) {                                  /* the hit's own length: its SID voice's gate */
        v->s[2]=DK_SID; v->s[5]=0; v->env_out=32767; dsk_hit(t,v->note,v->vel); return;
    }
#if MELODEE_USER_KITS
    if(drum_isuser(t)) {                                 /* the hit's own length: its channel's */
        v->s[2]=t->p[P_E0]; v->s[5]=0; v->env_out=32767; drk_hit(t,v->note,v->vel); return;
    }
#endif
    if(drum_islane(t)) {                                 /* the hit's own length: its lane's */
        v->s[2]=t->p[P_E0]; v->s[5]=0; v->env_out=32767; dlk_hit(t,v->note,v->vel); return;
    }
    if(drum_is909(t)) {
        drum909_part_t *k=drum909_of(t); if(!k)return;
        uint32_t ins=drum909_ins(v->note), o=voice_ix(t, v)+1;
        for(uint32_t j=0;j<D9_NUM;j++)if(k->owner[j]==o)k->owner[j]=0;
        k->owner[ins]=(uint8_t)o; v->s[0]=drum_lane(v->note); v->s[1]=(int32_t)ins; v->s[2]=DK_909;
        drum909_values(t,k,ins,drum_event_pitch,v->vel); v->env_out=32767; return;
    }
    v->s[2]=DK_808;
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = voice_ix(t, v), role, lane;
    int32_t st;
    if (!K || i >= NVOICE)
        return;
    role = drum_gm(t->p, v->note, &st);
    lane = DV_TYPE_LANE[role];
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    if (L->owner == i + 1u)                              /* this voice played another lane: it stops there */
        L->owner = 0;
    L = &K[lane];
    L->st = (int8_t)st;
    L->owner = (uint8_t)(i + 1u);                        /* (its last voice, if another, ends: drum_amp) */
    v->s[0] = (int32_t)lane;
    L->role = (uint8_t)role;
    uint32_t ins = dr8_ins(v->note, role);
    L->sound=(uint8_t)drum_sound_of(t,v->note);
    int16_t local[P_COUNT];drum808_values(t,L->sound,local);
    L->r.decay=drum_offs(t)->c[L->sound][1];
    L->r.pitch=(drum_offs(t)->c[L->sound][0]+drum_event_pitch)*16;
    dr8_hit(&L->r, ins, v->vel, local);
    v->env_out = mulq15(127 * 258, dr8_level(v->vel, t->p[P_E5]));
    if (lane == DV_HATC && ins == DR_CH)
        drum_choke(K);
}

/* the voice's amplitude: the hit, not the ADSR (which still runs, held at full so a release never ends
 * it). A voice that plays no lane any more, or whose drum has rung out, ends here. The 808's hits have their
 * velocity in their trigger level: the voice's own (voice.c: x velocity) taken out again, ACC's put in */
static int32_t drum_amp(track_t *t, voice_t *v, int32_t adsr)
{
    if(v->s[2]>=DK_SID || drum_isone(t)) {               /* the chip, the lane kits: on while any of it sounds */
#if MELODEE_USER_KITS
        if(drum_isuser(t)) { if(v->s[2]==t->p[P_E0] && drk_tick(t)) { v->env=1<<24; return 32767*127/(v->vel?v->vel:1); } v->active=v->gate=v->stage=0; v->env=0; return 0; }
#endif
        if(v->s[2]==t->p[P_E0] && (drum_issid(t) ? dsk_tick(t) : dlk_tick(t))) { v->env=1<<24; return 32767*127/(v->vel?v->vel:1); }
        v->active=v->gate=v->stage=0; v->env=0; return 0;
    }
    if(v->s[5] && v->s[3]<=0){v->active=v->gate=v->stage=0;v->env=0;return 0;}
    if(drum_is909(t) && v->s[2]==DK_909) {
        drum909_part_t *k=drum909_of(t); uint32_t ins=(uint32_t)v->s[1];
        if(k && ins<D9_NUM && k->owner[ins]==voice_ix(t, v)+1 && drum909_live(k,ins)) { v->env=1<<24; return 32767*127/(v->vel?v->vel:1); }
        v->active=v->gate=v->stage=0; v->env=0; return 0;
    }
    const drum_lane_t *L = drum_lane_of(t, v);
    (void)adsr;
    if (!v->active)                                      /* (taken for another part: env_tick ended it) */
        return 0;
    if (!L || !drum_live(L)) {
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;
    return (int32_t)(((int64_t)(32767 * 127 / (v->vel ? v->vel : 1)) * dr8_level(v->vel, t->p[P_E5])) >> 15);
}

/* Optional processing has bounded helpers; the neutral output loop stays small. */
static __attribute__((noinline)) int drum_source(track_t *t,voice_t *v,drum_lane_t *L,int32_t *y,uint32_t n,dr_scratch_t *scratch,const int16_t *p)
{
    uint32_t i;
    if(DRUM_IS(p, DK_909)) {
        drum909_part_t *k=drum909_of(t); if(!k || !k->nz)return 0;
        float f[CTL]={0}; drum909_render_voice(&k->d,v->s[1],k->nz,f,(int)n);
        for(i=0;i<n;i++)y[i]=(int32_t)(fm_clampf(f[i],-2.0f,2.0f)*12000.0f);
    } else {int16_t local[P_COUNT];for(uint32_t k=0;k<8u;k++)local[P_E0+k]=p[P_E0+k];const int8_t *c=drum_offs(t)->c[L->sound];
        local[P_E3]=(int16_t)clamp(local[P_E3]+c[1],0,127);
        if(L->sound==D9_SD)local[P_E4]=(int16_t)clamp(local[P_E4]+c[2],0,127);dr8_run_into(&L->r,local,y,n,scratch);}
    return 1;
}
static __attribute__((noinline)) void drum_limit(voice_t *v,int32_t *y,uint32_t n)
{
    uint32_t i;
    if(v->s[5]){
        uint32_t left=(uint32_t)v->s[3];
        for(i=0;i<n;i++)y[i]=i>=left?0:left-i<64u?y[i]*(int32_t)(left-i)/64:y[i];
        v->s[3]=left>n?(int32_t)(left-n):0;
    }
}
static __attribute__((noinline)) void drum_color(drum_lane_t *L,int32_t value,int32_t *y,uint32_t n)
{
    int32_t k=dr_lp1(dr_exp(4000u,value,24));
    for(uint32_t i=0;i<n;i++){L->r.color+=(int32_t)(((int64_t)(y[i]-L->r.color)*k)>>14);y[i]=value<0?L->r.color:y[i]+((y[i]-L->r.color)*value>>6);}
}
/* the kit's offsets for the sound: CHARACTER's colour on y, the lane's level less the attenuation (a call of its own:
 * in a module the patch is a host call, kept out of drum_render_into's frame) */
static __attribute__((noinline)) int32_t drum_lane_level(track_t *t,voice_t *v,drum_lane_t *L,int32_t *y,uint32_t n,const int16_t *p)
{
    uint32_t sound=drum_is909(t)?(uint32_t)v->s[1]:L->sound;
    const int8_t *c=drum_offs(t)->c[sound];
    if(!drum_is909(t) && sound!=D9_SD && c[2])drum_color(L,c[2],y,n);
    return clamp(p[P_LN0 + ((uint32_t)v->s[0] & (DV_NLANE - 1u))], 0, 127)*(127-c[3])/127;
}
static void drum_render_into(track_t *t,voice_t *v,int32_t *out,uint32_t n,const vmod_t *m,dr_scratch_t *scratch,const int16_t *p)
{
    if (DRUM_IS(p, DK_SID)) {
        if (v->s[2] == DK_SID)
            dsk_render(t, out, n > CTL ? CTL : n, m, p);
        return;
    }
#if MELODEE_USER_KITS
    if (DRUM_KV(p) >= DK_USER0) {
        if (v->s[2] == p[P_E0])
            drk_render(t, out, n > CTL ? CTL : n, m, p);
        return;
    }
#endif
    if (DRUM_KV(p) >= DK_CR78) {
        if (v->s[2] == p[P_E0])
            dlk_render(t, out, n > CTL ? CTL : n, m, p);
        return;
    }
    drum_lane_t *L = drum_lane_of(t, v);
    int32_t y[CTL], drv = p[P_E7], g = 0, mk = 0;
    uint32_t i;
    if (!L && !drum_is909(t))
        return;
    if (n > CTL)
        n = CTL;
    if(!drum_source(t,v,L,y,n,scratch,p))return;
    if(v->s[5])drum_limit(v,y,n);
    if (drv > 0) {                                       /* DRV: x1..x4 into the soft clip, the level kept (Q12) */
        g = 4096 + drv * 3 * 4096 / 127;
        mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
    }
    int32_t lv = drum_lane_level(t,v,L,y,n,p);
    /* Fold lane gain into the block's amplitude ramp: no extra per-sample multiply. */
    vmod_t lane_mod;
    if (lv != 127) {
        int32_t lane_gain = lv * lv * 32767 / (127 * 127);
        lane_mod = *m;
        lane_mod.amp0 = mulq15(m->amp0, lane_gain);
        lane_mod.amp1 = mulq15(m->amp1, lane_gain);
        m = &lane_mod;
    }
    for (i = 0; i < n; i++) {                            /* x1.25 and the knee below, in 32 bits */
        int32_t s = y[i];
        if (g)
            s = (softclip((s * g) >> 12) * mk) >> 15;
        s = voice_amp(soft_knee(s + (s >> 2), 24000), m, i);
        out[i] += s << 1;
    }
}

static void drum_render(track_t *t,voice_t *v,int32_t *out,uint32_t n,const vmod_t *m)
{ drum_render_into(t,v,out,n,m,&dr_scratch,t->p); }
static void drum_render_p(track_t *t,voice_t *v,int32_t *out,uint32_t n,const vmod_t *m,const int16_t *p)
{ dr_scratch_t scratch; drum_render_into(t,v,out,n,m,&scratch,p); }   /* (CPU1: voice.c engine_batch_kernel) */
/* the kit's cost on CPU1: the 909 its own, KIT SID the SID's, KIT FM FM6's, the others the 808's */
static int32_t drum_batch(const track_t *t) { return drum_issid(t) ? AP_SID : drum_is909(t) ? AP_909 : drum_isfm(t) ? AP_FM6 : AP_808; }
static uint32_t drum_state(const track_t *t) { return t || DRUM_ONLY ? drum_bytes(t) : sizeof(drum909_part_t); }   /* (0: the most a kit takes) */

/* the GM drum map, the first C (key 7) is the kick (C2, 36); a user kit: its white keys (drum_user_key) */
static int32_t drum_keys(const track_t *t, uint32_t k)
{
    if (drum_isuser(t))
        return (int32_t)drum_user_key(t, (int32_t)k);
    return clamp(29 + 12 * input_octave(t) + (int32_t)k, 0, 127);
}

/* {KIT, TUNE, TONE, DECY, SNAP, ACC, KICK, DRV} (SID: DIGI, RES; CR78: HATS); every kit suggests the BEAT pattern (GM
 * notes). Each kit is a synth of its own (808, 909, SID KIT, CR78, FM KIT, SAMPLER: their presets below) */
#define KIT808_N 1u
#define KIT909_N 1u
#define KITSID_N 2u
#define KITCR_N 1u
#define KITFM_N 1u
#define SAMPLER_N 8u
#if !MOD_SIDE                    /* (a synth module's presets are in its file: tools/mod_presets.py) */
static const preset_t KIT808_PRESETS[] = {DRUM_PRESET_808};
static const preset_t KIT909_PRESETS[] = {{"909 KIT", {DK_909, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0}};
static const preset_t KITSID_PRESETS[] = {
    {"SID KIT", {DK_SID, 64, 80, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0},    /* every drum on the SID */
    {"SID DIGI", {DK_SID, 64, 80, 64, 64, 100, 1, 0}, {0, 100, 127, 100}, 0, 0},   /* kick, snare on $D418 */
};
static const preset_t KITCR_PRESETS[] = {{"CR78 KIT", {DK_CR78, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0}};
static const preset_t KITFM_PRESETS[] = {{"FM KIT", {DK_FM, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0}};
#define SAMPLER_KIT(n) {"KIT " #n, {DK_USER0 + n - 1, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0}
static const preset_t SAMPLER_PRESETS[] = {SAMPLER_KIT(1), SAMPLER_KIT(2), SAMPLER_KIT(3), SAMPLER_KIT(4), SAMPLER_KIT(5),
                                           SAMPLER_KIT(6), SAMPLER_KIT(7), SAMPLER_KIT(8)};
#undef SAMPLER_KIT
_Static_assert(NELEM(KIT808_PRESETS) == KIT808_N && NELEM(KIT909_PRESETS) == KIT909_N && NELEM(KITSID_PRESETS) == KITSID_N &&
               NELEM(KITCR_PRESETS) == KITCR_N && NELEM(KITFM_PRESETS) == KITFM_N && NELEM(SAMPLER_PRESETS) == SAMPLER_N,
               "the kits' presets");
#define KIT_PRESETS(x) .presets = x,
#else
#define KIT_PRESETS(x)
#endif
/* KIT SID: KICK is DIGI (KICK and SNARE on $D418), DRV the filter's resonance */
static const char *const N_DRUM_DIGI[] = {"OFF", "ON"};
static const param_desc_t DRUM_SID_DIGI = {"DIGI", F_ENUM, 0, 1, 0, N_DRUM_DIGI, 0};
static const param_desc_t DRUM_SID_RES = {"RES", F_PCT, 0, 127, 0, 0, 0};
/* a user kit: its one kick */
static const char *const N_DRUM_USER_KICK[] = {"KIT"};
static const param_desc_t DRUM_USER_KICK = {"KICK", F_ENUM, 0, 0, 0, N_DRUM_USER_KICK, 0};
static const param_desc_t *drum_desc(const track_t *t, uint32_t k)
{
    if (drum_isuser(t))
        return k == 6u ? &DRUM_USER_KICK : 0;
    if (drum_iscr(t))
        return k == 6u ? &DRUM_CR_HATS : 0;
    return !drum_issid(t) ? 0 : k == 6u ? &DRUM_SID_DIGI : k == 7u ? &DRUM_SID_RES : 0;
}
static uint32_t drum_cap(const track_t *t) { return drum_isone(t) ? 1u : DV_NLANE; }
/* the part's working memory for its kit (the host's: a module's part_mem, a host build's resources) */
#if !MOD_SIDE
static void *mod_part_mem(uint32_t part, uint32_t bytes);   /* (eng_module.c) */
#endif
static drum909_part_t *drum909_part(uint32_t part) { return mod_part_mem(part, sizeof(drum909_part_t)); }
static drum_lane_t *drum_kit_part(uint32_t part) { return mod_part_mem(part, sizeof(drum_lane_t) * DV_NLANE); }
static dsk_part_t *dsk_part(uint32_t part) { return mod_part_mem(part, sizeof(dsk_part_t)); }
static dlk_part_t *dlk_part(uint32_t part) { return mod_part_mem(part, sizeof(dlk_part_t)); }
#if MELODEE_USER_KITS
static drk_part_t *drk_part(uint32_t part) { return mod_part_mem(part, sizeof(drk_part_t)); }
#endif

/* a kit's engine: its name, its KIT values (the kit synths' one, SAMPLER's its eight records'), its presets */
#if MELODEE_USER_KITS
#define KIT_RECS .rec_check = drum_rec_check, .rec_take = drum_rec_take, .rec_pad = drum_rec_pad,
#else
#define KIT_RECS
#endif
#define KIT_ENGINE(sym, nm, lo, hi, pre, npre, recs)                                                                  \
    static const engine_t sym = {                                                                                  \
        .name = nm,                                                                                                \
        .page_title = {"KIT", "HIT"},                                                                              \
        .edit = {                                                                                                  \
            {"KIT", F_ENUM, lo, hi, lo, N_DRUM_KIT, 0},                                                            \
            {"TUNE", F_PCT, 0, 127, 64, 0, 0},                                                                     \
            {"TONE", F_PCT, 0, 127, 64, 0, 0},                                                                     \
            {"DECY", F_PCT, 0, 127, 64, 0, 0},                                                                     \
            {"SNAP", F_PCT, 0, 127, 64, 0, 0},                                                                     \
            {"ACC", F_PCT, 0, 127, 100, 0, 0},                                                                     \
            {"KICK", F_ENUM, 0, 1, 0, N_DRUM_KICK, 0},                                                             \
            {"DRV", F_PCT, 0, 127, 0, 0, 0},                                                                       \
        },                                                                                                         \
        KIT_PRESETS(pre)                                                                                           \
        .npresets = npre,                                                                                          \
        .note_on = drum_note_on, .render = drum_render, .amp = drum_amp, .block = drum_block,                     \
        .knob = {P_E1, P_E2, P_E3, P_E4},                                                                          \
        .poly = DV_NLANE, .cap = drum_cap, .desc = drum_desc, .oneshot = 1, .keys = drum_keys,                    \
        .state = drum_state, .reuse = drum_reuse, .batch = drum_batch, .render_p = drum_render_p,                 \
        .wclass = 5, recs                                                                                          \
    };
#if !DRUM_ONLY || DRUM_ONLY == DK_808
KIT_ENGINE(ENG_KIT808, "808", DK_808, DK_808, KIT808_PRESETS, KIT808_N, )
#endif
#if !DRUM_ONLY || DRUM_ONLY == DK_909
KIT_ENGINE(ENG_KIT909, "909", DK_909, DK_909, KIT909_PRESETS, KIT909_N, )
#endif
#if !DRUM_ONLY || DRUM_ONLY == DK_SID
KIT_ENGINE(ENG_KITSID, "SID KIT", DK_SID, DK_SID, KITSID_PRESETS, KITSID_N, )
#endif
#if !DRUM_ONLY || DRUM_ONLY == DK_CR78
KIT_ENGINE(ENG_KITCR, "CR78", DK_CR78, DK_CR78, KITCR_PRESETS, KITCR_N, )
#endif
#if !DRUM_ONLY || DRUM_ONLY == DK_FM
KIT_ENGINE(ENG_KITFM, "FM KIT", DK_FM, DK_FM, KITFM_PRESETS, KITFM_N, )
#endif
#if MELODEE_USER_KITS && (!DRUM_ONLY || DRUM_ONLY == DK_USER0)
KIT_ENGINE(ENG_SAMPLER, "SAMPLER", DK_USER0, DK_KIT_MAX, SAMPLER_PRESETS, SAMPLER_N, KIT_RECS)
#endif
#undef KIT_ENGINE
