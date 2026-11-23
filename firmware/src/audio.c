/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* I2S output (ALNK0 -> external codec) and the
 * audio ISR: per half buffer, blocks of CTL samples: mix_block (mix.c: events ->
 * each synth part -> dist -> level / pan -> sends -> buses -> master, Q15 << MIX_FRAC from the level on) -> 24-bit
 * stereo with the bus's low bits. */
/* registers: hal/fm1_audio.h */
#include "performance_options.h"
#define HALF_WORDS (HALF_FRAMES * 2u)
#define DAC_TICKS 544u            /* TIMER4 ticks per I2S frame (24 MHz / 44,117.6 Hz) */
#define CPU_AVG (4096u / HALF_FRAMES)   /* halves the load meter averages: ~93 ms whatever the half */
#define OUT_SHIFT (MIX_FRAC - 7)  /* the bus (Q15 << MIX_FRAC) -> 24-bit as Q15 << 7: -6 dBFS ceiling (M0f ran clean at -18 dBFS) */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(32)));
#if MELODEE_RENDER_AHEAD
/* Render ahead: each half IRQ hands the free DMA half the block rendered at the IRQ before (a 1 KiB copy) and
 * renders the next one here. A render then has two halves to finish in, not one: a slow half rides on a quick
 * one instead of reaching the DAC late (the CPU each half needs on average is unchanged). One half more latency:
 * a block plays two halves after its IRQ (MIDI to output three halves, ~8.7 ms) */
#define AHEAD_HALVES 1u
static int32_t ahead[HALF_WORDS] __attribute__((aligned(32)));
#else
#define AHEAD_HALVES 0u
#endif

/* diagnostics, kept across resets and UBOOT entry: read with `fm1t memr` */
#define DBG_MAGIC 0x44424731u                       /* "DBG1" */
struct melodee_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, page, home;           /* where the main loop is (breadcrumbs) */
    uint32_t prev_stage, prev_page, prev_home, prev_rst, prev_frames;   /* as found at boot */
} melodee_dbg __attribute__((section(".noinit")));
static volatile uint32_t audio_halves,audio_max_us;
static uint32_t audio_full_last_us,audio_full_max_us,audio_deadline_misses,audio_target_exceeded;
/* Written by the assembly wrapper, observed at the following interrupt. */
volatile uint32_t fm1_audio_irq_start_ticks,fm1_audio_irq_elapsed_ticks;
static uint32_t audio_irq_max_ticks,audio_timer5_max_ticks;
#define AUDIO_DEADLINE_TICKS (HALF_FRAMES*544u)
#define AUDIO_TARGET_TICKS (AUDIO_DEADLINE_TICKS*80u/100u)
#if MELODEE_RENDER_AHEAD
/* The TIMER4 tick at which the DMA took up the half this IRQ serves. The IRQ's entry, unless the render before
 * ran into this half (it may now): then one half after the last boundary, on the DMA's own clock (544 ticks a
 * frame), so MIDI input and the clock follow stay on the DAC's timeline whatever a render's lateness. An entry a
 * whole half late (a half went by unserved and played stale) restarts the timeline there and counts as late */
static uint32_t audio_due;
static uint8_t audio_due_ok;
#define AUDIO_DUE_SLACK (AUDIO_DEADLINE_TICKS / 4u)   /* later than this: the render before ran over */
static uint32_t audio_boundary(uint32_t t0)
{
    uint32_t d = t0 - (audio_due + AUDIO_DEADLINE_TICKS);
    if (audio_due_ok && d >= AUDIO_DUE_SLACK && d < AUDIO_DEADLINE_TICKS)
        return audio_due += AUDIO_DEADLINE_TICKS;
    if (audio_due_ok && d >= AUDIO_DEADLINE_TICKS && (int32_t)d >= 0)
        melodee_dbg.late++;
    audio_due_ok = 1;
    return audio_due = t0;                      /* on time (or early: the last guess ran late) */
}
#endif
static volatile uint32_t t5_nested_ticks;              /* TIMER4 ticks TIMER5 spent nested in this ISR (main.c) */
static uint32_t audio_cpu_rem;                         /* keep the fractional IIR step: no low-load bias */
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;

static void audio_block(int32_t *out, uint32_t n)       /* mix (mix.c, the bus), then 24 bit */
{
    uint32_t i;
    mix_block(out, n);
    AP_START(output_start);
#if MELODEE_USB_AUDIO
    /* USB capture copies the finished master or optional stems; computer playback follows capture.
     * TIMER5 serves the endpoints nested in this render (main.c): only this short copy goes without IRQs,
     * stream resets and alternate changes included, not the synth and mixer work */
    fm1_irq_off();
    if (usb.up && usb.config && !usb.suspended)
        ua_audio(out, track_capture_on || master_capture_on ? track_capture : 0, n,
                 (settings_usb_volume > 100u ? 100u : settings_usb_volume) * 4096u / 100u);   /* (USB VOLUME) */
    fm1_irq_on();
#endif
    click_render(out, n);
    for (i = 0; i < n; i++) {
        if (i & 1u)
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)(out[2u * i] >> MIX_FRAC);
        out[2u * i] >>= OUT_SHIFT;
        out[2u * i + 1u] >>= OUT_SHIFT;
#if defined(MELODEE_BENCH_SILENT) && MELODEE_BENCH_SILENT
        /* Bench builds silence only the physical output. Full engine/mixer work,
         * scope and USB capture still run; never use track mutes for timing. */
        out[2u * i] = out[2u * i + 1u] = 0;
#endif
    }
    AP_END(AP_OUTPUT,output_start);
}

/* Overload: two halves in a row above 85 % (the render plus TIMER5 nested in it), fade one voice over the
 * next block, and one more each half while it lasts. Keep each part's
 * lowest POLY note and MONO / LEGATO / UNISON lead, as the shared budget does. */
static volatile uint8_t shed_req;
static uint8_t shed_over;                              /* bit k: the half k halves ago was over 85 % */
#define SHED_TICKS ((HALF_FRAMES * 1000000u / FS) * 85u / 100u * FM1_TICKS_PER_US)   /* 85 % of a half */
static uint32_t shed_count;
#if MELODEE_AUDIO_BENCH
static uint32_t audio_bench_shed_requests;
#endif
static uint32_t shed_part[NPART];

/* end of a half: all = its ticks, TIMER5 nested in it included -> the render alone in us. The deadline sees
 * both; shed after 2 overloaded halves in a row (out of line: keeps the ISR's render loops as they were) */
static __attribute__((noinline)) uint32_t shed_check(uint32_t all)
{
    shed_over = (uint8_t)(shed_over << 1 | (all > SHED_TICKS));
    if ((shed_over & 3u) == 3u)
        shed_req = 1;
    return (all - t5_nested_ticks) / FM1_TICKS_PER_US;
}

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

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0, at;
    AP_START(half_start);
    if(fm1_audio_irq_elapsed_ticks>audio_irq_max_ticks)audio_irq_max_ticks=fm1_audio_irq_elapsed_ticks;
    melodee_dbg.in_audio = 1;                   /* first: TIMER5 nests from here on (main.c) */
    p = fm1_audio_pending();
#if MELODEE_AUDIO_PROFILE
    /* The wrapper publishes its duration after this C body returns. Consume
     * the preceding half once; resets discard the pending sample. */
    if(audio_profile_irq_pending)audio_profile_irq(fm1_audio_irq_elapsed_ticks);
    audio_profile_irq_pending=(p&FM1_AUDIO_HALF)!=0;
#endif
    at = t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us;
#if MELODEE_RENDER_AHEAD
        int32_t *o = ahead;
        at = audio_boundary(t0);
        {
            int32_t *d = &abuf[half * HALF_WORDS];
            for (b = 0; b < HALF_WORDS; b++)
                d[b] = ahead[b];
        }
        fm1_audio_ack_half();                   /* before the render: a boundary it runs into raises the next IRQ */
        if (fm1_audio_free_half() != half)
            melodee_dbg.late++;                 /* the copy landed after its half began to play */
#else
        int32_t *o = &abuf[half * HALF_WORDS];
#endif
        if (shed_req) {
            shed_req = 0;
#if MELODEE_AUDIO_BENCH
            if(audio_bench_active())audio_bench_shed_requests++;
            else
#endif
            if (settings_safety)                        /* GLO > WEIGHTS 2 SAFETY OFF: overload cuts nothing */
                shed_voice();
        }
#if MELODEE_DUAL_CORE
        audio_worker_check();                   /* a CPU1 that misread or faulted: render alone from here */
#endif
        for (b = 0; b < HALF_FRAMES; b += CTL) {
            /* A fixed one-half lookback makes input available before its
             * block renders; four CTL blocks follow audio time, not CPU time.
             * MIDI-to-output latency is 2 + AHEAD_HALVES halves (~8.7 ms rendering
             * ahead), independent of synth load. Queued later events wait for their own block. */
            midi_render_time = at - (HALF_FRAMES - b) * DAC_TICKS;
            midi_render_timed = 1;
#ifdef FM1_INPUT_LAT
            kb_out_tick = at + ((1u + AHEAD_HALVES) * HALF_FRAMES + b) * DAC_TICKS;   /* when this block plays (seq.c kb_lat) */
#endif
            audio_block(o + 2u * b, CTL);
        }
        midi_render_timed = 0;
#if !MELODEE_RENDER_AHEAD
        fm1_audio_ack_half();
#endif
        audio_halves++;
        us = shed_check(fm1_ticks() - t0);
        if (us > audio_max_us)
            audio_max_us = us;
        {
            uint32_t load = song.cpu_q8 * (CPU_AVG - 1u) + (us * 256u) / (HALF_FRAMES * 1000000u / FS) + audio_cpu_rem;
            song.cpu_q8 = load / CPU_AVG;
            audio_cpu_rem = load % CPU_AVG;
        }
#if !MELODEE_RENDER_AHEAD
        if (fm1_audio_free_half() != half)
            melodee_dbg.late++;                         /* the DMA moved on while we rendered */
#endif
        melodee_dbg.halves++;
        melodee_dbg.last_us = us;
        if (us > melodee_dbg.max_us)
            melodee_dbg.max_us = us;
        melodee_dbg.cpu_q8 = song.cpu_q8;
    }
    uint32_t complete=fm1_ticks()-t0;
    if(p&FM1_AUDIO_HALF){
        audio_full_last_us=(complete+FM1_TICKS_PER_US-1u)/FM1_TICKS_PER_US;
        if(audio_full_last_us>audio_full_max_us)audio_full_max_us=audio_full_last_us;
        audio_deadline_misses+=t0+complete-at>(1u+AHEAD_HALVES)*AUDIO_DEADLINE_TICKS;   /* the block not ready when its copy is due */
        audio_target_exceeded+=complete>AUDIO_TARGET_TICKS;
    }
    AP_END(AP_HALF,half_start);
    melodee_dbg.in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* codec and ALNK0 bring-up (fm1_audio.h) */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
#if MELODEE_RENDER_AHEAD
    for (i = 0; i < HALF_WORDS; i++)
        ahead[i] = 0;
#endif
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
