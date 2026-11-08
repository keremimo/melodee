/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Record kit: the sound's sheet on a track whose synth records its pads (mod_schema.h MOD3_PD_RECORD: its data
 * records, rec_store.c; a take made a pad's by the synth, engine_t.rec_take), "Record kit..." opens the kit's own sheet:
 *   Pad          KNOB 1 (or OCT+) the drum the next take goes to
 *   Record       OCT+: it waits for audio from the computer or the phone on USB, the take starts at the first hit
 *                and ends at 150 ms of silence or a full buffer; it is kept at once (the keys play it)
 *   Clear pad    the pad's take gone (red)
 *   Delete kit   the whole kit, after its question
 * A pad struck while the sheet is open (a key, an SMK-37 pad, a note from MIDI in) plays its take and becomes the
 * Pad; held KT_HOLD_MS it is Record's OCT+ too. The track plays the kit recorded into while the sheet is open: its
 * own, or the first empty record slot (the track's kit back when the sheet goes without a take kept). The transport
 * stays stopped; the take's buffer is borrowed from the audio arena while the sheet is open (kit_rec_poll gives it
 * back when the sheet goes). The card alone redraws as its values change (sheet_draw's sig): no page under it. */
#define KT_HOLD_MS 500u
static struct { uint8_t on, slot, pad, saving, msg, trk, held; int16_t prev; uint32_t t, f; } ks;
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
static void kt_sheet(uint32_t sel)
{
    char t[12];
    const track_t *k = &trk[ks.trk];
    const pads_c_t *c = kt_pads();
    if (!rec_name(ks.slot, t, 6)) {                     /* (no record: the selector's name for it, "KIT 1") */
        const param_desc_t *d = c && c->sel >= P_E0 && c->sel < P_E0 + 8u ? track_desc(k, c->sel) : 0;
        int32_t v = kt_value(ks.slot);
        str_cpy(t, d && d->fmt == F_ENUM && d->names && v >= d->min && v <= d->max ? d->names[v] : "", 6);
    }
    sheet_open(t, "record", SHEET_KIT, 4u);
    pop.sel = (uint8_t)sel;
}
static void kt_release(void)
{
    kt.state = KT_OFF;
    UA_MEMORY_GUARD_OFF();
    kt.buf = 0;
    resource_release(RES_KIT_TAKE);
    UA_MEMORY_GUARD_ON();
    ks.on = 0;
    if (ks.prev && !rec_tab[ks.slot].n && pads_rec(&trk[ks.trk]) == (int32_t)ks.slot)
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
    else while (u < n && rec_tab[u].n) u++;
    if (u >= n) { ui_message("8 KITS IN USE"); return; }
    ks.trk = (uint8_t)song.sel; ks.prev = 0;
    if (r != (int32_t)u) {                              /* the keys play the kit recorded into (the kit before: ks.prev) */
        ks.prev = (int16_t)kit;
        kt_kit(c->rec0 + (int32_t)u);
    }
    void *p = 0;
    uint32_t b = 0;
    UA_MEMORY_GUARD_OFF();
    (void)eng_state_prepare(TSEL);                      /* its part in the arena before the take's buffer */
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
    kt.cap = b / 2u - 64u;
    ks.slot = (uint8_t)u; ks.pad = 0; ks.msg = MOD3_TK_NONE; ks.saving = 0;
    kh.news = kh.down = kh.rels = 0; ks.held = 1;
    ks.on = 1;
    kt_sheet(1);
}
static void kt_pad_v(char *b) { str_cpy(b, pads_name(&trk[ks.trk], ks.pad), 8); }
static void kt_pad_turn(int32_t s)
{
    uint32_t n = pads_count(&trk[ks.trk]);
    ks.pad = (uint8_t)((ks.pad + (s > 0 ? 1u : n - 1u)) % n);
    if (kt.state == KT_ARMED) kt.pw = 0;              /* (still waiting, for this pad) */
    ks.msg = MOD3_TK_NONE;
}
static void kt_pad_act(void) { kt_pad_turn(1); kt_sheet(0); }
static void kt_rec_v(char *b)
{
    static const char *const MSG[] = {0, "Kept", "Too quiet", "No room", "Not saved"};
    const engine_t *g = ENGINES[eng_idx(trk[ks.trk].eng_req)];
    uint32_t e = eng_idx(trk[ks.trk].eng_req);
    if (kt.state == KT_ARMED) str_cpy(b, "Waiting", 10);
    else if (kt.state == KT_REC) { fmt_fix(b, (int32_t)(kt.n * 10u / FS), 1); str_cpy(b + str_len(b), " s", 3); }
    else if (kt.state == KT_DONE) str_cpy(b, "Saving", 10);
    else if (ks.msg) str_cpy(b, MSG[ks.msg], 10);
    else str_cpy(b, !mod_idle(e) && g->rec_pad && MOD_MAIN(e, g->rec_pad(ks.slot, ks.pad)) ? "Taken" : "Empty", 10);
}
static int kt_go(void)                                  /* Record: waiting for the first hit */
{
    if (transport_busy()) { ui_message("STOP FIRST"); return 0; }
    kt.pw = kt.n = 0;
    ks.msg = MOD3_TK_NONE;
    kt.state = KT_ARMED;
    return 1;
}
static void kt_arm(void) { kt_go(); kt_sheet(1); }
static uint32_t kt_take(int16_t *x, uint32_t n, uint32_t cap)   /* the synth's rec_take (n 0: the pad cleared) */
{
    uint32_t e = eng_idx(trk[ks.trk].eng_req);
    const engine_t *g = ENGINES[e];
    return mod_idle(e) || !g->rec_take ? MOD3_TK_FLASH : MOD_MAIN(e, g->rec_take(ks.slot, ks.pad, x, n, cap));
}
static void kt_clear(void)
{
    kt.state = KT_OFF;
    ks.msg = (uint8_t)(kt_take(0, 0, 0) == MOD3_TK_FLASH ? MOD3_TK_FLASH : MOD3_TK_NONE);
    kt_sheet(2);
}
static void kt_del(void) { kt.state = KT_OFF; confirm_open(CF_DEL_KIT, ks.slot); }
static void kit_rec_delete(uint32_t u)                  /* CF_DEL_KIT's answer (ui_input.c) */
{
    if (rec_delete(u)) ui_message("NOT DELETED");
}
static const sheet_row_t SHEET_KIT[] = {{"Pad", kt_pad_v, kt_pad_turn, kt_pad_act, 0}, {"Record", kt_rec_v, 0, kt_arm, 0},
                                        {"Clear pad", 0, 0, kt_clear, SR_RED}, {"Delete kit", 0, 0, kt_del, CF_DEL_KIT}};
/* the main loop: a pad struck (the Pad; held: Record), the take kept once it ends, the buffer back when the sheet
 * goes. Every change shows through the card's values (sheet_draw redraws it when one differs) */
static void kit_rec_poll(void)
{
    if (!ks.on) return;
    if (pop.on != POP_SHEET || pop.rows != SHEET_KIT) { kt_release(); return; }
    if (kt.state != KT_OFF && kt.state != KT_DONE && transport_busy()) kt.state = KT_OFF;
    uint32_t hp = pads_pad_of(&trk[ks.trk], kh.note);   /* (the pad the last note struck) */
    if (kh.rels) {                                      /* a note let go: the press of its pad ends */
        kh.rels = 0;
        if (pads_pad_of(&trk[ks.trk], kh.rel) == hp) kh.down = 0;
    }
    if (kh.news) {                                      /* struck: that pad (not while a take runs: it is another's) */
        kh.news = 0;
        ks.held = 0;
        if (kt.state != KT_REC && kt.state != KT_DONE && ks.pad != hp) {
            ks.pad = (uint8_t)hp;
            if (kt.state == KT_ARMED) kt.pw = 0;
            ks.msg = MOD3_TK_NONE;
        }
    }
    if (kh.down && !ks.held && fm1_ms - kh.t >= KT_HOLD_MS) {   /* held: Record, once a press */
        ks.held = 1;
        if (kt.state == KT_OFF && ks.pad == hp && kt_go()) pop.sel = 1;
    }
    if (kt.state != KT_DONE) return;
    if (!ks.saving) { ks.saving = 1; ks.t = fm1_ms; ks.f = ui.frame; return; }
    if (ui.frame == ks.f && fm1_ms - ks.t < 100u) return;   /* "Saving" drawn first (ui_input runs between frames) */
    ks.msg = (uint8_t)kt_take(kt.buf, kt.n, kt.cap);
    ks.saving = 0;
    kt.state = KT_OFF;
    if (ks.msg == MOD3_TK_KEPT && pads_rec(&trk[ks.trk]) != (int32_t)ks.slot)
        kt_kit(kt_value(ks.slot));                      /* the track plays the kit it records */
}
