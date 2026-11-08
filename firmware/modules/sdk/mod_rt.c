/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* What the compiler may call by itself in a module (struct copies, cleared arrays): a module links nothing of the
 * firmware's, so it carries these few bytes. mod_entry_ is the link's entry (never called). */
#include <stdint.h>
#include <stddef.h>
void *memset(void *d, int c, size_t n)
{
    uint8_t *p = d;
    while (n--)
        *p++ = (uint8_t)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *p = d;
    const uint8_t *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}
void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *p = d;
    const uint8_t *q = s;
    if (p < q)
        while (n--)
            *p++ = *q++;
    else
        while (n--)
            p[n] = q[n];
    return d;
}
void mod_entry_(void) {}
