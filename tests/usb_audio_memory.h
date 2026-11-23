/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Standalone stream/SIE fixtures inject the arena boundary, with red zones
 * and an adjustable failure limit. Mixer fixtures use the real allocator. */
#include <stdint.h>
static struct { uint32_t before;int16_t data[3072];uint32_t after; } ua_test_memory={0x51a7beefu,{0},0xa17e5afeu};
static uint32_t ua_test_memory_bytes,ua_test_memory_limit=6144u;               /* (the stems bank: 16-bit halves + low bytes) */
static void *ua_test_memory_resize(uint32_t bytes)
{
    if(bytes>ua_test_memory_limit)return 0;
    if(bytes>ua_test_memory_bytes)memset((uint8_t *)ua_test_memory.data+ua_test_memory_bytes,0,bytes-ua_test_memory_bytes);
    ua_test_memory_bytes=bytes;return bytes?ua_test_memory.data:0;
}
#define UA_TRACKS_MEMORY(bytes) ua_test_memory_resize(bytes)
#define UA_MEMORY_GUARD_OFF() ((void)0)
#define UA_MEMORY_GUARD_ON() ((void)0)
