/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Renders of the firmware UI (firmware/src/ui*.c on tests/ui_test.c's stubs), the layout lint, the MONO check
 * and the host draw cost.
 *   ui_render OUTDIR [SLOTDIR]   (run by tests/run_tests.sh; tests/ui_render.py makes the PNGs;
 *                                 SLOTDIR: filmstrips of the rolling digits, MONO and GREEN)
 * Layout lint, over every screen below in every palette, every page of every engine and every value of every
 * column: the ink box of every text and icon drawn (gfx.c GFX_HOOK_TEXT) as it lands on the screen. A finding:
 * a box off the screen, cut by its canvas, partly covered by a later strip or fill, overlapping another box, or
 * ellipsised when it is not free text (labels and values must fit; names and messages may be ellipsised), or
 * spilling out of its cell: touching a rounded rectangle of its canvas (gfx.c cv_rrect: a cell, card, row, button,
 * GFX_HOOK_CELL) without lying inside it, though still on the screen (the FX map's REVERSE).
 * MONO: every pixel of every screen is RGB565 gray (R = B, G = 2 R).
 * Rolling digits: every frame of a header BPM roll and a card roll, up and down, in every palette (lint, MONO).
 * FM6 charts (ui_graph.c graph_fm6, its parts through FM6_CHART_HOOK), every algorithm and every chart drawn: no two
 * operator boxes overlapping or touching, no route, loop, bus or label inside a box or touching one it does not
 * connect, no two nets (one gap's routes that share an operator, the output, the loop, the label) sharing or
 * touching a pixel, everything inside the panel.
 * Output: OUTDIR/ppm/<PALETTE>_<screen>.ppm for MONO GREEN PAPER, OUTDIR/report.txt (findings, ellipsised free
 * text, the draw cost), OUTDIR/text_audit.tsv (MONO, per screen: texts, icons and keycaps with their ink-box px, the
 * ellipsised ones, the texts closer than 2 px to their cell's edge, the words).
 * Exit 1 on a finding or a MONO pixel off gray. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

typedef struct { int16_t x0, y0, x1, y1; uint8_t flags; char s[28]; } rbox_t;
static rbox_t pend[256], scr[1200];
static int16_t cells[64][4];                    /* GFX_HOOK_CELL: the cells and cards of the canvas being drawn */
static uint32_t ncells, nspill;
static FILE *aud;                               /* OUTDIR/text_audit.tsv: MONO, the text of each screen */
static char tight[400];                         /* .. the texts of this screen closer than 2 px to their cell's edge */
static uint32_t ntight;
static uint32_t npend, nscr, nfind, nfree;
static uint64_t px_visited, n_text, blit_px;
static FILE *rep;
static const char *cur_name = "";
static char free_seen[64][40];
static uint32_t nfree_seen;
static void hk_text(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char *s, uint32_t flags)
{
    n_text++;
    if (npend >= 256u) return;
    pend[npend] = (rbox_t){(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, (uint8_t)flags, {0}};
    strncpy(pend[npend].s, s, 27);
    npend++;
}
static void hk_cell(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (ncells < 64u) {
        cells[ncells][0] = (int16_t)x0; cells[ncells][1] = (int16_t)y0;
        cells[ncells][2] = (int16_t)x1; cells[ncells][3] = (int16_t)y1;
        ncells++;
    }
}
static void hk_blit(uint32_t x, uint32_t y, uint32_t r0, uint32_t w, uint32_t h);
static void hk_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) hk_text(x0, y0, x1, y1, s, flags)
#define GFX_HOOK_BLIT(x, y, r0) hk_blit(x, y, r0, cv_w, cv_h)
#define UI_REF_SCREEN 1
static uint16_t ref_screen[240 * 240];                  /* the screen as sending every canvas whole leaves it */
static void hk_whole(uint32_t x, uint32_t y, uint32_t r0, uint32_t fill);
#define GFX_HOOK_WHOLE(x, y, r0, fill) hk_whole(x, y, r0, fill)
#define GFX_HOOK_BEGIN() (npend = ncells = 0)
#define GFX_HOOK_CELL(x0, y0, x1, y1) hk_cell(x0, y0, x1, y1)
#define GFX_HOOK_PIXELS(n) (px_visited += (n))
#define UI_FILL_HOOK(x, y, w, h) hk_fill(x, y, w, h)
static void fm6_hook(uint32_t kind, uint32_t a, uint32_t b);
#define FM6_CHART_HOOK(kind, a, b) fm6_hook(kind, a, b)
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

/* the FM6 chart lint: each part of a chart (FMH_*) drawn alone onto a canvas of SENT, its pixels kept in fmp_mask
 * (bit: the part), then the canvas as drawn restored with the part on it; at FMH_END the parts are checked */
#define FMP_MAX 48u
#define FMP_SENT 0x0861u
static uint64_t fmp_mask[CV_MAX];
static uint16_t fmp_save[CV_MAX];
static uint8_t fmp_kind[FMP_MAX], fmp_a[FMP_MAX], fmp_b[FMP_MAX];
static uint32_t fmp_n, fmp_charts;
static void finding(const char *what, const rbox_t *b, const rbox_t *o);
static void fmp_find(uint32_t alg, const char *what, uint32_t i, int32_t x, int32_t y, uint32_t j)
{
    static const char *const KIND[] = {"box", "route", "carrier", "bus", "loop", "label"};
    rbox_t b = {(int16_t)x, (int16_t)y, (int16_t)x, (int16_t)y, 0, {0}}, o = b;
    snprintf(b.s, sizeof b.s, "ALG %u %s %u>%u", alg + 1u, KIND[fmp_kind[i]], fmp_a[i] + 1u, fmp_b[i] + 1u);
    snprintf(o.s, sizeof o.s, "%s %u>%u", j < FMP_MAX ? KIND[fmp_kind[j]] : "panel", j < FMP_MAX ? fmp_a[j] + 1u : 0u,
             j < FMP_MAX ? fmp_b[j] + 1u : 0u);
    finding(what, &b, &o);
}
static void fmp_check(uint32_t alg)
{
    int32_t bx0[FMP_MAX], by0[FMP_MAX], bx1[FMP_MAX], by1[FMP_MAX], x, y, dx, dy;
    uint32_t net[FMP_MAX], par[12], i, j, w = cv_w, h = cv_h;
    for (i = 0; i < 12u; i++) par[i] = i;               /* the routes' nets: an operator as source (0..5), as destination (6..11) */
    #define FMP_ROOT(v) ({ uint32_t r_ = (v); while (par[r_] != r_) r_ = par[r_]; r_; })
    for (i = 0; i < fmp_n; i++)
        if (fmp_kind[i] == FMH_ROUTE) par[FMP_ROOT(fmp_a[i])] = FMP_ROOT(6u + fmp_b[i]);
    for (i = 0; i < fmp_n; i++) {
        net[i] = fmp_kind[i] == FMH_ROUTE ? FMP_ROOT(6u + fmp_b[i]) : fmp_kind[i] == FMH_CAR || fmp_kind[i] == FMH_BUS ? 20u
                 : 21u + fmp_kind[i];
        bx0[i] = by0[i] = 9999; bx1[i] = by1[i] = -1;
    }
    #undef FMP_ROOT
    for (y = 0; y < (int32_t)h; y++)                     /* each part's extent */
        for (x = 0; x < (int32_t)w; x++)
            for (i = 0; i < fmp_n; i++)
                if (fmp_mask[y * w + x] >> i & 1u) {
                    if (x < bx0[i]) bx0[i] = x;
                    if (y < by0[i]) by0[i] = y;
                    if (x > bx1[i]) bx1[i] = x;
                    if (y > by1[i]) by1[i] = y;
                }
    for (i = 0; i < fmp_n; i++)                          /* boxes: apart, by a pixel at least */
        for (j = 0; j < i; j++)
            if (fmp_kind[i] == FMH_BOX && fmp_kind[j] == FMH_BOX && bx0[i] <= bx1[j] + 1 && bx0[j] <= bx1[i] + 1 &&
                by0[i] <= by1[j] + 1 && by0[j] <= by1[i] + 1)
                fmp_find(alg, "FM6 boxes overlap", i, bx0[i], by0[i], j);
    for (y = 0; y < (int32_t)h; y++)
        for (x = 0; x < (int32_t)w; x++) {
            uint64_t mk = fmp_mask[y * w + x];
            if (!mk) continue;
            for (i = 0; i < fmp_n; i++) {
                if (!(mk >> i & 1u) || fmp_kind[i] == FMH_BOX) continue;
                if (x < 6 || x > 233 || y < 2 || y > 119)
                    fmp_find(alg, "FM6 part off the panel", i, x, y, FMP_MAX);
                for (j = 0; j < fmp_n; j++) {
                    if (fmp_kind[j] != FMH_BOX) continue;
                    if (x >= bx0[j] && x <= bx1[j] && y >= by0[j] && y <= by1[j])
                        fmp_find(alg, "FM6 part through a box", i, x, y, j);
                    else if (x >= bx0[j] - 1 && x <= bx1[j] + 1 && y >= by0[j] - 1 && y <= by1[j] + 1 &&
                             !((fmp_kind[i] == FMH_ROUTE && (fmp_a[i] == fmp_a[j] || fmp_b[i] == fmp_a[j])) ||
                               (fmp_kind[i] == FMH_CAR && fmp_a[i] == fmp_a[j]) ||
                               (fmp_kind[i] == FMH_FB && (fmp_a[i] == fmp_a[j] || fmp_b[i] == fmp_a[j]))))
                        fmp_find(alg, "FM6 part touches a box", i, x, y, j);
                }
                for (dy = -1; dy <= 1; dy++)              /* another net on this pixel or next to it */
                    for (dx = -1; dx <= 1; dx++) {
                        int32_t nx = x + dx, ny = y + dy;
                        uint64_t mn;
                        if (nx < 0 || ny < 0 || nx >= (int32_t)w || ny >= (int32_t)h) continue;
                        mn = fmp_mask[ny * w + nx];
                        for (j = 0; j < fmp_n; j++)
                            if (mn >> j & 1u && fmp_kind[j] != FMH_BOX && net[j] != net[i])
                                fmp_find(alg, "FM6 nets cross or touch", i, x, y, j);
                    }
            }
        }
    fmp_charts++;
}
static void fm6_hook(uint32_t kind, uint32_t a, uint32_t b)
{
    uint32_t i, n = cv_w * cv_h;
    if (fmp_n) {                                         /* the part just drawn: its pixels */
        for (i = 0; i < n; i++)
            if (cv_px[i] != FMP_SENT) fmp_mask[i] |= (uint64_t)1u << (fmp_n - 1u);
            else cv_px[i] = fmp_save[i];
    } else {
        memset(fmp_mask, 0, n * sizeof fmp_mask[0]);
    }
    if (kind == FMH_END) {
        fmp_check(a);
        fmp_n = 0;
        return;
    }
    if (fmp_n >= FMP_MAX) { fprintf(stderr, "ui_render: an FM6 chart of more than %u parts\n", FMP_MAX); exit(1); }
    memcpy(fmp_save, cv_px, n * sizeof cv_px[0]);
    for (i = 0; i < n; i++) cv_px[i] = FMP_SENT;
    fmp_kind[fmp_n] = (uint8_t)kind; fmp_a[fmp_n] = (uint8_t)a; fmp_b[fmp_n] = (uint8_t)b;
    fmp_n++;
}

static uint8_t aud_on;                                  /* the LCD load audit runs: its frames are not linted */
static void finding(const char *what, const rbox_t *b, const rbox_t *o)
{
    static char seen[600][64];
    static uint32_t nseen;
    char key[64];
    uint32_t i;
    if (aud_on)
        return;
    nfind++;
    snprintf(key, sizeof key, "%.20s|%.14s|%.24s", cur_name, what, b->s);
    for (i = 0; i < nseen && strcmp(seen[i], key); i++) ;
    if (i < nseen) return;                       /* each finding once per screen */
    if (nseen < 600u) strcpy(seen[nseen++], key);
    else return;
    fprintf(rep, "LINT %-22s %-32s '%s' (%d,%d)-(%d,%d)", cur_name, what, b->s, b->x0, b->y0, b->x1, b->y1);
    if (o) fprintf(rep, " / '%s' (%d,%d)-(%d,%d)", o->s, o->x0, o->y0, o->x1, o->y1);
    fputc('\n', rep);
}
/* a rectangle of the screen is drawn again: boxes inside it are gone, boxes it covers in part are hidden */
static void cover(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t i, k = 0;
    for (i = 0; i < nscr; i++) {
        rbox_t *b = &scr[i];
        int in = b->x0 >= x0 && b->x1 <= x1 && b->y0 >= y0 && b->y1 <= y1;
        int touch = b->x0 < x1 && x0 < b->x1 && b->y0 < y1 && y0 < b->y1;
        if (in) continue;
        if (touch && !(b->flags & 48u)) finding("hidden in part by a later draw", b, 0);
        if (touch && (b->flags & 48u)) scr[k++] = *b;   /* split across bands / scrolled: drawn on purpose */
        else scr[k++] = *b;
    }
    nscr = k;
}
static void hk_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { cover((int32_t)x, (int32_t)y, (int32_t)(x + w), (int32_t)(y + h)); }
/* a text across two bands (the menu and the document draw 124 + 85 rows): each band draws its part, the same
 * screen box twice, cut at the band edge; together they are whole (flag 16) */
static int split_pair(rbox_t *b)
{
    uint32_t j;
    if (!(b->flags & 2u)) return 0;
    for (j = 0; j < nscr; j++) {
        rbox_t *o = &scr[j];
        if ((o->flags & 2u) && o->x0 == b->x0 && o->x1 == b->x1 && o->y0 == b->y0 && o->y1 == b->y1 && !strcmp(o->s, b->s)) {
            o->flags = (uint8_t)((o->flags & ~2u) | 16u);
            return 1;
        }
    }
    return 0;
}
/* a text or icon box that touches a cell or card of its canvas (GFX_HOOK_CELL) but spills past its edge; for the
 * audit, a text inside its smallest cell closer than 2 px to an edge */
static void contain(int32_t x, int32_t y)
{
    uint32_t i, c;
    for (i = 0; i < npend; i++) {
        const rbox_t *b = &pend[i];
        int32_t best = 1 << 30, gap = 99;
        for (c = 0; c < ncells && !(b->flags & 4u); c++) {   /* (texts only: an icon's box is its whole cell) */
            const int16_t *r = cells[c];
            int32_t a = (r[2] - r[0]) * (r[3] - r[1]), g;
            if (b->x0 < r[0] || b->x1 > r[2] || b->y0 < r[1] || b->y1 > r[3] || a >= best) continue;
            best = a;
            g = b->x0 - r[0];
            if (r[2] - b->x1 < g) g = r[2] - b->x1;
            if (b->y0 - r[1] < g) g = b->y0 - r[1];
            if (r[3] - b->y1 < g) g = r[3] - b->y1;
            gap = g;
        }
        if (gap < 2 && aud) {
            ntight++;
            if (strlen(tight) + strlen(b->s) + 8u < sizeof tight)
                snprintf(tight + strlen(tight), sizeof tight - strlen(tight), "%s'%s' %dpx", tight[0] ? ", " : "", b->s, (int)gap);
        }
        for (c = 0; c < ncells; c++) {
            const int16_t *r = cells[c];
            int touch = b->x0 < r[2] && r[0] < b->x1 && b->y0 < r[3] && r[1] < b->y1;
            int in = b->x0 >= r[0] && b->x1 <= r[2] && b->y0 >= r[1] && b->y1 <= r[3];
            int around = r[0] >= b->x0 && r[2] <= b->x1 && r[1] >= b->y0 && r[3] <= b->y1;   /* (a mark inside the box) */
            if (touch && !in && !around) {
                rbox_t t = *b, o = {(int16_t)(r[0] + x), (int16_t)(r[1] + y), (int16_t)(r[2] + x), (int16_t)(r[3] + y), 0, "cell"};
                t.x0 += (int16_t)x; t.x1 += (int16_t)x; t.y0 += (int16_t)y; t.y1 += (int16_t)y;
                nspill++;
                finding("spills out of its cell", &t, &o);
                break;
            }
        }
    }
}
static void hk_whole(uint32_t x, uint32_t y, uint32_t r0, uint32_t fill)
{
    uint32_t i, j, w = cv_w, h = cv_h;
    if (fill) {                                         /* a fill: r0 its w | h << 16, fill its colour | 1 << 16 */
        uint16_t c = (uint16_t)fill;
        w = r0 & 0xFFFFu; h = r0 >> 16;
        for (j = 0; j < h && y + j < 240u; j++)
            for (i = 0; i < w && x + i < 240u; i++)
                ref_screen[(y + j) * 240u + x + i] = (uint16_t)((c >> 8) | (c << 8));
        return;
    }
    for (j = r0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            ref_screen[(y + j) * 240u + x + i] = cv_px[j * w + i];
}
static void hk_blit(uint32_t x, uint32_t y, uint32_t r0, uint32_t w, uint32_t h)
{
    uint32_t i, k = 0;
    contain((int32_t)x, (int32_t)y);
    ncells = 0;
    blit_px += (uint64_t)w * (h > r0 ? h - r0 : 0u);
    for (i = 0; i < npend; i++) {
        rbox_t b = pend[i];
        if (b.y1 <= (int32_t)r0) continue;           /* rows not blitted */
        if (b.y0 < (int32_t)r0) b.flags |= 2u;
        b.x0 += (int16_t)x; b.x1 += (int16_t)x; b.y0 += (int16_t)y; b.y1 += (int16_t)y;
        if (!split_pair(&b)) pend[k++] = b;
    }
    cover((int32_t)x, (int32_t)(y + r0), (int32_t)(x + w), (int32_t)(y + h));
    for (i = 0; i < k && nscr < 1200u; i++)
        scr[nscr++] = pend[i];
    npend = 0;
}

static void lint(void)
{
    uint32_t i, j;
    for (i = 0; i < nscr; i++) {
        const rbox_t *b = &scr[i];
        if (b->x0 < 0 || b->y0 < 0 || b->x1 > 240 || b->y1 > 240) finding("off the screen", b, 0);
        if ((b->flags & 2u) && !(b->flags & 32u)) finding("cut by its canvas", b, 0);
        if ((b->flags & 1u) && !(b->flags & 8u)) finding("ellipsised (not free text)", b, 0);
        if ((b->flags & 9u) == 9u) {
            for (j = 0; j < nfree_seen && strcmp(free_seen[j], b->s); j++) ;
            if (j == nfree_seen && nfree_seen < 64u) {
                snprintf(free_seen[nfree_seen++], 40, "%s", b->s);
                fprintf(rep, "free   %-22s ellipsised '%s'\n", cur_name, b->s);
            }
            nfree++;
        }
        for (j = i + 1u; j < nscr; j++) {
            const rbox_t *o = &scr[j];
            if (b->x0 < o->x1 && o->x0 < b->x1 && b->y0 < o->y1 && o->y0 < b->y1) finding("overlaps", b, o);
        }
    }
}

/* the audit of one screen (MONO): its texts (count, ink-box px), icons, keycaps, the ellipsised and tight texts,
 * and the words themselves */
static FILE *audf;
static void audit_scene(const char *name)
{
    uint32_t i, nt = 0, ni = 0, nk = 0, ne = 0, at = 0, ai = 0, ak = 0;
    char words[1200] = "";
    if (!audf) return;
    for (i = 0; i < nscr; i++) {
        const rbox_t *b = &scr[i];
        uint32_t a = (uint32_t)((b->x1 - b->x0) * (b->y1 - b->y0));
        if ((b->flags & 4u) && !strcmp(b->s, "icon")) { ni++; ai += a; continue; }
        if (b->flags & 4u) { nk++; ak += a; continue; }
        nt++; at += a;
        if (b->flags & 1u) ne++;
        if (strlen(words) + strlen(b->s) + 4u < sizeof words)
            snprintf(words + strlen(words), sizeof words - strlen(words), "%s%s", words[0] ? " | " : "", b->s);
    }
    fprintf(audf, "%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%s\t%s\n", name, nt, at, ni, ai, nk, ak, ne, ntight, words, tight);
}
static uint32_t mono_bad;
static void mono_check(void)
{
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        if ((c >> 11) != (c & 31u) || ((c >> 5) & 63u) != (c >> 11) * 2u) {
            if (!mono_bad) fprintf(rep, "MONO %s: pixel %u,%u = %04x off gray\n", cur_name, i % 240u, i / 240u, c);
            mono_bad++;
        }
    }
}
static void write_ppm(const char *dir, const char *pal, const char *name)
{
    char path[512];
    uint32_t i;
    FILE *f;
    snprintf(path, sizeof path, "%s/ppm/%s_%s.ppm", dir, pal, name);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        uint8_t b[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u), (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}

/* ------------------------------------------------------------------ the screens --- */
static void pal(uint32_t p) { settings.palette = p; palette_set(p); }
static void state(void)                          /* a playing song with steps on every track */
{
    uint32_t i;
    ui_power_on();
    settings_boot = 0;
    memset(&tmpl, 0, sizeof tmpl);
#if MELODEE_USB_AUDIO
    ua_off_want = 0;
#endif
    song.playing = 1;
    song.g[G_BPM] = 124;
    for (i = 0; i < NTRK; i++) trk[i].seq_idx = 5;
    my_steps(&trk[1]);
    trk[0].step[2].flags |= SF_ACCENT;
    trk[0].step[6].flags |= SF_SLIDE;
#if MELODEE_SLICE && SMP_USER_SLOTS
    if (usr_nz[0]) {                              /* (slices_usr filled USR1: empty again) */
        memset(host_slots, 0, sizeof host_slots);
        smp_user_scan(0);
        slc_man_save = 0;
    }
#endif
    for (i = 0; i < SCOPE_N; i++)                 /* a stand-in signal for the scope */
        scope_buf[i] = (int16_t)((int32_t)((i * 37u) % 128u) * 200 - 12800 + (int32_t)((i % 32u) < 16u ? 3000 : -3000));
    scope_w = 0;
}
static void drum(uint32_t kit)
{
    uint32_t i;
    song.sel = 3;
    trk[3].p[P_E0] = (int16_t)kit;
    for (i = 0; i < 16u; i++) {
        step_t *s = &trk[3].step[i];
        memset(s, 0, sizeof *s);
        s->time = ST_NOTE; s->vel = 100;
        if (i % 4u == 0u) s->hit |= 1u;
        if (i == 4u || i == 12u) s->hit |= 2u, s->acc |= 2u;
        if (i == 10u) s->hit |= 4u;
        if (i % 2u == 0u) s->hit |= 8u;
        if (i == 14u) s->hit |= 16u;
        if (i == 7u || i == 15u) s->hit |= 32u;
        if (i == 3u) s->hit |= 64u;
        if (i == 0u) s->hit |= 128u, s->acc |= 128u;
    }
    trk[3].p[P_SLEN] = 32;
}
static void eng(uint32_t e) { set_engine_of(TSEL, e); }
/* the FM engine of the FM screens: DIGITAL with MELODEE_FM4, else FM6 (DIGITAL retired: its own screens, the OP ENV /
 * OP LEVEL pages and the algorithm charts, exist only there: fm4_screen) */
#define E_FM (MELODEE_FM4 ? ENGI_DIGITAL : ENGI_FM6)

enum { S_HOME, S_HOME_IDLE, S_HOME_NOTE, S_HOME_CHORD, S_HOME_INVERSION, S_HOME_WIDE, S_HOME_RELEASED, S_HOME_FM6, S_MESSAGE, S_MESSAGE_KEY, S_PRESETS, S_PRESETS_NOFAV, S_PRESETS_CAT, S_PRESETS_PENDING, S_PRESETS_RECENT, S_USER, S_PROJECT, S_PROJECT_BOOT, S_TEMPO,
       S_SONG_EMPTY, S_SONG, S_STEP, S_PATTERN, S_MOTION, S_DRUM, S_MIXER, S_MIXER_PAN,
       S_ENV, S_ENVDEST, S_LFO, S_MOD, S_FX, S_SLICER, S_DLY, S_SCL, S_CHORD, S_CHORD_WIDE, S_CHORD_OFF, S_CHORD_KIT, S_ARP, S_VOICE, S_GLOBAL, S_SYSTEM,
       S_EDIT_ANALOG, S_EDIT_DIGITAL, S_OP_ENV, S_EDIT_WHEEL, S_EDIT_PHYS,
       S_ALG1, S_ALG2, S_ALG3, S_ALG4, S_ALG5, S_ALG6, S_ALG7, S_ALG8, S_OP_LEVEL,
       S_FM6_ALG1, S_FM6_ALG5, S_FM6_ALG22, S_FM6_ALG32, S_FM6_FREQ, S_FM6_EG, S_FM6_PEG, S_FM6_STORE, S_CZ1_ENV,
       S_CONFIRM_SEQ, S_CONFIRM_PROJ, S_CONFIRM_USER, S_CONFIRM_MOTION, S_CONFIRM_ERASE,
       S_MENU, S_MENU_SPEAKER, S_ABOUT, S_ABOUT_REC, S_ABOUT_CREDITS, S_ABOUT_END, S_UBOOT, S_CALIBRATION,
       S_BATT0, S_BATT1, S_BATT2, S_BATT3, S_BATT_USB, S_MOTION_REC, S_MOTION_OFF, S_SONG_HOME,
       S_FX_PEEK, S_FX_HELD, S_FX_WAIT, S_FX_HARM, S_MENU_HOLD, S_MENU_USB, S_REVERB,
       S_GLO_PEEK, S_GLO_ACTIVE, S_GLO_EXT, S_SCL_PEEK, S_SCL_ACTIVE, S_EDIT_PEEK, S_EDIT_ACTIVE, S_EDIT_USER, S_LAYER_HINT,
       S_NAME_USER, S_NAME_TYPING, S_NAME_123, S_NAME_EMPTY, S_NAME_FULL, S_NAME_PLAYING, S_PROJECT_NAMED, S_SONG_NAMED,
       S_USER_FOOT,
#if MELODEE_SLICE && SMP_USER_SLOTS
       S_SLICES_BREAK, S_SLICES_USR,
#endif
       S_ROLL_EMPTY, S_ROLL_ACID, S_ROLL_CHORDS, S_ROLL_TIES, S_ROLL_LEN32, S_ROLL_HIGH, S_ROLL_LOW, S_ROLL_WIDE, S_ROLL_PLAYING,
       S_MOCK_HOME, S_MOCK_PRESETS, S_MOCK_SEQ, S_MOCK_DRUM, S_MOCK_MIXER, S_MOCK_DIALOG, S_MOCK_MENU, S_NATIVE_FM_USER, S_NATIVE_CZ_USER,
       S_NOTES_SLIDE, S_NOTES_MIXED, S_NOTES_CHORD, S_NOTES_EMPTY, S_NOTES_RAW, S_NOTES_ZOOM, S_NOTES_LOOP, S_NOTES_DRUM, S_NOTES_DENSE, S_NOTES_REC, S_NOTES_ERASE, S_NOTES_DRUM_REC, S_NOTES_DRUM_ERASE, S_SCL_MICRO, S_SCL_MICRO_LAYER, S_SCL_MICRO_CHORD, S_SCALE_PICKER_EDO, S_SCALE_PICKER_HIST, S_SCALE_PICKER_FAV, S_SCALE_PICKER_EMPTY, S_SCALE_SETTINGS_FAV, S_MENU_CLICK, S_MENU_CLICK_LEVEL, S_MENU_COUNTIN, S_MENU_PREVIEW, S_MENU_ADD, S_DRUM_SOUND_808, S_DRUM_SOUND_909, S_DRUM_MIX_909, S_DRUM_HIT_909, S_DRUM_HIT_FREE, S_DRUM_HIT_LONG,
       S_STAGE_DRUM, S_STAGE_CZ, S_STAGE_P5, S_STAGE_QUEUED, S_STAGE_BROWSE, S_STAGE_STOPPED, S_STAGE_FILTER, S_STAGE_ENV, S_PATGRID, S_PATGRID_STOPPED, S_PATGRID_SHEET, S_PATGRID_COPY, S_PATGRID_REPLACE, S_PATGRID_DELETE, S_PROJECT_NEW, S_NEW_KEY, S_NEW_ROLES,
       S_REF_STAGE_HELD, S_REF_STAGE_RELEASED, S_REF_STAGE_CUTOFF, S_REF_STAGE_DRUM, S_REF_BROWSER, S_REF_PATTERNS, S_REF_SONG,
       S_REF_ENV, S_REF_LFO, S_REF_EDIT_OSC, S_REF_FX, S_REF_DLY, S_REF_MIXER, S_REF_NOTES, S_REF_SETTINGS, S_REF_DIALOG,
       S_REF_SHEET_SOUND, S_REF_SHEET_SONG, S_REF_PICKER_WAVE, S_REF_SECTIONS_P5, S_REF_MAP_P5, S_REF_MAP_FM6,
       S_REF_SCL_LIST, S_REF_SHEET_PROJECT, S_REF_SHEET_NOTE, S_REF_SHEET_HIT, S_REF_MOTION,
       S_REF_SHEET_MOTION, S_REF_MOD, S_REF_PICKER_MOD, S_REF_ARP, S_REF_PATTERN,
       S_REF_SLICER, S_REF_FMEG, S_REF_GRID, S_REF_SCALES, S_COUNT };
static const char *const S_NAME[S_COUNT] = {"home", "home_idle", "home_note", "home_chord", "home_inversion", "home_wide", "home_released", "home_fm6", "message", "message_key", "presets", "presets_nofav", "presets_cat", "presets_pending", "presets_recent", "user",
    "project", "project_boot", "tempo", "song_empty", "song", "step", "pattern", "motion", "drum",
    "mixer", "mixer_pan", "env", "env_dest", "lfo", "mod", "fx", "slicer", "dly", "scl", "chord", "chord_wide", "chord_off", "chord_kit", "arp",
    "voice", "global", "system", "edit_analog", MELODEE_FM4 ? "edit_digital" : "edit_fm6", "op_env", "edit_wheel",
    "edit_phys", "alg_1", "alg_2", "alg_3", "alg_4", "alg_5", "alg_6", "alg_7", "alg_8", "op_level", "fm6_alg_01", "fm6_alg_05", "fm6_alg_22", "fm6_alg_32", "fm6_freq", "fm6_eg", "fm6_peg", "fm6_store", "cz1_env", "confirm_seq", "confirm_project", "confirm_user",
    "confirm_motion", "confirm_erase", "menu", "menu_speaker", "about", "about_rec", "about_credits", "about_end", "uboot", "calibration",
    "batt_0", "batt_1", "batt_2", "batt_3", "batt_usb", "motion_rec", "motion_off", "song_home",
    "perform_peek", "perform_held", "perform_wait", "perform_harm", "menu_hold", "menu_usb", "reverb_spring",
    "layer_glo_peek", "layer_glo_active", "layer_glo_ext", "layer_scl_peek", "layer_scl_active", "layer_edit_peek",
    "layer_edit_active", "layer_edit_user", "layer_hint",
    "name_user", "name_typing", "name_123", "name_empty", "name_full", "name_playing", "project_named", "song_named",
    "user_foot",
#if MELODEE_SLICE && SMP_USER_SLOTS
    "slices_break", "slices_usr",
#endif
    "roll_empty", "roll_acid", "roll_chords", "roll_ties", "roll_len32_p2", "roll_high", "roll_low", "roll_wide", "roll_playing",
    "mock_home", "mock_presets", "mock_seq", "mock_drum", "mock_mixer", "mock_dialog", "mock_menu", "native_fm_user", "native_cz_user",
    "notes_slide", "notes_mixed", "notes_chord", "notes_empty", "notes_raw", "notes_zoom", "notes_loop", "notes_drum", "notes_dense", "notes_rec", "notes_erase", "notes_drum_rec", "notes_drum_erase", "scl_micro", "scl_micro_layer", "scl_micro_chord", "scale_picker_edo", "scale_picker_historical", "scale_picker_favorites", "scale_picker_empty", "scale_settings_favorite", "menu_click", "menu_click_level", "menu_countin", "menu_preview", "menu_add", "drum_sound_808", "drum_sound_909", "drum_mix_909", "drum_hit_909", "drum_hit_free", "drum_hit_long",
    "stage_drum", "stage_cz", "stage_p5", "stage_queued", "stage_browse", "stage_stopped", "stage_filter", "stage_env", "patterns", "patterns_stopped", "patterns_sheet", "patterns_copy", "patterns_replace", "patterns_delete", "project_new", "new_key", "new_roles",
    "ref_stage_held", "ref_stage_released", "ref_stage_cutoff", "ref_stage_drum", "ref_browser", "ref_patterns", "ref_song", "ref_env", "ref_lfo", "ref_edit_osc", "ref_fx", "ref_dly", "ref_mixer", "ref_notes", "ref_settings", "ref_dialog", "ref_sheet_sound", "ref_sheet_song", "ref_picker_wave", "ref_sections_p5", "ref_map_p5", "ref_map_fm6", "ref_scl_list", "ref_sheet_project", "ref_sheet_note", "ref_sheet_hit", "ref_motion", "ref_sheet_motion", "ref_mod", "ref_picker_mod", "ref_arp", "ref_pattern", "ref_slicer", "ref_fmeg", "ref_grid", "ref_scales"};

/* the scenes of the UI design screens: the state the UI-redesign
 * prototype drew them from (its setup(): two pattern tracks, the drum pattern on track 4, a synthetic scope),
 * plus the hot knob each mock shows; tests/ui_mockcmp.py compares these renders with the mock PNGs */
static void mock_state(int s)
{
    uint32_t i;
    ui_power_on();
    usb.config = 0;
    song.batt_raw = 600;
    for (i = 0; i < SCOPE_N; i++) {                  /* a saw with a little second harmonic */
        double ph = fmod(i / 64.0, 1.0);
        scope_buf[i] = (int16_t)((ph * 2.0 - 1.0) * 9000.0 + sin(i * 0.4908) * 2500.0);
    }
    scope_w = 0;
    song.playing = 1;
    song.g[G_BPM] = 124;
    for (i = 0; i < NTRK; i++) trk[i].seq_idx = 5;
    demo_pat16(&trk[0], DEMO_ACID);
    demo_pat16(&trk[1], DEMO_PAD);
    for (i = 0; i < 16u; i++) {
        step_t *st = &trk[3].step[i];
        memset(st, 0, sizeof *st);
        st->time = ST_NOTE; st->vel = 100;
        if (i % 4u == 0u) st->hit |= 1u;
        if (i == 4u || i == 12u) st->hit |= 2u, st->acc |= 2u;
        if (i == 10u) st->hit |= 4u;
        if (i % 2u == 0u) st->hit |= 8u;
        if (i == 14u) st->hit |= 16u;
        if (i == 7u || i == 15u) st->hit |= 32u;
        if (i == 3u) st->hit |= 64u;
        if (i == 0u) st->hit |= 128u, st->acc |= 128u;
    }
    trk[3].p[P_SLEN] = 32;
    switch (s) {
    case S_MOCK_HOME: ui.home = 1; ui.hot_col = 1; ui.hot_t = 30; break;
    case S_MOCK_PRESETS: go_page(GR_BROWSE); break;
    case S_MOCK_SEQ: song.sel = 0; song.rec = 1; go_page(GR_ROLL); ui.cursor = 6; ui.hot_col = 0; ui.hot_t = 30; break;
    case S_MOCK_DRUM: song.sel = 3; go_page(GR_ROLL); ui.cursor = 4; ui.lane = 1; trk[3].seq_idx = 9; ui.hot_col = 0; ui.hot_t = 30; break;
    case S_MOCK_MIXER:
        go_page(GR_TRK);
        trk[0].p[P_LEVEL] = 100; trk[1].p[P_LEVEL] = 84; trk[2].p[P_LEVEL] = 64; trk[3].p[P_LEVEL] = 110;
        trk[0].p[P_PAN] = -20; trk[1].p[P_PAN] = 24; trk[2].p[P_PAN] = 0; trk[3].p[P_PAN] = -4;
        trk[0].p[P_REV] = 30; trk[1].p[P_REV] = 80; trk[2].p[P_REV] = 10; trk[3].p[P_REV] = 0;
        trk[2].p[P_MUTE] = 1;
        song.rec = 2u;
        trk[0].peak = 11000; trk[1].peak = 3300; trk[3].peak = 20000;     /* (the mock's stand-in meters) */
        ui.hot_col = 1; ui.hot_t = 30;
        break;
    case S_MOCK_DIALOG: ui.confirm = CF_OVR_PROJ; ui.confirm_trk = 0; go_title("PROJECT"); break;
    case S_MOCK_MENU: ui.menu = 1; ui.menu_sel = 0; break;
    default: break;
    }
}

/* the STEP page's piano roll (ui_graph.c graph_roll) on track 1, stopped unless said: an empty pattern; ACID in
 * A minor; POLY chords (Am7 F C G, tied); a line with ties, slides and accents in C major; LEN 32 on its second
 * page, playing; high (A6..) and low (C-1..) notes; a page wider than the view (edge marks); ACID playing with a
 * key held (A3, its row lit) */
static void step_put(step_t *st, uint32_t time, uint32_t flags, uint32_t n, const uint8_t *notes)
{
    uint32_t j;
    memset(st, 0, sizeof *st);
    st->time = (uint8_t)time; st->flags = (uint8_t)flags; st->n = (uint8_t)n; st->vel = 100;
    for (j = 0; j < n; j++) st->note[j] = notes[j];
}
static void roll_scene(int s)
{
    static const uint8_t AM7[] = {57, 60, 64, 67}, FM7[] = {53, 57, 60, 64}, CMA[] = {60, 64, 67}, GMA[] = {55, 59, 62, 67};
    static const uint8_t LINE[16] = {48, 0, 55, 60, 0, 59, 0, 0, 62, 64, 0, 67, 0, 65, 64, 0};
    static const uint8_t LINE_T[16] = {0, 1, 0, 0, 1, 0, 2, 2, 0, 0, 1, 0, 1, 0, 0, 2};   /* 0 note, 1 tie, 2 rest */
    track_t *t;
    uint32_t i;
    song.sel = 0; t = TSEL;
    song.playing = 0;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    go_page(GR_ROLL);
    ui.cursor = 0;
    switch (s) {
    case S_ROLL_EMPTY: break;
    case S_ROLL_ACID:
    case S_ROLL_PLAYING:
        demo_pat16(t, DEMO_ACID);
        t->p[P_ROOT] = 9; t->p[P_SCALE] = 2; ui.cursor = 6;
        if (s == S_ROLL_PLAYING) {
            song.playing = 1; t->seq_idx = 9;
            kb_trk[3] = 0; kb_chn[3] = 1; kb_chord[3][0] = 57;            /* A3 held */
        }
        break;
    case S_ROLL_CHORDS:
        t->p[P_VOICE] = V_POLY; t->p[P_ROOT] = 9; t->p[P_SCALE] = 2;
        for (i = 0; i < 16u; i++) {
            const uint8_t *c = i / 4u == 0u ? AM7 : i / 4u == 1u ? FM7 : i / 4u == 2u ? CMA : GMA;
            uint32_t n = i / 4u == 2u ? 3u : 4u;
            if (i % 4u == 0u) step_put(&t->step[i], ST_NOTE, i == 8u ? SF_ACCENT : 0u, n, c);
            else if (i % 4u == 1u) step_put(&t->step[i], ST_TIE, 0, 0, c);
            else if (i % 4u == 2u) step_put(&t->step[i], ST_NOTE, 0, n, c);
            else step_put(&t->step[i], ST_REST, 0, 0, c);
        }
        ui.cursor = 4;
        break;
    case S_ROLL_TIES:
        t->p[P_ROOT] = 0; t->p[P_SCALE] = 1;
        for (i = 0; i < 16u; i++) {
            uint8_t n = LINE[i];
            uint32_t f = (i == 3u || i == 9u ? SF_SLIDE : 0u) | (i == 0u || i == 8u || i == 13u ? SF_ACCENT : 0u);
            if (LINE_T[i] == 0u) step_put(&t->step[i], ST_NOTE, f, 1, &n);
            else step_put(&t->step[i], LINE_T[i] == 1u ? ST_TIE : ST_REST, 0, 0, &n);
        }
        t->step[2].flags |= SF_SLIDE;                  /* (slides into a note, from before a tie) */
        ui.cursor = 9;
        break;
    case S_ROLL_LEN32:
        demo_pat16(t, DEMO_ACID);
        t->p[P_SLEN] = 32;
        for (i = 16; i < 32u; i++) {
            uint8_t n = (uint8_t)(60 + (i * 5u) % 12u);
            if (i % 3u != 2u) step_put(&t->step[i], ST_NOTE, i % 8u == 0u ? SF_ACCENT : 0u, 1, &n);
        }
        ui.cursor = 20; song.playing = 1; t->seq_idx = 22;
        break;
    case S_ROLL_HIGH:
    case S_ROLL_LOW:
        for (i = 0; i < 16u; i += 2u) {
            uint8_t n = (uint8_t)(s == S_ROLL_HIGH ? 112 + (i * 7u) % 15u : (i * 7u) % 15u);
            step_put(&t->step[i], ST_NOTE, 0, 1, &n);
        }
        ui.cursor = 2;
        break;
    case S_ROLL_WIDE:
        t->p[P_VOICE] = V_POLY;
        for (i = 0; i < 16u; i += 2u) {
            uint8_t n[2] = {(uint8_t)(36 + i), (uint8_t)(72 + i)};
            step_put(&t->step[i], ST_NOTE, 0, 2, n);
        }
        ui.cursor = 4;
        break;
    default: break;
    }
    ui.bank = (uint8_t)(ui.cursor / 16u);
}

/* the redesign's reference scenes (docs/design/ref, tools/ui_mockcmp.py): the mockups' state with the device's own
 * sounds: 1 909 KIT, 2 Pickle Pincher (PROPHET, selected), 3 a CZ-1 ELEC.PIANO, 4 FM6 BRASS SECT; patterns P1 P2 P1 P4;
 * playing at 124 BPM, step 6 */
static uint32_t preset_named(uint32_t e, const char *n)
{
    uint32_t k;
    for (k = 0; k < ENGINES[e]->npresets; k++)
        if (!strcmp(ENGINES[e]->presets[k].name, n)) return k;
    return 0;
}
/* Stage's knobs out (a knob just turned), dropped in all the way */
static void stage_out(void) { stage.knob_ms = fm1_ms | 1u; stage.out = 1; stage.drop = 0; }
static void ref_steps(track_t *t, const char *on, uint32_t note, int hits)
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        step_t *st = &t->step[i];
        memset(st, 0, sizeof *st);
        st->time = ST_REST;
        if (on[i] != '1') continue;
        st->time = ST_NOTE; st->vel = 100;
        if (hits) st->hit = (uint8_t)(i % 4u == 0u ? 1u : i % 4u == 2u ? 8u : 2u);
        else { st->n = 1; st->note[0] = (uint8_t)(note + i % 5u); }
    }
    t->p[P_SLEN] = 16;
}
static void ref_scene(int s)
{
    static const char *const ON[4] = {"1000100010001010", "1001001000100100", "1000000010000000", "0010010001000110"};
    static const uint8_t PAT[4] = {0, 1, 0, 3};
    uint32_t k;
    song.playing = 0;
    set_engine_of(&trk[0], ENGI_DRUM); apply_preset_to(&trk[0], preset_named(ENGI_DRUM, "909 KIT"));
    set_engine_of(&trk[1], ENGI_PROPHET); apply_preset_to(&trk[1], 33);           /* Pickle Pincher */
    set_engine_of(&trk[2], ENGI_CZ); apply_preset_to(&trk[2], preset_named(ENGI_CZ, "ELEC.PIANO"));
    set_engine_of(&trk[3], ENGI_FM6); apply_preset_to(&trk[3], preset_named(ENGI_FM6, "BRASS SECT"));
    for (k = 0; k < NTRK; k++) {
        trk[k].engine = trk[k].eng_req;
        if (PAT[k]) pattern_switch(&trk[k], PAT[k]);
        ref_steps(&trk[k], ON[k], 45u + 12u * k, k == 0u);
        trk[k].seq_idx = 5;
        trk[k].peak = 9000 + 3000 * (int32_t)k;
    }
    p5_patch_of(&trk[1])->raw[P5_CUTOFF] = 79; p5_patch_of(&trk[1])->raw[P5_RESONANCE] = 38;
    p5_patch_of(&trk[1])->raw[P5_ENV_FILTER] = 105; p5_patch_of(&trk[1])->raw[P5_RELEASE_AMP] = 40;
    song.playing = 1; song.g[G_BPM] = 124; song.sel = 1; trk[1].pattern_next = 2;   /* (P3 waiting for the bar) */
    song.batt_raw = 600; usb.config = 0;               /* (the mockups show no battery) */
    for (k = 0; k < SCOPE_N; k++) {                     /* the mockups' waveform: three partials (let go: quieter) */
        double a = k * 2.0 * 3.14159265 / 75.0, g = s == S_REF_STAGE_RELEASED ? 0.04 : 1.0;
        scope_buf[k] = (int16_t)(g * 12000.0 * (0.55 * sin(a) + 0.25 * sin(a * 2.0 + 0.6) + 0.15 * sin(a * 3.05)));
    }
    scope_w = 0;
    switch (s) {
    case S_REF_STAGE_HELD: case S_REF_STAGE_RELEASED: case S_REF_STAGE_CUTOFF:
        go_home();
        input_on(TSEL, 57, 100); input_on(TSEL, 60, 100); input_on(TSEL, 64, 100); input_on(TSEL, 67, 100);
        if (s == S_REF_STAGE_RELEASED) { input_off(TSEL, 57); input_off(TSEL, 60); input_off(TSEL, 64); input_off(TSEL, 67); }
        if (s == S_REF_STAGE_CUTOFF) { ui.hot_col = 0; ui.hot_t = 30; stage_out(); }
        break;
    case S_REF_STAGE_DRUM:
        song.sel = 0; go_home(); drum_flash[0] = 2u | 8u; break;
    case S_REF_BROWSER: {
        uint32_t i;
        for (i = 0; i < NELEM(CAT_ORDER) && CAT_ORDER[i] != CAT_BASS; i++) {}
        list_set(LM_CAT + i); go_page(GR_BROWSE);
        {   /* (a fast turn through it: x4, its 12th sound pending, as the mockup's) */
            uint32_t total;
            list_cur(&total);
            brw.on = 1; brw.trk = song.sel; brw.mode = (uint8_t)list_mode(); brw.n = (uint16_t)(total > 11u ? 11u : 0u);
            brw.t = fm1_ms; brw.x = 4;
        }
        break;
    }
    case S_REF_PATTERNS:                             /* the mock's: T1 1..3 (1 playing), T2 1, 2 playing, 3 waiting, 4;
                                                      * T3 1 playing, 2; T4 1, 4 playing; the song A A B B C A on B */
        song.playing = 0;
        pattern_switch(&trk[0], 1); ref_steps(&trk[0], ON[0], 0, 1); pattern_switch(&trk[0], 2); ref_steps(&trk[0], ON[0], 0, 1);
        pattern_switch(&trk[0], 0);
        pattern_switch(&trk[1], 0); ref_steps(&trk[1], ON[1], 57, 0); pattern_switch(&trk[1], 3); ref_steps(&trk[1], ON[1], 57, 0);
        pattern_switch(&trk[1], 2); ref_steps(&trk[1], ON[3], 57, 0);
        pattern_switch(&trk[1], 1);
        pattern_switch(&trk[2], 1); ref_steps(&trk[2], ON[2], 69, 0); pattern_switch(&trk[2], 0);
        pattern_switch(&trk[3], 0); ref_steps(&trk[3], ON[3], 81, 0); pattern_switch(&trk[3], 3);
        song.playing = 1; trk[1].pattern_next = 2; fm1_ms = 0;
        chain_config.count = 6;
        for (k = 0; k < 6u; k++) {
            static const uint8_t ROW[6] = {0, 0, 1, 1, 2, 0};
            chain_config.row[k] = (chain_row_t){ROW[k], 1};
            memset(chain_patterns[k], ROW[k], NTRK);
        }
        chain.running = 1; chain.row = 2;
        go_page(GR_PATGRID);
        break;
    case S_REF_SONG: {                               /* the mock's: four sections, B twice; T1 1 2 2 3, T2 1 2 2 4,
                                                      * T3 - 1 2 1, T4 - - 3 2 (-: pattern 6, empty); B's second time
                                                      * playing, a quarter in; KNOB 1 on D */
        static const uint8_t SP[4][NTRK] = {{0, 0, 5, 5}, {1, 1, 0, 5}, {1, 1, 1, 2}, {2, 3, 0, 1}};
        uint32_t b;
        song.playing = 0;
        for (k = 0; k < NTRK; k++) {
            for (b = 0; b < 4u; b++) { pattern_switch(&trk[k], b); ref_steps(&trk[k], ON[(k + b) % 4u], 45u + 12u * k, k == 0u); }
            pattern_switch(&trk[k], 0);
        }
        project_save(0); ui.msg_t = 0;
        chain_config.count = 4;
        for (k = 0; k < 4u; k++) {
            chain_config.row[k] = (chain_row_t){SP[k][0], (uint8_t)(k == 1u ? 2u : 1u)};
            memcpy(chain_patterns[k], SP[k], NTRK);
        }
        ui.song_row = 3; go_page(GR_SONG);
        song.playing = 1; chain.running = 1; chain.row = 1; chain.remaining = 1; trk[0].seq_idx = 4;
        break;
    }
    case S_REF_ENV: case S_REF_LFO: case S_REF_EDIT_OSC: case S_REF_FX: case S_REF_DLY: case S_REF_NOTES:
        /* (R5) track 2: ANALOG ACID; ENV: SUSTAIN just turned, LFO: RATE, DLY: FEEDBACK */
        set_engine_of(&trk[1], 0); apply_preset_to(&trk[1], preset_named(0, "ACID")); trk[1].engine = 0;
        song.playing = 0; trk[1].pattern_next = 0xFF; trk[1].p[P_LD_FLT] = 30;
        if (s == S_REF_EDIT_OSC) { trk[1].p[P_E1] = 0; trk[1].p[P_E2] = 0; trk[1].p[P_E3] = 0; ui.home = 0; ui.page = (uint8_t)page_first(FAM_EDIT); page_entered(); }
        else if (s != S_REF_NOTES) go_title(s == S_REF_ENV ? "ENV" : s == S_REF_LFO ? "LFO" : s == S_REF_FX ? "FX" : "DLY");
        if (s == S_REF_DLY) { song.g[G_DTIME] = 1; song.g[G_DFDBK] = 47; song.g[G_DCOLOR] = 51; song.g[G_DMIX] = 85; }
        if (s == S_REF_NOTES) {                      /* (the step at the cursor its note, PITCH just turned; playing) */
            static const uint8_t N[8][3] = {{0, 51, 2}, {2, 51, 1}, {3, 54, 1}, {4, 56, 3}, {8, 51, 2}, {10, 54, 1},
                                            {12, 56, 2}, {14, 58, 2}};   /* the mock's: step, note, length */
            uint32_t i, j;
            ref_steps(&trk[1], "0000000000000000", 0, 0);
            for (i = 0; i < 8u; i++) {
                step_t *st = &trk[1].step[N[i][0]];
                st->time = ST_NOTE; st->vel = 100; st->n = 1; st->note[0] = N[i][1];
                for (j = 1; j < N[i][2]; j++) trk[1].step[N[i][0] + j].time = ST_TIE;
            }
            go_title("NOTES"); cursor_set(7); song.playing = 1; trk[1].seq_idx = 5;
            ui.hot_col = 1; ui.hot_t = 30;
        }
        if (s != S_REF_EDIT_OSC && s != S_REF_FX && s != S_REF_NOTES) { ui.hot_col = s == S_REF_ENV ? 2 : s == S_REF_LFO ? 0 : 1; ui.hot_t = 30; }
        break;
    case S_REF_SECTIONS_P5: case S_REF_MAP_P5:       /* (R7) Prophet's FILTER (CUTOFF just turned), its map */
        song.playing = 0; go_title("P5 FILTER");
        if (s == S_REF_MAP_P5) smap_open();
        else { ui.hot_col = 0; ui.hot_t = 30; }
        break;
    case S_REF_SCL_LIST:                             /* (R6) SCL as a list: D# minor, the Scale row */
        song.playing = 0; trk[1].p[P_ROOT] = 3; trk[1].p[P_SCALE] = 2;
        go_title("SCL"); lst.page = ui.page; lst.row = 1; lst.first = 0;
        break;
    case S_REF_MAP_FM6:                              /* (R7) FM6's map from OP3 EG RATE */
        set_engine_of(&trk[1], ENGI_FM6); trk[1].engine = ENGI_FM6; song.playing = 0;
        fm6_opsel = 2; go_title("EG RATE"); smap_open();
        break;
    case S_REF_SHEET_SOUND:                          /* (R6) the sound's sheet over its first EDIT page */
        song.playing = 0; ui.home = 0; ui.page = (uint8_t)page_first(FAM_EDIT); page_entered(); page_sheet_open();
        break;
    case S_REF_SHEET_PROJECT:                        /* (R6) PROJECT: A's sheet, Save here */
        song.playing = 0; project_save_as(0, "NIGHT DRIVE"); project_save_as(2, "SKETCH 4"); template_save();
        settings_boot = 1; song.g[G_SLOT] = 1; ui.frame++; go_title("PROJECT"); slot_enter(); pop.sel = 1;
        ui.msg_t = 0;
        break;
    case S_REF_SHEET_NOTE:                           /* (R6) NOTES: the note at step 5 (G#3, 3 steps), its sheet */
        ref_scene(S_REF_NOTES); song.playing = 0; ui.hot_t = 0; cursor_set(4); step_enter(); pop.sel = 1;
        break;
    case S_REF_SHEET_HIT: case S_REF_GRID: {         /* (R6) the drum grid: SD at step 5 (accent, 75 %), its sheet */
        static const char *const G[4] = {"1000100010001000", "0000100000001000", "0000000000000001", "1010101010101010"};
        uint32_t l, i;
        song.playing = 0; song.sel = 3; set_engine_of(&trk[3], ENGI_DRUM); trk[3].engine = ENGI_DRUM;
        track_defaults_steps(&trk[3]); trk[3].p[P_SLEN] = 32;
        for (l = 0; l < 4u; l++)
            for (i = 0; i < 16u; i++)
                if (G[l][i] == '1') grid_hit(&trk[3], i, l, 1);
        grid_acc(&trk[3], 4, 1, 1);
        step_set_chance(&trk[3].step[4], 75);
        go_title("NOTES"); ui.lane = 1; cursor_set(4);
        if (s == S_REF_SHEET_HIT) { step_enter(); pop.sel = 1; }
        else { ui.hot_col = 1; ui.hot_t = 30; song.playing = 1; trk[3].seq_idx = 6; }
        break;
    }
    case S_REF_MOTION: case S_REF_SHEET_MOTION: {    /* (R6) MOTION: ACID's CUTOFF RESO DRIVE moved; the playhead at 5 */
        static const uint8_t CUT[16] = {60, 60, 70, 70, 80, 80, 90, 95, 95, 90, 85, 80, 85, 90, 80, 70};
        static const uint8_t RES[16] = {30, 30, 30, 40, 40, 50, 50, 40, 40, 40, 30, 30, 40, 40, 30, 30};
        static const uint8_t DRV[16] = {10, 10, 20, 20, 30, 40, 40, 30, 40, 50, 50, 40, 30, 30, 20, 20};
        uint32_t i;
        set_engine_of(&trk[1], 0); apply_preset_to(&trk[1], preset_named(0, "ACID")); trk[1].engine = 0;
        song.playing = 0; trk[1].pattern_next = 0xFF; trk[1].p[P_SLEN] = 16;
        motion_clear(&trk[1]);
        for (i = 0; i < 16u; i++) {
            motion_set_event(&trk[1], i, P_E0 + 4u, CUT[i]);
            motion_set_event(&trk[1], i, P_E0 + 5u, RES[i]);
            motion_set_event(&trk[1], i, P_E0 + 6u, DRV[i]);
        }
        go_title("MOTION"); mo.lane = 0;
        if (s == S_REF_SHEET_MOTION) { motion_enter(); pop.sel = 1; }
        else { song.playing = 1; trk[1].seq_idx = 4; }
        break;
    }
    case S_REF_MOD: case S_REF_PICKER_MOD:           /* (R6) MOD: LFO > Cutoff, Mod wheel > Mix (picked), Velocity > Level */
        set_engine_of(&trk[1], 0); apply_preset_to(&trk[1], preset_named(0, "ACID")); trk[1].engine = 0;
        song.playing = 0; trk[1].pattern_next = 0xFF;
        trk[1].p[P_M1SRC] = MS_LFO; trk[1].p[P_M1DST] = MD_CUT; trk[1].p[P_M1AMT] = 40;
        trk[1].p[P_M2SRC] = MS_MODW; trk[1].p[P_M2DST] = MD_E1 + 2; trk[1].p[P_M2AMT] = -64;
        trk[1].p[P_M3SRC] = MS_VEL; trk[1].p[P_M3DST] = MD_AMP; trk[1].p[P_M3AMT] = 19;
        mod_ui_slot = 1; go_title("MOD");
        if (s == S_REF_PICKER_MOD) pick_touch(0);
        else { ui.hot_col = 3; ui.hot_t = 30; }
        break;
    case S_REF_ARP: case S_REF_PATTERN: case S_REF_SLICER: {   /* (R6) ARP Up 2 oct; PATTERN 32 steps; SLICER Gate */
        uint32_t i;
        set_engine_of(&trk[1], 0); apply_preset_to(&trk[1], preset_named(0, "ACID")); trk[1].engine = 0;
        song.playing = 0; trk[1].pattern_next = 0xFF;
        if (s == S_REF_ARP) {
            trk[1].p[P_AMODE] = 1; trk[1].p[P_AOCT] = 2; go_title("ARP"); ui.hot_col = 0;
        } else if (s == S_REF_PATTERN) {
            track_defaults_steps(&trk[1]); trk[1].p[P_SLEN] = 32; trk[1].p[P_SSWING] = 0;
            for (i = 0; i < 32u; i++)
                if ((0x8A2A4A2Bu >> i) & 1u) { trk[1].step[i].time = ST_NOTE; trk[1].step[i].n = 1; trk[1].step[i].note[0] = 57; }
            song.playing = 1; trk[1].seq_idx = 6; go_title("PATTERN"); ui.hot_col = 0;
        } else {
            trk[1].p[P_SLCR] = SL_GATE; trk[1].p[P_SLPAT] = 1; trk[1].p[P_SLDEPTH] = 127; go_title("SLICER"); ui.hot_col = 3;
        }
        ui.hot_t = 30;
        break;
    }
    case S_REF_FMEG: {                               /* (R6) FM6 Tine EP, OP3's EG: R 95 20 20 50, R3 turning */
        static const uint8_t R[4] = {95, 20, 20, 50}, L[4] = {99, 74, 58, 0};
        uint8_t *op;
        uint32_t i;
        set_engine_of(&trk[1], ENGI_FM6); apply_preset_to(&trk[1], preset_named(ENGI_FM6, "TINE EP")); trk[1].engine = ENGI_FM6;
        song.playing = 0; trk[1].pattern_next = 0xFF; fm6_opsel = 2;
        op = &fm6_patch[1][(5u - 2u) * FP_OP];
        for (i = 0; i < 4u; i++) { op[FP_R1 + i] = R[i]; op[FP_L1 + i] = L[i]; }
        fm6_pgen[1]++;
        go_title("EG RATE"); ui.hot_col = 2; ui.hot_t = 30;
        break;
    }
    case S_REF_SCALES:                               /* (R6) SCALES: EDO, 53-tone equal (a favourite) */
        song.playing = 0; song.sel = 1; go_page(GR_SCALE_PICKER); trk[1].p[P_SCALE] = 39; ui.scale_family = 2;
        scale_favorite_set(39, 1);
        break;
    case S_REF_SHEET_SONG:                           /* (R6) the song's sheet over SONG */
        ref_scene(S_REF_SONG); page_sheet_open();
        break;
    case S_REF_PICKER_WAVE:                          /* (R6) ANALOG's WAVE turning: its picker */
        set_engine_of(&trk[1], 0); apply_preset_to(&trk[1], preset_named(0, "ACID")); trk[1].engine = 0;
        song.playing = 0; trk[1].pattern_next = 0xFF;
        ui.home = 0; ui.page = (uint8_t)page_first(FAM_EDIT); page_entered(); pick_touch(0);
        break;
    case S_REF_DIALOG:                               /* (R5) PROJECT: overwrite A? */
        song.playing = 0; go_title("PROJECT"); ui.confirm = CF_OVR_PROJ; ui.confirm_trk = 0;
        break;
    case S_REF_SETTINGS:                             /* (R5) the menu, COLOR picked */
        song.playing = 0; ui.menu = 1; ui.menu_sel = 0;
        break;
    case S_REF_MIXER: {                              /* (R5) the mock's levels, pans, sends; 3 muted, 1 and 4 armed */
        static const int16_t LV[4] = {91, 70, 44, 112}, PN[4] = {0, -20, 0, 13}, RV[4] = {51, 15, 0, 20};
        for (k = 0; k < NTRK; k++) { trk[k].p[P_LEVEL] = LV[k]; trk[k].p[P_PAN] = PN[k]; trk[k].p[P_REV] = RV[k]; }
        trk[2].p[P_MUTE] = 1; song.rec = 1u | 8u; song.playing = 0;
        go_title("MIXER");
        break;
    }
    default: break;
    }
}

static void setup(int s)
{
    memset(kb_chn, 0, sizeof kb_chn);               /* no key held (roll_playing holds one) */
    memset(&stage, 0, sizeof stage);                /* (Stage's knobs in) */
    chain.running = 0;                              /* (the song the SONG scene plays) */
    if (s >= S_MOCK_HOME && s <= S_MOCK_MENU) {
        mock_state(s);
        return;
    }
    state();
    if (s >= S_REF_STAGE_HELD && s <= S_REF_SCALES) {
        ref_scene(s);
        return;
    }
    switch (s) {
    case S_HOME: song.octave = 2; song.rec = 1; usb.config = 1; break;
    case S_HOME_IDLE: song.playing = 0; song.batt_raw = 570; ui.hot_col = 1; ui.hot_t = 30; break;
    case S_HOME_NOTE: input_on(TSEL, 61, 100); break;
    case S_HOME_FM6: eng(ENGI_FM6); /* fall through */
    case S_HOME_CHORD:
    case S_HOME_RELEASED:
        input_on(TSEL, 57, 100); input_on(TSEL, 60, 100); input_on(TSEL, 64, 100); input_on(TSEL, 67, 100);
        if (s == S_HOME_RELEASED) {
            input_off(TSEL, 57); input_off(TSEL, 60); input_off(TSEL, 64); input_off(TSEL, 67);
        }
        break;
    case S_HOME_INVERSION:
        input_on(TSEL, 65, 100); input_on(TSEL, 68, 100); input_on(TSEL, 73, 100); break; /* C#/F */
    case S_HOME_WIDE: {
        static const uint8_t notes[] = {0, 1, 13, 25, 61, 97, 126, 127};
        uint32_t i;
        for (i = 0; i < sizeof notes; i++) input_on(TSEL, notes[i], 100);
        break;
    }
    case S_MESSAGE: ui_say("LOADED ", "07 A VERY LONG PATTERN NAME"); break;
    case S_MESSAGE_KEY: ui_message("[SAVE] HOLD TO UNDO"); break;            /* a message with a keycap */
    case S_PRESETS: favorite_set(0, 4, 1); favorite_set(0, 5, 1); go_page(GR_BROWSE); break;
    case S_PRESETS_NOFAV: favorites.filter = 1; go_page(GR_BROWSE); break;
    case S_PRESETS_CAT: list_set(LM_CAT + 5u); eng(ENGI_PROPHET); go_page(GR_BROWSE); break;   /* STRING */
    case S_PRESETS_PENDING: {                                               /* a fast turn, still moving */
        uint32_t total;
        eng(ENGI_CZ); go_page(GR_BROWSE); brw.on = 1; brw.trk = song.sel; brw.mode = (uint8_t)list_mode();
        brw.n = (uint16_t)(list_cur(&total) + 9u); brw.t = fm1_ms; break;
    }
    case S_PRESETS_RECENT: list_set(LM_RECENT); go_page(GR_BROWSE); break;
    case S_USER: song.playing = 0; up_store(3, "MY LONG BASS NAME"); up_store(4, "PAD"); ui.uslot = 3; go_page(GR_USER); break;
    case S_PROJECT: song.playing = 0; project_save(1); song.g[G_SLOT] = 2; go_page(GR_SLOTS); ui.act = 4; break;
    case S_TEMPO: go_title("TEMPO"); song.g[G_SWING] = 12; break;
    case S_PROJECT_BOOT:                                 /* A, B saved, the template; BOOT B, SLOT TMPL, KNOB 2 turned */
        song.playing = 0; project_save(0); project_save_as(1, "LIVE SET"); template_save(); settings_boot = 2;
        song.g[G_SLOT] = PROJ_TMPL; ui.frame++; go_page(GR_SLOTS); ui.act = 3; ui.hot_col = 1; ui.hot_t = 30;
        break;
    case S_SONG_EMPTY: song.playing = 0; go_page(GR_SONG); break;
    case S_SONG:
        song.playing = 0; project_save(0); project_save(1);
        chain_config.count = 3;
        chain_config.row[0] = (chain_row_t){0, 2}; chain_config.row[1] = (chain_row_t){1, 4}; chain_config.row[2] = (chain_row_t){2, 1};
        memset(chain_patterns[1], 1, NTRK); memset(chain_patterns[2], 2, NTRK); ui.song_row = 1; go_page(GR_SONG); chain_prepare(); events_block(32);
        break;
    case S_NOTES_SLIDE:
    case S_NOTES_MIXED:
    case S_NOTES_CHORD:
    case S_NOTES_EMPTY:
    case S_NOTES_RAW:
    case S_NOTES_ZOOM:
    case S_NOTES_LOOP:
    case S_NOTES_DRUM:
    case S_NOTES_DENSE:
    case S_NOTES_REC:
    case S_NOTES_ERASE:
    case S_NOTES_DRUM_REC:
    case S_NOTES_DRUM_ERASE: {
        song.sel=0; song.rec=0; song.playing=0;
        track_t *t=TSEL;
        if(s==S_NOTES_DRUM||s==S_NOTES_DRUM_REC||s==S_NOTES_DRUM_ERASE){set_engine_of(t,ENGI_DRUM);t->engine=t->eng_req;}
        recording_reset();track_defaults_steps(t);t->p[P_SLEN]=16;t->p[P_SDIV]=2;t->p[P_SSWING]=24;
        for(uint32_t i=0;i<NSTEP;i++)step_clear(&t->step[i]);
        uint32_t count=s==S_NOTES_EMPTY||s==S_NOTES_CHORD||s==S_NOTES_SLIDE?0:s==S_NOTES_DENSE?RECORD_MAX:5;
        for(uint32_t i=0;i<count;i++) {
            static const uint16_t on[]={8192,24576,49152,16384,60000};
            static const uint8_t step[]={0,0,64,1,15|64|128}, note[]={60,64,60,67,72};
            uint32_t start=count==RECORD_MAX?(i+1u)*60u:on[i];
            uint8_t pos=count==RECORD_MAX?(start>=32768u?64u:0u):step[i];
            recording[i]=(recorded_note_t){(uint16_t)start,(uint16_t)(count==RECORD_MAX?96:32768),
                (uint8_t)(s==S_NOTES_DRUM?(i%2u?38:36):count==RECORD_MAX?60:note[i]),(uint8_t)(90+i%38u),(uint8_t)recording_owner(t),pos};
            uint32_t view=recording_view(t,&recording[i]);t->step[view].time=ST_NOTE;t->step[view].flags=SF_RECORDED;
        }
        recording_reindex();
        for(uint32_t i=0;i<NSTEP;i++)if(t->step[i].flags&SF_RECORDED)notes_rebuild(t,i);
        if(s==S_NOTES_MIXED||s==S_NOTES_CHORD||s==S_NOTES_SLIDE) {
            t->step[4].time=ST_NOTE;t->step[4].n=3;t->step[4].note[0]=60;t->step[4].note[1]=64;t->step[4].note[2]=67;t->step[4].vel=96;
            step_note_resize(t,4,3);
        }
        go_page(GR_ROLL);cursor_set(s==S_NOTES_LOOP?15:s==S_NOTES_CHORD||s==S_NOTES_SLIDE?4:0);
        if(s==S_NOTES_CHORD)ui.note_slot=1;
        if(s==S_NOTES_SLIDE){ui.step_mods=1u<<panel.btn[B_ENV];t->step[4].flags|=SF_SLIDE;}
        ui.note_zoom=s==S_NOTES_RAW||s==S_NOTES_EMPTY||s==S_NOTES_CHORD||s==S_NOTES_MIXED||s==S_NOTES_SLIDE?0:s==S_NOTES_LOOP?2:4;
        notes_selected(t);if(s==S_NOTES_DENSE)ui.note_pick=RECORD_MAX;else if(s==S_NOTES_ZOOM)notes_cycle(2);
        if(s==S_NOTES_REC||s==S_NOTES_ERASE||s==S_NOTES_DRUM_REC||s==S_NOTES_DRUM_ERASE) {
            song.playing=1;song.rec=1;t->seq_idx=1;t->seq_pos=600;
            if(s==S_NOTES_ERASE||s==S_NOTES_DRUM_ERASE) {
                fm1_in.buttons=1u<<panel.btn[B_EDIT];
                seq_erase_button=(uint16_t)fm1_in.buttons;
                seq_erase_generation=t->pattern_gen;
                seq_erase_request=0x80000000u|((uint32_t)seq_erase_button<<16)|recording_owner(t);
                ui.step_mods=ui.step_used=seq_erase_button;
            }
        }
        break;
    }
    case S_STEP: song.rec = 1; go_page(GR_ROLL); ui.cursor = 6; break;
    /* Stage: the drum track selected, three lanes just hit; CZ-1's and PROPHET's own knobs (CZ-1's KNOB 3 just
     * turned); track 2 waiting for pattern 3 (blinking: drawn lit), track 3 muted, track 2 armed; browsing (a sound
     * shown, not loaded); stopped with notes let go */
    /* NEW SONG: PROJECT with NEW picked (KNOB 3: OCT+); the KEY screen (KNOB 3 TEMPO just turned); ROLES */
    case S_PROJECT_NEW: song.playing = 0; go_page(GR_SLOTS); song.g[G_SLOT] = PROJ_TMPL; ui.proj_new = 1; ui.act = 3; break;
    case S_NEW_KEY:
    case S_NEW_ROLES:
        song.playing = 0; new_open(); nw.root = 9; nw.scale = 2; nw.bpm = 96;
        if (s == S_NEW_ROLES) { nw.on = 2; nw.role[0] = NR_DRUMS; nw.role[1] = NR_BASS; nw.role[2] = NR_CHORDS; }
        else { ui.hot_col = 2; ui.hot_t = 30; }
        break;
    case S_STAGE_DRUM:
        drum(0); go_home(); trk[3].seq_idx = 9; drum_flash[3] = 1u | 8u | 32u; break;
    case S_STAGE_CZ:
        eng(ENGI_CZ); apply_preset(3); go_home(); ui.hot_col = 2; ui.hot_t = 30; stage_out();
        input_on(TSEL, 48, 100); input_on(TSEL, 55, 100); input_on(TSEL, 64, 100); break;
    case S_STAGE_P5:
        eng(ENGI_PROPHET); go_home(); input_on(TSEL, 62, 100); input_on(TSEL, 65, 100); input_on(TSEL, 69, 100);
        input_on(TSEL, 72, 100); ui.hot_col = 0; ui.hot_t = 30; stage_out(); break;   /* (CUTOFF turning: the filter's curve) */
    case S_STAGE_FILTER:                                 /* ANALOG's RES turning, a chord held: the curve behind it */
        go_home(); TSEL->p[P_E5] = 100; ui.hot_col = 1; ui.hot_t = 30; stage_out();
        input_on(TSEL, 57, 100); input_on(TSEL, 60, 100); input_on(TSEL, 64, 100); break;
    case S_STAGE_ENV:                                    /* ANALOG's REL turning: the envelope */
        go_home(); TSEL->p[P_ATK] = 20; TSEL->p[P_DEC] = 50; TSEL->p[P_SUS] = 80; TSEL->p[P_REL] = 90;
        ui.hot_col = 3; ui.hot_t = 30; stage_out(); break;
    case S_STAGE_QUEUED:
        go_home(); trk[1].pattern_next = 2; trk[2].p[P_MUTE] = 1; song.rec = 2u; fm1_ms = 0; break;
    case S_STAGE_BROWSE:
        go_home(); brw.on = 1; brw.trk = song.sel; brw.mode = (uint8_t)list_mode(); brw.n = 9; brw.t = fm1_ms; break;
    case S_STAGE_STOPPED:
        song.playing = 0; go_home(); input_on(TSEL, 60, 100); input_on(TSEL, 63, 100); input_on(TSEL, 67, 100);
        input_on(TSEL, 70, 100); input_off(TSEL, 60); input_off(TSEL, 63); input_off(TSEL, 67); input_off(TSEL, 70); break;
    /* SEQ > PATTERNS: patterns on every track (track 1: 1..3, track 2: 1 and 5, track 4: 1), track 2 waiting for 5,
     * track 3 muted; stopped: KNOB 2 just turned */
    case S_PATGRID:
    case S_PATGRID_STOPPED:
    case S_PATGRID_SHEET:                                /* stopped, track 1's pattern: its sheet; Copy to (to 4, */
    case S_PATGRID_COPY:                                 /* empty); over 2 (in use): the question; Delete pattern */
    case S_PATGRID_REPLACE:
    case S_PATGRID_DELETE: {
        uint32_t b;
        song.playing = 0;
        for (b = 1; b < 3u; b++) { pattern_switch(&trk[0], b); my_steps(&trk[0]); }
        pattern_switch(&trk[0], 0);
        pattern_switch(&trk[1], 4); my_steps(&trk[1]); pattern_switch(&trk[1], 0);
        song.playing = s == S_PATGRID;
        if (s == S_PATGRID) trk[1].pattern_next = 4;
        else { ui.hot_col = 1; ui.hot_t = 30; }
        trk[2].p[P_MUTE] = 1; fm1_ms = 0;
        if (s == S_PATGRID) {                            /* a song of four rows, row 2 playing */
            chain_config.count = 4;
            chain_config.row[0] = (chain_row_t){0, 2}; chain_config.row[1] = (chain_row_t){1, 4};
            chain_config.row[2] = (chain_row_t){0, 1}; chain_config.row[3] = (chain_row_t){2, 16};
            memset(chain_patterns[1], 1, NTRK); chain_patterns[1][3] = 0; memset(chain_patterns[3], 2, NTRK);
            chain.running = 1; chain.row = 1;
        } else {                                         /* no song: the jam's rows */
            jam.n = 2; jam.pat[0][0] = 0; jam.rep[0] = 4; jam.pat[1][0] = 1; jam.pat[1][1] = 4; jam.rep[1] = 2;
        }
        go_page(GR_PATGRID);
        if (s >= S_PATGRID_SHEET)                        /* (track 1's pattern 1: something to copy, to delete) */
            my_steps(&trk[0]);
        if (s == S_PATGRID_SHEET) {
            ui.hot_t = 0; page_sheet_open();
        } else if (s == S_PATGRID_COPY || s == S_PATGRID_REPLACE) {
            ui.hot_t = 0; ptc_start();
            if (s == S_PATGRID_REPLACE) { ui.ptc_dst = 1; confirm_open(CF_PASTE_PAT, song.sel); }
        } else if (s == S_PATGRID_DELETE) {
            ui.hot_t = 0; confirm_open(CF_DEL_PAT, song.sel);
        }
        break;
    }
    case S_PATTERN: go_title("PATTERN"); ui.cursor = 3; break;
    case S_MOTION: go_page(GR_MOTION); break;
    case S_DRUM: drum(0); go_page(GR_ROLL); ui.cursor = 4; ui.lane = 1; trk[3].seq_idx = 9; break;
    case S_MIXER:
        go_page(GR_TRK);
        trk[0].p[P_LEVEL] = 100; trk[1].p[P_LEVEL] = 84; trk[2].p[P_LEVEL] = 64; trk[3].p[P_LEVEL] = 110;
        trk[0].p[P_PAN] = -20; trk[1].p[P_PAN] = 24; trk[3].p[P_PAN] = -4; trk[1].p[P_REV] = 80;
        trk[2].p[P_MUTE] = 1; trk[3].p[P_MUTE] = 1; song.rec = 2u | 8u; trk[0].peak = 9000;
        break;
    case S_MIXER_PAN:                            /* PAN just turned, the ends of each knob, OFF, armed while stopped */
        go_page(GR_TRK); song.playing = 0; song.sel = 1;
        trk[0].p[P_LEVEL] = 127; trk[1].p[P_LEVEL] = 0; trk[2].p[P_LEVEL] = 1; trk[3].p[P_LEVEL] = 104;
        trk[0].p[P_PAN] = -64; trk[1].p[P_PAN] = 63; trk[2].p[P_PAN] = 0; trk[3].p[P_PAN] = 1;
        trk[0].p[P_REV] = 127; trk[2].p[P_REV] = 1; trk[3].p[P_MUTE] = 1;
        song.rec = 1u | 8u; ui.hot_col = 3; ui.hot_t = 30;
        break;
    case S_ENV: go_title("ENV"); ui.hot_col = 2; ui.hot_t = 30; break;
    case S_ENVDEST: go_title("ENV DEST"); break;
    case S_LFO: go_title("LFO"); break;
    case S_MOD: TSEL->p[P_M1SRC] = 1; TSEL->p[P_M1DST] = 2; TSEL->p[P_M1AMT] = 40; TSEL->p[P_M2SRC] = 6;
        TSEL->p[P_M2DST] = 14; TSEL->p[P_M2AMT] = -64; mod_ui_slot = 1; go_title("MOD"); break;
    case S_FX: go_title("FX"); break;
    case S_SLICER: TSEL->p[P_SLCR] = 1; go_title("SLICER"); break;
    case S_DLY: go_title("DLY"); break;
    case S_SCL: TSEL->p[P_SCALE] = 2; go_title("SCL"); break;
    case S_SCALE_PICKER_EDO:
    case S_SCALE_PICKER_HIST:
    case S_SCALE_PICKER_FAV:
    case S_SCALE_PICKER_EMPTY:
        go_page(GR_SCALE_PICKER); TSEL->p[P_QUANT] = Q_ALL; TSEL->p[P_ROOT] = 6;
        TSEL->p[P_SCALE] = s == S_SCALE_PICKER_HIST ? 50 : 39;
        ui.scale_family = s == S_SCALE_PICKER_HIST ? 5 : 2;
        if (s == S_SCALE_PICKER_FAV || s == S_SCALE_PICKER_EMPTY) {
            ui.scale_family = SCALE_FAMILIES + 1u;
            if (s == S_SCALE_PICKER_FAV) { scale_favorite_set(39, 1); scale_favorite_set(50, 1); scale_favorite_set(63, 1); }
        }
        break;
    case S_SCALE_SETTINGS_FAV:
        go_title("SCL"); TSEL->p[P_SCALE] = 50; TSEL->p[P_ROOT] = 6; TSEL->p[P_QUANT] = Q_ALL;
        scale_favorite_set(50, 1); break;
    case S_SCL_MICRO:
        for (uint32_t i = 0; i < SCALE_TOTAL; i++) if (!strcmp(N_SCALE[i], "53EDO")) TSEL->p[P_SCALE] = (int16_t)i;
        TSEL->p[P_QUANT] = Q_ALL; go_title("SCL"); break;
    case S_SCL_MICRO_LAYER:
        go_home(); ui.layer = LAYER_SCL; TSEL->p[P_SCALE] = SCALE_TOTAL - 1; break;
    case S_SCL_MICRO_CHORD:
        for (uint32_t i = 0; i < SCALE_TOTAL; i++) if (!strcmp(N_SCALE[i], "24EDO")) TSEL->p[P_SCALE] = (int16_t)i;
        TSEL->p[P_QUANT] = Q_ALL; TSEL->p[P_CHRD] = CH_DIA7; TSEL->p[P_VOICE] = V_POLY; go_title("CHORD"); break;
    case S_CHORD:                                    /* A minor DIA7, the last chord on B: Bm7b5 */
    case S_CHORD_WIDE: {                             /* A harmonic minor DIA7 +OCT on G#: G#dim7 over three octaves */
        uint8_t out[CHORD_MAX];
        TSEL->p[P_VOICE] = V_POLY; TSEL->p[P_ROOT] = 9; TSEL->p[P_SCALE] = s == S_CHORD ? 2 : 7;
        TSEL->p[P_CHRD] = CH_DIA7; TSEL->p[P_VOIC] = s == S_CHORD ? VC_CLOSE : VC_BASS;
        chord_build(TSEL, s == S_CHORD ? 71u : 68u, out);
        go_title("CHORD"); ui.hot_col = 0; ui.hot_t = 30;
        break;
    }
    case S_CHORD_OFF: go_title("CHORD"); break;
    case S_CHORD_KIT: eng(ENGI_DRUM); TSEL->p[P_CHRD] = CH_DIA3; go_title("CHORD"); break;
    case S_ARP: go_title("ARP"); break;
    case S_VOICE: go_title("VOICE"); break;
    case S_GLOBAL: go_title("GLOBAL"); break;
    case S_SYSTEM: go_title("SYSTEM"); break;
    case S_EDIT_ANALOG: go_title("EDIT 1"); break;
    case S_EDIT_DIGITAL: eng(E_FM); go_title("EDIT 1"); break;
    case S_OP_ENV: eng(1); go_title("OP1 ENV"); break;
    case S_EDIT_WHEEL: eng(7); go_title("EDIT 1"); ui.hot_col = 1; ui.hot_t = 30; break;
    case S_EDIT_PHYS: eng(9); go_title("EDIT 1"); break;
    case S_ALG1: case S_ALG2: case S_ALG3: case S_ALG4: case S_ALG5: case S_ALG6: case S_ALG7: case S_ALG8:
        eng(1); TSEL->p[P_E0] = (int16_t)(s - S_ALG1);      /* the 8 DIGITAL charts; FB on the odd ones, IDX high .. 0 */
        TSEL->p[P_E6] = (s - S_ALG1) & 1 ? 40 : 0; TSEL->p[P_E4] = (int16_t)((S_ALG8 - s) * 18);
        go_title("EDIT 2"); break;
    case S_OP_LEVEL: eng(1); TSEL->p[P_E0] = 1; TSEL->p[P_FM4_LEVEL] = 0; go_title("OP LEVEL");   /* op 4 silent, op 3 hot */
        ui.hot_col = 2; ui.hot_t = 30; break;
    /* FM6's algorithm charts: 1 as it is; 5 with FB +3 just turned; 22 on EDIT 2, DTUN just turned (the carriers);
     * 32 with operator 6 at output level 0, MLVL just turned (no routes: nothing in ACCENT but nothing either) */
    case S_FM6_ALG1: eng(ENGI_FM6); TSEL->p[P_E0] = 1; go_title("EDIT 1"); break;
    case S_FM6_ALG5: eng(ENGI_FM6); TSEL->p[P_E0] = 5; TSEL->p[P_E1] = 3; go_title("EDIT 1"); ui.hot_col = 1; ui.hot_t = 30; break;
    case S_FM6_ALG22: eng(ENGI_FM6); TSEL->p[P_E0] = 22; TSEL->p[P_E6] = 40; go_title("EDIT 2"); ui.hot_col = 2; ui.hot_t = 30; break;
    case S_FM6_ALG32:
        eng(ENGI_FM6); TSEL->p[P_E0] = 32; fm6_patch[song.sel][FP_OL] = 0; fm6_pgen[song.sel]++;   /* (op 6: the patch's first) */
        go_title("EDIT 1"); ui.hot_col = 2; ui.hot_t = 30;
        break;
    /* FM6's own pages: operator 2's FREQ (KNOB 1 just turned), operator 3's EG RATE, the pitch EG, STORE onto B3
     * (a patch there: its name) */
    case S_FM6_FREQ: eng(ENGI_FM6); go_title("FREQ"); fm6_opsel = 1; ui.hot_col = 1; ui.hot_t = 30; break;
    case S_FM6_EG: eng(ENGI_FM6); go_title("EG RATE"); fm6_opsel = 2; break;
    case S_FM6_PEG: eng(ENGI_FM6); fm6_factory(3, fm6_buf); fm6_unpack(fm6_buf, fm6_patch[song.sel]); fm6_pgen[song.sel]++;
        go_title("PITCH EG"); break;
    case S_FM6_STORE:
        eng(ENGI_FM6); song.playing = 0; fm6_factory(5, fm6_buf); (void)native_store(ENGI_FM6, 63, song.sel, "WOOD BARS");
        go_title("STORE"); fm6_bslot = 63;
        break;
    case S_CZ1_ENV: eng(ENGI_CZ); go_title("C1 WAV R1-4"); break;   /* INIT TONE: R1 R2 live, R3 R4 dim (END 2) */
    case S_CONFIRM_SEQ: ui.confirm = CF_CLEAR_SEQ; ui.confirm_trk = 2; break;
    case S_CONFIRM_PROJ: ui.confirm = CF_OVR_PROJ; ui.confirm_trk = 0; break;
    case S_CONFIRM_USER: song.playing = 0; up_store(6, "A VERY LONG SOUND NAME"); ui.confirm = CF_OVR_USER; ui.confirm_trk = 6; break;
    case S_CONFIRM_MOTION: ui.confirm = CF_CLEAR_MOTION; ui.confirm_trk = 3; break;
    case S_CONFIRM_ERASE: song.playing = 0; up_store(6, "A VERY LONG SOUND NAME"); ui.confirm = CF_ERASE_USER; ui.confirm_trk = 6; break;
    case S_MENU: ui.menu = 1; ui.menu_sel = 0; song.rec = 1; break;
    case S_MENU_SPEAKER: ui.menu = 1; ui.menu_sel = 1; settings.lowcut = 2; break;
    case S_ABOUT: ui.menu = 2; ui.menu_scroll = 0; break;
    case S_ABOUT_REC: ui.menu = 2; ui.menu_scroll = 0; song.rec = 1; break;          /* the REC mark beside OCT- BACK */
    case S_ABOUT_CREDITS: ui.menu = 2; ui.menu_scroll = 360; break;
    case S_ABOUT_END: ui.menu = 2; ui.menu_scroll = (uint16_t)menu_scroll_max(); break;
    case S_UBOOT: ui.uboot = 3; break;
    /* the header's battery at each stock level (raw ADC: under 531, 531.., 561.., 591..) and on USB power */
    case S_BATT0: usb.config = 0; song.batt_raw = 500; go_home(); break;
    case S_BATT1: usb.config = 0; song.batt_raw = 540; go_home(); break;
    case S_BATT2: usb.config = 0; song.batt_raw = 570; go_home(); break;
    case S_BATT3: usb.config = 0; song.batt_raw = 600; go_home(); break;
    case S_BATT_USB: usb.config = 1; usb.suspended = 0; song.batt_raw = 500; go_home(); break;
    case S_MOTION_REC: song.rec = 1u << song.sel; go_page(GR_MOTION); break;    /* recording into the motion */
    case S_MOTION_OFF: song.playing = 0; go_page(GR_MOTION); ui.act = 4; break;  /* stopped, CLEAR picked */
    case S_SONG_HOME:                            /* the song playing: the disc and its row in the header */
        song.playing = 0; project_save(0); project_save(1);
        chain_config.count = 2;
        chain_config.row[0] = (chain_row_t){0, 2}; chain_config.row[1] = (chain_row_t){1, 4};
        go_page(GR_SONG); chain_prepare(); events_block(32); go_home(); ui.msg_t = 0;
        break;
    /* the FX layer's map: held alone; REPEAT 1/16 + LPF + a mute playing with the macros turned;
     * at 72 BPM: a REPEAT 1/16 waiting for its 1/16 (shown THEME) beside TAPE STOP playing, and REPEAT 1/8
     * held, too long at that tempo (it and REVERSE dimmed) */
    case S_FX_PEEK: go_title("ENV"); ui.layer = LAYER_FX; break;
    case S_FX_HELD:
        go_home(); ui.layer = LAYER_FX;
        perf_held = perf_act = PF_BIT(PF_R16) | PF_BIT(PF_LPF) | PF_BIT(PF_M1 + 1);
        perf_k[0] = -40; perf_k[1] = 25; perf_k[3] = 30; ui.hot_col = 0; ui.hot_t = 30;
        break;
    case S_FX_WAIT:
        song.playing = 1; song.g[G_BPM] = 72; ui.layer = LAYER_FX;
        perf_held = PF_BIT(PF_R16) | PF_BIT(PF_R8) | PF_BIT(PF_TAPE); perf_act = PF_BIT(PF_TAPE);
        perf_k[2] = 100;
        break;
    case S_FX_HARM:                                 /* OCT UP playing, KNOB 4 its shimmer; OCT DN held under it */
        song.playing = 1; ui.layer = LAYER_FX;
        perf_ord[PF_ODN] = ++perf_seq; perf_ord[PF_OUP] = ++perf_seq;
        perf_held = perf_act = PF_BIT(PF_OUP) | PF_BIT(PF_ODN);
        perf_k[3] = 60; ui.hot_col = 3; ui.hot_t = 30;
        break;
    case S_REVERB: go_title("REVERB"); song.g[G_RTYPE] = 1; ui.hot_col = 0; ui.hot_t = 30; break;   /* TYPE: SPRING */
    case S_MENU_CLICK: ui.menu=1;ui.menu_sel=MI_CLICK;settings_click=2;break;
    case S_MENU_CLICK_LEVEL: ui.menu=1;ui.menu_sel=MI_CLICK_LEVEL;settings_click_level=2;break;
    case S_MENU_COUNTIN: ui.menu=1;ui.menu_sel=MI_COUNTIN;settings_countin=2;break;
    case S_MENU_PREVIEW: ui.menu=1;ui.menu_sel=MI_PREVIEW;settings_preview=1;break;
    case S_DRUM_SOUND_808: case S_DRUM_SOUND_909: case S_DRUM_MIX_909: case S_DRUM_HIT_909: case S_DRUM_HIT_FREE: case S_DRUM_HIT_LONG:
        drum(s==S_DRUM_SOUND_808?DK_808:DK_909);eng(ENGI_DRUM);TSEL->p[P_E0]=s==S_DRUM_SOUND_808?DK_808:DK_909;TSEL->engine=TSEL->eng_req;TSEL->preset=s==S_DRUM_SOUND_808?0:1;song.playing=0;
        ui.drum_sound=s==S_DRUM_SOUND_808?12:D9_SD;ui.lane=(uint8_t)drum_lane(drum_sound_note(TSEL,ui.drum_sound));ui.cursor=4;
        drum_patch[song.sel].c[ui.drum_sound][0]=-12;drum_patch[song.sel].c[ui.drum_sound][1]=31;drum_patch[song.sel].c[ui.drum_sound][2]=16;drum_patch[song.sel].c[ui.drum_sound][3]=40;
        if(s==S_DRUM_HIT_909 || s==S_DRUM_HIT_FREE || s==S_DRUM_HIT_LONG){recording_restore_note(TSEL,0,(recorded_note_t){27000,16384,38,100,(uint8_t)recording_owner(TSEL),4,7,s==S_DRUM_HIT_909});if(s==S_DRUM_HIT_LONG){recording[0].duration=65535;recording[0].owner|=224;recording[0].length=1;}ui.note_pick=1;ui.note_identity=recording[0];ui.note_generation=recording_generation;TSEL->step[4].flags|=SF_RECORDED;go_title("DRUM HIT");}
        else go_title(s==S_DRUM_MIX_909?"DRUM MIX":"DRUM SOUND");
        ui.hot_col=2;ui.hot_t=30;break;
    case S_MENU_ADD: ui.menu=1;ui.menu_sel=MI_ADD;settings_chord_add=1;break;
    case S_MENU_HOLD: ui.menu = 1; ui.menu_sel = MI_HOLD; settings_hold = 2; break;
#if MELODEE_USB_AUDIO
    case S_MENU_USB: ui.menu = 1; ui.menu_sel = MI_USB; ua_off_want = UA_OFF_IN; break;   /* (OUT only) */
#endif
    /* the GLO SCL EDIT layers (ui_layer.c): just opened (a peek), and in use: GLO with T2 muted, T3 soloed (its key
     * held) and KNOB 1 turned; with CLK EXT (TAP dimmed); SCL at D# minor, KNOB 2 turned; EDIT on DIGITAL preset 3,
     * a favourite; a user preset; the hint after a tap */
    case S_GLO_PEEK: go_title("ENV"); ui.layer = LAYER_GLO; break;
    case S_GLO_ACTIVE:
        go_home(); ui.layer = LAYER_GLO; trk[1].p[P_MUTE] = 1; perf_solo = 4; trk[0].p[P_LEVEL] = 90;
        ui.hot_col = 0; ui.hot_t = 30;
        break;
    case S_GLO_EXT: go_home(); ui.layer = LAYER_GLO; song.g[G_CLOCK] = 1; trk[0].p[P_MUTE] = trk[3].p[P_MUTE] = 1; break;
    case S_SCL_PEEK: go_title("ENV"); ui.layer = LAYER_SCL; break;
    case S_SCL_ACTIVE: go_home(); ui.layer = LAYER_SCL; TSEL->p[P_ROOT] = 3; TSEL->p[P_SCALE] = 2; ui.hot_col = 1; ui.hot_t = 30; break;
    case S_EDIT_PEEK: go_title("EDIT 1"); ui.layer = LAYER_EDIT; break;
    case S_EDIT_ACTIVE: eng(E_FM); apply_preset_to(TSEL, 2); favorite_set(E_FM, 2, 1); go_home(); ui.layer = LAYER_EDIT;
        ui.hot_col = 0; ui.hot_t = 30; break;
    case S_EDIT_USER: song.playing = 0; eng(6); up_store(6, "MY LONG TRIO NAME"); up_load(6); favorite_set(NENGINES, 6, 1);
        go_home(); ui.layer = LAYER_EDIT; break;
    case S_LAYER_HINT: go_page(GR_TRK); ui.msg_t = 0; layer_tap(LAYER_GLO); break;
    /* NAME (ui_name.c): USER SAVE prefilled; a letter cycling (RS: S, R next); 123 on a project; an empty project name
     * (the placeholder); 12 of the widest letters, the cursor past them; playing (OCT+ dim) */
    case S_NAME_USER: song.playing = 0; go_page(GR_USER); ui.uslot = 6; name_open(NK_USER_SAVE, 6); break;
    case S_NAME_TYPING:
        song.playing = 0; go_page(GR_USER); name_open(NK_USER_SAVE, 6);
        str_cpy(nm.s, "SUB BAS", sizeof nm.s); nm.len = 7; nm.cur = 6; nm.key = 9; nm.tap = 1; nm.t = fm1_ms;
        break;
    case S_NAME_123:
        song.playing = 0; go_page(GR_SLOTS); name_open(NK_PROJ_SAVE, 1);
        str_cpy(nm.s, "LIVE 2026", sizeof nm.s); nm.len = 9; nm.cur = 5; nm.num = 1;
        break;
    case S_NAME_EMPTY: song.playing = 0; go_page(GR_SLOTS); proj_name[0] = 0; name_open(NK_PROJ_SAVE, 2); break;
    case S_NAME_FULL:
        song.playing = 0; up_store(4, "PAD"); go_page(GR_USER); name_open(NK_USER_RENAME, 4);
        str_cpy(nm.s, "MWMWMWMWMWMW", sizeof nm.s); nm.len = nm.cur = 12;
        break;
    case S_NAME_PLAYING: go_page(GR_USER); name_open(NK_USER_SAVE, 6); ui_message("STOP TO SAVE"); break;
    case S_PROJECT_NAMED:
        song.playing = 0; project_save_as(0, "LOFI JAM"); project_save_as(1, "MWMWMWMWMWMW"); project_save_as(2, "");
        song.g[G_SLOT] = 2; go_page(GR_SLOTS); ui.act = 4; ui.msg_t = 0;
        break;
    case S_SONG_NAMED:
        song.playing = 0; project_save_as(0, "LOFI JAM"); project_save_as(1, "MWMWMWMWMWMW");
        chain_config.count = 3;
        chain_config.row[0] = (chain_row_t){0, 2}; chain_config.row[1] = (chain_row_t){1, 4}; chain_config.row[2] = (chain_row_t){2, 1};
        ui.song_row = 0; go_page(GR_SONG); ui.msg_t = 0;
        break;
    case S_ROLL_EMPTY: case S_ROLL_ACID: case S_ROLL_CHORDS: case S_ROLL_TIES: case S_ROLL_LEN32: case S_ROLL_HIGH:
    case S_ROLL_LOW: case S_ROLL_WIDE: case S_ROLL_PLAYING: roll_scene(s); break;
    case S_USER_FOOT: song.playing = 0; up_store(3, "MY BASS"); ui.uslot = 3; go_page(GR_USER); break;   /* EDIT NAME lit */
#if MELODEE_SLICE && SMP_USER_SLOTS
    /* EDIT > SLICES: BREAK's 16 slices (slice 6 selected); a user sample's slices set by hand: DIV 8 taken as MAN,
     * slice 3's start moved (KNOB 2 hot), SPLIT picked (OCT+ lit) */
    case S_SLICES_BREAK: eng(13u); go_page(GR_SLICES); sp.sel = 5; break;
    case S_SLICES_USR:
        eng(13u); host_slot_make(0); smp_user_scan(0);
        TSEL->p[P_E0] = 1; TSEL->p[P_E1] = 1;
        go_page(GR_SLICES); sp.sel = 0; slice_knob(0, 2); slice_knob(1, 6); ui.act = 3; ui.msg_t = 0;
        ui.hot_col = 1; ui.hot_t = 30;
        break;
#endif
    case S_NATIVE_FM_USER: eng(ENGI_FM6); song.playing=0; native_store(ENGI_FM6,63,song.sel,"LAST VOICE"); ui.uslot=63; go_page(GR_USER); break;
    case S_NATIVE_CZ_USER: eng(ENGI_CZ); song.playing=0; native_store(ENGI_CZ,127,song.sel,"LAST CZ TONE"); ui.uslot=127; go_page(GR_USER); break;
    default: break;
    }
}
static void draw(int s)
{
    nscr = npend = 0;
    ntight = 0;
    tight[0] = 0;
    screen_clear();
    if (s == S_CALIBRATION) {                     /* the blocking setup screen: its two drawing steps */
        setup_title();
        setup_show("TURN RIGHT", "ALGORITHM");
        return;
    }
    ui.force = 1;
    ui_draw();
}

/* every value of every column on every page of every engine: labels and values fit their column */
static uint32_t nsweep;
static void sweep_columns(void)
{
    uint32_t e, i, c;
    char name[64];
    for (e = 0; e < NENGINES; e++) {
        if (!eng_ok(e))
            continue;                                    /* (DIGITAL without MELODEE_FM4: no track has it) */
        for (i = 0; i < NPAGES; i++) {
            state();
            pal(UI_GRAY_INDEX);
            eng(e);
            ui.home = 0; ui.page = (uint8_t)i; page_entered();
            if (!page_visible(i)) continue;
            snprintf(name, sizeof name, "%s/%s", ENGINES[e]->name, PAGES[i].title);
            cur_name = name;
            for (c = 0; c < 4u; c++) {
                int16_t *vp;
                int fm6 = PAGES[i].scope == SC_FM6 || PAGES[i].scope == SC_FMOP;   /* (a copy: written back) */
                const param_desc_t *d = PAGES[i].scope == SC_GLOBAL || PAGES[i].scope == SC_TRACK || PAGES[i].scope == SC_ENGINE || fm6
                                        ? page_desc(cur_page(), c, &vp) : 0;
                int32_t v, v0;
                if (!d || !d->label || d->label[0] == '-' || PAGES[i].graph == GR_MOD) continue;
                v0 = *vp;
                for (v = d->min; v <= d->max; v++) {
                    if (fm6)
                        fm6_page_put(cur_page(), c, v);
                    else
                        *vp = (int16_t)v;
                    nscr = npend = 0;
                    ui.force = 1;
                    draw_columns();
                    lint();
                    nsweep++;
                    if (v - d->min > 300) v = d->max - 1;      /* wide ranges: the ends */
                }
                if (fm6)
                    fm6_page_put(cur_page(), c, v0);
                else
                    *vp = (int16_t)v0;
            }
            draw(-1);                                    /* the whole page */
            lint();
        }
        state(); pal(UI_GRAY_INDEX); eng(e); go_home();   /* HOME's four knobs of this engine */
        snprintf(name, sizeof name, "%s/HOME", ENGINES[e]->name);
        cur_name = name;
        draw(-1);
        lint();
    }
    for (e = 0; e < 4u; e++) {                           /* the MOD page: every source and destination */
        int32_t v;
        state(); pal(UI_GRAY_INDEX); go_title("MOD");
        cur_name = "MOD sweep";
        for (v = 0; v < MD_N; v++) {
            TSEL->p[P_M1SRC] = (int16_t)(v % MS_N); TSEL->p[P_M1DST] = (int16_t)v; TSEL->p[P_M1AMT] = (int16_t)(e * 40 - 64);
            mod_ui_slot = (uint8_t)(v & 3u);
            draw(-1);
            lint();
        }
    }
}

/* the rolling digits (ui_draw.c roll_*): GLOBAL, KNOB 4 turned and the BPM (SEQ > TEMPO's) moved together, BPM 129 -> 130 in the header
 * and TUNE 19 -> 20 on a card, then back. Every frame is linted and (MONO) checked for gray; with a directory,
 * filmstrips of both (each frame side by side, x4: the static frame before, then the roll's frames) as
 * DIR/<PALETTE>_bpm_roll.ppm and DIR/<PALETTE>_card_roll.ppm, the up roll above the down roll. */
#define FS_N (1u + ROLL_FRAMES)                   /* frames per filmstrip row */
#define FS_Z 4u                                   /* zoom */
#define FS_GAP 2u                                 /* px between frames, before the zoom */
static void roll_turns(int32_t s)                 /* one UI frame with the BPM and KNOB 4 turned by s */
{
    song.g[G_BPM] = (int16_t)(song.g[G_BPM] + s);   /* (SELECT no longer sets it: SEQ > TEMPO KNOB 1 does) */
    host_enc[panel.enc[EN_K4]] += s * panel.dir[EN_K4];
    host_ticks += 16000u; fm1_ms += 16u;
    ui_input();
    ui_draw();
    host_ticks += 200000u;
}
static void roll_film_put(uint8_t *img, uint32_t iw, uint32_t row, uint32_t k, int32_t x0, int32_t y0, uint32_t w, uint32_t h)
{
    uint32_t x, y;
    for (y = 0; y < h * FS_Z; y++)
        for (x = 0; x < w * FS_Z; x++) {
            uint16_t c = swap16(host_screen[(uint32_t)(y0 + (int32_t)(y / FS_Z)) * 240u + (uint32_t)x0 + x / FS_Z]);
            uint8_t *p = img + (((row * (h + FS_GAP) + FS_GAP) * FS_Z + y) * iw + (k * (w + FS_GAP) + FS_GAP) * FS_Z + x) * 3u;
            p[0] = (uint8_t)((c >> 11) * 255u / 31u);
            p[1] = (uint8_t)(((c >> 5) & 63u) * 255u / 63u);
            p[2] = (uint8_t)((c & 31u) * 255u / 31u);
        }
}
static void roll_film_save(const char *dir, const char *pal, const char *name, const uint8_t *img, uint32_t iw, uint32_t ih)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s_%s.ppm", dir, pal, name);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fprintf(f, "P6\n%u %u\n255\n", iw, ih);
    fwrite(img, 1, (size_t)iw * ih * 3u, f);
    fclose(f);
}
static void roll_frames(const char *dir)
{
    enum { HX = 56, HW = 48, HH = H_HEAD };       /* the header crop: the tempo icon and the BPM strip */
    static uint8_t bimg[(FS_N * (HW + FS_GAP) + FS_GAP) * FS_Z * (2u * (HH + FS_GAP) + FS_GAP) * FS_Z * 3u];
    static uint8_t cimg[(FS_N * (COL_W + FS_GAP) + FS_GAP) * FS_Z * (2u * (COL_H + FS_GAP) + FS_GAP) * FS_Z * 3u];
    const uint32_t biw = (FS_N * (HW + FS_GAP) + FS_GAP) * FS_Z, bih = (2u * (HH + FS_GAP) + FS_GAP) * FS_Z;
    const uint32_t ciw = (FS_N * (COL_W + FS_GAP) + FS_GAP) * FS_Z, cih = (2u * (COL_H + FS_GAP) + FS_GAP) * FS_Z;
    uint32_t p, row, k;
    for (p = 0; p < UI_PAL_ENTRIES; p++) {
        char name[64];
        memset(bimg, 90, sizeof bimg);
        memset(cimg, 90, sizeof cimg);
        state(); song.playing = 0; pal(p);
        song.g[G_BPM] = 129; song.g[G_TUNE] = 19;
        go_title("GLOBAL");
        snprintf(name, sizeof name, "%s/roll", UI_PALETTES[p].name);
        cur_name = name;
        draw(-1);
        for (k = 0; k < 8u; k++) { ui.hot_t = 0; ui.bpm_t = 0; ui_draw(); }   /* settled, nothing hot */
        lint();
        for (row = 0; row < 2u; row++) {
            roll_film_put(bimg, biw, row, 0, HX, 0, HW, HH);
            roll_film_put(cimg, ciw, row, 0, CARD_X(3), Y_LABEL, COL_W, COL_H);
            for (k = 1; k < FS_N; k++) {
                if (k == 1u) roll_turns(row ? -1 : 1);
                else ui_draw();
                lint();
                if (p == UI_GRAY_INDEX) mono_check();
                roll_film_put(bimg, biw, row, k, HX, 0, HW, HH);
                roll_film_put(cimg, ciw, row, k, CARD_X(3), Y_LABEL, COL_W, COL_H);
            }
            if (ui.roll[3].from[0] || ui.roll[ROLL_BPM].from[0] || song.g[G_BPM] != (row ? 129 : 130) ||
                song.g[G_TUNE] != (row ? 19 : 20)) {
                fprintf(rep, "ROLL %s: not over after %u frames (BPM %d TUNE %d)\n", name, ROLL_FRAMES, song.g[G_BPM], song.g[G_TUNE]);
                nfind++;
            }
            for (k = 0; k < 8u; k++) ui_draw();
        }
        if (dir && (!strcmp(UI_PALETTES[p].name, "NIGHT") || !strcmp(UI_PALETTES[p].name, "GRAY"))) {
            roll_film_save(dir, UI_PALETTES[p].name, "bpm_roll", bimg, biw, bih);
            roll_film_save(dir, UI_PALETTES[p].name, "card_roll", cimg, ciw, cih);
        }
    }
}
/* the host cost of a roll frame: the header and the cards (draw_head, draw_columns) while both strips roll,
 * against the same calls on an idle frame (nothing drawn) and a full redraw of the four cards and the header */
/* the LCD's load (the SPI, 12 MHz: 1000 px ~ 1.3 ms): on every page of every engine, what a KNOB's detent sends (that
 * frame and the 10 after it, less what the page sends idle: the scope's signal moving), a run of KNOB 1 detents, and a
 * frame of play; the worst listed in the report (UI_AUDIT=1: all). Every frame checked: the screen as the diff (gfx.c)
 * left it = every canvas and fill sent whole. UI_AUDIT_TRACE=<page>: its frames' sends; UI_AUDIT_BLITS=1 with it: each
 * transfer */
#define AUD_N 900u
static struct { char name[56]; uint32_t px; } ldl[AUD_N];
static uint32_t naud;
static void aud_note(const char *what, const char *page, uint32_t px)
{
    uint32_t i, j;
    for (i = 0; i < naud && ldl[i].px >= px; i++)
        ;
    if (i >= AUD_N)
        return;
    for (j = naud < AUD_N ? naud : AUD_N - 1u; j > i; j--)
        ldl[j] = ldl[j - 1u];
    snprintf(ldl[i].name, sizeof ldl[i].name, "%s %s", page, what);
    ldl[i].px = px;
    if (naud < AUD_N) naud++;
}
static const char *aud_trace;                          /* UI_AUDIT_TRACE: a page whose frames are listed */
static uint32_t aud_bad;                                /* frames whose screen was not what a full redraw draws */
static void aud_verify(void)                            /* the screen as sent (gfx.c diff) = every canvas sent whole */
{
    const uint16_t *got = ref_screen;
    if (memcmp(got, host_screen, sizeof ref_screen)) {
        uint32_t i, x0 = 240, y0 = 240, x1 = 0, y1 = 0;
        for (i = 0; i < 240u * 240u; i++)
            if (got[i] != host_screen[i]) {
                if (i % 240u < x0) x0 = i % 240u;
                if (i % 240u > x1) x1 = i % 240u;
                if (i / 240u < y0) y0 = i / 240u;
                if (i / 240u > y1) y1 = i / 240u;
            }
        if (aud_bad++ < 12u)
            fprintf(stderr, "ui_render: %s: the screen sent differs from every canvas sent whole at (%u,%u)-(%u,%u)\n",
                    cur_name ? cur_name : "?", x0, y0, x1, y1);
    }
}
static uint32_t aud_frames(uint32_t n)                  /* n frames: what they send */
{
    uint32_t i;
    uint64_t s = host_sent;
    for (i = 0; i < n; i++) {
        uint64_t f = host_sent;
        uint32_t j;
        int16_t first = scope_buf[0];                   /* (the scope's signal moves on, as the audio's does) */
        for (j = 0; j + 1u < SCOPE_N; j++)
            scope_buf[j] = scope_buf[j + 1u];
        scope_buf[SCOPE_N - 1u] = first;
        frame(); nscr = npend = 0;
        if (aud_trace && cur_name && strstr(cur_name, aud_trace)) fprintf(stderr, " %llu", (unsigned long long)(host_sent - f));
        aud_verify();
    }
    return (uint32_t)(host_sent - s);
}
static void aud_page(const char *page, int playing)
{
    static const uint32_t K[4] = {EN_K1, EN_K2, EN_K3, EN_K4};
    uint32_t k, idle, px, worst = 0, wk = 0, i;
    char what[24];
    track_t *t = TSEL;
    if (playing) {                                      /* a frame of play: a sixth of a step a frame */
        uint32_t period = seq_div_samples((uint32_t)t->p[P_SDIV]), len = step_pattern_len(t), j;
        song.playing = 1;
        t->seq_pos = 0;
        ui.force = 1; frame(); aud_frames(2);
        px = 0;
        for (j = 0; j < 48u; j++) {
            for (i = 0; i < NTRK; i++) {
                track_t *u = &trk[i];
                uint32_t ul = step_pattern_len(u) ? step_pattern_len(u) : 1u, pu = seq_div_samples((uint32_t)u->p[P_SDIV]);
                if (u->seq_pos >= 0x7FFFFFFFu) u->seq_pos = 0;
                u->seq_pos += pu / 6u;
                while (u->seq_pos >= step_samples(u, pu, u->seq_idx)) {
                    u->seq_pos -= step_samples(u, pu, u->seq_idx);
                    u->seq_idx = (uint16_t)((u->seq_idx + 1u) % ul);
                }
            }
            px += aud_frames(1);
        }
        (void)period; (void)len;
        aud_note("playing, a frame", page, px / 48u);
        song.playing = 0;
        return;
    }
    song.playing = 0;
    ui.force = 1; frame(); aud_frames(4);
    idle = aud_frames(8) / 8u;
    for (k = 0; k < 4u; k++) {
        int32_t d;
        for (d = -1; d <= 1; d += 2) {
            uint64_t s0 = host_sent;
            host_blit_trace = aud_trace && strstr(page, aud_trace) && getenv("UI_AUDIT_BLITS");
            turn(K[k], d); nscr = npend = 0;
            host_blit_trace = 0;
            aud_verify();
            if (aud_trace && strstr(page, aud_trace)) fprintf(stderr, "\n%s K%u %+d: turn %llu, then", page, k + 1u, d, (unsigned long long)(host_sent - s0));
            aud_frames(10);
            px = (uint32_t)(host_sent - s0);
            px = px > idle * 11u ? px - idle * 11u : 0u;
            if (px > worst) { worst = px; wk = k; }
        }
    }
    snprintf(what, sizeof what, "KNOB %u detent", wk + 1u);
    aud_note(what, page, worst);
    {   /* KNOB 1 turned on: 8 detents, a frame each */
        uint64_t s0 = host_sent;
        for (i = 0; i < 8u; i++) { host_enc[panel.enc[EN_K1]] += panel.dir[EN_K1]; frame(); nscr = npend = 0; aud_verify(); }
        px = (uint32_t)(host_sent - s0) / 8u;
        aud_note("KNOB 1 run, a frame", page, px > idle ? px - idle : 0u);
    }
    if (idle > 1000u)
        aud_note("idle, a frame", page, idle);
}
static void audit_sends(FILE *rep)
{
    uint32_t e, i, playing;
    char name[56];
    aud_trace = getenv("UI_AUDIT_TRACE");
    aud_on = 1;
    for (playing = 0; playing < 2u; playing++)
        for (e = 0; e < NENGINES; e++) {
            if (!eng_ok(e))
                continue;
            for (i = 0; i <= NPAGES; i++) {
                state();
                pal(1);
                demo_pat16(&trk[0], DEMO_ACID);
                eng(e);
                if (i == NPAGES) {
                    go_home();
                    snprintf(name, sizeof name, "%s/STAGE", ENGINES[e]->name);
                } else {
                    ui.home = 0; ui.page = (uint8_t)i; page_entered();
                    if (!page_visible(i)) continue;
                    if (playing && PAGES[i].fam != FAM_SEQ) continue;
                    snprintf(name, sizeof name, "%s/%s", ENGINES[e]->name, PAGES[i].title);
                }
                cur_name = name;
                aud_page(name, (int)playing);
            }
            if (playing) break;                         /* (play: one engine; the drum grid below) */
        }
    state(); pal(1); drum(0); go_page(GR_ROLL); cur_name = "DRUM/NOTES grid";
    aud_page("DRUM/NOTES grid", 0);
    state(); pal(1); drum(0); go_page(GR_ROLL); aud_page("DRUM/NOTES grid", 1);
    aud_on = 0;
    fprintf(rep, "\nLCD load, the worst (px sent; the SPI 12 MHz: 1000 px ~ 1.3 ms; a full screen 57600 ~ 77 ms):\n");
    for (i = 0; i < naud && (i < 40u || getenv("UI_AUDIT")); i++)
        fprintf(rep, "  %7u  %s\n", ldl[i].px, ldl[i].name);
    fprintf(rep, "  %u frames not as sending every canvas whole leaves the screen\n", aud_bad);
    nfind += aud_bad;
}
static void roll_cost(void)
{
    enum { N = 400 };
    uint32_t i, k;
    double t_roll = 0, t_full = 0;
    uint64_t px_roll = 0, bl_roll = 0, nroll = 0, px_full = 0, bl_full = 0;
    for (i = 0; i < N; i++) {
        clock_t c0;
        state(); song.playing = 0; pal(1);
        song.g[G_BPM] = 129; song.g[G_TUNE] = 19;
        go_title("GLOBAL");
        draw(-1);
        for (k = 0; k < 8u; k++) { ui.hot_t = 0; ui.bpm_t = 0; ui_draw(); }
        px_visited = 0; blit_px = 0;
        c0 = clock();
        ui.force = 1; ui.frame++; draw_head(); draw_columns(); ui.force = 0;
        t_full += (double)(clock() - c0); px_full += px_visited; bl_full += blit_px;
        nscr = npend = 0;
        roll_turns(i & 1u ? -1 : 1);                /* the change: the card and the header redrawn whole */
        for (k = 2; k < ROLL_FRAMES; k++) {         /* the strip-only frames */
            px_visited = 0; blit_px = 0;
            c0 = clock();
            ui.frame++; draw_head(); draw_columns();
            t_roll += (double)(clock() - c0); px_roll += px_visited; bl_roll += blit_px; nroll++;
            nscr = npend = 0;
        }
    }
    fprintf(rep, "  roll frame (header BPM 129->130 and card TUNE 19->20, strips only): %.2f us, %llu glyph px, %llu px blitted;"
            " header + 4 cards redrawn whole: %.2f us, %llu glyph px, %llu px blitted\n",
            t_roll / CLOCKS_PER_SEC / (double)nroll * 1e6, (unsigned long long)(px_roll / nroll), (unsigned long long)(bl_roll / nroll),
            t_full / CLOCKS_PER_SEC / N * 1e6, (unsigned long long)(px_full / N), (unsigned long long)(bl_full / N));
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "build/ui_new";
    char path[600];
    uint32_t p, s;
    static const char *const SHOW[] = {"NIGHT", "DAY", "CONTRAST", "GRAY"};
    snprintf(path, sizeof path, "%s/report.txt", out);
    rep = fopen(path, "w");
    if (!rep) { fprintf(stderr, "cannot write %s\n", path); return 1; }
    {
        char ap[600];
        snprintf(ap, sizeof ap, "%s/text_audit.tsv", out);
        audf = fopen(ap, "w");
    }
    if (audf) fprintf(audf, "screen\ttexts\ttext_px\ticons\ticon_px\tkeycaps\tkeycap_px\tellipsised\ttight\twords\ttight_list\n");
    {   /* the lint itself: an overlap, a value too wide for its column, a text past its canvas, one past its cell */
        state(); pal(0); cur_name = "self-test";
        nscr = npend = 0;
        cv_begin(COL_W, COL_H, T_BG);
        cv_text(0, 0, &AF_S, "AAAA", T_TEXT);
        cv_text(10, 4, &AF_S, "BBBB", T_TEXT);
        cv_text_fit(0, 15, &AF_M, "WWWWWWWW", T_THEME, T_BG, COL_W);
        cv_text(40, 30, &AF_M, "CUT", T_THEME);
        cv_rrect(2, 31, 20, 13, 3, T_SURF, T_BG);          /* a cell narrower than its word */
        cv_text(4, 31, &AF_S, "SPILL", T_TEXT);
        cv_blit(4, Y_LABEL);
        lint();
        if (nfind < 4u || !nspill) { fprintf(stderr, "ui_render: the lint missed its self-test (%u)\n", nfind); return 1; }
        fprintf(rep, "(self-test: %u findings above are expected)\n\n", nfind);
        nfind = 0;
    }
    for (p = 0; p < UI_PAL_ENTRIES; p++)
        for (s = 0; s < S_COUNT; s++) {
            char name[64];
            uint32_t k;
            if (!MELODEE_FM4 && (s == S_OP_ENV || (s >= S_ALG1 && s <= S_OP_LEVEL)))
                continue;                           /* (DIGITAL's own screens: MELODEE_FM4=1 only) */
            snprintf(name, sizeof name, "%s/%s", UI_PALETTES[p].name, S_NAME[s]);
            cur_name = name;
            setup((int)s);
            pal(p);
            if (p == UI_GRAY_INDEX) aud = audf;
            draw((int)s);
            lint();
            if (p == UI_GRAY_INDEX) { audit_scene(S_NAME[s]); aud = 0; mono_check(); }
            for (k = 0; k < 3u; k++)
                if (!strcmp(UI_PALETTES[p].name, SHOW[k])) write_ppm(out, SHOW[k], S_NAME[s]);
        }
    {   /* every FM6 chart (the lint above, fmp_check, runs on each), in MONO: gray */
        uint32_t a, e, c0 = fmp_charts;
        for (e = FM6_MODERN; e <= FM6_OPL; e++)
        for (a = 1; a <= 32u; a++) {
            char name[32];
            snprintf(name, sizeof name, "FM6 engine %u ALG %u", e, a);
            cur_name = name;
            state(); pal(UI_GRAY_INDEX); eng(ENGI_FM6); TSEL->p[P_E0] = (int16_t)a; go_title("EDIT 1");
            fm6_fn_set(song.sel, FN_ENGINE, (int32_t)e);
            draw(-1);
            lint();
            mono_check();
            if (a >= 3u && a <= 6u) {
                char image[32];
                snprintf(image, sizeof image, "fm6_engine_%u_alg_%02u", e, a);
                write_ppm(out, "MONO", image);
            }
        }
        if (fmp_charts - c0 != 96u) { fprintf(stderr, "ui_render: %u FM6 charts drawn of 96\n", fmp_charts - c0); return 1; }
    }
    sweep_columns();
    roll_frames(argc > 2 ? argv[2] : 0);
    /* draw cost: a full redraw of a screen (all strips), and HOME frame by frame (the scope, every other frame) */
    {
        static const int COST[] = {S_HOME, S_PRESETS, S_STEP, S_ROLL_CHORDS, S_DRUM, S_MIXER, S_MENU, S_ABOUT_CREDITS, S_CONFIRM_PROJ};
        enum { N = 200 };
        uint32_t i, k;
        fprintf(rep, "\ndraw cost on the host (cc -O1; only the ratios mean anything on the device):\n");
        fprintf(rep, "  screen            full redraw us   texts+icons   glyph px read\n");
        for (k = 0; k < sizeof COST / sizeof COST[0]; k++) {
            clock_t c0;
            uint64_t px, nt;
            setup(COST[k]); pal(1);
            draw(COST[k]);
            px_visited = n_text = 0;
            c0 = clock();
            for (i = 0; i < N; i++) draw(COST[k]);
            px = px_visited / N; nt = n_text / N;
            fprintf(rep, "  %-16s %15.1f %13llu %15llu\n", S_NAME[COST[k]], (double)(clock() - c0) / CLOCKS_PER_SEC / N * 1e6,
                    (unsigned long long)nt, (unsigned long long)px);
        }
        {
            clock_t c0;
            setup(S_HOME); pal(1); draw(S_HOME);
            px_visited = n_text = 0;
            c0 = clock();
            for (i = 0; i < N * 4; i++) { ui_draw(); nscr = npend = 0; }
            fprintf(rep, "  HOME, a frame without force (lazy strips): %.1f us, %llu texts\n",
                    (double)(clock() - c0) / CLOCKS_PER_SEC / (N * 4) * 1e6, (unsigned long long)(n_text / (N * 4)));
        }
        {   /* the STEP page's piano roll: its panel alone redrawn, and a frame that changes nothing (the signature only) */
            static const int G[] = {S_ROLL_CHORDS, S_ROLL_PLAYING, S_DRUM};
            for (k = 0; k < 3u; k++) {
                clock_t c0;
                double tg, ti;
                setup(G[k]); pal(1); draw(G[k]);
                c0 = clock();
                for (i = 0; i < N * 4; i++) { ui.force = 1; draw_graph(); ui.force = 0; nscr = npend = 0; }
                tg = (double)(clock() - c0) / CLOCKS_PER_SEC / (N * 4) * 1e6;
                song.playing = 0;                        /* (no playhead moving) */
                ui_draw(); ui_draw(); nscr = npend = 0;
                c0 = clock();
                for (i = 0; i < N * 4; i++) { ui.frame++; draw_graph(); nscr = npend = 0; }
                ti = (double)(clock() - c0) / CLOCKS_PER_SEC / (N * 4) * 1e6;
                fprintf(rep, "  %s: the panel redrawn %.1f us; an unchanged frame (signature only) %.2f us\n", S_NAME[G[k]], tg, ti);
            }
        }
        roll_cost();
        audit_sends(rep);
        {   /* playing, the playhead alone moving (NOTES, the drum grid, PATTERN): only its columns are sent, and the
             * screen is what a full redraw draws */
            static const int P[] = {S_ROLL_PLAYING, S_DRUM, S_PATTERN};
            static uint16_t got[240 * 240];
            for (k = 0; k < 3u; k++) {
                track_t *t;
                uint32_t period, mv, nmv = 40u, same = 1, len;
                uint64_t sent = 0, full;
                setup(P[k]); pal(1); song.playing = 1;
                t = TSEL;
                t->seq_pos = 0;
                draw(P[k]);
                screen_clear(); host_sent = 0; ui.force = 1; ui_draw(); ui.force = 0; full = host_sent;
                period = seq_div_samples((uint32_t)t->p[P_SDIV]);
                len = step_pattern_len(t);
                for (mv = 0; mv < nmv; mv++) {          /* a quarter step a frame: 10 steps */
                    t->seq_pos += period / 4u;
                    while (t->seq_pos >= step_samples(t, period, t->seq_idx)) {
                        t->seq_pos -= step_samples(t, period, t->seq_idx);
                        t->seq_idx = (uint16_t)((t->seq_idx + 1u) % len);
                    }
                    nscr = npend = 0; ui.frame++;
                    host_sent = 0; ui_draw(); sent += host_sent;
                    memcpy(got, host_screen, sizeof got);
                    ui.force = 1; ui_draw(); ui.force = 0;
                    if (memcmp(got, host_screen, sizeof got)) {
                        same = 0;
                        break;
                    }
                }
                fprintf(rep, "  %s playing: the playhead alone moved, %.0f px sent a frame (a full redraw %llu)%s\n", S_NAME[P[k]],
                        (double)sent / nmv, (unsigned long long)full, same ? "" : "  -- NOT AS A FULL REDRAW");
                if (!same || sent / nmv > full / 8u) {
                    fprintf(stderr, "ui_render: %s playing: %s\n", S_NAME[P[k]], !same ? "the playhead's columns differ from a full redraw" :
                            "the playhead sends too much");
                    nfind++;
                }
                song.playing = 0;
            }
        }
    }
    if (audf) fclose(audf);
    fprintf(rep, "\n%u lint findings, %u ellipsised free texts, %u MONO pixels off gray; %u column values swept; "
            "%u FM6 charts linted\n", nfind, nfree, mono_bad, nsweep, fmp_charts);
    fclose(rep);
    printf("ui_render: %u screens x %u palettes + the page/value sweep; %u lint findings, %u ellipsised free texts (%u distinct), "
           "%u MONO pixels off gray; report %s\n", (unsigned)(S_COUNT - (MELODEE_FM4 ? 0 : 1 + S_OP_LEVEL - S_ALG1 + 1)), (unsigned)NPALETTES, nfind, nfree, nfree_seen, mono_bad, path);
    return nfind || mono_bad ? 1 : 0;
}
