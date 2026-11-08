/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Scales and tunings apart: a scale says which notes, a tuning what pitch they have. The maqam case (keremimo/melodee#118:
 * Rast on the chromatic keyboard, QNT OFF, quarter tones), old projects as they were, what is stored, the SCL list. */
#include <assert.h>
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static track_t *fresh(void)
{
    ui_power_on();
    memset(live_held, 0, sizeof live_held);
    memset(midi_ch, 0, sizeof midi_ch); memset(midi_sel_on, 0, sizeof midi_sel_on);
    memset(midi_owners, 0, sizeof midi_owners); memset(mchord, 0, sizeof mchord);
    mi_r = mi_w = 0; fm1_in.notes = kb_prev = 0;
    TSEL->p[P_CHRD] = 0; TSEL->p[P_VOICE] = V_POLY; TSEL->p[P_AMODE] = 0;
    return TSEL;
}
static uint32_t scale_id(const char *name)
{
    for (uint32_t i = 0; i < SCALE_TOTAL; i++) if (!strcmp(N_SCALE[i], name)) return i;
    assert(0); return 0;
}
static uint32_t tuning_id(const char *name)
{
    for (uint32_t i = 0; i < TUNING_COUNT; i++) if (!strcmp(N_TUNING[i], name)) return i;
    assert(0); return 0;
}
static double heard_cents(const track_t *t, uint32_t note)   /* what a voice on the note sounds, cents over MIDI note 0 */
{
    voice_t v = {0};
    voice_scale_pitch(t, &v, note);
    return v.pitch16 * 6.25 + v.scale_fine / 2.367;
}

/* -------------------------------------------------- what an old project does --- */
/* Every entry a project of before could hold, in every quantize mode, root and transposition: each key's and MIDI
 * note's mapping, the note's pitch, the chords. FNV-1a over all of it, taken from the firmware before tunings came
 * (3fe570d2): an entry that became a tuning plays exactly as it did. A microtonal scale's fixed chord shapes are left
 * out: on every entry they aim at just ratios now (microtonal_test). */
#define OLD_FINGERPRINT 0xeb2b1369u
static uint32_t fp;
static void fp_add(int32_t v)
{
    for (uint32_t i = 0; i < 4u; i++)
        fp = (fp ^ (((uint32_t)v >> (i * 8u)) & 255u)) * 16777619u;
}
static uint32_t old_entries_fingerprint(void)
{
    static const int8_t ROOTS[3] = {0, 2, 7}, TRANS[2] = {0, -5};
    static const uint8_t MODES[5] = {0, CH_DIA3, CH_DIA7, CH_MAJ, CH_MIN};
    track_t *t = fresh();
    fp = 2166136261u;
    for (uint32_t s = 0; s < 70u; s++)
        for (uint32_t q = Q_OFF; q <= Q_MPC; q++)
            for (uint32_t r = 0; r < NELEM(ROOTS); r++)
                for (uint32_t tr = 0; tr < NELEM(TRANS); tr++) {
                    t->p[P_SCALE] = (int16_t)s; t->p[P_QUANT] = (int16_t)q;
                    t->p[P_ROOT] = ROOTS[r]; t->p[P_TRANS] = TRANS[tr];
                    t->p[P_MPCDEG] = 1;
                    for (uint32_t k = 0; k < 27u; k++)
                        fp_add((int32_t)kb_map(t, k));
                    for (uint32_t n = 0; n < 128u; n++) {
                        voice_t v = {0};
                        fp_add((int32_t)midi_map(t, n));
                        voice_scale_pitch(t, &v, n);
                        fp_add(v.pitch16); fp_add(v.scale_fine);
                    }
                    for (uint32_t m = 0; m < NELEM(MODES); m++) {
                        uint8_t out[CHORD_MAX + 2u]; int32_t root; uint16_t mask; uint32_t c;
                        if (MODES[m] >= CH_MAJ && micro_active(t)) continue;
                        t->p[P_CHRD] = MODES[m];
                        c = chord_make(t, 60, out, &root, &mask);
                        fp_add((int32_t)c); fp_add(root); fp_add(mask);
                        for (uint32_t i = 0; i < c; i++) fp_add(out[i]);
                    }
                    t->p[P_CHRD] = 0;
                    fp_add((int32_t)scale_mask(t)); fp_add((int32_t)scale_count(t));
                }
    t->p[P_SCALE] = 1; t->p[P_QUANT] = Q_OFF; t->p[P_ROOT] = 0; t->p[P_TRANS] = 0;
    return fp;
}
static void old_projects(void)
{
    uint32_t got = old_entries_fingerprint();
    if (got != OLD_FINGERPRINT) printf("old entries fingerprint 0x%08xu, was 0x%08xu\n", got, OLD_FINGERPRINT);
    assert(got == OLD_FINGERPRINT);
    assert(!strcmp(N_SCALE[53], "WERCK3") && !strcmp(N_SCALE[64], "RAST-D") && scale_retired(53) && scale_retired(64));
    track_t *t = fresh();                               /* the report's project as it was: Rast's old id at QNT OFF */
    t->p[P_SCALE] = (int16_t)scale_id("RAST-D"); t->p[P_QUANT] = Q_OFF;
    assert(!tune_active(t) && !micro_active(t) && heard_cents(t, 64) == 6400.0);
    t->p[P_QUANT] = Q_ALL;                              /* .. and in degrees: note 62 its third degree, 350 cents */
    assert(fabs(heard_cents(t, 62) - 6350.0) < 0.5 && kb_map(t, 9) == 62);
    puts("scales and tunings: every old entry maps, pitches and voices chords as before, but a microtonal scale's fixed shapes");
}

/* ----------------------------------------------------------- the maqam case --- */
static void issue_case(void)
{
    static const int8_t OFF[12] = {0, 0, 0, 0, -50, 0, 0, 0, 0, 0, 0, -50};
    track_t *t = fresh();
    char b[8];
    go_page(GR_SCALE_PICKER);
    t->p[P_QUANT] = Q_OFF;
    scale_picker_select(scale_id("RAST"));
    assert(t->p[P_TUNING] == (int16_t)tuning_id("Rast"));
    for (uint32_t k = 0; k < NTRK; k++) assert(trk[k].p[P_TUNING] == t->p[P_TUNING]);
    assert(!micro_scale(t) && !micro_active(t) && tune_active(t));
    for (uint32_t k = 0; k < 27u; k++) assert(kb_map(t, k) == 53u + k);   /* the chromatic keyboard, as notated */
    for (uint32_t n = 24; n < 108u; n++) {
        assert(midi_map(t, n) == n);
        assert(fabs(heard_cents(t, n) - (n * 100.0 + OFF[n % 12u])) < 0.5);
    }
    note_name(b, 64); assert(!strcmp(b, "E4"));
    t->p[P_ROOT] = 2; scale_share(t);                   /* built on the root: Rast on D lowers F# and C# */
    assert(fabs(heard_cents(t, 66) - 6550.0) < 0.5 && fabs(heard_cents(t, 61) - 6050.0) < 0.5);
    assert(heard_cents(t, 64) == 6400.0);
    t->p[P_ROOT] = 0; t->p[P_TRANS] = 2; scale_share(t);   /* TRN takes it along, as the old entry's root did */
    assert(fabs(heard_cents(t, 66) - 6550.0) < 0.5 && heard_cents(t, 64) == 6400.0);
    t->p[P_TRANS] = 0;
    song.rec = song.playing = 1;                        /* a take keeps the notes played, not degrees */
    input_on(t, 64, 100); input_off(t, 64);
    assert(t->step[0].n == 1 && t->step[0].note[0] == 64);
    song.rec = song.playing = 0;
    puts("scales and tunings: #118 Rast at QNT OFF plays quarter tones on the chromatic keyboard");
}
static void maqams_keep_their_pitches(void)
{
    track_t *t = fresh();
    for (uint32_t k = 0; k < SCALE_TOTAL - SCALE_MICRO_END; k++) {
        const uint32_t now = SCALE_MICRO_END + k, old = SCALE_MAQAM_OLD + k;
        uint32_t deg = 0;
        assert(!strcmp(SCALE_TITLE[now], SCALE_TITLE[old]) && SCALE_DEGREES[now] == SCALE_DEGREES[old]);
        assert(SCALE_FAMILY[now] == SCALE_FAMILY[old] && !scale_retired(now) && scale_retired(old));
        for (uint32_t n = 0; n < 12u; n++) {
            int32_t was;
            t->p[P_SCALE] = (int16_t)now; t->p[P_TUNING] = SCALE_LINK[k]; t->p[P_QUANT] = Q_OFF;
            if (!((scale_mask(t) >> n) & 1u)) continue;
            int32_t then = scale_tuned(t) ? scale_pitch(t, 60 + (int32_t)n) : (60 + (int32_t)n) * 10000;
            t->p[P_SCALE] = (int16_t)old; t->p[P_TUNING] = 0; t->p[P_QUANT] = Q_ALL;
            was = micro_pitch(t, 60 + (int32_t)deg++);
            assert(then == was);
        }
        assert(deg == SCALE_DEGREES[now]);
    }
    /* WHITE: the white keys walk the seven notes, the same way */
    t->p[P_SCALE] = (int16_t)scale_id("BAYATI"); t->p[P_TUNING] = (int16_t)tuning_id("Bayati, saba"); t->p[P_QUANT] = Q_WHITE;
    assert(kb_map(t, 7) == 60 && kb_map(t, 9) == 62 && kb_map(t, 11) == 63 && fabs(heard_cents(t, 62) - 6150.0) < 0.5);
    puts("scales and tunings: each maqam's scale on its tuning plays the pitches of the entry it took over from");
}
static double audio_frequency(track_t *t)
{
    int32_t out[CTL], previous = 0;
    double first = 0, last = 0; uint32_t crossings = 0;
    for (uint32_t at = 0; at < FS / 2; at += CTL) {
        track_render(t, out, CTL);
        for (uint32_t k = 0; k < CTL; k++) {
            if (at + k > 4000 && previous < 0 && out[k] >= 0) {
                double x = at + k - (double)out[k] / (out[k] - previous);
                if (!crossings++) first = x;
                last = x;
            }
            previous = out[k];
        }
    }
    return crossings > 1 ? (crossings - 1) * FS / (last - first) : 0;
}
static void rendered(void)                              /* FM6's INIT VOICE: one sine; what comes out of the engine */
{
    track_t *t = fresh();
    t->eng_req = t->engine = ENGI_FM6;
    eng_state_reset(); fm6_fn_reset();
    uint8_t patch[FP_SIZE + 1]; fm6_unpack(FM6_INIT, patch); fm6_put_patch(0, patch, 1);
    for (uint32_t j = P_E0; j < P_COUNT; j++) t->p[j] = 0;
    t->p[P_GLIDE] = t->p[P_LD_PIT] = t->p[P_ED_PIT] = 0;
    t->p[P_ATK] = 0; t->p[P_SUS] = 127;
    t->p[P_SCALE] = (int16_t)scale_id("RAST"); t->p[P_TUNING] = (int16_t)tuning_id("Rast"); t->p[P_QUANT] = Q_OFF;
    trk_note_on(t, 64, 100);
    double measured = audio_frequency(t), expected = 440 * pow(2, (63.5 - 69) / 12);
    double error = fabs(1200 * log2(measured / expected));
    printf("scales and tunings: E half-flat from FM6 %.3f Hz, error %.3f cents\n", measured, error);
    assert(error < 0.6);
}

/* --------------------------------------------------------------- the tunings --- */
static void tunings(void)
{
    track_t *t = fresh();
    assert(!TUNING[0].pitch && !t->p[P_TUNING] && !tune_active(t));
    for (uint32_t tn = 1; tn < TUNING_COUNT; tn++) {
        const int32_t *p = TUNING[tn].pitch;
        assert(strlen(N_TUNING[tn]) < 16u && p[0] == 0 && p[12] == 120000);
        for (uint32_t i = 0; i < 12u; i++) assert(p[i] < p[i + 1]);
    }
    for (uint32_t s = SCALE_LEGACY; s < SCALE_MICRO_END; s++) {   /* an entry that became a tuning: the pitches it had */
        uint32_t tn = 0;
        for (uint32_t i = 1; i < TUNING_COUNT; i++) if (TUNING[i].pitch == MICRO_SCALE[s - SCALE_LEGACY].pitch) tn = i;
        if (!tn) continue;
        assert(scale_retired(s));
        for (uint32_t n = 0; n < 12u; n++) {
            int32_t was, then;
            t->p[P_SCALE] = (int16_t)s; t->p[P_TUNING] = 0; t->p[P_QUANT] = Q_ALL; t->p[P_ROOT] = 0;
            was = micro_pitch(t, 60 + (int32_t)n);
            t->p[P_SCALE] = 0; t->p[P_TUNING] = (int16_t)tn; t->p[P_QUANT] = Q_OFF;
            then = scale_pitch(t, 60 + (int32_t)n);
            assert(then == was);
        }
    }
    /* a temperament stays on C, so each key keeps its colour; a just tuning moves with the root */
    t->p[P_SCALE] = 2; t->p[P_QUANT] = Q_OFF; t->p[P_TUNING] = (int16_t)tuning_id("Werckmeister 3");
    t->p[P_ROOT] = 0; int32_t on_c = tune_off(t, 69);
    t->p[P_ROOT] = 9; assert(tune_off(t, 69) == on_c && on_c);
    t->p[P_TRANS] = 3; assert(tune_off(t, 69) == on_c); t->p[P_TRANS] = 0;
    t->p[P_TUNING] = (int16_t)tuning_id("Just chromatic");
    t->p[P_ROOT] = 9; assert(tune_off(t, 69) == 0 && tune_off(t, 73) == 38631 - 40000);
    /* a scale still says which notes: any scale in any tuning, the Western tools working */
    t->p[P_ROOT] = 0; t->p[P_SCALE] = 1; t->p[P_TUNING] = (int16_t)tuning_id("Vallotti"); t->p[P_QUANT] = Q_WHITE;
    assert(scale_mask(t) == SCALE_MASK[1] && scale_count(t) == 7 && kb_map(t, 7) == 60 && kb_map(t, 9) == 62);
    uint8_t out[CHORD_MAX + 2u]; int32_t root; uint16_t mask;
    t->p[P_CHRD] = CH_DIA3;
    assert(chord_make(t, 60, out, &root, &mask) == 3 && out[1] == 64 && out[2] == 67);
    t->p[P_CHRD] = CH_AUTO;
    assert(chord_make(t, 60, out, &root, &mask) >= 3);   /* (AUTO has its key: a microtonal scale has none) */
    t->p[P_CHRD] = 0; t->p[P_QUANT] = Q_OFF;
    /* what passes a tuning by */
    t->p[P_SCALE] = (int16_t)scale_id("19EDO");
    assert(!tune_active(t));                             /* a microtonal scale tunes itself */
    t->p[P_QUANT] = Q_OFF; assert(heard_cents(t, 69) == 6900.0);
    t->p[P_SCALE] = 1; assert(tune_active(t));
    t->eng_req = ENGI_DRUM; assert(!tune_active(t)); t->eng_req = t->engine;
    t->p[P_TUNING] = TUNING_COUNT; assert(!tune_active(t));
    t->p[P_TUNING] = 0;
    puts("scales and tunings: tables, what each is built on, any scale in any tuning, what passes one by");
}
static void linked_picks(void)
{
    track_t *t = fresh();
    const int16_t rast = (int16_t)tuning_id("Rast"), werck = (int16_t)tuning_id("Werckmeister 3");
    go_page(GR_SCALE_PICKER);
    scale_picker_select(scale_id("RAST")); assert(t->p[P_TUNING] == rast);
    scale_picker_select(scale_id("SIKAH")); assert(t->p[P_TUNING] == (int16_t)tuning_id("Sikah"));
    scale_picker_select(1); assert(t->p[P_TUNING] == 0);          /* leaving a maqam takes its tuning along .. */
    t->p[P_TUNING] = rast; scale_share(t);
    scale_picker_select(3); assert(t->p[P_TUNING] == rast);       /* .. not one chosen by hand on any other scale */
    scale_picker_select(scale_id("RAST")); t->p[P_TUNING] = werck; scale_share(t);
    scale_picker_select(1); assert(t->p[P_TUNING] == werck);      /* .. nor one changed since */
    scale_picker_select(scale_id("HIJAZ")); assert(t->p[P_TUNING] == 0);   /* (Hijaz is equal) */
    t->p[P_TUNING] = werck; scale_share(t);
    scale_picker_select(scale_id("19EDO")); assert(t->p[P_TUNING] == werck);
    scale_picker_select(2); assert(t->p[P_TUNING] == werck);      /* (a microtonal scale leaves it waiting) */
    puts("scales and tunings: a maqam picked brings its tuning, each still its own");
}

/* --------------------------------------------------------------- the screens --- */
static void picker(void)
{
    track_t *t = fresh();
    go_page(GR_SCALE_PICKER);
    ui.scale_family = 0; scale_picker_select(1);
    assert(scale_picker_count() == 60u);
    for (uint32_t r = 0; r < 60u; r++) assert(!scale_retired(scale_picker_at(r)));
    /* a retired entry: listed while selected, gone once left */
    scale_picker_select(scale_id("WERCK3"));
    assert(scale_picker_count() == 61u && scale_picker_match(scale_id("WERCK3")));
    ui.scale_family = (uint8_t)(SCALE_FAMILY[scale_id("WERCK3")] + 1u);
    assert(scale_picker_count() == 1u && scale_picker_rank() == 0u);
    edit_param(0, 1);                                    /* KNOB 1 on: the next family's first scale, the entry left */
    assert(ui.scale_family == 6u && !scale_retired((uint32_t)t->p[P_SCALE]));
    /* Historical has only tunings now: KNOB 1 passes it by, both ways */
    edit_param(0, -1); assert(ui.scale_family == 4u);
    edit_param(0, 1); assert(ui.scale_family == 6u);
    edit_param(0, -100); assert(ui.scale_family == 0u);
    edit_param(0, 100); assert(ui.scale_family == SCALE_FAMILIES + 1u);
    ui.scale_family = 8u; scale_picker_select(scale_picker_at(0));
    assert(scale_picker_count() == 6u && t->p[P_SCALE] == (int16_t)SCALE_MICRO_END);
    /* a retired maqam's star is on its scale now, in the store it was in */
    ui.scale_family = 0;
    assert(!scale_favorite(SCALE_MICRO_END));
    assert(scale_favorite_set(SCALE_MAQAM_OLD, 1) && scale_favorite(SCALE_MICRO_END));
    ui.scale_family = SCALE_FAMILIES + 1u; scale_picker_select(SCALE_MICRO_END);
    assert(scale_picker_count() == 1u && scale_picker_at(0) == SCALE_MICRO_END);
    assert(scale_favorite_set(SCALE_MICRO_END, 0) && !scale_favorite(SCALE_MAQAM_OLD));
    /* the steps that know no families */
    assert(scale_land(48, 49) == 57 && scale_land(57, 56) == 48 && scale_land(63, 64) == 70 && scale_land(70, 69) == 63);
    assert(scale_land(1, 2) == 2 && scale_land(5, 5) == 5);
    puts("scales and tunings: SCALES leaves the retired entries out, stars kept, an empty family passed");
}
static void scl_held(void)
{
    track_t *t = fresh();
    go_home();
    t->p[P_SCALE] = 48; scale_share(t);                  /* HARRIS, then the tunings */
    lay_combo(B_SCL, black(4)); key_up(black(4)); frame();
    turn(EN_K2, 1); frame();
    assert(t->p[P_SCALE] == 57);
    t->p[P_SCALE] = 63; scale_share(t);                  /* BP-JI: the retired maqams, the new Rast and its tuning */
    turn(EN_K2, 1); frame();
    assert(t->p[P_SCALE] == 70 && t->p[P_TUNING] == (int16_t)tuning_id("Rast"));
    oct_back();                                          /* OCT-: as it opened, its tuning too */
    assert(t->p[P_SCALE] == 48 && t->p[P_TUNING] == 0);
    for (uint32_t k = 0; k < NTRK; k++) assert(trk[k].p[P_TUNING] == 0);
    btn_up(B_SCL); frame();
    puts("scales and tunings: SCL held steps past the retired entries, OCT- puts the tuning back");
}
static void list_row(void)
{
    track_t *t = fresh();
    char label[16], value[16];
    uint16_t color;
    go_title("SCL");
    const list_t *list = list_on();
    assert(list && list_n(list) == 6u);                  /* Root, Scale, Tuning, Quantize, Transpose, Favourite */
    list_cap(list_at(list, 2), label, value, &color);
    assert(!strcmp(label, "Tuning") && !strcmp(value, "Equal"));
    turn(EN_K2, -100); frame();
    turn(EN_K2, 2); frame();
    turn(EN_K1, 1); frame();                             /* KNOB 1 its value */
    assert(t->p[P_TUNING] == 1);
    for (uint32_t k = 0; k < NTRK; k++) assert(trk[k].p[P_TUNING] == 1);
    press(B_OCTUP); frame();                             /* OCT+ its whole list */
    assert(pop.on == POP_LIST);
    for (uint32_t i = 0; i < 6u; i++) { turn(EN_K2, 1); frame(); }
    assert(t->p[P_TUNING] == 1);
    press(B_OCTUP); frame();
    assert(pop.on == POP_NONE && t->p[P_TUNING] == (int16_t)tuning_id("Werckmeister 3"));
    for (uint32_t k = 0; k < NTRK; k++) assert(trk[k].p[P_TUNING] == t->p[P_TUNING]);
    list_cap(list_at(list, 2), label, value, &color);
    assert(!strcmp(value, "Werckmeister 3"));
    for (uint32_t tn = 0; tn < TUNING_COUNT; tn++) {     /* every name whole in a row */
        t->p[P_TUNING] = (int16_t)tn;
        list_cap(list_at(list, 2), label, value, &color);
        assert(!strcmp(value, N_TUNING[tn]));
    }
    t->p[P_TUNING] = 3;
    /* a sound loaded leaves it, as it leaves the scale */
    apply_preset_to(t, 1);
    assert(t->p[P_TUNING] == 3 && param_kept(P_TUNING) && !param_reserved(P_TUNING));
    /* a microtonal scale or a retired entry: no row */
    t->p[P_SCALE] = (int16_t)scale_id("19EDO");
    assert(list_n(list) == 5u);
    for (uint32_t r = 0; r < list_n(list); r++) {
        list_cap(list_at(list, r), label, value, &color);
        assert(strcmp(label, "Tuning"));
    }
    t->p[P_SCALE] = (int16_t)scale_id("WERCK3");
    assert(list_n(list) == 5u);
    list_cap(list_at(list, 1), label, value, &color);
    assert(!strcmp(value, "Werck3"));
    t->p[P_SCALE] = 1;
    frames(3);
    puts("scales and tunings: SCL's Tuning row, its value and list, shared, kept by sounds, hidden where it means nothing");
}

/* ------------------------------------------------------------- what is stored --- */
/* a project of before: format FUN4's fixed parts, np parameters (tests/motion_test.c) */
static void pack_np(project_store_t *out, const project_t *q, uint32_t np)
{
    uint8_t *b = out->raw; uint32_t pos = 68u, t, i, sum;
    motion_store_t m; memset(&m, 0, sizeof m);
    proj_pack(out, q);
    memset(b + 68, 0, PROJ_FM6_OFF - 68u);
    uint32_t magic = 0x46554E43u; memcpy(b, &magic, 4); memcpy(b + 8, q->g, 2u * PROJ_GLOBALS);
    b[66] = (uint8_t)np;
    for (t = 0; t < NPART; t++) {
        for (i = 0; i < np; i++) b[pos++] = (uint8_t)(q->t[t].p[i < np - 8u ? i : i - (np - 8u) + P_E0] + 64);
        b[pos++] = q->t[t].engine; b[pos++] = q->t[t].preset;
        for (i = 0; i < NSTEP; i++) {
            const step_t *s = &q->t[t].step[i];
            memcpy(b + pos, s->note, 4); pos += 4;
            b[pos++] = (uint8_t)(s->n | s->time << 3 | s->flags << 5);
            b[pos++] = s->vel; b[pos++] = s->hit; b[pos++] = s->acc; b[pos++] = s->probability;
        }
    }
    memcpy(b + pos, &q->chain, sizeof q->chain); pos += sizeof q->chain;
    memcpy(b + pos, &m, sizeof m);
    sum = proj_hash(b, PROJ_STORE_SIZE - 4u); memcpy(b + PROJ_STORE_SIZE - 4u, &sum, 4);
}
static void stored(void)
{
    static project_t before, after;
    static project_store_t wire, old;
    track_t *t = fresh();
    const int16_t werck = (int16_t)tuning_id("Werckmeister 3");
    /* P_COUNT and every id stay where they were */
    assert(P_TUNING == 47u && P_COUNT == 124u && TP[P_TUNING].max == (int16_t)(TUNING_COUNT - 1u) && !TP[P_TUNING].def);
    /* a project with a tuning, through its format and back */
    t->p[P_SCALE] = 1; t->p[P_TUNING] = werck; scale_share(t);
    project_capture(&before); assert(proj_pack(&wire, &before));
    assert(proj_unpack(&after, wire.raw));
    for (uint32_t k = 0; k < PROJ_TRK; k++) assert(after.t[k].p[P_TUNING] == werck && after.t[k].p[P_SCALE] == 1);
    t->p[P_TUNING] = 0; scale_share(t);
    assert(!project_restore_runtime(&after));
    for (uint32_t k = 0; k < NTRK; k++) assert(trk[k].p[P_TUNING] == werck);
    /* the format stays the one older firmware reads: the same magic, the slot a byte of it */
    assert(!memcmp(wire.raw, "IUNF", 0) && *(const uint32_t *)wire.raw == PROJ_MAGIC);
    /* a project of before: what the slot held then (a slicer's rate) is no tuning */
    before.t[0].p[P_TUNING] = before.t[1].p[P_TUNING] = 2;
    before.t[2].p[P_TUNING] = before.t[3].p[P_TUNING] = 127;
    pack_np(&old, &before, 104u);
    assert(proj_import_same(&after, old.raw, PROJ_STORE_SIZE));
    for (uint32_t k = 0; k < PROJ_TRK; k++) assert(after.t[k].p[P_TUNING] == 0);
    /* a retired entry, its take in degrees, saved and back: the stored value, the same notes, the same pitches */
    t = fresh();
    t->p[P_SCALE] = (int16_t)scale_id("KIRN3"); t->p[P_QUANT] = Q_ALL; t->p[P_ROOT] = 2; scale_share(t);
    t->step[0] = (step_t){{60, 64, 67}, 3, ST_NOTE, 0, 100};
    double was = heard_cents(t, 64);
    project_capture(&before); assert(proj_pack(&wire, &before)); assert(proj_unpack(&after, wire.raw));
    t->p[P_SCALE] = 1; t->p[P_QUANT] = Q_OFF; t->p[P_ROOT] = 0; scale_share(t);
    assert(!project_restore_runtime(&after));
    t = TSEL;
    assert(t->p[P_SCALE] == (int16_t)scale_id("KIRN3") && !t->p[P_TUNING] && t->step[0].note[1] == 64);
    assert(micro_active(t) && heard_cents(t, 64) == was);
    /* old automation on the slot stays valid and does nothing */
    t = fresh();
    motion_store_t m; memset(&m, 0, sizeof m);
    m.count = 1; m.event[0] = (motion_event_t){0, P_TUNING, 127};
    assert(motion_valid(&m) && !motion_param(P_TUNING) && !motion_capture(t, P_TUNING, 5));
    m.on = 1;
    motion = m; motion_pattern[0] = t->pattern;
    motion_step(t, 0, &m);
    assert(t->p[P_TUNING] == 0);
    { uint16_t ids[MO_MAX]; assert(!mo_lanes(ids)); }
    memset(&motion, 0, sizeof motion);
    /* the template keeps it */
    t->p[P_SCALE] = 1; t->p[P_TUNING] = werck; scale_share(t);
    template_save(); assert(tmpl.t[0].p[P_TUNING] == werck);
    t->p[P_TUNING] = 0; scale_share(t);
    template_load();
    for (uint32_t k = 0; k < NTRK; k++) assert(trk[k].p[P_TUNING] == werck);
    puts("scales and tunings: stored in a retired slot, no layout moved, older records read as Equal, old motion inert");
}

int main(void)
{
    old_projects();
    issue_case();
    maqams_keep_their_pitches();
    rendered();
    tunings();
    linked_picks();
    picker();
    scl_held();
    list_row();
    stored();
    puts("scales and tunings passed");
    return 0;
}
