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
    for (uint32_t k = 1; k < NPART; k++) assert(trk[k].p[P_ROOT] == 0);
    edit_param(2, 100); assert(TSEL->p[P_ROOT] == 11);
    edit_param(2, -100); assert(TSEL->p[P_ROOT] == 0);
    for (uint32_t family = 1; family <= SCALE_FAMILIES; family++) {
        edit_param(0, (int32_t)family - ui.scale_family);
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
    assert(visited == SCALE_TOTAL);
    ui.scale_family = 0; scale_picker_select(39); /* 53 EDO */
    TSEL->p[P_MPCDEG] = 53; scale_share(TSEL);
    mark(1); assert(scale_favorite(39));
    scale_picker_select(50); /* meantone */
    for (uint32_t k = 0; k < NPART; k++) assert(trk[k].p[P_MPCDEG] == 12);
    mark(1); assert(scale_favorite(50));
    edit_param(0, SCALE_FAMILIES + 1u);
    assert(scale_picker_count() == 2 && scale_picker_rank() == 1);
    uint32_t engine = TSEL->eng_req;
    turn(EN_PRESET, -1); frame();
    assert(TSEL->p[P_SCALE] == 39 && TSEL->eng_req == engine);
    mark(0); assert(!scale_favorite(39) && TSEL->p[P_SCALE] == 39 && scale_picker_count() == 1);
    edit_param(1, 1); assert(TSEL->p[P_SCALE] == 50);
    mark(0); assert(!scale_picker_count() && TSEL->p[P_SCALE] == 50);
    edit_param(1, 1); assert(TSEL->p[P_SCALE] == 50);
    mark(1); assert(scale_picker_count() == 1 && scale_favorite(50));
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
    puts("scale picker: SCL/SELECT skip picker, OCT+ entry, every family, shared scale/MPC/QNT, track ROOT, favorites and empty list passed");
    return 0;
}
