/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The tests' presets, the same in both cores (legacy.c, check.c): user preset k loaded on track 1, the sound it gives
 * (its synth's ID, every parameter, the patch's bytes) as one line; PP_ID(t) and PP_E0(t, v) say it as 2.0 would. Also
 * a chord held 1 s then let go, 1 s more (interleaved float) to OUT-k.f32 (free-running phases differ between cores) */
static int pp_state(uint32_t k, FILE *o)
{
    track_t *t = &trk[0];
    if (up_load_to(k, 0)) return 1;
    core_poll(60);
    uint32_t e = t->eng_req % NENGINES, n = patch_len(e);
    fprintf(o, "preset %u id %016llx p", (unsigned)k, (unsigned long long)PP_ID(t));
    for (uint32_t i = 0; i < P_COUNT; i++) fprintf(o, " %d", i == P_E0 ? PP_E0(t, t->p[i]) : t->p[i]);
    fprintf(o, " patch");
    for (uint32_t i = 0; i < n; i++) fprintf(o, "%02x", patch_of(0)[i]);
    fprintf(o, "\n");
    return 0;
}
static int pp_render(uint32_t k, const char *prefix)
{
    static const uint8_t on[] = {0x90u, 36u, 110u, 0x90u, 48u, 100u, 0x90u, 55u, 90u, 0x90u, 64u, 80u};
    static const uint8_t off[] = {0x80u, 36u, 0u, 0x80u, 48u, 0u, 0x80u, 55u, 0u, 0x80u, 64u, 0u};
    char path[600];
    if (up_load_to(k, 0)) return 1;
    core_poll(60);
    snprintf(path, sizeof path, "%s-%u.f32", prefix, (unsigned)k);
    FILE *f = fopen(path, "wb");
    if (!f) return 2;
    core_midi_in(on, sizeof on);
    for (uint32_t n = 0; n < 2u * FS; n += 1024u) {
        if (n == FS - FS % 1024u) core_midi_in(off, sizeof off);
        fwrite(core_render(1024u), sizeof(float), 2u * 1024u, f);
    }
    fclose(f);
    core_midi_in(off, sizeof off);
    core_poll(300);
    return 0;
}
