/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Exercise the actual FLASH path, with the complete RAM codec as its oracle. */
#define BACKUP_TEST_NO_MAIN 1
#include "backup_test.c"
#include <assert.h>
static uint8_t oracle[BANK_STORE_SIZE],actual[BANK_STORE_SIZE],old_flash[sizeof nor];
/* a stored copy's record as the loader reads it: a packed or compact image expanded, or a raw one */
static int ps_image(uint32_t obj,uint32_t copy,uint8_t *dst){return pj_image(obj,copy,dst);}
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
    assert(song_sound_valid(&sound, 0));
    uint32_t id = song_sound_store(&sound); assert(id);
    chain_config.sound[1][0] = chain_config.sound[2][1] = (uint8_t)id;
    if (patterns) pattern_sound[1][15] = (uint8_t)id;
    project_capture(&proj_scratch); assert(bank_pack(oracle, &proj_scratch, 1));
    assert(*(uint32_t *)oracle == BANKS_MAGIC);
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
/* ---- the packed images (project_stream.c HPJ2) ---- */
static uint8_t img[BANK_STORE_SIZE*2u],blk[BANK_TAIL_SIZE+64u];
/* n bytes as a packed block a careful writer could have made: runs of one byte as copies from 1 back, the rest as they
 * are; its stored bytes */
static uint32_t test_pack(uint8_t *o,const uint8_t *s,uint32_t n)
{
    uint32_t k=0,i=0;
    while(i<n){
        uint32_t run=0,lit=0;
        while(i&&run<130u&&i+run<n&&s[i+run]==s[i-1u])run++;
        if(run>=3u){o[k++]=(uint8_t)(0x80u|(run-3u));o[k++]=0;o[k++]=0;i+=run;continue;}
        while(lit<128u&&i+lit<n&&!(i+lit&&i+lit+2u<n&&s[i+lit]==s[i+lit-1u]&&s[i+lit+1u]==s[i+lit]&&s[i+lit+2u]==s[i+lit]))lit++;
        o[k++]=(uint8_t)(lit-1u);memcpy(o+k,s+i,lit);k+=lit;i+=lit;
    }
    return k;
}
/* rec as a packed image in img: its length. tail / fun: those blocks as they are (PZ_RAW) */
static uint32_t test_image(const uint8_t *rec,int rawtail,int rawfun)
{
    pj_head_t h;memset(&h,0,sizeof h);h.magic=PZ_MAGIC;h.len=BANK_STORE_SIZE;
    memcpy(&h.map[0],rec,4);memcpy(&h.map[1],rec+BANK_STORE_SIZE-4u,4);
    uint32_t at=PJ_HEAD,n;
    if(rawtail){memcpy(img+at,rec+BANK_ACTIVE_OFF,PZ_TAIL);n=PZ_TAIL;h.map[2]=n|PZ_RAW;}else h.map[2]=n=test_pack(img+at,rec+BANK_ACTIVE_OFF,PZ_TAIL);
    at+=n;
    if(rawfun){memcpy(img+at,rec+8u,BANK_PROJ_SIZE);n=BANK_PROJ_SIZE;h.map[3]=n|PZ_RAW;}else h.map[3]=n=test_pack(img+at,rec+8u,BANK_PROJ_SIZE);
    memcpy(img,&h,sizeof h);return at+n;
}
static void head_set(uint32_t word,uint32_t v){memcpy(img+8u+4u*word,&v,4);}
static uint32_t head_get(uint32_t word){uint32_t v;memcpy(&v,img+8u+4u*word,4);return v;}
#define PJ_MAGIC 0x314A5048u                     /* HPJ1: Melodee 1.x's compact image (2.0 refuses it) */
/* rec as a compact image (HPJ1, what firmware before the packed images wrote) in img: its length */
static uint32_t test_compact(const uint8_t *rec)
{
    pj_head_t h;memset(&h,0,sizeof h);h.magic=PJ_MAGIC;h.len=BANK_STORE_SIZE;uint32_t at=PJ_HEAD;
    for(uint32_t r=0;r<2u;r++){
        uint32_t from=r?0u:BANK_ACTIVE_OFF,size=r?BANK_ACTIVE_OFF:BANK_TAIL_SIZE,pages=(size+255u)/256u;
        for(uint32_t p=0;p<pages;p++){
            uint32_t k=size-p*256u<256u?size-p*256u:256u,zero=1;
            for(uint32_t i=0;i<k;i++)zero&=!rec[from+p*256u+i];
            if(zero&&!(!r&&p+1u==pages))continue;
            h.map[2u*r+p/32u]|=1u<<(p%32u);memcpy(img+at,rec+from+p*256u,k);at+=k;
        }
    }
    memcpy(img,&h,sizeof h);return at;
}
static uint32_t put_img(uint32_t id,uint32_t n){return put_all(id,img,n,st_crc32(img,n));}
static uint32_t stored_len(uint32_t obj){st_hdr_t h;return st_current(obj,&h)<0?0u:h.len;}
static uint32_t stored_sectors(uint32_t obj){return (stored_len(obj)+ST_LIN_PAYLOAD_MAX-1u)/ST_LIN_PAYLOAD_MAX;}
static void workload(uint32_t count,uint32_t spread,uint32_t tail,uint32_t quant,uint32_t engine);
static void packed_images(void)
{
    static uint8_t rec[BANK_STORE_SIZE],noise[PZ_TAIL];
    uint32_t n,field,at,lcg=9;
    /* the packer and the loader's unpacker, block by block on a stage: zeros, noise (kept as it is), every short
     * length, runs and repeats at the lengths where the codes change, a copy from the block's first byte */
    for(uint32_t i=0;i<sizeof noise;i++){lcg=lcg*1664525u+1013904223u;noise[i]=(uint8_t)(lcg>>24);}
    for(uint32_t c=0;c<400u;c++){
        st_stream_t w;pz_in_t in;memset(&in,0,sizeof in);
        uint32_t len=c<140u?c+1u:c<300u?(c-139u)*37u:PZ_TAIL-(c-300u);
        for(uint32_t i=0;i<len;i++)
            blk[i]=c%5u==0?0u:c%5u==1u?noise[i]:c%5u==2u?(uint8_t)(i%(c%130u+1u)):c%5u==3u?(i<len/2u?noise[i]:noise[i-len/2u]):(uint8_t)(noise[i/131u]&3u);
        memcpy(img,blk,len);
        assert(!st_stream_begin_lazy(&w,OBJ_BANK0));at=PJ_HEAD;
        field=pz_size(blk,len);
        assert(!pz_store(&w,&at,blk,len,field));
        assert(!memcmp(img,blk,len));                   /* (the check reads its RAM, never changes it) */
        assert(field&PZ_RAW?field==(len|PZ_RAW)&&at==PJ_HEAD+len:field<len&&at==PJ_HEAD+field);
        assert(c%5u!=1u||len<8u||(field&PZ_RAW));       /* noise: as it is */
        assert(c%5u!=0u||len<4u||field<=4u+3u*(len/130u+1u));   /* zeros: one byte, then copies of 130 */
        memset(blk,0xA5,len);in.w=&w;in.at=PJ_HEAD;
        assert(!pz_block(&in,field,blk,len,0)&&!memcmp(img,blk,len)&&in.at==at);
        blk[len/2u]^=1u;in.at=PJ_HEAD;
        assert(pz_block(&in,field,blk,len,1));          /* (a byte off: not the same) */
    }
    /* a whole record: what a save stores, and the same record packed by another hand, load as the same music */
    workload(256,1,1,0,0);int16_t slot0=song.g[G_SLOT];project_capture(&proj_scratch);assert(bank_pack(rec,&proj_scratch,1));
    assert(!project_save_as(0,0)&&!ps_saved(OBJ_BANK0,actual)&&!memcmp(actual,rec,sizeof rec));
    {pj_head_t h;st_hdr_t sh;int copy=st_current(OBJ_BANK0,&sh);assert(copy>=0&&!st_read_range(OBJ_BANK0,(uint32_t)copy,0,&h,PJ_HEAD));
     assert(h.magic==PZ_MAGIC&&h.len==BANK_STORE_SIZE&&PJ_HEAD+(h.map[2]&~PZ_RAW)+(h.map[3]&~PZ_RAW)==sh.len&&!memcmp(&h.map[0],rec,4)&&!memcmp(&h.map[1],rec+BANK_STORE_SIZE-4u,4));}
    uint32_t packed=stored_len(OBJ_BANK0);
    n=test_compact(rec);
    printf("Project stream: a save stores %u bytes in %u sectors (the compact image before: %u in %u)\n",packed,stored_sectors(OBJ_BANK0),n,(n+ST_LIN_PAYLOAD_MAX-1u)/ST_LIN_PAYLOAD_MAX);
    assert(packed<n&&stored_sectors(OBJ_BANK0)<(n+ST_LIN_PAYLOAD_MAX-1u)/ST_LIN_PAYLOAD_MAX);
    /* a compact image (HPJ1, Melodee 1.x's) is refused, nothing written; a raw record loads, its next save is packed */
    assert(put_img(3,n)==2u&&!stored_len(OBJ_BANK0+1u));
    assert(!put_all(3,rec,sizeof rec,st_crc32(rec,sizeof rec))&&stored_len(OBJ_BANK0+1u)==BANK_STORE_SIZE&&
           !ps_saved(OBJ_BANK0+1u,actual)&&!memcmp(actual,rec,sizeof rec));
    n=BANK_STORE_SIZE;
    /* (the music of a load, captured again: the same whichever way the record was stored; SLOT aside, which it keeps) */
    static uint8_t music[BANK_STORE_SIZE];
    project_new_blank();open_slot(0);song.g[G_SLOT]=slot0;project_capture(&proj_scratch);assert(bank_pack(music,&proj_scratch,1));
    project_new_blank();open_slot(1);song.g[G_SLOT]=slot0;project_capture(&proj_scratch);assert(bank_pack(actual,&proj_scratch,1)&&!memcmp(actual,music,sizeof music));
    assert(!project_rename(1,"AGAIN")&&stored_len(OBJ_BANK0+1u)<n&&proj_read_slot(1)==BANK_STORE_SIZE&&pj.on==2u);
    for(uint32_t m=0;m<4u;m++){                         /* packed by another hand, either block as it is */
        n=test_image(rec,m&1u,m>>1);
        assert(n<=ED_BK_MAX&&!put_img(4,n)&&!ps_saved(OBJ_BANK0+2u,actual)&&!memcmp(actual,rec,sizeof rec));
        project_new_blank();open_slot(2);song.g[G_SLOT]=slot0;project_capture(&proj_scratch);assert(bank_pack(actual,&proj_scratch,1)&&!memcmp(actual,music,sizeof music));
    }
    /* an image that is not what its head says is refused, the slot as it was */
    memcpy(old_flash,nor,sizeof nor);
#define REFUSED(why) do{assert(put_img(4,n)==2u);assert(!ps_saved(OBJ_BANK0+2u,actual)&&!memcmp(actual,rec,sizeof rec));}while(0)
    n=test_image(rec,0,0);uint32_t tn=head_get(2),sn=head_get(3);
    head_set(1,head_get(1)^1u);REFUSED("the record's sum");
    n=test_image(rec,0,0);img[PJ_HEAD+tn/2u]^=0x40u;REFUSED("a stored byte");
    n=test_image(rec,0,0);head_set(2,tn-1u);head_set(3,sn+1u);REFUSED("the tail's stored bytes one short");
    n=test_image(rec,0,0);head_set(2,tn+1u);head_set(3,sn-1u);REFUSED("the tail's stored bytes one more than used");
    n=test_image(rec,0,0);head_set(2,tn+1u);REFUSED("blocks past the image");
    n=test_image(rec,0,0);head_set(2,tn|PZ_RAW);REFUSED("a packed block called as it is");
    n=test_image(rec,1,0);head_set(2,PZ_TAIL);REFUSED("a block as it is called packed");
    n=test_image(rec,0,0);img[PJ_HEAD]=0x80u;REFUSED("a copy from before the block");
    n=test_image(rec,0,0);memmove(img+PJ_HEAD+3u,img+PJ_HEAD,n-PJ_HEAD);img[PJ_HEAD]=0;img[PJ_HEAD+1u]=rec[BANK_ACTIVE_OFF];img[PJ_HEAD+2u]=0xFFu;
    head_set(2,tn+3u);n+=3u;REFUSED("a run past the block's end");
    n=test_image(rec,0,0);head_set(0,0x314A5048u);REFUSED("no FBK record");
    n=test_image(rec,0,0);memcpy(img,rec,4);REFUSED("no image");
    for(uint32_t c=0;c<300u;c++){                       /* noise where a block should be: never more than a refusal */
        n=test_image(rec,0,0);
        for(uint32_t i=0;i<40u;i++){lcg=lcg*1664525u+1013904223u;img[PJ_HEAD+(lcg>>8)%(n-PJ_HEAD)]=(uint8_t)(lcg>>24);}
        if(c&1u)for(uint32_t i=PJ_HEAD;i<n;i++){lcg=lcg*1664525u+1013904223u;img[i]=(uint8_t)(lcg>>24);}
        assert(put_img(4,n)==2u);
    }
#undef REFUSED
    assert(!ps_saved(OBJ_BANK0+2u,actual)&&!memcmp(actual,rec,sizeof rec));
    puts("Project stream: packed images: blocks at every length, another hand's and raw records loaded, 1.x compact and damaged ones refused");
}
static uint32_t stored_magic(uint32_t obj){st_hdr_t h;uint32_t m=0;int c=st_current(obj,&h);if(c>=0)st_read_range(obj,(uint32_t)c,0,&m,4);return m;}
static void power_on(void){memset(proj_meta,0,sizeof proj_meta);memset(proj_saved_hash,0,sizeof proj_saved_hash);proj_boot_scan();}
/* power-on with a 1.x compact project in slot 1: OLD FORMAT, kept byte for byte, never rewritten */
static void boot_old(void)
{
    static uint8_t one[BANK_STORE_SIZE];
    uint32_t n,was;
    workload(256,1,1,0,0);project_capture(&proj_scratch);assert(bank_pack(one,&proj_scratch,1));
    reset();n=test_compact(one);assert(!st_save(OBJ_BANK0,img,n));
    was=erases;power_on();
    assert(erases==was&&stored_magic(OBJ_BANK0)==PJ_MAGIC&&stored_len(OBJ_BANK0)==n&&proj_meta[0].old&&!proj_meta[0].used);
    power_on();assert(erases==was&&stored_magic(OBJ_BANK0)==PJ_MAGIC);
    puts("Project stream: boot: a 1.x compact project is OLD FORMAT, kept as it is, never rewritten");
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
    /* An earlier raw snapshot (ten-byte notes): refused, kept */
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
    /* (Melodee 1.x's: 2.0 never reads it, the slot keeps it as OLD FORMAT until replaced) */
    assert(!st_save(OBJ_BANK0+3u,legacy,len));assert(!proj_read_slot(3)&&proj_meta[3].old&&stored_len(OBJ_BANK0+3u)==len);
    packed_images();
    boot_old();
    midi_tracks_stream();
    extended_songs_stream(0);
    extended_songs_stream(1);
    song_sounds_stream(0); song_sounds_stream(1);
    printf("Project stream: %u complete RAM-codec comparisons, hash/dirty/save/load/rename, failed save and track removal passed\n",cases);
    return 0;
}
