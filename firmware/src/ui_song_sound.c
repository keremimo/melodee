/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The normal browser auditions into its owning track. A separate bounded
 * snapshot restores that sound on every exit, including navigation away.
 * OCT+ publishes one immutable sound across the selected pattern block. */
typedef struct {
    song_sound_t base;
    uint8_t fm[FP_SIZE + 1u], fm_on;
    cz_patch_t cz; p5_patch_t p5; drum_patch_t drum;
} song_preview_t;
static void song_sound_mark(uint8_t *used, const chain_config_t *c)
{
    for (uint32_t r = 0; r < c->count; r++) for (uint32_t k = 0; k < NTRK; k++)
        if (c->sound[r][k] && c->sound[r][k] <= CHAIN_SOUNDS) used[c->sound[r][k] - 1u] = 1;
}
static uint32_t song_sound_store(const song_sound_t *sound)
{
    uint8_t used[CHAIN_SOUNDS] = {0};
    for (uint32_t k = 0; k < NTRK; k++) for (uint32_t b = 0; b < NPAT; b++)
        if (pattern_sound[k][b]) used[pattern_sound[k][b] - 1u] = 1;
    undo_levels();
    for (uint32_t i = 0; i < undo_cnt; i++)
        if ((undo_lv[i].what & UNDO_PATTERN_SOUND) && undo_lv[i].pattern_sound)
            used[undo_lv[i].pattern_sound - 1u] = 1;
    if (resource[RES_NOTE_HISTORY].size && note_transaction_ptr && note_transaction.valid && note_transaction.clear && note_transaction.pattern_sound)
        used[note_transaction.pattern_sound - 1u] = 1;
    song_edit_sync(); song_sound_mark(used, &chain_config);
    song_edit_snapshot_t *h = resource[RES_SONG_UNDO].ptr;
    for (uint32_t i = 0; i < se.count; i++) song_sound_mark(used, &h[i].config);
    uint32_t slot = CHAIN_SOUNDS;
    for (uint32_t i = 0; i < song_sound_count; i++) {
        if (!memcmp(song_sound_pool() + i, sound, sizeof *sound)) return i + 1u;
        if (!used[i] && slot == CHAIN_SOUNDS) slot = i;
    }
    if (slot == CHAIN_SOUNDS) slot = song_sound_count;
    if (slot >= CHAIN_SOUNDS) return 0;
    uint32_t n = slot < song_sound_count ? song_sound_count : slot + 1u;
    uint32_t extra = (n - song_sound_count) * sizeof(song_sound_t);
    if (!song_sound_count) extra += 4u + CHAIN_ROWS * NTRK;
    if (song_picker.active && song_picker.pattern && !pattern_sounds_any()) extra += sizeof pattern_sound;
    uint32_t room = pattern_room();
    if (extra > room || pattern_file_cost() > room - extra) return 0;
    song_sound_t *p = resource_resize(RES_SONG_SOUNDS, n * sizeof *p);
    if (!p) return 0;
    p[slot] = *sound; song_sound_count = (uint8_t)n;
    return slot + 1u;
}
static void song_preset_cancel(void)
{
    if (!song_picker.active) return;
    brw.on = 0;
    uint32_t f = motion_guard();
    if (song_picker.epoch == pattern_epoch && resource[RES_SONG_PREVIEW].size) {
        uint32_t k = song_picker.track;
        const song_preview_t *preview = resource[RES_SONG_PREVIEW].ptr;
        memcpy(fm6_patch_of(k), preview->fm, sizeof preview->fm);
        fm6_on[k] = preview->fm_on; fm6_lgen[k]++; RING_PUBLISH(); fm6_pgen[k]++;
        *cz_patch_of(k) = preview->cz; *p5_patch_of(&trk[k]) = preview->p5; *drum_patch_of(k) = preview->drum;
        panic_req |= (uint8_t)(1u << k);
        song_sound_apply(k, &preview->base);
        /* An external MIDI/editor start can arrive while the browser is open.
         * Its pre-start baseline was the audition; restore the real default
         * before resuming the currently resolved playback sound. */
        if ((song.playing || chain_busy()) && (song_sounds.mask >> k & 1u)) {
            ((song_sound_t *)resource[RES_SONG_BASE].ptr)[k] = preview->base;
            const song_sound_t *active = song_sound_get(song_sounds.active[k]);
            if (active) song_sound_apply(k, active);
        }
    }
    song_picker.active = 0; resource_release(RES_SONG_PREVIEW);
    sync_reload = 1; ui.force = 1; motion_unguard(f);
}
static void local_preset_open(int pattern)
{
    if (chain_busy() || transport_busy()) { ui_message("STOP TO EDIT"); return; }
    if (!pattern && ui.song_row >= chain_config.count) { ui_message("ADD A SONG ROW"); return; }
    if (trk_midi_slot(song.sel)) { ui_message("MIDI TRACK ONLY"); return; }
    browse_commit(); momentary_restore();
    uint32_t f = motion_guard();
    song_preview_t *preview = resource_get(RES_SONG_PREVIEW, sizeof *preview);
    if (!preview) { motion_unguard(f); ui_message("NO SOUND MEMORY"); return; }
    char name[16]; sound_name(TSEL, name);
    motion_restore(TSEL); song_sound_capture(&preview->base, TSEL, name);
    memcpy(preview->fm, fm6_patch_of(song.sel), sizeof preview->fm); preview->fm_on = fm6_on[song.sel];
    preview->cz = *cz_patch_of(song.sel); preview->p5 = *p5_patch_of(TSEL); preview->drum = *drum_patch_of(song.sel);
    song_picker.pattern = (uint8_t)pattern;
    song_picker.row = pattern ? TSEL->pattern : ui.song_row; song_picker.track = song.sel;
    song_picker.return_page = ui.page; song_picker.epoch = pattern_epoch;
    const song_sound_t *s = song_sound_get(pattern ? pattern_sound_id(song.sel, TSEL->pattern) : resolved_sound_id(ui.song_row, song.sel));
    if (s) { panic_req |= (uint8_t)(1u << song.sel); song_sound_apply(song.sel, s); }
    motion_unguard(f);
    for (uint32_t i = 0; i < NPAGES; i++) if (PAGES[i].graph == GR_BROWSE) { ui.page = (uint8_t)i; break; }
    ui.home = 0; page_entered(); song_picker.active = 1;
    se.pending = se.range = 0; ui.msg_t = 0; ui.force = 1;
}
static void song_preset_open(void) { local_preset_open(0); }
static void pattern_preset_open(void) { local_preset_open(1); }
static void pattern_preset_remove(void)
{
    if (transport_busy()) { ui_message("STOP TO EDIT"); return; }
    if (!pattern_sound[song.sel][TSEL->pattern]) return;
    uint32_t f = motion_guard();
    undo_seal(); load_begin(TSEL, UNDO_PATTERN_SOUND);
    pattern_sound[song.sel][TSEL->pattern] = 0;
    load_end(TSEL); undo_seal(); motion_unguard(f);
    ui.force = 1;
}
static void song_preset_finish(int keep)
{
    uint32_t page = song_picker.return_page, row = song_picker.row, k = song_picker.track;
    if (keep) {
        if (transport_busy()) { ui_message("STOP TO EDIT"); return; }
        browse_commit();
        song_sound_t sound; char name[16]; sound_name(&trk[k], name);
        uint32_t f = motion_guard(); song_sound_capture(&sound, &trk[k], name); motion_unguard(f);
        /* A sound is independent of the preset slot and the track's settings. */
        sound.user[0] = sound.user[1] = sound.user_native = 0;
        for (uint32_t i = 0; i < P_COUNT; i++) if (param_kept(i)) sound.p[i] = (uint8_t)(param_desc_of(sound.engine, i)->def + 64);
        if (!song_sound_valid(&sound)) { ui_message("INVALID SOUND"); return; }
        f = motion_guard();
        if (song_picker.pattern && !pattern_sounds_any() &&
            (pattern_room() < sizeof pattern_sound || pattern_file_cost() > pattern_room() - sizeof pattern_sound)) {
            motion_unguard(f); ui_message("PROJECT FULL"); return;
        }
        if (song_picker.pattern && !resource_get(RES_SONG_BASE, NTRK * sizeof(song_sound_t))) {
            motion_unguard(f); ui_message("NO SOUND MEMORY"); return;
        }
        uint32_t id = song_sound_store(&sound);
        if (!id) { motion_unguard(f); ui_message("NO SOUND MEMORY"); return; }
        if (song_picker.pattern) {
            uint8_t old = pattern_sound[k][row];
            pattern_sound[k][row] = (uint8_t)id;
            if (pattern_file_cost() > pattern_room()) {
                pattern_sound[k][row] = old;
                motion_unguard(f); ui_message("PROJECT FULL"); return;
            }
            pattern_sound[k][row] = old;
            /* Finish audition restoration before capturing the assignment undo. */
            song_preset_cancel();
            undo_seal(); load_begin(&trk[k], UNDO_PATTERN_SOUND);
            pattern_sound[k][row] = (uint8_t)id;
            load_end(&trk[k]); undo_seal();
        }
        else {
            uint32_t first = chain_origin(row, k), end = chain_block_end(first, k), changed = 0;
            for (uint32_t r = first; r < end; r++) changed |= chain_config.sound[r][k] != id;
            if (changed) {
                if (!song_edit_begin()) { motion_unguard(f); return; }
                for (uint32_t r = first; r < end; r++) chain_config.sound[r][k] = (uint8_t)id;
                song_edit_end();
            }
        }
        motion_unguard(f);
    }
    song_preset_cancel(); ui.page = (uint8_t)page; ui.home = 0; page_entered();
    ui.msg_t = 0;
}
static void song_preset_remove(void)
{
    if (ui.song_row >= chain_config.count) return;
    uint32_t first = chain_origin(ui.song_row, song.sel), end = chain_block_end(first, song.sel), any = 0;
    for (uint32_t r = first; r < end; r++) any |= chain_config.sound[r][song.sel];
    if (!any || !song_edit_begin()) return;
    for (uint32_t r = first; r < end; r++) chain_config.sound[r][song.sel] = 0;
    song_edit_end();
}
