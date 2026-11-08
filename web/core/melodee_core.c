/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The virtual FM-1: the firmware's editor (editor.c with the Studio session and song commands), its storage
 * (storage.c on a 1 MiB NOR in RAM), its projects, kits and presets, its engines, mixer and sequencer, without the
 * UI drawing or any hardware. Melodee Studio's IN THIS BROWSER mode talks to it in the editor's own SysEx, as it
 * talks to an FM-1 over USB, and plays its sound.
 * Composition: tests/editor_test.c's (hostsim.c's engines and sequencer, ui.c, the editor) with backup_test.c's
 * NOR flash under MELODEE_FLASH 1, in melodee.c's order (storage, kits, user presets, projects, CZ/FM6 stores,
 * editor). The clock is web/emu/melodee_web.c's: a millisecond at a time, mix_block every CTL frames at FS as the
 * audio ISR; main.c's loop services (pattern_poll, msx_service, ed_service, glo_poll, settings_poll)
 * every 15 ms, ed_service every millisecond (main.c's wait between frames).
 * A wait of the main loop for the ISR (ed_flash_stop, the backup LIST, a kit's settle) feeds the watchdog: here
 * that is a millisecond of the ISR's time (fm1_wdt_feed below), the sound it makes kept for core_render.
 * The firmware's hooks for this file (nothing on the device): voice.c's MELODEE_NOTE_TAP, motion.c's
 * MELODEE_MOTION_TAP, mix.c's MELODEE_MASTER_RAW.
 *
 * Sound: stereo float, Q15 full scale = 1.0 (as the USB capture, Melodee In, sends it at USB VOLUME 100), with the
 * MASTER pot all the way up (unity: the core has no pot). core_render plays it; core_render_taps also gives each
 * audio track's share (its post-fader, post-pan, post-solo sum into the mix, mix.c mix_part) and the shared FX
 * return (mix.c master_samples), both before punch, the limiter and the DC blocker, so they do not sum to the
 * master exactly.
 *
 * CORE_MODULES (web/core/build.sh builds it so): the five synths as the device plays them, synth modules
 * (eng_module.c, module_store.c in the pool) bound to the same ABI 3 host table. The module files (build/modules/
 * <name>.mmod, the device's) go into the pool as an installer puts them (core_mod_install); their code is the
 * module's own source built for this core: in WebAssembly a side module the page links into the core's memory and
 * function table (tools/mod_build.py --wasm: the file's WASM chunk) and names to the core (core_mod_link), natively
 * the module objects linked beside it (as tests/module_test.c's device build). module_store.c's mod_host_desc
 * gives the loader the linked module's descriptor for the file's engine.
 *   web/core/build.sh  ->  build/core/melodee_core.wasm
 *   cc -DCORE_NATIVE_MAIN ... web/core/melodee_core.c: the same core natively, replaying SysEx frames (stdin) */
#include <stdint.h>
#ifdef CORE_MODULES
#define MELODEE_SID_BUILTIN 0
#define MELODEE_DRUM_BUILTIN 0
#define MELODEE_CZ_BUILTIN 0
#define MELODEE_FM6_BUILTIN 0
#define MELODEE_P5_BUILTIN 0
#endif
static unsigned char nor[0x100000];                     /* the FM-1's 1 MiB NOR flash, erased at a first boot */
#define REC_BASE ((const uint8_t *)nor)                  /* the data records (user kits) play from the pool in place */
#define MELODEE_OTA 1
#define MELODEE_FLASH 1
#ifndef MELODEE_VERSION
#define MELODEE_VERSION "CORE"
#endif
static void core_note_tap(uint32_t tr, uint32_t note, uint32_t vel);
#define MELODEE_NOTE_TAP(tr, note, vel) core_note_tap(tr, note, vel)
static void core_motion_tap(uint32_t k, uint32_t id, int32_t v);
#define MELODEE_MOTION_TAP(k, id, v) core_motion_tap(k, id, v)
static uint8_t master_raw;                              /* core_master_raw: the master without its limiter */
#define MELODEE_MASTER_RAW master_raw
#define main hostsim_main
#include "hostsim.c"
#undef main

static void fm1_wdt_feed(void);                         /* the ISR's millisecond (below) */
static void core_drain(void);
static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
static void panel_setup(void) {}
/* ui_draw.c's (not built here): the name the device shows for a track's sound; Prophet's patch name as
 * ui_graph.c p5_short_name has it */
static void sound_name(const track_t *t, char *b)
{
    if (song_picker.active && t == &trk[song_picker.track] && browse_loads == browse_mark) {
        const song_sound_t *s = song_sound_get(song_picker.pattern ? pattern_sound_id(song_picker.track, song_picker.row) : resolved_sound_id(song_picker.row, song_picker.track));
        if (s) { str_cpy(b, s->name, 16); return; }
    }
    if (song.playing && !song_picker.active) {
        const song_sound_t *s = song_sound_get(song_sounds.active[t - trk]);
        if (s) { str_cpy(b, s->name, 16); return; }
    }
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    b[0] = 0;
    if (sch_flag(eng_idx(t->eng_req), MOD_SCH_F_SNAME)) {   /* (ui_draw.c's: a synth whose sound is its patch's name) */
        char full[64];
        patch_name_of(eng_idx(t->eng_req), patch_of(trk_ix(t)), full, sizeof full);
        str_cpy(b, full, 16);
    } else if (t->user_native && user_of(t) < USER_NONE) native_name(t->eng_req, user_of(t), b);
    else if (user_of(t) < UP_SLOTS) up_name(user_of(t), b);
    else if (e->npresets) str_cpy(b, eng_preset(e, t->preset % e->npresets)->name, 16);
}
/* ui_layer.c's: the layers play unless a menu, a dialog or the UBOOT countdown is up (NAME, NEW SONG, the popups
 * and the section map never open here: nobody holds the panel) */
static int layer_allowed(void) { return !ui.menu && !ui.confirm && !ui.uboot; }

/* the NOR: reads as XIP, 4 KiB erases, programming only clears bits (as the part) */
static uint8_t flash_ok;
static uint32_t nor_erases, nor_progs;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > sizeof nor || n > sizeof nor - off) return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    off &= ~0xFFFu;
    if (off >= sizeof nor) return -1;
    memset(nor + off, 0xFF, 4096);
    nor_erases++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    if (off > sizeof nor || n > sizeof nor - off) return -1;
    for (uint32_t i = 0; i < n; i++) nor[off + i] &= s[i];
    nor_progs++;
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's part: persist_boot finds its flash */
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
static void audio_silence(void) {}
#include "../firmware/src/storage.c"
#include "../firmware/src/rec_store.c"
#ifdef CORE_MODULES
#include "../firmware/src/module_store.c"
#endif
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#include "../firmware/src/sysex_route.c"
#define ED_BK_FLASH_PTR(off) (nor + (off))
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { core_drain(); }            /* the USB IN endpoint: the page takes every packet */
#include "../firmware/src/editor.c"

/* ------------------------------------------------------------------ the page's side --- */
#define OUTQ 0x40000u                                   /* replies and pushes not read yet (a BACKUP GET: ~300 B) */
static uint8_t out_q[OUTQ];
static uint32_t out_w, out_r, out_lost;
static void core_drain(void)                           /* USB-MIDI SysEx packets -> bytes (editor_test.c host_drain) */
{
    while (so_r != so_w) {
        uint32_t p = sx_out_q[so_r++ % SXQ], cin = p & 15u, i;
        uint32_t n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (i = 0; i < n; i++) {
            if (out_w - out_r >= OUTQ) { out_lost++; continue; }
            out_q[out_w++ % OUTQ] = (uint8_t)(p >> (8u * (i + 1u)));
        }
    }
}

/* MIDI events for the page (core_midi_events): 8 bytes each, the frame (of core_render's sound) the block that
 * made it starts at, then a tag and three bytes. Tag 0..7: a note a track played (voice.c trk_note_on/off: the
 * sequencer's, the keys', the arpeggiator's and chords'; before MIDI DELAY), note, velocity (0 = off). Tag 0x80:
 * a message the FM-1 sent over USB (its MIDI tracks, MIDI clock and Start/Stop), status and two data bytes.
 * Tag 0x40 | k: a value motion set on audio track k + 1 (motion.c motion_step, and motion_restore putting the
 * patch's own back), the parameter id, then the value as int16 (low byte, high byte); the engines' own patch
 * bytes (MO_NATIVE) are not reported. */
#define EVQ 8192u
static struct { uint32_t frame; uint8_t b[4]; } ev_q[EVQ];
static uint32_t ev_w, ev_r, ev_lost, ev_mask;          /* ev_mask: bit 0 notes, 1 USB out, 2 motion (core_midi_tap) */
static uint32_t frames_made;                            /* frames of sound made so far: the events' clock */
static void ev_put(uint32_t tag, uint32_t a, uint32_t b, uint32_t c)
{
    if (ev_w - ev_r >= EVQ) { ev_lost++; return; }
    uint32_t i = ev_w++ % EVQ;
    ev_q[i].frame = frames_made;
    ev_q[i].b[0] = (uint8_t)tag; ev_q[i].b[1] = (uint8_t)a; ev_q[i].b[2] = (uint8_t)b; ev_q[i].b[3] = (uint8_t)c;
}
static void core_note_tap(uint32_t tr, uint32_t note, uint32_t vel)
{
    if ((ev_mask & 1u) && tr < NTRK) ev_put(tr, note, vel, 0);
}
static void core_motion_tap(uint32_t k, uint32_t id, int32_t v)
{
    if ((ev_mask & 4u) && k < NPART) ev_put(0x40u | k, id, (uint32_t)v & 255u, ((uint32_t)v >> 8) & 255u);
}
static void usb_out_drain(void)                        /* the IN endpoint's MIDI: what a computer would read */
{
    while (mo_r != mo_w) {
        uint32_t p = midi_out_q[mo_r % MQ];
        mo_r++;
        if (ev_mask & 2u) ev_put(0x80u, p >> 8, p >> 16, p >> 24);
    }
    while (rt_r != rt_w) {
        uint32_t p = midi_rt_q[rt_r % RTQ];
        rt_r++;
        if (ev_mask & 2u) ev_put(0x80u, p >> 8, p >> 16, p >> 24);
    }
}

/* the sound: stereo float, made a millisecond at a time and kept until core_render takes it */
#define RING 32768u                                     /* frames: 0.74 s (an ed_flash_stop waits 100 ms at most) */
#define TAPS (NPART + 1u)                               /* core_render_taps: each audio track, then the FX return */
#define OUT_SCALE (1.0f / (32768.0f * MIX_ONE))         /* the bus (Q15 << MIX_FRAC, mix.c): full scale = 1.0 */
#define METER_SCALE (1.0f / 32768.0f)                   /* a track's meter (trk_peak_take): Q15 */
static float ring[RING * 2u], rendered[4096u * 2u];
static float tap_ring[RING][TAPS * 2u], tapped[4096u * 2u * (1u + TAPS)];
static uint32_t ring_w, ring_r, owed, ring_lost;
static uint8_t in_isr, in_loop, taps_on;
static int32_t master_peak[2];                          /* |sample| since core_meters last read them */

/* mix.c mix_block with each track's share and the FX return measured on the way (the same calls in the same order,
 * so the sound is the same: web/core/core_check.mjs compares) */
static int32_t tap_pre_l[CTL], tap_pre_r[CTL];
static void core_mix_block_taps(int32_t *out, uint32_t n, int32_t tap[TAPS][2u * CTL])
{
#if MELODEE_USB_AUDIO
    {int cap=ua_capture_active();track_capture_on=(uint8_t)(cap==4);master_capture_on=(uint8_t)(cap==2);}
    if(track_capture_on)memset(track_capture,0,n*NPART*sizeof *track_capture);
#endif
    memset(mix_l,0,n*sizeof *mix_l);memset(mix_r,0,n*sizeof *mix_r);
    dither_block();
    events_block(n);
    fx_bus_select();fx_send_on=(uint8_t)fx_bus_active();if(fx_send_on){memset(fx_send_l,0,n*sizeof *fx_send_l);memset(fx_send_r,0,n*sizeof *fx_send_r);
        if(fx_bus_separate){memset(fx_send_extra_l,0,sizeof fx_send_extra_l);memset(fx_send_extra_r,0,sizeof fx_send_extra_r);}}
    uint32_t nt=trk_parts();
#if MELODEE_BATCH_AUDIO
    mix_prepare_batch(n,nt);
#endif
    memset(tap, 0, TAPS * 2u * CTL * sizeof tap[0][0]);
    for(uint32_t i=0;i<nt;i++){
        memcpy(tap_pre_l, mix_l, n * sizeof *mix_l); memcpy(tap_pre_r, mix_r, n * sizeof *mix_r);
        mix_part(&trk[i],n);
        for (uint32_t j = 0; j < n; j++) { tap[i][2u * j] = mix_l[j] - tap_pre_l[j]; tap[i][2u * j + 1u] = mix_r[j] - tap_pre_r[j]; }
    }
#if MELODEE_BATCH_AUDIO
    batch_ready=0;part_buf=part_storage;spread_buf=spread_storage;
#endif
    fx_bus_start(n);
    fx_bus_join();
    if (fx_bus_result) {                                /* master_samples' return, its ramp as it runs it */
        int32_t gain = fx_bus_mix, target = fx_bus_target;
        for (uint32_t i = 0; i < n; i++) {
            if (gain != target) gain += clamp(target - gain, -128, 128);
            uint32_t ret = (uint32_t)gain * 2u;
            tap[NPART][2u * i] = mix_fx_return(fx_left[i], ret);
            tap[NPART][2u * i + 1u] = mix_fx_return(fx_right[i], ret);
        }
    }
    void (*volatile run)(int32_t *,uint32_t)=master_samples;
    run(out,n);
}

static void isr_ms(void)                               /* TIMER5's millisecond, then the ALNK0 blocks it owes */
{
    fm1_ms++;
    owed += FS;
    while (owed >= 1000u * CTL) {
        int32_t o[2 * CTL], tap[TAPS][2u * CTL];
        float master = (float)song.master_q12 / 4096.0f * OUT_SCALE;
        owed -= 1000u * CTL;
        in_isr = 1;
        if (taps_on) core_mix_block_taps(o, CTL, tap);
        else mix_block(o, CTL);
        usb_out_drain();
        in_isr = 0;
        for (uint32_t i = 0; i < CTL; i++) {
            if (ring_w - ring_r >= RING) { ring_r++; ring_lost++; }   /* nobody listening: the oldest goes */
            uint32_t at = ring_w++ % RING, r = at * 2u;
            int32_t l = o[2u * i], rr = o[2u * i + 1u];
            ring[r] = (float)l * OUT_SCALE;
            ring[r + 1u] = (float)rr * OUT_SCALE;
            if (l < 0) l = -l;
            if (rr < 0) rr = -rr;
            if (l > master_peak[0]) master_peak[0] = l;
            if (rr > master_peak[1]) master_peak[1] = rr;
            if (taps_on)                                /* (taps are the bus's: MASTER scales them as the output) */
                for (uint32_t k = 0; k < TAPS; k++) {
                    tap_ring[at][2u * k] = (float)tap[k][2u * i] * master;
                    tap_ring[at][2u * k + 1u] = (float)tap[k][2u * i + 1u] * master;
                }
        }
        frames_made += CTL;
    }
}
static uint32_t wdt_feeds;
static void fm1_wdt_feed(void)                         /* a main-loop wait: the ISR keeps running meanwhile */
{
    wdt_feeds++;
    if (!in_isr) isr_ms();
}
static void frame_done(void)                           /* a frame the editor left (not ours): ota_service's, dropped */
{
    if (sx_ready) ota_frame_done();
}
static void loop_services(void)                        /* main.c's loop, without the panel, the screen and the USB */
{
    if (in_loop) return;
    in_loop = 1;
    wire_release();
    pattern_poll();
#ifdef MODULE_STORE
    mod_service(fm1_ms);                                /* synth modules: the used ones in, the idle ones out */
#endif
    msx_service();
    ed_service();
    frame_done();
    dq_poll();                                          /* (ui_input.c's frame: edits waiting for a loop end) */
    glo_poll();
    settings_poll();
    in_loop = 0;
}
static void core_tick(void)                            /* one millisecond of the device */
{
    isr_ms();
    if (in_loop) return;
    if (fm1_ms % 15u == 0u) loop_services();
    else { in_loop = 1; ed_service(); frame_done(); in_loop = 0; }   /* (main.c: between two frames) */
}

static void core_melodee_init(void)                    /* main.c melodee_init */
{
    uint32_t i;
    trk_voices_bind();
    midi_insert_init();
    punch_reset();
    chain_defaults(&chain_config);
    pattern_init();
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    undo_depth++;
    msx_reset();
#ifdef CORE_MODULES
    sch_info_sync();                                    /* (the synths' patches: schema.c) */
#else
    fm6_init();
    cz_init();
#endif
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, trk_def_engine(i));
        apply_preset_to(t, trk_def_preset(i));
        t->engine = t->eng_req;
        track_defaults_steps(t);
        pat_sig[i] = steps_sig(t);
        pat_last[i] = 0;
    }
    undo_depth--;
    song.sel = 0;
    song.master_q12 = 2048;
    ui.home = 1;
    ui.force = 1;
}

#ifdef __wasm__
#undef __attribute__
#define EXPORT(name) __attribute__((used, export_name(#name)))
#else
#define EXPORT(name)
#endif

/* the page's scratch: requests, records and kits are written here before a call names them */
static uint8_t scratch[0x20000];
/* the exports' version: Studio refuses a core whose calls it does not know */
#define CORE_ABI 4u                                     /* 2: motion events (core_midi_tap bit 2), core_master_raw;
                                                         * 3: the synths as modules (core_mod_*); 4: modules by
                                                         * their files' IDs (core_mod_link, _install, _crc_of) */
EXPORT(core_abi) uint32_t core_abi(void) { return CORE_ABI; }
EXPORT(core_scratch) uint8_t *core_scratch(void) { return scratch; }
EXPORT(core_scratch_size) uint32_t core_scratch_size(void) { return sizeof scratch; }
EXPORT(core_nor_ptr) uint8_t *core_nor_ptr(void) { return nor; }
EXPORT(core_nor_len) uint32_t core_nor_len(void) { return sizeof nor; }
EXPORT(core_now) uint32_t core_now(void) { return fm1_ms; }
EXPORT(core_fs) uint32_t core_fs(void) { return FS; }
EXPORT(core_ntrk) uint32_t core_ntrk(void) { return NTRK; }
EXPORT(core_taps_count) uint32_t core_taps_count(void) { return TAPS; }
#ifdef CORE_MODULES
/* ---- the synth modules: their code linked by the page (WebAssembly side modules) or beside the core (natively). A
 * module is its file's ID (mod_abi3.h; the file head's uid, its first 4 bytes, names it here): whatever number this
 * device gives it, any number of synths whatever numbers they ask for */
static struct { uint32_t uid; const mod_desc_v3 *d; } core_links[2u * NMOD];   /* the linked modules' descriptors */
static const mod_desc_v3 *core_desc_id(uint32_t uid)
{
    for (uint32_t k = 0; uid && k < 2u * NMOD; k++)
        if (core_links[k].uid == uid)
            return core_links[k].d;
    return 0;
}
static uint8_t core_mod_mem[1u << 20] __attribute__((aligned(16)));   /* their data, .bss and stacks */
static uint32_t core_mod_used;
/* n bytes of the modules' memory, aligned (a power of two): a side module's __memory_base, a stack; 0 when full */
EXPORT(core_mod_alloc) uint8_t *core_mod_alloc(uint32_t n, uint32_t align)
{
    uint32_t at = (core_mod_used + (align ? align - 1u : 0u)) & ~(align ? align - 1u : 0u);
    if (at > sizeof core_mod_mem || n > sizeof core_mod_mem - at) return 0;
    core_mod_used = at + n;
    memset(core_mod_mem + at, 0, n);
    return core_mod_mem + at;
}
/* a linked module's descriptor for the file whose uid (its head's) is uid: its code from now on, when that file is in
 * the pool. 0 ok (1: not a descriptor, or no room) */
EXPORT(core_mod_link) uint32_t core_mod_link(const mod_desc_v3 *d, uint32_t uid)
{
    uint32_t k = 0;
    if (!d || d->magic != MOD3_MAGIC || d->abi != MOD3_ABI || !uid) return 1;
    while (k < 2u * NMOD && core_links[k].uid && core_links[k].uid != uid) k++;
    if (k == 2u * NMOD) return 1;
    core_links[k].uid = uid;
    core_links[k].d = d;
    return 0;
}
static uint32_t core_head_uid(const uint8_t *p, uint32_t n) { return n >= MOD3_HEAD ? mod_le32(p + 24) : 0u; }
/* a module file (the device's, n bytes at p) into the pool, as an installer puts it (the slot holding the same synth:
 * its ID, else the first free one), and loaded: MS_* (>= 0), or a storage error (< 0; -2 not a module file) */
EXPORT(core_mod_install) int32_t core_mod_install(const uint8_t *p, uint32_t n)
{
    uint32_t uid = core_head_uid(p, n), k = NMOD;
    for (uint32_t j = 0; j < NMOD && k == NMOD; j++) if (uid && mods[j].len && (uint32_t)mods[j].uid == uid) k = j;
    for (uint32_t j = 0; j < NMOD && k == NMOD; j++) if (!mods[j].len) k = j;
    if (k == NMOD) return -1;
    int rc = mod_put_begin(k, n);
    if (!rc) rc = mod_put_write(k, 0, p, n);
    return rc ? rc : mod_put_commit(k);
}
/* the CRC (its head's) of the file in the pool whose uid is uid, 0 none: the page puts its own file in when the NOR it
 * kept holds another */
EXPORT(core_mod_crc_of) uint32_t core_mod_crc_of(uint32_t uid)
{
    mod_file3_t f;
    for (uint32_t k = 0; uid && k < NMOD; k++)
        if (mods[k].len && (uint32_t)mods[k].uid == uid && mod_open(k) && !mod_rd(k, 0, &f, sizeof f))
            return f.crc;
    return 0;
}
#endif
/* the NOR's write count (erases + programs): the page saves it when this moves */
EXPORT(core_nor_writes) uint32_t core_nor_writes(void) { return nor_erases + nor_progs; }

/* power on (fm1_main's order, without the screen, the input and the audio hardware). keep: the NOR holds a
 * previous session's flash (written to core_nor_ptr), else it is erased as a new device's. 1 = the flash works */
EXPORT(core_boot) uint32_t core_boot(uint32_t keep)
{
    if (!keep) memset(nor, 0xFF, sizeof nor);
#ifdef CORE_MODULES
    mod_host_desc_id = core_desc_id;                    /* (the loader's descriptors: the linked modules') */
#endif
    settings_init();
    persist_boot();
    settings_init();
    panel_init();
    core_melodee_init();
    glo_restore();
    project_boot();
    midi_master_physical(4096u);                        /* the MASTER pot all the way up */
    usb.config = 1;                                     /* the editor's port is open */
    return flash_ok;
}

/* SysEx from the page: each complete frame goes to the editor at once (as the main loop's wait does) */
EXPORT(core_sysex_in) uint32_t core_sysex_in(const uint8_t *p, uint32_t n)
{
    uint32_t i, frames = 0;
    for (i = 0; i < n; i++) {
        sysex_byte(p[i]);
        if (p[i] == 0xF7u) {
            in_loop = 1;
            ed_service();
            frame_done();
            in_loop = 0;
            frames++;
        }
    }
    core_drain();
    return frames;
}
/* SysEx to the page: up to max bytes of the replies and pushes waiting; the count */
EXPORT(core_sysex_out) uint32_t core_sysex_out(uint8_t *p, uint32_t max)
{
    uint32_t k = 0;
    core_drain();
    while (k < max && out_r != out_w) p[k++] = out_q[out_r++ % OUTQ];
    return k;
}
EXPORT(core_sysex_pending) uint32_t core_sysex_pending(void) { core_drain(); return out_w - out_r; }

/* time without a listener: ms milliseconds of the device (its sound is dropped); ms 0 runs the loop once */
EXPORT(core_poll) void core_poll(uint32_t ms)
{
    if (!ms) loop_services();
    while (ms--) core_tick();
    ring_r = ring_w;
    core_drain();
}
/* sound made but not taken yet (frames): a wait of the main loop (a save stopping the song) makes some ahead.
 * core_ring_trim drops all but keep of them (a live page holds its latency; a render keeps every frame) */
EXPORT(core_ring_fill) uint32_t core_ring_fill(void) { return ring_w - ring_r; }
EXPORT(core_ring_trim) uint32_t core_ring_trim(uint32_t keep)
{
    uint32_t over = ring_w - ring_r > keep ? ring_w - ring_r - keep : 0u;
    ring_r += over;
    return over;
}
/* n frames (<= 4096) of stereo, interleaved float; the device runs as long as they take */
EXPORT(core_render) float *core_render(uint32_t n)
{
    uint32_t k;
    if (n > 4096u) n = 4096u;
    while (ring_w - ring_r < n) core_tick();
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        rendered[2u * k] = ring[i];
        rendered[2u * k + 1u] = ring[i + 1u];
    }
    core_drain();
    return rendered;
}
/* the taps on (1) or off (0): from now on core_render_taps can take them (sound made before has none: dropped) */
EXPORT(core_taps) uint32_t core_taps(uint32_t on)
{
    uint32_t was = taps_on;
    if (on && !taps_on) ring_r = ring_w;
    taps_on = (uint8_t)(on != 0u);
    return was;
}
/* n frames (<= 4096) as core_render, then each tap: n interleaved stereo frames for the master, then n for each
 * audio track 1..NPART (silent when not in use or a MIDI-only one), then n for the FX return. 0: taps are off */
EXPORT(core_render_taps) float *core_render_taps(uint32_t n)
{
    if (!taps_on) return 0;
    if (n > 4096u) n = 4096u;
    while (ring_w - ring_r < n) core_tick();
    for (uint32_t k = 0; k < n; k++) {
        uint32_t at = ring_r++ % RING;
        tapped[2u * k] = ring[2u * at];
        tapped[2u * k + 1u] = ring[2u * at + 1u];
        for (uint32_t t = 0; t < TAPS; t++) {
            float *d = tapped + 2u * n * (1u + t) + 2u * k;
            d[0] = tap_ring[at][2u * t];
            d[1] = tap_ring[at][2u * t + 1u];
        }
    }
    core_drain();
    return tapped;
}

/* a project record (the expanded form, BANK_STORE_SIZE bytes; mel_file.js pjExpand) saved to slot (0..31: projects
 * 1..32) as SAVE would, then loaded as LOAD would. 0 = loaded, 1 = not a record or no such slot, 2 = not written,
 * 3 = not loaded, 4 = busy (the transport would not stop) */
EXPORT(core_load_record) uint32_t core_load_record(const uint8_t *p, uint32_t n, uint32_t slot)
{
    if (n != BANK_STORE_SIZE || !bank_valid(p, n) || slot >= PROJ_SLOTS) return 1;
    if (ed_flash_stop()) return 4;
    if (proj_write_slot(slot, p, n)) return 2;
    project_load(slot);
    if (proj_cur != slot) return 3;
    song.g[G_SLOT] = (int16_t)(slot + 1u);
    return 0;
}
/* a data record (a user kit: KIT1, as DRUM's drum_enc.c / melodee_kit.wasm write it) into record slot u (0..7: KIT
 * 9..16), as the editor's BACKUP PUT of ids 92..99 commits it; n 0 empties the slot. 0 = done, 1 = not a record its
 * synth could play (or no such slot), 2 = the flash refused it */
EXPORT(core_load_kit) uint32_t core_load_kit(const uint8_t *p, uint32_t n, uint32_t u)
{
    st_stream_t w;
    int rc;
    if (u >= ST_NREC) return 1;
    if (!n) return rec_delete(u) ? 2u : 0u;
    if (!rec_len_ok(n)) return 1;
    if (rec_put_begin(&w, u, n) || rec_put_write(&w, p, n)) return 2;
    rc = rec_put_commit(&w, u);
    return rc == -2 ? 1u : rc ? 2u : 0u;
}

/* SONG from row (as SONG's PLAY): the internal clock, no loop, so it ends. 0 = started, else chain_prepare_from's */
EXPORT(core_song_prepare) uint32_t core_song_prepare(uint32_t row)
{
    if (ed_flash_stop()) return 2;
    song.g[G_CLOCK] = 0;                                /* CLK INT: nothing else would advance it */
    chain_config.rsv[CHAIN_LOOP] = 0;
    return chain_prepare_from(row);
}
/* bit 0 playing, 1 chain running, 2 armed (starts at the next block), 3 ended; bits 8..15 the row */
EXPORT(core_song_state) uint32_t core_song_state(void)
{
    uint32_t ended = !chain.running && !song.playing && !chain.armed && transport_req != 1u;
    return (uint32_t)song.playing | (uint32_t)chain.running << 1 | (uint32_t)chain.armed << 2 | ended << 3 |
           (uint32_t)chain.row << 8;
}
/* the song from row to its end, in frames of sound at its tempo (song_chain.c chain_row_samples: repeats included);
 * a render of it takes this long, then the tails */
EXPORT(core_song_samples) uint32_t core_song_samples(uint32_t row)
{
    uint64_t units = 0;
    uint32_t bpm = (uint32_t)clamp(song.g[G_BPM], 1, 999);
    for (uint32_t r = row; r < chain_config.count; r++) units += chain_row_samples(r);
    return chain_div_samples(units * 60u * FS + MIDI_BEAT_UNITS * bpm / 2u, MIDI_BEAT_UNITS * bpm, 0);
}
EXPORT(core_stop) uint32_t core_stop(void) { return (uint32_t)ed_flash_stop(); }
/* the master without its limiter and soft clip (1; the DC blocker stays), or as on the FM-1 (0, the default): an
 * export's choice, so its sound may go past full scale (1.0). The setting it had */
EXPORT(core_master_raw) uint32_t core_master_raw(uint32_t on)
{
    uint32_t was = master_raw;
    master_raw = (uint8_t)(on != 0u);
    return was;
}

/* which tracks sound (bit k: track k + 1), as the editor's TRACKS solo: the latch, never P_MUTE; 0 = all. At once:
 * mix.c's 128-sample solo ramp only moves while a track sounds, so a render's solo would let a silenced track's
 * first note through for 3 ms. The mask applied (bits past the tracks in use dropped) */
EXPORT(core_solo) uint32_t core_solo(uint32_t mask)
{
    track_solo_latch = (uint8_t)(mask & ((1u << trk_n()) - 1u));
    track_solo = track_solo_latch;
    for (uint32_t k = 0; k < NPART; k++)
        solo_gain[k] = !trk[k].p[P_MUTE] && (!track_solo || ((track_solo >> k) & 1u)) ? 32768 : 0;
    return track_solo;
}
/* CLK INT (the FM-1 makes its own tempo): a browser has no USB or TRS clock to follow. The clock it had */
EXPORT(core_clock_internal) uint32_t core_clock_internal(void)
{
    uint32_t was = (uint32_t)song.g[G_CLOCK];
    song.g[G_CLOCK] = 0;
    return was;
}

/* MIDI to the FM-1 as from a computer over USB (usb.c midi_in_event): channel messages, real time; running status
 * kept across calls. SysEx goes to core_sysex_in. The messages taken */
static uint8_t mi_status, mi_data[2], mi_have;
EXPORT(core_midi_in) uint32_t core_midi_in(const uint8_t *p, uint32_t n)
{
    uint32_t taken = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t b = p[i];
        if (b >= 0xF8u) { midi_in_event(0xFu | b << 8); taken++; continue; }
        if (b >= 0xF0u) { mi_status = 0; mi_have = 0; continue; }   /* (system common, SysEx: not here) */
        if (b & 0x80u) { mi_status = (uint8_t)b; mi_have = 0; continue; }
        if (!mi_status) continue;
        mi_data[mi_have++] = (uint8_t)b;
        uint32_t need = (mi_status & 0xE0u) == 0xC0u ? 1u : 2u;     /* program change, channel pressure: one */
        if (mi_have < need) continue;
        midi_in_event((uint32_t)(mi_status >> 4) | (uint32_t)mi_status << 8 | (uint32_t)mi_data[0] << 16 |
                      (need > 1u ? (uint32_t)mi_data[1] << 24 : 0u));
        mi_have = 0;
        taken++;
    }
    return taken;
}
/* what core_midi_events keeps: bit 0 the tracks' notes, bit 1 the FM-1's USB MIDI out, bit 2 the values motion
 * sets; 0 = none (the default). The mask it had */
EXPORT(core_midi_tap) uint32_t core_midi_tap(uint32_t mask)
{
    uint32_t was = ev_mask;
    ev_mask = mask & 7u;
    if (!ev_mask) ev_r = ev_w;
    return was;
}
/* up to max events (8 bytes each, as ev_q above) into p; the count */
EXPORT(core_midi_events) uint32_t core_midi_events(uint8_t *p, uint32_t max)
{
    uint32_t k = 0;
    while (k < max && ev_r != ev_w) {
        memcpy(p + 8u * k, &ev_q[ev_r++ % EVQ], 8u);
        k++;
    }
    return k;
}

/* the meters since the last read: each track's peak (after level, solo and mute; 1.0 = Q15 full scale; the screen's
 * reader of mix.c trk_peak_take, so the editor's METERS keep theirs), the master's left and right, then the
 * transport as TRANSPORT's state (1 playing, 2 the song, 4 counting in, 8 armed) | the song's row << 8, and track 1's
 * step */
static float meters[NTRK + 4u];
EXPORT(core_meters) float *core_meters(void)
{
    for (uint32_t k = 0; k < NTRK; k++) meters[k] = (float)trk_peak_take(k, 0) * METER_SCALE;
    meters[NTRK] = (float)master_peak[0] * OUT_SCALE;
    meters[NTRK + 1u] = (float)master_peak[1] * OUT_SCALE;
    master_peak[0] = master_peak[1] = 0;
    meters[NTRK + 2u] = (float)(ed_tp_state() | (uint32_t)chain.row << 8);
    meters[NTRK + 3u] = (float)trk[0].seq_idx;
    return meters;
}

/* counters: [0] ISR ms, [1] watchdog feeds, [2] NOR erases, [3] NOR programs, [4] reply bytes lost, [5] frames
 * dropped unheard, [6] the flash state (flash_ok), [7] projects used, [8] MIDI events lost, [9] frames made */
static uint32_t stats[10];
EXPORT(core_stats) uint32_t *core_stats(void)
{
    uint32_t used = 0;
    for (uint32_t i = 0; i < PROJ_SLOTS; i++) used += proj_meta[i].used != 0;
    stats[0] = fm1_ms; stats[1] = wdt_feeds; stats[2] = nor_erases; stats[3] = nor_progs;
    stats[4] = out_lost; stats[5] = ring_lost; stats[6] = flash_ok; stats[7] = used;
    stats[8] = ev_lost; stats[9] = frames_made;
    return stats;
}

#ifdef CORE_NATIVE_MAIN
/* the same core natively:
 *   core_native < frames       SysEx frames (F0 .. F7, one after another), each reply as a hex line: "> frame",
 *                              then "< reply" for every frame the core sent back before the next request
 *   core_native render REC SECONDS OUT   a project record (expanded) loaded to slot 0, its song from row 0, SECONDS
 *                              of it as interleaved float (raw) in OUT, as web/core/core_check.mjs renders it in WASM
 *   core_native synth E P OUT  preset P of engine E on track 1, a chord held 1 s then let go, 2 s of sound (raw float):
 *                              each synth module as core_check.mjs plays it in WASM */
#ifdef CORE_MODULES
static int boot_modules(void);
#endif
#define SYNTH_CHORD {0x90u, 48u, 100u, 0x90u, 55u, 90u, 0x90u, 64u, 80u}
#define SYNTH_OFF {0x80u, 48u, 0u, 0x80u, 55u, 0u, 0x80u, 64u, 0u}
static int synth_main(uint32_t e, uint32_t p, const char *out)
{
    static const uint8_t on[] = SYNTH_CHORD, off[] = SYNTH_OFF;
    uint8_t f[] = {0xF0u, 0x7Du, 0x46u, 0x4Cu, 8u, (uint8_t)e, (uint8_t)(p & 127u), (uint8_t)(p >> 7), 0xF7u};
    FILE *o;
#ifdef CORE_MODULES
    if (boot_modules()) return 1;
#else
    core_boot(0);
    core_poll(100);
#endif
    if (p < 128u) { f[7] = 0xF7u; core_sysex_in(f, 8u); } else core_sysex_in(f, 9u);
    core_sysex_out(scratch, sizeof scratch);
    core_poll(30);
    if (!(o = fopen(out, "wb"))) return 2;
    core_midi_in(on, sizeof on);
    for (uint32_t k = 0; k < 3u * FS; k += 1024u) {
        if (k == FS - FS % 1024u) core_midi_in(off, sizeof off);
        fwrite(core_render(1024u), sizeof(float), 2u * 1024u, o);
    }
    fclose(o);
    return 0;
}
static int render_main(const char *rec, uint32_t secs, const char *out)
{
    static uint8_t r[BANK_STORE_SIZE + 1u];
    FILE *f = fopen(rec, "rb");
    if (!f) return 2;
    uint32_t n = (uint32_t)fread(r, 1, sizeof r, f), rc;
    fclose(f);
#ifdef CORE_MODULES
    if (boot_modules()) return 1;
#else
    core_boot(0);
    core_poll(100);
#endif
    if ((rc = core_load_record(r, n, 0)) != 0) { printf("core: load %u\n", (unsigned)rc); return 1; }
    core_poll(30);
    if ((rc = core_song_prepare(0)) != 0) { printf("core: song %u\n", (unsigned)rc); return 1; }
    if (!(f = fopen(out, "wb"))) return 2;
    for (uint32_t left = secs * FS; left;) {
        uint32_t k = left > 1024u ? 1024u : left;
        fwrite(core_render(k), sizeof(float), 2u * k, f);
        left -= k;
    }
    fclose(f);
    printf("core: rendered %u s, state %#x\n", (unsigned)secs, (unsigned)core_song_state());
    return 0;
}
#ifdef CORE_MODULES
extern const mod_desc_v3 mod_sid, mod_drum, mod_cz, mod_fm6, mod_p5;   /* (the module objects, linked beside) */
/* the five synths: their objects named, the device's files (CORE_MODULES_DIR, else build/modules) in the pool of a new
 * device, then booted again (an installed FM-1's power-on), as web/core/core_check.mjs does in WebAssembly */
static int boot_modules(void)
{
    static const char *const stem[] = {"sid", "drum", "cz", "fm6", "prophet"};
    static const mod_desc_v3 *const desc[] = {&mod_sid, &mod_drum, &mod_cz, &mod_fm6, &mod_p5};
    const char *dir = getenv("CORE_MODULES_DIR");
    core_boot(0);
    core_poll(100);
    for (uint32_t i = 0; i < 5u; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.mmod", dir ? dir : "build/modules", stem[i]);
        FILE *f = fopen(path, "rb");
        if (!f) { printf("core: no %s\n", path); return 1; }
        uint32_t n = (uint32_t)fread(scratch, 1, sizeof scratch, f);
        fclose(f);
        int32_t rc = 0;
        if (core_mod_link(desc[i], core_head_uid(scratch, n)) || (rc = core_mod_install(scratch, n)) < 0) { printf("core: %s not installed (%d)\n", path, (int)rc); return 1; }
    }
    core_poll(100);
    core_boot(1);
    core_poll(100);
    return 0;
}
#endif
int main(int argc, char **argv)
{
    static uint8_t in[1 << 20], out[1 << 18];
    if (argc == 5 && !strcmp(argv[1], "render")) return render_main(argv[2], (uint32_t)atoi(argv[3]), argv[4]);
    if (argc == 5 && !strcmp(argv[1], "synth")) return synth_main((uint32_t)atoi(argv[2]), (uint32_t)atoi(argv[3]), argv[4]);
    size_t n = fread(in, 1, sizeof in, stdin), at = 0;
#ifdef CORE_MODULES
    if (boot_modules()) return 1;
#else
    core_boot(0);
    core_poll(100);
#endif
    while (at < n) {
        size_t e = at;
        while (e < n && in[e] != 0xF7u) e++;
        if (e == n) break;
        printf(">");
        for (size_t i = at; i <= e; i++) printf(" %02X", in[i]);
        printf("\n");
        core_sysex_in(in + at, (uint32_t)(e + 1u - at));
        uint32_t k = core_sysex_out(out, sizeof out);
        for (uint32_t i = 0; i < k; i++) {
            if (out[i] == 0xF0u) printf("<");
            printf(" %02X", out[i]);
            if (out[i] == 0xF7u) printf("\n");
        }
        at = e + 1u;
    }
    return 0;
}
#endif
