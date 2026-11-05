/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Charles Vestal
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MELODEE memory map on fm1-base: firmware/app.ld's, with the base's image layout (tools/build_base.py copies this to
 * the base's device/app.ld).
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * RAMTEXT: .ram_text (code that must run while the flash is busy: the flash driver), then Melodee's DSP code and
 * tables, one load image copied at boot (ramtext.bin); the PC guard's RAM window starts here and ends at the resident
 * audio's end (tools/base_overlay.py _sd_exec_end).
 * RAM  : .data/.bss (96 KiB), its tail Melodee's audio arena's.
 * POOL : big zero-initialised buffers (canvases, patterns, USB rings), the resident audio code (.audio_text, copied at
 *        boot), CPU1's stacks, then the audio working arena (resources.c) to the stacks' guard band.
 * STACKS: 11.5 KiB main, 6 KiB interrupts (Melodee's 12 + 6, the main one 512 B less for the base's records in NOINIT),
 *        each with a 256-byte guard band.
 * NOINIT survives resets and UBOOT entry: Melodee's retained patterns and diagnostics, the base's crash record.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table.
 * RES: the resource region in flash (res.c), 0xCB020..0xDFFFF, read in place: installed after an update, not part of
 *      the image. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x9DFBC   /* the app slot (0x8DFBC) and 64 KB: the image before
                                                       * boot_pack packs it; build.py checks the slot */
    RES    (r)   : ORIGIN = 0x020C7020, LENGTH = 0x14FE0
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x8000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rwx) : ORIGIN = 0x01C20000, LENGTH = 0x57600
    NOINIT (rw)  : ORIGIN = 0x01C7BE00, LENGTH = 0x3F50
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*)
              . = ALIGN(4); _etext = .; } > XIP
    .mres : { _mres_start = .; *(.mres*) . = ALIGN(4); _mres_end = .; } > RES
    /* .c1_text: an app's second core's RAM loop (tools/build.py), copied and guarded with the rest */
    /* Melodee's DSP code (.dsp_text: the engines' kernels and CPU1's worker loop, hal/fm1_multicore.h AW_RAM_LOOP)
     * is the second core's window for the base's checks (tools/build.py: its calls stay in RAM; Melodee's
     * tools/audio_ram.py checks every flash/RAM boundary of the resident audio), then its tables */
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*))
                  . = ALIGN(4); _c1_start = .; KEEP(*(.c1_text*))
                  . = ALIGN(4); _dsp_start = .; *(.dsp_text*) . = ALIGN(4); _dsp_end = .; _c1_end = .;
                  . = ALIGN(4); _dt_start = .; *(.dsp_tables*) . = ALIGN(4); _dt_end = .;
                  _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    _resource_low_start = ALIGN(_rt_end, 4);
    _resource_low_end = ORIGIN(RAMTEXT) + LENGTH(RAMTEXT);
    _app_xip_end = ORIGIN(XIP) + 0x8DFBC;            /* the app slot's end (the boot's bound on the packed load
                                                      * images: main_fm1.c) */
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    _resource_data_start = ALIGN(_bss_end, 4);
    _resource_data_end = ORIGIN(RAM) + LENGTH(RAM);
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); } > POOL
    .audio_text : { _sd_audio_start = .; _audio_start = .; *(.audio_text*) *(.audio_libc*) . = ALIGN(4);
                    _sd_audio_end = .; _audio_end = .; } > POOL AT > XIP
    _sd_audio_load = LOADADDR(.audio_text);
    _sd_exec_end = _sd_audio_end;
    .cpu1_stacks (NOLOAD) : { . = ALIGN(32); _cpu1_stacks_lo = .; KEEP(*(.cpu1_stacks)) _cpu1_stacks_hi = .; } > POOL
    _cpu1_ustack_top = _cpu1_stacks_lo + 3072;
    _cpu1_sstack_top = _cpu1_stacks_lo + 4096;
    .audio_scratch (NOLOAD) : { *(.audio_scratch*) } > POOL
    _pool_end = ALIGN(., 32);
    _resource_start = _pool_end;
    _resource_end = ORIGIN(POOL) + LENGTH(POOL);
    /* Four largest engine states must fit 48 KiB (ram_audio_test), as firmware/app.ld's with AUDIO_RESIDENT */
    ASSERT(_resource_end - _resource_start >= 48K, "audio working arena below worst-case budget")
    ASSERT(_resource_low_start <= _resource_low_end, "RAM code overflows RAMTEXT")
    ASSERT(_resource_data_start <= _resource_data_end, "data overlaps resource pool")
    .noinit (NOLOAD) : { *(.noinit*) . = ALIGN(4); _noinit_end = .; } > NOINIT
    _resource_retained_start = ALIGN(_noinit_end, 4);
    _resource_retained_end = ORIGIN(NOINIT) + LENGTH(NOINIT);
    _resource_cache_start = 0; _resource_cache_end = 0;   /* (MELODEE_CACHE_RAM 0 on the base, so far) */
    _guard0 = 0x01C77600; _ustack_lo = 0x01C77700; _ustack_top = 0x01C7A500;
    _guard1 = 0x01C7A500; _sstack_lo = 0x01C7A600; _sstack_top = 0x01C7BE00;
    /DISCARD/ : { *(.comment) *(.note*) }
}
