/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Arrangement transactions retain pattern references, never copy note banks.
 * Eight demand-allocated snapshots support the existing EDIT undo gestures. */
#define SONG_UNDO_LEVELS 8u
typedef struct {
    chain_config_t config;
    uint8_t patterns[CHAIN_ROWS][NTRK], cursor;
} song_edit_snapshot_t;
static struct {
    uint32_t signature, guard;
    uint16_t epoch;
    uint8_t top, count, range, anchor, end, pending, first, last, track, locked;
} se;
enum { SONG_CELL_COPY = 1, SONG_CELL_MOVE, SONG_SECTIONS_COPY, SONG_SECTIONS_MOVE };
/* The target runtime exposes memcpy, so handle overlapping editor ranges here. */
static void song_edit_shift(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = dst; const uint8_t *s = src;
    if ((uintptr_t)d < (uintptr_t)s) for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    else while (n) { n--; d[n] = s[n]; }
}
static uint32_t song_edit_signature(void)
{
    return fnv(fnv(2166136261u, &chain_config, sizeof chain_config), chain_patterns, sizeof chain_patterns);
}
static void song_edit_cancel(void)
{
    se.pending = se.range = 0;
    if (ui.song_row >= CHAIN_ROWS) ui.song_row = CHAIN_ROWS - 1u;
    ui.force = 1;
}
/* Every UI pass. The song is the main loop's alone, so its ~2 KB hash runs with IRQs on: under the guard it held
 * TIMER5 back ~60 us each pass, and the stretched panel columns made the dim LEDs shimmer. */
static void song_edit_sync(void)
{
    uint32_t sig = song_edit_signature();
    uint32_t f = motion_guard();
    if (se.epoch != pattern_epoch || se.signature != sig) {
        resource_release(RES_SONG_UNDO);
        memset(&se, 0, sizeof se);
        se.epoch = pattern_epoch; se.signature = sig;
    }
    if (!resource[RES_SONG_UNDO].size) se.top = se.count = 0;
    motion_unguard(f);
}
static void song_snapshot_get(song_edit_snapshot_t *s)
{
    s->config = chain_config;
    memcpy(s->patterns, chain_patterns, sizeof s->patterns);
    s->cursor = ui.song_row;
}
static int song_edit_begin(void)
{
    if (chain_busy()) { ui_message("STOP TO EDIT"); return 0; }
    uint32_t f = motion_guard();
    if (chain_busy()) { motion_unguard(f); ui_message("STOP TO EDIT"); return 0; }
    song_edit_sync();
    uint32_t n = se.top < SONG_UNDO_LEVELS ? se.top + 1u : SONG_UNDO_LEVELS;
    song_edit_snapshot_t *h = resource_resize(RES_SONG_UNDO, n * sizeof *h);
    if (!h) { motion_unguard(f); ui_message("NO EDIT MEMORY"); return 0; }
    if (se.top == SONG_UNDO_LEVELS) {
        song_edit_shift(h, h + 1, (SONG_UNDO_LEVELS - 1u) * sizeof *h); se.top--;
    }
    song_snapshot_get(&h[se.top++]); se.count = se.top;
    se.guard = f; se.locked = 1; return 1;
}
static void song_edit_end(void)
{
    se.signature = song_edit_signature(); ui.force = 1;
    if (se.locked) { se.locked = 0; motion_unguard(se.guard); }
}
static void song_loop_set(int on)
{
    if (chain_config.rsv[CHAIN_LOOP] == (uint32_t)on || !song_edit_begin()) return;
    chain_config.rsv[CHAIN_LOOP] = (uint8_t)on;
    song_edit_end();
}
static struct { uint8_t row, event; } song_punch;
static int song_punch_on(void);                       /* ui_popup.c: the visual section FX editor */
static uint16_t punch_color(uint32_t fx);             /* ui_layer.c: same family colours as live FX */
static uint32_t song_punch_steps_at(uint32_t row)
{
    uint32_t bank = chain_patterns[row][0];
    return bank < NPAT ? (uint32_t)pattern_timing(0, bank, 0) : 16u;
}
static void song_punch_edit(uint32_t field, int32_t delta)
{
    uint32_t row = song_punch.row, j = song_punch.event;
    if (row >= chain_config.count || j >= CHAIN_FX_EVENTS) return;
    chain_fx_t e = chain_config.fx[row][j];
    uint32_t steps = song_punch_steps_at(row);
    if (!field) {
        e.fx = (uint8_t)clamp((int32_t)e.fx + delta, 0, PUNCH_NFX);
        if (!e.fx) memset(&e, 0, sizeof e);
        else if (!e.length) e.length = 1;
    } else {
        if (!e.fx) { ui_message("CHOOSE AN EFFECT"); return; }
        /* Moving a punch retains its duration at the right edge. */
        if (field == 1u) e.start = (uint8_t)clamp((int32_t)e.start + delta, 0,
            steps - (e.length < steps ? e.length : steps));
        else e.length = (uint8_t)clamp((int32_t)e.length + delta, 1, steps);
    }
    if (e.fx) {
        if (e.start >= steps) e.start = (uint8_t)(steps - 1u);
        if (e.length > steps - e.start) e.length = (uint8_t)(steps - e.start);
    }
    if (!memcmp(&e, &chain_config.fx[row][j], sizeof e) || !song_edit_begin()) return;
    chain_config.fx[row][j] = e; song_edit_end();
}
static void song_punch_clear(void)
{
    if (song_punch.row >= chain_config.count || !chain_fx_count(song_punch.row) || !song_edit_begin()) return;
    memset(chain_config.fx[song_punch.row], 0, sizeof chain_config.fx[0]); song_edit_end();
}
static void song_edit_history(int redo)
{
    uint32_t f = motion_guard();
    song_edit_sync();
    if ((!redo && !se.top) || (redo && se.top == se.count)) {
        motion_unguard(f); ui_message(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO"); return;
    }
    song_edit_snapshot_t *h = resource[RES_SONG_UNDO].ptr;
    song_edit_snapshot_t *s = &h[redo ? se.top++ : --se.top];
    note_swap(&chain_config, &s->config, sizeof chain_config);
    note_swap(chain_patterns, s->patterns, sizeof chain_patterns);
    uint8_t row = ui.song_row; ui.song_row = s->cursor; s->cursor = row;
    song_edit_cancel(); song_edit_end(); motion_unguard(f);
    ui_message(redo ? "REDO SONG" : "UNDO SONG");
}
static uint32_t song_range_first(void) { return se.range && se.anchor < se.end ? se.anchor : se.range ? se.end : ui.song_row; }
static uint32_t song_range_last(void) { return se.range && se.anchor > se.end ? se.anchor : se.range ? se.end : ui.song_row; }
static int song_range_has(uint32_t r) { return se.range && r >= song_range_first() && r <= song_range_last(); }
static void song_materialize(uint32_t row)
{
    if (row >= chain_config.count) return;
    for (uint32_t k = 0; k < NTRK; k++)
        if (chain_patterns[row][k] == CHAIN_HOLD) {
            chain_config.sound[row][k] = (uint8_t)chain_sound_id(row, k);
            chain_patterns[row][k] = (uint8_t)chain_bank(row, k);
        }
    chain_config.row[row].slot = chain_patterns[row][0];
}
static uint32_t song_snapshot_bank(const song_edit_snapshot_t *s, uint32_t row, uint32_t track)
{
    while (row && s->patterns[row][track] == CHAIN_HOLD) row--;
    return s->patterns[row][track];
}
static void song_extend_rows(uint32_t count)
{
    while (chain_config.count < count) {
        uint32_t row = chain_config.count++;
        chain_config.row[row] = (chain_row_t){CHAIN_REST, 1};
        memset(chain_patterns[row], CHAIN_REST, NTRK);
    }
}
static void song_cell_rest(void)
{
    if (ui.song_row >= chain_config.count || chain_patterns[ui.song_row][song.sel] == CHAIN_REST) return;
    uint32_t first = chain_origin(ui.song_row, song.sel), end = chain_block_end(first, song.sel);
    if (!song_edit_begin()) return;
    for (uint32_t r = first; r < end; r++) {
        chain_patterns[r][song.sel] = CHAIN_REST;
        chain_config.sound[r][song.sel] = 0;
        chain_config.row[r].slot = chain_patterns[r][0];
    }
    song_edit_end();
}
static void song_cell_pattern(int32_t steps)
{
    uint32_t row = ui.song_row;
    if (row >= chain_config.count) {
        if (row >= CHAIN_ROWS || !song_edit_begin()) return;
        song_extend_rows(row + 1u);
        chain_patterns[row][song.sel] = TSEL->pattern;
        chain_config.row[row].slot = chain_patterns[row][0];
        song_edit_end(); return;
    }
    uint32_t first = chain_origin(row, song.sel), end = chain_block_end(first, song.sel);
    int32_t old = chain_patterns[first][song.sel] == CHAIN_REST ? -1 : chain_patterns[first][song.sel];
    int32_t value = clamp(old + steps, -1, (int32_t)trk_npat(song.sel) - 1);
    if (value == old || !song_edit_begin()) return;
    chain_patterns[first][song.sel] = (uint8_t)(value < 0 ? CHAIN_REST : value);
    for (uint32_t r = first; r < end; r++) {
        if (value < 0) { chain_patterns[r][song.sel] = CHAIN_REST; chain_config.sound[r][song.sel] = 0; }
        chain_config.row[r].slot = chain_patterns[r][0];
    }
    song_edit_end();
}
static void song_block_resize(int32_t steps)
{
    if (ui.song_row >= chain_config.count) return;
    uint32_t first = chain_origin(ui.song_row, song.sel), end = chain_block_end(first, song.sel);
    if (chain_patterns[first][song.sel] >= NPAT) { ui_message("SELECT A BLOCK"); return; }
    uint32_t length = (uint32_t)clamp((int32_t)(end - first) + steps, 1, CHAIN_ROWS - first);
    uint32_t next = first + length;
    if (next == end) return;
    for (uint32_t r = end; r < next && r < chain_config.count; r++)
        if (chain_patterns[r][song.sel] != CHAIN_REST) { ui_message("NEXT BLOCK OCCUPIED"); return; }
    if (!song_edit_begin()) return;
    uint8_t sound = (uint8_t)chain_sound_id(first, song.sel);
    song_extend_rows(next > chain_config.count ? next : chain_config.count);
    for (uint32_t r = first + 1u; r < (next > end ? next : end); r++) {
        chain_patterns[r][song.sel] = r < next ? CHAIN_HOLD : CHAIN_REST;
        chain_config.sound[r][song.sel] = r < next ? sound : 0;
        chain_config.row[r].slot = chain_patterns[r][0];
    }
    ui.song_row = (uint8_t)first; song_edit_end();
}
static void song_arm(uint32_t mode)
{
    if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
    if (ui.song_row >= chain_config.count) { ui_message("ADD A SONG ROW"); return; }
    se.first = (uint8_t)(mode >= SONG_SECTIONS_COPY ? song_range_first() : ui.song_row);
    se.last = (uint8_t)(mode >= SONG_SECTIONS_COPY ? song_range_last() : ui.song_row);
    if (mode < SONG_SECTIONS_COPY) {
        se.first = (uint8_t)chain_origin(ui.song_row, song.sel);
        se.last = (uint8_t)(chain_block_end(se.first, song.sel) - 1u);
    }
    se.track = song.sel; se.pending = (uint8_t)mode; se.range = 0; ui.force = 1;
}
static void song_cell_copy(void) { song_arm(SONG_CELL_COPY); }
static void song_cell_move(void) { song_arm(SONG_CELL_MOVE); }
static void song_sections_copy(void) { song_arm(SONG_SECTIONS_COPY); }
static void song_sections_move(void) { song_arm(SONG_SECTIONS_MOVE); }
static void song_rows_remove(uint32_t first, uint32_t n)
{
    uint32_t tail = chain_config.count - first - n;
    song_materialize(first + n);
    song_edit_shift(chain_config.row + first, chain_config.row + first + n, tail * sizeof(chain_row_t));
    song_edit_shift(chain_config.fx + first, chain_config.fx + first + n, tail * sizeof chain_config.fx[0]);
    song_edit_shift(chain_config.sound + first, chain_config.sound + first + n, tail * NTRK);
    song_edit_shift(chain_patterns + first, chain_patterns + first + n, tail * NTRK);
    chain_config.count -= (uint8_t)n;
    for (uint32_t r = chain_config.count; r < CHAIN_ROWS; r++) {
        chain_config.row[r] = (chain_row_t){0, 1}; memset(chain_patterns[r], 0, NTRK);
        memset(chain_config.fx[r], 0, sizeof chain_config.fx[r]);
        memset(chain_config.sound[r], 0, NTRK);
    }
    if (ui.song_row > chain_config.count) ui.song_row = chain_config.count;
}
static void song_sections_delete(void)
{
    uint32_t first = song_range_first(), last = song_range_last();
    if (last >= chain_config.count || !song_edit_begin()) return;
    song_rows_remove(first, last - first + 1u); song_edit_cancel(); song_edit_end();
    ui_message("SECTIONS DELETED");
}
static void song_insert(uint32_t duplicate)
{
    uint32_t r = ui.song_row < chain_config.count ? ui.song_row : chain_config.count;
    uint32_t first = duplicate ? song_range_first() : r, n = duplicate ? song_range_last() - first + 1u : 1u;
    if (duplicate && first >= chain_config.count) return;
    if (chain_config.count + n > CHAIN_ROWS) { ui_message("SONG FULL"); return; }
    if (!song_edit_begin()) return;
    const song_edit_snapshot_t *s = &((song_edit_snapshot_t *)resource[RES_SONG_UNDO].ptr)[se.top - 1u];
    if (duplicate) r = first + n;
    song_materialize(r);
    song_edit_shift(chain_config.row + r + n, chain_config.row + r, (chain_config.count - r) * sizeof(chain_row_t));
    song_edit_shift(chain_config.fx + r + n, chain_config.fx + r, (chain_config.count - r) * sizeof chain_config.fx[0]);
    song_edit_shift(chain_config.sound + r + n, chain_config.sound + r, (chain_config.count - r) * NTRK);
    song_edit_shift(chain_patterns + r + n, chain_patterns + r, (chain_config.count - r) * NTRK);
    for (uint32_t j = 0; j < n; j++) {
        chain_config.row[r + j] = duplicate ? s->config.row[first + j] : (chain_row_t){CHAIN_REST, 1};
        if (duplicate) memcpy(chain_config.sound[r + j], s->config.sound[first + j], NTRK);
        else memset(chain_config.sound[r + j], 0, NTRK);
        if (duplicate) memcpy(chain_config.fx[r + j], s->config.fx[first + j], sizeof chain_config.fx[0]);
        else memset(chain_config.fx[r + j], 0, sizeof chain_config.fx[0]);
        if (duplicate) memcpy(chain_patterns[r + j], s->patterns[first + j], NTRK);
        else memset(chain_patterns[r + j], CHAIN_REST, NTRK);
        if (duplicate && !j) {
            for (uint32_t k = 0; k < NTRK; k++) chain_patterns[r][k] = (uint8_t)song_snapshot_bank(s, first, k);
            chain_config.row[r].slot = chain_patterns[r][0];
        }
    }
    chain_config.count += (uint8_t)n; ui.song_row = (uint8_t)r;
    song_edit_cancel(); song_edit_end();
    ui_message(duplicate ? "SECTION DUPLICATED" : "EMPTY SECTION INSERTED");
}
static void song_apply_destination(void)
{
    uint32_t mode = se.pending, dest = ui.song_row, n = se.last - se.first + 1u;
    if (!mode) return;
    if (mode < SONG_SECTIONS_COPY) {
        if (dest + n > CHAIN_ROWS) { ui_message("SONG FULL"); return; }
        if (dest == se.first && song.sel == se.track) { song_edit_cancel(); return; }
        uint32_t rest = chain_patterns[se.first][se.track] == CHAIN_REST;
        for (uint32_t r = dest; !rest && r < dest + n && r < chain_config.count; r++) {
            int source = song.sel == se.track && r >= se.first && r <= se.last;
            if (chain_patterns[r][song.sel] != CHAIN_REST && !(mode == SONG_CELL_MOVE && source)) {
                ui_message("DESTINATION OCCUPIED"); return;
            }
        }
        if (!song_edit_begin()) return;
        const song_edit_snapshot_t *s = &((song_edit_snapshot_t *)resource[RES_SONG_UNDO].ptr)[se.top - 1u];
        song_extend_rows(dest + n > chain_config.count ? dest + n : chain_config.count);
        if (mode == SONG_CELL_MOVE) for (uint32_t r = se.first; r <= se.last; r++) {
            chain_config.sound[r][se.track] = 0;
            chain_patterns[r][se.track] = CHAIN_REST; chain_config.row[r].slot = chain_patterns[r][0];
        }
        uint32_t clear_first = rest ? chain_origin(dest, song.sel) : dest;
        uint32_t clear_end = rest ? chain_block_end(clear_first, song.sel) : dest + n;
        for (uint32_t r = clear_first; r < clear_end; r++) {
            if (r >= dest && r < dest + n)
                chain_config.sound[r][song.sel] = s->config.sound[se.first + r - dest][se.track];
            chain_patterns[r][song.sel] = rest ? CHAIN_REST : s->patterns[se.first + r - dest][se.track];
            chain_config.row[r].slot = chain_patterns[r][0];
        }
    } else {
        if (mode == SONG_SECTIONS_MOVE && dest >= se.first && dest <= se.last + 1u) { song_edit_cancel(); return; }
        if (mode == SONG_SECTIONS_COPY && chain_config.count + n > CHAIN_ROWS) { ui_message("SONG FULL"); return; }
        if (!song_edit_begin()) return;
        const song_edit_snapshot_t *s = &((song_edit_snapshot_t *)resource[RES_SONG_UNDO].ptr)[se.top - 1u];
        if (mode == SONG_SECTIONS_MOVE) {
            song_rows_remove(se.first, n);
            if (dest > se.last) dest -= n;
        }
        song_materialize(dest);
        song_edit_shift(chain_config.row + dest + n, chain_config.row + dest, (chain_config.count - dest) * sizeof(chain_row_t));
        song_edit_shift(chain_config.fx + dest + n, chain_config.fx + dest, (chain_config.count - dest) * sizeof chain_config.fx[0]);
        song_edit_shift(chain_config.sound + dest + n, chain_config.sound + dest, (chain_config.count - dest) * NTRK);
        song_edit_shift(chain_patterns + dest + n, chain_patterns + dest, (chain_config.count - dest) * NTRK);
        memcpy(chain_config.row + dest, s->config.row + se.first, n * sizeof(chain_row_t));
        memcpy(chain_config.fx + dest, s->config.fx + se.first, n * sizeof chain_config.fx[0]);
        memcpy(chain_config.sound + dest, s->config.sound + se.first, n * NTRK);
        memcpy(chain_patterns[dest], s->patterns[se.first], n * NTRK);
        for (uint32_t k = 0; k < NTRK; k++) chain_patterns[dest][k] = (uint8_t)song_snapshot_bank(s, se.first, k);
        chain_config.row[dest].slot = chain_patterns[dest][0];
        chain_config.count += (uint8_t)n; ui.song_row = (uint8_t)dest;
    }
    song_edit_cancel(); song_edit_end(); ui_message("SONG UPDATED");
}

static const char *song_edit_status(void)
{
    if (song_picker.active) return "SET PRESET";
    if (chain.running && chain.config.rsv[CHAIN_LOOP]) return "SONG LOOP";
    if (ui.home || cur_page()->graph != GR_SONG) return "";
    if (se.pending) return se.pending == SONG_CELL_MOVE || se.pending == SONG_SECTIONS_MOVE ? "MOVE TO..." : "COPY TO...";
    if (se.range) return "SELECTED";
    if (chain_config.rsv[CHAIN_LOOP]) return "LOOP";
    const song_sound_t *sound = ui.song_row < chain_config.count ? song_sound_get(chain_sound_id(ui.song_row, song.sel)) : 0;
    if (sound) return sound->name;
    return "";
}
