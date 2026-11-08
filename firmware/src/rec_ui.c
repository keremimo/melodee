/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Record kit: the sound's sheet on a track whose synth records its pads (mod_schema.h MOD3_PD_RECORD: its kits' pad
 * records, rec_store.c; a take streamed into a pad's record by the synth as it comes, engine_t.rec_stream), "Record
 * kit..." opens the kit's own sheet:
 *   Pad          KNOB 1 (or OCT+) the pad the next take goes to
 *   Record       OCT+: the free store made ready ("Preparing"), then it waits for audio from the computer or the phone
 *                on USB; the take starts at the first hit and ends at 150 ms of silence, or when the store is full: a
 *                take may be as long as the free store (delete projects, synths or kits for a longer one). It is
 *                kept at once (the keys play it)
 *   Clear pad    the pad's take gone (red)
 *   Delete kit   the whole kit, after its question
 * A pad struck while the sheet is open (a key, an SMK-37 pad, a note from MIDI in) plays its take and becomes the
 * Pad; held KT_HOLD_MS it is Record's OCT+ too. The track plays the kit recorded into while the sheet is open: its
 * own, or the first empty kit (the track's kit back when the sheet goes without a take kept). The transport stays
 * stopped; the take's ring is borrowed from the audio arena while the sheet is open (kit_rec_poll gives it back when
 * the sheet goes), and the main loop feeds the synth from it while the take is recorded. The card alone redraws as its
 * values change (sheet_draw's sig): no page under it. */
#define KT_HOLD_MS 500u
#define KT_FEED 4096u                                   /* samples fed to the synth a poll, at most */
static struct { uint8_t on, slot, pad, saving, msg, trk, held, stop, prep; int16_t prev; uint32_t t, f; } ks;
/* the sheet's track struck (input_on / input_off, the audio ISR's or the UI's): the drum, when, still down */
static volatile struct { uint8_t note, down, news, rel, rels; uint32_t t; } kh;   /* (rel: a note let go: its pad
                                                  * the last struck one's, the press ends: kit_rec_poll) */
static const sheet_row_t SHEET_KIT[];
static void kit_rec_hear(const track_t *t, uint32_t note, uint32_t vel)
{
    if (!ks.on || t != &trk[ks.trk] || pads_rec(t) < 0)
        return;
    if (vel) { kh.note = (uint8_t)note; kh.t = fm1_ms; kh.down = 1; kh.news = 1; kh.rels = 0; }
    else { kh.rel = (uint8_t)note; kh.rels = 1; }
}
static const pads_c_t *kt_pads(void) { return pads_of(&trk[ks.trk]); }
static void kt_kit(int32_t kit)                         /* the sheet's track to this kit (its synth's selector) */
{
    track_t *t = &trk[ks.trk];
    const pads_c_t *c = kt_pads();
    if (c && c->sel < P_COUNT) t->p[c->sel] = (int16_t)kit;
    load_extend(t);
}
static int32_t kt_value(uint32_t u) { const pads_c_t *c = kt_pads(); return c ? c->rec0 + (int32_t)u : 0; }
static uint32_t kt_slot(void) { return ks.slot * MOD3_RPAD + ks.pad; }   /* the Pad's record */
static void kt_sheet(uint32_t sel)
{
    char t[12];
    const track_t *k = &trk[ks.trk];
    const pads_c_t *c = kt_pads();
    if (!rec_name(ks.slot, t, 6)) {                     /* (no name: the selector's name for it, "KIT 1") */
        const param_desc_t *d = c && c->sel >= P_E0 && c->sel < P_E0 + 8u ? track_desc(k, c->sel) : 0;
        int32_t v = kt_value(ks.slot);
        str_cpy(t, d && d->fmt == F_ENUM && d->names && v >= d->min && v <= d->max ? d->names[v] : "", 6);
    }
    sheet_open(t, "record", SHEET_KIT, 4u);
    pop.sel = (uint8_t)sel;
}
/* the synth's rec_stream (MOD3_TS_*): MOD3_TK_* */
static uint32_t kt_stream(const int16_t *x, uint32_t n, uint32_t op)
{
    uint32_t e = eng_idx(trk[ks.trk].eng_req);
    const engine_t *g = ENGINES[e];
    return mod_idle(e) || !g->rec_stream ? MOD3_TK_FLASH : MOD_MAIN(e, g->rec_stream(ks.slot, ks.pad, x, n, op));
}
/* a take begun (Preparing, Waiting) or going on: dropped, its record never published */
static void kt_stop(void)
{
    uint8_t st = kt.state;
    kt.state = KT_OFF;
    if (st == KT_ARMED || st == KT_REC || st == KT_DONE)
        (void)kt_stream(0, 0, MOD3_TS_ABORT);
    ks.saving = 0;
}
static void kt_release(void)
{
    kt_stop();
    UA_MEMORY_GUARD_OFF();
    kt.buf = 0;
    resource_release(RES_KIT_TAKE);
    UA_MEMORY_GUARD_ON();
    ks.on = 0;
    if (ks.prev && !rec_used(ks.slot) && pads_rec(&trk[ks.trk]) == (int32_t)ks.slot)
        kt_kit(ks.prev);                                /* nothing kept: the kit it played before */
}
static void kit_rec_open(void)
{
    static const uint32_t TRY[] = {98304u, 65536u, 49152u, 32768u, 16384u};
    const pads_c_t *c = pads_of(TSEL);
    int32_t kit = c && c->sel < P_COUNT ? TSEL->p[c->sel] : 0, r = pads_rec(TSEL);
    uint32_t u = 0, n = c ? c->nrec : 0u;
    if (transport_busy()) { ui_message("STOP FIRST"); return; }
    if (!c || !(c->flags & MOD3_PD_RECORD) || !n) return;
    if (r >= 0) u = (uint32_t)r;
    else while (u < n && rec_used(u)) u++;              /* (a pad the audio does not list is still the kit's) */
    if (u >= n) { ui_message("8 KITS IN USE"); return; }
    ks.trk = (uint8_t)song.sel; ks.prev = 0;
    if (r != (int32_t)u) {                              /* the keys play the kit recorded into (the kit before: ks.prev) */
        ks.prev = (int16_t)kit;
        kt_kit(c->rec0 + (int32_t)u);
    }
    void *p = 0;
    uint32_t b = 0;
    UA_MEMORY_GUARD_OFF();
    (void)eng_state_prepare(TSEL);                      /* its part in the arena before the take's ring */
    UA_MEMORY_GUARD_ON();
    for (uint32_t i = 0; !p && i < NELEM(TRY); i++) {
        b = TRY[i];
        UA_MEMORY_GUARD_OFF();
        p = resource_allocate(RES_KIT_TAKE, b);
        UA_MEMORY_GUARD_ON();
    }
    if (!p) { if (ks.prev) kt_kit(ks.prev); ui_message("NO MEMORY"); return; }
    kt.state = KT_OFF;
    kt.buf = p;
    kt.cap = b / 2u;
    ks.slot = (uint8_t)u; ks.pad = 0; ks.msg = MOD3_TK_NONE; ks.saving = 0;
    kh.news = kh.down = kh.rels = 0; ks.held = 1;
    ks.on = 1;
    kt_sheet(1);
}
static void kt_pad_v(char *b) { str_cpy(b, pads_name(&trk[ks.trk], ks.pad), 8); }
static void kt_pad_to(uint32_t pad)                     /* the Pad: a take waiting goes to it (made ready again) */
{
    uint8_t st = kt.state;
    if (pad == ks.pad) return;
    if (st == KT_ARMED) kt_stop();
    ks.pad = (uint8_t)pad;
    ks.msg = MOD3_TK_NONE;
    if (st == KT_ARMED || st == KT_PREP) { kt.state = KT_PREP; ks.prep = 0; }
}
static void kt_pad_turn(int32_t s)
{
    uint32_t n = pads_count(&trk[ks.trk]);
    if (kt.state == KT_REC || kt.state == KT_DONE) return;
    kt_pad_to((ks.pad + (s > 0 ? 1u : n - 1u)) % n);
}
static void kt_pad_act(void) { kt_pad_turn(1); kt_sheet(0); }
#define KT_FULL (MOD3_TK_FLASH + 1u)                    /* ks.msg: kept, the store full */
#define KT_CUT (MOD3_TK_FLASH + 2u)                     /* .. kept, the synth fell behind */
#define KT_HIDDEN (MOD3_TK_FLASH + 3u)                  /* .. the pad holds a take the audio does not list */
static void kt_rec_v(char *b)
{
    static const char *const MSG[] = {0, "Kept", "Too quiet", "No room", "Not saved", "Store full", "Cut short", "In use"};
    if (kt.state == KT_PREP) str_cpy(b, "Preparing", 10);
    else if (kt.state == KT_ARMED) str_cpy(b, "Waiting", 10);
    else if (kt.state == KT_REC) { fmt_fix(b, (int32_t)(kt.n * 10u / FS), 1); str_cpy(b + str_len(b), " s", 3); }
    else if (kt.state == KT_DONE) str_cpy(b, "Saving", 10);
    else if (ks.msg) str_cpy(b, MSG[ks.msg], 11);
    else str_cpy(b, rec_tab[kt_slot()].n ? "Taken" : "Empty", 10);
}
static int kt_go(void)                                  /* Record: the store made ready, then the first hit awaited */
{
    if (transport_busy()) { ui_message("STOP FIRST"); return 0; }
    if (rec_hidden(kt_slot())) { ks.msg = KT_HIDDEN; return 0; }   /* (Delete kit still empties it) */
    ks.msg = MOD3_TK_NONE;
    ks.prep = 0;
    kt.state = KT_PREP;
    return 1;
}
static void kt_arm(void) { if (kt.state == KT_OFF) kt_go(); kt_sheet(1); }
static void kt_clear(void)
{
    kt_stop();
    if (rec_hidden(kt_slot())) ks.msg = KT_HIDDEN;
    else ks.msg = (uint8_t)(rec_delete_pad(kt_slot()) ? MOD3_TK_FLASH : MOD3_TK_NONE);
    kt_sheet(2);
}
static void kt_del(void) { kt_stop(); confirm_open(CF_DEL_KIT, ks.slot); }
static void kit_rec_delete(uint32_t u)                  /* CF_DEL_KIT's answer (ui_input.c) */
{
    if (rec_delete(u)) ui_message("NOT DELETED");
}
static const sheet_row_t SHEET_KIT[] = {{"Pad", kt_pad_v, kt_pad_turn, kt_pad_act, 0}, {"Record", kt_rec_v, 0, kt_arm, 0},
                                        {"Clear pad", 0, 0, kt_clear, SR_RED}, {"Delete kit", 0, 0, kt_del, CF_DEL_KIT}};
/* a frame drawn since the state was shown (ui_input runs between frames): "Preparing" and "Saving" are on the screen
 * before the main loop goes away for them */
static int kt_drawn(void)
{
    if (!ks.saving) { ks.saving = 1; ks.t = fm1_ms; ks.f = ui.frame; return 0; }
    if (ui.frame == ks.f && fm1_ms - ks.t < 100u) return 0;
    ks.saving = 0;
    return 1;
}
/* the ring into the synth, KT_FEED samples at most: its word (MOD3_TK_NONE while it goes on) */
static uint32_t kt_feed_ring(void)
{
    uint32_t budget = KT_FEED, st = MOD3_TK_NONE;
    while (budget && kt.r != kt.n) {
        uint32_t k = kt.n - kt.r;
        if (k > kt.cap - kt.ri) k = kt.cap - kt.ri;
        if (k > budget) k = budget;
        st = kt_stream(kt.buf + kt.ri, k, MOD3_TS_FEED);
        kt.ri = kt.ri + k == kt.cap ? 0u : kt.ri + k;
        kt.r += k;
        budget -= k;
        if (st != MOD3_TK_NONE) break;
    }
    return st;
}
/* the main loop: a pad struck (the Pad; held: Record), the store made ready, the take fed to the synth as it comes and
 * kept once it ends, the ring back when the sheet goes. Every change shows through the card's values (sheet_draw
 * redraws it when one differs) */
static void kit_rec_poll(void)
{
    if (!ks.on) return;
    if (pop.on != POP_SHEET || pop.rows != SHEET_KIT) { kt_release(); return; }
    if (kt.state != KT_OFF && transport_busy()) kt_stop();
    uint32_t hp = pads_pad_of(&trk[ks.trk], kh.note);   /* (the pad the last note struck) */
    if (kh.rels) {                                      /* a note let go: the press of its pad ends */
        kh.rels = 0;
        if (pads_pad_of(&trk[ks.trk], kh.rel) == hp) kh.down = 0;
    }
    if (kh.news) {                                      /* struck: that pad (not while a take runs: it is another's) */
        kh.news = 0;
        ks.held = 0;
        if (kt.state != KT_REC && kt.state != KT_DONE) kt_pad_to(hp);
    }
    if (kh.down && !ks.held && fm1_ms - kh.t >= KT_HOLD_MS) {   /* held: Record, once a press */
        ks.held = 1;
        if (kt.state == KT_OFF && ks.pad == hp && kt_go()) pop.sel = 1;
    }
    if (kt.state == KT_PREP) {                          /* the free store erased now: none while the take runs */
        if (!kt_drawn()) return;
        int room = rec_take_begin(kt_slot());
        if (room < 0) { kt.state = KT_OFF; ks.msg = (uint8_t)(room == -5 ? MOD3_TK_ROOM : MOD3_TK_FLASH); return; }
        if (kt_stream(0, (uint32_t)room, MOD3_TS_BEGIN) != MOD3_TK_NONE) { rec_take_abort(); kt.state = KT_OFF; ks.msg = MOD3_TK_FLASH; return; }
        kt.pw = 0;
        kt.n = kt.r = 0;
        ks.stop = MOD3_TK_NONE;
        kt.state = KT_ARMED;
        return;
    }
    if (kt.state == KT_REC || (kt.state == KT_DONE && kt.r != kt.n && !ks.stop)) {
        uint32_t st = kt_feed_ring();
        if (st != MOD3_TK_NONE) { ks.stop = (uint8_t)st; kt.state = KT_DONE; }   /* (a full store: what came is the take) */
        return;
    }
    if (kt.state != KT_DONE) return;
    if (!kt_drawn()) return;                            /* "Saving" drawn first */
    uint32_t end = kt_stream(0, kt.r, MOD3_TS_END);
    ks.msg = (uint8_t)(end != MOD3_TK_KEPT ? end : ks.stop == MOD3_TK_ROOM ? KT_FULL : kt.over ? KT_CUT : MOD3_TK_KEPT);
    if (ks.stop == MOD3_TK_FLASH) ks.msg = MOD3_TK_FLASH;
    kt.state = KT_OFF;
    if (end == MOD3_TK_KEPT && pads_rec(&trk[ks.trk]) != (int32_t)ks.slot)
        kt_kit(kt_value(ks.slot));                      /* the track plays the kit it records */
}
