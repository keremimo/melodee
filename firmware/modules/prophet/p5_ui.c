/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The Prophet's panel as its schema (prophet_schema.h) shows it: the values that are not plain bytes of the program
 * (pget / pput: the tuning in audible semitones, BEND from 1, five unison voices for a stored 0), the tuning's
 * descriptor while it follows the keys, the hints that dim a control whose waveform, destination or release is off
 * (pactive), the program's voice settings becoming the part's (patch_changed). Editing one value leaves every other
 * byte as it is. Included by eng_prophet_test.c (the module and host builds) */
#if !MOD_SIDE
#include "prophet_schema.h"                       /* (the module's file carries it: its SCHM chunk) */
#endif
static const param_desc_t P5_TUNE_SEMI = {"FREQ B", F_SEMI, -12, 36, 0, 0, 0};
static int p5_keyed(const uint8_t *p, uint32_t id) { return id == P5_FREQ_A || (id == P5_FREQ_B && p[P5_KEY_B]); }
static int32_t p5_pget(const track_t *t, const uint8_t *p, uint32_t id)
{
    (void)t;
    if (p5_keyed(p, id))
        return clamp(p[id], 0, 96) / 2 - 12;
    if (id == P5_UNISON_COUNT)
        return p[id] ? p[id] : 5;                  /* (Sequential stores zero for the default five) */
    if (id == P5_BEND)
        return p[id] + 1;
    return id < P5_RAW_MAX ? p[id] : 0;
}
static int p5_pput(track_t *t, uint8_t *p, uint32_t id, int32_t v)
{
    uint8_t old;
    (void)t;
    if (id >= P5_RAW_MAX)
        return 0;
    old = p[id];
    if (p5_keyed(p, id))
        v = (v + 12) * 2;
    else if (id == P5_BEND)
        v -= 1;
    p[id] = (uint8_t)clamp(v, id == P5_UNISON_COUNT ? 1 : 0, id == P5_UNISON_COUNT ? 5 : id == P5_BEND ? 11 : 127);
    return p[id] != old;
}
static const param_desc_t *p5_desc(const track_t *t, uint32_t k)
{
    return k == MOD3_DESC_ID + P5_FREQ_B && p5_keyed(p5_patch_of(t)->raw, P5_FREQ_B) ? &P5_TUNE_SEMI : 0;
}
/* Conservative signal-path hints, as on CZ-1: dim controls whose required waveform, destination or release switch is
 * off. They remain editable. This describes dependencies, not an audibility threshold or MIDI presence */
static int p5_pactive(const track_t *t, const uint8_t *p, uint32_t id)
{
    /* A track matrix can open a mixer level or supply extra modulation.
     * Leave hints conservative while any such dynamic route is enabled. */
    for(uint32_t k=0;k<4u;k++)if(t->p[P_M1SRC+3u*k]&&t->p[P_M1DST+3u*k]&&t->p[P_M1AMT+3u*k])return 1;
    if((id==P5_UNISON_COUNT||id==P5_UNISON_DETUNE||id==P5_RETRIGGER) &&
       t->p[P_VOICE]!=(p[P5_UNISON]?V_UNISON:V_POLY))return 1;
    int a=clamp(p[P5_LEVEL_A]+t->p[P_E6],0,127)>0;
    int ap=a&&p[P5_PULSE_A];
    int b=clamp(p[P5_LEVEL_B]+t->p[P_E7],0,127)>0;
    int poly=p[P5_POLY_FREQ]||p[P5_POLY_PW]||p[P5_POLY_FILTER];
    int source=p[P5_POLY_ENV]||p[P5_POLY_B];
    int wheel=p[P5_WHEEL_FREQ_A]||p[P5_WHEEL_FREQ_B]||p[P5_WHEEL_PW_A]||p[P5_WHEEL_PW_B]||p[P5_WHEEL_FILTER];
    int depth=p[P5_LFO_INITIAL]||t->mw||p[P5_PRESS_LFO];
    int wave=p[P5_LFO_SAW]||p[P5_LFO_TRI]||p[P5_LFO_PULSE];
    int filter_env=p[P5_ENV_FILTER]||(p[P5_POLY_ENV]&&poly);
    switch(id){
    case P5_PW_A:return ap;
    case P5_PW_B:return p[P5_PULSE_B]&&(b||(p[P5_POLY_B]&&poly));
    case P5_FREQ_B:case P5_FINE_B:case P5_LOW_B:case P5_KEY_B:
        return b || (p[P5_POLY_B]&&poly) || (p[P5_SYNC]&&a);
    case P5_LFO_RATE:return wheel&&depth&&wave&&p[P5_WHEEL_MIX]<127u;
    case P5_LFO_SAW:case P5_LFO_TRI:case P5_LFO_PULSE:
        return wheel&&depth&&p[P5_WHEEL_MIX]<127u;
    case P5_LFO_INITIAL:return wheel&&(wave||p[P5_WHEEL_MIX]);
    case P5_WHEEL_MIX:return wheel&&depth;
    case P5_WHEEL_PW_A:return ap;
    case P5_WHEEL_PW_B:return p[P5_PULSE_B]&&(b||(p[P5_POLY_B]&&poly));
    case P5_POLY_ENV:case P5_POLY_B:return poly;
    case P5_POLY_FREQ:case P5_POLY_FILTER:return source;
    case P5_POLY_PW:return source&&ap;
    case P5_VEL_FILTER:return p[P5_ENV_FILTER]!=0;
    case P5_ATTACK_FILTER:case P5_DECAY_FILTER:case P5_SUSTAIN_FILTER:return filter_env;
    case P5_RELEASE_FILTER:return filter_env&&p[P5_RELEASE_ON];
    case P5_RELEASE_AMP:return p[P5_RELEASE_ON]!=0;
    case P5_UNISON_COUNT:case P5_RETRIGGER:return p[P5_UNISON]!=0;
    case P5_UNISON_DETUNE:return p[P5_UNISON]&&p5_cap_of(t,p)>1u;
    default:return 1;
    }
}
/* a new program (a preset, a user preset), or an edit of its unison or priority: its voice settings become the part's */
static void p5_patch_changed(track_t *t, uint8_t *p, uint32_t how, uint32_t id)
{
    if (how == MOD3_PATCH_NEW || how == MOD3_PATCH_LOAD || (how == MOD3_PATCH_EDIT &&
        (id == P5_UNISON || id == P5_UNISON_COUNT || id == P5_UNISON_DETUNE || id == P5_RETRIGGER)))
        p5_track_accept_raw(t, p);
}
