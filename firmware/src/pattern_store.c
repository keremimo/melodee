/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FBKG: a FUN13 sound/current-pattern record plus seven other banks per track,
 * all bank timings, arrangement assignments and bounded motion bank tags.
 * Five base sectors plus two extension sectors per A/B copy; the 27,200-byte record is validated in main-loop
 * staging before any runtime state is replaced. */
/* FBKK preserves all 32 pattern banks alongside the expanded recording. Its extra
 * steps use exactly 64 bits: four 7-bit notes, n/time + recorded marker (4 bits), flags (2), velocity (7),
 * hits/accents (8 each), probability (7). Only FBKK (written since Melodee 1.0.0) is read; older banks were
 * rewritten by 1.2's boot pass. */
/* FBKL: the same extent and FUN record, then all NTRK tracks: written when more than PROJ_TRK tracks are in use (FBKK
 * otherwise, which firmware before the MIDI-only tracks reads too). Its tail: each track's active bank, every bank's
 * timing, the song's columns, the motion tags, the FM6 function settings, the timed notes' directory past the FUN
 * record's (owners PROJ_TRK * NPAT ..), each MIDI-only track's parameters and MPC octave; then the banks the FUN record
 * does not hold, sparse: per track a byte (bit b: bank b follows), per bank its 64-bit step mask and the steps set in
 * it (step_packed_put); the rest zero. Music that does not fit is not written (PROJECT FULL). */
#define BANK_MAGIC 0x4B4B4246u                  /* FBKK: sound controls and full-precision per-hit pitch/length */
#define BANKL_MAGIC 0x4C4B4246u                 /* FBKL: every track, the banks sparse */
#define BANKM_MAGIC 0x4D4B4246u                 /* FBKM: FBKL plus 64 sections and explicit rests */
#define BANKN_MAGIC 0x4E4B4246u                 /* FBKN: FBKM plus timed master punch events */
#define BANKO_MAGIC 0x4F4B4246u                 /* FBKO: song-local sound snapshots */
#define BANKP_MAGIC 0x504B4246u                 /* FBKP: FBKO with 16 patterns a part, 8 a MIDI-only track (NPAT); always
                                                 * the song and punch blocks, song byte 2 = sounds follow. Before it, every
                                                 * format kept NPAT_V1 a track: those layouts read below */
#define BANKQ_MAGIC 0x514B4246u                 /* FBKQ: FBKP plus per-pattern sound IDs after the sound pool */
#define BANKQ_PATTERN_BYTES (NTRK * NPAT)
#define BANK_STORE_SIZE PROJ_BANK_BYTES
#define BANK_PROJ_SIZE PROJ_STORE_SIZE
#define BANK_STEP_SIZE 8u
#define BANK_ACTIVE_OFF (8u + BANK_PROJ_SIZE)
#define BANK_EXTRA_OFF (BANK_ACTIVE_OFF + PROJ_TRK)
#define BANK_TIMING_OFF (BANK_EXTRA_OFF + PROJ_TRK * (NPAT_V1 - 1u) * NSTEP * BANK_STEP_SIZE)
#define BANK_TIME_SIZE 4u
#define BANK_CHAIN_OFF (BANK_TIMING_OFF + PROJ_TRK * NPAT_V1 * BANK_TIME_SIZE)
#define BANK_MOTION_OFF (BANK_CHAIN_OFF + CHAIN_LEGACY_ROWS * PROJ_TRK)
#define BANK_FN_OFF (BANK_MOTION_OFF + MOTION_MAX)
#define BANK_FN_MAGIC 0x31364E46u
_Static_assert(BANK_FN_OFF + 4u + PROJ_TRK * FM6_NFN <= BANK_STORE_SIZE - 4u, "bank project extent");
#define BANKL_TIMING_OFF (BANK_ACTIVE_OFF + NTRK)
#define BANKL_CHAIN_OFF (BANKL_TIMING_OFF + NTRK * NPAT_V1 * BANK_TIME_SIZE)
#define BANKL_MOTION_OFF (BANKL_CHAIN_OFF + CHAIN_LEGACY_ROWS * NTRK)
#define BANKL_FN_OFF (BANKL_MOTION_OFF + MOTION_MAX)
#define BANKL_REC_OFF (BANKL_FN_OFF + 4u + PROJ_TRK * FM6_NFN)
#define BANKL_TRK_OFF (BANKL_REC_OFF + PROJ_REC_DIR_BYTES)
#define BANKL_TRK_BYTES (P_COUNT + 1u)                  /* the parameters + 64, the MPC octave + 3 */
#define BANKL_STEPS_OFF (BANKL_TRK_OFF + NMIDI * BANKL_TRK_BYTES)
#define BANKM_SONG_BYTES (4u + CHAIN_ROWS * (NTRK + 1u))
#define BANKN_FX_BYTES (CHAIN_ROWS * CHAIN_FX_EVENTS * 3u)
_Static_assert(sizeof(chain_fx_t) == 3u, "song punch wire event");
#define BANKO_HEAD_BYTES (4u + CHAIN_ROWS * NTRK)
#define BANKP_REC_BYTES ((NTRK * NPAT * 11u + 7u) / 8u)  /* FBKP: every owner's timed-note count (the FUN record's: 0) */
#define BANKP_CHAIN_OFF (BANKL_TIMING_OFF + NTRK * NPAT * BANK_TIME_SIZE)
#define BANKP_MOTION_OFF (BANKP_CHAIN_OFF + CHAIN_LEGACY_ROWS * NTRK)
#define BANKP_FN_OFF (BANKP_MOTION_OFF + MOTION_MAX)
#define BANKP_REC_OFF (BANKP_FN_OFF + 4u + PROJ_TRK * FM6_NFN)
#define BANKP_TRK_OFF (BANKP_REC_OFF + BANKP_REC_BYTES)
#define BANKP_STEPS_OFF (BANKP_TRK_OFF + NMIDI * BANKL_TRK_BYTES)
/* the FBKL family's layout: FBKL..FBKO (NPAT_V1 a track, one mask byte) or FBKP (NPAT, two) */
typedef struct { uint32_t npat, timing, chain, motion, fn, rec, recn, trk, steps, mask; } bank_lay_t;
static const bank_lay_t BANK_LAY[2] = {
    {NPAT_V1, BANKL_TIMING_OFF, BANKL_CHAIN_OFF, BANKL_MOTION_OFF, BANKL_FN_OFF, BANKL_REC_OFF, PROJ_REC_DIR_BYTES,
     BANKL_TRK_OFF, BANKL_STEPS_OFF, 1u},
    {NPAT, BANKL_TIMING_OFF, BANKP_CHAIN_OFF, BANKP_MOTION_OFF, BANKP_FN_OFF, BANKP_REC_OFF, BANKP_REC_BYTES,
     BANKP_TRK_OFF, BANKP_STEPS_OFF, 2u}};
static uint8_t bank_force_v1;                           /* capture as FBKK (project.c: FBKP did not fit) */
#define BL (BANK_LAY[bank_p16 != 0])
static uint8_t bank_patterns;
static uint8_t bank_song64, bank_songfx, bank_sounds, bank_sound_count, bank_sound_ids[CHAIN_SOUNDS];
static uint32_t bank_sound_bytes;
static uint32_t bank_sound_off(void) { return BL.steps - BANK_ACTIVE_OFF + BANKM_SONG_BYTES + BANKN_FX_BYTES; }

static uint32_t bank_pattern_off(void) { return bank_sound_off() + bank_sound_bytes; }
static uint32_t bank_steps_off(void) { return BL.steps + (bank_song64 ? BANKM_SONG_BYTES : 0u) + (bank_songfx ? BANKN_FX_BYTES : 0u) + bank_sound_bytes + (bank_patterns ? BANKQ_PATTERN_BYTES : 0u); }
_Static_assert(BANKL_STEPS_OFF + NTRK <= BANK_STORE_SIZE - 4u && (NTRK - PROJ_TRK) * NPAT_V1 <= 32u && NSTEP == 64u,
               "FBKL: its fixed fields, one more directory of owners, a 64-bit step mask");
_Static_assert(BANKP_STEPS_OFF + BANKM_SONG_BYTES + BANKN_FX_BYTES + 2u * NTRK <= BANK_STORE_SIZE - 4u && NPAT <= 16u,
               "FBKP: its fixed fields and two mask bytes a track");
#define BT(off) ((off) - BANK_ACTIVE_OFF)               /* an offset into the tail */
static uint8_t bank_too_full;                           /* the music did not fit an FBKL record */
static int bank_full(uint32_t n) { return n == BANK_STORE_SIZE; }
static uint32_t bank_magic(void) { return bank_patterns ? BANKQ_MAGIC : bank_p16 ? BANKP_MAGIC : bank_sounds ? BANKO_MAGIC : bank_songfx ? BANKN_MAGIC : bank_song64 ? BANKM_MAGIC : bank_wide ? BANKL_MAGIC : BANK_MAGIC; }
static void bank_capture_kind(int runtime)
{
    bank_patterns = 0;
    bank_sounds = bank_sound_count = 0; bank_sound_bytes = 0;
    if (runtime) for (uint32_t r = 0; r < chain_config.count; r++) for (uint32_t k = 0; k < cap_tracks(); k++) {
        uint32_t src = cap_map(k), id = src < NTRK ? chain_config.sound[r][src] : 0;
        if (!id) continue;
        uint32_t j = 0; while (j < bank_sound_count && bank_sound_ids[j] != id) j++;
        if (j == bank_sound_count && j < CHAIN_SOUNDS) bank_sound_ids[bank_sound_count++] = (uint8_t)id;
    }
    if (runtime) for (uint32_t k = 0; k < cap_tracks(); k++) for (uint32_t b = 0; b < NPAT; b++) {
        uint32_t src = cap_map(k), id = src < NTRK ? pattern_sound[src][b] : 0;
        if (!id) continue;
        bank_patterns = 1;
        uint32_t j = 0; while (j < bank_sound_count && bank_sound_ids[j] != id) j++;
        if (j == bank_sound_count && j < CHAIN_SOUNDS) bank_sound_ids[bank_sound_count++] = (uint8_t)id;
    }
    bank_sounds = bank_sound_count != 0;
    if (bank_sounds) bank_sound_bytes = BANKO_HEAD_BYTES + bank_sound_count * sizeof(song_sound_t);
    bank_songfx = bank_sounds;
    if (runtime) for (uint32_t r = 0; r < chain_config.count && r < CHAIN_ROWS; r++)
        if (chain_fx_count(r)) bank_songfx = 1;
    bank_song64 = (uint8_t)(bank_songfx || (runtime && (chain_config.count > CHAIN_LEGACY_ROWS || chain_config.rsv[CHAIN_LOOP])));
    if (runtime) for (uint32_t r = 0; r < chain_config.count && r < CHAIN_ROWS; r++)
        for (uint32_t k = 0; k < cap_tracks(); k++)
            if (cap_map(k) < NTRK && chain_patterns[r][cap_map(k)] >= NPAT) bank_song64 = 1;
    bank_wide = (uint8_t)(bank_song64 || (runtime && cap_tracks() > PROJ_TRK));
    bank_p16 = (uint8_t)!bank_force_v1;                 /* FBKP, unless a dense legacy project falls back to FBKK */
    if (bank_p16) bank_wide = bank_song64 = bank_songfx = 1;
}
static uint32_t bank_motion_off(void) { return bank_wide ? BL.motion : BANK_MOTION_OFF; }
static void bank_checksum(uint8_t *raw)
{
    uint32_t sum = proj_hash(raw, BANK_STORE_SIZE - 4u);
    memcpy(raw + BANK_STORE_SIZE - 4u, &sum, 4);
}
static int bank_step_pack(uint8_t *b, const step_t *s)
{
    if (s->n > 4u || s->time > ST_REST || s->flags > 7u || s->vel > 127u || s->probability > 101u || !step_acc_valid(s)) return 0;
    uint32_t pos = 0;
    memset(b, 0, 8u);
    for (uint32_t j = 0; j < 4u; j++) { if (s->note[j] > 127u) return 0; wire_bits_put(b, &pos, s->note[j], 7u); }
    wire_bits_put(b, &pos, (s->time == ST_NOTE ? s->n : s->time == ST_TIE ? 5u : 6u) | (s->flags & SF_RECORDED ? 8u : 0u), 4u);
    wire_bits_put(b, &pos, s->flags & 3u, 2u);
    wire_bits_put(b, &pos, s->vel, 7u); wire_bits_put(b, &pos, s->hit, 8u);
    wire_bits_put(b, &pos, s->acc, 8u); wire_bits_put(b, &pos, s->probability, 7u);
    return 1;
}
#define BANK_TAIL_SIZE (BANK_STORE_SIZE-BANK_ACTIVE_OFF)
/* timed notes of an owner past the FUN record's directory: the count FBKL keeps */
static uint32_t bank_rec_count(const project_t *q, uint32_t owner)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < RECORD_MAX; i++) n += q->recording[i].vel && recorded_owner(&q->recording[i]) == owner;
    return n;
}
/* a MIDI-only track's place no track moves to (track_remove): what a new one there starts with */
static void bank_midi_blank(uint8_t *b, uint32_t k)
{
    const preset_t *pr = eng_preset(ENGINES[ENGI_MIDI], 0);
    for (uint32_t i = 0; i < P_COUNT; i++)
        b[i] = (uint8_t)((i >= P_E0 ? (i == P_E0 ? (int16_t)(k + 1u) : pr->e[i - P_E0]) :
                          param_reserved(i) || param_fx_send(i) ? 0 : TP[i].def) + 64);
    b[P_FXTYPE] = b[P_FX2TYPE] = b[P_FX3TYPE] = (uint8_t)(FX_OFF + 64);
    b[P_COUNT] = 3u;
}
static int bankl_pack_tail(uint8_t *raw, const project_t *q)   /* runtime only (cap_map) */
{
    uint32_t pos = BT(bank_steps_off()), f = motion_guard();
    step_t empty = {{0}, 0, ST_REST};
    memset(raw, 0, BANK_TAIL_SIZE);
    if (pos + NTRK > BANK_TAIL_SIZE - 4u) { bank_too_full = 1; motion_unguard(f); return 0; }
    if (bank_sounds) {
        uint8_t *out = raw + bank_sound_off();
        out[0] = bank_sound_count; out[1] = P_COUNT;
        out[2] = (uint8_t)sizeof(song_sound_t); out[3] = (uint8_t)(sizeof(song_sound_t) >> 8);
        for (uint32_t j = 0; j < bank_sound_count; j++) {
            const song_sound_t *sound = song_sound_get(bank_sound_ids[j]);
            if (!sound || !song_sound_valid(sound)) { motion_unguard(f); return 0; }
            memcpy(out + BANKO_HEAD_BYTES + j * sizeof *sound, sound, sizeof *sound);
        }
        for (uint32_t r = 0; r < chain_config.count; r++) for (uint32_t k = 0; k < NTRK; k++) {
            uint32_t src = cap_map(k), id = src < NTRK ? chain_config.sound[r][src] : 0;
            if (!id) continue;
            for (uint32_t j = 0; j < bank_sound_count; j++) if (bank_sound_ids[j] == id) out[4u + r * NTRK + k] = (uint8_t)(j + 1u);
        }
    }
    if (bank_patterns) {
        uint8_t *out = raw + bank_pattern_off();
        for (uint32_t k = 0; k < NTRK; k++) for (uint32_t b = 0; b < NPAT; b++) {
            uint32_t src = cap_map(k), id = src < NTRK ? pattern_sound[src][b] : 0;
            for (uint32_t j = 0; id && j < bank_sound_count; j++)
                if (bank_sound_ids[j] == id) out[k * NPAT + b] = (uint8_t)(j + 1u);
        }
    }
    if (bank_song64) {
        uint8_t *song = raw + BT(BL.steps);
        song[0] = chain_config.count;
        song[1] = chain_config.rsv[CHAIN_LOOP];
        song[2] = bank_p16 && bank_sounds;              /* (FBKP: sounds follow) */
        for (uint32_t r = 0; r < CHAIN_ROWS; r++) {
            song[4u + r * (NTRK + 1u)] = chain_config.row[r].repeat;
            for (uint32_t k = 0; k < NTRK; k++)
                song[5u + r * (NTRK + 1u) + k] = cap_map(k) < NTRK ? chain_patterns[r][cap_map(k)] : 0u;
        }
    }
    if (bank_songfx) memcpy(raw + BT(BL.steps) + BANKM_SONG_BYTES, chain_config.fx, BANKN_FX_BYTES);
    if (pos + NTRK * BL.mask > BANK_TAIL_SIZE - 4u) { bank_too_full = 1; motion_unguard(f); return 0; }
    for (uint32_t k = 0; k < NTRK; k++) {
        uint32_t src = cap_map(k), active = src < NTRK ? trk[src].pattern : 0u, at = pos;
        pos += BL.mask;
        track_t *t = src < NTRK ? &trk[src] : 0;
        raw[k] = (uint8_t)active;
        if (t) pattern_commit(t);
        for (uint32_t b = 0; b < BL.npat; b++) {
            pattern_t p;
            if (t && b < trk_npat(src)) pattern_read(src, b, &p);
            else { for (uint32_t i = 0; i < NSTEP; i++) p.step[i] = empty; for (uint32_t i = 0; i < 4u; i++) p.timing[i] = TP[P_SLEN + i].def; }
            for (uint32_t i = 0; i < 4u; i++)
                raw[BT(BL.timing) + (k * BL.npat + b) * 4u + i] = (uint8_t)p.timing[i];
            if (k < PROJ_TRK && b == active) continue;  /* (the FUN record holds a part's active bank) */
            uint32_t lo = 0, hi = 0, n = 0;
            for (uint32_t i = 0; i < NSTEP; i++) if (memcmp(&p.step[i], &empty, sizeof empty)) {
                if (i < 32u) lo |= 1u << i; else hi |= 1u << (i - 32u);
                n++;
            }
            if (!n) continue;
            if (pos + 8u + n * BANK_STEP_SIZE > BANK_TAIL_SIZE - 4u) { bank_too_full = 1; motion_unguard(f); return 0; }
            raw[at + b / 8u] |= (uint8_t)(1u << (b % 8u));
            for (uint32_t i = 0; i < 4u; i++) { raw[pos + i] = (uint8_t)(lo >> (8u * i)); raw[pos + 4u + i] = (uint8_t)(hi >> (8u * i)); }
            pos += 8u;
            for (uint32_t i = 0; i < NSTEP; i++) if (memcmp(&p.step[i], &empty, sizeof empty)) {
                step_packed_put(raw + pos, &p.step[i]);
                pos += BANK_STEP_SIZE;
            }
        }
        if (k >= PROJ_TRK) {
            uint8_t *b = raw + BT(BL.trk) + (k - PROJ_TRK) * BANKL_TRK_BYTES;
            if (!t) bank_midi_blank(b, k);
            else {
                for (uint32_t i = 0; i < P_COUNT; i++) {
                    int16_t v = motion_base_value(t, i);
                    if (v < -64 || v > 127) { motion_unguard(f); return 0; }
                    b[i] = (uint8_t)(v + 64);
                }
                b[P_COUNT] = (uint8_t)(clamp(t->mpc_octave, -3, 3) + 3);
            }
        }
        for (uint32_t r = 0; r < CHAIN_LEGACY_ROWS; r++) {
            uint32_t p = t ? chain_patterns[r][src] : 0u;
            raw[BT(BL.chain) + r * NTRK + k] = (uint8_t)(p < BL.npat ? p : 0u);
        }
    }
    memcpy(raw + BT(BL.motion), cap_motion_tags, sizeof cap_motion_tags);
    if (q->fm6_fn_ok) {
        uint32_t m = BANK_FN_MAGIC;
        memcpy(raw + BT(BL.fn), &m, 4);
        memcpy(raw + BT(BL.fn) + 4u, q->fm6_fn, sizeof q->fm6_fn);
    }
    /* the timed notes' counts: FBKP every owner's (in their order in the FUN record), FBKL those past its own */
    for (uint32_t o = 0, first = bank_p16 ? 0u : PROJ_TRK * NPAT_V1; first + o < proj_owners(); o++) {
        uint32_t dir = o * 11u;
        wire_bits_put(raw + BT(BL.rec), &dir, bank_rec_count(q, proj_owner_id(first + o)), 11);
    }
    motion_unguard(f); return 1;
}
/* FBKP's room for the banks' steps: its tail past the fixed fields, the song and punch blocks and the sounds kept
 * (pattern.c pattern_fits) */
static uint32_t pattern_room(void)
{
    uint32_t fixed = BT(BANKP_STEPS_OFF) + BANKM_SONG_BYTES + BANKN_FX_BYTES + 2u * NTRK + 4u;
    if (pattern_sounds_any()) fixed += BANKQ_PATTERN_BYTES;
    if (song_sound_count) fixed += BANKO_HEAD_BYTES + song_sound_count * (uint32_t)sizeof(song_sound_t);
    return fixed < BANK_TAIL_SIZE ? BANK_TAIL_SIZE - fixed : 0u;
}
/* the music fits FBKK, the dense four-part record of before 16 patterns: the parts' banks past NPAT_V1 empty, none
 * playing there, no MIDI-only track in use */
static int bank_v1_fits(void)
{
    if (cap_tracks() > PROJ_TRK || pattern_sounds_any()) return 0;
    for (uint32_t k = 0; k < PROJ_TRK; k++) {
        uint32_t src = cap_map(k);
        if (src >= NTRK) continue;
        if (trk[src].pattern >= NPAT_V1) return 0;
        for (uint32_t b = NPAT_V1; b < trk_npat(src); b++) if (pattern_bank_cost(src, b)) return 0;
    }
    return 1;
}
static int bank_pack_tail(uint8_t *raw,const project_t *q,int runtime)
{
    if (runtime && !chain_valid(&chain_config)) return 0;
    bank_capture_kind(runtime);
    bank_too_full = 0;
    if (bank_wide) return bankl_pack_tail(raw, q);
    uint32_t pos=BANK_EXTRA_OFF-BANK_ACTIVE_OFF,f=motion_guard();
    memset(raw,0,BANK_TAIL_SIZE);
    for (uint32_t k = 0; k < PROJ_TRK; k++) {
        uint32_t src = runtime ? cap_map(k) : k;
        track_t *t = runtime && src < NTRK ? &trk[src] : 0;
        uint32_t active = runtime ? (t ? t->pattern : 0u) : q->pattern[k];
        raw[0u + k] = (uint8_t)active;
        if (t) pattern_commit(t);
        if (active >= NPAT_V1) { motion_unguard(f); return 0; }
        for (uint32_t b = 0; b < NPAT_V1; b++) {
            pattern_t p;
            if (t) pattern_read(src, b, &p);
            for (uint32_t i = 0; i < 4u; i++) {
                int16_t v = t ? p.timing[i] : (b != active || runtime ? TP[P_SLEN + i].def : q->t[k].p[P_SLEN + i]);
                raw[(BANK_TIMING_OFF - BANK_ACTIVE_OFF)+(k*NPAT_V1+b)*4u+i]=(uint8_t)v;
            }
            if (b == active) continue;
            for (uint32_t i = 0; i < NSTEP; i++, pos += BANK_STEP_SIZE) {
                step_t empty = {{0},0,ST_REST};
                if (!bank_step_pack(raw + pos, t ? &p.step[i] : &empty)) { motion_unguard(f); return 0; }
            }
        }
    }
    if (runtime) {
        for (uint32_t r = 0; r < CHAIN_LEGACY_ROWS; r++)
            for (uint32_t k = 0; k < PROJ_TRK; k++)
                raw[(BANK_CHAIN_OFF - BANK_ACTIVE_OFF) + r * PROJ_TRK + k] = cap_map(k) < NTRK ? chain_patterns[r][cap_map(k)] : 0u;
        memcpy(raw + (BANK_MOTION_OFF - BANK_ACTIVE_OFF), cap_motion_tags, sizeof cap_motion_tags);
    }
    if (q->fm6_fn_ok) {
        uint32_t m = BANK_FN_MAGIC;
        memcpy(raw + (BANK_FN_OFF - BANK_ACTIVE_OFF), &m, 4);
        memcpy(raw + (BANK_FN_OFF - BANK_ACTIVE_OFF) + 4u, q->fm6_fn, sizeof q->fm6_fn);
    }
    motion_unguard(f);return 1;
}
static int bank_pack(uint8_t *raw,const project_t *q,int runtime)
{
    uint32_t magic,size=BANK_STORE_SIZE;
    if(!bank_pack_tail(raw+BANK_ACTIVE_OFF,q,runtime))return 0;
    magic=bank_magic();memcpy(raw,&magic,4);memcpy(raw+4,&size,4);
    if(!proj_pack((project_store_t *)(raw+8u),q))return 0;
    bank_checksum(raw);return 1;
}
static uint8_t bank_import_tags[MOTION_MAX];
/* the motion tags, the FM6 function settings: FBKK's and FBKL's */
static int bank_tail_common(const uint8_t *raw, uint32_t motion_off, uint32_t fn_off)
{
    for (uint32_t i = 0; i < proj_scratch.motion.count; i++) {
        if (raw[BT(motion_off) + i] >= NPAT) return 0;
        for (uint32_t j = 0; j < i; j++) if (raw[BT(motion_off) + i] == raw[BT(motion_off) + j] &&
            proj_scratch.motion.event[i].place == proj_scratch.motion.event[j].place &&
            proj_scratch.motion.event[i].param == proj_scratch.motion.event[j].param) return 0;
    }
    memset(bank_import_tags, 0, sizeof bank_import_tags);
    uint32_t n = 0;
    for (uint32_t i = 0; i < proj_scratch.motion.count; i++) {
        bank_import_tags[n++] = raw[BT(motion_off) + i];
    }
    {   /* the FM6 function settings (none written: Dexed's) */
        uint32_t m, k;
        memcpy(&m, raw + BT(fn_off), 4);
        if (m == BANK_FN_MAGIC) {
            const uint8_t *f = raw + BT(fn_off) + 4u;
            for (k = 0; k < PROJ_TRK; k++)
                if (!fm6_fn_ok(f + k * FM6_NFN))
                    return 0;
            memcpy(proj_scratch.fm6_fn, f, sizeof proj_scratch.fm6_fn);
            proj_scratch.fm6_fn_ok = 1;
            proj_scratch.sum = proj_sum(&proj_scratch);
        }
    }
    return 1;
}
/* a song row's pattern as stored -> as played: before FBKP, rest and hold were NPAT_V1 and NPAT_V1 + 1 */
static uint32_t bank_song_value(uint32_t p)
{
    if (bank_p16) return p;
    return p == NPAT_V1 ? CHAIN_REST : p == NPAT_V1 + 1u ? CHAIN_HOLD : p;
}
static uint32_t bank_present(const uint8_t *raw, uint32_t at)   /* a track's bank mask */
{
    return BL.mask == 2u ? raw[at] | (uint32_t)raw[at + 1u] << 8 : raw[at];
}
/* a step as FBKL keeps it: one a bank record could hold */
static int bankl_step_ok(const uint8_t *b)
{
    step_t s = step_packed_get(b);
    return (b[3] >> 4 & 7u) != 7u && s.probability <= 101u && step_acc_valid(&s);
}
static int bankl_tail_valid(const uint8_t *raw)
{
    bank_sound_bytes = 0;
    if (bank_p16) {
        if (raw[BT(BL.steps) + 2u] > 1u) return 0;
        bank_sounds = raw[BT(BL.steps) + 2u];
    }
    if (bank_patterns && !bank_sounds) return 0;
    if (bank_sounds) {
        const uint8_t *in = raw + bank_sound_off();
        uint32_t count = in[0], size = in[2] | (uint32_t)in[3] << 8;
        if (!count || count > CHAIN_SOUNDS || in[1] != P_COUNT || size != sizeof(song_sound_t)) return 0;
        bank_sound_bytes = BANKO_HEAD_BYTES + count * size;
        if (bank_sound_off() + bank_sound_bytes + (bank_patterns ? BANKQ_PATTERN_BYTES : 0u) + NTRK * BL.mask > BANK_TAIL_SIZE - 4u) return 0;
        uint8_t used[CHAIN_SOUNDS] = {0};
        for (uint32_t r = 0; r < CHAIN_ROWS; r++) for (uint32_t k = 0; k < NTRK; k++) {
            uint32_t id = in[4u + r * NTRK + k];
            if (id > count || (r >= raw[BT(BL.steps)] && id)) return 0;
            if (id) {
                const song_sound_t *sound = (const song_sound_t *)(in + BANKO_HEAD_BYTES + (id - 1u) * size);
                if (k >= NPART && sound->engine != ENGI_MIDI) return 0;
                used[id-1u] = 1;
            }
        }
        if (bank_patterns) {
            const uint8_t *ids = raw + bank_pattern_off();
            uint32_t any = 0;
            for (uint32_t k = 0; k < NTRK; k++) for (uint32_t b = 0; b < NPAT; b++) {
                uint32_t id = ids[k * NPAT + b];
                if (id > count || (b >= trk_npat(k) && id)) return 0;
                if (!id) continue;
                const song_sound_t *sound = (const song_sound_t *)(in + BANKO_HEAD_BYTES + (id - 1u) * size);
                if (k >= NPART && sound->engine != ENGI_MIDI) return 0;
                used[id - 1u] = 1; any = 1;
            }
            if (!any) return 0;
        }
        for (uint32_t j = 0; j < count; j++)
            if (!used[j] || !song_sound_valid((const song_sound_t *)(in + BANKO_HEAD_BYTES + j * size))) return 0;
    }
    uint32_t pos = BT(bank_steps_off());
    if (!bank_song64 && proj_scratch.parts <= PROJ_TRK) return 0;
    if (bank_song64) {
        const uint8_t *song = raw + BT(BL.steps);
        if (song[0] > CHAIN_ROWS || song[1] > 1u || (song[2] && !bank_p16) || song[3] ||
            proj_scratch.chain.count != (song[0] < CHAIN_LEGACY_ROWS ? song[0] : CHAIN_LEGACY_ROWS)) return 0;
        for (uint32_t r = 0; r < CHAIN_ROWS; r++) {
            uint32_t rep = song[4u + r * (NTRK + 1u)];
            if (!rep || rep > 16u) return 0;
            for (uint32_t k = 0; k < NTRK; k++) {
                uint32_t p = song[5u + r * (NTRK + 1u) + k], v = bank_song_value(p);
                if (v != CHAIN_REST && v != CHAIN_HOLD && v >= (bank_p16 ? trk_npat(k) : NPAT_V1)) return 0;
                if (v == CHAIN_HOLD && (!r || bank_song_value(song[5u + (r-1u) * (NTRK + 1u) + k]) == CHAIN_REST)) return 0;
                if (r < CHAIN_LEGACY_ROWS && raw[BT(BL.chain) + r * NTRK + k] != (p < BL.npat ? p : 0u)) return 0;
                if (!k && r < proj_scratch.chain.count &&
                    (proj_scratch.chain.row[r].repeat != rep || proj_scratch.chain.row[r].slot != (p < BL.npat ? p : 0u))) return 0;
            }
        }
    }
    if (bank_songfx) {
        const uint8_t *events = raw + BT(BL.steps) + BANKM_SONG_BYTES;
        uint32_t count = raw[BT(BL.steps)];
        for (uint32_t r = 0; r < CHAIN_ROWS; r++) for (uint32_t j = 0; j < CHAIN_FX_EVENTS; j++) {
            chain_fx_t e; memcpy(&e, events + (r * CHAIN_FX_EVENTS + j) * 3u, sizeof e);
            if (!chain_fx_valid(&e) || (r >= count && e.fx)) return 0;
        }
    }
    for (uint32_t k = 0; k < NTRK; k++) {
        uint32_t active = raw[k], present = bank_present(raw, pos), have = bank_p16 ? trk_npat(k) : NPAT_V1;
        pos += BL.mask;
        if (active >= have || (present >> have) ||
            (k < PROJ_TRK && (active != proj_scratch.pattern[k] || (present >> active & 1u)))) return 0;
        for (uint32_t b = 0; b < BL.npat; b++) {
            for (uint32_t i = 0; i < 4u; i++) {
                int16_t v = raw[BT(BL.timing) + (k * BL.npat + b) * 4u + i];
                if (v < TP[P_SLEN + i].min || v > TP[P_SLEN + i].max) return 0;
                if (k < PROJ_TRK && b == active && v != proj_scratch.t[k].p[P_SLEN + i]) return 0;
            }
            if (!(present >> b & 1u)) continue;
            if (pos + 8u > BANK_TAIL_SIZE - 4u) return 0;
            uint32_t n = 0;
            for (uint32_t i = 0; i < 8u; i++) n += (uint32_t)__builtin_popcount(raw[pos + i]);
            pos += 8u;
            if (!n || pos + n * BANK_STEP_SIZE > BANK_TAIL_SIZE - 4u) return 0;
            for (uint32_t i = 0; i < n; i++, pos += BANK_STEP_SIZE) if (!bankl_step_ok(raw + pos)) return 0;
        }
        if (k >= PROJ_TRK) {
            const uint8_t *b = raw + BT(BL.trk) + (k - PROJ_TRK) * BANKL_TRK_BYTES;
            for (uint32_t i = 0; i < P_COUNT; i++) if (b[i] > 191u) return 0;
            if (b[P_COUNT] > 6u) return 0;
        }
    }
    for (; pos < BANK_TAIL_SIZE - 4u; pos++) if (raw[pos]) return 0;
    for (uint32_t i = 0; i < CHAIN_LEGACY_ROWS * NTRK; i++) if (raw[BT(BL.chain) + i] >= BL.npat) return 0;
    if (!bank_tail_common(raw, BL.motion, BL.fn)) return 0;
    /* Reserve before project_restore_runtime replaces any active music. The
     * old pool's bytes remain intact when validation or allocation fails. */
    if (bank_sounds) {
        uint32_t bytes = raw[bank_sound_off()] * sizeof(song_sound_t), f = motion_guard();
        int ok = bytes <= resource[RES_SONG_SOUNDS].size || resource_resize(RES_SONG_SOUNDS, bytes) != 0;
        motion_unguard(f); if (!ok) return 0;
    }
    if (bank_patterns) {
        uint32_t f = motion_guard();
        int ok = resource_get(RES_SONG_BASE, NTRK * sizeof(song_sound_t)) != 0;
        if (ok) song_base_reserved = 1;
        motion_unguard(f); if (!ok) return 0;
    }
    return 1;
}
static int bank_tail_valid(const uint8_t *raw)
{
    if (bank_wide) return bankl_tail_valid(raw);
    uint32_t pos=BANK_EXTRA_OFF-BANK_ACTIVE_OFF;
    if (proj_scratch.parts > PROJ_TRK) return 0;
    for (uint32_t k = 0; k < PROJ_TRK; k++) {
        if (raw[0u + k] >= NPAT_V1) return 0;
        if (raw[0u + k] != proj_scratch.pattern[k]) return 0;
        for (uint32_t b = 0; b < NPAT_V1; b++) {
            for (uint32_t i = 0; i < 4u; i++) {
                int16_t v = raw[(BANK_TIMING_OFF - BANK_ACTIVE_OFF) + (k * NPAT_V1 + b) * 4u + i];
                if (v < TP[P_SLEN + i].min || v > TP[P_SLEN + i].max) return 0;
                if (b == raw[0u + k] && v != proj_scratch.t[k].p[P_SLEN + i]) return 0;
            }
            if (b == raw[0u + k]) continue;
            for (uint32_t i = 0; i < NSTEP; i++, pos += BANK_STEP_SIZE) { step_t s; if (!step_wire_unpack(&s, raw + pos)) return 0; }
        }
    }
    for (uint32_t i = 0; i < CHAIN_LEGACY_ROWS * PROJ_TRK; i++) if (raw[(BANK_CHAIN_OFF - BANK_ACTIVE_OFF) + i] >= NPAT_V1) return 0;
    return bank_tail_common(raw, BANK_MOTION_OFF, BANK_FN_OFF);
}
/* the FUN record's timed notes past its own directory (FBKL), before it is read */
static void bank_rec_ext(const uint8_t *tail)
{
    if (bank_wide) memcpy(proj_rec_ext, tail + BT(BL.rec), BL.recn);
}
static int bank_valid(const uint8_t *raw,uint32_t len)
{
    uint32_t magic,size,sum;
    if(!bank_full(len))return 0;
    memcpy(&magic,raw,4);memcpy(&size,raw+4,4);memcpy(&sum,raw+len-4u,4);
    if((magic!=BANK_MAGIC && magic!=BANKL_MAGIC && magic!=BANKM_MAGIC && magic!=BANKN_MAGIC && magic!=BANKO_MAGIC && magic!=BANKP_MAGIC && magic!=BANKQ_MAGIC)||size!=len||sum!=proj_hash(raw,len-4u))return 0;
    bank_patterns=(uint8_t)(magic==BANKQ_MAGIC);
    bank_p16=(uint8_t)(magic==BANKP_MAGIC || bank_patterns);
    bank_sounds=(uint8_t)(magic==BANKO_MAGIC); bank_sound_bytes=0;
    bank_songfx=(uint8_t)(magic==BANKN_MAGIC || bank_sounds || bank_p16);
    bank_song64=(uint8_t)(magic==BANKM_MAGIC || bank_songfx);
    bank_wide=(uint8_t)(magic!=BANK_MAGIC);bank_rec_ext(raw+BANK_ACTIVE_OFF);
    if(!proj_import(&proj_scratch,raw+8u,BANK_PROJ_SIZE))return 0;
    return bank_tail_valid(raw+BANK_ACTIVE_OFF);
}
static void bankl_restore_tail(const uint8_t *raw)
{
    uint32_t pos = BT(bank_steps_off()), f = motion_guard();
    pattern_loading = 1;                                /* (what a record holds always loads) */
    step_t empty = {{0}, 0, ST_REST};
    chain_from_legacy(&proj_scratch.chain);
    memset(chain_patterns, 0, sizeof chain_patterns);
    for (uint32_t r = 0; r < CHAIN_LEGACY_ROWS; r++)
        for (uint32_t k = 0; k < NTRK; k++) chain_patterns[r][k] = raw[BT(BL.chain) + r * NTRK + k];
    if (bank_song64) {
        const uint8_t *song = raw + BT(BL.steps);
        chain_config.count = song[0];
        chain_config.rsv[CHAIN_LOOP] = song[1];
        for (uint32_t r = 0; r < CHAIN_ROWS; r++) {
            chain_config.row[r].repeat = song[4u + r * (NTRK + 1u)];
            for (uint32_t k = 0; k < NTRK; k++) chain_patterns[r][k] = (uint8_t)bank_song_value(song[5u + r * (NTRK + 1u) + k]);
            chain_config.row[r].slot = chain_patterns[r][0];
        }
    }
    if (bank_sounds) {
        const uint8_t *in = raw + bank_sound_off();
        song_sound_count = in[0];
        memcpy(chain_config.sound, in + 4u, CHAIN_ROWS * NTRK);
        memcpy(song_sound_pool(), in + BANKO_HEAD_BYTES, song_sound_count * sizeof(song_sound_t));
    } else { song_sound_count = 0; resource_release(RES_SONG_SOUNDS); }
    if (bank_patterns) memcpy(pattern_sound, raw + bank_pattern_off(), sizeof pattern_sound);
    song_base_reserved = 0;
    if (bank_songfx) memcpy(chain_config.fx, raw + BT(BL.steps) + BANKM_SONG_BYTES, BANKN_FX_BYTES);
    memcpy(motion_pattern, bank_import_tags, sizeof motion_pattern);
    for (uint32_t k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        uint32_t present = bank_present(raw, pos);
        pos += BL.mask;
        if (k >= PROJ_TRK) {                            /* (project_restore_runtime made it a blank MIDI Track) */
            const uint8_t *b = raw + BT(BL.trk) + (k - PROJ_TRK) * BANKL_TRK_BYTES;
            for (uint32_t i = 0; i < P_COUNT; i++) {
                const param_desc_t *d = param_desc_of(ENGI_MIDI, i);
                t->p[i] = param_reserved(i) ? 0 : (int16_t)clamp((int32_t)b[i] - 64, d->min, d->max);
            }
            t->mpc_octave = (int8_t)((int32_t)b[P_COUNT] - 3);
        }
        t->pattern = raw[k]; t->pattern_next = 0xff; t->pattern_gen++;
        for (uint32_t b = 0; b < trk_npat(k); b++) {     /* (an older record's banks past its NPAT_V1: empty) */
            pattern_t p;
            for (uint32_t i = 0; i < 4u; i++) p.timing[i] = b < BL.npat ? raw[BT(BL.timing) + (k * BL.npat + b) * 4u + i] : TP[P_SLEN + i].def;
            if (k < PROJ_TRK && b == t->pattern) memcpy(p.step, t->step, sizeof p.step);
            else if (present >> b & 1u) {
                uint32_t lo = 0, hi = 0;
                for (uint32_t i = 0; i < 4u; i++) { lo |= (uint32_t)raw[pos + i] << (8u * i); hi |= (uint32_t)raw[pos + 4u + i] << (8u * i); }
                pos += 8u;
                for (uint32_t i = 0; i < NSTEP; i++)
                    if ((i < 32u ? lo >> i : hi >> (i - 32u)) & 1u) { p.step[i] = step_packed_get(raw + pos); pos += BANK_STEP_SIZE; }
                    else p.step[i] = empty;
            } else for (uint32_t i = 0; i < NSTEP; i++) p.step[i] = empty;
            pattern_write(k, b, &p);
        }
    }
    scale_share(TSEL);                                  /* the selected MIDI track's key is available only now */
    pattern_loading = 0;
    motion_unguard(f);
}
/* Only after bank_valid and project_restore_runtime. */
static void bank_restore_tail(const uint8_t *raw)
{
    if (bank_wide) { bankl_restore_tail(raw); return; }
    uint32_t pos = (BANK_EXTRA_OFF - BANK_ACTIVE_OFF), f = motion_guard();
    pattern_loading = 1;
    chain_from_legacy(&proj_scratch.chain);
    memset(chain_patterns, 0, sizeof chain_patterns);
    for (uint32_t r = 0; r < CHAIN_LEGACY_ROWS; r++)
        for (uint32_t k = 0; k < PROJ_TRK; k++) chain_patterns[r][k] = raw[(BANK_CHAIN_OFF - BANK_ACTIVE_OFF) + r * PROJ_TRK + k];
    memcpy(motion_pattern, bank_import_tags, sizeof motion_pattern);
    for (uint32_t k = 0; k < PROJ_TRK; k++) {
        trk[k].pattern = raw[0u + k]; trk[k].pattern_next = 0xff; trk[k].pattern_gen++;
        for (uint32_t b = 0; b < NPAT; b++) {
            pattern_t p;
            step_t empty = {{0}, 0, ST_REST};
            for (uint32_t i = 0; i < 4u; i++) p.timing[i] = b < NPAT_V1 ? raw[(BANK_TIMING_OFF - BANK_ACTIVE_OFF) + (k * NPAT_V1 + b) * 4u + i] : TP[P_SLEN + i].def;
            if (b == trk[k].pattern) memcpy(p.step, trk[k].step, sizeof p.step);
            else if (b >= NPAT_V1) for (uint32_t i = 0; i < NSTEP; i++) p.step[i] = empty;
            else for (uint32_t i = 0; i < NSTEP; i++, pos += BANK_STEP_SIZE) step_wire_unpack(&p.step[i], raw + pos);
            pattern_write(k, b, &p);
        }
    }
    pattern_loading = 0;
    motion_unguard(f);
}


static void bank_restore(const uint8_t *raw){bank_restore_tail(raw+BANK_ACTIVE_OFF);}
