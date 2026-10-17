/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Main-loop laboratory controls. No state, persistence or synthetic audio-tick
 * path: use the same queued transport and rec_set semantics as the panel.
 * Included after seq.c/project.c; callers pause audio interrupts. */
#if MELODEE_AUDIO_PROFILE && MELODEE_AUDIO_BENCH
static uint32_t ed_cpu_control(uint32_t op, uint32_t mask)
{
    if (op > 5u || mask >= (1u << NTRK) || (op <= 2u ? mask != 0u : mask == 0u)) return 1;
    if (!AUDIO_LAB_GUARDED()) return 3;
    if (!op) return 0;
    /* Stop and disarm remain available for cleanup, including a pending Start.
     * Arming/starting a chain or momentary snapshot is never a plain fixture. */
    if (op != 2u && op != 5u && (chain_busy() || momentary.active || template_old())) return 3;
    /* Tracks not in use take no input or CPU; the panel cannot arm them. */
    if ((op == 3u || op == 4u) && (mask >> trk_n())) return 3;
    if (op == 1u) {
        /* transport_busy() without its own cli/sti: interrupts are already
         * off here and a nested sti would reopen them before the snapshot. */
        if (song.g[G_CLOCK] || song.playing || seq_counting() || transport_req) return 3;
        transport_req = 1;
    } else if (op == 2u) transport_req = 2;
    else {
        for (uint32_t k = 0; k < NTRK; k++) if (mask & (1u << k)) {
            uint8_t bit = (uint8_t)(1u << k);
            /* Arming is idempotent: do not turn an already latched take into a
             * one-pass take, or vice versa, after a lost acknowledgement. */
            if (op == 5u) rec_set(&trk[k], 0);
            else if (!(song.rec & bit)) {
                rec_set(&trk[k], 1);
                if (op == 3u) song.rec_latch |= bit;
            }
        }
    }
    ui.force = 1;
    return 0;
}
static void ed_cpu_reply(const uint8_t *a, uint32_t n)
{
    uint32_t values[9];
    fm1_irq_off();
    values[0] = n == 2u ? ed_cpu_control(a[0], a[1]) : 1u;
    values[1] = song.playing; values[2] = seq_counting(); values[3] = song.rec;
    values[4] = song.rec_wait; values[5] = song.rec_pass; values[6] = song.rec_latch;
    values[7] = transport_req; values[8] = (uint32_t)song.g[G_CLOCK];
    fm1_irq_on();
    ed_b(values[0]); ed_b(1); /* schema 1 */
    for (uint32_t k = 1; k < NELEM(values); k++) ed_b(values[k]);
}
#endif
