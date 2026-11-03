/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Host test of the SMK-37 velocity keybed (hal/smk_input.h smk__keys, smk__pad, smk_curve, smk_trim_load) against
 * a model of the scan and of the two contacts of a key. The scan is the TIMER5 one (fm1_input_tick): one column
 * every 100 us, the frame (smk__keys) after column 15; the consumer is seq.c's (keyboard_block: the level of
 * fm1_in.notes once per audio half, 2.9 ms, the outer keys from smk_extq, a strike while held from notes_retrig).
 * Checked, for every key:
 *   - a clean press (contact 1, then contact 2 after a gap of 0..100 ms) at every phase of the scan: one note-on,
 *     one note-off, no note while only contact 1 is closed, velocity 1..127 falling with the gap, 127 at 0 ms;
 *   - the contacts bouncing (runs of 30..700 us for up to 3 ms) on the way down and up: one note each way;
 *   - contact 2 before contact 1, both in one frame, one tick apart: one note, full velocity;
 *   - struck again without a full lift (contact 2 reopens and recloses while contact 1 stays closed): a note
 *     each strike, a quick chatter of contact 2 (one frame) is not one;
 *   - fast repeats (contact 1 open for 2 ms, inside the release debounce): every press sounds;
 *   - a slow press (100 ms): still a note, at the floor;
 *   - the queue of the 10 outer keys: 30 presses in one block, their order and velocities, the overflow drops
 *     a press rather than a release;
 *   - a pad hit: the peak through its curve, one note-on and one note-off, a second hit right after;
 *   - the factory record (trimdat): taken when its marker and rising curves check out, the defaults otherwise;
 *   - VELOCITY: FIXED 100, SOFT above NORMAL above HARD in the middle, the ends kept, pads too.
 * The register helpers of the header are compiled, never called. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#define SMK_KEYS_RETRIG 1
#define __attribute__(x)
#include "../firmware/hal/smk_input.h"
#undef __attribute__

#define TICK_US 100u
#define BLOCK_US 2900u                              /* an audio half at 44.1 kHz (core.h HALF_FRAMES) */
static int fails;
static int check(const char *what, int ok)
{
    printf("smk keys: %-80s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
    return ok;
}

static uint32_t rng = 0x2545F491u;
static uint32_t rnd(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng % n;
}

/* a contact: the times (us) at which it toggles, starting open */
#define NEDGE 2048
typedef struct { uint32_t t[NEDGE]; uint32_t n; } contact_t;
static contact_t c1, c2;
static int closed_at(const contact_t *c, uint32_t us)
{
    uint32_t i, s = 0;
    for (i = 0; i < c->n && c->t[i] <= us; i++)
        s ^= 1u;
    return (int)s;
}
static void edge(contact_t *c, uint32_t us)
{
    uint32_t i = c->n;
    if (i >= NEDGE)
        return;
    while (i && c->t[i - 1u] > us) {                /* keep the list sorted */
        c->t[i] = c->t[i - 1u];
        i--;
    }
    c->t[i] = us;
    c->n++;
}
/* a bouncy change at t: the contact settles to `final` within `span` us */
static void bounce(contact_t *c, uint32_t t, uint32_t span, int final)
{
    uint32_t end = t + span;
    int s = final;
    edge(c, t);
    while (span) {
        uint32_t run = 30u + rnd(670u);
        if (t + run >= end)
            break;
        t += run;
        edge(c, t);
        s ^= 1;
    }
    if (s != final) {
        t += 30u + rnd(200u);
        edge(c, t);
    }
}

/* the scan */
static uint32_t sim_us, sim_col, key;              /* the key under test */
static void tick(void)
{
    uint32_t p = sim_col, a = SMK_KEYC[key][0], b = SMK_KEYC[key][1], raw = 0;
    if ((a >> 4) == p && closed_at(&c1, sim_us))
        raw |= 1u << (a & 15u);
    if ((b >> 4) == p && closed_at(&c2, sim_us))
        raw |= 1u << (b & 15u);
    fm1_in.raw[p] = (uint8_t)raw;
    sim_col = p + 1u == FM1_NCOL ? 0u : p + 1u;
    if (sim_col == 0u)
        smk__keys(sim_us * 24u);
    sim_us += TICK_US;
}

/* the consumer: note events of the key under test */
typedef struct { uint32_t us, on, vel; } ev_t;
#define NEV 256
static ev_t ev[NEV];
static uint32_t nev, prev_notes, c1_only_notes;   /* c1_only_notes: frames the note was on with only contact 1 closed */
static void event(uint32_t on, uint32_t vel)
{
    if (nev < NEV) {
        ev[nev].us = sim_us;
        ev[nev].on = on;
        ev[nev].vel = vel;
    }
    nev++;
}
static void block(void)                             /* keyboard_block: once per audio half */
{
    uint32_t cur = fm1_in.notes, ch = cur ^ prev_notes, bit, k;
    int32_t e;
#ifdef SMK_KEYS_RETRIG
    uint32_t rt = fm1_in.notes_retrig & cur & prev_notes;
    fm1_in.notes_retrig = 0;
#endif
    if (key >= SMK_KEY_F3 && key < SMK_KEY_F3 + 27u) {
        bit = 1u << (key - SMK_KEY_F3);
#ifdef SMK_KEYS_RETRIG
        if (rt & bit) {
            event(0, 0);
            event(1, fm1_in.key_vel[key]);
        }
#endif
        if (ch & bit)
            event((cur & bit) != 0u, fm1_in.key_vel[key]);
    }
    prev_notes = cur;
    while ((e = smk_extkey_take()) >= 0) {
        k = (uint32_t)e >> 8;
        if (k == key)
            event(((uint32_t)e & 0xFFu) != 0u, (uint32_t)e & 0xFFu);
    }
}
static void reset(void)
{
    uint32_t i;
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    memset(&c1, 0, sizeof c1);
    memset(&c2, 0, sizeof c2);
    smk_extq_w = smk_extq_r = 0;
    for (i = 0; i < SMK_NPAD; i++)
        fm1_in.pad_base[i] = 100u;                                  /* (fm1_input_init) */
    sim_us = sim_col = 0;
    nev = prev_notes = c1_only_notes = 0;
}
static void run(uint32_t until)
{
    uint32_t next_block = sim_us + BLOCK_US;
    while (sim_us < until) {
        tick();
        if (sim_col == 0u && key >= SMK_KEY_F3 && key < SMK_KEY_F3 + 27u &&
            ((fm1_in.notes >> (key - SMK_KEY_F3)) & 1u) && closed_at(&c1, sim_us) && !closed_at(&c2, sim_us) &&
            c1.n == 2u && c2.n == 2u && sim_us < c2.t[0])
            c1_only_notes++;
        if (sim_us >= next_block) {
            block();
            next_block += BLOCK_US;
        }
    }
    block();
}
static uint32_t ons(void) { uint32_t i, n = 0; for (i = 0; i < nev && i < NEV; i++) n += ev[i].on != 0u; return n; }
static uint32_t offs(void) { uint32_t i, n = 0; for (i = 0; i < nev && i < NEV; i++) n += ev[i].on == 0u; return n; }
static uint32_t first_vel(void) { uint32_t i; for (i = 0; i < nev && i < NEV; i++) if (ev[i].on) return ev[i].vel; return 0; }
static int alternate(void)                           /* on, off, on, off .. */
{
    uint32_t i;
    for (i = 0; i < nev && i < NEV; i++)
        if ((ev[i].on != 0u) != (i % 2u == 0u))
            return 0;
    return 1;
}

/* a clean press at t0: contact 1 closes, contact 2 `gap` us later, both open again `hold` us after that
 * (contact 2 first, contact 1 `lift` later); the bounces (us) of each change */
static void press(uint32_t t0, uint32_t gap, uint32_t hold, uint32_t lift, uint32_t bnc)
{
    if (bnc) {
        bounce(&c1, t0, bnc, 1);
        bounce(&c2, t0 + gap, bnc, 1);
        bounce(&c2, t0 + gap + hold, bnc, 0);
        bounce(&c1, t0 + gap + hold + lift, bnc, 0);
    } else {
        edge(&c1, t0);
        edge(&c2, t0 + gap);
        edge(&c2, t0 + gap + hold);
        edge(&c1, t0 + gap + hold + lift);
    }
}

static uint32_t vel_of_gap(uint32_t k, uint32_t gap_us)   /* a clean press of key k with that gap: its velocity */
{
    reset();
    key = k;
    press(10000u + rnd(1600u), gap_us, 60000u, 3000u, 0);
    run(10000u + gap_us + 90000u);
    return ons() == 1u && offs() == 1u ? first_vel() : 0u;
}

int main(void)
{
    uint32_t k, ph, bad, i;
    static const uint32_t GAPS[9] = {0, 1000, 2000, 4000, 8000, 16000, 32000, 64000, 100000};
    smk_trim_defaults();
    printf("smk keys: frame %u us, release %u frames, curve {%u %u %u %u %u}\n", (unsigned)(FM1_NCOL * TICK_US),
           (unsigned)SMK_KEY_REL, smk_trim_key[0][0], smk_trim_key[0][1], smk_trim_key[0][2], smk_trim_key[0][3],
           smk_trim_key[0][4]);

    /* clean presses: every key, every phase of the scan, several gaps */
    bad = 0;
    for (k = 0; k < FM1_NKEY; k++)
        for (ph = 0; ph < FM1_NCOL * TICK_US; ph += 170u) {
            uint32_t gap = GAPS[rnd(9)];
            reset();
            key = k;
            press(10000u + ph, gap, 50000u, 2000u, 0);
            run(10000u + gap + 80000u);
            if (ons() != 1u || offs() != 1u || !alternate() || first_vel() < 1u || first_vel() > 127u || c1_only_notes)
                bad++;
        }
    check("clean presses: one note-on and one note-off each, every key and phase, velocity 1..127", bad == 0u);
    check("no note while only contact 1 is closed", c1_only_notes == 0u);
    {
        uint32_t v[9], mono = 1;
        for (i = 0; i < 9u; i++)
            v[i] = vel_of_gap(17, GAPS[i]);
        printf("smk keys: velocity by contact gap (ms -> vel):");
        for (i = 0; i < 9u; i++) {
            printf(" %u->%u", (unsigned)(GAPS[i] / 1000u), (unsigned)v[i]);
            if (i && v[i] > v[i - 1u])
                mono = 0;
        }
        printf("\n");
        check("velocity falls with the contact gap, 127 at 0 ms, 1..127 at 100 ms", mono && v[0] == 127u && v[8] >= 1u && v[8] <= 40u);
        check("a normal stroke (8 ms) lands in the middle (60..110)", v[4] >= 60u && v[4] <= 110u);
        check("a slow press (100 ms) still sounds", v[8] >= 1u);
    }

    /* bouncy contacts */
    bad = 0;
    for (i = 0; i < 1500u; i++) {
        uint32_t gap = 2000u + rnd(30000u), hold = 20000u + rnd(80000u), end;
        reset();
        key = rnd(FM1_NKEY);
        press(10000u + rnd(1600u), gap, hold, 1000u + rnd(5000u), 500u + rnd(2500u));
        end = c1.t[c1.n - 1u] + 20000u;
        run(end);
        if (ons() != 1u || offs() != 1u || !alternate())
            bad++;
    }
    check("1500 bouncy presses and releases (up to 3 ms of bounce): one note-on and one note-off each", bad == 0u);
    {   /* no note ending while the key is held, none hanging after the last bounce */
        uint32_t hang = 0, early = 0;
        for (i = 0; i < 300u; i++) {
            uint32_t gap = 3000u + rnd(20000u), hold = 30000u + rnd(60000u), t0 = 10000u + rnd(1600u), j;
            reset();
            key = rnd(FM1_NKEY);
            press(t0, gap, hold, 2000u + rnd(3000u), 500u + rnd(2500u));
            run(c1.t[c1.n - 1u] + 20000u);
            for (j = 0; j < nev && j < NEV; j++)
                if (!ev[j].on) {
                    if (ev[j].us < t0 + gap + hold)
                        early++;
                    if (ev[j].us > c1.t[c1.n - 1u] + 12000u)
                        hang++;
                }
        }
        check("bouncy: no note-off while the key is held, none later than 12 ms after the last bounce", early == 0u && hang == 0u);
    }

    /* orders */
    reset(); key = 20; press(10000u, 0, 40000u, 2000u, 0); run(80000u);
    check("both contacts in one frame: one note at 127", ons() == 1u && offs() == 1u && first_vel() == 127u);
    reset(); key = 20; edge(&c2, 10000u); edge(&c1, 10500u); edge(&c2, 50000u); edge(&c1, 52000u); run(80000u);
    check("contact 2 before contact 1: one note at 127, one release", ons() == 1u && offs() == 1u && first_vel() == 127u);
    reset(); key = 20; edge(&c1, 10000u); edge(&c2, 10100u); edge(&c2, 50000u); edge(&c1, 52000u); run(80000u);
    check("contacts one tick apart: one note at 127", ons() == 1u && offs() == 1u && first_vel() == 127u);
    reset(); key = 20; edge(&c1, 10000u); edge(&c1, 30000u); run(60000u);
    check("contact 1 alone (a key brushed): no note", nev == 0u);
    reset(); key = 20; edge(&c1, 10000u); edge(&c1, 11500u); edge(&c1, 13000u); edge(&c2, 20000u); edge(&c2, 50000u); edge(&c1, 52000u); run(80000u);
    check("contact 1 bouncing open once while waiting for contact 2: one note", ons() == 1u && offs() == 1u);

    /* struck again without a full lift: contact 1 held, contact 2 opens 20 ms and closes again, 5 times */
    reset(); key = 20;
    edge(&c1, 10000u); edge(&c2, 15000u);
    for (i = 0; i < 5u; i++) { edge(&c2, 40000u + i * 50000u); edge(&c2, 60000u + i * 50000u); }
    edge(&c2, 300000u); edge(&c1, 305000u);
    run(340000u);
    printf("smk keys: partial-lift repeats: %u note-ons, %u note-offs\n", (unsigned)ons(), (unsigned)offs());
    check("struck again without a full lift (5 times): 6 note-ons and 6 note-offs, alternating", ons() == 6u && offs() == 6u && alternate());
    reset(); key = 20;
    edge(&c1, 10000u); edge(&c2, 15000u); edge(&c2, 40000u); edge(&c2, 41200u); edge(&c2, 80000u); edge(&c1, 85000u);
    run(120000u);
    check("contact 2 chattering open for one frame while held: no new note", ons() == 1u && offs() == 1u);
    {   /* the re-strike's velocity: from the time contact 2 was open */
        uint32_t fast, slow;
        reset(); key = 20; edge(&c1, 10000u); edge(&c2, 15000u); edge(&c2, 40000u); edge(&c2, 44000u); edge(&c2, 80000u); edge(&c1, 85000u);
        run(120000u);
        fast = nev >= 3u ? ev[2].vel : 0u;
        reset(); key = 20; edge(&c1, 10000u); edge(&c2, 15000u); edge(&c2, 40000u); edge(&c2, 70000u); edge(&c2, 100000u); edge(&c1, 105000u);
        run(140000u);
        slow = nev >= 3u ? ev[2].vel : 0u;
        printf("smk keys: re-strike after 4 ms -> %u, after 30 ms -> %u\n", (unsigned)fast, (unsigned)slow);
        check("a re-strike's velocity follows how fast contact 2 came back", fast >= 100u && slow < fast && slow >= 1u);
    }

    /* fast repeats: the key comes up for 2 ms only (inside the release debounce) */
    reset(); key = 12;
    for (i = 0; i < 8u; i++) press(10000u + i * 40000u, 6000u, 30000u, 2000u, 0);
    run(400000u);
    printf("smk keys: 8 fast repeats (2 ms up): %u note-ons, %u note-offs\n", (unsigned)ons(), (unsigned)offs());
    check("8 fast repeats with the key up for 2 ms: 8 note-ons and 8 note-offs", ons() == 8u && offs() == 8u && alternate());
    reset(); key = 12;
    for (i = 0; i < 8u; i++) press(10000u + i * 40000u, 6000u, 20000u, 2000u, 300u);
    run(400000u);
    check("8 fast repeats, bouncy: 8 note-ons and 8 note-offs", ons() == 8u && offs() == 8u && alternate());
    reset(); key = 12;
    for (i = 0; i < 10u; i++) press(10000u + i * 25000u, 3000u, 12000u, 8000u, 0);
    run(300000u);
    check("10 repeats at 40 Hz with a full lift: 10 note-ons and 10 note-offs", ons() == 10u && offs() == 10u && alternate());

    /* the outer keys: the queue */
    {
        uint32_t order = 1, n = 0;
        int32_t e;
        reset();
        for (i = 0; i < 30u; i++)
            smk__key_event(i % 2u ? 36u : 0u, 1u + i);
        for (i = 0; (e = smk_extkey_take()) >= 0; i++) {
            if (i < 30u && (uint32_t)e != ((i % 2u ? 36u : 0u) << 8 | (1u + i)))
                order = 0;
            n++;
        }
        check("outer keys: 30 events through the queue in order, with their velocities", order && n == 30u);
        reset();
        for (i = 0; i < 40u; i++)
            smk__key_event(0, i % 2u ? 0u : 90u);                /* 20 presses and 20 releases: 8 do not fit */
        for (n = 0, order = 0; (e = smk_extkey_take()) >= 0;)
            if (!((uint32_t)e & 0xFFu)) { n++; order = 1; } else { n--; order = 0; }
        check("queue overflow: a press is dropped, never a release (the last event is the release)", (int32_t)n >= 0 && order);
        reset(); key = 0;
        press(10000u, 5000u, 30000u, 2000u, 0);
        run(80000u);
        check("outer key 0 (C3): one note through the queue, one release", ons() == 1u && offs() == 1u && first_vel() > 60u);
        reset(); key = 36;
        press(10000u, 0, 30000u, 2000u, 0);
        run(80000u);
        check("outer key 36 (C6): a hard strike at 127", ons() == 1u && offs() == 1u && first_vel() == 127u);
    }

    /* pads: raw samples 2.4 ms apart (smk__pad), the hit as a white-key note */
    {
        static const uint16_t HIT[] = {30, 30, 30, 300, 620, 580, 400, 200, 60, 30, 30, 30, 30, 30};
        uint32_t p = 9, n = SMK_WHITE_NOTE[SMK_WHITE_OF_PAD(p)], on = 0, off = 0, v = 0, prev = 0, j;
        reset();
        for (j = 0; j < 80u; j++)
            smk__pad(p, 30);                                        /* the baseline settles */
        for (j = 0; j < sizeof HIT / sizeof HIT[0]; j++) {
            smk__pad(p, HIT[j]);
            if (((fm1_in.notes >> n) & 1u) && !prev) { on++; v = fm1_in.key_vel[SMK_KEY_F3 + n]; }
            if (!((fm1_in.notes >> n) & 1u) && prev) off++;
            prev = (fm1_in.notes >> n) & 1u;
        }
        printf("smk keys: pad %u hit peak 620 -> vel %u\n", (unsigned)p, (unsigned)v);
        check("a pad hit: one note-on with the peak's velocity, one note-off", on == 1u && off == 1u && v >= 30u && v <= 127u);
        for (j = 0; j < sizeof HIT / sizeof HIT[0]; j++) {
            smk__pad(p, HIT[j]);
            if (((fm1_in.notes >> n) & 1u) && !prev) on++;
            if (!((fm1_in.notes >> n) & 1u) && prev) off++;
            prev = (fm1_in.notes >> n) & 1u;
        }
        check("a second hit right after: another note", on == 2u && off == 2u);
    }

    /* the factory record */
    {
        static uint8_t rec[0x22C];
        uint32_t ok;
        memset(rec, 0xFF, sizeof rec);
        check("an erased record (0xFF): refused", smk_trim_load(rec) == 0);
        memcpy(rec + 0x224u, "trimdat", 8);
        for (i = 0; i < SMK_NPAD + FM1_NKEY; i++)
            for (k = 0; k < 5u; k++) {
                uint16_t v = (uint16_t)(100u + k * 200u + i);
                rec[8u + i * 10u + 2u * k] = (uint8_t)v;
                rec[9u + i * 10u + 2u * k] = (uint8_t)(v >> 8);
            }
        ok = smk_trim_load(rec) == 1;
        check("a record with the marker and rising curves: taken", ok && smk_trim_pad[3][0] == 103u && smk_trim_key[36][4] == 900u + 16u + 36u);
        rec[8u + 20u * 10u + 4u] = 0; rec[8u + 20u * 10u + 5u] = 0;   /* key 4's second point below its first */
        smk_trim_defaults();
        check("a record with a curve not rising: refused, the defaults kept", smk_trim_load(rec) == 0 && smk_trim_key[4][1] == 640u);
        check("the curve: 1 below the first point, 1023 above the last, 512 at the third", smk_curve(smk_trim_key[0], 5) == 1u && smk_curve(smk_trim_key[0], 1000) == 1023u && smk_curve(smk_trim_key[0], 800) == 512u);
    }

#ifdef SMK_KEYS_RETRIG
    /* VELOCITY */
    {
        uint32_t nrm, sft, hrd, fix, lo, hi;
        smk_vel_curve = 0; nrm = vel_of_gap(17, 16000u); lo = vel_of_gap(17, 100000u); hi = vel_of_gap(17, 0);
        smk_vel_curve = 1; sft = vel_of_gap(17, 16000u);
        check("SOFT: above NORMAL in the middle, the ends kept", sft > nrm && vel_of_gap(17, 0) == hi && vel_of_gap(17, 100000u) <= lo + 3u && vel_of_gap(17, 100000u) >= 1u);
        smk_vel_curve = 2; hrd = vel_of_gap(17, 16000u);
        check("HARD: below NORMAL in the middle, 127 at 0 ms", hrd < nrm && vel_of_gap(17, 0) == hi);
        smk_vel_curve = 3; fix = vel_of_gap(17, 16000u);
        check("FIXED: 100 whatever the gap", fix == 100u && vel_of_gap(17, 0) == 100u && vel_of_gap(17, 100000u) == 100u);
        printf("smk keys: 16 ms gap: NORMAL %u SOFT %u HARD %u FIXED %u\n", (unsigned)nrm, (unsigned)sft, (unsigned)hrd, (unsigned)fix);
        smk_vel_curve = 0;
    }
    /* the hit log (console `keys`) */
    {
        uint32_t n0 = smk_hit_n;
        (void)vel_of_gap(9, 8000u);
        printf("smk keys: log n %u -> %u, entry src %u a %u b %u vel %u\n", (unsigned)n0, (unsigned)smk_hit_n,
               smk_hit[n0 % SMK_HITS].src, smk_hit[n0 % SMK_HITS].a, smk_hit[n0 % SMK_HITS].b, smk_hit[n0 % SMK_HITS].vel);
        check("the hit log takes a key press with its gap and velocity", smk_hit_n == n0 + 1u &&
              smk_hit[n0 % SMK_HITS].src == 9u && smk_hit[n0 % SMK_HITS].a >= 80u && smk_hit[n0 % SMK_HITS].a <= 110u);
    }
#endif
    printf("smk keys: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
