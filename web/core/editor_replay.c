/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* tests/editor_test.c's own harness (editor.c with MELODEE_FLASH 0, its RAM mirrors, its fm1_wdt_feed that stops the
 * transport by hand) replaying SysEx frames from stdin, in core_native's format: "> frame", then "< reply" per frame
 * sent back. Its tracks power on as main.c's melodee_init has them (editor_test.c's reset() puts SID on all four), so
 * web/core/spike.mjs can set its replies beside the virtual FM-1's (web/core/melodee_core.c) for the same requests.
 *   cc -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/core/editor_replay web/core/editor_replay.c -lm */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"

static void power_on(void)                             /* main.c melodee_init's tracks, after editor_test.c's reset */
{
    uint32_t i;
    midi_insert_init();
    punch_reset();
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
    song.ntrk = NPART;
    song.master_q12 = 2048;
}

int main(void)
{
    static uint8_t in[1 << 20];
    size_t n = fread(in, 1, sizeof in, stdin), at = 0;
    reset();
    power_on();
    while (at < n) {
        size_t e = at, i;
        while (e < n && in[e] != 0xF7u) e++;
        if (e == n) break;
        printf(">");
        for (i = at; i <= e; i++) printf(" %02X", in[i]);
        printf("\n");
        host_wire_n = 0;
        ota_frame_done();
        for (i = at; i <= e; i++) sysex_byte(in[i]);
        ed_service();
        host_drain();
        for (i = 0; i < host_wire_n; i++) {
            if (host_wire[i] == 0xF0u) printf("<");
            printf(" %02X", host_wire[i]);
            if (host_wire[i] == 0xF7u) printf("\n");
        }
        at = e + 1u;
    }
    return 0;
}
