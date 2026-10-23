/* SPDX-License-Identifier: GPL-3.0-only */
/* USB host: what Melodee reads from an attached device, with no register access (usb_host.c uses it; the host
 * tests include it alone).
 *   uh_parse_midi   a configuration descriptor -> the first USB-MIDI streaming interface with a bulk or interrupt
 *                   IN endpoint (and its OUT endpoint, if any)
 *   uh_midi_ok      one USB-MIDI event packet -> 1 when it is a channel message or a clock / transport byte */
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t found;               /* an IN endpoint of a MIDI streaming interface was found */
    uint8_t cfg;                 /* bConfigurationValue */
    uint8_t iface, alt;          /* its interface and alternate setting */
    uint8_t in_ep;               /* endpoint number (without the direction bit) */
    uint8_t in_type;             /* 2 bulk, 3 interrupt */
    uint8_t in_interval;         /* bInterval (interrupt only) */
    uint16_t in_mps;
    uint8_t out_ep;              /* the OUT endpoint of the same interface, 0 = none */
} uh_midi_t;

/* d / len: the configuration descriptor as read (possibly cut short: a descriptor that does not fit is ignored) */
static int uh_parse_midi(const uint8_t *d, uint32_t len, uh_midi_t *m)
{
    uint32_t i = 0;
    uint8_t cfg = 0, in_midi = 0, iface = 0, alt = 0;
    m->found = m->cfg = m->iface = m->alt = m->in_ep = m->in_type = m->in_interval = m->out_ep = 0;
    m->in_mps = 0;
    while (i + 2u <= len) {
        uint32_t l = d[i];
        if (l < 2u || i + l > len)
            break;
        if (d[i + 1] == 2u && l >= 9u) {
            cfg = d[i + 5];
        } else if (d[i + 1] == 4u && l >= 9u) {
            iface = d[i + 2];
            alt = d[i + 3];
            in_midi = d[i + 5] == 1u && d[i + 6] == 3u;     /* audio class, MIDI streaming subclass */
            if (m->found && iface != m->iface)
                in_midi = 0;                                /* the first MIDI interface is the one used */
        } else if (d[i + 1] == 5u && l >= 7u && in_midi) {
            uint8_t ea = d[i + 2], type = d[i + 3] & 3u;
            if (type == 2u || type == 3u) {
                if ((ea & 0x80u) && !m->found) {
                    m->found = 1;
                    m->cfg = cfg;
                    m->iface = iface;
                    m->alt = alt;
                    m->in_ep = ea & 0x0Fu;
                    m->in_type = type;
                    m->in_interval = d[i + 6];
                    m->in_mps = (uint16_t)(d[i + 4] | d[i + 5] << 8);
                } else if (!(ea & 0x80u) && !m->out_ep && (!m->found || iface == m->iface)) {
                    m->out_ep = ea & 0x0Fu;
                }
            }
        }
        i += l;
    }
    return m->found;
}

/* A USB-MIDI event packet: cable / CIN, then the MIDI bytes. Channel messages (CIN 8..E whose status matches)
 * and the one-byte clock, start, continue and stop are passed on; SysEx and everything else is not: a
 * controller has no business with the SysEx commands (update, editor) that usb.c's own parser answers. The
 * cable number is dropped. Returns the packet to enqueue, or 0. */
static uint32_t uh_midi_ok(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    pkt &= ~0xF0u;
    if (cin == 0xFu)
        return st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu ? pkt : 0u;
    if (cin >= 8u && cin <= 0xEu && (st >> 4) == cin && !((pkt >> 16) & 0x80u) &&
        ((cin == 0xCu || cin == 0xDu) || !(pkt & 0x80000000u)))
        return pkt;
    return 0u;
}
