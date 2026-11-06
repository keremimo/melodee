/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * The tables and the queues are hal/smk_input.h's: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton
 * Instruments; SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* hal/smk_input.h on fm1-base. The base scans the SMK-37 (its device/plat_smk37.c: the keybed and its velocity
 * curves, the pads, the faders, the wheels, the pedal, the 18 buttons, the 8 encoders, the LEDs, the pads' lights);
 * the instrument keeps that header's API and state as its modules read them: fm1_in in the SMK's button and encoder
 * ids (panel.c PANEL_DEFAULT: the base's own map and HARDWARE CALIBRATION put the functions there), the 27 note keys
 * and the 37 keybed positions, the pads' key map and the UI's two queues (the pads it has, the ten outer keys).
 * melodee_app.c smk_base_poll fills them once a pass from the bridge (ml_input, ml_smk_read); the button LEDs and the
 * pads' colours go back with ml_smk_lights. */
#pragma once

#define FM1_NCOL 16u
#define FM1_NBTN 18u
#define FM1_NKEY 37u              /* the keybed */
#define FM1_NENC 8u
#define SMK_NPAD 16u
#define SMK_NLED 19u
#define SMK_KEY_F3 5u             /* keybed key 5 = F3: keys 5..31 are the FM-1's 27 note keys (bits 0..26) */
enum { FM1_BTN_OCT_DOWN = 4, FM1_BTN_OCT_UP = 5 };
enum { SMK_BTN_KNOB_BANK = 0, SMK_BTN_FADER_BANK = 1, SMK_BTN_LEFT = 2, SMK_BTN_RIGHT = 3 };
#define FM1_OCT_BOTH (1u << FM1_BTN_OCT_DOWN | 1u << FM1_BTN_OCT_UP)

/* the pads (0..7 the top row, 8..15 the bottom, left to right): hal/smk_input.h's maps, set by the UI each frame
 * (ui_input.c smk_pads_sync): the note key a pad presses, -1 none (the press goes to the UI's queue) */
static const uint8_t SMK_WHITE_NOTE[SMK_NPAD] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const int8_t SMK_PK_KEYS[SMK_NPAD] = {14, 16, 18, 19, 21, 23, 24, 26, 0, 2, 4, 6, 7, 9, 11, 12};
static const int8_t SMK_PK_GRID[SMK_NPAD] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const int8_t SMK_PK_PIANO[SMK_NPAD] = {7, 9, 11, 12, 14, 16, 18, 19, 8, 10, -1, 13, 15, 17, -1, 20};
static const int8_t SMK_PK_NONE[SMK_NPAD] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static const int8_t *volatile smk_pad_key = SMK_PK_KEYS;
static uint8_t smk_pad_note[SMK_NPAD];   /* the note key a pad pressed (+1; 0 = none; 0xFF: the UI's queue) */
#define SMK_PADQ 16u
static volatile uint16_t smk_padq[SMK_PADQ];
static volatile uint8_t smk_padq_w, smk_padq_r;
#define SMK_EXTQ 32u
static volatile uint16_t smk_extq[SMK_EXTQ];
static volatile uint8_t smk_extq_w, smk_extq_r;
/* VELOCITY (the menu, settings_persist.c): 0 NORMAL, 1 SOFT, 2 HARD, 3 FIXED 100; the base's curves (plat_vel_curve) */
static uint8_t smk_vel_curve;
/* the sustain pedal, down or up: the base reads the jack's rest and re-learns it (seq.c sustain_poll) */
static volatile uint8_t smk_pedal;
#define SMK_PEDAL_DOWN() ((uint32_t)smk_pedal)

static volatile struct {
    uint32_t notes;              /* bit n = note key n (0 = F3 .. 26 = G5): keybed keys 5..31 | the pads */
    uint32_t knotes, pnotes;     /* (the two sources of notes: keybed keys 5..31, pads as white keys) */
    uint32_t buttons;            /* bit i = button i (0..17) */
    uint32_t pressed, released;  /* button edges since the last fm1_input_edges() */
    uint32_t remote, remote_tap; /* MIDI LEARN: buttons a controller holds; pressed, not yet seen down */
    uint32_t notes_pressed;      /* note-key press edges since the last fm1_input_note_edges() */
    uint32_t notes_retrig;       /* note keys struck again while down (their bit in notes stays set): seq.c */
    uint64_t keys;               /* all 37 keybed keys: bit k = key k (0 = lowest C), set while its note sounds */
    uint8_t key_vel[FM1_NKEY];   /* MIDI velocity 1..127 of each key's last press (a pad: its white key's) */
    int16_t enc_steps[FM1_NENC]; /* detents not yet taken (fm1_enc_take), by encoder id */
    uint8_t pad_st[SMK_NPAD];    /* 0 idle, 0x80 on */
    uint8_t pad_press[SMK_NPAD]; /* pressure 0..127 while on */
    uint8_t fader[4];            /* 0..127 */
    uint32_t frames;             /* 1.6 ms scan frames since power-on (the readings' settling: smk_controls.h) */
} fm1_in;
static uint8_t fm1_led[SMK_NLED];     /* 1 = lit (button LED i = button i; 18 SEQ PLAY's red) */

static inline uint32_t fm1__lock(void) { return ml_lock(); }
static inline void fm1__unlock(uint32_t v) { ml_unlock(v); }

static void smk__extq(uint32_t k, uint32_t vel)
{
    uint32_t w = smk_extq_w;
    if ((w + 1u) % SMK_EXTQ == smk_extq_r) {          /* full: a press is dropped, a release takes the newest entry's place */
        if (!vel)
            smk_extq[(w + SMK_EXTQ - 1u) % SMK_EXTQ] = (uint16_t)(k << 8);
        return;
    }
    smk_extq[w] = (uint16_t)(k << 8 | vel);
    smk_extq_w = (uint8_t)((w + 1u) % SMK_EXTQ);
}
static void smk__key_event(uint32_t k, uint32_t vel)     /* vel 0 = release */
{
    if (vel)
        fm1_in.key_vel[k] = (uint8_t)vel;
    if (k >= SMK_KEY_F3 && k < SMK_KEY_F3 + 27u) {
        uint32_t bit = 1u << (k - SMK_KEY_F3);
        if (vel) {
            fm1_in.knotes |= bit;
            fm1_in.notes_pressed |= bit;
        } else {
            fm1_in.knotes &= ~bit;
        }
        fm1_in.notes = fm1_in.knotes | fm1_in.pnotes;
    } else {
        smk__extq(k, vel);
    }
    if (vel)
        fm1_in.keys |= (uint64_t)1u << k;
    else
        fm1_in.keys &= ~((uint64_t)1u << k);
}
static void smk__padq_ui(uint32_t pad, uint32_t vel)  /* .. to the UI's queue (a full queue drops a press, never a release) */
{
    uint8_t w = smk_padq_w;
    if ((uint8_t)(w + 1u) % SMK_PADQ == smk_padq_r) {
        if (vel) return;
        smk_padq_r = (uint8_t)((smk_padq_r + 1u) % SMK_PADQ);
    }
    smk_padq[w] = (uint16_t)(pad << 8 | vel);
    smk_padq_w = (uint8_t)((w + 1u) % SMK_PADQ);
}
static void smk__padq(uint32_t pad, uint32_t vel)     /* a pad hit (vel) or release (0): its key, or the UI's queue */
{
    int32_t n0 = smk_pad_key[pad];
    uint32_t n, bit;
    if (vel) {
        if (n0 < 0) {
            smk_pad_note[pad] = 0xFF;
            smk__padq_ui(pad, vel);
            return;
        }
        smk_pad_note[pad] = (uint8_t)(n0 + 1);
    } else {
        if (!smk_pad_note[pad])
            return;
        if (smk_pad_note[pad] == 0xFF) {
            smk_pad_note[pad] = 0;
            smk__padq_ui(pad, 0);
            return;
        }
        n0 = smk_pad_note[pad] - 1;                    /* the key it pressed, whatever the map is now */
        smk_pad_note[pad] = 0;
    }
    n = (uint32_t)n0;
    bit = 1u << n;
    if (vel) {
        fm1_in.key_vel[SMK_KEY_F3 + n] = (uint8_t)vel;
        fm1_in.pnotes |= bit;
        fm1_in.notes_pressed |= bit;
    } else {
        fm1_in.pnotes &= ~bit;
    }
    fm1_in.notes = fm1_in.knotes | fm1_in.pnotes;
}

/* ------------------------------------------------------- main-loop API --- */
static int32_t fm1_enc_take(uint32_t e)
{
    uint32_t k = fm1__lock();
    int32_t s = e < FM1_NENC ? fm1_in.enc_steps[e] : 0;
    if (e < FM1_NENC)
        fm1_in.enc_steps[e] = 0;
    fm1__unlock(k);
    return s;
}
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    fm1__unlock(k);
    return p;
}
static uint32_t fm1_input_note_edges(void)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    fm1__unlock(k);
    return p;
}
/* the next pad event of the UI's (pad << 8 | velocity, 0 = release), -1 if none */
static int32_t smk_pad_take(void)
{
    int32_t v = -1;
    if (smk_padq_r != smk_padq_w) {
        v = smk_padq[smk_padq_r];
        smk_padq_r = (uint8_t)((smk_padq_r + 1u) % SMK_PADQ);
    }
    return v;
}
/* the next outer-key event (keybed key << 8 | velocity, 0 = release), -1 if none */
static int32_t smk_extkey_take(void)
{
    int32_t v = -1;
    if (smk_extq_r != smk_extq_w) {
        v = smk_extq[smk_extq_r];
        smk_extq_r = (uint8_t)((smk_extq_r + 1u) % SMK_EXTQ);
    }
    return v;
}
static void fm1_led_key(uint32_t id, int on)
{
    if (id < SMK_NLED)
        fm1_led[id] = (uint8_t)(on != 0);
}

/* the 16 RGB pads (lcd.c's padled_* on the classic build): the colours the UI set, 0xRRGGBB, sent each frame */
#define PADLED_N 16u
static uint32_t padled_rgb[PADLED_N];
static uint32_t padled_hold_until;              /* (the classic console's padtest; never set here) */
static void padled_set(uint32_t pad, uint8_t r, uint8_t g, uint8_t b)
{
    if (pad < PADLED_N)
        padled_rgb[pad] = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}
