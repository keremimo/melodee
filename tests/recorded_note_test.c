/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Exhaust the larger identifier space while the shipping schema remains
 * frozen. A separate byte fixture protects old event identity; mixed edits
 * protect duration, rounding, wrapping, and drum FREE versus gated playback. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "recorded_note.h"

static void legacy_bytes(void)
{
    for (uint32_t owner=0;owner<32u;owner++)
    for (uint32_t step=0;step<64u;step++)
    for (uint32_t exponent=0;exponent<8u;exponent++)
    for (uint32_t flags=0;flags<8u;flags++) {
        struct { uint16_t on,duration;uint8_t note,vel,owner,step;int8_t pitch;uint8_t length; } old={.on=52341,.duration=37421,.note=127,.vel=91,
            .owner=(uint8_t)(owner+32u*exponent),
            .step=(uint8_t)(step+64u*(flags&3u)),.pitch=-24,.length=(uint8_t)(flags>>2)};
        recorded_note_t v={.on=52341,.duration=37421,.note7=127,.vel=91,.pitch=-24};
        recorded_set_owner(&v,owner);recorded_set_step(&v,step,flags&1u,flags&2u);
        recorded_set_exponent(&v,exponent);recorded_set_gate(&v,flags&4u);
        /* Canonical ten-byte fixtures stay independent of packed fields. */
        uint8_t canonical[10]={v.on,v.on>>8,v.duration,v.duration>>8,recorded_note(&v),v.vel,v.owner,v.step,(uint8_t)v.pitch,recorded_legacy_length(&v)};
        assert(sizeof old==10u && !memcmp(&old,canonical,sizeof old));
    }
}
static void expanded_edits(void)
{
    for (uint32_t owner=0;owner<64u;owner++)
    for (uint32_t step=0;step<128u;step++)
    for (uint32_t exponent=0;exponent<8u;exponent++)
    for (uint32_t flags=0;flags<8u;flags++) {
        recorded_note_t v={.on=65535,.duration=49321,.note7=0,.vel=127,.pitch=24};
        recorded_set_exponent(&v,exponent);recorded_set_step(&v,step,flags&1u,flags&2u);
        recorded_set_gate(&v,flags&4u);recorded_set_owner(&v,owner);
        recorded_note_t snapshot=v;
        assert(recorded_owner(&v)==owner && recorded_step(&v)==step);
        assert(recorded_exponent(&v)==exponent && recorded_gate(&v)==!!(flags&4u));
        assert(recorded_rounded(&v)==!!(flags&1u) && recorded_wrapped(&v)==!!(flags&2u));
        /* A copy across the owner/step boundaries, a length edit, then undo.
         * The low and high halves must both clear, not only set. */
        recorded_set_owner(&v,63u-owner);recorded_set_exponent(&v,7u-exponent);
        recorded_set_step(&v,127u-step,!(flags&1u),!(flags&2u));
        recorded_set_gate(&v,!(flags&4u));
        assert(recorded_owner(&v)==63u-owner && recorded_step(&v)==127u-step);
        assert(recorded_exponent(&v)==7u-exponent && recorded_gate(&v)==!(flags&4u));
        assert(recorded_rounded(&v)==!(flags&1u) && recorded_wrapped(&v)==!(flags&2u));
        assert(v.on==65535 && v.duration==49321 && recorded_note(&v)==0 && v.vel==127 && v.pitch==24);
        recorded_set_step(&v,step,flags&1u,flags&2u);recorded_set_gate(&v,flags&4u);
        recorded_set_exponent(&v,exponent);recorded_set_owner(&v,owner);
        assert(!memcmp(&v,&snapshot,sizeof v));
    }
}
int main(void)
{
    legacy_bytes();expanded_edits();
    puts("Recorded-note layout: canonical legacy bytes and 64 owners x 128 steps preserved in nine RAM bytes");
    return 0;
}
