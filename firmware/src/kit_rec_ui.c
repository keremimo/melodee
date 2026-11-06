/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Record kit: the sound's sheet on a drum track (OCT+ held), "Record kit..." opens the kit's own sheet:
 *   Pad          KNOB 1 (or OCT+) the drum the next take goes to
 *   Record       OCT+: it waits for audio from the computer or the phone on USB, the take starts at the first hit
 *                and ends at 150 ms of silence or a full buffer; it is kept at once (the keys play it)
 *   Clear pad    the pad's take gone (red)
 *   Delete kit   the whole kit, after its question
 * A pad struck while the sheet is open (a key, an SMK-37 pad, a note from MIDI in) plays its take and becomes the
 * Pad; held KT_HOLD_MS it is Record's OCT+ too. The track plays the kit recorded into while the sheet is open: its
 * own, or the first empty user slot (the track's kit back when the sheet goes without a take kept). The transport
 * stays stopped; the take's buffer is borrowed from the audio arena while the sheet is open (kit_rec_poll gives it
 * back when the sheet goes). The card alone redraws as its values change (sheet_draw's sig): no page under it. */
#define KT_HOLD_MS 500u
static struct { uint8_t on, slot, pad, saving, msg, trk, held; int16_t prev; uint32_t t, f; } ks;
/* the sheet's track struck (input_on / input_off, the audio ISR's or the UI's): the drum, when, still down */
static volatile struct { uint8_t pad, down, news; uint32_t t; } kh;
static const sheet_row_t SHEET_KIT[];
static void kit_rec_hear(const track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t d;
    if (!ks.on || t != &trk[ks.trk] || !drum_isuser(t) || (d = drum909_ins(note)) >= DRK_NDRUM)
        return;
    if (vel) { kh.pad = (uint8_t)d; kh.t = fm1_ms; kh.down = 1; kh.news = 1; }
    else if (d == kh.pad) kh.down = 0;
}
static void kt_kit(int32_t kit)                         /* the sheet's track to this kit */
{
    track_t *t = &trk[ks.trk];
    t->p[P_E0] = (int16_t)kit;
    load_extend(t);
}
static void kt_sheet(uint32_t sel)
{
    char t[12];
    drk_user_name(ks.slot, t);
    if (str_eq(t, "EMPTY")) { str_cpy(t, "KIT 1", 6); t[4] = (char)('1' + ks.slot); }
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
    if (ks.prev && !drk_user[ks.slot].n && trk[ks.trk].p[P_E0] == DK_USER0 + ks.slot)
        kt_kit(ks.prev);                                /* nothing kept: the kit it played before */
}
static void kit_rec_open(void)
{
    static const uint32_t TRY[] = {98304u, 65536u, 49152u, 32768u, 16384u};
    int32_t kit = TSEL->p[P_E0];
    uint32_t u = 0;
    if (transport_busy()) { ui_message("STOP FIRST"); return; }
    if (kit >= DK_USER0 && kit <= DK_KIT_MAX) u = (uint32_t)(kit - DK_USER0);
    else while (u < ST_NKIT && drk_user[u].n) u++;
    if (u >= ST_NKIT) { ui_message("8 KITS IN USE"); return; }
    ks.trk = (uint8_t)song.sel; ks.prev = 0;
    if (kit != DK_USER0 + (int32_t)u) {                 /* the keys play the kit recorded into (KIT >= DK_808: 0 none) */
        ks.prev = (int16_t)kit;
        kt_kit(DK_USER0 + (int32_t)u);
    }
    void *p = 0;
    uint32_t b = 0;
    UA_MEMORY_GUARD_OFF();
    (void)drk_of(TSEL);                                 /* its part in the arena before the take's buffer */
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
    ks.slot = (uint8_t)u; ks.pad = D9_BD; ks.msg = KM_NONE; ks.saving = 0;
    kh.news = kh.down = 0; ks.held = 1;
    ks.on = 1;
    kt_sheet(1);
}
static void kt_pad_v(char *b) { str_cpy(b, DRUM_SOUND_NAME[ks.pad], 8); }
static void kt_pad_turn(int32_t s)
{
    ks.pad = (uint8_t)((ks.pad + (s > 0 ? 1u : DRK_NDRUM - 1u)) % DRK_NDRUM);
    if (kt.state == KT_ARMED) kt.pw = 0;              /* (still waiting, for this pad) */
    ks.msg = KM_NONE;
}
static void kt_pad_act(void) { kt_pad_turn(1); kt_sheet(0); }
static void kt_rec_v(char *b)
{
    static const char *const MSG[] = {0, "Kept", "Too quiet", "No room", "Not saved"};
    const uint32_t *secs[DRK_KSEC];
    uint32_t n = kt_secs(ks.slot, secs);
    const drk_kit_t *O = n ? (const drk_kit_t *)(const void *)secs[0] : 0;
    if (kt.state == KT_ARMED) str_cpy(b, "Waiting", 10);
    else if (kt.state == KT_REC) { fmt_fix(b, (int32_t)(kt.n * 10u / FS), 1); str_cpy(b + str_len(b), " s", 3); }
    else if (kt.state == KT_DONE) str_cpy(b, "Saving", 10);
    else if (ks.msg) str_cpy(b, MSG[ks.msg], 10);
    else str_cpy(b, O && O->map[ks.pad].sample != 0xFFu ? "Taken" : "Empty", 10);
}
static int kt_go(void)                                  /* Record: waiting for the first hit */
{
    if (transport_busy()) { ui_message("STOP FIRST"); return 0; }
    kt.pw = kt.n = 0;
    ks.msg = KM_NONE;
    kt.state = KT_ARMED;
    return 1;
}
static void kt_arm(void) { kt_go(); kt_sheet(1); }
static void kt_clear(void)
{
    kt.state = KT_OFF;
    ks.msg = kt_write(ks.slot, ks.pad, 0, 0, 0, 0) ? KM_FLASH : KM_NONE;
    kt_sheet(2);
}
static void kt_del(void) { kt.state = KT_OFF; confirm_open(CF_DEL_KIT, ks.slot); }
static void kit_rec_delete(uint32_t u)                  /* CF_DEL_KIT's answer (ui_input.c) */
{
    if (kit_delete(u)) ui_message("NOT DELETED");
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
    if (kh.news) {                                      /* struck: that pad (not while a take runs: it is another's) */
        kh.news = 0;
        ks.held = 0;
        if (kt.state != KT_REC && kt.state != KT_DONE && ks.pad != kh.pad) {
            ks.pad = kh.pad;
            if (kt.state == KT_ARMED) kt.pw = 0;
            ks.msg = KM_NONE;
        }
    }
    if (kh.down && !ks.held && fm1_ms - kh.t >= KT_HOLD_MS) {   /* held: Record, once a press */
        ks.held = 1;
        if (kt.state == KT_OFF && ks.pad == kh.pad && kt_go()) pop.sel = 1;
    }
    if (kt.state != KT_DONE) return;
    if (!ks.saving) { ks.saving = 1; ks.t = fm1_ms; ks.f = ui.frame; return; }
    if (ui.frame == ks.f && fm1_ms - ks.t < 100u) return;   /* "Saving" drawn first (ui_input runs between frames) */
    ks.msg = (uint8_t)kt_keep(ks.slot, ks.pad, kt.buf, kt.n, kt.cap);
    ks.saving = 0;
    kt.state = KT_OFF;
    if (ks.msg == KM_KEPT && trk[ks.trk].p[P_E0] != DK_USER0 + ks.slot)
        kt_kit(DK_USER0 + (int32_t)ks.slot);            /* the track plays the kit it records */
}
