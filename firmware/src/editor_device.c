/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee Studio's FM-1 page (EDITOR_PROTOCOL.md "Studio device"): SETTINGS (50) lists and sets the HOME menu's rows
 * (menu_rows.c: the menu's own values, steps and words; saved as UI_SET saves), MLEARN (51) lists MIDI LEARN's controls
 * and parameters, forgets one or all and learns one from the next message, PADS (53) reads and writes a kit track's pads'
 * parameters, as its synth's pads block has them (pads.c; BLE 52: editor_ble.c). WATCH bit 7 pushes DEVICE_CHANGED (60) when they or Bluetooth's state change on the
 * device. Included by editor.c */
#include "menu_rows.c"                                    /* (the firmware: ui_menu.c has it already) */
enum { ED_SETTINGS = 50, ED_MLEARN, ED_PADS = 53, ED_DEVICE_CHANGED = 60 };
static uint8_t ed_learn;                                  /* MLEARN: the control + 1 waiting for a message, 0 none */
static uint16_t ed_plearn __attribute__((section(".pool")));   /* MLEARN: (any << 10 | track << 7 | parameter) + 1 waiting, 0 none */

static void ed_set_values(void)                           /* the rows, then each one's value (14 bits) */
{
    ed_b(MI_COUNT);
    for (uint32_t i = 0; i < MI_COUNT; i++)
        ed_u14(menu_get(i));
}
/* what DEVICE_CHANGED follows: 0 the rows' values, 1 MIDI LEARN's table, 2 Bluetooth's state, 3 the kits' pads' values */
static uint32_t ed_dev_sig(uint32_t k)
{
    uint32_t h = 2166136261u, i;
    if (k == 0)
        for (i = 0; i < MI_COUNT; i++) h = (h ^ menu_get(i)) * 16777619u;
    if (k == 1)
        h = fnv(fnv(h, mlearn_row, sizeof mlearn_row), mparam_row, sizeof mparam_row);
#if MELODEE_BLE
    if (k == 2)
        h = mb.generation + mb.on * 3u + MB_READY_BITS * 997u + mb_saved.flags * 7919u + mel_ble_code() * 17u + mb.attempt * 31u;
#endif
    if (k == 3)
        for (i = 0; i < NPART; i++)
            if (sch_info[eng_idx(patch_eng_of(i))].pads)  /* (a synth with pads: its patch) */
                h = fnv(h, patch_of(i), patch_len(patch_eng_of(i)));
    return h;
}
static void ed_dev_known(void)                            /* the editor's own change: no push */
{
    for (uint32_t k = 0; k < 4u; k++)
        ed_w.psig[4u + k] = ed_dev_sig(k);
}
static uint32_t ed_device_push(uint32_t n)                /* ed_session_push's last: DEVICE_CHANGED (WATCH bit 7) */
{
    uint32_t what = 0, k, s;
    if (!(ed_w.bits & 128u) || n >= ED_PUSH_MAX || !ed_room())
        return n;
    for (k = 0; k < 4u; k++)
        if ((s = ed_dev_sig(k)) != ed_w.psig[4u + k]) {
            ed_w.psig[4u + k] = s;
            what |= 1u << k;
        }
    if (!what)
        return n;
    ed_begin(ED_DEVICE_CHANGED);                          /* what changed, then the rows' values */
    ed_b(what);
    ed_set_values();
    ed_send();
    return n + 1u;
}
/* ed_service, every pass: MLEARN's wait for a message (it ends when the device's MIDI LEARN page opens, or 3 s after the
 * editor's last request), and BLE's scan from the editor */
static void ed_learn_end(void)
{
    ed_learn = 0;
    ed_plearn = 0;
    if (ui.menu != 4u)
        mlearn_on = 0;                                    /* (the page's own wait is the page's) */
}
static void ed_device_poll(void)
{
    int32_t seen;
#if MELODEE_BLE
    if (ed_bt_scan && fm1_ms - ed_w.last_ms > 3000u) {
        ed_bt_scan = 0;
        if (ui.menu != 3u)
            mel_ble_scan(0);                              /* (the Bluetooth page's own scan stays) */
    }
#endif
    if (!ed_learn && !ed_plearn)
        return;
    if (ui.menu == 4u || fm1_ms - ed_w.last_ms > 3000u) {
        ed_learn_end();
        return;
    }
    mlearn_on = 1;                                        /* (as the page: nothing learned acts, a message waits) */
    if ((seen = mlearn_seen) < 0)
        return;
    mlearn_seen = -1;
    uint32_t k = ed_learn - 1u, src = (uint32_t)seen & 255u, ch = (uint32_t)seen >> 8, p = ed_plearn - 1u;
    if ((src & MLEARN_NOTE) && (ed_plearn || k < NE))    /* (a knob, a parameter: a CC only) */
        return;
    if (ed_plearn)                                        /* (all 23 taken, none the parameter's: nothing learned) */
        mparam_bind(p >> 7 & 7u, p & 127u, src, p >> 10 ? MPARAM_ANY : ch);
    else
        mlearn_bind(k, src, ch);
    ed_learn_end();
}

static const char *ed_mlearn_name(uint32_t r)             /* a control as the MIDI LEARN page names it */
{
    if (r < NE)
        return E_NAME[r];
#if BOARD_SMK37
    return B_NAME[(r - NE) % NB];                         /* (a row: the FM-1 function) */
#else
    uint32_t b = panel_btn_of(r - NE);                    /* (a row: the matrix button) */
    return b < NB ? B_NAME[b] : "";
#endif
}

/* (a[1..3] are read before the length is known: frames sit in buffers of 64 bytes or more, and a value is used only
 * once the length says it was sent) */
static int ed_device_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t op = ed_arg(a, n, 0), x = a[1], rc = 0, i, c, at, k, m;
    char b[24], w[24];
    if (cmd == ED_SETTINGS) {
        /* [op: 0 first count (the rows), 1 row value lo hi (set)] -> op, rc, the values; 0: first, count, total, then per
         * row its kind, values (14 bits; a percentage: its top), step, name, the values' words (a list) */
        uint32_t v = a[2] | (uint32_t)a[3] << 7;
        if (n && (op > 1u || n != 3u + op))
            rc = 1;
        else if (op == 1u) {
            k = x < MI_COUNT ? menu_kind(x) : MR_PAGE;
            if (k > MR_PERCENT || (k == MR_PERCENT ? v > MR_PCT || v % 5u : v >= MR_N[x]))
                rc = 1;                                   /* (the values KNOB 1 reaches: a percentage in steps of 5) */
#if MELODEE_USB_HOST
            else if (x == MI_USB_HOST && v)
                rc = 2;                                   /* (the USB-C port would host: the editor's own link ends) */
#endif
            else {
                menu_put(x, v);
                ui.force = 1;
                rc = ed_ui_save();                        /* (as UI_SET: 0 saved, 3 not saved, 4 saved after STOP) */
            }
        }
        ed_echo(a, n, 1u); ed_b(rc);
        ed_set_values();
        if (n && !rc && !op) {
            ed_u14(x); at = ed_n; ed_u14(0); ed_u14(MI_COUNT);
            for (c = ed_page(x, a[2], MI_COUNT), i = 0; i < c; i++) {
                uint32_t r = x + i, j, e = ed_n;
                k = menu_kind(r); m = MR_N[r];
                ed_b(k); ed_u14(m); ed_b(k == MR_PERCENT ? 5u : k <= MR_CYCLE);
                ed_str(MI_NAME[r], 24);
                for (j = 0; k <= MR_CYCLE && j < m; j++) {
                    menu_words(w, menu_value(r, j, b), sizeof w);
                    ed_str(w, 24);
                }
                if (!ed_left()) { ed_n = e; break; }      /* (cut where the next row would not fit) */
            }
            ed_out[at] = (uint8_t)(i & 127u); ed_out[at + 1u] = (uint8_t)(i >> 7);
        }
    } else if (cmd == ED_MLEARN) {
        /* [op: 1 control (forget it), 2 (forget all: FORGET LEARNED, the parameters too), 3 control (learn it from the
         * next message), 4 (stop waiting), 5 track parameter [any] (learn the parameter from the next CC: on its channel, or
         * any), 6 k (forget parameter k)] -> op, rc, waiting (control + 1, 0 none), controls, knobs, per control its
         * message (kind 0 none, 1 CC, 2 note; number; channel: a note's, 127 any), the controls' names, then (v2) the
         * parameters: their count, the one waiting (track, parameter; 127 127 none), per one CC (0 none), channel (127
         * any), track, parameter */
        uint32_t id = a[2], want = op == 5u ? 3u + (n > 3u) : op == 6u || (op & 1u) ? 2u : 1u;
        const param_desc_t *pd = ed_tdesc(&trk[x % NTRK], id % P_COUNT);
        if (!n)
            ;                                             /* (the list) */
        else if (!op || op > 6u || n != want || ((op == 1u || op == 3u) && x >= MLEARN_N) || (op == 6u && x >= MPARAM_N) ||
                 (op == 5u && (x >= NTRK || !MPARAM_TARGET(id) || pd->max <= pd->min)))
            rc = 1;
        else if ((op == 3u || op == 5u) && ui.menu == 4u)
            rc = 2;                                       /* (the device's own MIDI LEARN page is open) */
        else if (op == 3u || op == 5u) {
            fm1_irq_off();
            fm1_in.remote = 0;                            /* (as the page: nothing a controller held stays down) */
            mlearn_seen = -1;
            mlearn_on = 1;
            fm1_irq_on();
            ed_learn = (uint8_t)(op == 3u ? x + 1u : 0u);   /* (one waits at a time) */
            ed_plearn = (uint16_t)(op == 5u ? 1u + (x << 7 | id | (n > 3u && a[3]) << 10) : 0u);
        } else if (op == 4u) {
            if (ed_learn || ed_plearn)
                ed_learn_end();
        } else {
            if (op == 2u)
                mlearn_forget();
            else if (op == 6u)
                mparam_row[x] &= mparam_valid() ? ~127u : ~0u;   /* (its CC 0: one word, the interrupt's own read) */
            else if (mlearn_valid()) {
                fm1_irq_off();
                mlearn_row[x] = 0;
                fm1_in.remote = 0;
                fm1_irq_on();
            }
            ui.force = 1;
            rc = ed_ui_save();
        }
        ed_echo(a, n, 1u); ed_b(rc); ed_b(ed_learn); ed_b(MLEARN_N); ed_b(NE);
        for (i = 0; i < MLEARN_N; i++) {
            uint32_t s = mlearn_valid() ? mlearn_row[i] : 0u;
            ed_b(s ? (s & MLEARN_NOTE ? 2u : 1u) : 0u); ed_b(s); ed_b(s & MLEARN_NOTE && i >= NE ? mlearn_note_ch(i - NE) : 127u);
        }
        for (i = 0; i < MLEARN_N; i++)
            ed_str(ed_mlearn_name(i), 16);
        k = ed_plearn - 1u;                               /* (v2) the parameters */
        ed_b(MPARAM_N); ed_b(ed_plearn ? k >> 7 & 7u : 127u); ed_b(ed_plearn ? k : 127u);
        for (i = 0, c = mparam_valid(); i < 4u * MPARAM_N; i++)
            ed_b(c ? ((const uint8_t *)mparam_row)[i] : 0u);
    } else if (cmd == ED_PADS) {
        /* [track] (read); [track, first, m, m x npar values] (write) -> track, rc, pads, npar, per parameter its lowest as
         * the page shows it + 64, its values' count - 1 and its starting value (the synth's starting patch: a kit's own),
         * the pads in the order KNOB 1 steps through them, per pad its
         * values, then per pad its name and its parameters' labels. A value from the parameter's lowest as the page shows
         * it: v - min, an INVERT one max - v (pads.c) */
        uint32_t tr = a[0], np = 0, k;
        const track_t *t = &trk[tr < NPART ? tr : 0];
        const pads_v_t *v = tr < NPART ? pads_view(t) : 0;
        uint32_t ns = pads_count(t);
        char lb[MOD_SCH_NAME + 1u];
        np = v ? v->npar : 0u;
        m = a[2];
        if (!n || tr >= NPART || !v || (n > 1u && (n != 3u + np * m || !m || x + m > ns)))
            rc = 1;
        for (i = 0; !rc && n > 1u && i < m; i++)
            for (k = 0; k < np; k++)
                if (a[3u + np * i + k] > (uint32_t)(v->par[k].max - v->par[k].min) || !pads_par_at(tr, x + i, k))
                    rc = 1;                               /* (a value past its range: TUNE -24..24 semitones) */
        if (!rc && n > 1u) {
            fm1_irq_off();                                /* (as the pages: DRUM SOUND's TUNE, DECAY, the third, MIX's LEVEL) */
            for (i = 0; i < m; i++)
                for (k = 0; k < np; k++) {
                    int32_t w = a[3u + np * i + k];
                    *pads_par_at(tr, x + i, k) = (int8_t)(v->par[k].flags & MOD3_PP_INVERT ? v->par[k].max - w : v->par[k].min + w);
                }
            fm1_irq_on();
            patch_note(tr, MOD3_PATCH_EDIT, 0xFFFFu);
            ui.force = 1;
        }
        ed_echo(a, n, 1u); ed_b(rc);
        ns = rc ? 0u : ns;
        ed_b(ns);
        ed_b(rc ? 0u : np);
        if (!rc) {
            static uint8_t def[MOD3_PATCH_MAX];
            patch_default(eng_idx(t->eng_req), def);
            for (k = 0; k < np; k++) {
                int32_t w = (int8_t)def[v->par[k].off], s = v->par[k].flags & MOD3_PP_SIGNED ? w : (int32_t)(uint8_t)w;
                ed_b((uint32_t)((v->par[k].flags & MOD3_PP_INVERT ? 0 : v->par[k].min + v->par[k].add) + 64));
                ed_b((uint32_t)(v->par[k].max - v->par[k].min));
                ed_b((uint32_t)(v->par[k].flags & MOD3_PP_INVERT ? v->par[k].max - s : s - v->par[k].min));
            }
        }
        for (i = 0; i < ns; i++)
            ed_b(v->order[i]);
        for (i = 0; i < ns; i++)
            for (k = 0; k < np; k++) {
                const int8_t *q = pads_par_at(tr, i, k);
                int32_t w = q ? pads_par_shown(v, k, q) : 0;
                ed_b((uint32_t)(v->par[k].flags & MOD3_PP_INVERT ? w : w - v->par[k].min - v->par[k].add));
            }
        for (i = 0; i < ns; i++) {
            ed_str(pads_name(t, i), 8);
            for (k = 0; k < np; k++)
                ed_str(pads_par_label(t, i, k, lb), 8);
        }
    } else
        return 0;
    ed_dev_known();
    return 1;
}
