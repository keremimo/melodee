/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* No-fallback policy: healthy dual-core work still completes, while failures
 * stop the session without publishing partial state or switching to CPU0. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MELODEE_CORE1_FALLBACK 0
#define __attribute__(x)
#include "../firmware/hal/fm1_core1_bar.h"
static jmp_buf failure_return;
static uint32_t failures, holds, faults, ticks, callbacks;
static _Noreturn void session_failure(void) { failures++; longjmp(failure_return, 1); }
#define AW_WORD uint32_t
#define AW_LOAD(p) (*(p))
#define AW_STORE(p,v) (*(p) = (v))
#define AW_TICKS() (ticks++)
#define AW_TIMEOUT_TICKS 10u
#define AW_HOLD() (holds++)
#define AW_RETIRED() fm1_core1_bar_set()
#define AW_FAULTS() faults
#define AW_FAILURE() session_failure()
#define AW_RAM_LOOP static
#include "../firmware/src/audio_worker.h"
#define CTL 32u
#define AUDIO_PROFILE_TICKS() (ticks++)
#include "../firmware/src/audio_profile.h"
#include "../firmware/src/audio_batch.h"

static void reset_worker(void)
{
    memset(&audio_worker, 0, sizeof audio_worker);
    audio_worker_online = 1;
    fm1_core1_bar.magic = 0;
    faults = holds = ticks = 0;
}
static void callback(void *context) { assert(context == &callbacks); callbacks++; }
static void join_pending(void) { (void)audio_worker_join(); }
static void expect_failure(void (*operation)(void))
{
    uint32_t before = failures;
    if (!setjmp(failure_return)) {
        operation();
        assert(!"failure must stop the session");
    }
    assert(failures == before + 1 && !holds && audio_worker_online);
    assert(!fm1_core1_barred());
}
int main(void)
{
    /* Saved retirement and an early failed boot cannot silently choose CPU0. */
    fm1_core1_bar_set();
    assert(fm1_core1_boot_allowed(1, 0) && fm1_core1_boot_allowed(1, 1));
    assert(!fm1_core1_boot_allowed(0, 0) && !fm1_core1_boot_allowed(0, 1));

    reset_worker();
    uint32_t done = 0;
    assert(audio_worker_submit(callback, &callbacks));
    audio_worker_poll(&done);
    assert(audio_worker_join() && callbacks == 1);
    audio_worker_check();
    assert(!failures && !holds && audio_worker_online);

    reset_worker();
    assert(audio_worker_submit(callback, &callbacks));
    faults = 1;
    expect_failure(join_pending);
    assert(audio_worker.complete != audio_worker.request && !audio_worker.timeouts);

    reset_worker();
    ticks = UINT32_MAX - 4u;
    assert(audio_worker_submit(callback, &callbacks));
    expect_failure(join_pending);
    assert(audio_worker.timeouts == 1 && audio_worker.complete != audio_worker.request);

    reset_worker();
    audio_worker.rejected = 1;
    expect_failure(audio_worker_check);
    reset_worker();
    faults = 1;
    expect_failure(audio_worker_check);

    /* A completed worker that rejected a batch descriptor must not fall back
     * through the batch-specific error path either. Never merge its PCM. */
    reset_worker();
    audio_batch_count = audio_batch_started = 1;
    audio_batch_worker_mask = audio_batch_published = audio_batch_closed = audio_batch_invalid = 1;
    expect_failure(audio_batch_flush);
    assert(callbacks == 1);
    puts("CPU1 fallback disabled: healthy jobs, retained-bar bypass, supply gate, faults, wrapping timeout, rejected jobs and invalid batch halt without serial recovery: OK");
    return 0;
}
