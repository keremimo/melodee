/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Juno-106 native tones: the module's patch (31 bytes, Saadet's j106_patch_t byte for byte). The sixteen slider bytes
 * and two switch bytes are kept in the order of the instrument's patch messages (F0 41 30 0n pp, sixteen sliders, SW1,
 * SW2, F7), then the patch number, for export, and a library name. Interpreting a byte never rewrites it: an imported
 * tone round-trips unchanged.
 *
 * The layout follows the instrument's published MIDI implementation:
 *   SW1 bit 0..2: 16', 8', 4' range; 3: pulse; 4: saw;
 *       bit 5: chorus off (0 = on); bit 6: chorus I (0 = II).
 *   SW2 bit 0: PWM manual (0 = LFO); 1: VCF envelope inverted;
 *       bit 2: VCA gate (0 = envelope); 3..4: HPF, 3 - value.
 * No other implementation's code or data is used. */
#pragma once
#include <stdint.h>
enum {
    J106_LFO_RATE, J106_LFO_DELAY, J106_DCO_LFO, J106_DCO_PWM, J106_NOISE,
    J106_VCF_FREQ, J106_VCF_RES, J106_VCF_ENV, J106_VCF_LFO, J106_VCF_KYBD,
    J106_VCA_LEVEL, J106_ENV_A, J106_ENV_D, J106_ENV_S, J106_ENV_R, J106_SUB,
    J106_SW1, J106_SW2, J106_RAW,
    /* panel fields past the bytes: the switch bits, edited in place (the schema's ids) */
    J106_RANGE = J106_RAW, J106_PULSE, J106_SAW, J106_CHORUS,
    J106_PWM_MODE, J106_ENV_POL, J106_VCA_MODE, J106_HPF, J106_FIELDS
};
#define J106_NAME_LEN 12u
#define J106_PROGRAM J106_RAW           /* the patch number's offset */
#define J106_NAME (J106_RAW + 1u)       /* the name's offset in the patch */
#define J106_BLOB (J106_NAME + J106_NAME_LEN)   /* the patch's bytes: 31 */
#define J106_FRAME 24u                  /* a patch message, F0 to F7 */
typedef struct {
    uint8_t raw[J106_RAW];
    uint8_t program;                    /* 0..127: A11..A88, B11..B88 */
    char name[J106_NAME_LEN];           /* ASCII, space-padded */
} j106_patch_t;
_Static_assert(sizeof(j106_patch_t) == J106_BLOB, "Juno-106 tone layout");

/* the switch bits as the panel shows them */
static uint32_t j106_range(const uint8_t *p)    /* 0 16', 1 8', 2 4' */
{
    uint32_t s = p[J106_SW1];
    return s & 1u ? 0u : s & 2u ? 1u : s & 4u ? 2u : 1u;
}
static uint32_t j106_chorus(const uint8_t *p)   /* 0 off, 1 I, 2 II */
{
    uint32_t s = p[J106_SW1];
    return s & 32u ? 0u : s & 64u ? 1u : 2u;
}
static uint32_t j106_hpf(const uint8_t *p) { return 3u - ((p[J106_SW2] >> 3) & 3u); }
static uint32_t j106_switch(const uint8_t *p, uint32_t field)
{
    switch (field) {
    case J106_RANGE: return j106_range(p);
    case J106_PULSE: return (p[J106_SW1] >> 3) & 1u;
    case J106_SAW: return (p[J106_SW1] >> 4) & 1u;
    case J106_CHORUS: return j106_chorus(p);
    case J106_PWM_MODE: return p[J106_SW2] & 1u;
    case J106_ENV_POL: return (p[J106_SW2] >> 1) & 1u;
    case J106_VCA_MODE: return (p[J106_SW2] >> 2) & 1u;
    case J106_HPF: return j106_hpf(p);
    }
    return 0;
}
/* A switch edit changes only its own bits. Chorus off keeps the I/II bit. */
static void j106_switch_set(uint8_t *p, uint32_t field, uint32_t v)
{
    uint8_t *s1 = &p[J106_SW1], *s2 = &p[J106_SW2];
    switch (field) {
    case J106_RANGE: *s1 = (uint8_t)((*s1 & ~7u) | (1u << (v > 2u ? 1u : v))); break;
    case J106_PULSE: *s1 = (uint8_t)((*s1 & ~8u) | (v ? 8u : 0u)); break;
    case J106_SAW: *s1 = (uint8_t)((*s1 & ~16u) | (v ? 16u : 0u)); break;
    case J106_CHORUS:
        if (!v) *s1 |= 32u;
        else *s1 = (uint8_t)((*s1 & ~96u) | (v == 1u ? 64u : 0u));
        break;
    case J106_PWM_MODE: *s2 = (uint8_t)((*s2 & ~1u) | (v ? 1u : 0u)); break;
    case J106_ENV_POL: *s2 = (uint8_t)((*s2 & ~2u) | (v ? 2u : 0u)); break;
    case J106_VCA_MODE: *s2 = (uint8_t)((*s2 & ~4u) | (v ? 4u : 0u)); break;
    case J106_HPF: *s2 = (uint8_t)((*s2 & ~24u) | ((3u - (v > 3u ? 3u : v)) << 3)); break;
    }
}

static void j106_patch_name_set(j106_patch_t *p, const char *name)
{
    for (uint32_t i = 0; i < J106_NAME_LEN; i++) p->name[i] = ' ';
    for (uint32_t i = 0; i < J106_NAME_LEN && name && name[i]; i++) p->name[i] = name[i];
}
/* the instrument's own label of patch k: A11..A88, B11..B88 */
static void j106_program_label(char *b, uint32_t k)
{
    b[0] = (char)(k & 64u ? 'B' : 'A');
    b[1] = (char)('1' + (k >> 3 & 7u));
    b[2] = (char)('1' + (k & 7u));
    b[3] = 0;
}
static const uint8_t J106_INIT[J106_RAW] = {
    64, 0, 0, 0, 0,  127, 0, 0, 0, 0,  64, 0, 60, 127, 30, 0,
    0x32,           /* 8', saw, chorus off */
    0x10            /* PWM by LFO, envelope +, VCA by envelope, HPF 1 */
};
static void j106_patch_init(j106_patch_t *p)
{
    for (uint32_t i = 0; i < J106_RAW; i++) p->raw[i] = J106_INIT[i];
    p->program = 0;
    j106_patch_name_set(p, "INIT TONE");
}
static int j106_patch_valid(const j106_patch_t *p)
{
    for (uint32_t i = 0; i < J106_RAW; i++) if (p->raw[i] > 127u) return 0;
    if (p->program > 127u) return 0;
    for (uint32_t i = 0; i < J106_NAME_LEN; i++) if ((uint8_t)p->name[i] < 32u || (uint8_t)p->name[i] > 126u) return 0;
    return 1;
}

/* One exact patch message (F0 41 30|31 0n pp, 18 data bytes, F7). Atomic on error. A manual-mode message (31) has no
 * stored patch number: it names the tone MANUAL; a patch message names it by the instrument's label. */
static int j106_patch_decode(j106_patch_t *out, const uint8_t *f, uint32_t n)
{
    j106_patch_t p;
    char name[10] = "PATCH ";
    if (!f || n != J106_FRAME || f[0] != 0xF0u || f[1] != 0x41u || (f[2] != 0x30u && f[2] != 0x31u) ||
        f[3] > 15u || f[n - 1u] != 0xF7u) return 0;
    for (uint32_t i = 4; i < n - 1u; i++) if (f[i] > 127u) return 0;
    for (uint32_t i = 0; i < J106_RAW; i++) p.raw[i] = f[5u + i];
    p.program = f[2] == 0x30u ? f[4] : 0u;
    j106_program_label(name + 6, p.program);
    j106_patch_name_set(&p, f[2] == 0x30u ? name : "MANUAL");
    *out = p;
    return 1;
}
static uint32_t j106_patch_encode(const j106_patch_t *p, uint32_t channel, uint8_t *f, uint32_t cap)
{
    if (cap < J106_FRAME || !j106_patch_valid(p)) return 0;
    f[0] = 0xF0u; f[1] = 0x41u; f[2] = 0x30u; f[3] = (uint8_t)(channel & 15u); f[4] = p->program;
    for (uint32_t i = 0; i < J106_RAW; i++) f[5u + i] = p->raw[i];
    f[J106_FRAME - 1u] = 0xF7u;
    return J106_FRAME;
}
