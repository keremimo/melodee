/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Frozen serialized bytes from 53f05c4, before changing the RAM note layout: FBKK, the dense four-part record a
 * project of eight banks a part still saves as when FBKP cannot hold it (bank_force_v1). */
#define AUDIO_CPU_FIXTURE_NO_MAIN 1
#include "audio_cpu_fixture.c"
int main(void)
{
    static const char *const engines[]={"0124","0000","1111","2222","3333","4444","5555"};
    static const uint32_t counts[]={64,256,RECORD_MAX};
    uint32_t hash=2166136261u,cases=0;
    for(uint32_t e=0;e<NELEM(engines);e++)for(uint32_t c=0;c<NELEM(counts);c++)
        for(uint32_t q=0;q<2;q++)for(uint32_t s=0;s<2;s++)for(uint32_t t=0;t<2;t++){
            bank_force_v1=1;assert(make_fixture(counts[c],q,s,t,engines[e])&&*(uint32_t *)fixture_raw==BANK_MAGIC);bank_force_v1=0;
            hash=fnv(hash,fixture_raw,sizeof fixture_raw);cases++;
        }
    printf("Frozen FBKK/FUN22 bytes: %u cases, hash %08x\n",cases,hash);
    assert(hash==0x519392b4u);return 0;
}
