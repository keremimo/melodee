/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* USB-only calibration recovery. 81: [0] status, [1, packed checked copy] import.
 * The musical store and stock VM are never addressed by the caller. */
#define ED_BLE_CAL 81u
static int ed_ble_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
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
    ed_b(rc); ed_b(1); ed_b(mb.cal); ed_b(mb.on); ed_b(mb.ready);
    ed_b(mb_revision == mb_committed && ble_rf_copy_ok(mb_saved.rf));
    uint32_t crc = ble_rf_copy_ok(mb_saved.rf) ? ble_rd16(mb_saved.rf + 98) : 0;
    ed_b(crc); ed_b(crc >> 7); ed_b(crc >> 14);
    return 1;
}
