/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A synth module's image (tools/mod_build.py): one block from MOD_BASE (0; the tool links a second time at another
 * base to check the address list), the descriptor first, then code, constants and initialised data; .bss after it
 * (zeroed by the loader). --emit-relocs keeps the absolute words the loader must move. */
ENTRY(mod_entry_)
SECTIONS
{
    . = MOD_BASE;
    .image : {
        KEEP(*(.mod_desc))
        *(.text*) *(.audio_text*) *(.dsp_text*) *(.ram_text*)   /* (an engine-source module: the firmware's placements, */
        mod_rodata_ = .;                                         /* all of them SRAM here; a label where the code
                                                                  * ends: its strings follow, unnamed, and
                                                                  * tests/target_budget.py reads up to a label) */
        *(.rodata*) *(.dsp_tables*)
        *(.data*) *(.sdata*)
        . = ALIGN(4);
    }
    .bss (NOLOAD) : { *(.bss*) *(.sbss*) *(COMMON) *(.pool*) . = ALIGN(4); }
    .mod_ident 0 (INFO) : { KEEP(*(.mod_ident)) }               /* (who it is: the builder's IDNT chunk, not loaded) */
    .mod_presets 0 (INFO) : { KEEP(*(.mod_presets)) }           /* (its presets: the builder's PRST chunk, not loaded) */
    /DISCARD/ : { *(.comment) *(.note*) }
}
