/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The editor protocol as JSON, for Melodee Studio (tools/studio_bundle.py: protocol.json), from the firmware's own
 * editor.c through tests/editor_test.c's harness: the frame header, every command number (editor.c's enums and the
 * sub-handlers' ids; the build a command needs), the limits (the reply buffer, the request frame, the small frames,
 * a backup chunk), INFO's tagged capability blocks walked out of a real INFO reply (a block this table does not know,
 * or one of another length, fails the dump), the backup objects (editor_backup.c ED_BK_IDS, with what each one is
 * by the handler's own functions) and UI_SET's preference ids (editor_preferences.c).
 *   build/host/protodump > protocol.json */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"

static void js_str(const char *s)
{
    putchar('"');
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\')
            putchar('\\');
        putchar(*s);
    }
    putchar('"');
}

/* every command the firmware answers; "when": the build flag it needs ("" every build) */
static const struct { uint32_t id; const char *name, *group, *when; } CMDS[] = {
    {ED_INFO, "INFO", "core", ""}, {ED_GET, "GET", "core", ""}, {ED_SET, "SET", "core", ""},
    {ED_DUMP, "DUMP", "core", ""}, {ED_DESC, "DESC", "core", ""}, {ED_STEP_GET, "STEP_GET", "steps", ""},
    {ED_STEP_SET, "STEP_SET", "steps", ""}, {ED_PRESET, "PRESET", "presets", ""}, {ED_PROJECT, "PROJECT", "projects", ""},
    {ED_NAMES, "NAMES", "presets", ""},
    {ED_SMP_BEGIN, "SMP_BEGIN", "samples (retired)", ""}, {ED_SMP_WRITE, "SMP_WRITE", "samples (retired)", ""},
    {ED_SMP_END, "SMP_END", "samples (retired)", ""}, {ED_SMP_ERASE, "SMP_ERASE", "samples (retired)", ""},
    {ED_SMP_INFO, "SMP_INFO", "samples (retired)", ""},
    {ED_UP_LIST, "UP_LIST", "user presets", ""}, {ED_UP_GET, "UP_GET", "user presets", ""},
    {ED_UP_PUT, "UP_PUT", "user presets", ""}, {ED_UP_STORE, "UP_STORE", "user presets", ""},
    {ED_UP_LOAD, "UP_LOAD", "user presets", ""}, {ED_UP_ERASE, "UP_ERASE", "user presets", ""},
    {ED_WATCH, "WATCH", "live sync", ""}, {ED_CHANGED, "CHANGED", "live sync (push)", ""},
    {ED_RELOAD, "RELOAD", "live sync (push)", ""}, {ED_PING, "PING", "live sync", ""},
    {ED_STEP_CHANGED, "STEP_CHANGED", "live sync (push)", ""},
    {ED_TRACK, "TRACK", "tracks", ""}, {ED_TRACK_MIX, "TRACK_MIX", "tracks", ""}, {ED_TRACK_DUMP, "TRACK_DUMP", "tracks", ""},
    {ED_TRACK_STEP, "TRACK_STEP", "tracks", ""}, {ED_TRACK_PARAM, "TRACK_PARAM", "tracks", ""},
    {ED_TRACK_CHANGED, "TRACK_CHANGED", "live sync (push)", ""}, {ED_SONG, "SONG", "song (retired)", ""},
    {ED_UI_STATE, "UI_STATE", "preferences", ""}, {ED_UI_SET, "UI_SET", "preferences", ""},
    {ED_UI_PALETTES, "UI_PALETTES", "preferences", ""}, {ED_FAV_GET, "FAV_GET", "preferences", ""},
    {ED_FAV_SET, "FAV_SET", "preferences", ""},
    {ED_CAPS, "CAPS", "studio session", ""}, {ED_TRACKS, "TRACKS", "studio session", ""},
    {ED_TRANSPORT, "TRANSPORT", "studio session", ""}, {ED_PROJECTS, "PROJECTS", "studio session", ""},
    {ED_STORAGE, "STORAGE", "studio session", ""},
    {ED_SONG2, "SONG2", "studio song", ""}, {ED_SONG_SOUND, "SONG_SOUND", "studio song", ""}, {ED_PUNCH, "PUNCH", "studio song", ""},
    {ED_METERS, "METERS", "studio session (push)", ""}, {ED_TRACKS_CHANGED, "TRACKS_CHANGED", "studio session (push)", ""},
    {ED_TRANSPORT_CHANGED, "TRANSPORT_CHANGED", "studio session (push)", ""},
    {ED_SONG_CHANGED, "SONG_CHANGED", "studio song (push)", ""},
    {ED_PATTERN_DATA, "PATTERN_DATA", "studio patterns", ""}, {ED_NOTES, "NOTES", "studio patterns", ""},
    {ED_PATTERN_CHANGED, "PATTERN_CHANGED", "studio patterns (push)", ""},
    {ED_SETTINGS, "SETTINGS", "studio device", ""}, {ED_MLEARN, "MLEARN", "studio device", ""},
    {52, "BLE", "studio device", "MELODEE_BLE"},                                       /* (editor_ble.c ED_BLE) */
    {ED_DRUM, "DRUM", "studio device", ""}, {ED_DEVICE_CHANGED, "DEVICE_CHANGED", "studio device (push)", ""},
    {ED_MOTION, "MOTION", "motion", ""}, {ED_BACKUP_LIST, "BACKUP_LIST", "backup", ""},
    {ED_BACKUP_GET, "BACKUP_GET", "backup", ""}, {ED_BACKUP_PUT, "BACKUP_PUT", "backup", ""},
    {ED_FM6_GET, "FM6_GET", "fm6", ""}, {ED_FM6_PUT, "FM6_PUT", "fm6", ""}, {ED_FM6_LIST, "FM6_LIST", "fm6", ""},
    {ED_FM6_ERASE, "FM6_ERASE", "fm6", ""},
    {ED_AUDIO_STATS, "AUDIO_STATS", "diagnostics", "MELODEE_USB_AUDIO"},
    {ED_PATTERN, "PATTERN", "pattern banks", ""}, {ED_BANK_SONG, "BANK_SONG", "pattern banks", ""},
    {ED_CZ_GET, "CZ_GET", "cz", ""}, {ED_CZ_PUT, "CZ_PUT", "cz", ""}, {ED_CZ_BANK, "CZ_BANK", "cz", ""},
    {ED_NATIVE, "NATIVE", "native slots", ""},
    {79, "CORE1_TEST", "laboratory", "MELODEE_CORE1_TEST && MELODEE_DUAL_CORE"},   /* (editor.c ED_CORE1_TEST) */
    {ED_LIBRARY, "LIBRARY", "shared library", ""},
    {81, "BLE_CAL", "bluetooth", "MELODEE_BLE"},                                      /* (editor_ble.c ED_BLE_CAL) */
    {82, "RES", "resources", "RES_BASE"},                                             /* (editor_res.c ED_RES) */
    {95, "PROPHET", "prophet", ""},                                                   /* (editor_prophet.c) */
    {97, "FX_PROFILE", "laboratory", "MELODEE_FX_PROFILE"},
    {98, "AUDIO_PROFILE", "laboratory", "MELODEE_AUDIO_PROFILE"},
    {99, "AUDIO_BENCH", "laboratory", "MELODEE_AUDIO_BENCH"},
    {100, "CLOCK_REPORT", "diagnostics", ""},
    {101, "CPU_LAB", "laboratory", "MELODEE_AUDIO_PROFILE && MELODEE_AUDIO_BENCH"},
};
#ifdef ED_BLE_CAL
_Static_assert(ED_BLE_CAL == 81u, "BLE_CAL");
#endif
#ifdef ED_RES
_Static_assert(ED_RES == 82u, "RES");
#endif
#ifdef ED_BLE
_Static_assert(ED_BLE == 52u, "BLE");
#endif

/* INFO's tagged blocks after the engine names, NTRK and the retired SONG byte, in the order the firmware sends them
 * (the web editor reads them by position: a tag can come twice, 'U'); each block's payload names */
static const struct { uint8_t tag, ver; const char *name, *cmds, *fields[8]; } TAGS[] = {
    {0x55, 1, "preferences", "34-38", {"ui_caps"}},
    {0x4d, 2, "motion", "64", {"motion_max", "chance"}},
    {0x42, 1, "backup", "65-67", {"caps"}},
    {0x50, 4, "pattern banks", "73-74", {"patterns", "chain_rows"}},
    {0x46, 1, "fm6 patches", "68-71", {"factory", "bank"}},
    {0x43, 1, "native cz tones", "75-77", {"name_bytes", "bank", "retired", "retired"}},
    {0x35, 1, "native namespace", "78", {"prophet_engine", "user_general", "user_native_fm", "user_native_cz",
                                         "user_native_p5", "flags", "native_version"}},
    {0x4e, 1, "native pools", "78", {"fm6_capacity_lo", "fm6_capacity_hi", "cz_capacity_lo", "cz_capacity_hi"}},
    {0x55, 1, "shared library", "80", {"capacity_lo", "capacity_hi"}},
    {0x53, 1, "studio", "39-45, 47-53, 55-60", {"caps_command"}},
};

static uint32_t nfields(uint32_t k) { uint32_t n = 0; while (n < 8u && TAGS[k].fields[n]) n++; return n; }

static int info_tags(void)
{
    uint8_t none[1];
    uint32_t n = request(ED_INFO, none, 0), i = 5, e, k;
    const uint8_t *w = host_wire;
    if (n < 8u || w[0] != 0xF0 || w[4] != ED_INFO || w[n - 1u] != 0xF7) return 0;
    while (i < n && w[i]) i++;                           /* the version string */
    i += 1u + 5u;                                        /* NENGINES, P_COUNT, G_COUNT, NSTEP, P_E0 */
    for (e = 0; e < NENGINES; e++) { while (i < n && w[i]) i++; i++; }
    i += 2u;                                             /* NTRK, the retired SONG rows */
    printf(",\n\"info_tags\":[");
    for (k = 0; k < sizeof TAGS / sizeof TAGS[0]; k++) {
        uint32_t f, m = nfields(k);
        if (i + 2u + m > n - 1u || w[i] != TAGS[k].tag || w[i + 1u] != TAGS[k].ver) {
            fprintf(stderr, "protodump: INFO block %u is not 0x%02x v%u\n", (unsigned)k, TAGS[k].tag, TAGS[k].ver);
            return 0;
        }
        printf("%s\n {\"tag\":%u,\"char\":\"%c\",\"version\":%u,\"name\":", k ? "," : "", TAGS[k].tag, TAGS[k].tag,
               TAGS[k].ver);
        js_str(TAGS[k].name);
        printf(",\"commands\":");
        js_str(TAGS[k].cmds);
        printf(",\"payload\":{");
        for (f = 0; f < m; f++) {
            printf("%s", f ? "," : "");
            js_str(TAGS[k].fields[f]);
            printf(":%u", w[i + 2u + f]);
        }
        printf("}}");
        i += 2u + m;
    }
    printf("]");
    if (i != n - 1u) { fprintf(stderr, "protodump: INFO has %u bytes after the known blocks\n", (unsigned)(n - 1u - i)); return 0; }
    return 1;
}

static void backup_objects(void)
{
    uint32_t i, id;
    printf(",\n\"backup\":{\"chunk\":%u,\"kit_first\":%u,\"kits\":%u,\"objects\":[", (unsigned)sizeof main_scratch.transfer,
           ED_BK_KIT0, (unsigned)ST_NKIT);
    /* ED_BK_IDS, then the user kits (every device build has KIT_STORE; editor_test.c does not) */
    for (i = 0; i < ED_BK_N + (ED_BK_IDS[ED_BK_N - 1u] < ED_BK_KIT0 ? ST_NKIT : 0u); i++) {
        id = i < ED_BK_N ? ED_BK_IDS[i] : ED_BK_KIT0 + (i - ED_BK_N);
        printf("%s\n {\"id\":%u,\"kind\":", i ? "," : "", (unsigned)id);
        if (id == 0u) printf("\"runtime\"");
        else if (id == 1u) printf("\"settings\"");
        else if (ed_bk_project(id)) printf("\"project\",\"slot\":%u", (unsigned)ed_bk_slot(id));
        else if (ed_bk_preset_bank(id) < UP_BANKS) printf("\"preset_bank\",\"bank\":%u", (unsigned)ed_bk_preset_bank(id));
        else if (id >= ED_BK_KIT0 && id < ED_BK_KIT0 + ST_NKIT) printf("\"kit\",\"slot\":%u", (unsigned)(id - ED_BK_KIT0));
        else printf("\"retired\"");
        putchar('}');
    }
    printf("]}");
}

int main(void)
{
    uint32_t i;
    reset();
    printf("{\"header\":[%u,%u,%u,%u],\"end\":%u,\"value\":{\"bits\":14,\"offset\":8192},\n", 0xF0u, ED_HDR0, ED_HDR1,
           ED_HDR2, 0xF7u);
    printf("\"limits\":{\"reply\":%u,\"request\":%u,\"small_frame\":64,\"small_below\":%u,\"backup_chunk\":%u,"
           "\"push_per_pass\":%u,\"motion_max\":%u,\"tracks_listed\":%u},\n",
           (unsigned)sizeof ed_out, (unsigned)sizeof sx_frame, ED_SMP_BEGIN, (unsigned)sizeof main_scratch.transfer,
           ED_PUSH_MAX, MOTION_MAX, (unsigned)ED_TRK);   /* small: cmds below SMP_BEGIN are copied to f[64] (ed_service) */
    printf("\"commands\":[");
    for (i = 0; i < sizeof CMDS / sizeof CMDS[0]; i++) {
        printf("%s\n {\"id\":%u,\"name\":", i ? "," : "", (unsigned)CMDS[i].id);
        js_str(CMDS[i].name);
        printf(",\"group\":");
        js_str(CMDS[i].group);
        if (CMDS[i].when[0]) {
            printf(",\"when\":");
            js_str(CMDS[i].when);
        }
        putchar('}');
    }
    printf("]");
    printf(",\n\"ui_set\":{\"palette\":0,\"font\":1,\"monitor\":2,\"favorites_filter\":3,\"click\":4,\"click_level\":5,"
           "\"countin\":6,\"preview\":7,\"chord_add\":8,\"retired\":9,\"screen_off\":10}");
    printf(",\n\"ui_caps\":{\"palette\":%u,\"font\":%u,\"monitor\":%u,\"favorites\":%u,\"recording\":%u,"
           "\"performance\":%u,\"click_percent\":%u}", ED_UI_PALETTE, ED_UI_FONT, ED_UI_MONITOR, ED_UI_FAVORITES,
           ED_UI_RECORDING, ED_UI_PERFORMANCE, ED_UI_CLICK_PERCENT);
    if (!info_tags()) return 1;
    backup_objects();
    printf("}\n");
    return 0;
}
