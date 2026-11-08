/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A built-in engine's factory presets as the host firmware makes them (the engines and their preset makers built in:
 * MELODEE_*_BUILTIN), printed for tools/mod_presets.py, which packs them into the engine's module file (its PRST chunk,
 * firmware/src/module_presets.h). Built and run by tools/mod_build.py with the host's C compiler:
 *     mod_presets ENGINE   ->   N engine init / P k category name common patch (hex; "-": none)
 *     mod_presets S ENGINE ->   S bytes payload: its schema's SCHM chunk (module_schema.h; "-": none) */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"
static void hex(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    if (!n)
        printf(" -");
    else
        printf(" ");
    for (uint32_t i = 0; i < n; i++)
        printf("%02x", b[i]);
}
int main(int argc, char **argv)
{
    uint32_t e = argc == 2 ? (uint32_t)atoi(argv[1]) : argc == 3 ? (uint32_t)atoi(argv[2]) : NENGINES;
    reset();
    if (argc == 3 && !strcmp(argv[1], "S")) {      /* the engine's schema (module_schema.h): its SCHM payload, or none */
        static uint8_t b[16384];
        const mod_sch_src *s = e < NENGINES ? sch_builtin_src(e) : 0;
        uint32_t n = s ? mod_sch_write(s, b, sizeof b) : 0;
        if (s && !n) {
            fprintf(stderr, "mod_presets: engine %u's schema does not write (mod_sch_write)\n", e);
            return 1;
        }
        printf("S %u", n);
        hex(b, n);
        printf("\n");
        return 0;
    }
    if (e == 6u && ENGINES[6] == &ENG_RETIRED)    /* (TRIO and WHEEL, retired in the firmware: their modules' sounds */
        ENGINES[6] = &ENG_TRIO;                   /* are the built-in references' rows, eng_trio.c, eng_wheel.c) */
    if (e == 7u && ENGINES[7] == &ENG_RETIRED)
        ENGINES[7] = &ENG_WHEEL;
    if (e >= NENGINES || ENGINES[e] == &ENG_RETIRED || ENGINES[e]->preset_make == 0 && ENGINES[e]->presets == 0) {
        fprintf(stderr, "mod_presets: engine %u is not built into this host firmware\n", e);
        return 1;
    }
    printf("N %u %u\n", e, eng_npresets(e) ? preset_init(e) : MOD_PST_NONE);
    for (uint32_t k = 0; k < eng_npresets(e); k++) {
        static preset_entry_t x;
        uint8_t c[MOD_PST_COMMON] = {0};
        preset_fetch_into(e, k, &x, 1);
        memcpy(c, x.pr.e, 8);
        memcpy(c + 8, x.pr.env, 4);
        c[12] = (uint8_t)x.pr.fenv;
        c[13] = x.pr.mono;
        printf("P %u %u", k, preset_cat(e, k));
        hex(x.pr.name, (uint32_t)strlen(x.pr.name));
        hex(c, sizeof c);
        hex(x.patch, x.plen);
        printf("\n");
    }
    return 0;
}
