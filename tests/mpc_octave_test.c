/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* MPC-only octave ownership, physical button paths, held releases, and saved-state compatibility. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include "fixtures/mpc_sample_pad_notes.h"
#include <assert.h>

static void setup_mpc(void)
{
    ui_power_on();
    midi_hint = 0;
    for (uint32_t k = 0; k < NPART; k++) {
        set_engine_of(&trk[k], 3);
        trk[k].p[P_QUANT] = Q_MPC; trk[k].p[P_SCALE] = 1;
        trk[k].p[P_ROOT] = trk[k].p[P_TRANS] = 0;
        trk[k].p[P_MPCDEG] = 1; trk[k].p[P_CHRD] = 0;
        trk[k].p[P_AMODE] = 0; trk[k].p[P_VOICE] = V_POLY;
    }
    song.octave = 2; go_home();
}

static int gate(const track_t *t, uint32_t note)
{
    for (uint32_t k = 0; k < NVOICE; k++) if (t->v[k].active && t->v[k].gate && t->v[k].note == note) return 1;
    return 0;
}

static void buttons_and_mapping(void)
{
    setup_mpc();
    static const int8_t oct[] = {-2, -1, 1, 2};
    for (uint32_t k = 0; k < NPART; k++) {
        song.sel = (uint8_t)k;
        for (int32_t n = 0; n < (oct[k] < 0 ? -oct[k] : oct[k]); n++) press(oct[k] < 0 ? B_OCTDN : B_OCTUP);
        assert(trk[k].mpc_octave == oct[k] && song.octave == 2);
        assert(oct_leds() == (oct[k] < 0 ? 1u : 2u));
        assert(midi_map(&trk[k], MPC_PAD_NOTES[k][1]) == (uint32_t)(60 + 12 * oct[k]));
        assert(kb_map(&trk[k], 7) == (uint32_t)(60 + 12 * oct[k]));
    }
    /* Clamps and the reset chord affect just the selected bank. */
    for (uint32_t k = 0; k < 5u; k++) press(B_OCTUP);
    assert(trk[3].mpc_octave == 3 && trk[2].mpc_octave == 1);
    fm1_in.buttons |= (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    host_pressed |= fm1_in.buttons; frame();
    fm1_in.buttons = 0; frame();
    assert(!trk[3].mpc_octave && !oct_leds() && song.octave == 2 && trk[0].mpc_octave == -2);
    for (uint32_t k = 0; k < 5u; k++) press(B_OCTDN);
    assert(trk[3].mpc_octave == -3);
    /* The press's bank owns a deferred Stage tap even if selection changes before release. */
    song.sel = 0;
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP]; host_pressed |= fm1_in.buttons; frame();
    assert(oct_deferred);
    song.sel = 1;
    fm1_in.buttons = 0; frame();
    assert(trk[0].mpc_octave == -1 && trk[1].mpc_octave == -1);
    go_title("PATTERN"); press(B_OCTDN);
    assert(ui.home && trk[1].mpc_octave == -1 && song.octave == 2);
    /* All other QNT modes retain the global input octave. */
    for (uint32_t q = Q_OFF; q < Q_MPC; q++) {
        TSEL->p[P_QUANT] = (int16_t)q; song.octave = 0;
        press(B_OCTUP);
        assert(song.octave == 1 && trk[1].mpc_octave == -1 && input_octave(TSEL) == 1);
    }
    TSEL->p[P_QUANT] = Q_MPC;
    assert(input_octave(TSEL) == -1 && oct_leds() == 1u);
    apply_preset_to(TSEL, 0);
    assert(trk[1].mpc_octave == -1);
    /* Releases use their original tones after an octave change. */
    midi_enqueue(0x09u | 0x90u << 8 | (uint32_t)MPC_PAD_NOTES[1][1] << 16 | 100u << 24, 1u);
    events_block(CTL);
    assert(gate(&trk[1], 48));
    press(B_OCTUP); assert(trk[1].mpc_octave == 0);
    midi_enqueue(0x08u | 0x80u << 8 | (uint32_t)MPC_PAD_NOTES[1][1] << 16, 1u);
    events_block(CTL);
    assert(!gate(&trk[1], 48) && !midi_owners[1]);
    puts("MPC octaves: independent buttons/LEDs/keys/pads, bounds/reset, deferred target, presets and original releases: OK");
}

static void persistence(void)
{
    static project_t p, restored;
    static project_store_t wire;
    static tmpl_t saved;
    setup_mpc();
    for (uint32_t k = 0; k < NPART; k++) trk[k].mpc_octave = (int8_t)((int32_t)k * 2 - 3);
    project_capture(&p);
    assert(proj_pack(&wire, &p) && proj_unpack(&restored, wire.raw));
    for (uint32_t k = 0; k < NPART; k++) assert(restored.t[k].mpc_octave == trk[k].mpc_octave);
    for (uint32_t k = 0; k < NPART; k++) trk[k].mpc_octave = 0;
    assert(!project_restore_runtime(&restored));
    for (uint32_t k = 0; k < NPART; k++) assert(trk[k].mpc_octave == (int32_t)k * 2 - 3);
    /* An old project's zero tail restores neutral bank octaves; malformed tagged state is rejected. */
    wire.raw[PROJ_MPC_OCT_WIRE] = wire.raw[PROJ_MPC_OCT_WIRE+1u] = 0;
    uint32_t hash = proj_hash(wire.raw, PROJ_STORE_SIZE - 4u);
    memcpy(wire.raw + PROJ_STORE_SIZE - 4u, &hash, 4);
    assert(proj_unpack(&restored, wire.raw));
    for (uint32_t k = 0; k < NPART; k++) assert(!restored.t[k].mpc_octave);
    wire.raw[PROJ_MPC_OCT_WIRE] = 7; wire.raw[PROJ_MPC_OCT_WIRE+1u] = 0xA0;
    hash = proj_hash(wire.raw, PROJ_STORE_SIZE - 4u); memcpy(wire.raw+PROJ_STORE_SIZE-4u, &hash, 4);
    assert(!proj_unpack(&restored, wire.raw));
    template_save(); saved = tmpl;
    assert(!saved.master[17] && !saved.master[18] && !saved.master[19]); /* older readers retain the FX layout */
    assert(tmpl_blob_valid((const uint8_t *)&saved, sizeof saved));
    memset(&tmpl, 0, sizeof tmpl);
    assert(tmpl_take((const uint8_t *)&saved, sizeof saved));
    for (uint32_t k = 0; k < NPART; k++) trk[k].mpc_octave = 0;
    template_load();
    for (uint32_t k = 0; k < NPART; k++) assert(trk[k].mpc_octave == (int32_t)k * 2 - 3);
    saved.g[TMPL_MPC_OCT_G] = (int16_t)0xA007u;
    assert(!tmpl_blob_valid((const uint8_t *)&saved, sizeof saved));
    saved.g[TMPL_MPC_OCT_G] = 0;
    assert(tmpl_take((const uint8_t *)&saved, sizeof saved)); template_load();
    for (uint32_t k = 0; k < NPART; k++) assert(!trk[k].mpc_octave);
    /* Removing a track carries the octave with its sound, and adding a track starts neutral. */
    for (uint32_t k = 0; k < NPART; k++) trk[k].mpc_octave = (int8_t)k;
    assert(!track_remove(1));
    assert(trk[0].mpc_octave == 0 && trk[1].mpc_octave == 2 && trk[2].mpc_octave == 3 && trk[3].mpc_octave == 0);
    track_blank(3); assert(!trk[3].mpc_octave);
    puts("MPC octaves: project/template roundtrips, old zero fields, malformed state, and track lifecycle: OK");
}

static void track_follow(void)
{
    setup_mpc();
    ui.msg_t = 0;
    for (uint32_t k = 0; k < NPART; k++) {
        trk[k].mpc_octave = (int8_t)((int32_t)k - 2);
        midi_event(0x90, 0, MPC_PAD_NOTES[k][1], 100);
        frame();
        assert(song.sel == k && !midi_hint && !ui.msg_t);
        assert(input_octave(TSEL) == (int32_t)k - 2);
        midi_event(0x80, 0, MPC_PAD_NOTES[k][1], 0);
    }
    /* A burst ending back on the already-selected track must cancel an earlier different bank. */
    track_select(0);
    midi_event(0x90, 0, MPC_PAD_NOTES[2][1], 100);
    midi_event(0x90, 0, MPC_PAD_NOTES[0][1], 100);
    frame(); assert(song.sel == 0 && !ui.msg_t);
    assert(mn_get(0, MPC_PAD_NOTES[2][1]) == 3u && mn_get(0, MPC_PAD_NOTES[0][1]) == 1u);
    midi_event(0x80, 0, MPC_PAD_NOTES[0][1], 0);
    assert(mn_get(0, MPC_PAD_NOTES[2][1]) == 3u);
    midi_event(0x80, 0, MPC_PAD_NOTES[2][1], 0);
    frame(); assert(song.sel == 0);
    /* Invalid/absent banks and release events do not change focus. */
    midi_event(0x90, 0, 60, 100); frame(); assert(song.sel == 0);
    song.ntrk = 2;
    midi_event(0x90, 0, MPC_PAD_NOTES[3][1], 100); frame(); assert(song.sel == 0);
    song.ntrk = NPART;
    /* Follow works on parameter pages too, preserving the current page and unrelated feedback. */
    go_title("PATTERN"); uint32_t page = ui.page;
    ui_message("KEPT");
    midi_event(0x90, 0, MPC_PAD_NOTES[1][1], 100); frame();
    assert(song.sel == 1 && ui.page == page && !ui.home && msg_is("KEPT"));
    midi_event(0x80, 0, MPC_PAD_NOTES[1][1], 0);
    /* A mode change discards a queued follow; normal MIDI is silent and keeps manual selection. */
    go_home(); ui.msg_t = 0; track_select(0);
    midi_event(0x90, 0, MPC_PAD_NOTES[2][1], 100);
    for (uint32_t k = 0; k < NPART; k++) trk[k].p[P_QUANT] = Q_OFF;
    frame(); assert(song.sel == 0 && !ui.msg_t);
    midi_event(0x80, 0, MPC_PAD_NOTES[2][1], 0);
    midi_event(0x90, 1, 60, 100); frame();
    assert(song.sel == 0 && !ui.msg_t && !toast_shown && mn_get(1,60) == 2u);
    midi_event(0x80, 1, 60, 0);
    ui.msg_t = 0;
    midi_hint = 2; /* normal notice already queued when MPC is enabled */
    for (uint32_t k = 0; k < NPART; k++) trk[k].p[P_QUANT] = Q_MPC;
    frame(); assert(!ui.msg_t && song.sel == 0);
    for (uint32_t source = 1; source <= 2u; source++) {
        setup_mpc(); ui.msg_t = 0;
        song.g[G_MIDI] = (int16_t)(source - 1u);
        song.rec = 15u;
        for (uint32_t k = 0; k < NPART; k++) memset(trk[k].step, 0, sizeof trk[k].step);
        for (uint32_t k = 0; k < NPART; k++) {
            uint32_t note = MPC_PAD_NOTES[k][1];
            if (source == 1u) midi_in_event(0x09u | 0x90u << 8 | note << 16 | 100u << 24);
            else { um_byte(0x90); um_byte((uint8_t)note); um_byte(100); }
            events_block(CTL); frame();
            assert(song.sel == k && !ui.msg_t && song.playing && song.rec == 15u);
            if (source == 1u) midi_in_event(0x08u | 0x80u << 8 | note << 16);
            else { um_byte(0x80); um_byte((uint8_t)note); um_byte(0); }
            events_block(CTL); frame();
            assert(trk[k].step[0].n == 1u && trk[k].step[0].note[0] == 60u && !midi_owners[k]);
        }
    }
    puts("MPC follow: latest valid pad selects its track silently, releases/ignored pads do not, normal MIDI unchanged: OK");
    puts("MPC follow: USB/TRS pad input selects and records into its own armed track without routing notices: OK");
}

static void drum_pads(void)
{
    /* Expected GM outputs in physical pad order, bottom row first. Do not derive from the firmware's table. */
    static const uint8_t notes[2][16] = {
        {36, 38, 39, 37, 42, 46, 56, 49, 41, 45, 50, 70, 64, 61, 60, 75},
        {36, 38, 39, 37, 42, 46, 51, 49, 41, 45, 50, 51, 36, 38, 42, 46},
    };
    for (uint32_t kit = DK_808; kit <= DK_FM; kit++) {
        setup_mpc();
        for (uint32_t k = 0; k < NPART; k++) {
            track_t *t = &trk[k];
            set_engine_of(t, ENGI_DRUM); t->p[P_E0] = (int16_t)kit;
            t->p[P_QUANT] = Q_MPC; t->p[P_SCALE] = 2; t->p[P_ROOT] = 11;
            t->p[P_TRANS] = 24; t->p[P_MPCDEG] = 7;
            for (int32_t oct = -3; oct <= 3; oct++) {
                t->mpc_octave = (int8_t)oct;
                for (uint32_t n = 0; n < 128u; n++) {
                    uint32_t want = KB_SILENT;
                    for (uint32_t p = 0; p < 16u; p++)
                        if (n == MPC_PAD_NOTES[k][p]) want = notes[kit == DK_909][p];
                    assert(midi_map(t, n) == want);
                }
                /* Native keys retain their chromatic drum map and bank octave. */
                for (uint32_t key = 0; key < 27u; key++)
                    assert(kb_map(t, key) == (uint32_t)clamp(29 + 12 * oct + (int32_t)key, 0, 127));
            }
            for (uint32_t q = Q_OFF; q < Q_MPC; q++) {
                t->p[P_QUANT] = (int16_t)q;
                for (uint32_t n = 0; n < 128u; n++) assert(midi_map(t, n) == n);
            }
        }
    }
    for (uint32_t source = 1; source <= 2u; source++)
        for (uint32_t kit = DK_808; kit <= DK_FM; kit++) {
            setup_mpc(); song.g[G_MIDI] = (int16_t)(source - 1u); ui.msg_t = 0;
            for (uint32_t k = 0; k < NPART; k++) {
                track_t *t = &trk[k]; set_engine_of(t, ENGI_DRUM); t->p[P_E0] = (int16_t)kit;
                t->p[P_QUANT] = Q_MPC; t->mpc_octave = 3;
                for (uint32_t p = 0; p < 16u; p++) {
                    seq_stop(); memset(t->step, 0, sizeof t->step); song.rec = (uint8_t)(1u << k);
                    uint32_t src = MPC_PAD_NOTES[k][p], want = notes[kit == DK_909][p];
                    if (source == 1u) midi_in_event(0x09u | 0x90u << 8 | src << 16 | 100u << 24);
                    else { um_byte(0x90); um_byte((uint8_t)src); um_byte(100); }
                    events_block(CTL); frame();
                    assert(song.sel == k && !ui.msg_t && mn_get(0, src) == k + 1u && midi_owners[k] == 1u);
                    assert(song.playing && midi_note_held(t, want));
                    uint32_t lane = drum_lane(want);
                    if (want == DRUM_LANE_NOTE[lane]) assert(t->step[0].hit & (1u << lane));
                    else assert(t->step[0].n == 1u && t->step[0].note[0] == want);
                    if (source == 1u) midi_in_event(0x08u | 0x80u << 8 | src << 16);
                    else { um_byte(0x80); um_byte((uint8_t)src); um_byte(0); }
                    events_block(CTL); frame();
                    assert(!mn_get(0, src) && !midi_owners[k] && !midi_note_held(t, want));
                }
            }
        }
    /* Duplicated 909 hits retain separate pad ownership across a kit/octave change. */
    setup_mpc(); set_engine_of(&trk[0], ENGI_DRUM); trk[0].p[P_QUANT] = Q_MPC; trk[0].p[P_E0] = DK_909;
    int32_t out[2u * CTL];
    for (uint32_t b = 0; b < 8u; b++) mix_block(out, CTL); /* finish the normal engine switch */
    assert(trk[0].engine == ENGI_DRUM && !trk[0].xf_on);
    midi_event(0x90, 0, MPC_PAD_NOTES[0][0], 100);
    midi_event(0x90, 0, MPC_PAD_NOTES[0][12], 119);
    assert(midi_owners[0] == 2u && midi_note_held(&trk[0], 36));
    uint32_t struck = 0;
    for (uint32_t v = 0; v < NVOICE; v++)
        struck |= trk[0].v[v].active && trk[0].v[v].note == 36u && trk[0].v[v].mvel == 119u;
    assert(struck); /* the second pad actually retriggers at its own velocity */
    trk[0].p[P_E0] = DK_808; trk[0].mpc_octave = -3;
    midi_event(0x80, 0, MPC_PAD_NOTES[0][0], 0);
    assert(midi_owners[0] == 1u && midi_note_held(&trk[0], 36));
    midi_event(0x80, 0, MPC_PAD_NOTES[0][12], 0);
    assert(!midi_owners[0] && !midi_note_held(&trk[0], 36));
    puts("MPC drums: all banks/notes/kits/octaves, native keys and normal MIDI preserved: OK");
    puts("MPC drums: USB/TRS pads select silently, record GM hits and release their original owners: OK");
}

static void capture_follow_pad(uint32_t source,uint32_t note,uint32_t vel)
{
    uint32_t status=vel?0x90u:0x80u;
    if(source==1u)midi_in_event((vel?9u:8u)|status<<8|note<<16|vel<<24);
    else {um_byte((uint8_t)status);um_byte((uint8_t)note);um_byte((uint8_t)vel);}
    events_block(CTL);
}
static void capture_follow(void)
{
    for (uint32_t source=1;source<=2u;source++) {
        setup_mpc();song.g[G_MIDI]=(int16_t)(source-1u);
        seq_start();seq_advance(0);
        capture_follow_pad(source,MPC_PAD_NOTES[0][1],100);
        uint32_t first=cap_w;
        capture_follow_pad(source,MPC_PAD_NOTES[1][1],110);
        assert(song.sel==0 && cap_track==1 && cap_w==first+1u && cap_from==first);
        uint32_t retained=cap_from;
        frame();
        assert(song.sel==1 && cap_track==1 && cap_from==retained && cap_w==first+1u);
        capture_follow_pad(source,MPC_PAD_NOTES[0][1],0);
        assert(cap_ev[first%CAP_N].tr==(1u|128u));
        seq_advance(100);
        capture_follow_pad(source,MPC_PAD_NOTES[1][1],0);
        assert(cap_ev[first%CAP_N].tr==1 && cap_ev[first%CAP_N].dur>0);
        capture_take();
        assert(recording_head[recording_owner(&trk[0])]==RECORD_MAX);
        assert(recording_head[recording_owner(&trk[1])]<RECORD_MAX);
        seq_stop();
    }
    puts("MPC Take Taker: first USB/TRS pad retained across selection; prior bank discarded: OK");
}
int main(void)
{
    buttons_and_mapping();
    persistence();
    track_follow();
    drum_pads();
    capture_follow();
    return 0;
}
