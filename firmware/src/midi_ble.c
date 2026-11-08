/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee transport adapter for Optimist's stack (see docs/BLE_MIDI.md).
 * BLE IRQ -> private RX ring -> TIMER5 -> the existing audio input ring.
 * Audio/UI -> TX ring -> BLE IRQ. Foreground mutations mask the BLE IRQs only.
 * One radio link, either role: Mode CONNECT OUT scans on the Bluetooth page and keeps the last device linked
 * (relink), Mode VISIBLE advertises for a computer or phone. No USB echo/bridge and no wireless update SysEx.
 * BLE_LINKS 2 (MELODEE_BLE_LINKS=2, docs/BLE-MULTILINK.md): a computer or phone on link 0 (BLE_LINK_PERIPH) and a device
 * Melodee connected out to on link 1 (BLE_LINK_CENTRAL) at once, each with MIDI queues of its own (struct mb_link: the
 * stack's hooks run under the link they serve, ble_ll_link()). Mode still says what an idle link does (VISIBLE: link 0
 * advertises; CONNECT OUT: link 1 scans, relinks), a link that is up stays up through a Mode change, and MIDI out goes to
 * every link that is up. */
#include "ble/ble_stack.c"
#include "ble/ble_vm.c"

#define MBQ 64u
#define MBRT 16u                                /* clock, transport, Song Position (usb.c midi_rt_q) */
struct mb_link {                                /* one link's MIDI: in (TIMER5 drains it), out, its clock and transport */
    uint32_t rx[MBQ], time[MBQ], tx[MBQ], ms[MBQ];
    volatile uint32_t rw, rr, tw, tr, rt_w, rt_r;
    uint32_t rt[MBRT], rt_ms[MBRT];
    uint8_t peeked_rt;
    volatile uint8_t ready, lost, overflow;
    uint8_t connected, closing;
};
static struct {
    struct mb_link ln[BLE_LINKS];               /* by link: BLE_LINK_PERIPH (the computer's), BLE_LINK_CENTRAL (a device's) */
    uint8_t on, started, cal, scanning, attempt, was_ready, was_pairing, forgetting;
    uint8_t boot, relink, relink_scan;          /* radio start owed from power-on; looking for the last device */
#if BLE_LINKS > 1
    uint8_t up_seen;                            /* the links ready at the last poll (bit n: link n) */
#endif
    uint8_t idx[BLE_SCAN_N], count;
    uint32_t epoch_ms, deadline, aged, generation, relink_t;
    char shown[24];                             /* "VISIBLE 1A2B": Melodee 1A2B on the computer */
    struct ble_rf_trims trims;
    struct ble_scan_tab found;
    struct ble_dev_store store;
    struct ble_found target;
    struct ble_keys keys;
    volatile uint8_t keys_new;
    char failure[24];
} mb __attribute__((section(".pool")));
static volatile uint8_t midi_ble_lost;
#define MBL (&mb.ln[ble_ll_link()])            /* inside a hook: the link it serves (one link: 0) */
#define MBC (&mb.ln[BLE_LINK_CENTRAL])         /* the link Melodee connects out on */
#if BLE_LINKS > 1
#define MB_ANY(f) (mb.ln[0].f || mb.ln[1].f)
#define MB_READY_BITS ((uint32_t)mb.ln[0].ready | (uint32_t)mb.ln[1].ready << 1)
#define mb_link_up(n) ble_ll_link_connected((uint8_t)(n))
static uint8_t mb_in_link;                      /* the link of the message mel_ble_drain hands midi_enqueue */
#else
#define MB_ANY(f) (mb.ln[0].f)
#define MB_READY_BITS ((uint32_t)mb.ln[0].ready)
#define mb_link_up(n) ble_connected()
#endif
#define mb_central_up() mb_link_up(BLE_LINK_CENTRAL)   /* (one link: the link, whichever role) */

#if MELODEE_BLE_EDITOR
/* Melodee Studio over Bluetooth (MELODEE_BLE_EDITOR, EDITOR_PROTOCOL.md "Studio over Bluetooth"): editor frames on the
 * computer's link (the peripheral role: a computer or phone that connected to Melodee). The BLE IRQ gathers one frame,
 * the bytes between F0 and F7, as usb.c does (a frame arriving while one waits is dropped: Studio asks again); editor.c
 * takes it in the main loop. A reply is not copied: it goes out from ed_out as the link takes it, ahead of the link's own
 * MIDI (a channel message inside a SysEx would end it; real time may go between its bytes), and the main loop leaves
 * ed_out alone until it is all out. */
#define MB_ED_IN 640u                           /* the longest request (CAPS 'L', as usb.c's sx_frame) */
#if BLE_LINKS > 1
#define MB_ED_HERE() (ble_ll_link() == BLE_LINK_PERIPH)          /* (inside a hook: the computer's link) */
#define MB_ED_UP() (mb.on && mb.ln[BLE_LINK_PERIPH].ready)
#else
#define MB_ED_HERE() (!ble_ll_central())
#define MB_ED_UP() (mb.on && mb.ln[0].ready && !ble_ll_central())
#endif
static struct {
    uint8_t in[MB_ED_IN];
    uint32_t n;                                 /* the frame's bytes gathered so far */
    volatile uint32_t ready;                    /* a frame waits for the main loop: its length (0 none) */
    uint8_t on, peeked;                         /* gathering; the bytes of the reply's packet peeked */
    const uint8_t *out;                         /* the reply, F0..F7: out_n bytes, sent up to out_at */
    uint32_t out_n, out_ms;
    volatile uint32_t out_at, epoch;            /* epoch: counts the computer's link coming up (WATCH ends with it) */
} mbe __attribute__((section(".pool")));
static void mb_ed_in(uint32_t pkt)              /* BLE IRQ: a SysEx packet (CIN 4..7) of the computer's link */
{
    uint32_t cin = pkt & 15u, k = cin == 4u ? 3u : cin - 4u;
    for (uint32_t i = 0; i < k; i++) {
        uint32_t b = pkt >> (8u + 8u * i) & 255u;
        if (b == 0xF0u) { mbe.on = !mbe.ready; mbe.n = 0; }
        else if (b == 0xF7u) {
            if (mbe.on && mbe.n) { RING_PUBLISH(); mbe.ready = mbe.n; }
            mbe.on = 0;
        } else if ((b & 0x80u) || mbe.n >= MB_ED_IN) mbe.on = 0;   /* (a status ends it; too long: not Studio's) */
        else if (mbe.on) mbe.in[mbe.n++] = (uint8_t)b;
    }
}
#endif

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
    struct mb_link *l = MBL;
    l->peeked_rt = l->rt_r != l->rt_w;
    if (l->peeked_rt) {
        RING_PUBLISH();
        *pkt = l->rt[l->rt_r % MBRT]; *ms = l->rt_ms[l->rt_r % MBRT];
        return 1;
    }
#if MELODEE_BLE_EDITOR
    mbe.peeked = 0;
    if (mbe.out_at < mbe.out_n && MB_ED_HERE()) {   /* the editor's reply: the link's MIDI waits for its F7 */
        uint32_t at = mbe.out_at, k = mbe.out_n - at < 3u ? mbe.out_n - at : 3u;
        const uint8_t *s = mbe.out + at;
        *pkt = (at + k < mbe.out_n ? 4u : 4u + k) | (uint32_t)s[0] << 8 | (k > 1u ? (uint32_t)s[1] << 16 : 0u) |
               (k > 2u ? (uint32_t)s[2] << 24 : 0u);
        *ms = mbe.out_ms; mbe.peeked = (uint8_t)k;
        return 1;
    }
#endif
    if (l->tr == l->tw) return 0;
    RING_PUBLISH();
    *pkt = l->tx[l->tr % MBQ]; *ms = l->ms[l->tr % MBQ];
    return 1;
}
BLE_API void ble_app_midi_pop(void)                 /* (the one peeked: a pulse may have arrived since) */
{
    struct mb_link *l = MBL;
    RING_PUBLISH();
    if (l->peeked_rt) l->rt_r++;
#if MELODEE_BLE_EDITOR
    else if (mbe.peeked) mbe.out_at += mbe.peeked;
#endif
    else l->tr++;
}
BLE_API void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last)
{
    struct mb_link *l = MBL;
    uint32_t status = (pkt >> 8) & 255u;
    if ((pkt & 15u) - 4u < 4u) {                /* SysEx: a synth's (sysex_rx.c), Studio's frames */
        uint32_t cin = pkt & 15u, k = cin == 4u ? 3u : cin - 4u;
        if (!mb.on || l->closing)
            return;
#ifdef MSX_ROUTES
        for (uint32_t i = 0; i < k; i++)
            msx_byte(pkt >> (8u + 8u * i) & 255u, MSX_BLE);
#endif
#if MELODEE_BLE_EDITOR
        if (MB_ED_HERE()) mb_ed_in(pkt);       /* (Studio: on the computer's link only) */
#endif
        return;
    }
#ifdef MSX_ROUTES
    if (status >= 0x80u && status < 0xF8u)
        msx_byte(status, MSX_BLE);              /* (another status ends a synth's frame) */
#endif
    /* Route performance messages and clock only. OTA/editor framing stays USB. */
    if (!mb.on || l->closing || !((status >= 0x80u && status < 0xF0u) ||
        status == 0xF2u || status == 0xF8u || status == 0xFAu || status == 0xFBu || status == 0xFCu)) return;
    if (l->rw - l->rr >= MBQ) { l->overflow = 1; return; }
    mb.epoch_ms = fm1_ms;
    uint32_t at = l->rw % MBQ, back = (last - ts) & 0x1FFFu;
    l->rx[at] = pkt;
    /* Preserve relative timing within the packet without trusting the peer's epoch. */
    l->time[at] = MIDI_TIME_NOW() - (back < 1000u ? back : 0u) * MIDI_TICKS_PER_MS;
    RING_PUBLISH(); l->rw++;
}
BLE_API void ble_app_state(void)                    /* (the link's own: ble_midi_ready and ble_connected are its) */
{
    struct mb_link *l = MBL;
    uint8_t ready = (uint8_t)(ble_midi_ready() && !l->closing);
    uint8_t connected = (uint8_t)ble_connected();
    if ((l->ready && !ready) || (l->connected && !connected)) l->lost = 1;
    if (!connected) {
        l->closing = 0;
        if (mb.forgetting) { memset(&bhs_key, 0, sizeof bhs_key); mb.forgetting = 0; }
    }
#if MELODEE_BLE_EDITOR
    if (ble_ll_link() == BLE_LINK_PERIPH && ready != l->ready) {   /* the computer's link: up (a new epoch) or gone */
        if (ready) mbe.epoch++;
        else { mbe.on = 0; mbe.out_n = mbe.out_at = 0; }   /* (the reply's rest is dropped) */
    }
#endif
    l->connected = connected;
    l->ready = ready;
    if (!ready) { l->tr = l->tw; l->rt_r = l->rt_w; }
}
BLE_API void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16])
{
    if (!mb.on || MBL->closing) return;
    mb_saved.bond[0] = 0xB5u; mb_saved.bond[1] = 1;
    ble_cpy(mb_saved.bond + 2, rand, 8);
    ble_wr16(mb_saved.bond + 10, ediv);
    ble_cpy(mb_saved.bond + 12, ltk, 16); mb_dirty();
}
BLE_API void ble_app_peer_id(const uint8_t irk[16], const uint8_t addr[6], uint8_t rnd)
{ (void)irk; (void)addr; (void)rnd; }
BLE_API void ble_app_central_keys(const struct ble_keys *k)
{
    if (!mb.on || MBL->closing || !mb.attempt) return;
    mb.keys = *k; RING_PUBLISH(); mb.keys_new = 1;
}

static int mel_ble_ready(void) { return MB_ANY(ready) && mb.on; }
/* n slots free on every link in `links` (bit n: link n) that is up (one link: on it, up or not): output is admitted
 * whole or not at all */
static int mel_ble_room_for(uint32_t n, uint32_t links)
{
    for (uint32_t i = 0; i < BLE_LINKS; i++) {
        const struct mb_link *l = &mb.ln[i];
        if ((links >> i & 1u) && (BLE_LINKS == 1 || l->ready) && MBQ - (l->tw - l->tr) < n) return 0;
    }
    return 1;
}
static int mel_ble_room(void) { return mel_ble_room_for(1u, ~0u); }
/* clock and transport to the links in `links` (bit n: link n) that are up, each admitting it alone */
static void mel_ble_sync_to(uint32_t pkt, uint32_t pulse, uint32_t links)
{
    if (!mb.on) return;
    for (uint32_t i = 0; i < BLE_LINKS; i++) {
        struct mb_link *l = &mb.ln[i];
        if (!(links >> i & 1u) || !l->ready || l->rt_w - l->rt_r >= (pulse ? MBRT - 4u : MBRT)) continue;
        l->rt[l->rt_w % MBRT] = pkt; l->rt_ms[l->rt_w % MBRT] = fm1_ms;
        RING_PUBLISH(); l->rt_w++;
    }
}
static void mel_ble_sync(uint32_t pkt, uint32_t pulse) { mel_ble_sync_to(pkt, pulse, ~0u); }
/* to the links in `links` that are up -> 1 queued on one at least */
static int mel_ble_emit_to(uint32_t pkt, uint32_t links)
{
    int any = 0;
    if (!mb.on) return 0;
    /* All callers are serialized against BLE consumption. Refuse admission (mel_ble_room)
     * before other transports accept the packet, so retries cannot duplicate it. */
    for (uint32_t i = 0; i < BLE_LINKS; i++) {
        struct mb_link *l = &mb.ln[i];
        if (!(links >> i & 1u) || !l->ready || l->tw - l->tr >= MBQ) continue;
        l->tx[l->tw % MBQ] = pkt; l->ms[l->tw % MBQ] = fm1_ms;
        RING_PUBLISH(); l->tw++;
        any = 1;
    }
    return any;
}
static int mel_ble_emit(uint32_t pkt) { return mel_ble_emit_to(pkt, ~0u); }
/* MIDI THRU: the links a Bluetooth message midi_enqueue has in hand did not come in on (one link: none) */
static uint32_t mel_ble_other_links(void)
{
#if BLE_LINKS > 1
    return 3u & ~(1u << mb_in_link);
#else
    return 0;
#endif
}

static void mel_ble_drain(void) /* TIMER5, the shared input queue's sole producer context */
{
    for (uint32_t i = 0; i < BLE_LINKS; i++) {
        struct mb_link *l = &mb.ln[i];
        if (l->lost || l->overflow) {
            mb_lock(); l->rr = l->rw; l->lost = l->overflow = 0; mb_unlock();
            midi_ble_lost = 1;                  /* (every Bluetooth note ends: seq.c knows one source, 3) */
        }
#if BLE_LINKS > 1
        mb_in_link = (uint8_t)i;
#endif
        for (uint32_t n = 0; l->rr != l->rw && n < MBQ; n++) {
            RING_PUBLISH(); uint32_t at = l->rr % MBQ;
            if (!midi_enqueue(l->rx[at], 3u)) { l->rr = l->rw; midi_ble_lost = 1; break; }
            midi_in_time[(mi_w - 1u) % MQ] = l->time[at];
            l->rr++;
        }
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
    mb.boot = (uint8_t)!!(mb_saved.flags & MB_RADIO);   /* (ble_boot_poll starts it once quiet) */
    ble_hw_wl82_attach(); fm1_ble_crumb_boot();
}

/* Relink: a round every MB_RELINK_MS looks for the last device up to MB_RELINK_SCAN_MS and connects when it is seen
 * (a private address resolves only from a scan). Never a new pairing loop: a refusal ends it, as does Disconnect.
 * One link: Mode CONNECT OUT only. Two links: in VISIBLE too, beside the computer's link (Kerem 2026-11-06: each round's
 * scan pauses advertising, BLE_IDLE_EXCL, until the device is back) */
#define MB_RELINK_MS 30000u
#define MB_RELINK_SCAN_MS 10000u
#define MB_RELINK_VISIBLE (BLE_LINKS > 1)
static void mb_relink(int on)
{
    mb.relink = (uint8_t)(on && (MB_RELINK_VISIBLE || !(mb_saved.flags & MB_VISIBLE)) && ble_store_has_last(&mb.store));
    mb.relink_scan = 0; mb.relink_t = fm1_ms - MB_RELINK_MS;
}

static int mel_ble_set(int on)
{
    mb.boot = 0;
    if (on && (!mb.cal || !RES_OK())) return 0;     /* (RES_OK: the radio program, res.c) */
    if (on && !mb.started) {
        if (!mb_saved.valid) {
            for (uint32_t i = 0; i < 6u; i++) mb_saved.addr[i] = (uint8_t)fm1_ble_rng32();
            mb_saved.addr[5] |= 0xC0u; mb_saved.valid = 1; mb_dirty();
        }
        /* Caller rejects live music; startup holds IRQs, keeps CPU1 in its RAM idle loop. */
        mel_ble_radio_start(&mb.trims);
        ble_init(mb_saved.addr, 1);
        ble_ll_advertise(!!(mb_saved.flags & MB_VISIBLE));
        if (mb_saved.bond[0] == 0xB5u && mb_saved.bond[1] == 1)
            ble_host_set_key(mb_saved.bond + 2, ble_rd16(mb_saved.bond + 10), mb_saved.bond + 12);
        mb.started = 1;
    }
    uint8_t f = (uint8_t)(on ? mb_saved.flags | MB_RADIO : mb_saved.flags & ~MB_RADIO);
    if (f != mb_saved.flags) { mb_saved.flags = f; mb_dirty(); }
    mb_lock();
    mb.on = (uint8_t)!!on;
    mb.failure[0] = 0;
    if (!on) { mb.scanning = mb.attempt = 0; for (uint32_t i = 0; i < BLE_LINKS; i++) mb.ln[i].lost = 1; }
    if (mb.started) ble_enable(on);
    mb_unlock(); mb.generation++;
    mb_relink(on);
    return 1;
}

static void mel_ble_scan(int on)
{
    mb_lock();
    mb.scanning = (uint8_t)(on && mb.on && !(mb_saved.flags & MB_VISIBLE) && !mb.attempt && !mb_central_up());
    if (mb.started) ble_ll_scan(mb.scanning || (MB_RELINK_VISIBLE && mb.relink_scan));   /* (two links: a VISIBLE relink
                                                 * round's scan is not the page's to end) */
    mb_unlock();
}
/* Mode: VISIBLE advertises (no page scan; one link: no relink), CONNECT OUT stays quiet unless scanning or connecting out.
 * A link up stays up; the mode applies when it ends (two links: to the link that has none). */
static void mel_ble_visible(int on)
{
    uint8_t f = (uint8_t)(on ? mb_saved.flags | MB_VISIBLE : mb_saved.flags & ~MB_VISIBLE);
    if (f == mb_saved.flags) return;
    mb_saved.flags = f; mb_dirty();
    mb_lock();
    mb.failure[0] = 0;
    if (on) mb.scanning = 0;
    if (mb.started) { ble_ll_advertise(on); if (on) ble_ll_scan(0); }
    mb_unlock(); mb.generation++;
    mb_relink(mb.on);
}
static void mb_drop(void)                       /* every link ends (Disconnect, Forget) */
{
    mb_lock();
    for (uint32_t i = 0; i < BLE_LINKS; i++) {
        struct mb_link *l = &mb.ln[i];
        l->closing = (uint8_t)mb_link_up(i); l->ready = 0; l->tr = l->tw; l->rt_r = l->rt_w;
    }
    if (mb.started) { ble_enable(0); ble_enable(mb.on); }
    mb.attempt = 0;
    for (uint32_t i = 0; i < BLE_LINKS; i++) mb.ln[i].lost = 1;
    mb.failure[0] = 0; mb.generation++;
    mb_unlock();
}
/* a connection out that did not come up in time ends; two links: the computer's link stays */
static void mb_drop_out(void)
{
#if BLE_LINKS > 1
    struct mb_link *l = MBC;
    mb_lock();
    l->closing = (uint8_t)mb_central_up(); l->ready = 0; l->tr = l->tw; l->rt_r = l->rt_w;
    if (mb.started) ble_central_cancel();
    mb.attempt = 0; l->lost = 1;
    mb.failure[0] = 0; mb.generation++;
    mb_unlock();
#else
    mb_drop();
#endif
}
static void mel_ble_disconnect(void) { mb.relink = 0; mb_drop(); }
static void mel_ble_forget(void)
{
    mel_ble_disconnect();
    mb_lock();
    mb.forgetting = MB_ANY(closing);
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
    if (!mb.on || row >= mb.count || mb_central_up() || mb.attempt) return 0;
    mb.target = mb.found.e[mb.idx[row]];
    return mb_connect_target();
}
static int mel_ble_last(void)
{
    if (!mb.on || !ble_store_has_last(&mb.store) || mb_central_up() || mb.attempt) return 0;
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

static void mb_relink_poll(void)
{
    if (!mb.relink || !mb.on || mb.attempt || mb_central_up() || mb.scanning) {
        if (mb.relink_scan && !mb.scanning && mb.started) { mb_lock(); ble_ll_scan(0); mb_unlock(); }   /* (the page's own) */
        mb.relink_scan = 0;
        return;
    }
    uint32_t t = fm1_ms - mb.relink_t;
    if (t >= MB_RELINK_MS) {
        mb.relink_t = fm1_ms; t = 0; mb.relink_scan = 1;
        mb_lock(); ble_ll_scan(1); mb_unlock();
    }
    if (!mb.relink_scan) return;
    for (uint32_t i = 0; i < mb.count; i++) {
        const struct ble_found *e = &mb.found.e[mb.idx[i]];
        if (mb_same_peer(e->addr, e->addr_rand)) {
            mb.relink_scan = 0;
            mb_lock(); ble_ll_scan(0); mb_unlock();
            mb.target = *e; (void)mb_connect_target();
            return;
        }
    }
    if (t >= MB_RELINK_SCAN_MS) { mb.relink_scan = 0; mb_lock(); ble_ll_scan(0); mb_unlock(); }
}

static void mel_ble_poll(void)
{
    if (!mb.started) return;
    uint8_t pdu[39]; uint16_t rssi; uint32_t n;
    for (uint32_t i = 0; i < BLE_SCAN_RING && (n = ble_ll_scan_take(pdu, &rssi)); i++)
        ble_scan_add(&mb.found, pdu, n, rssi, fm1_ms);
    if (fm1_ms - mb.aged >= 1000u) { mb.aged = fm1_ms; ble_scan_age(&mb.found, fm1_ms); }
    mb.count = (uint8_t)ble_scan_list(&mb.found, mb.idx);
#if BLE_LINKS > 1
    uint8_t up = (uint8_t)MB_READY_BITS;
    if (ble_host_key_refused()) { str_cpy(mb.failure, "FORGET ON PEER", sizeof mb.failure); mb.generation++; }
    else if (up & ~mb.up_seen) mb.failure[0] = 0;   /* (a link came up: what was shown for an earlier one is over) */
    mb.up_seen = up;
#else
    if (ble_host_key_refused()) { str_cpy(mb.failure, "FORGET ON PEER", sizeof mb.failure); mb.generation++; }
    else if (mb.ln[0].ready) mb.failure[0] = 0; /* (a peer came in: a refusal shown for an earlier one is over) */
#endif
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
            mb.relink = (uint8_t)(MB_RELINK_VISIBLE || !(mb_saved.flags & MB_VISIBLE)); mb.relink_t = fm1_ms;   /* (kept linked from now) */
            mb_remember();
            ble_store_save(&mb.store, mb_saved.dev); mb_dirty(); mb.generation++;
        } else if (state == BLE_CS_IDLE) {
            uint32_t why = ble_central_fail();
            if (why == BLE_CF_KEY) { ble_store_drop_bond(&mb.store); ble_store_save(&mb.store, mb_saved.dev); mb_dirty(); }
            if (why != BLE_CF_LOST && why != BLE_CF_NONE) mb.relink = 0;   /* (gone: looked for again; refused: not) */
            if (!mb.relink) str_cpy(mb.failure, why == BLE_CF_KEY ? "FORGET ON PEER" : why == BLE_CF_AUTH || why == BLE_CF_PAIRING ?
                "PAIRING FAILED" : why == BLE_CF_NO_MIDI ? "NO MIDI SERVICE" : "DISCONNECTED", sizeof mb.failure);
            mb.attempt = 0; mb.generation++;
        } else if (!mb.was_ready && fm1_ms - mb.deadline > (ble_central_pairing() ? 45000u : 10000u)) {
            if (ble_central_pairing()) mb.relink = 0;   /* (a code nobody entered: no second prompt) */
            mb_drop_out();
            if (!mb.relink) str_cpy(mb.failure, "CONNECTION TIMEOUT", sizeof mb.failure);
        }
    }
    mb_relink_poll();
}
static uint32_t mel_ble_code(void)
{ return mb.on && mb.attempt ? ble_central_passkey() : BLE_NO_PASSKEY; }
static const char *mel_ble_status(void)
{
    if (!mb.cal) return "NO CAL";
    if (!mb.on) return "BLE OFF";
    if (mel_ble_code() != BLE_NO_PASSKEY) return "PAIRING CODE";
#if BLE_LINKS > 1
    if (MB_READY_BITS == 3u) return "COMPUTER + DEVICE";
    if (MB_ANY(ready) && !mb.attempt && !mb.scanning && !mb.relink_scan) return "BLE CONNECTED";
#else
    if (mb.ln[0].ready) return "BLE CONNECTED";
#endif
    if (mb.failure[0]) return mb.failure;
    if (mb.attempt) return ble_central_pairing() ? "PAIRING" : mb.relink ? "RECONNECTING" : "FINDING MIDI";
    if (ble_connected() && !mb.ln[0].ready) return "WAITING FOR MIDI";   /* (two links: the computer's, MIDI not on) */
    if (mb.scanning) return "SCANNING";
    if (mb.relink && !(BLE_LINKS > 1 && (mb_central_up() || ((mb_saved.flags & MB_VISIBLE) && !mb.relink_scan))))
        return "RECONNECTING";                  /* (two links, VISIBLE: only while a round scans; between them, the name) */
#if BLE_LINKS > 1
    if (MB_ANY(ready)) return "BLE CONNECTED";
#endif
    if (!(mb_saved.flags & MB_VISIBLE)) return "BLE AVAILABLE";
    const uint8_t *p; uint32_t n = ble_gap_name(&p), i = n;
    while (i && p[i - 1u] != ' ') i--;          /* (the header has room for the name's address digits only) */
    if (n - i > sizeof mb.shown - 9u) n = i + sizeof mb.shown - 9u;
    str_cpy(mb.shown, "VISIBLE ", sizeof mb.shown);
    memcpy(mb.shown + 8, p + i, n - i); mb.shown[8u + n - i] = 0;
    return mb.shown;
}
static uint32_t mel_ble_signature(void)
{ return mb.generation + mb.found.gen * 13u + MB_READY_BITS * 997u + mel_ble_code() * 17u + (uint32_t)ble_central_state() * 65537u; }

/* USB maintenance only: restore a checked trim copy, never a raw flash sector.
 * Existing valid calibration cannot be replaced by a different unit's copy. */
static uint32_t mel_ble_import_cal(const uint8_t copy[BLE_RF_COPY_SIZE])
{
    if (!ble_rf_copy_ok(copy)) return 1;
    if (mb.on || MB_ANY(closing)) return 4;
    if (mb.cal && !fm1_rf_cal_on && memcmp(mb_saved.rf, copy, BLE_RF_COPY_SIZE)) return 2;
    if (mb.cal && !fm1_rf_cal_on) return 0;
    memcpy(mb_saved.rf, copy, BLE_RF_COPY_SIZE);
    ble_rf_copy_get(copy, &mb.trims);
    fm1_rf_cal_on = 0;                        /* (the saved one instead of the measurement) */
    mb.cal = 1; mb_dirty(); mb.generation++;
    return 0;
}
