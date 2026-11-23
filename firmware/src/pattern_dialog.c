/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SEQ's pattern dialog edits and the loop-end queue (moved here from ui_input.c / ui_patterns.c so the editor's PATTERN
 * command can do them too: EDITOR_PROTOCOL.md "Studio patterns"): whether a bank holds anything, its deletion (playing:
 * at the track's loop end), halving and doubling. Included by ui.c after note_tools.c; ui_input.c calls dq_poll every
 * frame */
/* pattern b of track k holds something: a note, a hit, a recorded note's step */
static int pattern_used(uint32_t k, uint32_t b)
{
    if (b >= NPAT) return 0;
    pattern_t p;
    pattern_read(k, b, &p);
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (step_on(&p.step[i]) || (p.step[i].flags & SF_RECORDED))
            return 1;
    return 0;
}
/* pattern b of track k holds nothing at all: no note, no hit, no motion (Copy to there asks nothing) */
static int pattern_empty(uint32_t k, uint32_t b)
{
    uint32_t i;
    if (pattern_used(k, b) || pattern_sound_id(k, b))
        return 0;
    for (i = 0; i < motion.count; i++)
        if ((motion.event[i].place >> 6) == k && motion_pattern[i] == b)
            return 0;
    return 1;
}
/* Destructive edits of what a track plays (delete or clear a pattern, clear its motion, copy over a pattern in use or
 * the one playing) wait for the track's loop end while the transport runs, as a pattern switch does (Kerem
 * 2026-10-15): queued here (dq_defer, "DELETE AT LOOP END"), done by dq_poll every frame once the track is on its last
 * step (nothing more of the loop to play) or, a frame having missed it, its loop has ended (seq.c then keeps the bank
 * silent: seq_hush); stopped (STOP too) at once. Live edits (a note, a hit, a motion lane, ERASE) stay live */
#define DQ_N 8u
static struct { uint8_t cf, trk, a, b; uint16_t loop, epoch; } dq[DQ_N];   /* cf: CF_DEL_PAT, CF_CLEAR_SEQ,
                                                                            * CF_CLEAR_TRK (bank a), CF_CLEAR_MOTION (a),
                                                                            * CF_PASTE_PAT (track b / NPAT's bank
                                                                            * b % NPAT over a); epoch: its
                                                                            * project's (pattern_epoch) */
static uint8_t dq_n;
/* the pattern playing on track k cleared (its notes, motion, the latched chord), undoably */
static void seq_clear_now(uint32_t kind, uint32_t k)
{
    track_t *t = &trk[k % NTRK];
    if (recording_head[recording_owner(t)] < RECORD_MAX) {
        if (!note_clear_pattern(t)) return;
        note_transaction.sound_clear = kind == CF_DEL_PAT;
    } else {
        load_begin(t, UNDO_PAT | (kind == CF_DEL_PAT ? UNDO_PATTERN_SOUND : 0));
        track_defaults_steps(t);
        load_end(t);
    }
    if (kind == CF_DEL_PAT) {
        pattern_sound[k % NTRK][t->pattern] = 0;
        if (song.playing) pattern_sound_switch(k % NTRK, t->pattern);
    }
    t->nheld = 0;                                       /* and the latched arp chord */
    t->arp_phys = 0;
    if (kind == CF_CLEAR_TRK) {
        char b[12] = "1 CLEARED";
        b[0] = (char)('1' + k % NTRK);
        ui_say("TRACK ", b);
    } else {
        ui_message(kind == CF_DEL_PAT ? "PATTERN DELETED" : "PATTERN CLEARED");
    }
}
static void dq_do(uint32_t cf, uint32_t k, uint32_t a, uint32_t b)
{
    track_t *t = &trk[k % NTRK];
    int rc;
    ui.force = 1;
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
    } else if (cf == CF_CLEAR_MOTION) {
        load_begin(t, UNDO_PAT);
        motion_clear_bank(t, a);
        load_end(t);
        ui_message("MOTION CLEARED");
    } else if (cf == CF_PASTE_PAT) {
        rc = pattern_copy_to(&trk[b / NPAT % NTRK], b % NPAT, t, a, 1);
        ui_message(rc == 4 ? "PATTERN STORAGE FULL" : rc == 2 ? "PATTERN DATA FULL" : rc ? "STOP TO COPY" : "PATTERN COPIED");
    } else if (a == t->pattern) {
        seq_clear_now(cf, k);
    } else if (pattern_clear(t, a)) {
        ui_message("STOP TO EDIT");
    } else {
        ui_message(cf == CF_DEL_PAT ? "PATTERN DELETED" : "PATTERN CLEARED");
    }
}
static void dq_pend(uint32_t k)                         /* (seq.c: the banks still waiting on track k) */
{
    uint32_t i, m = 0;
    for (i = 0; i < dq_n; i++)
        if (dq[i].trk == k && dq[i].cf != CF_CLEAR_MOTION)
            m |= 1u << dq[i].a;
    fm1_irq_off();
    seq_pend[k] = (uint16_t)m;
    if (!m) seq_hush &= (uint8_t)~(1u << k);
    fm1_irq_on();
}
/* the edit now, or (playing) at track k's loop end: 1 = queued, 2 = refused (the queue full) */
static int dq_defer(uint32_t cf, uint32_t k, uint32_t a, uint32_t b)
{
    uint32_t i;
    if (!song.playing || chain_busy())
        return 0;
    for (i = 0; i < dq_n; i++)
        if (dq[i].cf == cf && dq[i].trk == k && dq[i].a == a && dq[i].b == b)
            break;
    if (i == dq_n) {
        if (dq_n == DQ_N) {
            ui_message("WAIT FOR LOOP END");
            return 2;
        }
        dq[dq_n].cf = (uint8_t)cf; dq[dq_n].trk = (uint8_t)k; dq[dq_n].a = (uint8_t)a; dq[dq_n].b = (uint8_t)b;
        dq[dq_n].epoch = pattern_epoch;
        dq[dq_n++].loop = seq_loops[k];
        dq_pend(k);
    }
    ui_message(cf == CF_PASTE_PAT ? "COPY AT LOOP END" : cf == CF_DEL_PAT ? "DELETE AT LOOP END" : "CLEAR AT LOOP END");
    ui.force = 1;
    return 1;
}
/* every frame: the edits whose track is on its last step, has looped, or stopped (another project since: dropped) */
static void dq_poll(void)
{
    uint32_t i = 0;
    while (i < dq_n) {
        uint32_t k = dq[i].trk % NTRK, len = (uint32_t)trk[k].p[P_SLEN];
        if (dq[i].epoch == pattern_epoch && song.playing && seq_loops[k] == dq[i].loop &&
            trk[k].seq_idx + 1u < (len ? len : 1u)) {
            i++;
            continue;
        }
        {
            uint32_t cf = dq[i].cf, a = dq[i].a, b = dq[i].b, mine = dq[i].epoch == pattern_epoch, j;
            dq_n--;
            for (j = i; j < dq_n; j++)                  /* (in order: the firmware has no memmove) */
                dq[j] = dq[j + 1u];
            if (mine)
                dq_do(cf, k, a, b);
            dq_pend(k);
        }
    }
}
/* track k's pattern src copied to dst: over one in use or the one playing, while playing: at the loop end */
static void pattern_copy_ui(uint32_t k, uint32_t src, uint32_t dst)
{
    int rc;
    if (src != dst && (dst == trk[k].pattern || !pattern_empty(k, dst)) && dq_defer(CF_PASTE_PAT, k, dst, k * NPAT + src))
        return;
    rc = pattern_copy(&trk[k], src, dst);
    ui_message(rc == 4 ? "PATTERN STORAGE FULL" : rc == 2 ? "PATTERN DATA FULL" : rc ? "STOP TO COPY" : "PATTERN COPIED");
    ui.force = 1;
}
/* SEQ's pattern dialog (CF_PAT_KEY): pattern b of track k deleted (playing: at its loop end): the one playing as SEQ >
 * PATTERNS' Delete pattern (EDIT held undoes it), another emptied in its bank */
static int pattern_key_delete(uint32_t k, uint32_t b)   /* (the editor's rc: 0 done or nothing there, 1 a song plays,
                                                         * 5 queued for the loop end, 2 the queue full) */
{
    int rc = 0;
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        rc = 1;
    } else if (pattern_empty(k, b))
        ui_message("NOTHING TO DELETE");
    else if ((rc = dq_defer(CF_DEL_PAT, k, b, 0)) != 0)
        rc = rc == 1 ? 5 : 2;
    else
        dq_do(CF_DEL_PAT, k, b, 0);
    ui.force = 1;
    return rc;
}
/* .. C# halves it, D# doubles it (motion.c pattern_double: its steps copied after themselves); the dialog stays,
 * saying the length, while the pattern key or SEQ is held. The pattern playing: undoable (EDIT
 * held) as a load, with recorded notes as a precise clear */
static int pattern_key_len(uint32_t k, uint32_t b, int up)   /* (the editor's rc: pattern_double's / _half's, 2 no
                                                             * edit memory) */
{
    track_t *t = &trk[k];
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    int rc = up ? pattern_double(t, b, 0) : pattern_half(t, b, 0);
    ui.force = 1;
    if (rc) {
        ui_message(rc == 4 ? "PATTERN STORAGE FULL" : rc == 2 ? "PATTERN DATA FULL" : rc == 3 ? (up ? "64 STEPS MAX" : "1 STEP MIN") : "STOP TO EDIT");
        return rc;
    }
    if (b != t->pattern) {                              /* (another bank: as a copy there, not undoable) */
        if (up) pattern_double(t, b, 1); else pattern_half(t, b, 1);
    } else if (up && recording_head[recording_owner(t)] < RECORD_MAX) {
        fm1_irq_off();
        if (!note_transaction_begin_track(t)) { fm1_irq_on(); return 2; }
        motion_snapshot_track(t, &note_transaction.motion);
        note_transaction.clear = 1;
        pattern_double(t, b, 1);
        note_transaction.slen = (uint8_t)len;           /* (undone: LEN back too) */
        note_transaction.len = (uint8_t)step_pattern_len(t);
        note_transaction_end();
        fm1_irq_on();
        undo_seal();
    } else {                                            /* (a load clears the motion it keeps a copy of: kept here) */
        motion_store_t m;
        motion_snapshot_track(t, &m);
        load_begin(t, UNDO_PAT);
        motion_replace_track(t, &m);
        if (up) pattern_double(t, b, 1); else pattern_half(t, b, 1);
        load_end(t);
    }
    ui.pat_did = 1;
    ui_message(up ? "PATTERN DOUBLED" : "PATTERN HALVED");
    return 0;
}
