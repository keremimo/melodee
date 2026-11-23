/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The 16-step phrases the UI tests and renders start from (the firmware's factory patterns until the phrases
 * left it, 2026-10-10). Absolute notes, 0 = rest; flags 1 accent, 2 slide, 4 tie (holds the previous note).
 * demo_pat16: into steps 1..16, the rest empty, LEN 16; on a DRUM track its grid (pads.c step_to_grid) */
#define DT_ 4
static const uint8_t DEMO_ACID[2][16] = {{45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50},
                                         {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1}};
static const uint8_t DEMO_PAD[2][16] = {{60, 0, 0, 0, 0, 0, 0, 0, 57, 0, 0, 0, 55, 0, 0, 0},
                                        {0, DT_, DT_, DT_, DT_, DT_, DT_, 0, 0, DT_, DT_, 0, 0, DT_, DT_, 0}};
static const uint8_t DEMO_BEAT[2][16] = {{36, 42, 42, 42, 38, 42, 36, 42, 36, 42, 42, 36, 38, 42, 46, 42},
                                         {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}};
#undef DT_

static void demo_pat16(track_t *t, const uint8_t (*d)[16])
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        step_t *s = &t->step[i];
        uint8_t n = i < 16u ? d[0][i] : 0, fl = i < 16u ? d[1][i] : 0;
        memset(s, 0, sizeof *s);
        s->note[0] = n;
        s->n = n ? 1 : 0;
        s->time = (fl & 4u) ? ST_TIE : n ? ST_NOTE : ST_REST;
        s->flags = n ? (fl & (SF_ACCENT | SF_SLIDE)) : 0;
        s->vel = n ? 96 : 0;
        if (pads_on(t))
            step_to_grid(t, s);
    }
    t->p[P_SLEN] = 16;
}
