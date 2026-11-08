/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A synth module's factory presets, inside its file (tools/mod_presets.py builds them, module_store.c reads them).
 * Nothing here depends on a module ABI: the chunks follow the address words of any module file, the loader copies
 * only the image into SRAM and never a chunk, and the lists read a module's presets from the pool without loading it.
 *
 * Chunks: a file whose head has MOD_FILE_CHUNKS in its flags (mod_abi3.h mod_file3_t) goes on after its address words with chunks,
 * each a u32 tag, a u32 length, that many bytes, then zero bytes to the next multiple of 4. The file's CRC (mod_file3_t
 * .crc) covers them, its total counts them. Readers skip tags they do not know.
 *
 * MOD_PST_TAG ("PRST"), version 1, little endian:
 *   head (16 bytes)    version, count, init (the engine's own starting point, its INIT; 0xFFFF none), dict (bytes),
 *                      ulen (every entry's bytes unpacked: the 16-byte common block, then the patch), 0, crc (CRC-32
 *                      of the payload after the head)
 *   index              count + 1 records of 8 bytes: u32 the entry's offset in the payload, u16 the name's offset in the
 *                      names, u8 its category (the browser's LIST: browse.c CAT_*), u8 0; the last record holds the
 *                      ends (an entry's packed bytes and a name's characters run to the next record's)
 *   names              the names one after another (ASCII 32..126, at most MOD_PST_NAME each, no terminator)
 *   dictionary         dict bytes every entry unpacks after (its copies may reach back into them)
 *   entries            each one as dsp.c lz_unpack reads it, ulen bytes, at most MOD_PST_WIN packed
 * The common block: E1..E8 (signed), ATK DEC SUS REL, ENV -> FILTER (signed), MONO, 0, 0 (preset_t's values). The
 * patch: the synth's own bytes (the Prophet's program as p5_patch_t, the CZ-1's 144-byte tone, FM6's 128-byte packed
 * voice; SID and DRUM none) */
#pragma once
#include <stdint.h>

#define MOD_FILE_CHUNKS 1u                       /* the head's flags: chunks follow the address words (MOD3_FILE_CHUNKS) */
#define MOD_PST_TAG 0x54535250u                  /* "PRST" */
#define MOD_PST_VERSION 1u
#define MOD_PST_HEAD 16u
#define MOD_PST_REC 8u
#define MOD_PST_COMMON 16u
#define MOD_PST_NAME 20u                          /* a name's characters, at most */
#define MOD_PST_BUF 640u                          /* the dictionary and one entry unpacked, at most (scratch.h dec) */
#define MOD_PST_WIN 256u                          /* an entry's packed bytes, at most (the reader's window) */
#define MOD_PST_NONE 0xFFFFu

typedef struct {
    uint16_t version, count, init, dict, ulen, rsv;
    uint32_t crc;
} mod_pst_head_t;
_Static_assert(sizeof(mod_pst_head_t) == MOD_PST_HEAD, "PRST head");
