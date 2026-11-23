/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The converter tests' user kits, made by Melodee 1.x's own kit maker (the frozen tree's web/kit/melodee_kit.c: its
 * Record kit, encoder and checks), as a 1.x device would hold them: OUT/92.bin (KIT 1: kick, snare, low tom at
 * 22.05 kHz, both hats, a crash and a ride, the cymbals looped) and OUT/93.bin (KIT 2: a kick and a ride only), as
 * BACKUP GET 92 and 93 give them. The takes are drawn here (decaying tones and noise), the same on every run.
 *   kit_fixture OUT   (web/convert/build.sh builds it beside legacy_native) */
#include "melodee_kit.c"
#include <stdio.h>

static uint32_t kf_lcg = 7u;
static int32_t kf_noise(void) { kf_lcg = kf_lcg * 1664525u + 1013904223u; return (int32_t)(kf_lcg >> 16) - 32768; }
/* n samples at 44.1 kHz: a tone from f0 falling to f1 over its first sweep samples, noise mixed in, decaying */
static void kf_draw(int16_t *x, uint32_t n, float f0, float f1, uint32_t sweep, float noise, float tau_ms)
{
    float ph = 0.0f, lp = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        float f = i < sweep ? f0 + (f1 - f0) * (float)i / (float)sweep : f1, env = expf(-(float)i / (tau_ms * 44.1f));
        ph += f / 44100.0f;
        ph -= floorf(ph);
        float w = (float)kf_noise() / 32768.0f;
        lp += 0.6f * (w - lp);
        float v = (1.0f - noise) * sinf(6.2831853f * ph) + noise * (w - lp);
        x[i] = (int16_t)lrintf(24000.0f * env * v);
    }
}
static int kf_kit(const char *out, uint32_t id, const char *name, const uint8_t *drums, uint32_t nd)
{
    static int16_t x[44100 * 2];
    static uint8_t rec[DRK_KIT_MAX];
    if (kit_begin(0, 0) || kit_name(name)) return 1;
    for (uint32_t i = 0; i < nd; i++) {
        uint32_t d = drums[i], n = 44100u, rate = 44100u;
        switch (d) {
        case D9_BD: n = 9000; kf_draw(x, n, 160.0f, 50.0f, 1800, 0.05f, 90.0f); break;
        case D9_SD: n = 8000; kf_draw(x, n, 220.0f, 180.0f, 400, 0.6f, 60.0f); break;
        case D9_LT: n = 6000; rate = 22050u; kf_draw(x, n, 140.0f, 90.0f, 900, 0.1f, 70.0f); break;   /* (22.05 kHz) */
        case D9_CH: n = 3000; kf_draw(x, n, 6000.0f, 6000.0f, 1, 0.95f, 12.0f); break;
        case D9_OH: n = 22000; kf_draw(x, n, 5200.0f, 5200.0f, 1, 0.9f, 140.0f); break;
        case D9_CR: n = 40000; kf_draw(x, n, 3100.0f, 2900.0f, 20000, 0.85f, 420.0f); break;
        case D9_RD: n = 36000; kf_draw(x, n, 4300.0f, 4300.0f, 1, 0.45f, 380.0f); break;
        }
        if (kit_pad((int)d, x, (int)n, (int)rate, 0, (int)n, 0, 0, 0, 0)) return 1;
    }
    int len = kit_build(rec, sizeof rec);
    if (len <= 0) { printf("kit_fixture: %s not built (%d)\n", name, len); return 1; }
    char path[600];
    snprintf(path, sizeof path, "%s/%u.bin", out, (unsigned)id);
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    fwrite(rec, 1, (size_t)len, f);
    fclose(f);
    printf("kit_fixture: %s (%d B)", name, len);
    for (uint32_t i = 0; i < nd; i++) printf(" %u:%d", (unsigned)drums[i], (int)kit_report((int)drums[i])[0]);
    printf("\n");
    return 0;
}
int main(int argc, char **argv)
{
    static const uint8_t A[] = {D9_BD, D9_SD, D9_LT, D9_CH, D9_OH, D9_CR, D9_RD}, B[] = {D9_BD, D9_RD};
    if (argc != 2) { printf("usage: kit_fixture OUT\n"); return 2; }
    return kf_kit(argv[1], 92, "DRUMS", A, sizeof A) || kf_kit(argv[1], 93, "RIDE", B, sizeof B);
}
