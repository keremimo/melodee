/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Settings > Bluetooth: PRESETS/K2 chooses, OCT+/SAVE acts, OCT- returns.
 * The ongoing connection state and passkey stay in the shared header. */
static uint8_t ble_row;
static uint32_t ble_page_sig;

static int ble_start_quiet(void)
{
    if (mb.started) return 1;
    if (transport_busy()) return 0;
    for (uint32_t k = 0; k < NPART; k++)
        for (uint32_t v = 0; v < NVOICE; v++) if (trk[k].v[v].active) return 0;
#if MELODEE_USB_AUDIO
    if (ua.play_alt || ua.cap_alt) return 0;
#endif
    return 1;
}
static void ble_page_open(void)
{ ui.menu = 3; ble_row = 0; ui.force = 1; mel_ble_scan(1); }

static void ble_page_input(uint32_t oct)
{
    int32_t delta = panel_enc(EN_PRESET) + panel_enc(EN_K2);
    ble_row = (uint8_t)clamp((int32_t)ble_row + delta, 0, (int32_t)mb.count + 3);
    int32_t turn = panel_enc(EN_K1), link = panel_enc(EN_K4);
    if (oct & 1u) { mel_ble_scan(0); ui.menu = 1; ui.force = 1; return; }
    if ((oct & 2u) || turn || link) {
        if (ble_row == 0) {
            int on = turn ? turn > 0 : !mb.on;
            if (on && !ble_start_quiet()) ui_message("STOP AUDIO TO START BLE");
            else if (!mel_ble_set(on)) ui_message(RES_OK() ? "RADIO CALIBRATION MISSING" : "FINISH SETUP IN WEB APP");
            mel_ble_scan(1);
        } else if (ble_row == 1) {
            mel_ble_disconnect(); mel_ble_scan(1);
        } else if (ble_row == 2) {
            (void)mel_ble_last();
        } else if (ble_row == 3) {
            mel_ble_scan(0); ui.menu = 0; confirm_open(CF_BLE_FORGET, 0);
        } else {
            (void)mel_ble_connect(ble_row - 4u);
        }
        ui.force = 1;
    }
    /* A scan is a page action, never a background reconnection/pairing loop. */
    if (mb.on && !mb.attempt && !ble_connected() && !mb.scanning && !mb.failure[0]) mel_ble_scan(1);
    enc_drop();
}

static void draw_ble_page(void)
{
    uint32_t sig = mel_ble_signature() + ble_row * 1009u;
    draw_head();
    if (!ui.force && sig == ble_page_sig) return;
    ble_page_sig = sig;
    uint32_t first = ble_row >= 4u ? ble_row - 3u : 0;
    for (uint32_t band = 0; band < 210u; band += CV_MAX / 240u) {
        cv_begin_band(240, 210, T_BG, band); cv_oy = -30;
        for (uint32_t r = first; r < first + 4u && r < 4u + mb.count; r++) {
            char name[32], value[20]; value[0] = 0;
            if (r == 0) { str_cpy(name, "Radio", sizeof name); str_cpy(value, mb.on ? "ON" : "OFF", sizeof value); }
            else if (r == 1) str_cpy(name, "Disconnect", sizeof name);
            else if (r == 2) { str_cpy(name, "Last device", sizeof name); ble_store_name(&mb.store, value); }
            else if (r == 3) str_cpy(name, "Forget pairings", sizeof name);
            else {
                const struct ble_found *f = &mb.found.e[mb.idx[r - 4u]];
                if (f->name[0]) str_cpy(name, f->name, sizeof name);
                else ble_addr_text(f->addr, name);
            }
            row_draw(34 + (int32_t)(r - first) * 36, name, value, T_TEXT, r == ble_row, 0, r);
        }
        cv_text(8, 188, TY_LB, mel_ble_code() != BLE_NO_PASSKEY ? "ENTER CODE ON PEER" : "K2 PICK   SAVE LINK", T_MID);
        cv_text(8, 212, TY_LB, "OCT- BACK", T_MID);
        cv_oy = 0; cv_blit_band(0, 30);
    }
}
