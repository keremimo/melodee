/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Drive real panel input and native setters, render the resulting recipes. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
static int failures;
/* the selected synth's path named w (its FEATURES row's word; else the part's path of that word) */
static uint32_t df(const char *w)
{
    char b[24];
    for(uint32_t r=0;r<designer_feats();r++){(void)designer_word(designer_code(r),b);if(str_eq(b,w))return designer_code(r);}
    for(uint32_t h=0;h<MOD3_HF_COUNT;h++)if(str_eq(DS_HF[h].word,w))return DS_HOST+h;
    failures+=check("the path named is the synth's",0);return DS_NONE;
}
static const char *dword(uint32_t c) { static char b[24];(void)designer_word(c,b);return b; }
static void designer_at(const char *title,uint32_t op)
{
    for(uint32_t i=0;i<ds.n;i++)if(ds.route[i].page!=DS_FEAT && str_eq(PG(ds.route[i].page).title,title) && ds.route[i].op==op){
        ds.at=(uint8_t)i;designer_page();return;
    }
    failures+=check("requested sound page is reachable",0);
}
/* HOME: the designer's sheet; row r (0 Keep the sound, 1 Save as user.., 2 Discard), OCT+, its question: yes */
static void designer_finish_row(uint32_t r)
{
    press(B_HOME);
    for(uint32_t i=0;i<r;i++)turn(EN_K2,1);
    press(B_OCTUP);
    if(ui.confirm)press(B_OCTUP);
}
static void transaction_tests(void)
{
    ui_power_on();sound_init_of(TSEL,ENGI_PROPHET);
    p5_patch_of(TSEL)->raw[120]=57; /* opaque bytes must survive */
    designer_sound_t before,after,draft;
    TSEL->step[0].note[0]=64;TSEL->step[0].n=1;TSEL->p[P_ARATE]=3;
    designer_snapshot(&before);
    uint32_t steps=steps_sig(TSEL);int16_t arp=TSEL->p[P_ARATE];
    designer_open();
    turn(EN_K2,1);int init=designer_init() && ds.goal==DG_FREE;turn(EN_K2,1);turn(EN_K2,1);
    int pad=designer_init() && ds.goal==DG_PAD;turn(EN_K1,1);turn(EN_K2,-1);turn(EN_K2,-1);turn(EN_K2,-1);turn(EN_K2,-1);
    int other=designer_init() && designer_start()==1u;turn(EN_K1,-1);turn(EN_K2,-1);
    failures+=check("START steps CURRENT, INIT, then the goals; another engine starts from INIT",init && pad && other && !designer_init());
    press(B_OCTUP);designer_snapshot(&after);
    failures+=check("START CURRENT preserves the exact sound",!memcmp(&before,&after,sizeof before));
    designer_at("P5 OSC A",0);turn(EN_K4,1);designer_snapshot(&draft);
    press(B_SAVE);designer_snapshot(&after);
    failures+=check("SAVE compares against exact original, including opaque bytes",ds.compare && !memcmp(&before,&after,sizeof before));
    turn(EN_K1,1);designer_snapshot(&after);
    failures+=check("original cannot be changed while comparing",!memcmp(&before,&after,sizeof before));
    press(B_SAVE);designer_snapshot(&after);
    failures+=check("second SAVE returns to exact edited draft",!ds.compare && !memcmp(&draft,&after,sizeof draft));
    designer_finish_row(2);designer_snapshot(&after);
    failures+=check("discard restores exact original without touching pattern or arp",!designer_on() && !memcmp(&before,&after,sizeof before) && steps_sig(TSEL)==steps && TSEL->p[P_ARATE]==arp);
    designer_open();ds.eng=ENGI_CZ;ds.current=0;press(B_OCTUP);
    failures+=check("engine change happens on BEGIN and retains pattern",TSEL->eng_req==ENGI_CZ && steps_sig(TSEL)==steps);
    press(B_SAVE);press(B_SAVE);designer_finish_row(2);designer_snapshot(&after);
    failures+=check("cross-engine compare/discard restores the original native patch",!memcmp(&before,&after,sizeof before));
}
static void dependency_tests(void)
{
    ui_power_on();sound_init_of(TSEL,ENGI_PROPHET);designer_open();press(B_OCTUP);
    uint8_t *p=p5_patch_of(TSEL)->raw;
    designer_at("P5 FILTER",0);p5_patch_t was=*p5_patch_of(TSEL);turn(EN_K1,1);was.raw[P5_CUTOFF]=p[P5_CUTOFF];
    failures+=check("a control that sounds changes nothing else",!memcmp(&was,p5_patch_of(TSEL),sizeof was) && !ui.msg_t);
    designer_at("P5 OSC A",0);p[P5_PULSE_A]=0;turn(EN_K4,1);
    failures+=check("turning a dim WIDTH A switches PULSE A on and says so",p[P5_PULSE_A] && p[P5_LEVEL_A] &&
                    designer_need()==DS_NONE && str_eq(ui.msg,"PULSE A ON"));
    designer_at("P5 POLYMOD",0);p[P5_POLY_FREQ]=p[P5_POLY_PW]=p[P5_POLY_FILTER]=0;turn(EN_K1,1);
    failures+=check("turning Poly-Mod ENV AMT pairs it with a destination",p[P5_POLY_ENV] && p[P5_POLY_FILTER] && designer_need()==DS_NONE);
    designer_at("P5 POLYMOD",0);p[P5_PULSE_A]=0;p[P5_POLY_FILTER]=p[P5_POLY_FREQ]=0;ds.focus=3;
    failures+=check("Poly-Mod PW A first needs pulse A",designer_need()==df("Pulse A"));
    turn(EN_K4,1);
    failures+=check("one turn switches both of its paths on",p[P5_PULSE_A] && p[P5_POLY_PW] && designer_need()==DS_NONE);
    designer_at("P5 UNISON",0);p[P5_UNISON]=0;TSEL->p[P_VOICE]=V_POLY;turn(EN_K2,-1);
    failures+=check("turning VOICES switches native unison on",p[P5_UNISON] && TSEL->p[P_VOICE]==V_UNISON && p5_cap(TSEL)>1);
    designer_at("P5 LFO",0);p[P5_LFO_INITIAL]=0;p[P5_LFO_SAW]=p[P5_LFO_TRI]=p[P5_LFO_PULSE]=0;
    p[P5_WHEEL_FREQ_A]=p[P5_WHEEL_FREQ_B]=p[P5_WHEEL_PW_A]=p[P5_WHEEL_PW_B]=p[P5_WHEEL_FILTER]=0;turn(EN_K1,1);
    failures+=check("turning LFO RATE connects wave, initial depth and destination",p[P5_LFO_TRI] && p[P5_LFO_INITIAL] && p[P5_WHEEL_FILTER] && designer_need()==DS_NONE);
    designer_at("P5 FLT ENV",0);p[P5_RELEASE_ON]=0;p[P5_ENV_FILTER]=0;p[P5_POLY_ENV]=0;turn(EN_K4,1);
    failures+=check("turning filter REL switches release and the filter envelope on",p[P5_RELEASE_ON] && p[P5_ENV_FILTER] && designer_need()==DS_NONE);
    designer_close(0);sound_init_of(TSEL,ENGI_FM6);designer_open();press(B_OCTUP);
    fm6_on_set(0,FM6_ON_ALL);sch_set_t(0,3*FP_OP+FP_OL,50);designer_route();
    designer_at("OUT",2);fm6_on_set(0,fm6_on_of(0)&~(1u<<3));sch_set_t(0,3*FP_OP+FP_OL,0);turn(EN_K1,-1);
    failures+=check("a silent operator's level turned down stays silent",!(fm6_on_of(0)&(1u<<3)) && !fm6_patch_of(0)[3*FP_OP+FP_OL]);
    turn(EN_K1,1);
    failures+=check("turned up, the displayed OP3 (not reversed OP4) is switched on",(fm6_on_of(0)&(1u<<3)) && fm6_patch_of(0)[3*FP_OP+FP_OL]>0);
    designer_close(0);sound_init_of(TSEL,ENGI_CZ);designer_open();press(B_OCTUP);
    designer_at("CZ VIBRATO",0);sch_set_t(0,LCZ_VDEP,0);turn(EN_K2,1);
    failures+=check("turning CZ vibrato RATE gives it depth",cz_patch_of(0)->raw[11]!=0 && designer_need()==DS_NONE);
    designer_at("CZ DETUNE",0);sch_set_t(0,LCZ_LINE,0);turn(EN_K4,1);
    failures+=check("turning a line-2 DETUNE selects 1+2'",designer_feat_get(df("Lines"))==2u && designer_need()==DS_NONE);
    designer_close(0);
}
/* FEATURES' row of path f (KNOB 1 turns it: the knob returned) */
static uint32_t designer_feature(uint32_t f)
{
    uint32_t n=designer_feats();
    for(uint32_t i=0;i<ds.n;i++)if(ds.route[i].page==DS_FEAT){
        ds.at=(uint8_t)i;designer_page();
        for(uint32_t r=0;r<n;r++)if(designer_code(r)==f){ds.row=(uint8_t)r;return 0;}
    }
    failures+=check("requested feature is reachable",0);return 0;
}
#define DS_PCM (FS*2u)
static int32_t pcm_off[DS_PCM],pcm_again[DS_PCM],pcm_on[DS_PCM];
/* two overlapping notes at two velocities, both released: glide, voice mode, velocity and release all sound */
static void designer_render(int32_t *pcm)
{
    track_t *t=TSEL;
    vage=0;t->engine=t->eng_req;t->xf=t->xf_on=0;memset(t->v, 0, NVOICE * sizeof *t->v);eng_state_clear(0);eng_state_prepare(t);
    trk_note_on(t,55,40);
    for(uint32_t frame=0;frame<DS_PCM;frame+=CTL){
        if(frame<=FS/3&&frame+CTL>FS/3)trk_note_on(t,67,110);
        if(frame<=FS/2&&frame+CTL>FS/2)trk_note_off(t,55);
        if(frame<=FS&&frame+CTL>FS)trk_note_off(t,67);
        int32_t out[CTL]={0};track_render(t,out,CTL);
        for(uint32_t i=0;i<CTL&&frame+i<DS_PCM;i++)pcm[frame+i]=out[i];
    }
    for(uint32_t i=0;i<NVOICE;i++)t->v[i].active=0;
}
static void feature_tests(void)
{
    static const uint8_t engines[]={ENGI_PROPHET,ENGI_FM6,ENGI_CZ,ENGI_SID};
    int first=1,states=1,audible=1,steady=1,drums=1;
    for(uint32_t ei=0;ei<NENG_SHOWN;ei++){
        eng_state_reset();ui_power_on();sound_init_of(TSEL,eng_vis(ei));designer_open();press(B_OCTUP);
        if(pads_on(TSEL) || midi_track_only(TSEL))drums&=ds.stage!=DT_BUILD;
        else first&=ds.stage==DT_BUILD && designer_feat_on() && ds.at==0;
        designer_close(1);
    }
    for(uint32_t e=0;e<NELEM(engines);e++){
        uint32_t feats;
        eng_state_reset();ui_power_on();sound_init_of(TSEL,engines[e]);designer_open();ds.current=0;press(B_OCTUP);
        TSEL->p[P_RESERVED33]=TSEL->p[P_RESERVED34]=TSEL->p[P_RESERVED35]=TSEL->p[P_RESERVED36]=0;
        feats=designer_feats();
        /* every operator on: a modulator is heard through its carrier */
        if(TSEL->eng_req==ENGI_FM6)for(uint32_t op=0;op<6u;op++){char w[4]={'O','P',(char)('1'+op),0};uint32_t k=designer_feature(df(w));turn(EN_K1+k,1);}
        designer_sound_t base;designer_snapshot(&base);
        for(uint32_t r=0;r<feats;r++){
            uint32_t f=designer_code(r),knob;
            designer_restore(&base);knob=designer_feature(f);   /* (one feature at a time) */
            if(f==DS_HOST+MOD3_HF_GLIDE)TSEL->p[P_VOICE]=V_MONO;            /* (poly voices start fresh: no slide) */
            for(uint32_t n=0;n<4;n++)turn(EN_K1+knob,-1);
            states&=designer_feat_get(f)==0;
            designer_render(pcm_off);designer_render(pcm_again);
            if(memcmp(pcm_off,pcm_again,sizeof pcm_off)){printf("unsteady render: %s %s\n",ENGINES[TSEL->eng_req]->name,dword(f));steady=0;}
            turn(EN_K1+knob,1);if(TSEL->eng_req==ENGI_CZ && f==df("Lines"))turn(EN_K1+knob,1);   /* (LINE2 alone may equal LINE1: 1+2') */
            states&=designer_feat_get(f)!=0;
            designer_render(pcm_on);
            /* INIT's two CZ lines are equal (1+2' sounds once line 2 is shaped: its controls are live);
             * SPREAD places voices after the track, in the stereo mix */
            if(TSEL->eng_req==ENGI_CZ && f==df("Lines"))audible&=cz_ed_active(cz_patch_of(0)->raw,LCZ_LBASE(1)+LCZ_W1) && cz_ed_active(cz_patch_of(0)->raw,LCZ_MOD);
            else if(f==DS_HOST+MOD3_HF_SPREAD)audible&=TSEL->p[P_SPRD]>0 && trk_vmode(TSEL)==V_POLY;
            else if(!memcmp(pcm_off,pcm_on,sizeof pcm_on)){printf("inaudible feature: %s %s\n",ENGINES[TSEL->eng_req]->name,dword(f));audible=0;}
            if(TSEL->eng_req==ENGI_CZ)states&=cz_patch_valid(cz_patch_of(0)->raw);
        }
        designer_close(1);
    }
    failures+=check("FEATURES is the first stage on every melodic engine",first);
    failures+=check("drum kits have no FEATURES stage",drums);
    failures+=check("every feature switches off and on through its knob",states);
    failures+=check("renders repeat exactly (the difference below is the feature's)",steady);
    failures+=check("every feature changes the audio of the real DSP when switched on",audible);
    /* The building blocks the request named: Prophet pulse, Poly-Mod and unison */
    eng_state_reset();ui_power_on();sound_init_of(TSEL,ENGI_PROPHET);designer_open();ds.current=0;press(B_OCTUP);
    uint8_t *p=p5_patch_of(TSEL)->raw;
    turn(EN_K1+designer_feature(df("Pulse A")),1);turn(EN_K1+designer_feature(df("Poly-Mod")),1);turn(EN_K1+designer_feature(df("Unison")),1);
    designer_at("P5 OSC A",0);ds.focus=3;int width=designer_need()==DS_NONE;
    designer_at("P5 POLYMOD",0);ds.focus=0;int poly=designer_need()==DS_NONE;
    designer_at("P5 UNISON",0);ds.focus=1;int uni=designer_need()==DS_NONE && TSEL->p[P_VOICE]==V_UNISON && p5_cap(TSEL)>1;
    failures+=check("Prophet FEATURES make WIDTH A, Poly-Mod and unison controls live",p[P5_PULSE_A] && width && poly && uni);
    designer_feature(df("Poly-Mod"));turn(EN_K1+designer_feature(df("Poly-Mod")),-1);
    failures+=check("Poly-Mod OFF opens its destinations and keeps its amounts",!p[P5_POLY_FREQ] && !p[P5_POLY_PW] && !p[P5_POLY_FILTER] && p[P5_POLY_ENV]);
    designer_finish_row(2);
    failures+=check("discard after FEATURES restores the original sound",!p[P5_UNISON] && !p[P5_PULSE_A] && TSEL->p[P_VOICE]!=V_UNISON);
    /* FM: the knob switches the operator it names; a build without carriers says so */
    ui_power_on();sound_init_of(TSEL,ENGI_FM6);designer_open();press(B_OCTUP);
    uint32_t k=designer_feature(df("OP3"));turn(EN_K1+k,-1);
    failures+=check("FM OP3's knob switches OP3 (the patch keeps OP6 first)",!(fm6_on_of(0)&(1u<<3)) && (fm6_on_of(0)&(1u<<2)));
    for(uint32_t op=0;op<6u;op++)turn(EN_K1+designer_feature(op),-1);   /* (OP1 .. OP6: its first rows) */
    failures+=check("FM with every carrier off is reported silent",designer_silent()!=0);
    turn(EN_K1+designer_feature(df("OP1")),1);
    failures+=check("one carrier on clears the warning",designer_silent()==0);
    designer_close(1);
}
/* Sounds are auditioned on the keys only: nothing plays by itself */
static void audition_tests(void)
{
    int quiet=1,keys=1;
    for(uint32_t e=0;e<3u;e++){
        static const uint8_t engines[]={ENGI_PROPHET,ENGI_FM6,ENGI_CZ};
        eng_state_reset();ui_power_on();sound_init_of(TSEL,engines[e]);designer_open();press(B_OCTUP);
        turn(EN_K1+designer_feature(df(engines[e]==ENGI_FM6?"OP2":engines[e]==ENGI_CZ?"Ring":"Pulse A")),1);
        press(B_OCTUP);turn(EN_SELECT,1);turn(EN_K1,1);press(B_SAVE);press(B_SAVE);
        quiet&=!song.playing && !transport_req && !seq_counting();
        for(uint32_t i=0;i<NVOICE;i++)quiet&=!TSEL->v[i].active;
        TSEL->engine=TSEL->eng_req;eng_state_prepare(TSEL);
        key_down(5);frame();
        int64_t energy=0;int32_t out[CTL];
        for(uint32_t b=0;b<200u;b++){memset(out,0,sizeof out);track_render(TSEL,out,CTL);for(uint32_t n=0;n<CTL;n++)energy+=llabs(out[n]);}
        keys&=energy>0 && kb_note[5]!=KB_SILENT;key_up(5);frame();
        designer_close(1);
    }
    failures+=check("designing never starts playback or a note by itself",quiet);
    failures+=check("a key plays the designed sound while designing",keys);
}
static void route_recipe_tests(void)
{
    int coverage=1,audible=1,valid=1,clean=1;
    for(uint32_t ei=0;ei<NENG_SHOWN;ei++)for(uint32_t goal=0;goal<DG_COUNT;goal++){
        eng_state_reset();ui_power_on();sound_init_of(TSEL,eng_vis(ei));
        designer_open();ds.current=0;ds.goal=(uint8_t)goal;ds.expert=1;press(B_OCTUP);
        for(uint32_t p=0;p<NPAGES;p++)if(designer_page_ok(p)){
            for(uint32_t op=0;op<(sec_inst(p)?6u:1u);op++){
                int found=0;for(uint32_t i=0;i<ds.n;i++)found|=ds.route[i].page==p && ds.route[i].op==op && ds.route[i].stage!=DT_BUILD;
                if(designer_live(p) && (!sec_inst(p) || designer_inst_on(op)))coverage &= found;
                else coverage &= !found;            /* (what FEATURES switched off is not shaped) */
            }
        }
        clean &= !song.grid && !kb_mask ;
        if(TSEL->eng_req==ENGI_CZ)valid &= cz_patch_valid(cz_patch_of(0)->raw);
        TSEL->engine=TSEL->eng_req;memset(TSEL->v,0,sizeof TSEL->v);eng_state_clear(0);eng_state_prepare(TSEL);
        trk_note_on(TSEL,pads_on(TSEL)?36:60,100);int64_t energy=0;int32_t out[CTL];
        for(uint32_t k=0;k<1000;k++){track_render(TSEL,out,CTL);for(uint32_t n=0;n<CTL;n++)energy+=llabs(out[n]);}
        if(TSEL->eng_req==ENGI_MIDI)valid &= !energy && !gates();
        else if(!energy && !(pads_flag(TSEL,MOD3_PD_RECORD) && pads_rec(TSEL)>=0 && !rec_tab[pads_rec(TSEL)].n)){   /* (SAMPLER's
            * kit with no take recorded here: silent, as the device's) */
            printf("silent recipe: %s %s\n",ENGINES[TSEL->eng_req]->name,DS_GOAL[goal]);audible=0;}
        trk_note_off(TSEL,pads_on(TSEL)?36:60);
        designer_close(1);
    }
    failures+=check("ALL pages: every eligible page, each FM operator switched on, nothing switched off",coverage);
    failures+=check("every engine and goal recipe produces audio through the real DSP (SAMPLER's kits: once recorded)",audible);
    failures+=check("all CZ goal recipes retain valid native envelope records",valid);
    failures+=check("wizard keys stay playable and do not become layer/grid shortcuts",clean);
}
static void save_tests(void)
{
    ui_power_on();sound_init_of(TSEL,ENGI_PROPHET);designer_open();press(B_OCTUP);
    turn(EN_K1+designer_feature(df("Unison")),1);
    designer_finish_row(1);
    failures+=check("Save as user.. keeps the design and opens USER, as SAVE does",
                    !designer_on() && cur_page()->graph==GR_USER && p5_patch_of(TSEL)->raw[P5_UNISON] && !motion_design_pause);
    press(B_OCTUP);if(pop.on)press(B_OCTUP);if(ui.confirm)press(B_OCTUP);
    failures+=check("its slot is named with the existing NAME screen",name_on() && nm.kind==NK_USER_SAVE);
    str_cpy(nm.s,"MY NEW TONE",sizeof nm.s);nm.len=(uint8_t)str_len(nm.s);nm.cur=nm.len;press(B_OCTUP);
    failures+=check("the designed tone is saved to the native bank with its name",
                    native_used(ENGI_PROPHET,nm.slot) && !memcmp(native_raw(ENGI_PROPHET,nm.slot)+P5_NAME,"MY NEW TONE",11) &&
                    native_raw(ENGI_PROPHET,nm.slot)[P5_UNISON]);
    designer_open();press(B_OCTUP);turn(EN_K1+designer_feature(df("Noise")),1);designer_finish_row(0);
    failures+=check("Keep the sound leaves with the draft, nothing restored",!designer_on() && p5_patch_of(TSEL)->raw[P5_NOISE]);
    designer_open();press(B_OCTUP);press(B_OCTDN);
    failures+=check("OCT- before the first stage asks with the designer's sheet",designer_on() && pop.on==POP_SHEET);
    press(B_OCTDN);
    failures+=check("OCT- closes the sheet and the design goes on",designer_on() && !pop.on && ui.designer_mode==DS_SHAPE);
    uint32_t at=ds.at;turn(EN_SELECT,1);
    failures+=check("SELECT turns the pages",ds.at==at+1u && !designer_feat_on());
    uint32_t st=ds.stage;turn(EN_PRESET,1);
    failures+=check("PRESETS jumps to the next stage's first page",ds.stage>st && (ds.at==0 || ds.route[ds.at-1u].stage!=ds.stage));
    project_save(0);
    TSEL->p[P_RESERVED35]=7;project_load(0);
    failures+=check("external project load ends design without restoring over the new project",!designer_on() && !motion_design_pause);
    ui_power_on();ui.menu=1;ui.menu_sel=MI_DESIGNER;menu_input(2);
    failures+=check("designer is reachable from device menu",designer_on() && !ui.menu);
    press(B_OCTUP);ui.menu=1;ui.menu_sel=MI_DESIGNER;menu_input(2);
    failures+=check("the menu's SOUND DESIGNER over an open design goes back to it",designer_on() && !ui.menu && ui.designer_mode==DS_SHAPE);
    designer_close(1);
}
/* A synth with a designer block and no recipe records: its envelope's roles take the goal's shape over each parameter's
 * range (an inverted role turned over), the part's own ADSR left; a synth with neither: the part's ADSR. The
 * Prophet's records hidden here (its ENV_A .. R roles: 44, 46, 48, 50) */
static void role_recipe_tests(void)
{
    uint32_t len,i;uint8_t *h=(uint8_t *)(uintptr_t)sch_builtin(ENGI_PROPHET,&len),nrec[2],*fl=0;
    mod_sch_head hd;int16_t atk;
    memcpy(&hd,h,sizeof hd);memcpy(nrec,h+10,2);h[10]=h[11]=0;
    for(i=0;i<hd.nrole;i++)if(h[hd.role+i*MOD_SCH_RREC]==MOD3_R_ENV_A)fl=h+hd.role+i*MOD_SCH_RREC+1;
    ui_power_on();sound_init_of(TSEL,ENGI_PROPHET);atk=TSEL->p[P_ATK];
    designer_open();ds.current=0;ds.goal=DG_PAD;press(B_OCTUP);
    const uint8_t *q=patch_of(0);
    failures+=check("no recipe records: PAD on the synth's envelope roles, the part's ADSR left",
                    q[44]==75 && q[46]==45 && q[48]==110 && q[50]==85 && TSEL->p[P_ATK]==atk && TSEL->p[P_VOICE]==V_POLY);
    designer_close(1);
    if(fl)*fl|=MOD3_RF_INVERT;
    designer_open();ds.current=0;ds.goal=DG_FX;press(B_OCTUP);
    failures+=check("an inverted role turned over; FX's part LFO as ever",fl && q[44]==127 && q[46]==45 && q[48]==110 &&
                    q[50]==20 && TSEL->p[P_LRATE]==65 && TSEL->p[P_LD_PIT]==25 && TSEL->p[P_LD_SHP]==40);
    designer_close(1);
    if(fl)*fl&=(uint8_t)~MOD3_RF_INVERT;
    memcpy(h+10,nrec,2);
    ui_power_on();sound_init_of(TSEL,ENGI_SID);
    designer_open();ds.current=0;ds.goal=DG_PAD;press(B_OCTUP);
    failures+=check("no block, no roles: the part's ADSR",TSEL->p[P_ATK]==75 && TSEL->p[P_DEC]==45 && TSEL->p[P_SUS]==110 &&
                    TSEL->p[P_REL]==85);
    designer_close(1);
}
/* The designer's data as the writer takes it: a program or a recipe naming a parameter the synth does not have is
 * refused */
static void block_tests(void)
{
    static uint8_t out[12288];
    static const uint8_t BAD_ID[]={D_NZ(200),MOD3_D_END};
    mod_sch_src s=*sch_builtin_src(ENGI_PROPHET);mod_sch_design_src d=*s.design;mod_sch_feat_src f[16];
    mod_sch_rec_src rec[1]={{MOD3_Q_GOAL,MOD3_GM_ALL,200,1}};
    int ok=mod_sch_write(&s,out,sizeof out)!=0;
    memcpy(f,d.feat,d.nfeat*sizeof *f);d.feat=f;s.design=&d;
    f[0].get=BAD_ID;ok&=!mod_sch_write(&s,out,sizeof out);
    s=*sch_builtin_src(ENGI_PROPHET);s.rec=rec;s.nrec=1;ok&=!mod_sch_write(&s,out,sizeof out);
    failures+=check("the writer refuses designer programs and recipes naming what the synth has not",ok);
}
static void motion_tests(void)
{
    ui_power_on();motion_design_pause=0;
    motion.count=1;motion.on=1;motion.event[0]=(motion_event_t){.place=0,.param=P_E4,.value=17};motion_pattern[0]=TSEL->pattern;
    motion_event_t event=motion.event[0];
    designer_open();press(B_OCTUP);TSEL->p[P_E4]=92;motion_step(TSEL,0,&motion);
    failures+=check("existing automation cannot overwrite a wizard edit",TSEL->p[P_E4]==92 && motion.on==1 && !memcmp(&motion.event[0],&event,sizeof event));
    designer_close(0);motion_step(TSEL,0,&motion);
    failures+=check("leaving resumes unchanged recorded automation",!motion_design_pause && TSEL->p[P_E4]==17 && motion.on==1 && motion.count==1);
    motion_end();motion.count=motion.on=0;
}
int main(void)
{
    transaction_tests();dependency_tests();feature_tests();audition_tests();route_recipe_tests();save_tests();motion_tests();role_recipe_tests();block_tests();
    printf("sound designer: %d failure(s)\n",failures);return failures?1:0;
}
