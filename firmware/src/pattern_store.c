/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The project record (Melodee 2.0: FBKS, the only one it reads or writes; every record before it is a Melodee 1.x one,
 * the web installer converts those): its magic and length, the FUN record (project.c, the active banks), then the bank
 * tail: each track's active bank, every bank's timing, the song's columns, the motion tags, the FM6 function settings,
 * every owner's timed-note count, each MIDI-only track's parameters and MPC octave; the song (64 sections, rests),
 * the punch events, the song-local sounds and their per-pattern IDs (the trailer's flags); then the banks the FUN
 * record does not hold, sparse: per track two bytes (bit b: bank b follows), per bank its 64-bit step mask and the
 * steps set in it (step_packed_put); then the end block before the sum: an entry (handle, ID, name) for every synth
 * the record names (a handle no entry names: none) and the patches of its parts whose synth has no Melodee 1.x store
 * format (bank_ext_*). Music that does not fit is not written (PROJECT FULL). Its offsets grew from the 1.x records' */
#define BANKS_MAGIC 0x534B4246u                 /* "FBKS" */
#define BANKR_TAG 0x32584D45u                   /* "EMX2": the end block's last word (its entries name synths by ID) */
#define BANKR_TRAIL 12u                         /* its trailer: payload bytes u16, entries, patches, flags (1: FBKQ), 0 x 3,
                                                 * the tag */
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
_Static_assert(BANK_FN_OFF + 4u + PROJ_TRK * PROJ_FN_BYTES <= BANK_STORE_SIZE - 4u, "bank project extent");
#define BANKL_TIMING_OFF (BANK_ACTIVE_OFF + NTRK)
#define BANKL_CHAIN_OFF (BANKL_TIMING_OFF + NTRK * NPAT_V1 * BANK_TIME_SIZE)
#define BANKL_MOTION_OFF (BANKL_CHAIN_OFF + CHAIN_LEGACY_ROWS * NTRK)
#define BANKL_FN_OFF (BANKL_MOTION_OFF + MOTION_MAX)
#define BANKL_REC_OFF (BANKL_FN_OFF + 4u + PROJ_TRK * PROJ_FN_BYTES)
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
#define BANKP_REC_OFF (BANKP_FN_OFF + 4u + PROJ_TRK * PROJ_FN_BYTES)
#define BANKP_TRK_OFF (BANKP_REC_OFF + BANKP_REC_BYTES)
#define BANKP_STEPS_OFF (BANKP_TRK_OFF + NMIDI * BANKL_TRK_BYTES)
/* the record's layout (FBKP's, which FBKS keeps: NPAT a track, two mask bytes) */
typedef struct { uint32_t npat, timing, chain, motion, fn, rec, recn, trk, steps, mask; } bank_lay_t;
static const bank_lay_t BL = {NPAT, BANKL_TIMING_OFF, BANKP_CHAIN_OFF, BANKP_MOTION_OFF, BANKP_FN_OFF, BANKP_REC_OFF,
                              BANKP_REC_BYTES, BANKP_TRK_OFF, BANKP_STEPS_OFF, 2u};
static uint8_t bank_ext;                                /* FBKR: the record has an end block */
static uint32_t bank_ext_len;                           /* .. its bytes, trailer included (the steps end before it) */
static const uint8_t *bank_blob[PROJ_TRK];              /* the record read last: its parts' patches in its end block
                                                         * (pointers into it), 0 none */
static uint16_t bank_blob_len[PROJ_TRK];
static uint8_t bank_blob_eng[PROJ_TRK];
static uint8_t bank_patterns;
static uint8_t bank_sounds, bank_sound_count, bank_sound_ids[CHAIN_SOUNDS];
static uint32_t bank_sound_bytes;
static uint32_t bank_sound_off(void) { return BL.steps - BANK_ACTIVE_OFF + BANKM_SONG_BYTES + BANKN_FX_BYTES; }

static uint32_t bank_pattern_off(void) { return bank_sound_off() + bank_sound_bytes; }
static uint32_t bank_steps_off(void) { return BL.steps + BANKM_SONG_BYTES + BANKN_FX_BYTES + bank_sound_bytes + (bank_patterns ? BANKQ_PATTERN_BYTES : 0u); }
_Static_assert(BANKL_STEPS_OFF + NTRK <= BANK_STORE_SIZE - 4u && (NTRK - PROJ_TRK) * NPAT_V1 <= 32u && NSTEP == 64u,
               "FBKL: its fixed fields, one more directory of owners, a 64-bit step mask");
_Static_assert(BANKP_STEPS_OFF + BANKM_SONG_BYTES + BANKN_FX_BYTES + 2u * NTRK <= BANK_STORE_SIZE - 4u && NPAT <= 16u,
               "FBKP: its fixed fields and two mask bytes a track");
#define BT(off) ((off) - BANK_ACTIVE_OFF)               /* an offset into the tail */
static uint8_t bank_too_full;                           /* the music did not fit an FBKL record */
static int bank_full(uint32_t n) { return n == BANK_STORE_SIZE; }
static uint32_t bank_magic(void) { return BANKS_MAGIC; }
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
}
static uint32_t bank_motion_off(void) { return BL.motion; }
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
/* ---- FBKR's end block: what a project of another device's (or another author's) synths needs to come back as it was
 *   entries   n x EMAP_ENTRY (eng_map.c): the numbers it names that are not Melodee's defaults, who they are
 *   patches   per part whose synth has no Melodee 1.x store format and a patch: part, engine, length u16, the bytes
 *   trailer   BANKR_TRAIL bytes, the last of them the tag */
/* engine e is named by the project captured: a part's, a song section's or a pattern's sound */
static int bank_names(const project_t *q, uint32_t e)
{
    for (uint32_t k = 0; k < PROJ_TRK; k++)
        if (q->t[k].engine == e)
            return 1;
    for (uint32_t j = 0; j < bank_sound_count; j++) {
        const song_sound_t *s = song_sound_get(bank_sound_ids[j]);
        if (s && s->engine == e)
            return 1;
    }
    return 0;
}
/* part k's patch the end block keeps (its synth has no 1.x store format): its bytes, *len, *e; 0 none. A capture: the
 * part's own; else (a record re-packed: the backup's paths) the one the record read last had */
static const uint8_t *bank_ext_patch(const project_t *q, uint32_t k, int runtime, uint32_t *len)
{
    uint32_t e = q->t[k].engine, src = runtime ? cap_map(k) : k, n = patch_len(e);
    if (!n && runtime && src < NPART && patch_eng_of(src) == e)
        n = patch_kept(src);                        /* (a synth this device has not got: the bytes it came with) */
    if (!n || patch_store_tag(e) != MOD_SCH_STORE_NONE)
        return 0;
    if (runtime)
        return src < NPART && patch_eng_of(src) == e ? (*len = n, patch_of(src)) : 0;
    return bank_blob[k] && bank_blob_eng[k] == e ? (*len = bank_blob_len[k], bank_blob[k]) : 0;
}
/* the end block a capture needs (bank_ext, bank_ext_len): 0 none */
static void bank_ext_plan(const project_t *q, int runtime)
{
    uint32_t n = 0;
    for (uint32_t e = 0; e < NENGINES; e++)
        if (eng_map_own(e) && bank_names(q, e))
            n += EMAP_ENTRY;
    for (uint32_t k = 0, len; k < PROJ_TRK; k++)
        if (bank_ext_patch(q, k, runtime, &len))
            n += 4u + len;
    bank_ext = 1;                                   /* (2.0: always, the synths' entries) */
    bank_ext_len = n + BANKR_TRAIL;
}
static void bank_ext_write(uint8_t *raw, const project_t *q, int runtime)
{
    uint8_t *b = raw + BANK_TAIL_SIZE - 4u - bank_ext_len, ne = 0, np = 0;
    uint32_t pos = 0, len;
    if (!bank_ext)
        return;
    for (uint32_t e = 0; e < NENGINES; e++)
        if (eng_map_own(e) && bank_names(q, e)) {
            eng_map_put(b + pos, e);
            pos += EMAP_ENTRY;
            ne++;
        }
    for (uint32_t k = 0; k < PROJ_TRK; k++) {
        const uint8_t *p = bank_ext_patch(q, k, runtime, &len);
        if (!p)
            continue;
        b[pos] = (uint8_t)k; b[pos + 1u] = q->t[k].engine; b[pos + 2u] = (uint8_t)len; b[pos + 3u] = (uint8_t)(len >> 8);
        memcpy(b + pos + 4u, p, len);
        pos += 4u + len;
        np++;
    }
    b[pos] = (uint8_t)pos; b[pos + 1u] = (uint8_t)(pos >> 8); b[pos + 2u] = ne; b[pos + 3u] = np;
    b[pos + 4u] = bank_patterns; b[pos + 5u] = b[pos + 6u] = b[pos + 7u] = 0;
    memcpy(b + pos + 8u, &(uint32_t){BANKR_TAG}, 4);
}
/* an end block's trailer t (room: bytes before it at most): its length, entries' and patches' counts; 0 bad */
static uint32_t bank_ext_head(const uint8_t *t, uint32_t room)
{
    uint32_t tag, plen = t[0] | (uint32_t)t[1] << 8;
    memcpy(&tag, t + 8, 4);
    return tag == BANKR_TAG && t[4] <= 1u && !t[5] && !t[6] && !t[7] && plen <= room && t[3] <= PROJ_TRK &&
           t[2] * EMAP_ENTRY <= plen ? plen + BANKR_TRAIL : 0u;
}
/* its entries (the remap) and patches (bank_blob) from the trailer t that bank_ext_head passed; 0 bad. The template's
 * end block (project.c tmpl_ext_load) is read here too */
static int bank_ext_body(const uint8_t *t)
{
    uint32_t plen = t[0] | (uint32_t)t[1] << 8, ne = t[2], np = t[3], pos;
    const uint8_t *b = t - plen;
    if (ne && eng_remap_read(b, ne))            /* (none: the remap as it is, the template's: project.c tmpl_remap) */
        return 0;
    pos = ne * EMAP_ENTRY;
    for (uint32_t i = 0; i < np; i++) {
        uint32_t k, len;
        if (pos + 4u > plen || (k = b[pos]) >= PROJ_TRK || bank_blob[k] || b[pos + 1u] >= NENGINES ||
            (len = b[pos + 2u] | (uint32_t)b[pos + 3u] << 8) > MOD3_PATCH_MAX || pos + 4u + len > plen)
            return 0;
        bank_blob[k] = b + pos + 4u;
        bank_blob_len[k] = (uint16_t)len;
        bank_blob_eng[k] = (uint8_t)eng_in(b[pos + 1u]);
        pos += 4u + len;
    }
    return pos == plen;
}
static void bank_ext_none(void)                  /* no end block: the identity remap, no patches */
{
    eng_remap_reset();
    bank_ext = 0;
    bank_ext_len = 0;
    for (uint32_t k = 0; k < PROJ_TRK; k++)
        bank_blob[k] = 0;
}
/* a record's end block (tail: its bank tail; fbkr: its magic was FBKR): checked, the engine map's remap set for the
 * record's engine bytes (eng_map.c eng_in: new numbers taken for synths this device has not got), its patches noted.
 * Any other record: no end block, the identity remap. 0 bad */
static int bank_ext_read(const uint8_t *tail, int fbkr)
{
    const uint8_t *t = tail + BANK_TAIL_SIZE - 4u - BANKR_TRAIL;
    uint32_t n;
    bank_ext_none();
    if (!fbkr)
        return 1;
    if (!(n = bank_ext_head(t, BANK_TAIL_SIZE / 2u)))
        return 0;
    bank_patterns = t[4];
    bank_ext = 1;
    bank_ext_len = n;
    return bank_ext_body(t);
}
static int bankl_pack_tail(uint8_t *raw, const project_t *q)   /* runtime only (cap_map) */
{
    uint32_t pos = BT(bank_steps_off()), f = motion_guard();
    step_t empty = {{0}, 0, ST_REST};
    memset(raw, 0, BANK_TAIL_SIZE);
    if (pos + NTRK > BANK_TAIL_SIZE - 4u - bank_ext_len) { bank_too_full = 1; motion_unguard(f); return 0; }
    if (bank_sounds) {
        uint8_t *out = raw + bank_sound_off();
        out[0] = bank_sound_count; out[1] = P_COUNT;
        out[2] = (uint8_t)sizeof(song_sound_t); out[3] = (uint8_t)(sizeof(song_sound_t) >> 8);
        for (uint32_t j = 0; j < bank_sound_count; j++) {
            const song_sound_t *sound = song_sound_get(bank_sound_ids[j]);
            if (!sound || !song_sound_valid(sound, 0)) { motion_unguard(f); return 0; }
            {
                song_sound_t st;
                song_sound_stored(&st, sound);         /* (its synth's store form) */
                memcpy(out + BANKO_HEAD_BYTES + j * sizeof *sound, &st, sizeof st);
            }
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
    {
        uint8_t *song = raw + BT(BL.steps);
        song[0] = chain_config.count;
        song[1] = chain_config.rsv[CHAIN_LOOP];
        song[2] = bank_sounds;                          /* (sounds follow) */
        for (uint32_t r = 0; r < CHAIN_ROWS; r++) {
            song[4u + r * (NTRK + 1u)] = chain_config.row[r].repeat;
            for (uint32_t k = 0; k < NTRK; k++)
                song[5u + r * (NTRK + 1u) + k] = cap_map(k) < NTRK ? chain_patterns[r][cap_map(k)] : 0u;
        }
    }
    memcpy(raw + BT(BL.steps) + BANKM_SONG_BYTES, chain_config.fx, BANKN_FX_BYTES);
    if (pos + NTRK * BL.mask > BANK_TAIL_SIZE - 4u - bank_ext_len) { bank_too_full = 1; motion_unguard(f); return 0; }
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
            if (pos + 8u + n * BANK_STEP_SIZE > BANK_TAIL_SIZE - 4u - bank_ext_len) { bank_too_full = 1; motion_unguard(f); return 0; }
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
    if (q->voice_fn_ok) {
        uint32_t m = BANK_FN_MAGIC;
        memcpy(raw + BT(BL.fn), &m, 4);
        memcpy(raw + BT(BL.fn) + 4u, q->voice_fn, sizeof q->voice_fn);
    }
    /* the timed notes' counts: every owner's (in their order in the FUN record) */
    for (uint32_t o = 0; o < proj_owners(); o++) {
        uint32_t dir = o * 11u;
        wire_bits_put(raw + BT(BL.rec), &dir, bank_rec_count(q, o), 11);
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
static int bank_pack_tail(uint8_t *raw,const project_t *q,int runtime)
{
    if (runtime && !chain_valid(&chain_config)) return 0;
    bank_capture_kind(runtime);
    bank_too_full = 0;
    bank_ext_plan(q, runtime);
    if (!bankl_pack_tail(raw, q)) return 0;
    bank_ext_write(raw, q, runtime);
    return 1;
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
            for (k = 0; k < PROJ_TRK; k++)               /* (as 1.x checked them: the synth's schema, MOD3_R_CHECK) */
                if (!patch_settings_ok(MOD_SCH_STORE_VOICE, f + k * PROJ_FN_BYTES))
                    return 0;
            memcpy(proj_scratch.voice_fn, f, sizeof proj_scratch.voice_fn);
            proj_scratch.voice_fn_ok = 1;
            proj_scratch.sum = proj_sum(&proj_scratch);
        }
    }
    return 1;
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
    if (raw[BT(BL.steps) + 2u] > 1u) return 0;
    bank_sounds = raw[BT(BL.steps) + 2u];
    if (bank_patterns && !bank_sounds) return 0;
    if (bank_sounds) {
        const uint8_t *in = raw + bank_sound_off();
        uint32_t count = in[0], size = in[2] | (uint32_t)in[3] << 8;
        if (!count || count > CHAIN_SOUNDS || in[1] != P_COUNT || size != sizeof(song_sound_t)) return 0;
        bank_sound_bytes = BANKO_HEAD_BYTES + count * size;
        if (bank_sound_off() + bank_sound_bytes + (bank_patterns ? BANKQ_PATTERN_BYTES : 0u) + NTRK * BL.mask > BANK_TAIL_SIZE - 4u - bank_ext_len) return 0;
        uint8_t used[CHAIN_SOUNDS] = {0};
        for (uint32_t r = 0; r < CHAIN_ROWS; r++) for (uint32_t k = 0; k < NTRK; k++) {
            uint32_t id = in[4u + r * NTRK + k];
            if (id > count || (r >= raw[BT(BL.steps)] && id)) return 0;
            if (id) {
                const song_sound_t *sound = (const song_sound_t *)(in + BANKO_HEAD_BYTES + (id - 1u) * size);
                if (k >= NPART && eng_in(sound->engine) != ENGI_MIDI) return 0;   /* (its handle as the record names it) */
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
                if (k >= NPART && eng_in(sound->engine) != ENGI_MIDI) return 0;   /* (its handle as the record names it) */
                used[id - 1u] = 1; any = 1;
            }
            if (!any) return 0;
        }
        for (uint32_t j = 0; j < count; j++)
            if (!used[j] || !song_sound_valid((const song_sound_t *)(in + BANKO_HEAD_BYTES + j * size), 1)) return 0;
    }
    uint32_t pos = BT(bank_steps_off());
    {
        const uint8_t *song = raw + BT(BL.steps);
        if (song[0] > CHAIN_ROWS || song[1] > 1u || song[3] ||
            proj_scratch.chain.count != (song[0] < CHAIN_LEGACY_ROWS ? song[0] : CHAIN_LEGACY_ROWS)) return 0;
        for (uint32_t r = 0; r < CHAIN_ROWS; r++) {
            uint32_t rep = song[4u + r * (NTRK + 1u)];
            if (!rep || rep > 16u) return 0;
            for (uint32_t k = 0; k < NTRK; k++) {
                uint32_t p = song[5u + r * (NTRK + 1u) + k], v = p;
                if (v != CHAIN_REST && v != CHAIN_HOLD && v >= trk_npat(k)) return 0;
                if (v == CHAIN_HOLD && (!r || song[5u + (r-1u) * (NTRK + 1u) + k] == CHAIN_REST)) return 0;
                if (r < CHAIN_LEGACY_ROWS && raw[BT(BL.chain) + r * NTRK + k] != (p < BL.npat ? p : 0u)) return 0;
                if (!k && r < proj_scratch.chain.count &&
                    (proj_scratch.chain.row[r].repeat != rep || proj_scratch.chain.row[r].slot != (p < BL.npat ? p : 0u))) return 0;
            }
        }
    }
    {
        const uint8_t *events = raw + BT(BL.steps) + BANKM_SONG_BYTES;
        uint32_t count = raw[BT(BL.steps)];
        for (uint32_t r = 0; r < CHAIN_ROWS; r++) for (uint32_t j = 0; j < CHAIN_FX_EVENTS; j++) {
            chain_fx_t e; memcpy(&e, events + (r * CHAIN_FX_EVENTS + j) * 3u, sizeof e);
            if (!chain_fx_valid(&e) || (r >= count && e.fx)) return 0;
        }
    }
    for (uint32_t k = 0; k < NTRK; k++) {
        uint32_t active = raw[k], present = bank_present(raw, pos), have = trk_npat(k);
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
            if (pos + 8u > BANK_TAIL_SIZE - 4u - bank_ext_len) return 0;
            uint32_t n = 0;
            for (uint32_t i = 0; i < 8u; i++) n += (uint32_t)__builtin_popcount(raw[pos + i]);
            pos += 8u;
            if (!n || pos + n * BANK_STEP_SIZE > BANK_TAIL_SIZE - 4u - bank_ext_len) return 0;
            for (uint32_t i = 0; i < n; i++, pos += BANK_STEP_SIZE) if (!bankl_step_ok(raw + pos)) return 0;
        }
        if (k >= PROJ_TRK) {
            const uint8_t *b = raw + BT(BL.trk) + (k - PROJ_TRK) * BANKL_TRK_BYTES;
            for (uint32_t i = 0; i < P_COUNT; i++) if (b[i] > 191u) return 0;
            if (b[P_COUNT] > 6u) return 0;
        }
    }
    for (; pos < BANK_TAIL_SIZE - 4u - bank_ext_len; pos++) if (raw[pos]) return 0;
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
static int bank_tail_valid(const uint8_t *raw) { return bankl_tail_valid(raw); }
/* the FUN record's timed notes past its own directory (FBKL), before it is read */
static void bank_rec_ext(const uint8_t *tail)
{
    memcpy(proj_rec_ext, tail + BT(BL.rec), BL.recn);
}
static int bank_valid(const uint8_t *raw,uint32_t len)
{
    uint32_t magic,size,sum;
    if(!bank_full(len))return 0;
    memcpy(&magic,raw,4);memcpy(&size,raw+4,4);memcpy(&sum,raw+len-4u,4);
    if(magic!=BANKS_MAGIC||size!=len||sum!=proj_hash(raw,len-4u))return 0;   /* (a 1.x record: the web installer's) */
    if(!bank_ext_read(raw+BANK_ACTIVE_OFF,1))return 0;   /* (its end block: the synths it names, its patches) */
    bank_sounds=0;bank_sound_bytes=0;bank_rec_ext(raw+BANK_ACTIVE_OFF);
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
    {
        const uint8_t *song = raw + BT(BL.steps);
        chain_config.count = song[0];
        chain_config.rsv[CHAIN_LOOP] = song[1];
        for (uint32_t r = 0; r < CHAIN_ROWS; r++) {
            chain_config.row[r].repeat = song[4u + r * (NTRK + 1u)];
            for (uint32_t k = 0; k < NTRK; k++) chain_patterns[r][k] = song[5u + r * (NTRK + 1u) + k];
            chain_config.row[r].slot = chain_patterns[r][0];
        }
    }
    if (bank_sounds) {
        const uint8_t *in = raw + bank_sound_off();
        song_sound_count = in[0];
        memcpy(chain_config.sound, in + 4u, CHAIN_ROWS * NTRK);
        memcpy(song_sound_pool(), in + BANKO_HEAD_BYTES, song_sound_count * sizeof(song_sound_t));
        for (uint32_t j = 0; j < song_sound_count; j++) {
            song_sound_pool()[j].engine = (uint8_t)eng_in(song_sound_pool()[j].engine);   /* (FBKR: the number here) */
            song_sound_ram(song_sound_pool() + j);   /* (its synth's own bytes) */
        }
    } else { song_sound_count = 0; resource_release(RES_SONG_SOUNDS); }
    if (bank_patterns) memcpy(pattern_sound, raw + bank_pattern_off(), sizeof pattern_sound);
    song_base_reserved = 0;
    memcpy(chain_config.fx, raw + BT(BL.steps) + BANKM_SONG_BYTES, BANKN_FX_BYTES);
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
        for (uint32_t b = 0; b < trk_npat(k); b++) {
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
static void bank_restore_tail(const uint8_t *raw) { bankl_restore_tail(raw); }

static void bank_restore(const uint8_t *raw){bank_restore_tail(raw+BANK_ACTIVE_OFF);}
