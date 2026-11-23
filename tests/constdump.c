/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The firmware's sizes and record identities as JSON, for Melodee Studio (tools/studio_bundle.py: constants.json),
 * from the FM-1 build's own sources through tests/mel_fixture.c's harness (backup_test.c with the user kits and
 * kit_take.c): tracks, patterns, song rows and project slots; a kit's bounds (its blob, its pad records) and Record
 * kit's quality;
 * the store's sector geometry; the records Studio reads and writes, each with the magics the firmware writes now and
 * the ones it still reads.
 *   build/host/constdump > constants.json */
static unsigned char nor[0x100000];
#define REC_BASE ((const uint8_t *)nor)        /* (the data records' flash: the player reads them in place) */
#define BACKUP_TEST_NO_MAIN 1
#define BACKUP_TEST_KITS 1
#include "backup_test.c"
/* (SAMPLER's Record kit take, encoder and record check: built in, modules/kits/kit_take.c) */

static void magic(uint32_t m, int first)
{
    char le[5], be[5];
    for (uint32_t i = 0; i < 4u; i++) {
        uint8_t b = (uint8_t)(m >> (8u * i));
        le[i] = b >= 32u && b < 127u && b != '"' && b != '\\' ? (char)b : '?';
        be[3u - i] = le[i];
    }
    le[4] = be[4] = 0;
    printf("%s{\"u32\":%u,\"hex\":\"0x%08x\",\"le\":\"%s\",\"be\":\"%s\"}", first ? "" : ",", (unsigned)m, (unsigned)m, le, be);
}
static void record(const char *name, const char *what, uint32_t at, const uint32_t *w, uint32_t nw, const uint32_t *r,
                   uint32_t nr, int last)
{
    uint32_t i;
    printf("  \"%s\":{\"what\":\"%s\",\"magic_at\":%u,\"writes\":[", name, what, (unsigned)at);
    for (i = 0; i < nw; i++) magic(w[i], !i);
    printf("],\"reads\":[");
    for (i = 0; i < nr; i++) magic(r[i], !i);
    printf("]}%s\n", last ? "" : ",");
}
#define N(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

int main(void)
{
    uint32_t i;
    /* project.c proj_import: FUN22 (PROJ_MAGIC) inside a 2.0 record (Melodee 1.x's: the web installer converts them) */
    static const uint32_t wire_w[] = {PROJ_MAGIC};
    static const uint32_t wire_r[] = {PROJ_MAGIC};
    /* pattern_store.c bank_magic / bank_valid: the whole record, its magic at 0: FBKS only (2.0) */
    static const uint32_t rec_w[] = {BANKS_MAGIC};
    static const uint32_t rec_r[] = {BANKS_MAGIC};
    /* project_stream.c: a saved project as stored (and as BACKUP_GET / PUT carry it): packed, or a raw record */
    static const uint32_t pj_w[] = {PZ_MAGIC};
    static const uint32_t pj_r[] = {PZ_MAGIC, BANKS_MAGIC};
    static const uint32_t kit_w[] = {DRK_BOX_MAGIC};
    static const uint32_t pad_w[] = {DRK_PAD_MAGIC};
    static const uint32_t set_w[] = {PERSIST_MAGIC};
    /* project.c template: TPLM (2.0) only */
    static const uint32_t tpl_w[] = {TMPL_MAGIC};
    static const uint32_t tpl_r[] = {TMPL_MAGIC};

    printf("{\"board\":\"FM-1\",\n");
    printf("\"music\":{\"parts\":%u,\"midi_tracks\":%u,\"tracks\":%u,\"patterns\":%u,\"patterns_midi\":%u,\"steps\":%u,"
           "\"chain_rows\":%u,\"project_slots\":%u,\"motion_max\":%u,\"p_count\":%u,\"g_count\":%u,\"p_e0\":%u},\n",
           NPART, NMIDI, NTRK, NPAT, NPAT_MIDI, NSTEP, CHAIN_ROWS, PROJ_SLOTS, MOTION_MAX, P_COUNT, G_COUNT, P_E0);
    printf("\"kits\":{\"user_first\":%u,\"user_count\":%u,\"store_slots\":%u,\"pads\":%u,\"name_len\":%u,"
           "\"kit_max\":%u,\"pad_max\":%u,\"sectors\":%u,\"sector_bytes\":%u,\"head_bytes\":%u,\"pad_head_bytes\":%u,"
           "\"snr\":[%d]},\n",
           DK_USER0, DK_NUSER, ST_NREC, DRK_NPAD, DRK_NAMELEN, (unsigned)(DRK_BOX_BYTES + ST_LONG_SECTORS * DRK_SECBYTES),
           (unsigned)(ST_LONG_SECTORS * DRK_SECBYTES), (unsigned)ST_LONG_SECTORS, DRK_SECBYTES, (unsigned)DRK_BOX_BYTES,
           (unsigned)DRK_PAD_BYTES, TK_SNR);
    printf("\"storage\":{\"sector\":%u,\"lin_payload_max\":%u,\"payload_off\":%u,\"guard_off\":%u,\"guard_len\":%u,"
           "\"run0\":%u,\"lin_base\":%u,\"lin_sectors\":%u,\"pool_sectors\":%u,\"preset_pool_sectors\":%u,"
           "\"project_sectors_max\":%u,\"record_bytes\":%u,\"wire_bytes\":%u,\"tail_at\":%u},\n",
           ST_SECTOR, ST_LIN_PAYLOAD_MAX, ST_POOL_PAYLOAD_OFF, ST_GUARD_OFF, ST_GUARD_LEN, ST_RUN0, ST_LIN_BASE, ST_LN,
           ST_POOL_SECTORS, ST_SP_N, ST_PROJECT_SECTORS, (unsigned)BANK_STORE_SIZE, (unsigned)PROJ_STORE_SIZE,
           (unsigned)BANK_ACTIVE_OFF);
    printf("\"records\":{\n");
    record("project_record", "a project's whole record (pattern_store.c bank_pack)", 0, rec_w, N(rec_w), rec_r, N(rec_r), 0);
    record("project_wire", "the FUN project inside it (project.c)", 8, wire_w, N(wire_w), wire_r, N(wire_r), 0);
    record("project_stored", "a saved project as stored and as the backup carries it (project_stream.c)", 0, pj_w, N(pj_w),
           pj_r, N(pj_r), 0);
    record("kit", "a kit as one blob, backup objects 92..99 (rec_store.c, drum_kit.h drk_box_t)", 0, kit_w, N(kit_w), kit_w, N(kit_w), 0);
    record("kit_pad", "a kit's pad record inside it (drum_kit.h drk_pad_t)", 0, pad_w, N(pad_w), pad_w, N(pad_w), 0);
    record("settings", "the settings record, backup object 1 (settings_persist.c)", 0, set_w, N(set_w), set_w, N(set_w), 0);
    record("template", "the template after the settings (project.c)", 0, tpl_w, N(tpl_w), tpl_r, N(tpl_r), 1);
    printf("}}\n");
    return 0;
}
