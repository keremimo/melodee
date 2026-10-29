/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee transport adapter for Optimist's stack (see docs/BLE_MIDI.md).
 * BLE IRQ -> private RX ring -> TIMER5 -> the existing audio input ring.
 * Audio/UI -> TX ring -> BLE IRQ. Foreground mutations mask the BLE IRQs only.
 * One radio link, either role. No USB echo/bridge and no wireless update SysEx. */
#include "ble/ble_stack.c"
#include "ble/ble_vm.c"

#define MBQ 64u
static struct {
    uint32_t rx[MBQ], time[MBQ], tx[MBQ], ms[MBQ];
    volatile uint32_t rw, rr, tw, tr;
    volatile uint8_t ready, lost, overflow;
    uint8_t on, started, cal, scanning, attempt, was_ready, connected, was_pairing, closing, forgetting;
    uint8_t idx[BLE_SCAN_N], count;
    uint32_t epoch_ms, deadline, aged, generation;
    struct ble_rf_trims trims;
    struct ble_scan_tab found;
    struct ble_dev_store store;
    struct ble_found target;
    struct ble_keys keys;
    volatile uint8_t keys_new;
    char failure[24];
} mb __attribute__((section(".pool")));
static volatile uint8_t midi_ble_lost;

static void mb_dirty(void) { mb_saved.magic = MEL_BLE_MAGIC; mb_revision++; }
/* TIMER5 may enter while foreground holds this lock. Keep its unlock from
 * re-enabling BLE underneath a foreground mutation. The depth itself is atomic. */
static uint32_t mb_lock_depth;
static void mb_lock(void)
{
    uint32_t ie = fm1_icfg(); fm1_irq_off();
    if (!mb_lock_depth++ && mb.started) fm1_ble_irqs_hold(1);
    fm1_icfg_set(ie);
}
static void mb_unlock(void)
{
    uint32_t ie = fm1_icfg(); fm1_irq_off();
    if (--mb_lock_depth == 0u && mb.started) fm1_ble_irqs_hold(0);
    fm1_icfg_set(ie);
}

BLE_API int ble_app_midi_peek(uint32_t *pkt, uint32_t *ms)
{
    if (mb.tr == mb.tw) return 0;
    RING_PUBLISH();
    *pkt = mb.tx[mb.tr % MBQ]; *ms = mb.ms[mb.tr % MBQ];
    return 1;
}
BLE_API void ble_app_midi_pop(void) { RING_PUBLISH(); mb.tr++; }
BLE_API void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last)
{
    uint32_t status = (pkt >> 8) & 255u;
    /* Route performance messages and clock only. OTA/editor framing stays USB. */
    if (!mb.on || mb.closing || !((status >= 0x80u && status < 0xF0u) ||
        status == 0xF8u || status == 0xFAu || status == 0xFBu || status == 0xFCu)) return;
    if (mb.rw - mb.rr >= MBQ) { mb.overflow = 1; return; }
    mb.epoch_ms = fm1_ms;
    uint32_t at = mb.rw % MBQ, back = (last - ts) & 0x1FFFu;
    mb.rx[at] = pkt;
    /* Preserve relative timing within the packet without trusting the peer's epoch. */
    mb.time[at] = MIDI_TIME_NOW() - (back < 1000u ? back : 0u) * MIDI_TICKS_PER_MS;
    RING_PUBLISH(); mb.rw++;
}
BLE_API void ble_app_state(void)
{
    uint8_t ready = (uint8_t)(ble_midi_ready() && !mb.closing);
    uint8_t connected = (uint8_t)ble_connected();
    if ((mb.ready && !ready) || (mb.connected && !connected)) mb.lost = 1;
    if (!connected) {
        mb.closing = 0;
        if (mb.forgetting) { memset(&bhs_key, 0, sizeof bhs_key); mb.forgetting = 0; }
    }
    mb.connected = connected;
    mb.ready = ready;
    if (!ready) mb.tr = mb.tw;
}
BLE_API void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16])
{
    if (!mb.on || mb.closing) return;
    mb_saved.bond[0] = 0xB5u; mb_saved.bond[1] = 1;
    ble_cpy(mb_saved.bond + 2, rand, 8);
    ble_wr16(mb_saved.bond + 10, ediv);
    ble_cpy(mb_saved.bond + 12, ltk, 16); mb_dirty();
}
BLE_API void ble_app_peer_id(const uint8_t irk[16], const uint8_t addr[6], uint8_t rnd)
{ (void)irk; (void)addr; (void)rnd; }
BLE_API void ble_app_central_keys(const struct ble_keys *k)
{
    if (!mb.on || mb.closing || !mb.attempt) return;
    mb.keys = *k; RING_PUBLISH(); mb.keys_new = 1;
}

static int mel_ble_ready(void) { return mb.ready && mb.on; }
static int mel_ble_room(void) { return mb.tw - mb.tr < MBQ; }
static int mel_ble_emit(uint32_t pkt)
{
    if (!mb.ready || !mb.on) return 0;
    /* All callers are serialized against BLE consumption. Refuse admission
     * before other transports accept the packet, so retries cannot duplicate it. */
    if (mb.tw - mb.tr >= MBQ) return 0;
    mb.tx[mb.tw % MBQ] = pkt; mb.ms[mb.tw % MBQ] = fm1_ms;
    RING_PUBLISH(); mb.tw++;
    return 1;
}

static void mel_ble_drain(void) /* TIMER5, the shared input queue's sole producer context */
{
    if (mb.lost || mb.overflow) {
        mb_lock(); mb.rr = mb.rw; mb.lost = mb.overflow = 0; mb_unlock();
        midi_ble_lost = 1;
    }
    for (uint32_t n = 0; mb.rr != mb.rw && n < MBQ; n++) {
        RING_PUBLISH(); uint32_t at = mb.rr % MBQ;
        if (!midi_enqueue(mb.rx[at], 3u)) { mb.rr = mb.rw; midi_ble_lost = 1; break; }
        midi_in_time[(mi_w - 1u) % MQ] = mb.time[at];
        mb.rr++;
    }
}

static void mel_ble_boot(void) /* CPU1 held, IRQs off; does not start the radio */
{
    struct ble_vm_info info;
    struct ble_rf_trims vm;
    int save = 0;
    if (mb_saved.magic != MEL_BLE_MAGIC) memset(&mb_saved, 0, sizeof mb_saved);
    int complete = ble_vm_scan(mel_ble_flash_read, 0, &info, &vm, 0, 0);
    mb.cal = (uint8_t)(ble_rf_choose(complete, &vm, info.area, mb_saved.rf, &mb.trims, &save) != BLE_RF_NONE);
    if (save) mb_dirty();
    if (!mb.cal) {
        /* No stored calibration: the radio measures its own at start (hal/fm1_rf_cal.h, JieLi's SDK calibration
         * in C). 106 / 107 / 108 are the SDK board's constants; 187 is measured, about 160 ms. */
        static const struct ble_rf_trims sdk = {{0x0B, 0x0B}, {1, 7, 4, 7, 11, 1, 7}, {0}, {0}};
        mb.trims = sdk;
        fm1_rf_cal_on = 1;
        mb.cal = 1;                           /* (a saved calibration imported later still replaces it) */
    }
    ble_store_load(&mb.store, mb_saved.dev);
    ble_scan_clear(&mb.found);
    ble_hw_wl82_attach(); fm1_ble_crumb_boot();
}

static int mel_ble_set(int on)
{
    if (on && (!mb.cal || !RES_OK())) return 0;     /* (RES_OK: the radio program, res.c) */
    if (on && !mb.started) {
        if (!mb_saved.valid) {
            for (uint32_t i = 0; i < 6u; i++) mb_saved.addr[i] = (uint8_t)fm1_ble_rng32();
            mb_saved.addr[5] |= 0xC0u; mb_saved.valid = 1; mb_dirty();
        }
        /* Caller rejects live music; startup holds IRQs, keeps CPU1 in its RAM idle loop. */
        mel_ble_radio_start(&mb.trims);
        ble_init(mb_saved.addr, 1);
        if (mb_saved.bond[0] == 0xB5u && mb_saved.bond[1] == 1)
            ble_host_set_key(mb_saved.bond + 2, ble_rd16(mb_saved.bond + 10), mb_saved.bond + 12);
        mb.started = 1;
    }
    mb_lock();
    mb.on = (uint8_t)!!on;
    if (!on) { mb.scanning = mb.attempt = 0; mb.lost = 1; }
    if (mb.started) ble_enable(on);
    mb_unlock(); mb.generation++;
    return 1;
}

static void mel_ble_scan(int on)
{
    mb_lock();
    mb.scanning = (uint8_t)(on && mb.on && !mb.attempt && !ble_connected());
    if (mb.started) ble_ll_scan(mb.scanning);
    mb_unlock();
}
static void mel_ble_disconnect(void)
{
    mb_lock();
    mb.closing = (uint8_t)ble_connected(); mb.ready = 0; mb.tr = mb.tw;
    if (mb.started) { ble_enable(0); ble_enable(mb.on); }
    mb.attempt = 0; mb.lost = 1;
    mb.failure[0] = 0; mb.generation++;
    mb_unlock();
}
static void mel_ble_forget(void)
{
    mel_ble_disconnect();
    mb_lock();
    mb.forgetting = mb.closing;
    memset(mb_saved.bond, 0, sizeof mb_saved.bond);
    ble_store_forget(&mb.store); ble_store_save(&mb.store, mb_saved.dev);
    if (mb.started) { memset(&bhs_key, 0, sizeof bhs_key); ble_smp_reset(); }
    mb_dirty(); mb_unlock();
}

static int mb_same_peer(const uint8_t addr[6], uint8_t rnd)
{
    const struct ble_dev *d = &mb.store.dev;
    return ble_store_has_last(&mb.store) &&
        ((((d->info & BLE_DEV_RANDOM) != 0) == rnd && !memcmp(addr, d->addr, 6)) ||
         (rnd && (d->info & BLE_DEV_IRK) && ble_rpa_resolve(d->irk, addr)));
}
static void mb_remember(void)
{
    if (!mb_same_peer(mb.target.addr, mb.target.addr_rand))
        ble_store_set_last(&mb.store, mb.target.addr, mb.target.addr_rand, mb.target.name, mb.target.kind);
}
static int mb_connect_target(void)
{
    struct ble_peer peer;
    memset(&peer, 0, sizeof peer);
    memcpy(peer.addr, mb.target.addr, 6); peer.addr_rand = mb.target.addr_rand;
    struct ble_dev *d = &mb.store.dev;
    if (mb_same_peer(peer.addr, peer.addr_rand)) {
        peer.bonded = !!(d->info & BLE_DEV_BONDED);
        memcpy(peer.ltk, d->ltk, 16); memcpy(peer.rand, d->rand, 8); peer.ediv = ble_rd16(d->ediv);
        peer.sec = d->sec;
    }
    mb.failure[0] = 0; mb.keys_new = 0; memset(&mb.keys, 0, sizeof mb.keys);
    mb_lock(); int ok = ble_central_connect(&peer); mb_unlock();
    if (ok) { mb.attempt = 1; mb.was_ready = mb.was_pairing = 0; mb.deadline = fm1_ms; mb.generation++; }
    return ok;
}
static int mel_ble_connect(uint32_t row)
{
    if (!mb.on || row >= mb.count || ble_connected() || mb.attempt) return 0;
    mb.target = mb.found.e[mb.idx[row]];
    return mb_connect_target();
}
static int mel_ble_last(void)
{
    if (!mb.on || !ble_store_has_last(&mb.store) || ble_connected() || mb.attempt) return 0;
    memset(&mb.target, 0, sizeof mb.target);
    memcpy(mb.target.addr, mb.store.dev.addr, 6);
    mb.target.addr_rand = !!(mb.store.dev.info & BLE_DEV_RANDOM);
    memcpy(mb.target.name, mb.store.dev.name, BLE_NAME_MAX);
    mb.target.kind = BLE_KIND_MIDI;
    /* Resolve a remembered private address from the live scan table. No automatic pairing loops. */
    if (mb.store.dev.info & BLE_DEV_IRK)
        for (uint32_t i = 0; i < mb.count; i++) {
            struct ble_found *f = &mb.found.e[mb.idx[i]];
            if (f->addr_rand && ble_rpa_resolve(mb.store.dev.irk, f->addr)) { mb.target = *f; break; }
        }
    return mb_connect_target();
}

static void mel_ble_poll(void)
{
    if (!mb.started) return;
    uint8_t pdu[39]; uint16_t rssi; uint32_t n;
    for (uint32_t i = 0; i < BLE_SCAN_RING && (n = ble_ll_scan_take(pdu, &rssi)); i++)
        ble_scan_add(&mb.found, pdu, n, rssi, fm1_ms);
    if (fm1_ms - mb.aged >= 1000u) { mb.aged = fm1_ms; ble_scan_age(&mb.found, fm1_ms); }
    mb.count = (uint8_t)ble_scan_list(&mb.found, mb.idx);
    if (mb.attempt) {
        uint8_t state = ble_central_state(), pairing = (uint8_t)ble_central_pairing();
        if (pairing != mb.was_pairing) { mb.was_pairing = pairing; mb.deadline = fm1_ms; }
        if (mb.keys_new) {
            mb_lock(); struct ble_keys k = mb.keys; mb.keys_new = 0; mb_unlock();
            mb_remember();
            if (k.has & BLE_KEYS_LTK) ble_store_set_bond(&mb.store, k.ltk, k.rand, k.ediv, k.has & BLE_KEYS_AUTH);
            if (k.has & BLE_KEYS_ID) ble_store_set_id(&mb.store, k.irk, k.id, k.id_rand);
            ble_store_save(&mb.store, mb_saved.dev); mb_dirty();
        }
        if (state == BLE_CS_READY && !mb.was_ready) {
            mb.was_ready = 1;
            mb_remember();
            ble_store_save(&mb.store, mb_saved.dev); mb_dirty(); mb.generation++;
        } else if (state == BLE_CS_IDLE) {
            uint32_t why = ble_central_fail();
            if (why == BLE_CF_KEY) { ble_store_drop_bond(&mb.store); ble_store_save(&mb.store, mb_saved.dev); mb_dirty(); }
            str_cpy(mb.failure, why == BLE_CF_KEY ? "FORGET ON PEER" : why == BLE_CF_AUTH || why == BLE_CF_PAIRING ?
                "PAIRING FAILED" : why == BLE_CF_NO_MIDI ? "NO MIDI SERVICE" : "DISCONNECTED", sizeof mb.failure);
            mb.attempt = 0; mb.generation++;
        } else if (!mb.was_ready && fm1_ms - mb.deadline > (ble_central_pairing() ? 45000u : 10000u)) {
            mel_ble_disconnect(); str_cpy(mb.failure, "CONNECTION TIMEOUT", sizeof mb.failure);
        }
    }
}
static uint32_t mel_ble_code(void)
{ return mb.on && mb.attempt ? ble_central_passkey() : BLE_NO_PASSKEY; }
static const char *mel_ble_status(void)
{
    if (!mb.cal) return "NO CAL";
    if (!mb.on) return "BLE OFF";
    if (mel_ble_code() != BLE_NO_PASSKEY) return "PAIRING CODE";
    if (mb.ready) return "BLE CONNECTED";
    if (mb.failure[0]) return mb.failure;
    if (mb.attempt) return ble_central_pairing() ? "PAIRING" : "FINDING MIDI";
    if (ble_connected()) return "WAITING FOR MIDI";
    return mb.scanning ? "SCANNING" : "BLE AVAILABLE";
}
static uint32_t mel_ble_signature(void)
{ return mb.generation + mb.found.gen * 13u + (uint32_t)mb.ready * 997u + mel_ble_code() * 17u + (uint32_t)ble_central_state() * 65537u; }

/* USB maintenance only: restore a checked trim copy, never a raw flash sector.
 * Existing valid calibration cannot be replaced by a different unit's copy. */
static uint32_t mel_ble_import_cal(const uint8_t copy[BLE_RF_COPY_SIZE])
{
    if (!ble_rf_copy_ok(copy)) return 1;
    if (mb.on || mb.closing) return 4;
    if (mb.cal && !fm1_rf_cal_on && memcmp(mb_saved.rf, copy, BLE_RF_COPY_SIZE)) return 2;
    if (mb.cal && !fm1_rf_cal_on) return 0;
    memcpy(mb_saved.rf, copy, BLE_RF_COPY_SIZE);
    ble_rf_copy_get(copy, &mb.trims);
    fm1_rf_cal_on = 0;                        /* (the saved one instead of the measurement) */
    mb.cal = 1; mb_dirty(); mb.generation++;
    return 0;
}
