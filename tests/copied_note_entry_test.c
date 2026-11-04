/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Exercise the panel/MIDI entry path, not just the pitch-edit helper. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void entry_input(uint32_t source, uint32_t key, uint32_t note, int down)
{
    if (!source) {
        fm1_in.notes = down ? 1u << key : 0;
        host_notes = down ? 1u << key : 0;
    } else {
        uint32_t status = down ? 0x90u : 0x80u;
        midi_enqueue((status >> 4) | status << 8 | note << 16 |
                     (down ? 100u : 0u) << 24, source);
        events_block(CTL);
    }
    frame();
}

static int copied_entry(uint32_t source, uint32_t moved, uint32_t mode)
{
    ui_power_on();
    set_engine_of(TSEL, ENGI_FM6);
    track_defaults_steps(TSEL);
    TSEL->p[P_SLEN] = 16;
    go_page(GR_ROLL);
    frame();
    recorded_note_t original = {.step = 2, .on = 5000, .note7 = 60,
        .vel = 83, .duration = 23000, .owner = (uint8_t)recording_owner(TSEL)};
    recording_restore_note(TSEL, 0, original);
    note_rebuild_all();
    cursor_set(2);
    frame();
    note_move_snapped(moved ? 1 : 0, RECORD_UNIT / 4u, 1);
    uint32_t selected = notes_selected(TSEL);
    int bad = check("copy fixture selects a distinct precise event",
                    selected < RECORD_MAX && selected != 0 && recording_active(TSEL, selected));
    if (bad) return bad;
    recorded_note_t saved[RECORD_MAX];
    memcpy(saved, recording, sizeof saved);
    uint32_t cursor = ui.cursor;
    uint32_t key = 0;
    while (key < 27u && (kb_map(TSEL, key) == 60 || kb_map(TSEL, key) == KB_SILENT)) key++;
    if (key == 27u) return bad + check("fixture has an alternate playable key", 0);
    uint32_t pitch = source ? 69u : kb_map(TSEL, key);
    song.playing = mode == 1;
    song.rec = mode == 2 ? 1u << song.sel : 0;
    ui.msg[0] = 0;
    entry_input(source, key, pitch, 1);
    entry_input(source, key, pitch, 0);
    printf("entry case: %s, %s copy, %s\n", source == 0 ? "panel" : source == 1 ? "USB" : "TRS",
           moved ? "moved" : "overlapping", mode == 0 ? "stopped" : mode == 1 ? "playing" : "armed");
    if (!mode) {
        recorded_set_note(&saved[selected], (uint8_t)pitch);
        bad += check("entry changes only the selected copy's pitch, preserving every other event byte",
                     !memcmp(saved, recording, sizeof saved));
        bad += check("entry retains copy selection/cursor and never opens step replacement",
                     notes_selected(TSEL) == selected && ui.cursor == cursor && !ui.entry_open &&
                     strcmp(ui.msg, "STOP TO EDIT"));
        hold(B_EDIT);
        recorded_set_note(&saved[selected], 60);
        bad += check("one undo restores the copy's original pitch and leaves its source intact",
                     !memcmp(saved, recording, sizeof saved));
    } else {
        /* Armed input may record new events; it must never rewrite an existing one. */
        bad += check("playing/armed input preserves both source and selected copy",
                     !memcmp(&saved[0], &recording[0], sizeof saved[0]) &&
                     !memcmp(&saved[selected], &recording[selected], sizeof saved[selected]) && !ui.entry_open);
    }
    return bad;
}

int main(void)
{
    int bad = 0;
    for (uint32_t source = 0; source < 3; source++)
        for (uint32_t moved = 0; moved < 2; moved++)
            for (uint32_t mode = 0; mode < 3; mode++)
                bad += copied_entry(source, moved, mode);
    printf("Copied note entry: %s\n", bad ? "FAILED" : "passed");
    return !!bad;
}
