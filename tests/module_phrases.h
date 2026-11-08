/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The phrases the native-module tests render (tests/module_test.c): SID, DRUM and CZ-1 on part 1, every preset, POLY
 * and LEGATO, the C64 models, ENV3 as ENV DEST, every kit with the drum patch, lane levels, DRV, recorded hit lengths
 * and pitches, every CZ-1 tone edited as it plays, the matrix's ENV (CZ-1's DCA), LFO, a MIDI insert release, concert
 * pitch, each also on the dual-core path (engine_batch_kernel). Each phrase starts from the same state, so its
 * render is a function of the phrase alone: module_test.c holds the modules to the built-in engines sample for sample
 * and to MP_GOLDEN, the hashes the built-in engines rendered before they left the firmware (203a5c04: this file
 * compiled there with tests/persistence_test.c, mp_golden_print()). Needs persistence_test.c (reset, host_preset). */
#define MP_NB 200u
static int32_t mp_out[MP_NB * CTL];

/* FNV-1a over the samples */
static uint32_t mp_hash(const int32_t *x, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t b = 0; b < 4u; b++)
            h = (h ^ (((uint32_t)x[i] >> (8u * b)) & 0xFFu)) * 16777619u;
    return h;
}
static uint8_t mp_nor_kept[sizeof nor];
static void mp_start(void)
{
    memcpy(mp_nor_kept, nor, sizeof nor);           /* (reset() erases the flash: the stored modules stay) */
    reset();
    memcpy(nor, mp_nor_kept, sizeof nor);
    eng_state_reset();
    vage = 0;
    dith_r = DITH_SEED;
    memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target);
    memset(drum_patch, 0, sizeof drum_patch);
    memset((void *)drum_flash, 0, sizeof drum_flash);
    drum_event_pitch = 0;
    drum_event_length = 0;
    tune_cache.valid = 0;
}
static void mp_block(uint32_t b, uint32_t batch)
{
    dither_block();
    audio_batch_active = (uint8_t)batch;
    track_render(&inst, mp_out + b * CTL, CTL);
    if (batch)
        audio_batch_flush();
    audio_batch_active = 0;
}

/* SID preset pi; model: -1 the preset's, else MODEL 0..3 (2, 3: the C64's); env3: ENV DEST on pitch and filter */
static uint32_t mp_sid(uint32_t pi, uint32_t mono, int32_t model, uint32_t env3, uint32_t batch)
{
    mp_start();
    host_preset(&inst, 3, pi);
    inst.p[P_VOICE] = (int16_t)(mono ? V_LEGATO : V_POLY);
    if (model >= 0)
        inst.p[P_E0] = (int16_t)model;
    if (env3) {
        inst.p[P_ED_PIT] = 24;
        inst.p[P_ED_FLT] = -40;
    }
    for (uint32_t b = 0; b < MP_NB; b++) {
        if (b == 4) trk_note_on(&inst, 48, 100);
        if (b == 40) trk_note_on(&inst, 55, 80);
        if (b == 60) trk_note_off(&inst, 48);
        if (b == 70) trk_note_on(&inst, 64, 127);
        if (b == 71) trk_note_on(&inst, 67, 60);
        if (b == 100) inst.p[P_E0 + 3] = 20;        /* PW */
        if (b == 120) { trk_note_off(&inst, 55); trk_note_off(&inst, 64); trk_note_off(&inst, 67); }
        if (b == 130) inst.p[P_E0 + 4] = 30;        /* CUT */
        if (b == 140) trk_note_on(&inst, 36, 90);
        if (b == 150) inst.p[P_E0 + 2] = (int16_t)((inst.p[P_E0 + 2] + 5) & 7);   /* STACK */
        if (b == 180) trk_note_off(&inst, 36);
        mp_block(b, batch);
    }
    return mp_hash(mp_out, MP_NB * CTL);
}

/* DRUM: KIT kit (its preset pi first), the GM notes from 35 up, every lane, chokes, retriggers; the part's drum
 * patch offsets, lane levels, DRV and ACC moved; recorded lengths and pitches on some hits */
static uint32_t mp_drum(uint32_t pi, int32_t kit, uint32_t batch)
{
    mp_start();
    host_preset(&inst, ENGI_DRUM, pi);
    if (kit >= 0)
        inst.p[P_E0] = (int16_t)kit;
    for (uint32_t s = 0; s < 16u; s++) {             /* every sound a little off its factory setting */
        drum_patch[0].c[s][0] = (int8_t)((int32_t)(s % 5u) - 2);
        drum_patch[0].c[s][1] = (int8_t)((int32_t)(s * 7u % 21u) - 10);
        drum_patch[0].c[s][2] = (int8_t)((int32_t)(s * 5u % 17u) - 8);
        drum_patch[0].c[s][3] = (int8_t)(s % 3u * 10u);
    }
    for (uint32_t b = 0; b < MP_NB; b++) {
        if (b % 3u == 0 && b < 150u) {
            uint32_t note = 35u + (b / 3u) % 47u;
            if (b % 15u == 0) { drum_event_pitch = (int32_t)(b % 7u) - 3; drum_event_length = 900u + b * 3u; }
            trk_note_on(&inst, note, 40u + (b * 13u) % 88u);
            drum_event_pitch = 0;
            drum_event_length = 0;
        }
        if (b == 61 || b == 64) trk_note_on(&inst, 46, 120);   /* an open hat, then closed ones choke it */
        if (b == 62 || b == 66) trk_note_on(&inst, 42, 90);
        if (b == 80) trk_note_on(&inst, 36, 127);                /* a kick struck again on its lane */
        if (b == 82) trk_note_on(&inst, 36, 64);
        if (b == 90) { inst.p[P_E0 + 1] = 90; inst.p[P_E0 + 3] = 30; inst.p[P_E0 + 7] = 70; }   /* TUNE DECY DRV */
        if (b == 110) { inst.p[P_LN0 + 0] = 60; inst.p[P_LN0 + 3] = 100; inst.p[P_E0 + 5] = 30; }   /* lanes, ACC */
        if (b == 120) inst.p[P_E0 + 6] = (int16_t)!inst.p[P_E0 + 6];   /* KICK (DIGI on KIT SID) */
        if (b % 5u == 4u) trk_note_off(&inst, 35u + (b / 3u) % 47u);
        mp_block(b, batch);
    }
    return mp_hash(mp_out, MP_NB * CTL);
}

/* CZ-1: a block with the matrix as mix.c mix_part runs it (its ENV: engine_t.menv, the native DCA) */
static void mp_cz_block(uint32_t b, uint32_t batch)
{
    dither_block();
    audio_batch_active = (uint8_t)batch;
    mod_begin(&inst);
    track_render(&inst, mp_out + b * CTL, CTL);
    if (batch)
        audio_batch_flush();
    if (mod.on)
        mod_end(&inst);
    audio_batch_active = 0;
}
/* CZ-1 preset pi (0 INIT TONE, then Casio's 64 tones); mono: LEGATO. By pi: the matrix's ENV on cutoff and pitch,
 * LFO on pitch and cutoff, a MIDI insert release, concert pitch moved; every tone edited as it plays (line select,
 * modulation, window, a DCW rate, vibrato: cz_native_block makes its definitions again) */
static uint32_t mp_cz(uint32_t pi, uint32_t mono, uint32_t batch)
{
    int16_t a4 = tuning_a4;
    mp_start();
    memset(midi_verb, 0, sizeof midi_verb);
    host_preset(&inst, ENGI_CZ, pi);
    inst.p[P_VOICE] = (int16_t)(mono ? V_LEGATO : V_POLY);
    if (pi % 4u == 1u) {
        inst.p[P_M1SRC] = MS_ENV; inst.p[P_M1DST] = MD_CUT; inst.p[P_M1AMT] = 50;
        inst.p[P_M2SRC] = MS_ENV; inst.p[P_M2DST] = MD_PITCH; inst.p[P_M2AMT] = -20;
    }
    if (pi % 4u == 2u) { inst.p[P_LD_PIT] = 12; inst.p[P_LD_FLT] = 40; }
    if (pi % 4u == 3u) midi_verb[0] = 30;
    for (uint32_t b = 0; b < MP_NB; b++) {
        if (b == 4) trk_note_on(&inst, 48, 100);
        if (b == 40) trk_note_on(&inst, 55, 80);
        if (b == 60) trk_note_off(&inst, 48);
        if (b == 70) trk_note_on(&inst, 64, 127);
        if (b == 71) trk_note_on(&inst, 67, 30);
        if (b == 100) {
            uint8_t *r = cz_patch_of(0)->raw;
            r[0] = (uint8_t)((r[0] & ~3u) | (pi + 1u) % 4u);                     /* LINE SELECT */
            r[15] = (uint8_t)((r[15] & ~0x38u) | (pi & 1u ? 3u : 4u) << 3);      /* MODULATION: noise, ring */
            r[15] ^= 0x40u;                                                     /* the window */
            r[CZ_ENV_BASE[0][1]] ^= 0x10u;                                      /* DCW 1's first rate */
            r[12] = 30; r[13] = 0;                                              /* vibrato depth */
        }
        if (b == 120) { trk_note_off(&inst, 55); trk_note_off(&inst, 64); trk_note_off(&inst, 67); }
        if (b == 140) trk_note_on(&inst, 36, 90);
        if (b == 150 && pi % 3u == 0) tuning_a4 = 432;
        if (b == 180) trk_note_off(&inst, 36);
        mp_cz_block(b, batch);
    }
    tuning_a4 = a4;
    midi_verb[0] = 0;
    return mp_hash(mp_out, MP_NB * CTL);
}

/* phrase k (0 .. MP_COUNT - 1) rendered into mp_out: its hash; *what, *a, *b: what it plays */
#define MP_SID_PRESETS 32u
#define MP_DRUM_PRESETS 6u
#define MP_CZ_PRESETS 65u                                /* INIT TONE, Casio's 64 */
#define MP_SID_A (2u * MP_SID_PRESETS * 2u)              /* every preset x POLY / LEGATO, serial then on CPU1 */
#define MP_SID_B (2u * 4u * 2u * 2u)                     /* MODEL 0..3 x mode x ENV3, serial then on CPU1 */
#define MP_DRUM (2u * (MP_DRUM_PRESETS + 7u))            /* every preset, KIT 4..10, serial then on CPU1 */
#define MP_CZ (2u * MP_CZ_PRESETS * 2u)                  /* every tone x POLY / LEGATO, serial then on CPU1 */
#define MP_COUNT (MP_SID_A + MP_SID_B + MP_DRUM + MP_CZ)
#define MP_CZ0 (MP_SID_A + MP_SID_B + MP_DRUM)           /* (the first CZ-1 phrase) */
static uint32_t mp_run(uint32_t k, const char **what, uint32_t *a, uint32_t *b)
{
    if (k < MP_SID_A) {
        uint32_t batch = k / (MP_SID_A / 2u), pi = k % (MP_SID_A / 2u) / 2u, mono = k % 2u;
        *what = batch ? "SID preset (CPU1)" : "SID preset"; *a = pi; *b = mono;
        return mp_sid(pi, mono, -1, pi % 3u == 1u, batch);
    }
    k -= MP_SID_A;
    if (k < MP_SID_B) {                                  /* the chips and the C64s (one voice, frames, the chord) */
        uint32_t batch = k / (MP_SID_B / 2u), model = k % (MP_SID_B / 2u) / 4u, mono = k % 4u / 2u, env3 = k % 2u;
        *what = batch ? "SID MODEL x mode x ENV3 (CPU1)" : "SID MODEL x mode x ENV3"; *a = model; *b = mono * 2u + env3;
        return mp_sid(model * 7u + 2u, mono, (int32_t)model, env3, batch);
    }
    k -= MP_SID_B;
    if (k >= MP_DRUM) {                                  /* CZ-1 */
        k -= MP_DRUM;
        uint32_t batch = k / (MP_CZ / 2u), pi = k % (MP_CZ / 2u) / 2u, mono = k % 2u;
        *what = batch ? "CZ-1 tone (CPU1)" : "CZ-1 tone"; *a = pi; *b = mono;
        return mp_cz(pi, mono, batch);
    }
    uint32_t batch = k / (MP_DRUM / 2u), j = k % (MP_DRUM / 2u);
    if (j < MP_DRUM_PRESETS) {
        *what = batch ? "DRUM preset (CPU1)" : "DRUM preset"; *a = j; *b = 0;
        return mp_drum(j, -1, batch);
    }
    *what = batch ? "DRUM KIT (CPU1)" : "DRUM KIT"; *a = j - MP_DRUM_PRESETS + 4u; *b = 0;   /* 808 909 SID CR78 FM, */
    return mp_drum(0, (int32_t)(j - MP_DRUM_PRESETS + 4u), batch);                         /* user kits 1, 2 */
}

/* what the built-in engines rendered (203a5c04, before SID, DRUM and CZ-1 left the firmware), phrase by phrase */
static const uint32_t MP_GOLDEN[MP_COUNT] = {
    0x49b11ea6u, /*   0 SID preset 0 0 */
    0x12c28a5fu, /*   1 SID preset 0 1 */
    0x150f0581u, /*   2 SID preset 1 0 */
    0xe557cef3u, /*   3 SID preset 1 1 */
    0xdb5a7be2u, /*   4 SID preset 2 0 */
    0xcc10bc84u, /*   5 SID preset 2 1 */
    0xe5e18fdeu, /*   6 SID preset 3 0 */
    0xbb425ebeu, /*   7 SID preset 3 1 */
    0x09865b3au, /*   8 SID preset 4 0 */
    0x4dcd32f1u, /*   9 SID preset 4 1 */
    0x52712c05u, /*  10 SID preset 5 0 */
    0xb53e6705u, /*  11 SID preset 5 1 */
    0xa7c50b43u, /*  12 SID preset 6 0 */
    0xf65859b8u, /*  13 SID preset 6 1 */
    0x10e5929cu, /*  14 SID preset 7 0 */
    0xa0120418u, /*  15 SID preset 7 1 */
    0x2e256ce0u, /*  16 SID preset 8 0 */
    0xef60ab72u, /*  17 SID preset 8 1 */
    0x06e150e7u, /*  18 SID preset 9 0 */
    0xc3370e59u, /*  19 SID preset 9 1 */
    0x2ec6e515u, /*  20 SID preset 10 0 */
    0xba679f25u, /*  21 SID preset 10 1 */
    0x3c598a64u, /*  22 SID preset 11 0 */
    0x462ebeceu, /*  23 SID preset 11 1 */
    0xdf3e55d6u, /*  24 SID preset 12 0 */
    0xcd95b237u, /*  25 SID preset 12 1 */
    0xaa011512u, /*  26 SID preset 13 0 */
    0x4e581c07u, /*  27 SID preset 13 1 */
    0x13618464u, /*  28 SID preset 14 0 */
    0x3063ba5cu, /*  29 SID preset 14 1 */
    0x73097862u, /*  30 SID preset 15 0 */
    0x739733a1u, /*  31 SID preset 15 1 */
    0x5d70690bu, /*  32 SID preset 16 0 */
    0xf552d166u, /*  33 SID preset 16 1 */
    0xc675c4a3u, /*  34 SID preset 17 0 */
    0xbac75ccbu, /*  35 SID preset 17 1 */
    0x752b6e4au, /*  36 SID preset 18 0 */
    0x42d4a3d8u, /*  37 SID preset 18 1 */
    0xcd757428u, /*  38 SID preset 19 0 */
    0xef8d986fu, /*  39 SID preset 19 1 */
    0x0bcfd163u, /*  40 SID preset 20 0 */
    0x66b03725u, /*  41 SID preset 20 1 */
    0xeaa35a79u, /*  42 SID preset 21 0 */
    0x38e8afc7u, /*  43 SID preset 21 1 */
    0x6176731eu, /*  44 SID preset 22 0 */
    0x649f374cu, /*  45 SID preset 22 1 */
    0xf9eb38cfu, /*  46 SID preset 23 0 */
    0x38eff2ecu, /*  47 SID preset 23 1 */
    0x70586a99u, /*  48 SID preset 24 0 */
    0xcd9f2892u, /*  49 SID preset 24 1 */
    0x7b4cb34fu, /*  50 SID preset 25 0 */
    0xbd63542cu, /*  51 SID preset 25 1 */
    0x06927893u, /*  52 SID preset 26 0 */
    0x72b6fe20u, /*  53 SID preset 26 1 */
    0x227f5661u, /*  54 SID preset 27 0 */
    0x0e2ae971u, /*  55 SID preset 27 1 */
    0x9f8d3c07u, /*  56 SID preset 28 0 */
    0x2467cd0bu, /*  57 SID preset 28 1 */
    0x23098839u, /*  58 SID preset 29 0 */
    0xbf0a99deu, /*  59 SID preset 29 1 */
    0xa2c41d24u, /*  60 SID preset 30 0 */
    0x1c72a28au, /*  61 SID preset 30 1 */
    0x537ad2fcu, /*  62 SID preset 31 0 */
    0xbdf7c577u, /*  63 SID preset 31 1 */
    0x49b11ea6u, /*  64 SID preset (CPU1) 0 0 */
    0x12c28a5fu, /*  65 SID preset (CPU1) 0 1 */
    0x150f0581u, /*  66 SID preset (CPU1) 1 0 */
    0xe557cef3u, /*  67 SID preset (CPU1) 1 1 */
    0xdb5a7be2u, /*  68 SID preset (CPU1) 2 0 */
    0xcc10bc84u, /*  69 SID preset (CPU1) 2 1 */
    0xe5e18fdeu, /*  70 SID preset (CPU1) 3 0 */
    0xbb425ebeu, /*  71 SID preset (CPU1) 3 1 */
    0x09865b3au, /*  72 SID preset (CPU1) 4 0 */
    0x4dcd32f1u, /*  73 SID preset (CPU1) 4 1 */
    0x52712c05u, /*  74 SID preset (CPU1) 5 0 */
    0xb53e6705u, /*  75 SID preset (CPU1) 5 1 */
    0xa7c50b43u, /*  76 SID preset (CPU1) 6 0 */
    0xf65859b8u, /*  77 SID preset (CPU1) 6 1 */
    0x10e5929cu, /*  78 SID preset (CPU1) 7 0 */
    0xa0120418u, /*  79 SID preset (CPU1) 7 1 */
    0x2e256ce0u, /*  80 SID preset (CPU1) 8 0 */
    0xef60ab72u, /*  81 SID preset (CPU1) 8 1 */
    0x06e150e7u, /*  82 SID preset (CPU1) 9 0 */
    0xc3370e59u, /*  83 SID preset (CPU1) 9 1 */
    0x2ec6e515u, /*  84 SID preset (CPU1) 10 0 */
    0xba679f25u, /*  85 SID preset (CPU1) 10 1 */
    0x3c598a64u, /*  86 SID preset (CPU1) 11 0 */
    0x462ebeceu, /*  87 SID preset (CPU1) 11 1 */
    0xdf3e55d6u, /*  88 SID preset (CPU1) 12 0 */
    0xcd95b237u, /*  89 SID preset (CPU1) 12 1 */
    0xaa011512u, /*  90 SID preset (CPU1) 13 0 */
    0x4e581c07u, /*  91 SID preset (CPU1) 13 1 */
    0x13618464u, /*  92 SID preset (CPU1) 14 0 */
    0x3063ba5cu, /*  93 SID preset (CPU1) 14 1 */
    0x73097862u, /*  94 SID preset (CPU1) 15 0 */
    0x739733a1u, /*  95 SID preset (CPU1) 15 1 */
    0x5d70690bu, /*  96 SID preset (CPU1) 16 0 */
    0xf552d166u, /*  97 SID preset (CPU1) 16 1 */
    0xc675c4a3u, /*  98 SID preset (CPU1) 17 0 */
    0xbac75ccbu, /*  99 SID preset (CPU1) 17 1 */
    0x752b6e4au, /* 100 SID preset (CPU1) 18 0 */
    0x42d4a3d8u, /* 101 SID preset (CPU1) 18 1 */
    0xcd757428u, /* 102 SID preset (CPU1) 19 0 */
    0xef8d986fu, /* 103 SID preset (CPU1) 19 1 */
    0x0bcfd163u, /* 104 SID preset (CPU1) 20 0 */
    0x66b03725u, /* 105 SID preset (CPU1) 20 1 */
    0xeaa35a79u, /* 106 SID preset (CPU1) 21 0 */
    0x38e8afc7u, /* 107 SID preset (CPU1) 21 1 */
    0x6176731eu, /* 108 SID preset (CPU1) 22 0 */
    0x649f374cu, /* 109 SID preset (CPU1) 22 1 */
    0xf9eb38cfu, /* 110 SID preset (CPU1) 23 0 */
    0x38eff2ecu, /* 111 SID preset (CPU1) 23 1 */
    0x70586a99u, /* 112 SID preset (CPU1) 24 0 */
    0xcd9f2892u, /* 113 SID preset (CPU1) 24 1 */
    0x7b4cb34fu, /* 114 SID preset (CPU1) 25 0 */
    0xbd63542cu, /* 115 SID preset (CPU1) 25 1 */
    0x06927893u, /* 116 SID preset (CPU1) 26 0 */
    0x72b6fe20u, /* 117 SID preset (CPU1) 26 1 */
    0x227f5661u, /* 118 SID preset (CPU1) 27 0 */
    0x0e2ae971u, /* 119 SID preset (CPU1) 27 1 */
    0x9f8d3c07u, /* 120 SID preset (CPU1) 28 0 */
    0x2467cd0bu, /* 121 SID preset (CPU1) 28 1 */
    0x23098839u, /* 122 SID preset (CPU1) 29 0 */
    0xbf0a99deu, /* 123 SID preset (CPU1) 29 1 */
    0xa2c41d24u, /* 124 SID preset (CPU1) 30 0 */
    0x1c72a28au, /* 125 SID preset (CPU1) 30 1 */
    0x537ad2fcu, /* 126 SID preset (CPU1) 31 0 */
    0xbdf7c577u, /* 127 SID preset (CPU1) 31 1 */
    0xdb5a7be2u, /* 128 SID MODEL x mode x ENV3 0 0 */
    0x45be90a3u, /* 129 SID MODEL x mode x ENV3 0 1 */
    0xcc10bc84u, /* 130 SID MODEL x mode x ENV3 0 2 */
    0x9ad1b67cu, /* 131 SID MODEL x mode x ENV3 0 3 */
    0x06e150e7u, /* 132 SID MODEL x mode x ENV3 1 0 */
    0x7e352aa9u, /* 133 SID MODEL x mode x ENV3 1 1 */
    0xc3370e59u, /* 134 SID MODEL x mode x ENV3 1 2 */
    0x71def651u, /* 135 SID MODEL x mode x ENV3 1 3 */
    0xcc94b0d4u, /* 136 SID MODEL x mode x ENV3 2 0 */
    0x5d8b7fe6u, /* 137 SID MODEL x mode x ENV3 2 1 */
    0x8ca631c2u, /* 138 SID MODEL x mode x ENV3 2 2 */
    0x1d715ac3u, /* 139 SID MODEL x mode x ENV3 2 3 */
    0x64eb3de1u, /* 140 SID MODEL x mode x ENV3 3 0 */
    0xe12c2a44u, /* 141 SID MODEL x mode x ENV3 3 1 */
    0x4213387bu, /* 142 SID MODEL x mode x ENV3 3 2 */
    0x9244d7c4u, /* 143 SID MODEL x mode x ENV3 3 3 */
    0xdb5a7be2u, /* 144 SID MODEL x mode x ENV3 (CPU1) 0 0 */
    0x45be90a3u, /* 145 SID MODEL x mode x ENV3 (CPU1) 0 1 */
    0xcc10bc84u, /* 146 SID MODEL x mode x ENV3 (CPU1) 0 2 */
    0x9ad1b67cu, /* 147 SID MODEL x mode x ENV3 (CPU1) 0 3 */
    0x06e150e7u, /* 148 SID MODEL x mode x ENV3 (CPU1) 1 0 */
    0x7e352aa9u, /* 149 SID MODEL x mode x ENV3 (CPU1) 1 1 */
    0xc3370e59u, /* 150 SID MODEL x mode x ENV3 (CPU1) 1 2 */
    0x71def651u, /* 151 SID MODEL x mode x ENV3 (CPU1) 1 3 */
    0xcc94b0d4u, /* 152 SID MODEL x mode x ENV3 (CPU1) 2 0 */
    0x5d8b7fe6u, /* 153 SID MODEL x mode x ENV3 (CPU1) 2 1 */
    0x8ca631c2u, /* 154 SID MODEL x mode x ENV3 (CPU1) 2 2 */
    0x1d715ac3u, /* 155 SID MODEL x mode x ENV3 (CPU1) 2 3 */
    0x64eb3de1u, /* 156 SID MODEL x mode x ENV3 (CPU1) 3 0 */
    0xe12c2a44u, /* 157 SID MODEL x mode x ENV3 (CPU1) 3 1 */
    0x4213387bu, /* 158 SID MODEL x mode x ENV3 (CPU1) 3 2 */
    0x9244d7c4u, /* 159 SID MODEL x mode x ENV3 (CPU1) 3 3 */
    0x1eb916d0u, /* 160 DRUM preset 0 0 */
    0xebe76d7bu, /* 161 DRUM preset 1 0 */
    0x5081c485u, /* 162 DRUM preset 2 0 */
    0x87fb6450u, /* 163 DRUM preset 3 0 */
    0x1ae97771u, /* 164 DRUM preset 4 0 */
    0xe34871c5u, /* 165 DRUM preset 5 0 */
    0x1eb916d0u, /* 166 DRUM KIT 4 0 */
    0xebe76d7bu, /* 167 DRUM KIT 5 0 */
    0xb6a0dd03u, /* 168 DRUM KIT 6 0 */
    0x1ae97771u, /* 169 DRUM KIT 7 0 */
    0xe34871c5u, /* 170 DRUM KIT 8 0 */
    0x8a04edc5u, /* 171 DRUM KIT 9 0 */
    0x8a04edc5u, /* 172 DRUM KIT 10 0 */
    0x1eb916d0u, /* 173 DRUM preset (CPU1) 0 0 */
    0xebe76d7bu, /* 174 DRUM preset (CPU1) 1 0 */
    0x5081c485u, /* 175 DRUM preset (CPU1) 2 0 */
    0x87fb6450u, /* 176 DRUM preset (CPU1) 3 0 */
    0x1ae97771u, /* 177 DRUM preset (CPU1) 4 0 */
    0xe34871c5u, /* 178 DRUM preset (CPU1) 5 0 */
    0x1eb916d0u, /* 179 DRUM KIT (CPU1) 4 0 */
    0xebe76d7bu, /* 180 DRUM KIT (CPU1) 5 0 */
    0xb6a0dd03u, /* 181 DRUM KIT (CPU1) 6 0 */
    0x1ae97771u, /* 182 DRUM KIT (CPU1) 7 0 */
    0xe34871c5u, /* 183 DRUM KIT (CPU1) 8 0 */
    0x8a04edc5u, /* 184 DRUM KIT (CPU1) 9 0 */
    0x8a04edc5u, /* 185 DRUM KIT (CPU1) 10 0 */
    0x0358e790u, /* 186 CZ-1 tone 0 0 */
    0x00e7aed1u, /* 187 CZ-1 tone 0 1 */
    0xc04e68acu, /* 188 CZ-1 tone 1 0 */
    0x81f22b56u, /* 189 CZ-1 tone 1 1 */
    0xbb9a08e1u, /* 190 CZ-1 tone 2 0 */
    0x552b6ab5u, /* 191 CZ-1 tone 2 1 */
    0x976f837bu, /* 192 CZ-1 tone 3 0 */
    0xbd27b6feu, /* 193 CZ-1 tone 3 1 */
    0xf3eee60cu, /* 194 CZ-1 tone 4 0 */
    0xcc2a7500u, /* 195 CZ-1 tone 4 1 */
    0xfeeb1795u, /* 196 CZ-1 tone 5 0 */
    0x36f0eb58u, /* 197 CZ-1 tone 5 1 */
    0xf170e0cbu, /* 198 CZ-1 tone 6 0 */
    0x9388543cu, /* 199 CZ-1 tone 6 1 */
    0x18baad06u, /* 200 CZ-1 tone 7 0 */
    0x570c3dc0u, /* 201 CZ-1 tone 7 1 */
    0x198beb76u, /* 202 CZ-1 tone 8 0 */
    0x4c9176f9u, /* 203 CZ-1 tone 8 1 */
    0xae3727a2u, /* 204 CZ-1 tone 9 0 */
    0x8c34b69au, /* 205 CZ-1 tone 9 1 */
    0x0c339d3au, /* 206 CZ-1 tone 10 0 */
    0x23a5af76u, /* 207 CZ-1 tone 10 1 */
    0x3585dd45u, /* 208 CZ-1 tone 11 0 */
    0x14bff101u, /* 209 CZ-1 tone 11 1 */
    0x45b8211bu, /* 210 CZ-1 tone 12 0 */
    0xffc300c7u, /* 211 CZ-1 tone 12 1 */
    0xc9251020u, /* 212 CZ-1 tone 13 0 */
    0x62389da9u, /* 213 CZ-1 tone 13 1 */
    0x80398ad2u, /* 214 CZ-1 tone 14 0 */
    0xbfa11bf8u, /* 215 CZ-1 tone 14 1 */
    0x0843bd08u, /* 216 CZ-1 tone 15 0 */
    0x31712182u, /* 217 CZ-1 tone 15 1 */
    0x0933ad7du, /* 218 CZ-1 tone 16 0 */
    0xb80fcb6eu, /* 219 CZ-1 tone 16 1 */
    0x9d940842u, /* 220 CZ-1 tone 17 0 */
    0xcb1390edu, /* 221 CZ-1 tone 17 1 */
    0xacdb13a2u, /* 222 CZ-1 tone 18 0 */
    0xea4f4c40u, /* 223 CZ-1 tone 18 1 */
    0xafe5bf32u, /* 224 CZ-1 tone 19 0 */
    0x7fc51d26u, /* 225 CZ-1 tone 19 1 */
    0xaf9ade9eu, /* 226 CZ-1 tone 20 0 */
    0xa73940abu, /* 227 CZ-1 tone 20 1 */
    0x8f7c6f01u, /* 228 CZ-1 tone 21 0 */
    0xa4ca9aa2u, /* 229 CZ-1 tone 21 1 */
    0x715ae0fbu, /* 230 CZ-1 tone 22 0 */
    0x4946af63u, /* 231 CZ-1 tone 22 1 */
    0x62f9a1b7u, /* 232 CZ-1 tone 23 0 */
    0x5291cacau, /* 233 CZ-1 tone 23 1 */
    0x419d4072u, /* 234 CZ-1 tone 24 0 */
    0x96cb15adu, /* 235 CZ-1 tone 24 1 */
    0xf6ad8d87u, /* 236 CZ-1 tone 25 0 */
    0x6e586121u, /* 237 CZ-1 tone 25 1 */
    0x87d92a17u, /* 238 CZ-1 tone 26 0 */
    0xf30c71acu, /* 239 CZ-1 tone 26 1 */
    0x240623cau, /* 240 CZ-1 tone 27 0 */
    0x58a92702u, /* 241 CZ-1 tone 27 1 */
    0x1e82e432u, /* 242 CZ-1 tone 28 0 */
    0x343c8846u, /* 243 CZ-1 tone 28 1 */
    0x0039d941u, /* 244 CZ-1 tone 29 0 */
    0x1150ef0fu, /* 245 CZ-1 tone 29 1 */
    0x1a5d4f47u, /* 246 CZ-1 tone 30 0 */
    0x463ec913u, /* 247 CZ-1 tone 30 1 */
    0x94e63299u, /* 248 CZ-1 tone 31 0 */
    0x99d99d81u, /* 249 CZ-1 tone 31 1 */
    0x9ff50a1fu, /* 250 CZ-1 tone 32 0 */
    0x2ac5b990u, /* 251 CZ-1 tone 32 1 */
    0xf745bf0bu, /* 252 CZ-1 tone 33 0 */
    0x7696c7d6u, /* 253 CZ-1 tone 33 1 */
    0xd9effc69u, /* 254 CZ-1 tone 34 0 */
    0x4cfcd328u, /* 255 CZ-1 tone 34 1 */
    0xdf94fe55u, /* 256 CZ-1 tone 35 0 */
    0xbaff5c70u, /* 257 CZ-1 tone 35 1 */
    0x61162184u, /* 258 CZ-1 tone 36 0 */
    0x05eae4deu, /* 259 CZ-1 tone 36 1 */
    0xb97e46cfu, /* 260 CZ-1 tone 37 0 */
    0x972c76dau, /* 261 CZ-1 tone 37 1 */
    0xe1c9fc86u, /* 262 CZ-1 tone 38 0 */
    0xf77bea86u, /* 263 CZ-1 tone 38 1 */
    0xb00615e7u, /* 264 CZ-1 tone 39 0 */
    0x2ef4385fu, /* 265 CZ-1 tone 39 1 */
    0x85159d4du, /* 266 CZ-1 tone 40 0 */
    0xc3dad3ebu, /* 267 CZ-1 tone 40 1 */
    0xa06d8086u, /* 268 CZ-1 tone 41 0 */
    0x112ee226u, /* 269 CZ-1 tone 41 1 */
    0x3be781f7u, /* 270 CZ-1 tone 42 0 */
    0xa3283850u, /* 271 CZ-1 tone 42 1 */
    0xa88522e8u, /* 272 CZ-1 tone 43 0 */
    0x30460690u, /* 273 CZ-1 tone 43 1 */
    0x3fa51a3cu, /* 274 CZ-1 tone 44 0 */
    0xe29bd0c2u, /* 275 CZ-1 tone 44 1 */
    0xe8b6a4eau, /* 276 CZ-1 tone 45 0 */
    0x6442d856u, /* 277 CZ-1 tone 45 1 */
    0x19544be8u, /* 278 CZ-1 tone 46 0 */
    0x21403637u, /* 279 CZ-1 tone 46 1 */
    0x9323a177u, /* 280 CZ-1 tone 47 0 */
    0xdaaabbd7u, /* 281 CZ-1 tone 47 1 */
    0xead128c5u, /* 282 CZ-1 tone 48 0 */
    0x3a9cabcdu, /* 283 CZ-1 tone 48 1 */
    0x546f4825u, /* 284 CZ-1 tone 49 0 */
    0x54ae630eu, /* 285 CZ-1 tone 49 1 */
    0x660aaa82u, /* 286 CZ-1 tone 50 0 */
    0xad10e74du, /* 287 CZ-1 tone 50 1 */
    0x812101fau, /* 288 CZ-1 tone 51 0 */
    0xa29ac115u, /* 289 CZ-1 tone 51 1 */
    0x58f2c109u, /* 290 CZ-1 tone 52 0 */
    0x2507d9cau, /* 291 CZ-1 tone 52 1 */
    0xfa68cc39u, /* 292 CZ-1 tone 53 0 */
    0x96110493u, /* 293 CZ-1 tone 53 1 */
    0xb7ab7fd7u, /* 294 CZ-1 tone 54 0 */
    0xf57f7a53u, /* 295 CZ-1 tone 54 1 */
    0x6cf93f81u, /* 296 CZ-1 tone 55 0 */
    0xe5d14d2fu, /* 297 CZ-1 tone 55 1 */
    0xab3e06c5u, /* 298 CZ-1 tone 56 0 */
    0xb70ba228u, /* 299 CZ-1 tone 56 1 */
    0x0693335eu, /* 300 CZ-1 tone 57 0 */
    0x2f6f796eu, /* 301 CZ-1 tone 57 1 */
    0xf03c379au, /* 302 CZ-1 tone 58 0 */
    0xaf13ab4bu, /* 303 CZ-1 tone 58 1 */
    0x2ca46b71u, /* 304 CZ-1 tone 59 0 */
    0x71f35db5u, /* 305 CZ-1 tone 59 1 */
    0x6bf29b5cu, /* 306 CZ-1 tone 60 0 */
    0xf355d201u, /* 307 CZ-1 tone 60 1 */
    0x871bcb71u, /* 308 CZ-1 tone 61 0 */
    0xab1a5478u, /* 309 CZ-1 tone 61 1 */
    0x0cfe2662u, /* 310 CZ-1 tone 62 0 */
    0x354459c1u, /* 311 CZ-1 tone 62 1 */
    0x037aa506u, /* 312 CZ-1 tone 63 0 */
    0xf3603e1fu, /* 313 CZ-1 tone 63 1 */
    0x06d4db46u, /* 314 CZ-1 tone 64 0 */
    0xb565d365u, /* 315 CZ-1 tone 64 1 */
    0x0358e790u, /* 316 CZ-1 tone (CPU1) 0 0 */
    0x00e7aed1u, /* 317 CZ-1 tone (CPU1) 0 1 */
    0xc04e68acu, /* 318 CZ-1 tone (CPU1) 1 0 */
    0x81f22b56u, /* 319 CZ-1 tone (CPU1) 1 1 */
    0xbb9a08e1u, /* 320 CZ-1 tone (CPU1) 2 0 */
    0x552b6ab5u, /* 321 CZ-1 tone (CPU1) 2 1 */
    0x976f837bu, /* 322 CZ-1 tone (CPU1) 3 0 */
    0xbd27b6feu, /* 323 CZ-1 tone (CPU1) 3 1 */
    0xf3eee60cu, /* 324 CZ-1 tone (CPU1) 4 0 */
    0xcc2a7500u, /* 325 CZ-1 tone (CPU1) 4 1 */
    0xfeeb1795u, /* 326 CZ-1 tone (CPU1) 5 0 */
    0x36f0eb58u, /* 327 CZ-1 tone (CPU1) 5 1 */
    0xf170e0cbu, /* 328 CZ-1 tone (CPU1) 6 0 */
    0x9388543cu, /* 329 CZ-1 tone (CPU1) 6 1 */
    0x18baad06u, /* 330 CZ-1 tone (CPU1) 7 0 */
    0x570c3dc0u, /* 331 CZ-1 tone (CPU1) 7 1 */
    0x198beb76u, /* 332 CZ-1 tone (CPU1) 8 0 */
    0x4c9176f9u, /* 333 CZ-1 tone (CPU1) 8 1 */
    0xae3727a2u, /* 334 CZ-1 tone (CPU1) 9 0 */
    0x8c34b69au, /* 335 CZ-1 tone (CPU1) 9 1 */
    0x0c339d3au, /* 336 CZ-1 tone (CPU1) 10 0 */
    0x23a5af76u, /* 337 CZ-1 tone (CPU1) 10 1 */
    0x3585dd45u, /* 338 CZ-1 tone (CPU1) 11 0 */
    0x14bff101u, /* 339 CZ-1 tone (CPU1) 11 1 */
    0x45b8211bu, /* 340 CZ-1 tone (CPU1) 12 0 */
    0xffc300c7u, /* 341 CZ-1 tone (CPU1) 12 1 */
    0xc9251020u, /* 342 CZ-1 tone (CPU1) 13 0 */
    0x62389da9u, /* 343 CZ-1 tone (CPU1) 13 1 */
    0x80398ad2u, /* 344 CZ-1 tone (CPU1) 14 0 */
    0xbfa11bf8u, /* 345 CZ-1 tone (CPU1) 14 1 */
    0x0843bd08u, /* 346 CZ-1 tone (CPU1) 15 0 */
    0x31712182u, /* 347 CZ-1 tone (CPU1) 15 1 */
    0x0933ad7du, /* 348 CZ-1 tone (CPU1) 16 0 */
    0xb80fcb6eu, /* 349 CZ-1 tone (CPU1) 16 1 */
    0x9d940842u, /* 350 CZ-1 tone (CPU1) 17 0 */
    0xcb1390edu, /* 351 CZ-1 tone (CPU1) 17 1 */
    0xacdb13a2u, /* 352 CZ-1 tone (CPU1) 18 0 */
    0xea4f4c40u, /* 353 CZ-1 tone (CPU1) 18 1 */
    0xafe5bf32u, /* 354 CZ-1 tone (CPU1) 19 0 */
    0x7fc51d26u, /* 355 CZ-1 tone (CPU1) 19 1 */
    0xaf9ade9eu, /* 356 CZ-1 tone (CPU1) 20 0 */
    0xa73940abu, /* 357 CZ-1 tone (CPU1) 20 1 */
    0x8f7c6f01u, /* 358 CZ-1 tone (CPU1) 21 0 */
    0xa4ca9aa2u, /* 359 CZ-1 tone (CPU1) 21 1 */
    0x715ae0fbu, /* 360 CZ-1 tone (CPU1) 22 0 */
    0x4946af63u, /* 361 CZ-1 tone (CPU1) 22 1 */
    0x62f9a1b7u, /* 362 CZ-1 tone (CPU1) 23 0 */
    0x5291cacau, /* 363 CZ-1 tone (CPU1) 23 1 */
    0x419d4072u, /* 364 CZ-1 tone (CPU1) 24 0 */
    0x96cb15adu, /* 365 CZ-1 tone (CPU1) 24 1 */
    0xf6ad8d87u, /* 366 CZ-1 tone (CPU1) 25 0 */
    0x6e586121u, /* 367 CZ-1 tone (CPU1) 25 1 */
    0x87d92a17u, /* 368 CZ-1 tone (CPU1) 26 0 */
    0xf30c71acu, /* 369 CZ-1 tone (CPU1) 26 1 */
    0x240623cau, /* 370 CZ-1 tone (CPU1) 27 0 */
    0x58a92702u, /* 371 CZ-1 tone (CPU1) 27 1 */
    0x1e82e432u, /* 372 CZ-1 tone (CPU1) 28 0 */
    0x343c8846u, /* 373 CZ-1 tone (CPU1) 28 1 */
    0x0039d941u, /* 374 CZ-1 tone (CPU1) 29 0 */
    0x1150ef0fu, /* 375 CZ-1 tone (CPU1) 29 1 */
    0x1a5d4f47u, /* 376 CZ-1 tone (CPU1) 30 0 */
    0x463ec913u, /* 377 CZ-1 tone (CPU1) 30 1 */
    0x94e63299u, /* 378 CZ-1 tone (CPU1) 31 0 */
    0x99d99d81u, /* 379 CZ-1 tone (CPU1) 31 1 */
    0x9ff50a1fu, /* 380 CZ-1 tone (CPU1) 32 0 */
    0x2ac5b990u, /* 381 CZ-1 tone (CPU1) 32 1 */
    0xf745bf0bu, /* 382 CZ-1 tone (CPU1) 33 0 */
    0x7696c7d6u, /* 383 CZ-1 tone (CPU1) 33 1 */
    0xd9effc69u, /* 384 CZ-1 tone (CPU1) 34 0 */
    0x4cfcd328u, /* 385 CZ-1 tone (CPU1) 34 1 */
    0xdf94fe55u, /* 386 CZ-1 tone (CPU1) 35 0 */
    0xbaff5c70u, /* 387 CZ-1 tone (CPU1) 35 1 */
    0x61162184u, /* 388 CZ-1 tone (CPU1) 36 0 */
    0x05eae4deu, /* 389 CZ-1 tone (CPU1) 36 1 */
    0xb97e46cfu, /* 390 CZ-1 tone (CPU1) 37 0 */
    0x972c76dau, /* 391 CZ-1 tone (CPU1) 37 1 */
    0xe1c9fc86u, /* 392 CZ-1 tone (CPU1) 38 0 */
    0xf77bea86u, /* 393 CZ-1 tone (CPU1) 38 1 */
    0xb00615e7u, /* 394 CZ-1 tone (CPU1) 39 0 */
    0x2ef4385fu, /* 395 CZ-1 tone (CPU1) 39 1 */
    0x85159d4du, /* 396 CZ-1 tone (CPU1) 40 0 */
    0xc3dad3ebu, /* 397 CZ-1 tone (CPU1) 40 1 */
    0xa06d8086u, /* 398 CZ-1 tone (CPU1) 41 0 */
    0x112ee226u, /* 399 CZ-1 tone (CPU1) 41 1 */
    0x3be781f7u, /* 400 CZ-1 tone (CPU1) 42 0 */
    0xa3283850u, /* 401 CZ-1 tone (CPU1) 42 1 */
    0xa88522e8u, /* 402 CZ-1 tone (CPU1) 43 0 */
    0x30460690u, /* 403 CZ-1 tone (CPU1) 43 1 */
    0x3fa51a3cu, /* 404 CZ-1 tone (CPU1) 44 0 */
    0xe29bd0c2u, /* 405 CZ-1 tone (CPU1) 44 1 */
    0xe8b6a4eau, /* 406 CZ-1 tone (CPU1) 45 0 */
    0x6442d856u, /* 407 CZ-1 tone (CPU1) 45 1 */
    0x19544be8u, /* 408 CZ-1 tone (CPU1) 46 0 */
    0x21403637u, /* 409 CZ-1 tone (CPU1) 46 1 */
    0x9323a177u, /* 410 CZ-1 tone (CPU1) 47 0 */
    0xdaaabbd7u, /* 411 CZ-1 tone (CPU1) 47 1 */
    0xead128c5u, /* 412 CZ-1 tone (CPU1) 48 0 */
    0x3a9cabcdu, /* 413 CZ-1 tone (CPU1) 48 1 */
    0x546f4825u, /* 414 CZ-1 tone (CPU1) 49 0 */
    0x54ae630eu, /* 415 CZ-1 tone (CPU1) 49 1 */
    0x660aaa82u, /* 416 CZ-1 tone (CPU1) 50 0 */
    0xad10e74du, /* 417 CZ-1 tone (CPU1) 50 1 */
    0x812101fau, /* 418 CZ-1 tone (CPU1) 51 0 */
    0xa29ac115u, /* 419 CZ-1 tone (CPU1) 51 1 */
    0x58f2c109u, /* 420 CZ-1 tone (CPU1) 52 0 */
    0x2507d9cau, /* 421 CZ-1 tone (CPU1) 52 1 */
    0xfa68cc39u, /* 422 CZ-1 tone (CPU1) 53 0 */
    0x96110493u, /* 423 CZ-1 tone (CPU1) 53 1 */
    0xb7ab7fd7u, /* 424 CZ-1 tone (CPU1) 54 0 */
    0xf57f7a53u, /* 425 CZ-1 tone (CPU1) 54 1 */
    0x6cf93f81u, /* 426 CZ-1 tone (CPU1) 55 0 */
    0xe5d14d2fu, /* 427 CZ-1 tone (CPU1) 55 1 */
    0xab3e06c5u, /* 428 CZ-1 tone (CPU1) 56 0 */
    0xb70ba228u, /* 429 CZ-1 tone (CPU1) 56 1 */
    0x0693335eu, /* 430 CZ-1 tone (CPU1) 57 0 */
    0x2f6f796eu, /* 431 CZ-1 tone (CPU1) 57 1 */
    0xf03c379au, /* 432 CZ-1 tone (CPU1) 58 0 */
    0xaf13ab4bu, /* 433 CZ-1 tone (CPU1) 58 1 */
    0x2ca46b71u, /* 434 CZ-1 tone (CPU1) 59 0 */
    0x71f35db5u, /* 435 CZ-1 tone (CPU1) 59 1 */
    0x6bf29b5cu, /* 436 CZ-1 tone (CPU1) 60 0 */
    0xf355d201u, /* 437 CZ-1 tone (CPU1) 60 1 */
    0x871bcb71u, /* 438 CZ-1 tone (CPU1) 61 0 */
    0xab1a5478u, /* 439 CZ-1 tone (CPU1) 61 1 */
    0x0cfe2662u, /* 440 CZ-1 tone (CPU1) 62 0 */
    0x354459c1u, /* 441 CZ-1 tone (CPU1) 62 1 */
    0x037aa506u, /* 442 CZ-1 tone (CPU1) 63 0 */
    0xf3603e1fu, /* 443 CZ-1 tone (CPU1) 63 1 */
    0x06d4db46u, /* 444 CZ-1 tone (CPU1) 64 0 */
    0xb565d365u, /* 445 CZ-1 tone (CPU1) 64 1 */
};
