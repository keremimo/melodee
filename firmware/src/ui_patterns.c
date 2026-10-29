/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SEQ > PATTERNS and SEQ > SONG, screens of their own drawn to the redesign's mockups (docs/design: concept_screens 3
 * and 4, ref/patterns, ref/song).
 * PATTERNS (SEQ held, or its page): the header, then a row per track: its number in a circle (the selected one
 * filled), a cell per pattern, eight at a time (a part's 16: two pages, their dots over the numerals; the page follows
 * the pattern turned to or copied to; a row's pattern on the other page: a tick on that side): the one playing filled
 * in the track's colour (how far through it at its foot), the one waiting for the bar dashed, the others with
 * something in them as three bars (their thirds' notes), empty ones an outline. At the foot the song as its sections'
 * letters (the rows with the same four patterns share one), the row playing (else the one SONG picked) lit; no song
 * yet: the rows the jam logged. KNOB k queues track k's pattern.
 * OCT+ the selected track's pattern's sheet (Copy to.., Delete pattern). Copy to: the place it goes framed in the
 * theme's colour on the track's row (the first empty one first), any knob moves it, OCT+ copies (over a pattern in
 * use: the question first), OCT- leaves.
 * SONG: its title, its length, the jam at the right (logging while patterns play without a song; REC held: TAKE);
 * the sections as columns (their letters; four at a time around the row picked, one more to add a row), a track's
 * patterns as blocks in its colour, one pattern over sections in a row as one block; the row picked marked, the
 * selected track's block in it outlined; a song playing: its playhead. At the foot the sections and bars, the row's
 * letter and repeats, how far the song has played. KNOB 1 the row, 2 the track, 3 its pattern in the row, 4 the
 * selected block's span; OCT+ edits, PLAY starts at the cursor, REC held takes the jam (ui_input.c).
 * Each part remembers what it drew. Included by ui_draw.c */

static int ptc_on(void);                                /* PATTERNS' Copy to (below) */
static int dq_defer(uint32_t cf, uint32_t k, uint32_t a, uint32_t b);   /* ui_input.c: playing, at the loop end */
static uint32_t ptc_next(uint32_t from, int32_t dir);
static void ptc_row(uint32_t k);
/* the track whose four rows the page shows: the selected one's (Copy to: the four of the track it goes to) */
static uint32_t pt_track(void) { return ptc_on() ? ui.ptc_to % NTRK : song.sel % NTRK; }
static uint32_t pt_group(void) { return pt_track() / NPART * NPART; }

/* ------------------------------------------------------------ shared --- */
/* pattern b of track k holds something: a note, a hit, a recorded note's step */
static int pattern_used(uint32_t k, uint32_t b)
{
    if (b >= NPAT) return 0;
    pattern_t p;
    pattern_read(k, b, &p);
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (step_on(&p.step[i]) || (p.step[i].flags & SF_RECORDED))
            return 1;
    return 0;
}
/* pattern b of track k holds nothing at all: no note, no hit, no motion (Copy to there asks nothing) */
static int pattern_empty(uint32_t k, uint32_t b)
{
    uint32_t i;
    if (pattern_used(k, b) || pattern_sound_id(k, b))
        return 0;
    for (i = 0; i < motion.count; i++)
        if ((motion.event[i].place >> 6) == k && motion_pattern[i] == b)
            return 0;
    return 1;
}
/* track k's pattern b: its steps, length and division (the one playing: the track's own; its bank lags behind) */
static void pat_timing(uint32_t k, uint32_t b, uint32_t *len, uint32_t *div)
{
    *len = (uint32_t)pattern_timing(k, b, 0);
    *div = (uint32_t)pattern_timing(k, b, 1);
}
/* its bars, whole ones up (N_DIV: a step of 1/4 .. 4BAR; steps in 4 bars) */
static uint32_t pat_bars(uint32_t k, uint32_t b)
{
    if (b == CHAIN_REST) return 1;
    static const uint8_t SPB4[] = {16, 32, 64, 128, 48, 96, 8, 4, 2, 1};
    uint32_t len, div, s;
    pat_timing(k, b, &len, &div);
    s = SPB4[div < NELEM(SPB4) ? div : 2u];
    len = len ? len : 1u;
    return (len * 4u + s - 1u) / s;
}
/* row r's letter: its four patterns' set; A the first set the song plays, B the next new one .. */
static char row_letter(const uint8_t (*pat)[NTRK], uint32_t r)
{
    uint32_t f, i, j, n = 0;
    for (f = 0; f < r && memcmp(pat[f], pat[r], NTRK); f++)
        ;
    for (i = 0; i < f; i++) {
        for (j = 0; j < i && memcmp(pat[j], pat[i], NTRK); j++)
            ;
        n += j == i;
    }
    return (char)('A' + n % 26u);
}
static uint32_t song_hash(const uint8_t (*pat)[NTRK], const uint8_t *rep, uint32_t n)
{
    uint32_t h = 2166136261u, i, k;
    for (i = 0; i < n; i++)
        for (k = 0; k <= NTRK; k++)
            h = (h ^ (k < NTRK ? pat[i][k] : 64u + rep[i])) * 16777619u;
    return h ^ n * 40503u;
}
static uint32_t fnv1(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* ---------------------------------------------------------- PATTERNS --- */
#define PT_NUM_Y H_HEAD                                 /* the patterns' numbers: rows 30 .. 49 */
#define PT_ROW_Y 50                                     /* a track a row: 38 px, to 201 */
#define PT_ROW_H 38
#define PT_SONG_Y (PT_ROW_Y + 4 * PT_ROW_H)             /* the song: 202 .. 239 */
#define PT_COLS 8u                                      /* a page of patterns (a part's 16: two) */
#define PT_CELL_X(b) (30 + 26 * (int32_t)((b) % PT_COLS))   /* a cell: 23 x 32 at row y 2 */
static struct { uint32_t num, row[NTRK], prog[NTRK], song, focus; } ptv;

/* the pattern a track's row is about: the one waiting for the bar, else the one playing */
static uint32_t pt_focus(uint32_t k) { return trk[k].pattern_next < NPAT ? trk[k].pattern_next : trk[k].pattern; }
/* the page the rows show: Copy to's place; else the selected track's pattern's, whenever that changes (KNOB k on
 * another row: that row's, patgrid_edit) */
static uint32_t pt_page(void)
{
    uint32_t k = song.sel % NTRK, f = pt_focus(k), sig = k * 64u + f + 1u;
    if (trk_npat(pt_track()) <= PT_COLS)
        return 0;
    if (ptc_on())
        ui.pt_page = (uint8_t)(ui.ptc_dst / PT_COLS);
    else if (ui.pt_page >= NPAT / PT_COLS || sig != ptv.focus)
        ui.pt_page = (uint8_t)(f / PT_COLS);
    ptv.focus = sig;
    return ui.pt_page;
}
static uint32_t pt_prog(const track_t *t)               /* px through the pattern playing (of 17), 0 stopped */
{
    uint32_t len = (uint32_t)t->p[P_SLEN];
    return song.playing && len ? (t->seq_idx % len + 1u) * 17u / len : 0u;
}
static void pt_prog_bar(int32_t x, int32_t y, uint32_t prog, uint16_t tc)   /* at the playing cell's foot */
{
    uint16_t tr = ux_mix(tc, T_BG, 35);
    cv_rrect(x, y, 17, 3, 1, tr, tc);
    cv_rrect(x, y, prog < 2u ? 2 : (int32_t)prog, 3, 1, T_BG, tr);
}
/* pattern b's notes as three bars, its thirds (up to 14 px; none: a dot) */
static void pt_bars(uint32_t k, uint32_t b, int32_t x, int32_t y, uint16_t c)
{
    uint32_t len, div, i, n[3] = {0, 0, 0}, m = 1;
    pattern_t p;
    pattern_read(k, b, &p);
    const step_t *s = p.step;
    len = (uint32_t)p.timing[0]; div = (uint32_t)p.timing[1];
    len = len > NSTEP ? NSTEP : len ? len : 1u;
    for (i = 0; i < len; i++)
        if (step_on(&s[i]))
            n[i * 3u / len]++;
    for (i = 0; i < 3u; i++)
        if (n[i] > m) m = n[i];
    for (i = 0; i < 3u; i++) {
        int32_t h = n[i] ? 4 + (int32_t)(n[i] * 10u / m) : 2;
        cv_rrect(x + 5 + 5 * (int32_t)i, y + 26 - h, 3, h, 1, c, T_SURF);
    }
}
/* the page's eight numbers (1 .. 8, 9 .. 16); two pages: their dots over the tracks' numerals, the page's a pill */
static void pt_numbers(uint32_t pg)
{
    uint32_t b, two = trk_npat(pt_track()) > PT_COLS, sig = ux.gen * 977u + ux.pal * 31u + 1u + 2u * two + 4u * pg;
    int32_t x;
    if (!ui.force && sig == ptv.num)
        return;
    ptv.num = sig;
    cv_begin(240, PT_ROW_Y - PT_NUM_Y, T_BG);
    for (b = pg * PT_COLS; b < pg * PT_COLS + PT_COLS; b++) {
        char n[3] = {(char)('0' + (b + 1u) / 10u), (char)('0' + (b + 1u) % 10u), 0};
        cv_text_c(PT_CELL_X(b) + 11, ty_y((PT_ROW_Y - PT_NUM_Y) / 2, TY_LB), TY_LB, b < 9u ? n + 1 : n, T_MID, T_BG);
    }
    for (b = 0, x = 4; two && b < NPAT / PT_COLS; b++) {   /* (as the header's, head_pages: over the numerals) */
        int32_t w = b == pg ? 12 : 6;
        cv_rrect(x, (PT_ROW_Y - PT_NUM_Y) / 2 - 3, w, 6, 3, b == pg ? T_THEME : T_LINE, T_BG);
        x += w + 2;
    }
    cv_blit(0, PT_NUM_Y);
}
static void pt_row(uint32_t k, uint32_t pg)
{
    const track_t *t = &trk[k];
    uint16_t tc = t->p[P_MUTE] ? T_DIM : T_TRK(k);
    uint32_t b, on = t->pattern / PT_COLS == pg, prog = on ? pt_prog(t) : 0u, sel = k == song.sel, h;
    uint32_t off = !on || (t->pattern_next < NPAT && t->pattern_next / PT_COLS != pg);   /* (on the other page) */
    int32_t y0 = PT_ROW_Y + (int32_t)(k % NPART) * PT_ROW_H;
    char n[2] = {(char)('1' + k), 0};
    h = fnv1(fnv1(fnv1(2166136261u, steps_hash(t)), t->pattern_gen), t->pattern + 16u * t->pattern_next + 8192u * sel +
            16384u * (uint32_t)(t->p[P_MUTE] != 0) + 65536u * (uint32_t)t->p[P_SLEN]);
    h = fnv1(h, ux.gen * 977u + ux.pal * 31u + (uint32_t)song.playing + 2u * pg);
    if (ptc_on() && k == ui.ptc_to)                     /* Copy to: where it goes */
        h = fnv1(h, 1u + ui.ptc_src + 16u * ui.ptc_dst);
    if (!ui.force && h == ptv.row[k]) {
        if (prog != ptv.prog[k]) {                      /* playing: the bar at the cell's foot only */
            ptv.prog[k] = prog;
            cv_begin(17, 3, tc);
            pt_prog_bar(0, 0, prog, tc);
            cv_blit((uint32_t)(PT_CELL_X(t->pattern) + 3), (uint32_t)(y0 + 2 + 25));
        }
        return;
    }
    ptv.row[k] = h;
    ptv.prog[k] = prog;
    cv_begin(240, PT_ROW_H, T_BG);
    if (sel) {                                          /* the track: its numeral in a chip, the selected one filled */
        cv_rrect(2, 2, 24, 32, 6, tc, T_BG);
        cv_text_c(14, ty_y(18, TY_TX), TY_TX, n, T_INK, tc);
    } else {
        cv_rrect(2, 2, 24, 32, 6, tc, T_BG);
        cv_rrect(4, 4, 20, 28, 4, T_BG, tc);
        cv_text_c(14, ty_y(18, TY_TX), TY_TX, n, tc, T_BG);
    }
    if (off)                                            /* its pattern on the other page: a tick on that side */
        cv_rrect(pg ? 27 : 236, 12, 2, 12, 1, tc, T_BG);
    for (b = pg * PT_COLS; b < pg * PT_COLS + PT_COLS && b < trk_npat(k); b++) {
        int32_t x = PT_CELL_X(b), y = 2;
        int dst = ptc_on() && k == ui.ptc_to && b == ui.ptc_dst;
        if (dst)                                        /* Copy to: the place, framed in the theme's colour */
            cv_rrect(x - 2, y - 2, 27, 36, 6, T_THEME, T_BG);
        if (b == t->pattern) {                          /* playing: filled, how far through it */
            cv_rrect(x, y, 23, 32, 4, tc, dst ? T_THEME : T_BG);
            if (prog)
                pt_prog_bar(x + 3, y + 25, prog, tc);
            continue;
        }
        if (pattern_used(k, b) || b == t->pattern_next) {
            cv_rrect(x, y, 23, 32, 4, T_SURF, dst ? T_THEME : T_BG);
            if (pattern_used(k, b))
                pt_bars(k, b, x, y, ux_mix(T_SURF, tc, 60));
        } else {                                        /* empty: an outline */
            cv_rrect(x, y, 23, 32, 4, dst ? T_BG : T_LINE, dst ? T_THEME : T_BG);
            cv_rrect(x + 1, y + 1, 21, 30, 3, T_BG, dst ? T_BG : T_LINE);
            if (dst)                                    /* (Copy to: an empty place) */
                cv_text_c(x + 11, ty_y(y + 16, TY_TX), TY_TX, "+", T_THEME, T_BG);
        }
        if (b == t->pattern_next)                       /* waiting for the bar: dashed */
            cv_dashed(x, y, 23, 32, 4, tc);
    }
    cv_blit(0, (uint32_t)y0);
}
/* the song's sections as letters (six around the row playing, else the one SONG picked: lit); no song: the jam's */
static void pt_song(void)
{
    uint32_t n = chain_config.count, jamrows = !n && jam.n, cur, i, first, sig;
    const uint8_t (*pat)[NTRK] = jamrows ? (const uint8_t (*)[NTRK])jam.pat : (const uint8_t (*)[NTRK])chain_patterns;
    uint8_t rep[CHAIN_ROWS];
    if (jamrows)
        n = jam.n;
    for (i = 0; i < n; i++)
        rep[i] = jamrows ? jam.rep[i] : chain_config.row[i].repeat;
    cur = chain.running ? chain.row : jamrows ? n - 1u : ui.song_row;
    if (n && cur >= n)
        cur = n - 1u;
    sig = song_hash(pat, rep, n) + cur * 7919u + jamrows * 3u + ux.gen * 977u + ux.pal * 31u;
    if (!ui.force && sig == ptv.song)
        return;
    ptv.song = sig;
    for (uint32_t band = 0; band < (uint32_t)(240 - PT_SONG_Y); band += CV_MAX / (240)) {
        cv_begin_band(240,240 - PT_SONG_Y,T_BG,band);
        cv_text_on(8, ty_y(19, TY_LB), TY_LB, jamrows ? "JAM" : "SONG", T_MID, T_BG);
        if (!n) {
            cv_text_on(62, ty_y(19, TY_LB), TY_LB, "Empty", T_DIM, T_BG);
        } else {
            first = cur > 2u ? cur - 2u : 0u;
            if (n > 6u && first > n - 6u)
                first = n - 6u;
            if (n <= 6u)
                first = 0;
            for (i = first; i < first + 6u && i < n; i++) {
                int32_t x = 62 + 29 * (int32_t)(i - first);
                int on = i == cur;
                char l[2] = {row_letter(pat, i), 0};
                cv_rrect(x, 7, 26, 24, 6, on ? T_TEXT : T_SURF, T_BG);
                cv_text_c(x + 13, ty_y(19, TY_LB), TY_LB, l, on ? T_BG : T_MID, on ? T_TEXT : T_SURF);
            }
        }
        cv_blit_band(0, PT_SONG_Y);
    }
}
static void patterns_draw(void)
{
    uint32_t k, pg = pt_page();
    pt_numbers(pg);
    pt_song();
    uint32_t g = pt_group(), n = trk_n() - g < NPART ? trk_n() - g : NPART;    /* (the selected track's four) */
    for (k = 0; k < n; k++)
        pt_row(g + k, pg);
    if (ui.force && n < NPART)                          /* (the rows of tracks not in use: empty) */
        lcd_fill(0, PT_ROW_Y + (int32_t)n * PT_ROW_H, 240, (NPART - n) * PT_ROW_H, T_BG);
}
/* a pattern queued on any track (PATTERNS' OCT- clears them before it leaves) */
static int patterns_queued(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (trk[k].pattern_next < NPAT)
            return 1;
    return 0;
}
/* KNOB k: track k's next pattern (stopped: at once; playing: at the end of its bar; a song plays: refused); Copy to:
 * the place on track k's row (another row: the frame there first, the same pattern number) */
static void patgrid_edit(uint32_t k, int32_t steps)
{
    track_t *t;
    k = pt_group() + k % NPART;                         /* (KNOB k: the four shown) */
    t = &trk[k];
    if (k >= trk_n())                                   /* (no track there) */
        return;
    if (ptc_on()) {
        if (k != ui.ptc_to)
            ptc_row(k);
        else
            ui.ptc_dst = (uint8_t)ptc_next(ui.ptc_dst, steps > 0 ? 1 : -1);
        ui.force = 1;
        return;
    }
    uint32_t from = t->pattern_next < NPAT ? t->pattern_next : t->pattern;
    if (pattern_request(t, (uint32_t)clamp((int32_t)from + (steps > 0 ? 1 : -1), 0, (int32_t)trk_npat(k) - 1)))
        ui_message("STOP SONG TO SWITCH");
    else                                                /* (the page goes with it) */
        ui.pt_page = (uint8_t)(pt_focus(k) / PT_COLS);
    ui.force = 1;
}

/* ----------------------------------------------- PATTERNS: Copy to --- */
static void confirm_open(uint32_t kind, uint32_t trk);  /* ui_input.c */
static int ptc_on(void) { return ui.ptc_on && !ui.home && cur_page()->graph == GR_PATGRID && ui.ptc_trk == song.sel; }
static uint32_t ptc_next(uint32_t from, int32_t dir)    /* the next place on its row that is not the pattern copied
                                                         * (ends kept) */
{
    int32_t b = (int32_t)from + dir;
    if (ui.ptc_to == ui.ptc_trk && b == (int32_t)ui.ptc_src)
        b += dir;
    return b < 0 || b >= (int32_t)trk_npat(ui.ptc_to) ? from : (uint32_t)b;
}
/* Copy to: the place onto track k's row (KNOB k; ALGORITHM, past the four shown too), the same number (or the one
 * beside the pattern copied) */
static void ptc_row(uint32_t k)
{
    uint32_t b = ui.ptc_dst < trk_npat(k) ? ui.ptc_dst : trk_npat(k) - 1u;
    ui.ptc_to = (uint8_t)k;
    ui.ptc_dst = (uint8_t)(k == ui.ptc_trk && b == ui.ptc_src ? ptc_next(b, b ? -1 : 1) : b);
    ui.force = 1;
}
static void ptc_start(void)                             /* the pattern sheet's Copy to: the selected track's pattern */
{
    uint32_t k = song.sel % NTRK, src = trk[k].pattern, i, n = trk_npat(k), b = (src + 1u) % n;
    if (chain_busy()) {
        ui_message("STOP SONG TO COPY");
        return;
    }
    if (pattern_empty(k, src)) {
        ui_message("NOTHING TO COPY");
        return;
    }
    for (i = 1; i < n && !pattern_empty(k, (src + i) % n); i++)   /* the first empty place after it */
        ;
    if (i < n)
        b = (src + i) % n;
    ui.ptc_on = 1;
    ui.ptc_trk = ui.ptc_to = (uint8_t)k;
    ui.ptc_src = (uint8_t)src;
    ui.ptc_dst = (uint8_t)b;
    ui.force = 1;
}
static void ptc_paste(int asked)                        /* OCT+: copy it there (over one in use: asked first;
                                                         * that or the one playing, playing: at its loop end) */
{
    uint32_t k = ui.ptc_trk % NTRK, d = ui.ptc_to % NTRK;
    int rc;
    if (!asked && !pattern_empty(d, ui.ptc_dst)) {
        confirm_open(CF_PASTE_PAT, k);
        return;
    }
    if ((d != k || ui.ptc_src != ui.ptc_dst) && (ui.ptc_dst == trk[d].pattern || !pattern_empty(d, ui.ptc_dst)) &&
        dq_defer(CF_PASTE_PAT, d, ui.ptc_dst, k * NPAT + ui.ptc_src)) {
        ui.ptc_on = 0;
        return;
    }
    rc = pattern_copy_to(&trk[k], ui.ptc_src, &trk[d], ui.ptc_dst, 0);
    if (!rc)
        ui.ptc_on = 0;
    ui_message(!rc ? "PATTERN COPIED" : rc == 4 ? "PATTERN STORAGE FULL" : rc == 2 ? "PATTERN DATA FULL" : chain_busy() ? "STOP SONG TO COPY" :
               "PATTERN IS PLAYING");
    ui.force = 1;
}

/* -------------------------------------------------------------- SONG --- */
#define SG_TOP_H H_HEAD                                 /* the title: rows 0 .. 29 */
#define SG_LAB_H 22                                     /* the sections' letters: 30 .. 51 */
#define SG_ROW_Y (SG_TOP_H + SG_LAB_H)                  /* a track a row: 36 px, 52 .. 195 */
#define SG_ROW_H 36
#define SG_FOOT_Y (SG_ROW_Y + 4 * SG_ROW_H)             /* the foot: 196 .. 239 */
#define SG_COL_W 56                                     /* a section: x 8 + 56 j, four shown */
#define SG_COLS 4u
static struct { uint32_t top, lab, row[NTRK], foot; } sgv;
static struct { uint32_t first, ncols, sel, bars, play; uint64_t total; int32_t px; } sg;   /* this frame's */

static uint32_t sg_row_bars(uint32_t r)                 /* the conductor pattern defines a section */
{
    return (chain_patterns[r][0] >= NPAT ? 1u : pat_bars(0, chain_patterns[r][0])) * chain_config.row[r].repeat;
}
static void sg_time_text(char *text, uint64_t samples)
{
    uint32_t seconds = chain_div_samples(samples + FS / 2u, FS, 0);
    fmt_int(text, seconds / 60u); str_cpy(text + str_len(text), seconds % 60u < 10u ? ":0" : ":", 3);
    fmt_int(text + str_len(text), seconds % 60u);
}
/* the window (four sections around the row picked or playing), the song's bars, the playhead's x (-1 none) */
static void sg_frame(void)
{
    uint32_t n = chain_config.count, r;
    sg.ncols = n < CHAIN_ROWS || se.pending == SONG_SECTIONS_MOVE ? n + 1u : CHAIN_ROWS;
    sg.sel = ui.song_row < sg.ncols ? ui.song_row : sg.ncols - 1u;
    r = chain.running ? chain.row : sg.sel;
    sg.first = r > 1u ? r - 1u : 0u;
    if (sg.ncols > SG_COLS && sg.first > sg.ncols - SG_COLS)
        sg.first = sg.ncols - SG_COLS;
    if (sg.ncols <= SG_COLS)
        sg.first = 0;
    for (r = 0, sg.bars = 0, sg.play = 0, sg.total = 0; r < n; r++) {
        uint32_t b = sg_row_bars(r);
        sg.total += chain_row_samples(r);
        if (chain.running && r < chain.row)
            sg.play += b;
        sg.bars += b;
    }
    sg.px = -1;
    if (chain.running && chain.row < n) {               /* how far through its row, its repeats */
        const track_t *t = &trk[0];
        uint32_t len = t->p[P_SLEN] ? (uint32_t)t->p[P_SLEN] : 1u, rep = chain_config.row[chain.row].repeat;
        uint32_t done = rep > chain.remaining ? rep - chain.remaining : 0u, at;
        if (chain_patterns[chain.row][0] >= NPAT) { len = 16u; at = done * len + chain.rest_pos / seq_div_samples(2u); }
        else at = done * len + t->seq_idx % len;
        if (chain.row >= sg.first && chain.row < sg.first + SG_COLS)
            sg.px = 8 + SG_COL_W * (int32_t)(chain.row - sg.first) + (int32_t)(at * SG_COL_W / (rep * len));
        sg.play += sg_row_bars(chain.row) * at / (rep * len);
    }
}
static void sg_lines(int32_t y, int32_t h)              /* the sections' rules and the playhead, rows y .. y + h */
{
    uint32_t j;
    for (j = 0; j < SG_COLS && sg.first + j < sg.ncols; j++)
        cv_rect(8 + SG_COL_W * (int32_t)j, y, 1, h, T_LINE);
}
static void sg_head(void)
{
    if (head_shared_status() || song_edit_status()[0]) { sgv.top = ~0u; draw_head(); return; }
    char b[24], t[12];
    uint32_t secs = chain_div_samples(sg.total + FS / 2u, FS, 0), jam_on = song.playing && !chain.running;
    uint32_t sig = secs * 7u + jam.n * 131u + jam_on * 3u + (uint32_t)(ui.act == 4u) * 5u + ux.gen * 977u + ux.pal * 31u +
                   (ui.msg_t ? str_hash(3u, ui.msg) : 0u);
    int32_t x;
    if (!ui.force && sig == sgv.top)
        return;
    sgv.top = sig;
    cv_begin(240, SG_TOP_H, T_BG);
    cv_text_r(236, ty_y(SG_TOP_H / 2, TY_TX), TY_TX, "SONG", T_THEME, T_BG);
    x = 8;
    if (sg.bars && !ui.msg_t) {                         /* its length at the tempo */
        str_cpy(t, "", 1);
        fmt_int(t, (int32_t)(secs / 60u));
        str_cpy(t + str_len(t), secs % 60u < 10u ? ":0" : ":", 3);
        fmt_int(t + str_len(t), (int32_t)(secs % 60u));
        cv_text_on(x, ty_y(SG_TOP_H / 2, TY_TX), TY_TX, t, T_TEXT, T_BG);
    }
    if (ui.msg_t) {
        cv_free_text(8, ty_y(SG_TOP_H / 2, ty_fit(TY_TX, ui.msg, 166)), ty_fit(TY_TX, ui.msg, 166), ui.msg, T_ACCENT, T_BG,
                     166);
    } else if (jam_on || jam.n) {                       /* the jam: logging (a dot), its rows */
        if (jam_on) {
            str_cpy(b, "capturing jam", sizeof b);
        } else {
            str_cpy(b, "jam \xb7 ", 8);
            fmt_int(b + str_len(b), jam.n);
            str_cpy(b + str_len(b), jam.n > 1u ? " rows" : " row", 6);
        }
        x = cv_text_r(176, ty_y(SG_TOP_H / 2, TY_LB), TY_LB, b, jam_on ? ux_mix(T_REC, T_TEXT, 30) : T_MID, T_BG);
        if (jam_on)
            cv_circle(x - 8, SG_TOP_H / 2, 9, T_REC, T_BG);
    }
    cv_blit(0, 0);
}
static void sg_labels(void)                             /* elapsed-time ruler and the playhead */
{
    uint32_t j, sig = sg.first * 7u + sg.ncols * 131u + sg.sel * 1031u + (uint32_t)(sg.px + 1) * 40503u + ux.gen * 977u +
                      ux.pal * 31u + (uint32_t)song.g[G_BPM] * 43u;
    for (j = 0; j < chain_config.count; j++)
        sig = fnv1(sig, chain_row_samples(j) + 32u * (uint32_t)song_range_has(j));
    if (!ui.force && sig == sgv.lab)
        return;
    sgv.lab = sig;
    cv_begin(240, SG_LAB_H, T_BG);
    sg_lines(2, SG_LAB_H - 2);
    uint64_t elapsed = 0;
    for (j = 0; j < sg.first && j < chain_config.count; j++) elapsed += chain_row_samples(j);
    for (j = 0; j < SG_COLS && sg.first + j < sg.ncols; j++) {
        uint32_t r = sg.first + j;
        int32_t x = 11 + SG_COL_W * (int32_t)j;
        int sel = r == sg.sel;
        char l[16]; sg_time_text(l, elapsed);
        const aafont_t *font = ty_fit(TY_LB, l, SG_COL_W - 6);
        x = cv_text_on(x, ty_y(10, font), font, l, sel ? T_TEXT : r < chain_config.count ? T_MID : T_DIM, T_BG);
        if (r < chain_config.count) elapsed += chain_row_samples(r);
        if (sel || song_range_has(r))                    /* selection remains visible across the range */
            cv_rrect(11 + SG_COL_W * (int32_t)j, 19, 14, 3, 1, T_THEME, T_BG);
        (void)x;
    }
    if (sg.px >= 0)
        cv_rect(sg.px, 18, 2, SG_LAB_H - 18, T_TEXT);
    cv_blit(0, SG_TOP_H);
}
/* Paint one vertical time range through the block's fill. Reuse its exact
 * radius-five coverage, including the anti-aliased corners and selection rim. */
static void sg_punch_span(int32_t x, int32_t w, int32_t from, int32_t to, uint16_t c, uint16_t under)
{
    if (from < x) from = x;
    if (from >= x + w) from = x + w - 1;
    if (to > x + w) to = x + w;
    if (to <= from) to = from + 1;
    cv_rect(from, 7, to - from, 18, c);
    for (int32_t px = from; px < to; px++) {
        int32_t edge = px - x < x + w - 1 - px ? px - x : x + w - 1 - px;
        for (int32_t y = 0; y < 5; y++) {
            uint32_t a = edge < 5 ? rr_cov(5, edge, y) : 16u;
            if (!a) continue;
            uint16_t fill = a == 16u ? c : ux_mix(under, c, (int32_t)(a * 100u / 16u));
            cv_pset(px, 2 + y, fill);
            cv_pset(px, 29 - y, fill);
        }
    }
}
static void sg_punch_block(uint32_t first, uint32_t last, int32_t x, int32_t w, uint16_t under)
{
    for (uint32_t r = first; r < last; r++) {
        uint32_t steps = song_punch_steps_at(r), repeats = chain_config.row[r].repeat, drawn = 0;
        /* Later starts/slots paint over earlier punches, matching playback.
         * Resuming ranges remain visible beneath the overlapping interval. */
        for (uint32_t pass = 0; pass < CHAIN_FX_EVENTS; pass++) {
            int32_t next = -1;
            for (uint32_t j = 0; j < CHAIN_FX_EVENTS; j++) {
                const chain_fx_t *fx = &chain_config.fx[r][j];
                if ((drawn & (1u << j)) || !fx->fx || fx->start >= steps) continue;
                if (next < 0 || fx->start < chain_config.fx[r][next].start) next = (int32_t)j;
            }
            if (next < 0) break;
            drawn |= 1u << next;
            const chain_fx_t *fx = &chain_config.fx[r][next];
            uint32_t end = fx->start + fx->length;
            if (end > steps) end = steps;
            uint16_t c = ux_mix(punch_color(fx->fx - 1u), T_TEXT, 30);
            for (uint32_t rep = 0; rep < repeats; rep++) {
                int32_t left = 8 + (int32_t)(r - sg.first) * SG_COL_W;
                int32_t from = left + (rep * steps + fx->start) * SG_COL_W / (steps * repeats);
                int32_t to = left + (rep * steps + end) * SG_COL_W / (steps * repeats);
                sg_punch_span(x, w, from, to > from ? to : from + 1, c, under);
            }
        }
    }
}
/* track k's blocks: a pattern over sections in a row as one (the selected track's block in the row picked alone,
 * outlined); an empty pattern: nothing (picked: dashed); a track not in use: its row's lines alone */
static void sg_row(uint32_t k)
{
    const track_t *t = &trk[k];
    uint16_t tc = t->p[P_MUTE] ? T_DIM : T_TRK(k);
    uint32_t live = k < trk_n(), cols = live ? SG_COLS : 0u;
    uint32_t j, h = fnv1(2166136261u, sg.first + 16u * sg.ncols + 256u * sg.sel + 4096u * (k == song.sel) +
                                     65536u * (uint32_t)(t->p[P_MUTE] != 0) + 131072u * live);
    h = fnv1(fnv1(h, (uint32_t)(sg.px + 1)), ux.gen * 977u + ux.pal * 31u + t->pattern_gen);
    for (j = 0; j < cols && sg.first + j < chain_config.count; j++) {
        uint32_t r = sg.first + j;
        h = fnv1(h, chain_patterns[r][k] + 16u * chain_bank(r, k) + 256u * (uint32_t)pattern_used(k, chain_bank(r, k)));
        h = fnv(h, chain_config.fx[r], sizeof chain_config.fx[r]);
        h = fnv1(h, song_punch_steps_at(r) + 128u * chain_config.row[r].repeat);
    }
    if (!ui.force && h == sgv.row[k])
        return;
    sgv.row[k] = h;
    cv_begin(240, SG_ROW_H, T_BG);
    sg_lines(0, SG_ROW_H);
    for (j = 0; j < cols && sg.first + j < chain_config.count; ) {
        uint32_t r = sg.first + j, p = chain_bank(r, k), e = j + 1u;
        uint32_t origin = chain_origin(r, k), end = chain_block_end(origin, k);
        int pick = k == song.sel && sg.sel >= origin && sg.sel < end;
        int used = pattern_used(k, p);
        int32_t x = 9 + SG_COL_W * (int32_t)j, w;
        char l[4] = {'P', (char)('0' + (p % NPAT + 1u) / 10u), (char)('0' + (p % NPAT + 1u) % 10u), 0};
        if (p % NPAT < 9u) { l[1] = l[2]; l[2] = 0; }
        if (p == CHAIN_REST) str_cpy(l, "--", sizeof l);
        while (e < SG_COLS && sg.first + e < end) e++;
        w = SG_COL_W * (int32_t)(e - j) - 2;
        if (used) {
            if (pick)                                   /* the selected track's, in the row picked: outlined */
                cv_rrect(x - 1, 1, w + 2, 30, 6, T_TEXT, T_BG);
            cv_rrect(x, 2, w, 28, 5, tc, pick ? T_TEXT : T_BG);
            sg_punch_block(r, sg.first + e, x, w, pick ? T_TEXT : T_BG);
            cv_over = 1;
            cv_text_on(x + 6, ty_y(16, TY_LB), TY_LB, l, T_INK, tc);
            cv_over = 0;
        } else {
            if (p < NPAT || pick) cv_dashed(x, 2, w, 28, 5, tc);
            cv_text_on(x + 6, ty_y(16, TY_LB), TY_LB, l, pick ? tc : T_DIM, T_BG);
        }
        j = e;
    }
    if (sg.px >= 0)
        cv_rect(sg.px, 0, 2, SG_ROW_H, T_TEXT);
    cv_blit(0, (uint32_t)(SG_ROW_Y + (int32_t)(k % NPART) * SG_ROW_H));
}
static void sg_foot(void)                               /* the sections and bars, the row's letter x repeats, how far */
{
    char b[32];
    uint32_t n = chain_config.count, r = sg.sel;
    uint32_t sig = n * 7u + sg.bars * 131u + sg.play * 1031u + r * 40503u + (uint32_t)(sg.px + 1) * 613u + ux.gen * 977u +
                   ux.pal * 31u + song.sel * 19u + (r < n ? (chain_block_end(r, song.sel) - chain_origin(r, song.sel)) * 7919u : 0u) + sg.first * 17u + sg.ncols;
    if (!ui.force && sig == sgv.foot)
        return;
    sgv.foot = sig;
    cv_begin(240, 240 - SG_FOOT_Y, T_BG);
    sg_lines(0, 4);
    if (sg.px >= 0)
        cv_rect(sg.px, 0, 2, 6, T_TEXT);
    if (!n) {
        cv_text_on(8, ty_y(17, TY_TX), TY_TX, "No sections yet", T_MID, T_BG);
    } else {
        fmt_int(b, r + 1u); str_cpy(b + str_len(b), "/", 2); fmt_int(b + str_len(b), n);
        cv_text_on(8, ty_y(17, TY_LB), TY_LB, b, T_MID, T_BG);
        if (r < n) {
            uint32_t bank = chain_bank(r, song.sel);
            str_cpy(b, bank < NPAT ? "LEN " : "REST", sizeof b);
            if (bank < NPAT) fmt_int(b + 4, chain_block_end(r, song.sel) - chain_origin(r, song.sel));
            cv_text_r(232, ty_y(17, TY_TX), TY_TX, b, T_TEXT, T_BG);
        }

    }
    cv_rrect(8, 32, 224, 6, 3, T_SURF, T_BG);
    if (chain.running && sg.bars) {
        int32_t w = (int32_t)(sg.play * 224u / sg.bars);
        cv_rrect(8, 32, w < 6 ? 6 : w, 6, 3, T_TEXT, T_SURF);
    }
    cv_blit(0, SG_FOOT_Y);
}
static void song_draw(void)
{
    uint32_t k;
    sg_frame();
    sg_head();
    sg_foot();
    sg_labels();
    for (k = trk_group(); k < trk_group() + NPART; k++)   /* (the selected track's four) */
        sg_row(k);
}
