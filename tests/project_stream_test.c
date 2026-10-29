/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Exercise the actual FLASH path, with the complete RAM codec as its oracle. */
#define BACKUP_TEST_NO_MAIN 1
#include "backup_test.c"
#include <assert.h>
static uint8_t oracle[BANK_STORE_SIZE],actual[BANK_STORE_SIZE],old_flash[sizeof nor];
/* a stored copy's record as the loader reads it: a compact HPJ1 image expanded (its holes zeros), or a raw one */
static int ps_image(uint32_t obj,uint32_t copy,uint8_t *dst)
{
    uint8_t on=pj.on;pj_head_t h=pj.h;
    pj.on=!st_read_range(obj,copy,0,&pj.h,PJ_HEAD)&&pj.h.magic==PJ_MAGIC;
    int rc=pj_read(obj,copy,0,dst,BANK_STORE_SIZE);
    pj.on=on;pj.h=h;return rc;
}
/* as the UI does: SLOT on the slot, then SAVE or LOAD (the record keeps SLOT; LOAD leaves it as it is) */
static int save_slot(uint32_t s,const char *name){song.g[G_SLOT]=(int16_t)(s+1u);return project_save_as(s,name);}
static void open_slot(uint32_t s){song.g[G_SLOT]=(int16_t)(s+1u);project_load(s);}
static int ps_saved(uint32_t obj,uint8_t *dst)       /* the current copy's: 0 */
{
    st_hdr_t h;int copy=st_current(obj,&h);
    return copy<0?-1:ps_image(obj,(uint32_t)copy,dst);
}
static void midi_tracks_stream(void)
{
    reset(); song.ntrk=NTRK; song.sel=NTRK-1u;
    uint32_t owners[NTRK*NPAT],n=0;                    /* every bank's: the parts' 16, the MIDI-only tracks' 8 */
    for(uint32_t k=0;k<NTRK;k++)for(uint32_t b=0;b<trk_npat(k);b++)owners[n++]=k*NPAT+b;
    for(uint32_t k=0;k<NTRK;k++){
        trk[k].p[P_ROOT]=9; trk[k].p[P_SCALE]=2;
        if(k>=NPART)trk[k].p[P_E0]=(int16_t)(16u-k);
        for(uint32_t b=0;b<trk_npat(k);b++){
            pattern_t p; pattern_read(k,b,&p);
            p.step[63]=(step_t){{(uint8_t)(40u+k+b)},1,ST_NOTE,0,99};
            p.timing[0]=64;
            assert(pattern_write(k,b,&p));
        }
        pattern_switch(&trk[k],k%NPAT);
    }
    for(uint32_t i=0;i<RECORD_MAX;i++){
        recorded_note_t r={.on=(uint16_t)(i*61u),.duration=123,.note7=60,.vel=100};
        recorded_set_owner(&r,owners[i%n]); recorded_set_step(&r,63,0,0);
        recording[i]=r;
    }
    recording_reindex(); chain_config.count=2; chain_patterns[1][NTRK-1u]=7;
    project_capture(&proj_scratch); assert(bank_pack(oracle,&proj_scratch,1));
    assert(bank_wide);
    uint32_t hash; project_capture(&proj_scratch);
    assert(proj_image_hash(1,&hash)&&hash==proj_hash(oracle,sizeof oracle));
    assert(!save_slot(0,0)); assert(!project_dirty());
    assert(!ps_saved(OBJ_BANK0,actual)&&!memcmp(actual,oracle,sizeof actual));
    project_new_blank(); open_slot(0);
    assert(trk_n()==NTRK&&song.sel==NTRK-1u&&chain_patterns[1][NTRK-1u]==7);
    uint32_t counts[NTRK*NPAT]={0};
    for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel)counts[recorded_owner(&recording[i])]++;
    for(uint32_t j=0;j<n;j++)assert(counts[owners[j]]==RECORD_MAX/n+(j<RECORD_MAX%n));
    for(uint32_t k=0;k<NTRK;k++){
        assert(trk[k].p[P_ROOT]==9&&trk[k].p[P_SCALE]==2);
        if(k>=NPART)assert(trk[k].eng_req==ENGI_MIDI&&trk[k].p[P_E0]==(int32_t)(16u-k));
        for(uint32_t b=0;b<trk_npat(k);b++){
            pattern_t p; pattern_read(k,b,&p);
            assert(p.step[63].note[0]==40u+k+b&&p.step[63].n==1&&p.timing[0]==64);
        }
    }
    assert(!project_dirty());
    template_save(); project_new_blank(); template_load();
    assert(trk_n()==NTRK&&song.sel==NTRK-1u);
    for(uint32_t k=0;k<NTRK;k++)assert(trk[k].p[P_ROOT]==9&&trk[k].p[P_SCALE]==2);
    open_slot(0);
    uint32_t before=erases; assert(!track_remove(NPART)); assert(erases==before&&trk_n()==NTRK-1u);
    assert(trk[NPART].p[P_E0]==(int32_t)(16u-NPART-1u));
    assert(chain_patterns[1][NTRK-2u]==7);
    assert(!save_slot(1,"MIDI SEVEN")); project_new_blank(); open_slot(1);
    assert(trk_n()==NTRK-1u&&trk[NPART].p[P_E0]==(int32_t)(16u-NPART-1u));
    /* A malformed extra recording directory must never publish a replacement. */
    uint32_t dir=0; wire_bits_put(oracle+BANKP_REC_OFF,&dir,RECORD_MAX+1u,11);
    bank_checksum(oracle);
    assert(!bank_valid(oracle,sizeof oracle));
    puts("Project stream: eight-track FBKP oracle, all 96 recording owners, save/load and MIDI removal passed");
}

static void extended_songs_stream(int punches)
{
    reset(); song.ntrk = NTRK;
    for (uint32_t k = NPART; k < NTRK; k++) trk[k].p[P_E0] = (int16_t)(k + 1u);
    chain_config.count = CHAIN_ROWS;
    for (uint32_t r = 0; r < CHAIN_ROWS; r++) {
        chain_config.row[r] = (chain_row_t){r % NPAT, (r % 16u) + 1u};
        for (uint32_t k = 0; k < NTRK; k++) {               /* every bank of the track and a rest */
            uint32_t v = (r + k) % (trk_npat(k) + 1u);
            chain_patterns[r][k] = (uint8_t)(v == trk_npat(k) ? CHAIN_REST : v);
        }
        chain_config.row[r].slot = chain_patterns[r][0];
    }
    chain_config.rsv[CHAIN_LOOP] = 1;
    if (punches) {
        chain_config.fx[0][0] = (chain_fx_t){PX_REV + 1, 3, 8};
        chain_config.fx[63][3] = (chain_fx_t){PX_FREEZE + 1, 63, 1};
    }
    chain_patterns[1][1] = CHAIN_HOLD; chain_patterns[63][2] = CHAIN_HOLD;
    chain_config_t expected = chain_config;
    uint8_t patterns[CHAIN_ROWS][NTRK]; memcpy(patterns, chain_patterns, sizeof patterns);
    project_capture(&proj_scratch); assert(bank_pack(oracle, &proj_scratch, 1));
    assert(proj_image_capture(1)); st_stream_t w;
    assert(!proj_stream_generate(&w, OBJ_BANK0)); assert(!st_stream_publish(&w));
    assert(!ps_saved(OBJ_BANK0, actual));
    assert(!memcmp(oracle, actual, sizeof oracle));
    project_new_blank(); open_slot(0);
    assert(!memcmp(&expected, &chain_config, sizeof expected));
    assert(!memcmp(patterns, chain_patterns, sizeof patterns));
    assert(!project_dirty());
    assert(!project_rename(0, "LONG SONG"));
    project_new_blank(); open_slot(0);
    assert(!memcmp(patterns, chain_patterns, sizeof patterns));
    assert(!memcmp(expected.fx, chain_config.fx, sizeof expected.fx));
    assert(!track_remove(2));
    for (uint32_t r = 0; r < CHAIN_ROWS; r++) for (uint32_t k = 2; k < NTRK - 1u; k++)
        assert(chain_patterns[r][k] == patterns[r][k + 1u]);
    assert(!memcmp(expected.fx, chain_config.fx, sizeof expected.fx));
    if (punches) {
        assert(!save_slot(1, "PUNCH SONG")); project_new_blank(); open_slot(1);
        assert(!memcmp(expected.fx, chain_config.fx, sizeof expected.fx));
        assert(!project_dirty());
        song_punch.row = 63; song_punch.event = 3; song_punch_edit(0, 1);
        assert(project_dirty());
    }
    puts(punches ? "Project stream: FBKP punch songs, exact codec, dirty hashes, rename and track removal passed" :
        "Project stream: FBKP 64-section songs, eight-track rests, dirty hashes, rename and track removal passed");
}
static void song_sounds_stream(int patterns)
{
    reset(); chain_config.count = 3;
    engine_load_of(&trk[0], ENGI_PROPHET, 1);
    for (uint32_t k = NPART; k < NTRK; k++) trk[k].p[P_E0] = (int16_t)(k + 1u);
    song_sound_t sound;
    song_sound_capture(&sound, &trk[0], "LOCAL SONG");
    assert(song_sound_valid(&sound));
    uint32_t id = song_sound_store(&sound); assert(id);
    chain_config.sound[1][0] = chain_config.sound[2][1] = (uint8_t)id;
    if (patterns) pattern_sound[1][15] = (uint8_t)id;
    project_capture(&proj_scratch); assert(bank_pack(oracle, &proj_scratch, 1));
    assert(*(uint32_t *)oracle == (patterns ? BANKQ_MAGIC : BANKP_MAGIC));
    assert(proj_image_capture(1)); st_stream_t w;
    assert(!proj_stream_generate(&w, OBJ_BANK0)); assert(!st_stream_publish(&w));
    assert(!ps_saved(OBJ_BANK0, actual) && !memcmp(oracle, actual, sizeof actual));
    project_new_blank(); open_slot(0);
    assert(chain_config.sound[1][0] && chain_config.sound[2][1]);
    if (patterns) assert(pattern_sound[1][15] && !pattern_sound[0][15]);
    assert(!memcmp(song_sound_get(chain_config.sound[1][0]), &sound, sizeof sound));
    assert(!project_dirty()); assert(!project_rename(0, "SOUND SONG"));
    assert(!track_remove(0));
    assert(!chain_config.sound[1][0] && chain_config.sound[2][0]);
    if (patterns) assert(pattern_sound[0][15] && !pattern_sound[1][15]);
    assert(!memcmp(song_sound_get(chain_config.sound[2][0]), &sound, sizeof sound));
    assert(!save_slot(1, "MOVED SOUND")); project_new_blank(); open_slot(1);
    assert(chain_config.sound[2][0] && !project_dirty());
    if (patterns) { assert(pattern_sound[0][15]); pattern_sound[0][14] = pattern_sound[0][15]; assert(project_dirty()); }
    assert(!put_all(2, oracle, sizeof oracle, st_crc32(oracle, sizeof oracle)));
    open_slot(0);
    assert(chain_config.sound[1][0] && chain_config.sound[2][1]);
    if (patterns) assert(pattern_sound[1][15] && !pattern_sound[0][15]);
    assert(!memcmp(song_sound_get(chain_config.sound[1][0]), &sound, sizeof sound));
    puts("Project stream: FBKP sound copies, exact codec, dirty hash, rename and track removal passed");
}
static void workload(uint32_t count,uint32_t spread,uint32_t tail,uint32_t quant,uint32_t engine)
{
    reset();song.ntrk=NPART;
    const uint8_t eng[]={19,12,15,3,10,10,0};
    for(uint32_t k=0;k<NPART;k++){
        track_t *t=&trk[k];set_engine_of(t,eng[(engine+k)%NELEM(eng)]);apply_preset_to(t,0);
        t->p[P_VOICE]=V_POLY;t->p[P_RECQ]=quant?3:0;
        for(uint32_t b=0;b<NPAT;b++){
            pattern_request(t,b);t->p[P_SLEN]=16+b;t->p[P_SDIV]=2;
            t->step[3]=(step_t){{40+k*8+b,60,64,67},4,ST_NOTE,SF_ACCENT,100,0x81,0x80,20+b};
        }
    }
    for(uint32_t i=0;i<count;i++){
        recorded_note_t r={.on=quant?0:(uint16_t)(8192u+i*1777u),.duration=(uint16_t)(14000u+i*109u),.note7=48+i%40,.vel=70+i%58,.step=3,.pitch=(int8_t)(i%49u)-24,.length=i&1u};
        r.owner=(spread?i%32u:7u)|(tail?3u<<5:0u);recording[i]=r;assert(recording_valid(&r));
    }
    recording_reindex();chain_config.count=1;chain_config.row[0]=(chain_row_t){0,2};
    for(uint32_t k=0;k<NPART;k++)chain_patterns[0][k]=k+1;
}
int main(void)
{
    uint32_t cases=0;const uint32_t counts[]={64,256,RECORD_MAX};
    for(uint32_t e=0;e<7;e++)for(uint32_t c=0;c<3;c++)for(uint32_t q=0;q<2;q++)for(uint32_t s=0;s<2;s++)for(uint32_t t=0;t<2;t++){
        workload(counts[c],s,t,q,e);project_capture(&proj_scratch);
        assert(bank_pack(oracle,&proj_scratch,1));
        uint32_t hash;project_capture(&proj_scratch);assert(proj_image_hash(1,&hash)&&hash==proj_hash(oracle,sizeof oracle));
        assert(proj_image_capture(1));st_stream_t w;
        assert(!proj_stream_generate(&w,OBJ_BANK0));
        assert(st_current(OBJ_BANK0,0)<0);
        assert(!ps_image(w.obj,w.head.slot,actual)&&!memcmp(actual,oracle,sizeof actual));
        assert(!proj_publish_slot(0,&w));proj_cur=0;assert(!project_dirty());
        assert(proj_read_slot(0)==BANK_STORE_SIZE);assert(proj_source_hash==hash);
        open_slot(0);project_capture(&proj_scratch);assert(bank_pack(actual,&proj_scratch,1));
        assert(proj_image_hash(1,&hash)&&hash==proj_hash(actual,sizeof actual));
        assert(project_dirty()==(hash!=proj_saved_hash[0]));
        /* A rename alters only the active record and container checksum. */
        assert(!project_rename(0,"RENAMED"));
        assert(!ps_saved(OBJ_BANK0,actual));
        assert(!memcmp(actual+BANK_ACTIVE_OFF,oracle+BANK_ACTIVE_OFF,BANK_TAIL_SIZE-4u));
        cases++;
    }
    /* Generated write failure must preserve all four previously saved slots. */
    workload(256,1,1,0,0);for(uint32_t k=0;k<4;k++)assert(!project_save(k));
    memcpy(old_flash,nor,sizeof nor);memcpy(oracle,proj_bank_slot[0],sizeof oracle);
    erase_error=1;assert(save_slot(0,"FAILED"));erase_error=0;
    assert(!ps_saved(OBJ_BANK0,actual)&&!memcmp(actual,oracle,sizeof actual));
    assert(!memcmp(nor,old_flash,sizeof nor));
    assert(put_all(2,oracle,sizeof oracle,st_crc32(oracle,sizeof oracle)^1u)==2u);
    assert(!ps_saved(OBJ_BANK0,actual)&&!memcmp(actual,oracle,sizeof actual));
    /* Track removal takes the same snapshot/restore route without using flash. */
    uint32_t erases_before=erases;assert(!track_remove(1));assert(erases==erases_before&&trk_n()==3);
    for(uint32_t k=0;k<NPART;k++)for(uint32_t b=0;b<NPAT;b++){
        pattern_t pat;pattern_read(k,b,&pat);
        assert(k==NPART-1u?pat.step[3].time==ST_REST:pat.step[3].note[0]==40u+(k>=1u?k+1u:k)*8u+b);
    }
    for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel)assert(recorded_owner(&recording[i])<3u*NPAT);
    assert(proj_image_capture(1));assert(proj_ok(&proj_scratch));
    /* Earlier raw snapshots use ten-byte notes, not the FUN wire codec. */
    static uint8_t legacy[sizeof(project_t)+RECORD_MAX];
    workload(RECORD_MAX,1,1,0,0);project_capture(&proj_scratch);
    static project_t expected;expected=proj_scratch;
    uint32_t at=__builtin_offsetof(project_t,recording),end=__builtin_offsetof(project_t,pattern);
    memcpy(legacy,&expected,at);
    for(uint32_t i=0;i<RECORD_MAX;i++){
        const recorded_note_t *r=&expected.recording[i];uint8_t *v=legacy+at+i*10u;
        v[0]=r->on;v[1]=r->on>>8;v[2]=r->duration;v[3]=r->duration>>8;
        v[4]=recorded_note(r);v[5]=r->vel;v[6]=r->owner;v[7]=r->step;v[8]=r->pitch;v[9]=recorded_legacy_length(r);
    }
    memcpy(legacy+at+RECORD_MAX*10u,(const uint8_t *)&expected+end,sizeof expected-end);
    uint32_t len=sizeof legacy,sum;memcpy(legacy+4,&len,4);sum=proj_hash(legacy,len-4u);memcpy(legacy+len-4u,&sum,4);
    assert(!st_save(OBJ_BANK0+3u,legacy,len));assert(proj_read_slot(3)==len);
    assert(!memcmp(&expected,&proj_scratch,sizeof expected));
    legacy[at+8u]=25;sum=proj_hash(legacy,len-4u);memcpy(legacy+len-4u,&sum,4);
    assert(!st_save(OBJ_BANK0+3u,legacy,len));assert(!proj_read_slot(3)&&proj_meta[3].old);
    midi_tracks_stream();
    extended_songs_stream(0);
    extended_songs_stream(1);
    song_sounds_stream(0); song_sounds_stream(1);
    printf("Project stream: %u complete RAM-codec comparisons, hash/dirty/save/load/rename, failed save and track removal passed\n",cases);
    return 0;
}
