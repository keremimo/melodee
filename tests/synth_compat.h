/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The host tests' view of the shipped synths' patches since the firmware keeps them as bytes its synths' schemas
 * describe (firmware/src/schema.c, patch.c): a parameter by its schema id, FM6's voice unpacked to read, the Motion
 * tags the schemas give. Included after the firmware */
#pragma once
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
