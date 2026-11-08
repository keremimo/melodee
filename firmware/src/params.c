/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Parameter descriptors, formatting and the page table. */
static const char *const N_LSYNC[] = {"OFF", "4BAR", "2BAR", "1/1", "1/2", "1/4", "1/8", "8T", "1/16", "16T", "1/32"};
static const uint8_t LSYNC_DIV[11] = {0, 9, 8, 7, 6, 0, 1, 4, 2, 5, 3};
static const char *const N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
static const char *const N_AMODE[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD", "REPEAT"};
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};
static const char *const N_RECQ[] = {"OFF", "1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};
static const char *const N_LTRIG[] = {"NOTE", "FREE"};
static const char *const N_LPOL[] = {"BI", "UNI"};
/* SOFT .. FUZZ are one DRIVE, their SHAPE on KNOB 4 (N_SHAPE) */
static const char *const N_FX[] = {"OFF", "PHASER", "WIDTH", "FORMANT", "TAPE", "SPIT", "", "ROOM", "REVERB", "CHORUS", "NOTE TAIL", "NOTE ECHO", "DRIVE", "DRIVE", "DRIVE", "DRIVE", "CRUSH", "FLANGER", "PEDAL PHASER", "PEDAL CHORUS"};
static const char *const N_NOTE_FX[FX_COUNT] = {"OFF", "", "", "", "", "", "", "", "", "", "NOTE TAIL", "NOTE ECHO", "", "", "", "", "", "", "", ""};
static const char *const N_AUDIO_FX[] = {"OFF", "PHASER", "WIDTH", "FORMANT", "TAPE", "SPIT", "", "ROOM", "REVERB", "CHORUS", "", "", "DRIVE", "DRIVE", "DRIVE", "DRIVE", "CRUSH", "FLANGER", "PEDAL PHASER", "PEDAL CHORUS"};
static const char *const N_SHAPE[FX_FUZZ + 1u] = {[FX_SOFT] = "SOFT", [FX_HARD] = "HARD", [FX_FOLD] = "FOLD", [FX_FUZZ] = "FUZZ"};
/* the effects as the picker and the list give them (README "FX rack"), in families: Space, Motion, Dirt, Colour,
 * Notes (the track's racks only: the last two); DRIVE stands for SOFT .. FUZZ. The stored IDs keep their numbers */
static const uint8_t FX_ORDER[] = {FX_OFF, FX_ROOM, FX_REVERB, FX_WIDTH, FX_PHASER, FX_CHORUS, FX_FLANGER, FX_PEDAL_PHASER,
                                   FX_PEDAL_CHORUS, FX_SOFT, FX_CRUSH, FX_FORMANT, FX_TAPE, FX_SPIT, FX_MIDIDELAY, FX_MIDIVERB};
static const uint8_t FX_FAM_FIRST[] = {0, 1, 4, 9, 11, 14, 16};   /* each family's first place in FX_ORDER */
static const char *const FX_FAM_NAME[] = {"Off", "Space", "Motion", "Dirt", "Colour", "Notes"};
enum { FXF_OFF, FXF_SPACE, FXF_MOTION, FXF_DIRT, FXF_COLOUR, FXF_NOTES };
static int fx_names(const param_desc_t *d) { return d->names == N_FX || d->names == N_AUDIO_FX; }
static uint32_t fx_order_n(const param_desc_t *d) { return d->names == N_AUDIO_FX ? FX_FAM_FIRST[FXF_NOTES] : NELEM(FX_ORDER); }
static uint32_t fx_rank(int32_t type)                   /* its place in FX_ORDER */
{
    uint32_t i;
    if (type >= FX_SOFT && type <= FX_FUZZ)
        type = FX_SOFT;
    for (i = 0; i < NELEM(FX_ORDER); i++)
        if (FX_ORDER[i] == type)
            return i;
    return 0;
}
static uint32_t fx_family(int32_t type)
{
    uint32_t r = fx_rank(type), f = 0;
    while (r >= FX_FAM_FIRST[f + 1u])
        f++;
    return f;
}
/* steps places on from an effect in FX_ORDER (its ends kept; no step: the effect as it is, a DRIVE's shape too) */
static int32_t fx_step(const param_desc_t *d, int32_t from, int32_t steps)
{
    int32_t r = (int32_t)fx_rank(from), to = clamp(r + steps, 0, (int32_t)fx_order_n(d) - 1);
    return to == r ? from : FX_ORDER[to];
}
static const char *const N_ONOFF[] = {"OFF", "ON"};
static const char *const N_VPOINTS[] = {"AUTO", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
                                        "14", "15"};
static const char *const N_QUANT[] = {"OFF", "SNAP", "WHITE", "ALL", "MPC"};   /* Q_OFF .. Q_MPC (seq.c kb_map, midi_map) */
/* chord keys (chord.c): OFF, the diatonic triad / seventh of the track's ROOT and SCALE on the key, fixed shapes */
static const char *const N_CHRD[] = {"OFF", "DIA3", "DIA7", "MAJ", "MIN", "DOM7", "MAJ7", "MIN7", "SUS4", "POW", "AUTO", "AUTO2"};
static const char *const N_STRUM[] = {"OFF", "1/8", "1/16", "1/32", "1/64", "1/128", "16T", "32T"};
static const char *const N_STROKE[] = {"DOWN", "UP", "ALT"};
static const char *const N_VOIC[] = {"CLOSE", "OPEN", "INV1", "INV2", "+OCT"};   /* VC_CLOSE .. VC_BASS */
static const char *const N_VOICE[] = {"POLY", "MONO", "LEG", "UNI"};   /* V_POLY .. V_UNISON */
static const char *const N_GLMODE[] = {"RATE", "TIME"};
static const char *const N_PRIO[] = {"LAST", "LOW", "HIGH"};
static const char *const N_ALLOC[] = {"ROT", "REUSE"};
static const char *const N_ORDER[] = {"NOTE", "PLAY"};
static const char *const N_CLOCK[] = {"INT", "USB", "TRS"
#if MELODEE_BLE
    , "BLE"
#endif
};
static const char *const N_MIDI_INPUT[] = {"USB", "TRS"
#if MELODEE_BLE
    , "BLE"
#endif
};
static const char *const N_ROUTE[] = {"CH1-4", "SEL", "CH5-8", "CH9-12", "CH13-16"}; /* Keep saved CH1-4/SEL ids; append ranges. */
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_BOOT[] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32"};   /* (PROJ_SLOTS) */
static const char *const N_DRUMCH[] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14",
                                       "15", "16"};
/* GLO > SYSTEM KNOB 2, DRUM: the MIDI channel that plays the first DRUM track within ROUT (none: normal
 * routing), 0 OFF, 1..16; 10 as General MIDI's. A device setting kept with the settings (ext.drumch), not the
 * project's: the column shows drumch_cell (seq.c midi_track) */
static uint8_t settings_drumch = 10;
static int16_t drumch_cell;
#define PROJ_TMPL 33                                     /* SLOT 33: the template (project.c tmpl) */
/* SAVE > PROJECT KNOB 2, BOOT: the project power-on loads (OFF, A..D; OFF or an empty slot: the template, if one is
 * saved). A device setting kept with the settings (ext.boot), not the project's: the column shows boot_cell */
static uint8_t settings_boot;
static int16_t settings_budget = VOICE_BUDGET_DEFAULT;
/* GLO > WEIGHTS: budget points per sounding voice of PROPHET, CZ-1, FM6, SID and DRUM (0 AUTO: voice.c's
 * built-in rule) and SAFETY (0: overload never cuts voices, audio.c). Device settings (settings_persist.c). */
#define VOICE_KINDS 5u
static int16_t settings_vpoints[VOICE_KINDS];
static int16_t settings_safety;
/* Device recording preferences: defaults preserve existing behavior. */
#include "recording_preferences.h"
#include "midi_knobs.h"
static int16_t boot_cell;
/* modulation matrix (mod.c): sources, destinations (E1..E8 = P_E0..P_E7: shown with the engine's labels) */
static const char *const N_MSRC[] = {"OFF", "LFO", "ENV", "VEL", "KEY", "RAND", "MODW", "AT", "EXPR", "S&H", "SLEW"};
static const char *const N_MDST[] = {"OFF", "PITCH", "CUT", "SHP", "AMP", "PAN", "", "", "", "", "RATE",
                                     "VIB", "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8", "DEPTH"};
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winitializer-overrides"
#define SHIP_NAME(n, u, nm, tg, wc, st, ns, f) [n] = (f) & SHIP_F_MODS ? nm : "-",
static const char *const N_ENGNAME[] = {[0 ... NENGINES - 1] = "-",   /* (retired and reserved numbers; 22..: synth
                                                                       * modules, INFO names them) */
    SHIP_ROWS(SHIP_NAME)                                               /* the shipped synths (../modules/shipped.h) */
    [5] = MELODEE_LEGACY_EXTRAS ? "VOICE" : "-", [ENGI_PHYS] = MELODEE_LEGACY_EXTRAS ? "PHYS" : "-",
    [11] = MELODEE_LEGACY_EXTRAS ? "NOISE" : "-", [ENGI_SLICE] = MELODEE_SLICE ? "SLICE" : "-", [ENGI_MIDI] = "MIDI Track",
#if MELODEE_HOST_ENGINES
    HOST_ENGNAMES                                                      /* (reference builds: PHASE TRIO WHEEL) */
#endif
};
#undef SHIP_NAME
#pragma clang diagnostic pop
_Static_assert(sizeof N_ENGNAME / sizeof *N_ENGNAME == NENGINES, "G_ENGSEL: one name per engine number");

#define PD(l, f, mn, mx, df) {l, f, mn, mx, df, 0, 0}
#define PE(l, n, df) {l, F_ENUM, 0, (int16_t)(sizeof(n) / sizeof(n[0]) - 1), df, n, 0}

static const param_desc_t TP[P_COUNT] = {
    [P_FXTYPE] = PE("NOTE FX", N_NOTE_FX, 0),
    [P_FXMIX] = PD("MIX", F_PCT, 0, 127, 0),
    [P_FXRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_FXCHAR] = PD("CHAR", F_PCT, 0, 127, 64),
    [P_FX2TYPE] = PE("NOTE FX", N_NOTE_FX, 0), [P_FX2MIX] = PD("MIX", F_PCT, 0, 127, 0),
    [P_FX2RATE] = PD("RATE", F_LFOHZ, 0, 127, 60), [P_FX2CHAR] = PD("CHAR", F_PCT, 0, 127, 64),
    [P_FX3TYPE] = PE("NOTE FX", N_NOTE_FX, 0), [P_FX3MIX] = PD("MIX", F_PCT, 0, 127, 0),
    [P_FX3RATE] = PD("RATE", F_LFOHZ, 0, 127, 60), [P_FX3CHAR] = PD("CHAR", F_PCT, 0, 127, 64),
    [P_LN0] = PD("KICK", F_PCT, 0, 127, 127),
    [P_LN1] = PD("SNARE", F_PCT, 0, 127, 127),
    [P_LN2] = PD("CLAP", F_PCT, 0, 127, 127),
    [P_LN3] = PD("HATCL", F_PCT, 0, 127, 127),
    [P_LN4] = PD("HATOP", F_PCT, 0, 127, 127),
    [P_LN5] = PD("TOM", F_PCT, 0, 127, 127),
    [P_LN6] = PD("RIM", F_PCT, 0, 127, 127),
    [P_LN7] = PD("BELL", F_PCT, 0, 127, 127),
    [P_LEVEL] = PD("LVL", F_DB, 0, 127, 104),
    [P_ATK] = PD("ATK", F_TIME, 0, 127, 10),
    [P_DEC] = PD("DEC", F_TIME, 0, 127, 70),
    [P_SUS] = PD("SUS", F_PCT, 0, 127, 90),
    [P_REL] = PD("REL", F_TIME, 0, 127, 60),
    [P_ED_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_ED_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_ED_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_RECQ] = PE("QNT", N_RECQ, 0),
    [P_LSYNC] = PE("SYNC", N_LSYNC, 0),
    [P_LTRIG] = PE("TRIG", N_LTRIG, 0),
    [P_LPOL] = PE("POL", N_LPOL, 0),
    [P_SPRD] = PD("SPRD", F_PCT, 0, 127, 0),
    [P_LRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_LWAVE] = PE("WAVE", N_LWAVE, 0),
    [P_LPHASE] = PD("PHS", F_INT, 0, 127, 0),
    [P_LFADE] = PD("FADE", F_TIME, 0, 127, 0),
    [P_LD_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_LD_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_LD_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_LD_AMP] = PD("AMP", F_PCT, 0, 127, 0),
    [P_AMODE] = PE("MODE", N_AMODE, 0),
    [P_ARATE] = PE("RATE", N_DIV, 2),
    [P_AOCT] = PD("OCT", F_INT, 1, 4, 1),
    [P_AGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_ASWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_APROB] = PD("PROB", F_PCT, 0, 127, 127),
    [P_AHOLD] = PE("HOLD", N_ONOFF, 0),
    [P_AORDER] = PE("ORD", N_ORDER, 0),
    [P_ROOT] = PD("ROOT", F_NOTE, 0, 11, 0),
    [P_SCALE] = PE("SCL", N_SCALE, 0),
    [P_QUANT] = PE("QNT", N_QUANT, 0),
    [P_TRANS] = PD("TRN", F_SEMI, -24, 24, 0),
    [P_SLEN] = PD("LEN", F_STEPS, 1, NSTEP, 16),
    [P_SDIV] = PE("DIV", N_DIV, 2),
    [P_SSWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_SGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_RESERVED33] = PD("", F_INT, 0, 127, 0),
    [P_RESERVED34] = PD("", F_INT, 0, 127, 0),
    [P_RESERVED35] = PD("", F_INT, 0, 127, 0),
    [P_RESERVED36] = PD("", F_INT, 0, 127, 0),
    [P_VOICE] = PE("VCE", N_VOICE, 0),
    [P_GLIDE] = PD("GLD", F_TIME, 0, 127, 0),
    [P_GLMODE] = PE("GLMOD", N_GLMODE, 0),
    [P_PRIO] = PE("PRIO", N_PRIO, 0),
    [P_ALLOC] = PE("ALLOC", N_ALLOC, 0),
    [P_DETUNE] = PD("DTUNE", F_INT, 0, 127, 40),
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("MUTE", N_ONOFF, 0),
    [P_RESERVED45] = PD("", F_INT, 0, 127, 0),
    [P_RESERVED46] = PD("", F_INT, 0, 127, 0),
    [P_TUNING] = PE("TUNING", N_TUNING, 0),
    [P_RESERVED48] = PD("", F_INT, 0, 127, 0),
#define MSLOT(k) [P_M##k##SRC] = PE("SRC" #k, N_MSRC, 0), [P_M##k##DST] = PE("DST" #k, N_MDST, 0), \
                 [P_M##k##AMT] = PD("AMT" #k, F_BIPCT, -64, 63, 0)
    MSLOT(1), MSLOT(2), MSLOT(3), MSLOT(4),
#undef MSLOT
/* the OP ENV / OP LEVEL values of DIGITAL (ids 61..80, retired): inert, on no page and in no editor layout. Their labels
 * stay: the editor's library files key parameters by label ("ATK#2" ..), so an old file's values still find them */
#define FMOP(k) [P_FM##k##_ATK] = PD("ATK", F_TIME, 0, 127, 0), \
                [P_FM##k##_DEC] = PD("DEC", F_TIME, 0, 127, 0), \
                [P_FM##k##_SUS] = PD("SUS", F_PCT, 0, 127, 127), \
                [P_FM##k##_REL] = PD("REL", F_TIME, 0, 127, 0), \
                [P_FM##k##_LEVEL] = PD("LVL", F_PCT, 0, 127, 127)
    FMOP(1), FMOP(2), FMOP(3), FMOP(4),
#undef FMOP
    [P_FXSEND] = PD("SEND", F_PCT, 0, 127, 0),
    [P_FXSEND2] = PD("SEND 2", F_PCT, 0, 127, 0),
    [P_FXSEND3] = PD("SEND 3", F_PCT, 0, 127, 0),
    [P_FXTONE] = PD("TONE", F_TONE, -FX_TONE_MAX, FX_TONE_MAX, 0),
    [P_FX2TONE] = PD("TONE", F_TONE, -FX_TONE_MAX, FX_TONE_MAX, 0),
    [P_FX3TONE] = PD("TONE", F_TONE, -FX_TONE_MAX, FX_TONE_MAX, 0),
    [P_CHRD] = PE("CHRD", N_CHRD, 0),
    [P_VOIC] = PE("VOIC", N_VOIC, 0),
    [P_STRUM] = PE("STRUM", N_STRUM, 0),
    [P_STROKE] = PE("STROKE", N_STROKE, 0),
    [P_MPCDEG] = PD("DEG", F_INT, 1, 12, 1),         /* its max: the scale's notes (track_desc) */
};

static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 120),
    [G_SWING] = PD("SWG", F_PCT, 0, 100, 0),
    [G_CLOCK] = PE("CLK", N_CLOCK, 0),
    [G_TUNE] = PD("TUNE", F_INT, -50, 50, 0),
    [G_RESERVED4] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED5] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED6] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED7] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED8] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED9] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED10] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED11] = PD("", F_INT, 0, 127, 0),
    [G_MIDI] = PE("MIDI", N_MIDI_INPUT, 0),
    [G_DRUMCH] = PE("DRUM", N_DRUMCH, 10),
    [G_ROUTE] = PE("ROUT", N_ROUTE, 0),          /* (was "--": stored 0 = CH1-4, as MIDI IN always was) */
    [G_INFO] = PD("CPU", F_INT, 0, 0, 0),
    [G_SLOT] = PD("SLOT", F_INT, 1, PROJ_TMPL, 1),     /* PROJ_TMPL: the template (param_format: TMPL) */
    [G_BOOT] = PE("BOOT", N_BOOT, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_ENGSEL] = PE("ENG", N_ENGNAME, 0),
    [G_A4] = {"A4", F_INT, A4_MIN, A4_MAX, A4_DEFAULT, 0, "Hz"},
    [G_CLRSEQ] = PE("CLRSQ", N_GO, 0),
    [G_INITSND] = PE("INIT", N_GO, 0),
    [G_RESERVED24] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED25] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED26] = PD("", F_INT, 0, 127, 0),
    [G_MFXTYPE] = PE("TYPE", N_AUDIO_FX, 0),
    [G_MFXMIX] = PD("MIX", F_PCT, 0, 127, 127),
    [G_MFXRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [G_MFXCHAR] = PD("CHAR", F_PCT, 0, 127, 64),
    [G_MFX2TYPE] = PE("TYPE", N_AUDIO_FX, 0),
    [G_MFX2MIX] = PD("MIX", F_PCT, 0, 127, 127),
    [G_MFX2RATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [G_MFX2CHAR] = PD("CHAR", F_PCT, 0, 127, 64),
    [G_MFX3TYPE] = PE("TYPE", N_AUDIO_FX, 0),
    [G_MFX3MIX] = PD("MIX", F_PCT, 0, 127, 127),
    [G_MFX3RATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [G_MFX3CHAR] = PD("CHAR", F_PCT, 0, 127, 64),
    [G_FXRETURN] = PD("RETURN", F_PCT, 0, 127, 100),
    [G_FXMODE] = {"MODE", F_ENUM, 0, 1, 0, (const char *const []){"CHAIN", "SPLIT"}, 0},
    [G_BUDGET] = {"BUDGET", F_INT, 8, 32, VOICE_BUDGET_DEFAULT, 0, "pts"},
    [G_MFXTONE] = PD("TONE", F_TONE, -FX_TONE_MAX, FX_TONE_MAX, 0),
    [G_MFX2TONE] = PD("TONE", F_TONE, -FX_TONE_MAX, FX_TONE_MAX, 0),
    [G_MFX3TONE] = PD("TONE", F_TONE, -FX_TONE_MAX, FX_TONE_MAX, 0),

};

static const param_desc_t *fx_pedal_desc(uint32_t type,uint32_t col,const param_desc_t *original)
{
    static param_desc_t d;d=*original;
    if(fx_sloop_swept(type)){if(col==4u){d.label="RESO";d.fmt=F_BIPCT;}else if(col==3u)d.label="DEPTH";}
    else if(type>=FX_SOFT && type<=FX_FUZZ){
        if(col==2u){d.label="DRIVE";d.fmt=F_PCT;}
        if(col==3u){d.label="TONE";d.fmt=F_CUTOFF;}
    }else if(type==FX_CRUSH){d.label=col==2u?"BITS":"ALIAS";d.fmt=col==2u?F_BITS:F_ALIAS;}
    return &d;
}
/* a DRIVE's KNOB 4 (the slot's TONE place): its SHAPE, the slot's effect SOFT .. FUZZ itself */
static const param_desc_t FX_SHAPE = {"SHAPE", F_ENUM, FX_SOFT, FX_FUZZ, FX_SOFT, N_SHAPE, 0};
static int fx_drive(int32_t type) { return type >= FX_SOFT && type <= FX_FUZZ; }
static const param_desc_t *global_desc(uint32_t id)
{
    if(id>=G_MFXTONE && id<=G_MFX3TONE){uint32_t type=song.g[G_MFXTYPE+(id-G_MFXTONE)*FX_PARAMS];if(fx_sloop_swept(type))return fx_pedal_desc(type,4,&GP[id]);}
    if(param_master_fx(id) && (id-G_MFXTYPE)%FX_PARAMS>=2u){
        static param_desc_t d;static const char *const character[]={"CHAR","DEPTH","DEPTH","FOCUS","JITTER","BITE","REPEAT","DECAY","DAMP","DEPTH","","","TONE","TONE","TONE","TONE","ALIAS","DEPTH","DEPTH","DEPTH"};
        uint32_t base=G_MFXTYPE+(id-G_MFXTYPE)/FX_PARAMS*FX_PARAMS,type=(uint32_t)clamp(song.g[base],0,NELEM(N_AUDIO_FX)-1);d=GP[id];
        if(fx_sloop_type(type))return fx_pedal_desc(type,id-base,&GP[id]);
        if(id-base==3u)d.label=character[type];else if(type>=6u && type<=8u){d.label=type==6u?"TIME":"SIZE";d.fmt=F_PCT;}
        return &d;
    }
    return &GP[id];
}
static uint32_t scale_count(const track_t *t);          /* seq.c */
/* PHASE's split envelopes (engine 2, eng_phase.c): the retired FM4 parameter storage. The
 * ids, ranges and defaults remain compatible with existing projects/presets. */
static const param_desc_t PH_ED[20] = {
#define PH_ROW(n) {"A" n, F_TIME, 0,127,0,0,0}, {"D" n, F_TIME,0,127,0,0,0}, \
    {"S" n,F_PCT,0,127,127,0,0}, {"R" n,F_TIME,0,127,0,0,0}, {"LV" n,F_PCT,0,127,127,0,0}
    PH_ROW("W1"), PH_ROW("W2"), PH_ROW("A2"), PH_ROW("P"),
#undef PH_ROW
};
static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if (midi_track_audio_disabled(t->eng_req, id)) {
        static param_desc_t disabled;
        disabled = TP[id]; disabled.min = disabled.max = disabled.def = t->p[id];
        return &disabled;
    }
    if(param_fx_tone(id)){uint32_t type=t->p[P_FXTYPE+(id-P_FXTONE)*FX_PARAMS];if(fx_sloop_swept(type))return fx_pedal_desc(type,4,&TP[id]);}
    if (id >= P_FXTYPE && id <= P_FX3CHAR) {
        uint32_t base = P_FXTYPE + (id - P_FXTYPE) / FX_PARAMS * FX_PARAMS, col = id - base;
        if (fx_is_midi(t->p[base]) && col) {
            static param_desc_t d;
            d = TP[id];
            if (t->p[base] == FX_MIDIVERB) { d.label = "REL"; d.fmt = F_TIME; }
            else if (col == 1u) d.label = "LEVEL";     /* (the echoes' velocity, of the note's) */
            else if (col == 2u) { d.label = "TIME"; d.fmt = F_ENUM; d.names = N_DIV; d.max = NELEM(N_DIV) - 1u; }
            else if (col == 3u) { d.label = "DECAY"; d.fmt = F_PCT; }
            return &d;
        }
    }
    if (param_fx(id) && !param_fx_tone(id) && ((id-P_FXTYPE)%FX_PARAMS)>=2u) {
        static param_desc_t fx_desc;
        static const char *const character[] = {"CHAR", "DEPTH", "DEPTH", "FOCUS", "JITTER", "BITE", "REPEAT", "DECAY", "DAMP", "DEPTH", "", "", "TONE", "TONE", "TONE", "TONE", "ALIAS", "DEPTH", "DEPTH", "DEPTH"};
        uint32_t base=P_FXTYPE+(id-P_FXTYPE)/FX_PARAMS*FX_PARAMS;
        fx_desc=TP[id];uint32_t type=(uint32_t)clamp(t->p[base],0,NELEM(N_FX)-1);
        if(fx_sloop_type(type))return fx_pedal_desc(type,id-base,&TP[id]);
        if(id-base==3u)fx_desc.label=character[type];
        else if(type>=6u && type<=8u){fx_desc.label=type==6u?"TIME":"SIZE";fx_desc.fmt=F_PCT;}
        return &fx_desc;
    }
    if (t->eng_req == 2u && id >= P_FM1_ATK && id <= P_FM4_LEVEL)
        return &PH_ED[id - P_FM1_ATK];
    if (id == P_MPCDEG) {                             /* a degree of the track's scale */
        static param_desc_t degree;
        degree = TP[P_MPCDEG];
        degree.max = (int16_t)scale_count(t);
        return &degree;
    }
    if (id >= P_E0 && id <= P_E7) {                   /* the engine asked for (t->engine follows after a fade) */
        const engine_t *e = ENGINES[eng_idx(t->eng_req)];
        const param_desc_t *d = e->desc ? e->desc(t, id - P_E0) : 0;   /* a mode-dependent label / names */
        return d ? d : &e->edit[id - P_E0];
    }
    return &TP[id];
}

/* the descriptor of parameter id for engine e without the engine's desc hook (the device display
 * only; range and default are the same): what the editor protocol, user presets and projects use */
static const param_desc_t *param_desc_of(uint32_t e, uint32_t id)
{
    if (e == 2u && id >= P_FM1_ATK && id <= P_FM4_LEVEL)
        return &PH_ED[id - P_FM1_ATK];
    return id >= P_E0 ? &ENGINES[e]->edit[id - P_E0] : &TP[id];
}

/* a retired F_ENUM value kept as an alias, so stored values stay valid: SAMPLE SET and GRAIN SRC 1, once
 * TRANH, play PIANO (tools/gen_samples.py SMP_SET_ORIG). It shows the original's name; knobs step over it
 * and the editor's SET lands on the original. -> the value v stands for */
static int32_t enum_orig(const param_desc_t *d, int32_t v)
{
    return d->fmt==F_ENUM && d->names && !d->names[v][0] ? 0 : v;
}

/* a knob moved an F_ENUM from `from` to v: past any alias in that direction (back to `from` at the end) */
static int32_t enum_step(const param_desc_t *d, int32_t from, int32_t v)
{
    int32_t dir = v > from ? 1 : -1;
    if (fx_names(d))                                    /* (the effects: in their families' order) */
        return fx_step(d, from, v - from);
    while (v != from && enum_orig(d, v) != v)
        v = v + dir > d->max || v + dir < d->min ? from : v + dir;
    return v;
}

/* CRUSH's ALIAS: samples each one is held (fx_sloop_insert.h INS_HOLD, its copy in flash for the pages) */
static const uint8_t CRUSH_HOLD[16] = {64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1};
static uint8_t fmt_named;        /* the last param_format was a name (F_ENUM, even "4"): ui_draw.c's digits do not roll it */

/* value string (<= 5 chars) and unit for a parameter value */
static int sch_format(const param_desc_t *d, int32_t v, char *val, const char **unit);   /* (below: a synth's) */
static const char *eng_abbr_of(uint32_t e);
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    fmt_named = d->fmt == F_ENUM;
    if (sch_format(d, v, val, unit))                /* (a synth's own text: its module's) */
        return;
    if (d == &GP[G_SLOT] && v == PROJ_TMPL) {
        str_cpy(val, "TMPL", 8);
        fmt_named = 1;
        return;
    }
    switch (d->fmt) {
    case F_PCT:                                     /* a 0..100 range (SWG) is its value, others a share of 127 */
        fmt_int(val, d->max == 100 ? v : (v * 100 + 63) / 127);
        *unit = "%";
        break;
    case F_BIPCT:
        fmt_int(val, v * 100 / 64);
        if (v > 0) {
            char t[8];
            fmt_int(t, v * 100 / 64);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "%";
        break;
    case F_TIME: {
        uint32_t ms10 = TIME_MS_X10[v & 127];
        if (ms10 < 100u) {
            fmt_fix(val, (int32_t)ms10, 1);
            *unit = "ms";
        } else if (ms10 < 10000u) {
            fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
            *unit = "ms";
        } else {
            fmt_fix(val, (int32_t)(ms10 / 100u), 2);
            if (ms10 >= 100000u)
                fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
            *unit = "s";
        }
        break;
    }
    case F_LFOHZ: {
        uint32_t h = LFO_HZ_X100[v & 127];
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else
            fmt_fix(val, (int32_t)(h / 10u), 1);
        *unit = "Hz";
        break;
    }
    case F_BITS:                                      /* CRUSH: fx_sloop_insert.h ins_bits_sh of 127 - v */
        fmt_int(val, 1 + ((127 - clamp(v, 0, 127)) >> 3));
        *unit = "bit";
        break;
    case F_ALIAS: {                                   /* CRUSH: the held sample's rate */
        uint32_t h = FS / CRUSH_HOLD[(127 - clamp(v, 0, 127)) >> 3];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else {
            fmt_fix(val, (int32_t)((h + 50u) / 100u), 1);
            *unit = "kHz";
        }
        break;
    }
    case F_TONE:                                      /* (the cut's frequency: the FX picture's strip) */
        str_cpy(val, v < 0 ? "DARK" : v > 0 ? "THIN" : "FULL", 6);
        break;
    case F_CUTOFF: {
        uint32_t h = CUTOFF_HZ[v & 127];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else {
            fmt_fix(val, (int32_t)(h / 100u), 1);
            *unit = "kHz";
        }
        break;
    }
    case F_DB:
        if (v <= 0) {
            str_cpy(val, "OFF", 6);
        } else {
            fmt_fix(val, LEVEL_DB_X10[v], 1);
            *unit = "dB";
        }
        break;
    case F_SEMI:
        fmt_int(val, v);
        if (v > 0) {
            char t[8];
            fmt_int(t, v);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    case F_ENUM:
        if (d->names == N_ROUTE) {
            v = clamp(v, d->min, d->max);
            /* The panel's compact card fits "13-16"; editor/picker names retain the CH prefix. */
            str_cpy(val, d->names[v] + (v == 1 ? 0 : 2), 6);
        }
#if MELODEE_RECORDS
        else if (pads_rec_value(d, v) >= 0) {        /* a kit's record: its own name (none: EMPTY) */
            if (!rec_name((uint32_t)pads_rec_value(d, v), val, 6))
                str_cpy(val, "EMPTY", 6);
        }
#endif
        else
            str_cpy(val, d->names==N_ENGNAME ? eng_abbr_of((uint32_t)clamp(v, d->min, d->max)) : d->names[v < d->min ? d->min : v > d->max ? d->max : v], 6);
        if (d->unit)
            *unit = d->unit;
        break;
    case F_BPM:
        fmt_int(val, v);
        *unit = "BPM";
        break;
    case F_NOTE:
        str_cpy(val, N_NOTE[v % 12], 6);
        break;
    case F_ONOFF:
        str_cpy(val, N_ONOFF[v ? 1 : 0], 6);
        break;
    case F_STEPS:
        fmt_int(val, v);
        *unit = "STEP";
        break;
    case F_OFS: {                                     /* FM6: 0 at the middle of the range (DTN -7..7, TRNS +-24) */
        int32_t o = v - (d->min + d->max) / 2;
        if (o > 0) {
            val[0] = '+';
            fmt_int(val + 1, o);
        } else {
            fmt_int(val, o);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
    case F_FMNOTE:                                    /* the DX7 break point: 0 = A-1, 39 = C3 (MIDI 60) */
        str_cpy(val, N_NOTE[(v + 21) % 12], 6);
        fmt_int(val + str_len(val), (v + 21) / 12 - 1);
        break;
    case F_FMFRQ:                                     /* (a synth's own text: its schema's, sch_format above) */
        fmt_int(val, v);
        break;
    default:
        if (d->names) {                               /* F_INT with a 0-terminated name list: the range */
            uint32_t k = 0;                           /* split evenly over the names (engine desc hooks) */
            while (d->names[k])
                k++;
            str_cpy(val, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * k / (uint32_t)(d->max - d->min + 1)], 6);
        } else {
            fmt_int(val, v);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
}

/* ------------------------------------------------------------ pages --- */
enum { FAM_HOME, FAM_ENV, FAM_LFO, FAM_SCL, FAM_EDIT, FAM_GLO, FAM_SAVE, FAM_ARP, FAM_SEQ, FAM_TRK,
       FAM_FX, FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_ENGINE, SC_STEP, SC_TRK,   /* SC_TRK: the TRACKS page (ui_input.c tracks_edit) */
       SC_MOD,                                   /* a page of the selected track's synth's schema (sch_page_at) */
       SC_CZ, SC_PADS, SC_PADHIT };   /* SC_PADS / SC_PADHIT: a kit's pads and hits (pads.c, sch_pages.c page_at) */
enum { GR_NONE, GR_ADSR, GR_LFO, GR_STEPS, GR_ARP, GR_SCALE, GR_ROLL, GR_BROWSE, GR_SLOTS, GR_USER, GR_TRK,
       GR_MOD, GR_PATS, GR_SONG, GR_MOTION, GR_CHORD, GR_SLICES,
       GR_ENV8, GR_STORE, GR_TOOLS, GR_SCALE_PICKER, GR_PADHIT, GR_PATGRID,
       GR_FXRACK, GR_WDRAW }; /* NOTES shares GR_ROLL for steps and recorded events; GR_ENV8, GR_STORE, GR_TOOLS,
                               * GR_WDRAW: a synth's schema's widgets (MOD3_W_ENV8, STORE, TOOLS, DRAW) */

typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* param ids; 0xFF = empty slot */
} page_t;

/* a synth's schema pages take these places in PAGES (their index in its schema: id[0]): the selected track's synth's
 * (sch_page_at) where the shipped synths' pages were, after EDIT 1 / 2 and the DRUM pages */
#define SCH_PG_MAX 48u
#define SCH_PG8(k) {0, FAM_EDIT, SC_MOD, GR_NONE, {k, 0xFF, 0xFF, 0xFF}}, {0, FAM_EDIT, SC_MOD, GR_NONE, {k + 1, 0xFF, 0xFF, 0xFF}}, \
    {0, FAM_EDIT, SC_MOD, GR_NONE, {k + 2, 0xFF, 0xFF, 0xFF}}, {0, FAM_EDIT, SC_MOD, GR_NONE, {k + 3, 0xFF, 0xFF, 0xFF}}, \
    {0, FAM_EDIT, SC_MOD, GR_NONE, {k + 4, 0xFF, 0xFF, 0xFF}}, {0, FAM_EDIT, SC_MOD, GR_NONE, {k + 5, 0xFF, 0xFF, 0xFF}}, \
    {0, FAM_EDIT, SC_MOD, GR_NONE, {k + 6, 0xFF, 0xFF, 0xFF}}, {0, FAM_EDIT, SC_MOD, GR_NONE, {k + 7, 0xFF, 0xFF, 0xFF}}

#define G_PROJECT_RECQ 0xFEu                            /* shared recording QNT, mirrored in track slots for storage/protocol compatibility */
static const page_t PAGE_TAB[] = {
    /* FX (docs/design README "FX rack"): the track's rack, its three slots (shown while they hold an effect), the
     * shared rack, its three slots. A rack: KNOB 2 the row (fx_rack_row), 1 its effect, 4 its amount, SHARED's 3 ROUTE
     * (page_desc) */
    {"FX", FAM_FX, SC_TRACK, GR_FXRACK, {P_FXTYPE, 0xFF, 0xFF, P_FXMIX}},
    {"FX 1", FAM_FX, SC_TRACK, GR_NONE, {P_FXMIX, P_FXRATE, P_FXCHAR, P_FXTONE}},
    {"FX 2", FAM_FX, SC_TRACK, GR_NONE, {P_FX2MIX, P_FX2RATE, P_FX2CHAR, P_FX2TONE}},
    {"FX 3", FAM_FX, SC_TRACK, GR_NONE, {P_FX3MIX, P_FX3RATE, P_FX3CHAR, P_FX3TONE}},
    {"SHARED", FAM_FX, SC_GLOBAL, GR_FXRACK, {G_MFXTYPE, 0xFF, 0xFF, G_MFXMIX}},
    {"SEND FX 1", FAM_FX, SC_GLOBAL, GR_NONE, {G_MFXMIX, G_MFXRATE, G_MFXCHAR, G_MFXTONE}},
    {"SEND FX 2", FAM_FX, SC_GLOBAL, GR_NONE, {G_MFX2MIX, G_MFX2RATE, G_MFX2CHAR, G_MFX2TONE}},
    {"SEND FX 3", FAM_FX, SC_GLOBAL, GR_NONE, {G_MFX3MIX, G_MFX3RATE, G_MFX3CHAR, G_MFX3TONE}},
    {"ENV", FAM_ENV, SC_TRACK, GR_ADSR, {P_ATK, P_DEC, P_SUS, P_REL}},
    {"ENV DEST", FAM_ENV, SC_TRACK, GR_NONE, {P_ED_FLT, P_ED_PIT, P_ED_SHP, 0xFF}},   /* the former fourth slot: SEQ > TEMPO's QNT */
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE}},
    {"LFO 2", FAM_LFO, SC_TRACK, GR_LFO, {P_LSYNC, P_LTRIG, P_LPOL, 0xFF}},
    {"LFO DEST", FAM_LFO, SC_TRACK, GR_NONE, {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP}},
    {"MOD", FAM_LFO, SC_TRACK, GR_MOD, {P_M1SRC, 0xFF, P_M1DST, P_M1AMT}},   /* KNOB 2: the route (mod_ui_slot) */
    {"SCALES", FAM_SCL, SC_TRACK, GR_SCALE_PICKER, {0xFF, P_SCALE, P_ROOT, P_QUANT}},
    {"SCL", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, 0xFF, P_QUANT, P_TRANS}}, /* K2: scale favorite */
    {"CHORD", FAM_SCL, SC_TRACK, GR_CHORD, {P_CHRD, P_VOIC, P_STRUM, P_STROKE}},   /* SCL again: the chord keys (chord.c) */
    {"MPC", FAM_SCL, SC_TRACK, GR_NONE, {P_MPCDEG, P_TUNING, 0xFF, 0xFF}},   /* the degree of each bank's pad 02: a row of SCL's list
                                                                          * (QNT MPC only), not a page of its own */
    {"EDIT 1", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E0, P_E1, P_E2, P_E3}},
    {"EDIT 2", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E4, P_E5, P_E6, P_E7}},
    /* a kit's pads (pads.c: its synth's pads block names them, the first three pad parameters, the fourth) */
    {"",FAM_EDIT,SC_PADS,GR_NONE,{0,1,2,3}},
    {"",FAM_EDIT,SC_PADS,GR_NONE,{0,4,255,255}},
    /* the selected track's synth's own pages (its schema: sch_page_at) */
    SCH_PG8(0), SCH_PG8(8), SCH_PG8(16), SCH_PG8(24), SCH_PG8(32), SCH_PG8(40),
    /* PHASE: the retired FM4 storage fields hold split CZ envelopes. */
    {"DCW1 ENV", FAM_EDIT, SC_CZ, GR_NONE, {P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL}},
    {"DCW2 ENV", FAM_EDIT, SC_CZ, GR_NONE, {P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL}},
    {"DCA2 ENV", FAM_EDIT, SC_CZ, GR_NONE, {P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL}},
    {"DCO ENV", FAM_EDIT, SC_CZ, GR_NONE, {P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL}},
    {"PD LEVEL", FAM_EDIT, SC_CZ, GR_NONE, {P_FM1_LEVEL, P_FM2_LEVEL, P_FM3_LEVEL, P_FM4_LEVEL}},
    {"OP1 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL}},
    {"OP2 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL}},
    {"OP3 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL}},
    {"OP4 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL}},
    {"OP LEVEL", FAM_EDIT, SC_TRACK, GR_NONE, {P_FM1_LEVEL, P_FM2_LEVEL, P_FM3_LEVEL, P_FM4_LEVEL}},
    {"LANES", FAM_EDIT, SC_TRACK, GR_NONE, {P_LN0, P_LN1, P_LN2, P_LN3}},
    {"LANES 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_LN4, P_LN5, P_LN6, P_LN7}},
    {"VOICE", FAM_EDIT, SC_TRACK, GR_NONE, {P_VOICE, P_GLIDE, P_GLMODE, P_PRIO}},
    {"VOICE 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_ALLOC, P_DETUNE, P_PAN, P_MUTE}},
    {"VOICE 3", FAM_EDIT, SC_TRACK, GR_NONE, {P_SPRD, 0xFF, 0xFF, 0xFF}},
    {"GLOBAL", FAM_GLO, SC_GLOBAL, GR_NONE, {G_A4, G_BUDGET, G_CLOCK, G_TUNE}}, /* device reference/budget; CLK/TUNE kept */
    {"SYSTEM", FAM_GLO, SC_GLOBAL, GR_NONE, {G_MIDI, G_DRUMCH, G_ROUTE, G_INFO}},
    {"WEIGHTS", FAM_GLO, SC_GLOBAL, GR_NONE, {G_WP5, G_WCZ, G_WFM6, G_WSID}},        /* budget points per voice */
    {"WEIGHTS 2", FAM_GLO, SC_GLOBAL, GR_NONE, {G_WDRUM, G_BUDGET, 0xFF, G_SAFETY}},  /* SAFETY: overload voice cuts */
    {"PRESETS", FAM_SAVE, SC_GLOBAL, GR_BROWSE, {0xFF, 0xFF, 0xFF, 0xFF}},   /* browser: KNOB 1 LIST, 2 / PRESETS the sounds */
    {"USER", FAM_SAVE, SC_GLOBAL, GR_USER, {0xFF, 0xFF, 0xFF, 0xFF}},       /* user presets: SLOT LOAD ERASE SAVE */
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, G_BOOT, G_LOAD, G_SAVE}},
    {"ARP", FAM_ARP, SC_TRACK, GR_ARP, {P_AMODE, P_ARATE, P_AOCT, P_AGATE}},
    {"ARP 2", FAM_ARP, SC_TRACK, GR_NONE, {P_ASWING, P_APROB, P_AHOLD, P_AORDER}},
    {"NOTES", FAM_SEQ, SC_STEP, GR_ROLL, {0, 1, 2, 3}},
    {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, {P_SLEN, P_SDIV, P_SSWING, P_SGATE}},
    {"TEMPO", FAM_SEQ, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_PROJECT_RECQ, 0xFF}},   /* project speed, swing and recording grid */
    {"MIXER", FAM_TRK, SC_TRK, GR_TRK, {0, 1, 2, 3}},   /* GLO button; LEVEL PAN MUTE */
    {"PATTERNS", FAM_SEQ, SC_GLOBAL, GR_PATGRID, {0xFF, 0xFF, 0xFF, 0xFF}},   /* the 4 x 8 grid: KNOB k track k's (ui_stage.c) */
    {"SONG", FAM_SEQ, SC_GLOBAL, GR_SONG, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"MOTION", FAM_SEQ, SC_TRACK, GR_MOTION, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"",FAM_SEQ,SC_PADHIT,GR_PADHIT,{0,1,2,3}},    /* a kit's hits (its pads block names it) */
};
#define NPAGES (sizeof(PAGE_TAB) / sizeof(PAGE_TAB[0]))
static const page_t *page_at(uint32_t i);                /* (below: a synth's page where PAGE_TAB has its place) */
#define PG(i) (*page_at(i))
static uint8_t mod_ui_slot;      /* the MOD page: the matrix slot (0..3) KNOB 2..4 edit */
static uint8_t fx_rack_row[2];   /* the racks' rows KNOB 2 picks: FX's (0..2 its slots, 3 the shared FX), SHARED's
                                  * (0..2 its slots, 3 RETURN) */

/* a rack's KNOB 1 (its row's effect) and KNOB 4 (the row's amount): FX's slots their MIX, its shared row the track's
 * SEND (CHAIN; in SPLIT the track's three sends are SHARED's rows); SHARED's slots their MIX (SPLIT: the track's send to
 * each), its last row RETURN. SHARED's KNOB 3: ROUTE (G_FXMODE) */
static const param_desc_t *fx_rack_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t shared = pg->scope == SC_GLOBAL, r = fx_rack_row[shared] & 3u, id;
    *valp = 0;
    if (slot == 0u && r < FX_SLOTS && !shared) {
        id = P_FXTYPE + r * FX_PARAMS;
        *valp = &TSEL->p[id];
        return track_desc(TSEL, id);
    }
    if (slot == 0u && r < FX_SLOTS) {                   /* (SHARED's effect: "FX" as the track's, not TYPE) */
        static param_desc_t type;
        id = G_MFXTYPE + r * FX_PARAMS;
        *valp = &song.g[id];
        type = *global_desc(id);
        type.label = "FX";
        return &type;
    }
    if (slot == 2u && shared) {                         /* SHARED's KNOB 3: the routing, CHAIN or SPLIT (its sheet too) */
        static param_desc_t route;
        *valp = &song.g[G_FXMODE];
        route = *global_desc(G_FXMODE);
        route.label = "ROUTE";
        return &route;
    }
    if (slot != 3u || (!shared && r == FX_SLOTS && song.g[G_FXMODE]))
        return 0;
    if (shared && r == FX_SLOTS) {
        *valp = &song.g[G_FXRETURN];
        return global_desc(G_FXRETURN);
    }
    if (shared && !song.g[G_FXMODE]) {
        *valp = &song.g[G_MFXMIX + r * FX_PARAMS];
        return global_desc(G_MFXMIX + r * FX_PARAMS);
    }
    id = shared ? P_FXSEND + r : r < FX_SLOTS ? P_FXMIX + r * FX_PARAMS : P_FXSEND;
    *valp = &TSEL->p[id];
    if (shared) {                                       /* (SPLIT: "SEND", the row says to which) */
        static param_desc_t send;
        send = *track_desc(TSEL, id);
        send.label = "SEND";
        return &send;
    }
    return track_desc(TSEL, id);
}
static const param_desc_t *sch_page_desc(const page_t *pg, uint32_t slot, int16_t **valp);   /* (below) */
static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = pg->id[slot];
    if (midi_track_only(TSEL) && ((pg->fam == FAM_FX && pg->scope == SC_TRACK) ||
        ((pg->scope == SC_TRACK || pg->scope == SC_ENGINE) && midi_track_audio_disabled(TSEL->eng_req, id)))) {
        *valp = 0; return 0;
    }
    if (pg->scope == SC_MOD)                             /* a synth's own page (a copy: sch_page_put writes it) */
        return sch_page_desc(pg, slot, valp);
    if(pg->scope==SC_PADS || pg->scope==SC_PADHIT){*valp=0;return 0;}
    if (pg->graph == GR_FXRACK)
        return fx_rack_desc(pg, slot, valp);
    if (id == 0xFFu) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_STEP || pg->scope == SC_TRK) {
        *valp = 0;
        return 0;
    }
    if (id == G_PROJECT_RECQ) {                      /* TEMPO's project-wide recording quantization */
        *valp = &TSEL->p[P_RECQ];
        return track_desc(TSEL, P_RECQ);
    }
    if (pg->scope == SC_GLOBAL && id >= G_WP5 && id <= G_SAFETY) {   /* GLO > WEIGHTS: the device's settings */
        static param_desc_t weights[VOICE_KINDS + 1u];    /* a cost class's row: its synth's tag (the manifest's) */
        if (!weights[VOICE_KINDS].label) {
            static const param_desc_t guard = PE("GUARD", N_ONOFF, 0);
            for (uint32_t c = 0; c < VOICE_KINDS; c++) {
                static const param_desc_t row = PE("-", N_VPOINTS, 0);
                weights[c] = row;
                for (uint32_t k = 0; k < NSHIP; k++)
                    if (SHIP[k].wclass == c + 1u)
                        weights[c].label = SHIP[k].tag;
            }
            weights[VOICE_KINDS] = guard;
        }
        *valp = id == G_SAFETY ? &settings_safety : &settings_vpoints[id - G_WP5];
        return &weights[id - G_WP5];
    }
    if (pg->scope == SC_GLOBAL && id == G_BOOT) {
        boot_cell = settings_boot;                       /* (a copy: ui_input.c edit_param writes it back) */
        *valp = &boot_cell;
        return global_desc(id);
    }
    if (pg->scope == SC_GLOBAL && id == G_A4) {
        *valp = &tuning_a4;
        return global_desc(id);
    }
    if (pg->scope == SC_GLOBAL && id == G_BUDGET) {
        *valp = &settings_budget;
        return global_desc(id);
    }
    if (pg->scope == SC_GLOBAL && id == G_DRUMCH) {
        drumch_cell = settings_drumch;                   /* (as BOOT) */
        *valp = &drumch_cell;
        return global_desc(id);
    }
    if (pg->scope == SC_TRACK && id >= P_FXMIX && id <= P_FX3CHAR) {
        uint32_t base = P_FXTYPE + (id - P_FXTYPE) / FX_PARAMS * FX_PARAMS;
        if (TSEL->p[base] == FX_MIDIVERB && id - base >= 2u) { *valp = 0; return 0; }
    }
    if (pg->scope == SC_GLOBAL && id >= G_MFXTONE && id <= G_MFX3TONE &&    /* a DRIVE's KNOB 4: its SHAPE */
        fx_drive(song.g[G_MFXTYPE + (id - G_MFXTONE) * FX_PARAMS])) {
        *valp = &song.g[G_MFXTYPE + (id - G_MFXTONE) * FX_PARAMS];
        return &FX_SHAPE;
    }
    if (pg->scope == SC_TRACK && param_fx_tone(id) && fx_drive(TSEL->p[P_FXTYPE + (id - P_FXTONE) * FX_PARAMS])) {
        *valp = &TSEL->p[P_FXTYPE + (id - P_FXTONE) * FX_PARAMS];
        return &FX_SHAPE;
    }
    if ((pg->scope == SC_GLOBAL && id >= G_MFXTONE && id <= G_MFX3TONE &&    /* TONE: ROOM only */
         !fx_third_control(song.g[G_MFXTYPE + (id - G_MFXTONE) * FX_PARAMS])) ||
        (pg->scope == SC_TRACK && param_fx_tone(id) && !fx_third_control(TSEL->p[P_FXTYPE + (id - P_FXTONE) * FX_PARAMS]))) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_GLOBAL) {
        *valp = &song.g[id];
        return global_desc(id);
    }
    if (pg->graph == GR_MOD)                          /* SRC DST AMT of the slot shown */
        id += 3u * mod_ui_slot;
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}

/* Sound loads preserve the track mix, sequencing, scale and routing. */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_AMODE && i <= P_SGATE) ||
           i == P_CHRD || i == P_VOIC || i == P_STRUM || i == P_STROKE || i == P_MPCDEG || i == P_RECQ || i == P_TUNING || param_fx(i) || param_fx_send(i);
}
#include "sch_pages.c"     /* a synth's own pages (its schema): page_at, their columns, values, edits */
