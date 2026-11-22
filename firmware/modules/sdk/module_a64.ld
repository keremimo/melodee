/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A synth module's AArch64 image (tools/mod_build.py --native): as module.ld, from MOD_BASE with the descriptor first,
 * but the data on pages of its own, 16 KB on (macOS's pages; Linux's 4 KB divide them), so a host maps the code and
 * constants read-only once it has moved their address words and leaves only the data writable. .bss after the data
 * (zeroed by the loader). --emit-relocs keeps the absolute words (R_AARCH64_ABS64) the loader must move. */
ENTRY(mod_entry_)
SECTIONS
{
    . = MOD_BASE;
    .text : {
        KEEP(*(.mod_desc))
        *(.text*) *(.audio_text*) *(.dsp_text*) *(.ram_text*)
        *(.rodata*) *(.dsp_tables*) *(.data.rel.ro*)
        . = ALIGN(8);
    }
    . = ALIGN(0x4000);
    .data : { *(.data*) *(.sdata*) . = ALIGN(8); }
    .bss (NOLOAD) : { *(.bss*) *(.sbss*) *(COMMON) *(.pool*) . = ALIGN(8); }
    .mod_ident 0 (INFO) : { KEEP(*(.mod_ident)) }
    .mod_presets 0 (INFO) : { KEEP(*(.mod_presets)) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.ARM.exidx*) }
}
