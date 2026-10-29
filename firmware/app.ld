/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MELODEE memory map.
 * Code + constants execute in place from XIP (body offset 0 = 0x02001000; boot stub at 0x02000120).
 * 0x01C00000..0x01C07F00 holds .ram_text (code that must run while the flash
 * is busy: the flash driver). Unused RAMTEXT/debug space ends at 0x01C07F00:
 * the boot stub writes STATUS in the last 256 bytes. Linker-derived SRAM tails
 * join the audio resource banks without changing retained state or stacks.
 * 0x01C7C000..0x01C7C01F belongs to the boot stub, across warm resets.
 * RAM  : .data/.bss (96 KiB)
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02001000, LENGTH = 0x8C000   /* body ends before commit-marker sector 0x91000 */
    RES    (r)   : ORIGIN = 0x020C7020, LENGTH = 0x14FE0   /* flash 0xCB020..0xDFFFF: the resource region (res.c),
                                                         * installed after an update, not part of the image */
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x7F00
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rwx) : ORIGIN = 0x01C20000, LENGTH = 0x57600
    NOINIT (rw)  : ORIGIN = 0x01C7C020, LENGTH = 0x3D30
    CACHE  (rw)  : ORIGIN = 0x01F28000, LENGTH = 28K
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .mres : { _mres_start = .; *(.mres*) . = ALIGN(4); _mres_end = .; } > RES
    /* Every section copied to SRAM at boot has an explicit LMA after .text, outside the XIP region: the packager
     * stores them boot-compressed from _rt_load on (tools/boot_audio.py), so their compressed size, not the ELF's raw
     * sections, determines the flash available. The packager enforces the image limit. */
    .ram_text : AT (ALIGN(_etext, 4)) { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT
    _rt_load = LOADADDR(.ram_text);
    .dsp_text : AT (ALIGN(LOADADDR(.ram_text) + SIZEOF(.ram_text), 4)) { _dsp_start = .; *(.dsp_text*) . = ALIGN(4); _dsp_end = .; } > RAMTEXT
    _dsp_load = LOADADDR(.dsp_text);
    .dsp_tables : AT (ALIGN(LOADADDR(.dsp_text) + SIZEOF(.dsp_text), 4)) { _dt_start = .; *(.dsp_tables*) . = ALIGN(4); _dt_end = .; } > RAMTEXT
    _dt_load = LOADADDR(.dsp_tables);
    _resource_low_start = ALIGN(_dt_end, 4);
    _resource_low_end = 0x01C07F00;
    .data : AT (ALIGN(LOADADDR(.dsp_tables) + SIZEOF(.dsp_tables), 4)) { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    _resource_data_start = _bss_end;
    _resource_data_end = ORIGIN(RAM) + LENGTH(RAM);
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .audio_text : AT (ALIGN(LOADADDR(.data) + SIZEOF(.data), 4)) { _audio_start = .; *(.audio_text*)
        /* The backend can synthesize libc calls after IR relocation. Keep
         * these resident entry points at the high end, within XIP call range. */
        *(.audio_libc*) . = ALIGN(4); _audio_end = .; } > POOL
    _audio_load = LOADADDR(.audio_text);
    _app_xip_end = ORIGIN(XIP) + LENGTH(XIP);
    _guard_ram_end = SIZEOF(.audio_text) ? _audio_end : _dsp_end;
    .cpu1_stacks (NOLOAD) : {
        . = ALIGN(32); _cpu1_stacks_lo = .;
        KEEP(*(.cpu1_stacks))
        _cpu1_stacks_hi = .; _resource_start = .;
    } > POOL
    _cpu1_ustack_top = _cpu1_stacks_lo + 3072;
    _cpu1_sstack_top = _cpu1_stacks_lo + 4096;
    _resource_end = ORIGIN(POOL) + LENGTH(POOL);
    /* Complete resident interrupt/input graph trades arena capacity for code.
     * Four largest engine states must fit 48 KiB (ram_audio_test); effect
     * delay memory has its own admission/allocation limits. */
    _resource_minimum = DEFINED(AUDIO_RESIDENT) ? 48K : 139K;
    ASSERT(_resource_end - _resource_start >= _resource_minimum, "audio working arena below worst-case budget")
    .noinit (NOLOAD) : { *(.noinit*) . = ALIGN(4); _noinit_end = .; } > NOINIT
    /* These transfer/job buffers are always filled before use, never retained
     * musical state. Keep diagnostic .noinit addresses ahead of them intact. */
    .audio_scratch (NOLOAD) : { . = ALIGN(32); *(.audio_scratch*) . = ALIGN(4); _audio_scratch_end = .; } > NOINIT
    _resource_retained_start = _audio_scratch_end;
    _resource_retained_end = ORIGIN(NOINIT) + LENGTH(NOINIT);
    /* Never part of .bss or startup zeroing: use only after HAL self-test. */
    _resource_cache_start = ORIGIN(CACHE);
    _resource_cache_end = ORIGIN(CACHE) + LENGTH(CACHE);
    ASSERT(_resource_low_start <= _resource_low_end, "RAM code overlaps startup STATUS")
    ASSERT(_resource_data_start <= _resource_data_end, "data overlaps resource pool")
    ASSERT(_resource_retained_start <= _resource_retained_end, "retained state overlaps boot records")
    /* Bounded target stack estimates are checked by the build. CPU0 keeps
     * 12 KiB foreground + 6 KiB interrupt stacks and both 256-byte guards. */
    _guard0 = 0x01C77600; _ustack_lo = 0x01C77700; _ustack_top = 0x01C7A700;
    _guard1 = 0x01C7A700; _sstack_lo = 0x01C7A800; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
