/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Drive every native tone knob through the real panel/UI path, then render
 * notes and releases. Each probe enables that control's required signal path.
 * Physical encoders, speakers and calibrated Prophet fidelity are separate. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <math.h>

#define P5_TEST_N (FS * 2u)
static int32_t reference[P5_TEST_N], result[P5_TEST_N];
static int failures;

static void control_setup(uint32_t id)
{
    eng_state_reset();ui_power_on();vage=0;
    set_engine_of(TSEL,ENGI_PROPHET);apply_preset_to(TSEL,0);
    TSEL->engine=TSEL->eng_req;TSEL->xf=TSEL->xf_on=0;
    panic_req=0;TSEL->p[P_RESERVED33]=TSEL->p[P_RESERVED34]=TSEL->p[P_RESERVED35]=TSEL->p[P_RESERVED36]=0;
    uint8_t *p=p5_patch_of(TSEL)->raw;
    p[P5_FREQ_A]=24;p[P5_FREQ_B]=38;p[P5_FINE_B]=17;
    p[P5_SAW_A]=p[P5_PULSE_A]=p[P5_SAW_B]=p[P5_PULSE_B]=p[P5_TRI_B]=1;
    p[P5_LEVEL_A]=90;p[P5_LEVEL_B]=80;p[P5_NOISE]=10;
    p[P5_CUTOFF]=55;p[P5_RESONANCE]=20;p[P5_ENV_FILTER]=30;
    p[P5_ATTACK_FILTER]=p[P5_ATTACK_AMP]=18;
    p[P5_DECAY_FILTER]=p[P5_DECAY_AMP]=45;
    p[P5_SUSTAIN_FILTER]=40;p[P5_SUSTAIN_AMP]=85;
    p[P5_RELEASE_FILTER]=p[P5_RELEASE_AMP]=60;
    p[P5_LFO_RATE]=90;p[P5_LFO_INITIAL]=40;p[P5_LFO_TRI]=1;
    p[P5_WHEEL_FREQ_A]=p[P5_WHEEL_FREQ_B]=p[P5_WHEEL_PW_A]=p[P5_WHEEL_PW_B]=p[P5_WHEEL_FILTER]=1;
    p[P5_POLY_ENV]=16;p[P5_POLY_B]=10;
    p[P5_POLY_FREQ]=p[P5_POLY_PW]=p[P5_POLY_FILTER]=1;
    TSEL->at=50;
    /* Prevent other modulation from masking the path under test. */
    if(id==P5_PW_A||id==P5_WHEEL_PW_A||id==P5_POLY_PW){p[P5_SAW_A]=0;p[P5_LEVEL_B]=0;}
    if(id==P5_PW_B||id==P5_WHEEL_PW_B){p[P5_SAW_B]=p[P5_TRI_B]=0;p[P5_LEVEL_A]=0;}
    if(id==P5_POLY_FILTER||id==P5_POLY_ENV||id==P5_ENV_FILTER||id==P5_VEL_FILTER||
       id==P5_ATTACK_FILTER||id==P5_DECAY_FILTER||id==P5_SUSTAIN_FILTER||id==P5_RELEASE_FILTER){
        p[P5_POLY_FREQ]=p[P5_POLY_PW]=0;p[P5_WHEEL_FILTER]=0;p[P5_CUTOFF]=30;
        p[P5_POLY_B]=0;p[P5_ENV_FILTER]=50;p[P5_POLY_ENV]=20;
    }
    if(id==P5_RELEASE_FILTER){p[P5_RELEASE_AMP]=100;}
    if(id==P5_GLIDE){p[P5_POLY_FREQ]=0;}
    if(id==P5_UNISON_COUNT||id==P5_UNISON_DETUNE||id==P5_RETRIGGER){
        p[P5_UNISON]=1;p[P5_UNISON_COUNT]=3;p[P5_UNISON_DETUNE]=3;
    }
    if(id==P5_RETRIGGER){p[P5_ATTACK_AMP]=65;p[P5_ATTACK_FILTER]=65;}
    if(id==P5_BEND)TSEL->bend_raw=4096;
    p5_track_accept(TSEL);
}

/* The Prophet's panel bytes as Melodee 1.x's P5_PANEL described them: its schema's parameters (ids = the bytes),
 * their raw ranges (the semitone, voice count and bend displays come from the module: p5_ui.c) */
#define P5_NPANEL 88u
static const param_desc_t *p5_raw_desc(uint32_t id)
{
    static param_desc_t d;
    static char l[MOD_SCH_NAME + 1u];
    sch_param_t p;
    if (id >= P5_NPANEL || sch_param(ENGI_PROPHET, id, &p))
        return 0;
    sch_str(ENGI_PROPHET, p.label, l, sizeof l);
    d = (param_desc_t){l, p.kind == MOD3_SK_FIELD ? p.fmt : F_INT, p.kind == MOD3_SK_FIELD ? p.min : id == P5_UNISON_COUNT ? 1 : 0,
                       p.kind == MOD3_SK_FIELD ? p.max : id == P5_UNISON_COUNT ? 5 : id == P5_BEND ? 11 : 127, 0, 0, 0};
    return &d;
}
static int p5_keyed_tune(const track_t *t, uint32_t id) { return p5_keyed(p5_patch_of(t)->raw, id); }
static int32_t p5_panel_raw(const track_t *t, uint32_t id, int32_t value)
{
    return p5_keyed_tune(t, id) ? (value + 12) * 2 : value - (id == P5_BEND ? 1 : 0);
}
static int p5_ed_active(const track_t *t, uint32_t id) { return p5_pactive(t, p5_patch_of(t)->raw, id); }
/* a raw panel byte written (the editor's op 95 2): its range, the engine told */
static void p5_edit_value(track_t *t, uint32_t id, int32_t value)
{
    const param_desc_t *d = p5_raw_desc(id);
    if (!d)
        return;
    p5_patch_of(t)->raw[id] = (uint8_t)clamp(value, d->min, d->max);
    patch_note(trk_ix(t), MOD3_PATCH_EDIT, id);
}

/* Set a raw field using slow detents on its actual tone page. Check exact
 * native bytes: all unrelated and opaque bytes must survive every detent. */
static int control_turn(uint32_t id,uint32_t value)
{
    uint32_t page=0,col=0;
    for(;page<NPAGES;page++)if(PG(page).scope==SC_MOD&&PG(page).graph!=GR_STORE&&PG(page).graph!=GR_TOOLS){   /* (a tone page, not the store's) */
        for(col=0;col<4;col++)if(PG(page).id[col]==id)break;
        if(col<4)break;
    }
    if(page==NPAGES)return 0;
    go_title(PG(page).title);
    if(p5_keyed_tune(TSEL,id))value=clamp(value,0,96)/2*2;
    int ok=1;
    for(uint32_t n=0;n<256&&p5_patch_of(TSEL)->raw[id]!=value;n++){
        p5_patch_t expected=*p5_patch_of(TSEL);
        int step=expected.raw[id]<value?1:-1;
        int16_t *vp;const param_desc_t *d=page_desc(cur_page(),col,&vp);
        expected.raw[id]=(uint8_t)p5_panel_raw(TSEL,id,clamp(*vp+step,d->min,d->max));
        turn(EN_K1+col,step);
        ok &= !memcmp(&expected,p5_patch_of(TSEL),sizeof expected);
    }
    return ok&&p5_patch_of(TSEL)->raw[id]==value;
}

static void control_render(uint32_t id,int32_t *pcm)
{
    track_t *t=TSEL;
    trk_note_on(t,55,40);
    for(uint32_t frame=0;frame<P5_TEST_N;frame+=CTL){
        /* A second, overlapping note exercises glide, tracking and priority;
         * both note-offs exercise the envelopes and RELEASE switch. */
        if(frame<=FS/3&&frame+CTL>FS/3)trk_note_on(t,72,110);
        if(frame<=FS/2&&frame+CTL>FS/2)trk_note_off(t,55);
        if(frame<=FS&&frame+CTL>FS)trk_note_off(t,72);
        int32_t out[CTL]={0};track_render(t,out,CTL);
        for(uint32_t i=0;i<CTL&&frame+i<P5_TEST_N;i++)pcm[frame+i]=out[i];
    }
    (void)id;
}

static double control_difference(void)
{
    double diff=0,power=1;
    for(uint32_t i=0;i<P5_TEST_N;i++){
        double d=(double)result[i]-reference[i];diff+=d*d;
        power+=(double)reference[i]*reference[i];
    }
    return diff?10*log10(diff/power):-999;
}

static void dependency_tests(void)
{
    static const uint8_t presets[]={0,1,2,7,14,31,64,117,200};
    uint32_t checked=0;
    for(uint32_t k=0;k<NELEM(presets);k++){
        control_setup(P5_CUTOFF);apply_preset_to(TSEL,presets[k]);
        p5_patch_t base=*p5_patch_of(TSEL);
        for(uint32_t id=0;id<P5_NPANEL;id++){
            if(!p5_raw_desc(id)||p5_ed_active(TSEL,id))continue;
            for(uint32_t side=0;side<2;side++){
                control_setup(P5_CUTOFF);*p5_patch_of(TSEL)=base;
                p5_track_accept(TSEL);p5_edit_value(TSEL,id,side?p5_raw_desc(id)->max:p5_raw_desc(id)->min);
                control_render(id,side?result:reference);
            }
            char label[128];snprintf(label,sizeof label,"Prophet preset %u: dim field %u %s leaves audio identical",presets[k],id,p5_raw_desc(id)->label);
            failures+=check(label,control_difference()==-999);checked++;
            *p5_patch_of(TSEL)=base;p5_track_accept(TSEL);
        }
    }
    failures+=check("inactive signal paths verified against INIT and eight factory sounds",checked>40);
    /* Oscillator B's pulse width remains active as a Poly-Mod source even
     * with its mixer level at zero; full-noise wheel mix bypasses LFO waves. */
    control_setup(P5_PW_B);uint8_t *p=p5_patch_of(TSEL)->raw;
    p[P5_LEVEL_B]=0;p[P5_LEVEL_A]=90;
    failures+=check("muted B still exposes pulse width when Poly-Mod uses it",p5_ed_active(TSEL,P5_PW_B));
    p[P5_WHEEL_MIX]=127;
    failures+=check("noise-only wheel mix dims the unused LFO rate and waveform",!p5_ed_active(TSEL,P5_LFO_RATE)&&!p5_ed_active(TSEL,P5_LFO_TRI));
    p[P5_RELEASE_ON]=0;
    failures+=check("RELEASE off dims both native release times",!p5_ed_active(TSEL,P5_RELEASE_FILTER)&&!p5_ed_active(TSEL,P5_RELEASE_AMP));
}

static void tuning_tests(void)
{
    int exact=1,untouched=1;
    for(uint32_t id=P5_FREQ_A;id<=P5_FREQ_B;id++)for(uint32_t raw=0;raw<=127;raw++){
        control_setup(id);p5_patch_of(TSEL)->raw[id]=(uint8_t)raw;
        p5_patch_t before=*p5_patch_of(TSEL);
        go_title(id==P5_FREQ_A?"P5 OSC A":"P5 OSC B");
        int16_t *vp;const param_desc_t *d=page_desc(cur_page(),0,&vp);
        int32_t value=*vp;
        untouched &= !memcmp(&before,p5_patch_of(TSEL),sizeof before);
        exact &= d->fmt==F_SEMI&&value==(int32_t)(raw>96?96:raw)/2-12;
        int step=value<36?1:-1;
        turn(EN_K1,step);
        before.raw[id]=(uint8_t)((value+step+12)*2);
        exact &= !memcmp(&before,p5_patch_of(TSEL),sizeof before);
    }
    failures+=check("all 256 keyed tuning values display and step by one semitone",exact);
    failures+=check("viewing keyed tuning preserves imported odd and upper raw bytes",untouched);
    control_setup(P5_FREQ_B);p5_patch_of(TSEL)->raw[P5_KEY_B]=0;
    failures+=check("keyboard-off B keeps its continuous raw range up to 127",control_turn(P5_FREQ_B,127));
    /* Native field edits are a raw protocol, independent of the display. */
    p5_edit_value(TSEL,P5_FREQ_A,113);
    failures+=check("native protocol still accepts exact raw tuning bytes",p5_patch_of(TSEL)->raw[P5_FREQ_A]==113);
    control_setup(P5_UNISON_COUNT);p5_patch_of(TSEL)->raw[P5_UNISON_COUNT]=0;
    go_title("P5 UNISON");int16_t *vp;page_desc(cur_page(),1,&vp);
    failures+=check("native zero voice count displays its actual five-voice default",*vp==5&&p5_patch_of(TSEL)->raw[P5_UNISON_COUNT]==0);
    turn(EN_K2,-1);
    failures+=check("turning down the default voice count selects four voices",p5_patch_of(TSEL)->raw[P5_UNISON_COUNT]==4);
}

static void assignment_tests(void)
{
    int priority=1,retrigger=1,count=1,bend=1;
    for(uint32_t mode=0;mode<4;mode++){
        control_setup(P5_RETRIGGER);control_turn(P5_RETRIGGER,mode);
        trk_note_on(TSEL,mode<2?72:55,100);
        int32_t out[CTL];for(uint32_t k=0;k<300;k++)track_render(TSEL,out,CTL);
        p5_voice_t *s=p5_voice(TSEL,&TSEL->v[0]);s->amp.stage=s->filter.stage=2;
        trk_note_on(TSEL,mode<2?55:72,100);
        priority &= TSEL->v[0].note==(mode<2?55:72);
        retrigger &= s->amp.stage==((mode&1)?1:2)&&s->filter.stage==((mode&1)?1:2);
    }
    failures+=check("LOW and LAST priority both select the intended held note",priority);
    failures+=check("both RET priority modes retrigger; their other modes stay legato",retrigger);
    for(uint32_t voices=1;voices<=5;voices++){
        control_setup(P5_UNISON_COUNT);control_turn(P5_UNISON_COUNT,voices);
        trk_note_on(TSEL,60,100);uint32_t sounding=0;
        for(uint32_t k=0;k<NVOICE;k++)sounding+=TSEL->v[k].active&&TSEL->v[k].gate;
        count &= sounding==voices;
    }
    failures+=check("every selectable unison count allocates the requested voices",count);
    for(uint32_t range=0;range<12;range++){
        control_setup(P5_BEND);control_turn(P5_BEND,range);
        TSEL->bend_raw=8191;trk_bend_own(TSEL,eng_of(TSEL->engine));bend &= midi_bend_target[0]==(int32_t)(range+1)*256;
        TSEL->bend_raw=-8192;trk_bend_own(TSEL,eng_of(TSEL->engine));bend &= midi_bend_target[0]==-(int32_t)(range+1)*256;
    }
    failures+=check("all twelve bend ranges reach their exact positive/negative endpoints",bend);
}

static void held_note_tests(void)
{
    uint32_t count=0;
    for(uint32_t id=0;id<P5_NPANEL;id++){
        if(!p5_raw_desc(id))continue;
        /* These are note assignment/onset settings, or need a pitch change
         * or note-off. The phrase probes above exercise their actual trigger. */
        if(id==P5_GLIDE||id==P5_VINTAGE||id==P5_ATTACK_FILTER||id==P5_ATTACK_AMP||
           id==P5_RELEASE_FILTER||id==P5_RELEASE_AMP||id==P5_RELEASE_ON||
           id==P5_UNISON||id==P5_UNISON_COUNT||id==P5_UNISON_DETUNE||id==P5_RETRIGGER)continue;
        for(uint32_t side=0;side<2;side++){
            /* Slow decay must still be running when edited; starting at a
             * zero decay already reaches sustain before the edit occurs. */
            control_setup(id);failures+=!control_turn(id,p5_raw_desc(id)->max);
            trk_note_on(TSEL,72,40);
            int32_t *pcm=side?result:reference;memset(pcm,0,sizeof reference);
            for(uint32_t frame=0;frame<FS;frame+=CTL){
                if(side&&frame<=FS/4&&frame+CTL>FS/4)failures+=!control_turn(id,p5_raw_desc(id)->min);
                int32_t out[CTL]={0};track_render(TSEL,out,CTL);
                for(uint32_t i=0;i<CTL&&frame+i<FS;i++)pcm[frame+i]=out[i];
            }
        }
        char label[128];double change=control_difference();
        snprintf(label,sizeof label,"Prophet field %u %s updates a held note (%.1f dB)",id,p5_raw_desc(id)->label,change);
        failures+=check(label,change>-60);count++;
    }
    failures+=check("all 46 controls applicable during a held note reach its renderer",count==46);
}

int main(void)
{
    uint32_t count=0;int ok;
    failures+=check("tone navigation matches prefixes without reading beyond short titles",str_prefix("P5 OSC A","P5 OSC")&&!str_prefix("EDIT 1","P5 B WAVES")&&!str_prefix("","P5")&&str_prefix("P5",""));
    control_setup(P5_CUTOFF);control_render(P5_CUTOFF,reference);
    control_setup(P5_CUTOFF);control_render(P5_CUTOFF,result);
    failures+=check("Prophet knob probe renders are deterministic",control_difference()==-999);
    for(uint32_t id=0;id<P5_NPANEL;id++){
        if(!p5_raw_desc(id))continue;
        control_setup(id);ok=control_turn(id,p5_raw_desc(id)->min);control_render(id,reference);
        control_setup(id);ok &= control_turn(id,p5_raw_desc(id)->max);control_render(id,result);
        double change=control_difference();char label[128];
        snprintf(label,sizeof label,"Prophet field %u %s: panel edit changes rendered sound (%.1f dB)",id,p5_raw_desc(id)->label,change);
        failures+=check(label,ok&&change>-60);count++;
    }
    failures+=check("all 57 native Prophet tone controls exercised",count==57u);
    dependency_tests();tuning_tests();held_note_tests();assignment_tests();
    printf("Prophet knobs: %u controls, %d failure(s)\n",count,failures);
    return failures!=0;
}
