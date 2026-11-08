/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* An SDK module's schema as its SCHM chunk (mod_schema.h): its source built for the host with this file around it
 * (mod_build.py: -DMOD_HOSTED -DMOD_DUMP -DMOD_SOURCE="its.c"), printed for the builder: "S bytes hex" */
#include <stdio.h>
#include <string.h>
#include "mod_schema.h"
#include MOD_SOURCE
int main(void)
{
    static uint8_t b[16384];
    uint32_t n = mod_sch_write(mod_schema_, b, sizeof b);
    if (!n) {
        fprintf(stderr, "mod_dump: the schema does not write (mod_sch_write)\n");
        return 1;
    }
    printf("S %u ", n);
    for (uint32_t i = 0; i < n; i++)
        printf("%02x", b[i]);
    printf("\n");
    return 0;
}
