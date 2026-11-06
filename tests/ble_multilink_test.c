/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Host test of two links at once (BLE_LINKS 2, docs/BLE-MULTILINK.md): a simulated computer connects to our peripheral
 * link (link 0) while we are the central of a simulated BLE-MIDI keyboard on the central link (link 1), in both orders,
 * behind a fake baseband (ble_hw.h) that tells the links apart. The keyboard is the peripheral of tests/ble_central_model.h
 * (the one tests/ble_central_test.c uses); the computer is a small central here.
 *   order         keyboard first then computer, computer first then keyboard; each leaves, the other stays
 *   MIDI          each link's notes reach the firmware tagged with their link (ble_ll_link() in the hooks), each link's
 *                 outgoing notes reach only its own peer
 *   banks         L2CAP frames in pieces on both links at once, ATT MTUs and CCCDs of their own
 *   radio         advertising goes on beside the master link, stops for the computer; a scan beside the computer link;
 *                 the hardware told per link (stops, kicks), the previous bank given back after every call
 *   endings       either peer terminating; BLUETOOTH off with both up */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef BLE_CENTRAL
#define BLE_CENTRAL 1
#endif
#ifndef BLE_LINKS
#define BLE_LINKS 2
#endif
#if BLE_LINKS != 2
#error "ble_multilink_test needs BLE_LINKS 2"
#endif
#include "ble_central_model.h"

#define MLK BLE_LINK_PERIPH                 /* the computer's link */

/* ------------------------------------------------------------------------------------- the computer */
static const uint8_t MADDR[6] = {0x77, 0x66, 0x55, 0x44, 0x33, 0x02};   /* public */
static struct {
    uint8_t q[64][2 + 255];
    int qn;
    uint16_t evt;
    int alive, max_frag;
    uint8_t l2[1024];
    int l2_have, l2_need;
    int notes;                              /* ATT notifications taken from us */
    uint8_t last_ntf[64];
    int last_ntf_n;
    uint8_t ctrl_last;
    int term_seen;
    uint16_t mtu_rsp;
} M;

static void m_pdu(uint8_t llid, const uint8_t *p, int n)
{
    uint8_t *q = M.q[M.qn++];
    q[0] = llid;
    q[1] = (uint8_t)n;
    memcpy(q + 2, p, n);
}
static void m_ctrl(const uint8_t *p, int n) { m_pdu(3, p, n); }
static void m_l2cap(uint16_t cid, const uint8_t *p, int n)
{
    uint8_t f[600];
    int off = 0;
    f[0] = (uint8_t)n, f[1] = (uint8_t)(n >> 8), f[2] = (uint8_t)cid, f[3] = (uint8_t)(cid >> 8);
    memcpy(f + 4, p, n);
    while (off < n + 4) {
        int k = n + 4 - off < M.max_frag ? n + 4 - off : M.max_frag;
        m_pdu(off ? 1 : 2, f + off, k);
        off += k;
    }
}
static void m_att(const uint8_t *p, int n) { m_l2cap(4, p, n); }

static void m_frame(const uint8_t *f, int n)           /* a whole L2CAP frame from us */
{
    uint16_t cid = (uint16_t)(f[2] | f[3] << 8);
    if (cid == 4 && f[4] == 0x1B) {                    /* Handle Value Notification */
        M.notes++;
        M.last_ntf_n = n - 4 < 64 ? n - 4 : 64;
        memcpy(M.last_ntf, f + 4, M.last_ntf_n);
    } else if (cid == 4 && f[4] == 0x03)
        M.mtu_rsp = (uint16_t)(f[5] | f[6] << 8);
}
static void m_take(const uint8_t *pdu)
{
    uint8_t llid = pdu[0] & 3, n = pdu[1];
    const uint8_t *p = pdu + 2;
    if (llid == 3) {
        M.ctrl_last = p[0];
        M.term_seen |= p[0] == LL_TERMINATE_IND;
        return;
    }
    if (llid == 2) {
        M.l2_have = 0;
        M.l2_need = (p[0] | p[1] << 8) + 4;
    }
    if (!M.l2_need || M.l2_have + n > (int)sizeof M.l2)
        return;
    memcpy(M.l2 + M.l2_have, p, n);
    M.l2_have += n;
    if (M.l2_have == M.l2_need) {
        m_frame(M.l2, M.l2_have);
        M.l2_need = 0;
    }
}

/* one connection event on the computer's link */
static void m_event(void)
{
    uint8_t pdu[2 + 255];
    int i, sent = 0;
    if (!hw.pconn_on)
        return;
    if (M.alive)
        for (i = 0; i < M.qn && hw.pconn_on; i++)
            ble_ll_hw_rx(MLK, M.q[i], (uint8_t)(2 + M.q[i][1]));
    M.qn = 0;
    while (M.alive && hw.pconn_on && sent < 32) {
        uint8_t n = ble_ll_hw_tx(MLK, pdu);
        if (!n)
            break;
        m_take(pdu);
        sent++;
        ble_ll_hw_tx_acked(MLK);
    }
    if (!hw.pconn_on)
        return;
    now_us += 24u * 1250u;
    ble_ll_hw_event_end(MLK, M.evt++, (uint8_t)M.alive);
}

static int m_connect(void)
{
    uint8_t ci[2 + 34] = {0x85, 34};
    memset(&M, 0, sizeof M);
    M.alive = 1;
    M.max_frag = 27;
    memcpy(ci + 2, MADDR, 6);
    memcpy(ci + 8, OWN, 6);
    ci[14] = 0x3D, ci[15] = 0x9C, ci[16] = 0x7A, ci[17] = 0x6B;       /* AA 0x6B7A9C3D */
    ci[18] = 0x33, ci[19] = 0x22, ci[20] = 0x11;                       /* CRCInit */
    ci[21] = 2;                                                        /* WinSize */
    ci[22] = 5;                                                        /* WinOffset */
    ci[24] = 24;                                                       /* Interval 30 ms */
    ci[28] = 72;                                                       /* Timeout 720 ms */
    memset(ci + 30, 0xFF, 4), ci[34] = 0x1F;
    ci[35] = 7;                                                        /* Hop 7, SCA 0 */
    return ble_ll_hw_connect_ind(ci, sizeof ci) == 1 && hw.pconn_on;
}
static void m_cccd(void)                                               /* the computer turns the MIDI notifications on */
{
    static const uint8_t w[] = {0x12, H_MIDI_CCC, 0, 0x01, 0x00};
    m_att(w, sizeof w);
}
static void m_midi_in(const uint8_t *bm, int n)                        /* a BLE-MIDI packet as a Write Without Response */
{
    uint8_t w[3 + 40] = {0x52, H_MIDI_IO, 0};
    memcpy(w + 3, bm, n);
    m_att(w, 3 + n);
}
static void m_events(int n)
{
    while (n-- > 0)
        m_event();
}

/* both links, one event each, n times (the clock moves by both) */
static void both(int n)
{
    while (n-- > 0) {
        p_event();
        m_event();
    }
}

#define NOTE_ON(note, vel) (0x09u | 0x90u << 8 | (uint32_t)(note) << 16 | (uint32_t)(vel) << 24)

/* ------------------------------------------------------------------------------------------- the scenarios */

static void settle_central(void)           /* the keyboard's discovery and subscription, to READY */
{
    p_events(60);
}

static void test_keyboard_then_computer(void)
{
    uint32_t calls0;
    ble_init(OWN, 1);
    ble_enable(1);
    check("start: advertising on the peripheral link, nothing else", hw.adv_on && !ble_ll_scanning() && !ble_connected());
    check("keyboard first: connected out as central on link 1, advertising goes on beside it",
          connect(0) && LL_BANK(BLE_LINK_CENTRAL).state == LL_CONN && hw.adv_on && !ble_connected() &&
              ble_ll_link() == MLK);
    settle_central();
    check("keyboard: GATT discovery and the subscription done: READY, MIDI ready on link 1 only",
          ble_central_state() == BLE_CS_READY && ble_midi_ready_link(BLE_LINK_CENTRAL) && !ble_midi_ready_link(MLK));
    check("the computer connects: link 0, advertising left, the central link untouched",
          m_connect() && ble_connected() && !hw.adv_on && LL_BANK(MLK).state == LL_CONN && !LL_BANK(MLK).central &&
              LL_BANK(BLE_LINK_CENTRAL).central && hw.pc.interval == 24 && ble_central_state() == BLE_CS_READY);
    calls0 = state_callsb[MLK];
    m_cccd();
    both(8);
    check("the computer turns the MIDI notifications on: MIDI ready on both links, each its own CCCD",
          ble_midi_ready_link(MLK) && ble_midi_ready_link(BLE_LINK_CENTRAL) && att_st_b[MLK].ccc_midi == 1 &&
              att_st_b[BLE_LINK_CENTRAL].ccc_midi == 0 && state_callsb[MLK] > calls0);
    check("hardware told per link: the keyboard's link kicked, no stop yet",
          hw.kicks[BLE_LINK_CENTRAL] > 0 && !hw.stops[MLK] && !hw.stops[BLE_LINK_CENTRAL]);

    /* MIDI both ways at once, each note to its own peer */
    {
        static const uint8_t bm[] = {0x80, 0x80, 0x90, 0x41, 0x55};
        static const uint8_t kbd[] = {0x1B, PH_MIDI_V, 0, 0x80, 0x80, 0x90, 0x3C, 0x64};
        int wcmd0 = 0, k;
        memset(in_nb, 0, sizeof in_nb);
        m_midi_in(bm, sizeof bm);              /* the computer plays 41 55 */
        p_att(kbd, sizeof kbd);                /* the keyboard plays 3C 64 */
        out_wb[MLK] = out_rb[MLK] = 0;
        out_qb[MLK][out_wb[MLK]++ % 64] = NOTE_ON(0x30, 0x20);                  /* to the computer */
        out_qb[BLE_LINK_CENTRAL][out_wb[BLE_LINK_CENTRAL]++ % 64] = NOTE_ON(0x31, 0x21);   /* to the keyboard */
        (void)wcmd0;
        for (k = 0; k < 12; k++)
            both(1);
        check("MIDI in: the computer's note arrives tagged link 0, the keyboard's tagged link 1, each once",
              in_nb[MLK] == 1 && in_pktb[MLK][0] == NOTE_ON(0x41, 0x55) && in_nb[BLE_LINK_CENTRAL] == 1 &&
                  in_pktb[BLE_LINK_CENTRAL][0] == NOTE_ON(0x3C, 0x64));
        check("MIDI out: the computer got its notification (30 20), the keyboard its Write Command (31 21), no mix-up",
              M.notes >= 1 && M.last_ntf_n >= 8 && M.last_ntf[0] == 0x1B && M.last_ntf[M.last_ntf_n - 3] == 0x90 &&
                  M.last_ntf[M.last_ntf_n - 2] == 0x30 && M.last_ntf[M.last_ntf_n - 1] == 0x20 && P.wcmd[3] == 0x31 &&
                  out_rb[MLK] == out_wb[MLK] && out_rb[BLE_LINK_CENTRAL] == out_wb[BLE_LINK_CENTRAL]);
    }

    /* L2CAP frames in pieces on both links at once: the banks' reassembly buffers are their own */
    {
        static const uint8_t bm[] = {0x80, 0x80, 0x90, 0x10, 0x11, 0x80, 0x92, 0x12, 0x13, 0x80, 0x94, 0x14, 0x15};
        static const uint8_t kbd[] = {0x1B, PH_MIDI_V, 0, 0x80, 0x80, 0x90, 0x20, 0x21, 0x80, 0x92, 0x22, 0x23, 0x80, 0x94, 0x24, 0x25};
        M.max_frag = 4;                        /* the computer's frame comes in 4-octet pieces */
        memset(in_nb, 0, sizeof in_nb);
        m_midi_in(bm, sizeof bm);
        p_att(kbd, sizeof kbd);
        both(14);
        check("pieces on both links at once: 3 notes from each, in order, on their own link",
              in_nb[MLK] == 3 && in_nb[BLE_LINK_CENTRAL] == 3 && in_pktb[MLK][0] == NOTE_ON(0x10, 0x11) &&
                  in_pktb[BLE_LINK_CENTRAL][0] == NOTE_ON(0x20, 0x21) && in_pktb[MLK][2] == (0x09u | 0x94u << 8 | 0x14u << 16 | 0x15u << 24) &&
                  in_pktb[BLE_LINK_CENTRAL][2] == (0x09u | 0x94u << 8 | 0x24u << 16 | 0x25u << 24));
        M.max_frag = 27;
    }

    /* the computer leaves: its link closes, the keyboard's stays, advertising again */
    {
        static const uint8_t term[] = {LL_TERMINATE_IND, 0x13};
        m_ctrl(term, sizeof term);
        both(3);
        check("the computer terminates: link 0 closed, the keyboard still READY, advertising again; link 1 never stopped",
              !ble_connected() && hw.adv_on && hw.stops[MLK] == 1 && !hw.stops[BLE_LINK_CENTRAL] &&
                  ble_central_state() == BLE_CS_READY && ble_midi_ready_link(BLE_LINK_CENTRAL) &&
                  !ble_midi_ready_link(MLK) && ble_ll_link() == MLK);
    }
    p_events(4);
    check("... and the keyboard link keeps running on its own events", ble_central_state() == BLE_CS_READY);
    leave();
    p_events(4);
    check("we leave the keyboard: TERMINATE_IND to it, link 1 stopped, advertising goes on",
          p_ctrl_seen(LL_TERMINATE_IND) && hw.stops[BLE_LINK_CENTRAL] >= 1 && hw.adv_on &&
              LL_BANK(BLE_LINK_CENTRAL).state == LL_OFF);
}

static void test_computer_then_keyboard(void)
{
    int stops_m, stops_c;
    ble_enable(0);
    ble_enable(1);
    check("again: advertising", hw.adv_on && !ble_connected());
    check("computer first: connected on link 0, advertising left", m_connect() && ble_connected() && !hw.adv_on);
    m_cccd();
    m_events(6);
    check("the computer's notifications on", ble_midi_ready_link(MLK));
    check("then the keyboard: initiating on link 1 while the computer's link runs, no advertising to stop",
          connect(0) && LL_BANK(BLE_LINK_CENTRAL).state == LL_CONN && hw.adv_stops == hw.adv_stops && ble_connected());
    {
        int k;
        for (k = 0; k < 80; k++)
            both(1);
    }
    check("the keyboard READY with the computer's link live: the order that failed on the vendor stack (fm1-base#10)",
          ble_central_state() == BLE_CS_READY && ble_midi_ready_link(BLE_LINK_CENTRAL) && ble_midi_ready_link(MLK) &&
              ble_connected() && !M.term_seen);
    stops_m = hw.stops[MLK];
    stops_c = hw.stops[BLE_LINK_CENTRAL];
    ble_central_cancel();
    both(4);
    check("we leave the keyboard: link 1 closed, the computer's link untouched and still ready",
          LL_BANK(BLE_LINK_CENTRAL).state == LL_OFF && ble_connected() && ble_midi_ready_link(MLK) &&
              hw.stops[BLE_LINK_CENTRAL] == stops_c + 1 && hw.stops[MLK] == stops_m && ble_ll_link() == MLK);
}

static void test_scan_and_off(void)
{
    ble_ll_scan(1);
    check("scan beside the computer's link: the central link scans, the computer stays",
          ble_ll_scanning() && ble_connected() && ble_midi_ready_link(MLK));
    ble_ll_scan(0);
    check("scan off: the central link idle, the computer stays", !ble_ll_scanning() && ble_connected());
    check("keyboard again with the computer connected", connect(0));
    p_events(60);
    both(2);
    ble_enable(0);
    both(4);
    check("BLUETOOTH off with both links up: TERMINATE_IND on each, both links stopped, nothing advertising",
          p_ctrl_seen(LL_TERMINATE_IND) && M.term_seen == 1 && hw.stops[MLK] >= 1 && hw.stops[BLE_LINK_CENTRAL] >= 1 &&
              !hw.adv_on && !ble_connected() && LL_BANK(BLE_LINK_CENTRAL).state == LL_OFF);
    check("the running link is the peripheral one again", ble_ll_link() == MLK);
}

int main(void)
{
    test_keyboard_then_computer();
    test_computer_then_keyboard();
    test_scan_and_off();
    printf(fails ? "BLE multilink: FAILED\n" : "BLE multilink: all passed\n");
    return fails != 0;
}
