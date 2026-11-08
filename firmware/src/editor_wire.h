/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Editor protocol: the request / reply helpers every command shares (EDITOR_PROTOCOL.md "Paging and argument
 * checks"). They need only ed_b (editor.c, or a test's own): editor.c includes this file, and so does each editor
 * file a test builds on its own (editor_backup.c, editor_preferences.c, editor_ble.c). */
#ifndef EDITOR_WIRE_H
#define EDITOR_WIRE_H
static void ed_n7(uint32_t v, uint32_t k)        /* v in k 7-bit groups, low first: 2 = 14 bits, 5 = 32 bits */
{
    for (; k; k--, v >>= 7)
        ed_b(v);
}
#define ed_u14(v) ed_n7((uint32_t)(v), 2u)
#define ed_u35(v) ed_n7((uint32_t)(v), 5u)
static void ed_u35s(const uint32_t *v, uint32_t n)   /* n words, 5 x 7 bits each (diagnostics' counters) */
{
    while (n--)
        ed_u35(*v++);
}
static void ed_str(const char *s, uint32_t max)   /* ASCII, 0-terminated */
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        ed_b((uint8_t)s[i] & 0x7Fu);
    ed_b(0);
}
static void ed_bytes(const uint8_t *p, uint32_t n)
{
    while (n--)
        ed_b(*p++);
}
/* bytes as nibble pairs, low first (native CZ-1 tones): out, and in (0 if a nibble is above 15) */
static void ed_nibbles(const uint8_t *p, uint32_t n)
{
    for (; n; n--, p++) {
        ed_b(*p & 15u);
        ed_b(*p >> 4);
    }
}
static int ed_unnibble(uint8_t *out, const uint8_t *a, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++, a += 2) {
        if (a[0] > 15u || a[1] > 15u)
            return 0;
        out[i] = (uint8_t)(a[0] | a[1] << 4);
    }
    return 1;
}

/* ---- requests */
static uint32_t ed_arg(const uint8_t *a, uint32_t n, uint32_t i) { return i < n ? a[i] : 127u; }  /* 127: absent */
static void ed_echo(const uint8_t *a, uint32_t n, uint32_t k)    /* a reply's first k bytes: the request's */
{
    for (uint32_t i = 0; i < k; i++)
        ed_b(ed_arg(a, n, i));
}
/* a name as text: n bytes, each printable ASCII (32..126), copied 0-terminated into dst (n + 1 bytes); 0 if not */
static int ed_text(char *dst, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] < 32u || s[i] > 126u)
            return 0;
        dst[i] = (char)s[i];
    }
    dst[n] = 0;
    return 1;
}
/* a stored name shown: n bytes, anything not printable a space, 0-terminated into dst (n + 1 bytes) */
static void ed_shown(char *dst, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        dst[i] = s[i] >= 32u && s[i] <= 126u ? (char)s[i] : ' ';
    dst[n] = 0;
}
/* paged replies: how many of `want` items from `first` exist in `total` (editor.c ed_left: the room for them) */
static uint32_t ed_page(uint32_t first, uint32_t want, uint32_t total)
{
    return first >= total ? 0u : want > total - first ? total - first : want;
}
#endif
