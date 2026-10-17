/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Bounded fault handling and wrap-safe deadlines without waiting in real time,
 * and the worker's defence against CPU1 misreads (keremimo/melodee#17). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __attribute__(x)           /* host has no target .noinit section */
#include "../firmware/hal/fm1_core1_bar.h"
#undef __attribute__
static uint32_t clock_value, callback_calls, holds, faults, forgiven;
#define AW_WORD uint32_t
#define AW_LOAD(p) (*(p))
#define AW_STORE(p,v) (*(p) = (v))
#define AW_TICKS() (clock_value++)
#define AW_TIMEOUT_TICKS 10u
#define AW_HOLD() (holds++)
#define AW_RETIRED() fm1_core1_bar_set()
#define AW_FAULTS() faults
#define AW_MISREAD(n) ((n) <= forgiven)      /* a platform that answers the first ones */
#define AW_RAM_LOOP static
#include "../firmware/src/audio_worker.h"
static void callback(void *context) { (void)context; callback_calls++; }
static void reset(void)
{
    uint32_t r = audio_worker.request;
    audio_worker.complete = r;
    audio_worker.rejected = 0;
    audio_worker_online = 1;
    fm1_core1_bar.magic = 0;          /* a fresh powered-on session */
    holds = faults = forgiven = 0;
}
int main(void)
{
    uint32_t done;
    /* No worker launched (barred boot or handshake failure): no retirement. */
    assert(!fm1_core1_barred());
    assert(fm1_core1_boot_allowed(1, 0));
    assert(!fm1_core1_boot_allowed(0, 0) && !fm1_core1_boot_allowed(1, 1));
    audio_worker_check();
    assert(!holds && !fm1_core1_barred());
    assert(!audio_worker_submit(callback, 0));
    audio_worker_online = 1;
    audio_worker_check();
    assert(audio_worker_online && !fm1_core1_barred());
    assert(audio_worker_submit(callback, &callback_calls));
    assert(!audio_worker_submit(callback, 0));
    assert(audio_worker.context == &callback_calls);

    /* a stall: CPU1 is held, the job dropped and never rerun, rendering goes on alone */
    clock_value = UINT32_MAX - 4u;
    assert(!audio_worker_join());
    assert(audio_worker.timeouts == 1 && holds == 1 && !audio_worker_online);
    /* No main-loop poll has run: the first audio job's retirement must bar
     * CPU1 immediately, before a watchdog or intended reset can happen. */
    assert(fm1_core1_barred());
    assert(!fm1_core1_boot_allowed(1, 0));
    assert(callback_calls == 0);
    assert(audio_worker.request == audio_worker.complete);
    assert(!audio_worker_submit(callback, 0));

    /* a CPU1 fault caught by the exception handler: no waiting out the timeout */
    reset();
    assert(audio_worker_submit(callback, 0));
    faults = 1;
    clock_value = 0;
    assert(!audio_worker_join());
    assert(clock_value < 5u && audio_worker.timeouts == 1 && holds == 1 && !audio_worker_online);
    assert(fm1_core1_barred());

    /* the worker runs the next request when its check matches, once */
    reset();
    done = audio_worker.complete;
    assert(audio_worker_submit(callback, &callback_calls));
    audio_worker_poll(&done);
    assert(callback_calls == 1 && audio_worker.complete == audio_worker.request && done == audio_worker.request);
    audio_worker_poll(&done);
    assert(callback_calls == 1 && !audio_worker.rejected);
    assert(audio_worker_join());

    /* misreads at rest: never run, counted (fn is stale or null; before, it was called) */
    audio_worker.fn = 0;
    audio_worker.request = done + 0x00200000u;              /* as read on an affected unit */
    audio_worker_poll(&done);
    assert(audio_worker.rejected == 1 && callback_calls == 1);
    audio_worker.request = done + 1u;                       /* a plausible count, the old check word */
    audio_worker_poll(&done);
    assert(audio_worker.rejected == 2 && callback_calls == 1);
    audio_worker.request = done;
    audio_worker_check();
    assert(!audio_worker_online && holds == 1 && !audio_worker_submit(callback, 0));
    assert(fm1_core1_barred());

    /* a real request whose fn, context or check is misread waits for a clean read */
    reset();
    done = audio_worker.complete;
    assert(audio_worker_submit(callback, &callback_calls));
    audio_worker.context = &done;
    audio_worker_poll(&done);
    assert(audio_worker.rejected == 1 && callback_calls == 1 && audio_worker.complete != audio_worker.request);
    audio_worker.context = &callback_calls;
    audio_worker_poll(&done);
    assert(callback_calls == 2 && audio_worker_join());
    audio_worker_check();
    assert(!audio_worker_online && holds == 1);
    assert(fm1_core1_barred());

    /* misreads the platform answered keep the worker; the next one retires it */
    reset();
    done = audio_worker.complete;
    forgiven = 2;
    audio_worker.request = done + 0x00200000u;
    audio_worker_poll(&done);
    audio_worker_poll(&done);
    audio_worker.request = done;
    audio_worker_check();
    assert(audio_worker.rejected == 2 && audio_worker_online && !holds);
    assert(!fm1_core1_barred());
    assert(audio_worker_submit(callback, &callback_calls));
    audio_worker_poll(&done);
    assert(callback_calls == 3 && audio_worker_join());
    audio_worker.request = done + 0x00200000u;
    audio_worker_poll(&done);
    audio_worker.request = done;
    audio_worker_check();
    assert(audio_worker.rejected == 3 && !audio_worker_online && holds == 1);
    assert(fm1_core1_barred());

    /* Soft reset clears application state but leaves .noinit intact. */
    memset(&audio_worker, 0, sizeof audio_worker);
    holds = faults = 0;
    audio_worker_check();
    assert(fm1_core1_barred() && !holds);
    fm1_core1_bar.magic = 0;         /* power-off clears the bar */
    assert(!fm1_core1_barred());

    /* a healthy worker stays online */
    reset();
    audio_worker_check();
    assert(audio_worker_online && !holds);
    assert(!fm1_core1_barred());
    puts("audio worker: busy/offline, stall and fault retire, immediate soft-reset bar, misread requests never run, answered misreads kept, wrapping deadline ok");
}
