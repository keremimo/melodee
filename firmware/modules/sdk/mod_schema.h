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
 *                    (what the patch is called: "CZ TONE"; its NAME screen's title), 0, crc (CRC-32 of the payload
 *                    after the head)
 *   params           nparam records of 28 bytes, ids ascending (mod_sch_param below, packed)
 *   pages            npage records of 24 bytes
 *   roles            nrole records of 6 bytes: role (MOD3_R_*), flags, id (a parameter's, or a page's index), label
 *                    (a string: what the role shows it as, MOD_SCH_NONE: the parameter's label)
 *   recipes          nrec records of 6 bytes (the sound designer's: question, answer, id, value)
 *   widget data      what a page's widget draws from (MOD3_W_*)
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
    MOD3_R_FEATURE,                              /* the sound designer's FEATURES (see the recipes) */
    MOD3_R_CC_GLIDE,                             /* MIDI CC 5 (portamento time) sets it */
    MOD3_R_COUNT
};
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
} mod_sch_param_src;
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
} mod_sch_src;

/* ---- the chunk as it is read */
typedef struct {
    uint16_t version, patch, nparam, npage, nrole, nrec, def, param, page, role, rec, wid, str, nstr;
    uint8_t store, mtag, flags, nlen;
    uint16_t abbr, wdef, mmap, name;
    uint8_t first, utag;
    uint16_t sound, pname, rsv4;
    uint32_t crc;
} mod_sch_head;
_Static_assert(sizeof(mod_sch_head) == MOD_SCH_HEAD, "SCHM head");
/* a parameter record (28 bytes): id, kind, fmt, min, max, def, off, shift, width, flags, 0, label, name, names,
 * unit, icon (string offsets, MOD_SCH_NONE: none; icon: a label whose picture it has, else its label's), 0 */
/* a page record (24 bytes): title, widget, flags, id[4], ninst, chip (the title's characters its short name leaves
 * out), istride[4] (a column's id step per instance), inames
 * (an offset: ninst names), wid (the widget data's offset in the widget bytes), section */

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
/* the SCHM payload of s into out (cap bytes): its length, 0 when it does not fit or s is malformed (ids not ascending,
 * a field outside the patch, a page naming an id it does not have, a name too long) */
static uint32_t mod_sch_write(const mod_sch_src *s, uint8_t *out, uint32_t cap)
{
    static uint8_t str[16384];
    uint32_t nstr = 0, i, k, o, v;
    uint32_t param = MOD_SCH_HEAD, page = param + s->nparam * MOD_SCH_PREC, role = page + s->npage * MOD_SCH_GREC;
    uint32_t rec = role + s->nrole * MOD_SCH_RREC, def = rec + s->nrec * MOD_SCH_XREC, wid = def + s->patch;
    uint32_t at = wid + s->nwid;
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
                      g->widget == MOD3_W_STORE && g->inames ? mod_sch_str(str, &nstr, sizeof str, g->inames[0]) : MOD_SCH_NONE;
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
    mod_sch_w16(out + 8, s->nrole);
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
