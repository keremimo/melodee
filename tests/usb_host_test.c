/* SPDX-License-Identifier: GPL-3.0-only */
/* USB MIDI host (firmware/src/usb_host.c, usb_host_desc.h): the production code against a model of the SIE and of
 * what hangs on the cable. The model answers EP0 requests the way a keyboard, a hub and a mass-storage stick do,
 * so this tests the enumeration order, the hub handling, the packet filter and the fault paths. It does not test
 * the controller: the register sequences come from the AC79 SDK's USB0 host code and need a bench. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/hal/fm1_usb.h"
#pragma GCC diagnostic pop

/* ------------------------------------------------------- the devices --- */
enum { KBD, HUB, STICK, NDEV };
typedef struct {
    uint8_t addr, cfg, alt, present, in_default;
    uint8_t cls, mps0;
    const uint8_t *cfgd;
    uint16_t cfglen;
    uint8_t ports;                      /* hub: downstream devices by port (1-based), 0 = empty */
    uint8_t port_dev[8], port_pow[8], port_en[8], port_chg[8];
    uint32_t seen_set_cfg, seen_set_if;
} dev_t_;
static dev_t_ dev[NDEV];
static uint8_t kbd_cfg[128], stick_cfg[64], hub_cfg[32];
static uint16_t kbd_cfg_len;
static int root;                        /* the device on the root port, -1 none */
static dev_t_ *dflt;                    /* the device answering at address 0 */

static uint16_t build_kbd_cfg(uint8_t *d, int with_out)
{
    static const uint8_t body[] = {
        9, 4, 0, 0, 0, 1, 1, 0, 0,                      /* IF0 audio control */
        9, 0x24, 1, 0, 1, 9, 0, 1, 1,
        9, 4, 1, 0, 2, 1, 3, 0, 0,                      /* IF1 MIDI streaming, two endpoints */
        7, 0x24, 1, 0, 1, 0x41, 0,
        6, 0x24, 2, 1, 1, 0,
        9, 0x24, 3, 1, 3, 1, 2, 1, 0,
        9, 5, 0x02, 2, 64, 0, 0, 0, 0,                  /* OUT first, as many devices list it */
        5, 0x25, 1, 1, 1,
        9, 5, 0x81, 2, 64, 0, 0, 0, 0,                  /* EP1 IN bulk */
        5, 0x25, 1, 1, 3,
    };
    uint16_t n = 9u + sizeof body;
    static const uint8_t head[9] = {9, 2, 0, 0, 2, 1, 0, 0x80, 50};
    (void)with_out;
    memcpy(d, head, 9);
    memcpy(d + 9, body, sizeof body);
    d[2] = (uint8_t)n;
    d[3] = (uint8_t)(n >> 8);
    return n;
}

static void devices_init(void)
{
    static const uint8_t stick[] = {9, 2, 32, 0, 1, 1, 0, 0x80, 50, 9, 4, 0, 0, 2, 8, 6, 0x50, 0,
                                    7, 5, 0x81, 2, 64, 0, 0, 7, 5, 0x02, 2, 64, 0, 0};
    static const uint8_t hub[] = {9, 2, 25, 0, 1, 1, 0, 0xE0, 50, 9, 4, 0, 0, 1, 9, 0, 0, 0,
                                  7, 5, 0x81, 3, 1, 0, 12};
    memset(dev, 0, sizeof dev);
    kbd_cfg_len = build_kbd_cfg(kbd_cfg, 1);
    memcpy(stick_cfg, stick, sizeof stick);
    memcpy(hub_cfg, hub, sizeof hub);
    dev[KBD] = (dev_t_){.present = 1, .cls = 0, .mps0 = 64, .cfgd = kbd_cfg, .cfglen = kbd_cfg_len};
    dev[HUB] = (dev_t_){.present = 1, .cls = 9, .mps0 = 64, .cfgd = hub_cfg, .cfglen = 25, .ports = 4};
    dev[STICK] = (dev_t_){.present = 1, .cls = 0, .mps0 = 8, .cfgd = stick_cfg, .cfglen = 32};
    root = -1;
    dflt = 0;
}

/* --------------------------------------------------- the SIE and bus --- */
static uint8_t regs[4][32], common[16], index_reg, rsp[512];
static uint32_t rsp_len, rsp_pos, ep_cnt[4];
static void *ep_radr[4];
static uint32_t USB_CON0, line_state;
static uint8_t bus_dead;                /* nothing answers (cable pulled) */
static uint8_t devctl_ls;               /* the device is low speed */
static uint32_t ctl_requests, rx_toggle_clears;
static dev_t_ *pend_dev;
static uint8_t pend_addr;
static uint8_t rx_q[8][64];
static uint8_t rx_n[8];
static uint32_t rx_head, rx_tail;

static dev_t_ *by_addr(uint8_t a)
{
    int i;
    if (a == 0)
        return dflt;
    for (i = 0; i < NDEV; i++)
        if (dev[i].present && dev[i].addr == a && !dev[i].in_default)
            return &dev[i];
    return 0;
}

static void bus_reset(void)
{
    int i;
    for (i = 0; i < NDEV; i++) {
        dev[i].addr = 0;
        dev[i].cfg = 0;
        memset(dev[i].port_pow, 0, sizeof dev[i].port_pow);
        memset(dev[i].port_en, 0, sizeof dev[i].port_en);
        memset(dev[i].port_chg, 0, sizeof dev[i].port_chg);
    }
    dflt = root >= 0 ? &dev[root] : 0;
}

static int hub_port_dev(dev_t_ *h, uint32_t port)
{
    return port >= 1 && port <= h->ports ? (int)h->port_dev[port] - 1 : -1;
}

/* a SETUP packet to `d`: fill rsp, return 1 if answered, 0 if stalled */
static int answer(dev_t_ *d, const uint8_t *s)
{
    uint8_t rt = s[0], rq = s[1];
    uint16_t val = (uint16_t)(s[2] | s[3] << 8), idx = (uint16_t)(s[4] | s[5] << 8), len = (uint16_t)(s[6] | s[7] << 8);
    uint32_t n = 0;
    rsp_len = rsp_pos = 0;
    ctl_requests++;
    if (rt == 0x80 && rq == 6 && (val >> 8) == 1) {
        uint8_t dd[18] = {18, 1, 0x00, 0x02, d->cls, 0, 0, d->mps0, 0x09, 0x12, 1, 0, 0, 1, 1, 2, 0, 1};
        memcpy(rsp, dd, 18);
        n = 18;
    } else if (rt == 0x80 && rq == 6 && (val >> 8) == 2) {
        memcpy(rsp, d->cfgd, d->cfglen);
        n = d->cfglen;
    } else if (rt == 0xA0 && rq == 6 && val == 0x2900 && d->ports) {
        uint8_t hd[9] = {9, 0x29, d->ports, 0, 0, 10, 0, 0, 0xFF};
        memcpy(rsp, hd, 9);
        n = 9;
    } else if (rt == 0x00 && rq == 5) {
        pend_dev = d;                                   /* takes effect after the status stage */
        pend_addr = (uint8_t)val;
    } else if (rt == 0x00 && rq == 9) {
        d->cfg = (uint8_t)val;
        d->seen_set_cfg++;
    } else if (rt == 0x01 && rq == 11) {
        d->alt = (uint8_t)val;
        d->seen_set_if++;
    } else if (rt == 0x23 && rq == 3 && d->ports) {
        int c = hub_port_dev(d, idx);
        if (val == 8)
            d->port_pow[idx] = 1;
        if (val == 4 && d->port_pow[idx]) {
            d->port_en[idx] = 1;
            d->port_chg[idx] |= 0x10;
            if (c >= 0) {
                dev[c].addr = 0;
                dev[c].in_default = 1;
                dflt = &dev[c];
            }
        }
    } else if (rt == 0x23 && rq == 1 && d->ports) {
        if (val == 20)
            d->port_chg[idx] &= (uint8_t)~0x10u;
    } else if (rt == 0xA3 && rq == 0 && d->ports) {
        int c = hub_port_dev(d, idx);
        uint32_t st = (c >= 0 && d->port_pow[idx] ? 1u : 0u) | (d->port_en[idx] ? 2u : 0u) | 0x100u;
        rsp[0] = (uint8_t)st;
        rsp[1] = (uint8_t)(st >> 8);
        rsp[2] = d->port_chg[idx];
        rsp[3] = 0;
        n = 4;
    } else {
        return 0;
    }
    rsp_len = n < len ? n : len;
    return 1;
}

static void deliver_rx(void)            /* an IN request is pending on EP1 and the keyboard has something */
{
    dev_t_ *d;
    if (regs[1][20] & 1u || !(regs[1][20] & 0x20u))
        return;
    if (bus_dead) {
        regs[1][20] = 0x04;
        return;
    }
    d = by_addr(common[0]);
    if (!d || d != &dev[KBD] || rx_head == rx_tail)
        return;
    memcpy(ep_radr[1], rx_q[rx_tail % 8], rx_n[rx_tail % 8]);
    regs[1][22] = rx_n[rx_tail % 8];
    regs[1][23] = 0;
    rx_tail++;
    regs[1][20] = 1;
}

static void mock_write(uint32_t r, uint32_t v)
{
    if (r == 14) {
        index_reg = (uint8_t)v;
    } else if (r == 1 && (v & 8u)) {                    /* POWER: bus reset */
        bus_reset();
        common[r] = (uint8_t)v;
    } else if (r < 16) {
        common[r] = (uint8_t)v;
    } else if (index_reg == 0 && r == 17) {             /* EP0 CSR, host */
        dev_t_ *d = by_addr(common[0]);
        if (v == 0x0A) {
            uint8_t s[8];
            memcpy(s, ep_radr[0], 8);
            if (bus_dead || !d || !answer(d, s))
                regs[0][17] = 0x10;                     /* no answer */
            else
                regs[0][17] = 0;
        } else if (v == 0x20) {
            uint32_t mps = d && d->mps0 ? d->mps0 : 8u, n = rsp_len - rsp_pos;
            if (n > mps)
                n = mps;
            memcpy(ep_radr[0], rsp + rsp_pos, n);
            rsp_pos += n;
            regs[0][22] = (uint8_t)n;
            regs[0][17] = 1;
        } else if (v == 0x60) {
            regs[0][22] = 0;
            regs[0][17] = bus_dead || !d ? 0x10 : 1;
            if (pend_dev) {
                pend_dev->addr = pend_addr;
                pend_dev->in_default = 0;
                if (pend_dev == dflt)
                    dflt = 0;
                pend_dev = 0;
            }
        } else if (v == 0x42) {
            regs[0][17] = bus_dead || !d ? 0x10 : 0;
        } else {
            regs[0][17] = (uint8_t)v;
        }
    } else if (index_reg == 1 && r == 20) {
        if (v == 0x20) {
            regs[1][20] = 0x20;
            deliver_rx();
        } else if (v == 0x10) {
            regs[1][20] &= (uint8_t)~1u;
        } else if (v == 0x80) {
            rx_toggle_clears++;                         /* ClrDataTog with nothing else */
            regs[1][20] = 0;
        } else {
            regs[1][20] = (uint8_t)v;
        }
    } else {
        regs[index_reg][r] = (uint8_t)v;
    }
}
static uint8_t last_read;
static void mock_rd(uint32_t r) { last_read = (uint8_t)r; }
static uint32_t mock_dt(void)
{
    uint32_t r = last_read;
    if (r == 15)
        return devctl_ls ? 0x20u : 0x40u;
    return r < 16 ? common[r] : regs[index_reg][r];
}
static void mock_enable(uint32_t eps) { (void)eps; }
static void mock_txbuf(uint32_t ep, void *p) { (void)ep; (void)p; }
static void mock_rxbuf(uint32_t ep, void *p) { ep_radr[ep] = p; }
static void mock_send(uint32_t ep, void *p, uint32_t n) { (void)p; ep_cnt[ep] = n; }
static void mock_ep0_send(void *p, uint32_t n) { mock_send(0, p, n); }
static void mock_ep0_buf(void *p) { ep_radr[0] = p; }
static void mock_nop(void) {}
static void mock_attach(void *p) { (void)p; }
static void mock_host_attach(void *p) { ep_radr[0] = p; USB_CON0 = 0x25; }
static uint32_t mock_one(void) { return 1; }
static uint32_t mock_zero(void) { return 0; }
static uint32_t mock_line(void) { return line_state; }
static void fm1_delay_ms(uint32_t ms) { (void)ms; }

#define fm1_usb_ep_enable mock_enable
#define fm1_usb_ep_txbuf mock_txbuf
#define fm1_usb_ep_rxbuf mock_rxbuf
#define fm1_usb_ep_send mock_send
#define fm1_usb_ep0_send mock_ep0_send
#define fm1_usb_ep0_buf mock_ep0_buf
#define fm1_usb_rx_sync mock_nop
#define fm1_usb_sie_on mock_one
#define fm1_usb_sie_done mock_one
#define fm1_usb_sie_wr_start mock_write
#define fm1_usb_sie_rd_start mock_rd
#define fm1_usb_sie_data mock_dt
#define fm1_usb_reset mock_nop
#define fm1_usb_off mock_nop
#define fm1_usb_attach mock_attach
#define fm1_usb_host_attach mock_host_attach
#define fm1_usb_line mock_line
#define fm1_usb_sof_take mock_zero
#define fm1_wdt_feed() (fm1_ms++)                       /* the host code's waits run on this clock */
#define RING_PUBLISH() ((void)0)
#define MELODEE_USB_AUDIO 0
#define MELODEE_CDC 0
#define MELODEE_OTA 0
#define MELODEE_USB_HOST 1
#include "../firmware/src/usb.c"

/* ---------------------------------------------------------- the tests --- */
static void connect(int which)
{
    devices_init();
    root = which;
    bus_dead = devctl_ls = 0;
    memset(common, 0, sizeof common);
    memset(regs, 0, sizeof regs);
    dflt = 0;
    rx_head = rx_tail = 0;
    mi_r = mi_w = 0;
    midi_in_overflow = 0;
    if (which == HUB) {
        dev[HUB].port_dev[1] = STICK + 1;               /* port 1: a mass-storage stick, port 3: the keyboard */
        dev[HUB].port_dev[3] = KBD + 1;
    }
    line_state = 2;
    uh.pkts = uh.events = 0;
}

static void host_on(void)
{
    uh.want = 1;
    uh_task(fm1_ms);
    assert(uh.on && uh.msg == UH_MSG_ON && !usb.up);
    uh.msg = 0;
}

static int enumerate(void)                              /* the main loop until run or give-up */
{
    int k;
    for (k = 0; k < 400 && !uh.run; k++) {
        fm1_ms += 20;
        uh_task(fm1_ms);
    }
    return uh.run;
}

static void push_packet(uint32_t pkt)
{
    uint8_t *q = rx_q[rx_head % 8];
    q[0] = (uint8_t)pkt;
    q[1] = (uint8_t)(pkt >> 8);
    q[2] = (uint8_t)(pkt >> 16);
    q[3] = (uint8_t)(pkt >> 24);
    rx_n[rx_head % 8] = 4;
    rx_head++;
}

static void poll_n(int n)
{
    while (n--) {
        usb_poll();
        deliver_rx();
    }
}

static void test_parse(void)
{
    uh_midi_t m;
    uint8_t cut[128];
    assert(uh_parse_midi(kbd_cfg, kbd_cfg_len, &m));
    assert(m.found && m.cfg == 1 && m.iface == 1 && m.alt == 0 && m.in_ep == 1 && m.in_type == 2 &&
           m.in_mps == 64 && m.out_ep == 2);
    assert(!uh_parse_midi(stick_cfg, 32, &m) && !m.found);
    assert(!uh_parse_midi(hub_cfg, 25, &m));
    memcpy(cut, kbd_cfg, kbd_cfg_len);
    assert(!uh_parse_midi(cut, 76, &m));                    /* cut inside the IN endpoint (it starts at 72) */
    assert(uh_parse_midi(cut, kbd_cfg_len, &m));
    cut[72] = 0;                                            /* a zero-length descriptor ends the walk safely */
    assert(!uh_parse_midi(cut, kbd_cfg_len, &m));
    assert(uh_midi_ok(0x643C9009u) == 0x643C9009u);        /* note on ch0 */
    assert(uh_midi_ok(0x643C9019u) == 0x643C9009u);        /* cable 1 is dropped */
    assert(uh_midi_ok(0x0000F80Fu) == 0x0000F80Fu);        /* clock */
    assert(uh_midi_ok(0x0000F70Fu) == 0 && uh_midi_ok(0x4F7DF004u) == 0);   /* SysEx, other realtime */
    assert(uh_midi_ok(0x643C8009u) == 0);                  /* CIN does not match status */
    assert(uh_midi_ok(0x003CC00Cu) == 0x003CC00Cu);        /* program change */
    assert(uh_midi_ok(0x64BC9009u) == 0 && uh_midi_ok(0xE43C9009u) == 0);   /* data bytes with bit 7 */
    puts("USB host: descriptor walk and packet filter: OK");
}

static void test_keyboard(int via_hub)
{
    connect(via_hub ? HUB : KBD);
    host_on();
    assert(enumerate());
    assert(uh.run && uh.msg == UH_MSG_MIDI);
    uh.msg = 0;
    assert(dev[KBD].cfg == 1 && dev[KBD].addr == uh.dev && dev[KBD].addr != 0);
    if (via_hub) {
        assert(dev[HUB].cfg == 1 && dev[HUB].addr == 1);
        assert(dev[STICK].addr != 0 && dev[STICK].addr != dev[KBD].addr);   /* the stick was addressed, then passed over */
        assert(dev[KBD].addr == 3);
    } else {
        assert(dev[KBD].addr == 1);
    }
    assert(common[0] == dev[KBD].addr);                 /* FADDR is the keyboard's for the endpoint */
    assert(regs[1][26] == (2 << 4 | 1));                /* RXTYPE: bulk, endpoint 1 */
    assert(regs[1][19] == 0xFF);

    /* events: note on, SysEx (dropped), clock, note on from cable 1 */
    push_packet(0x643C9009u);                           /* cin 9: 90 3C 64 */
    push_packet(0x4F7DF004u);                           /* SysEx start: dropped */
    push_packet(0x0000F80Fu);                           /* clock */
    push_packet(0x7F409119u);                           /* cable 1 note on ch1 */
    poll_n(8);
    assert(mi_w - mi_r == 3);
    assert(midi_in_q[mi_r % MQ] == 0x643C9009u && midi_in_source[mi_r % MQ] == 1);
    assert(midi_in_q[(mi_r + 1) % MQ] == 0x0000F80Fu);
    assert(midi_in_q[(mi_r + 2) % MQ] == 0x7F409109u);  /* the cable bits are gone */
    assert(uh.pkts == 4 && uh.events == 3);

    /* a full ring holds the packet back instead of overflowing */
    mi_r = mi_w = 0;
    mi_w = MQ - 10u;                                    /* fewer than EP1_ROOM free */
    mi_r = 0;
    push_packet(0x643C9009u);
    poll_n(4);
    assert(mi_w == MQ - 10u && !midi_in_overflow);
    mi_r = mi_w;                                        /* the main loop drained it */
    poll_n(2);
    assert(mi_w == mi_r + 1u);
    mi_r = mi_w;

    /* the cable goes: errors, then a fault the main loop recovers from */
    bus_dead = 1;
    line_state = 0;
    rx_toggle_clears = 0;
    poll_n(40);
    assert(!uh.run && uh.fault);
    assert(rx_toggle_clears == 0);                      /* errors never reset the data toggle */
    fm1_ms += 20;
    uh_task(fm1_ms);
    assert(!uh.fault && !uh.run && uh.msg == UH_MSG_LOST);
    uh.msg = 0;
    for (int k = 0; k < 100; k++) {                     /* nothing attached: nothing happens */
        fm1_ms += 20;
        uh_task(fm1_ms);
    }
    assert(!uh.run && uh.msg == 0);

    /* plugged in again */
    bus_dead = 0;
    bus_reset();
    line_state = 2;
    assert(enumerate() && uh.msg == UH_MSG_MIDI);
    uh.msg = 0;

    /* back to a device: the host leaves the port */
    uh.want = 0;
    uh_task(fm1_ms);
    assert(!uh.on && !uh.run && uh.msg == UH_MSG_OFF);
    uh.msg = 0;
    usb.up = 0;
    usb_retry(fm1_ms + 5000u);                          /* usb_start again as a device */
    assert(usb.up);
    usb.up = 0;
    printf("USB host: keyboard %s: enumeration, events, ring back-pressure, unplug, replug, back to device: OK\n",
           via_hub ? "behind a hub" : "direct");
}

static void test_no_midi(void)
{
    connect(STICK);
    host_on();
    assert(!enumerate());
    assert(uh.msg == UH_MSG_NOMIDI);                    /* said once, then quiet */
    uh.msg = 0;
    {
        int k;
        for (k = 0; k < 400; k++) {
            fm1_ms += 20;
            uh_task(fm1_ms);
        }
    }
    assert(uh.msg == 0 && !uh.run);
    uh.want = 0;
    uh_task(fm1_ms);
    uh.msg = 0;

    connect(KBD);
    devctl_ls = 1;
    host_on();
    assert(!enumerate() && uh.msg == UH_MSG_LOW);
    uh.msg = 0;
    for (int k = 0; k < 600; k++) {                     /* 12 s with the device still attached: said once */
        fm1_ms += 20;
        uh_task(fm1_ms);
    }
    assert(uh.msg == 0 && !uh.run);
    uh.want = 0;
    uh_task(fm1_ms);
    uh.msg = 0;
    puts("USB host: a stick without MIDI, a low-speed device: reported, no endpoint started: OK");
}

static void test_device_mode_untouched(void)
{
    /* with the host off, usb_poll is the device driver and uh_task does nothing */
    connect(KBD);
    uh.want = uh.on = uh.run = 0;
    usb.up = 0;
    uh_task(100);
    assert(!uh.on && !uh.msg);
    usb_retry(fm1_ms + 5000u);
    assert(usb.up);
    usb.up = 0;
    puts("USB host: off at boot, the device side starts as before: OK");
}

int main(void)
{
    devices_init();
    test_parse();
    test_device_mode_untouched();
    test_keyboard(0);
    test_keyboard(1);
    test_no_midi();
    puts("USB host: all OK");
    return 0;
}
