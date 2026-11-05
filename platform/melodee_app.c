/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee on fm1-base: the instrument as one unit (firmware/src, as firmware/src/melodee.c includes it), with the
 * platform's half in app.c. The base owns the main loop, USB (MIDI, audio, host), the update, the TRS jack's reader,
 * the LCD driver, the audio interrupt and the boot; what Melodee's modules called of its own HAL comes here from the
 * bridge (bridge.h). Included modules keep their own notices and licences. */
#include "bridge.h"

#ifdef PLAT_HOST
#undef MELODEE_DUAL_CORE
#define MELODEE_DUAL_CORE 0
#endif
#define MELODEE_CACHE_RAM 0
#ifdef MELODEE_SMK37
#define BOARD_SMK37 1
#endif
#include "../firmware/hal/fm1_cc.h"
#include "../firmware/src/build_options.h"
#ifdef PLAT_HOST
#include <string.h>
#define __attribute__(x)
#endif
#define MELODEE_FM1_BASE 1
#define MELODEE_FLASH 1
#define MELODEE_OTA 1           /* the editor's SysEx (editor.c); the update itself is the base's */
#define MELODEE_OTA_DRYRUN 0
#define MELODEE_USB_AUDIO 0     /* the streams are the base's */
#define MELODEE_USB_HOST 0      /* the base's (fm1.toml usb_host): uh below is its state for the UI */
#define MELODEE_CDC 0
#define MELODEE_UART 0          /* the TRS jack's reader is the base's: its messages come with plat_midi_in */
#define MELODEE_BLE 0           /* (the own stack through the bridge: not yet) */
#ifndef MELODEE_ID
#define MELODEE_ID "FM-1_900"
#endif
#ifdef APP_VERSION
#define MELODEE_VERSION "v" APP_VERSION
#else
#define MELODEE_VERSION "v1.7.0-base-dev"
#endif
#define MELODEE_RES __attribute__((section(".mres"), aligned(4)))
#define RES_OK() (res_ok != 0u)
static uint8_t res_ok;                  /* res.c: installed and this build's; else the readers fall back */
#ifdef PLAT_HOST
/* the host: no resource region in a flash; the MELODEE_RES data are ordinary constants there (no section), read as
 * installed (mlb_init) */
const uint8_t _mres_start[1], _mres_end[1];
#endif
#define panel ml_instrument_panel
#define settings ml_instrument_settings
#define bootguard ml_instrument_bootguard

/* ---- time, interrupts, watchdog (fm1_time.h, fm1_irq.h, fm1_sys.h) */
#define FM1_TICKS_PER_US 24u
#ifndef PLAT_HOST
#define FM1_IRQ_TARGET 1
#endif
static uint32_t fm1_ticks(void){return ml_ticks();}
static uint32_t irq_save(void){return ml_lock();}
static void irq_restore(uint32_t token){ml_unlock(token);}
static uint32_t ml_irq_depth,ml_irq_token;
static void fm1_irq_off(void){if(!ml_irq_depth++)ml_irq_token=ml_lock();}
static void fm1_irq_on(void){if(ml_irq_depth&&! --ml_irq_depth)ml_unlock(ml_irq_token);}
static uint32_t fm1_icfg(void){return ml_lock();}
static void fm1_icfg_set(uint32_t token){ml_unlock(token);}
static void fm1_wdt_feed(void){ml_watchdog();}
static void fm1_delay_ms(uint32_t n){ml_delay(n);}
static void fm1_reboot(void){ml_reboot();}
#ifndef PLAT_HOST
#include "hardware/cpu1.h"
#else
static void fm1_aw_relax(void){}
#endif

/* ---- the panel: buttons, keys, encoders, LEDs (fm1_input.h); fed by ml_poll from the base's scan */
#define FM1_NCOL 11u
#define FM1_NKEY 41u
#define FM1_NENC 7u
#define FM1_NBTN 14u
static const int8_t FM1_KEYMAP[6][FM1_NCOL]={
    {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {5,11,4,10,3,9,2,8,-1,-1,-1},
    {34,35,36,37,38,40,39,13,7,6,12},
    {23,24,25,26,27,28,29,30,31,32,33},
    {0,1,15,14,17,16,19,18,20,21,22},
    {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}
};
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };
#define FM1_OCT_BOTH (1u << FM1_BTN_OCT_DOWN | 1u << FM1_BTN_OCT_UP)
static volatile struct {
    uint32_t notes, buttons, remote, remote_tap, notes_retrig;
    uint32_t note_t[27];
} fm1_in;
static uint32_t ml_pressed,ml_released,ml_notes;
static int32_t ml_enc[7];
static uint8_t fm1_led[11],fm1_led_dim[3][11],fm1_led_dim_mask[3];
static uint32_t fm1_input_edges(uint32_t *released){uint32_t p=ml_pressed;if(released)*released=ml_released;ml_pressed=ml_released=0;return p;}   /* (released may be 0) */
static uint32_t fm1_input_note_edges(void){uint32_t p=ml_notes;ml_notes=0;return p;}
static int32_t fm1_enc_take(uint32_t i){int32_t n=ml_enc[i%7u];ml_enc[i%7u]=0;return n;}

/* ---- the LCD (lcd.c): the base's driver */
static void lcd_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t c){ml_fill(x,y,w,h,c);}
#define lcd_blit ml_blit
#define lcd_sync ml_sync
#define UI_LCD_POWER(s) ml_lcd_power(s)
#define UI_LCD_WAKE() ml_lcd_power(2)

#define memset melodee_memset
#define memcpy melodee_memcpy
#define memcmp melodee_memcmp
#include "../firmware/src/libc.c"
#include "melodee_tables.h"
#include "../firmware/src/gfx.c"
#include "../firmware/src/core.h"
#include "../firmware/src/engines.c"
#include "../firmware/src/params.c"
#include "../firmware/src/mod.c"
#include "../firmware/src/voice.c"
#include "../firmware/src/mix.c"

/* ---- MIDI over the base (firmware/src/usb.c's rings and their admission, without its USB) */
#define MIDI_TIME_NOW() fm1_ticks()
#define MIDI_TICKS_PER_MS (1000u * FM1_TICKS_PER_US)
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
/* what the modules read of the USB device: up, configured, awake, and the counters the editor shows */
static struct {
    uint8_t up, config;
    volatile uint8_t suspended;
    uint32_t resets, setups, rx_pkts, tx_pkts, sof_seen;
    volatile uint8_t uboot_req, ota_req;
} usb = {1, 0, 0};
#define MQ 64u
static uint32_t midi_in_q[MQ];
static uint32_t midi_in_ms[MQ];
static uint32_t midi_in_time[MQ];
static uint8_t midi_in_source[MQ];                    /* 1 USB (and the USB host's device), 2 TRS, 3 Bluetooth */
static uint16_t midi_in_pulse[MQ], midi_rx_pulses[3];
static volatile uint32_t mi_w, mi_r;
static volatile uint8_t midi_in_overflow;
static volatile uint8_t midi_usb_lost;
static uint32_t midi_render_time;
static uint8_t midi_render_timed;
static int midi_enqueue_at(uint32_t pkt, uint32_t source, uint32_t t24)
{
    uint32_t at = mi_w % MQ;
    uint32_t clock_source = source >= 1u && source <= 3u ? source - 1u : 0u;
    if (((pkt >> 8) & 0xFFu) == 0xF8u)
        midi_rx_pulses[clock_source]++;
    if (midi_in_overflow)
        return 0;
    if (mi_w - mi_r >= MQ) {
        midi_in_overflow = 1;
        return 0;
    }
    midi_in_q[at] = pkt;
    midi_in_ms[at] = fm1_ms;
    midi_in_time[at] = t24;
    midi_in_source[at] = (uint8_t)source;
    midi_in_pulse[at] = midi_rx_pulses[clock_source];
    RING_PUBLISH();
    mi_w++;
    return 1;
}
static int midi_enqueue(uint32_t pkt, uint32_t source) { return midi_enqueue_at(pkt, source, MIDI_TIME_NOW()); }
static int usb_midi_out_ready(void) { return usb.config != 0; }
/* From the audio ISR: whole or not at all (voice.c: a note-on that cannot go out does not sound held there) */
static int midi_track_emit(uint32_t pkt)
{
    if (!ml_midi_ready() || ml_midi_room() < 1u) return 0;
    ml_midi_out(pkt);
    return 1;
}
static void midi_out_event(uint32_t pkt) { (void)midi_track_emit(pkt); }
static void midi_sync_send(uint32_t pkt)            /* clock, transport, Song Position: four slots kept for transport */
{
    uint32_t pulse = ((pkt >> 8) & 0xFFu) == 0xF8u;
    if (ml_midi_ready() && ml_midi_room() > (pulse ? 4u : 0u)) ml_midi_out(pkt);
}
static void midi_sync_emit(uint32_t st) { midi_sync_send(0xFu | st << 8); }

/* the editor's SysEx (editor.c, ed_service): one frame at a time, the bytes between F0 and F7, from app_sysex */
static const uint8_t *ml_sx_p;
static uint32_t ml_sx_n;
static uint8_t ml_sx_ready;
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!ml_sx_ready) return 0;
    *p = ml_sx_p;
    *n = ml_sx_n;
    return 1;
}
static void ota_frame_done(void) { ml_sx_ready = 0; }
static int ota_wire_send(const uint8_t *p, uint32_t n) { return ml_sysex_send(p, n); }
/* editor.c ed_room: the SysEx ring at most half full, room for a reply: in packets of three bytes, as usb.c's ring */
#define SXQ 64u
#define so_w (ml_sysex_queued() / 3u)
#define so_r 0u

/* the menu's USB AUDIO, USB INPUT and USB VOLUME (usb_audio.c, MELODEE_USB_AUDIO) are not built yet: the base's
 * streams are fixed by platform/fm1.toml (both directions, the stereo mix before MASTER, the computer's at full) */

/* the menu's USB HOST (usb_host.c): the base's (fm1.toml usb_host), its state for the UI */
static struct { uint8_t want, on, run, msg; } uh;
enum { UH_MSG_NONE, UH_MSG_MIDI, UH_MSG_LOST, UH_MSG_NOMIDI, UH_MSG_LOW, UH_MSG_NOLINE, UH_MSG_NOANSWER,
       UH_MSG_HUB_EMPTY, UH_MSG_HUB_NOMIDI };

#include "../firmware/src/song_chain.c"
#include "../firmware/src/seq.c"

/* ---- the render (firmware/src/audio.c's, called by the base's audio interrupt: app_render) */
#define DAC_TICKS 544u
#define OUT_SHIFT 8               /* Q15 -> 24 bit at full scale: the base's -6 dB ceiling (platform/ceiling.h) follows the
                                   * USB recording, as Melodee's own OUT_SHIFT 7 followed its capture */
#define DBG_MAGIC 0x44424731u
struct melodee_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, page, home;
    uint32_t prev_stage, prev_page, prev_home, prev_rst, prev_frames;
} melodee_dbg __attribute__((section(".noinit")));
static uint32_t audio_full_last_us, audio_full_max_us, audio_deadline_misses, audio_target_exceeded;
static uint32_t audio_irq_max_ticks, audio_timer5_max_ticks;
static uint32_t ml_render_max_us;
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
static void audio_silence(void) {}                  /* the base keeps the codec quiet while the flash is busy */
static volatile uint8_t shed_req;
static uint32_t shed_count;
static uint32_t shed_part[NPART];
static void shed_voice(void)
{
    uint32_t p, i;
    voice_t *best = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && !v->gate && v->stage != 4u && (!best || v->env < best->env))
                best = v;
        }
    if (best) {
        voice_kill(best);
        shed_count++;
        shed_part[voice_part(best) % NPART]++;
        return;
    }
    for (p = 0; p < NPART; p++) {
        const track_t *t = &trk[p];
        uint32_t mode = trk_vmode(t), low = mode == V_POLY ? lowest_held(t) : 0u;
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && v->gate && v->stage != 4u &&
                ((mode == V_POLY && i != low) || (mode != V_POLY && i > 0u))) {
                if (!best || v->age < best->age)
                    best = v;
            }
        }
    }
    if (best) {
        voice_kill(best);
        shed_count++;
        shed_part[voice_part(best) % NPART]++;
    }
}
void mlb_overload(void) { shed_req = 1; }          /* the base: a render over 90 % of its time */
void mlb_deadline(uint32_t us, uint32_t budget)
{
    if (us > ml_render_max_us) ml_render_max_us = us;
    if (us > melodee_dbg.max_us) melodee_dbg.max_us = us;
    melodee_dbg.last_us = us;
    melodee_dbg.halves++;
    song.cpu_q8 = (song.cpu_q8 * 31u + (budget ? us * 256u / budget : 0u)) / 32u;
    melodee_dbg.cpu_q8 = song.cpu_q8;
    audio_full_last_us = us;
    if (us > audio_full_max_us) audio_full_max_us = us;
    audio_deadline_misses += us > budget;
    audio_target_exceeded += us * 5u > budget * 4u;
}
static void audio_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    mix_block(out, n);
    click_render(out, n);
    for (i = 0; i < n; i++) {
        if (i & 1u)
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)out[2u * i];
        out[2u * i] *= 1 << OUT_SHIFT;
        out[2u * i + 1u] *= 1 << OUT_SHIFT;
    }
}
void mlb_render(int32_t *out, uint32_t n, uint32_t gain)
{
    uint32_t t0, b;
    melodee_dbg.in_audio = 1;
    fm1_ms = ml_ms();
    midi_master_physical(gain);                     /* MASTER (the base gives 4096 with the USB recording at full) */
    if (shed_req) {
        shed_req = 0;
        if (settings_safety)                        /* GLO > WEIGHTS 2 SAFETY OFF: overload cuts nothing */
            shed_voice();
    }
#if MELODEE_DUAL_CORE
    audio_worker_check();
#endif
    t0 = fm1_ticks();
    for (b = 0; b < n; b += CTL) {
        midi_render_time = t0 - (n - b) * DAC_TICKS;
        midi_render_timed = 1;
        audio_block(out + 2u * b, CTL);
    }
    midi_render_timed = 0;
    melodee_dbg.in_audio = 0;
}

/* ---- the UI */
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_graph.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/ui_layer.c"

/* ---- the store (firmware/src/storage_hw.c's st_*, over the bridge: Melodee's windows, store_map.h) */
#include "../firmware/hal/fm1_flash_map.h"
static uint8_t flash_ok;
static int st_read(uint32_t off, void *p, uint32_t n) { return ml_read(off, p, n); }
static int st_erase_raw(uint32_t off) { return ml_erase(off); }
static int st_erase(uint32_t off) { return FL_STORE_OK(off, 0x1000u) ? ml_erase(off) : -8; }
static int st_prog(uint32_t off, const void *p, uint32_t n) { return FL_STORE_OK(off, n) ? ml_write(off, p, n) : -8; }
static uint32_t fl_jedec_ram(void) { return ml_flash_id(); }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
static const uint8_t *fm1_xip_ptr(uint32_t off) { return ml_flash(off); }
#include "../firmware/src/storage.c"
#include "../firmware/src/res.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#include "../firmware/src/cz_store.c"
#include "../firmware/src/fm6_store.c"

/* ---- the editor */
#define OTA_SCRATCH proj_wire_borrow()
/* the development readout (F0 7D 4C 47 F7) is the base's own (device/main_fm1.c log_frame) */
#define INSTALLER_REPORT_FRAME(p, n) 0
#include "../firmware/src/editor.c"

/* ---- power-on, the frame, the input (firmware/src/main.c's, without the hardware the base brings up) */
static void clock_boot(void)
{
#ifndef PLAT_HOST
    uint32_t m = fm1_clock_mult();
    vbudget_cpu1 = VBUDGET * (m ? fm1_clock_mhz_of(m) : 360u) / 360u;
#endif
}
static void melodee_init(void)                    /* the parts with their default sounds (TRK_DEF); the sequencers empty */
{
    uint32_t i;
    trk_voices_bind();
    midi_insert_init();
    punch_reset();
    chain_defaults(&chain_config);
    pattern_init();
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    undo_depth++;
    fm6_init();
    cz_init();
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
static void ml_poll(void)
{
    uint32_t u[5], buttons, keys, packet, i;
    int32_t enc[7];
    int port;
    fm1_ms = ml_ms();
    ml_usb_state(u);
    usb.config = (uint8_t)u[0]; usb.resets = u[1]; usb.up = (uint8_t)u[2]; usb.suspended = (uint8_t)u[3]; usb.rx_pkts = u[4];
    if (ml_usb_lost()) midi_usb_lost = 1;
    ml_input(&buttons, &keys, enc);
    buttons |= fm1_in.remote | fm1_in.remote_tap;  /* MIDI LEARN: a controller's button, closed as a panel button */
    fm1_in.remote_tap = 0;
    ml_pressed |= buttons & ~fm1_in.buttons; ml_released |= fm1_in.buttons & ~buttons;
    ml_notes |= keys & ~fm1_in.notes;
    for (i = 0; i < 27u; i++) if ((keys & ~fm1_in.notes) >> i & 1u) fm1_in.note_t[i] = fm1_ticks();
    fm1_in.buttons = buttons; fm1_in.notes = keys;
    for (i = 0; i < 7u; i++) ml_enc[i] += enc[i];
    while (ml_midi_in(&packet, &port))              /* port: 1 USB (and the host's device), 2 TRS, 3 Bluetooth */
        (void)midi_enqueue_at(packet, (uint32_t)port, ml_midi_t24());
}
static void ml_host_messages(void)
{
    static const char *const UH_TEXT[][2] = {
        [UH_MSG_MIDI] = {"USB MIDI CONNECTED", ""},
        [UH_MSG_LOST] = {"USB MIDI DISCONNECTED", ""},
        [UH_MSG_NOMIDI] = {"NOT A USB-MIDI DEVICE", ""},
        [UH_MSG_LOW] = {"LOW-SPEED USB DEVICE", "NOT SUPPORTED"},
    };
    uint32_t st;
    if (uh.want != uh.on) { ml_usb_host(uh.want); uh.on = uh.want; }
    st = (uint32_t)ml_usb_host_state();
    uh.run = st == 2u;
    uh.msg = (uint8_t)ml_usb_host_msg();
    if (uh.msg && uh.msg < sizeof UH_TEXT / sizeof UH_TEXT[0] && UH_TEXT[uh.msg][0]) {
        ui_message(UH_TEXT[uh.msg][0]);
        str_cpy(ui.msg2, UH_TEXT[uh.msg][1], sizeof ui.msg2);
    }
    uh.msg = 0;
}
void mlb_init(void)
{
    fm1_ms = ml_ms();
    settings_init();
    dsp_tables_init();
    flash_ok = (uint8_t)ml_flash_ok();
    persist_boot();
    settings_init();
    clock_boot();
    if (melodee_dbg.magic != DBG_MAGIC) {
        memset(&melodee_dbg, 0, sizeof melodee_dbg);
        melodee_dbg.magic = DBG_MAGIC;
    }
    melodee_dbg.boots++;
    melodee_dbg.max_us = 0;
    melodee_dbg.in_audio = 0;
    panel_init();
    ml_panel_set(panel.btn, panel.enc, panel.dir);
    melodee_init();
#if MELODEE_DUAL_CORE && !defined(PLAT_HOST)
    if (ml_worker_allowed() && !fm1_core1_barred())
        fm1_multicore_start();                      /* bounded handshake; the base arms the PC limits after app_init */
#endif
    ml_usb_full(1);                                 /* the USB recording before MASTER (#103), MASTER for the speaker */
#ifdef PLAT_HOST
    res_ok = 1;
#endif
    glo_restore();
    project_boot();
#ifdef RES_BASE
    if (flash_ok && !res_ok && !boot_work.state) {  /* the resource region missing: ask for an install (ui_boot.c) */
        boot_work.state = 4u;
        ui.force = 1;
    }
#endif
    ml_poll();
}
void mlb_input_only(void) { ml_poll(); ui_input(); }
void mlb_frame(void)
{
    ml_poll();
    wire_release();
    pattern_poll();
    ml_host_messages();
    {
        static uint32_t at;
        if ((uint32_t)(fm1_ms - at) >= 250u) {      /* battery: slow IIR */
            int32_t b = (int32_t)ml_battery();
            at = fm1_ms;
            if (b > 0)
                song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
        }
    }
    cz_service();
    fm6_service();
    melodee_dbg.ui_frames++;
    melodee_dbg.page = ui.page;
    melodee_dbg.home = ui.home;
    melodee_dbg.stage = 1;
    ui_input();
    glo_poll();
    settings_poll();
    melodee_dbg.stage = 2;
    ui_leds();
    ui_draw();
    ml_lights(fm1_led, &fm1_led_dim[0][0], fm1_led_dim_mask);
    melodee_dbg.stage = 9;
}
void mlb_panic(void) { panic_req = (1u << NTRK) - 1u; }
void mlb_say(const char *a, const char *b) { ui_say(a, b); ui.force = 1; }
int mlb_sysex(const uint8_t *p, uint32_t n)
{
    ml_sx_p = p;
    ml_sx_n = n;
    ml_sx_ready = 1;
    ed_service();                                   /* takes it (or drops it) now, while p is the base's */
    ml_sx_ready = 0;
    return 1;
}
int mlb_expect(const char *s, int *v)
{
    if (str_eq(s, "engine")) { *v = TSEL->eng_req; return 1; }
    if (str_eq(s, "playing")) { *v = song.playing; return 1; }
    if (str_eq(s, "track")) { *v = song.sel; return 1; }
    if (str_eq(s, "clock_source")) { *v = song.g[G_CLOCK]; return 1; }
    return 0;
}
void mlb_sysex_byte(uint8_t byte) { fm6_sx_byte(byte); cz_sx_byte(byte); }
void mlb_panel_calibrated(const uint8_t *buttons, const uint8_t *enc, const int8_t *direction)
{
    memcpy(panel.btn, buttons, 14u); memcpy(panel.enc, enc, 7u); memcpy(panel.dir, direction, 7u);
    panel.magic = PANEL_MAGIC; panel_init(); settings_save();
}
/* The base's own screens (boot, crash, safe mode, calibration) in Melodee's faces, on the base's canvas */
static const aafont_t *ml_sys_face(int face) { return face == 2 ? TY_VL : face == 1 ? TY_HR : TY_TX; }
int32_t mlb_sys_h(int face) { return ml_sys_face(face)->h; }
int32_t mlb_sys_w(int face, const char *s) { return text_w(ml_sys_face(face), s); }
void mlb_sys_text(uint16_t *px, uint32_t w, uint32_t h, int32_t x, int32_t y, int face, const char *s, uint16_t fg)
{
    uint16_t *keep_px = cv_px;
    uint32_t keep_w = cv_w, keep_h = cv_h, keep_top = cv_top, keep_rows = cv_rows;
    int16_t keep_c0 = cv_cy0, keep_c1 = cv_cy1;
    int32_t keep_oy = cv_oy;
    cv_px = px; cv_w = w; cv_h = h; cv_top = 0; cv_rows = h; cv_cy0 = 0; cv_cy1 = (int16_t)h; cv_oy = 0;
    cv_text_flags(x, y, ml_sys_face(face), s, fg, swap16(px[0]), 0);
    cv_px = keep_px; cv_w = keep_w; cv_h = keep_h; cv_top = keep_top; cv_rows = keep_rows; cv_cy0 = keep_c0;
    cv_cy1 = keep_c1; cv_oy = keep_oy;
}
