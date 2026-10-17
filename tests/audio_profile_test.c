/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#include <assert.h>
#include <stdio.h>
#define MELODEE_AUDIO_PROFILE 1
#include "../firmware/src/audio_profile.h"
int main(void)
{
    audio_profile_irq(0);audio_profile_irq(AP_HIST_TICKS-1);
    audio_profile_irq(AP_HIST_TICKS);audio_profile_irq(63*AP_HIST_TICKS);
    audio_profile_irq(UINT32_MAX);
    assert(audio_profile_hist[0]==2 && audio_profile_hist[1]==1 && audio_profile_hist[63]==2);
    assert(audio_profile_irq_max==UINT32_MAX);
    /* TIMER4 subtraction must remain correct across its 179-second wrap. */
    uint32_t start=UINT32_MAX-31, end=48;
    audio_profile_add(AP_HALF,end-start);
    assert(audio_profile[AP_HALF].ticks==80 && audio_profile[AP_HALF].max_ticks==80);
    audio_profile_add(AP_HALF,16);
    assert(audio_profile[AP_HALF].calls==2 && audio_profile[AP_HALF].ticks==96);
    puts("audio profile: bounded histogram, overflow, TIMER4 wrap and span maxima passed");
}
