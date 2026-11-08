/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A take made a pad's record as it is recorded, the part with no flash in it: what Record kit does on the device
 * (kit_take.c: the host's take calls) and what the web's kit maker does (web/kit, this file in WebAssembly), so that
 * both make a pad record byte for byte the same. The samples come in pieces (tk_feed) from the first above the take's
 * trigger, MOD3_TAKE_PRE samples early; the kick and the toms go to 22.05 kHz (a half-band filter); every piece is
 * encoded (drum_enc.c, 30 dB under each slice's level) and handed to the host as it comes, 32 slices at a time (the
 * device keeps them in RAM until the take ends), so a take is as long as its room. At its end (tk_end): the record's header first in it, the take's level (gain: its peak to
 * full scale), its length to 10 ms after its last sample above -60 dBFS (the slices after that stay, unplayed).
 * The includer gives KT_ON (usb_audio_stream.c), drum_enc.c, and the record's calls: kt_h_write(off, src, n) (each
 * payload byte once, src RAM), kt_h_end(slot, len) (published once checked: 0, -2 not playable, -5 no room, -n),
 * kt_h_abort(). Every function here is built for size (TK_SIZE) as tools/size_fns.py builds kit_take.c's. */
enum { KM_NONE, KM_KEPT, KM_QUIET, KM_ROOM, KM_FLASH };   /* a take's end, as the sheet shows it */
#define TK_SIZE __attribute__((minsize))
#define TK_SNR 30                                    /* dB under each slice's level */
#define TK_STAGE 512u                                /* samples staged for the encoder (at the take's rate) */
#define TK_PAGE 64u                                  /* words written at a time: a flash page's 256 bytes */
#define TK_QUIET 33                                  /* -60 dBFS: a take's end after its last sample above */
static TK_SIZE int tk_low(uint32_t pad) { return pad == 0u || (pad >= 2u && pad <= 4u); }   /* kick, toms: 22.05 kHz */
/* a new pad's level (Q12), TONE's pivot (Hz), in the 808's sixteen (drum_map.c: KICK SNARE TOM L/M/H RIM CLAP HAT C
 * HAT O CYMB BELL CONGL CONGM CONGH CLAVE MARAC); tuned once for recorded kits. Its decay: held to the take's end */
static const uint16_t TK_PAD[DRK_NPAD][2] = {{4700, 4000}, {4800, 6000}, {4700, 3000}, {4700, 3000}, {4700, 3000},
    {5000, 5000}, {5000, 5000}, {4600, 10000}, {4200, 10000}, {4000, 10000}, {4200, 6000}, {4700, 3000}, {4700, 3000},
    {4700, 3000}, {4600, 6000}, {4200, 10000}};

static struct {
    dke_t e;
    uint32_t slot, pad, half, room, off;             /* the record, its pad, at 22.05 kHz; its room, the next byte */
    uint32_t fed, kept, peak, loud, wpos;            /* samples fed, staged; the largest; the last loud one's end; the
                                                      * samples the slices written so far hold */
    uint32_t ns, np, hn;                             /* staged samples, words waiting, the half-band's inputs */
    int16_t h[7];                                    /* the half-band's last inputs */
    uint8_t on, end;                                 /* a take going on; how it ended early (KM_ROOM, KM_FLASH) */
    int16_t x[TK_STAGE + 64u];
    uint32_t page[TK_PAGE];
} tk;

/* the words waiting into the record (all: a full page's or the last) */
static TK_SIZE int tk_flush(void)
{
    uint32_t b = tk.np * 4u;
    if (!b) return 0;
    if (tk.off + b > tk.room) { tk.end = KM_ROOM; return -5; }
    int rc = kt_h_write(tk.off, tk.page, b);
    if (rc) { tk.end = rc == -5 ? KM_ROOM : KM_FLASH; return rc; }
    for (uint32_t i = 0; i < tk.np; i += 2u) tk.wpos += DRK_NS[tk.page[i] >> 30];
    tk.off += b;
    tk.np = 0;
    return 0;
}
/* the staged samples encoded (end: all of them, zeros after), the slices written a page at a time */
static TK_SIZE int tk_encode(int end)
{
    uint32_t at = 0;
    if (end) for (uint32_t i = 0; i < 64u; i++) tk.x[tk.ns + i] = 0;
    for (;;) {
        uint32_t take = dke_step(&tk.e, tk.x + at, tk.ns - at, end, tk.page + tk.np, (TK_PAGE - tk.np) / 2u);
        tk.np = 2u * (tk.e.o.bytes / 8u) - 2u * ((tk.off - DRK_PAD_BYTES) / 8u);
        at += take;
        if (tk.np == TK_PAGE && tk_flush()) return -1;
        if (!take || at >= tk.ns) break;
    }
    for (uint32_t i = at; i < tk.ns; i++) tk.x[i - at] = tk.x[i];   /* (the rest to the front: no memmove here) */
    tk.ns -= at;
    return end ? tk_flush() : 0;
}
/* one sample at the take's rate */
static TK_SIZE int tk_stage(int32_t v)
{
    uint32_t a = (uint32_t)(v < 0 ? -v : v);
    if (a > tk.peak) tk.peak = a;
    tk.x[tk.ns++] = (int16_t)v;
    if (++tk.kept && a >= (uint32_t)TK_QUIET) tk.loud = tk.kept;
    return tk.ns == TK_STAGE ? tk_encode(0) : 0;
}
/* one sample at 44.1 kHz: the half-band's (-1 0 9 16 9 0 -1) / 32 around every other one, three behind */
static TK_SIZE int tk_in(int32_t s)
{
    if (!tk.half) return tk_stage(s);
    for (uint32_t i = 0; i < 6u; i++) tk.h[i] = tk.h[i + 1u];
    tk.h[6] = (int16_t)s;
    if (++tk.hn < 4u || ((tk.hn - 4u) & 1u)) return 0;
    return tk_stage(dke_clip((-tk.h[0] + 9 * tk.h[2] + 16 * tk.h[3] + 9 * tk.h[4] - tk.h[6]) >> 5));
}

/* a take for pad of record slot, room its bytes */
static TK_SIZE void tk_begin(uint32_t slot, uint32_t pad, uint32_t room)
{
    memset(&tk, 0, sizeof tk);
    dke_begin(&tk.e, TK_SNR, 0);
    tk.slot = slot; tk.pad = pad; tk.half = (uint32_t)tk_low(pad); tk.room = room;
    tk.off = DRK_PAD_BYTES;                          /* (the header: last, tk_end) */
    tk.on = 1;
}
/* n more samples: KM_NONE while it goes on, KM_ROOM once the store is full, KM_FLASH a write failed */
static TK_SIZE uint32_t tk_feed(const int16_t *x, uint32_t n)
{
    for (uint32_t i = 0; tk.on && !tk.end && i < n; i++) {
        tk.fed++;
        if (tk_in(x[i])) break;
    }
    return tk.end;
}
/* the take ends: its record published (KM_KEPT, a full store's take too) or dropped (KM_QUIET, KM_FLASH) */
static TK_SIZE uint32_t tk_end(void)
{
    drk_pad_t H;
    uint32_t tail = tk.half ? 221u : 441u, n;
    if (!tk.on) return KM_NONE;
    tk.on = 0;
    if (tk.end != KM_ROOM) {                         /* (a full store: the slices written so far are the take) */
        for (uint32_t i = 0; tk.half && i < 3u; i++) tk_in(0);
        if (!tk.end) tk_encode(1);
    }
    n = tk.loud + tail < tk.kept ? tk.loud + tail : tk.kept;
    if (n > tk.wpos) n = tk.wpos;                    /* (a full store: what its written slices hold) */
    if (tk.end == KM_FLASH || tk.peak < (uint32_t)KT_ON || n < tail) { kt_h_abort(); return tk.end == KM_FLASH ? KM_FLASH : KM_QUIET; }
    memset(&H, 0, sizeof H);
    H.magic = DRK_PAD_MAGIC; H.bytes = (uint16_t)DRK_PAD_BYTES; H.pad = (uint8_t)tk.pad; H.codec = DRK_CODEC_LMS;
    H.n = n;
    H.inc = tk.half ? 16384u : 32768u;
    H.gain = (uint16_t)(32000u * 1024u / tk.peak > 65535u ? 65535u : 32000u * 1024u / tk.peak);
    H.out = TK_PAD[tk.pad][0]; H.pivot = TK_PAD[tk.pad][1];
    int rc = kt_h_write(0, &H, sizeof H);
    if (!rc) rc = kt_h_end(tk.slot, tk.off);
    if (rc) { if (rc != -2) kt_h_abort(); return rc == -5 ? KM_ROOM : KM_FLASH; }
    return KM_KEPT;
}
/* the take stopped (its record never published) */
static TK_SIZE void tk_abort(void)
{
    if (tk.on) kt_h_abort();
    tk.on = 0;
}
