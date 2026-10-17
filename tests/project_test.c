/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project record (firmware/src/project.c, -DPROJ_HOST part): today's FUN16 round trip (the
 * parameters, the engines, the steps, the song chain, the name, the FM6 patches), damaged and out-of-range ones
 * refused or bounded, and records of the formats before FUN16 refused (1.2 rewrote them; archives restore there).
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1                             /* (trk_def_engine: engines.c, through hostsim.c) */
#include "../firmware/src/project.c"

static int check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* an empty project as the stores hold it */
static void blank(project_t *a)
{
    memset(a, 0, sizeof *a);
    for (uint32_t k = 0; k < NTRK; k++) {
        p5_patch_init(&a->p5[k]);
        cz_patch_init(a->cz[k].raw);
        memcpy(a->fm6[k], FM6_INIT, FM6_PACKED);
    }
    a->magic = PROJ_MAGIC; a->size = sizeof *a; a->parts = NPART; a->phys = PROJ_PHYS;
    chain_defaults(&a->chain);
    a->sum = proj_sum(a);
}

int main(void)
{
    static project_t q, q2;
    int bad = 0, ok;

    bad += check("layout: SLICER after DETUNE, then the matrix just before P_E0",
                 P_RESERVED45 == P_DETUNE + 1 && P_RESERVED48 + 1 == P_M1SRC && P_M4AMT + 1 == P_FM1_ATK && P_FM4_LEVEL + 1 == P_CHRD && P_VOIC + 1 == P_MPCDEG && P_MPCDEG + 1 == P_LN0 && P_LN7 + 1 == P_LSYNC && P_SPRD + 1 == P_FXTYPE && P_FX3CHAR + 1 == P_FXSEND && P_FXSEND + 1 == P_FXSEND2 && P_FXSEND2 + 1 == P_FXSEND3 && P_FXSEND3 + 1 == P_FXTONE && P_FX3TONE + 1 == P_STRUM && P_STRUM + 1 == P_STROKE && P_STROKE + 1 == P_E0 && P_E0 == 116);
    bad += check("FUN16 fits its record", sizeof(project_store_t) == PROJ_STORE_SIZE);

    /* a project_t round trip (the host's RAM slots): as stored (a matrix slot, a retired engine's byte, a DRUM step
     * with notes and hits) */
    blank(&q);
    q.t[1].engine = 8;
    q.t[2].p[P_M2SRC] = 6;
    q.t[2].p[P_M2DST] = 11;
    q.t[2].p[P_M2AMT] = -17;
    q.t[0].engine = ENGI_DRUM;
    q.t[0].step[0] = (step_t){{36, 42, 0, 0}, 2, ST_NOTE, 0, 96, 1u << DV_SNARE, 1u << DV_SNARE};
    q.sum = proj_sum(&q);
    bad += check("project_t: as stored (a matrix slot, engine 8, a DRUM step with notes and hits)",
                 proj_import(&q2, &q, (int)sizeof q) && !memcmp(&q, &q2, sizeof q) && q2.t[1].engine == 8 &&
                 q2.t[2].p[P_M2AMT] == -17 && q2.t[0].step[0].n == 2u && q2.t[0].step[0].hit == 1u << DV_SNARE);
    {   /* New ranges use the existing global slot in the unchanged project record. */
        static project_store_t stored;
        uint32_t ranges_ok = 1;
        for (uint32_t route = 0; route <= 4; route++) {
            q.g[G_ROUTE] = (int16_t)route; q.sum = proj_sum(&q);
            ranges_ok &= proj_pack(&stored, &q) && proj_import(&q2, &stored, sizeof stored) && q2.g[G_ROUTE] == route;
        }
        bad += check("FUN16: every ROUT value survives pack/import", ranges_ok);
    }
    q.chain.count = 2;
    q.chain.row[0] = (chain_row_t){0, 4};
    q.chain.row[1] = (chain_row_t){3, 16};
    q.sum = proj_sum(&q);
    bad += check("project_t: chain round trip", proj_import(&q2, &q, sizeof q) && !memcmp(&q.chain, &q2.chain, sizeof q.chain));
    q.chain.row[0].repeat = 0;
    q.sum = proj_sum(&q);
    bad += check("project_t: invalid row refused even with a correct checksum", !proj_import(&q2, &q, sizeof q));

    {   /* the name: the record's last 12 bytes before the hash, covered by it; zero = no name */
        project_t a, c;
        static project_store_t st, st2;
        uint32_t i, zero = 1;
        bad += check("FUN16: the name at the end of the record, the FM6 patches before the tones, after the data",
                     PROJ_NAME_OFF == PROJ_STORE_SIZE - 16u && PROJ_FM6_OFF == 3104u && 68u + NTRK * (P_COUNT + 2u + NSTEP * 8u) +
                     sizeof(chain_config_t) + sizeof(motion_store_t) + 188u == PROJ_FM6_OFF);
        blank(&a);
        a.t[1].p[P_LEVEL] = 99;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a);
        for (i = 0; i < PROJ_NAME_LEN; i++) zero &= st.raw[PROJ_NAME_OFF + i] == 0u;
        bad += check("FUN16 no name: the tail all zero", ok && zero);
        bad += check("FUN16 without a name: loads, no name", proj_import(&c, &st, sizeof st) && !c.name[0] &&
                     c.t[1].p[P_LEVEL] == 99);
        memcpy(a.name, "lofi jam", 8);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN16 name round trip (upper case, 0-padded)", ok && !memcmp(c.name, "LOFI JAM\0\0\0\0", 12) &&
                     !memcmp(st.raw + PROJ_NAME_OFF, "LOFI JAM\0\0\0\0", 12));
        memcpy(a.name, "ABCDEFGHIJKL", 12);         /* 12: no 0 */
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN16 name of 12 characters (no terminator)", ok && !memcmp(c.name, "ABCDEFGHIJKL", 12));
        st2 = st;
        st2.raw[PROJ_NAME_OFF + 3] ^= 1u;
        bad += check("FUN16 name byte changed: the hash refuses the project", !proj_import(&c, &st2, sizeof st2));
        st2 = st;
        st2.raw[PROJ_NAME_OFF + 3] = 7u;            /* not printable, the hash right */
        {
            uint32_t sum = proj_hash(st2.raw, PROJ_STORE_SIZE - 4u);
            memcpy(st2.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        }
        bad += check("FUN16 name with a byte outside 32..126: the project loads, no name",
                     proj_import(&c, &st2, sizeof st2) && !c.name[0] && c.t[1].p[P_LEVEL] == 99);
    }

    {   /* each track's FM6 patch travels with the project */
        project_t a, c;
        static project_store_t st, slot;
        uint32_t i, k, sum;
        blank(&a);
        for (k = 0; k < NTRK; k++) {
            memcpy(a.fm6[k], FM6_FACTORY[k * 2u], FM6_PACKED);
            a.t[k].engine = ENGI_FM6;
            a.t[k].p[P_E7] = (int16_t)(k * 2u);
        }
        memcpy(a.name, "FM SONG", 7);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && ((uint32_t *)st.raw)[0] == PROJ_MAGIC && proj_import(&c, &st, sizeof st);
        for (k = 0; k < NTRK; k++)
            ok &= !memcmp(c.fm6[k], FM6_FACTORY[k * 2u], FM6_PACKED) && c.t[k].engine == ENGI_FM6 &&
                  !memcmp(st.raw + PROJ_FM6_OFF + k * FM6_PACKED, FM6_FACTORY[k * 2u], FM6_PACKED);
        bad += check("FUN16: the four FM6 patches round trip (bytes at PROJ_FM6_OFF)", ok && !memcmp(c.name, "FM SONG", 7));
        st.raw[PROJ_FM6_OFF + 5] ^= 1u;
        bad += check("FUN16: a patch byte changed: the hash refuses it", !proj_import(&c, &st, sizeof st));
        st.raw[PROJ_FM6_OFF + 5] ^= 1u;
        bad += check("FUN16 one byte short: refused", !proj_import(&c, &st, sizeof st - 1u));
        slot = st;                                  /* an older record's magic, its hash right: not read any more */
        ((uint32_t *)slot.raw)[0] = 0x46554E40u;    /* (the FUN16 of the 0.x betas) */
        sum = proj_hash(slot.raw, PROJ_STORE_SIZE - 4u);
        memcpy(slot.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        bad += check("a record of an older format (another magic): refused", !proj_import(&c, &slot, sizeof slot));
        memset(&slot, 0xA5, sizeof slot);
        bad += check("a retained slot of garbage is empty", !proj_import(&c, &slot, sizeof slot));
        for (i = 0; i < FM6_PACKED; i++) a.fm6[1][i] = (uint8_t)(i | 0x80u);
        a.sum = proj_sum(&a);
        bad += check("FUN16: a patch byte above 127 cannot be packed", !proj_pack(&st, &a));
        memcpy(a.fm6[1], FM6_INIT, FM6_PACKED);
        a.t[0].step[3].n = 1; a.t[0].step[3].note[0] = 200; a.t[0].step[3].note[2] = 128;
        a.t[0].step[3].vel = 255; a.t[0].step[3].hit = 0x05; a.t[0].step[3].acc = 0xF7;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN16: a step out of range packs inside it (note, velocity 127; accents on hits): it loads",
                     ok && c.t[0].step[3].note[0] == 127u && c.t[0].step[3].note[2] == 127u && c.t[0].step[3].vel == 127u &&
                     c.t[0].step[3].hit == 0x05u && c.t[0].step[3].acc == 0x05u);
    }

    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
