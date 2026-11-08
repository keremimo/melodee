/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SEQ > MOTION (docs/design: mock/r6_pages MOTION, mock/r6_popups): the selected track's recorded motion as lanes, one
 * per parameter it moves on the pattern playing: the value step by step (held from one event to the next, the sound's
 * own before the first), the playhead over them. No chips: KNOB 2 the lane, KNOB 1 Play on / off, OCT+ the lane's
 * sheet (Play, Clear <lane>, Clear all motion), KNOB 3 the step, KNOB 4 its value;
 * SEQ + KNOB 2/4 clears the lane/point. OCT- Stage. Included by ui_draw.c */
#define MO_MAX 8                                        /* lanes (parameters) at most */
#define MO_Y0 Y_LABEL                                   /* the lanes: 60 px each, 3 shown, from y 32 to the sound bar */
#define MO_H 60
#define MO_VIS 3
static struct { uint8_t lane, first, step, edit; uint32_t sig; } mo;
static int16_t mo_cell;                                 /* (the sheet's lane: its parameter) */

static int motion_page(void) { return !ui.home && !ui.menu && !ui.layer && cur_page()->graph == GR_MOTION; }
/* the parameters with events on the selected track's pattern, by id; their number */
static uint32_t mo_lanes(uint16_t *ids)
{
    uint32_t i, j, n = 0, k = song.sel % NTRK;
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        if ((e->place >> 6) != k || motion_pattern[i] != TSEL->pattern || e->param == P_RECQ || e->param == P_TUNING ||
            (motion_native_event(e->param,e->value) && (uint32_t)e->value >> 8 != motion_native_tag(TSEL)))   /* (another engine's) */
            continue;
        for (j = 0; j < n && ids[j] != motion_token(e); j++)
            ;
        if (j < n || n >= MO_MAX)
            continue;
        for (j = n++; j > 0 && ids[j - 1] > motion_token(e); j--)
            ids[j] = ids[j - 1];
        ids[j] = (uint16_t)motion_token(e);
    }
    return n;
}
static uint32_t mo_events(uint32_t id)                  /* the lane's events */
{
    uint32_t i, n = 0, k = song.sel % NTRK;
    for (i = 0; i < motion.count; i++)
        n += (motion.event[i].place >> 6) == k && motion_pattern[i] == TSEL->pattern && motion_token(&motion.event[i]) == id;
    return n;
}
static int mo_value(uint32_t id, uint32_t step, int16_t *v)   /* the event at step, 1 if there is one */
{
    uint32_t i, place = (song.sel % NTRK) << 6 | step;
    for (i = 0; i < motion.count; i++)
        if (motion_pattern[i] == TSEL->pattern && motion.event[i].place == place && motion_token(&motion.event[i]) == id) {
            *v = (int16_t)((id&256u) ? motion.event[i].value & 255 : motion.event[i].value);
            return 1;
        }
    return 0;
}
/* a lane of the synth's own patch (motion.c MO_NATIVE + i): by its schema. Motion by ids (FM6's): the parameter i, its
 * label in words ("Cutoff"), an instanced page's ("OP3 OUT"); by bytes (the Prophet's, the CZ-1's): the parameter at
 * byte i (else its label, the byte's range: Motion's map), or the schema's lane names ("DCW 1 env", the bytes 0..255) */
static const param_desc_t MO_CZ_BYTE = {"TONE", F_INT, 0, 255, 0, 0, 0};
static param_desc_t mo_d;
static char mo_dl[MOD_SCH_NAME + 1u];
static const param_desc_t *mo_desc(uint32_t id)         /* its range (and name) */
{
    uint32_t i = (id&255u) - MO_NATIVE, e = eng_idx(TSEL->eng_req);
    sch_param_t p;
    uint8_t mx;
    mod_sch_head h;
    if (!(id&256u))
        return param_desc_of(e, id);
    if (sch_head(e, &h) || (h.flags & MOD_SCH_F_MLANES))
        return &MO_CZ_BYTE;
    if (sch_param(e, i, &p))
        return &MO_CZ_BYTE;
    if (!(h.flags & MOD_SCH_F_MBYTES) || (p.kind == MOD3_SK_FIELD && p.off == i && !p.shift)) {
        const param_desc_t *d = sch_param_desc(e, &p);   /* (its own: a value as its pages show it) */
        if (d)
            return d;
    }
    sch_str(e, p.label, mo_dl, sizeof mo_dl);            /* (a byte its parameter computes from: the byte's own range) */
    if (sch_wid(e, h.mmap + i, &mx, 1u))
        mx = 255u;
    mo_d = (param_desc_t){mo_dl, F_INT, 0, mx, 0, 0, 0};
    return &mo_d;
}
static void mo_name(uint32_t id, char *b, uint32_t n)   /* "Cutoff" */
{
    uint32_t i = (id&255u) - MO_NATIVE, e = eng_idx(TSEL->eng_req), k;
    mod_sch_head h;
    if ((id&256u) && !sch_head(e, &h) && (h.flags & MOD_SCH_F_MLANES)) {   /* (the schema's lane names) */
        uint8_t d[3], x;
        if (sch_wid(e, h.mmap + 2u * h.patch, d, 3u) || i >= h.patch || sch_wid(e, h.mmap + 2u * h.patch + 3u + i, &x, 1u) ||
            x >= d[2]) {
            str_cpy(b, "TONE", n);
            return;
        }
        uint32_t off = sch_u16(d);
        char t[MOD_SCH_NAME + 1u];
        for (k = 0; k < x; k++)
            off += sch_str(e, off, t, sizeof t) + 1u;
        sch_str(e, off, b, n);
        return;
    }
    if ((id&256u) && n > 4u && sch_inst_of(e, i, &k)) {   /* "OP3 OUT" */
        uint32_t m = sch_inst_name(k, b, n - 1u);
        b[m++] = ' ';
        str_cpy(b + m, mo_desc(id)->label, n - m);
        return;
    }
    list_words(b, mo_desc(id)->label, n);
}
static int16_t mo_base(uint32_t id)                     /* the value the patch itself has */
{
    uint32_t i = (id&255u) - MO_NATIVE, k = song.sel % NTRK;
    if (!(id&256u))
        return motion_base_value(TSEL, id);
    return (int16_t)(motion_nbit(k, i) ? motion_nbase[k][i] : motion_slot_get(TSEL, i));
}

/* ------------------------------------------------------- the sheet --- */
static int mo_free(void)
{
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return 0;
    }
    return 1;
}
static void mo_play_v(char *b) { str_cpy(b, motion_enabled(TSEL) ? "On" : "Off", 8); }
static void mo_play(void)
{
    if (mo_free())
        motion_set_enabled(TSEL, !motion_enabled(TSEL));
}
/* A sound-load undo normally clears motion. Preserve the current lane pool while
 * taking that snapshot so an automation edit cannot erase neighbouring lanes. */
static void mo_edit_begin(void)
{
    motion_store_t saved;
    motion_snapshot_track(TSEL, &saved);
    undo_seal();                                   /* this edit has its own undo */
    load_begin(TSEL, UNDO_PAT);
    (void)motion_replace_track(TSEL, &saved);
}
static void mo_clear_lane(void)
{
    if (!mo_free())
        return;
    mo_edit_begin();
    motion_clear_param(TSEL, (uint32_t)mo_cell);
    load_end(TSEL);
    ui_message("LANE CLEARED");
}
static char mo_clear_label[20];
static const sheet_row_t SHEET_MOTION[] = {{"Play", mo_play_v, 0, mo_play, 0}, {mo_clear_label, 0, 0, mo_clear_lane, SR_RED},
    {"Clear all motion", 0, 0, 0, CF_CLEAR_MOTION}};

static void motion_enter(void)                          /* OCT+ on a lane: its sheet */
{
    uint16_t ids[MO_MAX];
    uint32_t n = mo_lanes(ids);
    char t[20], sub[16];
    if (!n)
        return;
    mo_cell = ids[mo.lane % n];
    mo_name((uint32_t)mo_cell, t, sizeof t);
    str_cpy(mo_clear_label, "Clear ", sizeof mo_clear_label);
    mo_name((uint32_t)mo_cell, mo_clear_label + 6, sizeof mo_clear_label - 6);
    if (mo_clear_label[6] >= 'A' && mo_clear_label[6] <= 'Z')
        mo_clear_label[6] = (char)(mo_clear_label[6] + 32);   /* ("Clear cutoff") */
    fmt_int(sub, (int32_t)mo_events((uint32_t)mo_cell));
    str_cpy(sub + str_len(sub), " events", 8);
    sheet_open(t, sub, SHEET_MOTION, NELEM(SHEET_MOTION));
}
/* KNOB 3 picks a step; KNOB 4 edits its held value or inserts a point.
 * SEQ + KNOB 2 clears the selected lane; SEQ + KNOB 4 deletes one point. */
static void motion_knob(uint32_t k, int32_t s)
{
    uint16_t ids[MO_MAX];
    uint32_t n = mo_lanes(ids), len = (uint32_t)clamp(TSEL->p[P_SLEN], 1, NSTEP);
    int clear = (fm1_in.buttons & (1u << panel.btn[B_SEQ])) && (k == 1u || k == 3u);
    if (clear) ui.seq_t0 |= 2u;                       /* consume SEQ's tap and hold */
    if (k == 1u && n && !clear)
        mo.lane = (uint8_t)clamp((int32_t)mo.lane + (s > 0 ? 1 : -1), 0, (int32_t)n - 1);
    else if (k == 0u && mo_free())
        motion_set_enabled(TSEL, s > 0);
    else if (k == 2u) {
        mo.step = (uint8_t)clamp((int32_t)mo.step + s, 0, (int32_t)len - 1);
        mo.edit = 1;
    } else if (n && (k == 3u || clear) && mo_free()) {
        uint32_t id = ids[mo.lane % n], step = mo.step % len;
        int16_t v = mo_base(id);
        if (clear && k == 1u) {
            mo_cell = (int16_t)id;
            mo_clear_lane();
        } else if (clear) {
            if (!mo_value(id, step, &v)) return;
            mo_edit_begin();
            motion_delete_event(TSEL, step, id);
            load_end(TSEL);
            ui_message("POINT CLEARED");
        } else {
            const param_desc_t *d = mo_desc(id);
            for (uint32_t i = 0; i <= step; i++) mo_value(id, i, &v);
            v = (int16_t)clamp((int32_t)v + s, d->min, d->max);
            /* Packed CZ bytes have gaps between legal values. Skip those codes
             * in the knob's direction, so a wave/flag byte remains editable. */
            int16_t wire = v;
            if (id & 256u) {
                int32_t next = v;
                while (next >= d->min && next <= d->max &&
                       !motion_native_ok(id & 255u, (int16_t)(motion_native_tag(TSEL) << 8 | (uint32_t)next), 0))
                    next += s > 0 ? 1 : -1;
                if (next < d->min || next > d->max) return;
                wire = (int16_t)(motion_native_tag(TSEL) << 8 | (uint32_t)next);
            }
            if (!mo_value(id, step, &v) && motion.count >= MOTION_MAX) {
                ui_message("MOTION FULL");
                return;
            }
            int was_on = motion_enabled(TSEL);
            mo_edit_begin();
            int result = motion_set_event(TSEL, step, id & 255u, wire);
            if (!was_on) motion_set_enabled(TSEL, 0);
            load_end(TSEL);
            if (result) ui_message(result == 2 ? "MOTION FULL" : "INVALID VALUE");
            mo.edit = 1;
        }
    }
}

/* ----------------------------------------------------------- drawing --- */
/* a lane every 60 px (large type): its name over it (the lane picked: tinted, the name and the line in the track's colour,
 * the step picked and its value at the right), the value step by step over x 12..228, the playhead across them all;
 * motion off: the lines dim */
static void mo_label(uint32_t id, char *b)              /* "CUTOFF" */
{
    uint32_t i;
    mo_name(id, b, 16);
    for (i = 0; b[i]; i++)
        if (b[i] >= 'a' && b[i] <= 'z') b[i] = (char)(b[i] - 32);
}
static void motion_draw(void)
{
    uint16_t ids[MO_MAX];
    uint32_t n = mo_lanes(ids), len = (uint32_t)clamp(TSEL->p[P_SLEN], 1, NSTEP), r, s, sig, on = motion_enabled(TSEL);
    uint32_t ph = song.playing ? TSEL->seq_idx % len : 0xFFu;
    if (ui.force) {
        lcd_fill(0, H_HEAD, 240, PV_SOUND_Y - H_HEAD, T_BG);
        mo.sig = 0;
    }
    mo.step %= len;
    draw_head();
    pv_sound();
    if (n && mo.lane >= n)
        mo.lane = (uint8_t)(n - 1u);
    if (mo.lane < mo.first)
        mo.first = mo.lane;
    if (mo.lane >= mo.first + MO_VIS)
        mo.first = (uint8_t)(mo.lane + 1u - MO_VIS);
    sig = n * 7u + mo.lane * 131u + mo.first * 1031u + ph * 40503u + on * 977u + len * 13u + ux.gen * 7919u + ux.pal * 31u +
          mo.step * 8191u + mo.edit * 524287u + song.sel * 3u + TSEL->pattern * 104729u + TSEL->eng_req * 613u;
    for (r = 0; r < motion.count; r++)
        sig = (sig ^ (uint32_t)(motion.event[r].place * 131u + motion.event[r].param * 7u + (uint16_t)motion.event[r].value)) *
              16777619u;
    if (!ui.force && sig == mo.sig)
        return;
    mo.sig = sig;
    for (r = 0; r < MO_VIS; r++) {
        uint32_t l = mo.first + r;
        int32_t top = (int32_t)r * MO_H, x0 = 12, x1 = 228;
        int sel = l < n && l == mo.lane;
        uint16_t bg = sel ? ux_mix(T_BG, T_THEME, 18) : T_BG;
        cv_begin(240, MO_H, T_BG);
        if (sel)
            cv_rrect(BOX_X, 1, BOX_W, MO_H - 2, 8, bg, T_BG);
        else if (r && l < n && mo.lane + 1u != l)
            cv_rect(12, 0, 216, 1, T_LINE);
        if (!n && r == 1u)
            cv_text_c(120, ty_y(MO_H / 2, TY_TX), TY_TX, "No motion", T_MID, T_BG);
        if (l < n) {
            const param_desc_t *d = mo_desc(ids[l]);
            int32_t lo, hi, span, gy0 = 24, gh = 30, py = -1, lx = 130;
            uint16_t lc = !on ? T_DIM : sel ? T_THEME : ux_mix(T_BG, T_THEME, 60);
            int16_t v = mo_base(ids[l]), w = v;
            char b[16];
            lo = hi = clamp(v, d->min, d->max);         /* (the lane scaled to what it moves through, 16 at least) */
            for (s = 0; s < len; s++) {
                mo_value(ids[l], s, &w);
                lo = clamp(w, d->min, d->max) < lo ? clamp(w, d->min, d->max) : lo;
                hi = clamp(w, d->min, d->max) > hi ? clamp(w, d->min, d->max) : hi;
            }
            if (hi - lo < 16) {
                lo = (lo + hi) / 2 - 8;
                hi = lo + 16;
            }
            span = hi - lo;
            for (s = 4; s < len; s += 4)                /* (the beats, faint) */
                cv_rect(x0 + (int32_t)s * (x1 - x0) / (int32_t)len, gy0, 1, gh, ux_mix(bg, T_LINE, 50));
            if (sel) {                                  /* (the step picked, its value) */
                char vb[16], sb[12];
                const char *unit;
                int16_t sv = mo_base(ids[l]);
                for (s = 0; s <= mo.step; s++) mo_value(ids[l], s, &sv);
                param_format(d, sv, vb, &unit);
                lx = cv_text_r(228, ty_y(13, TY_LB), TY_LB, vb, T_THEME, bg);
                str_cpy(sb, "Step ", sizeof sb);
                fmt_int(sb + 5, mo.step + 1u);
                lx = cv_text_r(lx - 8, ty_y(13, TY_LB), TY_LB, sb, T_MID, bg);
            }
            mo_label(ids[l], b);
            cv_free_text(12, ty_y(13, TY_LB), TY_LB, b, !on ? T_DIM : sel ? T_THEME : T_MID, bg, lx - 8 - 12);
            for (s = 0; s < len; s++) {                 /* the value held step by step */
                int32_t xa = x0 + (int32_t)s * (x1 - x0) / (int32_t)len, xb = x0 + (int32_t)(s + 1u) * (x1 - x0) / (int32_t)len;
                int32_t y;
                mo_value(ids[l], s, &v);
                y = gy0 + gh - 2 - (clamp(clamp(v, d->min, d->max), lo, hi) - lo) * (gh - 2) / span;
                if (py >= 0 && py != y)
                    cv_rect(xa, py < y ? py : y, 2, (py < y ? y - py : py - y) + 2, lc);
                cv_rect(xa, y, xb - xa, 2, lc);
                py = y;
            }
            if (mo.edit && sel)
                cv_rect(x0 + (int32_t)mo.step * (x1 - x0) / (int32_t)len, gy0 - 2, 1, gh + 4, T_ACCENT);
        }
        if (ph < len && n && l < n) {                   /* the playhead, across the lanes */
            int32_t x = x0 + (int32_t)ph * (x1 - x0) / (int32_t)len;
            cv_rect(x, 22, 2, MO_H - 26, T_TEXT);
        }
        if (n > MO_VIS) {                               /* where the lanes are */
            int32_t sp = MO_VIS * MO_H, th = sp * MO_VIS / (int32_t)n, ty = (sp - th) * mo.first / (int32_t)(n - MO_VIS) - top;
            cv_rect(238, 0, 2, MO_H, T_LINE);
            if (ty < MO_H && ty + th > 0)
                cv_rect(238, ty, 2, th, T_THEME);
        }
        cv_blit(0, (uint32_t)(MO_Y0 + top));
    }
}
