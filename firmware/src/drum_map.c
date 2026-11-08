/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* What the firmware knows of DRUM (engine ENGI_DRUM) without its engine, and what the engine shares with it: the
 * lanes and the General MIDI map (the DRUM grid plays them on any engine), the kits' numbers on KIT (E1), the drums'
 * names and the MPC pads, the user kits' keys. The engine (eng_drum.c: the 808, the 909, KIT SID, CR78 and FM, the
 * user kits) is a synth module on the device (firmware/modules/drum): it compiles this file too, its own copy; the
 * data the two share (drum_flash, drum_event_*) is the firmware's. */
#include "x0x/drum909_ids.h"                    /* the 909's drums (D9_*): the sounds' numbers */
/* Lane and GM roles shared by the grid and the synthesized 808. */
enum { DV_KICK, DV_SNARE, DV_CLAP, DV_HATC, DV_HATO, DV_TOM, DV_RIM, DV_BELL, DV_NLANE };
enum { DVT_PUNCH, DVT_ROUND, DVT_SNARE, DVT_CLAP, DVT_HATC, DVT_HATO, DVT_TOM, DVT_CONGA,
       DVT_RIM, DVT_CLAVE, DVT_BELL, DVT_CYM, DVT_COUNT };
enum { DK_808 = 4, DK_909 = 5, DK_SID = 6, DK_CR78 = 7, DK_FM = 8, DK_USER0 = 9 }; /* Retain the stored value of existing 808 patches. */
#ifndef MELODEE_USER_KITS
#define MELODEE_USER_KITS 1                      /* the user's kits (kit_store.c): KIT 9..16, played by drum_real.c, both boards */
#endif
/* Record kit (kit_rec_ui.c): the device itself, with USB audio and flash; host builds of the UI never show it */
#if defined(FM1_IRQ_TARGET) && MELODEE_USER_KITS && MELODEE_USB_AUDIO && MELODEE_FLASH
#define KIT_REC 1
#else
#define KIT_REC 0
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
/* a hit's pitch and length as the sequencer plays it (seq.c), for the note_on it calls; lanes hit since the UI last
 * looked, a bit per lane (drum_lane) and track: Stage flashes their names (ui_stage.c) */
#if MOD_SIDE                              /* (DRUM as a module: the firmware's) */
static int32_t *mod_imp_drum_event_pitch;
static uint32_t *mod_imp_drum_event_length;
static volatile uint8_t *mod_imp_drum_flash;
#define drum_event_pitch (*mod_imp_drum_event_pitch)
#define drum_event_length (*mod_imp_drum_event_length)
#define drum_flash mod_imp_drum_flash
#else
static int32_t drum_event_pitch;
static uint32_t drum_event_length;
static volatile uint8_t drum_flash[NTRK];
#endif
static int drum_is909(const track_t *t) { return t->p[P_E0] == DK_909; }
static int drum_issid(const track_t *t) { return t->p[P_E0] == DK_SID; }
static int drum_iscr(const track_t *t) { return t->p[P_E0] == DK_CR78; }
static int drum_isfm(const track_t *t) { return t->p[P_E0] == DK_FM; }
static int drum_islane(const track_t *t) { return t->p[P_E0] == DK_CR78 || t->p[P_E0] == DK_FM; }   /* the lane kits (drum_lanes.c) */
static int drum_isuser(const track_t *t) { return MELODEE_USER_KITS && t->p[P_E0] >= DK_USER0 && t->p[P_E0] <= DK_KIT_MAX; }   /* the user's (drum_real.c) */
static int drum_map11(const track_t *t) { return drum_is909(t) || drum_isuser(t); }   /* the 909's eleven drums, a real ride and crash */
static int drum_isone(const track_t *t) { return t->p[P_E0] >= DK_SID; }      /* a whole kit on one voice */
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
static const char *const DRUM_SOUND_NAME[16]={"KICK","SNARE","TOM L","TOM M","TOM H","RIM","CLAP","HAT C","HAT O","CRASH","RIDE","CONGL","CONGM","CONGH","CLAVE","MARAC"};
static uint32_t drum_sound_count(const track_t *t){return drum_map11(t)?D9_NUM:16u;}
static uint32_t drum_sound_note(const track_t *t,uint32_t sound){return !drum_map11(t) && sound==D9_RD?56u:DRUM_SOUND_NOTE[sound];}
/* MPC pads 01..16, left to right from the bottom row. Keep the main beat under the fingers;
 * the 909 repeats useful hits where the 808/SID have extra percussion. These are sound identities,
 * independent of scale, transpose and input octave, just like a recorded GM drum hit. */
static uint32_t drum_mpc_note(const track_t *t, uint32_t pad)
{
    static const uint8_t PAD_808[16] = {
        D9_BD, D9_SD, D9_CP, D9_RS, D9_CH, D9_OH, D9_RD, D9_CR,
        D9_LT, D9_MT, D9_HT, 15, 11, 12, 13, 14,
    };
    static const uint8_t PAD_909[16] = {
        D9_BD, D9_SD, D9_CP, D9_RS, D9_CH, D9_OH, D9_RD, D9_CR,
        D9_LT, D9_MT, D9_HT, D9_RD, D9_BD, D9_SD, D9_CH, D9_OH,
    };
    return drum_sound_note(t, (drum_map11(t) ? PAD_909 : PAD_808)[pad & 15u]);
}
/* CR78: its tambourine plays the clap, its conga and bongos the congas */
static const char *const DRUM_CR78_NAME[16]={"KICK","SNARE","TOM L","TOM M","TOM H","RIM","TAMB","HAT C","HAT O","CYMB","BELL","CONGA","BONGL","BONGH","CLAVE","MARAC"};
static const char *drum_sound_name(const track_t *t,uint32_t sound){return drum_iscr(t)?DRUM_CR78_NAME[sound&15u]:!drum_map11(t) && sound==D9_RD?"BELL":!drum_map11(t) && sound==D9_CR?"CYMB":DRUM_SOUND_NAME[sound];}
/* .. in two letters, as on the 808's panel (DRUM HIT: the picked lane's name, which of its drums) */
static const char *drum_sound_abbr(const track_t *t,uint32_t sound)
{
    static const char *const N[16]={"BD","SD","LT","MT","HT","RS","CP","CH","OH","CR","RD","LC","MC","HC","CL","MA"};
    static const char *const CR[16]={"BD","SD","LT","MT","HT","RS","TB","CH","OH","CY","CB","CG","BL","BH","CL","MA"};
    return drum_iscr(t)?CR[sound&15u]:!drum_map11(t) && sound==D9_RD?"CB":!drum_map11(t) && sound==D9_CR?"CY":N[sound&15u];
}
static uint32_t drum_sound_of(const track_t *t,uint32_t note)
{
    if(drum_map11(t))return drum909_ins(note);
    static const uint8_t map[DR_N]={D9_BD,D9_SD,D9_LT,D9_MT,D9_HT,11,12,13,D9_RS,14,D9_CP,15,D9_RD,D9_CR,D9_OH,D9_CH};
    int32_t st;uint32_t role=drum_gm(t->p,note,&st);return map[dr8_ins(note,role)];
}
/* the drums in the black keys' order, lane by lane (KICK SNARE CLAP HATCL HATOP TOM RIM BELL), a lane's drums
 * together (the toms and congas low to high, the lane's own drum before the cymbal): KNOB 2 on DRUM HIT and
 * KNOB 1 on DRUM SOUND / MIX step through them so */
static const uint8_t DRUM_ORDER_808[16]={D9_BD,D9_SD,D9_CP,D9_CH,15,D9_OH,D9_LT,D9_MT,D9_HT,11,12,13,D9_RS,14,D9_RD,D9_CR};
static const uint8_t DRUM_ORDER_909[D9_NUM]={D9_BD,D9_SD,D9_CP,D9_CH,D9_OH,D9_LT,D9_MT,D9_HT,D9_RS,D9_RD,D9_CR};
static uint32_t drum_sound_step(const track_t *t,uint32_t sound,int32_t delta)
{
    const uint8_t *o=drum_map11(t)?DRUM_ORDER_909:DRUM_ORDER_808;int32_t n=(int32_t)drum_sound_count(t),r=0;
    while(r<n-1 && o[r]!=sound%(uint32_t)n)r++;
    return o[clamp(r+delta,0,n-1)];
}
static const char *drum_character(uint32_t sound)
{return sound==D9_SD?"SNAPPY":sound<=D9_HT?"ATTACK":"TONE";}
/* ----------------------------------------------------- the grid's lanes --- */
/* A step's lane hits (step_t.hit / acc, the DRUM grid: SEQ > STEP on a DRUM track) play these GM notes, on any
 * engine: DRUM strikes its lanes, a synth the pitches. Each is the designed pitch of
 * its lane (st 0 in DRUM_GM) */
static const uint8_t DRUM_LANE_NOTE[NLANE] = {36, 38, 39, 42, 46, 45, 37, 56};

/* the lane a GM note strikes */
static uint32_t drum_lane(uint32_t note)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u;
    return DV_TYPE_LANE[DRUM_GM[n - 35u][0]];
}

/* the lane's name as the track's KIT plays it (5 characters at most) */
static const char *drum_lane_name(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL"};
    if(drum_map11(t) && (l&(NLANE-1u))==DV_BELL)return "RIDE";
    if(drum_iscr(t) && (l&(NLANE-1u))==DV_CLAP)return "TAMB";
    return N[l & (NLANE - 1u)];
}
/* .. in two letters, the drum machine way (the grid's lane column) */
static const char *drum_lane_abbr(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
    if(drum_map11(t) && (l&(NLANE-1u))==DV_BELL)return "RD";
    if(drum_iscr(t) && (l&(NLANE-1u))==DV_CLAP)return "TB";
    return N[l & (NLANE - 1u)];
}

/* the lanes step s strikes: its hits, and its notes on their lanes (a NOTE step only) */
static uint32_t step_lanes(const step_t *s)
{
    uint32_t m = 0, i;
    if (s->time != ST_NOTE)
        return 0;
    for (i = 0; i < s->n && i < 4u; i++)
        m |= 1u << drum_lane(s->note[i]);
    return m | s->hit;
}

/* .. and which of them are accented */
static uint32_t step_accents(const step_t *s) { return s->flags & SF_ACCENT ? step_lanes(s) : s->hit ? s->acc & step_lanes(s) : 0u; }

/* the note step s strikes drum `sound` with (DRUM HIT): one of its notes, else its lane's hit when the lane's own
 * drum is that sound (the 909 ride's lane note is not its sound note); -1: none */
static int32_t step_sound_note(const track_t *t, const step_t *s, uint32_t sound)
{
    uint32_t i, l = drum_lane(drum_sound_note(t, sound));
    for (i = 0; i < s->n && i < 4u; i++)
        if (drum_sound_of(t, s->note[i]) == sound)
            return s->note[i];
    return (s->hit >> l) & 1u && drum_sound_of(t, DRUM_LANE_NOTE[l]) == sound ? DRUM_LANE_NOTE[l] : -1;
}

/* a step's notes that are a lane's note become that lane's hits (the same note, the same velocity: nothing
 * sounds different). Other notes (a low tom 41, a crash 49) stay notes, shown on their lane. A step accent
 * of a step left with hits only becomes the hits' accents. Pattern loads into a DRUM track, projects of
 * before the grid, the grid's edits */
static void step_to_grid(step_t *s)
{
    if (!s->hit) s->acc = 0;                       /* recorded synth gate is not a drum accent */
    uint32_t i, k = 0;
    if (s->time != ST_NOTE)
        return;
    for (i = 0; i < s->n && i < 4u; i++) {
        uint32_t l = drum_lane(s->note[i]);
        if (s->note[i] == DRUM_LANE_NOTE[l])
            s->hit |= (uint8_t)(1u << l);
        else
            s->note[k++] = s->note[i];
    }
    for (i = k; i < 4u; i++)
        s->note[i] = 0;
    s->n = (uint8_t)k;
    if (!k && (s->flags & SF_ACCENT)) {
        s->acc |= s->hit;
        s->flags &= (uint8_t)~SF_ACCENT;
    }
}

/* A user kit (KIT 1..8) on the keys: white key n plays MPC pad n (drum_mpc_note: the eleven drums, five again), from
 * the first white key (the FM-1's F3, the SMK-37's lowest C, so its sixteen reach D5); the black keys and the white
 * keys past the sixteenth are silent, the octave is not followed. ks: the key from F3 (the SMK-37's outer keys:
 * -5..-1 below, 27..31 above). DRUM_SILENT: seq.c's KB_SILENT (asserted there) */
#define DRUM_SILENT 255u
#if BOARD_SMK37
#define DRUM_WHITE0 0                                /* the first white key's place from C3: the SMK-37's C3 */
#else
#define DRUM_WHITE0 3                                /* (the FM-1's F3) */
#endif
static uint32_t drum_user_key(const track_t *t, int32_t ks)
{
    static const int8_t W[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};   /* C .. B: a white key's place */
    int32_t c = ks + 5, w;                                                   /* (from C3) */
    if (c < 0 || W[c % 12] < 0)
        return DRUM_SILENT;
    w = 7 * (c / 12) + W[c % 12] - DRUM_WHITE0;
    return w >= 0 && w < 16 ? drum_mpc_note(t, (uint32_t)w) : DRUM_SILENT;
}
#if !MOD_SIDE
/* TEMPORARY (phase 5, the pads): a DRUM part's patch as the firmware's DRUM SOUND / DRUM MIX pages and the editor's DRUM
 * read it (main loop): its 16 sounds' offsets, int8 each (TUNE, DECAY, the third control, 127 - LEVEL) */
static int8_t (*drum_offsets(uint32_t k))[4] { return (int8_t (*)[4])(void *)patch_of(k); }
#endif
