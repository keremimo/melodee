/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* GLO > WEIGHTS: per-engine budget points, their effect on the shared budget,
 * SAFETY, device persistence and the two pages. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static const uint8_t kinds[VOICE_KINDS] = {ENGI_PROPHET, ENGI_CZ, ENGI_FM6, 3u /* SID */, 10u /* DRUM */};
/* The rule before GLO > WEIGHTS (admission off): what AUTO must keep. */
static uint32_t builtin(const track_t *t)
{
    const engine_t *e = ENGINES[t->engine];
    return e->units ? e->units(t) : trk_nvoice(t) > NPOLY ? 1u : 2u;
}
static uint32_t sounding(const track_t *t)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < NVOICE; i++) n += t->v[i].active && t->v[i].stage != 4u;
    return n;
}
static void defaults(void) { memset(settings_vpoints, 0, sizeof settings_vpoints); settings_safety = 0; }
static const char *value(const char *title, uint32_t slot, int16_t **vp, const char **label)
{
    static char v[8];
    const char *u;
    go_title(title);
    const param_desc_t *d = page_desc(cur_page(), slot, vp);
    if (!d) return "";
    *label = d->label;
    param_format(d, **vp, v, &u);
    return v;
}

int main(void)
{
    int bad = 0, ok = 1;
    ui_power_on(); defaults();
    for (uint32_t k = 0; k < VOICE_KINDS; k++) {
        host_preset(&trk[0], kinds[k], 0);
        uint32_t auto_units = builtin(&trk[0]);
        ok &= voice_units(&trk[0]) == auto_units;
        for (int16_t pts = 1; pts <= 15; pts++) {
            settings_vpoints[k] = pts;
            ok &= voice_units(&trk[0]) == (uint32_t)pts;
            settings_vpoints[(k + 1u) % VOICE_KINDS] = 9;      /* another engine's weight never applies here */
            ok &= voice_units(&trk[0]) == (uint32_t)pts;
            settings_vpoints[(k + 1u) % VOICE_KINDS] = 0;
        }
        settings_vpoints[k] = 0;
        ok &= voice_units(&trk[0]) == auto_units;
    }
    bad += check("AUTO keeps each engine's built-in points; 1..15 replace only that engine's", ok);

    /* The budget follows the weights: 16 points hold two 8-point Prophet voices, or more at 1 point. */
    ui_power_on(); defaults(); settings_budget = 16;
    host_preset(&trk[0], ENGI_PROPHET, 0);
    settings_vpoints[0] = 8;
    trk_note_on(&trk[0], 60, 100); trk_note_on(&trk[0], 64, 100); trk_note_on(&trk[0], 67, 100);
    ok = sounding(&trk[0]) == 2u && voices_busy() <= voice_budget();
    for (uint32_t i = 0; i < NVOICE; i++) memset(&trk[0].v[i], 0, sizeof trk[0].v[i]);
    settings_vpoints[0] = 1;
    trk_note_on(&trk[0], 60, 100); trk_note_on(&trk[0], 64, 100); trk_note_on(&trk[0], 67, 100);
    ok &= sounding(&trk[0]) == 3u && voices_busy() == 3u;
    bad += check("BUDGET 16: Prophet at 8 points sounds two voices of a triad, at 1 point all three", ok);
    for (uint32_t i = 0; i < NVOICE; i++) memset(&trk[0].v[i], 0, sizeof trk[0].v[i]);

    /* Device persistence in favorites row 8 bytes 8..15; neighbours untouched. */
    persist_t p;
    ui_power_on(); defaults();
    memset(&p, 0, sizeof p);
    for (uint32_t i = 0; i < 32u; i++) favorites.factory[8][i] = (uint8_t)(0xA0u + i);
    settings_export(&p);
    const uint8_t *w = p.favorites.factory[8] + 8;
    ok = settings_legacy_valid(&p);
    for (uint32_t i = 0; i < 8u; i++) ok &= !w[i];
    for (uint32_t i = 0; i < 32u; i++) if (i < 8u || i >= 16u) ok &= p.favorites.factory[8][i] == (uint8_t)(0xA0u + i);
    bad += check("defaults store zeros in row 8 bytes 8..15 and keep its stars and FX key map", ok);

    settings_safety = 1;
    settings_export(&p);
    defaults();
    ok = w[0] == 'W' && w[1] == 1u && w[7] == 0u &&
         settings_import(&p, sizeof p) && settings_safety == 1;
    bad += check("explicit SAFETY ON survives export/import even with AUTO weights", ok);

    static const int16_t custom[VOICE_KINDS] = {3, 0, 1, 15, 2};
    memcpy(settings_vpoints, custom, sizeof custom); settings_safety = 0;
    settings_export(&p);
    ok = settings_legacy_valid(&p) && w[0] == 'W' && w[1] == 1u && w[7] == 1u;
    for (uint32_t k = 0; k < VOICE_KINDS; k++) ok &= w[2u + k] == (uint8_t)custom[k];
    defaults();
    ok &= settings_import(&p, (int)sizeof p) && !settings_safety && !memcmp(settings_vpoints, custom, sizeof custom);
    bad += check("weights and SAFETY OFF survive export/import, tagged; the record stays valid", ok);

    persist_t q = p;
    q.favorites.factory[8][8] = 'X';                           /* an untagged older record */
    ok = settings_import(&q, (int)sizeof q) && settings_safety == 0;
    for (uint32_t k = 0; k < VOICE_KINDS; k++) ok &= !settings_vpoints[k];
    q = p; q.favorites.factory[8][10] = 16;                    /* out of range: AUTO */
    ok &= settings_import(&q, (int)sizeof q) && !settings_vpoints[0] && settings_vpoints[3] == 15 && !settings_safety;
    bad += check("untagged bytes are the defaults; an out-of-range weight is AUTO", ok);

    /* The pages: GLO > WEIGHTS and WEIGHTS 2 edit the device settings, never song.g or the project. */
    ui_power_on(); defaults(); settings_budget = 16;
    int16_t *vp; const char *label;
    ok = str_eq(value("WEIGHTS", 0, &vp, &label), "AUTO") && str_eq(label, "P5") && vp == &settings_vpoints[0];
    turn(EN_K1, 1);
    ok &= settings_vpoints[0] == 1 && str_eq(value("WEIGHTS", 0, &vp, &label), "1");
    ok &= str_eq(value("WEIGHTS", 1, &vp, &label), "AUTO") && str_eq(label, "CZ-1");
    ok &= str_eq(value("WEIGHTS", 2, &vp, &label), "AUTO") && str_eq(label, "FM6");
    ok &= str_eq(value("WEIGHTS", 3, &vp, &label), "AUTO") && str_eq(label, "SID");
    ok &= str_eq(value("WEIGHTS 2", 0, &vp, &label), "AUTO") && str_eq(label, "DRUM") && vp == &settings_vpoints[4];
    value("WEIGHTS 2", 1, &vp, &label);
    ok &= str_eq(label, "BUDGET") && vp == &settings_budget;
    ok &= !page_desc(cur_page(), 2, &vp);
    ok &= str_eq(value("WEIGHTS 2", 3, &vp, &label), "OFF") && str_eq(label, "GUARD") && vp == &settings_safety;
    turn(EN_K4, 1);
    ok &= settings_safety == 1 && str_eq(value("WEIGHTS 2", 3, &vp, &label), "ON");
    turn(EN_K4, -1);
    ok &= settings_safety == 0 && str_eq(value("WEIGHTS 2", 3, &vp, &label), "OFF");
    for (uint32_t id = G_WP5; id <= G_SAFETY; id++) ok &= !song.g[id] && global_reserved(id);
    bad += check("WEIGHTS: P5 CZ-1 FM6 SID; WEIGHTS 2: DRUM BUDGET - SAFETY; edits reach the settings only", ok);

    project_capture(&proj_scratch);
    ok = !project_restore_runtime(&proj_scratch) && settings_vpoints[0] == 1 && !settings_safety;
    for (uint32_t id = G_WP5; id <= G_SAFETY; id++) ok &= !song.g[id];
    bad += check("a project load keeps the device's weights and SAFETY", ok);

    defaults();
    printf(bad ? "voice weights: %d FAILED\n" : "GLO > WEIGHTS: per-engine points, budget effect, SAFETY, persistence and pages: OK\n", bad);
    return bad != 0;
}
