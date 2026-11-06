/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulated BLE-MIDI peripheral behind a fake baseband (ble_hw.h) that tests/ble_central_test.c drives the central
 * role against, and the helpers that connect to it: shared with tests/ble_multilink_test.c, which runs the same peripheral
 * on the central link of a two-link stack beside a simulated computer on the peripheral link. The includes of the stack
 * and its fake hooks come with it. Not a test of its own. */
#ifndef BLE_CENTRAL
#define BLE_CENTRAL 1
#endif
#include "ble/ble_prim.c"
#include "ble/ble_aes.c"
#include "ble/ble_ll.c"
#include "ble/ble_smp.c"
#include "ble/ble_host.c"
#include "ble/ble_att.c"
#include "ble/ble_midi.c"
#include "ble/ble_diag.c"
#include "ble/ble_store.c"
#include "ble/ble_scan.c"
#include "ble/ble_gattc.c"
#include "ble/ble_central.c"
#define CL BLE_LINK_CENTRAL                      /* the link the central role runs on (1 with BLE_LINKS 2) */
#define CENTRAL_UP() (LL_BANK(CL).state == LL_CONN && LL_BANK(CL).central)   /* ble_ll_central() is the running link's: from here the peripheral link */

static int fails;
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
#if !BLE_SMP_LEGACY
/* BLE_CENTRAL 3 without BLE_BOND: the stack has no c1 / s1 (no SMP at all); the simulated responder must never need
 * them, as nothing of ours pairs: reaching one is a failure */
static void ble_smp_c1(const uint8_t k[16], const uint8_t r[16], const uint8_t preq[7], const uint8_t pres[7],
                       uint8_t iat, const uint8_t ia[6], uint8_t rat, const uint8_t ra[6], uint8_t out[16])
{
    (void)k, (void)r, (void)preq, (void)pres, (void)iat, (void)ia, (void)rat, (void)ra;
    memset(out, 0, 16);
    check("no pairing at this level: the responder's c1 never reached", 0);
}
static void ble_smp_s1(const uint8_t k[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t out[16])
{
    (void)k, (void)r1, (void)r2;
    memset(out, 0, 16);
    check("no pairing at this level: the responder's s1 never reached", 0);
}
#endif

/* ------------------------------------------------------------------------------------------ the fake driver */
static struct {
    int adv_on, conn_on, pconn_on, stops[2], kicks[2], adv_starts, adv_stops, init_on, init_starts, init_stops, upd_n, chm_n;
    struct ble_hw_init in;
    uint8_t cind[36];
    struct ble_hw_conn_upd upd;
    uint8_t chm[5];
    uint16_t chm_instant;
    uint8_t rnd_fill;
    struct ble_hw_conn pc;                /* the peripheral link's connection as the stack set it up */
} hw;
static uint32_t now_us = 1000000, rng = 0x2468ACE1u;

void ble_hw_adv_start(const struct ble_hw_adv *a) { (void)a, hw.adv_on = 1, hw.adv_starts++; }
void ble_hw_adv_stop(void) { hw.adv_on = 0, hw.adv_stops++; }
void ble_hw_conn_start(const struct ble_hw_conn *c) { hw.pc = *c, hw.pconn_on = 1, hw.adv_on = 0, hw.conn_on |= BLE_LINKS == 1; }
void ble_hw_conn_stop(uint8_t link) { hw.stops[link]++; if (link == BLE_LINK_CENTRAL) hw.conn_on = 0; else hw.pconn_on = 0; if (BLE_LINKS == 1) hw.conn_on = hw.pconn_on = 0; }
void ble_hw_scan_start(const struct ble_hw_scan *s) { (void)s; }
void ble_hw_scan_stop(void) {}
void ble_hw_init_start(const struct ble_hw_init *i)
{
    hw.adv_on = 0;
    hw.init_on = 1;
    hw.init_starts++;
    hw.in = *i;
    memcpy(hw.cind, i->cind, 36);
}
void ble_hw_init_stop(void) { hw.init_on = 0, hw.init_stops++; }
void ble_hw_conn_update(uint8_t link, const struct ble_hw_conn_upd *u) { hw.upd = *u, hw.upd_n++; }
void ble_hw_chmap_update(uint8_t link, const uint8_t chm[5], uint16_t instant) { memcpy(hw.chm, chm, 5), hw.chm_instant = instant, hw.chm_n++; }
void ble_hw_set_lengths(uint8_t link, uint8_t max_tx, uint8_t max_rx) { (void)max_tx, (void)max_rx; }
void ble_hw_tx_kick(uint8_t link) { hw.kicks[link]++; }
uint32_t ble_hw_time_us(void) { return now_us; }
uint32_t ble_hw_diag_now(void) { return now_us; }
static const uint8_t OWN[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};
uint8_t ble_hw_addr(uint8_t addr[6]) { memcpy(addr, OWN, 6); return 1; }
void ble_hw_rand(uint8_t *out, uint8_t n)
{
    while (n--) {
        rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
        *out++ = (uint8_t)rng;
    }
}

/* ------------------------------------------------------------------------------------------ the firmware */
/* the firmware's MIDI queues, one pair per link (the hooks tell the link by ble_ll_link()); the central test's names are the
 * central link's */
static uint32_t out_qb[BLE_LINKS][64], out_rb[BLE_LINKS], out_wb[BLE_LINKS], in_pktb[BLE_LINKS][64], in_nb[BLE_LINKS];
static uint32_t state_calls, state_callsb[BLE_LINKS];
#define out_q (out_qb[BLE_LINK_CENTRAL])
#define out_r (out_rb[BLE_LINK_CENTRAL])
#define out_w (out_wb[BLE_LINK_CENTRAL])
#define in_pkt (in_pktb[BLE_LINK_CENTRAL])
#define in_n (in_nb[BLE_LINK_CENTRAL])
int ble_app_midi_peek(uint32_t *pkt, uint32_t *t)
{
    uint32_t l = ble_ll_link();
    if (out_rb[l] == out_wb[l])
        return 0;
    *pkt = out_qb[l][out_rb[l] % 64];
    *t = 500;
    return 1;
}
void ble_app_midi_pop(void) { out_rb[ble_ll_link()]++; }
void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last)
{
    uint32_t l = ble_ll_link();
    (void)ts, (void)last, in_pktb[l][in_nb[l]++ % 64] = pkt;
}
void ble_app_state(void) { state_calls++, state_callsb[ble_ll_link()]++; }
void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16]) { (void)rand, (void)ediv, (void)ltk; }
void ble_app_peer_id(const uint8_t irk[16], const uint8_t a[6], uint8_t r) { (void)irk, (void)a, (void)r; }
static struct ble_keys got_keys;
static int got_keys_n;
void ble_app_central_keys(const struct ble_keys *k) { got_keys = *k, got_keys_n++; }

/* ------------------------------------------------------------------------------------- the peripheral */
static const uint8_t PADDR[6] = {0x37, 0x37, 0x4B, 0x65, 0x79, 0xC4};   /* random static */
static const uint8_t PID[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0xC9};     /* its identity (Identity Address Info.) */
static const uint8_t MIDI_IO[16] = {0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38, 0xDB,
                                    0xE5, 0x72, 0x77};
enum { PH_GAP = 1, PH_NAME_D, PH_NAME, PH_BAT, PH_BAT_D, PH_BAT_V, PH_MIDI, PH_MIDI_D, PH_MIDI_V, PH_CCCD, PH_DESC,
       PH_DIS, PH_DIS_D, PH_DIS_V, PH_LAST = PH_DIS_V };
static struct {
    int alive, need_auth, no_fbtv, no_midi, sec_req, wrong_conf, fail_pair, lost_bond;
    uint8_t q[96][2 + 255];
    int qn;
    uint8_t l2[700];
    int have, need;
    uint8_t ctrl[96][32];
    int ctrl_n;
    uint8_t att_ops[64];
    int att_n;
    uint16_t evt;
    int enc_rx, enc_tx;
    struct ble_ccm ctx, crx;
    uint8_t ltk[16], rand[8], irk[16];          /* its bond as handed out, and its IRK */
    uint8_t our_irk[16];                        /* the IRK we (the master) sent it */
    uint8_t our_ltk[16], our_edrand[10];        /* the key we sent it: what it asks for when it connects to us */
    uint16_t ediv;
    uint8_t preq[7], pres[7], mconf[16], srand[16], stk[16];
    int smp_st, paired, keys_from_master;
    int cccd, writes, midi_ok;
    uint8_t wcmd[8];
    int ltk_used;
    /* an iPhone app's MIDI characteristic (blell-dev3, docs/BLE-STACK.md §9.2): authenticated encryption needed */
    int mitm_need, iphone, sc_only, io;        /* CCCD only with an authenticated key; its Pairing Response AuthReq 01
                                                * and a Pairing Failed after a Just Works pairing; refused even after a
                                                * legacy passkey (a Secure Connections-only peer); its IO capability */
    int pk, enc_auth, ltk_auth;                /* passkey entry chosen; the link's key authenticated; its bond's */
    uint8_t preq_first[7];                     /* the link's first Pairing Request */
    int pair_reqs, pause_n;                    /* Pairing Requests on this link; our LL_PAUSE_ENC_REQs */
    int enc_stk;                               /* the link's key is this pairing's STK (its keys may go) */
    int no_repair, mute_repair, no_pause;      /* a pairing on the encrypted link: refused / unanswered; the
                                                * encryption pause unknown to it */
    int late_after, late_pending;              /* its late Pairing Failed only after its next ATT error */
    long typed;                                /* the passkey its user typed (-1: not yet) */
    uint8_t tk[16];
    /* the encryption start (Core Vol 6 Part B 5.1.3.1) as the iPhone kept it (blell-dev6): from our LL_ENC_REQ to our
     * LL_START_ENC_RSP no other PDU of ours, else it leaves with MIC Failure (0x3D), as NimBLE / Zephyr do */
    int enc_start, dying, mic_terms;           /* in the start; its TERMINATE_IND queued; those sent */
    int own_len, len_always, len_done;         /* its LL_LENGTH_REQ on our LL_ENC_REQ when no length exchange was
                                                * made yet (the iPhone, blell-dev6) / always; one was made */
    int enc_wait, mic_on_enc;                  /* events until its LL_ENC_RSP (its host's LTK lookup); it leaves
                                                * with 0x3D on our LL_ENC_REQ (a key it cannot use) */
    uint8_t enc_req[23];
} P;

static void p_reset(void)
{
    uint8_t keep_ltk[16], keep_rand[8];
    uint16_t ediv = P.ediv;
    int lost = P.lost_bond, la = P.ltk_auth;
    memcpy(keep_ltk, P.ltk, 16), memcpy(keep_rand, P.rand, 8);
    memset(&P, 0, sizeof P);
    memcpy(P.ltk, keep_ltk, 16), memcpy(P.rand, keep_rand, 8);
    P.ediv = ediv;
    P.lost_bond = lost;
    P.ltk_auth = la;
    P.typed = -1;
    P.io = 0x03;                               /* NoInputNoOutput: a controller (Just Works); iphone(): it can type */
    P.alive = 1;
    memset(P.irk, 0xA7, 16);
}

static void p_pdu(uint8_t llid, const uint8_t *d, int n)
{
    uint8_t *q = P.q[P.qn++];
    q[0] = llid;
    q[1] = (uint8_t)n;
    memcpy(q + 2, d, n);
    if (P.enc_tx && n) {
        ble_ccm_encrypt(&P.ctx, q[0], q + 2, (uint8_t)n);
        q[1] = (uint8_t)(n + 4);
    }
}
static void p_ctrl(const uint8_t *d, int n) { p_pdu(3, d, n); }
static void p_l2(uint16_t cid, const uint8_t *d, int n)
{
    uint8_t f[600];
    int off = 0;
    f[0] = (uint8_t)n, f[1] = (uint8_t)(n >> 8), f[2] = (uint8_t)cid, f[3] = (uint8_t)(cid >> 8);
    memcpy(f + 4, d, n);
    while (off < n + 4) {
        int k = n + 4 - off < 27 ? n + 4 - off : 27;
        p_pdu(off ? 1 : 2, f + off, k);
        off += k;
    }
}
static void p_att(const uint8_t *d, int n) { p_l2(4, d, n); }
static void p_att_err(uint8_t op, uint16_t h, uint8_t e)
{
    uint8_t d[5] = {0x01, op, (uint8_t)h, (uint8_t)(h >> 8), e};
    p_att(d, 5);
}

/* it leaves with MIC Failure (0x3D): its TERMINATE_IND, then nothing more from it */
static void p_mic_term(void)
{
    static const uint8_t t[2] = {LL_TERMINATE_IND, 0x3D};
    p_ctrl(t, 2);
    P.mic_terms++;
    P.dying = 1;
    P.enc_start = P.enc_wait = 0;
}
static void p_enc_answer(const uint8_t *p);

/* its LL: the master's control PDUs */
static void p_ll(const uint8_t *p, int n)
{
    uint8_t d[32];
    memcpy(P.ctrl[P.ctrl_n % 96], p, n < 32 ? n : 32);
    P.ctrl_n++;
    switch (p[0]) {
    case LL_VERSION_IND:
        d[0] = LL_VERSION_IND, d[1] = 10, d[2] = 0x4C, d[3] = 0, d[4] = 1, d[5] = 0;
        p_ctrl(d, 6);
        return;
    case LL_FEATURE_REQ:
        memset(d, 0, 9);
        d[0] = LL_FEATURE_RSP, d[1] = 0x21;     /* encryption, DLE */
        p_ctrl(d, 9);
        return;
    case LL_LENGTH_REQ:
        P.len_done = 1;
        d[0] = LL_LENGTH_RSP, d[1] = 251, d[2] = 0, d[3] = 0x48, d[4] = 0x08, d[5] = 251, d[6] = 0, d[7] = 0x48, d[8] = 0x08;
        p_ctrl(d, 9);
        return;
    case LL_LENGTH_RSP:
        P.len_done = 1;
        return;
    case LL_ENC_REQ:
        if (P.mic_on_enc) {
            p_mic_term();
            return;
        }
        P.enc_start = 1;
        if ((P.own_len && !P.len_done) || P.len_always) {
            d[0] = LL_LENGTH_REQ, d[1] = 251, d[2] = 0, d[3] = 0x48, d[4] = 0x08, d[5] = 251, d[6] = 0, d[7] = 0x48,
            d[8] = 0x08;
            p_ctrl(d, 9);                      /* (its own, crossing ours) */
            memcpy(P.enc_req, p, 23);
            P.enc_wait = 3;                    /* (its LL_ENC_RSP a few events later: the LTK from its host) */
            return;
        }
        p_enc_answer(p);
        return;
    case LL_START_ENC_RSP:
        P.enc_start = 0;
        P.enc_tx = 1;
        d[0] = LL_START_ENC_RSP;
        p_ctrl(d, 1);
        return;
    case LL_PAUSE_ENC_REQ:                     /* (5.1.3.2) ours encrypted, then it receives plain */
        P.pause_n++;
        if (P.no_pause) {
            d[0] = LL_UNKNOWN_RSP, d[1] = LL_PAUSE_ENC_REQ;
            p_ctrl(d, 2);
            return;
        }
        d[0] = LL_PAUSE_ENC_RSP;
        p_ctrl(d, 1);
        P.enc_rx = 0;
        return;
    case LL_PAUSE_ENC_RSP:                     /* the master's, plain: it sends plain too */
        P.enc_tx = 0;
        return;
    case LL_TERMINATE_IND:
        P.alive = 0;
        return;
    default:
        return;
    }
}

/* its answer to our LL_ENC_REQ (p): the key by EDIV / Rand (the STK, its bond) or Key Missing */
static void p_enc_answer(const uint8_t *p)
{
    uint8_t d[32], key[16], skd[16], i;
    static const uint8_t zero[8] = {0};
    uint16_t ediv = (uint16_t)(p[9] | p[10] << 8);
    if (!memcmp(p + 1, zero, 8) && !ediv && P.smp_st == 4) {
        memcpy(key, P.stk, 16);
        P.enc_auth = P.pk;
        P.enc_stk = 1;
    } else if (!P.lost_bond && ediv == P.ediv && !memcmp(p + 1, P.rand, 8) && P.ediv) {
        memcpy(key, P.ltk, 16);
        P.ltk_used++;
        P.enc_auth = P.ltk_auth;
    } else {
        d[0] = LL_REJECT_EXT_IND, d[1] = LL_ENC_REQ, d[2] = 0x06;
        p_ctrl(d, 3);
        P.enc_start = 0;
        return;
    }
    d[0] = LL_ENC_RSP;
    for (i = 0; i < 12; i++)
        d[1 + i] = (uint8_t)(0x90 + i);     /* SKDs, IVs */
    p_ctrl(d, 13);
    for (i = 0; i < 8; i++)
        skd[i] = d[1 + 7 - i], skd[8 + i] = p[11 + 7 - i];
    for (i = 0; i < 16; i++)
        P.ctx.key[i] = key[15 - i];
    ble_aes128(P.ctx.key, skd, P.ctx.key);
    memcpy(P.ctx.iv, p + 19, 4);
    memcpy(P.ctx.iv + 4, d + 9, 4);
    P.ctx.ctr = 0, P.ctx.ctr_hi = 0;
    P.crx = P.ctx;
    P.ctx.dir = 0, P.crx.dir = 1;
    d[0] = LL_START_ENC_REQ;
    p_ctrl(d, 1);
    P.enc_rx = 1;
}

/* its SMP responder (legacy Just Works, bonding): c1 / s1 from the stack's primitives (checked against the Core
 * spec's samples in ble_prim_test.c) */
/* its Sconfirm (with a passkey: once its user typed it, TK = what was typed) */
static void p_sconfirm(void)
{
    uint8_t c[17];
    memset(P.tk, 0, 16);
    if (P.pk) {
        P.tk[0] = (uint8_t)P.typed, P.tk[1] = (uint8_t)(P.typed >> 8), P.tk[2] = (uint8_t)(P.typed >> 16);
    }
    memset(P.srand, 0x3C, 16);
    c[0] = 0x03;
    ble_smp_c1(P.tk, P.srand, P.preq, P.pres, 1, OWN, 1, PADDR, c + 1);
    if (P.wrong_conf)
        c[1] ^= 1;
    p_l2(6, c, 17);
    P.smp_st = 2;
}

static void p_smp(const uint8_t *p, int n)
{
    uint8_t c[17];
    (void)n;
    switch (p[0]) {
    case 0x01:                                 /* Pairing Request */
        if (!P.pair_reqs++)
            memcpy(P.preq_first, p, 7);
        P.enc_stk = 0;
        if (P.enc_rx && P.mute_repair)
            return;                            /* (on the encrypted link: no answer) */
        if (P.fail_pair || (P.enc_rx && P.no_repair)) {
            c[0] = 0x05, c[1] = 0x08;
            p_l2(6, c, 2);
            return;
        }
        memcpy(P.preq, p, 7);
        P.pres[0] = 0x02, P.pres[1] = (uint8_t)(P.io ? P.io : 0x04), P.pres[2] = 0,
        P.pres[3] = P.iphone ? 0x01 : 0x0D, P.pres[4] = 16, P.pres[5] = p[5] & 3, P.pres[6] = p[6] & 3;
        /* KeyboardDisplay, bonding + MITM + SC asked (an iPhone: bonding only, as blell-dev3 saw): legacy, with ours */
        P.pk = ((p[3] | P.pres[3]) & 0x04) && (p[1] == 0x00 || p[1] == 0x01 || p[1] == 0x04) &&
               (P.pres[1] == 0x02 || P.pres[1] == 0x04);   /* (Table 2.8: the initiator displays, its user types) */
        p_l2(6, P.pres, 7);
        P.smp_st = 1;
        return;
    case 0x03:                                 /* Mconfirm */
        memcpy(P.mconf, p + 1, 16);
        if (P.pk && P.typed < 0) {
            P.smp_st = 10;                     /* (its user has not typed the passkey yet) */
            return;
        }
        p_sconfirm();
        return;
    case 0x04:                                 /* Mrand */
        ble_smp_c1(P.tk, p + 1, P.preq, P.pres, 1, OWN, 1, PADDR, c);
        if (memcmp(c, P.mconf, 16)) {
            c[0] = 0x05, c[1] = 0x04;
            p_l2(6, c, 2);
            P.smp_st = -100;
            return;
        }
        c[0] = 0x04;
        memcpy(c + 1, P.srand, 16);
        p_l2(6, c, 17);
        ble_smp_s1(P.tk, P.srand, p + 1, P.stk);
        P.smp_st = 4;
        return;
    case 0x06: case 0x07: case 0x08: case 0x09:   /* the master's keys (phase 3) */
        P.keys_from_master++;
        if (p[0] == 0x06)
            memcpy(P.our_ltk, p + 1, 16);
        if (p[0] == 0x07)
            memcpy(P.our_edrand, p + 1, 10);
        if (p[0] == 0x08)
            memcpy(P.our_irk, p + 1, 16);
        if (p[0] == 0x09) {
            P.paired = 1;
            if (P.iphone && !P.pk && P.late_after)
                P.late_pending = 1;
            else if (P.iphone && !P.pk) {      /* (iOS after a Just Works pairing, blell-dev3 pdu 121 / 160) */
                c[0] = 0x05, c[1] = 0x08;
                p_l2(6, c, 2);
            }
        }
        return;
    case 0x05:                                 /* Pairing Failed from the master */
        P.smp_st = -(int)p[1];
        return;
    default:
        return;
    }
}

static void p_keys(void)                       /* phase 3 (after the STK encryption): its keys first */
{
    uint8_t c[17];
    int i;
    for (i = 0; i < 16; i++)
        P.ltk[i] = (uint8_t)(0x40 + i);
    for (i = 0; i < 8; i++)
        P.rand[i] = (uint8_t)(0x70 + i);
    P.ediv = 0xBEEF;
    P.ltk_auth = P.pk;                         /* (its bond is as authenticated as the pairing was) */
    P.lost_bond = 0;
    c[0] = 0x06, memcpy(c + 1, P.ltk, 16), p_l2(6, c, 17);
    c[0] = 0x07, c[1] = 0xEF, c[2] = 0xBE, memcpy(c + 3, P.rand, 8), p_l2(6, c, 11);
    if (P.pres[6] & 2) {                       /* (its identity: only when agreed) */
        c[0] = 0x08, memcpy(c + 1, P.irk, 16), p_l2(6, c, 17);
        c[0] = 0x09, c[1] = 1, memcpy(c + 2, PID, 6), p_l2(6, c, 8);
    }
    P.smp_st = 5;
}

/* its ATT server: GAP 1-3, Battery 4-6, BLE-MIDI 7-11 (value 9, CCCD 10, a description 11), Device Information 12-14 */
static void p_att_rx(const uint8_t *p, int n)
{
    uint8_t r[64];
    uint16_t s, e;
    P.att_ops[P.att_n++ % 64] = p[0];
    switch (p[0]) {
    case 0x0A:
        /* A peer with an unsupported initial read must still allow CCC setup. */
        p_att_err(0x0A, (uint16_t)(p[1] | p[2] << 8), 0x02);
        return;
    case 0x02:
        r[0] = 0x03, r[1] = 185, r[2] = 0;
        p_att(r, 3);
        return;
    case 0x06:                                 /* Find By Type Value: primary service, the MIDI UUID */
        if (P.no_fbtv) {
            p_att_err(0x06, 0, 0x06);
            return;
        }
        if (P.no_midi || n != 23 || memcmp(p + 7, BLE_UUID_MIDI_SVC, 16)) {
            p_att_err(0x06, 1, 0x0A);
            return;
        }
        r[0] = 0x07, r[1] = PH_MIDI, r[2] = 0, r[3] = PH_DESC, r[4] = 0;
        p_att(r, 5);
        return;
    case 0x10:                                 /* Read By Group Type (primary services) */
        s = (uint16_t)(p[1] | p[2] << 8);
        if (s <= PH_GAP) {
            r[0] = 0x11, r[1] = 6, r[2] = PH_GAP, r[3] = 0, r[4] = PH_NAME, r[5] = 0, r[6] = 0x00, r[7] = 0x18;
            r[8] = PH_BAT, r[9] = 0, r[10] = PH_BAT_V, r[11] = 0, r[12] = 0x0F, r[13] = 0x18;
            p_att(r, 14);
        } else if (s <= PH_MIDI && !P.no_midi) {
            r[0] = 0x11, r[1] = 20, r[2] = PH_MIDI, r[3] = 0, r[4] = PH_DESC, r[5] = 0;
            memcpy(r + 6, BLE_UUID_MIDI_SVC, 16);
            p_att(r, 22);
        } else if (s <= PH_DIS) {
            r[0] = 0x11, r[1] = 6, r[2] = PH_DIS, r[3] = 0, r[4] = PH_DIS_V, r[5] = 0, r[6] = 0x0A, r[7] = 0x18;
            p_att(r, 8);
        } else
            p_att_err(0x10, s, 0x0A);
        return;
    case 0x08:                                 /* Read By Type 0x2803 */
        s = (uint16_t)(p[1] | p[2] << 8), e = (uint16_t)(p[3] | p[4] << 8);
        if (s <= PH_MIDI_D && e >= PH_MIDI_D) {
            r[0] = 0x09, r[1] = 21, r[2] = PH_MIDI_D, r[3] = 0, r[4] = 0x16, r[5] = PH_MIDI_V, r[6] = 0;
            memcpy(r + 7, MIDI_IO, 16);
            p_att(r, 23);
        } else
            p_att_err(0x08, s, 0x0A);
        return;
    case 0x04:                                 /* Find Information */
        s = (uint16_t)(p[1] | p[2] << 8);
        if (s <= PH_CCCD) {
            r[0] = 0x05, r[1] = 1, r[2] = PH_CCCD, r[3] = 0, r[4] = 0x02, r[5] = 0x29, r[6] = PH_DESC, r[7] = 0,
            r[8] = 0x01, r[9] = 0x29;
            p_att(r, 10);
        } else
            p_att_err(0x04, s, 0x0A);
        return;
    case 0x12:                                 /* Write Request: the CCCD */
        if (p[1] != PH_CCCD) {
            p_att_err(0x12, p[1], 0x03);
            return;
        }
        if ((P.need_auth && !P.enc_rx) || (P.mitm_need && (!P.enc_rx || !P.enc_auth || P.sc_only))) {
            p_att_err(0x12, PH_CCCD, 0x05);    /* Insufficient Authentication (Apple's peripherals, QA1831) */
            if (P.late_pending) {              /* (its late Pairing Failed after the refusal: our MITM request out) */
                static const uint8_t f[2] = {0x05, 0x08};
                P.late_pending = 0;
                p_l2(6, f, 2);
            }
            return;
        }
        P.cccd = p[3] & 1;
        r[0] = 0x13;
        p_att(r, 1);
        return;
    case 0x52:                                 /* Write Command: the FM-1's MIDI out */
        if (p[1] == PH_MIDI_V) {
            P.writes++;
            memcpy(P.wcmd, p + 3, n - 3 < 8 ? n - 3 : 8);
        }
        return;
    default:
        return;
    }
}

static void p_take(uint8_t *pdu, int len)
{
    uint8_t llid = pdu[0] & 3, n = pdu[1], *p = pdu + 2;
    (void)len;
    if (P.enc_rx && n) {
        if (!ble_ccm_decrypt(&P.crx, pdu[0], p, n)) {
            printf("peripheral: MIC failure\n");
            fails++;
            return;
        }
        n = (uint8_t)(n - 4);
    }
    if (P.enc_start && n && (llid != 3 || (p[0] != LL_START_ENC_RSP && p[0] != LL_TERMINATE_IND))) {
        p_mic_term();                          /* (anything else of ours during the start: it leaves) */
        return;
    }
    if (llid == 3) {
        p_ll(p, n);
        return;
    }
    if (llid == 2) {
        P.have = 0;
        P.need = (p[0] | p[1] << 8) + 4;
    }
    if (!P.need || P.have + n > (int)sizeof P.l2)
        return;
    memcpy(P.l2 + P.have, p, n);
    P.have += n;
    if (P.have == P.need) {
        uint16_t cid = (uint16_t)(P.l2[2] | P.l2[3] << 8);
        P.need = 0;
        if (cid == 4)
            p_att_rx(P.l2 + 4, P.have - 4);
        else if (cid == 6)
            p_smp(P.l2 + 4, P.have - 4);
    }
}

/* one connection event (the master transmits first) */
static void p_event(void)
{
    uint8_t pdu[2 + 255];
    int i, k;
    if (!hw.conn_on)
        return;
    for (k = 0; k < 32 && hw.conn_on; k++) {
        uint8_t n = ble_ll_hw_tx(CL, pdu);
        if (!n)
            break;
        if (P.alive) {
            p_take(pdu, n);
            ble_ll_hw_tx_acked(CL);
        }
    }
    if (P.enc_wait && !--P.enc_wait)
        p_enc_answer(P.enc_req);               /* (its host's LTK came) */
    if (P.smp_st == 10 && P.typed >= 0)
        p_sconfirm();                          /* (its user typed the passkey) */
    if (P.smp_st == 4 && P.enc_tx && P.enc_stk)
        p_keys();                              /* (phase 3: once encrypted with the STK, not an older key) */
    if (P.alive)
        for (i = 0; i < P.qn && hw.conn_on; i++)
            ble_ll_hw_rx(CL, P.q[i], (uint8_t)(2 + P.q[i][1]));
    P.qn = 0;
    if (!hw.conn_on)
        return;
    now_us += 9u * 1250u;
    ble_ll_hw_event_end(CL, P.evt++, (uint8_t)P.alive);
    if (P.dying)
        P.alive = P.dying = 0;                 /* (its TERMINATE_IND went: gone) */
}
static void p_events(int n)
{
    while (n-- > 0)
        p_event();
}
static int p_ctrl_seen(uint8_t op)
{
    int i;
    for (i = 0; i < P.ctrl_n && i < 96; i++)
        if (P.ctrl[i][0] == op)
            return i + 1;
    return 0;
}

/* connect to the peripheral (bonded: with its bond; sec: BLE_PEER_*) and let the engine make us the master */
static int connect_sec(int bonded, uint8_t sec)
{
    struct ble_peer pr;
    memset(&pr, 0, sizeof pr);
    memcpy(pr.addr, PADDR, 6);
    pr.addr_rand = 1;
    pr.sec = sec;
    if (bonded) {
        pr.bonded = 1;
        memcpy(pr.ltk, got_keys.ltk, 16);
        memcpy(pr.rand, got_keys.rand, 8);
        pr.ediv = got_keys.ediv;
    }
    p_reset();
    if (!ble_central_connect(&pr) || !hw.init_on)
        return 0;
    ble_ll_hw_master_start();
    hw.conn_on = 1;
    hw.init_on = 0;
    return CENTRAL_UP() && ble_central_state() == BLE_CS_SETUP;
}
static int connect(int bonded) { return connect_sec(bonded, 0); }

static void leave(void)
{
    if (CENTRAL_UP()) {
        ble_central_cancel();
        p_events(3);
    }
}

