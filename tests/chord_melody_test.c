/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#include <assert.h>
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
static track_t *setup(void)
{
    ui_power_on(); track_t *t=TSEL; set_engine_of(t,HOST_SYNTH);
    t->p[P_VOICE]=V_POLY; t->p[P_SCALE]=1; t->p[P_ROOT]=t->p[P_TRANS]=0;
    t->p[P_CHRD]=CH_MELODY; t->p[P_QUANT]=Q_OFF; t->p[P_VOIC]=VC_CLOSE;
    events_block(CTL); return t;
}
static void expect(track_t *t,uint32_t key,const uint8_t *want,uint32_t count,const char *label)
{
    uint8_t out[4];int32_t root;uint16_t mask;char name[12];
    uint32_t n=chord_make(t,key,out,&root,&mask);
    chord_melody_name(name,(uint32_t)root,mask);
    if(n!=count || memcmp(out,want,count) || strcmp(name,label)) {
        printf("AUTO2 key %u: %s (wanted %s)",key,name,label);for(uint32_t i=0;i<n;i++)printf(" %u",out[i]);puts("");assert(0);
    }
}
static void examples(void)
{
    track_t *t=setup();
    /* The melody C D E F G A B stays on top; the chord root may be different. */
    static const uint8_t key[]={60,62,64,65,67,69,71};
    static const uint8_t notes[][3]={{52,55,60},{53,57,62},{55,60,64},{57,60,65},{60,64,67},{60,64,69},{62,67,71}};
    static const char *names[]={"C","Dm","C","F","C","Am","G"};
    for(uint32_t i=0;i<7;i++)expect(t,key[i],notes[i],3,names[i]);
    t->p[P_SCALE]=2;t->p[P_ROOT]=7;
    expect(t,67,(uint8_t[]){58,62,67},3,"Gm");
    t->p[P_VOIC]=VC_OPEN;
    expect(t,67,(uint8_t[]){46,62,67},3,"Gm");
    t->p[P_VOIC]=VC_INV1;
    expect(t,67,(uint8_t[]){58,62,67},3,"Gm");
    t->p[P_VOIC]=VC_INV2;
    expect(t,67,(uint8_t[]){50,58,67},3,"Gm");
    t->p[P_VOIC]=VC_BASS;
    expect(t,67,(uint8_t[]){55,58,62,67},4,"Gm");
    t->p[P_VOIC]=VC_CLOSE;t->p[P_ROOT]=9;
    expect(t,69,(uint8_t[]){60,64,69},3,"Am");
    t->p[P_SCALE]=7;expect(t,68,(uint8_t[]){59,64,68},3,"E");
    t->p[P_SCALE]=3;t->p[P_ROOT]=2;expect(t,62,(uint8_t[]){53,57,62},3,"Dm");
    t->p[P_ROOT]=0;t->p[P_SCALE]=5;expect(t,62,(uint8_t[]){55,60,62},3,"Csus2");
    t->p[P_SCALE]=13;expect(t,60,(uint8_t[]){52,56,60},3,"Caug");
    t->p[P_SCALE]=12;expect(t,66,(uint8_t[]){58,63,66},3,"D#m");
    t->p[P_SCALE]=1;t->p[P_TRANS]=1;t->p[P_QUANT]=Q_ALL;
    expect(t,kb_map(t,7),(uint8_t[]){53,56,61},3,"C#");
    puts("AUTO2: melody-top examples, G minor, voicings, modes, pentatonic, whole tone, blues and transpose passed");
}
static void exhaustive(void)
{
    track_t *t=setup();const int trans[]={-24,-13,0,1,24};uint32_t cases=0;
    for(uint32_t scale=0;scale<SCALE_LEGACY;scale++)for(int tonic=0;tonic<12;tonic++)
      for(uint32_t x=0;x<NELEM(trans);x++)for(uint32_t v=0;v<=VC_BASS;v++) {
        t->p[P_SCALE]=scale;t->p[P_ROOT]=tonic;t->p[P_TRANS]=trans[x];t->p[P_VOIC]=v;
        uint32_t allowed=0;
        for(int i=0;i<12;i++)if(SCALE_MASK[scale]&(1u<<i))allowed|=1u<<((i+tonic+trans[x]+120)%12);
        for(uint32_t input=0;input<128;input++) {
            int melody=(int)input;
            while(melody>=0 && !(allowed&(1u<<(melody%12))))melody--;
            if(melody<0){melody=0;while(!(allowed&(1u<<(melody%12))))melody++;}
            uint8_t out[4],again[4];int32_t root,r2;uint16_t mask,m2;
            uint32_t n=chord_make(t,input,out,&root,&mask),n2=chord_make(t,input,again,&r2,&m2);
            assert(n>=1 && n<=4 && n==n2 && root==r2 && mask==m2 && !memcmp(out,again,n));
            assert(out[n-1]==melody);
            if(melody>=36)assert(n>=3);
            for(uint32_t i=0;i<n;i++) {
                assert(out[i]<=127 && (allowed&(1u<<(out[i]%12))));
                if(i)assert(out[i]>out[i-1]);
            }
            cases++;
        }
      }
    printf("AUTO2: %u cases; all Western scales/roots/transpose/voicings/MIDI inputs, exact melody on top, strictly in scale, stable and ordered\n",cases);
}
static void independence(void)
{
    track_t *t=setup();t->p[P_ROOT]=7;t->p[P_SCALE]=2;t->p[P_VOIC]=VC_OPEN;
    song.ntrk=3;
    for(uint32_t k=1;k<3;k++) {
        set_engine_of(&trk[k],HOST_SYNTH);trk[k].p[P_VOICE]=V_POLY;trk[k].p[P_SCALE]=2;trk[k].p[P_ROOT]=7;
        trk[k].p[P_CHRD]=k==1?CH_AUTO:CH_OFF;
    }
    events_block(CTL);
    uint8_t expected[4],actual[4],other[4];uint32_t n=chord_build(t,67,expected);
    for(uint32_t note=36;note<96;note++) {
        input_on(&trk[2],note,100);chord_build(&trk[1],note,other);
        assert(chord_build(t,67,actual)==n && !memcmp(actual,expected,n));
        chord_build(t,note,other); /* Different previous melody cannot change a repeated key's chord. */
        input_off(&trk[2],note);seq_advance(CTL);
        assert(chord_build(t,67,actual)==n && !memcmp(actual,expected,n));
    }
    seq_stop();assert(chord_build(t,67,actual)==n && !memcmp(actual,expected,n));
    puts("AUTO2: backing notes, original AUTO history, previous melody, timing and STOP cannot change a key's chord");
}
static void transposition(void)
{
    track_t *t=setup();
    for(uint32_t scale=0;scale<SCALE_LEGACY;scale++)for(uint32_t v=0;v<=VC_BASS;v++)
      for(uint32_t key=60;key<72;key++)for(uint32_t shift=0;shift<12;shift++) {
        uint8_t base[4],shifted[4];int32_t r,r2;uint16_t mask,m2;
        t->p[P_SCALE]=scale;t->p[P_VOIC]=v;t->p[P_ROOT]=t->p[P_TRANS]=0;
        uint32_t n=chord_make(t,key,base,&r,&mask);
        t->p[P_ROOT]=shift;
        uint32_t n2=chord_make(t,key+shift,shifted,&r2,&m2);
        assert(n==n2 && mask==m2 && (r+shift)%12==r2%12);
        for(uint32_t i=0;i<n;i++)assert(base[i]+shift==shifted[i]);
        t->p[P_ROOT]=0;t->p[P_TRANS]=shift;
        assert(chord_make(t,key+shift,shifted,&r2,&m2)==n);
        for(uint32_t i=0;i<n;i++)assert(base[i]+shift==shifted[i]);
    }
    puts("AUTO2: transposing key or TRN transposes the same harmony and melody in every Western scale/voicing");
}
static uint32_t melody_gates(const track_t *t)
{
    uint32_t n=0;for(uint32_t i=0;i<NVOICE;i++)n+=t->v[i].active && t->v[i].gate;return n;
}
static int gated(const track_t *t,uint32_t note)
{
    for(uint32_t i=0;i<NVOICE;i++)if(t->v[i].active && t->v[i].gate && t->v[i].note==note)return 1;return 0;
}
static void midi(uint32_t status,uint32_t note,uint32_t vel)
{
    midi_enqueue(status>>4|status<<8|note<<16|vel<<24,1);events_block(CTL);
}
static void lifecycle(void)
{
    track_t *t=setup();key_down(7);assert(melody_gates(t)==3 && gated(t,60));
    uint8_t frozen[4];memcpy(frozen,kb_chord[7],4);
    t->p[P_SCALE]=2;t->p[P_TRANS]=1;t->p[P_CHRD]=CH_AUTO;
    assert(!memcmp(frozen,kb_chord[7],4));key_up(7);assert(!melody_gates(t));
    for(uint32_t dir=0;dir<3;dir++)for(uint32_t rate=1;rate<NELEM(N_STRUM);rate++) {
        t=setup();t->p[P_STRUM]=rate;t->p[P_STROKE]=dir;
        key_down(7);assert(melody_gates(t)==1 && gated(t,60) && kb_chord[7][0]==60);
        t->p[P_CHRD]=CH_AUTO; /* Pending accompaniment keeps the captured order/melody. */
        seq_advance(beat_samples());assert(melody_gates(t)==3 && gated(t,52) && gated(t,55) && gated(t,60));
        key_up(7);assert(!melody_gates(t));
        t=setup();t->p[P_STRUM]=rate;t->p[P_STROKE]=dir;
        midi(0x90,60,100);assert(melody_gates(t)==1 && gated(t,60));
        midi(0x80,60,0);seq_advance(beat_samples());assert(!melody_gates(t) && !midi_strum_pending);
    }
    t=setup();midi(0x90,60,100);midi(0xB0,64,127);midi(0x80,60,0);assert(melody_gates(t)==3);
    t->p[P_CHRD]=CH_AUTO;midi(0xB0,64,0);assert(!melody_gates(t));
    t=setup();t->p[P_AMODE]=1;key_down(7);assert(t->nheld==3);key_up(7);assert(!t->nheld);
    t=setup();song.rec=song.playing=1;key_down(7);
    assert(t->step[0].n==3 && !memcmp(t->step[0].note,(uint8_t[]){52,55,60},3));key_up(7);
    static project_t before,after;static project_store_t wire;
    project_capture(&before);assert(proj_pack(&wire,&before) && proj_unpack(&after,wire.raw));
    assert(after.t[0].p[P_CHRD]==CH_MELODY && after.t[0].step[0].n==3);
    t=setup();t->p[P_VOICE]=V_MONO;key_down(7);assert(melody_gates(t)==1 && gated(t,60));key_up(7);assert(!melody_gates(t));
    t=setup();host_preset(t,ENGI_808,0);t->p[P_CHRD]=CH_MELODY;
    uint8_t out[4];assert(chord_build(t,36,out)==1 && out[0]==36);
    t=setup();t->p[P_SCALE]=31;t->p[P_QUANT]=Q_ALL;
    assert(chord_build(t,60,out)==1 && out[0]==60);
    puts("AUTO2: immediate melody in all strum rates/directions, frozen notes, MIDI/sustain, ARP, recording, persistence and bypasses passed");
}
int main(void)
{
    _Static_assert(CH_AUTO==10 && CH_MELODY==11,"AUTO stays compatible; AUTO2 is appended");
    examples();exhaustive();transposition();independence();lifecycle();puts("AUTO2 chord tests passed");return 0;
}
