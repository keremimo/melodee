/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#define MELODEE_FAVORITES 1
/* Settings' bytes kept with the favorites (settings_persist.c), and the factory presets' stars by synth ID:
 *   rows 9..13 the stars (FAV_STAR_ROW below; Melodee 2.0)
 *   row 8  4..7 tagged USB VOLUME, 8..15 GLO > WEIGHTS (settings_persist.c), 16..25 the FX key map, 28..31 its tag
 *          (below), 26 tagged VELOCITY (SMK-37, settings_persist.c)
 *   row 14 16..27 the scale favorites, 28..31 their tag
 *   row 15 26 tagged USB INPUT (settings_persist.c), 27 SCREEN OFF, 28 tagged AUTOSAVE OFF, 29 the PRESETS LIST
 *          (browse.c), 30 bit 1 KNOB ACCEL OFF, bit 5 PICKER OFF, bit 6 FONT PIXEL (ui.c PREF_BITS), 31 the quick
 *          layers seen (ui_layer.c), 25 HOME SELECT (ui.c HOME_SELECT), 24 bits 1..4 GLO SELECT (ui.c GLO_SELECT), 24
 *          bit 5 UNUSED TRACKS HIDE (ui.c TRACKS_HIDE), 24 bit 6 TRS MIDI OUT OFF (SMK-37, ui.c TRS_OUT_OFF), 24 bit 7
 *          MIDI THRU ON (fm1-base, ui.c MIDI_THRU_ON)
 *   row 1  24..27 tagged CLICK LEVEL (CK + version 1 + percent); 28..31 mark validated Melodee settings (MDS + version 1)
 *   row 4  MIDI LEARN's table when bytes 28..31 hold its tag (midi_knobs.h)
 *   rows 5..7 MIDI LEARN's parameters when row 7's bytes 28..31 hold their tag (midi_knobs.h mparam_row)
 * Every other byte is spare (zero). */
typedef struct {
    uint8_t factory[16][32]; /* settings' bytes (above), the stars by synth ID in rows 9..13 (below) */
    uint32_t user, filter;
} favorites_t;
static favorites_t favorites;
static uint32_t favorites_user_hi;
/* Scale favorites use the upper half of retired engine 14's PER5 row. Keep
 * old preset bits 0..127 intact; tagged storage ignores unrecognized bytes.
 * No settings/template layout change. */
#include "scales.h"
#define SCALE_FAV_CAP 96u
static int scale_fav_valid(void)
{
    const uint8_t *p = favorites.factory[14];
    return p[28] == 'S' && p[29] == 'C' && p[30] == 'L' && p[31] == 1;
}
static uint32_t scale_fav_id(uint32_t scale)    /* a maqam's star stays where its retired entry had it */
{
    return scale >= SCALE_MICRO_END && scale < SCALE_TOTAL ? scale - SCALE_MICRO_END + SCALE_MAQAM_OLD : scale;
}
static int scale_favorite(uint32_t scale)
{
    scale = scale_fav_id(scale);
    return scale < SCALE_FAV_CAP && scale_fav_valid() &&
        ((favorites.factory[14][16u + scale / 8u] >> (scale % 8u)) & 1u);
}
static int scale_favorite_set(uint32_t scale, int on)
{
    scale = scale_fav_id(scale);
    if (scale >= SCALE_FAV_CAP || scale_favorite(scale) == !!on) return 0;
    uint8_t *p = favorites.factory[14];
    if (!scale_fav_valid()) {
        memset(p + 16, 0, 12);
        p[28] = 'S'; p[29] = 'C'; p[30] = 'L'; p[31] = 1;
    }
    if (on) p[16u + scale / 8u] |= (uint8_t)(1u << (scale % 8u));
    else p[16u + scale / 8u] &= (uint8_t)~(1u << (scale % 8u));
    return 1;
}
/* Melodee 2.0: the factory presets' stars by synth ID, rows FAV_STAR_ROW.. (1.x's stars there and in the other rows
 * by engine number mean nothing now: the web installer converts them). Entries packed from the start: the synth's ID
 * (8 bytes, little endian), n, then n bytes of stars (preset k: bit k % 8 of byte k / 8); a zero ID ends them */
#define FAV_STAR_ROW 9u
#define FAV_STAR_BYTES (5u * 32u)                    /* rows 9..13 */
static uint8_t *fav_star_store(void) { return favorites.factory[FAV_STAR_ROW]; }
/* engine e's entry: its offset (the store's end with *n 0: none, where a new one goes) */
static uint32_t fav_star_find(uint32_t e, uint32_t *n)
{
    const uint8_t *b = fav_star_store();
    uint64_t uid = e < NENGINES ? eng_map[e].uid : 0u;
    uint32_t at = 0;
    for (*n = 0; at + 9u <= FAV_STAR_BYTES && mod_le64(b + at); at += 9u + b[at + 8u])
        if (uid && mod_le64(b + at) == uid && at + 9u + b[at + 8u] <= FAV_STAR_BYTES)
            return *n = b[at + 8u], at;
    return at;
}
static int fav_star_has(uint32_t e, uint32_t preset)
{
    uint32_t n, at = fav_star_find(e, &n);
    return preset / 8u < n && (fav_star_store()[at + 9u + preset / 8u] >> (preset % 8u) & 1u);
}
static int fav_star_set(uint32_t e, uint32_t preset, int on)
{
    uint8_t *b = fav_star_store();
    uint32_t n, at = fav_star_find(e, &n), need = (eng_npresets(e) + 7u) / 8u;
    if (!n) {                                       /* (a new entry: entries with no star left go first) */
        if (!on || e >= NENGINES || !eng_map[e].uid || preset / 8u >= need)
            return 0;
        for (uint32_t k = 0; k + 9u <= FAV_STAR_BYTES && mod_le64(b + k);) {
            uint32_t m = b[k + 8u], any = 0;
            for (uint32_t i = 0; i < m && k + 9u + i < FAV_STAR_BYTES; i++)
                any |= b[k + 9u + i];
            if (any) { k += 9u + m; continue; }
            for (uint32_t i = k; i + 9u + m < FAV_STAR_BYTES; i++)   /* (down: no memmove on the target) */
                b[i] = b[i + 9u + m];
            memset(b + FAV_STAR_BYTES - 9u - m, 0, 9u + m);
        }
        at = fav_star_find(e, &n);
        if (at + 9u + need > FAV_STAR_BYTES)
            return 0;                               /* (no room: the star is not kept) */
        mod_put64(b + at, eng_map[e].uid);
        b[at + 8u] = (uint8_t)(n = need);
    }
    if (preset / 8u >= n)
        return 0;
    if (on) b[at + 9u + preset / 8u] |= (uint8_t)(1u << (preset % 8u));
    else b[at + 9u + preset / 8u] &= (uint8_t)~(1u << (preset % 8u));
    return 1;
}
static int favorite_has(uint32_t engine, uint32_t preset)
{
    if(engine==USER_NATIVE_P5 || engine==USER_NATIVE_FM || engine==USER_NATIVE_CZ)engine=USER_GENERAL;
    if (engine == USER_GENERAL)
        return up_favorite(preset);
    return engine < NENGINES && preset < 256u && fav_star_has(engine, preset);
}
static int favorite_set(uint32_t engine, uint32_t preset, int on)
{
    if(engine==USER_NATIVE_P5 || engine==USER_NATIVE_FM || engine==USER_NATIVE_CZ)engine=USER_GENERAL;
    if (engine == USER_GENERAL)
        return preset < UP_SLOTS && favorite_has(engine, preset) != !!on && up_favorite_set(preset, on);
    if (engine >= NENGINES || preset >= 256u || favorite_has(engine, preset) == !!on)
        return 0;
    return fav_star_set(engine, preset, on);
}
