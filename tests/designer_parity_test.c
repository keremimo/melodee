/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The sound designer's answers from the synths' designer blocks (mod_schema.h, ui_designer.c) against a894c6f7's own
 * per-synth designer: tests/fixtures/designer-a894c6f7.txt was written by this file built against a894c6f7 (its
 * FEATURES, recipes and needs in ui_designer.c; the a894c6f7 shims are outside this file). For every engine and start
 * (INIT, each factory preset): the routes (MAIN and ALL), each FEATURES row set to each state and turned by its knob,
 * what each row reads then and what cannot sound, each goal's recipe from INIT, and on every page of the route each
 * control's need and what turning it up and down switches on (the sound as it starts and with every path off): the
 * patches (as Melodee 1.x stored them), the part's parameters and the messages the same, line by line (a hash a group
 * of an engine and start; DP_LINES=file writes the lines). */
#include <stdarg.h>
#include <stdio.h>
#ifndef DP_SHIMS
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
/* the shims: this tree's designer */
#define dp_title(i) (PG(i).title)
static void dp_boot(void) {}
static uint32_t dp_nrows(void) { return designer_feats(); }
static uint32_t dp_code(uint32_t r) { return designer_code(r); }
static void dp_word(uint32_t r, char *b) { (void)designer_word(designer_code(r), b); }
static void dp_need(char *b)
{
    uint32_t f = designer_need();
    if (f == DS_NONE) str_cpy(b, "-", 16);
    else if (f == DS_END) str_cpy(b, "END", 16);
    else (void)designer_word(f, b);
}
static uint32_t dp_blob(uint8_t *b)              /* the patch as Melodee 1.x stored it (0: none) */
{
    uint32_t e = TSEL->eng_req, n = patch_eng_of(song.sel) == e ? patch_len(e) : 0u;
    memcpy(b, patch_of(song.sel), n);
    return n;
}
#endif

static FILE *dp_out;
static uint32_t dp_h, dp_lines;
static void dp_line(const char *fmt, ...)
{
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    for (const char *s = b; *s; s++)
        dp_h = (dp_h ^ (uint8_t)*s) * 16777619u;
    dp_h = (dp_h ^ '\n') * 16777619u;
    dp_lines++;
    if (dp_out)
        fprintf(dp_out, "%s\n", b);
}
/* the sound now: its patch, the part's parameters, the message shown */
static uint32_t dp_sound(void)
{
    uint8_t b[512];
    uint32_t n = dp_blob(b), h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ b[i]) * 16777619u;
    for (i = 0; i < P_COUNT; i++)
        h = (h ^ (uint16_t)TSEL->p[i]) * 16777619u;
    return h ^ n << 24;
}
static const char *dp_msg(void) { return ui.msg_t ? ui.msg : ""; }
static void dp_quiet(void) { ui.msg[0] = 0; ui.msg_t = 0; }
static void dp_route(const char *what)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < ds.n; i++) {
        const char *t = ds.route[i].page == DS_FEAT ? "FEATURES" : dp_title(ds.route[i].page);
        for (; *t; t++)
            h = (h ^ (uint8_t)*t) * 16777619u;
        h = (h ^ ds.route[i].op) * 16777619u;
        h = (h ^ ds.route[i].stage) * 16777619u;
    }
    dp_line("route %s %u %08x", what, ds.n, h);
}
static void dp_rows(const char *what)            /* what each FEATURES row reads, what cannot sound */
{
    char b[160] = "";
    uint32_t n = dp_nrows();
    for (uint32_t r = 0; r < n && str_len(b) < 140u; r++) {
        uint32_t k = str_len(b);
        b[k] = (char)('0' + (designer_feat_get(dp_code(r)) % 10u));
        b[k + 1] = 0;
    }
    dp_line("%s %08x [%s] %s|%s", what, dp_sound(), b, designer_silent() ? designer_silent() : "", dp_msg());
}
static designer_sound_t dp_s0, dp_s1;
static struct { uint8_t page, op, stage; } dp_rt[DS_ROUTES];
static uint32_t dp_nrt;
static void dp_at(uint32_t i)                    /* route place i (the ALL route kept) */
{
    memcpy(ds.route, dp_rt, sizeof dp_rt);
    ds.n = (uint8_t)dp_nrt;
    ds.at = (uint8_t)i;
    designer_page();
}
static void dp_needs(const designer_sound_t *s, const char *tag)
{
    char w[24];
    for (uint32_t i = 0; i < dp_nrt; i++) {
        uint32_t nf, f;
        if (dp_rt[i].page == DS_FEAT)
            continue;
        designer_restore(s);
        dp_at(i);
        nf = list_on() ? list_n(list_on()) : 4u;
        if (nf > 16u)
            nf = 16u;
        for (f = 0; f < nf; f++) {
            for (int32_t dir = 1; dir >= -1; dir -= 2) {
                designer_restore(s);
                dp_at(i);
                if (list_on()) { lst.row = (uint8_t)f; ds.focus = 0; }
                else ds.focus = (uint8_t)f;
                dp_need(w);
                dp_quiet();
                designer_enable(dir);
                dp_line("need %s %s/%u col %u %+d: %s -> %08x %s", tag, dp_title(dp_rt[i].page), dp_rt[i].op, f, dir, w,
                        dp_sound(), dp_msg());
            }
        }
    }
}
static void dp_start(uint32_t e, uint32_t st)
{
    char w[24], ws[400] = "";
    uint32_t n, r, s;
    ui_power_on();
    sound_init_of(TSEL, e);
    if (st)
        apply_preset_to(TSEL, st - 1u);
    dp_quiet();
    dp_line("start %08x", dp_sound());
    designer_open();
    ds.current = 1;
    press(B_OCTUP);
    dp_route("main");
    ds.expert = 1;
    designer_route();
    dp_route("all");
    memcpy(dp_rt, ds.route, sizeof dp_rt);
    dp_nrt = ds.n;
    designer_snapshot(&dp_s0);
    n = dp_nrows();
    for (r = 0; r < n; r++) {
        dp_word(r, w);
        if (str_len(ws) + str_len(w) + 2u < sizeof ws) { str_cpy(ws + str_len(ws), w, 24); str_cpy(ws + str_len(ws), ",", 2); }
    }
    dp_line("rows %u %s", n, ws);
    for (r = 0; r < n; r++) {
        uint32_t c = dp_code(r), ns = designer_feat_states(c);
        for (s = 0; s < ns; s++) {
            char t[64];
            designer_restore(&dp_s0);
            dp_quiet();
            fm1_irq_off();
            designer_feat_set(c, s);
            fm1_irq_on();
            snprintf(t, sizeof t, "set %u=%u", r, s);
            dp_rows(t);
        }
        for (int32_t dir = 1; dir >= -1; dir -= 2) {
            char t[64];
            designer_restore(&dp_s0);
            dp_quiet();
            ds.row = (uint8_t)r;
            designer_feat_knob(0, dir);
            snprintf(t, sizeof t, "knob %u %+d", r, dir);
            dp_rows(t);
        }
    }
    dp_needs(&dp_s0, "as is");
    designer_restore(&dp_s0);
    fm1_irq_off();
    for (r = 0; r < n; r++)
        designer_feat_set(dp_code(r), 0);
    fm1_irq_on();
    designer_snapshot(&dp_s1);
    dp_rows("all off");
    dp_needs(&dp_s1, "all off");
    designer_restore(&dp_s0);
    designer_close(1);
    for (uint32_t g = 0; g < DG_COUNT; g++) {        /* (INIT and a goal's recipe) */
        designer_open();
        ds.current = 0;
        ds.goal = (uint8_t)g;
        press(B_OCTUP);
        snprintf(w, sizeof w, "goal %u", g);
        dp_rows(w);
        dp_route(w);
        designer_close(1);
    }
}
int main(int argc, char **argv)
{
    const char *fix = "tests/fixtures/designer-a894c6f7.txt";
    FILE *want = 0, *wr = 0;
    int bad = 0, groups = 0;
    uint32_t total = 0;
    if (argc > 2 && !strcmp(argv[1], "write"))
        wr = fopen(argv[2], "w");
    else if (!(want = fopen(argc > 1 ? argv[1] : fix, "r"))) {
        printf("designer parity: %s unreadable\n", fix);
        return 1;
    }
    if (getenv("DP_LINES"))
        dp_out = fopen(getenv("DP_LINES"), "w");
    dp_boot();
    for (uint32_t ei = 0; ei < NENG_SHOWN; ei++) {
        uint32_t e = eng_vis(ei), np = ENGINES[e]->npresets;
        for (uint32_t st = 0; st <= np && st <= 64u; st++) {
            char got[96], line[96];
            dp_h = 2166136261u;
            dp_lines = 0;
            if (dp_out)
                fprintf(dp_out, "== %s %u\n", ENGINES[e]->name, st);
            dp_start(e, st);
            snprintf(got, sizeof got, "%s %u %08x %u\n", ENGINES[e]->name, st, dp_h, dp_lines);
            total += dp_lines;
            groups++;
            if (wr)
                fputs(got, wr);
            else if (!fgets(line, sizeof line, want) || strcmp(line, got)) {
                printf("designer parity: %s start %u differs from a894c6f7's (%s)", ENGINES[e]->name, st, got);
                bad++;
            }
        }
    }
    if (wr)
        fclose(wr);
    if (dp_out)
        fclose(dp_out);
    if (want) {
        char line[96];
        if (fgets(line, sizeof line, want)) {
            printf("designer parity: a894c6f7 had more groups\n");
            bad++;
        }
        fclose(want);
    }
    printf("designer parity: %d groups, %u answers, %s\n", groups, total, bad ? "FAILED" : "as a894c6f7's designer gave them");
    return bad != 0;
}
