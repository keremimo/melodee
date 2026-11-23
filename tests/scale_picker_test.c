/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#include <assert.h>
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void open_picker(void)
{
    assert(scale_settings_page(cur_page()));
    turn(EN_K2, -100); frame();
    turn(EN_K2, 1); frame();
    press(B_OCTUP); assert(cur_page()->graph == GR_SCALE_PICKER);
}

static void mark(int on)
{
    press(B_SCL); frames(400); assert(scale_settings_page(cur_page()));
    turn(EN_K2, -100); frame();
    turn(EN_K2, 4); frame();                       /* (SCL is a list: Favourite its 5th row, KNOB 1 its value) */
    turn(EN_K1, on ? 1 : -1); frame();
    open_picker();
}

static void project_scale_test(void)
{
    static project_t saved;
    ui_power_on();
    song.sel = 1;
    go_title("SCL");
    edit_param(0, 7);                               /* ordinary SCL page: G */
    for (uint32_t k = 0; k < NPART; k++) assert(trk[k].p[P_ROOT] == 7);
    go_home();
    lay_combo(B_SCL, black(4));                     /* SCL + D#: project key, silent */
    for (uint32_t k = 0; k < NPART; k++) assert(trk[k].p[P_ROOT] == 3);
    assert(!gates());
    key_up(black(4)); frame();
    oct_back();
    for (uint32_t k = 0; k < NPART; k++) assert(trk[k].p[P_ROOT] == 7);
    turn(EN_K1, 2);                                 /* SCL layer knob: A */
    for (uint32_t k = 0; k < NPART; k++) assert(trk[k].p[P_ROOT] == 9);
    btn_up(B_SCL); frame();

    set_engine_of(&trk[3], ENGI_808);
    song.sel = 3;                                  /* shared settings can be edited on drums too */
    go_title("SCL");
    edit_param(0, -4);
    edit_param(2, Q_WHITE);
    scale_picker_select(2);                         /* F minor */
    trk[0].p[P_TRANS] = -12;
    project_capture(&saved);
    assert(!project_save(0));
    TSEL->p[P_ROOT] = 0; TSEL->p[P_SCALE] = 0; TSEL->p[P_QUANT] = Q_OFF;
    scale_share(TSEL);
    project_load(0);
    for (uint32_t k = 0; k < NPART; k++) {
        assert(trk[k].p[P_ROOT] == 5 && trk[k].p[P_SCALE] == 2 && trk[k].p[P_QUANT] == Q_WHITE);
        if (k != 3u) assert(midi_map(&trk[k], 60) == (k ? 65u : 53u));
    }
    for (uint32_t mode = Q_OFF; mode <= Q_MPC; mode++) {
        trk[3].p[P_QUANT] = (int16_t)mode;
        scale_share(&trk[3]);
        for (uint32_t key = 0; key < 27u; key++)
            assert(kb_map(&trk[3], key) == (uint32_t)ENGINES[ENGI_808]->keys(&trk[3], key));
        if (mode != Q_MPC)
            for (uint32_t note = 0; note < 128u; note++) assert(midi_map(&trk[3], note) == note);
        else assert(midi_map(&trk[3], 21) == 38u);    /* MPC drum pads retain their mapping */
    }
    /* Existing projects can disagree: the saved selected track supplies the shared key. */
    for (uint32_t k = 0; k < NPART; k++) saved.t[k].p[P_ROOT] = (int16_t)(k + 1u);
    saved.sel = 2;
    saved.sum = proj_sum(&saved);
    assert(!project_restore_runtime(&saved));
    for (uint32_t k = 0; k < NPART; k++) assert(trk[k].p[P_ROOT] == 3);
    template_save();
    TSEL->p[P_ROOT] = 11; scale_share(TSEL);
    template_load();
    for (uint32_t k = 0; k < NPART; k++) assert(trk[k].p[P_ROOT] == 3);
    puts("project scale: page/layer edits, cancel, save/load, legacy roots, templates and drum bypass passed");
}

int main(void)
{
    ui_power_on(); go_home(); press(B_SCL); frames(400);
    assert(scale_settings_page(cur_page()));
    press(B_SCL); frames(400); assert(cur_page()->graph == GR_CHORD);
    press(B_SCL); frames(400); assert(scale_settings_page(cur_page()));
    turn(EN_SELECT, -1); frame(); assert(cur_page()->graph == GR_CHORD);
    turn(EN_SELECT, 1); frame(); assert(scale_settings_page(cur_page()));
    open_picker();
    assert(cur_page()->graph == GR_SCALE_PICKER && ui.scale_family == 1);
    assert(cur_page()->id[1] == P_SCALE && cur_page()->id[2] == P_ROOT);
    uint32_t visited = 0;
    turn(EN_K3, 5); frame();
    assert(TSEL->p[P_ROOT] == 5 && !scale_favorite(TSEL->p[P_SCALE]));
    for (uint32_t k = 0; k < NPART; k++) assert(trk[k].p[P_ROOT] == 5);
    edit_param(2, 100); assert(TSEL->p[P_ROOT] == 11);
    edit_param(2, -100); assert(TSEL->p[P_ROOT] == 0);
    for (uint32_t family = 1; family <= SCALE_FAMILIES; family++) {
        if (scale_family_empty(family)) continue;       /* (Historical: tunings now) */
        while (ui.scale_family != family) edit_param(0, ui.scale_family < family ? 1 : -1);
        uint32_t total = scale_picker_count();
        assert(total && scale_picker_match(TSEL->p[P_SCALE]));
        for (uint32_t rank = 0; rank < total; rank++) {
            scale_picker_select(scale_picker_at(rank));
            assert(scale_picker_rank() == rank && SCALE_FAMILY[TSEL->p[P_SCALE]] + 1u == family);
            for (uint32_t k = 0; k < NPART; k++) assert(trk[k].p[P_SCALE] == TSEL->p[P_SCALE]);
            visited++;
        }
        int16_t last = TSEL->p[P_SCALE]; edit_param(1, 1000); assert(TSEL->p[P_SCALE] == last);
        edit_param(1, -1000); assert(TSEL->p[P_SCALE] == scale_picker_at(0));
    }
    assert(visited == 60u);                             /* (every entry but the retired) */
    ui.scale_family = 0; scale_picker_select(39); /* 53 EDO */
    TSEL->p[P_MPCDEG] = 53; scale_share(TSEL);
    mark(1); assert(scale_favorite(39));
    scale_picker_select(59); /* shruti */
    for (uint32_t k = 0; k < NPART; k++) assert(trk[k].p[P_MPCDEG] == 22);
    mark(1); assert(scale_favorite(59));
    edit_param(0, SCALE_FAMILIES + 1u);
    assert(scale_picker_count() == 2 && scale_picker_rank() == 1);
    uint32_t engine = TSEL->eng_req;
    turn(EN_PRESET, -1); frame();
    assert(TSEL->p[P_SCALE] == 39 && TSEL->eng_req == engine);
    mark(0); assert(!scale_favorite(39) && TSEL->p[P_SCALE] == 39 && scale_picker_count() == 1);
    edit_param(1, 1); assert(TSEL->p[P_SCALE] == 59);
    mark(0); assert(!scale_picker_count() && TSEL->p[P_SCALE] == 59);
    edit_param(1, 1); assert(TSEL->p[P_SCALE] == 59);
    mark(1); assert(scale_picker_count() == 1 && scale_favorite(59));
    edit_param(3, -100); assert(TSEL->p[P_QUANT] == Q_OFF);
    edit_param(3, 1);
    for (uint32_t k = 0; k < NPART; k++) assert(trk[k].p[P_QUANT] == TSEL->p[P_QUANT]);
    press(B_SCL); frames(400); assert(!strcmp(cur_page()->title, "SCL"));
    {
        char label[16], value[16], expected[16];
        uint16_t color;
        const list_t *list = list_on();
        assert(list);
        for (uint32_t scale = 0; scale < SCALE_TOTAL; scale++) {
            TSEL->p[P_SCALE] = (int16_t)scale;
            list_cap(list_at(list, 1), label, value, &color);
            menu_words(expected, N_SCALE[scale], sizeof expected);
            assert(!strcmp(label, "Scale") && !strcmp(value, expected));
        }
    }
    press(B_SCL); frames(400); assert(!strcmp(cur_page()->title, "CHORD"));
    puts("scale picker: SCL/SELECT skip picker, OCT+ entry, every family, shared ROOT/scale/MPC/QNT, favorites and empty list passed");
    project_scale_test();
    return 0;
}
