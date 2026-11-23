/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DRUM's map (the engine's own: the firmware knows DRUM through its schema's pads, drum_schema.h): its lanes and the
 * General MIDI map, the kits' numbers on KIT (E1), the MPC pads, the user kits' keys. */
#include "x0x/drum909_ids.h"                    /* the 909's drums (D9_*): the sounds' numbers */
/* Lane and GM roles shared by the grid and the synthesized 808. */
enum { DV_KICK, DV_SNARE, DV_CLAP, DV_HATC, DV_HATO, DV_TOM, DV_RIM, DV_BELL, DV_NLANE };
enum { DVT_PUNCH, DVT_ROUND, DVT_SNARE, DVT_CLAP, DVT_HATC, DVT_HATO, DVT_TOM, DVT_CONGA,
       DVT_RIM, DVT_CLAVE, DVT_BELL, DVT_CYM, DVT_COUNT };
#define DK_808 4                                 /* the kits' KIT values (macros: DRUM_ONLY's #if tests them) */
#define DK_909 5
#define DK_SID 6
#define DK_CR78 7
#define DK_FM 8
#define DK_USER0 9
#ifndef MELODEE_USER_KITS
#define MELODEE_USER_KITS 1                      /* the user's kits (kit_store.c): KIT 9..16, played by drum_real.c, both boards */
#endif
#define DK_NUSER 8u
#define DK_KIT_MAX (DK_USER0 + (int)DK_NUSER - 1)
#define DK_LAST (MELODEE_USER_KITS ? DK_KIT_MAX : DK_FM)
static const uint8_t DV_TYPE_LANE[DVT_COUNT] = {
    DV_KICK, DV_KICK, DV_SNARE, DV_CLAP, DV_HATC, DV_HATO, DV_TOM, DV_TOM, DV_RIM, DV_RIM, DV_BELL, DV_BELL,
};
/* KIT 808 as DRUM's first preset has it (eng_drum.c DRUM_PRESETS; project.c: the kit of old GM projects) */
#define DRUM_PRESET_808 {"808 KIT", {DK_808, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0}
static uint32_t drum_lane(uint32_t note);
static uint32_t drum_gm(const int16_t *p,uint32_t note,int32_t *st);
/* a hit's pitch and length as the sequencer plays it, for its note_on (the firmware's: mod_abi3.h host 4 note_pitch,
 * note_len; a host build's built-in engine reads them by the firmware's names) */
#if MOD_SIDE
#define drum_event_pitch (*mod_imp_note_pitch)
#define drum_event_length (*mod_imp_note_len)
#else
#define drum_event_pitch note_hit_pitch
#define drum_event_length note_hit_len
#endif
/* A kit module plays one kit (its KIT value is fixed; SAMPLER's: the user kits, DK_USER0, its KIT the record): DRUM_ONLY
 * names it, so the other kits' code folds away; 0: every kit by KIT (a host build's reference engine) */
#ifndef DRUM_ONLY
#define DRUM_ONLY 0
#endif
#define DRUM_KV(p) (DRUM_ONLY ? DRUM_ONLY : (p)[P_E0])   /* the kit's class: its value, the user kits' DK_USER0 */
#define DRUM_IS(p, k) (DRUM_KV(p) == (k))
static int drum_is909(const track_t *t) { return DRUM_IS(t->p, DK_909); }
static int drum_issid(const track_t *t) { return DRUM_IS(t->p, DK_SID); }
static int drum_iscr(const track_t *t) { return DRUM_IS(t->p, DK_CR78); }
static int drum_isfm(const track_t *t) { return DRUM_IS(t->p, DK_FM); }
static int drum_islane(const track_t *t) { return drum_iscr(t) || drum_isfm(t); }   /* the lane kits (drum_lanes.c) */
static int drum_isuser(const track_t *t)                 /* the user's (drum_real.c) */
{
    return MELODEE_USER_KITS && (DRUM_ONLY ? DRUM_ONLY == DK_USER0 : t->p[P_E0] >= DK_USER0 && t->p[P_E0] <= DK_KIT_MAX);
}
static int drum_map11(const track_t *t) { return drum_is909(t); }   /* the 909's eleven drums, a real ride and crash */
static int drum_isone(const track_t *t) { return DRUM_KV(t->p) >= DK_SID; }      /* a whole kit on one voice */
/* the 808's instruments (drum_808.c) and its own GM map: drum_sound_of names a note's drum by them */
enum { DR_BD, DR_SD, DR_LT, DR_MT, DR_HT, DR_LC, DR_MC, DR_HC, DR_RS, DR_CL, DR_CP, DR_MA, DR_CB, DR_CY,
       DR_OH, DR_CH, DR_N };
#define DR_X 0xFFu
static const uint8_t DR_GM[47] = {               /* GM percussion keys 35..81 -> instrument */
    DR_BD, DR_BD, DR_RS, DR_SD, DR_CP, DR_SD, DR_LT, DR_CH,       /* 35 kick 2, kick, side stick .. */
    DR_LT, DR_CH, DR_MT, DR_OH, DR_MT, DR_HT, DR_CY, DR_HT,       /* 43 */
    DR_CY, DR_CY, DR_CY, DR_MA, DR_CY, DR_CB, DR_CY, DR_X,        /* 51 ride, china, bell, tamb. .. */
    DR_CY, DR_HC, DR_MC, DR_HC, DR_MC, DR_LC, DR_X, DR_X,         /* 59 ride 2, bongos, congas */
    DR_X, DR_X, DR_MA, DR_MA, DR_X, DR_X, DR_X, DR_X,             /* 67 cabasa, maracas */
    DR_CL, DR_CL, DR_CL, DR_X, DR_X, DR_X, DR_X,                  /* 75 claves, wood blocks */
};
static const uint8_t DR_OF_DVT[DVT_COUNT] = {    /* fallback GM role (DVT_*) -> the 808 instrument */
    DR_BD, DR_BD, DR_SD, DR_CP, DR_CH, DR_OH, DR_MT, DR_MC, DR_RS, DR_CL, DR_CB, DR_CY,
};

static uint32_t dr8_ins(uint32_t note, uint32_t role)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u, k = DR_GM[n - 35u];
    return k == DR_X ? DR_OF_DVT[role % DVT_COUNT] : k;
}


/* General MIDI notes 35..81 -> the drum (DVT_*; DVT_PUNCH: the kick KICK picks) and semitones from its
 * designed pitch */
static const int8_t DRUM_GM[47][2] = {
    {DVT_PUNCH, -2}, {DVT_PUNCH, 0}, {DVT_RIM, 0}, {DVT_SNARE, 0}, {DVT_CLAP, 0}, {DVT_SNARE, 2},     /* 35 */
    {DVT_TOM, -7}, {DVT_HATC, 0}, {DVT_TOM, -4}, {DVT_HATC, -2}, {DVT_TOM, 0}, {DVT_HATO, 0},         /* 41 */
    {DVT_TOM, 3}, {DVT_TOM, 5}, {DVT_CYM, 0}, {DVT_TOM, 8}, {DVT_CYM, -3}, {DVT_CYM, 2},              /* 47 */
    {DVT_BELL, 5}, {DVT_HATC, 5}, {DVT_CYM, 4}, {DVT_BELL, 0}, {DVT_CYM, 1}, {DVT_CLAVE, -12},        /* 53 */
    {DVT_CYM, -2}, {DVT_CONGA, 5}, {DVT_CONGA, 2}, {DVT_CONGA, 0}, {DVT_CONGA, 0}, {DVT_CONGA, -5},   /* 59 */
    {DVT_TOM, 10}, {DVT_TOM, 7}, {DVT_BELL, 7}, {DVT_BELL, 3}, {DVT_HATC, 3}, {DVT_HATC, 7},          /* 65 */
    {DVT_CLAVE, 7}, {DVT_CLAVE, 5}, {DVT_HATC, -4}, {DVT_HATC, -6}, {DVT_CLAVE, 0}, {DVT_CLAVE, -4},  /* 71 */
    {DVT_CLAVE, -7}, {DVT_CONGA, 7}, {DVT_CONGA, 3}, {DVT_BELL, 12}, {DVT_BELL, 12},                  /* 77 */
};
/* the drum of a note (DVT_*) and its semitones; KICK picks the kick, the 808 selects its instrument from the GM note */
static uint32_t drum_gm(const int16_t *p, uint32_t note, int32_t *st)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u, t = (uint32_t)DRUM_GM[n - 35u][0];
    *st = DRUM_GM[n - 35u][1];
    if (t == DVT_PUNCH && p[P_E6] > 0)
        t = DVT_ROUND;
    return t;
}
static uint32_t drum909_ins(uint32_t note)
{
    if (note==49 || note==52 || note==55 || note==57) return D9_CR;
    if (note==51 || note==53 || note==59 || note==56) return D9_RD;
    switch (drum_lane(note)) {
    case DV_KICK:return D9_BD; case DV_SNARE:return D9_SD; case DV_CLAP:return D9_CP;
    case DV_HATC:return D9_CH; case DV_HATO:return D9_OH; case DV_RIM:return D9_RS;
    default:return note<45?D9_LT:note<48?D9_MT:D9_HT;
    }
}
static const uint8_t DRUM_SOUND_NOTE[16]={36,38,41,45,50,37,39,42,46,49,51,64,61,60,75,70};
static uint32_t drum_sound_count(const track_t *t){return drum_map11(t)?D9_NUM:16u;}
static uint32_t drum_sound_note(const track_t *t,uint32_t sound){return !drum_map11(t) && sound==D9_RD?56u:DRUM_SOUND_NOTE[sound];}
static uint32_t drum_sound_of(const track_t *t,uint32_t note)
{
    if(drum_map11(t))return drum909_ins(note);
    static const uint8_t map[DR_N]={D9_BD,D9_SD,D9_LT,D9_MT,D9_HT,11,12,13,D9_RS,14,D9_CP,15,D9_RD,D9_CR,D9_OH,D9_CH};
    int32_t st;uint32_t role=drum_gm(t->p,note,&st);return map[dr8_ins(note,role)];
}
/* the lane a GM note strikes */
static uint32_t drum_lane(uint32_t note)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u;
    return DV_TYPE_LANE[DRUM_GM[n - 35u][0]];
}

/* A user kit (KIT 1..8, SAMPLER) on the notes: pad n on C4 + n (60 .. 75, chromatic, pad 1 first: a sampler's layout,
 * a DAW's drum rack's), any other note the pad the 808's GM map gives it (the lanes' notes, a GM beat); its lane the
 * pad's 808 drum's (DRUM MIX's lane levels), the GM note's own off the sixteen. drum_schema.h's SAMPLER tables the same
 * (tests/pads_test.c) */
#define DRUM_USER_C4 60u
static uint32_t drum_user_pad(const track_t *t, uint32_t note)
{
    return note - DRUM_USER_C4 < 16u ? note - DRUM_USER_C4 : drum_sound_of(t, note) & 15u;
}
static uint32_t drum_user_lane(const track_t *t, uint32_t note)
{
    return drum_lane(note - DRUM_USER_C4 < 16u ? drum_sound_note(t, note - DRUM_USER_C4) : note);
}
/* .. on the keys: C4 .. D#5 play the sixteen (the FM-1's C4 is its eighth key), the rest are silent, the octave is not
 * followed. ks: the key from F3 (53; the SMK-37's outer keys: -5..-1 below, 27..31 above). DRUM_SILENT: seq.c's
 * KB_SILENT (asserted there) */
#define DRUM_SILENT 255u
static uint32_t drum_user_key(const track_t *t, int32_t ks)
{
    uint32_t n = (uint32_t)(53 + ks);
    (void)t;
    return n - DRUM_USER_C4 < 16u ? n : DRUM_SILENT;
}
