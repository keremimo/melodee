/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Eight manual STEP edits of the selected track (SEQ > NOTES; CHANCE: the note's sheet): note / chord entry with its held length,
 * SCL + SELECT moves, ENV + SELECT lengths, PITCH, LENGTH, VEL, SLIDE and CHANCE turns. EDIT held undoes the last one (with nothing to undo here:
 * the sound / pattern load undo, ui.c undo_swap); OCT- / OCT+ with EDIT held undo further / redo
 * (ui_input.c). The frames of a held entry or move are one edit; a new edit drops the redo. Exact comparisons start
 * a fresh history after anything else changed the steps: live recording, the editor, a pattern load or clear, another
 * track or LEN. */
/* Recorded edits keep one event before and after each edit. Its storage
 * generation prevents undo from overwriting slots reused by another take. */
#define STEP_HISTORY 8u
static struct {
    struct {
        step_snapshot_t *step;
        recorded_note_t removed, replacement;
        uint16_t removed_index, selection;
        uint8_t cursor, note_slot, has_removed;
    } state[STEP_HISTORY + 1u];
    step_snapshot_t *live;
    uint32_t pattern_gen;
    uint32_t recording_gen;
    recorded_note_t removed, replacement;
    uint16_t removed_index;
    uint8_t has_removed;
    uint8_t valid, track, len;
    uint8_t head, count, pos, pending, cursor_before, slot_before;
} step_history __attribute__((section(".pool")));
#define STEP_SNAPSHOT_BYTES (NSTEP*sizeof(step_snapshot_t))
#define STEP_HISTORY_BYTES ((STEP_HISTORY+2u)*STEP_SNAPSHOT_BYTES)
/* Only metadata is permanent. Eight edits plus the live comparison are
 * borrowed while editing; audio may retire this handle between UI calls. */
static int step_history_memory(void)
{
    return resource[RES_STEP_UNDO].size==STEP_HISTORY_BYTES &&
        step_history.live==(step_snapshot_t *)resource[RES_STEP_UNDO].ptr+(STEP_HISTORY+1u)*NSTEP;
}

static void step_history_clear(void)
{
    step_history.valid = 0;
    resource_release(RES_STEP_UNDO);step_history.live=0;
}

static uint32_t step_history_index(void)
{
    return (step_history.head + step_history.pos) % (STEP_HISTORY + 1u);
}

static int step_history_context(void)
{
    return
           step_history_memory() &&
           step_history.valid && step_history.recording_gen == recording_generation &&
           step_history.pattern_gen == TSEL->pattern_gen && step_history.track == song.sel && step_history.len == step_pattern_len(TSEL) &&
           !(song.playing && (song.rec & (1u << song.sel)));
}

/* before the input: a change from outside the manual editor starts a fresh history. The audio ISR may record: the
 * steps are compared and copied with the IRQs off */
static void step_history_sync_locked(void)
{
    if(!step_history_memory()){
        step_history.valid=step_history.head=step_history.pos=step_history.pending=0;
        step_history.has_removed=0;
        if(ui.home || (cur_page()->scope!=SC_STEP && cur_page()->scope!=SC_PADHIT) ||
           (song.playing && (song.rec&(1u<<song.sel))))return;
        step_snapshot_t *data=resource_get(RES_STEP_UNDO,STEP_HISTORY_BYTES);
        if(!data)return; /* editing still works when audio needs all the RAM */
        for(uint32_t i=0;i<=STEP_HISTORY;i++)step_history.state[i].step=data+i*NSTEP;
        step_history.live=data+(STEP_HISTORY+1u)*NSTEP;
    }
    if (!step_history_context() || !step_snapshot_equal(step_history.live,TSEL->step)) {
        step_history.head = step_history.pos = step_history.pending = 0;
        step_history.count = 1;
        step_history.track = song.sel;
        step_history.pattern_gen = TSEL->pattern_gen;
        step_history.recording_gen = recording_generation;
        step_history.has_removed = step_history.state[0].has_removed = 0;
        step_history.len = (uint8_t)step_pattern_len(TSEL);
        step_snapshot_save(step_history.state[0].step, TSEL->step);
        step_snapshot_save(step_history.live,TSEL->step);
        step_history.state[0].cursor = ui.cursor;
        step_history.state[0].selection = ui.note_pick;
        step_history.state[0].note_slot = ui.note_slot;
        step_history.valid = !(song.playing && (song.rec & (1u << song.sel)));
    }
    step_history.cursor_before = ui.cursor;
    step_history.slot_before = ui.note_slot;
}
static void step_history_sync(void)
{
    fm1_irq_off();
    step_history_sync_locked();
    fm1_irq_on();
}

/* an edit done (no keys or edit modifier held any more): kept, if the steps changed (moving the cursor keeps the redo) */
static void step_history_finish_locked(void)
{
    uint32_t at = step_history_index();
    if (!step_history_context()) {
        step_history_clear();
        return;
    }
    if (step_history.has_removed || memcmp(step_history.state[at].step,step_history.live,STEP_SNAPSHOT_BYTES)) {
        step_history.count = (uint8_t)(step_history.pos + 1u);   /* the redo goes */
        if (step_history.count == STEP_HISTORY + 1u) {
            step_history.head = (uint8_t)((step_history.head + 1u) % (STEP_HISTORY + 1u));
            step_history.pos--;
            step_history.count--;
        }
        step_history.pos++;
        step_history.count++;
        at = step_history_index();
        memcpy(step_history.state[at].step,step_history.live,STEP_SNAPSHOT_BYTES);
        step_history.state[at].cursor = ui.cursor;
        step_history.state[at].selection = ui.note_pick;
        step_history.state[at].note_slot = ui.note_slot;
        step_history.state[at].has_removed = step_history.has_removed;
        note_assign(&step_history.state[at].removed,&step_history.removed);
        note_assign(&step_history.state[at].replacement,&step_history.replacement);
        step_history.state[at].removed_index = step_history.removed_index;
    }
    step_history.has_removed = 0;
    step_history.pending = 0;
}

static void step_history_finish(void)
{
    uint32_t f=motion_guard();step_history_finish_locked();motion_unguard(f);
}

/* after the input: the steps as this frame left them, so a change from outside on the next frame is no part of an
 * edit on the panel */
static void step_history_end(void)
{
    fm1_irq_off();
    if (!step_history_context()) {
        step_history_clear();
        fm1_irq_on();
        return;
    }
    if (step_history.has_removed || !step_snapshot_equal(step_history.live,TSEL->step)) {
        if (ui.home || (cur_page()->scope != SC_STEP && cur_page()->scope != SC_PADHIT)) {  /* a pattern load, a clear, the editor: not a STEP edit */
            step_history_clear();
            fm1_irq_on();
            return;
        }
        if (!step_history.pending) {
            step_history.state[step_history_index()].cursor = step_history.cursor_before;
            step_history.state[step_history_index()].note_slot = step_history.slot_before;
        }
        step_history.pending = 1;
        step_snapshot_save(step_history.live,TSEL->step);
    }
    fm1_irq_on();
    if (!ui.entry_open && !ui.step_move)
        step_history_finish();
}

/* 1 = undone / redone, 0 = nothing there (the caller may undo something else) */
static int step_history_apply(int redo)
{
    uint32_t at;
    if(note_tools_history(redo))return 1;
    fm1_irq_off();
    step_history_finish();                       /* a gesture still held is an edit too */
    if (!step_history_context() || (redo ? step_history.pos + 1u >= step_history.count : !step_history.pos)) {
        fm1_irq_on();
        return recording_history(redo);
    }
    uint32_t change = redo ? (step_history.head + step_history.pos + 1u) % (STEP_HISTORY + 1u) : step_history_index();
    if (step_history.state[change].has_removed) {
        uint32_t i = step_history.state[change].removed_index;
        recording_remove(TSEL, i);
        recorded_note_t r = redo ? step_history.state[change].replacement : step_history.state[change].removed;
        if (r.vel) recording_restore_note(TSEL, i, r);
    }
    step_history.pos = (uint8_t)(step_history.pos + (redo ? 1 : -1));
    at = step_history_index();
    step_snapshot_load(TSEL->step, step_history.state[at].step);
    step_snapshot_save(step_history.live,TSEL->step);
    step_history.recording_gen = recording_generation;
    fm1_irq_on();
    cursor_set(step_history.state[at].cursor);
    ui.note_pick = step_history.state[at].selection;
    ui.note_slot = step_history.state[at].note_slot;
    ui.note_generation = recording_generation;
    ui.hot_t = 0;
    ui.force = 1;
    ui_message(redo ? "REDO" : "UNDO");
    return 1;
}
