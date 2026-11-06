/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* firmware/src/midi_ble.h and midi_ble.c on fm1-base: the stack runs in the base (device/bt_own.c, with Melodee's copy
 * of it in platform/ble and its rules in platform/ble_own.h); the instrument keeps what it always kept (mb_saved, in its
 * settings) and its pages (ui_ble.c, the header, editor_ble.c) read the base's state through the bridge. Bluetooth
 * MIDI itself comes and goes with the rest (plat_midi_in as source 3, plat_midi_send TO_BLE).
 * The base links the computer and a device at once: Mode VISIBLE leaves the device list empty and the last device
 * alone, CONNECT OUT looks for it, as before. */
#include "ble_saved.h"
#define BLE_NO_PASSKEY 0xFFFFFFFFu
#define BLE_NAME_MAX 16u
#define BLE_SCAN_N 16u
#define BLE_RF_COPY_SIZE ML_BLE_RF_COPY
#define BLE_DEV_STORE_SIZE ML_BLE_DEV_STORE
typedef ml_ble_saved_t mel_ble_saved_t;
struct ble_found { uint8_t addr[6], addr_rand; char name[BLE_NAME_MAX + 5u]; };   /* (the base's names: 21 bytes) */
struct ble_dev_store { uint8_t unused; };
#if BOARD_SMK37
static mel_ble_saved_t mb_saved;                /* (the SMK-37: RAM has room, the pool's arena has not) */
#else
static mel_ble_saved_t mb_saved __attribute__((section(".pool")));
#endif
static volatile uint32_t mb_revision;
static uint32_t mb_committed;
static volatile uint8_t midi_ble_lost;
static struct {
    uint8_t on, ready, started, scanning, cal, boot, attempt, closing, count, was_ready;
    uint8_t idx[BLE_SCAN_N];
    uint32_t epoch_ms, generation;
    char failure[24], shown[24];
    struct { struct ble_found e[BLE_SCAN_N]; uint32_t gen; } found;
    struct ble_dev_store store;
} mb;
static void mb_dirty(void) { mb_saved.magic = MEL_BLE_MAGIC; mb_revision++; }
static void mb_lock(void) { fm1_irq_off(); }
static void mb_unlock(void) { fm1_irq_on(); }
static uint32_t ble_rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static int ble_rf_copy_ok(const uint8_t *copy) { return ml_ble_rf_ok(copy); }
static int ble_connected(void) { return ml_ble_connected() || ml_ble_device_state() == ML_BLE_DEV_READY; }
static void ble_store_name(const struct ble_dev_store *s, char out[BLE_NAME_MAX + 1u])
{
    const char *n = ml_ble_device_name();
    (void)s;
    str_cpy(out, n ? n : "", BLE_NAME_MAX + 1u);
}
static void ble_addr_text(const uint8_t a[6], char *out)
{
    static const char H[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < 6u; i++) {
        out[i * 3u] = H[a[5u - i] >> 4]; out[i * 3u + 1u] = H[a[5u - i] & 15u];
        out[i * 3u + 2u] = i < 5u ? ':' : 0;
    }
}
/* the platform's half (app.c, ble_own.h): the instrument's copy, read and changed there */
ml_ble_saved_t *mlb_ble_saved(void)
{
    if (mb_saved.magic != MEL_BLE_MAGIC) { memset(&mb_saved, 0, sizeof mb_saved); mb_dirty(); }   /* (a first record) */
    return &mb_saved;
}
void mlb_ble_dirty(void) { mb_dirty(); }
int mlb_ble_at_boot(void) { return mb_saved.magic == MEL_BLE_MAGIC && (mb_saved.flags & MB_RADIO); }

static int mel_ble_ready(void) { return mb.ready && mb.on; }
static int mel_ble_set(int on)
{
    uint8_t f;
    mb.boot = 0;
    if (mb_saved.magic != MEL_BLE_MAGIC) { memset(&mb_saved, 0, sizeof mb_saved); mb_saved.magic = MEL_BLE_MAGIC; }
    f = (uint8_t)(on ? mb_saved.flags | MB_RADIO : mb_saved.flags & ~MB_RADIO);
    if (f != mb_saved.flags) { mb_saved.flags = f; mb_dirty(); }
    ml_ble_enable(on);
    ml_ble_relink(on && !(mb_saved.flags & MB_VISIBLE));
    mb.on = (uint8_t)!!on;
    mb.failure[0] = 0;
    mb.generation++;
    return 1;
}
static void mel_ble_scan(int on)
{
    mb.scanning = (uint8_t)(on && mb.on && !(mb_saved.flags & MB_VISIBLE));
}
static void mel_ble_visible(int on)
{
    uint8_t f = (uint8_t)(on ? mb_saved.flags | MB_VISIBLE : mb_saved.flags & ~MB_VISIBLE);
    if (f == mb_saved.flags) return;
    mb_saved.flags = f; mb_dirty();
    mb.failure[0] = 0;
    if (on) mb.scanning = 0;
    ml_ble_relink(mb.on && !on);
    mb.generation++;
}
static void mel_ble_disconnect(void)
{
    ml_ble_relink(0);
    ml_ble_drop_host();
    ml_ble_device_drop();
    mb.failure[0] = 0;
    mb.generation++;
}
static void mel_ble_forget(void)
{
    mel_ble_disconnect();
    memset(mb_saved.bond, 0, sizeof mb_saved.bond);
    mb_dirty();
    ml_ble_forget_arm(1);
    ml_ble_forget_confirm();                        /* (the base ends the links, then clears the bonds: ml_ble_page) */
}
static int mel_ble_connect(uint32_t row)
{
    if (!mb.on || row >= mb.count || mb.attempt) return 0;
    return ml_ble_device_connect((int)mb.idx[row]) == 0;
}
static int mel_ble_last(void)
{
    if (!mb.on) return 0;
    ml_ble_relink(1);
    return 1;
}
static uint32_t mel_ble_code(void)
{
    int32_t c = mb.on ? ml_ble_pairing_code() : -1;
    return c < 0 ? BLE_NO_PASSKEY : (uint32_t)c;
}
static void mel_ble_poll(void)                  /* main loop, every frame */
{
    uint32_t i, n, dev = (uint32_t)ml_ble_device_state();
    const char *msg;
    char name[BLE_NAME_MAX + 5u];
    int rssi;
    uint8_t ready;
    mb.on = (uint8_t)ml_ble_enabled();
    mb.started = (uint8_t)(ml_ble_state() != ML_BLE_OFF);
    mb.cal = 1;                                 /* (the base measures it when no stored copy: ble_own.h) */
    msg = ml_ble_page(mb.scanning, mb.scanning);   /* (the page open: its scan; no relink rounds meanwhile) */
    if (msg && msg[0]) { str_cpy(mb.failure, msg, sizeof mb.failure); mb.generation++; }
    ready = (uint8_t)(mb.on && (ml_ble_connected() || dev == ML_BLE_DEV_READY));
    if (mb.was_ready && !ready) midi_ble_lost = 1;
    if (ready && !mb.was_ready) mb.failure[0] = 0;
    mb.was_ready = mb.ready = ready;
    mb.attempt = (uint8_t)(dev == ML_BLE_DEV_CONNECTING || dev == ML_BLE_DEV_SEARCHING);
    n = (uint32_t)ml_ble_device_count();
    if (n > BLE_SCAN_N) n = BLE_SCAN_N;
    if (mb_saved.flags & MB_VISIBLE) n = 0;
    for (i = 0; i < n; i++) {
        struct ble_found *f = &mb.found.e[i];
        name[0] = 0;
        if (!ml_ble_device_found((int)i, name, &rssi)) { n = i; break; }
        if (memcmp(f->name, name, sizeof f->name)) { memset(f->name, 0, sizeof f->name); str_cpy(f->name, name, sizeof f->name); mb.found.gen++; }
        mb.idx[i] = (uint8_t)i;
    }
    if (n != mb.count) { mb.count = (uint8_t)n; mb.found.gen++; }
}
static const char *mel_ble_status(void)
{
    if (!mb.on) return "BLE OFF";
    if (mel_ble_code() != BLE_NO_PASSKEY) return "PAIRING CODE";
    if (mb.ready) return "BLE CONNECTED";
    if (mb.failure[0]) return mb.failure;
    if (mb.attempt) return "FINDING MIDI";
    if (ml_ble_linked()) return "WAITING FOR MIDI";
    if (mb.scanning) return "SCANNING";
    if (!(mb_saved.flags & MB_VISIBLE)) return "BLE AVAILABLE";
    str_cpy(mb.shown, "VISIBLE", sizeof mb.shown);
    return mb.shown;
}
static uint32_t mel_ble_signature(void)
{ return mb.generation + mb.found.gen * 13u + (uint32_t)mb.ready * 997u + mel_ble_code() * 17u + (uint32_t)mb.attempt * 65537u; }
/* USB maintenance only (editor_ble.c, cmd 81): a checked trim copy, used at the radio's next start (ble_own.h) */
static uint32_t mel_ble_import_cal(const uint8_t copy[BLE_RF_COPY_SIZE])
{
    if (!ble_rf_copy_ok(copy)) return 1;
    if (mb.on || mb.closing) return 4;
    if (ble_rf_copy_ok(mb_saved.rf) && memcmp(mb_saved.rf, copy, BLE_RF_COPY_SIZE)) return 2;
    memcpy(mb_saved.rf, copy, BLE_RF_COPY_SIZE);
    mb_dirty(); mb.generation++;
    return 0;
}
