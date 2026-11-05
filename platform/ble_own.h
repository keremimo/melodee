/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee's Bluetooth on the stack in platform/ble: part of device/bt_own.c, after bt_own_central.c
 * (tools/base_overlay.py). The rules are Melodee's (firmware/src/midi_ble.c), on the base's glue. One radio link, the
 * computer's or a device's:
 *   - the calibration: the stock firmware's stored trims when they are still there, kept in OBJ_BLE so they outlive
 *     that sector, else measured at every start (fm1_rf_cal.h);
 *   - the last device is looked for, not called blind: a round of ML_RELINK_SCAN_MS every ML_RELINK_MS, and a link only
 *     once it is heard, behind a private address too (its IRK). Between the rounds a computer can find Melodee, and with
 *     GLOBAL > BLE open there are no rounds;
 *   - never a new pairing loop: a lost link is looked for again, a refusal or a code nobody entered is not;
 *   - an attempt is bounded, longer while a code waits for its user. */
#define ML_RELINK_MS 30000u
#define ML_RELINK_SCAN_MS 10000u
#define ML_ATTEMPT_MS 10000u
#define ML_PAIRING_MS 45000u
#define ML_RETRY_SAVE_MS 30000u

static struct {
    struct ble_dev_store store;
    struct ble_found target;
    struct ble_keys keys;
    volatile uint8_t keys_new;
    uint8_t loaded, attempt, was_ready, was_pairing, again;
    uint8_t relink, relink_scan, page, hold;
    uint8_t dirty, save_failed;
    uint32_t deadline, relink_t, save_at;
    const char *message;
} ml_own;

static uint8_t ml_relink_scanning(void) { return ml_own.relink_scan; }

/* ---- the calibration */
static int ml_own_flash(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    (void)ctx;                                    /* (read only: a stock VM candidate is never written) */
    return off > 0x100000u || n > 0x100000u - off ? -1 : st_read(off, dst, n);
}
static void ml_own_trims(struct ble_rf_trims *use)
{
    struct ble_vm_info info;
    struct ble_rf_trims vm;
    int save = 0;
    int complete = ble_vm_scan(ml_own_flash, 0, &info, &vm, 0, 0);
    if (ble_rf_choose(complete, &vm, info.area, own.ml_rf, use, &save) != BLE_RF_NONE) {
        fm1_rf_cal_on = 0;
        if (save)
            own_dirty = 1;
        return;
    }
    *use = OWN_TRIMS;                             /* 106, 107, 108: the SDK board's; 187 measured */
    fm1_rf_cal_on = 1;
}

/* ---- the last device */
static void ml_own_load(void)
{
    uint8_t raw[BLE_DEV_STORE_SIZE];
    if (ml_own.loaded)
        return;
    ml_own.loaded = 1;
    if (plat_store_load(OBJ_BT_LAST, raw, sizeof raw) == (int)sizeof raw)
        (void)ble_store_load(&ml_own.store, raw);
    else
        ble_store_reset(&ml_own.store);
    ml_own.relink = (uint8_t)ble_store_has_last(&ml_own.store);
    ml_own.relink_t = shim_ms() - ML_RELINK_MS;
}
static int ml_same_peer(const uint8_t addr[6], uint8_t rnd)
{
    const struct ble_dev *d = &ml_own.store.dev;
    return ble_store_has_last(&ml_own.store) &&
           ((((d->info & BLE_DEV_RANDOM) != 0) == rnd && ble_eq(addr, d->addr, 6)) ||
            (rnd && (d->info & BLE_DEV_IRK) && ble_rpa_resolve(d->irk, addr)));
}
static void ml_remember(void)
{
    if (!ml_same_peer(ml_own.target.addr, ml_own.target.addr_rand))
        ble_store_set_last(&ml_own.store, ml_own.target.addr, ml_own.target.addr_rand, ml_own.target.name, ml_own.target.kind);
}
static int ml_connect_target(void)
{
    struct ble_peer peer;
    const struct ble_dev *d = &ml_own.store.dev;
    int ok;
    ble_zero((uint8_t *)&peer, sizeof peer);
    ble_cpy(peer.addr, ml_own.target.addr, 6);
    peer.addr_rand = ml_own.target.addr_rand;
    if (ml_same_peer(peer.addr, peer.addr_rand)) {
        peer.bonded = (uint8_t)!!(d->info & BLE_DEV_BONDED);
        ble_cpy(peer.ltk, d->ltk, 16);
        ble_cpy(peer.rand, d->rand, 8);
        peer.ediv = ble_rd16(d->ediv);
        peer.sec = d->sec;                        /* (BLE_DEV_SEC_* are BLE_PEER_*) */
    }
    ml_own.keys_new = 0;
    own_held(1);
    ok = ble_central_connect(&peer);
    own_held(0);
    if (ok) {
        ml_own.attempt = 1;
        ml_own.was_ready = ml_own.was_pairing = 0;
        ml_own.deadline = shim_ms();
        shim_log("device: connecting");
    }
    return ok;
}
static void ml_scan(int on)
{
    own_held(1);
    ble_ll_scan(on);
    own_held(0);
}
/* the BLE interrupts' context: copy them (ble.h) */
static void ml_own_keys(const struct ble_keys *k)
{
    if (!ml_own.attempt)
        return;
    ml_own.keys = *k;
    RING_PUBLISH();
    ml_own.keys_new = 1;
}
static void ml_relink_poll(uint32_t now)
{
    uint8_t idx[BLE_SCAN_N];
    uint32_t i, n, t;
    if (!ml_own.relink || ml_own.attempt || ml_own.page || ml_own.hold || ble_off || ble_connected()) {
        if (ml_own.relink_scan && !ml_own.page)
            ml_scan(0);                           /* (the page's scan is the page's) */
        ml_own.relink_scan = 0;
        return;
    }
    t = now - ml_own.relink_t;
    if (t >= ML_RELINK_MS) {
        ml_own.relink_t = now;
        t = 0;
        ml_own.relink_scan = 1;
        ml_scan(1);
    }
    if (!ml_own.relink_scan)
        return;
    n = ble_scan_list(&own_scan, idx);
    for (i = 0; i < n; i++) {
        const struct ble_found *e = &own_scan.e[idx[i]];
        if (ml_same_peer(e->addr, e->addr_rand)) {
            ml_own.relink_scan = 0;
            ml_scan(0);
            ml_own.target = *e;
            ml_own.again = 0;
            (void)ml_connect_target();
            return;
        }
    }
    if (t >= ML_RELINK_SCAN_MS) {
        ml_own.relink_scan = 0;
        ml_scan(0);
    }
}
static void ml_save_poll(uint32_t now)
{
    uint8_t raw[BLE_DEV_STORE_SIZE];
    if (!ml_own.dirty || (ml_own.save_failed && now - ml_own.save_at < ML_RETRY_SAVE_MS) ||
        (plat_midi_count(PORT_BLE, 0) && plat_midi_ago(PORT_BLE, 0) < 2000u))   /* (a write holds the CPU: when MIDI pauses) */
        return;
    ble_store_save(&ml_own.store, raw);
    ml_own.save_at = now;
    ml_own.save_failed = (uint8_t)!!plat_store_save(OBJ_BT_LAST, raw, sizeof raw);
    if (!ml_own.save_failed)
        ml_own.dirty = 0;
}
/* the main loop, from own_central_poll */
static void ml_own_poll(void)
{
    uint32_t now = shim_ms();
    ml_own_load();
    if (ble_host_key_refused())
        ml_own.message = "FORGET ON PEER";        /* a central came with a key we do not know */
    if (ml_own.attempt) {
        uint8_t st = ble_central_state(), pairing = (uint8_t)ble_central_pairing();
        if (pairing != ml_own.was_pairing) {
            ml_own.was_pairing = pairing;
            ml_own.deadline = now;
        }
        if (ml_own.keys_new) {
            struct ble_keys k;
            own_held(1);
            k = ml_own.keys;
            ml_own.keys_new = 0;
            own_held(0);
            ml_remember();
            if (k.has & BLE_KEYS_LTK)
                ble_store_set_bond(&ml_own.store, k.ltk, k.rand, k.ediv, k.has & BLE_KEYS_AUTH);
            if (k.has & BLE_KEYS_ID)
                ble_store_set_id(&ml_own.store, k.irk, k.id, k.id_rand);
            ml_own.dirty = 1;
        }
        if (st == BLE_CS_READY && !ml_own.was_ready) {
            ml_own.was_ready = 1;
            ml_own.again = 0;
            ml_own.relink = 1;                    /* kept linked from now */
            ml_own.relink_t = now;
            ml_remember();
            ml_own.dirty = 1;
        } else if (st == BLE_CS_IDLE) {
            uint8_t why = ble_central_fail();
            ml_own.attempt = 0;
            if (why == BLE_CF_KEY) {
                ble_store_drop_bond(&ml_own.store);
                ml_own.dirty = 1;
            }
            if (why == BLE_CF_NEED_MITM && !ml_own.again) {   /* refused Just Works: one new link, the passkey (ble.h) */
                ml_own.again = 1;
                ml_remember();
                ble_store_drop_bond(&ml_own.store);
                ble_store_set_mitm(&ml_own.store);
                ml_own.dirty = 1;
                if (ml_connect_target()) {
                    ml_save_poll(now);
                    return;
                }
            }
            if (why != BLE_CF_LOST && why != BLE_CF_NONE)
                ml_own.relink = 0;                /* gone: looked for again; refused: not */
            if (why == BLE_CF_KEY)
                ml_own.message = "FORGET ON PEER";
            else if (why == BLE_CF_AUTH || why == BLE_CF_PAIRING || why == BLE_CF_NEED_MITM)
                ml_own.message = "PAIRING FAILED";
            else if (why == BLE_CF_NO_MIDI)
                ml_own.message = "NO MIDI SERVICE";
        } else if (!ml_own.was_ready && now - ml_own.deadline > (pairing ? ML_PAIRING_MS : ML_ATTEMPT_MS)) {
            if (pairing)
                ml_own.relink = 0;                /* a code nobody entered: no second prompt */
            own_held(1);
            ble_central_cancel();
            own_held(0);
            ml_own.attempt = 0;
            shim_log("device: no answer");
            if (!ml_own.relink)
                ml_own.message = "CONNECTION TIMEOUT";
        }
    }
    ml_relink_poll(now);
    ml_save_poll(now);
}

/* ---- plat.h: bt_own_central.c's, with the last device known to them */
void plat_ble_scan(int on)
{
    if (ble_state != BLE_READY)
        return;
    ml_own.page = (uint8_t)!!on;
    ml_own.relink_scan = 0;
    if (on)
        ble_scan_clear(&own_scan);
    ml_scan(on);
}
int plat_ble_connect(int i)
{
    uint8_t idx[BLE_SCAN_N];
    uint32_t n = ble_state == BLE_READY ? ble_scan_list(&own_scan, idx) : 0u;
    if (i < 0 || (uint32_t)i >= n || ml_own.attempt || ble_connected())
        return -1;
    ml_own.target = own_scan.e[idx[i]];
    ml_own.again = 0;
    return ml_connect_target() ? 0 : -1;
}

/* ---- what platform/ble_devices.h and ble_reconnect.h call */
void mlb_ble_relink(int on)
{
    ml_own_load();
    ml_own.relink = (uint8_t)(on && ble_store_has_last(&ml_own.store));
    ml_own.relink_t = shim_ms() - ML_RELINK_MS;
}
void mlb_ble_hold(int on) { ml_own.hold = (uint8_t)!!on; }
const char *mlb_ble_message(void)
{
    const char *m = ml_own.message;
    ml_own.message = 0;
    return m ? m : "";
}
const char *mlb_ble_device_name(void)
{
    return ble_central_state() != BLE_CS_IDLE ? ml_own.target.name : 0;
}
int mlb_ble_forget_device(void)
{
    uint8_t raw[BLE_DEV_STORE_SIZE];
    ml_own_load();
    ble_store_forget(&ml_own.store);
    ml_own.relink = ml_own.relink_scan = ml_own.dirty = 0;
    ble_store_save(&ml_own.store, raw);
    return !plat_store_save(OBJ_BT_LAST, raw, sizeof raw);
}
int32_t ble_midi_pairing_code(void) { return plat_ble_passkey(); }
void ble_midi_drop_host(void)
{
    own_held(1);
    if (own_role == 1u)
        ble_ll_disconnect(BLE_ERR_REMOTE_USER);
    own_held(0);
}
void ble_midi_reconnect(void)
{
    own_held(1);
    if (own_role == 1u) {
        ble_ll_disconnect(BLE_ERR_REMOTE_USER);   /* advertising again when it has gone */
    } else if (own_role == 0u) {
        ble_enable(0);
        ble_enable(1);
    }
    own_held(0);
}
int ble_midi_can_forget(void) { return !ble_connected() && ble_central_state() == BLE_CS_IDLE; }
/* our side of every bond: a new secret next time (ble_host_new_key), so the keys handed out are all gone with it */
int ble_midi_forget_all(void)
{
    if (!ble_midi_can_forget())
        return 0;
    own_held(1);
    ble_zero((uint8_t *)&bhs_key, sizeof bhs_key);
    ble_smp_reset();
    own_held(0);
    own.have_bond = 0;
    ble_zero(own.bond_rand, sizeof own.bond_rand);
    ble_zero(own.bond_ltk, sizeof own.bond_ltk);
    own.bond_ediv = 0;
    own_dirty = 0;
    return !plat_store_save(OBJ_BLE, &own, sizeof own);
}
