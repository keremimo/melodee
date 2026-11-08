/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The synths' SysEx in the main loop: the frame sysex_rx.c collected handed to its synth's module (mod_engine_v3.sysex,
 * host 3), which answers it itself (a dump out, a handshake) through the host table below, offers what it received for
 * user slots (sysex_library.c's import) or edits a part's patch. A module not in SRAM is loaded for its frame first; a
 * synth with no module drops it. The sends of the STORE page and the user pages ask the module for the patch as its
 * synth's SysEx (mod_engine_v3.sysex_dump). Included by melodee.c after project.c */
static uint32_t msx_fed;                     /* the bytes of the frame the module was given */
static uint8_t msx_idle;                     /* it asked to be called each turn (a handshake's clock) */

static void msx_finish(void)                 /* the frame served: the collector free for the next one */
{
    uint32_t f = motion_guard();
    msx.on = 0;
    msx.abort = 0;
    msx.n = 0;
    RING_PUBLISH();
    msx.eng = 0xFFu;
    if (msx.ready != 2u)
        msx.ready = 0;
    motion_unguard(f);
    msx_fed = 0;
    msx_idle = 0;
}
static void msx_hooks_set(void);
static void msx_reset(void) { msx.ready = 0; msx_finish(); msx_hooks_set(); }   /* (power-on, the host tests) */
static void msx_release(void)                /* an import staged in the scratch committed or dropped */
{
    if (msx.ready == 2u) { RING_PUBLISH(); msx.ready = 0; }
}
static int msx_link_up(void)
{
#if MELODEE_OTA
    return msx.src != MSX_USB || usb.config;
#else
    return 1;
#endif
}
/* main loop: the frame waiting or coming to its synth */
static void msx_service(void)
{
    uint32_t e = msx.eng, n, ev, r;
    const engine_t *g;
    if (e >= NENGINES || msx.ready == 2u)
        return;
    if (!msx_hook.part)
        msx_hooks_set();                         /* (a host test without the power-on) */
    RING_PUBLISH();                              /* (the bytes before the count, the count before the flags) */
    n = msx.n;
    ev = msx.abort ? MOD3_SX_ABORT : msx.ready ? MOD3_SX_END : n > msx_fed ? MOD3_SX_MORE : MOD3_SX_IDLE;
    if (ev == MOD3_SX_IDLE && !msx_idle)
        return;
    if (ev == MOD3_SX_IDLE && !msx_link_up())
        ev = MOD3_SX_ABORT;                      /* (its link went away while it waited) */
    if (ENGINES[e] == &ENG_MOD_IDLE)
        mod_want(e);                             /* (the synth's module into SRAM for its frame) */
    g = ENGINES[e];
    if (g == &ENG_MOD_IDLE || g == &ENG_RETIRED || !g->sysex) {
        msx_finish();                            /* (no module for it, or no room: the frame dropped) */
        return;
    }
    if (ev == MOD3_SX_END)
        wire_job = 1;                            /* (the frame is this job's: a dump out may reuse the scratch) */
    msx_serving = 1;
    r = g->sysex(proj_wire_u.raw, n, ev);
    msx_serving = 0;
    msx_fed = n;
    if (r == MOD3_SXR_DONE || ev == MOD3_SX_END || ev == MOD3_SX_ABORT)
        msx_finish();
    else
        msx_idle = r == MOD3_SXR_IDLE;
}

/* ---- the host table's SysEx members (eng_module.c MOD_HOST3), main loop */
static mod_part_v3 *msx_h_part(uint32_t k) { return (mod_part_v3 *)(void *)&trk[k % NTRK]; }
static uint8_t *msx_h_part_patch(uint32_t k, uint32_t e)
{
    return k < NTRK && patch_eng_of(k) == e ? patch_of(k) : 0;
}
static int msx_h_patch_put(uint32_t k, uint32_t e, const uint8_t *src, uint32_t n, uint32_t how)
{
    if (k >= NTRK || trk[k].eng_req != e)
        return 1;                                /* (another synth's part: no patch of this one there) */
    patch_set(k, e, src, n, how);
    ui.force = 1;
    return 0;
}
static int msx_h_sx_part(uint32_t e, uint32_t ch)
{
    uint32_t k;
    if (ch == MOD3_SX_SEL)
        return (int)(song.sel % NTRK);
    if (song.sel < trk_n() && trk[song.sel].eng_req == e)
        return (int)song.sel;
    if (ch < trk_n() && trk[ch].eng_req == e)
        return (int)ch;
    for (k = 0; k < trk_n(); k++)
        if (trk[k].eng_req == e)
            return (int)k;
    return -1;
}
static int msx_h_sx_send(const uint8_t *f, uint32_t n)
{
#if MELODEE_OTA
    if (!usb.config)
        return -1;
    return n ? ota_wire_send(f, n) : 0;
#else
    (void)f; (void)n;
    return -1;
#endif
}
static uint8_t *msx_h_sx_scratch(void) { return proj_wire_borrow(); }
static int sysex_stage(uint32_t e, const uint8_t *raw, uint32_t count, uint32_t size);   /* sysex_library.c */
static int msx_h_sx_stage(uint32_t e, const uint8_t *p, uint32_t count, uint32_t stride)
{
    return sysex_stage(e, p, count, stride);
}
static uint32_t msx_h_slot_get(uint32_t e, uint32_t k, uint8_t *patch)
{
    uint32_t n;
    if (!native_used(e, k))
        return 0;
    n = native_size(e);
    memcpy(patch, native_raw(e, k), n);
    return n;
}
static uint32_t msx_h_store_slot(uint32_t e) { return sch_store_slot[e % NENGINES] % UP_SLOTS; }
static void msx_h_message(const char *s)
{
    if (s)
        ui_message(s);
    else
        ui.force = 1;
}
static void msx_hooks_set(void)                  /* (eng_module.c: the host table's members call these) */
{
    msx_hook.part = msx_h_part;
    msx_hook.part_patch = msx_h_part_patch;
    msx_hook.patch_put = msx_h_patch_put;
    msx_hook.sx_part = msx_h_sx_part;
    msx_hook.sx_send = msx_h_sx_send;
    msx_hook.sx_scratch = msx_h_sx_scratch;
    msx_hook.sx_stage = msx_h_sx_stage;
    msx_hook.slot_get = msx_h_slot_get;
    msx_hook.store_slot = msx_h_store_slot;
    msx_hook.message = msx_h_message;
}

/* ---- the sends: a part's patch (the STORE page's SEND), a user slot's (the user pages'), as the synth's SysEx */
static uint32_t msx_dump(uint32_t e, const uint8_t *patch, uint32_t what, uint32_t ch, uint8_t *out, uint32_t max)
{
    const engine_t *g;
    if (e >= NENGINES)
        return 0;
    if (ENGINES[e] == &ENG_MOD_IDLE)
        mod_want(e);
    g = ENGINES[e];
    return g != &ENG_MOD_IDLE && g->sysex_dump ? g->sysex_dump(patch, what, ch, out, max) : 0;
}
/* synth e's STORE page's words for its SEND (MOD3_ST_SENDMSG: inames' second when it went, third when not), if it has
 * them */
static void sch_store_send_msg(uint32_t e, int ok)
{
    mod_sch_head h;
    sch_page_t g;
    char b[24];
    uint32_t j, off;
    if (sch_head(e, &h))
        return;
    for (j = 0; j < h.npage; j++)
        if (!sch_page_rec(e, j, &g) && g.widget == MOD3_W_STORE)
            break;
    if (j == h.npage || g.inames == MOD_SCH_NONE || !(g.wid & MOD3_ST_SENDMSG))
        return;
    off = g.inames;
    for (uint32_t i = ok ? 1u : 2u; i; i--)
        off += sch_str(e, off, b, sizeof b) + 1u;
    if (sch_str(e, off, b, sizeof b))
        ui_message(b);
}
/* part tr's sound as its synth's SysEx (on channel tr + 1): 0 sent; say: the STORE page's words for it */
static uint8_t msx_frame[320] __attribute__((section(".pool")));   /* (a send's frame: not on the main loop's stack) */
static int store_send(uint32_t tr, int say)
{
    uint8_t *frame = msx_frame;
    uint32_t e = trk[tr % NTRK].eng_req, n = patch_eng_of(tr) == e ? msx_dump(e, patch_of(tr), MOD3_DUMP_PART, tr % 16u,
                                                                              frame, sizeof msx_frame) : 0;
    int rc = n ? msx_h_sx_send(frame, n) : -1;
    if (say)
        sch_store_send_msg(e, !rc);
    return rc;
}
