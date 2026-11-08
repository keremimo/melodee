/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A synth module's patch and its parameters, inside its file: the SCHM chunk (after the address words, as PRST:
 * module_presets.h). The firmware reads it where the file is, the module's code loaded or not: how many bytes the
 * part's patch has and what it starts as, every parameter by its stable id (where it is, its range and text, what it
 * is for), the pages that show them, and the roles (HOME's knobs, the SMK-37's banks, the MIDI controls, the sound
 * designer's recipes). Nothing here depends on the module ABI.
 *
 * A module describes it in C (mod_sch_src_t below, in its own source: firmware/modules/<name>/<name>_schema.h);
 * tools/mod_build.py writes the chunk from it (tools/mod_schema.c, mod_sch_write), host builds that compile the
 * engine in write the same bytes at power-on.
 *
 * MOD_SCH_TAG ("SCHM"), version 1, little endian, every offset from the payload's start:
 *   head (52 bytes)  version, patch (the patch's bytes, <= MOD3_PATCH_MAX), nparam, npage, nrole, nrec (recipe
 *                    steps), def (the patch it starts as: patch bytes), param, page, role, rec, wid (widget data),
 *                    str (strings), nstr, store (MOD_SCH_STORE_*: how Melodee 1.x stored it), mtag (Motion's tag:
 *                    0 none), flags (MOD_SCH_F_*), nlen, abbr (its short name: a string, the lists' tag), wdef (the
 *                    picture of its pages without one and of EDIT 1 / 2: an algorithm chart's widget data (MOD3_W_ALG),
 *                    MOD_SCH_NONE none), mmap (Motion
 *                    by bytes: patch bytes of each byte's highest value, 0 not recorded, then patch bytes of bits
 *                    that must be 0), name (the patch's own name: nlen bytes there), first (the preset a part
 *                    picking the synth starts from), utag (its user presets' tag letter in the lists, 0 'U'), sound (the patch's bytes that are the sound, 0: all), pname
 *                    (what the patch is called: "CZ TONE"; its NAME screen's title), design (the sound designer's
 *                    block: widget data, MOD_SCH_NONE none), crc (CRC-32 of the payload after the head)
 *   params           nparam records of 28 bytes, ids ascending (mod_sch_param below, packed)
 *   pages            npage records of 24 bytes
 *   roles            nrole records of 6 bytes: role (MOD3_R_*), flags, id (a parameter's, or a page's index), label
 *                    (a string: what the role shows it as, MOD_SCH_NONE: the parameter's label)
 *   recipes          nrec records of 6 bytes, the sound designer's answers in order: question (MOD3_Q_*, + MOD3_RQ_*
 *                    flags), the answers it is for (a bit each: MOD3_GM), id (a parameter's; MOD3_D_TRACK + a P_*
 *                    number: the part's own; MOD3_RQ_ROLE: a role, the parameter it names first), value
 *   widget data      what a page's widget draws from (MOD3_W_*); the designer block (below)
 *   strings          NUL-terminated; an enum's names one after another
 * A parameter's id never changes (Motion, MIDI learn, the editor and stored sounds name it). */
#pragma once
#include <stdint.h>

#define MOD_SCH_TAG 0x4D484353u                  /* "SCHM" */
#define MOD_SCH_VERSION 1u
#define MOD_SCH_HEAD 52u
#define MOD_SCH_PREC 28u
#define MOD_SCH_GREC 24u
#define MOD_SCH_RREC 6u
#define MOD_SCH_XREC 6u
#define MOD_SCH_NONE 0xFFFFu
#define MOD_SCH_NAME 12u                         /* a label's characters, at most (a page title's: 16) */

/* where a parameter is (kind) */
enum {
    MOD3_SK_FIELD,                               /* bits of the patch: off (byte), shift, width (1..16) */
    MOD3_SK_TRACK,                               /* the part's own p[off] (a P_* number: E1..E8, ATK ..) */
    MOD3_SK_CALL                                 /* the module's pget / pput (mod_engine_v3) */
};
/* a parameter's flags */
#define MOD3_SF_SIGNED 1u                        /* FIELD: two's complement in its width */
#define MOD3_SF_MOTION 2u                        /* Motion records it (its id < MOD3_MOTION_IDS) */
#define MOD3_SF_HIDDEN 4u                        /* no page shows it (a name's characters ..) */
#define MOD3_SF_FORMAT 8u                        /* its text is the module's (pformat) */
#define MOD3_SF_INVERT 16u                       /* more is less: a role turned up sets it down (rates) */
#define MOD3_SF_DIM 32u                          /* the module says when it has no effect (pactive) */
#define MOD3_SF_NOTIFY 64u                       /* an edit tells the module (patch_changed EDIT with its id) */
#define MOD3_SF_DESC 128u                        /* its descriptor now is the module's: desc(t, MOD3_DESC_ID + id) */
/* a parameter's flags2 */
#define MOD3_SF2_WHOLE 1u                        /* MOD3_F_ENUM: a name shown whole in a page's cell where it fits there
                                                  * (every name: its first five characters) */
#define MOD_SCH_F_SNAME 8u                       /* (head flags) a part's sound is called by its patch's own name */
#define MOD_SCH_F_NOEDIT 4u                      /* (head flags) no EDIT 1 / EDIT 2 pages (its E values are not a panel) */
#define MOD_SCH_F_PCT127 2u                      /* (head flags) Stage's cards show its 0..127 values as a share (%) */
#define MOD_SCH_F_MBYTES 1u                      /* (head flags) Motion records the patch's bytes an edit changed (mmap
                                                  * says which, and their ranges): event ids are byte offsets; else the
                                                  * MOD3_SF_MOTION parameters by their ids */
#define MOD_SCH_F_MLANES 16u                     /* (head flags, with MBYTES) Motion's lanes named per byte: after mmap's
                                                  * two runs, u16 the names' string offset (one after another), u8 their
                                                  * count, then a name's index per patch byte (0xFF: none); a lane's
                                                  * values the byte's, 0..255 */
#define MOD3_MOTION_IDS 144u                     /* Motion's native ids */
/* a page's flags */
#define MOD3_PG_ENV 1u                           /* the ENV button's pages: the synth's own envelopes (the track's ADSR
                                                  * hidden) */
#define MOD3_PG_LFO 2u                           /* the LFO button's first pages (the track's LFO after them) */
#define MOD3_PG_BANK 4u                          /* a knob bank only (the SMK-37): not in the pages */
#define MOD3_PG_INST 8u                          /* instanced: ninst copies, column c's id + k * istride[c], tabs inames */
#define MOD3_PG_NOMAP 16u                        /* left out of the sound designer's stages */
#define MOD3_PG_LIST 32u                         /* a list page: its rows its columns, then the next pages' marked INLIST */
#define MOD3_PG_INLIST 64u                       /* gathered into the list page before it (not in the SELECT round) */
#define MOD3_PG_LAST 128u                        /* its section after the others (Store, Tools) */
/* widgets (a page's picture under its knobs) */
enum {
    MOD3_W_NONE,
    MOD3_W_ENV8,                                 /* a 4-rate 4-level envelope (rates 0..99: slower is wider; held at
                                                  * L3, then R4 to L4): data = u16 ids[8] (rates, levels: the first
                                                  * instance's), i8 the ids' step per instance, u8 flags (1: a pitch
                                                  * envelope, levels around 50) */
    MOD3_W_ALG,                                  /* an FM algorithm chart: data = u16 alg id, u16 feedback id, u16 mode
                                                  * id, u16 the first operator's output level id, i8 its step per
                                                  * operator, u8 the mode's value that closes a loop at its end (below),
                                                  * u8 a track parameter whose 1..nalg pick the algorithm (0xFF none),
                                                  * u8 one added to the feedback (0xFF none), u8 hot[8]: what EDIT 1 / 2's
                                                  * E1..E8 light (MOD3_WA_*), u8 nalg, u8 nop (<= 8), routes[nalg][nop]
                                                  * (the order the operators run, the last operator first: bits 0-1 out
                                                  * (0 the voice, 1..3 a bus), 2 adds to the bus, 4-5 the bus it reads,
                                                  * 6+7 its own feedback, 7 alone a loop's end in that mode),
                                                  * cells[nalg][nop] (operator 1 first: column bits 0..2, row 4..6) */
    MOD3_W_DRAW,                                 /* the module's draw(widget = the page's data word) */
    MOD3_W_STORE,                                /* the user slots: column 1 the slot, 2..4 STORE, SEND, INIT; the data
                                                  * word MOD3_ST_*; its inames: the INIT message */
    MOD3_W_TOOLS                                 /* actions on the patch: its columns' ids are MOD3_A_* or the module's
                                                  * own (pput with v 1) */
};
enum { MOD3_WA_NONE, MOD3_WA_LABEL, MOD3_WA_LOOP, MOD3_WA_ROUTES, MOD3_WA_MODS, MOD3_WA_CARS };   /* W_ALG hot[] */
#define MOD3_ST_NAMED 1u                         /* STORE names the user preset by the patch's own (no NAME screen) */
#define MOD3_ST_NOUNDO 2u                        /* INIT takes no undo level (a new sound all the same) */
#define MOD3_ST_SENDMSG 4u                       /* inames [1] SEND's words when sent, [2] when it failed (host 3) */
/* the actions a TOOLS page's columns may name (ids >= 0xF0; else a parameter the module acts on: pput(id, 1)) */
enum { MOD3_A_NAME = 0xF0, MOD3_A_COMPARE = 0xF1 };
/* roles: what the firmware asks a synth for (role records: role, flags, id, label) */
enum {
    MOD3_R_NONE,
    MOD3_R_HOME1, MOD3_R_HOME2, MOD3_R_HOME3, MOD3_R_HOME4,      /* HOME's and the Stage cards' knobs */
    MOD3_R_CC_ATK, MOD3_R_CC_DEC, MOD3_R_CC_REL, /* MIDI CC 73 / 75 / 72 set these (several: all of them) */
    MOD3_R_CC_BRIGHT, MOD3_R_CC_RESO,            /* MIDI CC 74 / 71 */
    MOD3_R_CUTOFF, MOD3_R_RESO,                  /* Stage's filter picture */
    MOD3_R_ENV_A, MOD3_R_ENV_D, MOD3_R_ENV_S, MOD3_R_ENV_R,      /* Stage's envelope picture (the amplitude's) */
    MOD3_R_BANK_FILTER, MOD3_R_BANK_ENV, MOD3_R_BANK_LFO,        /* the SMK-37's knob banks (page indexes) */
    MOD3_R_FEATURE,                              /* (retired: the designer block holds the FEATURES) */
    MOD3_R_CC_GLIDE,                             /* MIDI CC 5 (portamento time) sets it */
    MOD3_R_SYSEX,                                /* the SysEx it answers (mod_engine_v3.sysex): id the widget data's
                                                  * offset of flags patterns, 4 bytes each: n (1..3), then the n bytes
                                                  * after F0 a frame of its begins with ({1, 0x43}: Yamaha's) */
    MOD3_R_CHECK,                                /* its patch as stored, checked where the module is not loaded (a store,
                                                  * a project, a user preset read): id the widget data's offset of flags
                                                  * MOD3_CK_* records (8 bytes each), label 0 */
    MOD3_R_PICK_BANK, MOD3_R_PICK_SLOT,          /* two of the part's own parameters (MOD3_RF_TRACK) pick its patch:
                                                  * slot 1.. of bank 0.. (bank b's slot s: the bank Melodee 1.x saved,
                                                  * else, for b < flags, factory preset b * slots + s; slot 0: the
                                                  * synth's starting patch); a picked patch is the part's (MOD3_PATCH_SET) */
    MOD3_R_COUNT
};
/* a MOD3_R_CHECK record (8 bytes): u16 off, u8 n, u8 stride, u8 mask, u8 flags, u8 lo, u8 hi. The n bytes from off,
 * stride apart (0: 1), each masked and shifted down by the mask's lowest bit, must be lo..hi (MOD3_CK_SIGNED: the
 * byte as int8 before the mask, 0xFF; MOD3_CK_PAIR: lo or hi, nothing between). Every record of the role must pass */
#define MOD_SCH_CREC 8u
#define MOD3_CK_SIGNED 1u
#define MOD3_CK_PAIR 2u
#define MOD3_RF_TRACK 1u                         /* the id is the part's own parameter (a P_* number: E1 ..) */
#define MOD3_RF_PIC_FILTER 4u                    /* HOME: turning it shows the filter picture */
#define MOD3_RF_PIC_ENV 8u                       /* HOME: .. the envelope picture */
#define MOD3_RF_RAW 32u                          /* CC: the controller's value itself (its range clamps it), not scaled */
#define MOD3_RF_INVERT 16u                       /* CC: more is less (a rate: the control turned up sets it down) */
/* how Melodee 1.x stored this patch (the formats keep their places; 0: in no old format) */
enum { MOD_SCH_STORE_NONE, MOD_SCH_STORE_VOICE, MOD_SCH_STORE_TONE, MOD_SCH_STORE_PROGRAM, MOD_SCH_STORE_KIT };

/* ---- the module's own description (C, in its source) */
typedef struct {
    uint16_t id;
    uint8_t kind, fmt;                           /* MOD3_SK_*, MOD3_F_* */
    int16_t min, max, def;
    uint16_t off;                                /* FIELD: the byte; TRACK: the P_* number */
    uint8_t shift, width;                        /* FIELD */
    uint8_t flags;                               /* MOD3_SF_* */
    const char *label;                           /* its column's (<= MOD_SCH_NAME) */
    const char *name;                            /* its long name (0: the label) */
    const char *const *names;                    /* MOD3_F_ENUM: max - min + 1 names */
    const char *unit;
    const char *icon;                            /* (0: its label's picture) */
    uint8_t flags2;                              /* MOD3_SF2_* */
} mod_sch_param_src;
/* ---- the sound designer (Melodee's tone wizard). START asks a question (MOD3_Q_GOAL: a goal, MOD3_G_*) and puts that
 * goal's recipe (the recipe records answering it, in order; a synth without any: the firmware's from its roles), then
 * FEATURES lists the synth's paths: each a row with its states (2: Off / On, or up to 4 named), read by a get program and
 * set by an on / off program (the state in MOD3_D_STATE); a dim control turned (pactive 0) asks the need rules which path
 * to switch on first. Programs are bytes: an expression (postfix: MOD3_D_VAL .. MOD3_D_STATE, ends MOD3_D_END) or
 * actions (MOD3_D_SET .. MOD3_D_SETCAR, ends MOD3_D_END; MOD3_D_IF n <expression> guards the n actions after it). An
 * id is a parameter's (sch_get / sch_put: its range), or MOD3_D_TRACK + a P_* number (the part's own, as it is). */
enum { MOD3_Q_GOAL = 1 };                        /* recipe questions */
enum { MOD3_G_FREE, MOD3_G_BASS, MOD3_G_PAD, MOD3_G_LEAD, MOD3_G_KEYS, MOD3_G_PLUCK, MOD3_G_FX, MOD3_G_COUNT };
#define MOD3_GM(g) (1u << MOD3_G_##g)            /* a recipe record's answers: MOD3_GM(PAD) | MOD3_GM(LEAD) */
#define MOD3_GM_ALL 0x7Eu                        /* (every goal: FREE puts no recipe) */
#define MOD3_RQ_NOTE 0x80u                       /* (a recipe record's question) not a value: the synth told (patch_changed
                                                  * EDIT, the id) */
#define MOD3_RQ_ROLE 0x40u                       /* (a recipe record's question) its id is a role (MOD3_R_*) */
enum { MOD3_D_END, MOD3_D_VAL, MOD3_D_NZ, MOD3_D_CONST, MOD3_D_EQ, MOD3_D_NE, MOD3_D_GT, MOD3_D_LE, MOD3_D_AND,
       MOD3_D_OR, MOD3_D_NOT, MOD3_D_ADD, MOD3_D_CAR, MOD3_D_ACT, MOD3_D_STATE,
       MOD3_D_SET = 0x20, MOD3_D_SETS, MOD3_D_AMT, MOD3_D_IF, MOD3_D_NOTE, MOD3_D_SETCAR };
/* expression: VAL id16 (its value), NZ id16 (value != 0), CONST i8, EQ NE GT LE AND OR ADD (two below), NOT, CAR op8
 * (operator op is a carrier in the patch's algorithm: the wdef chart), ACT id16 (pactive), STATE (the state set);
 * actions: SET id16 v16 (i16), SETS id16 (the state), AMT id16 v16 (on: the value kept unless 0, then v; off: 0),
 * IF n8 <expression>, NOTE id16 (patch_changed EDIT), SETCAR id16 op8 v8 w8 (v for a carrier, else w) */
#define MOD3_D_TRACK 0x8000u
#define MOD3_D_ID(id) (uint8_t)((id) & 255u), (uint8_t)((uint32_t)(id) >> 8)
#define D_VAL(id) MOD3_D_VAL, MOD3_D_ID(id)
#define D_NZ(id) MOD3_D_NZ, MOD3_D_ID(id)
#define D_CONST(v) MOD3_D_CONST, (uint8_t)(int8_t)(v)
#define D_SET(id, v) MOD3_D_SET, MOD3_D_ID(id), (uint8_t)((v) & 255), (uint8_t)(((v) >> 8) & 255)
#define D_SETS(id) MOD3_D_SETS, MOD3_D_ID(id)
#define D_AMT(id, v) MOD3_D_AMT, MOD3_D_ID(id), (uint8_t)((v) & 255), (uint8_t)(((v) >> 8) & 255)
#define D_IF(n) MOD3_D_IF, (uint8_t)(n)
#define D_NOTE(id) MOD3_D_NOTE, MOD3_D_ID(id)
#define D_SETCAR(id, op, v, w) MOD3_D_SETCAR, MOD3_D_ID(id), (uint8_t)(op), (uint8_t)(v), (uint8_t)(w)
/* a FEATURES row's flags: MOD3_FF_HOST a firmware path (inst: MOD3_HF_*: the part's own parameters); MOD3_FF_INST it
 * switches instance inst (an operator: its pages are shaped only while on); MOD3_FF_CARRIER its row says carrier /
 * modulator of operator inst (the wdef chart) */
#define MOD3_FF_HOST 1u
#define MOD3_FF_INST 2u
#define MOD3_FF_CARRIER 4u
enum { MOD3_HF_VOICE, MOD3_HF_GLIDE, MOD3_HF_SPREAD, MOD3_HF_PENV, MOD3_HF_VIB, MOD3_HF_WOBBLE, MOD3_HF_TREM,
       MOD3_HF_ENVF, MOD3_HF_COUNT };
/* a need rule: ids lo..hi, every step-th; the path fa (cond true or none), else fb; MOD3_DN_END: the control is past
 * its envelope's end. MOD3_DN_DIM only while the control does nothing (pactive 0). MOD3_DN_INST: an instanced page's
 * columns need path fa + the instance shown while that is off, but the column whose id there is lo + (int8)step *
 * the instance. MOD3_DN_DOWN (checked first; with MOD3_DN_INST on instanced pages only): turned down, a column of
 * these ids switches nothing on */
#define MOD3_DN_DIM 1u
#define MOD3_DN_INST 2u
#define MOD3_DN_DOWN 4u
#define MOD3_DN_END 0xFEu
#define MOD3_DN_NONE 0xFFu
/* a page's stage (the designer's pages: 3 bytes a page, stage | flags, u16 live id; 0: Source), MOD3_DS_SKIP not
 * among MAIN's pages, MOD3_DS_LIVE shaped only while its live parameter acts */
enum { MOD3_DS_DEFAULT, MOD3_DS_SOURCE, MOD3_DS_TONE, MOD3_DS_ENV, MOD3_DS_MOVE, MOD3_DS_PLAY };
#define MOD3_DS_SKIP 16u
#define MOD3_DS_LIVE 32u
/* the designer block's flags: the firmware's VOICE pages left out (MOD3_DSF_NO_VOICE), EDIT 1 / 2 left out
 * (MOD3_DSF_NO_EDIT) or in ALL only (MOD3_DSF_EDIT_ALL), the track's LFO pages in ALL only (MOD3_DSF_OWN_LFO: it has
 * its own), its pages' titles as the header's chip (MOD3_DSF_TITLE_CHIP), the designer's engine when the track's
 * cannot be designed (MOD3_DSF_FIRST) */
#define MOD3_DSF_NO_VOICE 1u
#define MOD3_DSF_NO_EDIT 2u
#define MOD3_DSF_EDIT_ALL 4u
#define MOD3_DSF_OWN_LFO 8u
#define MOD3_DSF_TITLE_CHIP 16u
#define MOD3_DSF_FIRST 32u
typedef struct {
    const char *word;                            /* the row */
    const char *const *names;                    /* states > 2: their names (else Off / On) */
    const char *eword;                           /* what a need switching it on says ("LINE 2 ON"; 0: the word) */
    uint8_t states, flags, inst, estate;         /* estate: the state a need sets */
    const uint8_t *get, *on, *off;               /* programs (off: s 0, on: the rest) */
} mod_sch_feat_src;
typedef struct { uint16_t lo, hi; uint8_t step, kind, fa, fb; const uint8_t *cond; } mod_sch_need_src;
typedef struct {
    uint8_t flags;                               /* MOD3_DSF_* */
    const mod_sch_feat_src *feat;
    uint8_t nfeat;
    const mod_sch_need_src *need;
    uint8_t nneed;
    const uint8_t *silent;                       /* an expression: the build cannot sound (0: never) */
    const char *silent_msg;
    const uint8_t *pages;                        /* per page of the schema: stage | MOD3_DS_* flags, live id (u16) */
} mod_sch_design_src;
typedef struct {
    const char *title;
    uint8_t widget, flags;                       /* MOD3_W_*, MOD3_PG_* */
    uint16_t id[4];                              /* MOD_SCH_NONE: an empty column */
    uint8_t ninst;                               /* MOD3_PG_INST */
    int8_t istride[4];
    const char *const *inames;                   /* the instances' tabs (MOD3_W_STORE: [0] its INIT message) */
    uint16_t wid;                                /* the widget's data: an offset into the source's widget bytes */
    const char *section;                         /* its section (the tabs, the map; pages of one name together) */
} mod_sch_page_src;
typedef struct { uint8_t role, flags; uint16_t id; const char *label; } mod_sch_role_src;
typedef struct { uint8_t q, a; uint16_t id; int16_t v; } mod_sch_rec_src;
typedef struct {
    uint16_t patch;
    const uint8_t *def;
    void (*def_make)(uint8_t *patch);            /* (instead of def: its bytes made) */
    uint8_t store, mtag, flags;
    const char *abbr;                            /* (0: its name) */
    uint16_t name;                               /* the patch's own name: its place (nlen bytes, ASCII, spaces past it) */
    uint8_t nlen;                                /* (0: none) */
    uint8_t first;                               /* the preset a part picking the synth starts from (0: its first) */
    char utag;                                   /* its user presets' tag letter in the lists ("P012"; 0: 'U') */
    const char *pname;                           /* what its patch is called ("CZ TONE": NAME's title, a name's
                                                  * placeholder), 0: "SOUND" */
    uint16_t sound;                              /* the patch's bytes that are the sound (a STORE page's INIT writes
                                                  * them, the rest are kept settings: FM6's functions); 0: all */
    uint16_t wdef, mmap;                         /* (widget data offsets; MOD_SCH_NONE: none) */
    const mod_sch_param_src *param;
    uint16_t nparam;
    const mod_sch_page_src *page;
    uint16_t npage;
    const mod_sch_role_src *role;
    uint16_t nrole;
    const mod_sch_rec_src *rec;
    uint16_t nrec;
    const uint8_t *wid;                          /* widget data */
    uint16_t nwid;
    const char *const *mlanes;                   /* MOD_SCH_F_MLANES: the lanes' names (the widget data names their
                                                  * indexes per byte), nmlanes of them */
    uint8_t nmlanes;
    const char *const *strip;                    /* title prefixes a page's short name leaves out ("P5 FLT ENV" ->
                                                  * "FLT ENV": the first that leaves something), 0-terminated; 0 none */
    const mod_sch_design_src *design;            /* the sound designer's FEATURES, needs, pages (0: the firmware's) */
    const uint8_t *sysex;                        /* the SysEx it answers: nsysex patterns of 4 bytes (MOD3_R_SYSEX: the
                                                  * writer puts them after the designer block, a role record naming them) */
    uint8_t nsysex;
    const uint8_t *check;                        /* its stored patch's checks: ncheck MOD3_R_CHECK records (8 bytes) */
    uint8_t ncheck;
} mod_sch_src;

/* ---- the chunk as it is read */
typedef struct {
    uint16_t version, patch, nparam, npage, nrole, nrec, def, param, page, role, rec, wid, str, nstr;
    uint8_t store, mtag, flags, nlen;
    uint16_t abbr, wdef, mmap, name;
    uint8_t first, utag;
    uint16_t sound, pname, design;
    uint32_t crc;
} mod_sch_head;
_Static_assert(sizeof(mod_sch_head) == MOD_SCH_HEAD, "SCHM head");
/* a parameter record (28 bytes): id, kind, fmt, min, max, def, off, shift, width, flags, flags2, label, name, names,
 * unit, icon (string offsets, MOD_SCH_NONE: none; icon: a label whose picture it has, else its label's), 0 */
/* a page record (24 bytes): title, widget, flags, id[4], ninst, chip (the title's characters its short name leaves
 * out), istride[4] (a column's id step per instance), inames
 * (an offset: ninst names), wid (the widget data's offset in the widget bytes), section */

/* a designer program's op at p: its bytes (MOD3_D_IF: its two; its expression follows) */
static uint32_t mod_sch_dop(uint32_t op)
{
    switch (op) {
    case MOD3_D_VAL: case MOD3_D_NZ: case MOD3_D_ACT: case MOD3_D_SETS: case MOD3_D_NOTE: return 3;
    case MOD3_D_CONST: case MOD3_D_CAR: case MOD3_D_IF: return 2;
    case MOD3_D_SET: case MOD3_D_AMT: return 5;
    case MOD3_D_SETCAR: return 6;
    default: return 1;
    }
}
#define MOD_SCH_DHEAD 10u                        /* the designer block: head, then features 16 B, needs 10 B, pages 3 B */
#define MOD_SCH_DFEAT 16u
#define MOD_SCH_DNEED 10u

#ifndef MOD_SCH_NO_WRITER
static uint32_t mod_sch_crc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (uint32_t k = 0; k < 8u; k++)
            c = c >> 1 ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1u));
    }
    return ~c;
}
static void mod_sch_w16(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
/* the strings so far, s added (once: the same text is the same offset); MOD_SCH_NONE for 0, ~0 when full */
static uint32_t mod_sch_str(uint8_t *str, uint32_t *n, uint32_t cap, const char *s)
{
    uint32_t len, i;
    if (!s)
        return MOD_SCH_NONE;
    for (len = 0; s[len]; len++)
        ;
    for (i = 0; i + len < *n; i++) {             /* (an earlier string, or the end of one) */
        uint32_t j = 0;
        while (j <= len && str[i + j] == (uint8_t)s[j])
            j++;
        if (j > len)
            return i;
    }
    if (*n + len + 1u > cap)
        return 0xFFFFFFFFu;
    for (i = 0; i <= len; i++)
        str[*n + i] = (uint8_t)s[i];
    *n += len + 1u;
    return *n - len - 1u;
}
/* names k of a list as one run of strings (an enum's: max - min + 1), its first's offset */
static uint32_t mod_sch_strs(uint8_t *str, uint32_t *n, uint32_t cap, const char *const *names, uint32_t k)
{
    uint32_t at, len, i, j;
    if (!names)
        return MOD_SCH_NONE;
    for (at = 0; at < *n; at++) {                /* (the same run already there) */
        uint32_t p = at;
        for (i = 0; i < k; i++) {
            for (j = 0; names[i][j] && p + j < *n && str[p + j] == (uint8_t)names[i][j]; j++)
                ;
            if (names[i][j] || p + j >= *n || str[p + j])
                break;
            p += j + 1u;
        }
        if (i == k)
            return at;
    }
    at = *n;
    for (i = 0; i < k; i++) {
        for (len = 0; names[i][len]; len++)
            ;
        if (*n + len + 1u > cap)
            return 0xFFFFFFFFu;
        for (j = 0; j <= len; j++)
            str[*n + j] = (uint8_t)names[i][j];
        *n += len + 1u;
    }
    return at;
}
/* program p's bytes (to its last END; an IF's expression and its END inside), 0 malformed; each id it names one s has */
static uint32_t mod_sch_dlen(const mod_sch_src *s, const uint8_t *p)
{
    uint32_t n = 0, depth = 0, k;
    if (!p)
        return 0;
    for (;;) {
        uint32_t op = p[n], m = mod_sch_dop(op);
        if (n > 4096u)
            return 0;
        if (m == 3u || op == MOD3_D_SET || op == MOD3_D_AMT || op == MOD3_D_SETCAR) {
            uint32_t id = p[n + 1] | (uint32_t)p[n + 2] << 8;
            if (!(id & MOD3_D_TRACK)) {
                for (k = 0; k < s->nparam && s->param[k].id != id; k++)
                    ;
                if (k == s->nparam)
                    return 0;
            }
        }
        n += m;
        if (op == MOD3_D_IF)
            depth++;                             /* (its expression's END closes it) */
        else if (op == MOD3_D_END) {
            if (!depth)
                return n;
            depth--;
        }
    }
}
static uint32_t mod_sch_dsize(const mod_sch_src *s)  /* the designer block's bytes, 0 none or malformed */
{
    const mod_sch_design_src *d = s->design;
    uint32_t n, i, k, bad = 0;
    if (!d)
        return 0;
    n = MOD_SCH_DHEAD + d->nfeat * MOD_SCH_DFEAT + d->nneed * MOD_SCH_DNEED + s->npage * 3u;
    if (d->silent)
        bad |= !(k = mod_sch_dlen(s, d->silent)), n += k;
    for (i = 0; i < d->nfeat; i++)
        if (!(d->feat[i].flags & MOD3_FF_HOST)) {
            bad |= !(k = mod_sch_dlen(s, d->feat[i].get)), n += k;
            bad |= !(k = mod_sch_dlen(s, d->feat[i].on)), n += k;
            bad |= !(k = mod_sch_dlen(s, d->feat[i].off)), n += k;
        }
    for (i = 0; i < d->nneed; i++)
        if (d->need[i].cond)
            bad |= !(k = mod_sch_dlen(s, d->need[i].cond)), n += k;
    return bad ? 0u : n;
}
static uint32_t mod_sch_dput(const mod_sch_src *s, uint8_t *b, uint32_t *at, const uint8_t *p)   /* its offset */
{
    uint32_t n = p ? mod_sch_dlen(s, p) : 0, o = *at;
    if (!n)
        return 0;
    for (uint32_t i = 0; i < n; i++)
        b[o + i] = p[i];
    *at += n;
    return o;
}
/* the SCHM payload of s into out (cap bytes): its length, 0 when it does not fit or s is malformed (ids not ascending,
 * a field outside the patch, a page naming an id it does not have, a name too long) */
static uint32_t mod_sch_write(const mod_sch_src *s, uint8_t *out, uint32_t cap)
{
    static uint8_t str[16384];
    uint32_t nstr = 0, i, k, o, v;
    uint32_t param = MOD_SCH_HEAD, page = param + s->nparam * MOD_SCH_PREC, role = page + s->npage * MOD_SCH_GREC;
    uint32_t nrole = s->nrole + (s->nsysex ? 1u : 0u) + (s->ncheck ? 1u : 0u);   /* (+ the SysEx and checks' roles) */
    uint32_t rec = role + nrole * MOD_SCH_RREC, def = rec + s->nrec * MOD_SCH_XREC, wid = def + s->patch;
    uint32_t dsize = mod_sch_dsize(s), xat = s->nwid + dsize, cat = xat + 4u * s->nsysex;
    uint32_t at = wid + cat + MOD_SCH_CREC * s->ncheck;
    if (s->patch > 512u || at > cap || (s->wdef != MOD_SCH_NONE && s->wdef >= s->nwid) ||
        (s->mmap != MOD_SCH_NONE && (uint32_t)s->mmap + 2u * s->patch > s->nwid))
        return 0;
    for (i = 0; i < at; i++)
        out[i] = 0;
    for (i = 0; i < s->nparam; i++) {
        const mod_sch_param_src *p = &s->param[i];
        uint8_t *b = out + param + i * MOD_SCH_PREC;
        uint32_t lab, nam, nms, unit;
        if ((i && p->id <= s->param[i - 1u].id) || (p->fmt == MOD3_F_ENUM && p->min != 0))
            return 0;                            /* (ids ascending; an enum's values from 0) */
        if (p->kind == MOD3_SK_FIELD && (!p->width || p->width > 16u || p->shift > 7u ||
                                         p->off + (p->shift + p->width + 7u) / 8u > s->patch))
            return 0;
        if ((p->flags & MOD3_SF_MOTION) && p->id >= MOD3_MOTION_IDS)
            return 0;
        for (k = 0; p->label && p->label[k]; k++)
            ;
        if (k > MOD_SCH_NAME)
            return 0;
        lab = mod_sch_str(str, &nstr, sizeof str, p->label);
        nam = mod_sch_str(str, &nstr, sizeof str, p->name);
        nms = p->fmt == MOD3_F_ENUM ? mod_sch_strs(str, &nstr, sizeof str, p->names, (uint32_t)(p->max - p->min + 1)) : MOD_SCH_NONE;
        unit = mod_sch_str(str, &nstr, sizeof str, p->unit);
        k = mod_sch_str(str, &nstr, sizeof str, p->icon);
        if ((lab | nam | nms | unit | k) > 0xFFFFu)
            return 0;
        mod_sch_w16(b + 24, k);
        mod_sch_w16(b, p->id);
        b[2] = p->kind;
        b[3] = p->fmt;
        mod_sch_w16(b + 4, (uint16_t)p->min);
        mod_sch_w16(b + 6, (uint16_t)p->max);
        mod_sch_w16(b + 8, (uint16_t)p->def);
        mod_sch_w16(b + 10, p->off);
        b[12] = p->shift;
        b[13] = p->width;
        b[14] = p->flags;
        b[15] = p->flags2;
        mod_sch_w16(b + 16, lab);
        mod_sch_w16(b + 18, nam);
        mod_sch_w16(b + 20, nms);
        mod_sch_w16(b + 22, unit);
    }
    for (i = 0; i < s->npage; i++) {
        const mod_sch_page_src *g = &s->page[i];
        uint8_t *b = out + page + i * MOD_SCH_GREC;
        uint32_t t = mod_sch_str(str, &nstr, sizeof str, g->title);
        uint32_t in = (g->flags & MOD3_PG_INST) ? mod_sch_strs(str, &nstr, sizeof str, g->inames, g->ninst) :
                      g->widget == MOD3_W_STORE && g->inames ? (g->wid & MOD3_ST_SENDMSG ? mod_sch_strs(str, &nstr, sizeof str, g->inames, 3) :
                      mod_sch_str(str, &nstr, sizeof str, g->inames[0])) : MOD_SCH_NONE;
        uint32_t sec = mod_sch_str(str, &nstr, sizeof str, g->section);
        if ((t | in | sec) > 0xFFFFu || ((g->widget == MOD3_W_ENV8 || g->widget == MOD3_W_ALG) && g->wid >= s->nwid))
            return 0;
        for (k = 0; g->title && g->title[k]; k++)
            ;
        if (k > MOD_SCH_NAME)
            return 0;
        for (k = 0; k < 4u; k++) {               /* (each column an id the schema has, every instance's too) */
            uint32_t n = (g->flags & MOD3_PG_INST) ? g->ninst : 1u, j, q;
            if (g->id[k] == MOD_SCH_NONE || g->widget == MOD3_W_STORE || (g->widget == MOD3_W_TOOLS && g->id[k] >= 0xF0u))
                continue;
            if (g->id[k] > 254u)                 /* (a page names ids the firmware's pages hold in a byte) */
                return 0;
            for (j = 0; j < n; j++) {
                v = (uint32_t)((int32_t)g->id[k] + (int32_t)j * g->istride[k]);
                for (q = 0; q < s->nparam && s->param[q].id != v; q++)
                    ;
                if (q == s->nparam)
                    return 0;
            }
        }
        mod_sch_w16(b, t);
        b[2] = g->widget;
        b[3] = g->flags;
        for (k = 0; k < 4u; k++)
            mod_sch_w16(b + 4 + 2 * k, g->id[k]);
        b[12] = g->ninst;
        for (k = 0; s->strip && s->strip[k] && g->title; k++) {   /* (the short name: the title past a prefix) */
            uint32_t n = 0;
            while (s->strip[k][n] && s->strip[k][n] == g->title[n])
                n++;
            if (!s->strip[k][n] && g->title[n]) {
                b[13] = (uint8_t)n;
                break;
            }
        }
        for (k = 0; k < 4u; k++)
            b[14 + k] = (uint8_t)g->istride[k];
        mod_sch_w16(b + 18, in);
        mod_sch_w16(b + 20, g->wid);
        mod_sch_w16(b + 22, sec);
    }
    for (i = 0; i < s->nrole; i++) {
        uint8_t *b = out + role + i * MOD_SCH_RREC;
        uint32_t lab = mod_sch_str(str, &nstr, sizeof str, s->role[i].label);
        if (lab > 0xFFFFu)
            return 0;
        b[0] = s->role[i].role;
        b[1] = s->role[i].flags;
        mod_sch_w16(b + 2, s->role[i].id);
        mod_sch_w16(b + 4, lab);
    }
    for (i = 0; i < s->nsysex; i++) {            /* (patterns: 1..3 bytes after F0, 7-bit) */
        const uint8_t *q = s->sysex + 4u * i;
        if (!q[0] || q[0] > 3u || (q[1] | (q[0] > 1u ? q[2] : 0u) | (q[0] > 2u ? q[3] : 0u)) > 127u)
            return 0;
    }
    for (i = 0; i < s->ncheck; i++) {            /* (records inside the patch, a mask each) */
        const uint8_t *q = s->check + MOD_SCH_CREC * i;
        uint32_t off = q[0] | (uint32_t)q[1] << 8, st = q[3] ? q[3] : 1u;
        if (!q[2] || !q[4] || off + (q[2] - 1u) * st >= s->patch)
            return 0;
    }
    if (s->nsysex) {
        uint8_t *b = out + role + s->nrole * MOD_SCH_RREC;
        b[0] = MOD3_R_SYSEX;
        b[1] = s->nsysex;
        mod_sch_w16(b + 2, xat);
        mod_sch_w16(b + 4, MOD_SCH_NONE);
        for (i = 0; i < 4u * s->nsysex; i++)
            out[wid + xat + i] = s->sysex[i];
    }
    if (s->ncheck) {
        uint8_t *b = out + role + (s->nrole + (s->nsysex ? 1u : 0u)) * MOD_SCH_RREC;
        b[0] = MOD3_R_CHECK;
        b[1] = s->ncheck;
        mod_sch_w16(b + 2, cat);
        mod_sch_w16(b + 4, MOD_SCH_NONE);
        for (i = 0; i < MOD_SCH_CREC * s->ncheck; i++)
            out[wid + cat + i] = s->check[i];
    }
    v = mod_sch_str(str, &nstr, sizeof str, s->abbr);
    if (v > 0xFFFFu)
        return 0;
    mod_sch_w16(out + 32, v);
    v = mod_sch_str(str, &nstr, sizeof str, s->pname);
    if (v > 0xFFFFu)
        return 0;
    mod_sch_w16(out + 44, v);
    for (i = 0; i < s->nrec; i++) {
        uint8_t *b = out + rec + i * MOD_SCH_XREC;
        if (!(s->rec[i].q & (MOD3_RQ_NOTE | MOD3_RQ_ROLE)) && !(s->rec[i].id & MOD3_D_TRACK)) {
            for (k = 0; k < s->nparam && s->param[k].id != s->rec[i].id; k++)
                ;
            if (k == s->nparam)
                return 0;                        /* (a recipe's value for a parameter it does not have) */
        }
        b[0] = s->rec[i].q;
        b[1] = s->rec[i].a;
        mod_sch_w16(b + 2, s->rec[i].id);
        mod_sch_w16(b + 4, (uint16_t)s->rec[i].v);
    }
    if (s->def_make)
        s->def_make(out + def);
    else
        for (i = 0; i < s->patch; i++)
            out[def + i] = s->def ? s->def[i] : 0;
    for (i = 0; i < s->nwid; i++)
        out[wid + i] = s->wid[i];
    if (s->design) {                             /* the designer block (widget data after the source's) */
        const mod_sch_design_src *d = s->design;
        uint8_t *b = out + wid + s->nwid;
        uint32_t fo = MOD_SCH_DHEAD, no = fo + d->nfeat * MOD_SCH_DFEAT, po = no + d->nneed * MOD_SCH_DNEED;
        uint32_t c = po + s->npage * 3u;
        if (!dsize || d->nfeat > 0x3Fu)
            return 0;
        b[0] = 1;
        b[1] = d->flags;
        b[2] = d->nfeat;
        b[3] = d->nneed;
        b[4] = (uint8_t)s->npage;
        mod_sch_w16(b + 6, d->silent ? mod_sch_dput(s, b, &c, d->silent) : 0u);
        v = mod_sch_str(str, &nstr, sizeof str, d->silent_msg);
        if (v > 0xFFFFu)
            return 0;
        mod_sch_w16(b + 8, v);
        for (i = 0; i < d->nfeat; i++) {
            const mod_sch_feat_src *f = &d->feat[i];
            uint8_t *e = b + fo + i * MOD_SCH_DFEAT;
            uint32_t w = mod_sch_str(str, &nstr, sizeof str, f->word), x = mod_sch_str(str, &nstr, sizeof str, f->eword);
            uint32_t nm = f->names && f->states > 2u ? mod_sch_strs(str, &nstr, sizeof str, f->names, f->states) : MOD_SCH_NONE;
            if ((w | x | nm) > 0xFFFFu || (!(f->flags & MOD3_FF_HOST) && (!f->get || !f->on || !f->off)))
                return 0;
            mod_sch_w16(e, w);
            mod_sch_w16(e + 2, nm);
            mod_sch_w16(e + 4, x);
            e[6] = f->states;
            e[7] = f->flags;
            e[8] = f->inst;
            e[9] = f->estate;
            if (!(f->flags & MOD3_FF_HOST)) {
                mod_sch_w16(e + 10, mod_sch_dput(s, b, &c, f->get));
                mod_sch_w16(e + 12, mod_sch_dput(s, b, &c, f->on));
                mod_sch_w16(e + 14, mod_sch_dput(s, b, &c, f->off));
            }
        }
        for (i = 0; i < d->nneed; i++) {
            const mod_sch_need_src *q = &d->need[i];
            uint8_t *e = b + no + i * MOD_SCH_DNEED;
            mod_sch_w16(e, q->lo);
            mod_sch_w16(e + 2, q->hi);
            e[4] = q->step;
            e[5] = q->kind;
            e[6] = q->fa;
            e[7] = q->fb;
            mod_sch_w16(e + 8, q->cond ? mod_sch_dput(s, b, &c, q->cond) : 0u);
        }
        for (i = 0; i < s->npage; i++) {
            const uint8_t *q = d->pages ? d->pages + i * 3u : 0;
            b[po + i * 3u] = q ? q[0] : 0u;
            mod_sch_w16(b + po + i * 3u + 1u, q && (q[0] & MOD3_DS_LIVE) ? (uint32_t)(q[1] | q[2] << 8) : MOD_SCH_NONE);
        }
        if (c != dsize)
            return 0;
    }
    if (s->flags & MOD_SCH_F_MLANES) {           /* (Motion's lane names: their offset into the widget data) */
        uint32_t at2 = wid + s->mmap + 2u * s->patch;
        if (!(s->flags & MOD_SCH_F_MBYTES) || s->mmap == MOD_SCH_NONE || !s->mlanes ||
            s->mmap + 3u * s->patch + 3u > s->nwid || out[at2 + 2u] != s->nmlanes)
            return 0;
        v = mod_sch_strs(str, &nstr, sizeof str, s->mlanes, s->nmlanes);
        if (v > 0xFFFFu)
            return 0;
        mod_sch_w16(out + at2, v);
    }
    if (at + nstr > cap)
        return 0;
    for (i = 0; i < nstr; i++)
        out[at + i] = str[i];
    o = 0;
    mod_sch_w16(out + o, MOD_SCH_VERSION);
    mod_sch_w16(out + 2, s->patch);
    mod_sch_w16(out + 4, s->nparam);
    mod_sch_w16(out + 6, s->npage);
    mod_sch_w16(out + 8, nrole);
    mod_sch_w16(out + 10, s->nrec);
    mod_sch_w16(out + 12, def);
    mod_sch_w16(out + 14, param);
    mod_sch_w16(out + 16, page);
    mod_sch_w16(out + 18, role);
    mod_sch_w16(out + 20, rec);
    mod_sch_w16(out + 22, wid);
    mod_sch_w16(out + 24, at);
    mod_sch_w16(out + 26, nstr);
    out[28] = s->store;
    out[29] = s->mtag;
    out[30] = s->flags;
    out[31] = s->nlen;
    mod_sch_w16(out + 34, s->wdef);
    mod_sch_w16(out + 36, s->mmap);
    mod_sch_w16(out + 38, s->name);
    out[40] = s->first;
    out[41] = (uint8_t)s->utag;
    mod_sch_w16(out + 42, s->sound);
    mod_sch_w16(out + 46, s->design ? s->nwid : MOD_SCH_NONE);
    if (at + nstr > 0xFFFFu)
        return 0;
    v = mod_sch_crc(out + MOD_SCH_HEAD, at + nstr - MOD_SCH_HEAD);
    out[48] = (uint8_t)v;
    out[49] = (uint8_t)(v >> 8);
    out[50] = (uint8_t)(v >> 16);
    out[51] = (uint8_t)(v >> 24);
    return at + nstr;
}
#endif
