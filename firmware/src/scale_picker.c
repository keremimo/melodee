/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The filter is UI state; the selected scale retains its stable project ID. */
_Static_assert(SCALE_TOTAL <= SCALE_FAV_CAP, "scale favorite capacity");
static int scale_settings_page(const page_t *pg)
{
    return pg->fam == FAM_SCL && pg->graph == GR_SCALE && pg->id[1] == 0xFF;
}
static void scale_picker_mark(int on)
{
    if (scale_favorite_set((uint32_t)TSEL->p[P_SCALE], on)) settings_save();
    ui.force = 1;                   /* marking a favorite keeps the active tuning */
}
/* A retired entry stays listed only while it is the one selected: its project keeps it, nothing new picks it */
static int scale_in_family(uint32_t id, uint32_t family)
{
    return id < SCALE_TOTAL && (!scale_retired(id) || id == (uint32_t)TSEL->p[P_SCALE]) &&
        (!family || (family == SCALE_FAMILIES + 1u ? scale_favorite(id) : SCALE_FAMILY[id] + 1u == family));
}
static int scale_picker_match(uint32_t id) { return scale_in_family(id, ui.scale_family); }
static int scale_family_empty(uint32_t family)  /* (all its entries are tunings now: KNOB 1 passes it by) */
{
    for (uint32_t i = 0; i < SCALE_TOTAL; i++)
        if (scale_in_family(i, family)) return 0;
    return 1;
}
/* from a list stepped to `to`: on past the retired entries, the way it was going */
static int32_t scale_land(int32_t from, int32_t to)
{
    int32_t dir = to > from ? 1 : -1, at = to;
    if (to == from) return from;
    while (at >= 0 && at < (int32_t)SCALE_TOTAL && scale_retired((uint32_t)at)) at += dir;
    return at >= 0 && at < (int32_t)SCALE_TOTAL ? at : from;
}
static uint32_t scale_picker_count(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < SCALE_TOTAL; i++) n += scale_picker_match(i);
    return n;
}
static uint32_t scale_picker_at(uint32_t rank)
{
    for (uint32_t i = 0; i < SCALE_TOTAL; i++)
        if (scale_picker_match(i) && !rank--) return i;
    return SCALE_TOTAL;
}
static uint32_t scale_picker_rank(void)
{
    uint32_t n = 0, selected = (uint32_t)clamp(TSEL->p[P_SCALE], 0, SCALE_TOTAL - 1u);
    for (uint32_t i = 0; i < SCALE_TOTAL; i++) {
        if (!scale_picker_match(i)) continue;
        if (i == selected) return n;
        n++;
    }
    return n;
}
static void scale_picker_select(uint32_t id)
{
    if (id >= SCALE_TOTAL) return;
    scale_tuning_follow(TSEL, (uint32_t)TSEL->p[P_SCALE], id);
    TSEL->p[P_SCALE] = (int16_t)id;
    motion_capture(TSEL, P_SCALE, (int16_t)id);
    scale_share(TSEL);
    ui.force = 1;
}
static void scale_picker_step(int32_t steps)
{
    uint32_t count = scale_picker_count(), rank = scale_picker_rank();
    if (!count) return;
    scale_picker_select(scale_picker_at(rank >= count ? (steps > 0 ? 0u : count - 1u) :
        (uint32_t)clamp((int32_t)rank + steps, 0, count - 1u)));
}
static void scale_picker_edit(uint32_t slot, int32_t steps)
{
    if (slot == 0u) {
        int32_t f = ui.scale_family, dir = steps > 0 ? 1 : -1;
        for (int32_t n = steps < 0 ? -steps : steps; n > 0; n--) {
            int32_t g = f + dir;
            while (g > 0 && g <= (int32_t)SCALE_FAMILIES && scale_family_empty((uint32_t)g)) g += dir;
            if (g < 0 || g > (int32_t)SCALE_FAMILIES + 1) break;
            f = g;
        }
        ui.scale_family = (uint8_t)f;
        if (!scale_picker_match((uint32_t)TSEL->p[P_SCALE])) scale_picker_select(scale_picker_at(0));
    } else if (slot == 1u) {
        scale_picker_step(steps);
    } else if (slot == 2u) {
        TSEL->p[P_ROOT] = (int16_t)clamp(TSEL->p[P_ROOT] + steps, TP[P_ROOT].min, TP[P_ROOT].max);
        motion_capture(TSEL, P_ROOT, TSEL->p[P_ROOT]);
        scale_share(TSEL);
    } else {
        TSEL->p[P_QUANT] = (int16_t)clamp(TSEL->p[P_QUANT] + steps, TP[P_QUANT].min, TP[P_QUANT].max);
        motion_capture(TSEL, P_QUANT, TSEL->p[P_QUANT]);
        scale_share(TSEL);
    }
    ui.force = 1;
}
