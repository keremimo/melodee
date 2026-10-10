/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Real sequencer/project/UI sources: motion lifecycle and compact migration. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int motion_recording(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->p[P_REV] = 23; song.sel = 0; song.rec = 1;
    seq_start(); seq_tick(t, CTL); /* first real step */
    t->p[P_REV] = 92;
    bad += check("motion records the selected armed track at its current step", !motion_capture(t, P_REV, 92) &&
        motion.count == 1u && motion.event[0].place == 0u && motion.event[0].value == 92);
    t->p[P_REV] = 110; motion_capture(t, P_REV, 110);
    bad += check("same step/parameter overwrites rather than consuming capacity", motion.count == 1u && motion.event[0].value == 110);
    bad += check("sounding values never replace the original patch base", t->p[P_REV] == 110 && motion_base_value(t, P_REV) == 23);
    project_t q; project_store_t packed;
    project_capture(&q);
    bad += check("project snapshot saves base + independent events while sounding", q.t[0].p[P_REV] == 23 && q.motion.event[0].value == 110 &&
        proj_pack(&packed, &q) && sizeof packed == PROJ_STORE_SIZE);
    seq_stop();
    bad += check("stop before another step restores the original parameter", t->p[P_REV] == 23);
    seq_start(); seq_tick(t, CTL);
    bad += check("recorded motion plays back at step zero", t->p[P_REV] == 110);
    motion_set_enabled(t, 0);
    bad += check("bypass restores base without deleting automation", t->p[P_REV] == 23 && motion_count(t) == 1u && !motion_enabled(t));
    motion_set_enabled(t, 1); motion_step(t, 0, &motion);
    song.rec = 0; t->p[P_REV] = 37; motion_capture(t, P_REV, 37); seq_stop();
    bad += check("manual edits outside recording become the new base", t->p[P_REV] == 37);
    song.rec = 3; seq_start(); seq_tick(t, CTL);
    trk[1].p[P_REV] = 45; motion_capture(&trk[1], P_REV, 45);
    bad += check("only the selected track captures knob motion", motion_count(&trk[1]) == 0u);
    bad += check("transport and engine-switch parameters cannot be captured", !motion_param(P_SLEN) && !motion_param(P_AMODE) &&
        motion_set_event(t, 0, P_SLEN, 8) == 1);
    seq_stop(); motion_clear(t);
    t->p[P_REV] = 42; song.rec = 1; seq_start(); seq_tick(t, CTL);
    motion_clear(t); t->p[P_REV] = 70; motion_capture(t, P_REV, 70); seq_stop();
    bad += check("clearing then recording mid-play keeps the correct new base", t->p[P_REV] == 42);
    return bad;
}
static int motion_capacity(void)
{
    int bad = 0; ui_power_on();
    for (uint32_t i = 0; i < MOTION_MAX; i++) bad += motion_set_event(&trk[0], i, P_REV, (int16_t)i);
    motion_store_t saved = motion;
    bad += check("full event pool refuses append without overwriting earlier events", motion_set_event(&trk[1], 0, P_REV, 90) == 2 &&
        !memcmp(&motion, &saved, sizeof saved));
    bad += check("full pool still permits a targeted overwrite", !motion_set_event(&trk[0], 12, P_REV, 100) && motion.count == MOTION_MAX);
    motion_delete_event(&trk[0], 12, P_REV);
    bad += check("deleting one event releases one slot", motion.count == MOTION_MAX - 1u && !motion_set_event(&trk[1], 0, P_REV, 90));
    saved = motion; motion_store_t invalid = motion; invalid.count = MOTION_MAX + 1;
    bad += check("invalid restore leaves the pool intact", motion_replace_track(&trk[0], &invalid) != 0 && !memcmp(&motion, &saved, sizeof saved));
    motion_clear(&trk[0]);
    bad += check("track clear keeps every other track's events", motion.count == 1u && motion.event[0].place >> 6 == 1u);
    return bad;
}
static int probability_playback(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    step_t s = {{60, 64, 0, 0}, 2, ST_NOTE, 0, 90, 1u << DV_KICK, 0, 0};
    bad += check("zero-initialized probability remains legacy 100 percent", step_chance(&s) == 100u);
    step_set_chance(&s, 0); seq_step(t, &s, div_samples(2), 0);
    bad += check("zero percent suppresses the whole chord and drum hits", step_chance(&s) == 0u && !t->seq_n);
    step_set_chance(&s, 100); seq_step(t, &s, div_samples(2), 0);
    bad += check("100 percent plays all chord notes and drum hits", t->seq_n == 3u);
    seq_release(t); step_set_chance(&s, 50); uint32_t heard = 0;
    for (uint32_t i = 0; i < 1000u; i++) { seq_step(t, &s, div_samples(2), 0); heard += t->seq_n != 0; seq_release(t); }
    bad += check("chance is evaluated each repeat with one decision per step", heard > 350u && heard < 650u);
    return bad;
}
static int compact_project(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->step[3] = (step_t){{60, 67}, 2, ST_NOTE, SF_ACCENT, 110, 0x81, 0x80, 0};
    step_set_chance(&t->step[3], 25); t->p[P_FM1_LEVEL] = 80; t->p[P_ED_FLT] = -50;
    motion_set_event(t, 3, P_REV, 110);
    project_t before, after; project_store_t packed, corrupt;
    project_capture(&before);
    bad += check("FUN8 fits the retained and flash extent", sizeof(proj_slot) == 4u * PROJ_STORE_SIZE && proj_pack(&packed, &before));
    fm6_fn[0][FN_PTIME] = 33;                            /* (FM6 functions: in the FBK9 around FUN8, not in FUN8) */
    project_capture(&before);
    {
        static uint8_t fbk[BANK_STORE_SIZE];
        bad += check("FBK9 keeps the tracks' FM6 function settings", bank_pack(fbk, &before, 1) &&
                     bank_valid(fbk, sizeof fbk) && proj_scratch.fm6_fn_ok && proj_scratch.fm6_fn[0][FN_PTIME] == 33u);
    }
    proj_fn_none(&before); proj_pack(&packed,&before);
    bad += check("FUN8 round trip preserves signed values/FM params/probability/motion", proj_import(&after, &packed, sizeof packed) &&
        !memcmp(&before, &after, sizeof before));
    corrupt = packed; corrupt.raw[112] ^= 1u;
    bad += check("FUN7 torn or corrupted payload is refused", !proj_import(&after, &corrupt, sizeof corrupt));
    corrupt = packed; corrupt.raw[68] = 255; uint32_t sum = proj_hash(corrupt.raw, sizeof corrupt - 4u);
    memcpy(corrupt.raw + sizeof corrupt - 4u, &sum, 4);
    bad += check("compact parameter range is validated even with a correct hash", !proj_import(&after, &corrupt, sizeof corrupt));
    corrupt = packed; uint32_t probability_byte = 68u + P_COUNT + 2u + 8u; corrupt.raw[probability_byte] = 127;
    sum = proj_hash(corrupt.raw, sizeof corrupt - 4u); memcpy(corrupt.raw + sizeof corrupt - 4u, &sum, 4);
    bad += check("invalid probability is refused even with a correct hash", !proj_import(&after, &corrupt, sizeof corrupt));
    project_v6_t old; memset(&old, 0, sizeof old); old.magic = PROJ_MAGIC_V6; old.size = sizeof old;
    memcpy(old.g, before.g, sizeof old.g); old.parts = NPART; old.phys = PROJ_PHYS;
    for (uint32_t k = 0; k < NTRK; k++) {
        for (uint32_t j = 0; j < 61u; j++) old.t[k].p[j] = before.t[k].p[j];
        for (uint32_t j = 0; j < 8u; j++) old.t[k].p[61u + j] = before.t[k].p[P_E0 + j];
        old.t[k].engine = before.t[k].engine; old.t[k].preset = before.t[k].preset;
        for (uint32_t j = 0; j < NSTEP; j++) memcpy(&old.t[k].step[j], &before.t[k].step[j], sizeof(step10_t));
    }
    old.chain = before.chain; old.sum = proj_hash(&old, sizeof old - 4u);
    bad += check("real FUN6 disk image migrates with FM defaults/100% chance/no motion", proj_import(&after, &old, sizeof old) &&
        after.t[0].p[P_E0] == before.t[0].p[P_E0] && after.t[0].p[P_ED_FLT] == -50 &&
        after.t[0].p[P_FM1_LEVEL] == 127 && !after.motion.count && step_chance(&after.t[0].step[3]) == 100u);
    bad += check("migrated FUN6 can be written as fixed-size FUN7", proj_pack(&packed, &after));
    project_save(1); motion_clear(t); t->p[P_FM1_LEVEL] = 127; step_set_chance(&t->step[3], 100);
    project_load(1);
    bad += check("actual project save/load restores motion, chance and operator settings", motion_count(t) == 1u &&
        step_chance(&t->step[3]) == 25u && t->p[P_FM1_LEVEL] == 80);
    return bad;
}
/* a FUN7 image as the firmware of 89 parameters (before the chord keys P_CHRD / P_VOIC) wrote it: the engine's
 * values at 81..88, motion ids from 81 on for E0..E7 (m: its events as that firmware numbered them) */
static void pack_fun7_89(project_store_t *out, const project_t *q, const motion_store_t *m)
{
    uint8_t *b = out->raw; uint32_t pos = 68u, t, i, magic = PROJ_MAGIC_V7, size = 3388u, sum;
    memset(out, 0, sizeof *out); memcpy(b, &magic, 4); memcpy(b + 4, &size, 4);
    memcpy(b + 8, q->g, sizeof q->g); b[62] = q->sel; b[63] = q->parts; b[64] = q->phys; b[66] = 89;
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < 89u; i++) b[pos++] = (uint8_t)(q->t[t].p[i < 81u ? i : i - 81u + P_E0] + 64);
        b[pos++] = q->t[t].engine; b[pos++] = q->t[t].preset;
        for (i = 0; i < NSTEP; i++) {
            const step_t *s = &q->t[t].step[i];
            memcpy(b + pos, s->note, 4); pos += 4;
            b[pos++] = (uint8_t)(s->n | s->time << 3 | s->flags << 5);
            b[pos++] = s->vel; b[pos++] = s->hit; b[pos++] = s->acc; b[pos++] = s->probability;
        }
    }
    memcpy(b + pos, &q->chain, sizeof q->chain); pos += sizeof q->chain;
    memcpy(b + pos, m, sizeof *m);
    sum = proj_hash(b, size - 4u); memcpy(b + size - 4u, &sum, 4);
}
static int fun7_89(void)
{
    int bad = 0, ok; ui_power_on(); track_t *t = &trk[0];
    project_t before, after; project_store_t old; motion_store_t m;
    uint32_t i, k;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < 8u; i++) trk[k].p[P_E0 + i] = (int16_t)(ENGINES[trk[k].eng_req]->edit[i].min + (int16_t)(k + i) %
            (ENGINES[trk[k].eng_req]->edit[i].max - ENGINES[trk[k].eng_req]->edit[i].min + 1));
    t->p[P_FM1_ATK] = 33; t->p[P_REV] = 20;
    t->step[2] = (step_t){{60, 64, 67}, 3, ST_NOTE, 0, 100};
    project_capture(&before);
    memset(&m, 0, sizeof m);                                       /* as the 89-parameter firmware numbered them */
    m.count = 3; m.on = 1;
    m.event[0] = (motion_event_t){3, P_REV, 90};
    m.event[1] = (motion_event_t){5, 81, 40};                      /* its P_E0 (81) */
    m.event[2] = (motion_event_t){6, 61, 20};                      /* FM OP1 ATK: 61 then and now */
    pack_fun7_89(&old, &before, &m);
    ok = proj_import(&after, &old, sizeof old);
    for (k = 0; ok && k < NTRK; k++) {
        for (i = 0; i < 8u; i++) ok &= after.t[k].p[P_E0 + i] == before.t[k].p[P_E0 + i];
        for (i = 0; i < 81u; i++) ok &= after.t[k].p[i] == before.t[k].p[i];
        ok &= after.t[k].p[P_CHRD] == 0 && after.t[k].p[P_VOIC] == 0;
    }
    bad += check("FUN7 of 89 parameters: E0..E7 at 83..90, the chord keys OFF / CLOSE, the rest in place", ok &&
        after.t[0].p[P_FM1_ATK] == 33 && !memcmp(after.t[0].step, before.t[0].step, sizeof before.t[0].step));
    bad += check("  its motion: E0 (81) -> 83, REV and FM OP1 ATK (61) kept",
        after.motion.count == 3u && after.motion.event[0].param == P_REV && after.motion.event[1].param == P_E0 &&
        after.motion.event[1].value == 40 && after.motion.event[2].param == P_FM1_ATK);
    bad += check("  written again as FUN7 of 91: the same project", proj_pack(&old, &after) && old.raw[66] == P_COUNT &&
        proj_import(&before, &old, sizeof old) && !memcmp(&before, &after, sizeof before));
    pack_fun7_89(&old, &before, &m); memcpy(&proj_slot[2], &old, sizeof old);
    project_load(2);
    bad += check("legacy project's motion migrates into bank 1 at today's parameter ids", motion.count == 3u &&
        motion.event[1].param == P_E0 && trk[0].pattern == 0u);
    m.event[1].param = 82;                                          /* (any id P_E0 .. P_E7 of then moves by 2) */
    pack_fun7_89(&old, &before, &m);
    bad += check("  E1 (82) -> 84", proj_import(&after, &old, sizeof old) && after.motion.event[1].param == P_E1);
    return bad;
}
static int loads_and_song(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->p[P_REV] = 21; motion_set_event(t, 0, P_REV, 100); t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    apply_preset_to(t, 1);
    bad += check("sound load clears incompatible motion and keeps pattern", !motion_count(t) && t->step[0].note[0] == 60);
    undo_swap(); bad += check("sound undo restores the original motion pool and base", motion_count(t) == 1u && t->p[P_REV] == 21);
    undo_step(1); bad += check("sound redo restores the loaded motion state", !motion_count(t));
    undo_swap(); pattern_commit(t); pattern_request(t, 1);
    t->p[P_REV] = 43; t->step[0].note[0] = 72; chain_config.count = 1; chain_config.row[0] = (chain_row_t){0, 1};
    bad += check("song preparation imports saved motion alongside steps", chain_prepare() == 0 && motion_count(t) == 0u);
    seq_start(); seq_tick(t, CTL);
    bad += check("song plays saved automation with current instruments", chain.running && t->p[P_REV] == 100 && t->step[0].note[0] == 60);
    seq_stop(); bad += check("song stop restores current base and editable pattern", t->p[P_REV] == 43 && t->step[0].note[0] == 72);
    return bad;
}
static int repeat_mode(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->p[P_AMODE] = 6; t->p[P_AOCT] = 4; t->p[P_APROB] = 127;
    arp_add(t, 72); arp_add(t, 60);
    arp_tick(t, CTL);
    bad += check("REPEAT retriggers the last played note without octave traversal", t->arp_note == 60);
    t->arp_pos = 0xFFFFFFF; arp_tick(t, CTL);
    bad += check("REPEAT remains on the same note on the next pulse", t->arp_note == 60);
    arp_remove(t, 60); arp_remove(t, 72); arp_tick(t, CTL);
    bad += check("REPEAT releases after the last held key is released", !t->nheld && !t->arp_note);
    t->p[P_AHOLD] = 1; arp_add(t, 65); arp_remove(t, 65); t->arp_pos = 0xFFFFFFF; arp_tick(t, CTL);
    bad += check("REPEAT supports ARP HOLD", t->nheld == 1u && t->arp_note == 65);
    return bad;
}
static int native_motion_test(void)
{
    int bad = 0;
    const uint32_t engines[] = {ENGI_PROPHET, ENGI_CZ, ENGI_FM6};
    const uint32_t bytes[] = {P5_CUTOFF, 38u, FP_OL};
    for (uint32_t e = 0; e < 3u; e++) {
        ui_power_on(); track_t *t = TSEL; set_engine_of(t, engines[e]); apply_preset_to(t, 0);
        uint32_t i = bytes[e], tag = motion_native_tag(t), id = MO_NATIVE + i;
        uint8_t old[MO_NATIVE_N], base = *motion_native_at(t, i), changed = base == 80u ? 70u : 80u;
        song.rec = 1; seq_start(); seq_tick(t, CTL);
        motion_native_peek(t, old); *motion_native_at(t, i) = changed; motion_native_edited(t, old);
        bad += check("native edit records tagged motion and keeps its patch base", motion.count == 1u &&
            motion.event[0].param == id && motion.event[0].value == (int16_t)(tag << 8 | changed) && motion_nbase[0][i] == base);
        project_t q, restored; project_store_t packed; uint8_t unpacked[FP_SIZE + 1u];
        project_capture(&q); fm6_unpack(q.fm6[0], unpacked);
        bad += check("native project snapshot saves the original patch while motion sounds",
            (tag == MO_TAG_P5 ? q.p5[0].raw[i] : tag == MO_TAG_CZ ? q.cz[0].raw[i] : unpacked[i]) == base &&
            proj_pack(&packed, &q) && proj_import(&restored, &packed, sizeof packed) &&
            restored.motion.event[0].value == (int16_t)(tag << 8 | changed));
        seq_stop(); bad += check("native stop immediately restores the original patch", *motion_native_at(t, i) == base);
        song.rec = 0; seq_start(); seq_tick(t, CTL);
        bad += check("native motion replays from the real sequencer", *motion_native_at(t, i) == changed);
        motion_set_enabled(t, 0);
        bad += check("native bypass restores the patch without losing events", *motion_native_at(t, i) == base && motion.count == 1u);
        motion_set_enabled(t, 1); motion_step(t, 0, &motion);
        motion_native_peek(t, old); *motion_native_at(t, i) = 65; motion_native_edited(t, old); seq_stop();
        bad += check("native manual edit outside REC becomes the new patch base", *motion_native_at(t, i) == 65);
        int16_t lane_value;
        bad += check("native motion has a named lane and the raw value", (mo_name(id, (char *)unpacked, sizeof unpacked), unpacked[0]) &&
            mo_value(id, 0, &lane_value) && lane_value == changed);
        motion_clear_param(t, id);
        bad += check("native lane clear removes its events", !motion.count);
        bad += check("native persisted project restores base then replays its motion", !project_restore_runtime(&restored));
        seq_start(); seq_tick(t, CTL);
        bad += check("native saved events replay after loading", *motion_native_at(TSEL, i) == changed);
        seq_stop(); bad += check("loaded native motion restores the saved patch", *motion_native_at(TSEL, i) == base);
    }
    ui_power_on(); set_engine_of(TSEL, ENGI_PROPHET); apply_preset_to(TSEL, 0);
    go_home(); song.rec = 1; turn(EN_K1, -1);
    bad += check("armed stopped Stage edit explains how to record motion", !motion.count && msg_is("PLAY TO RECORD MOTION"));
    seq_start(); seq_tick(TSEL, CTL); turn(EN_K1, -1);
    bad += check("Prophet Stage cutoff records through the real UI", motion.count && motion.event[0].param == MO_NATIVE + P5_CUTOFF);
    seq_stop();
    ui_power_on(); set_engine_of(TSEL, ENGI_CZ); apply_preset_to(TSEL, 0); go_home();
    song.rec = 1; seq_start(); seq_tick(TSEL, CTL); turn(EN_K1, 1);
    bad += check("CZ Stage DCW records through the real UI", motion.count && (uint32_t)motion.event[0].value >> 8 == MO_TAG_CZ);
    seq_stop();
    ui_power_on(); set_engine_of(TSEL, ENGI_FM6); apply_preset_to(TSEL, 0);
    for (uint32_t p = 0; p < NPAGES; p++) if (PAGES[p].scope == SC_FMOP) { ui.home = 0; ui.page = p; break; }
    song.rec = 1; seq_start(); seq_tick(TSEL, CTL); turn(EN_K1, -1);
    bad += check("FM6 operator page records its native patch through UI", motion.count && (uint32_t)motion.event[0].value >> 8 == MO_TAG_FM6);
    seq_stop();
    ui_power_on(); uint32_t page = 0;
    for (uint32_t p = 0; p < NPAGES; p++) if (PAGES[p].graph == GR_MOTION) page = p;
    bad += check("empty motion page is hidden", !page_visible(page));
    motion_set_event(TSEL, 0, P_REV, 50);
    bad += check("recorded motion makes its page visible", page_visible(page));
    ui.home = 0; ui.page = page; motion_clear(TSEL);
    bad += check("clearing keeps the current page until leaving it", page_visible(page));
    go_home(); bad += check("empty motion disappears after leaving", !page_visible(page));
    motion_set_event(TSEL, 0, MO_NATIVE + P5_CUTOFF, MO_TAG_P5 << 8 | 60);
    bad += check("another engine's native events cannot expose an empty motion page", !page_visible(page));
    motion_clear(TSEL);
    bad += check("invalid native tags, fields and values are refused", !motion_native_ok(MO_NATIVE, 0) &&
        !motion_native_ok(MO_NATIVE + P5_UNISON, MO_TAG_P5 << 8 | 1) &&
        !motion_native_ok(MO_NATIVE + P5_SAW_A, MO_TAG_P5 << 8 | 127) &&
        !motion_native_ok(MO_NATIVE + FP_TRNSP, MO_TAG_FM6 << 8) &&
        !motion_native_ok(MO_NATIVE + FP_DET, MO_TAG_FM6 << 8 | 99));
    ui_power_on(); set_engine_of(TSEL, ENGI_CZ); apply_preset_to(TSEL, 0);
    for (uint32_t j = 0; j < MOTION_MAX - 1u; j++) motion_set_event(TSEL, j, P_REV, 30);
    song.rec = 1; seq_start(); seq_tick(TSEL, CTL); uint8_t old[MO_NATIVE_N]; motion_native_peek(TSEL, old);
    cz_patch[0].raw[38] ^= 1; cz_patch[0].raw[39] ^= 1; motion_native_edited(TSEL, old);
    bad += check("full pool refuses a whole multi-byte native edit without partial events", motion.count == MOTION_MAX - 1u && motion_full);
    seq_stop();
    return bad;
}
int main(void)
{
    int bad = native_motion_test() + motion_recording() + motion_capacity() + probability_playback() + compact_project() + fun7_89() + loads_and_song() + repeat_mode();
    printf("%s\n", bad ? "MOTION TEST FAILED" : "motion/chance/compact storage tests passed"); return bad != 0;
}
