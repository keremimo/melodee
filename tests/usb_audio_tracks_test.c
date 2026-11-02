/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Real render path (mix.c mix_part -> track_capture -> usb_audio_stream.c ua_audio): Melodee's four input channels are
 * the four tracks, each isolated, after DIST, the SLICER and LEVEL, independent of the monitor's pan, sends and
 * MASTER; a silent block clears its channel. */
#include <assert.h>
#define MELODEE_USB_AUDIO 1
#define main hostsim_main
#include "hostsim.c"
#undef main

struct result { uint64_t hash, energy, monitor, taken; };   /* taken: the stereo capture's energy */

static struct result render(uint32_t channel, int sliced, int variant)
{
    int fd[2], status;
    struct result result = {2166136261u, 0, 0};
    assert(pipe(fd) == 0);
    pid_t child = fork();                     /* fresh DSP state for each comparison */
    assert(child >= 0);
    if (!child) {
        int32_t out[CTL * 2];
        track_t *t = &trk[channel];
        close(fd[0]);
        usb.up=usb.config=1;usb.suspended=0;ua.cap_alt=1;
        ua_tracks=ua_tracks_want=1;ua_off=0;ua_cfg_build();assert(ua_tracks_memory_prepare(ua_tracks&&!(ua_off&UA_OFF_IN)));
        host_tracks_init();
        for (uint32_t k = 0; k < NPART; k++) host_preset(&trk[k], HOST_SYNTH, 0);
        t->p[P_RESERVED33] = 60;
        t->p[P_RESERVED45] = sliced ? 2 : 0;
        t->p[P_RESERVED46] = 1;
        t->p[P_RESERVED48] = 127;
        t->p[P_PAN] = variant == 1 ? -64 : variant == 2 ? 64 : 0;
        t->p[P_RESERVED34] = t->p[P_RESERVED35] = t->p[P_RESERVED36] = variant == 1 ? 127 : 0;
        song.master_q12 = variant == 2 ? 0 : 4096;
        if (variant == 3)
            t->p[P_LEVEL] = 0;
        trk_note_on(t, 60, 100);
        for (uint32_t block = 0; block < 2048; block++) {
            mix_block(out, CTL);
            ua.cw = ua.cr = 0;
            ua_audio(out, track_capture, CTL, song.master_q12);
            for (uint32_t i = 0; i < CTL; i++) {
                for (uint32_t ch = 0; ch < NPART; ch++) {
                    int32_t x = track_capture[i * NPART + ch];
                    assert((ch<2u?ua.cap[i*2u+ch]:ua_track_extra[i*2u+ch-2u]) == ua_clip(x));
                    if (ch != channel) assert(x == 0);
                    else {
                        result.hash = (result.hash ^ (uint32_t)x) * 16777619u;
                        result.energy += (int64_t)x * x;
                    }
                }
                result.monitor += (int64_t)out[i * 2] * out[i * 2];
                result.monitor += (int64_t)out[i * 2 + 1] * out[i * 2 + 1];
            }
        }
        memset(trk, 0, sizeof trk);trk_voices_bind();
        mix_block(out, CTL);
        for (uint32_t i = 0; i < CTL * NPART; i++) assert(track_capture[i] == 0);
        assert(write(fd[1], &result, sizeof result) == sizeof result);
        _exit(0);
    }
    close(fd[1]);
    assert(read(fd[0], &result, sizeof result) == sizeof result);
    close(fd[0]);
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return result;
}

/* Compare every captured sample with the real finished mixer output. Separate
 * processes keep effect tails and limiter history identical between variants. */
static struct result stereo_render(int insert, int send, uint32_t master)
{
    int fd[2], status;
    struct result result = {2166136261u, 0, 0};
    assert(pipe(fd) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        int32_t out[CTL * 2];
        close(fd[0]);
        host_tracks_init();
        for (uint32_t k = 0; k < NPART; k++) host_preset(&trk[k], HOST_SYNTH, 0);
        trk[0].p[P_PAN] = -40;
        trk[0].p[P_FXTYPE] = insert ? FX_CHORUS : FX_OFF;
        trk[0].p[P_FXMIX] = 100;
        trk[0].p[P_FXRATE] = 30;
        trk[0].p[P_FXCHAR] = 80;
        trk[0].p[P_FXSEND] = send ? 127 : 0;
        song.g[G_MFXTYPE] = send ? FX_ROOM : FX_OFF;
        song.g[G_MFXMIX] = 127;
        song.g[G_FXRETURN] = 127;
        song.master_q12 = master;
        usb.up = usb.config = 1; usb.suspended = 0;
        ua.cap_alt = 1; ua_tracks = ua_tracks_want = 0; ua_off = 0; ua_cfg_build();assert(ua_tracks_memory_prepare(ua_tracks&&!(ua_off&UA_OFF_IN)));
        for (uint32_t i = 0; i < CTL * NPART; i++) track_capture[i] = 123456;
        trk_note_on(&trk[0], 60, 100);
        for (uint32_t block = 0; block < 2048; block++) {
            mix_block(out, CTL);
            assert(!track_capture_on && master_capture_on);   /* stereo: the mix before MASTER, L R a frame (#103) */
            for (uint32_t i = CTL * 2; i < CTL * NPART; i++) assert(track_capture[i] == 123456);
            ua.cw = ua.cr = 0;
            ua_audio(out, track_capture, CTL, master);
            assert(ua.cw == CTL);
            for (uint32_t i = 0; i < CTL * 2; i++) {
                assert(out[i] >= -32768 && out[i] <= 32767);
                assert(ua.cap[i] == ua_clip(track_capture[i]));
                if (master == 4096u) assert(ua.cap[i] == out[i]);   /* (full MASTER: the output itself) */
                result.taken += (int64_t)ua.cap[i] * ua.cap[i];
                result.hash = (result.hash ^ (uint32_t)out[i]) * 16777619u;
                result.energy += (int64_t)out[i] * out[i];
                if (!(i & 1u)) result.monitor += abs(out[i] - out[i + 1u]);
            }
        }
        assert(write(fd[1], &result, sizeof result) == sizeof result);
        _exit(0);
    }
    close(fd[1]);
    assert(read(fd[0], &result, sizeof result) == sizeof result);
    close(fd[0]);
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return result;
}

/* Follow the enumerated mode through mixer collection, ring writes and actual
 * PCM16/24 packets. A muted master makes any L/R fallback immediately visible;
 * all four simultaneous tracks must survive only while 4 TRACKS is enabled. */
static void mode_packets(uint8_t alt)
{
    pid_t child = fork();
    int status;
    assert(child >= 0);
    if (!child) {
        int32_t out[CTL * 2];
        int16_t expected[(UA_TARGET + CTL) * NPART];
        uint8_t packet[UA_PACKET];
        host_tracks_init();
        for (uint32_t ch = 0; ch < NPART; ch++) {
            host_preset(&trk[ch], HOST_SYNTH, 0);
            trk[ch].p[P_PAN] = ch & 1u ? 63 : -64;
            trk_note_on(&trk[ch], 48 + ch * 7u, 100);
        }
        song.master_q12 = 0;
        usb.up = usb.config = 1; usb.suspended = 0; ua_off = 0;
        for (uint32_t mode = 0; mode < 3; mode++) {
            uint32_t tracks = mode != 1;     /* four tracks -> stereo -> four tracks */
            ua_reset();
            ua_tracks = ua_tracks_want = (uint8_t)tracks;
            ua_cfg_build();assert(ua_tracks_memory_prepare(ua_tracks&&!(ua_off&UA_OFF_IN)));
            ua.cap_alt = alt;
            uint32_t channels = tracks ? NPART : 2u;
            assert(ua_cap_channels == channels);
            for (uint32_t at = 0; at < UA_TARGET + CTL; at += CTL) {
                for (uint32_t i = 0; i < CTL * NPART; i++) track_capture[i] = 123456;
                mix_block(out, CTL);
                assert(track_capture_on == tracks && master_capture_on == !tracks);
                for (uint32_t i = 0; i < CTL * 2u; i++) assert(out[i] == 0);
                if (!tracks)                            /* stereo: the mix before MASTER (0 here), L R a frame (#103) */
                    for (uint32_t i = CTL * 2u; i < CTL * NPART; i++) assert(track_capture[i] == 123456);
                for (uint32_t i = 0; i < CTL * channels; i++)
                    expected[at * channels + i] = (int16_t)ua_clip(track_capture[i]);
                ua_audio(out, track_capture, CTL, song.master_q12);
            }
            uint32_t read = 0, width = ua_sample_bytes(alt);
            uint64_t energy[NPART] = {0};
            for (uint32_t p = 0; p < 12; p++) {
                uint32_t bytes = ua_transmit(packet);
                assert(bytes && bytes % (channels * width) == 0);
                for (uint32_t i = 0; i < bytes / (channels * width); i++, read++) {
                    assert(read < UA_TARGET + CTL);
                    for (uint32_t ch = 0; ch < channels; ch++) {
                        int32_t sample = ua_decode(packet + (i * channels + ch) * width, width);
                        assert(sample == expected[read * channels + ch]);
                        energy[ch] += (int64_t)sample * sample;
                    }
                }
            }
            for (uint32_t ch = 0; ch < channels; ch++) assert(energy[ch]);   /* MASTER 0: both record (#103) */
        }
        _exit(0);
    }
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void memory_budget(void)
{
    memset(resource,0,sizeof resource);ua_track_extra=0;ua_reset();
    ua_tracks=ua_tracks_want=0;ua_off=ua_off_want=0;ua_cfg_build();
    usb.up=usb.config=1;ua.cap_alt=1;ua.cw=700;ua.cap[0]=12345;
    for(uint32_t b=0;b<RESOURCE_BANKS;b++)if(b!=RESOURCE_CACHE_BANK||RESOURCE_CACHE_AVAILABLE)
        assert(resource_allocate(b,(uint32_t)(resource_banks[b].end-resource_banks[b].start)));
    ua_tracks_set(1,1000);
    assert(ua_off_apply(1600)==2&&usb.up&&usb.config&&ua.cap_alt==1&&ua.cw==700&&ua.cap[0]==12345&&!ua_track_extra&&!resource[RES_USB_TRACKS].size);
    for(uint32_t b=0;b<RESOURCE_BANKS;b++)resource_release(b);
    assert(ua_tracks_memory_prepare(1)&&ua_track_extra&&resource[RES_USB_TRACKS].size==4096u);
    memset(ua_track_extra,0x5a,4096u);
    void *saved=ua_track_extra;
    assert(ua_tracks_memory_prepare(1)&&ua_track_extra==saved&&ua_track_extra[2047]==0x5a5a);
    ua_reset();assert(ua_tracks_memory_prepare(0)&&!ua_track_extra&&!resource[RES_USB_TRACKS].size);
    puts("USB capture memory: real arena failure preserves stereo, retry and release: OK");
}

int main(void)
{
    mode_packets(1);
    mode_packets(2);
    puts("USB mode packets: all four lanes survive a muted master, stereo disables stems, PCM16/24 transitions: OK");
    struct result dry = stereo_render(0, 0, 4096);
    struct result insert = stereo_render(1, 0, 4096);
    struct result wet = stereo_render(1, 1, 4096);
    assert(dry.energy && dry.monitor && insert.energy && wet.energy);
    assert(dry.hash == insert.hash && insert.hash != wet.hash); /* historical track audio FX are inert */
    struct result muted = stereo_render(1, 1, 0), half = stereo_render(1, 1, 2048);
    assert(muted.energy == 0 && half.energy < wet.energy);    /* MASTER sets the output, */
    assert(muted.taken == wet.taken && half.taken == wet.taken);   /* never the recording (#103) */
    puts("USB stereo: inert track audio FX, exact post-send/pan/limiter copy before MASTER, no stem collection: OK");
    for (uint32_t ch = 0; ch < NPART; ch++) {
        struct result dry = render(ch, 0, 0);
        assert(dry.energy && dry.monitor);
        for (int sliced = 0; sliced <= 1; sliced++) {
            struct result base = render(ch, sliced, 0);
            assert(base.energy && base.monitor);
            if (sliced) assert(base.hash == dry.hash);
            for (int variant = 1; variant <= 2; variant++) {
                struct result changed = render(ch, sliced, variant);
                assert(changed.hash == base.hash && changed.energy == base.energy);
                if (variant == 2) assert(changed.monitor == 0);
            }
            assert(render(ch, sliced, 3).energy == 0);
        }
    }
    puts("USB track capture: tracks 1..4 isolated, ignored legacy effects, level, pan/master isolation, stale blocks: OK");
    memory_budget();
    return 0;
}
