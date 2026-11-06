/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The SMK-37's main-loop controls, main.c's and the fm1-base build's (platform/melodee_app.c): TRS MIDI OUT's setting
 * and the faders. (The wheels' readings differ: main.c smk_wheels takes the scan's raw mux C, the base gives a bend and
 * a mod already.) */
#pragma once

/* the menu's TRS MIDI OUT to the jack's queue (settings load, restore, the menu): OFF ends what the jack sounds first,
 * ON again tries the UART again. The note paths and the ring are the audio ISR's: the IRQs are off meanwhile */
static void smo_sync(void)
{
    uint32_t on = !(GLO_SELECT & TRS_OUT_OFF);
    if (on == trs_out)
        return;
    fm1_irq_off();
    midi_trs_set(on);
    fm1_irq_on();
}

/* the SMK-37's 4 faders (ui_smk.c: FADER BANK, smk_fbank): each fader its bank's parameter (LEVELS: track i's
 * level; PAGE: the page's four, as the knobs'). A fader is away after a bank, track or sound change, and whenever
 * its parameter changed under it, until it crosses the value, or comes next to it (no jumps): then it follows, as a
 * MIDI knob's absolute value does (ui_smk.c smk_fader_poll, smk_fader_set).
 * A move brings the bank's cards into the strip for ST_KNOB_MS with a pickup gauge under each (smk_fader_ms). With
 * NOTE REPEAT (the FM-1's LFO) held a fader's edit is momentary, as a knob's is (ui.c momentary_take): released, the
 * value before comes back, and motion does not record it. LEVELS works everywhere; the other banks where the page has
 * a strip (bank_knobs_on). */
/* id: the selected track's parameter the fader sets (P_COUNT: none); native: 2 CZ-1, 3 PROPHET (the whole sound).
 * As ui.c momentary_take: the store keeps parameter ids of the captured track and one native patch */
static void smk_fader_momentary(uint32_t id, uint32_t native)
{
    uint32_t i, tr = song.sel;
    if (!(fm1_in.buttons & (1u << panel.btn[B_LFO])))
        return;
    if (!momentary.active) {
        momentary.track = (uint8_t)song.sel;
        momentary.active = 1;
    }
    if (native && !momentary.native) {
        fm1_irq_off();
        momentary.native = (uint8_t)native;
        if (native == 2u) {
            momentary.cz = *cz_patch_of(tr);
        } else {
            momentary.p5 = *p5_patch_of(TSEL);
            momentary.p5_voice = TSEL->p[P_VOICE];
            momentary.p5_prio = TSEL->p[P_PRIO];
            momentary.p5_detune = TSEL->p[P_DETUNE];
            momentary.p5_alloc = TSEL->p[P_ALLOC];
        }
        fm1_irq_on();
    }
    if (id < P_COUNT && momentary.track == song.sel) {
        for (i = 0; i < momentary.count && momentary.id[i] != id; i++)
            ;
        if (i == momentary.count && i < P_COUNT + G_COUNT) {
            momentary.id[i] = (uint8_t)id;
            momentary.value[i] = TSEL->p[id];
            momentary.count++;
        }
    }
    ui.pg_down &= (ui_btnmask_t)~(1u << panel.btn[B_LFO]);   /* (the release opens no LFO page) */
}
static void smk_faders(void)
{
    static uint8_t last[4] = {0xFF, 0xFF, 0xFF, 0xFF}, sel = 0xFF, eng = 0xFF;
    uint32_t i;
    if (smk_fbank_new || sel != song.sel || eng != TSEL->eng_req) {   /* a new bank, track or sound: every fader away */
        smk_fbank_new = 0;
        sel = (uint8_t)song.sel;
        eng = TSEL->eng_req;
        smk_fpick = 0;
    }
    for (i = 0; i < 4u; i++) {
        int32_t v = fm1_in.fader[i], prev;
        int moved;
        if (last[i] == 0xFF || fm1_in.frames < 200u) {   /* (the first readings settle) */
            last[i] = (uint8_t)v;
            continue;
        }
        prev = last[i];
        moved = v != prev;
        last[i] = (uint8_t)v;
        if (moved) {
            smk_fader_ms = fm1_ms | 1u;
            smk_last_fader = 1;
        }
        smk_fader_poll(i, v, prev, moved);
    }
}
