/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* UAC1 stereo playback and selectable stereo master / four-track capture.
 * Callers serialize USB service and each short audio block. Capture precedes
 * computer playback, so DAW monitoring cannot feed itself through capture.
 *
 * The I2S clock is independent of USB SOF. A low-pass ring-fill servo adjusts
 * capture packet lengths and the playback endpoint's explicit 10.14 feedback.
 * USB and I2S both run at the native 44.1 kHz rate; no resampler is needed. */
#include <stdint.h>
#define UA_RATE 44100u
#define UA_CAP_CHANNELS 4u                    /* maximum; active stride follows the enumerated mode */
static uint8_t ua_cap_channels = 2u;
#define UA_MAX_FRAMES 45u                      /* ceil(44.1 + maximum feedback correction) */
#define UA_PLAY_PACKET (UA_MAX_FRAMES * 2u * 3u)
#define UA_PACKET (UA_MAX_FRAMES * UA_CAP_CHANNELS * 3u)
#define UA_RING 1024u
#define UA_TARGET 512u
#define UA_NOMINAL ((UA_RATE * 16384u) / 1000u)
#ifdef __APPLE__
#define UA_POOL                                 /* (the host tests: Mach-O has no such section) */
#else
#define UA_POOL __attribute__((section(".pool")))   /* fixed stereo rings, 12 KiB; extra stems use the arena */
#endif

/* The rings keep a sample as its top 16 bits (Q15: play / cap, what PCM16 sends, and the Record kit's take) and its
 * low 8 (play_lo / cap_lo): with them PCM24 carries the bus (Q15 << MIX_FRAC, mix.c) both ways */
static struct {
    uint8_t play_alt, cap_alt, play_ready, cap_ready;
    uint32_t pw, pr, cw, cr, cap_frac;
    int32_t play_fill_q8, cap_fill_q8;
    uint32_t play_underruns, play_overruns, cap_underruns, cap_overruns, bad_packets;
    uint32_t rx_packets, tx_packets, missed_frames;
    uint32_t poll_max_ticks, service_max_ticks;
    int16_t play[UA_RING * 2u], cap[UA_RING * 2u];
    uint8_t play_lo[UA_RING * 2u], cap_lo[UA_RING * 2u];
} ua UA_POOL;
/* Stereo is always available. Four-track mode obtains a second stereo-sized
 * bank (its 16-bit halves, then its low bytes) before re-enumerating; it retains the same 1,024-frame jitter margin.
 * The main loop prepares/releases it with audio and USB interrupts guarded.
 * TIMER5 and per-sample paths never allocate or resize. */
#define UA_TRACKS_BYTES (UA_RING * 2u * (sizeof(int16_t) + 1u))
static int16_t *ua_track_extra;
static uint8_t *ua_track_extra_lo;
static int ua_tracks_memory_prepare(int enabled)
{
    int16_t *p=UA_TRACKS_MEMORY(enabled?UA_TRACKS_BYTES:0u);
    if(enabled&&!p)return 0;
    ua_track_extra=p;ua_track_extra_lo=p?(uint8_t *)(p+UA_RING*2u):0;return 1;
}

#define UA_VOLUME_MIN (-64 * 256)
/* Standard UAC1 volume: signed 1/256 dB, -64..0 dB in one-dB steps.
 * Q15 gain table: round(32768 * 10^(-dB/20)); no floating point in USB service. */
static const uint16_t ua_volume_gain[65] = {
    32768, 29205, 26029, 23198, 20675, 18427, 16423, 14637,
    13045, 11627, 10362, 9235, 8231, 7336, 6538, 5827,
    5193, 4629, 4125, 3677, 3277, 2920, 2603, 2320,
    2068, 1843, 1642, 1464, 1305, 1163, 1036, 924,
    823, 734, 654, 583, 519, 463, 413, 368,
    328, 292, 260, 232, 207, 184, 164, 146,
    130, 116, 104, 92, 82, 73, 65, 58,
    52, 46, 41, 37, 33, 29, 26, 23,
    21,
};
#define UA_VOLUME_DEFAULT 50u                  /* USB VOLUME: the old level at the default MASTER (0.5) */
static uint8_t settings_usb_volume = UA_VOLUME_DEFAULT;   /* the menu's USB VOLUME, 0..100 % (settings_persist.c) */
static int16_t ua_host_volume_q8;
static uint8_t ua_host_mute;
static int32_t ua_host_target_q15 = 32768, ua_host_gain_q15 = 32768;

static void ua_host_volume_reset(void)
{
    ua_host_volume_q8 = ua_host_mute = 0;
    ua_host_target_q15 = ua_host_gain_q15 = 32768;
}

static void ua_host_volume_set(int16_t volume)
{
    if (volume == (int16_t)0x8000) {         /* UAC1 CUR's explicit -infinity code */
        ua_host_volume_q8 = volume;
        ua_host_target_q15 = 0;
        return;
    }
    int32_t attenuation = volume >= 0 ? 0 : volume <= UA_VOLUME_MIN ? 64 : (-(int32_t)volume + 128) / 256;
    ua_host_volume_q8 = (int16_t)(-attenuation * 256);
    ua_host_target_q15 = ua_volume_gain[attenuation];
}

static int32_t ua_clip(int32_t x)
{
    return x > 32767 ? 32767 : x < -32768 ? -32768 : x;
}

/* a bus sample (Q15 << MIX_FRAC) to PCM24's range */
static int32_t ua_clip24(int32_t x)
{
    return x > 8388607 ? 8388607 : x < -8388608 ? -8388608 : x;
}

static uint32_t ua_sample_bytes(uint8_t alt) { return alt == 2u ? 3u : 2u; }

/* Packed little-endian PCM24 uses the same full-scale level as PCM16: a PCM24 sample is the bus's (mix.c, Q15 << 8),
 * a PCM16 one its top 16 bits */
#define UA_LO 8                                         /* PCM24's bits below PCM16 */
#ifdef MIX_FRAC
_Static_assert(MIX_FRAC == UA_LO, "PCM24 carries the bus as it is");
#endif
static int32_t ua_decode(const uint8_t *p, uint32_t width)
{
    if (width == 3u)
        return (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;
    return (int16_t)(p[0] | (uint16_t)p[1] << 8) * (1 << UA_LO);
}

/* a ring's sample (hi: its top 16 bits, lo: the 8 below) into a packet */
static void ua_encode(uint8_t *p, uint32_t width, int16_t hi, uint8_t lo)
{
    if (width == 3u)
        *p++ = lo;
    p[0] = (uint8_t)hi;
    p[1] = (uint8_t)((uint16_t)hi >> 8);
}

/* one lane of a block (x[0], x[stride], ..: bus samples) into a capture ring's side (0 L, 1 R) from frame cw, at the
 * stream's depth: PCM24 its top 16 and low 8 bits; PCM16 the top 16 alone, rounded with +-1 LSB of TPDF dither (as
 * dsp.c dith_cut cuts, #100: a fading tail's error stays a steady floor; a sample already on the 16-bit grid, 0 too,
 * goes exact), low byte 0. One loop a lane: the copy runs with USB service excluded */
static uint32_t ua_dith = 0x2545F491u;
static __attribute__((noinline)) void ua_cap_lane(int16_t *hi, uint8_t *lo, uint32_t cw, uint32_t side, const int32_t *x,
                                                  uint32_t stride, uint32_t n)
{
    uint32_t i, r = ua_dith;
    for (i = 0; i < n; i++, x += stride) {
        uint32_t at = ((cw + i) & (UA_RING - 1u)) * 2u + side;
        int32_t v = ua_clip24(*x);
        if (ua.cap_alt == 2u) {
            hi[at] = (int16_t)(v >> UA_LO);
            lo[at] = (uint8_t)v;
        } else {
            int32_t d;
            r = r * 1664525u + 1013904223u;
            d = (int32_t)(r >> 24) - (int32_t)((r >> 16) & 255u) + (1 << (UA_LO - 1));
            hi[at] = (int16_t)ua_clip((v + (d & -(int32_t)((v & ((1 << UA_LO) - 1)) != 0))) >> UA_LO);
            lo[at] = 0;
        }
    }
    ua_dith = r;
}

static void ua_play_reset(void)
{
    ua.pw = ua.pr = 0;
    ua.play_ready = 0;
    ua_host_gain_q15 = ua_host_mute ? 0 : ua_host_target_q15;
    ua.play_fill_q8 = UA_TARGET * 256;
}

static void ua_cap_reset(void)
{
    ua.cw = ua.cr = ua.cap_frac = 0;
    ua.cap_ready = 0;
    ua.cap_fill_q8 = UA_TARGET * 256;
}

static void ua_reset(void)
{
    ua.play_alt = ua.cap_alt = 0;
    ua_play_reset();
    ua_cap_reset();
}

static void ua_receive(const uint8_t *p, uint32_t bytes)
{
    uint32_t i, width = ua_sample_bytes(ua.play_alt), frame = width * 2u;
    uint32_t n = bytes / frame;
    if (!ua.play_alt)
        return;
    if (n > UA_MAX_FRAMES || bytes % frame) {
        ua.bad_packets++;
        return;
    }
    if (ua.pw - ua.pr + n > UA_RING) {
        ua.play_overruns++;
        ua_play_reset();                       /* re-prime, never replay stale data */
    }
    for (i = 0; i < n; i++) {
        uint32_t at = (ua.pw & (UA_RING - 1u)) * 2u;
        int32_t l = ua_decode(p + frame * i, width), r = ua_decode(p + frame * i + width, width);
        ua.play[at] = (int16_t)(l >> UA_LO);
        ua.play[at + 1u] = (int16_t)(r >> UA_LO);
        ua.play_lo[at] = (uint8_t)l;
        ua.play_lo[at + 1u] = (uint8_t)r;
        ua.pw++;
    }
    ua.rx_packets++;
}

/* Called once per observed USB frame, not once per poll or audio callback. */
static void ua_sof(void)
{
    if (ua.play_ready)
        ua.play_fill_q8 += ((int32_t)(ua.pw - ua.pr) * 256 - ua.play_fill_q8) / 32;
    if (ua.cap_ready)
        ua.cap_fill_q8 += ((int32_t)(ua.cw - ua.cr) * 256 - ua.cap_fill_q8) / 32;
}

static uint32_t ua_rate(int32_t error_q8)
{
    int32_t correction = error_q8 / 16;         /* fill error / 1024, in 10.14 */
    if (correction > 8192)
        correction = 8192;
    if (correction < -8192)
        correction = -8192;
    return (uint32_t)((int32_t)UA_NOMINAL + correction);
}

static uint32_t ua_feedback(void)
{
    return ua_rate(UA_TARGET * 256 - ua.play_fill_q8);
}

static uint32_t ua_transmit(uint8_t *p)
{
    uint32_t i, n, take = 0, width = ua_sample_bytes(ua.cap_alt);
    ua.cap_frac += ua_rate(ua.cap_fill_q8 - UA_TARGET * 256);
    n = ua.cap_frac >> 14;
    ua.cap_frac &= 16383u;
    if (!ua.cap_ready && ua.cw - ua.cr >= UA_TARGET)
        ua.cap_ready = 1;
    if (ua.cap_ready) {
        if (ua.cw - ua.cr >= n)
            take = 1;
        else {
            ua.cap_underruns++;
            ua_cap_reset();
        }
    }
    for (i = 0; i < n; i++) {
        int16_t sample[UA_CAP_CHANNELS] = {0};
        uint8_t low[UA_CAP_CHANNELS] = {0};
        uint32_t ch;
        if (take) {
            uint32_t at = (ua.cr++ & (UA_RING - 1u)) * 2u;
            sample[0]=ua.cap[at];sample[1]=ua.cap[at+1u];low[0]=ua.cap_lo[at];low[1]=ua.cap_lo[at+1u];
            if(ua_cap_channels==4u && ua_track_extra){
                sample[2]=ua_track_extra[at];sample[3]=ua_track_extra[at+1u];
                low[2]=ua_track_extra_lo[at];low[3]=ua_track_extra_lo[at+1u];
            }
        }
        for (ch = 0; ch < ua_cap_channels; ch++)
            ua_encode(p + (i * ua_cap_channels + ch) * width, width, sample[ch], low[ch]);
    }
    return n * width * ua_cap_channels;
}

#if MELODEE_RECORDS
/* A take from the computer's or phone's playback (Record kit, rec_ui.c: its synth makes it a pad's, engine_t.rec_take):
 * armed, it keeps the last KT_PRE samples; the
 * first above KT_ON starts the take with them in front; KT_END samples below KT_QUIET in a row, or a full buffer,
 * end it. Mono, before the host's volume: its slider does not set the take's level. */
enum { KT_OFF, KT_ARMED, KT_REC, KT_DONE };
#define KT_PRE 128u                                   /* ~3 ms before the trigger */
#define KT_ON 328                                     /* -40 dBFS */
#define KT_QUIET 33                                   /* -60 dBFS */
#define KT_END (FS * 15u / 100u)
_Static_assert(KT_ON == MOD3_TAKE_ON && KT_PRE == MOD3_TAKE_PRE, "the take as a synth's rec_take expects it");
static struct {
    int16_t *buf;                                     /* RES_KIT_TAKE */
    uint32_t cap, n, quiet, pw;                       /* its samples (64 zeros kept after them); taken; below */
    volatile uint8_t state;
} kt;
static void kt_reverse(int16_t *a, uint32_t i, uint32_t j)
{
    while (i + 1u < j) { int16_t t = a[i]; a[i++] = a[--j]; a[j] = t; }
}
static void kt_hear(uint32_t pr, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t pi = ((pr + i) & (UA_RING - 1u)) * 2u;
        int32_t s = (ua.play[pi] + ua.play[pi + 1u]) >> 1, a = s < 0 ? -s : s;
        if (kt.state == KT_ARMED) {
            kt.buf[kt.pw++ % KT_PRE] = (int16_t)s;
            if (a < KT_ON) continue;
            kt.n = kt.pw < KT_PRE ? kt.pw : KT_PRE;
            if (kt.pw > KT_PRE) {                     /* the ring in order, the oldest first */
                uint32_t k = kt.pw % KT_PRE;
                kt_reverse(kt.buf, 0, k); kt_reverse(kt.buf, k, KT_PRE); kt_reverse(kt.buf, 0, KT_PRE);
            }
            kt.quiet = 0;
            kt.state = KT_REC;
        } else if (kt.state == KT_REC) {
            kt.buf[kt.n++] = (int16_t)s;
            kt.quiet = a < KT_QUIET ? kt.quiet + 1u : 0u;
            if (kt.n >= kt.cap || kt.quiet >= KT_END) { kt.state = KT_DONE; return; }
        } else
            return;
    }
}
#endif

/* Stereo post-FX/limiter mix, or four mono stems, both before MASTER and on the bus (Q15 << 8, PCM24's scale): the
 * volume pot never sets the recording's level (#103). Stereo reads the mixer's
 * unity copy (tracks: L, R per frame), or the output for a block collected while
 * capture was off. Playback has its own level (volume_q12: USB VOLUME, apart from
 * MASTER, as the click) and the host's standard volume/mute controls, with a short
 * gain ramp.
 * Call in blocks of CTL (32), with USB service excluded during this copy.
 * In track mode a null stem pointer skips capture for a block collected while
 * capture was disabled. Stereo requires no stems or additional DSP pass. */
static void ua_audio(int32_t *out, const int32_t *tracks, uint32_t n, uint32_t volume_q12)
{
    uint32_t i, capture = ua.cap_alt && (ua_cap_channels == 2u || (tracks && ua_track_extra)), playback = 0;
    int32_t gain = ua_host_gain_q15, target = ua_host_mute ? 0 : ua_host_target_q15;
    uint32_t play_gain_q15 = (volume_q12 * (uint32_t)gain) >> 12;
    if (capture && ua.cw - ua.cr + n > UA_RING) {
        ua.cap_overruns++;
        ua_cap_reset();
    }
    if (ua.play_alt) {
        if (!ua.play_ready && ua.pw - ua.pr >= UA_TARGET)
            ua.play_ready = 1;
        if (ua.play_ready) {
            if (ua.pw - ua.pr >= n)
                playback = 1;
            else {
                ua.play_underruns++;
                ua_play_reset();
            }
        }
    }
    if (capture) {                           /* (before the playback below: DAW monitoring never feeds the capture) */
        if (ua_cap_channels == 2u) {
            const int32_t *x = tracks ? tracks : out;   /* L, R a frame either way */
            ua_cap_lane(ua.cap, ua.cap_lo, ua.cw, 0, x, 2u, n);
            ua_cap_lane(ua.cap, ua.cap_lo, ua.cw, 1u, x + 1, 2u, n);
        } else {
            ua_cap_lane(ua.cap, ua.cap_lo, ua.cw, 0, tracks, 4u, n);
            ua_cap_lane(ua.cap, ua.cap_lo, ua.cw, 1u, tracks + 1, 4u, n);
            ua_cap_lane(ua_track_extra, ua_track_extra_lo, ua.cw, 0, tracks + 2, 4u, n);
            ua_cap_lane(ua_track_extra, ua_track_extra_lo, ua.cw, 1u, tracks + 3, 4u, n);
        }
    }
    for (i = 0; i < n && playback; i++) {
        uint32_t pi = ((ua.pr + i) & (UA_RING - 1u)) * 2u;
        int32_t l = out[2u * i], r = out[2u * i + 1u];
        if (gain != target) {                /* at most 128 samples (~3 ms) for a full-scale change */
            int32_t delta = target - gain;
            gain += delta > 256 ? 256 : delta < -256 ? -256 : delta;
            play_gain_q15 = (volume_q12 * (uint32_t)gain) >> 12;
        }
        /* the computer's sample, its PCM24 low byte too, on the bus */
        l += (int32_t)(((int64_t)(ua.play[pi] * (1 << UA_LO) + ua.play_lo[pi]) * (int32_t)play_gain_q15) >> 15);
        r += (int32_t)(((int64_t)(ua.play[pi + 1u] * (1 << UA_LO) + ua.play_lo[pi + 1u]) * (int32_t)play_gain_q15) >> 15);
        out[2u * i] = ua_clip24(l);
        out[2u * i + 1u] = ua_clip24(r);
    }
    if (capture)
        ua.cw += n;
#if MELODEE_RECORDS
    if (playback && kt.state)
        kt_hear(ua.pr, n);
    else if (kt.state == KT_REC && (kt.quiet += n) >= KT_END)   /* the stream stopped (a player closes it right after */
        kt.state = KT_DONE;                                       /* the sound): its silence ends the take as well */
#endif
    if (playback)
        ua.pr += n;
    ua_host_gain_q15 = playback ? gain : target; /* opening a muted stream never starts at unity */
}
