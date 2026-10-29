/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Bounded live REC/overdub takes for the guarded CPU runner. The expected take
 * is the firmware's own MIDI/recording code replaying the same schedule; device
 * FBKK is decoded by the same validator. Onsets and releases keep a quarter
 * step from the boundaries that decide cells, overview rounding and ties, so
 * bounded input jitter cannot change the take's structure. No personal data,
 * slots or files: raw bytes and text go to stdout only. */
#define AUDIO_CPU_FIXTURE_NO_MAIN 1
#include "audio_cpu_fixture.c"

#define TAKE_MAX 256u
#define TAKE_LAST 56u                     /* last onset step: input ends a full second early at 120 BPM */
typedef struct { uint32_t bpm, notes, mask, latch, quant, base; const char *assignment; } take_spec_t;
typedef struct { uint32_t on, off; uint8_t track, note, vel, step; } take_note_t;
typedef struct { uint32_t at; uint8_t track, note, vel; } take_event_t;   /* vel 0: release */
typedef struct { recorded_note_t r; uint8_t track; } take_record_t;
static take_note_t take_notes[TAKE_MAX];
static take_event_t take_events[2u * TAKE_MAX];
static uint32_t take_count, take_step;
static uint8_t take_base_raw[BANK_STORE_SIZE], take_raw[BANK_STORE_SIZE];
static take_record_t take_base_records[RECORD_MAX], take_records[RECORD_MAX];

static uint32_t take_tracks(uint32_t mask) { return mask == 1u ? 1u : 0u; }
static int take_valid(const take_spec_t *s)
{
    uint32_t n = take_tracks(s->mask);
    if (s->bpm < 60u || s->bpm > 160u || !n || s->latch > 1u || s->quant > 1u) return 0;
    if (s->notes != 16u && s->notes != 64u) return 0;
    /* Overdub bases keep at most one note per cell on the recording track. */
    if (s->base != 0u && s->base != 64u) return 0;
    return strlen(s->assignment) == NPART;
}
static int take_restore(const uint8_t *raw)
{
    if (!bank_valid(raw, BANK_STORE_SIZE) || project_restore_runtime(&proj_scratch)) return 0;
    bank_restore(raw);
    return trk_n() == NPART;
}
static int take_pack(uint8_t *raw)
{
    recording_reindex(); project_capture(&proj_scratch);
    return proj_scratch.parts == NPART && bank_pack(raw, &proj_scratch, 1) && bank_valid(raw, BANK_STORE_SIZE);
}
/* The runner's common sound setup zeroes these after loading presets. */
static int take_base(const take_spec_t *s)
{
    if (!take_valid(s)) return 0;
    if (s->base ? !make_fixture(s->base, s->quant, s->mask == 15u, 0, s->assignment) : !fixture_tracks(s->quant, s->assignment))
        return 0;
    for (uint32_t k = 0; k < NPART; k++) {
        track_t *t = &trk[k];
        t->p[P_ALLOC] = t->p[P_GLIDE] = t->p[P_CHRD] = t->p[P_TRANS] = t->p[P_SPRD] = 0;
    }
    return take_pack(take_base_raw);
}
/* Stopped, disarmed, internal clock; the runner applies the same globals. */
static int take_prepare(const take_spec_t *s, const uint8_t *raw, uint32_t countin)
{
    ui_power_on();
    if (!take_restore(raw)) return 0;
    seq_stop(); mi_r = mi_w;
    song.rec = song.rec_latch = song.rec_wait = song.rec_pass = 0;
    song.g[G_BPM] = (int16_t)s->bpm; song.g[G_CLOCK] = 0; song.g[G_ROUTE] = 0; song.g[G_SWING] = 0;
    settings_countin = (uint8_t)countin; settings_drumch = 0;
    take_step = seq_div_samples((uint32_t)trk[0].p[P_SDIV]);
    return 1;
}
static uint32_t take_drum(const take_spec_t *s, uint32_t k) { return s->assignment[k] >= '4'; }
static void take_plan(const take_spec_t *s)
{
    uint32_t tracks[NPART], nt = 0, n = 0;
    for (uint32_t k = 0; k < NPART; k++) if (s->mask & (1u << k)) tracks[nt++] = k;
    uint32_t per = s->notes / nt;
    for (uint32_t j = 0; j < nt; j++) for (uint32_t k = 0; k < per; k++) {
        /* The first note starts the stopped, armed transport at step zero. */
        uint32_t first = !j && !k;
        uint32_t step = first ? 0u : !j ? 1u + (k - 1u) * (TAKE_LAST - 1u) / (per - 1u) : 1u + k * (TAKE_LAST - 1u) / per;
        take_note_t *x = &take_notes[n++];
        x->track = (uint8_t)tracks[j]; x->step = (uint8_t)step;
        x->on = step * take_step + (first ? 0u : take_step / 4u);
        /* Distinct from each other in a cell and from the overdub bases. */
        x->note = take_drum(s, tracks[j]) ? DRUM_LANE_NOTE[(step + 4u + (k & 1u)) % NLANE] : (uint8_t)(60u + (k * 5u + j * 7u) % 24u);
        x->vel = (uint8_t)(64u + (n * 37u) % 60u);
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t next = TAKE_LAST + 1u;
        for (uint32_t j = 0; j < n; j++)
            if (take_notes[j].track == take_notes[i].track && take_notes[j].step > take_notes[i].step && take_notes[j].step < next)
                next = take_notes[j].step;
        uint32_t gate = next - take_notes[i].step - 1u;
        if (gate > 2u) gate = 2u;
        take_notes[i].off = (take_notes[i].step + gate) * take_step + take_step * 13u / 20u;
    }
    take_count = n;
}
static uint32_t take_rand(uint32_t *x) { *x = *x * 1664525u + 1013904223u; return *x >> 8; }
/* Release first at equal times; the first onset stays the device's zero. */
static uint32_t take_order(uint32_t seed, uint32_t jitter)
{
    uint32_t n = 0, r = seed;
    for (uint32_t i = 0; i < take_count; i++) {
        const take_note_t *x = &take_notes[i];
        int32_t a = (int32_t)x->on, b = (int32_t)x->off;
        if (jitter) {
            if (i) a += (int32_t)(take_rand(&r) % (2u * jitter + 1u)) - (int32_t)jitter;
            b += (int32_t)(take_rand(&r) % (2u * jitter + 1u)) - (int32_t)jitter;
        }
        take_events[n++] = (take_event_t){(uint32_t)(a < 0 ? 0 : a), x->track, x->note, x->vel};
        take_events[n++] = (take_event_t){(uint32_t)b, x->track, x->note, 0};
    }
    for (uint32_t i = 1; i < n; i++) {
        take_event_t e = take_events[i];
        uint32_t j = i;
        while (j && (take_events[j - 1u].at > e.at || (take_events[j - 1u].at == e.at && take_events[j - 1u].vel && !e.vel))) {
            take_events[j] = take_events[j - 1u]; j--;
        }
        take_events[j] = e;
    }
    return n;
}
/* Mirrors editor 101: arm (op 3/4) while stopped, the input, then latched
 * punch-out (op 5) or the one-pass loop end, then Stop (op 2). */
static int take_simulate(const take_spec_t *s, const uint8_t *raw, uint32_t seed, uint32_t jitter, uint32_t countin)
{
    if (!take_prepare(s, raw, countin)) return 0;
    take_plan(s);
    for (uint32_t k = 0; k < NPART; k++) if (s->mask & (1u << k)) {
        rec_set(&trk[k], 1);
        if (s->latch) song.rec_latch |= (uint8_t)(1u << k);
    }
    uint32_t n = take_order(seed, jitter), e = 0, pos = 0, loop = 64u * take_step;
    while (e < n) {
        for (; e < n && take_events[e].at <= pos; e++) {
            const take_event_t *x = &take_events[e];
            uint32_t status = (x->vel ? 0x90u : 0x80u) | x->track;
            if (!midi_enqueue((x->vel ? 9u : 8u) | status << 8 | (uint32_t)x->note << 16 | (uint32_t)x->vel << 24, 1u)) return 0;
        }
        events_block(CTL); pos += CTL;
        if (pos > 2u * loop) return 0;
    }
    if (!song.playing || song.rec != s->mask) return 0;   /* the take must still be in its pass */
    for (uint32_t i = 0; i < 8u; i++) events_block(CTL);
    if (s->latch) {
        for (uint32_t k = 0; k < NPART; k++) if (s->mask & (1u << k)) rec_set(&trk[k], 0);
    } else {
        for (pos = 0; song.rec && pos < loop; pos += CTL) events_block(CTL);
    }
    if (song.rec || song.rec_latch || !song.playing) return 0;
    transport_req = 2; events_block(CTL);
    return !song.playing && !seq_counting() && take_pack(take_raw);
}
static uint32_t take_decode(take_record_t *out, const uint8_t *raw)
{
    uint32_t n = 0;
    if (!take_restore(raw)) return RECORD_MAX + 1u;
    for (uint32_t i = 0; i < RECORD_MAX; i++) if (recording[i].vel)
        out[n++] = (take_record_t){recording[i], (uint8_t)((recording[i].owner & 31u) / NPAT)};
    for (uint32_t i = 1; i < n; i++) {           /* canonical: track, bank, cell, note, onset */
        take_record_t v = out[i];
        uint32_t j = i;
        #define TAKE_KEY(x) ((uint64_t)((x).r.owner & 31u) << 40 | (uint64_t)((x).r.step & 63u) << 32 | (uint64_t)recorded_note(&(x).r) << 24 | (uint64_t)(x).r.on << 4 | ((x).r.step >> 6))
        while (j && TAKE_KEY(out[j - 1u]) > TAKE_KEY(v)) { out[j] = out[j - 1u]; j--; }
        #undef TAKE_KEY
        out[j] = v;
    }
    return n;
}
static void take_print(FILE *f, const char *tag, const uint8_t *raw)
{
    uint32_t n = take_decode(take_records, raw);
    for (uint32_t i = 0; i < n; i++) {
        const recorded_note_t *r = &take_records[i].r;
        fprintf(f, "%sE %u %u %u %u %u %u %u %u %u %d %u\n", tag, take_records[i].track, (r->owner & 31u) % NPAT,
                r->step & 63u, r->step >> 6, r->on, r->duration, r->owner >> 5, recorded_note(r), r->vel, r->pitch, r->length);
    }
    /* Overview of the active banks; the fractional gate byte is timing. */
    for (uint32_t k = 0; k < NPART; k++) for (uint32_t i = 0; i < NSTEP; i++) {
        const step_t *s = &trk[k].step[i];
        uint8_t notes[4] = {0};
        for (uint32_t j = 0; j < s->n && j < 4u; j++) notes[j] = s->note[j];
        for (uint32_t a = 1; a < 4u; a++) for (uint32_t b = a; b && notes[b - 1u] > notes[b]; b--) {
            uint8_t t = notes[b]; notes[b] = notes[b - 1u]; notes[b - 1u] = t;
        }
        if (s->time != ST_REST || s->n || s->hit)
            fprintf(f, "%sS %u %u %u %u %u %u %u %u %u %u %u\n", tag, k, i, s->time, s->n, notes[0], notes[1], notes[2],
                    notes[3], s->hit, s->flags, s->vel);
    }
}
static void take_header(FILE *f, const take_spec_t *s)
{
    /* Firmware sample arithmetic uses FS; device frames run at 24 MHz / 544. */
    fprintf(f, "H %u %u %u %u %u %u %u %u %u\n", take_step, FS, 544u, 24000000u, s->bpm, s->notes, s->mask, s->latch, s->quant);
    for (uint32_t i = 0; i < take_count; i++)
        fprintf(f, "N %u %u %u %u %u %u\n", take_notes[i].on, take_notes[i].off, take_notes[i].track, take_notes[i].note,
                take_notes[i].vel, take_notes[i].step);
}

static int take_spec(take_spec_t *s, char **v)
{
    uint32_t x[6];
    for (uint32_t i = 0; i < 6u; i++) {
        char *end = 0; unsigned long n = strtoul(v[i], &end, 10);
        if (!v[i][0] || *end || n > 1024u) return 0;
        x[i] = (uint32_t)n;
    }
    *s = (take_spec_t){x[0], x[1], x[2], x[3], x[4], x[5], v[6]};
    return take_valid(s);
}
static int take_read(uint8_t *raw)
{
    size_t n = fread(raw, 1, BANK_STORE_SIZE, stdin);
    return n == BANK_STORE_SIZE && fgetc(stdin) == EOF;
}

/* Every scheduled note once, base events exact, nothing else, all disarmed. */
static int take_check(const take_spec_t *s, uint32_t jitter)
{
    uint32_t nb = take_decode(take_base_records, take_base_raw), n = take_decode(take_records, take_raw);
    if (nb > RECORD_MAX || n != nb + take_count) return 0;
    uint8_t used[RECORD_MAX] = {0};
    for (uint32_t i = 0; i < nb; i++) {
        uint32_t j = 0;
        for (; j < n && (used[j] || memcmp(&take_records[j].r, &take_base_records[i].r, sizeof(recorded_note_t))); j++);
        if (j == n) return 0;
        used[j] = 1;
    }
    uint32_t bound = (jitter + CTL) * RECORD_UNIT / take_step + 2u;
    for (uint32_t i = 0; i < take_count; i++) {
        const take_note_t *x = &take_notes[i];
        uint32_t want = i ? (s->quant ? 0u : RECORD_UNIT / 4u) : 0u, j = 0;
        for (; j < n; j++) {
            const recorded_note_t *r = &take_records[j].r;
            if (used[j] || take_records[j].track != x->track || recorded_note(r) != x->note || r->vel != x->vel ||
                (r->step & 63u) != x->step || (r->step >> 6) || (r->owner & 31u) % NPAT != trk[x->track].pattern) continue;
            uint32_t d = r->on > want ? r->on - want : want - r->on;
            if (d <= bound && r->duration) break;
        }
        if (j == n) return 0;
        used[j] = 1;
    }
    return !song.rec && !song.playing;
}
/* MIDI IN's DRUM channel as settings_import derives it (0 = OFF). */
static uint32_t take_drum_channel(const persist_t *p) { return !p->ext.drumch ? 10u : p->ext.drumch == 17u ? 0u : p->ext.drumch; }
static int take_same_structure(const take_record_t *a, const take_record_t *b, uint32_t n, uint32_t bound)
{
    for (uint32_t i = 0; i < n; i++) {
        recorded_note_t x = a[i].r, y = b[i].r;
        uint32_t d = x.on > y.on ? x.on - y.on : y.on - x.on;
        uint32_t lx = (uint32_t)x.duration << (x.owner >> 5), ly = (uint32_t)y.duration << (y.owner >> 5);
        uint32_t dl = lx > ly ? lx - ly : ly - lx;
        if (d > bound || dl > 2u * bound + (1u << (x.owner >> 5)) + (1u << (y.owner >> 5))) return 0;
        x.on = y.on = 0; x.duration = y.duration = 0; x.owner &= 31u; y.owner &= 31u;
        if (a[i].track != b[i].track || memcmp(&x, &y, sizeof x)) return 0;
    }
    return 1;
}
static int take_selftest(void)
{
    static const take_spec_t specs[] = {
        {120, 16, 1, 1, 0, 0, "0124"}, {120, 64, 1, 0, 1, 64, "0124"}, {120, 64, 1, 1, 1, 64, "0124"},
        {120, 64, 1, 0, 0, 64, "0124"}, {90, 64, 1, 1, 0, 0, "4455"}, {160, 64, 1, 0, 1, 64, "1230"},
        {60, 16, 1, 1, 1, 0, "2222"}, {120, 64, 1, 1, 0, 64, "5555"}, {140, 64, 1, 1, 1, 64, "3333"},
    };
    static uint8_t exact[BANK_STORE_SIZE];
    static take_record_t exact_records[RECORD_MAX];
    uint32_t cases = 0;
    for (uint32_t c = 0; c < NELEM(specs); c++) {
        const take_spec_t *s = &specs[c];
        assert(take_base(s));
        assert(take_simulate(s, take_base_raw, 0, 0, 0) && take_check(s, 0));
        memcpy(exact, take_raw, sizeof exact);
        uint32_t n = take_decode(exact_records, exact);
        /* Count-in preference cannot affect a note-started take. */
        assert(take_simulate(s, take_base_raw, 0, 0, 1) && !memcmp(exact, take_raw, sizeof exact));
        /* Bounded jitter (6 ms) changes only onset/duration numbers. */
        uint32_t jitter = take_step / 20u;
        if (jitter > FS * 6u / 1000u) jitter = FS * 6u / 1000u;
        for (uint32_t seed = 1; seed <= 3u; seed++) {
            assert(take_simulate(s, take_base_raw, seed, jitter, seed & 1u) && take_check(s, jitter));
            assert(take_decode(take_records, take_raw) == n);
            assert(take_same_structure(exact_records, take_records, n, (jitter + CTL) * RECORD_UNIT / take_step + 2u));
            cases++;
        }
        cases++;
    }
    persist_t prefs;
    for (uint32_t ch = 0; ch <= 16u; ch++) {
        settings_drumch = (uint8_t)ch; settings_export(&prefs);
        assert(settings_legacy_valid(&prefs) && take_drum_channel(&prefs) == ch);
    }
    take_spec_t bad = specs[0];
    bad.notes = 256; assert(!take_valid(&bad));
    bad = specs[0]; bad.base = 256; assert(!take_valid(&bad));
    bad = specs[0]; bad.mask = 3; assert(!take_valid(&bad));
    bad = specs[0]; bad.mask = 15; assert(!take_valid(&bad));
    bad = specs[0]; bad.bpm = 170; assert(!take_valid(&bad));
    printf("CPU live takes: %u REC/overdub schedules, native simulation, jitter bounds and FBKK decoding\n", cases);
    return 0;
}

int main(int argc, char **argv)
{
    take_spec_t s;
    if (argc == 1) return take_selftest();
    if (argc == 2 && !strcmp(argv[1], "--inspect")) {
        if (!take_read(take_raw) || take_decode(take_records, take_raw) > RECORD_MAX) return 2;
        take_print(stdout, "", take_raw);
        return fflush(stdout) ? 2 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--drum-channel")) {
        /* Live preferences (RAM snapshot) through the firmware's own layout. */
        static persist_t p;
        _Static_assert(sizeof p == 604u, "live preference layout");
        if (fread(&p, 1, sizeof p, stdin) != sizeof p || !settings_legacy_valid(&p)) return 2;
        printf("%u\n", take_drum_channel(&p));
        return fflush(stdout) ? 2 : 0;
    }
    if (argc == 9 && !strcmp(argv[1], "--base")) {
        if (!take_spec(&s, argv + 2) || !take_base(&s)) return 2;
        return fwrite(take_base_raw, 1, sizeof take_base_raw, stdout) != sizeof take_base_raw || fflush(stdout) ? 2 : 0;
    }
    if (argc == 9 && !strcmp(argv[1], "--plan")) {
        /* Schedule, exact expected take and the base events it must keep. */
        if (!take_spec(&s, argv + 2) || !take_base(&s) || !take_simulate(&s, take_base_raw, 0, 0, 0) || !take_check(&s, 0)) return 2;
        take_header(stdout, &s);
        take_print(stdout, "X", take_raw);
        take_print(stdout, "B", take_base_raw);
        return fflush(stdout) ? 2 : 0;
    }
    if (argc == 11 && !strcmp(argv[1], "--observe")) {
        /* Host stand-in for a device take: SEED and JITTER frames. */
        char *end = 0; unsigned long seed = strtoul(argv[2], &end, 10), jitter;
        if (*end) return 2;
        jitter = strtoul(argv[3], &end, 10);
        if (*end || jitter > FS / 50u || !take_spec(&s, argv + 4) || !take_base(&s) ||
            !take_simulate(&s, take_base_raw, (uint32_t)seed, (uint32_t)jitter, 0)) return 2;
        take_print(stdout, "", take_raw);
        return fflush(stdout) ? 2 : 0;
    }
    return 2;
}
