/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define MIDI_TIMING_NO_MAIN 1
#include "midi_timing_test.c"

static void raw_begin(void)
{
    timing_reset(2, 24000000u);
    timing_packet(timing_now, 0xFA, 0, 0, 2);
    timing_packet(timing_now, 0xF8, 0, 0, 2);
    events_block(0);
    /* These event-store fixtures exercise deliberately continuous, multi-loop
     * takes. Pattern-pass recording is covered through the shortcut path below. */
    song.rec_pass = 0;
}
static void raw_replay(void)
{
    seq_stop(); song.rec = 0;
    seq_start(); seq_advance(0);
}
static int repeated_hits(void)
{
    int bad = 0;
    for (uint32_t source = 0; source < 3; source++) {
        raw_begin(); track_t *t = &trk[0];
        seq_advance(600);
        if (!source) input_on(t, 60, 70);
        else { timing_packet(timing_now, 0x90, 60, 70, source); events_block(0); }
        seq_advance(300);
        if (!source) input_off(t, 60);
        else { timing_packet(timing_now, 0x80, 60, 0, source); events_block(0); }
        seq_advance(900);
        if (!source) input_on(t, 60, 111);
        else { timing_packet(timing_now, 0x90, 60, 111, source); events_block(0); }
        seq_advance(600);
        if (!source) input_off(t, 60);
        else { timing_packet(timing_now, 0x80, 60, 0, source); events_block(0); }
        bad += check("OFF records two same-pitch hits inside one step with separate velocities/durations",
                     !t->p[P_RECQ] && recording[0].on == 6400 && recording[1].on == 19200 &&
                     recording[0].duration == 3200 && recording[1].duration == 6400 &&
                     recording[0].vel == 70 && recording[1].vel == 111);
        recorded_note_t saved[2]; memcpy(saved, recording, sizeof saved);
        raw_replay(); seq_advance(599);
        int exact = !gate_note(t, 60);
        seq_advance(1); exact &= gate_note(t, 60);
        seq_advance(299); exact &= gate_note(t, 60);
        seq_advance(1); exact &= !gate_note(t, 60);
        seq_advance(899); exact &= !gate_note(t, 60);
        seq_advance(1); exact &= gate_note(t, 60);
        seq_advance(600); exact &= !gate_note(t, 60);
        bad += check("OFF replays both hits at their exact captured edges through local/USB/TRS input", exact);
        seq_stop(); t->p[P_RECQ] = 3; raw_replay();
        bad += check("changing QNT after a take cannot move its playback", !gate_note(t, 60));
        seq_stop(); t->p[P_RECQ] = 0; raw_replay(); seq_advance(599);
        bad += check("changing the next take's QNT preserves the completed take byte-for-byte",
                     !gate_note(t,60) && !memcmp(saved, recording, sizeof saved));
    }
    return bad;
}
static int independent_lengths(void)
{
    raw_begin(); track_t *t = &trk[0];
    seq_advance(600); input_on(t,60,100); input_on(t,64,90);
    seq_advance(600); input_off(t,60);
    seq_advance(1200); input_off(t,64);
    int bad = check("overlapping chord notes store independent releases", recording[0].duration == 6400 && recording[1].duration == 19200);
    raw_replay(); seq_advance(600); seq_advance(600);
    bad += check("replay releases the short chord note while keeping the longer one held", !gate_note(t,60) && gate_note(t,64));
    seq_advance(1200);
    bad += check("the longer chord note releases at its own captured time", !gate_note(t,64));
    return bad;
}
static int raw_loop(void)
{
    raw_begin(); track_t *t = &trk[0];
    uint32_t on = 15u * 6144u + 5004u;
    seq_advance(on); input_on(t,60,100); seq_advance(2004); input_off(t,60);
    recorded_note_t original = recording[0];
    raw_replay(); int exact = !gate_note(t,60);
    seq_advance(on - 1u); exact &= !gate_note(t,60);
    seq_advance(1); exact &= gate_note(t,60);
    seq_advance(16u * 6144u - on); exact &= gate_note(t,60);
    seq_advance(2004u - (16u * 6144u - on)); exact &= !gate_note(t,60);
    int bad = check("an unquantized late-loop note crosses zero without an extra beginning onset", exact);
    seq_stop(); t->p[P_RECQ]=3; raw_replay();
    exact = !gate_note(t,60); seq_advance(on); exact &= gate_note(t,60);
    seq_advance(2004); exact &= !gate_note(t,60);
    bad += check("a completed unquantized take stays at its captured time after changing QNT",
                 exact && !memcmp(&original,&recording[0],sizeof original));
    return bad;
}
static int raw_storage(void)
{
    raw_begin(); track_t *t = &trk[0];
    seq_advance(606); input_on(t,60,101); seq_advance(1200); input_off(t,60); seq_stop();
    recorded_note_t original = recording[0];
    pattern_switch(t,7); t->p[P_RECQ]=5;
    seq_start(); seq_advance(0); seq_advance(1800); input_on(t,67,81); seq_advance(600); input_off(t,67); seq_stop();
    project_capture(&proj_scratch);
    int packed = bank_pack(proj_wire_u.raw,&proj_scratch,1);
    int valid = packed && bank_valid(proj_wire_u.raw,BANK_STORE_SIZE);
    int ok = valid;

    if(ok){ project_restore_runtime(&proj_scratch);bank_restore(proj_wire_u.raw); }
    ok &= t->pattern==7 && t->p[P_RECQ]==5 && !memcmp(&original,&recording[0],sizeof original) && recording[1].owner==7;
    pattern_switch(t,0); t->p[P_RECQ]=0; raw_replay(); seq_advance(605);ok &= !gate_note(t,60);seq_advance(1);ok &= gate_note(t,60);
    int bad = check("active and inactive bank recordings survive save/load and replay at their original time",ok);
    bad += check("the complete new project fits its base and two extension sectors", BANK_STORE_SIZE<=(5u * 4096u - 256u + 2u * 3840u));
    return bad;
}
static int raw_capacity(void)
{
    raw_begin(); track_t *t=&trk[0];
    for(uint32_t i=0;i<RECORD_MAX;i++) { seq_advance(54); input_on(t,60,100);seq_advance(12);input_off(t,60); }
    recorded_note_t saved[RECORD_MAX];memcpy(saved,recording,sizeof saved);
    seq_advance(12); input_on(t,62,99);input_off(t,62);
    int bad=check("a full recorder preserves all existing notes and reports the capacity limit",recording_full && !memcmp(saved,recording,sizeof saved));
    bad+=check("a full bank copy fails before changing the destination or source",pattern_copy(t,0,7)==2 && !memcmp(saved,recording,sizeof saved));
    seq_stop();step_clear(&t->step[0]);song.rec=1;seq_start();seq_advance(0);input_on(t,65,95);input_off(t,65);
    bad+=check("clearing recorded steps makes their event slots reusable",recorded_note(&recording[0])==65);
    return bad;
}
static int replace_during_replay(void)
{
    raw_begin();track_t *t=&trk[0];seq_advance(600);input_on(t,60,70);seq_advance(300);input_off(t,60);
    raw_replay();seq_advance(600);song.rec=1;input_on(t,60,111);seq_advance(600);input_off(t,60);
    int bad=check("recording over a sounding event replaces it without a duplicate entry",recording[0].vel==111 && recording[0].duration==6400 && !recording[1].vel);
    seq_stop();
    return bad;
}
static int standalone_and_template(void)
{
    raw_begin();track_t *t=&trk[0];seq_stop();pattern_switch(t,7);seq_start();seq_advance(0);
    seq_advance(600);input_on(t,60,90);seq_advance(300);input_off(t,60);seq_stop();
    project_capture(&proj_scratch);project_store_t bytes;project_t decoded;
    bank_p16=bank_wide=0;                     /* (a FUN16 on its own: its note directory, four parts of eight banks) */
    int ok=proj_pack(&bytes,&proj_scratch) && proj_import(&decoded,bytes.raw,sizeof bytes) && !project_restore_runtime(&decoded);
    raw_replay();seq_advance(599);ok &= t->pattern==7 && !gate_note(t,60);seq_advance(1);ok &= gate_note(t,60);
    int bad=check("standalone FUN16 restores the active bank and original recorded timing",ok);
    seq_stop();t->p[P_RECQ]=6;template_save();tmpl_t saved=tmpl;
    tmpl_take((const uint8_t *)&saved,sizeof saved);
    bad+=check("new templates preserve project timing quantization",tmpl.t[0].p[P_RECQ]==6);
    saved.magic=0x414C5054u;
    bad+=check("a template of an older format (TPLA) is not read",!tmpl_take((const uint8_t *)&saved,sizeof saved) && !template_used());
    bad+=check("recording timing QNT cannot become recorded knob automation",!motion_param(P_RECQ));
    return bad;
}
static int copy_and_edit(void)
{
    raw_begin(); track_t *t = &trk[0];
    seq_advance(600); input_on(t,60,100); seq_advance(300); input_off(t,60); seq_stop();
    int bad = check("bank copy includes original timing and independent duration", !pattern_copy(t,0,7) &&
                    recording[1].owner==7 && recording[1].on==recording[0].on && recording[1].duration==recording[0].duration);
    pattern_switch(t,7); raw_replay(); seq_advance(599);
    int exact = !gate_note(t,60);seq_advance(1);exact &= gate_note(t,60);
    bad += check("copied recordings replay at their captured time",exact);
    seq_stop(); go_page(GR_ROLL); step_history_sync();step_delete(t,0);step_history_end();raw_replay();seq_advance(600);
    bad += check("deleting the overview step silences its timed notes",!gate_note(t,60));
    seq_stop();int undone=step_history_apply(0);raw_replay();seq_advance(600);
    bad += check("manual undo restores the original timed note",undone && gate_note(t,60));
    seq_stop(); go_title("TEMPO"); turn(EN_K3,6); int16_t scale=t->p[P_QUANT];
    apply_preset_to(t,1);
    bad += check("project timing QNT survives sound loads and leaves scale QNT separate",
                 t->p[P_RECQ]==6 && trk[1].p[P_RECQ]==6 && t->p[P_QUANT]==scale);
    return bad;
}
static int slow_precision(void)
{
    raw_begin();track_t *t=&trk[0];t->p[P_SDIV]=9;t->p[P_SLEN]=64;
    uint32_t period=seq_div_samples(9), at=period*63u+12345u;
    seq_advance(at);input_on(t,60,100);seq_advance(1234);input_off(t,60);
    uint32_t captured=recording_on(t,&recording[0],period);
    uint32_t gate=(uint32_t)(((uint64_t)recording[0].duration<<(recording[0].owner>>5))*period/RECORD_UNIT);
    int bad=check("slow divisions retain onset/release precision even at the end of a 64-step pattern",
                  captured+3u>=at && captured<=at+3u && gate+6u>=1234u && gate<=1234u);
    raw_replay();seq_advance(captured-1);int exact=!gate_note(t,60);seq_advance(1);exact &= gate_note(t,60);
    seq_advance(gate);exact &= !gate_note(t,60);
    bad+=check("a long pattern replays its late unquantized note at the captured edge",exact);
    return bad;
}
static int full_recording_replay(void)
{
    raw_begin();track_t *t=&trk[0];
    /* 1024 distinct hits, spread across the whole loop. The overflow note must
     * not mutate the take, and its last event must survive project storage. */
    for(uint32_t i=0;i<RECORD_MAX;i++) {seq_advance(54);input_on(t,60,70+i%58u);seq_advance(12);input_off(t,60);}
    seq_stop();recorded_note_t last=recording[RECORD_MAX-1];
    project_capture(&proj_scratch);int ok=bank_pack(proj_wire_u.raw,&proj_scratch,1) && bank_valid(proj_wire_u.raw,BANK_STORE_SIZE);
    if(ok){ok=!project_restore_runtime(&proj_scratch);bank_restore(proj_wire_u.raw);}
    ok &= !memcmp(&last,&recording[RECORD_MAX-1],sizeof last);
    raw_replay();
    for(uint32_t i=0;i<RECORD_MAX;i++) {
        seq_advance(53);ok &= !gate_note(t,60);seq_advance(1);ok &= gate_note(t,60);
        seq_advance(12);ok &= !gate_note(t,60);
    }
    int bad=check("all 1024 notes survive save/load and replay with exact independent edges",ok);
    seq_stop();t->p[P_RECQ]=3;raw_replay();seq_advance(98304);
    t->p[P_RECQ]=0;seq_stop();raw_replay();seq_advance(53);ok=!gate_note(t,60);seq_advance(1);ok &= gate_note(t,60);
    bad+=check("a full completed take keeps its stored timing when the next-take QNT changes",ok);
    seq_stop();
    return bad;
}
static int control_tick_precision(void)
{
    int bad = 0;
    for (uint32_t clock = 0; clock < 2; clock++) {
        for (uint32_t div = 0; div < 10; div++) {
            raw_begin();
            if (!clock) { seq_stop(); midi_sync_active = 0; song.g[G_BPM] = 40; }
            track_t *t = &trk[0]; t->p[P_SDIV] = div;
            uint32_t period = seq_div_samples(div), remainder = period - 1u;
            song.playing = 1; song.rec = 1; t->seq_pos = 0;
            input_on(t, 60, 100);
            recording_run[0].held = 0;
            recording_fraction[0] = remainder;
            uint64_t total = remainder;
            for (uint32_t tick = 0; tick < 127u; tick++) {
                recording_tick(t, CTL); total += CTL * RECORD_UNIT;
            }
            bad += check("short audio ticks retain exact fractional recording time at every division", recording_fraction[0] == total % period && recording_run[0].held == (total / period > 8388607u ? 8388607u : total / period));
            recording_tick(t, 70000u); total += 70000ull * RECORD_UNIT;
            bad += check("large external-clock batches retain the wide recording calculation", recording_fraction[0] == total % period && recording_run[0].held == (total / period > 8388607u ? 8388607u : total / period));
        }
    }
    seq_stop(); return bad;
}

static void pass_begin(uint32_t source, uint32_t len, uint32_t q)
{
    timing_reset(source, 24000000u);
    memset(live_held, 0, sizeof live_held);
    memset(&motion, 0, sizeof motion);
    track_t *t = &trk[0];
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_RECQ] = (int16_t)q;
    t->p[P_SSWING] = 37;
    song.rec = 0;
    if (source) {
        timing_packet(timing_now, 0xFA, 0, 0, source);
        timing_packet(timing_now, 0xF8, 0, 0, source);
        events_block(0);
    } else { seq_start(); seq_advance(0); }
    /* The host HAL has no button LED wiring; assign two distinct positions. */
    ui_leds(); memset(led_pos, 0xff, sizeof led_pos);
    led_pos[panel.btn[B_REC]] = 0; led_pos[panel.btn[B_PLAY]] = 1;
}

static uint32_t pass_led(uint32_t plane, uint32_t id)
{
    uint8_t pos = led_pos[id];
    const uint8_t *led = plane ? fm1_led_dim[plane] : fm1_led;
    return pos != 0xffu && ((led[pos >> 3] >> (pos & 7u)) & 1u);
}
static int pattern_passes(void)
{
    int bad = 0;
    for (uint32_t source = 0; source < 3; source++) for (uint32_t q = 0; q <= 3; q += 3) {
        pass_begin(source, 3, q);
        track_t *t = &trk[0];
        uint32_t period = seq_div_samples(t->p[P_SDIV]), loop = recording_loop(t, period);
        seq_advance(300);
        rec_toggle(0);
        input_on(t, 60, 83); input_on(t, 62, 91); input_off(t, 62);
        motion_capture(t, P_LEVEL, 77);
        bad += check("shortcut arming during playback waits: keys and motion cannot write before loop zero",
            song.rec == 1 && !song.rec_latch && song.rec_wait == 1 && !song.rec_pass && !rec_on(t) &&
            recording_head[0] == RECORD_MAX && !motion.count && gate_note(t, 60));
        fm1_ms = 0; ui_leds();
        bad += check("waiting REC breathes while playback stays lit", pass_led(2, panel.btn[B_REC]) &&
            !pass_led(0, panel.btn[B_REC]) && pass_led(0, panel.btn[B_PLAY]));
        rec_toggle(0); seq_advance(100);
        bad += check("a second REC cancels waiting without losing a held live note",
            !song.rec && !song.rec_wait && !song.rec_pass && gate_note(t, 60) && recording_head[0] == RECORD_MAX);
        rec_toggle(0); seq_advance(loop - 401u);
        bad += check("the final sample before the selected pattern ends is still waiting", !rec_on(t) && song.rec_wait == 1);
        seq_advance(1);
        ui_notices();
        bad += check("INT/USB/TRS punch-in at swung loop zero retains a held note's original velocity",
            rec_on(t) && !song.rec_wait && song.rec_pass == 1 && t->seq_idx == 0 && !t->seq_pos &&
            recorded_note(&recording[0]) == 60 && recording[0].vel == 83 && !recording[0].on && !(recording[0].step & 63u) && !recording[1].vel);
        bad += check("punch-in clears the persistent waiting header", !head_waiting());
        ui_leds();
        bad += check("REC becomes steady when the pattern pass starts", !pass_led(2, panel.btn[B_REC]) && pass_led(0, panel.btn[B_REC]));
        motion_capture(t, P_LEVEL, 78);
        seq_advance(500); input_off(t, 60);
        seq_advance(loop - 1500u); input_on(t, 67, 105);
        seq_advance(999);
        bad += check("recording remains active through the last sample of its pass", rec_on(t) && (recording_flags[1] & 1u));
        seq_advance(1);
        uint32_t gate = (uint32_t)(((uint64_t)recording[1].duration << (recording[1].owner >> 5)) * period / RECORD_UNIT);
        bad += check("the end boundary seals held notes and disarms while playback and live monitoring continue",
            song.playing && !song.rec && !song.rec_wait && !song.rec_pass && !t->seq_idx && !t->seq_pos &&
            !(recording_flags[1] & 1u) && !t->rh_n && gate >= 998u && gate <= 1000u && gate_note(t, 67));
        bad += check("a late note snapped to zero is eligible immediately on the first playback loop",
            !q || (recording_left(1) && recording_refs[0][67]));
        recorded_note_t saved[2]; memcpy(saved, recording, sizeof saved);
        motion_capture(t, P_LEVEL, 79);
        input_off(t, 67); input_on(t, 72, 100); input_off(t, 72);
        seq_advance(loop * 2u + 17u);
        bad += check("later loops and knob/input gestures cannot extend or overwrite the completed take",
            !memcmp(saved, recording, sizeof saved) && !recording[2].vel && motion.count == 1 && motion.event[0].value == 78);
        seq_stop();
    }

    pass_begin(0, 1, 0); track_t *t = &trk[0];
    uint32_t period = seq_div_samples(t->p[P_SDIV]);
    rec_toggle(0); seq_advance(recording_loop(t, period));
    bad += check("a one-step pattern activates at its wrap rather than disarming immediately", rec_on(t) && song.rec_pass == 1);
    seq_advance(recording_loop(t, period));
    bad += check("an empty one-step take also disarms after exactly one pass", song.playing && !song.rec);
    rec_toggle(0); seq_stop();
    bad += check("STOP clears pending runtime state and leaves the track armed for the next start", song.rec == 1 && !song.rec_wait && !song.rec_pass);
    seq_start(); seq_advance(0); input_on(t, 65, 99);
    seq_advance(recording_loop(t, period));
    bad += check("an armed stopped start records immediately and seals at the first real end", !song.rec && song.playing && recorded_note(&recording[0]) == 65 && !(recording_flags[0] & 1u));
    input_off(t, 65); seq_stop();

    pass_begin(0, 2, 0); t = &trk[0];
    t->p[P_SSWING] = 0; trk[1].p[P_SLEN] = 3; trk[1].p[P_SDIV] = t->p[P_SDIV]; trk[1].p[P_SSWING] = 0;
    seq_advance(100); rec_set(t, 1); rec_set(&trk[1], 1);
    seq_advance(2u * period - 100u);
    bad += check("arming another waiting track cancels the previous arm", song.rec == 2 && !rec_on(t) && !rec_on(&trk[1]) && song.rec_wait == 2);
    seq_advance(period);
    bad += check("only the newly armed track punches in at its own loop start", !rec_on(t) && rec_on(&trk[1]) && song.rec_pass == 2);
    seq_advance(period);
    bad += check("the old track's loop boundary does not interrupt the new take", song.rec == 2 && !rec_on(t) && rec_on(&trk[1]));
    seq_advance(2u * period);
    bad += check("the longer track finishes at its own end", !song.rec && song.playing);
    seq_stop();

    pass_begin(2, 3, 0); t = &trk[0]; period = seq_div_samples(t->p[P_SDIV]);
    uint32_t loop = recording_loop(t, period);
    seq_advance(100); rec_toggle(0); seq_advance(loop * 5u);
    bad += check("a large external-clock batch can punch in and finish an empty pass without missing either boundary",
        !song.rec && !song.rec_wait && !song.rec_pass && song.playing);
    seq_stop();

    raw_begin(); t = &trk[0]; period = seq_div_samples(t->p[P_SDIV]);
    input_on(t, 60, 84); seq_advance(period * 3u); input_off(t, 60);
    raw_replay(); seq_advance(100);
    uint32_t left = recording_left(0);
    rec_toggle(0); rec_toggle(0);
    bad += check("cancelling a waiting pass preserves sounding backing notes and their remaining gates",
        left && recording_left(0) == left && recording_refs[0][60] && gate_note(t, 60));
    seq_stop();

    pass_begin(0, 3, 0); t = &trk[0]; rec_toggle(0);
    seq_advance(recording_loop(t, seq_div_samples(t->p[P_SDIV])));
    input_on(t, 64, 102); seq_advance(500); rec_toggle(0);
    recorded_note_t finished = recording[0];
    seq_advance(500); input_off(t, 64);
    bad += check("manual punch-out seals the note immediately rather than recording a later key release",
        !song.rec && !song.rec_pass && !song.rec_wait && !(recording_flags[0] & 1u) &&
        !memcmp(&finished, &recording[0], sizeof finished));
    seq_stop();

    pass_begin(2, 3, 0); t = &trk[0];
    seq_advance(100); rec_toggle(0); seq_stop();
    midi_clock_transport(0xFB, fm1_ms);
    period = seq_div_samples(t->p[P_SDIV]); loop = recording_loop(t, period);
    bad += check("MIDI Continue keeps a stopped armed take waiting for a complete pattern", song.rec_wait == 1 && !rec_on(t));
    seq_advance(loop - 100u);
    bad += check("the continued take begins at loop zero", rec_on(t) && song.rec_pass == 1 && !t->seq_idx && !t->seq_pos);
    seq_advance(loop);
    bad += check("the continued take also disarms after one full pattern", !song.rec && song.playing);
    seq_stop();

    pass_begin(0, 3, 0); t = &trk[0]; rec_toggle(0);
    input_on(t, 60, 73);
    for (uint32_t i = 0; i < CAP_N + 1u; i++) { input_on(t, 62, 95); input_off(t, 62); }
    seq_advance(recording_loop(t, seq_div_samples(t->p[P_SDIV])));
    bad += check("a held punch-in note retains its velocity even after the jam capture ring wraps",
        recording[0].vel == 73 && recorded_note(&recording[0]) == 60 && !recording[1].vel);
    input_off(t, 60); seq_stop(); led_pos_init(); return bad;
}

static int explicit_rec(void)
{
    int bad = 0;
    for (uint32_t source = 0; source < 3; source++) for (uint32_t q = 0; q <= 3; q += 3) {
        pass_begin(source, 3, q);
        track_t *t = TSEL;
        uint32_t loop = recording_loop(t, seq_div_samples(t->p[P_SDIV]));
        seq_advance(100); press(B_REC);
        bad += check("explicit REC queues latched recording at the next loop", song.rec_latch == 1 && song.rec_wait == 1 && !rec_on(t));
        seq_advance(loop - 100u);
        seq_advance(loop - 500u); input_on(t, 60, 93);
        seq_advance(1000);
        bad += check("explicit REC stays active across loop zero without sealing a held note",
            song.playing && rec_on(t) && song.rec_latch == 1 && (recording_flags[0] & 1u));
        input_off(t, 60); input_on(t, 67, 104); input_off(t, 67);
        motion_capture(t, P_LEVEL, 79);
        seq_advance(loop * 2u);
        bad += check("later loops still record notes and motion after explicit REC",
            rec_on(t) && recorded_note(&recording[1]) == 67 && recording[1].vel == 104 && motion.count == 1 && motion.event[0].value == 79);
        press(B_REC);
        bad += check("a second explicit REC finishes and quantizes the take while playback continues",
            !song.rec && !song.rec_latch && !song.rec_wait && !song.rec_pass && song.playing &&
            !(recording_flags[0] & 1u) && (!q || !recording[1].on));
        seq_stop();

        pass_begin(source, 1, q); t = TSEL;
        loop = recording_loop(t, seq_div_samples(t->p[P_SDIV]));
        press(B_REC); seq_stop();
        input_on(t, 64, 87);
        seq_advance(loop * 3u);
        bad += check("a note-started explicit REC stays armed through one-step loops",
            rec_on(t) && song.rec_latch == 1 && recorded_note(&recording[0]) == 64);
        seq_stop();
        bad += check("STOP finalizes a latched take and keeps explicit arming for the next start",
            song.rec == 1 && song.rec_latch == 1 && !song.rec_pass && !song.rec_wait && !(recording_flags[0] & 1u));
        input_off(t, 64); seq_start(); seq_advance(loop * 2u);
        bad += check("PLAY restarts latched recording across subsequent loops", rec_on(t) && song.rec_latch == 1);
        press(B_REC); seq_stop();
    }

    pass_begin(0, 2, 0); track_t *t = TSEL;
    uint32_t period = seq_div_samples(t->p[P_SDIV]);
    trk[1].p[P_SLEN] = 3; trk[1].p[P_SDIV] = t->p[P_SDIV];
    t->p[P_SSWING] = trk[1].p[P_SSWING] = 0;
    press(B_REC); rec_set(&trk[1], 1);
    bad += check("a one-pass arm replaces explicit REC and clears its latch", song.rec == 2 && song.rec_wait == 2 && !song.rec_latch);
    seq_advance(6u * period);
    bad += check("the replacement shortcut finishes without rearming the old explicit REC track",
        !song.rec && !song.rec_latch && !rec_on(t) && !rec_on(&trk[1]));
    seq_stop(); led_pos_init();
    return bad;
}

static int exclusive_recording(void)
{
    int bad = 0;
    for (uint32_t source = 0; source < 3; source++) for (uint32_t q = 0; q <= 3; q += 3) {
        pass_begin(source, 4, q);
        track_t *t = TSEL;
        t->p[P_SSWING] = 0;
        press(B_REC); seq_stop(); seq_start(); seq_advance(0);
        seq_advance(600); input_on(t, 60, 91); seq_advance(300);
        rec_set(&trk[1], 1);
        recording_undo_t *take_undo = recording_undo_state();
        bad += check("transferring REC seals and quantizes held notes, finishes undo, and keeps monitoring",
            song.playing && song.rec == 2 && song.rec_wait == 2 && !song.rec_pass && !song.rec_latch &&
            !(recording_flags[0] & 1u) && recording[0].duration && (!q || !recording[0].on) &&
            !t->rh_n && !t->rh_ties && !t->rskip_n && gate_note(t, 60) && take_undo && !take_undo->active && !take_undo->track);
        recorded_note_t sealed = recording[0];
        seq_advance(200); input_off(t, 60);
        bad += check("a later physical release cannot extend the previous take",
            !memcmp(&sealed, &recording[0], sizeof sealed) && !gate_note(t, 60));
        rec_set(&trk[1], 0);
        bad += check("cancelling the replacement arm keeps the previous take undo available",
            !song.rec && recording_undo_state() == take_undo && recording_history(0) == 1 && !recording[0].vel);
        seq_stop();
    }
    /* Reasserting the same arm must retain its latch and waiting state. */
    pass_begin(0, 4, 0); seq_advance(100); press(B_REC);
    rec_set(TSEL, 1);
    bad += check("arming the same track again preserves its pending latched take",
        song.rec == 1 && song.rec_wait == 1 && song.rec_latch == 1 && !song.rec_pass);
    rec_set(TSEL, 0); seq_stop(); led_pos_init();
    return bad;
}

static int first_note_start(void)
{
    int bad = 0;
    for (uint32_t source = 0; source < 3; source++) {
        timing_reset(source, 24000000u);
        settings_countin = 2;
        track_t *t = TSEL;
        t->p[P_SLEN] = 16;
        t->step[5] = (step_t){{71}, 1, ST_NOTE, 0, 91};
        step_t untouched = t->step[5];
        go_page(GR_ROLL); cursor_set(5);
        uint32_t note = source ? 60u : kb_map(t, 5);
        if (!source) {
            /* Let the UI see the key before the audio ISR: it must not enter
             * the editing cursor as well as starting the recorded take. */
            fm1_in.notes |= 1u << 5; host_notes |= 1u << 5;
            frame();
        } else timing_packet(timing_now, 0x90, note, 86, source);
        events_block(0); frame();
        uint32_t voices = 0;
        for (uint32_t i = 0; i < NVOICE; i++) voices += t->v[i].active && t->v[i].gate && t->v[i].note == note;
        bad += check("the first armed key/USB/TRS note starts at zero, skips count-in and sounds only once",
            song.playing && rec_on(t) && song.rec_pass == 1 && !song.rec_wait && !seq_counting() && !transport_req &&
            !midi_sync_active && !t->seq_idx && !t->seq_pos && recorded_note(&recording[0]) == note &&
            recording[0].vel == (source ? 86u : 100u) && !recording[0].on && !(recording[0].step & 63u) &&
            !recording[1].vel && voices == 1 && !ui.entry_open && !memcmp(&untouched, &t->step[5], sizeof untouched));
        seq_advance(500);
        if (!source) fm1_in.notes &= ~(1u << 5);
        else timing_packet(timing_now, 0x80, note, 0, source);
        events_block(0);
        bad += check("the triggering note retains its actual release after a note-started take",
            !(recording_flags[0] & 1u) && recording[0].duration > 1);
        uint32_t loop = recording_loop(t, seq_div_samples(t->p[P_SDIV]));
        seq_advance(loop - 500u);
        bad += check("a note-started take still disarms at the pattern end and keeps playback running", !song.rec && song.playing);
        seq_stop(); settings_countin = 0;
    }

    timing_reset(0, 24000000u); song.rec = 0;
    input_on(TSEL, 60, 90);
    bad += check("an unarmed stopped note only auditions", !song.playing && recording_head[0] == RECORD_MAX);
    input_off(TSEL, 60);
    song.rec = 1; input_on(&trk[1], 67, 90);
    bad += check("a note routed to an unarmed track cannot start another track's recording", !song.playing && recording_head[0] == RECORD_MAX);
    input_off(&trk[1], 67);
    song.grid = 2; fm1_in.notes = 1u << 5; events_block(0);
    bad += check("a typing key cannot start an armed take", !song.playing && recording_head[0] == RECORD_MAX);
    fm1_in.notes = 0; events_block(0); song.grid = 0;

    timing_reset(0, 24000000u); settings_countin = 1;
    transport_req = 1; events_block(0); input_on(TSEL, 60, 90);
    bad += check("notes during an explicitly started count-in keep its normal pickup behavior", seq_counting() && !song.playing && recording_head[0] == RECORD_MAX);
    input_off(TSEL, 60); seq_stop(); settings_countin = 0;

    timing_reset(0, 24000000u); chain.armed = 1;
    input_on(TSEL, 60, 90);
    bad += check("a note cannot start an armed song arrangement as a recording", !song.playing && recording_head[0] == RECORD_MAX);
    input_off(TSEL, 60); seq_stop();

    timing_reset(0, 24000000u); track_t *t = TSEL;
    t->p[P_AMODE] = 1;
    input_on(t, 60, 90);
    bad += check("an armed ARP starts on the first note and records its generated onset at zero",
        song.playing && rec_on(t) && t->arp_note == 60 && recorded_note(&recording[0]) == 60 && !recording[0].on && !(recording[0].step & 63u) && !recording[1].vel);
    input_off(t, 60); seq_stop();

    timing_reset(0, 24000000u); t = TSEL;
    set_engine_of(t, ENGI_DRUM); t->engine = t->eng_req;
    input_on(t, 36, 105);
    bad += check("the first armed drum hit starts the take and is recorded at zero", song.playing && recorded_note(&recording[0]) == 36 && !recording[0].on && recording[0].vel == 105);
    input_off(t, 36); seq_stop();

    for (uint32_t source = 0; source < 3; source++) {
        timing_reset(source, 24000000u); t = TSEL; t->p[P_CHRD] = CH_MAJ;
        uint8_t chord[CHORD_MAX]; uint32_t root = source ? 60u : kb_map(t, 5);
        uint32_t count = chord_build(t, root, chord), exact = count == 3;
        if (!source) key_down(5);
        else { timing_packet(timing_now, 0x90, root, 97, source); events_block(0); }
        for (uint32_t i = 0; i < count; i++)
            exact &= recorded_note(&recording[i]) == chord[i] && !recording[i].on && !(recording[i].step & 63u);
        uint32_t voices = 0;
        for (uint32_t i = 0; i < NVOICE; i++) voices += t->v[i].active && t->v[i].gate;
        bad += check("a first-note chord starts once and captures every tone at zero without duplicate voices",
            song.playing && exact && !recording[count].vel && voices == count);
        if (!source) key_up(5);
        else { timing_packet(timing_now, 0x80, root, 0, source); events_block(0); }
        seq_stop();
    }

    for (uint32_t source = 1; source <= 2; source++) {
        pass_begin(source, 3, 0); t = TSEL; t->p[P_SSWING] = 0;
        seq_stop(); rec_set(t, 1); settings_countin = 2;
        input_on(t, 60, 87);
        bad += check("note start with an owned MIDI clock restarts at zero without count-in or changing clock source",
            song.playing && midi_sync_active == source && midi_clock.mode == source && !seq_counting() &&
            !t->seq_idx && !t->seq_pos && recorded_note(&recording[0]) == 60 && !recording[0].on);
        input_off(t, 60);
        uint32_t start = timing_now;
        for (uint32_t pulse = 0; pulse <= 18; pulse++) {
            timing_packet(start + pulse * 500000u, 0xF8, 0, 0, source);
            events_block(0);
        }
        bad += check("a note-started external-clock take completes at the master's pattern boundary", song.playing && !song.rec && !t->seq_idx && !t->seq_pos);
        seq_stop(); settings_countin = 0;
    }
    led_pos_init(); return bad;
}

static int take_quantization(void)
{
    int bad = 0;
    for (uint32_t source = 0; source < 3; source++) for (uint32_t q = 1; q <= 10; q++) {
        pass_begin(source, 16, q); track_t *t = TSEL;
        uint32_t period = seq_div_samples(t->p[P_SDIV]), loop = recording_loop(t, period);
        uint32_t a = step_samples(t, period, 0), b = step_samples(t, period, 1);
        rec_toggle(0); seq_advance(loop);
        seq_advance(a / 3u); input_on(t, 60, 73); input_on(t, 64, 91);
        seq_advance(101); input_off(t, 60); seq_advance(299); input_off(t, 64);
        uint32_t at = a + b / 3u;
        seq_advance(at - (a / 3u + 400u)); input_on(t, 67, 115);
        recording[2].pitch = -5; recording[2].length = 1;
        seq_advance(200); input_off(t, 67);
        recorded_note_t raw[3]; memcpy(raw, recording, sizeof raw);
        uint32_t target[3];
        for (uint32_t i = 0; i < 3; i++) target[i] = recording_quant_on(t, &raw[i], period, q);
        seq_advance(loop - at - 201u);
        bad += check("QNT leaves captured onsets unmodified until the recording's last sample",
            rec_on(t) && !memcmp(raw, recording, sizeof raw) && recording_on(t, &recording[0], period) == recording_raw_on(t, &raw[0], period));
        seq_advance(1);
        int exact = !song.rec && song.playing;
        for (uint32_t i = 0; i < 3; i++) {
            uint32_t snapped = recording_on(t, &recording[i], period);
            recorded_note_t properties = recording[i]; properties.on = raw[i].on; properties.step = raw[i].step;
            exact &= snapped + 1u >= target[i] && snapped <= target[i] + 1u &&
                !memcmp(&properties, &raw[i], sizeof properties) && !(recording_flags[i] & 8u) && recording_active(t, i);
        }
        bad += check("all ten swung grids commit at take end on INT/USB/TRS, preserving every note property", exact);
        recorded_note_t snapped[3]; memcpy(snapped, recording, sizeof snapped);
        t->p[P_RECQ] = 0; seq_advance(loop);
        t->p[P_RECQ] = q == 10 ? 1 : q + 1u; seq_stop(); raw_replay();
        bad += check("changing or disabling QNT never re-quantizes a completed take", !memcmp(snapped, recording, sizeof snapped));
        seq_stop(); project_capture(&proj_scratch);
        int packed = bank_pack(proj_wire_u.raw, &proj_scratch, 1);
        if (packed) { project_restore_runtime(&proj_scratch); bank_restore(proj_wire_u.raw); }
        bad += check("committed quantized timing and independent durations survive project save/load", packed && !memcmp(snapped, recording, sizeof snapped));
    }

    raw_begin(); track_t *t = TSEL;
    seq_advance(600); input_on(t, 60, 83); seq_advance(300); input_off(t, 60); seq_stop();
    recorded_note_t older = recording[0];
    t->p[P_RECQ] = 3; song.rec = 1; seq_start(); seq_advance(0);
    seq_advance(1800); input_on(t, 67, 99); seq_advance(400); input_off(t, 67); seq_stop();
    bad += check("an early STOP quantizes only the newly captured take, preserving older events",
        !memcmp(&older, &recording[0], sizeof older) && !recording[1].on && !(recording[1].step & 63u) && recording[1].vel == 99);

    raw_begin(); t = TSEL;
    seq_advance(600); input_on(t, 60, 91); seq_advance(300); input_off(t, 60);
    t->p[P_RECQ] = 3; rec_toggle(0);
    bad += check("manual disarm commits the grid selected when recording finishes", !song.rec && !recording[0].on);

    raw_begin(); t = TSEL; t->p[P_RECQ] = 3;
    seq_advance(600); input_on(t, 60, 91); seq_advance(300); input_off(t, 60);
    recorded_note_t original = recording[0]; t->p[P_RECQ] = 0; seq_stop();
    bad += check("QNT OFF at the end leaves the entire captured performance intact", !memcmp(&original, &recording[0], sizeof original));

    raw_begin(); t = TSEL; t->p[P_RECQ] = 3;
    t->step[1] = (step_t){{72}, 1, ST_NOTE, 0, 100}; step_t manual = t->step[1];
    seq_advance(2500); input_on(t, 60, 91); seq_advance(500); input_off(t, 60);
    /* A finer grid would move this note's rounded overview onto the manual cell. */
    t->p[P_RECQ] = 4; original = recording[0]; seq_stop();
    bad += check("a quantization collision preserves the take and unrelated manual notes atomically",
        !memcmp(&original, &recording[0], sizeof original) && !memcmp(&manual, &t->step[1], sizeof manual) && recording_quant_blocked);
    ui_notices(); bad += check("a blocked automatic quantization reports its occupied destination", msg_is("QNT STEP OCCUPIED"));

    raw_begin(); t = TSEL; t->p[P_RECQ] = 3;
    for (uint32_t i = 0; i < RECORD_MAX; i++) {
        seq_advance(54); input_on(t, 60, 70 + i % 58u); seq_advance(12); input_off(t, 60);
    }
    recorded_note_t last = recording[RECORD_MAX - 1u];
    seq_stop(); int full = recording[RECORD_MAX - 1u].vel == last.vel && recording[RECORD_MAX - 1u].duration == last.duration;
    for (uint32_t i = 0; i < RECORD_MAX; i++) full &= recording[i].vel && !recording[i].on && recording_active(t, i);
    bad += check("a full 1024-note take commits without allocating or losing repeated hits and durations", full);

    for (uint32_t clock = 0; clock < 2; clock++) for (uint32_t div = 0; div < 10; div++) {
        raw_begin(); t = TSEL;
        if (!clock) { seq_stop(); midi_sync_active = 0; song.g[G_CLOCK] = 0; song.g[G_BPM] = 40; }
        t->p[P_SLEN] = 64; t->p[P_SDIV] = (int16_t)div;
        t->p[P_SSWING] = 87; song.g[G_SWING] = 23; t->p[P_RECQ] = (int16_t)(div % 6u + 1u);
        if (!clock) { seq_start(); seq_advance(0); }
        uint32_t period = seq_div_samples(div), span = step_samples(t, period, 63);
        uint32_t at = recording_prefix(t, period, 63) + span * 2u / 5u;
        seq_advance(at); input_on(t, 60, 87); seq_advance(span / 20u); input_off(t, 60);
        recorded_note_t raw = recording[0];
        uint32_t expected = recording_quant_on(t, &raw, period, (uint32_t)t->p[P_RECQ]);
        seq_stop(); uint32_t actual = recording_on(t, &recording[0], period);
        uint32_t tolerance = step_samples(t, period, recording[0].step & 63u) / RECORD_UNIT + 1u;
        bad += check("late 64-step quantization retains format precision at every division and maximum combined swing",
            actual + tolerance >= expected && actual <= expected + tolerance &&
            recording[0].duration == raw.duration && recording[0].owner == raw.owner && recording_active(t, 0));
    }

    raw_begin(); t = TSEL; t->p[P_SLEN] = 1; t->p[P_RECQ] = 4;
    seq_advance(2500); input_on(t, 60, 91); seq_advance(100); input_off(t, 60); seq_stop();
    bad += check("a fractional quantized onset in a one-step loop keeps its overview inside the pattern",
        recording[0].on == RECORD_UNIT / 2u && recording_view(t, &recording[0]) == 0 && recording_active(t, 0));

    pass_begin(0, 3, 0); t = TSEL;
    uint32_t loop = recording_loop(t, seq_div_samples(t->p[P_SDIV]));
    rec_toggle(0); seq_advance(loop);
    seq_advance(1000); input_on(t, 60, 91); seq_advance(100); input_off(t, 60);
    seq_advance(loop - 1100u); original = recording[0];
    t->p[P_RECQ] = 3; rec_toggle(0); seq_advance(loop);
    seq_advance(2000); input_on(t, 67, 99); seq_advance(100); input_off(t, 67);
    seq_advance(loop - 2100u);
    bad += check("an automatically finished OFF take is never swept into a later enabled quantization pass",
        !memcmp(&original, &recording[0], sizeof original) && !recording[1].on && !(recording_flags[0] & 8u));
    seq_stop();

    raw_begin(); t = TSEL; t->p[P_RECQ] = 3;
    seq_advance(600); input_on(t, 60, 91); seq_advance(300); input_off(t, 60);
    original = recording[0]; t->p[P_RECQ] = 0; rec_toggle(0);
    raw_replay(); int raw_playback = !gate_note(t, 60);
    seq_advance(599); raw_playback &= !gate_note(t, 60); seq_advance(1); raw_playback &= gate_note(t, 60);
    bad += check("switching OFF before disarm discards prepared targets and replays the actual captured edge",
        raw_playback && !memcmp(&original, &recording[0], sizeof original) && !(recording_flags[0] & 8u));
    seq_stop();
    led_pos_init(); return bad;
}
static int project_recording_quantization(void)
{
    raw_begin(); seq_stop(); song.ntrk=NTRK;
    for(uint32_t k=NPART;k<NTRK;k++) track_blank(k);
    song.sel=2; go_title("TEMPO"); turn(EN_K3,3);
    int ok=1;
    for(uint32_t k=0;k<NTRK;k++) {
        song.sel=(uint8_t)k; track_t *t=TSEL; int16_t *value;
        page_desc(cur_page(),2,&value);
        ok &= value && *value==3;
        t->p[P_SLEN]=16; t->p[P_SDIV]=2; t->p[P_SSWING]=0;
        song.rec=(uint8_t)(1u<<k); song.rec_pass=song.rec_wait=0;
        seq_start(); seq_advance(0); seq_advance(600);
        input_on(t,60,90); seq_advance(300); input_off(t,60); seq_stop();
        uint32_t i=recording_head[recording_owner(t)];
        ok &= i<RECORD_MAX && recording[i].on==0 && recording[i].duration==3200;
    }
    int bad=check("TEMPO QNT chosen on one track quantizes takes on all eight audio/MIDI tracks",ok);
    turn(EN_K3,-3); ok=1;
    for(uint32_t k=0;k<NTRK;k++) ok &= !trk[k].p[P_RECQ];
    bad+=check("turning project QNT OFF on a MIDI track disables it on every track",ok);

    /* Old projects may contain different per-track values. The selected
     * track wins, including a MIDI track restored from the bank tail. */
    for(uint32_t sel=2;sel<NTRK;sel+=5) {
        song.sel=(uint8_t)sel;
        for(uint32_t k=0;k<NTRK;k++) trk[k].p[P_RECQ]=(int16_t)(k+1u);
        project_capture(&proj_scratch);
        ok=bank_pack(proj_wire_u.raw,&proj_scratch,1) && bank_valid(proj_wire_u.raw,BANK_STORE_SIZE);
        if(ok) { ok=!project_restore_runtime(&proj_scratch); bank_restore(proj_wire_u.raw); }
        for(uint32_t k=0;k<NTRK;k++) ok &= trk[k].p[P_RECQ]==(int16_t)(sel+1u);
        bad+=check("legacy project load shares the selected audio/MIDI track's recording grid",ok);
        template_save(); project_new_blank(); template_load(); ok=1;
        for(uint32_t k=0;k<NTRK;k++) ok &= trk[k].p[P_RECQ]==(int16_t)(sel+1u);
        bad+=check("project-wide recording QNT survives a new project loaded from its template",ok);
    }
    return bad;
}
#ifndef RECORDING_NO_MAIN
int main(void)
{
    int bad=project_recording_quantization()+exclusive_recording()+explicit_rec()+take_quantization()+first_note_start()+pattern_passes()+control_tick_precision()+repeated_hits()+independent_lengths()+raw_loop()+raw_storage()+raw_capacity()+copy_and_edit()+slow_precision()+replace_during_replay()+standalone_and_template()+full_recording_replay();
    printf("%s\n",bad?"RAW RECORDING TEST FAILED":"Raw recording tests passed");return !!bad;
}
#endif
