/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the BLE stack's central role (BLE_CENTRAL: firmware/src/ble/ble_ll_central.c, ble_gattc.c, ble_central.c,
 * ble_smp_init.c) against a simulated BLE-MIDI peripheral behind a fake baseband (ble_hw.h): the mirror of
 * tests/ble_stack_test.c's simulated central. One connection event: the master's PDUs are taken (all acknowledged),
 * then the peripheral's are delivered, then the event ends.
 *   initiator     CONNECT_IND: InitA / AdvA / TxAdd / RxAdd, a valid random AA, a random CRCInit, WinSize 2,
 *                 WinOffset in [Interval / 2, Interval - 1], interval 9, latency 0, timeout 200, 37 channels, hop
 *                 5..16, SCA 0; advertising stopped first; cancel back to advertising
 *   master LL     we start the version and feature exchanges and the data length; the peripheral's feature exchange,
 *                 PHY request (no change), connection parameters request (an update at counter + 8..11), L2CAP
 *                 connection parameter update request; our channel map update (+ 7..10); LL encryption as master
 *   GATT client   MTU, Find By Type Value, Read By Type, Find Information, the CCCD write, notifications in, MIDI out
 *                 as Write Commands; a server without Find By Type Value; no BLE-MIDI service
 *   SMP           Insufficient Authentication -> legacy Just Works as initiator (c1 / s1 computed on the peripheral's
 *                 side), the STK encryption, its keys then ours, the keys to the firmware, the CCCD again; a Security
 *                 Request; a wrong Sconfirm; Pairing Failed; a bond reused (ENC_REQ with its EDIV / Rand); a bond
 *                 the peripheral lost (Key Missing -> pairing); an iPhone-like peripheral (blell-dev3: Insufficient
 *                 Authentication after Just Works, a late Pairing Failed): NEED_MITM, then legacy passkey entry with us
 *                 displaying (its user types the passkey, TK = the passkey), the authenticated bond reused, a bond it
 *                 lost (straight to the passkey), a passkey typed wrong, a peer that cannot type, one that wants Secure
 *                 Connections (AUTH, no loop), the 30 s SMP timeout while nobody types, a Security Request asking MITM
 *   RPA           ah() with the Core spec's sample (Vol 3 Part H D.7)
 *   endings       no packet from the peripheral (0x3E), its terminate, ours
 * Built at each level of BLE_CENTRAL (ble_cfg.h; tests/run_tests.sh): 1 all of the above; 3 (no pairing) a peer that asks for nothing ready as at every level, one
 * that asks for security (Insufficient Authentication, a Security Request) left with NEED_PAIR, nothing of SMP sent but
 * the Pairing Failed that answers a Security Request; with BLE_BOND=1 too (the responder kept beside it). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ble_central_model.h"

/* ---------------------------------------------------------------------------------------------- the scenarios */

static void test_initiator(void)
{
    struct ble_peer pr;
    const uint8_t *c = hw.cind;
    uint32_t aa, crc;
    uint16_t off;
    ble_init(OWN, 1);
    ble_enable(1);
    memset(&pr, 0, sizeof pr);
    memcpy(pr.addr, PADDR, 6);
    pr.addr_rand = 1;
    check("initiator: advertising first", hw.adv_on && !ble_ll_initiating());
    check("initiator: ble_central_connect: advertising stops, initiating (state 3) starts, CONNECTING",
          ble_central_connect(&pr) && hw.init_on && !hw.adv_on && ble_ll_initiating() &&
              ble_central_state() == BLE_CS_CONNECTING && hw.in.interval == 64 && hw.in.window == 60);
    aa = (uint32_t)c[14] | (uint32_t)c[15] << 8 | (uint32_t)c[16] << 16 | (uint32_t)c[17] << 24;
    crc = (uint32_t)c[18] | (uint32_t)c[19] << 8 | (uint32_t)c[20] << 16;
    off = (uint16_t)(c[22] | c[23] << 8);
    check("CONNECT_IND: type 5, TxAdd 1 (ours random), RxAdd 1 (its), length 34, InitA ours, AdvA its",
          c[0] == 0xC5 && c[1] == 34 && !memcmp(c + 2, OWN, 6) && !memcmp(c + 8, PADDR, 6));
    check("CONNECT_IND: a valid random access address (Core 2.1.2), a random CRCInit (not the vendor's 0x1983AE)",
          ble_aa_valid(aa) && crc != 0x1983AEu && crc <= 0xFFFFFFu && hw.in.conn.aa == aa && hw.in.conn.crc_init == crc);
    check("CONNECT_IND: WinSize 2, WinOffset in [4, 8], interval 9 (11.25 ms), latency 0, timeout 200 (2 s)",
          c[21] == 2 && off >= 4 && off <= 8 && c[24] == 9 && !c[25] && !c[26] && !c[27] && c[28] == 200 && !c[29]);
    check("CONNECT_IND: all 37 channels, hop 5..16, SCA 0 (251-500 ppm)",
          c[30] == 0xFF && c[31] == 0xFF && c[32] == 0xFF && c[33] == 0xFF && c[34] == 0x1F && (c[35] & 31) >= 5 &&
              (c[35] & 31) <= 16 && !(c[35] >> 5));
    ble_central_cancel();
    check("initiator: cancelled: state 3 stopped, advertising again, IDLE",
          !hw.init_on && hw.init_stops == 1 && hw.adv_on && !ble_ll_initiating() && ble_central_state() == BLE_CS_IDLE);
    ble_central_connect(&pr);
    check("initiator: a second CONNECT_IND draws another AA and CRCInit",
          hw.in.conn.aa != aa && hw.in.conn.crc_init != crc && ble_aa_valid(hw.in.conn.aa));
    ble_central_cancel();
    check("blell: two connects, two cancels counted", ble_dgc.connects == 2 && ble_dgc.cancels == 2);
}

static void test_master_and_gatt(void)
{
    uint8_t d[24];
    int i, v, f, l;
    uint16_t ev;
    check("master: the engine made us the master: connected as central, SETUP, the GATT client starts",
          connect(0) && (LL_BANK(CL).state == LL_CONN) && hw.adv_on == (BLE_LINKS > 1));
    p_events(40);
    v = p_ctrl_seen(LL_VERSION_IND), f = p_ctrl_seen(LL_FEATURE_REQ), l = p_ctrl_seen(LL_LENGTH_REQ);
    check("master LL: we start the version exchange, then the feature exchange, then the data length",
          v && f && l && v < f && f < l && LL_BANK(CL).feat_known && LL_BANK(CL).peer_feat == 0x21 && LL_BANK(CL).len_done);
    check("GATT client: MTU (185), Find By Type Value, Read By Type, Find Information, the CCCD write",
          P.att_ops[0] == 0x02 && P.att_ops[1] == 0x06 && P.att_ops[2] == 0x08 && att_st_b[CL].mtu == 185 && P.cccd == 1 &&
              ble_dgc.gc_svc_s == PH_MIDI && ble_dgc.gc_svc_e == PH_DESC && ble_dgc.gc_val == PH_MIDI_V &&
              ble_dgc.gc_cccd == PH_CCCD);
    check("ready: BLE-MIDI subscribed, ble_midi_ready_link(CL), the firmware told", ble_central_state() == BLE_CS_READY &&
          ble_midi_ready_link(CL) && ble_gattc_midi() == PH_MIDI_V && state_calls >= 2);
    {
        static const uint8_t ntf[] = {0x1B, PH_MIDI_V, 0, 0x80, 0x80, 0x90, 0x3C, 0x64};
        in_n = 0;
        p_att(ntf, sizeof ntf);
        p_event();
        check("MIDI in: the peripheral's notification decoded (note on 3C 64)",
              in_n == 1 && in_pkt[0] == (0x09u | 0x90u << 8 | 0x3Cu << 16 | 0x64u << 24));
    }
    out_q[out_w++ % 64] = 0x09u | 0x90u << 8 | 0x40u << 16 | 0x50u << 24;
    p_events(3);
    check("MIDI out: a Write Command (Write Without Response) to its MIDI I/O value: 90 40 50",
          P.writes == 1 && P.wcmd[2] == 0x90 && P.wcmd[3] == 0x40 && P.wcmd[4] == 0x50 && ble_dgc.gc_wcmd_tx == 1);
    memset(d, 0, 9);
    d[0] = LL_PERIPHERAL_FEATURE_REQ, d[1] = 0x21;
    p_ctrl(d, 9);
    d[0] = LL_PHY_REQ, d[1] = 3, d[2] = 3;
    p_ctrl(d, 3);
    i = P.ctrl_n;
    p_events(3);
    check("the peripheral's feature exchange -> FEATURE_RSP; its PHY_REQ -> PHY_UPDATE_IND with no change",
          p_ctrl_seen(LL_FEATURE_RSP) && p_ctrl_seen(LL_PHY_UPDATE_IND) &&
              !P.ctrl[p_ctrl_seen(LL_PHY_UPDATE_IND) - 1][1] && !P.ctrl[p_ctrl_seen(LL_PHY_UPDATE_IND) - 1][2] &&
              P.ctrl_n >= i + 2);
    memset(d, 0, 24);
    d[0] = LL_CONNECTION_PARAM_REQ, d[1] = 12, d[3] = 12, d[7] = 0x2C, d[8] = 1;   /* 15 ms, 3 s */
    p_ctrl(d, 24);
    ev = P.evt;
    p_events(2);
    {
        int k = p_ctrl_seen(LL_CONNECTION_UPDATE_IND);
        const uint8_t *u = k ? P.ctrl[k - 1] : d;
        uint16_t inst = (uint16_t)(u[10] | u[11] << 8);
        check("its CONNECTION_PARAM_REQ -> our CONNECTION_UPDATE_IND: interval 12, timeout 300, WinSize 1, WinOffset "
              "6, the instant 8..11 events ahead, the driver told",
              k && u[4] == 12 && u[8] == 0x2C && u[9] == 1 && u[1] == 1 && u[2] == 6 && inst >= ev + 8 &&
                  inst <= ev + 11 + 2 && hw.upd_n == 1 && hw.upd.interval == 12 && hw.upd.instant == inst);
        p_events(16);
        check("after the instant the link runs at 12; our procedure is over", LL_BANK(CL).interval == 12 && LL_BANK(CL).timeout == 300 &&
              LL_BANK(CL).lproc == P_NONE);
    }
    {
        static const uint8_t req[] = {0x12, 0x09, 8, 0, 6, 0, 9, 0, 0, 0, 200, 0};
        int n0 = P.ctrl_n;
        P.att_n = 0;
        p_l2(5, req, sizeof req);
        p_events(3);
        check("its L2CAP Connection Parameter Update Request {6, 9, 0, 200}: accepted, then an LL update to 9",
              hw.upd_n == 2 && hw.upd.interval == 9 && hw.upd.timeout == 200 && P.ctrl_n > n0 &&
                  ble_dgc.m_l2_upd_rx == 1);
        p_events(16);
    }
    {
        static const uint8_t map[5] = {0xFF, 0x0F, 0xF0, 0xFF, 0x1F};
        ev = P.evt;
        check("our channel map update (as master) at counter + 7..10, the driver told",
              ble_ll_chmap_update(map) && hw.chm_n == 1 && hw.chm_instant >= ev + 6 && hw.chm_instant <= ev + 10);
        p_events(14);
        check("... the LL keeps the new map after its instant", !memcmp(LL_BANK(CL).chm, map, 5) && LL_BANK(CL).lproc == P_NONE);
    }
    leave();
    check("we leave: TERMINATE_IND to the peripheral, the link gone, advertising again",
          p_ctrl_seen(LL_TERMINATE_IND) && !(LL_BANK(CL).state == LL_CONN) && hw.adv_on && ble_central_state() == BLE_CS_IDLE);
}

#if BLE_CEN_PAIR
static void test_pairing(void)
{
    int n0 = got_keys_n;
    connect(0);
    P.need_auth = 1;
    p_events(80);
    check("pairing: the CCCD write answered Insufficient Authentication -> legacy Just Works as initiator",
          ble_dgc.gc_auth_errs >= 1 && ble_dgc.si_pair_req >= 1 && ble_dgc.si_pair_rsp >= 1 && ble_dgc.si_confirm_ok >= 1);
    check("pairing: the STK encryption (EDIV 0, Rand 0) started by us, the link encrypted both ways",
          (LL_BANK(CL).state == LL_CONN && LL_BANK(CL).enc_tx) && P.enc_tx && P.enc_rx && ble_dgc.m_enc_on >= 1);
    check("pairing: its keys taken (LTK, EDIV / Rand, IRK, identity), ours sent after (4 PDUs), to the firmware",
          got_keys_n == n0 + 1 && got_keys.has == (BLE_KEYS_LTK | BLE_KEYS_ID) && got_keys.ltk[0] == 0x40 &&
              got_keys.ediv == 0xBEEF && got_keys.rand[7] == 0x77 && got_keys.irk[0] == 0xA7 &&
              !memcmp(got_keys.id, PID, 6) && got_keys.id_rand == 1 && P.keys_from_master == 4 && P.paired);
    {
        uint8_t irk[16];
        smp_own_irk(irk);
        check("pairing: our IRK is our own fixed one (from our address, not the random generator): a Mac that "
              "bonded once refuses a new IRK for the same identity address", !memcmp(P.our_irk, irk, 16));
    }
    {
        /* the iPad, 2026-11-03: paired with Melodee connecting out, then Mode VISIBLE: it connects to us and asks for
         * the key we gave it. Thrown away, that was Key Missing and iOS dropped the link */
        uint8_t k[16], first_ltk[16], first[10], other[16] = {0};
        memcpy(first, P.our_edrand, 10), memcpy(first_ltk, P.our_ltk, 16);
        check("pairing: the key we handed out as initiator is known when that peer connects to us (its EDIV, Rand)",
              ble_host_ltk(first + 2, (uint16_t)(first[0] | first[1] << 8), k) && !memcmp(k, first_ltk, 16) &&
                  memcmp(first_ltk, other, 16) && !ble_host_key_refused());
        uint8_t second[10], second_ltk[16];
        ble_host_new_key(16, second, second_ltk);
        check("pairing: a second peer's key is good, and the first peer's still is",
              memcmp(second_ltk, first_ltk, 16) && ble_host_ltk(second + 2, (uint16_t)(second[0] | second[1] << 8), k) &&
                  !memcmp(k, second_ltk, 16) && ble_host_ltk(first + 2, (uint16_t)(first[0] | first[1] << 8), k) &&
                  !memcmp(k, first_ltk, 16));
        ble_host_new_key(7, second, second_ltk);
        check("pairing: a short key comes back cut as it was handed out",
              !second_ltk[7] && !second_ltk[15] && ble_host_ltk(second + 2, (uint16_t)(second[0] | second[1] << 8), k) &&
                  !memcmp(k, second_ltk, 16));
        first[5]++;                             /* (one nonce octet of Rand: not a key of ours any more) */
        check("pairing: a key we did not hand out is refused, and that is told once",
              !ble_host_ltk(first + 2, (uint16_t)(first[0] | first[1] << 8), k) && ble_host_key_refused() &&
                  !ble_host_key_refused());
    }
    check("pairing: the CCCD written again once paired: ready", ble_central_state() == BLE_CS_READY && P.cccd == 1 &&
          ble_dgc.gc_retries >= 1);
    leave();
    connect(1);
    P.need_auth = 1;
    p_events(60);
    check("a bonded peer: encrypted at once with the bond's LTK (its EDIV / Rand), no pairing, ready",
          P.ltk_used == 1 && P.smp_st == 0 && ble_central_state() == BLE_CS_READY && ble_dgc.si_ltk_enc >= 1);
    leave();
    P.lost_bond = 1;
    connect(1);
    P.lost_bond = 1;
    P.need_auth = 1;
    p_events(90);
    check("a bond the peripheral lost: LL_ENC_REQ refused (Key Missing) -> paired afresh, ready",
          ble_dgc.m_enc_rej >= 1 && got_keys_n == n0 + 2 && ble_central_state() == BLE_CS_READY);
    leave();
    connect(0);
    {
        static const uint8_t sec[2] = {0x0B, 0x01};
        p_l2(6, sec, 2);
    }
    p_events(80);
    check("its SMP Security Request: we pair (no bond), then ready", ble_dgc.si_sec_req_rx == 1 && P.paired &&
          ble_central_state() == BLE_CS_READY);
    leave();
    connect(0);
    P.need_auth = 1;
    P.wrong_conf = 1;
    p_events(40);
    check("a Sconfirm that does not match its Srand: our Pairing Failed (Confirm Value Failed), the link left",
          P.smp_st == -4 && !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_PAIRING && ble_central_code() == 0x04);
    connect(0);
    P.need_auth = 1;
    P.fail_pair = 1;
    p_events(40);
    check("its Pairing Failed (Unspecified): the link left, PAIRING FAILED",
          !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_PAIRING && ble_central_code() == 0x08);
}
#endif

#if BLE_CEN_PAIR
/* p_events, watching whether a passkey was ever shown */
static int shown;
static void p_events_watch(int n)
{
    while (n-- > 0) {
        p_event();
        shown |= ble_central_passkey() != BLE_NO_PASSKEY;
    }
}

/* an iPhone app's MIDI characteristic (blell-dev3, docs/BLE-STACK.md §9.2): Insufficient Authentication even after a Just
 * Works pairing; a passkey pairing (we display, its user types) is what it takes */
static void iphone(void)
{
    P.need_auth = P.mitm_need = P.iphone = P.own_len = 1;
    P.io = 0x04;                               /* KeyboardDisplay, as a Mac and an iPhone answer */
}

/* an unauthenticated bond with it (a Just Works pairing, as the builds before 2026-10-18 made with a Mac or a phone),
 * for a reconnection that finds it needs a passkey after all */
static void unauth_bond(void)
{
    connect(0);
    P.need_auth = 1;
    p_events(80);
    leave();
}

static void test_passkey(void)
{
    int n0 = got_keys_n, i0 = hw.init_starts;
    uint32_t pr0 = ble_dgc.si_pair_req, pk;
    connect(0);
    iphone();
    shown = 0;
    p_events_watch(30);
    pk = ble_central_passkey();
    check("Mac / iPhone-like (it can type): Insufficient Authentication -> ONE pairing, the passkey at once: "
          "DisplayOnly, bonding + MITM, the passkey shown, no Just Works before it (macOS drops an alert that comes "
          "right after another)", (LL_BANK(CL).state == LL_CONN) && P.pair_reqs == 1 && P.preq_first[1] == 0x00 &&
              P.preq_first[3] == 0x05 && P.pk && pk < 1000000u && ble_dgc.cen_need_mitm == 0 &&
              ble_dgc.si_repair == 0 && ble_dgc.si_passkey == 1 && ble_central_pairing() && P.smp_st == 10 &&
              ble_dgc.si_pair_req == pr0 + 1 && hw.init_starts == i0 + 1 && got_keys_n == n0 &&
              ble_central_state() == BLE_CS_SETUP);
    p_events(400);
    check("... 4.5 s while its user types: still waiting, the same passkey, the link up (the SMP timer runs from our "
          "Mconfirm)", (LL_BANK(CL).state == LL_CONN) && ble_central_passkey() == pk && ble_central_state() == BLE_CS_SETUP);
    P.typed = (long)pk;
    p_events(80);
    check("... typed: Sconfirm / Srand with TK = the passkey, the STK, keys both ways: authenticated, ready, on the "
          "one link, no encryption pause (it was not encrypted before)",
          ble_central_state() == BLE_CS_READY && P.cccd == 1 && P.enc_auth && ble_dgc.si_auth_done == 1 &&
              P.pause_n == 0 && hw.init_starts == i0 + 1 && got_keys_n == n0 + 1 &&
              (got_keys.has & (BLE_KEYS_AUTH | BLE_KEYS_LTK)) == (BLE_KEYS_AUTH | BLE_KEYS_LTK) && ble_central_prompted());
    check("... the passkey no longer shown; no late Pairing Failed",
          ble_central_passkey() == BLE_NO_PASSKEY && ble_dgc.si_fail_late == 0);
    leave();
    pr0 = ble_dgc.si_pair_req;
    connect_sec(1, BLE_PEER_MITM | BLE_PEER_AUTH);
    iphone();
    shown = 0;
    p_events_watch(60);
    check("reconnect with the authenticated bond: encrypted with its LTK, accepted, ready, no pairing, nothing shown",
          P.ltk_used == 1 && P.enc_auth && ble_central_state() == BLE_CS_READY && ble_dgc.si_pair_req == pr0 && !shown);
    check("... the data length exchanged before our LL_ENC_REQ (the iPhone sends its own LL_LENGTH_REQ otherwise, "
          "and our answer during the encryption start made it leave with MIC Failure, blell-dev6)",
          P.mic_terms == 0 && p_ctrl_seen(LL_LENGTH_REQ) && p_ctrl_seen(LL_LENGTH_REQ) < p_ctrl_seen(LL_ENC_REQ) &&
              ble_dgc.m_len_done >= 1);
    leave();
    connect_sec(1, BLE_PEER_MITM | BLE_PEER_AUTH);
    iphone();
    P.len_always = 1;
    p_events(60);
    check("its LL_LENGTH_REQ crossing our LL_ENC_REQ: held while the encryption starts (5.1.3.1: no other PDU of "
          "ours), answered once encrypted: no MIC Failure, ready", P.mic_terms == 0 && P.ltk_used == 1 && P.enc_auth &&
              ble_dgc.m_held >= 1 && P.len_done && ble_central_state() == BLE_CS_READY && ble_dgc.si_pair_req == pr0);
    leave();
    connect_sec(1, BLE_PEER_MITM | BLE_PEER_AUTH);
    iphone();
    P.mic_on_enc = 1;
    p_events(30);
    check("it leaves with MIC Failure (0x3D) on our LL_ENC_REQ with the bond: KEY (3D), not a link lost, no pairing",
          !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_KEY && ble_central_code() == 0x3D &&
              ble_dgc.si_pair_req == pr0 && P.mic_terms == 1);
    leave();
    P.lost_bond = 1;
    connect_sec(1, BLE_PEER_MITM | BLE_PEER_AUTH);
    iphone();
    P.lost_bond = 1;
    p_events(30);
    check("its bond lost (the phone forgot it): the pairing goes straight to the passkey (MITM known), no Just Works",
          ble_dgc.m_enc_rej >= 1 && P.preq[3] == 0x05 && ble_central_passkey() != BLE_NO_PASSKEY &&
              ble_dgc.si_pair_req == pr0 + 1);
    P.typed = (long)((ble_central_passkey() + 1u) % 1000000u);
    p_events(40);
    check("a passkey typed wrong: its Pairing Failed (Confirm Value Failed) -> PAIRING (4), the link left, no retry",
          !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_PAIRING && ble_central_code() == 0x04 &&
              ble_dgc.si_pair_req == pr0 + 1 && ble_central_passkey() == BLE_NO_PASSKEY);
    P.lost_bond = 0;
    connect_sec(0, BLE_PEER_MITM);
    iphone();
    P.io = 0x03;
    p_events(20);
    check("a passkey needed but it cannot type (NoInputNoOutput): our Pairing Failed (Authentication Requirements) "
          "-> AUTH (3), nothing shown", !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_AUTH &&
              ble_central_code() == 0x03 && P.smp_st == -3 && ble_central_passkey() == BLE_NO_PASSKEY);
    connect_sec(0, BLE_PEER_MITM);
    iphone();
    P.sc_only = 1;
    p_events(10);
    P.typed = (long)ble_central_passkey();
    p_events(200); /* five 200-ms security-settling retries precede failure */
    check("refused even after the passkey (a peer that wants Secure Connections): AUTH (5), no other pairing, no loop",
          !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_AUTH && ble_central_code() == 0x05 &&
              ble_dgc.cen_need_mitm == 0 && ble_dgc.si_auth_done == 2);
    pr0 = ble_dgc.si_pair_req;
    connect_sec(0, BLE_PEER_MITM);
    iphone();
    p_events(2400);
    check("... 27 s and nobody typed: still waiting", (LL_BANK(CL).state == LL_CONN) && ble_central_passkey() != BLE_NO_PASSKEY);
    p_events(400);
    check("the 30 s SMP timeout (nobody typed): PAIRING (FF), the link left, the passkey gone",
          !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_PAIRING && ble_central_code() == 0xFF &&
              ble_central_passkey() == BLE_NO_PASSKEY && ble_dgc.si_pair_req == pr0 + 1);
    connect(0);
    P.io = 0x04;
    {
        static const uint8_t sec[2] = {0x0B, 0x05};   /* a Security Request: bonding + MITM */
        p_l2(6, sec, 2);
    }
    p_events(10);
    check("its Security Request asking MITM: our pairing asks MITM too, with the passkey",
          P.preq[3] == 0x05 && P.preq[1] == 0x00 && ble_central_passkey() != BLE_NO_PASSKEY);
    leave();    leave();
    unauth_bond();
    connect(1);
    iphone();
    shown = 0;
    p_events_watch(130);
    pk = ble_central_passkey();
    check("an older Just Works bond: encrypted with it, refused (0x05) -> paired again ON THE SAME LINK with the "
          "passkey: DisplayOnly, bonding + MITM, no disconnection", (LL_BANK(CL).state == LL_CONN) && P.ltk_used == 1 &&
              P.pair_reqs == 1 && P.preq[1] == 0x00 && P.preq[3] == 0x05 && P.pk && pk < 1000000u &&
              ble_dgc.cen_need_mitm == 1 && ble_dgc.si_repair == 1 && ble_central_pairing() && P.smp_st == 10);
    P.typed = (long)pk;
    p_events(80);
    check("... typed: the STK through the LL's encryption pause (PAUSE_ENC_REQ / RSP, a new ENC_REQ), authenticated, "
          "ready on the one link", ble_central_state() == BLE_CS_READY && P.enc_auth && P.pause_n == 1 &&
              ble_dgc.m_pause_tx >= 1 && ble_dgc.m_pause_rsp_rx >= 1 &&
              (got_keys.has & BLE_KEYS_AUTH) && ble_central_mitm());
    leave();
    unauth_bond();
    connect(1);
    iphone();
    P.no_repair = 1;
    shown = 0;
    p_events(30);
    p_events_watch(100);
    check("a peer that refuses the pairing on the encrypted link (before any passkey): NEED_MITM, the link left (a new "
          "link with the passkey from the start is the firmware's), nothing shown",
          !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_NEED_MITM && ble_dgc.si_repair_fallback == 1 && !shown &&
              ble_dgc.si_repair == 2);
    connect_sec(0, BLE_PEER_MITM);
    iphone();
    shown = 0;
    p_events_watch(10);
    pk = ble_central_passkey();
    check("again with BLE_PEER_MITM: a Pairing Request DisplayOnly, bonding + MITM, no SC; the passkey shown (0..999999)",
          P.preq_first[1] == 0x00 && P.preq_first[3] == 0x05 && P.pk && pk < 1000000u && ble_central_pairing() &&
              P.smp_st == 10 && P.pair_reqs == 1);
    P.typed = (long)pk;
    p_events(80);
    check("... typed: authenticated, ready; no pause (the link was not encrypted before)",
          ble_central_state() == BLE_CS_READY && P.enc_auth && P.pause_n == 0 && ble_dgc.si_auth_done == 4);
    leave();
    unauth_bond();
    connect(1);
    iphone();
    P.mute_repair = 1;
    p_events(100);
    check("a peer that never answers the pairing on the encrypted link: still waiting at first",
          (LL_BANK(CL).state == LL_CONN) && ble_central_passkey() == BLE_NO_PASSKEY);
    p_events(450);
    check("... 5 s: NEED_MITM (FF), the link left: a new link with the passkey instead",
          !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_NEED_MITM && ble_central_code() == 0xFF &&
              ble_dgc.si_repair_fallback == 2);
    unauth_bond();
    connect(1);
    iphone();
    P.no_pause = 1;
    p_events(100);
    P.typed = (long)ble_central_passkey();
    p_events(40);
    check("the encryption pause unknown to it (after the passkey was typed): PAIRING, the link left, no other pairing "
          "(one prompt per user action)", !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_PAIRING &&
              P.pause_n == 1 && P.pair_reqs == 1 && ble_central_passkey() == BLE_NO_PASSKEY);
    P.no_pause = 0;
}
#endif

#if !BLE_CEN_PAIR
/* BLE_CENTRAL 3 (no pairing): whatever asks for security leaves the link (NEED_PAIR); a peer that asks for nothing works */
static void test_no_pairing(void)
{
    connect(0);
    P.need_auth = 1;
    p_events(40);
    check("no pairing: the CCCD write answered Insufficient Authentication -> no Pairing Request, no LL_ENC_REQ: the "
          "link left, NEED_PAIR (5), advertising again", !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_NEED_PAIR &&
              ble_central_code() == 0x05 && P.pair_reqs == 0 && !p_ctrl_seen(LL_ENC_REQ) && !P.cccd && hw.adv_on);
    connect(0);
    {
        static const uint8_t sec[2] = {0x0B, 0x01};
        p_l2(6, sec, 2);
    }
    p_events(40);
    check("its Security Request: our Pairing Failed (Pairing Not Supported), the link left, NEED_PAIR (0)",
          !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_NEED_PAIR && ble_central_code() == 0 &&
              P.smp_st == -5 && P.pair_reqs == 0 && !p_ctrl_seen(LL_ENC_REQ));
    connect(0);
    p_events(40);
    in_n = 0;
    {
        static const uint8_t ntf[] = {0x1B, PH_MIDI_V, 0, 0x80, 0x80, 0x90, 0x3C, 0x64};
        p_att(ntf, sizeof ntf);
    }
    out_q[out_w++ % 64] = 0x09u | 0x90u << 8 | 0x41u << 16 | 0x50u << 24;
    p_events(3);
    check("again, a peer that asks for nothing (a controller, another FM-1): ready, MIDI both ways, never encrypted",
          ble_central_state() == BLE_CS_READY && P.cccd == 1 && in_n == 1 && P.wcmd[3] == 0x41 && !(LL_BANK(CL).state == LL_CONN && LL_BANK(CL).enc_tx) &&
              P.pair_reqs == 0);
    leave();
}
#endif

static void test_gatt_variants(void)
{
    connect(0);
    P.no_fbtv = 1;
    p_events(50);
    check("a server without Find By Type Value: the primary services walked (Read By Group Type), ready",
          ble_central_state() == BLE_CS_READY && ble_dgc.gc_svc_s == PH_MIDI);
    leave();
    connect(0);
    P.no_midi = 1;
    p_events(30);
    check("no BLE-MIDI service: the link left, NO MIDI SERVICE", !(LL_BANK(CL).state == LL_CONN) &&
          ble_central_fail() == BLE_CF_NO_MIDI && ble_dgc.gc_no_midi >= 1 && hw.adv_on);
}

static void test_endings(void)
{
    connect(0);
    P.alive = 0;
    p_events(8);
    check("the peripheral never answers: no packet in six intervals -> lost (0x3E), advertising again",
          !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_LOST && ble_central_code() == BLE_ERR_CONN_FAILED &&
              hw.adv_on && ble_dgc.m_estab_fails >= 1);
    connect(0);
    p_events(30);
    {
        static const uint8_t t[2] = {LL_TERMINATE_IND, 0x13};
        p_ctrl(t, 2);
    }
    p_events(2);
    check("its TERMINATE_IND after ready: lost (0x13)", !(LL_BANK(CL).state == LL_CONN) && ble_central_fail() == BLE_CF_LOST &&
          ble_central_code() == 0x13);
}

#if BLE_CEN_PAIR
/* ah(): the Core spec's sample (Vol 3 Part H D.7): IRK ec0234a357c8ad05341010a60a397d9b, prand 708194 -> 0dfbaa */
static void test_rpa(void)
{
    static const uint8_t irk_be[16] = {0xec, 0x02, 0x34, 0xa3, 0x57, 0xc8, 0xad, 0x05,
                                       0x34, 0x10, 0x10, 0xa6, 0x0a, 0x39, 0x7d, 0x9b};
    uint8_t irk[16], a[6] = {0xaa, 0xfb, 0x0d, 0x94, 0x81, 0x70};   /* hash, then prand, least significant first */
    int i;
    for (i = 0; i < 16; i++)
        irk[i] = irk_be[15 - i];
    check("RPA: the Core sample resolves (ah(IRK, 708194) = 0dfbaa)", ble_rpa_resolve(irk, a));
    a[0] ^= 1;
    check("RPA: a wrong hash does not", !ble_rpa_resolve(irk, a));
    a[0] ^= 1, a[5] = 0xF0;
    check("RPA: a static address (top bits 11) is not one", !ble_rpa_resolve(irk, a));
}
#endif

/* the console's blell: the central block printed (ble_diag.c bd_central) */
static char out[65536];
static size_t out_n;
static void put(const char *t)
{
    size_t n = strlen(t);
    if (out_n + n < sizeof out)
        memcpy(out + out_n, t, n + 1), out_n += n;
}
static void test_blell(void)
{
    ble_diag_print(put, 0);
    check("blell: the central counters printed (initiator, master, GATT client, SMP initiator, reconnection)",
          strstr(out, "cen_connects ") && strstr(out, "master_starts ") && strstr(out, "m_events_rx ") &&
              strstr(out, "gc_state ") && strstr(out, "gc_subscribed ") && strstr(out, "si_done ") &&
              strstr(out, "rc_tries ") && strstr(out, "c4_rx_to_evt_us "));
    ble_diag_clear();
    check("blell clear: the central block zeroed, its magic kept", !ble_dgc.connects && !ble_dgc.gc_subscribed &&
          ble_dgc.magic == BLE_DIAG_CENT_MAGIC && ble_dgc.m_first_evt == 0xFFFFu);
}

int main(void)
{
    test_initiator();
    test_master_and_gatt();
#if BLE_CEN_PAIR
    test_pairing();
    test_passkey();
#else
    test_no_pairing();
#endif
    test_gatt_variants();
    test_endings();
#if BLE_CEN_PAIR
    test_rpa();
#endif
    test_blell();
    printf("BLE central (level %d): %s\n", BLE_CENTRAL, fails ? "FAILED" : "all passed");
    return fails != 0;
}
