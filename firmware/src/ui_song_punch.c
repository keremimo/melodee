/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Named punch lanes share the section's numbered step grid. Start moves the
 * range; END edits its inclusive right edge, without duration arithmetic. */
static struct { uint32_t title, context, ruler, lane[4], card[4]; } spv;
static uint32_t punch_selector_leds(int32_t fx)
{
    return (((fm1_ms / 250u) & 1u) == 0u ? 0x7ffffffu : 0u) |
        (fx >= 0 ? 1u << punch_key_of_fx((uint32_t)fx) : 0u);
}
static uint32_t song_punch_leds(void)
{
    return punch_selector_leds((int32_t)chain_config.fx[song_punch.row][song_punch.event].fx - 1);
}
static void song_punch_change(uint32_t field, int32_t delta)
{
    uint8_t force = ui.force;
    song_punch_edit(field, delta);
    ui.force = force;                               /* changed contents invalidate their own lane and cards */
}
static void song_punch_choose(uint32_t fx)
{
    chain_fx_t *e = &chain_config.fx[song_punch.row][song_punch.event];
    song_punch_change(0, (int32_t)fx + 1 - e->fx);
}
static void song_punch_next(void)
{
    for (uint32_t i = 1; i <= CHAIN_FX_EVENTS; i++) {
        uint32_t j = (song_punch.event + i) % CHAIN_FX_EVENTS;
        if (!chain_config.fx[song_punch.row][j].fx) { song_punch.event = (uint8_t)j; return; }
    }
    ui_message("ALL FOUR IN USE");
}
static void song_punch_input(uint32_t notes, uint32_t pressed, uint32_t oct, int erase)
{
    if (ui.home || cur_page()->graph != GR_SONG) { pop_close(); return; }
    if (pressed & (1u << panel.btn[B_PLAY])) {
        if (song.playing || seq_counting() || chain_busy()) transport_req = 2;
        else chain_play_ui();
    }
    if (oct & 1u) { pop_close(); return; }
    int32_t delta = panel_enc(EN_SELECT);
    if (delta) {
        song_punch.row = (uint8_t)clamp((int32_t)song_punch.row + delta, 0, chain_config.count - 1u);
        ui.song_row = song_punch.row;
    }
    delta = panel_enc(EN_K2);
    if (delta) song_punch.event = (uint8_t)clamp((int32_t)song_punch.event + delta, 0, CHAIN_FX_EVENTS - 1u);
    if (oct & 2u) song_punch_next();
    chain_fx_t *e = &chain_config.fx[song_punch.row][song_punch.event];
    if (erase) song_punch_change(0, -(int32_t)e->fx);
    delta = panel_enc(EN_K1);
    if (delta) {
        int32_t key = e->fx ? (int32_t)punch_key_of_fx(e->fx - 1u) : -1;
        key = clamp(key + delta, -1, 26);
        if (key < 0) song_punch_change(0, -(int32_t)e->fx);
        else song_punch_choose((uint32_t)punch_fx_of_key((uint32_t)key));
    }
    for (uint32_t k = 0; k < 27u; k++) if ((notes >> k) & 1u) song_punch_choose((uint32_t)punch_fx_of_key(k));
    delta = panel_enc(EN_K3);
    if (delta) song_punch_change(1, delta);
    delta = panel_enc(EN_K4);
    if (delta) song_punch_change(2, delta);
}
/* Four lanes remain visible even when a different punch is selected. The grid
 * uses the saved conductor steps (also for rests and continuations), like DSP. */
static void song_punch_draw(void)
{
    const chain_fx_t *e = &chain_config.fx[song_punch.row][song_punch.event];
    uint32_t steps = song_punch_steps_at(song_punch.row);
    uint32_t style = ux.gen * 977u + ux.pal * 31u;
    uint32_t sig = e->fx + style;
    int32_t play = -1;
    if (chain.running && chain.row == song_punch.row)
        play = chain_patterns[chain.row][0] >= NPAT ?
            (int32_t)(chain.rest_pos / seq_div_samples(2u)) : (int32_t)trk[0].seq_idx;
    char b[32];
    draw_head();
    if (ui.force || spv.title != sig) {
        spv.title = sig;
        cv_begin(240, 36, T_BG);
        cv_text_on(8, ty_y(9, TY_LB), TY_LB, e->fx ? "MASTER FX" : "KEYS: CHOOSE FX", T_MID, T_BG);
        if (e->fx) cv_text_r(232, ty_y(9, TY_LB), TY_LB, "EACH REPEAT", T_MID, T_BG);
        cv_text_on(8, ty_y(26, TY_TX), TY_TX, e->fx ? PUNCH_NAME[e->fx - 1u] : "OFF",
                   e->fx ? punch_color(e->fx - 1u) : T_TEXT, T_BG);
        cv_blit(0, H_HEAD);
    }
    sig = song_punch.row + steps * 256u + style;
    if (ui.force || spv.context != sig) {
        spv.context = sig;
        cv_begin(240, 18, T_BG);
        str_cpy(b, "SECTION ", sizeof b); fmt_int(b + 8, song_punch.row + 1u);
        cv_text_on(8, ty_y(9, TY_LB), TY_LB, b, T_THEME, T_BG);
        fmt_int(b, steps); str_cpy(b + str_len(b), " STEPS", 7);
        cv_text_r(232, ty_y(9, TY_LB), TY_LB, b, T_MID, T_BG);
        cv_blit(0, 66);
    }
    sig = steps + style;
    if (ui.force || spv.ruler != sig) {
        spv.ruler = sig;
        cv_begin(240, 18, T_BG);
        uint32_t stride = steps <= 16u ? 4u : steps <= 32u ? 8u : 16u;
        for (uint32_t i = 0; i < steps; i += stride) {
            fmt_int(b, i + 1u);
            cv_text_on(8 + i * 224u / steps, ty_y(8, TY_LB), TY_LB, b, T_MID, T_BG);
        }
        cv_blit(0, 84);
    }
    for (uint32_t j = 0; j < CHAIN_FX_EVENTS; j++) {
        const chain_fx_t *p = &chain_config.fx[song_punch.row][j];
        sig = fnv(style, p, sizeof *p);
        sig = fnv1(fnv1(sig, steps + 128u * (j == song_punch.event)), (uint32_t)(play + 1));
        if (!ui.force && spv.lane[j] == sig) continue;
        spv.lane[j] = sig;
        uint16_t c = p->fx ? punch_color(p->fx - 1u) : T_DIM;
        uint16_t bg = j == song_punch.event ? T_LIFT : T_BG;
        cv_begin(240, 25, T_BG);
        cv_rrect(4, 0, 232, 25, 3, bg, T_BG);
        fmt_int(b, j + 1u); str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), p->fx ? PUNCH_NAME[p->fx - 1u] : "OFF", sizeof b - str_len(b));
        const aafont_t *font = ty_fit(TY_LB, b, 220);
        cv_text_on(8, ty_y(8, font), font, b, p->fx ? c : T_MID, bg);
        for (uint32_t i = 0; i < steps; i++) {
            int32_t x = 8 + i * 224u / steps;
            int32_t w = (i + 1u) * 224u / steps - i * 224u / steps;
            int active = p->fx && i >= p->start && i < (uint32_t)p->start + p->length;
            cv_rect(x, 17, w - (w > 2), 6, active ? c : T_SURF);
        }
        if (p->fx && p->start < steps && j == song_punch.event) {
            uint32_t end = p->start + p->length;
            if (end > steps) end = steps;
            int32_t x = 8 + p->start * 224u / steps;
            int32_t right = 8 + end * 224u / steps;
            cv_rect(x, 15, right - x, 1, T_TEXT);
            cv_rect(x, 24, right - x, 1, T_TEXT);
        }
        if (play >= 0 && (uint32_t)play < steps)
            cv_rect(8 + (uint32_t)play * 224u / steps, 15, 1, 10, T_TEXT);
        cv_blit(0, 102 + j * 25u);
    }
    static const char *const label[] = {"FX", "SLOT", "START", "END"};
    static const uint8_t xpos[] = {4, 62, 120, 178};
    for (uint32_t k = 0; k < 4u; k++) {
        if (!k) {
            if (!e->fx) str_cpy(b, "OFF", sizeof b);
            else {
                static const char *const note_name[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                uint32_t note = 53u + punch_key_of_fx(e->fx - 1u);
                str_cpy(b, note_name[note % 12u], sizeof b); fmt_int(b + str_len(b), note / 12u - 1u);
            }
        }
        else if (k == 1u) { fmt_int(b, song_punch.event + 1u); str_cpy(b + str_len(b), "/4", 3); }
        else if (!e->fx) str_cpy(b, "--", sizeof b);
        else fmt_int(b, k == 2u ? e->start + 1u : e->start + e->length);
        sig = str_hash(style, b);
        if (!ui.force && spv.card[k] == sig) continue;
        spv.card[k] = sig;
        cv_begin(56, 36, T_BG);
        cv_rrect(0, 0, 56, 36, 4, T_SURF, T_BG);
        cv_text_c(28, ty_y(9, TY_LB), TY_LB, label[k], T_MID, T_SURF);
        cv_text_c(28, ty_y(23, TY_LB), TY_LB, b, T_TEXT, T_SURF);
        cv_blit(xpos[k], 204);
    }
}
