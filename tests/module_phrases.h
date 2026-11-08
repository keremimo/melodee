/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The phrases the native-module tests render (tests/module_test.c): SID, DRUM, CZ-1, FM6 and the Prophet on part 1,
 * every preset, POLY and LEGATO, the C64 models, ENV3 as ENV DEST, every kit with the drum patch, lane levels, DRV,
 * recorded hit lengths and pitches, every CZ-1 tone edited as it plays, the matrix's ENV (CZ-1's DCA), LFO, a MIDI
 * insert release, concert pitch, every FM6 voice in POLY, LEGATO and UNISON with every algorithm, feedback, the three
 * ENGINEs, its controllers, macros and switches as it plays, a DX7 bank by SysEx, every Prophet program in POLY and
 * UNISON edited as it plays (both filters, the envelopes, Poly-Mod, the LFO and the wheel, Lo Freq, glide, sync,
 * vintage, voices stolen), each also on the dual-core path (engine_batch_kernel; FM6, the Prophet: their own queue
 * jobs). Each phrase starts from the same state, so its
 * render is a function of the phrase alone: module_test.c holds the modules to the built-in engines sample for sample
 * and to MP_GOLDEN, the hashes the built-in engines rendered before they left the firmware (SID, DRUM, CZ-1: 203a5c04,
 * FM6: 0df43be6, the Prophet: 47d57c42; this file compiled there with tests/persistence_test.c and fm6_store.c, each
 * phrase's mp_run printed).
 * Needs persistence_test.c (reset, host_preset) and sysex_route.c (msx_service: the DX7 SysEx to FM6's module). */
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
    memset((void *)pads_flash, 0, sizeof pads_flash);
    note_hit_pitch = 0;
    note_hit_len = 0;
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
    host_preset(&inst, ENGI_SID, pi);
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

/* DRUM's kits (2.0: the kit synths): 1.x DRUM's preset pi on its kit synth, or KIT kit (1.x: DRUM's preset 0, then
 * KIT kit; here on the kit synth that plays it), the GM notes from 35 up, every lane, chokes, retriggers; the part's
 * drum patch offsets, lane levels, DRV and ACC moved; recorded lengths and pitches on some hits */
static uint32_t mp_drum(uint32_t pi, int32_t kit, uint32_t batch)
{
    mp_start();
    if (kit >= 0)
        host_kit_values(&inst, kit);
    else
        host_kit_preset(&inst, pi);
    for (uint32_t s = 0; s < 16u; s++) {             /* every sound a little off its factory setting */
        int8_t *c = (int8_t *)(void *)patch_of(0) + 4u * s;   /* (the part's patch: DRUM's offsets) */
        c[0] = (int8_t)((int32_t)(s % 5u) - 2);
        c[1] = (int8_t)((int32_t)(s * 7u % 21u) - 10);
        c[2] = (int8_t)((int32_t)(s * 5u % 17u) - 8);
        c[3] = (int8_t)(s % 3u * 10u);
    }
    for (uint32_t b = 0; b < MP_NB; b++) {
        if (b % 3u == 0 && b < 150u) {
            uint32_t note = 35u + (b / 3u) % 47u;
            if (b % 15u == 0) { note_hit_pitch = (int32_t)(b % 7u) - 3; note_hit_len = 900u + b * 3u; }
            trk_note_on(&inst, note, 40u + (b * 13u) % 88u);
            note_hit_pitch = 0;
            note_hit_len = 0;
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
            uint8_t *r = patch_of(0);                    /* (the part's patch: the CZ-1's tone) */
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

/* FM6, the Prophet: a block as mix.c runs it: the matrix (mix_part), on CPU1 the part's post (Dexed's DC filter, the
 * Prophet's coupling capacitor) after the queue */
static void mp_fm6_block(uint32_t b, uint32_t batch)
{
    int32_t *out = mp_out + b * CTL;
    dither_block();
    audio_batch_active = (uint8_t)batch;
    mod_begin(&inst);
    uint32_t nr = track_render(&inst, out, CTL);
    if (batch) {
        audio_batch_flush();
        audio_batch_active = 0;
        if (nr && ENGINES[inst.engine]->post)
            ENGINES[inst.engine]->post(&inst, out, CTL, nr);
    }
    if (mod.on)
        mod_end(&inst);
    audio_batch_active = 0;
}
/* FM6 preset pi (F1..F24, INIT VOICE); mode 0 POLY, 1 LEGATO, 2 UNISON. By pi: ENGINE (MODERN, MARK I, OPL), the
 * algorithm (ALG: PAT and all 32 over the phrases), FB, velocity scaling, LFO -> FLT / SHP (MLVL, the feedback),
 * portamento and glissando (LEGATO), a MIDI insert release, a new TRANSPOSE as it plays (the notes stop, as a DX7
 * program change), concert pitch moved. In every phrase: the wheel, aftertouch, foot and breath with their targets,
 * pitch bend both ways, an operator switched off, the other macros and the patch edited as the notes sound (the
 * sounding notes follow), 18 keys at once (POLY: Dexed's 16 voices stolen) */
static uint32_t mp_fm6(uint32_t pi, uint32_t mode, uint32_t batch)
{
    int16_t a4 = tuning_a4;
    uint8_t *fn = patch_of(0) + FM6_B_FN;               /* (the part's patch: FM6's functions) */
    mp_start();
    memset(midi_verb, 0, sizeof midi_verb);
    host_preset(&inst, ENGI_FM6, pi);
    memcpy(fn, FM6_FNDEF, FM6_NFN);
    inst.p[P_VOICE] = (int16_t)(mode == 1u ? V_LEGATO : mode == 2u ? V_UNISON : V_POLY);
    inst.p[P_E0] = (int16_t)((pi + mode * 25u) % 33u);   /* ALG: PAT, 1..32 */
    inst.p[P_E1] = (int16_t)((int32_t)(pi % 15u) - 7);   /* FB */
    fn[FN_ENGINE] = (uint8_t)(pi % 3u);
    fn[FN_VNORM] = (uint8_t)(pi % 2u);
    fn[FN_ATR] = 60; fn[FN_ATA] = (uint8_t)(1u + pi % 7u);
    fn[FN_FCR] = 50; fn[FN_FCA] = 4;
    fn[FN_BCR] = 80; fn[FN_BCA] = 3;
    fn[FN_PBSTEP] = (uint8_t)(pi % 5u == 4u ? 2u : 0u);
    if (mode == 1u && pi % 2u) { fn[FN_PMODE] = 1; fn[FN_PTIME] = (uint8_t)(40u + pi); fn[FN_GLISS] = (uint8_t)(pi % 4u == 1u); }
    if (pi % 5u == 1u) { inst.p[P_LD_FLT] = 30; inst.p[P_LD_SHP] = 40; }
    if (pi % 4u == 3u) { inst.p[P_FXTYPE] = FX_MIDIVERB; inst.p[P_FXTYPE + 1] = 40; }   /* (the insert's release) */
    for (uint32_t b = 0; b < MP_NB; b++) {
        if (b == 4) trk_note_on(&inst, 48, 100);
        if (b == 20) { inst.mw = 90; inst.at = 60; }
        if (b == 30) inst.bend_raw = 3000;
        if (b == 40) trk_note_on(&inst, 55, 80);
        if (b == 50) { inst.foot = 100; inst.breath = 70; }
        if (b == 60) trk_note_off(&inst, 48);
        if (b == 70) trk_note_on(&inst, 64, 127);
        if (b == 71) trk_note_on(&inst, 67, 30);
        if (b == 90) fm6_on_set(0, FM6_ON_ALL & ~(1u << (pi % 6u)));   /* (an operator switched off) */
        if (b == 100) {                                  /* MLVL MRAT MEG VMOD DTUN */
            inst.p[P_E2] = (int16_t)(20 - (int32_t)pi); inst.p[P_E3] = (int16_t)((int32_t)(pi % 5u) - 2);
            inst.p[P_E4] = (int16_t)((int32_t)(pi * 7u % 60u) - 30); inst.p[P_E5] = (int16_t)((int32_t)(pi % 8u) - 3);
            inst.p[P_E6] = (int16_t)(pi * 5u % 128u);
        }
        if (b == 110) {                                  /* the patch edited as the pages edit it */
            uint8_t v[FP_SIZE + 1u];
            fm6_voice_of(0, v);
            v[FP_FB] = (uint8_t)((v[FP_FB] + 3u) & 7u);
            v[(pi % 6u) * FP_OP + FP_OL] = (uint8_t)(v[(pi % 6u) * FP_OP + FP_OL] / 2u + 40u);
            v[(pi % 6u) * FP_OP + FP_R1 + 3u] = 20;
            v[FP_LPMD] = 40; v[FP_LPMS] = 3; v[FP_LAMD] = 30;
            if (pi % 3u == 2u) v[FP_TRNSP] = 26;          /* (another TRANSPOSE: a load, the notes stop) */
            if (pi % 2u == 0) {                           /* KEY SYNC off, short releases: voices end, run on as */
                v[FP_OKS] = 0;                            /* Dexed's (fm6_ghost) and are taken again below */
                for (uint32_t k = 0; k < 6u; k++) { v[k * FP_OP + FP_R1 + 3u] = 80; v[k * FP_OP + FP_L1 + 3u] = 0; }
            }
            fm6_set_patch(0, v);
        }
        if (b == 120) { trk_note_off(&inst, 55); trk_note_off(&inst, 64); trk_note_off(&inst, 67); inst.bend_raw = -2000; }
        if (b >= 130 && b < 136)
            for (uint32_t j = 0; j < 3u; j++) trk_note_on(&inst, 40u + 3u * ((b - 130u) * 3u + j), 50u + (b - 130u) * 12u + j);
        if (b == 140) fm6_on_set(0, FM6_ON_ALL);
        if (b == 150 && pi % 3u == 0) tuning_a4 = 432;
        if (b >= 160 && b < 166)
            for (uint32_t j = 0; j < 3u; j++) trk_note_off(&inst, 40u + 3u * ((b - 160u) * 3u + j));
        if (b == 175) { inst.bend_raw = 0; inst.mw = 0; trk_note_on(&inst, 36, 90); }
        if (b >= 178 && b < 184)                         /* (again: voices that ended and ran on are taken) */
            for (uint32_t j = 0; j < 3u; j++) trk_note_on(&inst, 41u + 2u * ((b - 178u) * 3u + j), 70u + j * 9u);
        if (b >= 186 && b < 192)
            for (uint32_t j = 0; j < 3u; j++) trk_note_off(&inst, 41u + 2u * ((b - 186u) * 3u + j));
        if (b == 190) trk_note_off(&inst, 36);
        mp_fm6_block(b, batch);
    }
    tuning_a4 = a4;
    midi_verb[0] = 0;
    inst.mw = inst.at = inst.foot = inst.breath = 0;
    inst.bend_raw = 0;
    return mp_hash(mp_out, MP_NB * CTL);
}

/* DX7 SysEx: a 32-voice bank (F1..F24, then 8 voices of random values in their ranges) through the USB byte
 * collector, imported into the user slots as the PRESETS page does, then voices 4j..4j+3 of it loaded on the track as
 * the user list loads them, played; Dexed's live voice and function parameter messages as it plays */
static uint8_t mp_sx[MOD3_SX_MAX];
static void mp_fm6_send(const uint8_t *m, uint32_t n)
{
    wire_release();                                       /* (the next main-loop turn: the scratch free) */
    for (uint32_t i = 0; i < n; i++) msx_byte(m[i], MSX_USB);
    msx_service();                                        /* (FM6's module: its fm6_sysex.c) */
}
static uint32_t mp_fm6_sx(uint32_t j, uint32_t batch)
{
    uint8_t v[FP_SIZE + 1u];
    uint32_t r = 0x13579BDFu;
    mp_start();
    memset(midi_verb, 0, sizeof midi_verb);
    host_preset(&inst, ENGI_FM6, FM6_NFAC);
    memcpy(patch_of(0) + FM6_B_FN, FM6_FNDEF, FM6_NFN);
    memcpy(mp_sx, (const uint8_t[]){0xF0, 0x43, 0x00, 0x09, 0x20, 0x00}, 6);
    for (uint32_t k = 0; k < 32u; k++) {
        uint8_t *pk = mp_sx + 6u + k * FM6_PACKED;
        if (k < FM6_NFAC) { (void)preset_patch(ENGI_FM6, k, pk, FM6_PACKED); continue; }   /* (F1..F24) */
        for (uint32_t i = 0; i < FP_SIZE; i++) { r = r * 1664525u + 1013904223u; v[i] = (uint8_t)((r >> 16) % (fm6_max(i) + 1u)); }
        for (uint32_t i = 0; i < 10u; i++) v[FP_NAME + i] = (uint8_t)('A' + (k + i) % 26u);
        v[FP_TRNSP] = (uint8_t)(18u + k % 12u);
        memset(pk, 0, FM6_PACKED);
        fm6_pack(v, pk);
    }
    mp_sx[4102] = sx_checksum(mp_sx + 6, 4096);
    mp_sx[4103] = 0xF7;
    mp_fm6_send(mp_sx, MOD3_SX_MAX);
    sx_import.first = 0;
    sysex_commit();
    for (uint32_t b = 0; b < MP_NB; b++) {
        if (b % 50u == 0) {
            native_load(ENGI_FM6, j * 4u + b / 50u, 0);
            if (j % 2u) { inst.p[P_FXTYPE] = FX_MIDIVERB; inst.p[P_FXTYPE + 1] = (int16_t)(20u + j * 10u); }
        }
        if (b % 50u == 2u) trk_note_on(&inst, 48, 100);
        if (b % 50u == 10u) { trk_note_on(&inst, 60, 90); trk_note_on(&inst, 64, 70); trk_note_on(&inst, 67, 110); }
        if (b % 50u == 25u) {                            /* Dexed: a voice parameter, the switches, a function */
            uint8_t m[7] = {0xF0, 0x43, 0x10, 0x01, 0x06, (uint8_t)((j * 5u + b) % 32u), 0xF7};   /* ALG */
            mp_fm6_send(m, 7);
            m[3] = 0x00; m[4] = (uint8_t)(16u + 21u * (b / 50u)); m[5] = (uint8_t)(60u + j);      /* an OUTPUT LEVEL */
            mp_fm6_send(m, 7);
            m[3] = 0x01; m[4] = 0x1B; m[5] = (uint8_t)(0x3Fu & ~(4u << (j % 4u)));            /* the switches */
            mp_fm6_send(m, 7);
            m[3] = 0x08; m[4] = 65; m[5] = (uint8_t)(2u + j);                                   /* bend range */
            mp_fm6_send(m, 7);
            inst.bend_raw = (int16_t)(j % 2u ? 4000 : -4000);
        }
        if (b % 50u == 30u) { trk_note_off(&inst, 48); trk_note_off(&inst, 60); trk_note_off(&inst, 64); trk_note_off(&inst, 67); }
        if (b % 50u == 45u) inst.bend_raw = 0;
        mp_fm6_block(b, batch);
    }
    inst.bend_raw = 0;
    return mp_hash(mp_out, MP_NB * CTL);
}

/* the Prophet: program pi (0 INIT, Sequential's 200); mode 0 POLY, 1 UNISON (the program's unison, its voice count and
 * detune by pi). Before the first key, by pi: Lo Freq B (a fresh voice's free-running B), glide, vintage (each voice's
 * own mismatch), a filter oscillating behind a shut mixer (its seeded state), sync. As it plays: the mod wheel and
 * aftertouch, pitch bend both ways over the program's range (its bend_range: voice.c trk_bend_own), the E1..E8 offsets, then the
 * program edited as the panel edits it: the filter model swapped (Curtis / SSI: its saturating stages, the SSM
 * envelope), cutoff, resonance, key tracking, both envelopes, velocity, Poly-Mod (the filter envelope and oscillator B
 * on frequency A, PW A, the filter), the LFO (waves, rate, the noise mix; the wheel on both frequencies, both widths,
 * the filter; aftertouch on the LFO and the filter), Lo Freq B, sync, release off, the retrigger. By pi: the matrix's
 * ENV (the amp envelope: engine_t.menv) on cutoff and pitch, the track's LFO on pitch and cutoff, a MIDI insert release,
 * a small voice budget (budget victims fade out: the engine's last-sample fade), concert pitch moved. In every phrase:
 * 8 keys at once and a key struck again (POLY: the engine's 5 voices stolen and reused), the DC filter (post) */
/* a key on the Prophet's part. UNISON (mono): mono_play's random start phases come from one seed for the whole run, so
 * a voice that starts afresh after it ended would take its phase from the phrases before; it starts from the phase a
 * new phrase gives it instead, so each phrase is a function of itself alone */
static void mp_p5_key(uint32_t mono, uint32_t note, uint32_t vel)
{
    for (uint32_t i = 0; mono && i < NVOICE; i++)
        if (!inst.v[i].active) inst.v[i].ph[0] = 0;
    trk_note_on(&inst, note, vel);
}
static uint32_t mp_p5(uint32_t pi, uint32_t mode, uint32_t batch)
{
    int16_t a4 = tuning_a4, budget = settings_budget;
    mp_start();
    memset(midi_verb, 0, sizeof midi_verb);
    host_preset(&inst, ENGI_PROPHET, pi);
    uint8_t *r = patch_of(0);                          /* (the part's patch: the Prophet's program) */
    if (mode) {
        r[P5_UNISON] = 1; r[P5_UNISON_COUNT] = (uint8_t)(pi % 6u); r[P5_UNISON_DETUNE] = (uint8_t)(pi % 8u);
        r[P5_RETRIGGER] = (uint8_t)(pi % 4u);
        p5_track_accept(&inst);
    } else
        inst.p[P_VOICE] = V_POLY;
    if (pi % 6u == 5u) { r[P5_LOW_B] = 1; r[P5_KEY_B] = (uint8_t)(pi % 12u < 6u); r[P5_POLY_B] = 100; r[P5_POLY_FILTER] = 1; }
    if (pi % 8u == 2u) r[P5_GLIDE] = (uint8_t)(40u + pi % 60u);
    if (pi % 9u == 4u) r[P5_VINTAGE] = (uint8_t)(pi % 2u ? 127u : 42u);
    if (pi % 10u == 7u) { r[P5_RESONANCE] = 120; r[P5_LEVEL_A] = r[P5_LEVEL_B] = r[P5_NOISE] = 0; }
    if (pi % 7u == 3u) { r[P5_SYNC] = 1; r[P5_SAW_A] = 1; r[P5_LEVEL_A] = 110; }
    r[P5_BEND] = (uint8_t)(pi % 12u);
    if (pi % 4u == 1u) {
        inst.p[P_M1SRC] = MS_ENV; inst.p[P_M1DST] = MD_CUT; inst.p[P_M1AMT] = 50;
        inst.p[P_M2SRC] = MS_ENV; inst.p[P_M2DST] = MD_PITCH; inst.p[P_M2AMT] = -20;
    }
    if (pi % 4u == 2u) { inst.p[P_LD_PIT] = 12; inst.p[P_LD_FLT] = 40; }
    if (pi % 4u == 3u) { inst.p[P_FXTYPE] = FX_MIDIVERB; inst.p[P_FXTYPE + 1] = (int16_t)(20u + pi % 80u); }   /* (the
                                                         * insert's release: the engine's envelopes take it) */
    if (pi % 5u == 4u) settings_budget = 8;              /* (two Prophet voices: the others budget victims) */
    for (uint32_t b = 0; b < MP_NB; b++) {
        if (b == 4) mp_p5_key(mode, 48, 100);
        if (b == 20) { inst.mw = (uint8_t)(40u + pi % 80u); inst.at = 60; }
        if (b == 30) inst.bend_raw = 3000;
        if (b == 40) mp_p5_key(mode, 55, 80);
        if (b == 60) trk_note_off(&inst, 48);
        if (b == 70) mp_p5_key(mode, 64, 127);
        if (b == 71) mp_p5_key(mode, 67, 30);
        if (b == 90) {                                   /* CUT RES TUNA TUNB PWA PWB MIXA MIXB */
            inst.p[P_E0] = (int16_t)((int32_t)(pi * 7u % 64u) - 32); inst.p[P_E1] = (int16_t)((int32_t)(pi % 41u) - 20);
            inst.p[P_E2] = (int16_t)((int32_t)(pi % 5u) - 2); inst.p[P_E3] = (int16_t)((int32_t)(pi % 7u) - 3);
            inst.p[P_E4] = (int16_t)((int32_t)(pi * 3u % 50u) - 25); inst.p[P_E5] = (int16_t)((int32_t)(pi * 11u % 50u) - 25);
            inst.p[P_E6] = (int16_t)((int32_t)(pi % 30u) - 15); inst.p[P_E7] = (int16_t)((int32_t)(pi * 13u % 60u) - 20);
        }
        if (b == 100) {                                  /* the program edited as the panel edits it */
            if (pi % 2u) r[P5_FILTER_REV] ^= 1u;
            r[P5_CUTOFF] = (uint8_t)(30u + pi * 37u % 98u); r[P5_RESONANCE] = (uint8_t)(pi * 53u % 128u);
            r[P5_KEY_FILTER] = (uint8_t)(pi % 3u); r[P5_ENV_FILTER] = (uint8_t)(pi * 29u % 128u);
            r[P5_ATTACK_FILTER] = (uint8_t)(pi % 40u); r[P5_DECAY_FILTER] = (uint8_t)(20u + pi * 3u % 100u);
            r[P5_SUSTAIN_FILTER] = (uint8_t)(pi * 17u % 128u); r[P5_RELEASE_FILTER] = (uint8_t)(10u + pi % 90u);
            r[P5_ATTACK_AMP] = (uint8_t)(pi % 32u); r[P5_DECAY_AMP] = (uint8_t)(30u + pi * 7u % 90u);
            r[P5_SUSTAIN_AMP] = (uint8_t)(40u + pi * 23u % 88u); r[P5_RELEASE_AMP] = (uint8_t)(20u + pi * 5u % 100u);
            r[P5_VEL_FILTER] = (uint8_t)(pi >> 1 & 1u); r[P5_VEL_AMP] = (uint8_t)(pi >> 2 & 1u);
            r[P5_POLY_ENV] = (uint8_t)(pi * 31u % 128u); r[P5_POLY_B] = (uint8_t)(pi * 41u % 128u);
            r[P5_POLY_FREQ] = (uint8_t)(pi & 1u); r[P5_POLY_PW] = (uint8_t)(pi >> 1 & 1u);
            r[P5_POLY_FILTER] = (uint8_t)(pi >> 2 & 1u);
            r[P5_LFO_RATE] = (uint8_t)(pi * 13u % 128u); r[P5_LFO_INITIAL] = (uint8_t)(pi * 7u % 64u);
            r[P5_LFO_SAW] = (uint8_t)(pi >> 3 & 1u); r[P5_LFO_TRI] = (uint8_t)(pi >> 4 & 1u);
            r[P5_LFO_PULSE] = (uint8_t)(pi >> 5 & 1u); r[P5_WHEEL_MIX] = (uint8_t)(pi * 19u % 128u);
            r[P5_WHEEL_FREQ_A] = (uint8_t)(pi % 3u == 0); r[P5_WHEEL_FREQ_B] = (uint8_t)(pi % 5u == 1u);
            r[P5_WHEEL_PW_A] = (uint8_t)(pi >> 1 & 1u); r[P5_WHEEL_PW_B] = (uint8_t)(pi >> 3 & 1u);
            r[P5_WHEEL_FILTER] = (uint8_t)(pi % 3u == 1u);
            r[P5_PRESS_LFO] = (uint8_t)(pi >> 4 & 1u); r[P5_PRESS_FILTER] = (uint8_t)(pi >> 5 & 1u);
            r[P5_PULSE_A] = (uint8_t)(pi >> 2 & 1u); r[P5_PULSE_B] = 1; r[P5_TRI_B] = (uint8_t)(pi >> 3 & 1u);
            r[P5_PW_A] = (uint8_t)(pi * 9u % 128u); r[P5_PW_B] = (uint8_t)(20u + pi * 3u % 100u);
            r[P5_LEVEL_B] = (uint8_t)(r[P5_LEVEL_B] / 2u + 30u); r[P5_NOISE] = (uint8_t)(pi % 4u == 0 ? 50u : 0u);
            if (pi % 6u == 2u) { r[P5_LOW_B] = 1; r[P5_KEY_B] = 0; }
            if (pi % 7u == 6u) r[P5_SYNC] ^= 1u;
            r[P5_RELEASE_ON] = (uint8_t)(pi % 11u != 10u);
        }
        if (b == 120) { trk_note_off(&inst, 55); trk_note_off(&inst, 64); trk_note_off(&inst, 67); inst.bend_raw = -2000; }
        if (b >= 130 && b < 134)
            for (uint32_t j = 0; j < 2u; j++) mp_p5_key(mode, 40u + 5u * ((b - 130u) * 2u + j), 50u + (b - 130u) * 18u + j);
        if (b == 140) mp_p5_key(mode, 45, 90);        /* (a key struck again: its voice reused) */
        if (b == 150 && pi % 3u == 0) tuning_a4 = 432;
        if (b >= 160 && b < 164)
            for (uint32_t j = 0; j < 2u; j++) trk_note_off(&inst, 40u + 5u * ((b - 160u) * 2u + j));
        if (b == 175) { inst.bend_raw = 0; inst.mw = 0; inst.at = 0; mp_p5_key(mode, 36, 90); }
        if (b == 182) mp_p5_key(mode, 43, 70);
        if (b == 190) { trk_note_off(&inst, 36); trk_note_off(&inst, 43); }
        mp_fm6_block(b, batch);
    }
    tuning_a4 = a4;
    settings_budget = budget;
    midi_verb[0] = 0;
    inst.mw = inst.at = 0;
    inst.bend_raw = 0;
    return mp_hash(mp_out, MP_NB * CTL);
}

/* phrase k (0 .. MP_COUNT - 1) rendered into mp_out: its hash; *what, *a, *b: what it plays */
#define MP_SID_PRESETS 32u
#define MP_DRUM_PRESETS 6u                               /* (1.x DRUM's: 808 KIT, 909 KIT, SID KIT, SID DIGI, CR78 KIT, FM KIT) */
#define MP_CZ_PRESETS 65u                                /* INIT TONE, Casio's 64 */
#define MP_SID_A (2u * MP_SID_PRESETS * 2u)              /* every preset x POLY / LEGATO, serial then on CPU1 */
#define MP_SID_B (2u * 4u * 2u * 2u)                     /* MODEL 0..3 x mode x ENV3, serial then on CPU1 */
#define MP_DRUM (2u * (MP_DRUM_PRESETS + 7u))            /* every preset, KIT 4..10, serial then on CPU1 */
#define MP_CZ (2u * MP_CZ_PRESETS * 2u)                  /* every tone x POLY / LEGATO, serial then on CPU1 */
#define MP_FM6_PRESETS 25u                               /* F1..F24, INIT VOICE */
#define MP_FM6 (2u * MP_FM6_PRESETS * 3u)                /* every preset x POLY / LEGATO / UNISON, serial then on CPU1 */
#define MP_FM6_SX (2u * 8u)                              /* the bank's 32 voices four at a time, serial then on CPU1 */
#define MP_P5_PRESETS 201u                               /* INIT, Sequential's 200 */
#define MP_P5 (2u * MP_P5_PRESETS * 2u)                  /* every program x POLY / UNISON, serial then on CPU1 */
#define MP_COUNT (MP_SID_A + MP_SID_B + MP_DRUM + MP_CZ + MP_FM6 + MP_FM6_SX + MP_P5)
#define MP_CZ0 (MP_SID_A + MP_SID_B + MP_DRUM)           /* (the first CZ-1 phrase) */
#define MP_FM60 (MP_CZ0 + MP_CZ)                         /* (the first FM6 phrase) */
#define MP_P50 (MP_FM60 + MP_FM6 + MP_FM6_SX)            /* (the first Prophet phrase) */
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
    if (k >= MP_DRUM + MP_CZ + MP_FM6 + MP_FM6_SX) {     /* the Prophet */
        k -= MP_DRUM + MP_CZ + MP_FM6 + MP_FM6_SX;
        uint32_t batch = k / (MP_P5 / 2u), pi = k % (MP_P5 / 2u) / 2u, mode = k % 2u;
        *what = batch ? "PROPHET program (CPU1)" : "PROPHET program"; *a = pi; *b = mode;
        return mp_p5(pi, mode, batch);
    }
    if (k >= MP_DRUM + MP_CZ + MP_FM6) {                 /* FM6: a DX7 bank by SysEx */
        k -= MP_DRUM + MP_CZ + MP_FM6;
        uint32_t batch = k / (MP_FM6_SX / 2u), j = k % (MP_FM6_SX / 2u);
        *what = batch ? "FM6 SysEx bank (CPU1)" : "FM6 SysEx bank"; *a = j; *b = 0;
        return mp_fm6_sx(j, batch);
    }
    if (k >= MP_DRUM + MP_CZ) {                          /* FM6 */
        k -= MP_DRUM + MP_CZ;
        uint32_t batch = k / (MP_FM6 / 2u), pi = k % (MP_FM6 / 2u) / 3u, mode = k % 3u;
        *what = batch ? "FM6 preset (CPU1)" : "FM6 preset"; *a = pi; *b = mode;
        return mp_fm6(pi, mode, batch);
    }
    if (k >= MP_DRUM) {                                  /* CZ-1 */
        k -= MP_DRUM;
        uint32_t batch = k / (MP_CZ / 2u), pi = k % (MP_CZ / 2u) / 2u, mono = k % 2u;
        *what = batch ? "CZ-1 tone (CPU1)" : "CZ-1 tone"; *a = pi; *b = mono;
        return mp_cz(pi, mono, batch);
    }
    uint32_t batch = k / (MP_DRUM / 2u), j = k % (MP_DRUM / 2u);
    if (j < MP_DRUM_PRESETS) {
        *what = batch ? "KIT preset (CPU1)" : "KIT preset"; *a = j; *b = 0;
        return mp_drum(j, -1, batch);
    }
    *what = batch ? "KIT (CPU1)" : "KIT"; *a = j - MP_DRUM_PRESETS + 4u; *b = 0;   /* 808 909 SID CR78 FM, */
    return mp_drum(0, (int32_t)(j - MP_DRUM_PRESETS + 4u), batch);                         /* user kits 1, 2 */
}

/* what the built-in engines rendered (SID, DRUM, CZ-1: 203a5c04, before they left the firmware; FM6: 0df43be6, the
 * Prophet: 47d57c42, before each left), phrase by phrase */
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
    0x24337dedu, /* 446 FM6 preset 0 0 */
    0x08901515u, /* 447 FM6 preset 0 1 */
    0xbc592c1fu, /* 448 FM6 preset 0 2 */
    0xd63b81acu, /* 449 FM6 preset 1 0 */
    0xca65e77eu, /* 450 FM6 preset 1 1 */
    0x100c8338u, /* 451 FM6 preset 1 2 */
    0x93a8285cu, /* 452 FM6 preset 2 0 */
    0x7e15a53du, /* 453 FM6 preset 2 1 */
    0x99362c94u, /* 454 FM6 preset 2 2 */
    0x95c58704u, /* 455 FM6 preset 3 0 */
    0xe1c0b250u, /* 456 FM6 preset 3 1 */
    0x5cf0638du, /* 457 FM6 preset 3 2 */
    0x14ec7f30u, /* 458 FM6 preset 4 0 */
    0xb043f702u, /* 459 FM6 preset 4 1 */
    0x19ebd1f3u, /* 460 FM6 preset 4 2 */
    0xee6e89a1u, /* 461 FM6 preset 5 0 */
    0x7bf6f065u, /* 462 FM6 preset 5 1 */
    0xd62d5db2u, /* 463 FM6 preset 5 2 */
    0xeb0d050fu, /* 464 FM6 preset 6 0 */
    0x9afd481fu, /* 465 FM6 preset 6 1 */
    0xd90efd84u, /* 466 FM6 preset 6 2 */
    0x14f32f32u, /* 467 FM6 preset 7 0 */
    0x7624efc1u, /* 468 FM6 preset 7 1 */
    0x8f74064au, /* 469 FM6 preset 7 2 */
    0xa0199f0cu, /* 470 FM6 preset 8 0 */
    0x65b29c81u, /* 471 FM6 preset 8 1 */
    0x2ed3d404u, /* 472 FM6 preset 8 2 */
    0x29bc7a14u, /* 473 FM6 preset 9 0 */
    0xf3339f08u, /* 474 FM6 preset 9 1 */
    0x1e725cc8u, /* 475 FM6 preset 9 2 */
    0xb36ed443u, /* 476 FM6 preset 10 0 */
    0x7be99bfeu, /* 477 FM6 preset 10 1 */
    0xd44f1af6u, /* 478 FM6 preset 10 2 */
    0xd50c4b62u, /* 479 FM6 preset 11 0 */
    0x4b98cdc3u, /* 480 FM6 preset 11 1 */
    0x6d88be7au, /* 481 FM6 preset 11 2 */
    0xbea92acbu, /* 482 FM6 preset 12 0 */
    0x97855239u, /* 483 FM6 preset 12 1 */
    0xb1fb7d02u, /* 484 FM6 preset 12 2 */
    0x52fea2f4u, /* 485 FM6 preset 13 0 */
    0xdb150ddfu, /* 486 FM6 preset 13 1 */
    0xbe5da7f6u, /* 487 FM6 preset 13 2 */
    0x47dddc7cu, /* 488 FM6 preset 14 0 */
    0xaefc289au, /* 489 FM6 preset 14 1 */
    0x0ae8c2c0u, /* 490 FM6 preset 14 2 */
    0x8d0bac34u, /* 491 FM6 preset 15 0 */
    0xbd16e385u, /* 492 FM6 preset 15 1 */
    0x4e94066eu, /* 493 FM6 preset 15 2 */
    0x89c95bbdu, /* 494 FM6 preset 16 0 */
    0xd0e185ebu, /* 495 FM6 preset 16 1 */
    0x71d19ad8u, /* 496 FM6 preset 16 2 */
    0xe2d7bbaeu, /* 497 FM6 preset 17 0 */
    0xe9c468e6u, /* 498 FM6 preset 17 1 */
    0x97ebe25du, /* 499 FM6 preset 17 2 */
    0x7aa2ff01u, /* 500 FM6 preset 18 0 */
    0x49fdd278u, /* 501 FM6 preset 18 1 */
    0xc7f2144cu, /* 502 FM6 preset 18 2 */
    0xadb67c4eu, /* 503 FM6 preset 19 0 */
    0x4c0ab74du, /* 504 FM6 preset 19 1 */
    0x768e1936u, /* 505 FM6 preset 19 2 */
    0xfca2344cu, /* 506 FM6 preset 20 0 */
    0xc6d55051u, /* 507 FM6 preset 20 1 */
    0x5d526bd7u, /* 508 FM6 preset 20 2 */
    0xb240b931u, /* 509 FM6 preset 21 0 */
    0x5082b849u, /* 510 FM6 preset 21 1 */
    0x53c508b7u, /* 511 FM6 preset 21 2 */
    0xeaa829a8u, /* 512 FM6 preset 22 0 */
    0xe1c10d67u, /* 513 FM6 preset 22 1 */
    0x001bc2b5u, /* 514 FM6 preset 22 2 */
    0xef96b1e5u, /* 515 FM6 preset 23 0 */
    0xba1ba672u, /* 516 FM6 preset 23 1 */
    0x0b017a4du, /* 517 FM6 preset 23 2 */
    0x16363bffu, /* 518 FM6 preset 24 0 */
    0xabfba4bbu, /* 519 FM6 preset 24 1 */
    0xa89da008u, /* 520 FM6 preset 24 2 */
    0x24337dedu, /* 521 FM6 preset (CPU1) 0 0 */
    0x08901515u, /* 522 FM6 preset (CPU1) 0 1 */
    0xbc592c1fu, /* 523 FM6 preset (CPU1) 0 2 */
    0xd63b81acu, /* 524 FM6 preset (CPU1) 1 0 */
    0xca65e77eu, /* 525 FM6 preset (CPU1) 1 1 */
    0x100c8338u, /* 526 FM6 preset (CPU1) 1 2 */
    0x93a8285cu, /* 527 FM6 preset (CPU1) 2 0 */
    0x7e15a53du, /* 528 FM6 preset (CPU1) 2 1 */
    0x99362c94u, /* 529 FM6 preset (CPU1) 2 2 */
    0x95c58704u, /* 530 FM6 preset (CPU1) 3 0 */
    0xe1c0b250u, /* 531 FM6 preset (CPU1) 3 1 */
    0x5cf0638du, /* 532 FM6 preset (CPU1) 3 2 */
    0x14ec7f30u, /* 533 FM6 preset (CPU1) 4 0 */
    0xb043f702u, /* 534 FM6 preset (CPU1) 4 1 */
    0x19ebd1f3u, /* 535 FM6 preset (CPU1) 4 2 */
    0xee6e89a1u, /* 536 FM6 preset (CPU1) 5 0 */
    0x7bf6f065u, /* 537 FM6 preset (CPU1) 5 1 */
    0xd62d5db2u, /* 538 FM6 preset (CPU1) 5 2 */
    0xeb0d050fu, /* 539 FM6 preset (CPU1) 6 0 */
    0x9afd481fu, /* 540 FM6 preset (CPU1) 6 1 */
    0xd90efd84u, /* 541 FM6 preset (CPU1) 6 2 */
    0x14f32f32u, /* 542 FM6 preset (CPU1) 7 0 */
    0x7624efc1u, /* 543 FM6 preset (CPU1) 7 1 */
    0x8f74064au, /* 544 FM6 preset (CPU1) 7 2 */
    0xa0199f0cu, /* 545 FM6 preset (CPU1) 8 0 */
    0x65b29c81u, /* 546 FM6 preset (CPU1) 8 1 */
    0x2ed3d404u, /* 547 FM6 preset (CPU1) 8 2 */
    0x29bc7a14u, /* 548 FM6 preset (CPU1) 9 0 */
    0xf3339f08u, /* 549 FM6 preset (CPU1) 9 1 */
    0x1e725cc8u, /* 550 FM6 preset (CPU1) 9 2 */
    0xb36ed443u, /* 551 FM6 preset (CPU1) 10 0 */
    0x7be99bfeu, /* 552 FM6 preset (CPU1) 10 1 */
    0xd44f1af6u, /* 553 FM6 preset (CPU1) 10 2 */
    0xd50c4b62u, /* 554 FM6 preset (CPU1) 11 0 */
    0x4b98cdc3u, /* 555 FM6 preset (CPU1) 11 1 */
    0x6d88be7au, /* 556 FM6 preset (CPU1) 11 2 */
    0xbea92acbu, /* 557 FM6 preset (CPU1) 12 0 */
    0x97855239u, /* 558 FM6 preset (CPU1) 12 1 */
    0xb1fb7d02u, /* 559 FM6 preset (CPU1) 12 2 */
    0x52fea2f4u, /* 560 FM6 preset (CPU1) 13 0 */
    0xdb150ddfu, /* 561 FM6 preset (CPU1) 13 1 */
    0xbe5da7f6u, /* 562 FM6 preset (CPU1) 13 2 */
    0x47dddc7cu, /* 563 FM6 preset (CPU1) 14 0 */
    0xaefc289au, /* 564 FM6 preset (CPU1) 14 1 */
    0x0ae8c2c0u, /* 565 FM6 preset (CPU1) 14 2 */
    0x8d0bac34u, /* 566 FM6 preset (CPU1) 15 0 */
    0xbd16e385u, /* 567 FM6 preset (CPU1) 15 1 */
    0x4e94066eu, /* 568 FM6 preset (CPU1) 15 2 */
    0x89c95bbdu, /* 569 FM6 preset (CPU1) 16 0 */
    0xd0e185ebu, /* 570 FM6 preset (CPU1) 16 1 */
    0x71d19ad8u, /* 571 FM6 preset (CPU1) 16 2 */
    0xe2d7bbaeu, /* 572 FM6 preset (CPU1) 17 0 */
    0xe9c468e6u, /* 573 FM6 preset (CPU1) 17 1 */
    0x97ebe25du, /* 574 FM6 preset (CPU1) 17 2 */
    0x7aa2ff01u, /* 575 FM6 preset (CPU1) 18 0 */
    0x49fdd278u, /* 576 FM6 preset (CPU1) 18 1 */
    0xc7f2144cu, /* 577 FM6 preset (CPU1) 18 2 */
    0xadb67c4eu, /* 578 FM6 preset (CPU1) 19 0 */
    0x4c0ab74du, /* 579 FM6 preset (CPU1) 19 1 */
    0x768e1936u, /* 580 FM6 preset (CPU1) 19 2 */
    0xfca2344cu, /* 581 FM6 preset (CPU1) 20 0 */
    0xc6d55051u, /* 582 FM6 preset (CPU1) 20 1 */
    0x5d526bd7u, /* 583 FM6 preset (CPU1) 20 2 */
    0xb240b931u, /* 584 FM6 preset (CPU1) 21 0 */
    0x5082b849u, /* 585 FM6 preset (CPU1) 21 1 */
    0x53c508b7u, /* 586 FM6 preset (CPU1) 21 2 */
    0xeaa829a8u, /* 587 FM6 preset (CPU1) 22 0 */
    0xe1c10d67u, /* 588 FM6 preset (CPU1) 22 1 */
    0x001bc2b5u, /* 589 FM6 preset (CPU1) 22 2 */
    0xef96b1e5u, /* 590 FM6 preset (CPU1) 23 0 */
    0xba1ba672u, /* 591 FM6 preset (CPU1) 23 1 */
    0x0b017a4du, /* 592 FM6 preset (CPU1) 23 2 */
    0x16363bffu, /* 593 FM6 preset (CPU1) 24 0 */
    0xabfba4bbu, /* 594 FM6 preset (CPU1) 24 1 */
    0xa89da008u, /* 595 FM6 preset (CPU1) 24 2 */
    0x5c529b3fu, /* 596 FM6 SysEx bank 0 0 */
    0xd554ad45u, /* 597 FM6 SysEx bank 1 0 */
    0x0d20f51du, /* 598 FM6 SysEx bank 2 0 */
    0x73e40eb8u, /* 599 FM6 SysEx bank 3 0 */
    0x31aeb099u, /* 600 FM6 SysEx bank 4 0 */
    0x33d60df6u, /* 601 FM6 SysEx bank 5 0 */
    0x6ab552c4u, /* 602 FM6 SysEx bank 6 0 */
    0x6233c272u, /* 603 FM6 SysEx bank 7 0 */
    0x5c529b3fu, /* 604 FM6 SysEx bank (CPU1) 0 0 */
    0xd554ad45u, /* 605 FM6 SysEx bank (CPU1) 1 0 */
    0x0d20f51du, /* 606 FM6 SysEx bank (CPU1) 2 0 */
    0x73e40eb8u, /* 607 FM6 SysEx bank (CPU1) 3 0 */
    0x31aeb099u, /* 608 FM6 SysEx bank (CPU1) 4 0 */
    0x33d60df6u, /* 609 FM6 SysEx bank (CPU1) 5 0 */
    0x6ab552c4u, /* 610 FM6 SysEx bank (CPU1) 6 0 */
    0x6233c272u, /* 611 FM6 SysEx bank (CPU1) 7 0 */
    0xbbdeae25u, /* 612 PROPHET program 0 0 */
    0xb4265430u, /* 613 PROPHET program 0 1 */
    0x4b045e95u, /* 614 PROPHET program 1 0 */
    0x01231144u, /* 615 PROPHET program 1 1 */
    0x3bbbf278u, /* 616 PROPHET program 2 0 */
    0x87feea04u, /* 617 PROPHET program 2 1 */
    0x65a201cbu, /* 618 PROPHET program 3 0 */
    0x7d89ed68u, /* 619 PROPHET program 3 1 */
    0xf3641b0du, /* 620 PROPHET program 4 0 */
    0x55008411u, /* 621 PROPHET program 4 1 */
    0x41b36feau, /* 622 PROPHET program 5 0 */
    0xe1c5dc08u, /* 623 PROPHET program 5 1 */
    0x9eaf2b9eu, /* 624 PROPHET program 6 0 */
    0xc5887d6du, /* 625 PROPHET program 6 1 */
    0x68a5f46fu, /* 626 PROPHET program 7 0 */
    0x895aab48u, /* 627 PROPHET program 7 1 */
    0x89d48a63u, /* 628 PROPHET program 8 0 */
    0x326ecd81u, /* 629 PROPHET program 8 1 */
    0x3eb9ad3bu, /* 630 PROPHET program 9 0 */
    0xf312f3f2u, /* 631 PROPHET program 9 1 */
    0xdec59be2u, /* 632 PROPHET program 10 0 */
    0x3ce092e0u, /* 633 PROPHET program 10 1 */
    0x171d6159u, /* 634 PROPHET program 11 0 */
    0x945b2cd6u, /* 635 PROPHET program 11 1 */
    0x64b2b04eu, /* 636 PROPHET program 12 0 */
    0x1245c44au, /* 637 PROPHET program 12 1 */
    0x87e8baeeu, /* 638 PROPHET program 13 0 */
    0xac20d13eu, /* 639 PROPHET program 13 1 */
    0x567bb11fu, /* 640 PROPHET program 14 0 */
    0x50625dddu, /* 641 PROPHET program 14 1 */
    0xb0a24991u, /* 642 PROPHET program 15 0 */
    0x80931fe6u, /* 643 PROPHET program 15 1 */
    0x1ea058acu, /* 644 PROPHET program 16 0 */
    0xeda170b5u, /* 645 PROPHET program 16 1 */
    0x11240d97u, /* 646 PROPHET program 17 0 */
    0x5f756478u, /* 647 PROPHET program 17 1 */
    0x7a43daa3u, /* 648 PROPHET program 18 0 */
    0xd74d3689u, /* 649 PROPHET program 18 1 */
    0x0b37acbeu, /* 650 PROPHET program 19 0 */
    0x750fc061u, /* 651 PROPHET program 19 1 */
    0x75302e1au, /* 652 PROPHET program 20 0 */
    0x242c68c0u, /* 653 PROPHET program 20 1 */
    0x667ccaf5u, /* 654 PROPHET program 21 0 */
    0xa68725a4u, /* 655 PROPHET program 21 1 */
    0x33145ecbu, /* 656 PROPHET program 22 0 */
    0x40e325f9u, /* 657 PROPHET program 22 1 */
    0x75ebffbbu, /* 658 PROPHET program 23 0 */
    0x3a9f9f55u, /* 659 PROPHET program 23 1 */
    0x8d5414a4u, /* 660 PROPHET program 24 0 */
    0xe276d5cau, /* 661 PROPHET program 24 1 */
    0xaea15a32u, /* 662 PROPHET program 25 0 */
    0x85999925u, /* 663 PROPHET program 25 1 */
    0x11be5c01u, /* 664 PROPHET program 26 0 */
    0x5cad5138u, /* 665 PROPHET program 26 1 */
    0xfe32abceu, /* 666 PROPHET program 27 0 */
    0xb7be46d3u, /* 667 PROPHET program 27 1 */
    0x3c2bfd7eu, /* 668 PROPHET program 28 0 */
    0xfa634acbu, /* 669 PROPHET program 28 1 */
    0xa0da2fcau, /* 670 PROPHET program 29 0 */
    0x53f5a6feu, /* 671 PROPHET program 29 1 */
    0xbed52711u, /* 672 PROPHET program 30 0 */
    0x7a6161cfu, /* 673 PROPHET program 30 1 */
    0x06be0816u, /* 674 PROPHET program 31 0 */
    0xe817c100u, /* 675 PROPHET program 31 1 */
    0x1d09f68bu, /* 676 PROPHET program 32 0 */
    0x613a2b7du, /* 677 PROPHET program 32 1 */
    0x8186ed71u, /* 678 PROPHET program 33 0 */
    0xe8642084u, /* 679 PROPHET program 33 1 */
    0x3759f803u, /* 680 PROPHET program 34 0 */
    0x1310f719u, /* 681 PROPHET program 34 1 */
    0x29bb5d7du, /* 682 PROPHET program 35 0 */
    0x40f9f404u, /* 683 PROPHET program 35 1 */
    0xfbf203a4u, /* 684 PROPHET program 36 0 */
    0xce9179efu, /* 685 PROPHET program 36 1 */
    0xbeec72fcu, /* 686 PROPHET program 37 0 */
    0xd79c6f60u, /* 687 PROPHET program 37 1 */
    0x1bf21384u, /* 688 PROPHET program 38 0 */
    0xd177c132u, /* 689 PROPHET program 38 1 */
    0xdd343e4fu, /* 690 PROPHET program 39 0 */
    0x8dca574bu, /* 691 PROPHET program 39 1 */
    0x8f26f21bu, /* 692 PROPHET program 40 0 */
    0x261f079eu, /* 693 PROPHET program 40 1 */
    0x07f9ed4du, /* 694 PROPHET program 41 0 */
    0x99979950u, /* 695 PROPHET program 41 1 */
    0x270d2ba0u, /* 696 PROPHET program 42 0 */
    0x3817b8b0u, /* 697 PROPHET program 42 1 */
    0xd1209ad1u, /* 698 PROPHET program 43 0 */
    0xedad8179u, /* 699 PROPHET program 43 1 */
    0xc12549f0u, /* 700 PROPHET program 44 0 */
    0xd5c7318fu, /* 701 PROPHET program 44 1 */
    0xa87d6d38u, /* 702 PROPHET program 45 0 */
    0x90d2128du, /* 703 PROPHET program 45 1 */
    0x9f5f5c09u, /* 704 PROPHET program 46 0 */
    0xc43bebc0u, /* 705 PROPHET program 46 1 */
    0x8042a8f5u, /* 706 PROPHET program 47 0 */
    0xc3de4a34u, /* 707 PROPHET program 47 1 */
    0x27d3dc82u, /* 708 PROPHET program 48 0 */
    0xa60b0d32u, /* 709 PROPHET program 48 1 */
    0x1827b753u, /* 710 PROPHET program 49 0 */
    0xfe87143du, /* 711 PROPHET program 49 1 */
    0x87d74711u, /* 712 PROPHET program 50 0 */
    0xe6d5c23au, /* 713 PROPHET program 50 1 */
    0x33905d1eu, /* 714 PROPHET program 51 0 */
    0xf1336ed3u, /* 715 PROPHET program 51 1 */
    0x7e001ebdu, /* 716 PROPHET program 52 0 */
    0x8e73b6e6u, /* 717 PROPHET program 52 1 */
    0xbfe12339u, /* 718 PROPHET program 53 0 */
    0xb8e7f4b7u, /* 719 PROPHET program 53 1 */
    0x8839ed28u, /* 720 PROPHET program 54 0 */
    0xc92de3cfu, /* 721 PROPHET program 54 1 */
    0x253b113fu, /* 722 PROPHET program 55 0 */
    0x939acb3fu, /* 723 PROPHET program 55 1 */
    0x8db9805cu, /* 724 PROPHET program 56 0 */
    0x2ef350f4u, /* 725 PROPHET program 56 1 */
    0x7a9feba9u, /* 726 PROPHET program 57 0 */
    0xf67fa50bu, /* 727 PROPHET program 57 1 */
    0x429413eau, /* 728 PROPHET program 58 0 */
    0x3bf0fed8u, /* 729 PROPHET program 58 1 */
    0xe68f71dfu, /* 730 PROPHET program 59 0 */
    0xbd2582c6u, /* 731 PROPHET program 59 1 */
    0xdf1b1f0eu, /* 732 PROPHET program 60 0 */
    0xfb692886u, /* 733 PROPHET program 60 1 */
    0x70019886u, /* 734 PROPHET program 61 0 */
    0xfd464880u, /* 735 PROPHET program 61 1 */
    0x9e79442fu, /* 736 PROPHET program 62 0 */
    0x149395b4u, /* 737 PROPHET program 62 1 */
    0xab1edb10u, /* 738 PROPHET program 63 0 */
    0x30827e7cu, /* 739 PROPHET program 63 1 */
    0x6d3e3992u, /* 740 PROPHET program 64 0 */
    0xcc1699c0u, /* 741 PROPHET program 64 1 */
    0xaac586eeu, /* 742 PROPHET program 65 0 */
    0x9584b7ccu, /* 743 PROPHET program 65 1 */
    0xb02b2a1du, /* 744 PROPHET program 66 0 */
    0xf4ac7eafu, /* 745 PROPHET program 66 1 */
    0x2c87b2eeu, /* 746 PROPHET program 67 0 */
    0x417f67cdu, /* 747 PROPHET program 67 1 */
    0x94101899u, /* 748 PROPHET program 68 0 */
    0x4458139du, /* 749 PROPHET program 68 1 */
    0xfe33abfdu, /* 750 PROPHET program 69 0 */
    0x6ad500aau, /* 751 PROPHET program 69 1 */
    0x748c4fbau, /* 752 PROPHET program 70 0 */
    0xcd359334u, /* 753 PROPHET program 70 1 */
    0xa82614a3u, /* 754 PROPHET program 71 0 */
    0x04d429beu, /* 755 PROPHET program 71 1 */
    0x83b0bd04u, /* 756 PROPHET program 72 0 */
    0x274d0985u, /* 757 PROPHET program 72 1 */
    0xa492c620u, /* 758 PROPHET program 73 0 */
    0x7c0a1b48u, /* 759 PROPHET program 73 1 */
    0xbfe56a72u, /* 760 PROPHET program 74 0 */
    0x4e8511fau, /* 761 PROPHET program 74 1 */
    0xc905acd3u, /* 762 PROPHET program 75 0 */
    0x29f7caefu, /* 763 PROPHET program 75 1 */
    0x31b97211u, /* 764 PROPHET program 76 0 */
    0xfd748b75u, /* 765 PROPHET program 76 1 */
    0x028474c6u, /* 766 PROPHET program 77 0 */
    0x1e5c0c13u, /* 767 PROPHET program 77 1 */
    0xa0d31d76u, /* 768 PROPHET program 78 0 */
    0x5cbd187cu, /* 769 PROPHET program 78 1 */
    0x69c74bd5u, /* 770 PROPHET program 79 0 */
    0xafd57b9eu, /* 771 PROPHET program 79 1 */
    0xf94cd605u, /* 772 PROPHET program 80 0 */
    0x9c08096eu, /* 773 PROPHET program 80 1 */
    0x4e631befu, /* 774 PROPHET program 81 0 */
    0x721e4bcbu, /* 775 PROPHET program 81 1 */
    0x6356fe6au, /* 776 PROPHET program 82 0 */
    0x2f788ba4u, /* 777 PROPHET program 82 1 */
    0xcf402898u, /* 778 PROPHET program 83 0 */
    0xe4956088u, /* 779 PROPHET program 83 1 */
    0x5e4653d5u, /* 780 PROPHET program 84 0 */
    0x2642813du, /* 781 PROPHET program 84 1 */
    0x6897d604u, /* 782 PROPHET program 85 0 */
    0xb5626ec0u, /* 783 PROPHET program 85 1 */
    0x376a5e8bu, /* 784 PROPHET program 86 0 */
    0xcc5dd808u, /* 785 PROPHET program 86 1 */
    0x5fb9451au, /* 786 PROPHET program 87 0 */
    0x6ac5212du, /* 787 PROPHET program 87 1 */
    0xd35c5b2eu, /* 788 PROPHET program 88 0 */
    0x7406540du, /* 789 PROPHET program 88 1 */
    0xbc2b07e8u, /* 790 PROPHET program 89 0 */
    0xa71c101cu, /* 791 PROPHET program 89 1 */
    0x3530e26eu, /* 792 PROPHET program 90 0 */
    0xfea11db1u, /* 793 PROPHET program 90 1 */
    0x823c275cu, /* 794 PROPHET program 91 0 */
    0x94cd0d82u, /* 795 PROPHET program 91 1 */
    0x89edce56u, /* 796 PROPHET program 92 0 */
    0x659cc0f6u, /* 797 PROPHET program 92 1 */
    0x5e7bb7e2u, /* 798 PROPHET program 93 0 */
    0x68ea1ad7u, /* 799 PROPHET program 93 1 */
    0x4c610eccu, /* 800 PROPHET program 94 0 */
    0x3d677ad1u, /* 801 PROPHET program 94 1 */
    0x7c9f715au, /* 802 PROPHET program 95 0 */
    0x28387ba7u, /* 803 PROPHET program 95 1 */
    0x73d4b0d9u, /* 804 PROPHET program 96 0 */
    0x570e5251u, /* 805 PROPHET program 96 1 */
    0x5022dc63u, /* 806 PROPHET program 97 0 */
    0x150f7063u, /* 807 PROPHET program 97 1 */
    0x54048517u, /* 808 PROPHET program 98 0 */
    0x2e34e67bu, /* 809 PROPHET program 98 1 */
    0x23e7975cu, /* 810 PROPHET program 99 0 */
    0xa4d1fa64u, /* 811 PROPHET program 99 1 */
    0x054da554u, /* 812 PROPHET program 100 0 */
    0xe7946d03u, /* 813 PROPHET program 100 1 */
    0xbe365799u, /* 814 PROPHET program 101 0 */
    0xb5a715d2u, /* 815 PROPHET program 101 1 */
    0x780ff25bu, /* 816 PROPHET program 102 0 */
    0xee37ec0eu, /* 817 PROPHET program 102 1 */
    0x7683c0f7u, /* 818 PROPHET program 103 0 */
    0xd6cd321au, /* 819 PROPHET program 103 1 */
    0x2c0d5365u, /* 820 PROPHET program 104 0 */
    0x4ec1fe2au, /* 821 PROPHET program 104 1 */
    0x54760eaeu, /* 822 PROPHET program 105 0 */
    0x2b7a5d76u, /* 823 PROPHET program 105 1 */
    0x6922348du, /* 824 PROPHET program 106 0 */
    0x034ae343u, /* 825 PROPHET program 106 1 */
    0x56de4ba9u, /* 826 PROPHET program 107 0 */
    0x2e4656feu, /* 827 PROPHET program 107 1 */
    0x0d638165u, /* 828 PROPHET program 108 0 */
    0xe7240440u, /* 829 PROPHET program 108 1 */
    0xa566c99du, /* 830 PROPHET program 109 0 */
    0xa97c72ebu, /* 831 PROPHET program 109 1 */
    0x0bd8a335u, /* 832 PROPHET program 110 0 */
    0x8ad25b87u, /* 833 PROPHET program 110 1 */
    0xf2a8eaeeu, /* 834 PROPHET program 111 0 */
    0x78344c73u, /* 835 PROPHET program 111 1 */
    0x159395a0u, /* 836 PROPHET program 112 0 */
    0xbbe66b95u, /* 837 PROPHET program 112 1 */
    0x5d3cbb62u, /* 838 PROPHET program 113 0 */
    0xee20c570u, /* 839 PROPHET program 113 1 */
    0x5b2aabe7u, /* 840 PROPHET program 114 0 */
    0x0a04c59au, /* 841 PROPHET program 114 1 */
    0xd6029be0u, /* 842 PROPHET program 115 0 */
    0xf5aeec07u, /* 843 PROPHET program 115 1 */
    0xcbea057au, /* 844 PROPHET program 116 0 */
    0x29274089u, /* 845 PROPHET program 116 1 */
    0x2515e737u, /* 846 PROPHET program 117 0 */
    0x02bbf214u, /* 847 PROPHET program 117 1 */
    0x82b50dacu, /* 848 PROPHET program 118 0 */
    0xace46b14u, /* 849 PROPHET program 118 1 */
    0xc2a35ba6u, /* 850 PROPHET program 119 0 */
    0xe8a253a8u, /* 851 PROPHET program 119 1 */
    0xd86e4090u, /* 852 PROPHET program 120 0 */
    0xcd354a73u, /* 853 PROPHET program 120 1 */
    0xc120b5edu, /* 854 PROPHET program 121 0 */
    0xa6274069u, /* 855 PROPHET program 121 1 */
    0x8efbe391u, /* 856 PROPHET program 122 0 */
    0x7df82f1du, /* 857 PROPHET program 122 1 */
    0xd781e295u, /* 858 PROPHET program 123 0 */
    0x746745f0u, /* 859 PROPHET program 123 1 */
    0x2541f10fu, /* 860 PROPHET program 124 0 */
    0xf43976b0u, /* 861 PROPHET program 124 1 */
    0x19fc2762u, /* 862 PROPHET program 125 0 */
    0x7dd5c7c3u, /* 863 PROPHET program 125 1 */
    0x868e6691u, /* 864 PROPHET program 126 0 */
    0x949fc106u, /* 865 PROPHET program 126 1 */
    0xae279b44u, /* 866 PROPHET program 127 0 */
    0xffd13a1eu, /* 867 PROPHET program 127 1 */
    0x0918dc2bu, /* 868 PROPHET program 128 0 */
    0xa66faed3u, /* 869 PROPHET program 128 1 */
    0xb7d733cbu, /* 870 PROPHET program 129 0 */
    0x961b7dc6u, /* 871 PROPHET program 129 1 */
    0x0c5bb0c4u, /* 872 PROPHET program 130 0 */
    0x0e14dfc4u, /* 873 PROPHET program 130 1 */
    0xa6128269u, /* 874 PROPHET program 131 0 */
    0x97f515deu, /* 875 PROPHET program 131 1 */
    0xd03d8b03u, /* 876 PROPHET program 132 0 */
    0xe8e5f747u, /* 877 PROPHET program 132 1 */
    0x3fccebcfu, /* 878 PROPHET program 133 0 */
    0x2021bc20u, /* 879 PROPHET program 133 1 */
    0xa965deabu, /* 880 PROPHET program 134 0 */
    0x1b618638u, /* 881 PROPHET program 134 1 */
    0x3b339be3u, /* 882 PROPHET program 135 0 */
    0xeebfeb0fu, /* 883 PROPHET program 135 1 */
    0xbeaa5f8cu, /* 884 PROPHET program 136 0 */
    0x6b8a095eu, /* 885 PROPHET program 136 1 */
    0xc8a52fadu, /* 886 PROPHET program 137 0 */
    0xb114ab42u, /* 887 PROPHET program 137 1 */
    0x46730df2u, /* 888 PROPHET program 138 0 */
    0x01ed79a5u, /* 889 PROPHET program 138 1 */
    0xb9ea4874u, /* 890 PROPHET program 139 0 */
    0xce0defdbu, /* 891 PROPHET program 139 1 */
    0x4b6ddaf6u, /* 892 PROPHET program 140 0 */
    0xdf9b031au, /* 893 PROPHET program 140 1 */
    0x3dd6bfafu, /* 894 PROPHET program 141 0 */
    0xab5de7b1u, /* 895 PROPHET program 141 1 */
    0x9f98b6f6u, /* 896 PROPHET program 142 0 */
    0xba90cbe1u, /* 897 PROPHET program 142 1 */
    0x0a290f3cu, /* 898 PROPHET program 143 0 */
    0x0c5071f5u, /* 899 PROPHET program 143 1 */
    0xca412986u, /* 900 PROPHET program 144 0 */
    0x97163bf7u, /* 901 PROPHET program 144 1 */
    0xea21c026u, /* 902 PROPHET program 145 0 */
    0xa7e858c4u, /* 903 PROPHET program 145 1 */
    0x0763f8d1u, /* 904 PROPHET program 146 0 */
    0x51294ddau, /* 905 PROPHET program 146 1 */
    0xabc1753au, /* 906 PROPHET program 147 0 */
    0xa20a2447u, /* 907 PROPHET program 147 1 */
    0x4a485f7fu, /* 908 PROPHET program 148 0 */
    0xecfdcc65u, /* 909 PROPHET program 148 1 */
    0x34e98aceu, /* 910 PROPHET program 149 0 */
    0x451f90efu, /* 911 PROPHET program 149 1 */
    0x1c2e7ff1u, /* 912 PROPHET program 150 0 */
    0xc4a3a1afu, /* 913 PROPHET program 150 1 */
    0x492a5aa1u, /* 914 PROPHET program 151 0 */
    0xefdde0f5u, /* 915 PROPHET program 151 1 */
    0xff943f95u, /* 916 PROPHET program 152 0 */
    0x6ead7ec3u, /* 917 PROPHET program 152 1 */
    0xe8ef3e23u, /* 918 PROPHET program 153 0 */
    0xcd26074bu, /* 919 PROPHET program 153 1 */
    0xd1d6e64fu, /* 920 PROPHET program 154 0 */
    0xae3a42d3u, /* 921 PROPHET program 154 1 */
    0xcd720f4au, /* 922 PROPHET program 155 0 */
    0xa9969d76u, /* 923 PROPHET program 155 1 */
    0xfa623681u, /* 924 PROPHET program 156 0 */
    0x9f8e4e2au, /* 925 PROPHET program 156 1 */
    0x20bb98a4u, /* 926 PROPHET program 157 0 */
    0xe2fd19e0u, /* 927 PROPHET program 157 1 */
    0x4e16e622u, /* 928 PROPHET program 158 0 */
    0x7ba775d0u, /* 929 PROPHET program 158 1 */
    0x283cfa5cu, /* 930 PROPHET program 159 0 */
    0xc2c59cd5u, /* 931 PROPHET program 159 1 */
    0xa5d8d898u, /* 932 PROPHET program 160 0 */
    0x9accfc3fu, /* 933 PROPHET program 160 1 */
    0xe3de4bd9u, /* 934 PROPHET program 161 0 */
    0xa6c3215fu, /* 935 PROPHET program 161 1 */
    0x77f5454cu, /* 936 PROPHET program 162 0 */
    0x1b41233cu, /* 937 PROPHET program 162 1 */
    0x9d2642f0u, /* 938 PROPHET program 163 0 */
    0x5f882cc8u, /* 939 PROPHET program 163 1 */
    0x6aa617f5u, /* 940 PROPHET program 164 0 */
    0x623c2fe4u, /* 941 PROPHET program 164 1 */
    0x7d27575du, /* 942 PROPHET program 165 0 */
    0xaeab0280u, /* 943 PROPHET program 165 1 */
    0x657a5ae0u, /* 944 PROPHET program 166 0 */
    0x55c38fadu, /* 945 PROPHET program 166 1 */
    0x5e5f8636u, /* 946 PROPHET program 167 0 */
    0x8808ca82u, /* 947 PROPHET program 167 1 */
    0x807f4beeu, /* 948 PROPHET program 168 0 */
    0xa009e5ebu, /* 949 PROPHET program 168 1 */
    0xf0cd083eu, /* 950 PROPHET program 169 0 */
    0xc7c8ea95u, /* 951 PROPHET program 169 1 */
    0x3027ca50u, /* 952 PROPHET program 170 0 */
    0x8f8e7506u, /* 953 PROPHET program 170 1 */
    0x2e8bb126u, /* 954 PROPHET program 171 0 */
    0x545b0f1cu, /* 955 PROPHET program 171 1 */
    0x4ed16a01u, /* 956 PROPHET program 172 0 */
    0xb3de06b1u, /* 957 PROPHET program 172 1 */
    0xb44d909au, /* 958 PROPHET program 173 0 */
    0x3d12c6a6u, /* 959 PROPHET program 173 1 */
    0xa52ecab9u, /* 960 PROPHET program 174 0 */
    0xf67087afu, /* 961 PROPHET program 174 1 */
    0xeff4d51eu, /* 962 PROPHET program 175 0 */
    0xa1723042u, /* 963 PROPHET program 175 1 */
    0xb079f57du, /* 964 PROPHET program 176 0 */
    0x422fcdedu, /* 965 PROPHET program 176 1 */
    0xc92a93d6u, /* 966 PROPHET program 177 0 */
    0x06624cffu, /* 967 PROPHET program 177 1 */
    0xe58cac68u, /* 968 PROPHET program 178 0 */
    0x04ecfb1au, /* 969 PROPHET program 178 1 */
    0x8535033eu, /* 970 PROPHET program 179 0 */
    0x14a51d2fu, /* 971 PROPHET program 179 1 */
    0xef2865c2u, /* 972 PROPHET program 180 0 */
    0x6bcf7ff0u, /* 973 PROPHET program 180 1 */
    0x4bcc404bu, /* 974 PROPHET program 181 0 */
    0x9421ae9fu, /* 975 PROPHET program 181 1 */
    0x47d48eafu, /* 976 PROPHET program 182 0 */
    0x9b06d48eu, /* 977 PROPHET program 182 1 */
    0x273b7593u, /* 978 PROPHET program 183 0 */
    0x585a97a0u, /* 979 PROPHET program 183 1 */
    0x74f56da7u, /* 980 PROPHET program 184 0 */
    0x3df0d926u, /* 981 PROPHET program 184 1 */
    0x70eed611u, /* 982 PROPHET program 185 0 */
    0xcd8cb09eu, /* 983 PROPHET program 185 1 */
    0xf14a2a26u, /* 984 PROPHET program 186 0 */
    0xaf65b6efu, /* 985 PROPHET program 186 1 */
    0x86dbbb1du, /* 986 PROPHET program 187 0 */
    0x9c336975u, /* 987 PROPHET program 187 1 */
    0xeaf6ff4cu, /* 988 PROPHET program 188 0 */
    0x54ed712eu, /* 989 PROPHET program 188 1 */
    0x76e1ee2cu, /* 990 PROPHET program 189 0 */
    0xbdd40860u, /* 991 PROPHET program 189 1 */
    0xfac589f1u, /* 992 PROPHET program 190 0 */
    0x48491148u, /* 993 PROPHET program 190 1 */
    0xe2a33252u, /* 994 PROPHET program 191 0 */
    0xf900e0d1u, /* 995 PROPHET program 191 1 */
    0xc3a9c309u, /* 996 PROPHET program 192 0 */
    0xa2752d84u, /* 997 PROPHET program 192 1 */
    0x7192911cu, /* 998 PROPHET program 193 0 */
    0x87900884u, /* 999 PROPHET program 193 1 */
    0x955dcb6eu, /* 1000 PROPHET program 194 0 */
    0xf78e4ad0u, /* 1001 PROPHET program 194 1 */
    0xa25a233du, /* 1002 PROPHET program 195 0 */
    0xf14e8486u, /* 1003 PROPHET program 195 1 */
    0x04f18a5du, /* 1004 PROPHET program 196 0 */
    0x1775a63au, /* 1005 PROPHET program 196 1 */
    0x7e305da0u, /* 1006 PROPHET program 197 0 */
    0x063b0b1au, /* 1007 PROPHET program 197 1 */
    0xec5b3f9du, /* 1008 PROPHET program 198 0 */
    0xf0b21cbeu, /* 1009 PROPHET program 198 1 */
    0x0d520475u, /* 1010 PROPHET program 199 0 */
    0x404afcfdu, /* 1011 PROPHET program 199 1 */
    0x9c59c9aeu, /* 1012 PROPHET program 200 0 */
    0x44a06095u, /* 1013 PROPHET program 200 1 */
    0xbbdeae25u, /* 1014 PROPHET program (CPU1) 0 0 */
    0xb4265430u, /* 1015 PROPHET program (CPU1) 0 1 */
    0x4b045e95u, /* 1016 PROPHET program (CPU1) 1 0 */
    0x01231144u, /* 1017 PROPHET program (CPU1) 1 1 */
    0x3bbbf278u, /* 1018 PROPHET program (CPU1) 2 0 */
    0x87feea04u, /* 1019 PROPHET program (CPU1) 2 1 */
    0x65a201cbu, /* 1020 PROPHET program (CPU1) 3 0 */
    0x7d89ed68u, /* 1021 PROPHET program (CPU1) 3 1 */
    0xf3641b0du, /* 1022 PROPHET program (CPU1) 4 0 */
    0x55008411u, /* 1023 PROPHET program (CPU1) 4 1 */
    0x41b36feau, /* 1024 PROPHET program (CPU1) 5 0 */
    0xe1c5dc08u, /* 1025 PROPHET program (CPU1) 5 1 */
    0x9eaf2b9eu, /* 1026 PROPHET program (CPU1) 6 0 */
    0xc5887d6du, /* 1027 PROPHET program (CPU1) 6 1 */
    0x68a5f46fu, /* 1028 PROPHET program (CPU1) 7 0 */
    0x895aab48u, /* 1029 PROPHET program (CPU1) 7 1 */
    0x89d48a63u, /* 1030 PROPHET program (CPU1) 8 0 */
    0x326ecd81u, /* 1031 PROPHET program (CPU1) 8 1 */
    0x3eb9ad3bu, /* 1032 PROPHET program (CPU1) 9 0 */
    0xf312f3f2u, /* 1033 PROPHET program (CPU1) 9 1 */
    0xdec59be2u, /* 1034 PROPHET program (CPU1) 10 0 */
    0x3ce092e0u, /* 1035 PROPHET program (CPU1) 10 1 */
    0x171d6159u, /* 1036 PROPHET program (CPU1) 11 0 */
    0x945b2cd6u, /* 1037 PROPHET program (CPU1) 11 1 */
    0x64b2b04eu, /* 1038 PROPHET program (CPU1) 12 0 */
    0x1245c44au, /* 1039 PROPHET program (CPU1) 12 1 */
    0x87e8baeeu, /* 1040 PROPHET program (CPU1) 13 0 */
    0xac20d13eu, /* 1041 PROPHET program (CPU1) 13 1 */
    0x567bb11fu, /* 1042 PROPHET program (CPU1) 14 0 */
    0x50625dddu, /* 1043 PROPHET program (CPU1) 14 1 */
    0xb0a24991u, /* 1044 PROPHET program (CPU1) 15 0 */
    0x80931fe6u, /* 1045 PROPHET program (CPU1) 15 1 */
    0x1ea058acu, /* 1046 PROPHET program (CPU1) 16 0 */
    0xeda170b5u, /* 1047 PROPHET program (CPU1) 16 1 */
    0x11240d97u, /* 1048 PROPHET program (CPU1) 17 0 */
    0x5f756478u, /* 1049 PROPHET program (CPU1) 17 1 */
    0x7a43daa3u, /* 1050 PROPHET program (CPU1) 18 0 */
    0xd74d3689u, /* 1051 PROPHET program (CPU1) 18 1 */
    0x0b37acbeu, /* 1052 PROPHET program (CPU1) 19 0 */
    0x750fc061u, /* 1053 PROPHET program (CPU1) 19 1 */
    0x75302e1au, /* 1054 PROPHET program (CPU1) 20 0 */
    0x242c68c0u, /* 1055 PROPHET program (CPU1) 20 1 */
    0x667ccaf5u, /* 1056 PROPHET program (CPU1) 21 0 */
    0xa68725a4u, /* 1057 PROPHET program (CPU1) 21 1 */
    0x33145ecbu, /* 1058 PROPHET program (CPU1) 22 0 */
    0x40e325f9u, /* 1059 PROPHET program (CPU1) 22 1 */
    0x75ebffbbu, /* 1060 PROPHET program (CPU1) 23 0 */
    0x3a9f9f55u, /* 1061 PROPHET program (CPU1) 23 1 */
    0x8d5414a4u, /* 1062 PROPHET program (CPU1) 24 0 */
    0xe276d5cau, /* 1063 PROPHET program (CPU1) 24 1 */
    0xaea15a32u, /* 1064 PROPHET program (CPU1) 25 0 */
    0x85999925u, /* 1065 PROPHET program (CPU1) 25 1 */
    0x11be5c01u, /* 1066 PROPHET program (CPU1) 26 0 */
    0x5cad5138u, /* 1067 PROPHET program (CPU1) 26 1 */
    0xfe32abceu, /* 1068 PROPHET program (CPU1) 27 0 */
    0xb7be46d3u, /* 1069 PROPHET program (CPU1) 27 1 */
    0x3c2bfd7eu, /* 1070 PROPHET program (CPU1) 28 0 */
    0xfa634acbu, /* 1071 PROPHET program (CPU1) 28 1 */
    0xa0da2fcau, /* 1072 PROPHET program (CPU1) 29 0 */
    0x53f5a6feu, /* 1073 PROPHET program (CPU1) 29 1 */
    0xbed52711u, /* 1074 PROPHET program (CPU1) 30 0 */
    0x7a6161cfu, /* 1075 PROPHET program (CPU1) 30 1 */
    0x06be0816u, /* 1076 PROPHET program (CPU1) 31 0 */
    0xe817c100u, /* 1077 PROPHET program (CPU1) 31 1 */
    0x1d09f68bu, /* 1078 PROPHET program (CPU1) 32 0 */
    0x613a2b7du, /* 1079 PROPHET program (CPU1) 32 1 */
    0x8186ed71u, /* 1080 PROPHET program (CPU1) 33 0 */
    0xe8642084u, /* 1081 PROPHET program (CPU1) 33 1 */
    0x3759f803u, /* 1082 PROPHET program (CPU1) 34 0 */
    0x1310f719u, /* 1083 PROPHET program (CPU1) 34 1 */
    0x29bb5d7du, /* 1084 PROPHET program (CPU1) 35 0 */
    0x40f9f404u, /* 1085 PROPHET program (CPU1) 35 1 */
    0xfbf203a4u, /* 1086 PROPHET program (CPU1) 36 0 */
    0xce9179efu, /* 1087 PROPHET program (CPU1) 36 1 */
    0xbeec72fcu, /* 1088 PROPHET program (CPU1) 37 0 */
    0xd79c6f60u, /* 1089 PROPHET program (CPU1) 37 1 */
    0x1bf21384u, /* 1090 PROPHET program (CPU1) 38 0 */
    0xd177c132u, /* 1091 PROPHET program (CPU1) 38 1 */
    0xdd343e4fu, /* 1092 PROPHET program (CPU1) 39 0 */
    0x8dca574bu, /* 1093 PROPHET program (CPU1) 39 1 */
    0x8f26f21bu, /* 1094 PROPHET program (CPU1) 40 0 */
    0x261f079eu, /* 1095 PROPHET program (CPU1) 40 1 */
    0x07f9ed4du, /* 1096 PROPHET program (CPU1) 41 0 */
    0x99979950u, /* 1097 PROPHET program (CPU1) 41 1 */
    0x270d2ba0u, /* 1098 PROPHET program (CPU1) 42 0 */
    0x3817b8b0u, /* 1099 PROPHET program (CPU1) 42 1 */
    0xd1209ad1u, /* 1100 PROPHET program (CPU1) 43 0 */
    0xedad8179u, /* 1101 PROPHET program (CPU1) 43 1 */
    0xc12549f0u, /* 1102 PROPHET program (CPU1) 44 0 */
    0xd5c7318fu, /* 1103 PROPHET program (CPU1) 44 1 */
    0xa87d6d38u, /* 1104 PROPHET program (CPU1) 45 0 */
    0x90d2128du, /* 1105 PROPHET program (CPU1) 45 1 */
    0x9f5f5c09u, /* 1106 PROPHET program (CPU1) 46 0 */
    0xc43bebc0u, /* 1107 PROPHET program (CPU1) 46 1 */
    0x8042a8f5u, /* 1108 PROPHET program (CPU1) 47 0 */
    0xc3de4a34u, /* 1109 PROPHET program (CPU1) 47 1 */
    0x27d3dc82u, /* 1110 PROPHET program (CPU1) 48 0 */
    0xa60b0d32u, /* 1111 PROPHET program (CPU1) 48 1 */
    0x1827b753u, /* 1112 PROPHET program (CPU1) 49 0 */
    0xfe87143du, /* 1113 PROPHET program (CPU1) 49 1 */
    0x87d74711u, /* 1114 PROPHET program (CPU1) 50 0 */
    0xe6d5c23au, /* 1115 PROPHET program (CPU1) 50 1 */
    0x33905d1eu, /* 1116 PROPHET program (CPU1) 51 0 */
    0xf1336ed3u, /* 1117 PROPHET program (CPU1) 51 1 */
    0x7e001ebdu, /* 1118 PROPHET program (CPU1) 52 0 */
    0x8e73b6e6u, /* 1119 PROPHET program (CPU1) 52 1 */
    0xbfe12339u, /* 1120 PROPHET program (CPU1) 53 0 */
    0xb8e7f4b7u, /* 1121 PROPHET program (CPU1) 53 1 */
    0x8839ed28u, /* 1122 PROPHET program (CPU1) 54 0 */
    0xc92de3cfu, /* 1123 PROPHET program (CPU1) 54 1 */
    0x253b113fu, /* 1124 PROPHET program (CPU1) 55 0 */
    0x939acb3fu, /* 1125 PROPHET program (CPU1) 55 1 */
    0x8db9805cu, /* 1126 PROPHET program (CPU1) 56 0 */
    0x2ef350f4u, /* 1127 PROPHET program (CPU1) 56 1 */
    0x7a9feba9u, /* 1128 PROPHET program (CPU1) 57 0 */
    0xf67fa50bu, /* 1129 PROPHET program (CPU1) 57 1 */
    0x429413eau, /* 1130 PROPHET program (CPU1) 58 0 */
    0x3bf0fed8u, /* 1131 PROPHET program (CPU1) 58 1 */
    0xe68f71dfu, /* 1132 PROPHET program (CPU1) 59 0 */
    0xbd2582c6u, /* 1133 PROPHET program (CPU1) 59 1 */
    0xdf1b1f0eu, /* 1134 PROPHET program (CPU1) 60 0 */
    0xfb692886u, /* 1135 PROPHET program (CPU1) 60 1 */
    0x70019886u, /* 1136 PROPHET program (CPU1) 61 0 */
    0xfd464880u, /* 1137 PROPHET program (CPU1) 61 1 */
    0x9e79442fu, /* 1138 PROPHET program (CPU1) 62 0 */
    0x149395b4u, /* 1139 PROPHET program (CPU1) 62 1 */
    0xab1edb10u, /* 1140 PROPHET program (CPU1) 63 0 */
    0x30827e7cu, /* 1141 PROPHET program (CPU1) 63 1 */
    0x6d3e3992u, /* 1142 PROPHET program (CPU1) 64 0 */
    0xcc1699c0u, /* 1143 PROPHET program (CPU1) 64 1 */
    0xaac586eeu, /* 1144 PROPHET program (CPU1) 65 0 */
    0x9584b7ccu, /* 1145 PROPHET program (CPU1) 65 1 */
    0xb02b2a1du, /* 1146 PROPHET program (CPU1) 66 0 */
    0xf4ac7eafu, /* 1147 PROPHET program (CPU1) 66 1 */
    0x2c87b2eeu, /* 1148 PROPHET program (CPU1) 67 0 */
    0x417f67cdu, /* 1149 PROPHET program (CPU1) 67 1 */
    0x94101899u, /* 1150 PROPHET program (CPU1) 68 0 */
    0x4458139du, /* 1151 PROPHET program (CPU1) 68 1 */
    0xfe33abfdu, /* 1152 PROPHET program (CPU1) 69 0 */
    0x6ad500aau, /* 1153 PROPHET program (CPU1) 69 1 */
    0x748c4fbau, /* 1154 PROPHET program (CPU1) 70 0 */
    0xcd359334u, /* 1155 PROPHET program (CPU1) 70 1 */
    0xa82614a3u, /* 1156 PROPHET program (CPU1) 71 0 */
    0x04d429beu, /* 1157 PROPHET program (CPU1) 71 1 */
    0x83b0bd04u, /* 1158 PROPHET program (CPU1) 72 0 */
    0x274d0985u, /* 1159 PROPHET program (CPU1) 72 1 */
    0xa492c620u, /* 1160 PROPHET program (CPU1) 73 0 */
    0x7c0a1b48u, /* 1161 PROPHET program (CPU1) 73 1 */
    0xbfe56a72u, /* 1162 PROPHET program (CPU1) 74 0 */
    0x4e8511fau, /* 1163 PROPHET program (CPU1) 74 1 */
    0xc905acd3u, /* 1164 PROPHET program (CPU1) 75 0 */
    0x29f7caefu, /* 1165 PROPHET program (CPU1) 75 1 */
    0x31b97211u, /* 1166 PROPHET program (CPU1) 76 0 */
    0xfd748b75u, /* 1167 PROPHET program (CPU1) 76 1 */
    0x028474c6u, /* 1168 PROPHET program (CPU1) 77 0 */
    0x1e5c0c13u, /* 1169 PROPHET program (CPU1) 77 1 */
    0xa0d31d76u, /* 1170 PROPHET program (CPU1) 78 0 */
    0x5cbd187cu, /* 1171 PROPHET program (CPU1) 78 1 */
    0x69c74bd5u, /* 1172 PROPHET program (CPU1) 79 0 */
    0xafd57b9eu, /* 1173 PROPHET program (CPU1) 79 1 */
    0xf94cd605u, /* 1174 PROPHET program (CPU1) 80 0 */
    0x9c08096eu, /* 1175 PROPHET program (CPU1) 80 1 */
    0x4e631befu, /* 1176 PROPHET program (CPU1) 81 0 */
    0x721e4bcbu, /* 1177 PROPHET program (CPU1) 81 1 */
    0x6356fe6au, /* 1178 PROPHET program (CPU1) 82 0 */
    0x2f788ba4u, /* 1179 PROPHET program (CPU1) 82 1 */
    0xcf402898u, /* 1180 PROPHET program (CPU1) 83 0 */
    0xe4956088u, /* 1181 PROPHET program (CPU1) 83 1 */
    0x5e4653d5u, /* 1182 PROPHET program (CPU1) 84 0 */
    0x2642813du, /* 1183 PROPHET program (CPU1) 84 1 */
    0x6897d604u, /* 1184 PROPHET program (CPU1) 85 0 */
    0xb5626ec0u, /* 1185 PROPHET program (CPU1) 85 1 */
    0x376a5e8bu, /* 1186 PROPHET program (CPU1) 86 0 */
    0xcc5dd808u, /* 1187 PROPHET program (CPU1) 86 1 */
    0x5fb9451au, /* 1188 PROPHET program (CPU1) 87 0 */
    0x6ac5212du, /* 1189 PROPHET program (CPU1) 87 1 */
    0xd35c5b2eu, /* 1190 PROPHET program (CPU1) 88 0 */
    0x7406540du, /* 1191 PROPHET program (CPU1) 88 1 */
    0xbc2b07e8u, /* 1192 PROPHET program (CPU1) 89 0 */
    0xa71c101cu, /* 1193 PROPHET program (CPU1) 89 1 */
    0x3530e26eu, /* 1194 PROPHET program (CPU1) 90 0 */
    0xfea11db1u, /* 1195 PROPHET program (CPU1) 90 1 */
    0x823c275cu, /* 1196 PROPHET program (CPU1) 91 0 */
    0x94cd0d82u, /* 1197 PROPHET program (CPU1) 91 1 */
    0x89edce56u, /* 1198 PROPHET program (CPU1) 92 0 */
    0x659cc0f6u, /* 1199 PROPHET program (CPU1) 92 1 */
    0x5e7bb7e2u, /* 1200 PROPHET program (CPU1) 93 0 */
    0x68ea1ad7u, /* 1201 PROPHET program (CPU1) 93 1 */
    0x4c610eccu, /* 1202 PROPHET program (CPU1) 94 0 */
    0x3d677ad1u, /* 1203 PROPHET program (CPU1) 94 1 */
    0x7c9f715au, /* 1204 PROPHET program (CPU1) 95 0 */
    0x28387ba7u, /* 1205 PROPHET program (CPU1) 95 1 */
    0x73d4b0d9u, /* 1206 PROPHET program (CPU1) 96 0 */
    0x570e5251u, /* 1207 PROPHET program (CPU1) 96 1 */
    0x5022dc63u, /* 1208 PROPHET program (CPU1) 97 0 */
    0x150f7063u, /* 1209 PROPHET program (CPU1) 97 1 */
    0x54048517u, /* 1210 PROPHET program (CPU1) 98 0 */
    0x2e34e67bu, /* 1211 PROPHET program (CPU1) 98 1 */
    0x23e7975cu, /* 1212 PROPHET program (CPU1) 99 0 */
    0xa4d1fa64u, /* 1213 PROPHET program (CPU1) 99 1 */
    0x054da554u, /* 1214 PROPHET program (CPU1) 100 0 */
    0xe7946d03u, /* 1215 PROPHET program (CPU1) 100 1 */
    0xbe365799u, /* 1216 PROPHET program (CPU1) 101 0 */
    0xb5a715d2u, /* 1217 PROPHET program (CPU1) 101 1 */
    0x780ff25bu, /* 1218 PROPHET program (CPU1) 102 0 */
    0xee37ec0eu, /* 1219 PROPHET program (CPU1) 102 1 */
    0x7683c0f7u, /* 1220 PROPHET program (CPU1) 103 0 */
    0xd6cd321au, /* 1221 PROPHET program (CPU1) 103 1 */
    0x2c0d5365u, /* 1222 PROPHET program (CPU1) 104 0 */
    0x4ec1fe2au, /* 1223 PROPHET program (CPU1) 104 1 */
    0x54760eaeu, /* 1224 PROPHET program (CPU1) 105 0 */
    0x2b7a5d76u, /* 1225 PROPHET program (CPU1) 105 1 */
    0x6922348du, /* 1226 PROPHET program (CPU1) 106 0 */
    0x034ae343u, /* 1227 PROPHET program (CPU1) 106 1 */
    0x56de4ba9u, /* 1228 PROPHET program (CPU1) 107 0 */
    0x2e4656feu, /* 1229 PROPHET program (CPU1) 107 1 */
    0x0d638165u, /* 1230 PROPHET program (CPU1) 108 0 */
    0xe7240440u, /* 1231 PROPHET program (CPU1) 108 1 */
    0xa566c99du, /* 1232 PROPHET program (CPU1) 109 0 */
    0xa97c72ebu, /* 1233 PROPHET program (CPU1) 109 1 */
    0x0bd8a335u, /* 1234 PROPHET program (CPU1) 110 0 */
    0x8ad25b87u, /* 1235 PROPHET program (CPU1) 110 1 */
    0xf2a8eaeeu, /* 1236 PROPHET program (CPU1) 111 0 */
    0x78344c73u, /* 1237 PROPHET program (CPU1) 111 1 */
    0x159395a0u, /* 1238 PROPHET program (CPU1) 112 0 */
    0xbbe66b95u, /* 1239 PROPHET program (CPU1) 112 1 */
    0x5d3cbb62u, /* 1240 PROPHET program (CPU1) 113 0 */
    0xee20c570u, /* 1241 PROPHET program (CPU1) 113 1 */
    0x5b2aabe7u, /* 1242 PROPHET program (CPU1) 114 0 */
    0x0a04c59au, /* 1243 PROPHET program (CPU1) 114 1 */
    0xd6029be0u, /* 1244 PROPHET program (CPU1) 115 0 */
    0xf5aeec07u, /* 1245 PROPHET program (CPU1) 115 1 */
    0xcbea057au, /* 1246 PROPHET program (CPU1) 116 0 */
    0x29274089u, /* 1247 PROPHET program (CPU1) 116 1 */
    0x2515e737u, /* 1248 PROPHET program (CPU1) 117 0 */
    0x02bbf214u, /* 1249 PROPHET program (CPU1) 117 1 */
    0x82b50dacu, /* 1250 PROPHET program (CPU1) 118 0 */
    0xace46b14u, /* 1251 PROPHET program (CPU1) 118 1 */
    0xc2a35ba6u, /* 1252 PROPHET program (CPU1) 119 0 */
    0xe8a253a8u, /* 1253 PROPHET program (CPU1) 119 1 */
    0xd86e4090u, /* 1254 PROPHET program (CPU1) 120 0 */
    0xcd354a73u, /* 1255 PROPHET program (CPU1) 120 1 */
    0xc120b5edu, /* 1256 PROPHET program (CPU1) 121 0 */
    0xa6274069u, /* 1257 PROPHET program (CPU1) 121 1 */
    0x8efbe391u, /* 1258 PROPHET program (CPU1) 122 0 */
    0x7df82f1du, /* 1259 PROPHET program (CPU1) 122 1 */
    0xd781e295u, /* 1260 PROPHET program (CPU1) 123 0 */
    0x746745f0u, /* 1261 PROPHET program (CPU1) 123 1 */
    0x2541f10fu, /* 1262 PROPHET program (CPU1) 124 0 */
    0xf43976b0u, /* 1263 PROPHET program (CPU1) 124 1 */
    0x19fc2762u, /* 1264 PROPHET program (CPU1) 125 0 */
    0x7dd5c7c3u, /* 1265 PROPHET program (CPU1) 125 1 */
    0x868e6691u, /* 1266 PROPHET program (CPU1) 126 0 */
    0x949fc106u, /* 1267 PROPHET program (CPU1) 126 1 */
    0xae279b44u, /* 1268 PROPHET program (CPU1) 127 0 */
    0xffd13a1eu, /* 1269 PROPHET program (CPU1) 127 1 */
    0x0918dc2bu, /* 1270 PROPHET program (CPU1) 128 0 */
    0xa66faed3u, /* 1271 PROPHET program (CPU1) 128 1 */
    0xb7d733cbu, /* 1272 PROPHET program (CPU1) 129 0 */
    0x961b7dc6u, /* 1273 PROPHET program (CPU1) 129 1 */
    0x0c5bb0c4u, /* 1274 PROPHET program (CPU1) 130 0 */
    0x0e14dfc4u, /* 1275 PROPHET program (CPU1) 130 1 */
    0xa6128269u, /* 1276 PROPHET program (CPU1) 131 0 */
    0x97f515deu, /* 1277 PROPHET program (CPU1) 131 1 */
    0xd03d8b03u, /* 1278 PROPHET program (CPU1) 132 0 */
    0xe8e5f747u, /* 1279 PROPHET program (CPU1) 132 1 */
    0x3fccebcfu, /* 1280 PROPHET program (CPU1) 133 0 */
    0x2021bc20u, /* 1281 PROPHET program (CPU1) 133 1 */
    0xa965deabu, /* 1282 PROPHET program (CPU1) 134 0 */
    0x1b618638u, /* 1283 PROPHET program (CPU1) 134 1 */
    0x3b339be3u, /* 1284 PROPHET program (CPU1) 135 0 */
    0xeebfeb0fu, /* 1285 PROPHET program (CPU1) 135 1 */
    0xbeaa5f8cu, /* 1286 PROPHET program (CPU1) 136 0 */
    0x6b8a095eu, /* 1287 PROPHET program (CPU1) 136 1 */
    0xc8a52fadu, /* 1288 PROPHET program (CPU1) 137 0 */
    0xb114ab42u, /* 1289 PROPHET program (CPU1) 137 1 */
    0x46730df2u, /* 1290 PROPHET program (CPU1) 138 0 */
    0x01ed79a5u, /* 1291 PROPHET program (CPU1) 138 1 */
    0xb9ea4874u, /* 1292 PROPHET program (CPU1) 139 0 */
    0xce0defdbu, /* 1293 PROPHET program (CPU1) 139 1 */
    0x4b6ddaf6u, /* 1294 PROPHET program (CPU1) 140 0 */
    0xdf9b031au, /* 1295 PROPHET program (CPU1) 140 1 */
    0x3dd6bfafu, /* 1296 PROPHET program (CPU1) 141 0 */
    0xab5de7b1u, /* 1297 PROPHET program (CPU1) 141 1 */
    0x9f98b6f6u, /* 1298 PROPHET program (CPU1) 142 0 */
    0xba90cbe1u, /* 1299 PROPHET program (CPU1) 142 1 */
    0x0a290f3cu, /* 1300 PROPHET program (CPU1) 143 0 */
    0x0c5071f5u, /* 1301 PROPHET program (CPU1) 143 1 */
    0xca412986u, /* 1302 PROPHET program (CPU1) 144 0 */
    0x97163bf7u, /* 1303 PROPHET program (CPU1) 144 1 */
    0xea21c026u, /* 1304 PROPHET program (CPU1) 145 0 */
    0xa7e858c4u, /* 1305 PROPHET program (CPU1) 145 1 */
    0x0763f8d1u, /* 1306 PROPHET program (CPU1) 146 0 */
    0x51294ddau, /* 1307 PROPHET program (CPU1) 146 1 */
    0xabc1753au, /* 1308 PROPHET program (CPU1) 147 0 */
    0xa20a2447u, /* 1309 PROPHET program (CPU1) 147 1 */
    0x4a485f7fu, /* 1310 PROPHET program (CPU1) 148 0 */
    0xecfdcc65u, /* 1311 PROPHET program (CPU1) 148 1 */
    0x34e98aceu, /* 1312 PROPHET program (CPU1) 149 0 */
    0x451f90efu, /* 1313 PROPHET program (CPU1) 149 1 */
    0x1c2e7ff1u, /* 1314 PROPHET program (CPU1) 150 0 */
    0xc4a3a1afu, /* 1315 PROPHET program (CPU1) 150 1 */
    0x492a5aa1u, /* 1316 PROPHET program (CPU1) 151 0 */
    0xefdde0f5u, /* 1317 PROPHET program (CPU1) 151 1 */
    0xff943f95u, /* 1318 PROPHET program (CPU1) 152 0 */
    0x6ead7ec3u, /* 1319 PROPHET program (CPU1) 152 1 */
    0xe8ef3e23u, /* 1320 PROPHET program (CPU1) 153 0 */
    0xcd26074bu, /* 1321 PROPHET program (CPU1) 153 1 */
    0xd1d6e64fu, /* 1322 PROPHET program (CPU1) 154 0 */
    0xae3a42d3u, /* 1323 PROPHET program (CPU1) 154 1 */
    0xcd720f4au, /* 1324 PROPHET program (CPU1) 155 0 */
    0xa9969d76u, /* 1325 PROPHET program (CPU1) 155 1 */
    0xfa623681u, /* 1326 PROPHET program (CPU1) 156 0 */
    0x9f8e4e2au, /* 1327 PROPHET program (CPU1) 156 1 */
    0x20bb98a4u, /* 1328 PROPHET program (CPU1) 157 0 */
    0xe2fd19e0u, /* 1329 PROPHET program (CPU1) 157 1 */
    0x4e16e622u, /* 1330 PROPHET program (CPU1) 158 0 */
    0x7ba775d0u, /* 1331 PROPHET program (CPU1) 158 1 */
    0x283cfa5cu, /* 1332 PROPHET program (CPU1) 159 0 */
    0xc2c59cd5u, /* 1333 PROPHET program (CPU1) 159 1 */
    0xa5d8d898u, /* 1334 PROPHET program (CPU1) 160 0 */
    0x9accfc3fu, /* 1335 PROPHET program (CPU1) 160 1 */
    0xe3de4bd9u, /* 1336 PROPHET program (CPU1) 161 0 */
    0xa6c3215fu, /* 1337 PROPHET program (CPU1) 161 1 */
    0x77f5454cu, /* 1338 PROPHET program (CPU1) 162 0 */
    0x1b41233cu, /* 1339 PROPHET program (CPU1) 162 1 */
    0x9d2642f0u, /* 1340 PROPHET program (CPU1) 163 0 */
    0x5f882cc8u, /* 1341 PROPHET program (CPU1) 163 1 */
    0x6aa617f5u, /* 1342 PROPHET program (CPU1) 164 0 */
    0x623c2fe4u, /* 1343 PROPHET program (CPU1) 164 1 */
    0x7d27575du, /* 1344 PROPHET program (CPU1) 165 0 */
    0xaeab0280u, /* 1345 PROPHET program (CPU1) 165 1 */
    0x657a5ae0u, /* 1346 PROPHET program (CPU1) 166 0 */
    0x55c38fadu, /* 1347 PROPHET program (CPU1) 166 1 */
    0x5e5f8636u, /* 1348 PROPHET program (CPU1) 167 0 */
    0x8808ca82u, /* 1349 PROPHET program (CPU1) 167 1 */
    0x807f4beeu, /* 1350 PROPHET program (CPU1) 168 0 */
    0xa009e5ebu, /* 1351 PROPHET program (CPU1) 168 1 */
    0xf0cd083eu, /* 1352 PROPHET program (CPU1) 169 0 */
    0xc7c8ea95u, /* 1353 PROPHET program (CPU1) 169 1 */
    0x3027ca50u, /* 1354 PROPHET program (CPU1) 170 0 */
    0x8f8e7506u, /* 1355 PROPHET program (CPU1) 170 1 */
    0x2e8bb126u, /* 1356 PROPHET program (CPU1) 171 0 */
    0x545b0f1cu, /* 1357 PROPHET program (CPU1) 171 1 */
    0x4ed16a01u, /* 1358 PROPHET program (CPU1) 172 0 */
    0xb3de06b1u, /* 1359 PROPHET program (CPU1) 172 1 */
    0xb44d909au, /* 1360 PROPHET program (CPU1) 173 0 */
    0x3d12c6a6u, /* 1361 PROPHET program (CPU1) 173 1 */
    0xa52ecab9u, /* 1362 PROPHET program (CPU1) 174 0 */
    0xf67087afu, /* 1363 PROPHET program (CPU1) 174 1 */
    0xeff4d51eu, /* 1364 PROPHET program (CPU1) 175 0 */
    0xa1723042u, /* 1365 PROPHET program (CPU1) 175 1 */
    0xb079f57du, /* 1366 PROPHET program (CPU1) 176 0 */
    0x422fcdedu, /* 1367 PROPHET program (CPU1) 176 1 */
    0xc92a93d6u, /* 1368 PROPHET program (CPU1) 177 0 */
    0x06624cffu, /* 1369 PROPHET program (CPU1) 177 1 */
    0xe58cac68u, /* 1370 PROPHET program (CPU1) 178 0 */
    0x04ecfb1au, /* 1371 PROPHET program (CPU1) 178 1 */
    0x8535033eu, /* 1372 PROPHET program (CPU1) 179 0 */
    0x14a51d2fu, /* 1373 PROPHET program (CPU1) 179 1 */
    0xef2865c2u, /* 1374 PROPHET program (CPU1) 180 0 */
    0x6bcf7ff0u, /* 1375 PROPHET program (CPU1) 180 1 */
    0x4bcc404bu, /* 1376 PROPHET program (CPU1) 181 0 */
    0x9421ae9fu, /* 1377 PROPHET program (CPU1) 181 1 */
    0x47d48eafu, /* 1378 PROPHET program (CPU1) 182 0 */
    0x9b06d48eu, /* 1379 PROPHET program (CPU1) 182 1 */
    0x273b7593u, /* 1380 PROPHET program (CPU1) 183 0 */
    0x585a97a0u, /* 1381 PROPHET program (CPU1) 183 1 */
    0x74f56da7u, /* 1382 PROPHET program (CPU1) 184 0 */
    0x3df0d926u, /* 1383 PROPHET program (CPU1) 184 1 */
    0x70eed611u, /* 1384 PROPHET program (CPU1) 185 0 */
    0xcd8cb09eu, /* 1385 PROPHET program (CPU1) 185 1 */
    0xf14a2a26u, /* 1386 PROPHET program (CPU1) 186 0 */
    0xaf65b6efu, /* 1387 PROPHET program (CPU1) 186 1 */
    0x86dbbb1du, /* 1388 PROPHET program (CPU1) 187 0 */
    0x9c336975u, /* 1389 PROPHET program (CPU1) 187 1 */
    0xeaf6ff4cu, /* 1390 PROPHET program (CPU1) 188 0 */
    0x54ed712eu, /* 1391 PROPHET program (CPU1) 188 1 */
    0x76e1ee2cu, /* 1392 PROPHET program (CPU1) 189 0 */
    0xbdd40860u, /* 1393 PROPHET program (CPU1) 189 1 */
    0xfac589f1u, /* 1394 PROPHET program (CPU1) 190 0 */
    0x48491148u, /* 1395 PROPHET program (CPU1) 190 1 */
    0xe2a33252u, /* 1396 PROPHET program (CPU1) 191 0 */
    0xf900e0d1u, /* 1397 PROPHET program (CPU1) 191 1 */
    0xc3a9c309u, /* 1398 PROPHET program (CPU1) 192 0 */
    0xa2752d84u, /* 1399 PROPHET program (CPU1) 192 1 */
    0x7192911cu, /* 1400 PROPHET program (CPU1) 193 0 */
    0x87900884u, /* 1401 PROPHET program (CPU1) 193 1 */
    0x955dcb6eu, /* 1402 PROPHET program (CPU1) 194 0 */
    0xf78e4ad0u, /* 1403 PROPHET program (CPU1) 194 1 */
    0xa25a233du, /* 1404 PROPHET program (CPU1) 195 0 */
    0xf14e8486u, /* 1405 PROPHET program (CPU1) 195 1 */
    0x04f18a5du, /* 1406 PROPHET program (CPU1) 196 0 */
    0x1775a63au, /* 1407 PROPHET program (CPU1) 196 1 */
    0x7e305da0u, /* 1408 PROPHET program (CPU1) 197 0 */
    0x063b0b1au, /* 1409 PROPHET program (CPU1) 197 1 */
    0xec5b3f9du, /* 1410 PROPHET program (CPU1) 198 0 */
    0xf0b21cbeu, /* 1411 PROPHET program (CPU1) 198 1 */
    0x0d520475u, /* 1412 PROPHET program (CPU1) 199 0 */
    0x404afcfdu, /* 1413 PROPHET program (CPU1) 199 1 */
    0x9c59c9aeu, /* 1414 PROPHET program (CPU1) 200 0 */
    0x44a06095u, /* 1415 PROPHET program (CPU1) 200 1 */
};
