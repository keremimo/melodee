/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* TEMPORARY, until phase 4 (SysEx, the editor, Studio) moves this code into the synth modules or makes it generic: the
 * shipped synths' patch layouts (the modules' own headers) and a few accessors over the generic patch (patch.c), for
 * the code phase 4 replaces only: the DX7 / CZ / Prophet SysEx and their banks (fm6_voices.c, fm6_store.c, cz_tones.c,
 * cz_bank.c, cz_store.c, prophet_programs.c, prophet_user.c, native_presets.c, sysex_library.c), the editor's synth
 * commands (editor_fm6.c, editor_cz.c, editor_prophet.c, editor_library.c, editor.c's FM6 ENGINE), usb.c's routing.
 * Nothing else includes the synths' headers. A track's patch is written here only while its engine is that synth's
 * (or together with the engine: patch_set). Included by engines.c after patch.c */
#pragma once
#include "../modules/fm6/fm6_patch.h"
#include "../modules/cz/cz_tone.h"
#include "../modules/prophet/p5_patch.h"
#include "../modules/drum/drum_patch.h"

/* FM6: the voice of track k unpacked (VCED: 155 bytes + 1), the functions, writes as the DX7 SysEx makes them */
static void fm6_voice_of(uint32_t k, uint8_t *v) { fm6_unpack(patch_of(k), v); }
static uint8_t *fm6_fn_of(uint32_t k) { return patch_of(k) + FM6_B_FN; }
/* track tr's voice = v (155 bytes): load, another voice (a DX7 program change: the notes stop), else an edit */
static void fm6_put_patch(uint32_t tr, const uint8_t *v, int load)
{
    uint8_t b[FM6_PACKED];
    uint8_t s[FP_SIZE + 1u];
    if (trk[tr % NTRK].eng_req != ENGI_FM6)
        return;                                  /* (another engine's track: no FM6 voice there) */
    memcpy(s, v, FP_SIZE);
    fm6_sanitize(s);
    fm6_pack(s, b);
    patch_set(tr % NTRK, ENGI_FM6, b, FM6_PACKED, load ? MOD3_PATCH_NEW : MOD3_PATCH_EDIT);
}
/* .. the module decides: a load when its name or TRANSPOSE is another (a patch sent whole, an undo) */
static void fm6_set_patch(uint32_t tr, const uint8_t *v)
{
    uint8_t b[FM6_PACKED];
    uint8_t s[FP_SIZE + 1u];
    if (trk[tr % NTRK].eng_req != ENGI_FM6)
        return;
    memcpy(s, v, FP_SIZE);
    fm6_sanitize(s);
    fm6_pack(s, b);
    patch_set(tr % NTRK, ENGI_FM6, b, FM6_PACKED, MOD3_PATCH_SET);
}
static void fm6_fn_set(uint32_t tr, uint32_t k, int32_t v)
{
    if (tr < NTRK && k < FM6_NFN && patch_eng[tr < NPART ? tr : NPART] == ENGI_FM6) {
        fm6_fn_of(tr)[k] = (uint8_t)clamp(v, 0, FM6_FNMAX[k]);
        patch_note(tr, MOD3_PATCH_EDIT, 0xFFFFu);
    }
}
static void fm6_on_set(uint32_t tr, uint32_t on)
{
    if (tr < NTRK && patch_eng[tr < NPART ? tr : NPART] == ENGI_FM6) {
        patch_of(tr)[FM6_B_ON] = (uint8_t)(on & FM6_ON_ALL);
        patch_note(tr, MOD3_PATCH_EDIT, 0xFFFFu);
    }
}
static uint32_t fm6_on_of(uint32_t tr) { return patch_of(tr)[FM6_B_ON]; }

/* CZ-1: track k's tone; the Prophet: its program (main loop) */
static cz_patch_t *cz_patch_of(uint32_t k) { return (cz_patch_t *)(void *)patch_of(k); }
static p5_patch_t *p5_prog(uint32_t k) { return (p5_patch_t *)(void *)patch_of(k); }

/* TEMPORARY, until phase 5 (the pads): DRUM SOUND / DRUM MIX, the firmware's pages of the kit's offsets, read track k's */
static drum_patch_t *drum_patch_of(uint32_t k) { return (drum_patch_t *)(void *)patch_of(k); }
/* DRUM's patch as user preset records and the editor's library carry it (core.h kit_wire_*) */
#define DRUM_WIRE_BYTES KIT_WIRE_BYTES
static int drum_patch_valid(const drum_patch_t *p)
{
    for(uint32_t i=0;i<16;i++)if(p->c[i][0]<-24 || p->c[i][0]>24 || p->c[i][1]<-64 || p->c[i][1]>63 || p->c[i][2]<-64 || p->c[i][2]>63 || p->c[i][3]<0)return 0;
    return 1;
}
static void drum_patch_pack(uint8_t *b, const drum_patch_t *p) { kit_wire_pack(b, (const uint8_t *)(const void *)p); }
static int drum_patch_unpack(drum_patch_t *p, const uint8_t *b)
{
    kit_wire_unpack((uint8_t *)(void *)p, b);
    return drum_patch_valid(p);
}

/* TEMPORARY (phase 4 / phase 6: the stored formats' own checks, by layout: module_schema.h MOD_SCH_STORE_*): a patch's
 * bytes in a 1.x store as Melodee 1.x accepted them (a DX7 VMEM voice: 7-bit bytes; a CZ-1 tone, a Prophet program,
 * a kit's offsets: their checks); an unknown layout: the module checks its own when they load */
static int p4_store_valid(uint32_t tag, const uint8_t *b)
{
    switch (tag) {
    case MOD_SCH_STORE_VOICE:
        for (uint32_t i = 0; i < FM6_PACKED; i++)
            if (b[i] > 127u)
                return 0;
        return 1;
    case MOD_SCH_STORE_TONE: return cz_patch_valid(b);
    case MOD_SCH_STORE_PROGRAM: { p5_patch_t p; memcpy(&p, b, sizeof p); return p5_patch_valid(&p); }
    case MOD_SCH_STORE_KIT: { drum_patch_t p; memcpy(&p, b, sizeof p); return drum_patch_valid(&p); }
    default: return 1;
    }
}
/* .. a VOICE's settings as a project's bank holds them (16 bytes: FM6's functions) */
static int p4_voice_fn_valid(const uint8_t *f) { return fm6_fn_ok(f); }
/* .. a VOICE as a song sound's record holds it (the 155-byte voice, the switches): every value in its range */
static int p4_voice_valid(const uint8_t *v, uint32_t on)
{
    uint8_t s[FP_SIZE + 1u];
    memcpy(s, v, FP_SIZE);
    fm6_sanitize(s);
    return on <= FM6_ON_ALL && !memcmp(s, v, FP_SIZE);
}
/* the CZ-1's bank picks and the STORE pages' SEND (the DX7 / Prophet SysEx out): this code's until phase 4 */
static void cz_track_accept(track_t *t);           /* cz_tones.c */
static void p4_patch_heard(track_t *t) { cz_track_accept(t); }   /* (a whole patch written: patch_note) */
static void p5_send(uint32_t tr);                  /* prophet_user.c */
static void fm6_send(void);                        /* fm6_store.c */
static void p4_send(uint32_t e, uint32_t tr)
{
    if (e == ENGI_PROPHET)
        p5_send(tr);
    else if (e == ENGI_FM6)
        fm6_send();
}
