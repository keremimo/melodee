/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Host test of the WL82 driver with two links (BLE_LINKS 2, docs/BLE-MULTILINK.md) against the fake engine of
 * tests/ble_fake/fm1_ble.h, which now records every column write and stop per link and names the links with a pending
 * interrupt. Link 0 is the peripheral role's (advertising, then a connection), link 1 the central role's (scan,
 * initiate). What it checks is that the two never touch each other:
 *   instance table  entry 0 and entry 1 each point at their own control block, the blocks and buffers do not overlap
 *   columns         a write for one link lands in that link's columns only (the fake keeps them per link)
 *   interrupts      the event and RX vectors serve the links whose bits are pending, and only those
 *   stops           a stop beside a live link waits at most HW_STOP_SHARED_US, alone as long as before
 *   idle policy     BLE_IDLE_EXCL: a scan pauses advertising, a connected link goes on; beside a connection the radio
 *                   advertises or scans; the previous link is selected again after every call */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#ifndef BLE_CENTRAL
#define BLE_CENTRAL 1
#endif
#ifndef BLE_LINKS
#define BLE_LINKS 2
#endif
#if BLE_LINKS != 2
#error "ble_multilink_driver_test needs BLE_LINKS 2"
#endif
#include "ble/ble_stack.c"
#include "ble/ble_diag.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-118s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static int ble_app_midi_peek(uint32_t *pkt, uint32_t *t) { (void)pkt, (void)t; return 0; }
static void ble_app_midi_pop(void) {}
static void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last) { (void)pkt, (void)ts, (void)last; }
static void ble_app_state(void) {}
#if BLE_SMP_LEGACY
static void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16]) { (void)rand, (void)ediv, (void)ltk; }
static void ble_app_peer_id(const uint8_t irk[16], const uint8_t a[6], uint8_t r) { (void)irk, (void)a, (void)r; }
#endif
#if BLE_CENTRAL
static void ble_app_central_keys(const struct ble_keys *k) { (void)k; }
#endif

static const uint8_t OWN[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};
#define IV 24u
#define P0 BLE_LINK_PERIPH
#define P1 BLE_LINK_CENTRAL

/* the computer's CONNECT_IND, as the FM-1 stored it (the engine's advertising rule: RXTOG moved past the buffer) */
static void cind(void)
{
    uint32_t b = bb.lk[P0].cb.rxtog & 1u;
    uint8_t *p = bb.lk[P0].rx[b].buf + HW_SWHDR;
    static const uint8_t init_a[6] = {1, 2, 3, 4, 5, 0x46};
    memcpy(p, init_a, 6);
    memcpy(p + 6, drv_b[P0].adv.adv + 2, 6);
    p[12] = 0xDB, p[13] = 0xAC, p[14] = 0x9A, p[15] = 0xAF;
    p[16] = 0xA4, p[17] = 0xEF, p[18] = 0x2D;
    p[19] = 3;
    p[20] = 22, p[21] = 0;
    p[22] = IV, p[23] = 0;
    p[24] = 0, p[25] = 0;
    p[26] = 72, p[27] = 0;
    p[28] = 0xFF, p[29] = 0xFF, p[30] = 0xFF, p[31] = 0xFF, p[32] = 0x1F;
    p[33] = (uint8_t)(13u | 1u << 5);
    bb.lk[P0].cb.rxahdr[b] = 0x2285u;
    bb.lk[P0].cb.rxdhdr[b] = 0x2200u;
    bb.lk[P0].cb.rxstat[b] = 0x9401u;
    bb.lk[P0].cb.rxtog ^= 1u;
    bb.lk[P0].cb.txtog = 1u;
    fk.rx_pending = 1u << P0;
    ble_wl82_rx_irq();
    fk.rx_pending = 0;
}

static uint32_t off_of(const void *p) { return (uint32_t)((uintptr_t)p - (uintptr_t)&bb); }

static void t_links(void)
{
    struct ble_rf_trims tr;
    uint32_t lo0, hi0, lo1, hi1, ev0;
    memset(&fk, 0, sizeof fk);
    memset(&tr, 0, sizeof tr);
    fk.tick_per_read = FM1_TICKS_PER_US;
    fk.pend_set = 1;                                    /* the links' column 3 are their own (lcol3) */
    ble_hw_wl82_start(&tr);
    ble_init(OWN, 1);
    ble_enable(1);

    check("the block: link 0's control block at 0x40 as stock, link 1's after link 0's buffers",
          off_of(&bb.lk[P0].cb) == 0x40u && off_of(&bb.lk[P1].cb) >= off_of(&bb.lk[P0].tx[1]) + sizeof bb.lk[P0].tx[1] &&
              sizeof bb < 0xFFFFu);
    check("advertising on link 0 (state 2), instance 0 set, instance 1 empty, link 1 untouched",
          drv_b[P0].state == HW_ADV && fk.lcol[P0][2] == 0x2000u && bb.inst[P0] == (0x8000u | off_of(&bb.lk[P0].cb)) &&
              bb.inst[P1] == 0 && drv_b[P1].state == HW_OFF && !fk.lwr[P1] && ble_ll_link() == P0);

    /* the central role's scan: BLE_IDLE_EXCL pauses the advertising, then link 1 runs alone */
    fk.lwr[P0] = fk.lwr[P1] = 0;
    ble_ll_scan(1);
    check("a scan: advertising paused (link 0 stopped, column 14 = 0), link 1 scans (state 1), instance 1 set",
          drv_b[P0].state == HW_OFF && fk.lcol[P0][14] == 0 && drv_b[P1].state == HW_SCAN && fk.lcol[P1][2] == 0x1000u &&
              fk.lcol[P1][14] == 0x8000u && bb.inst[P1] == (0x8000u | off_of(&bb.lk[P1].cb)) && ble_ll_scanning());
    check("... every column write after the pause is link 1's; link 0 got only its stop",
          fk.lwr[P1] > 5u && fk.lwr[P0] <= 1u && fk.lstops[P0] >= 1u);
    check("... the scan's buffers are link 1's own (RXPTR / TXPTR inside link 1's part of the block)",
          bb.lk[P1].cb.rxptr[0] == off_of(bb.lk[P1].rx[0].buf + HW_SWHDR) &&
              bb.lk[P1].cb.rxptr[1] == off_of(bb.lk[P1].rx[1].buf + HW_SWHDR) &&
              bb.lk[P1].cb.txptr[0] == off_of(bb.lk[P1].tx[0].buf + HW_SWHDR));
    fkb.max_seen = 0;
    ble_ll_scan(0);
    check("scan off alone: the stop waits as long as ever (HW_STOP_WAIT_US), advertising back on link 0",
          fkb.max_seen == HW_STOP_WAIT_US && drv_b[P0].state == HW_ADV && drv_b[P1].state == HW_OFF && !ble_ll_scanning());

    /* a computer connects on link 0 */
    cind();
    check("the computer's CONNECT_IND taken on link 0: state 7, our LL link 0 connected, link 1 idle",
          drv_b[P0].state == HW_CONN && fk.lcol[P0][2] == 0x7000u && ble_connected() && LL_BANK(P0).state == LL_CONN &&
              LL_BANK(P1).state == LL_OFF && drv_b[P1].state == HW_OFF);

    /* a scan beside the connection */
    ble_ll_scan(1);
    check("a scan beside the connection: link 0 untouched (still state 7), link 1 scans",
          drv_b[P0].state == HW_CONN && fk.lcol[P0][2] == 0x7000u && drv_b[P1].state == HW_SCAN &&
              fk.lcol[P1][2] == 0x1000u && ble_connected() && ble_ll_scanning());
    fkb.max_seen = 0;
    ble_ll_scan(0);
    check("scan off beside a live link: the stop waits at most HW_STOP_SHARED_US, link 0 still connected",
          fkb.max_seen == HW_STOP_SHARED_US && drv_b[P0].state == HW_CONN && drv_b[P1].state == HW_OFF &&
              ble_connected());

    /* the interrupt vectors serve the pending links only */
    ble_ll_scan(1);
    fk.event_pending = 1u << P1;
    fk.rx_pending = 0;
    fk.lcol3[P0] = 5;                                   /* link 0 has an event open, but nothing is pending for it */
    ev0 = ble_hw_stat.events;
    fk.log_n = 0;
    ble_wl82_event_irq();
    check("event IRQ with link 1 pending: link 1 serves (its scan channel written), link 0's event not served",
          ble_hw_stat.events == ev0 && fk.lcol[P1][6] != 0 && ble_ll_link() == P0);
    fk.event_pending = 1u << P0;
    ble_wl82_event_irq();
    check("event IRQ with link 0 pending: link 0's event counted (column 3 - 1 = 4), the scan not turned",
          ble_hw_stat.events == ev0 + 1u && ble_ll_link() == P0);
    fk.event_pending = (1u << P0) | (1u << P1);
    fk.lcol3[P0] = 6;
    ev0 = ble_hw_stat.events;
    ble_wl82_event_irq();
    check("event IRQ with both pending: both served in link order, one event for link 0", ble_hw_stat.events == ev0 + 1u);
    fk.event_pending = 0;

    lo0 = off_of(&bb.lk[P0]), hi0 = lo0 + sizeof bb.lk[P0];
    lo1 = off_of(&bb.lk[P1]), hi1 = lo1 + sizeof bb.lk[P1];
    check("the two links' memory does not overlap", hi0 <= lo1 && hi1 <= sizeof bb);

    /* initiating beside the connection */
    {
        struct ble_peer pr;
        memset(&pr, 0, sizeof pr);
        memcpy(pr.addr, (const uint8_t[]){0x37, 0x37, 0x4B, 0x65, 0x79, 0xC4}, 6);
        pr.addr_rand = 1;
        ble_ll_scan(0);
        check("initiating beside a live link: link 1 in state 3, link 0 untouched",
              ble_central_connect(&pr) && drv_b[P1].state == HW_INIT && fk.lcol[P1][2] == 0x3000u &&
                  drv_b[P0].state == HW_CONN && fk.lcol[P0][2] == 0x7000u && ble_ll_initiating() && ble_connected());
        ble_central_cancel();
        check("cancelled: link 1 stopped, link 0 still connected, selection back on link 0",
              drv_b[P1].state == HW_OFF && drv_b[P0].state == HW_CONN && !ble_ll_initiating() && ble_connected() &&
                  ble_ll_link() == P0 && hw_n == P0);
    }

    /* the connection ends: link 0 stops, advertising again; link 1 idle */
    ble_ll_disconnect(BLE_ERR_REMOTE_USER);
    check("we drop the computer's link: TERMINATE queued on link 0 only", LL_BANK(P0).state == LL_CONN &&
          LL_BANK(P0).lproc == P_TERM && LL_BANK(P1).state == LL_OFF);
}

int main(void)
{
    t_links();
    printf(fails ? "BLE multilink driver: FAILED\n" : "BLE multilink driver: all passed\n");
    return fails != 0;
}
