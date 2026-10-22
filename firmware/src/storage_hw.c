/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The firmware side of storage.c (MELODEE_FLASH): flash reads in short IRQ-off windows so the
 * audio keeps up, erases and writes only inside the storage areas, the audio buffer silenced
 * first. The host tests (storage_test.c) provide their own st_* on a simulated NOR. */
static uint8_t flash_ok;                 /* JEDEC id matched at boot (persist_boot) */
#if BOARD_SMK37
#define ST_IDLE() fm1_wdt_feed()                /* (storage.c st_owner_check: a first boot clears ~80 sectors) */
#endif
static int st_read(uint32_t off, void *dst, uint32_t n)   /* 256-byte IRQ-off windows: audio keeps up */
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static void audio_silence(void)                 /* IRQs off: the DMA would loop stale audio (buzz) */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
}
#if BOARD_SMK37
static void st_stock_release(void);             /* storage.c: the stock mapping's end marker */
static int st_in_usrflash(uint32_t off);
#endif
static int st_erase(uint32_t off)
{
#if MELODEE_PROPHET_PROTOTYPE
    (void)off;
    return -8; /* measurement firmware must not persist the temporary index-0 renderer */
#else
    uint32_t took, f;
    int rc;
#if BOARD_SMK37
    if (!FL_STORE_OK(off, 0x1000u) && !FL_STOCKTAG_OK(off, 0x1000u))   /* (the marker: only its release) */
        return -8;
    if (st_in_usrflash(off))
        st_stock_release();                     /* before the first erase in USRFLASH: see storage.c */
#else
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
#endif
    f = irq_save();
    audio_silence();
    rc = FL_FAR(fl_erase4k_ram)(off, &took);
    irq_restore(f);
    return rc;
#endif
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
#if MELODEE_PROPHET_PROTOTYPE
    (void)off; (void)src; (void)n;
    return -8;
#else
    if (!FL_STORE_OK(off, n))                   /* only inside the storage areas */
        return -8;
#if BOARD_SMK37
    if (st_in_usrflash(off))
        st_stock_release();                     /* before the first program in USRFLASH too */
#endif
    return fl_write(off, src, n);
#endif
}
