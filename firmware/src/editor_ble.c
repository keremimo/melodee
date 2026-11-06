/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* USB-only calibration recovery. 81: [0] status, [1, packed checked copy] import.
 * The musical store and stock VM are never addressed by the caller. BLE (52): Melodee Studio's Bluetooth. */
#include "editor_wire.h"
#define ED_BLE_CAL 81u
#define ED_BLE 52u
static uint8_t ed_bt_scan;                       /* BLE: the editor's scan (it ends 3 s after its last request: editor_device.c) */
/* BLE (52), Melodee Studio's Bluetooth: the state, then what the Bluetooth page does (ui_ble.c): flags (1 calibrated, 2 the
 * radio on, 4 Mode VISIBLE, 8 scanning, 16 connecting out, 32 relinking), the links (per link: 1 MIDI up, 2 connected;
 * link 0 the computer's, link 1 a device Melodee connected out to), the code to enter on the peer (3 x 7 bits, all ones:
 * none), the header's status, the computer's pairing kept, the last device (1 kept, 2 paired) and its name, what the scan
 * found (CONNECT OUT) */
static void ed_bt_state(void)
{
    char nm[BLE_NAME_MAX + 1u];
    uint32_t i, code = mel_ble_code(), nf = ble_found_n();
    ed_b(mb.cal | mb.on << 1 | (mb_saved.flags & MB_VISIBLE) << 2 | mb.scanning << 3 | mb.attempt << 4 | mb.relink << 5);
    ed_b(BLE_LINKS);
    for (i = 0; i < BLE_LINKS; i++)
        ed_b(mb.ln[i].ready | (uint32_t)!!mb_link_up(i) << 1);
    ed_n7(code == BLE_NO_PASSKEY ? 0x1FFFFFu : code, 3u);
    ed_str(mel_ble_status(), 24);
    ed_b(mb_saved.bond[0] == 0xB5u && mb_saved.bond[1] == 1u);
    ed_b(ble_store_has_last(&mb.store) | (uint32_t)!!(mb.store.dev.info & BLE_DEV_BONDED) << 1);
    ble_store_name(&mb.store, nm);
    ed_str(nm, BLE_NAME_MAX);
    ed_b(nf);
    for (i = 0; i < nf; i++) {
        const struct ble_found *f = &mb.found.e[mb.idx[i]];
        if (f->name[0]) ed_str(f->name, BLE_NAME_MAX);
        else { ble_addr_text(f->addr, nm); ed_str(nm, BLE_NAME_MAX); }
    }
}
static int ed_ble_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (cmd == ED_BLE) {
        /* [op: 1 on (the radio), 2 visible (Mode: 0 CONNECT OUT, 1 VISIBLE), 3 (disconnect), 4 (the last device), 5 (forget
         * the pairings), 6 on (scan), 7 row (connect to what the scan found)] -> op, rc, the state. Saved with the settings,
         * as the page's (main.c: mb_revision) */
        uint32_t op = ed_arg(a, n, 0), x = ed_arg(a, n, 1), two = op == 1u || op == 2u || op >= 6u, rc = 0;
        if (n && (!op || op > 7u || n != 1u + two || (two && op != 7u && x > 1u)))
            rc = 1;
        else if (op == 1u) {
            if (x && !ble_start_quiet())
                rc = 3;                                  /* (STOP AUDIO TO START BLE: its start holds interrupts) */
            else if (!mel_ble_set((int)x))
                rc = 2;                                  /* (RADIO CALIBRATION MISSING, FINISH SETUP IN WEB APP) */
        } else if (op == 2u)
            mel_ble_visible((int)x);
        else if (op == 3u)
            mel_ble_disconnect();
        else if (op == 4u || op == 7u)
            rc = (op == 4u ? mel_ble_last() : mel_ble_connect(x)) ? 0u : 2u;
        else if (op == 5u)
            mel_ble_forget();                            /* (the page asks first: Studio does) */
        else if (op == 6u) {
            ed_bt_scan = (uint8_t)x;
            mel_ble_scan((int)x);
            rc = x && !mb.scanning ? 2u : 0u;            /* (off, VISIBLE, connecting, or a device linked) */
        }
        ed_echo(a, n, 1u); ed_b(rc);
        ed_bt_state();
        return 1;
    }
    if (cmd != ED_BLE_CAL) return 0;
    uint32_t rc = 1;
    if (n == 1u && a[0] == 0u) rc = 0;
    else if (n == 116u && a[0] == 1u) {
        uint8_t copy[BLE_RF_COPY_SIZE];
        if (ed_unpack7(a + 1, n - 1u, copy, sizeof copy) == sizeof copy && ble_rf_copy_ok(copy)) {
            rc = 0;
            if (transport_busy()) rc = 4;
            for (uint32_t k = 0; k < NPART; k++)
                for (uint32_t v = 0; v < NVOICE; v++) if (trk[k].v[v].active) rc = 4;
#if MELODEE_USB_AUDIO
            if (ua.play_alt || ua.cap_alt) rc = 4;
#endif
            if (!rc) rc = mel_ble_import_cal(copy);
            if (!rc) rc = ed_ui_save();
        }
    }
    ed_b(rc); ed_b(1); ed_b(mb.cal); ed_b(mb.on); ed_b(MB_ANY(ready));
    ed_b(mb_revision == mb_committed && ble_rf_copy_ok(mb_saved.rf));
    uint32_t crc = ble_rf_copy_ok(mb_saved.rf) ? ble_rd16(mb_saved.rf + 98) : 0;
    ed_n7(crc, 3u);
    return 1;
}
