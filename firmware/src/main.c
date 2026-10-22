/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MELODEE boot and main loop. Boot order: WDT first, boot-loop guard, fatal vectors,
 * guards; then LCD, input (TIMER5 IRQ, 10 kHz), audio (ALNK0 IRQ). */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _dsp_start[], _dsp_end[], _dsp_load[], _dt_start[], _dt_end[], _dt_load[];
extern uint32_t _audio_start[], _audio_end[], _audio_load[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];


/* TIMER5 outranks ALNK0, so the scan keeps its 100 us pace while a half buffer renders: before,
 * the ticks stopped for the whole render (0.7 ms idle, several ms loaded), the column lit when it
 * began stayed lit that long (a ~16 Hz flicker over all LEDs, beating with the scan) and the
 * encoders lost frames. Nested in ALNK0 it scans GPIO, counts ms and receives TRS MIDI;
 * USB MIDI polls wait for the first tick after the render,
 * while UART input keeps polling into its single-producer MIDI queue,
 * and the time spent nested is handed to the audio ISR so its load figures stay render-only.
 * The USB audio streams cannot wait for the render (a packet each way per 1 ms frame, a render takes up to
 * ~5 ms): ua_service also runs nested, by elapsed time (every 250 us at most: work spanning ticks does not
 * stretch the next deadline). It touches only the audio endpoints (INDEX is set on every access) and the
 * USB side of the audio rings (audio.c copies with the IRQs off), and usb_poll never runs nested, so the
 * two never interleave. */
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
    melodee_dbg.timer_irqs++;
    AP_START(input_start);fm1_input_tick();
    if(melodee_dbg.in_audio){AP_END(AP_INPUT,input_start);}
#if BOARD_SMK37
    smo_tick();                                 /* TRS MIDI OUT: at most one byte, never waits */
#endif
    {   /* milliseconds from the 24 MHz TIMER4 (robust to a late tick) */
        static uint32_t last, acc;
        acc += t0 - last;
        last = t0;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
    if (usb_due)
        owed |= 1u;                             /* 2 kHz: all USB SIE traffic lives here */
#if MELODEE_UART
    if (sub % 5u == 2u)
        owed |= 2u;                             /* 2 kHz: <= ~7 bytes per call at 31250 baud */
#endif
    if (++sub == 10u)
        sub = 0;
#if MELODEE_USB_AUDIO
    {
        static uint32_t last_ua;
        uint32_t gap = t0 - last_ua;
        if (!last_ua || gap >= 250u * FM1_TICKS_PER_US) {
            if (last_ua && gap > ua.poll_max_ticks)
                ua.poll_max_ticks = gap;
            last_ua = t0;
            AP_START(usb_start);ua_service();
            if(melodee_dbg.in_audio){AP_END(AP_USB,usb_start);}                       /* at most 4 kHz, independent of coalesced ticks */
            gap = fm1_ticks() - t0;
            if (gap > ua.service_max_ticks)
                ua.service_max_ticks = gap;
        }
    }
#endif
    /* TRS RX only touches its DMA ring and publishes timestamped input. It
     * can safely nest in audio; deferring it made clock jitter depend on load. */
#if MELODEE_UART
    if (owed & 2u) {
        AP_START(midi_start);uart_midi_poll();
        if(melodee_dbg.in_audio){AP_END(AP_MIDI_RX,midi_start);}
        owed &= ~2u;
    }
#endif
    if (melodee_dbg.in_audio) {
        melodee_dbg.nested++;
        uint32_t elapsed=fm1_ticks()-t0;t5_nested_ticks+=elapsed;
        if(elapsed>audio_timer5_max_ticks)audio_timer5_max_ticks=elapsed;
#if MELODEE_AUDIO_PROFILE
        audio_profile_add(AP_TIMER5,elapsed);
#endif
        return;
    }
    if (owed & 1u)
        usb_poll();
    owed = 0;
    /* The ISR footer can be preempted after the C audio-active flag clears.
     * Its conservative bound therefore needs ordinary TIMER5 service too. */
    uint32_t elapsed=fm1_ticks()-t0;
    if(elapsed>audio_timer5_max_ticks)audio_timer5_max_ticks=elapsed;
}
extern void isr_timer5(void);

static void timer5_start(void)                 /* OSC /4 = 6 MHz, PRD 600 -> 10 kHz */
{
    fm1_timer5_start(isr_timer5, 4);   /* above ALNK0 (3): the scan nests into the render (see fm1_timer5_irq) */
}

/* The voices CPU1 + CPU0 carry at the clock running: the SPL's 360 MHz, or a 320 that a soft reset kept from a
 * 1.3.x / 1.4 rc session until power-off (hal/fm1_clock.h: Melodee never writes the PLL) */
static void clock_boot(void)
{
    uint32_t m = fm1_clock_mult();
    vbudget_cpu1 = VBUDGET * (m ? fm1_clock_mhz_of(m) : 360u) / 360u;
}

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

#if BOARD_SMK37
/* the menu's TRS MIDI OUT to the jack's queue (settings load, restore, the menu): ON again tries the UART again */
static void smo_sync(void)
{
    uint32_t on = !(GLO_SELECT & TRS_OUT_OFF);
    if (on != trs_out)
        smo_on(on);
}

/* pads as a step grid (1-8 top row): SEQ held (patterns), the DRUM grid, NAME, the layers' tiles (not SCL: notes) */
static void smk_grid(void)
{
    uint32_t l = ui.layer ? ui.layer : ui.ly;
    smk_pad_grid = (uint8_t)(pattern_keys_on() || grid_on() || (name_on() && !ui.menu) || (l && l != LAYER_SCL));
}

/* the SMK-37's 4 faders, by FADER BANK (ui_input.c smk_fbank):
 *   A LEVEL     the levels of tracks 1..4
 *   B FILTER    the selected track: PROPHET its CUTOFF, RESONANCE, filter ENV AMOUNT, filter DECAY (the panel's
 *               own values); CZ-1 its HOME knobs (line 1's wave and DCW peak, detune, vibrato: the tone's own
 *               values); any other engine its four HOME knobs (the engine's own choice of live controls)
 *   C ENVELOPE  the selected track's amp ATTACK, DECAY, SUSTAIN, RELEASE (PROPHET: its amp envelope; CZ, FM6 and
 *               PHASE have envelopes of their own: no effect there)
 *   D FX        the selected track's FX rack: the MIX of slots 1, 2, 3 and its SEND to the shared FX
 * A fader takes over only once it moves (also after a bank or track change), so nothing jumps. Track parameters
 * go through motion_capture as a knob's do. With NOTE REPEAT (the FM-1's LFO) held a fader's edit is momentary,
 * as a knob's is (ui.c momentary_take): released, the value before comes back, and motion does not record it. */
static void smk_fader_momentary(int16_t *vp, uint32_t native)   /* native: 2 CZ-1, 3 PROPHET (the whole sound) */
{
    uint32_t i, tr = song.sel;
    if (!(fm1_in.buttons & (1u << panel.btn[B_LFO])))
        return;
    if (!momentary.active) {
        momentary.track = (uint8_t)song.sel;
        momentary.active = 1;
    }
    if (native && !momentary.native) {
        fm1_irq_off();
        momentary.native = (uint8_t)native;
        memcpy(momentary.fm, fm6_patch[tr], sizeof momentary.fm);
        memcpy(momentary.fn, fm6_fn[tr], FM6_NFN);
        momentary.on = fm6_on[tr];
        momentary.cz = cz_patch[tr];
        momentary.p5 = *p5_patch_of(TSEL);
        momentary.p5_voice = TSEL->p[P_VOICE];
        momentary.p5_prio = TSEL->p[P_PRIO];
        momentary.p5_detune = TSEL->p[P_DETUNE];
        momentary.p5_alloc = TSEL->p[P_ALLOC];
        fm1_irq_on();
    }
    if (vp) {
        for (i = 0; i < momentary.count && momentary.ptr[i] != vp; i++)
            ;
        if (i == momentary.count && i < P_COUNT + G_COUNT) {
            momentary.ptr[i] = vp;
            momentary.value[i] = *vp;
            momentary.count++;
        }
    }
    ui.pg_down &= (ui_btnmask_t)~(1u << panel.btn[B_LFO]);   /* (the release opens no LFO page) */
}
static void smk_fader_param(track_t *t, uint32_t id, uint32_t v)
{
    const param_desc_t *d = track_desc(t, id);
    smk_fader_momentary(&t->p[id], 0);
    t->p[id] = (int16_t)(d->min + ((int32_t)d->max - d->min) * (int32_t)v / 127);
    if (!momentary.active)
        (void)motion_capture(t, id, t->p[id]);
}
/* CZ-1: fader `slot` sets HOME's knob `slot` (ui.c stage_page), written into the tone as the knob does (ui_input.c) */
static void smk_fader_cz(uint32_t slot, uint32_t v)
{
    const page_t *pg = stage_page();
    int16_t *vp;
    const param_desc_t *d = page_desc(pg, slot, &vp);
    uint8_t raw[CZ_BYTES], old[MO_NATIVE_N];
    uint32_t tr = song.sel % NTRK, f;
    if (!d || !vp || d->max == d->min)
        return;
    smk_fader_momentary(0, 2);
    *vp = (int16_t)(d->min + ((int32_t)d->max - d->min) * (int32_t)v / 127);
    f = motion_guard();
    motion_native_peek(TSEL, old);
    if (cz_ed_put(tr, pg->id[slot], (uint32_t)*vp, raw)) {
        if (!momentary.active)
            cz_compare_take(tr);
        memcpy(cz_patch[tr].raw, raw, 128u);
        native_motion(old);
    }
    motion_unguard(f);
}
static void smk_faders(void)
{
    static const uint8_t P5_FLT[4] = {P5_CUTOFF, P5_RESONANCE, P5_ENV_FILTER, P5_DECAY_FILTER};
    static const uint8_t P5_AMP[4] = {P5_ATTACK_AMP, P5_DECAY_AMP, P5_SUSTAIN_AMP, P5_RELEASE_AMP};
    static const uint8_t ENV[4] = {P_ATK, P_DEC, P_SUS, P_REL}, SENDS[4] = {P_FXMIX, P_FX2MIX, P_FX3MIX, P_FXSEND};
    static uint8_t last[4] = {0xFF, 0xFF, 0xFF, 0xFF}, sel = 0xFF;
    uint32_t i;
    if (smk_fbank_new || sel != song.sel) {               /* a new bank or track: wait for each fader to move */
        smk_fbank_new = 0;
        sel = (uint8_t)song.sel;
        for (i = 0; i < 4u; i++)
            last[i] = 0xFF;
    }
    for (i = 0; i < 4u; i++) {
        uint32_t v = fm1_in.fader[i];
        track_t *t = TSEL;
        const engine_t *e = ENGINES[eng_idx(t->eng_req)];
        int p5 = t->eng_req == ENGI_PROPHET, cz = eng_idx(t->eng_req) == ENGI_CZ;
        if (last[i] == 0xFF || fm1_in.frames < 200u) {   /* (the first readings settle) */
            last[i] = (uint8_t)v;
            continue;
        }
        if (v == last[i])
            continue;
        last[i] = (uint8_t)v;
        switch (smk_fbank) {
        case 1:
            if (p5) {
                smk_fader_momentary(0, 3);
                p5_edit_value(t, P5_FLT[i], (int32_t)v);
            } else if (cz)
                smk_fader_cz(i, v);
            else
                smk_fader_param(t, e->knob[i], v);
            break;
        case 2:
            if (p5) {
                smk_fader_momentary(0, 3);
                p5_edit_value(t, P5_AMP[i], (int32_t)v);
            } else if (!e->ownenv)
                smk_fader_param(t, ENV[i], v);
            break;
        case 3:
            smk_fader_param(t, SENDS[i], v);
            break;
        default:
            if (i < NTRK)
                smk_fader_param(&trk[i], P_LEVEL, v);
            break;
        }
        ui.force = 1;
    }
}
#endif

#if BOARD_SMK37
/* the wheels (stock: mux C address 4 = pitch, 5 = mod), self-calibrating: the pitch wheel's rest is its centre
 * (taken at power-on, followed slowly while it rests), each wheel's range grows to what it has reached (the
 * travel is short: raw ~445..640 pitch, ~474..629 mod on a unit). Pitch: a 14-bit bend with a dead band around
 * the centre; mod: 0..127, up (lower raw) = more. Both go to the selected synth track (voice.c), as a MIDI
 * controller's would, and only when they change, so MIDI IN's bend and CC1 keep working; selecting another
 * track takes them off the old one */

static void smk_wheels(void)
{
    enum { DEAD = 12, SPAN0 = 80, MOD_LO = 485, MOD_HI = 615 };
    static int32_t pc, plo, phi, mlo, mhi, rest, sent_b, sent_m = -1;
    static uint8_t tsel = 0xFF;
    int32_t p = fm1_in.wheel[0], m = fm1_in.wheel[1], d, b, mw;
    track_t *t;
    if (fm1_in.frames < 300u)                     /* (the readings settle) */
        return;
    if (!pc) {
        pc = p;
        plo = p - SPAN0;
        phi = p + SPAN0;
        mlo = m < MOD_LO ? m : MOD_LO;
        mhi = m > MOD_HI ? m : MOD_HI;
    }
    if (p < plo)
        plo = p;
    if (p > phi)
        phi = p;
    if (m < mlo)
        mlo = m;
    if (m > mhi)
        mhi = m;
    d = p - pc;
    if (d > -DEAD && d < DEAD) {
        b = 0;
        if (++rest > 200) {                        /* resting: the centre follows a slow drift */
            pc += d > 0 ? 1 : d < 0 ? -1 : 0;
            rest = 0;
        }
    } else {
        rest = 0;
        b = d > 0 ? (d - DEAD) * 8191 / (phi - pc - DEAD > 1 ? phi - pc - DEAD : 1)
                  : (d + DEAD) * 8192 / (pc - plo - DEAD > 1 ? pc - plo - DEAD : 1);
        b = clamp(b, -8192, 8191);
    }
    mw = mhi - m <= DEAD ? 0 : (mhi - m - DEAD) * 127 / (mhi - mlo - DEAD > 1 ? mhi - mlo - DEAD : 1);
    mw = clamp(mw, 0, 127);
    smk_bend = b;
    if (tsel != song.sel) {                        /* another track: the old one back at rest */
        if (tsel < NTRK && (sent_b || sent_m > 0)) {
            t = &trk[tsel];
            t->bend_raw = 0;
            midi_bend_target[tsel] = 0;
            t->mw = 0;
        }
        tsel = (uint8_t)song.sel;
        sent_b = 0;
        sent_m = -1;
    }
    t = TSEL;
    smk_wheel_trk = (uint8_t)song.sel;
    smk_modw = drum_track(t) ? 0u : (uint8_t)mw;
    if (drum_track(t))                             /* drums ignore bend and the wheel (as MIDI's) */
        return;
    if (b != sent_b) {
        sent_b = b;
        t->bend_raw = (int16_t)b;                  /* PROPHET (its BEND) and FM6 (its DX7 range) read this */
        if (t->eng_req != ENGI_PROPHET)
            midi_bend_target[song.sel % NTRK] = b * (SMK_BEND_ST * 256) / (b < 0 ? 8192 : 8191);
    }
    if (mw != sent_m) {
        sent_m = mw;
        t->mw = (uint8_t)mw;
    }
}
#endif

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
#if MELODEE_DUAL_CORE && MELODEE_CORE1_FALLBACK
    if (audio_worker_online)
        fm1_core1_bar_set();                          /* one core on every boot until power-off */
#endif
    fm1_audio_stop();
    scr_wake_now();
    lcd_fill(0, 0, 240, 240, UI_CRASH_BG);            /* fixed, outside the palettes */
    draw_text_line(0, 10, 240, TY_TX, "Melodee crash", UI_CRASH_INK, UI_CRASH_BG, 1);   /* (large type) */
    hexs(b, c->vec);
    draw_text_line(10, 48, 220, TY_TX, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->pc);
    draw_text_line(10, 76, 220, TY_TX, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->emu);
    draw_text_line(10, 112, 220, TY_TX, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->dbg);
    draw_text_line(10, 140, 220, TY_TX, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->rets);
    draw_text_line(10, 168, 220, TY_TX, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

/* power-on: the parts with their default sounds (TRK_DEF); the sequencers empty */
static void melodee_init(void)
{
    uint32_t i;
    chain_defaults(&chain_config);
    pattern_init();
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    undo_depth++;                             /* (no undo copy of the power-on loads) */
    fm6_init();                               /* every track's FM6 patch: the init voice */
    cz_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);    /* with its sends */
        t->engine = t->eng_req;
        track_defaults_steps(t);              /* (a sound load never touches them) */
        pat_sig[i] = steps_sig(t);            /* (empty: not the user's) */
        pat_last[i] = 0;
    }
    undo_depth--;
    song.sel = 0;
    song.master_q12 = 2048;
    ui.home = 1;
    ui.force = 1;
}

static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    fm1__pc_out(6,0);                         /* codec held quiet before flash and splash */
    settings_init();                           /* a readable default palette before loading flash */
    lcd_init();                                /* maintenance must be visible before any boot writes */
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 70, 240, TY_HR, "MELODEE", T_THEME, 1);   /* (large type) */
    draw_text_box(0, 140, 240, TY_TX, "Starting up", T_MID, 1);
    lcd_sync();
    dsp_tables_init();                        /* the RAM lookup tables (FM6), before anything renders */
    persist_boot();
#if MELODEE_OTA
    if (flash_ok && boot_work.state != 2u)
        ota_boot_cleanup();                             /* staging area left by an update */
#endif
    settings_init();
    clock_boot();                             /* the voice budget at the clock found (never changed) */
    if (boot_work.state != 2u) {
        lcd_fill(0, 0, 240, 240, T_BG);
        draw_text_box(0, 70, 240, TY_HR, "MELODEE", T_THEME, 1);
        draw_text_box(0, 140, 240, TY_TX, "Multi-engine synth", T_MID, 1);
    }
    if (melodee_dbg.magic != DBG_MAGIC) {
        memset(&melodee_dbg, 0, sizeof melodee_dbg);
        melodee_dbg.magic = DBG_MAGIC;
    }
    melodee_dbg.boots++;
    melodee_dbg.max_us = 0;
    melodee_dbg.in_audio = 0;                       /* .noinit: a reset inside the audio ISR left it set, and
                                                       TIMER5 would treat every tick as nested (no USB poll) */
    melodee_dbg.prev_stage = melodee_dbg.stage;     /* a WDT reset leaves the last breadcrumb here */
    melodee_dbg.prev_page = melodee_dbg.page;
    melodee_dbg.prev_home = melodee_dbg.home;
    melodee_dbg.prev_frames = melodee_dbg.ui_frames;
    melodee_dbg.prev_rst = fm1_boot.p3_rst;
    fm1_input_init();
    fm1_adc_init();
#if BOARD_SMK37 && MELODEE_FLASH
    if (flash_ok || st_foreign)                     /* the factory key / pad velocity curves (USRTRIM, read only) */
        smk_trim_load(fm1_xip_ptr(SMK_TRIM_FLASH));
#endif
    panel_init();
    melodee_init();
#if MELODEE_DUAL_CORE
    /* Default recovery chooses one core after failed supply setup, an early
     * crash, or a retained CPU1 bar. No-fallback diagnostics bypass the last
     * two choices and halt on failed supply setup instead. */
    if (fm1_core1_boot_allowed(fm1_power_ready, bootguard.failed))
        fm1_multicore_start();              /* bounded handshake; selected policy handles failure */
    /* CPU1 starts through mask-ROM before it reaches our RAM worker. Restrict
     * instruction fetch only after that bootstrap has returned, or stopped
     * the core on timeout. The captured first-device fault was DBG bit 10
     * (c1_pc_limit_err_r) while CPU0 waited for the ready handshake. */
    fm1_guard_enable(FM1_GUARD_PC);
#endif
    audio_init();
    usb_start();
#if MELODEE_UART
    uart_midi_init();
#endif
#if BOARD_SMK37
    smk_uart1_tx_init();                      /* TRS MIDI OUT (before TIMER5 sends) */
#endif
    timer5_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    if ((fm1_in.buttons & FM1_OCT_BOTH) == FM1_OCT_BOTH) {
        panel_setup();                        /* OCT- + OCT+ held at power-on */
        settings_save();
    }
    glo_restore();                            /* CLK TUNE MIDI ROUT as last used, */
    project_boot();                           /* then the BOOT project or the template (theirs win) */
    fm1_delay_ms(400);
    if (boot_work.state != 2u)
        lcd_fill(0, 0, 240, 240, T_BG);

    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        pattern_poll();
        usb_retry(fm1_ms);
#if MELODEE_USB_AUDIO
        if (ua_off_apply(fm1_ms)) {                     /* USB direction or capture mode changed: the host re-reads */
            settings_save();                            /* while off the bus: no USB deadline missed */
            ui_say("USB ", "RECONNECTING");
        }
#endif
        if (fm1_ms > 30000u && bootguard.pending) {     /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        {
            int32_t b = fm1_adc_read(FM1_ADC_BATT);     /* battery: slow IIR */
            if (b > 0)
                song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
        }
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                uint32_t k10;
                knob += (a * 16 - knob) / 8;
                k10 = (uint32_t)(knob / 16);
                song.master_q12 = (k10 * k10) >> 8;            /* 0 .. ~4096 */
            }
        }
        {   /* OCT- + OCT+ held 5 s: enter UBOOT with RAM intact (debug / update); a countdown
             * shows from 2 s over the whole screen (the menu and the dialogs too: ui_draw), letting
             * go cancels it */
            static uint32_t t0;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both) {
                if (ui.uboot) {
                    ui.uboot = 0;
                    ui.force = 1;                       /* what the countdown covered */
                    ui_message("UPDATE CANCELLED");
                }
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != ui.uboot) {
                    ui.uboot = (uint8_t)left;
                    ui.force = 1;
                }
            } else if (fm1_ms - t0 > 5000u) {
                fm1_audio_stop();
                scr_wake_now();
                lcd_fill(0, 0, 240, 240, T_BG);
                draw_text_box(0, 100, 240, TY_VL, "UBOOT", T_THEME, 1);
                usb_detach();
                fm1_delay_ms(30);
                reset_intended();
                fm1_enter_uboot();
            }
        }
        cz_service();
        fm6_service();                                  /* DX7 SysEx for FM6, a pending FM6 settings save */
#if MELODEE_OTA
        ed_service();                                   /* web editor SysEx */
        ota_service();                                  /* M-UPGRADE handshake */
        if (usb.ota_req) {                              /* M-UPGRADE upgrade command */
            usb.ota_req = 0;
            panic_req = (1u << NTRK) - 1u;               /* every track (bit per track) */
            if (flash_ok)
                ota_session();                          /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, T_BG);
            ui.force = 1;
        }
#endif
        if (usb.uboot_req) {                            /* SysEx F0 22 24 35 7D F7 from the host */
            fm1_audio_stop();
            lcd_fill(0, 0, 240, 240, T_BG);
            draw_text_box(0, 100, 240, TY_VL, "UBOOT (USB)", T_THEME, 1);
            fm1_delay_ms(20);
            usb_detach();
            fm1_delay_ms(30);
            reset_intended();
            fm1_enter_uboot();
        }
#if MELODEE_CDC
        cdc_task();
#endif
        melodee_dbg.ui_frames++;
        melodee_dbg.page = ui.page;
        melodee_dbg.home = ui.home;
        melodee_dbg.stage = 1;
        ui_input();
        glo_poll();                                     /* a kept GLO value changed: the settings, later */
        settings_poll();                              /* queued settings save: only while stopped */
        melodee_dbg.stage = 2;
#if BOARD_SMK37
        smk_grid();
        smo_sync();
#endif
        ui_leds();
#if BOARD_SMK37
        smk_faders();
        smk_wheels();
        padled_flush();                                 /* the pads' colours, between LCD transfers */
#endif
        ui_draw();
        melodee_dbg.stage = 9;
        while (fm1_ms - m < 15u) {                               /* ~60 UI frames/s at most */
            ui_input();
#if BOARD_SMK37
            smk_wheels();                       /* (bends follow the wheel, not the screen) */
            smk_grid();
#endif
#if MELODEE_OTA
            ed_service();                       /* editor replies without waiting for the next frame */
#endif
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    fm1_core1_stop();                      /* hold CPU1 before clearing/copying application RAM */
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                /* flash driver code that must run from RAM */
    for (s = _dsp_load, d = _dsp_start; d < _dsp_end; s++, d++)
        *d = *s;                                /* DSP code: audio is stopped while NOR is busy */
    for (s = _dt_load, d = _dt_start; d < _dt_end; s++, d++)
        *d = *s;                                /* the oscillator correction tables */
    for (s = _audio_load, d = _audio_start; d < _audio_end; s++, d++)
        *d = *s;                                /* resident audio profile: executable ordinary SRAM */
    fm1_mailbox_clear();
    fm1_power_ready = (uint8_t)fm1_power_init(); /* stock V15 supplies before cache, peripherals or CPU1 */
#if !MELODEE_CORE1_FALLBACK
    if (!fm1_power_ready) fm1_core1_failure(); /* failed setup must not become a one-core session */
#endif
#if MELODEE_CACHE_RAM
    {   /* XIP-to-SRAM exceeds the direct-call range on pi32v2. */
        void (*volatile init)(uint32_t) = fm1_cache_init;
        init(bootguard.failed);             /* copied RAM code; IRQs off, CPU1 held */
    }
#endif
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS
#if !MELODEE_DUAL_CORE
                     | FM1_GUARD_PC
#endif
                     );
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
