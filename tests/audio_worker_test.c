/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded fault handling and wrap-safe deadlines without waiting in real time,
 * and the worker's defence against CPU1 misreads (keremimo/melodee#17). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t clock_value, callback_calls, holds, faults, forgiven;
#define AW_WORD uint32_t
#define AW_LOAD(p) (*(p))
#define AW_STORE(p,v) (*(p) = (v))
#define AW_TICKS() (clock_value++)
#define AW_TIMEOUT_TICKS 10u
#define AW_HOLD() (holds++)
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
    holds = faults = forgiven = 0;
}
int main(void)
{
    uint32_t done;
    assert(!audio_worker_submit(callback, 0));
    audio_worker_online = 1;
    assert(audio_worker_submit(callback, &callback_calls));
    assert(!audio_worker_submit(callback, 0));
    assert(audio_worker.context == &callback_calls);

    /* a stall: CPU1 is held, the job dropped and never rerun, rendering goes on alone */
    clock_value = UINT32_MAX - 4u;
    assert(!audio_worker_join());
    assert(audio_worker.timeouts == 1 && holds == 1 && !audio_worker_online);
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
    assert(audio_worker_submit(callback, &callback_calls));
    audio_worker_poll(&done);
    assert(callback_calls == 3 && audio_worker_join());
    audio_worker.request = done + 0x00200000u;
    audio_worker_poll(&done);
    audio_worker.request = done;
    audio_worker_check();
    assert(audio_worker.rejected == 3 && !audio_worker_online && holds == 1);

    /* a healthy worker stays online */
    reset();
    audio_worker_check();
    assert(audio_worker_online && !holds);
    puts("audio worker: busy/offline, stall and fault retire, misread requests never run, answered misreads kept, wrapping deadline ok");
}
