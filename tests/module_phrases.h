/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The phrases the native-module tests render (tests/module_test.c): SID and DRUM on part 1, every preset, POLY and
 * LEGATO, the C64 models, ENV3 as ENV DEST, every kit with the drum patch, lane levels, DRV, recorded hit lengths and
 * pitches, each also on the dual-core path (engine_batch_kernel). Each phrase starts from the same state, so its
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

/* phrase k (0 .. MP_COUNT - 1) rendered into mp_out: its hash; *what, *a, *b: what it plays */
#define MP_SID_PRESETS 32u
#define MP_DRUM_PRESETS 6u
#define MP_SID_A (2u * MP_SID_PRESETS * 2u)              /* every preset x POLY / LEGATO, serial then on CPU1 */
#define MP_SID_B (2u * 4u * 2u * 2u)                     /* MODEL 0..3 x mode x ENV3, serial then on CPU1 */
#define MP_DRUM (2u * (MP_DRUM_PRESETS + 7u))            /* every preset, KIT 4..10, serial then on CPU1 */
#define MP_COUNT (MP_SID_A + MP_SID_B + MP_DRUM)
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
    uint32_t batch = k / (MP_DRUM / 2u), j = k % (MP_DRUM / 2u);
    if (j < MP_DRUM_PRESETS) {
        *what = batch ? "DRUM preset (CPU1)" : "DRUM preset"; *a = j; *b = 0;
        return mp_drum(j, -1, batch);
    }
    *what = batch ? "DRUM KIT (CPU1)" : "DRUM KIT"; *a = j - MP_DRUM_PRESETS + 4u; *b = 0;   /* 808 909 SID CR78 FM, */
    return mp_drum(0, (int32_t)(j - MP_DRUM_PRESETS + 4u), batch);                         /* user kits 1, 2 */
}

/* what the built-in engines rendered (203a5c04, before SID and DRUM left the firmware), phrase by phrase */
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
};
