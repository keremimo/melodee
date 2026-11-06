/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Record kit: the sound's sheet on a drum track (OCT+ held), "Record kit..." opens the kit's own sheet:
 *   Pad          KNOB 1 (or OCT+) the drum the next take goes to
 *   Record       OCT+: it waits for audio from the computer or the phone on USB, the take starts at the first hit
 *                and ends at 150 ms of silence or a full buffer; it is kept at once (the keys play it)
 *   Clear pad    the pad's take gone (red)
 *   Delete kit   the whole kit, after its question
 * The track's kit is the one recorded into; another kit: the first empty user slot, the track switched to it at the
 * first take. The transport stays stopped; the take's buffer is borrowed from the audio arena while the sheet is
 * open (kit_rec_poll gives it back when the sheet goes). */
static struct { uint8_t on, slot, pad, saving, msg; uint32_t t; } ks;
static const sheet_row_t SHEET_KIT[];
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
    void *p = 0;
    uint32_t b = 0;
    for (uint32_t i = 0; !p && i < NELEM(TRY); i++) {
        b = TRY[i];
        UA_MEMORY_GUARD_OFF();
        p = resource_allocate(RES_KIT_TAKE, b);
        UA_MEMORY_GUARD_ON();
    }
    if (!p) { ui_message("NO MEMORY"); return; }
    kt.state = KT_OFF;
    kt.buf = p;
    kt.cap = b / 2u - 64u;
    ks.on = 1; ks.slot = (uint8_t)u; ks.pad = D9_BD; ks.msg = KM_NONE; ks.saving = 0;
    kt_sheet(1);
}
static void kt_pad_v(char *b) { str_cpy(b, DRUM_SOUND_NAME[ks.pad], 8); }
static void kt_pad_turn(int32_t s)
{
    ks.pad = (uint8_t)((ks.pad + (s > 0 ? 1u : DRK_NDRUM - 1u)) % DRK_NDRUM);
    if (kt.state == KT_ARMED) kt.pw = 0;              /* (still waiting, for this pad) */
    ks.msg = KM_NONE;
    ui.force = 1;
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
static void kt_arm(void)
{
    if (transport_busy()) { ui_message("STOP FIRST"); kt_sheet(1); return; }
    kt.pw = kt.n = 0;
    ks.msg = KM_NONE;
    kt.state = KT_ARMED;
    kt_sheet(1);
}
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
/* the main loop: the take's progress on the sheet, the take kept once it ends, the buffer back when the sheet goes */
static void kit_rec_poll(void)
{
    if (!ks.on) return;
    if (pop.on != POP_SHEET || pop.rows != SHEET_KIT) { kt_release(); return; }
    if (kt.state != KT_OFF && kt.state != KT_DONE && transport_busy()) { kt.state = KT_OFF; ui.force = 1; }
    if (kt.state == KT_REC && fm1_ms - ks.t >= 100u) { ks.t = fm1_ms; ui.force = 1; }
    if (kt.state != KT_DONE) return;
    if (!ks.saving) { ks.saving = 1; ui.force = 1; return; }   /* one frame of "Saving" first */
    ks.msg = (uint8_t)kt_keep(ks.slot, ks.pad, kt.buf, kt.n, kt.cap);
    ks.saving = 0;
    kt.state = KT_OFF;
    if (ks.msg == KM_KEPT && TSEL->p[P_E0] != DK_USER0 + ks.slot) {
        TSEL->p[P_E0] = (int16_t)(DK_USER0 + ks.slot);  /* the track plays the kit it records */
        load_extend(TSEL);
    }
    ui.force = 1;
}
