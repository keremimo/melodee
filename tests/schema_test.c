/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The synths' schemas (firmware/modules/sdk/mod_schema.h, firmware/src/schema.c) against the synths themselves:
 *   mapping   every parameter a patch field puts and reads back its whole range and touches its own bits only; the
 *             Prophet's ids are its program's bytes, FM6's read as its own codec unpacks the voice (fm6_unpack)
 *   roles     every role names a parameter (or a page) the schema has; MIDI CC 73 / 72 / 74 set what the roles name
 *   blobs     a part's patch belongs to one engine (another engine reads zeros), a project keeps the bytes
 *   by id     Motion records and plays a synth's parameters by their ids and gets them back through a project; a
 *             learned MIDI knob sets the parameter its column's id names, the same after the synth's pages are rebuilt
 *   mutations a schema with a field moved, a role pointed elsewhere, a patch tagged for another engine: each of the
 *             checks above sees it */
#define MIDI_CONTROL_NO_MAIN
#include "midi_control_test.c"

/* ---- schemas swapped in for the mutations (host builds: schema.c sch_mem) */
static uint8_t mut_buf[12288];
static uint8_t *mut_saved;
static uint16_t mut_saved_len;
static void schema_swap(uint32_t e, const mod_sch_src *s)
{
    uint32_t n = mod_sch_write(s, mut_buf, sizeof mut_buf), len;
    (void)sch_builtin(e, &len);
    mut_saved = sch_mem[e];
    mut_saved_len = sch_mem_len[e];
    sch_mem[e] = n ? mut_buf : 0;
    sch_mem_len[e] = (uint16_t)n;
    sch_gen++;
    sch_info_sync();
    sch_part_sync();
}
static void schema_back(uint32_t e)
{
    sch_mem[e] = mut_saved;
    sch_mem_len[e] = mut_saved_len;
    sch_gen++;
    sch_info_sync();
    sch_part_sync();
}

/* ---- mapping: its faults (0: none) for engine e on track k */
static uint32_t mapping_faults(uint32_t e, uint32_t k, int verbose)
{
    mod_sch_head h;
    sch_param_t p;
    uint32_t i, faults = 0;
    if (sch_head(e, &h))
        return 1;
    for (i = 0; i < h.nparam; i++) {
        uint8_t b[MOD3_PATCH_MAX], was[MOD3_PATCH_MAX];
        int32_t vs[3], j;
        if (sch_param_at(e, &h, i, &p) || p.kind != MOD3_SK_FIELD)
            continue;
        vs[0] = p.min; vs[1] = p.max; vs[2] = (p.min + p.max) / 2;
        for (j = 0; j < 3; j++) {
            uint32_t nb = (p.shift + p.width + 7u) / 8u, mask = ((1u << p.width) - 1u) << p.shift, x;
            memcpy(b, patch_of(k), sizeof b);
            memcpy(was, b, sizeof was);
            sch_put(&trk[k], e, b, &p, vs[j]);
            if (sch_get(&trk[k], e, b, &p) != vs[j]) {
                if (verbose) printf("schema: engine %u id %u: put %d, read %d\n", e, p.id, vs[j], sch_get(&trk[k], e, b, &p));
                faults++;
            }
            for (x = 0; x < sizeof b; x++) {
                uint32_t m = x >= p.off && x < p.off + nb ? (mask >> (8u * (x - p.off))) & 255u : 0u;
                if ((b[x] ^ was[x]) & ~m) {
                    if (verbose) printf("schema: engine %u id %u: byte %u touched\n", e, p.id, x);
                    faults++;
                    break;
                }
            }
        }
        if (patch_store_tag(e) == MOD_SCH_STORE_PROGRAM && (p.off != p.id || p.shift))   /* (ids = the bytes) */
            faults++;
        if (patch_store_tag(e) == MOD_SCH_STORE_VOICE && p.id < FP_SIZE && !(p.flags & MOD3_SF_HIDDEN)) {
            uint8_t v[FP_SIZE + 1u];
            memcpy(b, patch_of(k), sizeof b);
            sch_put(&trk[k], e, b, &p, vs[2]);
            fm6_unpack(b, v);                    /* (the voice as FM6 itself reads it: id = its VCED place) */
            if (v[p.id] != vs[2]) {
                if (verbose) printf("schema: FM6 id %u: the voice reads %u, put %d\n", p.id, v[p.id], vs[2]);
                faults++;
            }
        }
    }
    return faults;
}
static int mapping_tests(void)
{
    int bad = 0;
    static const uint32_t E[5] = {ENGI_PROPHET, ENGI_FM6, ENGI_CZ, ENGI_DRUM, ENGI_SID};
    for (uint32_t i = 0; i < 5u; i++) {
        char what[96];
        ui_power_on();
        set_engine_of(TSEL, E[i]);
        snprintf(what, sizeof what, "%s: every patch field puts and reads back, touches its own bits only", ENGINES[E[i]]->name);
        bad += check(what, sch_has(E[i]) && !mapping_faults(E[i], song.sel, 1));
    }
    /* a field moved (FM6's ALG one bit up, over FB's): the mapping checks see it */
    {
        static mod_sch_param_src prm[256];
        mod_sch_src s = FM6_SCHEMA;
        uint32_t n = s.nparam, faults;
        memcpy(prm, s.param, n * sizeof *prm);
        for (uint32_t i = 0; i < n; i++)
            if (prm[i].id == FP_ALG)
                prm[i].shift = 1;
        s.param = prm;
        ui_power_on();
        set_engine_of(TSEL, ENGI_FM6);
        schema_swap(ENGI_FM6, &s);
        faults = mapping_faults(ENGI_FM6, song.sel, 0);
        schema_back(ENGI_FM6);
        bad += check("mutation: FM6's ALG moved over FB is caught by the mapping checks", faults > 0u);
    }
    return bad;
}

/* ---- roles */
static uint32_t role_faults(uint32_t e)
{
    mod_sch_head h;
    uint8_t r[MOD_SCH_RREC];
    sch_param_t p;
    uint32_t faults = 0;
    if (sch_head(e, &h))
        return 0;
    for (uint32_t i = 0; i < h.nrole; i++) {
        uint32_t id;
        if (sch_rd(e, h.role + i * MOD_SCH_RREC, r, sizeof r))
            return faults + 1u;
        id = sch_u16(r + 2);
        if (r[0] == MOD3_R_SYSEX || r[0] == MOD3_R_CHECK)   /* (the widget data's records: their offset, a count) */
            faults += (uint32_t)id + r[1] * (r[0] == MOD3_R_SYSEX ? 4u : MOD_SCH_CREC) > (uint32_t)h.str - h.wid;
        else if (r[0] == MOD3_R_PADS || r[0] == MOD3_R_PACK) /* (the widget data's blocks: the pads head, the packing) */
            faults += (uint32_t)id + (r[0] == MOD3_R_PADS ? MOD_SCH_PDHEAD : 2u * r[1] + 2u) > (uint32_t)h.str - h.wid;
        else if (r[0] >= MOD3_R_BANK_FILTER && r[0] <= MOD3_R_BANK_LFO)
            faults += id >= h.npage;
        else if (r[1] & MOD3_RF_TRACK)
            faults += id >= P_COUNT;             /* (the part's own: a P_* number, an E value or not) */
        else
            faults += sch_param(e, id, &p) != 0;
    }
    return faults;
}
/* CC cc at value on the selected track: every parameter its role names, as set (0: none moved as asked) */
static int cc_sets_role(uint32_t cc, uint32_t value, uint32_t role)
{
    uint32_t e = eng_idx(TSEL->eng_req), id, f, l, n, seen = 0;
    sch_param_t p;
    midi_parameter(TSEL, cc, value);
    for (n = 0; !sch_role(e, role, n, &id, &f, &l); n++) {
        int32_t want;
        if (f & MOD3_RF_TRACK) {
            const param_desc_t *d = id < P_E0 ? &TP[id] : &ENGINES[TSEL->eng_req]->edit[id - P_E0];
            if (TSEL->p[id] != d->min + (int32_t)(value * (uint32_t)(d->max - d->min) / 127u))
                return 0;
        } else {
            if (sch_param(e, id, &p))
                return 0;
            want = f & MOD3_RF_INVERT ? p.max - (int32_t)(value * (uint32_t)(p.max - p.min) / 127u)
                                      : p.min + (int32_t)(value * (uint32_t)(p.max - p.min) / 127u);
            if (sch_val(TSEL, e, id, 0) != want)
                return 0;
        }
        seen++;
    }
    return seen != 0u;
}
static int role_tests(void)
{
    int bad = 0;
    static const uint32_t E[5] = {ENGI_PROPHET, ENGI_FM6, ENGI_CZ, ENGI_DRUM, ENGI_SID};
    for (uint32_t i = 0; i < 5u; i++) {
        char what[96];
        snprintf(what, sizeof what, "%s: every role names a parameter or a page it has", ENGINES[E[i]]->name);
        bad += check(what, sch_has(E[i]) && !role_faults(E[i]));
    }
    ui_power_on(); midi_test_reset();
    set_engine_of(TSEL, ENGI_FM6);
    bad += check("FM6: CC 73 sets every operator's R1 (inverted), both ways", cc_sets_role(73, 127, MOD3_R_CC_ATK) &&
                 cc_sets_role(73, 0, MOD3_R_CC_ATK) && sch_get_t(song.sel, 5 * FP_OP + FP_R1) == 99);
    bad += check("FM6: CC 72 sets every operator's R4", cc_sets_role(72, 90, MOD3_R_CC_REL));
    set_engine_of(TSEL, ENGI_PROPHET);
    bad += check("Prophet: CC 73 / 72 set its amp envelope's bytes by their ids", cc_sets_role(73, 100, MOD3_R_CC_ATK) &&
                 cc_sets_role(72, 20, MOD3_R_CC_REL) && patch_of(song.sel)[P5_ATTACK_AMP] == 100u * 127u / 127u);
    bad += check("Prophet: CC 74 sets the part's E value its BRIGHT role names", cc_sets_role(74, 64, MOD3_R_CC_BRIGHT));
    set_engine_of(TSEL, ENGI_CZ);
    bad += check("CZ-1: CC 73 / 72 set the amp envelope's rates (its module's)", cc_sets_role(73, 64, MOD3_R_CC_ATK) &&
                 cc_sets_role(72, 64, MOD3_R_CC_REL));
    /* a role pointed elsewhere (the Prophet's CC ATK at its decay): the CC check sees it */
    {
        static mod_sch_role_src rol[64];
        mod_sch_src s = P5_SCHEMA;
        int moved;
        memcpy(rol, s.role, s.nrole * sizeof *rol);
        for (uint32_t i = 0; i < s.nrole; i++)
            if (rol[i].role == MOD3_R_CC_ATK)
                rol[i].id = P5_DECAY_AMP;
        s.role = rol;
        ui_power_on(); midi_test_reset();
        set_engine_of(TSEL, ENGI_PROPHET);
        patch_of(song.sel)[P5_ATTACK_AMP] = 3;
        schema_swap(ENGI_PROPHET, &s);
        midi_parameter(TSEL, 73, 100);
        moved = patch_of(song.sel)[P5_ATTACK_AMP] != 3u;
        schema_back(ENGI_PROPHET);
        bad += check("mutation: CC ATK's role at the decay is caught (the attack byte stays)", !moved);
        rol[0].id = 250;                         /* (a role naming no parameter: the role check) */
        schema_swap(ENGI_PROPHET, &s);
        moved = role_faults(ENGI_PROPHET) > 0u;
        schema_back(ENGI_PROPHET);
        bad += check("mutation: a role naming no parameter is caught by the role check", moved);
    }
    return bad;
}

/* ---- blobs */
static int blob_tests(void)
{
    int bad = 0;
    uint8_t keep[MOD3_PATCH_MAX];
    ui_power_on();
    set_engine_of(&trk[0], ENGI_PROPHET);
    trk[0].engine = ENGI_PROPHET;
    patch_of(0)[P5_CUTOFF] = 91;
    bad += check("a part's patch plays for its own engine", patch_play(&trk[0]) == patch_of(0) && patch_play(&trk[0])[P5_CUTOFF] == 91u);
    trk[0].engine = ENGI_FM6;                   /* (the audio switching: the patch still the Prophet's) */
    bad += check("mutation: another engine reads zeros, never the Prophet's bytes",
                 patch_play(&trk[0]) != patch_of(0) && !patch_play(&trk[0])[P5_CUTOFF]);
    trk[0].engine = ENGI_PROPHET;
    {
        static project_t q, r;
        static project_store_t packed;
        int same = 1;
        for (uint32_t k = 0; k < 4u; k++)
            set_engine_of(&trk[k], k == 0u ? ENGI_PROPHET : k == 1u ? ENGI_FM6 : k == 2u ? ENGI_CZ : ENGI_DRUM);
        patch_of(0)[97] = 0x5A;                  /* (an opaque byte) */
        sch_set_t(1, 2 * FP_OP + FP_OL, 33);
        fm6_fn_set(1, FN_PTIME, 7);
        patch_of(3)[5] = 9;
        memcpy(keep, patch_of(2), sizeof keep);
        project_capture(&q);
        bad += check("a project packs and unpacks", proj_pack(&packed, &q) && proj_import_same(&r, &packed, sizeof packed));
        uint8_t was[4][MOD3_PATCH_MAX];
        for (uint32_t k = 0; k < 4u; k++) memcpy(was[k], patch_of(k), MOD3_PATCH_MAX);
        same &= !project_save(0);                /* (the whole project: FM6's functions live in its bank, FBKK) */
        for (uint32_t k = 0; k < 4u; k++) apply_preset_to(&trk[k], 3);
        project_load(0);
        for (uint32_t k = 0; k < 4u; k++) {
            uint32_t i, n = patch_len(trk[k].eng_req);
            for (i = 0; i < n && was[k][i] == patch_of(k)[i]; i++)
                ;
            if (i < n) {
                printf("schema: track %u (engine %u) byte %u: %u, saved %u\n", k + 1u, trk[k].eng_req, i, patch_of(k)[i], was[k][i]);
                same = 0;
            }
        }
        bad += check("the four synths' patches come back from a project byte for byte (FM6's functions too)", same);
        packed.raw[200] ^= 0x10u;
        bad += check("mutation: a stored byte changed is refused (the record's check)", !proj_import_same(&r, &packed, sizeof packed));
    }
    return bad;
}

/* ---- by id: Motion, a learned knob */
static int motion_tests(void)
{
    int bad = 0;
    static const uint32_t E[3] = {ENGI_PROPHET, ENGI_CZ, ENGI_FM6};
    for (uint32_t i = 0; i < 3u; i++) {
        uint8_t mx[MO_NATIVE_N], mz[MO_NATIVE_N];
        uint32_t e = E[i], ids[4], n = 0, tag, ok = 1;
        char what[112];
        ui_power_on(); stop_transport();
        set_engine_of(TSEL, e);
        motion_clear(TSEL);
        tag = motion_native_tag(TSEL);
        patch_motion_map(e, mx, mz, MO_NATIVE_N);
        {
            uint8_t mk[MO_NATIVE_N / 8u + 1u];
            motion_slots(TSEL, mk);
            uint32_t all[MO_NATIVE_N], na = 0;
            for (uint32_t id = 0; id < MO_NATIVE_N; id++)
                if (MO_SLOT(mk, id) && (!sch_info[e].mbytes || mx[id]))
                    all[na++] = id;
            for (; n < 4u && na >= 4u; n++)      /* (four spread over what Motion records) */
                ids[n] = all[n * (na - 1u) / 3u];
        }
        for (uint32_t j = 0; j < n; j++) {
            uint32_t v = (uint32_t)motion_slot_get(TSEL, ids[j]) ? 0u : 1u;
            motion_set_event(TSEL, 2u + j, MO_NATIVE + ids[j], (int16_t)(tag << 8 | v));
        }
        project_save(0);
        motion_clear(TSEL);
        project_load(0);
        for (uint32_t j = 0; j < n; j++) {
            uint32_t id = ids[j], found = 0, v = 0;
            for (uint32_t x = 0; x < motion.count; x++)
                if (motion.event[x].param == MO_NATIVE + id && (uint32_t)motion.event[x].value >> 8 == tag) {
                    found = 1;
                    v = (uint32_t)motion.event[x].value & 255u;
                }
            ok &= found;
            motion_native_set(TSEL, id, v);
            ok &= motion_slot_get(TSEL, id) == (int32_t)v;
            if (!sch_info[e].mbytes)             /* (by ids: the schema reads the id Motion wrote) */
                ok &= sch_get_t(song.sel, id) == motion_slot_get(TSEL, id);
        }
        if (!ok || n != 4u) printf("schema: engine %u: %u ids, %u ok\n", e, n, ok);
        snprintf(what, sizeof what, "%s: Motion's events by parameter id through a project, played back to those ids",
                 ENGINES[e]->name);
        bad += check(what, n == 4u && ok);
    }
    return bad;
}
static int learn_tests(void)
{
    int bad = 0;
    uint32_t id = 4u * FP_OP + FP_FC;            /* (OP2's CRS: VCED place 4 * 21 + 18) */
    sch_param_t p;
    ui_power_on(); stop_transport(); midi_test_reset();
    settings_midi_knobs = MK_OFF; settings_midi_knob_mode = MK_ABSOLUTE;
    memset(mlearn_row, 0, sizeof mlearn_row);
    mlearn_row[28] = 'M'; mlearn_row[29] = 'L'; mlearn_row[30] = 1u;
    mlearn_row[EN_K1] = 20;                      /* (CC 20 learned as KNOB 1) */
    set_engine_of(TSEL, ENGI_FM6);
    go_title("FREQ");
    sch_inst = 1;                                /* (OP2) */
    page_entered();
    sch_param(ENGI_FM6, id, &p);
    route_input(1, 0xB0, 20, 0); ui_input();
    route_input(1, 0xB0, 20, 127); ui_input(); ui_input();
    int top = sch_get_t(song.sel, id) == p.max;
    route_input(1, 0xB0, 20, 0); ui_input(); ui_input();
    int bottom = sch_get_t(song.sel, id) == p.min;
    bad += check("a learned knob sets FM6 OP2's CRS by its id (its whole range)", top && bottom);
    set_engine_of(TSEL, ENGI_PROPHET); frame();
    set_engine_of(TSEL, ENGI_FM6);
    go_title("FREQ"); sch_inst = 1; page_entered();
    route_input(1, 0xB0, 20, 127); ui_input(); ui_input();
    bad += check("  .. the same id after the synth's pages were rebuilt", sch_get_t(song.sel, id) == p.max);
    memset(mlearn_row, 0, sizeof mlearn_row);
    return bad;
}

#ifndef SCHEMA_TEST_NO_MAIN
int main(void)
{
    int bad = mapping_tests() + role_tests() + blob_tests() + motion_tests() + learn_tests();
    printf("schema: %s\n", bad ? "FAILED" : "mappings, roles, blobs, Motion and learned knobs by id; every mutation caught");
    return bad != 0;
}
#endif
