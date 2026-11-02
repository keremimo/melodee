/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Exercise the production endpoint driver with a minimal indexed-SIE model.
 * This tests lifecycle and packet ownership, not the physical USB controller. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* Include the HAL once, then substitute only its hardware boundary. The real
 * usb.c (including EP0 setup/data/status handling) runs against this SIE model. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/hal/fm1_usb.h"
#pragma GCC diagnostic pop
static uint8_t regs[4][24], common[16], index_reg, read_reg, ep0_command;
static uint32_t USB_CON0, ep_cnt[4];
static void *ep_tadr[4], *ep_radr[4];
static void (*send_check)(uint32_t ep);
static void mock_enable(uint32_t eps) { USB_CON0 &= ~(eps << 19); }
static void mock_txbuf(uint32_t ep, void *p) { ep_tadr[ep] = p; }
static void mock_rxbuf(uint32_t ep, void *p) { ep_radr[ep] = p; }
static void mock_send(uint32_t ep, void *p, uint32_t n)
{
    ep_tadr[ep] = p;
    ep_cnt[ep] = n;
    if (send_check) send_check(ep);
}
static void mock_ep0_send(void *p, uint32_t n) { mock_send(0, p, n); }
static void mock_ep0_buf(void *p) { ep_radr[0] = p; }
static void mock_nop(void) {}
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
static void mock_attach(void *p) { (void)p; }
static uint32_t mock_one(void) { return 1; }
static uint32_t mock_zero(void) { return 0; }
static void mock_read(uint32_t r) { read_reg = (uint8_t)r; }
static uint32_t mock_data(void)
{
    uint32_t r = read_reg;
    uint8_t value = r < 16 ? common[r] : regs[index_reg][r];
    if (r == 2 || r == 4 || r == 6) common[r] = 0; /* read-clear IRQs */
    return value;
}
static void mock_write(uint32_t r, uint32_t v)
{
    if (r == 14)                            /* INDEX */
        index_reg = v;
    else if (r < 16)
        common[r] = v;
    else if (!index_reg && r == 17) {       /* EP0 CSR */
        ep0_command = (uint8_t)v;
        if (v == 0x80) regs[0][17] &= ~0x10u;
        else regs[0][17] = v == 0x60 ? 4 : (v & 2u);
    } else if ((r == 17 && (v & 8u)) || (r == 20 && (v & 16u)))
        regs[index_reg][r] = 0;             /* self-clearing flush */
    else
        regs[index_reg][r] = v;
}
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
#define fm1_usb_sie_rd_start mock_read
#define fm1_usb_sie_data mock_data
#define fm1_usb_reset mock_nop
#define fm1_usb_off mock_nop
#define fm1_usb_attach mock_attach
#define fm1_usb_sof_take mock_zero
#define RING_PUBLISH() ((void)0)
#define MELODEE_USB_AUDIO 1
#define MELODEE_CDC 0
#define MELODEE_OTA 0
#include "usb_audio_memory.h"
#include "../firmware/src/usb.c"

static void frame(uint16_t n)
{
    common[S_FRAME1] = n & 255;
    common[S_FRAME2] = (n >> 8) & 7;
}

static void setup(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t len)
{
    const uint8_t s[8] = {type, request, value & 255, value >> 8,
                         index & 255, index >> 8, len & 255, len >> 8};
    memcpy(ep0buf, s, 8);
    regs[0][S_CSR0] = 1;
    regs[0][S_COUNT0] = 8;
    ep0_service();
}

static void rate_data(uint32_t rate, uint8_t len)
{
    ep0buf[0] = rate & 255;
    ep0buf[1] = (rate >> 8) & 255;
    ep0buf[2] = rate >> 16;
    regs[0][S_CSR0] = 1;
    regs[0][S_COUNT0] = len;
    ep0_service();
}

static uint32_t get_rate(uint8_t ep, uint8_t request)
{
    setup(0xA2, request, 0x100, ep, 3);
    assert(ep0_command == 0x0A && ep_cnt[0] == 3);
    return ep0buf[0] | (uint32_t)ep0buf[1] << 8 | (uint32_t)ep0buf[2] << 16;
}

static void controls(void)
{
    uint32_t pw;
    ua_hw_stop();
    assert(get_rate(2, 0x81) == 44100 && get_rate(0x82, 0x81) == 44100);
    assert(get_rate(2, 0x82) == 44100 && get_rate(2, 0x83) == 44100);
    assert(get_rate(0x82, 0x84) == 0);
    /* A host may set the rate before enabling its alternate setting. */
    setup(0x22, 1, 0x100, 2, 3);
    assert(ua_rate_pending == 2 && ep0_command == 0x40);
    rate_data(44100, 3);
    assert(ep0_command == 0x48 && !ua_rate_pending && get_rate(2, 0x81) == 44100);
    assert(ua_feedback() == 44100u * 16384u / 1000u);
    assert(get_rate(0x82, 0x81) == 44100);
    setup(1, 11, 2, 3, 0);
    setup(1, 11, 2, UA_IF_CAP, 0);
    assert(ua.play_alt == 2 && ua.cap_alt == 2 && ep_cnt[2] == 528);
    setup(0x81, 10, 0, 3, 1);
    assert(ep_cnt[0] == 1 && ep0buf[0] == 2);
    setup(0x81, 10, 0, UA_IF_CAP, 1);
    assert(ep_cnt[0] == 1 && ep0buf[0] == 2);
    /* The shared control interface (IF2) has only alternate 0. */
    setup(0x81, 10, 0, 2, 1);
    assert(ep0_command == 0x0A && ep0buf[0] == 0);
    setup(1, 11, 0, 2, 0);
    assert(ep0_command == 0x48);
    setup(1, 11, 1, 2, 0);
    assert(ep0_command == 0x60 && ua.cap_alt == 2);
    setup(1, 11, 0, 6, 0);
    assert(ep0_command == 0x60);
    setup(0x81, 10, 0, 6, 1);
    assert(ep0_command == 0x60);
    /* The shared function and control interface use the Melodee product name. */
    setup(0x80, 6, 0x0302, 0x0409, 255);
    assert(ep_cnt[0] == 26 && ep0buf[1] == 3 &&
           !memcmp(ep0buf + 2, "M\0e\0l\0o\0d\0e\0e\0 \0F\0M\0-\0" "1\0", 24));
    setup(0x80, 6, 0x0303, 0x0409, 255);
    assert(ep0_command == 0x60);             /* retired split-device names */
    setup(0x80, 6, 0x0304, 0x0409, 255);
    assert(ep0_command == 0x60);
    setup(0x80, 6, 0x0305, 0x0409, 255);
    assert(ep0_command == 0x60);
    ua.pw = 600;
    ua.cw = 600;
    /* Fixed-rate SET_CUR does not flush either running direction. */
    pw = ua.pw;
    setup(0x22, 1, 0x100, 0x82, 3);
    rate_data(48000, 3);
    assert(ua.pw == pw && ua.cw == 600 && ep_cnt[2] == 528);
    for (uint32_t i = 0; i < ep_cnt[2]; i++) assert(((uint8_t *)ep_tadr[2])[i] == 0);
    assert(get_rate(0x82, 0x81) == 44100);
    setup(1, 11, 1, 3, 0);
    assert(ua.play_alt == 1 && ua.pw == 0);
    assert(get_rate(2, 0x81) == 44100 && ua.cap_alt == 2);
    setup(1, 11, 3, 3, 0);
    assert(ep0_command == 0x60 && ua.play_alt == 1);
    /* Reject malformed selectors, endpoints, lengths and unconfigured access. */
    setup(0x22, 1, 0x200, 2, 3); assert(ep0_command == 0x60);
    setup(0x22, 1, 0x101, 2, 3); assert(ep0_command == 0x60);
    setup(0x22, 1, 0x100, 0x83, 3); assert(ep0_command == 0x60);
    setup(0x22, 1, 0x100, 0x102, 3); assert(ep0_command == 0x60);
    setup(0x22, 1, 0x100, 2, 2); assert(ep0_command == 0x60);
    setup(0x22, 1, 0x100, 2, 3);
    rate_data(44100, 2);
    assert(ep0_command == 0x60 && !ua_rate_pending);
    assert(get_rate(2, 0x81) == 44100);
    setup(0x22, 1, 0x100, 2, 3);
    rate_data(46000, 3);                     /* UAC1 nearest-rate rounding */
    assert(get_rate(2, 0x81) == 44100);
    setup(0x22, 1, 0x100, 2, 3);
    rate_data(96000, 3);
    assert(get_rate(2, 0x81) == 44100 && ua_feedback() == UA_NOMINAL);
    /* Abandon the OUT stage, then send an unrelated request in the same poll. */
    setup(0x22, 1, 0x100, 2, 3);
    const uint8_t get_config[8] = {0x80, 8, 0, 0, 0, 0, 1, 0};
    memcpy(ep0buf, get_config, 8);
    regs[0][S_CSR0] = 0x11;
    regs[0][S_COUNT0] = 8;
    ep0_service();
    assert(!ua_rate_pending && ep_cnt[0] == 1 && ep0buf[0] == 1);
    setup(0x22, 1, 0x100, 2, 3);
    regs[0][S_CSR0] = 4;
    ep0_service();
    assert(!ua_rate_pending);               /* SentStall also cancels OUT */
    setup(0x22, 1, 0x100, 2, 3);
    common[S_INTRUSB] = 4;
    usb_poll();                            /* bus reset restores the defaults */
    assert(!ua_rate_pending && !usb.config && !ua.play_alt && !ua.cap_alt);
    setup(0x22, 1, 0x100, 2, 3);
    assert(ep0_command == 0x60);
    usb.config = 1;
    puts("USB audio EP0: formats, rates, malformed/aborted requests, bus reset: OK");
}

static uint32_t queued_cr, queued_pw, checked_sends;
static int16_t get_feature(uint8_t selector, uint8_t request, uint8_t channel)
{
    uint32_t bytes = selector == 1u ? 1u : 2u;
    setup(0xA1, request, (uint16_t)(selector << 8 | channel), 0x0502, bytes);
    assert(ep0_command == 0x0A && ep_cnt[0] == bytes);
    return bytes == 1u ? ep0buf[0] : (int16_t)(ep0buf[0] | (uint16_t)ep0buf[1] << 8);
}

static void feature_data(int16_t value, uint32_t bytes)
{
    ep0buf[0] = (uint8_t)value;
    ep0buf[1] = (uint8_t)((uint16_t)value >> 8);
    regs[0][S_CSR0] = 1;
    regs[0][S_COUNT0] = bytes;
    ep0_service();
}

static void set_feature(uint8_t selector, int16_t value)
{
    uint32_t bytes = selector == 1u ? 1u : 2u;
    setup(0x21, 1, (uint16_t)(selector << 8), 0x0502, bytes);
    assert(ua_feature_pending == selector && ep0_command == 0x40);
    feature_data(value, bytes);
    assert(ep0_command == 0x48 && !ua_feature_pending);
}

static void volume_controls(void)
{
    ua_host_volume_reset();
    assert(get_feature(2, 0x81, 0) == 0 && get_feature(1, 0x81, 0) == 0);
    assert(get_feature(2, 0x82, 0) == -64 * 256 && get_feature(2, 0x83, 0) == 0);
    assert(get_feature(2, 0x84, 0) == 256);
    set_feature(2, -6 * 256);
    assert(get_feature(2, 0x81, 0) == -6 * 256 && ua_host_target_q15 == 16423);
    assert(get_feature(2, 0x81, 0xFF) == -6 * 256); /* all available controls: only master */
    set_feature(2, -6 * 256 - 100);
    assert(get_feature(2, 0x81, 0) == -6 * 256);
    set_feature(2, -6 * 256 - 200);
    assert(get_feature(2, 0x81, 0) == -7 * 256);
    set_feature(2, 20 * 256);
    assert(get_feature(2, 0x81, 0) == 0 && ua_host_target_q15 == 32768);
    set_feature(2, -100 * 256);
    assert(get_feature(2, 0x81, 0) == -64 * 256 && ua_host_target_q15 == 21);
    set_feature(2, (int16_t)0x8000);
    assert(get_feature(2, 0x81, 0) == (int16_t)0x8000 && !ua_host_target_q15);
    set_feature(2, -6 * 256);
    set_feature(1, 1);
    assert(get_feature(1, 0x81, 0) == 1 && get_feature(2, 0x81, 0) == -6 * 256);
    set_feature(1, 0);

    /* Standard mute/volume requests must reject unsupported channels, units,
     * selectors and lengths, including malformed or abandoned data stages. */
    setup(0xA1, 0x82, 0x100, 0x0502, 1); assert(ep0_command == 0x60);
    setup(0xA1, 0x81, 0x201, 0x0502, 2); assert(ep0_command == 0x60);
    setup(0xA1, 0x81, 0x200, 0x0102, 2); assert(ep0_command == 0x60);
    setup(0xA1, 0x81, 0x200, 0x0503, 2); assert(ep0_command == 0x60);
    setup(0xA1, 0x81, 0x300, 0x0502, 2); assert(ep0_command == 0x60);
    setup(0x21, 1, 0x200, 0x0502, 3); assert(ep0_command == 0x60);
    setup(0x21, 1, 0x200, 0x0502, 2); feature_data(0, 1);
    assert(ep0_command == 0x60 && !ua_feature_pending && ua_host_volume_q8 == -6 * 256);
    setup(0x21, 1, 0x100, 0x0502, 1); feature_data(2, 1);
    assert(ep0_command == 0x60 && !ua_host_mute);
    setup(0x21, 1, 0x200, 0x0502, 2);
    const uint8_t get_config[8] = {0x80, 8, 0, 0, 0, 0, 1, 0};
    memcpy(ep0buf, get_config, sizeof get_config);
    regs[0][S_CSR0] = 0x11; regs[0][S_COUNT0] = 8;
    ep0_service();
    assert(!ua_feature_pending && ep_cnt[0] == 1 && ep0buf[0] == 1 && ua_host_volume_q8 == -6 * 256);
    setup(0x21, 1, 0x200, 0x0502, 2);
    regs[0][S_CSR0] = 4; ep0_service();
    assert(!ua_feature_pending);

    /* Apply real EP0 requests to both PCM depths and capture layouts. Neither
     * volume nor mute may alter the synth master or the four recorded stems. */
    for (uint32_t channels = 2; channels <= 4; channels += 2)
    for (uint32_t alt = 1; alt <= 2; alt++) {
        ua_host_volume_reset(); ua_reset(); ua_cap_channels = (uint8_t)channels;assert(ua_tracks_memory_prepare(channels==4u));
        ua.play_alt = ua.cap_alt = (uint8_t)alt;
        ua.pw = 800;
        for (uint32_t i = 0; i < UA_RING; i++) { ua.play[2*i] = 2000; ua.play[2*i+1] = -2000; }
        int32_t out[64], stems[128];
        for (uint32_t i = 0; i < 32; i++)
            for (uint32_t ch = 0; ch < 4; ch++) stems[i*4+ch] = (int32_t)(500 * (ch + 1));
        int32_t previous = 2000;
        for (uint32_t phase = 0; phase < 3; phase++) {
            if (!phase) set_feature(2, -6 * 256);
            else set_feature(1, phase == 1);
            assert(ua.pw == 800 && ua.play_alt == alt && ua.cap_alt == alt);
            for (uint32_t b = 0; b < 4; b++) {
                for (uint32_t i = 0; i < 32; i++) { out[2*i] = 1000; out[2*i+1] = -1000; }
                uint32_t at = ua.cw;
                ua_audio(out, stems, 32, 4096);
                for (uint32_t i = 0; i < 32; i++) {
                    int32_t sample = out[2*i] - 1000;
                    assert(sample >= 0 && sample <= 2000 && sample - previous <= 16 && previous - sample <= 16);
                    previous = sample;
                    for (uint32_t ch = 0; ch < channels; ch++)
                        assert((ch<2u?ua.cap[(at+i)*2u+ch]:ua_track_extra[(at+i)*2u+ch-2u]) == (channels == 4 ? stems[i*4+ch] : stems[i*2+ch]));   /* (stereo: the unity pairs, #103) */
                }
            }
            assert(previous == (phase == 1 ? 0 : (2000 * 16423) >> 15));
        }
        assert(ua_set_interface(UA_IF_PLAY, 0) && ua_host_volume_q8 == -6 * 256);
        set_feature(1, 1);
        assert(ua_set_interface(UA_IF_PLAY, alt) && !ua_host_gain_q15);
        setup(0, 9, 1, 0, 0);
        assert(ua_host_mute && ua_host_volume_q8 == -6 * 256); /* configuration does not lose host settings */
    }
    setup(0x21, 1, 0x200, 0x0502, 2);
    common[S_INTRUSB] = 4; usb_poll();
    assert(!ua_feature_pending && !usb.config && !ua_host_mute && !ua_host_volume_q8 && ua_host_gain_q15 == 32768);
    setup(0xA1, 0x81, 0x200, 0x0502, 2); assert(ep0_command == 0x60);
    usb.config = 1; ua_cap_channels = 4;assert(ua_tracks_memory_prepare(1));
    puts("USB host volume/mute: EP0 ranges, validation/reset, smooth PCM16/24 playback, stereo/stem capture isolation: OK");
}

static void capture_deadline(uint32_t ep)
{
    if (ep != 2u) return;
    /* Arming the prepared packet must precede either direction's PCM work.
     * Four-channel PCM24 IN transfers occupy nearly half a USB frame. */
    assert(ua.cr == queued_cr && ua.pw == queued_pw);
    checked_sends++;
}

static void capture_pipeline(void)
{
    uint8_t prepared[UA_PACKET], active[UA_PACKET];
    for (uint32_t alt = 1; alt <= 2; alt++) {
        ua_hw_stop();
        assert(ua_set_interface(3, alt) && ua_set_interface(UA_IF_CAP, alt));
        for (uint32_t i = 0; i < UA_RING; i++)
            for (uint32_t ch = 0; ch < ua_cap_channels; ch++)
                (ch<2u?ua.cap:ua_track_extra)[i*2u+(ch%2u)] = (int16_t)(1000 * (ch + 1));
        ua.cw = 800;
        checked_sends = 0;
        for (uint32_t packet = 0; packet < 6; packet++) {
            void *pending = ua_tx[ua_tx_slot];
            uint32_t bytes = ua_tx_bytes;
            memcpy(prepared, pending, bytes);
            queued_cr = ua.cr;
            queued_pw = ua.pw;
            send_check = capture_deadline;
            regs[2][S_TXCSR1] = 0;         /* host completed the previous IN */
            regs[2][S_RXCSR1] = 1;         /* simultaneous playback packet */
            regs[2][S_RXCOUNT1] = 24 * 2 * ua_sample_bytes(alt);
            regs[2][S_RXCOUNT2] = 0;
            memset(ua_rx, 0, sizeof ua_rx);
            frame(packet);
            ua_hw_poll();
            send_check = NULL;
            assert(ep_tadr[2] == pending && ep_cnt[2] == bytes);
            assert(!memcmp(prepared, ep_tadr[2], bytes));
            assert(ua.cr > queued_cr);     /* packing only after IN is armed */
            assert(ua_tx[ua_tx_slot] != ep_tadr[2] && ua_tx_bytes);
            memcpy(active, ep_tadr[2], bytes);
            queued_cr = ua.cr;
            ua_hw_poll();                 /* busy DMA and staged PCM stay intact */
            assert(ua.cr == queued_cr && !memcmp(active, ep_tadr[2], bytes));
            if (packet >= 3)
                for (uint32_t ch = 0; ch < ua_cap_channels; ch++)
                    assert(ua_decode(active + ch * ua_sample_bytes(alt), ua_sample_bytes(alt))
                           == (int16_t)(1000 * (ch + 1)));
        }
        assert(checked_sends == 6);
        assert(ua_set_interface(UA_IF_CAP, 0) && !ua_tx_bytes);
        assert(ua_set_interface(UA_IF_CAP, alt));
        for (uint32_t i = 0; i < ep_cnt[2]; i++)
            assert(((uint8_t *)ep_tadr[2])[i] == 0); /* no old prepared audio on restart */
        for (uint32_t i = 0; i < ua_tx_bytes; i++)
            assert(ua_tx[ua_tx_slot][i] == 0);
    }
    ua_hw_stop();
    puts("USB audio capture: prebuilt packets meet refill deadlines, DMA isolation, clean restart: OK");
}

static void duplex_lifecycle(void)
{
    common[S_INTRRX1E] = 2;                 /* MIDI is configured alongside audio */
    for (uint32_t alt = 1; alt <= 2; alt++)
    for (uint32_t capture_first = 0; capture_first < 2; capture_first++) {
        uint8_t first = capture_first ? UA_IF_CAP : UA_IF_PLAY;
        uint8_t second = capture_first ? UA_IF_PLAY : UA_IF_CAP;
        ua_hw_stop();
        assert(ua_set_interface(first, alt));
        if (capture_first) { ua.cw = 600; ua.cap[0] = 1234; }
        else { ua.pw = 600; ua.play[0] = 5678; }
        assert(ua_set_interface(second, alt));
        assert(ua.cap_alt == alt && ua.play_alt == alt);
        if (capture_first) assert(ua.cw == 600 && ua.cap[0] == 1234);
        else assert(ua.pw == 600 && ua.play[0] == 5678);
        /* Stopping/restarting either direction preserves the other stream. */
        ua.pw = ua.cw = 600;
        assert(ua_set_interface(UA_IF_CAP, 0));
        assert(!ua.cap_alt && ua.play_alt == alt && ua.pw == 600 && common[S_INTRRX1E] == 6);
        assert(ua_set_interface(UA_IF_CAP, alt));
        assert(ua.pw == 600 && ua.play_alt == alt);
        ua.cw = 600;
        assert(ua_set_interface(UA_IF_PLAY, 0));
        assert(!ua.play_alt && ua.cap_alt == alt && ua.cw == 600 && common[S_INTRRX1E] == 2);
        assert(ua_set_interface(UA_IF_PLAY, alt));
        assert(ua.cw == 600 && ua.cap_alt == alt);
        assert(ep_tadr[3] == ua_fb && ep_cnt[3] == 3);
        assert(ep_radr[2] == ua_rx && ep_cnt[2] && ua_tx_bytes);
    }
    ua_hw_stop();
    puts("USB duplex: either start order, independent stop/restart, feedback and prepared capture in PCM16/24: OK");
}

/* GLO > SYSTEM leaves audio directions out of the configuration: interfaces stay
 * contiguous, one shared AC header lists the retained streams, absent streams stall, and
 * with both on the host gets CFG_DESC unchanged. */
static void layouts(void)
{
    uint16_t revisions[8] = {0};
    static const struct { uint16_t len; uint8_t nif, play, cap; } L[4] = {
        {396, 5, 3, 4}, {251, 4, UA_NO_IF, 3}, {279, 4, 3, UA_NO_IF}, {109, 2, UA_NO_IF, UA_NO_IF}};
    for (uint32_t tracks = 0; tracks < 2; tracks++)
    for (uint32_t off = 0; off < 4; off++) {
        ua_tracks = ua_tracks_want = (uint8_t)tracks;
        const uint8_t *d;
        uint16_t len;
        uint32_t i, next_if = 0, iad_left = 0, ac_if = UA_NO_IF, current_if = UA_NO_IF, width = 0;
        uint32_t audio_headers = 0, audio_iads = 0, ac_bytes = 0, ac_total = 0, terminals = 0, features = 0;
        uint32_t streams = (L[off].play != UA_NO_IF) + (L[off].cap != UA_NO_IF);
        ua_off = ua_off_want = (uint8_t)off;
        usb_start();
        usb.config = 1;
        assert(get_desc(0x100, &d, &len) && len == sizeof DEV_DESC);
        uint16_t revision = (uint16_t)(d[12] | d[13] << 8);
        assert(revision >= 0x0330 && revision <= 0x0337); /* retire split-device identities */
        for (uint32_t previous = 0; previous < tracks * 4u + off; previous++)
            assert(revision != revisions[previous]);
        revisions[tracks * 4u + off] = revision;
        setup(0x80, 6, 0x0100, 0, sizeof DEV_DESC);
        assert(ep_cnt[0] == sizeof DEV_DESC && !memcmp(ep0buf, d, sizeof DEV_DESC));
        assert(usb.up && get_desc(0x200, &d, &len) && d == ua_cfg && len == ua_cfg_len);
        assert(len == L[off].len && (d[2] | d[3] << 8) == len && d[4] == L[off].nif);
        assert(ua_nif == L[off].nif && ua_if_play == L[off].play && ua_if_cap == L[off].cap);
        if (!off && tracks)
            assert(len == sizeof CFG_DESC && !memcmp(d, CFG_DESC, len));
        assert(ua_cap_channels == (tracks ? 4u : 2u));
        for (i = 9; i < len; i += d[i]) {
            const uint8_t *r = d + i;
            if (r[1] == 4) current_if = r[2];
            if (current_if == 2u && r[1] == 0x24 && r[2] == 2 && r[3] == 3)
                assert(r[7] == ua_cap_channels && r[8] == (tracks ? 0 : 3) && !r[9]);
            if (current_if == ua_if_cap && r[1] == 0x24 && r[2] == 2) {
                assert(r[4] == ua_cap_channels && (r[5] == 2 || r[5] == 3));
                assert(r[6] == 8*r[5] && r[7] == 1 && r[8] == 0x44 && r[9] == 0xAC && !r[10]);
                width = r[5];
            }
            if (current_if == ua_if_cap && r[1] == 5)
                assert((r[4] | r[5] << 8) == UA_MAX_FRAMES * ua_cap_channels * width);
            if (r[1] == 0x0B) {                 /* IAD: its interfaces follow, numbered on */
                assert(!iad_left && r[2] == next_if);
                iad_left = r[3];
                assert(r[7] == (r[2] ? 2 : 0)); /* one Melodee name for both directions */
                if (r[2] == 2u) {
                    audio_iads++;
                    assert(r[3] == streams + 1u);
                }
            } else if (r[1] == 4 && !r[3]) {    /* alternate 0: the next interface number */
                assert(r[2] == next_if++ && iad_left--);
                ac_if = r[5] == 1 && r[6] == 1 ? r[2] : UA_NO_IF;
            } else if (r[1] == 4) {
                assert(r[2] + 1u == next_if);
            } else if (r[1] == 0x24 && ac_if == 2u) {
                ac_bytes += r[0];
                if (r[2] == 1) {
                    audio_headers++;
                    assert(r[0] == 8u + streams && r[7] == streams);
                    ac_total = r[5] | r[6] << 8;
                    for (uint32_t j = 0; j < streams; j++) assert(r[8 + j] == 3u + j);
                } else if (r[2] == 6u) {
                    features++;
                    static const uint8_t feature[] = {10, 0x24, 6, 5, 1, 1, 3, 0, 0, 0};
                    assert(r[0] == sizeof feature && !memcmp(r, feature, sizeof feature));
                } else {
                    assert(r[2] == 2u || r[2] == 3u);
                    terminals |= 1u << r[3];
                    if (r[2] == 3u && r[3] == 2u) assert(r[7] == UA_FEATURE_PLAY);
                }
            }
        }
        assert(next_if == L[off].nif && !iad_left);
        assert(audio_headers == !!streams && audio_iads == !!streams && ac_bytes == ac_total);
        assert(terminals == ((L[off].play != UA_NO_IF ? 6u : 0u) | (L[off].cap != UA_NO_IF ? 24u : 0u)));
        assert(features == (L[off].play != UA_NO_IF));
        assert(!ac_total || ac_total == 8u + streams + 21u * streams + features * 10u);
        setup(0xA1, 0x81, 0x200, 0x0502, 2);
        if (L[off].play == UA_NO_IF) assert(ep0_command == 0x60);
        else assert(ep_cnt[0] == 2 && !ep0buf[0] && !ep0buf[1]);
        setup(0x80, 6, 0x0200, 0, 9);          /* what the host reads first */
        assert(ep_cnt[0] == 9 && ep0buf[2] == (len & 255) && ep0buf[3] == len >> 8 && ep0buf[4] == ua_nif);
        for (uint32_t f = 0; f < 2; f++) {
            uint8_t ifn = f ? ua_if_cap : ua_if_play, old = f ? UA_IF_CAP : UA_IF_PLAY, ep = f ? 0x82 : 0x02;
            if (ifn == UA_NO_IF) {
                if (old >= ua_nif) {
                    setup(1, 11, 1, old, 0);
                    assert(ep0_command == 0x60);
                }
                setup(0xA2, 0x81, 0x100, ep, 3);   /* its endpoint is gone too */
                assert(ep0_command == 0x60);
                setup(1, 11, 1, UA_NO_IF, 0);
                assert(ep0_command == 0x60);
                continue;
            }
            setup(1, 11, 1, ifn, 0);
            assert(ep0_command == 0x48 && (f ? ua.cap_alt : ua.play_alt) == 1);
            setup(0x81, 10, 0, ifn, 1);
            assert(ep_cnt[0] == 1 && ep0buf[0] == 1);
            assert(get_rate(ep, 0x81) == 44100);
            setup(1, 11, 0, ifn, 0);
            assert(ep0_command == 0x48 && !ua.play_alt && !ua.cap_alt);
        }
        setup(1, 11, 0, ua_nif, 0);
        assert(ep0_command == 0x60);
        for (i = 0; i < ua_nif; i++)            /* no interface number reaches an absent stream */
            setup(1, 11, 1, i, 0);
        assert(!!ua.play_alt == (ua_if_play != UA_NO_IF) && !!ua.cap_alt == (ua_if_cap != UA_NO_IF));
        ua_hw_stop();
    }
    ua_off = ua_off_want = 0;
    usb_start();
    usb.config = 1;
    puts("USB audio layouts: Out / In left out, interfaces renumbered, absent streams stall: OK");
}

/* The knob rests 0.6 s before the host sees a change; then a second off the bus. */
static void replug(void)
{
    assert(!ua_off_apply(100000));              /* nothing wanted */
    ua_off_set(UA_OFF_OUT, 0, 1000);
    ua_off_set(UA_OFF_OUT, 0, 1300);            /* already wanted: the rest timer keeps running */
    assert(ua_off_want == UA_OFF_OUT && ua_off == 0 && usb.up);
    assert(!ua_off_apply(1599) && usb.up && ua_cfg_len == 396);
    assert(ua_off_apply(1600) && ua_off == UA_OFF_OUT && !usb.up && !usb.config);
    usb_retry(2599);
    assert(!usb.up);
    usb_retry(2600);
    assert(usb.up && ua_cfg_len == 251 && ua_if_play == UA_NO_IF && ua_if_cap == 3);
    ua_off_set(UA_OFF_IN, 0, 3000);             /* through OFF and back on: no replug */
    ua_off_set(UA_OFF_IN, 1, 3200);
    assert(!ua_off_apply(9000) && usb.up);
    ua_off_set(UA_OFF_OUT, 1, 9000);
    ua_off_set(UA_OFF_IN, 0, 9100);
    assert(!ua_off_apply(9699) && ua_off_apply(9700) && ua_off == UA_OFF_IN && !usb.up);
    usb_retry(10700);
    assert(usb.up && ua_cfg_len == 279 && ua_if_play == 3 && ua_if_cap == UA_NO_IF);
    usb.detached = 1;                           /* on its way to UBOOT: stays off */
    ua_off_set(UA_OFF_IN, 1, 11000);
    assert(ua_off_apply(11600) && usb.up);
    usb.detached = 0;
    puts("USB audio on/off: debounced, replug, re-enumeration with the new layout: OK");
}

int main(void)
{
    uint32_t count;
    uint8_t previous[UA_PACKET];
    ua_tracks = ua_tracks_want = 1; ua_cap_channels = 4;assert(ua_tracks_memory_prepare(1));
    usb.up = usb.config = 1;
    common[S_INTRRX1E] = 2;                    /* MIDI endpoint must survive */
    ua_hw_stop();
    assert(common[S_INTRRX1E] == 2);
    assert(!ua_set_interface(2, 1));
    assert(!ua_set_interface(5, 1));            /* outside the duplex collection */
    assert(!ua_set_interface(3, 3));
    usb.config = 0;
    assert(!ua_set_interface(3, 1));
    usb.config = 1;
    assert(ua_set_interface(3, 1));
    assert(ua_set_interface(UA_IF_CAP, 1));
    assert(ua.play_alt && ua.cap_alt);
    /* MaxP 0xFF keeps single packet buffering (an exact MaxP doubles it and loses
     * every other OUT packet); IN packets are queued before the first IN token */
    assert(regs[2][S_RXMAXP] == 0xFF && regs[2][S_TXMAXP] == 0xFF && regs[3][S_TXMAXP] == 0xFF);
    assert(ua.tx_packets == 1 && ep_cnt[2] == 352 && ep_cnt[3] == 3);
    assert(common[S_INTRRX1E] == 6);
    assert(regs[2][S_RXCSR2] == 0x40 && regs[2][S_TXCSR2] == 0x40);   /* ISO; direction bit clear */
    assert(regs[3][S_TXCSR2] == 0x40);
    frame(2047);
    ua_hw_poll();
    assert(ep_cnt[2] == 352 && ep_cnt[3] == 3);
    assert(ep_tadr[2] == ua_tx[0] && ep_tadr[3] == ua_fb && ep_radr[2] == ua_rx);
    assert(regs[2][S_TXCSR1] == 1 && regs[3][S_TXCSR1] == 1);
    assert(ua_fb[0] == (UA_NOMINAL & 255));
    memcpy(previous, ep_tadr[2], sizeof previous);
    count = ua.tx_packets;
    ua_hw_poll();                           /* second poll in same frame */
    assert(ua.tx_packets == count);
    frame(0);                              /* frame number wraps; DMA still busy */
    ua_hw_poll();
    assert(!ua.missed_frames && ua.tx_packets == count);
    assert(memcmp(previous, ep_tadr[2], sizeof previous) == 0);
    regs[2][S_TXCSR1] = 0;                  /* host took the capture packet: refill in the same frame */
    ua_hw_poll();
    assert(ua.tx_packets == count + 1 && regs[2][S_TXCSR1] == 1 && !ua.missed_frames);
    count++;
    regs[2][S_TXCSR1] = regs[3][S_TXCSR1] = 0;
    regs[2][S_RXCSR1] = 1;
    regs[2][S_RXCOUNT1] = 176;
    memset(ua_rx, 0, sizeof ua_rx);
    frame(1);
    ua_hw_poll();
    assert(ua.pw == 44 && ua.rx_packets == 1 && ua.tx_packets == count + 1);
    assert(regs[2][S_RXCSR1] == 0);
    regs[2][S_RXCSR1] = 5;                   /* RX overrun: corrupted packet discarded */
    ua_hw_poll();
    assert(ua.pw == 44 && ua.bad_packets == 1);
    assert(ua_set_interface(3, 0));
    assert(!ua.play_alt && ua.cap_alt && ua.pw == 0);
    assert(common[S_INTRRX1E] == 2);          /* capture and MIDI remain available */
    assert(ua_set_interface(3, 1));
    usb.suspended = 1;
    ua_hw_poll();
    assert(ua.play_alt && ua.cap_alt && ua_paused && !ua_frame_valid);
    assert(regs[2][S_TXCSR1] == 0 && regs[3][S_TXCSR1] == 0);
    assert(ua.pw == ua.pr && ua.cw == ua.cr);
    count = ua.tx_packets;
    ua_hw_poll();
    assert(ua.tx_packets == count);
    usb.suspended = 0;
    frame(300);
    ua_hw_poll();
    assert(!ua_paused && ua.tx_packets == count + 1 && !ua.missed_frames);
    /* A stream that starts after an idle stretch has not missed those frames. */
    assert(ua_set_interface(3, 0) && ua_set_interface(UA_IF_CAP, 0));
    ua_hw_poll();
    assert(!ua_frame_valid);
    assert(ua_set_interface(UA_IF_CAP, 1));
    frame(1300);
    ua_hw_poll();
    assert(ua_frame_valid && !ua.missed_frames);
    frame(1302);
    ua_hw_poll();
    assert(ua.missed_frames == 1);
    ua_hw_stop();
    assert(!ua.play_alt && !ua.cap_alt && !ua_frame_valid);
    assert(common[S_INTRRX1E] == 2);
    puts("USB audio endpoints: ownership, alternate settings, suspend/reset, frame wrap: OK");
    controls();
    volume_controls();
    capture_pipeline();
    duplex_lifecycle();
    layouts();
    replug();
    ua_off = ua_off_want = 0; ua_tracks = ua_tracks_want = 0;
    usb_start(); usb.config = 1;
    assert(ua_cap_channels == 2 && !ua_tracks);
    assert(ua_set_interface(UA_IF_CAP, 2) && ep_cnt[2] == 264);
    assert(!ua_track_extra&&!ua_test_memory_bytes);
    ua.cw = 700; ua.cap[0] = 12345;
    const uint8_t *device;
    uint16_t device_len;
    assert(get_desc(0x100, &device, &device_len));
    uint16_t stereo_revision = (uint16_t)(device[12] | device[13] << 8);
    /* Failure must leave an already running stereo stream intact. */
    ua_test_memory_limit=0;ua_tracks_set(1,19000);
    assert(ua_off_apply(19600)==2&&usb.up&&usb.config&&ua.cap_alt==2&&ua.cw==700&&ua.cap[0]==12345&&!ua_tracks_want&&!ua_track_extra);
    ua_test_memory_limit=4096;
    ua_tracks_set(1, 20000); ua_tracks_set(1, 20300);
    assert((device[12] | device[13] << 8) == stereo_revision);
    assert(!ua_off_apply(20599) && ua_cap_channels == 2);
    assert(ua_off_apply(20600) && !usb.up && !usb.config && ua_cap_channels == 2);
    usb_retry(21600); usb.config = 1;
    assert(!ua.cap_alt && ua.cw == ua.cr);
    assert(ua_cap_channels == 4 && ua_set_interface(UA_IF_CAP, 2) && ep_cnt[2] == 528);
    assert(ua_track_extra&&ua_test_memory_bytes==4096u);
    assert(get_desc(0x100, &device, &device_len));
    assert((device[12] | device[13] << 8) != stereo_revision);
    for (uint32_t i = 0; i < ep_cnt[2]; i++) assert(!((uint8_t *)ep_tadr[2])[i]);
    ua_tracks_set(0, 22000); ua_tracks_set(1, 22300);
    assert(!ua_off_apply(23000) && usb.up);
    ua_tracks_set(0, 23000); assert(ua_off_apply(23600));
    usb_retry(24600); usb.config = 1;
    assert(ua_cap_channels == 2 && ua_set_interface(UA_IF_CAP, 1) && ep_cnt[2] == 176);
    assert(!ua_track_extra&&!ua_test_memory_bytes);
    assert(get_desc(0x100, &device, &device_len));
    assert((device[12] | device[13] << 8) == stereo_revision);
    capture_pipeline();
    assert(ua_test_memory.before==0x51a7beefu&&ua_test_memory.after==0xa17e5afeu);
    puts("USB input mode: stereo default, debounced live re-enumeration, no stale packets: OK");
    return 0;
}
