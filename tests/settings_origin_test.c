/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Real boot/import and A/B saves, including the shared PER5 fork collision. */
#define MELODEE_USB_AUDIO 1
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"

static void fixture(void)
{
    reset();
    memset(&tmpl, 0, sizeof tmpl); tmpl_dirty = 0;
    usb.up = 0;
    up_boot();                                  /* pre-existing banks must not be reformatted */
    memset(&boot_work, 0, sizeof boot_work);
    erases = boot_syncs = 0;
}
static uint32_t music_crc(void)
{
    /* Everything except the two settings sectors, including app/preset/project bytes. */
    return st_crc32(nor, 0xFC000u) ^ st_crc32(nor + 0xFE000u, 0x2000u);
}
static int defaults(void)
{
    int ok = settings.palette == UI_DEFAULT_INDEX && !settings.zoom && !settings.lowcut &&
        !settings_boot && settings_drumch == 10u && settings_hold == HOLD_DEF &&
        settings_lights == LIGHTS_MID && tuning_a4 == A4_DEFAULT &&
        settings_autosave && !settings_screen && !settings_click && settings_click_level == CLICK_LEVEL_DEFAULT &&
        !settings_countin && !settings_preview && !settings_chord_add &&
        !ua_off && !ua_off_want && !ua_tracks && !ua_tracks_want &&
        !memcmp(&panel, &PANEL_DEFAULT, sizeof panel) && !template_used() && !template_old();
    for (uint32_t k = 0; k < 4u; k++) ok &= settings_glo[k] == GP[GLO_KEPT[k]].def;
    for (uint32_t i = 0; i < sizeof favorites; i++)
        if (i < 60u || i >= 64u) ok &= !((const uint8_t *)&favorites)[i]; /* owner tag isn't a preference */
    return ok && !favorites_user_hi;
}
static void stale_ram(void)
{
    settings.magic = SETTINGS_MAGIC; settings.palette = 2; settings.zoom = 1;
    panel = PANEL_DEFAULT; panel.dir[0] = -1;
    settings_boot = 4; settings_drumch = 0; settings_hold = 3; settings_lights = 0;
    tuning_a4 = 432; settings_autosave = 0; settings_screen = 7;
    settings_click = settings_click_level = settings_countin = 2;
    settings_preview = settings_chord_add = 1;
    memset(&favorites, 0xFF, sizeof favorites); favorites_user_hi = 0xFFFFFFFFu;
    ua_off = ua_off_want = 3; ua_tracks = ua_tracks_want = 1;
    for (uint32_t k = 0; k < 4u; k++) settings_glo[k] = 30000;
}
static int test_foreign(void)
{
    int bad = 0;
    fixture();
    /* ChoralRoot's exact envelope: the shared 572-byte prefix plus CRS1/ext192,
     * with a valid FELU header and payload CRC. Old boot truncated it to 604
     * and read CRS1 & 3 as USB audio OFF in both directions. */
    uint8_t fork[764] = {0};
    persist_t p = {0}; settings_init(); settings_export(&p);
    memset(p.favorites.factory[1] + 28, 0, 4);
    memcpy(fork, &p, 572u);
    uint32_t crs = 0x31535243u; memcpy(fork + 572u, &crs, 4);
    st_save(OBJ_SETTINGS, fork, sizeof fork);
    /* Even unreadable musical objects must survive settings cleanup. */
    uint8_t old[80]; memset(old, 0x5A, sizeof old);
    st_save(OBJ_BANK0, old, sizeof old);
    uint32_t crc = music_crc(), before = erases;
    stale_ram(); boot_trace = 1;
    persist_boot();
    bad += check("foreign PER5 extension resets settings and enables stereo USB", defaults() && settings_owned(&persist_saved));
    bad += check("cleanup flushes warning and writes only one settings sector", erases == before + 1 && !boot_trace_bad && !boot_work.state && boot_work.done == 1 && !strcmp(boot_work.title, "RESETTING SETTINGS") && music_crc() == crc);
    before = erases; uint32_t flushed = boot_syncs;
    persist_boot();
    bad += check("cleaned foreign settings need no write on subsequent boot", erases == before && boot_syncs == flushed && defaults() && music_crc() == crc);
    return bad;
}
static int test_empty_and_garbage(void)
{
    int bad = 0;
    for (uint32_t seed = 0; seed < 32u; seed++) {
        fixture(); uint32_t rng = seed;
        if (seed) for (uint32_t k = 0xFC000u; k < 0xFE000u; k++) {
            rng = rng * 1664525u + 1013904223u; nor[k] = (uint8_t)(rng >> 24);
        }
        uint32_t crc = music_crc(); stale_ram(); boot_trace = 1;
        persist_boot();
        if (!defaults() || !settings_owned(&persist_saved) || erases != 1u || boot_trace_bad || music_crc() != crc) bad++;
        persist_boot(); if (erases != 1u) bad++;
    }
    bad += check("erased stock storage and random sectors reset retained RAM once", !bad);
    fixture(); persist_t p; memset(&p, 0xFF, sizeof p); p.magic = PERSIST_MAGIC;
    st_save(OBJ_SETTINGS, &p, sizeof p); stale_ram(); persist_boot();
    bad += check("CRC-valid garbage with only a PER5 magic is rejected", defaults());
    return bad;
}
static int test_legacy_and_owned(void)
{
    int bad = 0; fixture(); settings_init();
    settings.palette = 2; settings.zoom = 1; settings_boot = 3;
    settings_drumch = 0; settings_autosave = 0; tuning_a4 = 432;
    settings_hold = 3; ua_off = ua_off_want = 3; ua_tracks = ua_tracks_want = 1;
    panel.dir[0] = -1; favorites.factory[8][0] = 1;
    settings_glo[1] = 17;
    template_save();
    static uint8_t blob[sizeof set_rec];
    int n = st_load(OBJ_SETTINGS, blob, sizeof blob);
    persist_t *p = (persist_t *)blob;
    memset(p->favorites.factory[1] + 28, 0, 4);   /* pre-hotfix Melodee */
    uint32_t template_crc = st_crc32(blob + sizeof *p, (uint32_t)n - sizeof *p);
    st_save(OBJ_SETTINGS, blob, (uint32_t)n);
    uint32_t before = erases, crc = music_crc();
    persist_boot();
    bad += check("legacy Melodee settings and template survive one ownership write", !template_old() && settings_owned(&persist_saved) && erases == before + 1 && template_used() && st_crc32(&tmpl, sizeof tmpl) == template_crc && music_crc() == crc && settings.palette == 2 && settings.zoom == 1 && settings_boot == 3 && settings_drumch == 0 && !settings_autosave && tuning_a4 == 432 && settings_hold == 3 && ua_off == 3 && ua_tracks == 1 && panel.dir[0] == -1 && favorites.factory[8][0] == 1 && settings_glo[1] == 17);
    before = erases; persist_boot();
    bad += check("owned Melodee settings retain deliberate USB OFF and need no rewrite", erases == before && ua_off == 3 && settings.palette == 2 && template_used());
    p = &persist_saved; p->ext.usb_off = 0xFFFFFFFFu; p->ext.boot = 99;
    p->ext.glo[0] = 30000; p->zoom = 255; p->panel.enc[0] = 255;
    st_save(OBJ_SETTINGS, p, sizeof *p); before = erases;
    persist_boot();
    bad += check("owned malformed critical settings normalize to defaults once", !ua_off && !settings_boot && !settings.zoom && settings_glo[0] == GP[G_CLOCK].def && !memcmp(&panel, &PANEL_DEFAULT, sizeof panel) && erases == before + 1);
    before = erases; persist_boot();
    bad += check("normalized settings are stable on the next boot", erases == before);
    fixture(); settings_init(); persist_t future = {0}; settings_export(&future);
    future.favorites.factory[1][31] = 2;
    st_save(OBJ_SETTINGS, &future, sizeof future); before = erases;
    persist_boot(); persist_t saved;
    bad += check("unknown future Melodee ownership remains protected for recovery", persist_old && erases == before && st_load(OBJ_SETTINGS, &saved, sizeof saved) == sizeof saved && !memcmp(&saved, &future, sizeof future));
    return bad;
}
static int test_interrupted_cleanup(void)
{
    int bad = 0;
    for (int cut = 0; cut < 4; cut++) {
        fixture(); uint8_t old[64]; memset(old, 0x5A, sizeof old);
        st_save(OBJ_SETTINGS, old, sizeof old);
        uint32_t crc = music_crc();
        stale_ram(); boot_trace = 1; fail_after = cut;
        persist_boot();
        uint8_t got[64];
        bad += check("cut cleanup boots with safe RAM defaults and retains prior copy", defaults() && boot_work.state == 2 && persist_repair && persist_pending == 2 && !boot_trace_bad && st_load(OBJ_SETTINGS, got, sizeof got) == sizeof got && !memcmp(got, old, sizeof old) && music_crc() == crc);
        fail_after = -1;
        if (cut & 1) { fm1_ms += 1000; settings_poll(); }
        else persist_boot();
        persist_t saved;
        bad += check("retry commits defaults despite unchanged RAM and preserves music", !persist_repair && !persist_pending && !boot_trace_bad && st_load(OBJ_SETTINGS, &saved, sizeof saved) == sizeof saved && settings_owned(&saved) && music_crc() == crc);
        uint32_t before = erases; persist_boot();
        bad += check("successful cleanup retry is not repeated", erases == before && !boot_work.state);
    }
    return bad;
}
static int test_incompatible_projects(void)
{
    int bad = 0;
    static uint8_t bank[BANK_STORE_SIZE], saved[BANK_STORE_SIZE];
    for (uint32_t kind = 0; kind < 4u; kind++) {
        fixture(); settings_init(); settings_boot = 1;
        persist_t prefs = {0}; settings_export(&prefs); st_save(OBJ_SETTINGS, &prefs, sizeof prefs);
        project_capture(&proj_scratch); bank_pack(bank, &proj_scratch, 1);
        uint8_t *project = bank + 8u;
        uint32_t magic;
        if (kind == 0) {                         /* upstream Felucca bank magic, not Melodee FBKK */
            magic = 0x394B4246u; memcpy(bank, &magic, 4);
        } else if (kind == 1) {                  /* an older Felucca project inside a matching container */
            magic = 0x46554E38u; memcpy(project, &magic, 4);
        } else if (kind == 2) {                  /* fork reuses FUN16 with a different parameter layout */
            magic = 0x46554E43u; memcpy(project, &magic, 4); project[66] = 90;
        } else {                                /* a matching header must not remap an alien engine */
            project[68u + P_COUNT] = 255;
        }
        uint32_t hash = proj_hash(project, PROJ_STORE_SIZE - 4u);
        memcpy(project + PROJ_STORE_SIZE - 4u, &hash, 4); bank_checksum(bank);
        st_save(OBJ_BANK0, bank, sizeof bank);
        uint32_t crc = music_crc(), before = erases;
        persist_boot();
        int ok = !project_used(0) && project_old(0) && project_free_slot() != 0 && erases == before;
        trk[0].p[P_LEVEL] = 71;
        bootguard.failed = 0; project_boot();
        ok &= trk[0].p[P_LEVEL] == 71 && st_load(OBJ_BANK0, saved, sizeof saved) == sizeof saved &&
            !memcmp(saved, bank, sizeof bank) && music_crc() == crc;
        bad += check("incompatible project with valid CRC is quarantined before BOOT load", ok);
    }
    fixture(); project_capture(&proj_scratch); bank_pack(bank, &proj_scratch, 1);
    st_save(OBJ_BANK0, bank, sizeof bank); persist_boot();
    bad += check("valid current Melodee projects remain available after settings reset", project_used(0) && !project_old(0));
    return bad;
}
static int test_incompatible_templates(void)
{
    int bad = 0; static uint8_t blob[sizeof set_rec], saved[sizeof set_rec];
    for (uint32_t legacy = 0; legacy < 2u; legacy++) {
        fixture(); settings_init(); settings.palette = 2; template_save();
        int n = st_load(OBJ_SETTINGS, blob, sizeof blob);
        persist_t *p = (persist_t *)blob;
        if (legacy) memset(p->favorites.factory[1] + 28, 0, 4);
        /* Correct TPL schema/trailer and outer CRC, but a fork's engine ID. */
        blob[sizeof *p + 2u*PROJ_GLOBALS + 2u] = 255;
        st_save(OBJ_SETTINGS, blob, (uint32_t)n); uint32_t before = erases;
        persist_boot(); trk[0].p[P_LEVEL] = 71; bootguard.failed = 0; project_boot();
        bad += check("alien template engine cannot boot; valid settings and raw template survive", tmpl_old && !template_used() && settings.palette == 2 && trk[0].p[P_LEVEL] == 71 && erases == before && st_load(OBJ_SETTINGS, saved, sizeof saved) == n && !memcmp(blob, saved, (uint32_t)n));
    }
    return bad;
}
int main(void)
{
    int bad = test_foreign() + test_empty_and_garbage() + test_legacy_and_owned() + test_interrupted_cleanup() + test_incompatible_projects() + test_incompatible_templates();
    puts(bad ? "SETTINGS ORIGIN TEST FAILED" : "settings origin test passed");
    return bad != 0;
}
