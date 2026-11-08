/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#include <assert.h>
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void verify(const char *what, int ok) { bad += check(what, ok); }
static int near_ratio(uint32_t tuned, uint32_t normal, uint32_t hz)
{
    double want = (double)normal * hz / 440;
    return fabs(tuned - want) <= fmax(2.0, want * 0.00005);
}

static void pitch_tables(void)
{
    int same = 1, accurate = 1, ratios = 1;
    tuning_a4 = 440;
    uint32_t octave_up = pow2_q16(192), octave_down = pow2_q16(-192);
    for (uint32_t p = 0; p < 2048; p++) same &= tuned_pitch_inc(p) == pitch_inc(p);
    verify("440 Hz preserves every original pitch-table entry", same);
    for (uint32_t hz = A4_MIN; hz <= A4_MAX; hz++) {
        tuning_a4 = (int16_t)hz;
        for (uint32_t p = 0; p < 2048; p++) accurate &= near_ratio(tuned_pitch_inc(p), pitch_inc(p), hz);
        accurate &= fabs((double)tuned_pitch_inc(69 * 16) * FS / 4294967296.0 - hz) < 0.0001;
        ratios &= pow2_q16(192) == octave_up && pow2_q16(-192) == octave_down;
    }
    verify("400..480 Hz tunes A4 and every note to the frequency ratio", accurate);
    verify("Concert pitch leaves octave and other dimensionless ratios unchanged", ratios);
    tuning_a4 = 440;
}

/* Render a held note through each engine, retaining its real envelopes and pitch controls. */
static uint32_t oscillator_step(uint32_t eng, uint32_t hz, uint32_t osc)
{
    ui_power_on(); tuning_a4 = (int16_t)hz; eng_state_reset();
    track_t *t = TSEL;
    set_engine_of(t, eng); apply_preset_to(t, 0); t->engine = t->eng_req;
    t->p[P_LD_PIT] = t->p[P_ED_PIT] = 0;
    t->p[P_SUS] = 127; t->p[P_VOICE] = V_POLY;
    if (eng == ENGI_CZ) cz_patch_of(0)->raw[0] = (cz_patch_of(0)->raw[0] & ~3u) | 2u; /* LINE 1 + 1' */
    trk_note_on(t, 69, 100);
    voice_t *v = 0;
    for (uint32_t i = 0; i < NVOICE; i++) if (t->v[i].gate) { v = &t->v[i]; break; }
    assert(v);
    uint32_t before = v->ph[osc]; int32_t out[CTL];
    track_render(t, out, CTL);
    return (v->ph[osc] - before) / CTL;
}

static void engines(void)
{
    const uint32_t eng[] = {ENGI_SID, ENGI_CZ}, oscillators[] = {1, 2};
    for (uint32_t k = 0; k < NELEM(eng); k++) {
        int ok = 1;
        for (uint32_t osc = 0; osc < oscillators[k]; osc++) {
            uint32_t a = oscillator_step(eng[k], 440, osc);
            uint32_t b = oscillator_step(eng[k], 432, osc);
            ok &= a && (eng[k] == ENGI_SID ? fabs(b - (double)a * 432 / 440) <= (SID_FREQ_K16 >> 16) /* SID: to its 16-bit */
                                    : near_ratio(b, a, 432));                                     /* frequency register */
        }
        char msg[100]; snprintf(msg, sizeof msg, "%s: all oscillator pitches follow A4=432", ENGINES[eng[k]]->name);
        verify(msg, ok);
    }
    ui_power_on(); tuning_a4 = 440;
    track_t *t = TSEL;
    set_engine_of(t, ENGI_FM6); t->engine = t->eng_req;
    uint8_t patch[FP_SIZE + 1u]; fm6_unpack(FM6_INIT, patch);
    patch[FP_OP + FP_MODE] = 1; patch[FP_OP + FP_FC] = 2; /* a fixed-frequency operator too */
    fm6_put_patch(0, patch, 1); fm6_fn_reset();
    for (uint32_t k = 0; k < 8; k++) t->p[P_E0 + k] = 0;
    trk_note_on(t, 69, 100);
    int32_t out[CTL]; track_render(t, out, CTL);
    fm6_voice_t *s = fm6_state(t, &t->v[0]); uint32_t normal[6];
    for (uint32_t k = 0; k < 6; k++) normal[k] = s->fq[k];
    tuning_a4 = 432;
    for (uint32_t k = 0; k < 4; k++) track_render(t, out, CTL);
    int ok = 1;
    for (uint32_t k = 0; k < 6; k++) ok &= near_ratio((uint32_t)s->fq[k], normal[k], 432);
    verify("FM6 held notes: ratio and fixed operators follow the A4 reference once", ok);
    tuning_a4 = 440;
}

static void controls(void)
{
    ui_power_on(); tuning_a4 = 440; go_title("GLOBAL");
    int16_t old = song.g[G_A4]; turn(EN_K1, -8);
    verify("GLOBAL knob 1 selects 432 Hz without writing the project", tuning_a4 == 432 && song.g[G_A4] == old);
    char value[16]; const char *unit;
    param_format(&GP[G_A4], tuning_a4, value, &unit);
    verify("A4 card shows 432 Hz", str_eq(value, "432") && str_eq(unit, "Hz"));
    stop_transport(); project_save(0); tuning_a4 = 444;
    project_load(0); verify("Loading a project keeps the device's chosen A4", tuning_a4 == 444);
    int16_t previous_budget=settings_budget;settings_budget=32;
    project_load(0);verify("Loading a project keeps the shared workload allowance",settings_budget==32);
    template_save(); tuning_a4 = 432;
    template_load(); verify("Loading a template keeps the device's chosen A4", tuning_a4 == 432);
    settings_budget=24;template_load();verify("Loading a template keeps the device workload allowance",settings_budget==24);
    settings_budget=previous_budget;
    song.g[G_TUNE] = 7; go_title("GLOBAL"); turn(EN_K4, 1);
    verify("TUNE remains an independent cents adjustment", song.g[G_TUNE] == 8 && tuning_a4 == 432);
    tuning_a4 = 440; turn(EN_K1, -1000);
    verify("A4 knob clamps at 400 Hz", tuning_a4 == 400);
    turn(EN_K1, 1000); verify("A4 knob clamps at 480 Hz", tuning_a4 == 480);
    tuning_a4 = 432;
    track_t *t = TSEL; voice_t *v = &t->v[0]; vmod_t m = {0};
    mod.on = 1; mod.pit = 16; mod.nv = 0; mod.amp = 0;
    m.pitch16 = 69 * 16; m.inc = tuned_pitch_inc(m.pitch16); m.fine = 3;
    mod_voice(t, v, &m, m.fine);
    uint32_t expected = tuned_pitch_inc(70 * 16);
    expected += (uint32_t)((int32_t)(expected >> 12) * 3);
    verify("Matrix pitch modulation retains A4 tuning and the fine offset", m.inc == expected);
    tuning_a4 = 440;
}

int main(void)
{
    pitch_tables(); engines(); controls();
    puts(bad ? "CONCERT PITCH TEST FAILED" : "Concert pitch test passed");
    return bad != 0;
}
