/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../firmware/src/tuning.h"
#define __attribute__(x)
#define NENGINES 20u
#define USER_GENERAL 16u
#define USER_NATIVE_P5 20u
#define ENGI_PROPHET 19u
static int p5_favorite_has(uint32_t slot,int factory){(void)slot;(void)factory;return 0;}
static int p5_favorite_set(uint32_t slot,int on,int factory){(void)slot;(void)on;(void)factory;return 0;}
#define UP_SLOTS 64u
#define USER_NATIVE_FM 17u
#define USER_NATIVE_CZ 18u
#define ENGI_FM6 12u
#define ENGI_CZ 15u
static void fm1_led_key(unsigned k, int on) { (void)k; (void)on; }
static int fm1_enc_take(unsigned k) { (void)k; return 0; }
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
static void settings_save(void) {}
static uint8_t settings_boot, settings_drumch = 10;  /* (params.c) */
#define VOICE_BUDGET_DEFAULT 16u
static int16_t settings_budget=VOICE_BUDGET_DEFAULT;
#define VOICE_KINDS 5u
static int16_t settings_vpoints[VOICE_KINDS], settings_safety;  /* (params.c GLO > WEIGHTS) */
static int native_used(uint32_t e, uint32_t k) { (void)e; (void)k; return 0; }
#if __has_include("../firmware/src/favorites.c")
static int up_favorite(uint32_t k);
static int up_favorite_set(uint32_t k,int on);
#include "../firmware/src/favorites.c"
static int up_favorite(uint32_t k){return k<64u && (((k<32u?favorites.user:favorites_user_hi)>>(k%32u))&1u);}
static int up_favorite_set(uint32_t k,int on){if(k>=64u)return 0;uint32_t *word=k<32u?&favorites.user:&favorites_user_hi;uint32_t old=*word;if(on)*word|=1u<<(k%32u);else *word&=~(1u<<(k%32u));return old!=*word;}
#endif
#include "../firmware/src/settings_persist.c"
int main(void)
{
    persist_t original = {0}, p;
    {   /* Tagged knob preferences survive saves without touching adjacent favorites. */
        persist_t q = {0}; q.magic = PERSIST_MAGIC; q.panel = PANEL_DEFAULT;
        assert(settings_import(&q, sizeof q) && settings_midi_knobs == MK_OFF && settings_midi_knob_mode == MK_ABSOLUTE);
        for (uint32_t profile = 0; profile < MK_PROFILES; profile++)
            for (uint32_t mode = 0; mode < MK_MODES; mode++) {
                settings_midi_knobs = profile; settings_midi_knob_mode = mode;
                settings_export(&q);
                settings_midi_knobs = settings_midi_knob_mode = 255;
                assert(settings_import(&q, sizeof q) && settings_midi_knobs == profile && settings_midi_knob_mode == mode);
                assert(sizeof q == 604u && settings_owned(&q));
            }
        q.favorites.factory[1][MIDI_KNOB_PREF_BYTE + 2] = 255;
        assert(settings_import(&q, sizeof q) && settings_midi_knobs == MK_OFF && settings_midi_knob_mode == MK_ABSOLUTE);
        settings_export(&q);
    }
    {   /* Retired fidelity bytes are cleared on save; they now hold a tagged CLICK LEVEL (default: untagged). */
        persist_t q={0};q.magic=PERSIST_MAGIC;q.panel=PANEL_DEFAULT;
        uint8_t *f=q.favorites.factory[CLICK_PREF_ROW]+CLICK_PREF_BYTE;
        f[0]='F';f[1]='Q';f[2]=1;f[3]=2;
        assert(settings_import(&q,sizeof q) && settings_click_level==CLICK_LEVEL_DEFAULT);settings_export(&q);
        assert(sizeof q==604u && !f[0] && !f[1] && !f[2] && !f[3]);
        settings_click_level=80;settings_export(&q);
        assert(f[0]=='C' && f[1]=='K' && f[2]==1u && f[3]==80u);
        settings_click_level=0;assert(settings_import(&q,sizeof q) && settings_click_level==80u);
        f[3]=101;assert(settings_import(&q,sizeof q) && settings_click_level==25u);   /* untagged: HIGH, the nearest old level */
    }

    {   /* Device workload budget shares tagged high USB bits, never its low flags. */
        persist_t q={0};q.magic=PERSIST_MAGIC;q.panel=PANEL_DEFAULT;
        for(uint32_t usb=0;usb<4u;usb++)for(int16_t points=8;points<=32;points++){
            q.ext.usb_off=usb;settings_budget=points;settings_export(&q);
            assert((q.ext.usb_off&3u)==usb && sizeof q==604u);
            settings_budget=0;assert(settings_import(&q,sizeof q) && settings_budget==points);
        }
        q.ext.usb_off=WORK_BUDGET_TAG|(7u<<8)|2u;
        assert(settings_import(&q,sizeof q) && settings_budget==VOICE_BUDGET_DEFAULT && q.ext.usb_off==2u);
        q.ext.usb_off=0;assert(settings_import(&q,sizeof q) && settings_budget==VOICE_BUDGET_DEFAULT);
    }
    original.magic = PERSIST_MAGIC;
    original.palette = 4; original.bold = original.lowcut = original.zoom = 1;   /* an untagged id: the default */
    original.panel = PANEL_DEFAULT; original.panel.enc[0] = 3;
    original.favorites.factory[8][0] = 1;
    original.favorites.user = 1u << 31; original.favorites.filter = 1;
    p = original;
    assert(settings_import(&p, sizeof p) == 1); settings_init();
    assert(settings.palette == UI_DEFAULT_INDEX && !settings.lowcut && settings.zoom && panel.enc[0] == 3);
    assert(p.palette == palette_to_stored(UI_DEFAULT_INDEX));
    {   /* tagged ids round trip; any other id (the untagged ones of before 1.0 too) is the default */
        persist_t q = original;
        for (uint32_t i = 0; i < NPALETTES; i++) assert(palette_from_stored(palette_to_stored(i)) == i);
        for (uint32_t i = 0; i < 72u; i++) assert(palette_from_stored(i) == UI_DEFAULT_INDEX && !palette_stored_ok(i));
        assert(palette_stored_ok(palette_to_stored(3)));
        q.palette = palette_to_stored(2);
        assert(settings_import(&q, sizeof q) == 1 && settings.palette == 2u);
        p = original; assert(settings_import(&p, sizeof p) == 1); settings_init();
    }
#ifdef MELODEE_FAVORITES
    assert(favorite_has(8, 0) && favorite_has(USER_GENERAL, 31) && favorites.filter);
#endif
    favorite_set(USER_NATIVE_FM,63,1);favorite_set(USER_NATIVE_CZ,63,1);favorite_set(USER_GENERAL,63,1);
    settings_export(&p);memset(&favorites,0,sizeof favorites);favorites_user_hi=0;settings_import(&p,sizeof p);
    assert(favorite_has(USER_NATIVE_FM,63) && favorite_has(USER_NATIVE_CZ,63) && favorite_has(USER_GENERAL,63));
    assert(!favorite_has(USER_NATIVE_FM,64) && !favorite_has(USER_NATIVE_CZ,128));
    p=original;settings_import(&p,sizeof p);
    {
        uint8_t legacy[16]; memset(favorites.factory[14], 0xA5, 32);
        memcpy(legacy, favorites.factory[14], sizeof legacy);
        assert(!scale_favorite(0) && !scale_favorite(69));
        assert(scale_favorite_set(0, 1) && scale_favorite_set(69, 1) && scale_favorite_set(95, 1));
        assert(!scale_favorite_set(96, 1) && !scale_favorite_set(69, 1));
        assert(!memcmp(legacy, favorites.factory[14], sizeof legacy));
        settings_export(&p); memset(&favorites, 0, sizeof favorites); settings_import(&p, sizeof p);
        assert(scale_favorite(0) && scale_favorite(69) && scale_favorite(95));
        assert(scale_favorite_set(69, 0) && !scale_favorite(69) && scale_favorite(95));
        p=original;settings_import(&p,sizeof p);
    }
    settings.lowcut = 0;
    settings_export(&p);
    assert(!p.lowcut && p.bold == 1 && p.favorites.user == (1u << 31));   /* bold: kept as saved */
    assert(p.favorites.factory[8][0] == 1 && p.favorites.filter == 1);
    assert(p.panel.enc[0] == 3); /* saving one feature preserves the other */
    {   /* A4 shares PER5's last spare word; old and malformed records use 440 Hz. */
        persist_t q = original;
        assert(tuning_a4 == 440);
        tuning_a4 = 432;
        settings_export(&q);
        assert(q.ext.spare[1] == (A4_TAG | 432u));
        tuning_a4 = 480;
        assert(settings_import(&q, sizeof q) == 1 && tuning_a4 == 432);
        for (uint32_t hz = A4_MIN; hz <= A4_MAX; hz++) {
            tuning_a4 = (int16_t)hz; settings_export(&q);
            tuning_a4 = 0;
            assert(settings_import(&q, sizeof q) == 1 && tuning_a4 == (int16_t)hz);
        }
        const uint32_t invalid[] = {0u, 432u, 0xFFFFFFFFu, A4_TAG | 399u, A4_TAG | 481u};
        for (uint32_t k = 0; k < sizeof invalid / sizeof invalid[0]; k++) {
            q.ext.spare[1] = invalid[k]; tuning_a4 = 432;
            assert(settings_import(&q, sizeof q) == 1 && tuning_a4 == 440);
        }
        p = original; settings_import(&p, sizeof p);
    }
    {   /* AUTOSAVE: default ON for old/unknown bytes, tagged OFF survives a settings round trip. */
        persist_t q = original;
        assert(settings_autosave);
        settings_autosave = 0;
        settings_export(&q);
        assert(sizeof q == 604u && q.favorites.factory[15][28] == AUTOSAVE_OFF_TAG);
        settings_autosave = 1;
        assert(settings_import(&q, sizeof q) && !settings_autosave);
        settings_autosave = 1;
        settings_export(&q);
        settings_autosave = 0;
        assert(settings_import(&q, sizeof q) && settings_autosave);
        for (uint32_t byte = 0; byte < 256u; byte++) {
            q.favorites.factory[15][28] = (uint8_t)byte;
            assert(settings_import(&q, sizeof q) && settings_autosave == (byte != AUTOSAVE_OFF_TAG));
        }
        p = original; settings_import(&p, sizeof p);
    }
    {   /* records before PER5 (Felucca's PER1..PER4; 1.2 rewrote them) and short ones: not read */
        const uint32_t magic[4] = {0x50455231u, 0x50455232u, 0x50455233u, 0x50455234u};
        for (uint32_t k = 0; k < 4u; k++) {
            p = original; p.magic = magic[k];
            assert(!settings_import(&p, sizeof p) && !settings_import(&p, (int)(sizeof p - sizeof p.ext)));
        }
        p = original;
        assert(!settings_import(&p, (int)sizeof p - 1));
    }
    settings_drumch = 0; settings_export(&p);        /* DRUM OFF: 17 in the record; 10 (the default): 0 */
    assert(p.ext.drumch == 17u && settings_import(&p, sizeof p) == 1 && !settings_drumch);
    settings_drumch = 10; settings_export(&p); assert(!p.ext.drumch);
    settings_drumch = 3; assert(settings_import(&p, sizeof p) == 1 && settings_drumch == 10u);
    p = original; p.ext.usb_off = 2;                /* PER5: ext kept by a build without USB audio */
    assert(settings_import(&p, sizeof p) == 1 && p.ext.usb_off == 2u);
    settings_export(&p); assert(p.ext.usb_off == 2u);
    {   /* HOLD: in the retired bold field; older records (bold 0 / 1) are 0.4 s, and stay as saved */
        p = original; p.bold = 0;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF && HOLD_MS[settings_hold] == 400u);
        p = original;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF);
        settings_hold = 3;
        settings_export(&p);
        assert(hold_stored_ok(p.bold) && p.bold != 1u);
        settings_hold = 0;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == 3u && HOLD_MS[settings_hold] == 600u);
        settings_hold = HOLD_DEF;
        settings_export(&p);
        assert(p.bold == 0u && settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF);
        assert(!hold_stored_ok(2u) && !hold_stored_ok(HOLD_TAG + 4u) && hold_stored_ok(HOLD_TAG | 2u));
    }
    assert(settings_import(&p, 3) == 0 && settings_import(&p, -1) == 0);
    assert(settings_import(&p, sizeof p - 1) == 0);
    puts("Settings: PER5 only, palette ids, calibration, HOLD, AUTOSAVE, ext and independent feature preservation passed.");
}
