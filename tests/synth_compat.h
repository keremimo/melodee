/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The host tests' view of the shipped synths' patches since the firmware keeps them as bytes its synths' schemas
 * describe (firmware/src/schema.c, patch.c): a parameter by its schema id, FM6's voice unpacked to read, the Motion
 * tags the schemas give. Included after the firmware */
#pragma once
/* the shipped synths' patch layouts (their modules' headers: the firmware includes none of them) */
#include "../firmware/modules/fm6/fm6_patch.h"
#include "../firmware/modules/cz/cz_tone.h"
#include "../firmware/modules/prophet/p5_patch.h"
#include "../firmware/modules/drum/drum_patch.h"
/* FM6: track k's voice unpacked (VCED: 155 bytes + 1), its functions; writes as the DX7 SysEx makes them (the FM6
 * module's fm6_sysex.c: sanitized, packed, the part's patch put) */
static void fm6_voice_of(uint32_t k, uint8_t *v) { fm6_unpack(patch_of(k), v); }
static uint8_t *fm6_fn_of(uint32_t k) { return patch_of(k) + FM6_B_FN; }
static void fm6_put_v(uint32_t tr, const uint8_t *v, uint32_t how)
{
    uint8_t b[FM6_PACKED], s[FP_SIZE + 1u];
    if (trk[tr % NTRK].eng_req != ENGI_FM6)
        return;
    memcpy(s, v, FP_SIZE);
    fm6_sanitize(s);
    fm6_pack(s, b);
    patch_set(tr % NTRK, ENGI_FM6, b, FM6_PACKED, how);
}
static void fm6_put_patch(uint32_t tr, const uint8_t *v, int load) { fm6_put_v(tr, v, load ? MOD3_PATCH_NEW : MOD3_PATCH_EDIT); }
static void fm6_set_patch(uint32_t tr, const uint8_t *v) { fm6_put_v(tr, v, MOD3_PATCH_SET); }
static void fm6_fn_set(uint32_t tr, uint32_t k, int32_t v)
{
    if (tr < NTRK && k < FM6_NFN && patch_eng_of(tr) == ENGI_FM6) {
        fm6_fn_of(tr)[k] = (uint8_t)clamp(v, 0, FM6_FNMAX[k]);
        patch_note(tr, MOD3_PATCH_EDIT, 0xFFFFu);
    }
}
static void fm6_on_set(uint32_t tr, uint32_t on)
{
    if (tr < NTRK && patch_eng_of(tr) == ENGI_FM6) {
        patch_of(tr)[FM6_B_ON] = (uint8_t)(on & FM6_ON_ALL);
        patch_note(tr, MOD3_PATCH_EDIT, 0xFFFFu);
    }
}
static uint32_t fm6_on_of(uint32_t tr) { return patch_of(tr)[FM6_B_ON]; }
static void fm6_name(char *d, const uint8_t *v)          /* a VCED voice's name, trailing spaces cut */
{
    uint32_t i, n = 0;
    for (i = 0; i < 10u; i++) {
        d[i] = (char)v[FP_NAME + i];
        if (d[i] != ' ')
            n = i + 1u;
    }
    d[n] = 0;
}
/* a factory preset's voice on track t (Melodee 1.x's fm6_track_loaded: after INIT VOICE's place, INIT VOICE) */
static void fm6_track_loaded(const track_t *t)
{
    uint32_t tr = trk_ix(t);
    if (tr < NTRK && t->eng_req == ENGI_FM6) {
        uint8_t pk[MOD3_PATCH_MAX], v[FP_SIZE + 1u];
        if (t->preset >= FM6_NFAC || !preset_patch(ENGI_FM6, t->preset, pk, FM6_PACKED))
            patch_default(ENGI_FM6, pk);
        fm6_unpack(pk, v);
        fm6_put_patch(tr, v, 1);
    }
}
/* Sequential's program k, Casio's tone k (0..: the presets after INIT), for comparisons (four slots each) */
static const p5_patch_t *p5_fac(uint32_t k)
{
    static p5_patch_t s[4];
    static uint32_t n;
    p5_patch_t *p = &s[n++ % 4u];
    if (!preset_patch(ENGI_PROPHET, k + 1u, p, sizeof *p)) p5_patch_init(p);
    return p;
}
static const uint8_t *cz_fac(uint32_t k)
{
    static uint8_t s[4][CZ_BYTES];
    static uint32_t n;
    uint8_t *p = s[n++ % 4u];
    if (!preset_patch(ENGI_CZ, k + 1u, p, CZ_BYTES)) cz_patch_init(p);
    return p;
}
/* the CZ-1's tone, the Prophet's program, DRUM's kit offsets of track k: its patch's bytes (main loop) */
static cz_patch_t *cz_patch_of(uint32_t k) { return (cz_patch_t *)(void *)patch_of(k); }
static p5_patch_t *p5_prog(uint32_t k) { return (p5_patch_t *)(void *)patch_of(k); }
static drum_patch_t *drum_patch_of(uint32_t k) { return (drum_patch_t *)(void *)patch_of(k); }
/* the Prophet: track t's program's voice settings the part's (Melodee 1.x's p5_track_accept) */
static void p5_track_accept(track_t *t) { p5_track_accept_raw(t, p5_prog(trk_ix(t))->raw); }
/* factory program pi on track t (Melodee 1.x's p5_preset_loaded: 0, or none there, INIT), its voice settings the part's */
static void p5_preset_loaded(track_t *t, uint32_t pi)
{
    p5_patch_t *p = p5_prog(trk_ix(t));
    if (!pi || pi > P5_FACTORY_N || !preset_patch(ENGI_PROPHET, pi, p, sizeof *p))
        p5_patch_init(p);
    p5_track_accept(t);
}
/* user slot k's Prophet program (Melodee 1.x's p5_user_get): 0, 2 empty (upreset.c: after this file) */
#define p5_user_get(k, p) (native_used(ENGI_PROPHET, (k)) ? (memcpy((p), native_raw(ENGI_PROPHET, (k)), sizeof *(p)), 0) : 2)
/* Melodee 1.x's bank objects as the synths' structs see them (native_presets.c reads them as bytes: tone_bank_t,
 * program_bank_t, the same layouts) */
typedef struct { uint32_t magic; uint16_t ver, slots; uint32_t used; char name[16]; cz_patch_t tone[16]; } cz_bank_t;
typedef struct { uint32_t magic, used, favorites; p5_patch_t patch[26]; } p5_bank_t;
_Static_assert(sizeof(cz_bank_t) == 2332u && sizeof(p5_bank_t) == 3600u, "1.x banks: native_presets.c's layouts");
#define CZ_BANK_MAGIC 0x42435A43u
#define P5_BANK_MAGIC 0x31553550u
static void cz_bank_empty(cz_bank_t *b, uint32_t k)
{
    memset(b, 0, sizeof *b); b->magic = CZ_BANK_MAGIC; b->ver = 1; b->slots = 16;
    memcpy(b->name, "BANK A", 6); b->name[5] = (char)('A' + k);
}
static void p5_bank_defaults(p5_bank_t *b, uint32_t bank) { (void)bank; memset(b, 0, sizeof *b); b->magic = P5_BANK_MAGIC; }
#define cz_bank_valid(b) tone_bank_valid((const tone_bank_t *)(const void *)(b))   /* (native_presets.c: after this file) */
#define p5_bank_mask(bank) program_bank_mask(bank)
#define p5_bank_valid(b, bank) program_bank_valid((const program_bank_t *)(const void *)(b), bank)
#define DRUM_WIRE_BYTES MOD_SCH_KIT_PACKED
static int drum_patch_valid(const drum_patch_t *p) { return patch_check(ENGI_DRUM, (const uint8_t *)p, 0, sizeof *p); }
static void drum_patch_pack(uint8_t *b, const drum_patch_t *p)
{
    patch_pack(MOD_SCH_STORE_KIT, b, (const uint8_t *)(const void *)p, MOD_SCH_KIT_PACKED);   /* (its schema's packing) */
}
static int drum_patch_unpack(drum_patch_t *p, const uint8_t *b)
{
    patch_unpack(MOD_SCH_STORE_KIT, (uint8_t *)(void *)p, b, MOD_SCH_KIT_PACKED);
    return drum_patch_valid(p);
}
/* track k's synth parameter id (its schema's: FM6's VCED place, the Prophet's byte, the CZ-1's panel id) set to v,
 * told as an edit */
static void sch_set_t(uint32_t k, uint32_t id, int32_t v)
{
    sch_param_t p;
    uint32_t e = eng_idx(trk[k].eng_req);
    if (sch_param(e, id, &p)) {
        fprintf(stderr, "sch_set_t: engine %u has no parameter %u\n", e, id);
        exit(1);
    }
    if (sch_put(&trk[k], e, patch_of(k), &p, v))
        patch_note(k, MOD3_PATCH_EDIT, id);
}
static int32_t sch_get_t(uint32_t k, uint32_t id) { return sch_val(&trk[k], eng_idx(trk[k].eng_req), id, 0); }
/* FM6: track k's voice unpacked (VCED, 155 bytes + 1), a copy to read (writes: fm6_put_patch, fm6_set_patch,
 * sch_set_t); four at a time */
static uint8_t *fm6_patch_of(uint32_t k)
{
    static uint8_t v[4][FP_SIZE + 1u];
    static uint32_t n;
    uint8_t *b = v[n++ & 3u];
    fm6_voice_of(k, b);
    return b;
}
#define MO_TAG_P5 patch_mtag(ENGI_PROPHET)
#define MO_TAG_CZ patch_mtag(ENGI_CZ)
#define MO_TAG_FM6 patch_mtag(ENGI_FM6)
/* the parts whose patch is engine e's back to its start (its schema's default), Melodee 1.x's cz_init / clearing
 * p5_ready (the bytes written, nothing accepted: an edit told), fm6_init (the init voice loaded: a new sound); for the
 * parts playing that synth (another engine's patch comes with the engine) */
static void synth_parts_init(uint32_t e, uint32_t how)
{
    for (uint32_t k = 0; k < NTRK; k++)
        if (patch_eng_of(k) == e) {
            if (how == MOD3_PATCH_NEW) {
                patch_set(k, e, 0, 0, how);
            } else {
                patch_default(e, patch_of(k));
                patch_note(k, MOD3_PATCH_EDIT, 0xFFFFu);
            }
        }
}
static void cz_init(void) { synth_parts_init(ENGI_CZ, MOD3_PATCH_EDIT); }
static void fm6_init(void) { synth_parts_init(ENGI_FM6, MOD3_PATCH_NEW); }
static void p5_parts_init(void) { synth_parts_init(ENGI_PROPHET, MOD3_PATCH_EDIT); }
/* FM6: every FM6 track's function settings Dexed's (the patches' bytes past the voice) */
static void fm6_fn_reset(void)
{
    for (uint32_t k = 0; k < NTRK; k++)
        if (patch_eng_of(k) == ENGI_FM6) {
            memcpy(fm6_fn_of(k), FM6_FNDEF, FM6_NFN);
            patch_note(k, MOD3_PATCH_EDIT, 0xFFFFu);
        }
}
/* engine e's parameter id as a descriptor (its schema's label and range), 0 none; four at a time */
static const param_desc_t *sch_pd(uint32_t e, uint32_t id)
{
    static param_desc_t d[4];
    static char l[4][MOD_SCH_NAME + 1u];
    static uint32_t n;
    sch_param_t p;
    uint32_t k = n++ & 3u;
    if (sch_param(e, id, &p) || p.label == MOD_SCH_NONE)
        return 0;
    sch_str(e, p.label, l[k], sizeof l[k]);
    d[k] = (param_desc_t){l[k], p.fmt, p.min, p.max, p.def, 0, 0};
    return &d[k];
}
/* DRUM: every DRUM part's kit offsets 0 (Melodee 1.x's tests cleared drum_patch) */
static void drum_patches_zero(void)
{
    for (uint32_t k = 0; k < NTRK; k++)
        if (patch_eng_of(k) == ENGI_DRUM)
            memset(patch_of(k), 0, sizeof(drum_patch_t));
}
#define PROJ_DRUM(a, k) ((drum_patch_t *)(void *)(a).drum[k])   /* (a project's kit offsets: bytes) */
/* the Prophet's program as the main loop writes it: the part's own patch (patch_of), not the audio's view of it
 * (p5_patch.h: zeros while the part's engine is switching) */
#define p5_patch_of(t) p5_prog(trk_ix(t))
