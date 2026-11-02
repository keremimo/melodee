/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Jacek Mariański */
/* Bluetooth MIDI's stack as a unit of its own (MELODEE_BLE, tools/build.py: ble.o beside melodee.o): Optimist's
 * open BLE stack (ble/, w0ts/optimist fcdc4881, "GPL-3.0-only OR MPL-2.0", unchanged) with felucca-bsp's radio
 * headers (hal/fm1_ble.h, fm1_ble_rf.h: the start-up replay with the live calibration of hal/fm1_rf_cal.h).
 *
 * Why a unit of its own: its RAM. The link layer, host, ATT, the scanner's table and the TX ring (about 6 KB) are
 * this unit's .bss, and app.ld puts that in .ble_bss at the top of the audio working arena, not in RAM. Nothing
 * touches it until ble_midi.c claims those bytes from the arena (resources.c resource_claim, RES_BLE) the first
 * time BLUETOOTH is ON, and zeroes them: with Bluetooth OFF the FM-1 keeps all of its working memory. What the
 * radio's engine reads by DMA (the baseband block bb, the PLL table) stays in low RAM (app.ld: .bss), where the
 * engine reaches.
 *
 * Everything here is reached through the ble_unit_* functions below and the stack's ble_*() API (ble.h), whose
 * ble_app_* callbacks ble_midi.c implements. */
#include <stdint.h>
#define BLE_API                                  /* (extern: melodee.o calls in, and back for ble_app_*) */
#define BLE_DEVICE_NAME "Melodee"
#ifndef BLE_DIAG
#define BLE_DIAG 0                               /* (no console: no counters) */
#endif
#ifndef BLE_CENTRAL
#define BLE_CENTRAL 3                            /* connecting out to a BLE-MIDI keyboard / dongle, no pairing */
#endif
#ifndef BLE_SCAN_N
#define BLE_SCAN_N 4u                            /* (KEYBOARD connects to the first BLE-MIDI device: a short list) */
#endif
#ifndef BLE_LL_TX_RING
#define BLE_LL_TX_RING 512u                      /* (MIDI notifications are short: half Optimist's ring) */
#endif
/* ble/ble_stack.c's list (that file makes every function static, for Optimist's own unity build) */
#include "ble/ble_prim.c"
#include "ble/ble_midi.c"
#include "ble/ble_ll.c"
#include "ble/ble_smp.c"
#include "ble/ble_host.c"
#include "ble/ble_att.c"
#include "ble/ble_store.c"
#if BLE_CENTRAL
#include "ble/ble_scan.c"
#include "ble/ble_gattc.c"
#include "ble/ble_central.c"
#endif
#define BLE_HW_WL82 1
#include "ble/ble_hw_wl82.c"                     /* the AC791N / WL82 baseband (hal/ble/fm1_ble.h) */
#include "ble/ble_vm.c"                          /* the factory VM's trims, when the unit still has them */

/* ---- for ble_midi.c (melodee.o) */
void ble_unit_attach(void) { ble_hw_wl82_attach(); }   /* boot, IRQs off: the two vectors, masked */
void ble_unit_irqs_hold(int hold) { fm1_ble_irqs_hold(hold); }

static int ble_unit_vm_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    int (*rd)(uint32_t, uint8_t *, uint32_t) = (int (*)(uint32_t, uint8_t *, uint32_t))ctx;
    return rd(off, dst, n) ? -1 : 0;
}

/* the radio, the baseband and the stack (the BLE IRQs held). The trims: the factory VM's (rd: SPI reads), else
 * measured now (fm1_rf_cal.h) with the board's crystal and PA values, as fm1-base and fble2 do -> 1 from the VM,
 * 0 measured */
int ble_unit_start(const uint8_t addr[6], int (*rd)(uint32_t off, uint8_t *dst, uint32_t n))
{
    static const struct ble_rf_trims board = {{0x0B, 0x0B}, {1, 7, 4, 7, 11, 1, 7}, {0}, {0}};
    struct ble_vm_info info;
    struct ble_rf_trims vm, use;
    uint8_t copy[BLE_RF_COPY_SIZE];
    int complete, save, src;
    ble_zero((uint8_t *)&info, sizeof info);
    ble_zero((uint8_t *)&vm, sizeof vm);
    ble_zero(copy, sizeof copy);
    complete = ble_vm_scan(ble_unit_vm_rd, (void *)rd, &info, &vm, 0, 0);
    src = ble_rf_choose(complete, &vm, info.area, copy, &use, &save);
    if (src == BLE_RF_NONE) {
        use = board;
        fm1_rf_cal_on = 1;
    } else {
        fm1_rf_cal_on = 0;
    }
    ble_init(addr, 1);
    ble_hw_wl82_start(&use);
    return src != BLE_RF_NONE;
}

/* ---- the modes (ble_midi.c, the main loop): OFF; COMPUTER, advertising as "Melodee XXXX" for a computer or phone
 * to connect; KEYBOARD, scanning and connecting out to the first BLE-MIDI peripheral heard (a keyboard, a dongle),
 * the one connected before first while it is around, again after it goes. One link at a time (the stack's). */
enum { BU_MODE_OFF, BU_MODE_COMPUTER, BU_MODE_KEYBOARD };
enum { BU_OFF, BU_WAITING, BU_LINKED, BU_READY };   /* ble_unit_state: nothing / advertising or scanning or
                                                     * connecting / a link, MIDI not set up yet / MIDI both ways */
#define BU_RETRY_MS 1500u                        /* KEYBOARD: scanning again this long after a link ended or failed */
static struct {
    uint8_t mode, scanning, was_ready, last_ok, last[6];
    char name[BLE_NAME_MAX + 1u];               /* the device connected to (KEYBOARD) */
    uint32_t age_t, quiet_t;
    struct ble_scan_tab tab;
    uint8_t idx[BLE_SCAN_N];
} bu;

static void bu_scan(int on)                      /* (the BLE IRQs held) */
{
    bu.scanning = (uint8_t)(on != 0);
    ble_ll_scan(on);
}

void ble_unit_mode(int mode)
{
    fm1_ble_irqs_hold(1);
    ble_central_cancel();                        /* (a link out goes with any change; a computer's with OFF only) */
    bu.mode = (uint8_t)mode;
    bu_scan(mode == BU_MODE_KEYBOARD);
    ble_enable(mode != BU_MODE_OFF);             /* (scanning instead of advertising while the scan is wanted) */
    fm1_ble_irqs_hold(0);
    ble_scan_clear(&bu.tab);
    bu.quiet_t = 0;
}

static void bu_connect(const struct ble_found *f)
{
    struct ble_peer p;
    ble_zero((uint8_t *)&p, sizeof p);
    ble_cpy(p.addr, f->addr, 6);
    p.addr_rand = f->addr_rand;
    ble_cpy(bu.last, f->addr, 6);
    ble_cpy((uint8_t *)bu.name, (const uint8_t *)f->name, sizeof bu.name);
    fm1_ble_irqs_hold(1);
    bu_scan(0);
    if (!ble_central_connect(&p))
        bu_scan(1);
    fm1_ble_irqs_hold(0);
}

/* main loop, while the radio runs */
void ble_unit_poll(uint32_t now)
{
    uint8_t pdu[2 + 37], n;
    uint16_t rssi;
    uint32_t i, k, busy;
    while ((n = ble_ll_scan_take(pdu, &rssi)) != 0)
        ble_scan_add(&bu.tab, pdu, n, rssi, now);
    if (now - bu.age_t > 1000u) {
        bu.age_t = now;
        ble_scan_age(&bu.tab, now);
    }
    if (bu.mode != BU_MODE_KEYBOARD)
        return;
    busy = ble_ll_central() || ble_ll_initiating();
    if (ble_central_state() == BLE_CS_READY && !bu.was_ready)
        bu.last_ok = 1;                          /* (the one to prefer while it is around) */
    bu.was_ready = ble_central_state() == BLE_CS_READY;
    if (busy) {
        bu.quiet_t = now;
        return;
    }
    if (!bu.scanning) {                          /* a link ended, or connecting failed: scan again after a pause */
        if (now - bu.quiet_t < BU_RETRY_MS)
            return;
        fm1_ble_irqs_hold(1);
        bu_scan(1);
        fm1_ble_irqs_hold(0);
        return;
    }
    n = (uint8_t)ble_scan_list(&bu.tab, bu.idx);
    if (!n || now - bu.quiet_t < BU_RETRY_MS)
        return;
    k = 0;                                       /* the first one listed, or the last connected when it is there */
    for (i = 0; i < n && bu.last_ok; i++)
        if (ble_eq(bu.tab.e[bu.idx[i]].addr, bu.last, 6))
            k = i;
    bu.quiet_t = now;
    bu_connect(&bu.tab.e[bu.idx[k]]);
}

int ble_unit_state(void)
{
    if (bu.mode == BU_MODE_OFF)
        return BU_OFF;
    if (bu.mode == BU_MODE_KEYBOARD) {
        if (!ble_ll_central())
            return BU_WAITING;
        return ble_central_state() == BLE_CS_READY ? BU_READY : BU_LINKED;
    }
    if (!ble_connected())
        return BU_WAITING;
    return ble_midi_ready() ? BU_READY : BU_LINKED;
}

const char *ble_unit_peer(void) { return bu.mode == BU_MODE_KEYBOARD && ble_ll_central() ? bu.name : ""; }
