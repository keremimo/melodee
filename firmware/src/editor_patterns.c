/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee Studio's patterns (EDITOR_PROTOCOL.md "Studio patterns"): PATTERN_DATA (44) reads and writes any track's
 * pattern bank, the one playing or another, 32 steps a request; NOTES (45) reads a bank's timed notes and writes them
 * (onset inside its step, length, velocity, pitch; added, changed, removed), the bank's steps following as their view;
 * MOTION (64) takes a bank and PATTERN (73) clears, halves and doubles one as SEQ's pattern dialog does (editor.c,
 * pattern_dialog.c); WATCH bit 6 pushes PATTERN_CHANGED (59) when patterns change on the device. Included by editor.c */
enum { ED_PATTERN_DATA = 44, ED_NOTES, ED_PATTERN_CHANGED = 59 };
#define ED_HALF 32u                                       /* PATTERN_DATA: the steps a request carries */
#define ED_STEP_N 12u                                     /* .. a step's bytes */
#define ED_NOTE_N 17u                                     /* NOTES: a note's bytes */
#define ED_NOTE_NEW 16383u                                /* .. the id that adds a note */
_Static_assert(sizeof(step_t) == 11u, "PATTERN_DATA sends a step's bytes as they are");

/* what PATTERN_CHANGED follows: every track's steps, timing and banks, the motion, the timed notes and the banks'
 * writes (pattern_write); the banks not playing change only through pattern_write or a load (pattern_gen) */
static uint32_t ed_pat_sig(void)
{
    uint32_t s = fnv(fnv(2166136261u, &motion, sizeof motion), motion_pattern, sizeof motion_pattern), k;
    for (k = 0; k < NTRK; k++) {
        s = fnv(fnv(s, trk[k].step, sizeof trk[k].step), &trk[k].p[P_SLEN], 4u * sizeof(int16_t));
        s = (s ^ trk[k].pattern ^ (uint32_t)trk[k].pattern_next << 8 ^ trk[k].pattern_gen << 16) * 16777619u;
    }
    return s ^ recording_generation ^ (uint32_t)pattern_edits << 20;
}
static void ed_pat_own(void)                              /* the editor's own change: no PATTERN_CHANGED */
{
    if (ed_w.bits & 64u)
        ed_w.psig[3] = ed_pat_sig();
}
static void ed_pat_banks(void)                            /* PATTERN_CHANGED: per track the bank playing, the one queued */
{
    ed_b(NTRK);
    for (uint32_t k = 0; k < NTRK; k++) {
        ed_b(trk[k].pattern);
        ed_b(trk[k].pattern_next < NPAT ? trk[k].pattern_next : 127u);
    }
}
/* a NOTES write's note q (17 bytes, its id aside) as it would be stored on bank own, whose steps p holds: 1 if valid */
static int ed_note_in(const uint8_t *q, uint32_t own, const pattern_t *p, recorded_note_t *r)
{
    uint32_t st = q[2], on = ed_bk_r32(q + 3), du = ed_bk_r32(q + 8), len = (uint32_t)clamp(p->timing[0], 1, NSTEP);
    uint32_t half = on >= RECORD_UNIT / 2u, wrap = half && st && st + 1u == len, view = wrap ? 0u : st + half;
    *r = (recorded_note_t){.on = (uint16_t)on, .vel = q[14], .pitch = (int8_t)(q[15] - 64)};
    recorded_set_note(r, q[13]);
    recorded_set_owner(r, own);
    recorded_set_step(r, st, (int)half, (int)wrap);
    recorded_set_gate(r, q[16] & 1u);
    note_duration(r, du);                                 /* (a view on a step's own notes: not a timed note's place) */
    return st < NSTEP && on < RECORD_UNIT && (!r->vel || (du && du <= 65535u << 7)) && q[15] >= 40u && q[15] <= 88u &&
           q[16] <= 7u && view < NSTEP && !(r->vel && step_on(&p->step[view]) && !(p->step[view].flags & SF_RECORDED));
}

/* (a[0..3] are read before the length is known: frames sit in buffers of 64 bytes or more, and a value is used only
 * once the length says it was sent) */
static int ed_patterns_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t k = a[0], b = a[1], op = a[2], m = a[3], rc = 0, i, j;
    int ok = n >= 3u && k < NTRK && b < trk_npat(k);
    track_t *t = &trk[k % NTRK];
    pattern_t p;
    if (cmd == ED_PATTERN_DATA) {
        /* track, bank, half [, LEN, DIV, SWING, GATE, 32 x step (write)] -> track, bank, half, rc, then (a bank of the
         * track's) the bank's LEN, DIV, SWING, GATE and the half's 32 steps: a step's 11 bytes (step_t), then bit 7 of
         * its hits (bit 0) and of its accents (bit 1) */
        ok = ok && op < NSTEP / ED_HALF;
        if (!ok || (n != 3u && n != 7u + ED_HALF * ED_STEP_N))
            rc = 1;
        else if (n > 3u) {
            pattern_read(k, b, &p);
            for (i = 0; i < 4u; i++)
                p.timing[i] = (int16_t)clamp(a[3u + i], TP[P_SLEN + i].min, TP[P_SLEN + i].max);
            for (i = 0; i < ED_HALF; i++) {
                const uint8_t *s = a + 7u + i * ED_STEP_N;
                step_t *st = &p.step[op * ED_HALF + i];
                memcpy(st, s, sizeof *st);
                st->hit |= (uint8_t)((s[11] & 1u) << 7);
                st->acc |= (uint8_t)((s[11] & 2u) << 6);
                if (st->n > 4u || st->time > ST_REST || st->flags > 7u || st->probability > 101u || s[11] > 3u || !step_acc_valid(st))
                    rc = 1;
            }
            if (!rc)                                      /* (a song plays: STOP TO EDIT; PATTERN STORAGE FULL: 4) */
                rc = chain_busy() ? 3u : pattern_write(k, b, &p) ? 0u : 4u;
            if (!rc) {
                ui.force = 1;
                for (i = 0; k == song.sel && i < NSTEP; i++)
                    ed_w.st[i] = ed_step_sig(&seq_steps(t)[i]);   /* (the editor's own: no STEP_CHANGED) */
                ed_pat_own();
            }
        }
        ed_echo(a, n, 3u); ed_b(rc);
        if (ok) {
            pattern_read(k, b, &p);
            for (i = 0; i < 4u; i++)
                ed_b((uint32_t)p.timing[i]);
            for (i = 0; i < ED_HALF; i++) {
                ed_bytes((const uint8_t *)&p.step[op * ED_HALF + i], sizeof(step_t));
                ed_b(p.step[op * ED_HALF + i].hit >> 7 | (uint32_t)(p.step[op * ED_HALF + i].acc >> 7) << 1);
            }
        }
        return 1;
    }
    if (cmd != ED_NOTES)
        return 0;
    /* track, bank, 0, first (14 bits), count: the bank's timed notes; track, bank, 1, m, m x note: written in order (id
     * 16383 adds one, velocity 0 removes it) -> track, bank, op, rc, then 0: first, count, total (14 bits each), count
     * x note; 1: the m notes' ids (14 bits each). A note (17 bytes): id (14 bits: its slot), step, onset in the step
     * (32 bits: of 65,536), length (32 bits: 65,536 a step), note, velocity, pitch + 64 (a drum hit's tune, -24..24),
     * flags (1 a gated drum hit; read only: 2 rounded to the next step, 4 shown on step 1) */
    uint32_t own = k * NPAT + b, room = 0, keep, drum = drum_track(t);
    recorded_note_t r;
    const uint8_t *q;
    if (!ok || (op ? op > 1u || !m || n != 4u + m * ED_NOTE_N : n != 6u))
        rc = 1;
    else if (op) {                                        /* all checked before anything changes */
        pattern_read(k, b, &p);
        for (i = 0; i < RECORD_MAX; i++)
            room += !recording[i].vel;
        for (j = 0, q = a + 4; j < m; j++, q += ED_NOTE_N) {
            i = q[0] | (uint32_t)q[1] << 7;
            if (!ed_note_in(q, own, &p, &r) || (i == ED_NOTE_NEW ? !r.vel : i >= RECORD_MAX || !recording[i].vel ||
                                                recorded_owner(&recording[i]) != own))
                rc = 1;
            room -= i == ED_NOTE_NEW;                     /* (more new ones than free slots: past RECORD_MAX) */
        }
        if (!rc)                                          /* (each note one more step in use at most) */
            rc = room > RECORD_MAX ? 2u : !pattern_fits(k, b, pattern_steps_n(p.step, NSTEP) + m) ? 4u :
                 chain_busy() || (song.playing && (song.rec >> k & 1u)) ? 3u : 0u;
    }
    ed_echo(a, n, 3u);
    keep = !rc && op && b == t->pattern && !song.playing;   /* (the bank playing, stopped: EDIT undoes it) */
    if (!rc && op) {
        fm1_irq_off();
        if (keep && !note_transaction_begin_track(t)) {
            rc = 2;                                       /* (NO EDIT MEMORY: nothing changed) */
            fm1_irq_on();
        } else if (keep) {
            motion_snapshot_track(t, &note_transaction.motion);
            note_transaction.clear = 1;                   /* (undone whichever track is selected) */
        }
    }
    ed_b(rc);
    if (!rc && op) {
        for (i = recording_head[own]; i < RECORD_MAX; i = recording_next[i])
            if (!recording_present(&recording[i]))
                recording_remove(t, i);                   /* (stale: its step was cleared; else its view brings it back) */
        for (j = 0, q = a + 4; j < m; j++, q += ED_NOTE_N) {
            i = q[0] | (uint32_t)q[1] << 7;
            ed_note_in(q, own, &p, &r);
            if (i == ED_NOTE_NEW)
                for (i = 0; recording[i].vel; i++)
                    ;
            recording_remove(t, i);
            if (r.vel)
                recording[i] = r;
            ed_u14(i);
        }
        recording_reindex();
        for (i = 0; i < NSTEP; i++)                      /* the views again, as notes_rebuild */
            if (p.step[i].flags & SF_RECORDED)
                p.step[i].n = p.step[i].hit = p.step[i].acc = p.step[i].vel = 0;
        for (i = recording_head[own]; i < RECORD_MAX; i = recording_next[i]) {
            uint32_t note = recorded_note(&recording[i]), lane = drum_lane(note), vel = recording[i].vel;
            step_t *st = &p.step[recording_view(t, &recording[i])];
            st->time = ST_NOTE;
            st->flags |= SF_RECORDED;
            if (vel > st->vel)
                st->vel = (uint8_t)vel;
            if (drum && note == DRUM_LANE_NOTE[lane]) {
                st->hit |= (uint8_t)(1u << lane);
                if (vel > 110u)
                    st->acc |= (uint8_t)(1u << lane);
            } else {
                for (j = 0; j < st->n && st->note[j] != note; j++)
                    ;
                if (j == st->n && j < 4u)
                    st->note[st->n++] = (uint8_t)note;
            }
        }
        for (i = 0; i < NSTEP; i++) {
            step_t *st = &p.step[i];
            if (!(st->flags & SF_RECORDED))
                continue;
            st->flags = (uint8_t)(st->vel > 110u ? st->flags | SF_ACCENT : st->flags & ~SF_ACCENT);
            if (!st->n && !st->hit)
                step_clear(st);
        }
        pattern_write(k, b, &p);
        if (keep)
            note_transaction_end();
        fm1_irq_on();
        if (keep)
            undo_seal();
        ui.force = 1;
        ed_pat_own();
    }
    if (!rc && !op) {                                     /* each: id, step, onset, length, note, velocity, pitch, flags */
        uint32_t first = a[3] | (uint32_t)a[4] << 7, total = 0, c = 0, at;
        ed_u14(first); at = ed_n; ed_u14(0); ed_u14(0);
        for (i = recording_head[own]; i < RECORD_MAX; i = recording_next[i]) {
            const recorded_note_t *e = &recording[i];
            if (recording_present(e) && total++ >= first && c < a[5] && ed_left() >= ED_NOTE_N) {
                ed_u14(i); ed_b(recorded_step(e)); ed_u35(e->on); ed_u35((uint32_t)e->duration << recorded_exponent(e));
                ed_b(recorded_note(e)); ed_b(e->vel); ed_b((uint32_t)(e->pitch + 64)); ed_b(recorded_gate(e) | (e->step >> 5 & 6u));
                c++;
            }
        }
        ed_out[at] = (uint8_t)(c & 127u); ed_out[at + 1u] = (uint8_t)(c >> 7);
        ed_out[at + 2u] = (uint8_t)(total & 127u); ed_out[at + 3u] = (uint8_t)(total >> 7);
    }
    return 1;
}
