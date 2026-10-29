/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The tracks in use (song.ntrk, core.h trk_n): the four parts to start with, always from track 1; tracks 5..8 are
 * MIDI-only. Stage's sheet adds one (track_add: the next place, its power-on sound, every pattern empty; selected,
 * the browser open on it, a MIDI-only one its channel) or removes the selected one (track_remove: stopped, after its
 * question): the tracks after it move down a place with all that is theirs (the sound and its patches, the eight
 * patterns, their column of the song, motion, takes) and the last place is left empty. A removal captures the
 * decoded project and bank tail as SAVE does, but each place from the track after it (cap_map), then restores them.
 * Included by project.c */

static void track_say(uint32_t k, const char *what)     /* "TRACK 2 REMOVED" */
{
    char b[16] = "1 ";
    b[0] = (char)('1' + k % NTRK);
    str_cpy(b + 2, what, sizeof b - 2u);
    ui_say("TRACK ", b);
}

/* track j's bits down to j - 1 from the track removed (k) on; k's go */
static uint8_t trk_bits_remove(uint32_t bits, uint32_t k)
{
    return (uint8_t)((bits & ((1u << k) - 1u)) | ((bits >> (k + 1u)) << k));
}

/* remove track k (in use, not the only one): 0 done, 1 refused, 2 failed (nothing changed) */
static int track_remove(uint32_t k)
{
    project_t *p = &proj_scratch;
    uint32_t n = trk_n(), cur = proj_cur, j, r;
    int ok;
    if (k >= n || n < 2u)
        return 1;
    if (transport_busy()) {
        ui_message("STOP FIRST");
        return 1;
    }
    browse_commit();                                    /* (a sound still pending: into the track it was meant for) */
    momentary_restore();
    proj_wire_gen++;
    for (j = 0; j < NTRK; j++)                          /* place j: the track after it from k on, the last none */
        cap_track_offset[j] = (uint8_t)(j < k ? 0u : j + 1u < n ? 1u : TRK_GONE);
    cap_n = (uint8_t)(n - 1u);
#if MELODEE_FLASH
    uint8_t *raw = proj_image_tail();
    ok = proj_image_capture(1);
#else
    uint8_t *raw = wire_take();
    project_capture(p);
    p->sel = (uint8_t)(k + 1u < n ? k : k - 1u);        /* the track now in its place, else the one before */
    p->sum = proj_sum(p);
    ok = bank_pack(raw, p, 1);
#endif
    memset(cap_track_offset, 0, sizeof cap_track_offset);
    cap_n = 0;
    if (!ok)
        goto fail;
#if MELODEE_FLASH
    p->sel = (uint8_t)(k + 1u < n ? k : k - 1u);
    p->sum=proj_sum(p);
    if(!proj_image_valid(p)||!bank_tail_valid(raw)||project_restore_runtime(p))goto fail;
    bank_restore_tail(raw);
#else
    if (!bank_valid(raw, BANK_STORE_SIZE) || project_restore_runtime(&proj_scratch))
        goto fail;
    bank_restore(raw);
#endif
    proj_cur = (uint8_t)cur;                            /* (still the project of its slot) */
    for (r = 0; r < CHAIN_ROWS; r++)                    /* the jam so far, the armed and solo tracks too */
        for (j = k; j + 1u < NTRK; j++)
            jam.pat[r][j] = jam.pat[r][j + 1u];
    song.rec = trk_bits_remove(song.rec, k);
    song.rec_latch = trk_bits_remove(song.rec_latch, k);
    track_solo = trk_bits_remove(track_solo, k);
    track_say(k, "REMOVED");
    return 0;
fail:
    proj_wire_gen++;
    ui_message(bank_too_full ? "PROJECT FULL" : "TRACK NOT REMOVED");
    return 2;
}

/* place k as a new track finds it: its power-on sound, the mix's and the track's defaults, every pattern empty, no motion
 * or takes, its column of the song 0. Playing too: a place not in use is silent and its steps kept time */
static void track_blank(uint32_t k)
{
    track_t *t = &trk[k % NTRK];
    uint32_t b, i, r;
    undo_depth++;                                       /* (its sound loads take no undo levels) */
    track_defaults(t);                                  /* the mix, ARP, SCL, LEN ..; the steps, motion, takes */
    set_engine_of(t, trk_def_engine(k % NTRK));
    apply_preset_to(t, trk_def_preset(k % NTRK));
    undo_depth--;
    t->p[P_ROOT] = TSEL->p[P_ROOT];                     /* the song's key */
    scale_share(TSEL);
    fm1_irq_off();
    for (b = 0; b < trk_npat(k % NTRK); b++) {          /* the other patterns empty too */
        pattern_t p;
        pattern_read(k % NTRK, b, &p);
        for (i = 0; i < NSTEP; i++)
            step_clear(&p.step[i]);
        for (i = 0; i < 4u; i++)
            p.timing[i] = TP[P_SLEN + i].def;
        pattern_write(k % NTRK, b, &p);
    }
    memset(pattern_sound[k % NTRK], 0, sizeof pattern_sound[0]);
    pattern_switch(t, 0);
    t->pattern_next = 0xff;
    t->pattern_gen++;
    for (r = 0; r < CHAIN_ROWS; r++)
        chain_patterns[r][k % NTRK] = 0;
    fm1_irq_on();
    pat_sig[k % NTRK] = steps_sig(t);                   /* (empty: not the user's) */
    pat_last[k % NTRK] = 0;
    list_trk[k % NTRK] = 0;                             /* (the browser: the last LIST picked) */
}

/* Tracks are contiguous: create the missing places through k, then select it.
 * Patterns uses this without leaving its page; Stage opens the new track's sound. */
static int track_create_to(uint32_t k)
{
    if (k >= NTRK) {
        ui_message("TRACKS FULL");
        return 0;
    }
    browse_commit();
    momentary_restore();
    while (trk_n() <= k) {
        uint32_t next = trk_n();
        track_blank(next);
        song.ntrk = (uint8_t)(next + 1u);
    }
    track_select(k);
    track_say(k, "ADDED");
    return 1;
}
/* the next track in use (Stage's sheet): a new one, selected, the browser open on it (OCT- keeps its power-on sound) */
static void track_add(void)
{
    uint32_t k = trk_n(), i;
    if (!track_create_to(k)) return;
    for (i = 0; i < NPAGES; i++)                        /* the browser on it: its sound picked from there; a MIDI-only */
        if (trk_midi_slot(k) ? PAGES[i].scope == SC_ENGINE && PAGES[i].id[0] == P_E0 : PAGES[i].graph == GR_BROWSE)
            break;                                      /* track's sound is its channel */
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
}
