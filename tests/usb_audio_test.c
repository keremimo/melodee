/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "usb_audio_memory.h"
#include "../firmware/src/usb_audio_stream.c"

static void pcm(uint8_t *p, uint32_t frames, int16_t l, int16_t r)
{
    uint32_t i;
    for (i = 0; i < frames; i++) {
        p[i * 4] = (uint8_t)l;
        p[i * 4 + 1] = (uint8_t)((uint16_t)l >> 8);
        p[i * 4 + 2] = (uint8_t)r;
        p[i * 4 + 3] = (uint8_t)((uint16_t)r >> 8);
    }
}

static void block(int32_t l, int32_t r, uint32_t master, int32_t *out)
{
    uint32_t i;
    int32_t tracks[32 * UA_CAP_CHANNELS];
    for (i = 0; i < 32; i++) {
        out[2 * i] = l;
        out[2 * i + 1] = r;
        tracks[4 * i] = l;
        tracks[4 * i + 1] = r;
        tracks[4 * i + 2] = 300;
        tracks[4 * i + 3] = -700;
    }
    if (ua_cap_channels == 2u)                    /* stereo: the mixer's unity copy, L R a frame (#103) */
        for (i = 0; i < 32; i++) { tracks[2 * i] = l; tracks[2 * i + 1] = r; }
    ua_audio(out, tracks, 32, master);
}

static void routing(void)
{
    uint8_t packet[UA_PACKET];
    int32_t out[64];
    uint32_t i;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.play_alt = ua.cap_alt = 1;
    pcm(packet, 32, 2000, -2000);
    for (i = 0; i < 16; i++)
        ua_receive(packet, 128);
    block(1000, -1000, 4096, out);
    assert(out[0] == 3000 && out[1] == -3000);
    assert(ua.cap[0] == 1000 && ua.cap[1] == -1000); /* no playback loopback */
    assert(ua_track_extra[0] == 300 && ua_track_extra[1] == -700);
    uint32_t written=ua.cw;
    for(i=0;i<32u;i++){out[2u*i]=1000;out[2u*i+1u]=-1000;}
    ua_audio(out,0,32,4096);
    assert(ua.cw==written && out[0]==3000 && out[1]==-3000);
    block(1000, -1000, 0, out);
    assert(out[0] == 1000 && out[1] == -1000);
    assert(ua.cap[64] == 1000 && ua_track_extra[65] == -700); /* MASTER does not scale stems */
    block(32000, -32000, 4096, out);
    assert(out[0] == 32767 && out[1] == -32768);
    ua.play_alt = 0;
    block(1000, -1000, 4096, out);
    assert(out[0] == 1000 && out[1] == -1000);
    ua_reset();
    assert(!ua.play_alt && !ua.cap_alt && ua.pw == ua.pr && ua.cw == ua.cr);
}

/* Model a host honoring feedback every 16 ms, with independent I2S clock and
 * the real 256-frame render bursts. Exercise both clock drift directions and
 * each stream alone: playback feedback cannot depend on capture being open. */
static void drift(int ppm, int play, int cap)
{
    uint8_t packet[UA_PACKET], captured[UA_PACKET];
    int32_t out[64];
    uint64_t device_phase = 0;
    uint32_t host_phase = 0, feedback = UA_NOMINAL, ms, i, bytes;
    uint32_t pw = play == 2 ? 3u : 2u, cw = cap == 2 ? 3u : 2u;
    uint64_t step = (uint64_t)UA_RATE * (1000000 + ppm);
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.play_alt = play;
    ua.cap_alt = cap;
    for (ms = 0; ms < 120000; ms++) {
        device_phase += step;
        while (device_phase >= 256ull * 1000000000ull) {
            device_phase -= 256ull * 1000000000ull;
            for (i = 0; i < 8; i++) {
                block(1000, -1000, 4096, out);
                if (play && ms > 1000)
                    assert(out[0] == 3000 && out[1] == -3000);
            }
        }
        if (play) {
            host_phase += feedback;
            for (i = 0; i < host_phase >> 14; i++) {
                ua_encode(packet + i * pw * 2u, pw, 2000);
                ua_encode(packet + i * pw * 2u + pw, pw, -2000);
            }
            ua_receive(packet, (host_phase >> 14) * pw * 2u);
            host_phase &= 16383;
        }
        ua_sof();
        if (play && ms % 16u == 0)
            feedback = ua_feedback();
        if (cap) {
            bytes = ua_transmit(captured);
            assert(bytes <= 45u * cw * ua_cap_channels && bytes >= 43u * cw * ua_cap_channels && bytes % (cw * ua_cap_channels) == 0);
            if (ms > 1000)
                for (i = 0; i < bytes; i += cw * ua_cap_channels) {
                    assert(ua_decode(captured + i, cw) == 1000);
                    assert(ua_decode(captured + i + cw, cw) == -1000);
                    if (ua_cap_channels == 4u) {
                        assert(ua_decode(captured + i + cw * 2u, cw) == 300);
                        assert(ua_decode(captured + i + cw * 3u, cw) == -700);
                    }
                }
        }
        assert(ua.pw - ua.pr <= UA_RING && ua.cw - ua.cr <= UA_RING);
    }
    assert(!ua.play_underruns && !ua.play_overruns);
    assert(!ua.cap_underruns && !ua.cap_overruns && !ua.bad_packets);
    printf("USB audio: 120 s at 44100 Hz, drift %+d ppm, playback %d capture %d: OK\n",
           ppm, play, cap);
}

static void recovery(void)
{
    uint8_t packet[UA_PACKET];
    int32_t out[64];
    uint32_t i;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.play_alt = ua.cap_alt = 1;
    ua_receive(packet, UA_PACKET + 4);
    ua_receive(packet, 3);
    assert(ua.bad_packets == 2 && ua.pw == 0);
    pcm(packet, 32, 2000, -2000);
    for (i = 0; i < 33; i++)
        ua_receive(packet, 128);
    assert(ua.play_overruns == 1 && !ua.play_ready);
    for (i = 0; i < 16; i++)
        ua_receive(packet, 128);
    for (i = 0; i < 18; i++)
        block(1000, -1000, 4096, out);
    assert(ua.play_underruns == 1 && out[0] == 1000);
    for (i = 0; i < 16; i++)
        ua_receive(packet, 128);
    block(1000, -1000, 4096, out);
    assert(out[0] == 3000);                     /* playback re-primes */
    for (i = 0; i < 33; i++)
        block(1000, -1000, 4096, out);
    assert(ua.cap_overruns > 0);                /* unpolled capture stays bounded */
    ua_cap_reset();
    uint32_t bytes = ua_transmit(packet);
    for (i = 0; i < bytes; i++)
        assert(packet[i] == 0);                 /* no stale audio after restart */
    /* Unsigned ring indices must survive long-running streams. */
    ua_play_reset();
    ua.pw = ua.pr = 0xFFFFFF00u;
    pcm(packet, 32, 2000, -2000);
    for (i = 0; i < 16; i++)
        ua_receive(packet, 128);
    block(1000, -1000, 4096, out);
    assert(out[0] == 3000 && ua.pw - ua.pr == 480);
}

static void pcm24(void)
{
    static const uint8_t samples[][3] = {
        {0xFF, 0xFF, 0x7F}, {0, 0, 0x80}, {0xFF, 0xFF, 0xFF},
        {0xFF, 0, 0}, {0, 0x34, 0x12}, {0, 0xCC, 0xED}
    };
    static const int16_t values[] = {32767, -32768, -1, 0, 0x1234, -0x1234};
    uint8_t packet[UA_PACKET];
    int32_t out[64];
    ua_reset();
    ua.play_alt = ua.cap_alt = 2;
    for (uint32_t i = 0; i < 6; i++) {
        memcpy(packet + i * 6u, samples[i], 3);
        memcpy(packet + i * 6u + 3u, samples[i], 3);
    }
    ua_receive(packet, 36);
    for (uint32_t i = 0; i < 6; i++)
        assert(ua.play[2u * i] == values[i] && ua.play[2u * i + 1u] == values[i]);
    uint32_t bad = ua.bad_packets;
    ua_receive(packet, UA_PLAY_PACKET - 1);
    ua_receive(packet, UA_PLAY_PACKET + 6);
    assert(ua.bad_packets == bad + 2u && ua.pw == 6);
    ua_receive(packet, 0);                   /* legal empty isochronous packet */
    for (uint32_t i = 0; i < 16; i++) block(-32768, 32767, 0, out);
    uint32_t n = ua_transmit(packet);
    assert(n == 528);
    for (uint32_t i = 0; i < n; i += 12u) {
        assert(packet[i] == 0 && packet[i + 1u] == 0 && packet[i + 2u] == 0x80);
        assert(packet[i + 3u] == 0 && packet[i + 4u] == 0xFF && packet[i + 5u] == 0x7F);
        assert(packet[i + 6u] == 0 && ua_decode(packet + i + 6u, 3) == 300);
        assert(packet[i + 9u] == 0 && ua_decode(packet + i + 9u, 3) == -700);
    }
    /* Maximum PCM24 packet fits, including across unsigned ring-index wrap. */
    ua_play_reset();
    ua.pw = ua.pr = 0xFFFFFFF0u;
    memset(packet, 0, sizeof packet);
    ua_receive(packet, UA_PLAY_PACKET);
    assert(ua.pw - ua.pr == 45 && ua.bad_packets == bad + 2u);
    ua_cap_reset();
    ua.cw = ua.cr = 0xFFFFFFF0u;
    for (uint32_t i = 0; i < 16; i++) block(1234, -2345, 0, out);
    ua.cap_fill_q8 = (UA_TARGET + 512u) * 256u;
    ua.cap_frac = 16383;
    uint8_t guarded[UA_PACKET + 2];
    memset(guarded, 0xA5, sizeof guarded);
    assert(ua_transmit(guarded + 1) == UA_PACKET);
    assert(guarded[0] == 0xA5 && guarded[UA_PACKET + 1] == 0xA5);
    assert(ua.cw - ua.cr < UA_TARGET);
}

struct measurement { double re, im, energy; uint32_t count, used; };

static void measure(struct measurement *m, int32_t sample, double frequency, uint32_t rate)
{
    double phase = 6.283185307179586 * frequency * m->count++ / rate;
    if (m->count < 256) return;              /* exclude startup */
    m->re += sample * cos(phase);
    m->im += sample * sin(phase);
    m->energy += (double)sample * sample;
    m->used++;
}

/* Exercise actual packet -> ring -> engine and engine -> ring -> packet paths.
 * Correlation checks pitch as well as level at both USB sample widths. */
static void tone(int capture, double frequency, uint32_t channel, uint32_t alt)
{
    uint8_t packet[UA_PACKET];
    int32_t out[96];
    int32_t tracks[32 * UA_CAP_CHANNELS] = {0};
    struct measurement m = {0};
    uint32_t width = ua_sample_bytes(alt);
    ua_reset();
    ua.play_alt = capture ? 0 : alt;
    ua.cap_alt = capture ? alt : 0;
    for (uint32_t input = 0; input < UA_RATE;) {
        uint32_t n = 32u;
        if (n > UA_RATE - input) n = UA_RATE - input;
        for (uint32_t i = 0; i < n; i++) {
            int16_t sample = (int16_t)lrint(10000 * sin(6.283185307179586 * frequency * input++ / UA_RATE));
            if (capture) tracks[i * 4u + channel] = sample;
            else {
                ua_encode(packet + i * 2u * width, width, sample);
                ua_encode(packet + (i * 2u + 1u) * width, width, sample);
            }
        }
        if (capture) {
            memset(out, 0, sizeof out);
            ua_audio(out, tracks, n, 0);
            while (ua.cw - ua.cr >= UA_TARGET + 64u) {
                uint32_t bytes = ua_transmit(packet);
                for (uint32_t i = 0; i < bytes; i += 4u * width) {
                    measure(&m, ua_decode(packet + i + channel * width, width), frequency, UA_RATE);
                    for (uint32_t ch = 0; ch < 4; ch++)
                        if (ch != channel) assert(ua_decode(packet + i + ch * width, width) == 0);
                }
            }
        } else {
            ua_receive(packet, n * 2u * width);
            while (ua.pw - ua.pr >= UA_TARGET + 32u) {
                block(0, 0, 4096, out);
                for (uint32_t i = 0; i < 32u; i++)
                    measure(&m, out[i * 2u], frequency, UA_RATE);
            }
        }
    }
    if (capture)
        assert(ua.cr == m.count);
    else
        assert(ua.pw == 44100);
    double gain = 2 * sqrt(m.re * m.re + m.im * m.im) / m.used / 10000;
    double rms = sqrt(2 * m.energy / m.used) / 10000;
    assert(gain > 0.995 && gain < 1.005);
    assert(rms > 0.995 && rms < 1.005);
    printf("USB audio native PCM%u: %s ch%u %.0f Hz, gain %.5f, RMS %.5f: OK\n",
           width * 8u, capture ? "capture" : "playback", channel + 1, frequency, gain, rms);
}

int main(void)
{
    ua_cap_channels = 4;
    assert(ua_tracks_memory_prepare(1));
    routing();
    recovery();
    pcm24();
    for (int alt = 1; alt <= 2; alt++) {
        tone(0, 1000, 0, alt);
        tone(0, 10000, 0, alt);
        for (uint32_t ch = 0; ch < 4; ch++) {
            tone(1, 1000, ch, alt);
            tone(1, 10000, ch, alt);
        }
        drift(-1000, alt, alt);
        drift(0, alt, alt);
        drift(1000, alt, alt);
        drift(-1000, alt, 0);
        drift(1000, 0, alt);
    }
    drift(1000, 2, 1);
    drift(-1000, 1, 2);
    ua_cap_channels = 2;
    assert(ua_tracks_memory_prepare(0));
    ua_reset(); ua.cap_alt = 1;
    int32_t master[64];
    for (uint32_t i = 0; i < 32; i++) {master[2*i] = 32767; master[2*i+1] = -32768;}
    ua_audio(master, 0, 32, 4096);
    assert(ua.cw == 32 && ua.cap[0] == 32767 && ua.cap[1] == -32768);
    for (int alt = 1; alt <= 2; alt++) {
        drift(-1000, alt, alt);
        drift(0, alt, alt);
        drift(1000, alt, alt);
        drift(1000, 0, alt);
    }
    puts("USB stereo/master and track routing, drift, packet bounds and recovery: OK");
    assert(ua_test_memory.before==0x51a7beefu&&ua_test_memory.after==0xa17e5afeu);
    return 0;
}
