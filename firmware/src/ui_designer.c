/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The sound designer (docs/SOUND_DESIGNER.md): a sound-only transaction over the native editors, which stay the
 * authority for every control. START is NEW SONG's key screen (rings over a large panel, the keys under it), FEATURES
 * a list page (KNOB 2 the row, KNOB 1 its value), the shaping stages the sound pages themselves: the stages as the
 * tabs under the header, a stage's pages as its dots. OCT+ / OCT- the next / previous stage, PRESETS a stage, SELECT
 * the pages, SAVE the original and back, HOME (or OCT past either end) its sheet: Keep, Save as user.., Discard.
 * No serialized IDs, project formats or DSP defaults change. Included by ui_draw.c */
enum { DS_OFF, DS_SETUP, DS_SHAPE };
enum { DG_FREE, DG_BASS, DG_PAD, DG_LEAD, DG_KEYS, DG_PLUCK, DG_FX, DG_COUNT };
enum { DT_BUILD, DT_SOURCE, DT_TONE, DT_ENV, DT_MOVE, DT_PLAY, DT_COUNT };
static const char *const DS_GOAL[] = {"FREE", "BASS", "PAD", "LEAD", "KEYS", "PLUCK", "FX"};
static const char *const DS_TAB[DT_COUNT] = {"Features", "Source", "Tone", "Env", "Motion", "Play"};
static void designer_page(void);
/* One compact snapshot swaps A (original) and B (draft). Editing is allowed
 * only on B; switching engine during setup does not touch the track. */
typedef struct {
    uint8_t eng, preset, native;
    uint16_t user;
    int16_t p[P_COUNT];
    uint8_t peng;                                       /* the patch's engine, its bytes (patch.c) */
    uint8_t patch[MOD3_PATCH_MAX];
} designer_sound_t;
#define DS_ROUTES 160u
static struct {
    uint8_t track, eng, goal, current, expert, stage, at, n, compare, focus;
    uint8_t old_home, old_page, row, first;             /* row, first: FEATURES' row, its first shown */
    struct { uint8_t page, op, stage; } route[DS_ROUTES];
    designer_sound_t other;
    uint32_t sig;
    uint8_t deng, dgen, dflags, dpg[SCH_PG_MAX];        /* the synth's designer block: its engine + 1, flags, pages */
} ds __attribute__((section(".pool")));

static void designer_snapshot(designer_sound_t *s)
{
    track_t *t=TSEL;uint32_t k=song.sel;
    memset(s,0,sizeof *s);
    s->eng=t->eng_req;s->preset=t->preset;s->user=t->user;s->native=t->user_native;
    memcpy(s->p,t->p,sizeof s->p);
    s->peng=(uint8_t)patch_eng_of(k);memcpy(s->patch,patch_of(k),MOD3_PATCH_MAX);
}
static void designer_restore(const designer_sound_t *s)
{
    track_t *t=TSEL;uint32_t k=song.sel;
    mod_want(s->eng);
    panic_req|=(uint8_t)(1u<<k);
    t->eng_req=s->eng;t->preset=s->preset;t->user=s->user;t->user_native=s->native;
    for(uint32_t i=0;i<P_COUNT;i++)if(!param_kept(i))t->p[i]=s->p[i];
    patch_set(k,s->peng,s->patch,MOD3_PATCH_MAX,MOD3_PATCH_RESTORE);
    sync_reload=1;
}
static void designer_compare(void)
{
    designer_sound_t now;
    fm1_irq_off();designer_snapshot(&now);designer_restore(&ds.other);ds.other=now;fm1_irq_on();
    ds.compare^=1u;ui.force=1;
    if(!ds.compare && ui.designer_mode==DS_SHAPE)designer_page();   /* (another engine's original showed its own page) */
}
static void designer_draft(void) { if(ds.compare)designer_compare(); }
static uint32_t designer_first(void);
static void designer_open(void)
{
    if(designer_on()){ui.menu=0;ui.force=1;return;}   /* (the menu over it: back to it) */
    if(chain_busy()){ui_message("STOP SONG TO DESIGN");return;}
    browse_commit();momentary_restore();pop_close();smap.on=0;
    memset(&ds,0,sizeof ds);ds.track=song.sel;ds.eng=TSEL->eng_req;
    if(!eng_ok(ds.eng))ds.eng=(uint8_t)designer_first();
    ds.current=1;ds.old_home=ui.home;ds.old_page=ui.page;
    designer_quit=designer_close;ui.designer_mode=DS_SETUP;ui.home=0;ui.menu=0;ui.layer=ui.lock=ui.ly=0;ui.ly_t0=0;
    ui.pg_down=0;ui.force=1;ui.msg_t=0;ui.act=0;
    kb_mask=kb_lock=0;
}
static void designer_close(int discard)
{
    if(ui.designer_mode!=DS_SETUP){
        if(discard){if(!ds.compare){fm1_irq_off();designer_restore(&ds.other);fm1_irq_on();}}
        else designer_draft();
        fm1_irq_off();motion_design_pause&=(uint8_t)~(1u<<ds.track);motion_rebase(&trk[ds.track]);fm1_irq_on();
    }
    designer_quit=0;ui.designer_mode=DS_OFF;ui.home=ds.old_home;ui.page=ds.old_page;
    ui.home_t0=ui.save_t0=ui.rec_t0=ui.seq_t0=ui.oct_t0=0;
    ui.force=1;sec.ok=0;ui.pg_down=0;song.grid=0;page_entered();
}
/* ---- the synth's sound designer block (mod_schema.h: its FEATURES, need rules, pages' stages and what cannot sound;
 * its recipes are the schema's records), read from the selected track's synth's schema. Paths are codes: a FEATURES
 * row of the block (0 ..), or DS_HOST + a track path (MOD3_HF_*: the part's own parameters; a synth without a block
 * lists these). DS_NONE nothing, DS_END a control past its envelope's end */
#define DS_HOST 0x40u
#define DS_END MOD3_DN_END
#define DS_NONE MOD3_DN_NONE
_Static_assert(DG_COUNT == MOD3_G_COUNT && DT_PLAY == MOD3_DS_PLAY, "designer goals and stages");
static struct { uint8_t e, ok; uint32_t at; } dsb;      /* the block being read: its engine, its place in the schema */
static int ds_blk_of(uint32_t e)
{
    mod_sch_head h;
    dsb.e=(uint8_t)e;dsb.ok=0;
    if(!sch_head(e,&h) && h.design!=MOD_SCH_NONE){dsb.at=(uint32_t)h.wid+h.design;dsb.ok=1;}
    return dsb.ok;
}
static int ds_blk(void) { return ds_blk_of(eng_idx(TSEL->eng_req)); }
static uint32_t ds_rb(uint32_t off) { uint8_t b=0;if(dsb.ok)(void)sch_rd(dsb.e,dsb.at+off,&b,1u);return b; }
static uint32_t ds_rw(uint32_t off) { return ds_rb(off)|ds_rb(off+1u)<<8; }
static uint32_t ds_po(void) { return MOD_SCH_DHEAD+ds_rb(2)*MOD_SCH_DFEAT+ds_rb(3)*MOD_SCH_DNEED; }   /* (its pages') */
/* the block's flags and its pages' stages, kept while the selected synth is the same */
static void ds_cache(void)
{
    uint32_t e=eng_idx(TSEL->eng_req),n,j,po;
    if(ds.deng==e+1u && ds.dgen==sch_gen)return;
    ds.deng=(uint8_t)(e+1u);ds.dgen=sch_gen;ds.dflags=0;memset(ds.dpg,0,sizeof ds.dpg);
    if(!ds_blk())return;
    ds.dflags=(uint8_t)ds_rb(1);
    n=ds_rb(4);po=ds_po();
    for(j=0;j<n && j<SCH_PG_MAX;j++)ds.dpg[j]=(uint8_t)ds_rb(po+j*3u);
}
static int ds_flag(uint32_t f) { ds_cache();return (ds.dflags&f)!=0; }
/* the engine a track that cannot be designed starts from: a synth whose block says so (MOD3_DSF_FIRST), else the first */
static uint32_t designer_first(void)
{
    for(uint32_t e=0;e<NENGINES;e++)if(eng_ok(e) && ds_blk_of(e) && (ds_rb(1)&MOD3_DSF_FIRST))return e;
    return eng_vis(0);
}
/* a program's values: a parameter of the synth (its schema's range), or MOD3_D_TRACK + a P_* number (the part's own) */
static int32_t ds_vget(uint32_t id)
{
    sch_param_t p;
    if(id&MOD3_D_TRACK)return (id&0x7FFFu)<P_COUNT?TSEL->p[id&0x7FFFu]:0;
    return sch_param(dsb.e,id,&p)?0:sch_get(TSEL,dsb.e,patch_of(song.sel),&p);
}
static void ds_vput(uint32_t id,int32_t v)
{
    sch_param_t p;
    if(id&MOD3_D_TRACK){if((id&0x7FFFu)<P_COUNT)TSEL->p[id&0x7FFFu]=(int16_t)v;return;}
    if(!sch_param(dsb.e,id,&p))(void)sch_put(TSEL,dsb.e,patch_of(song.sel),&p,v);
}
static int ds_active(uint32_t id)                       /* the synth's control id does something (pactive) */
{
    uint32_t e=eng_idx(TSEL->eng_req);
    return !ENGINES[e]->pactive || mod_idle(e) || ENGINES[e]->pactive(TSEL,patch_of(song.sel),id);
}
static int ds_car(uint32_t op)                          /* operator op (0: the first) a carrier of the patch's algorithm */
{
    mod_sch_head h;alg_wid_t w;uint8_t f[ALG_NOP];uint32_t a,e=dsb.e;
    if(sch_head(e,&h) || h.wdef==MOD_SCH_NONE || alg_wid(e,h.wdef,&w))return 0;
    a=(uint32_t)sch_val(TSEL,e,w.alg,0)%w.nalg;
    return !sch_wid(e,h.wdef+ALG_HEAD+a*w.nop,f,w.nop) && ((alg_car_ops(f,w.nop)>>op)&1u);
}
static int32_t ds_expr(uint32_t *at,uint32_t s)         /* the expression at *at (*at past its END), state s */
{
    int32_t st[8],a,b;uint32_t n=0,op,k=0;
    while((op=ds_rb(*at))!=MOD3_D_END && k++<256u){
        uint32_t id=ds_rw(*at+1u);
        switch(op){
        case MOD3_D_VAL:a=ds_vget(id);break;
        case MOD3_D_NZ:a=ds_vget(id)!=0;break;
        case MOD3_D_CONST:a=(int8_t)ds_rb(*at+1u);break;
        case MOD3_D_CAR:a=ds_car(ds_rb(*at+1u));break;
        case MOD3_D_ACT:a=ds_active(id);break;
        case MOD3_D_STATE:a=(int32_t)s;break;
        case MOD3_D_NOT:a=n?!st[--n]:1;break;
        default:
            b=n?st[--n]:0;a=n?st[--n]:0;
            a=op==MOD3_D_EQ?a==b:op==MOD3_D_NE?a!=b:op==MOD3_D_GT?a>b:op==MOD3_D_LE?a<=b:op==MOD3_D_AND?(a&&b):
              op==MOD3_D_OR?(a||b):op==MOD3_D_ADD?a+b:0;
        }
        if(n<8u)st[n++]=a;
        *at+=mod_sch_dop(op);
    }
    (*at)++;
    return n?st[n-1u]:0;
}
static void ds_run(uint32_t at,uint32_t s)              /* the actions at at, state s (the audio reads: interrupts off) */
{
    uint32_t op,skip=0,k=0;
    while((op=ds_rb(at))!=MOD3_D_END && k++<256u){
        uint32_t id=ds_rw(at+1u),n;
        if(op==MOD3_D_IF){n=ds_rb(at+1u);at+=2u;skip=ds_expr(&at,s)?0u:n;continue;}
        if(skip){skip--;at+=mod_sch_dop(op);continue;}
        switch(op){
        case MOD3_D_SET:ds_vput(id,(int16_t)ds_rw(at+3u));break;
        case MOD3_D_SETS:ds_vput(id,(int32_t)s);break;
        case MOD3_D_AMT:{int32_t now=ds_vget(id);ds_vput(id,s?(now?now:(int16_t)ds_rw(at+3u)):0);break;}
        case MOD3_D_NOTE:patch_note(song.sel,MOD3_PATCH_EDIT,id);break;
        case MOD3_D_SETCAR:ds_vput(id,(int32_t)(ds_car(ds_rb(at+3u))?ds_rb(at+4u):ds_rb(at+5u)));break;
        }
        at+=mod_sch_dop(op);
    }
}
static int ds_cond(uint32_t at) { return (int)(ds_expr(&at,0)!=0); }
typedef struct { uint16_t word, names, eword; uint8_t states, flags, in, estate; uint16_t get, on, off; } ds_feat_t;
static int ds_feat(uint32_t i,ds_feat_t *f)             /* the block's FEATURES row i: 0 ok */
{
    uint8_t b[MOD_SCH_DFEAT];
    if(!dsb.ok || i>=ds_rb(2) || sch_rd(dsb.e,dsb.at+MOD_SCH_DHEAD+i*MOD_SCH_DFEAT,b,sizeof b))return 1;
    f->word=(uint16_t)sch_u16(b);f->names=(uint16_t)sch_u16(b+2);f->eword=(uint16_t)sch_u16(b+4);
    f->states=b[6]<2u?2u:b[6];f->flags=b[7];f->in=b[8];f->estate=b[9]?b[9]:1u;
    f->get=(uint16_t)sch_u16(b+10);f->on=(uint16_t)sch_u16(b+12);f->off=(uint16_t)sch_u16(b+14);
    return 0;
}
/* FEATURES, the first stage: the paths a sound is built from, as the instrument's own switches (the synth's block:
 * the Prophet's pulse, Poly-Mod, unison; FM operators; CZ lines, ring and noise), else the part's. ON wires an audible
 * path and keeps an amount already set; OFF opens the path again, keeping routing amounts. */
#define DS_FEAT 0xFFu                                   /* the FEATURES route (its list, not a page) */
static const struct { const char *word; uint16_t p; int16_t def; uint8_t sweep; } DS_HF[MOD3_HF_COUNT] = {
    {"Voice", P_VOICE, 0, 0}, {"Glide", P_GLIDE, 40, 0}, {"Spread", P_SPRD, 70, 0}, {"Pitch env", P_ED_PIT, 8, 0},
    {"Vibrato", P_LD_PIT, 4, 0}, {"Wobble", P_LD_FLT, 24, 1}, {"Tremolo", P_LD_AMP, 40, 0}, {"Filter env", P_ED_FLT, 32, 1}};
static uint32_t designer_feats(void)                    /* FEATURES' rows (a drum kit: none) */
{
    if(pads_on(TSEL) || midi_track_only(TSEL))return 0;
    return ds_blk()?ds_rb(2):MOD3_HF_COUNT;
}
static uint32_t designer_code(uint32_t r)               /* FEATURES' row r: its path */
{
    ds_feat_t f;
    if(!ds_blk())return DS_HOST+r;
    return ds_feat(r,&f)?DS_NONE:(f.flags&MOD3_FF_HOST)?DS_HOST+f.in:r;
}
static int designer_feat_on(void) { return ui.designer_mode==DS_SHAPE && ds.n && ds.route[ds.at].page==DS_FEAT; }
static uint32_t designer_feat_states(uint32_t c)
{
    ds_feat_t f;
    if(c>=DS_HOST)return c==DS_HOST+MOD3_HF_VOICE?4u:2u;
    return ds_blk() && !ds_feat(c,&f)?f.states:2u;
}
static uint32_t designer_feat_get(uint32_t c)
{
    ds_feat_t f;
    if(c>=DS_HOST){
        uint32_t h=c-DS_HOST;
        return h>=MOD3_HF_COUNT?0u:h==MOD3_HF_VOICE?(uint32_t)clamp(TSEL->p[P_VOICE],0,3):TSEL->p[DS_HF[h].p]!=0;
    }
    if(!ds_blk() || ds_feat(c,&f))return 0;
    {uint32_t at=f.get;return (uint32_t)ds_expr(&at,0);}
}
/* the instance op's pages are shaped: its FEATURES path (MOD3_FF_INST) on, or none */
static int designer_inst_on(uint32_t op)
{
    ds_feat_t f;
    if(!ds_blk())return 1;
    for(uint32_t i=0;!ds_feat(i,&f);i++)if((f.flags&MOD3_FF_INST) && f.in==op)return designer_feat_get(i)!=0;
    return 1;
}
/* A sweep from a general engine's wide-open filter is lost above it: switching
 * one on brings a cutoff (its EDIT knob CUT..) above three quarters to the middle */
static void designer_sweep_room(void)
{
    const engine_t *e=ENGINES[TSEL->eng_req%NENGINES];
    for(uint32_t i=0;i<8u;i++)if(e->edit[i].label && str_prefix(e->edit[i].label,"CUT")){
        if(TSEL->p[P_E0+i]>e->edit[i].min+(e->edit[i].max-e->edit[i].min)*3/4)TSEL->p[P_E0+i]=(int16_t)((e->edit[i].min+e->edit[i].max)/2);
        return;
    }
}
static int16_t designer_track(int16_t now,uint32_t on,int16_t def) { return on?(now?now:def):0; }
/* A path's state s into the patch (the audio code reads it: interrupts off) */
static void designer_feat_set(uint32_t c,uint32_t s)
{
    ds_feat_t f;
    if(c>=DS_HOST){
        uint32_t h=c-DS_HOST;track_t *t=TSEL;
        if(h==MOD3_HF_VOICE)t->p[P_VOICE]=(int16_t)s;
        else if(h<MOD3_HF_COUNT){
            t->p[DS_HF[h].p]=designer_track(t->p[DS_HF[h].p],s,DS_HF[h].def);
            if(s && DS_HF[h].sweep)designer_sweep_room();
        }
        return;
    }
    if(ds_blk() && !ds_feat(c,&f))ds_run(s?f.on:f.off,s);
}
/* path c's row (into b, 16 bytes: its length), the state a need switches it to, what it says then */
static uint32_t designer_word(uint32_t c,char *b)
{
    ds_feat_t f;
    if(c>=DS_HOST){str_cpy(b,c-DS_HOST<MOD3_HF_COUNT?DS_HF[c-DS_HOST].word:"",16);return str_len(b);}
    b[0]=0;
    return ds_blk() && !ds_feat(c,&f)?sch_str(dsb.e,f.word,b,16):0u;
}
static uint32_t designer_estate(uint32_t c)
{
    ds_feat_t f;
    if(c>=DS_HOST)return c==DS_HOST+MOD3_HF_VOICE?(uint32_t)V_UNISON:1u;
    return ds_blk() && !ds_feat(c,&f)?f.estate:1u;
}
static void designer_eword(uint32_t c,char *b)
{
    ds_feat_t f;
    if(c==DS_HOST+MOD3_HF_VOICE){str_cpy(b,"Unison",16);return;}
    if(c<DS_HOST && ds_blk() && !ds_feat(c,&f) && f.eword!=MOD_SCH_NONE){(void)sch_str(dsb.e,f.eword,b,16);return;}
    (void)designer_word(c,b);
}
static void designer_feat_text(uint32_t c,uint32_t s,char *b)   /* state s's name (16 bytes) */
{
    static const char *const VOICE[]={"Poly","Mono","Legato","Unison"};
    ds_feat_t f;
    if(c==DS_HOST+MOD3_HF_VOICE){str_cpy(b,VOICE[s&3u],16);return;}
    if(c<DS_HOST && ds_blk() && !ds_feat(c,&f) && f.states>2u && f.names!=MOD_SCH_NONE){
        uint32_t off=f.names;
        for(uint32_t i=0;i<s && i+1u<f.states;i++)off+=sch_str(dsb.e,off,b,16)+1u;
        (void)sch_str(dsb.e,off,b,16);
        return;
    }
    str_cpy(b,s?"On":"Off",16);
}
/* A build that cannot sound: said when it is made (a header notice) */
static const char *designer_silent(void)
{
    static char b[24];
    uint32_t at;
    if(!ds_blk() || !(at=ds_rw(6)) || !ds_cond(at))return 0;
    (void)sch_str(dsb.e,ds_rw(8),b,sizeof b);
    return b;
}
/* FEATURES: KNOB 2 the row, KNOB 1 its value */
static void designer_feat_knob(uint32_t k,int32_t s)
{
    uint32_t n=designer_feats(),c,st;
    if(!n)return;
    if(k==1u){ds.row=(uint8_t)clamp((int32_t)ds.row+s,0,(int32_t)n-1);return;}
    if(k)return;
    c=designer_code(ds.row%n);st=designer_feat_get(c);
    st=designer_feat_states(c)>2u?(uint32_t)clamp((int32_t)st+(s>0?1:-1),0,(int32_t)designer_feat_states(c)-1):s>0;
    fm1_irq_off();designer_feat_set(c,st);fm1_irq_on();
    sync_reload=1;
    if(designer_silent())ui_message(designer_silent());
}
/* Recipes are starting gestures, applied only when INIT was chosen: the synth's records answering the goal (each a
 * value, in order), else its envelope's roles, else the part's envelope. Current sound means byte-for-byte current
 * sound, regardless of the chosen goal. */
static void designer_recipe(void)
{
    static const uint8_t ROLE[4]={MOD3_R_ENV_A,MOD3_R_ENV_D,MOD3_R_ENV_S,MOD3_R_ENV_R};
    track_t *t=TSEL;uint32_t g=ds.goal,e=eng_idx(t->eng_req),i,n=0,id,fl,lb;
    mod_sch_head h;
    if(g==DG_FREE || pads_on(t) || midi_track_only(t))return;
    int pad=g==DG_PAD, shorty=g==DG_PLUCK||g==DG_KEYS;
    int16_t env[4]={(int16_t)(pad?75:0),(int16_t)(shorty?70:45),(int16_t)(shorty?0:110),(int16_t)(pad?85:shorty?40:20)};
    t->p[P_VOICE]=(g==DG_BASS||g==DG_LEAD)?V_MONO:V_POLY;
    dsb.e=(uint8_t)e;
    if(!sch_head(e,&h))for(i=0;i<h.nrec;i++){
        uint8_t b[MOD_SCH_XREC];
        if(sch_rd(e,h.rec+i*MOD_SCH_XREC,b,sizeof b))break;
        if((b[0]&0x3Fu)!=MOD3_Q_GOAL || !((b[1]>>g)&1u))continue;
        n++;
        id=sch_u16(b+2);
        if(b[0]&MOD3_RQ_NOTE)patch_note(song.sel,MOD3_PATCH_EDIT,id);
        else if(!(b[0]&MOD3_RQ_ROLE))ds_vput(id,(int16_t)sch_u16(b+4));
        else if(!sch_role(e,id,0,&id,&fl,&lb))ds_vput(fl&MOD3_RF_TRACK?MOD3_D_TRACK|id:id,(int16_t)sch_u16(b+4));
    }
    if(n)return;
    for(i=0;i<4u;i++){                                  /* (a synth's envelope: the goal's over its range) */
        sch_param_t p;
        if(sch_role(e,ROLE[i],0,&id,&fl,&lb) || (!(fl&MOD3_RF_TRACK) && sch_param(e,id,&p)))continue;
        if(fl&MOD3_RF_TRACK){if(id<P_COUNT)t->p[id]=env[i];n++;continue;}
        {int32_t v=p.min+(env[i]*(p.max-p.min)+63)/127;
         (void)sch_put(t,e,patch_of(song.sel),&p,fl&MOD3_RF_INVERT?p.max-(v-p.min):v);}
        n++;
    }
    if(n)patch_note(song.sel,MOD3_PATCH_EDIT,0xFFFFu);
    else{t->p[P_ATK]=env[0];t->p[P_DEC]=env[1];t->p[P_SUS]=env[2];t->p[P_REL]=env[3];}
    if(g==DG_FX){t->p[P_LRATE]=65;t->p[P_LD_PIT]=25;t->p[P_LD_SHP]=40;}
}
/* a page's stage: the synth's own as its block says, the firmware's by their families and names */
static uint32_t ds_pg(uint32_t i)                       /* PAGE_TAB i's byte in the synth's block (0: none) */
{
    uint32_t j;
    if(PG(i).scope!=SC_MOD || (j=sch_pg_index(&PG(i)))>=SCH_PG_MAX)return 0;
    ds_cache();
    return ds.dpg[j];
}
static int designer_category(uint32_t i)
{
    const page_t *p=&PG(i);const char *s=p->title;uint32_t d=ds_pg(i)&15u;
    if(d)return (int)d;
    if(p->fam==FAM_ENV)return DT_ENV;
    if(p->fam==FAM_LFO)return DT_MOVE;
    if(str_prefix(s,"VOICE"))return DT_PLAY;
    if(str_eq(s,"EDIT 2") || (p->scope==SC_PADS && p->id[1]==4))return DT_TONE;   /* (a kit's second page: its mix) */
    return DT_SOURCE;
}
static int designer_page_ok(uint32_t i)
{
    const page_t *p=&PG(i);
    if (midi_track_only(TSEL)) return p->scope == SC_ENGINE && p->id[0] == P_E0;
    if(!page_visible(i) || (p->fam!=FAM_EDIT && p->fam!=FAM_ENV && p->fam!=FAM_LFO))return 0;
    if(p->graph==GR_STORE || p->graph==GR_TOOLS || p->graph==GR_MOTION)return 0;
    /* Track mixer/performance parameters and the drum lanes' notes are outside a tone. */
    if(p->scope==SC_GLOBAL || str_prefix(p->title,"LANES"))return 0;
    if(ds_flag(MOD3_DSF_NO_VOICE) && (str_eq(p->title,"VOICE") || str_eq(p->title,"VOICE 2")))return 0;
    if(ds_flag(MOD3_DSF_NO_EDIT) && p->scope==SC_ENGINE)return 0;
    if(pads_on(TSEL) && (str_prefix(p->title,"VOICE") || p->fam==FAM_ENV))return 0;
    return 1;
}
/* MAIN pages: the engine's own first. A synth with LFOs of its own (the track LFO and the matrix: ALL), a drum kit
 * its sounds; its block's pages marked MOD3_DS_SKIP (FM6's macros, EDIT 1 / 2, repeat its own pages) */
static int designer_guided(uint32_t i)
{
    static const char *const skip[]={"VOICE 3","LFO 2","MOD"};
    const page_t *p=&PG(i);const char *s=p->title;
    for(uint32_t j=0;j<NELEM(skip);j++)if(str_eq(s,skip[j]))return 0;
    if(ds_pg(i)&MOD3_DS_SKIP)return 0;
    if(p->scope==SC_TRACK && p->fam==FAM_LFO && (ds_flag(MOD3_DSF_OWN_LFO) || pads_on(TSEL)))return 0;
    if(ds_flag(MOD3_DSF_EDIT_ALL) && p->scope==SC_ENGINE)return 0;
    return 1;
}
static int designer_in(uint32_t i,uint32_t st)
{
    return designer_page_ok(i) && designer_category(i)==(int)st && (ds.expert || designer_guided(i));
}
/* what FEATURES leaves out: a page of the block marked MOD3_DS_LIVE whose control does nothing (a CZ line its line
 * mode does not play) */
static int designer_live(uint32_t i)
{
    uint32_t j;
    if(!(ds_pg(i)&MOD3_DS_LIVE) || !ds_blk())return 1;
    j=sch_pg_index(&PG(i));
    return ds_active(ds_rw(ds_po()+j*3u+1u));
}
static void designer_add(uint32_t i,uint32_t op,uint32_t st)
{
    if(ds.n>=DS_ROUTES)return;
    ds.route[ds.n].page=(uint8_t)i;ds.route[ds.n].op=(uint8_t)op;ds.route[ds.n++].stage=(uint8_t)st;
}
/* FEATURES, then each stage's pages: the engine's own before the track's, an instanced page's together, in instance
 * order (built again whenever FEATURES is left: what it switched off is not shaped) */
static void designer_route(void)
{
    uint32_t st,pass,i,j,op,ni;
    ds.n=0;
    if(designer_feats())designer_add(DS_FEAT,0,DT_BUILD);
    for(st=DT_SOURCE;st<DT_COUNT;st++)for(pass=0;pass<2u;pass++){
        int ops_done=0;
        for(i=0;i<NPAGES;i++){
            if(!designer_in(i,st) || (PG(i).scope==SC_TRACK)!=(int)pass)continue;
            if(!(ni=sch_pg_ninst(&PG(i)))){if(designer_live(i))designer_add(i,0,st);continue;}
            if(ops_done)continue;
            ops_done=1;
            for(op=0;op<ni;op++)if(designer_inst_on(op))
                for(j=i;j<NPAGES;j++)if(sch_pg_ninst(&PG(j)) && designer_in(j,st))designer_add(j,op,st);
        }
    }
}
static void designer_page(void)
{
    if(!ds.n)return;
    ds.stage=ds.route[ds.at].stage;
    if(ds.route[ds.at].page!=DS_FEAT){ui.page=ds.route[ds.at].page;sch_inst=ds.route[ds.at].op;}
    else for(uint32_t i=0;i<ds.n;i++)if(ds.route[i].page!=DS_FEAT){ui.page=ds.route[i].page;break;}   /* (a page of this engine) */
    ds.focus=0;
    if(pop.on==POP_PICK)pop_close();
    page_entered();
}
static void designer_begin(void)
{
    undo_seal();fm1_irq_off();motion_restore(TSEL);motion_design_pause|=(uint8_t)(1u<<ds.track);designer_snapshot(&ds.other);fm1_irq_on();
    load_begin(TSEL,UNDO_SOUND);
    if(!ds.current || ds.eng!=TSEL->eng_req){sound_init_of(TSEL,ds.eng);fm1_irq_off();designer_recipe();fm1_irq_on();}
    load_end(TSEL);undo_seal();
    ds.eng=TSEL->eng_req;ds.compare=0;ds.at=0;designer_route();ui.designer_mode=DS_SHAPE;designer_page();
}
/* What the control the knob turned (ds.focus; a list page: its row) needs before it sounds: the synth's need rules on
 * its pages (the first that names the control: its path), the part's on the track's; DS_NONE nothing, DS_END an
 * envelope step after its END (that is the POINT page's to change) */
static int ds_need_has(const uint8_t *b,uint32_t id)    /* a need rule's ids: lo..hi, every step-th */
{
    uint32_t lo=sch_u16(b),hi=sch_u16(b+2),st=b[4];
    return id>=lo && id<=hi && (st<2u || !((id-lo)%st));
}
static uint32_t designer_need(void)
{
    const page_t *p=cur_page();uint32_t col=ds.focus&3u,id=p->id[col];
    const list_t *l=list_on();
    if(l){
        const lrow_t *rw=list_at(l,lst.row);uint32_t pg=page_titled(rw->page);
        if(ds.focus || pg>=NPAGES)return DS_NONE;      /* (KNOB 2: the row) */
        p=&PG(pg);col=rw->slot&3u;id=p->id[col];
    }
    if(p->scope==SC_MOD && ds_blk()){
        uint32_t n=ds_rb(3),no=MOD_SCH_DHEAD+ds_rb(2)*MOD_SCH_DFEAT,i,ni,k;
        for(i=0;i<n;i++){
            uint8_t b[MOD_SCH_DNEED];
            if(sch_rd(dsb.e,dsb.at+no+i*MOD_SCH_DNEED,b,sizeof b))break;
            if(b[5]&MOD3_DN_DOWN)continue;
            if(b[5]&MOD3_DN_INST){                      /* (the instance shown: its path, but by the one id it leaves out) */
                if(!(ni=sch_pg_ninst(p)))continue;
                k=sch_inst%ni;
                if(sch_col_id(p,col)==(uint32_t)((int32_t)sch_u16(b)+(int8_t)b[4]*(int32_t)k) || designer_feat_get(b[6]+k))continue;
                return b[6]+k;
            }
            if(!ds_need_has(b,id) || ((b[5]&MOD3_DN_DIM) && ds_active(id)))continue;
            return sch_u16(b+8) && !ds_cond(sch_u16(b+8))?b[7]:b[6];
        }
    }
    if(p->scope==SC_TRACK && id==P_DETUNE && TSEL->p[P_VOICE]!=V_UNISON)return DS_HOST+MOD3_HF_VOICE;
    if(p->scope==SC_TRACK && p->fam==FAM_LFO && !str_eq(p->title,"LFO DEST") && p->graph!=GR_MOD &&
       !TSEL->p[P_LD_PIT] && !TSEL->p[P_LD_FLT] && !TSEL->p[P_LD_SHP] && !TSEL->p[P_LD_AMP] && !TSEL->p[P_M1SRC])
        return DS_HOST+MOD3_HF_VIB;
    return DS_NONE;
}
/* the control turned down switches nothing on (a need rule MOD3_DN_DOWN: FM6's silent operator's level stays silent) */
static int designer_down(void)
{
    const page_t *p=cur_page();uint32_t n,no,i,id;
    if(!ds_blk())return 0;
    n=ds_rb(3);no=MOD_SCH_DHEAD+ds_rb(2)*MOD_SCH_DFEAT;id=sch_col_id(p,ds.focus&3u);
    for(i=0;i<n;i++){
        uint8_t b[MOD_SCH_DNEED];
        if(sch_rd(dsb.e,dsb.at+no+i*MOD_SCH_DNEED,b,sizeof b))break;
        if((b[5]&MOD3_DN_DOWN) && (!(b[5]&MOD3_DN_INST) || sch_pg_ninst(p)) && ds_need_has(b,id))return 1;
    }
    return 0;
}
/* A dim control turned (s): the paths it needs switched on first, the last said in the header ("PULSE A ON").
 * A control that sounds already changes nothing else */
static void designer_enable(int32_t s)
{
    char b[24];uint32_t f=DS_NONE,n=0,i;
    if(s<0 && designer_down())return;
    while(n<3u && (f=designer_need())<DS_END){
        fm1_irq_off();designer_feat_set(f,designer_estate(f));fm1_irq_on();
        sync_reload=1;n++;
        designer_eword(f,b);
        for(i=0;b[i];i++)if(b[i]>='a' && b[i]<='z')b[i]=(char)(b[i]-32);
        str_cpy(b+str_len(b)," ON",4);
    }
    if(f==DS_END)ui_message("AFTER THE END POINT");
    else if(n)ui_message(b);
}
/* the header: its chip (START, FEATURES, the original compared: their own; a page: its own), the stage's pages as
 * its dots (ui_draw.c head_chip_text, head_pages) */
static int designer_head_chip(char *b)
{
    const char *c=ds.compare?"ORIGINAL":ui.designer_mode==DS_SETUP?"DESIGN":designer_feat_on()?"FEATURES":0;
    if(c){str_cpy(b,c,16);return 1;}
    if(sch_pg_ninst(cur_page())){                       /* (the tabs are the stages: the instance, the line here) */
        uint32_t n=sch_inst_name(sch_inst,b,12);b[n]=' ';sec_chip(ui.page,b+n+1,(uint32_t)(15u-n));return 1;
    }
    if(cur_page()->scope==SC_MOD && ds_flag(MOD3_DSF_TITLE_CHIP)){str_cpy(b,cur_page()->title,16);return 1;}
    return 0;
}
static void designer_pages(uint32_t *n,uint32_t *k)
{
    *n=*k=0;
    for(uint32_t i=0;ui.designer_mode==DS_SHAPE && i<ds.n;i++)if(ds.route[i].stage==ds.stage){if(i==ds.at)*k=*n;(*n)++;}
}
/* the stages there are as the tabs under the header (a sound page's, ui_pages.c) */
static int designer_tabs_on(void) { return ui.designer_mode==DS_SHAPE && !designer_feat_on(); }
static void designer_tabs(void)
{
    const char *nm[DT_COUNT];uint32_t n=0,cur=0,i,st;
    for(st=0;st<DT_COUNT;st++){
        for(i=0;i<ds.n && ds.route[i].stage!=st;i++);
        if(i==ds.n)continue;
        if(st==ds.stage)cur=n;
        nm[n++]=DS_TAB[st];
    }
    tabs_draw(nm,n,cur,cur*7u+n*131u+ds.eng*1031u+ux.gen*977u+ux.pal*31u+song.sel+0x5E5u);
}
/* START (large type, C): ENGINE, START (this sound, INIT, or INIT with a goal's recipe), PAGES (MAIN, ALL) as rows
 * (KNOB 1 .. 3 turn them: the row turning tinted), the engine large under them, the keys under that (NEW SONG's) */
static void new_keys_row(const char *go,uint32_t icon);  /* ui_new.c */
static int designer_init(void) { return !ds.current || ds.eng!=TSEL->eng_req; }
static uint32_t designer_start(void) { return !designer_init()?0u:ds.goal==DG_FREE?1u:1u+ds.goal; }   /* 0 .. DG_COUNT */
static void designer_start_set(uint32_t k)
{
    k=k<(ds.eng==TSEL->eng_req?0u:1u)?1u:k>DG_COUNT?DG_COUNT:k;   /* (another engine: from INIT) */
    ds.current=k==0u;ds.goal=(uint8_t)(k<2u?DG_FREE:k-1u);
}
static void designer_start_draw(void)
{
    char b[24];
    const aafont_t *f=TY_HR;
    uint32_t k,s=ds.eng*7u+ds.goal*131u+ds.current*1031u+ds.expert*40503u+(ui.hot_t?ui.hot_col+1u:0u)*7919u+ux.gen*977u+
               ux.pal*31u+song.sel;
    if(ui.force)lcd_fill(0,H_HEAD,240,240-H_HEAD,T_BG);
    draw_head();
    if(!ui.force && s==ds.sig)return;
    ds.sig=s;
    uint32_t st=designer_start();
    for(k=0;k<3u;k++){
        static const char *const LB[3]={"Engine","Start","Pages"};
        if(k==0u)menu_words(b,eng_name(ds.eng),sizeof b);
        else if(k==1u){if(st<2u)str_cpy(b,st?"Init":"This sound",sizeof b);else{menu_words(b,DS_GOAL[ds.goal],16);str_cpy(b+str_len(b)," from Init",12);}}
        else str_cpy(b,ds.expert?"All":"Main",sizeof b);
        cv_begin(240,LIST_ROW,T_BG);
        row_draw(0,LB[k],b,T_TEXT,ui.hot_t && ui.hot_col==k,0,k);
        cv_blit(0,(uint32_t)(LIST_Y0+(int32_t)k*LIST_ROW));
    }
    if(text_w(f,eng_name(ds.eng))>220 || !text_has(f,eng_name(ds.eng)))f=TY_VL;
    for (uint32_t band = 0; band < (uint32_t)(Y_FOOT-(LIST_Y0+3*LIST_ROW)); band += CV_MAX / (240)) {
        cv_begin_band(240,Y_FOOT-(LIST_Y0+3*LIST_ROW),T_BG,band);   /* (the engine: large) */
        cv_text_c(120,ty_y((Y_FOOT-(LIST_Y0+3*LIST_ROW))/2,f),f,eng_name(ds.eng),T_THEME,T_BG);
        cv_blit_band(0,LIST_Y0+3*LIST_ROW);
    }
    new_keys_row("Design",ICON_X_RIGHT);
}
/* FEATURES: a row a path, its state at the right (off: dim); an operator (MOD3_FF_CARRIER) says what it is in the
 * algorithm */
static int designer_feat_cap(uint32_t r,char *lb,char *v,uint16_t *vc)
{
    uint32_t n=designer_feats(),c,st,w;ds_feat_t f;
    c=designer_code(r%n);st=designer_feat_get(c);
    w=designer_word(c,lb);
    if(c<DS_HOST && !ds_feat(c,&f) && (f.flags&MOD3_FF_CARRIER) && w<16u)
        str_cpy(lb+w,ds_car(f.in)?" carrier":" modulator",16u-w);
    designer_feat_text(c,st,v);
    *vc=st || designer_feat_states(c)>2u?T_TEXT:T_DIM;
    return 0;
}
static void designer_feat_draw(void)
{
    uint32_t n,r,sig,vis=(uint32_t)((PV_SOUND_Y-LIST_Y0-2)/LIST_ROW);
    char lb[16],v[16];uint16_t vc;
    n=designer_feats();
    if(ui.force){lcd_fill(0,H_HEAD,240,PV_SOUND_Y-H_HEAD,T_BG);ui.graph_sig=0;}
    draw_head();
    pv_sound();
    if(ds.row>=n)ds.row=0;
    if(ds.row<ds.first)ds.first=ds.row;
    if(ds.row>=ds.first+vis)ds.first=(uint8_t)(ds.row+1u-vis);
    sig=ds.row*7u+ds.first*131u+ux.gen*977u+ux.pal*31u+song.sel;
    for(r=ds.first;r<n && r<ds.first+vis;r++){designer_feat_cap(r,lb,v,&vc);sig=str_hash(str_hash(sig,lb),v)+vc;}
    if(ui.force || sig!=ds.sig){ds.sig=sig;list_rows(n,ds.row,ds.first,vis,designer_feat_cap);}
}
/* under a stage's page, as under NEW SONG: OCT- back, the sound, OCT+ the next stage (the last: Finish), in place of
 * the sound's line (ui_pages.c pv_sound) */
static int designer_next(void)                          /* the stage after this one, DT_COUNT none */
{
    for(uint32_t i=ds.at+1u;i<ds.n;i++)if(ds.route[i].stage!=ds.stage)return ds.route[i].stage;
    return DT_COUNT;
}
static int designer_foot(void)
{
    char nm[20];
    uint32_t nx,sig;
    const char *go;
    int32_t gw;
    if(ui.designer_mode!=DS_SHAPE)return 0;
    nx=(uint32_t)designer_next();go=nx<DT_COUNT?DS_TAB[nx]:"Finish";
    sound_name(TSEL,nm);
    sig=str_hash(str_hash(nx*7u+song.sel*131u+ux.gen*977u+ux.pal*31u+0xF00Du,go),nm);
    if(!ui.force && sig==pv.sound)return 1;
    pv.sound=sig;
    gw=text_w(TY_TX,go)+42;
    cv_begin(240,240-PV_SOUND_Y,T_BG);                  /* (NEW SONG's keys, the sound between them) */
    cv_rrect(4,2,48,24,6,T_SURF,T_BG);
    cv_icon_mid(20,14,16,ICON_X_UNDO,T_MID,T_SURF);
    if(text_w(TY_LB,nm)<=236-gw-8-60)                  /* (the sound's name where it fits whole) */
        cv_text_on(60,ty_y(14,ty_fit(TY_TX,nm,236-gw-8-60)),ty_fit(TY_TX,nm,236-gw-8-60),nm,T_SEC,T_BG);
    cv_rrect(236-gw,2,gw,24,6,T_THEME,T_BG);
    cv_icon_mid(236-gw+10,14,16,nx<DT_COUNT?ICON_X_RIGHT:ICON_X_CHECK,T_INK,T_THEME);
    cv_text_on(236-gw+32,ty_y(14,TY_TX),TY_TX,go,T_INK,T_THEME);
    cv_blit(0,PV_SOUND_Y);
    return 1;
}
/* START and FEATURES (draw_page): the shaping stages are pages, drawn as ever */
static int designer_screen(void) { return designer_on() && (ui.designer_mode==DS_SETUP || designer_feat_on()); }
static void designer_draw(void)
{
    if(ui.designer_mode==DS_SETUP)designer_start_draw();
    else designer_feat_draw();
}
/* its sheet (HOME; OCT past the first or the last stage): the sound kept, saved as a user sound, or the original's */
static void ds_keep(void) { designer_close(0); }
static void ds_save_user(void) { designer_close(0);ps_save(); }
static const sheet_row_t SHEET_DESIGN[] = {{"Keep the sound", 0, 0, ds_keep, 0}, {"Save as user\x85", 0, 0, ds_save_user, 0},
                                           {"Discard", 0, 0, 0, CF_DISCARD_DESIGN}};
static void designer_finish(void)
{
    char b[24];
    sound_name(TSEL,b);
    sheet_open(b[0]?b:"Sound design","design",SHEET_DESIGN,NELEM(SHEET_DESIGN));
}
