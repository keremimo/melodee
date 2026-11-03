/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Settings > MIDI LEARN (midi_knobs.c): the panel's controls as rows, the list following the one last touched. A button
 * pressed or a knob turned here does nothing but wait for a message, then that control is the message's, until learned
 * again or FORGET LEARNED; a message with nothing waiting shows the control it has. HOME held leaves, as every menu. */
_Static_assert(MLEARN_KNOBS == NE && MLEARN_BUTTONS == NB, "mlearn_row: a byte per control");
static uint8_t mlearn_sel, mlearn_wait;                 /* the row; it waits for a message */
static uint32_t mlearn_sig;

static uint32_t mlearn_count(void)
{
    uint32_t n = 0;
    if (mlearn_valid())
        for (uint32_t k = 0; k < MLEARN_N; k++) n += mlearn_row[k] != 0u;
    return n;
}
static void mlearn_page_open(void)
{
    ui.menu = 4;
    fm1_irq_off();
    fm1_in.remote = 0;                                  /* (nothing a controller held stays down) */
    mlearn_seen = -1;
    mlearn_on = 1;
    fm1_irq_on();
    mlearn_sel = mlearn_wait = 0;
    ui.force = 1;
}
static void mlearn_forget(void)
{
    fm1_irq_off();
    memset(mlearn_row, 0, sizeof mlearn_row);
    fm1_in.remote = 0;
    fm1_irq_on();
    settings_save();
}
static void mlearn_bind(uint32_t k, uint32_t src, uint32_t ch)
{
    fm1_irq_off();
    if (!mlearn_valid()) {                              /* (Felucca 1.0's stars of a retired engine, if any, go) */
        memset(mlearn_row, 0, sizeof mlearn_row);
        mlearn_row[28] = 'M'; mlearn_row[29] = 'L'; mlearn_row[30] = 1u;
    }
    for (uint32_t i = 0; i < MLEARN_N; i++)             /* the message leaves the control it had */
        if (mlearn_row[i] == src && (!(src & MLEARN_NOTE) || (i >= NE && mlearn_note_ch(i - NE) == ch)))
            mlearn_row[i] = 0;
    mlearn_row[k] = (uint8_t)src;
    if (k >= NE) {
        uint32_t b = k - NE, shift = b & 1u ? 4u : 0u;
        uint8_t *c = &mlearn_row[MLEARN_N + b / 2u];
        *c = (uint8_t)((*c & ~(15u << shift)) | ch << shift);
    }
    fm1_irq_on();
    mlearn_wait = 0;
    settings_save();
}
static void mlearn_page_input(uint32_t pressed)
{
    int32_t seen = mlearn_seen;
    if (seen >= 0) {                                    /* (first: a message from before a touch is not its answer) */
        uint32_t src = (uint32_t)seen & 255u, ch = (uint32_t)seen >> 8;
        mlearn_seen = -1;
        if (mlearn_wait) {
            if (mlearn_sel >= NE || !(src & MLEARN_NOTE))   /* (a knob: a CC only) */
                mlearn_bind(mlearn_sel, src, ch);
        } else if (mlearn_valid()) {
            for (uint32_t k = 0; k < MLEARN_N; k++)
                if (mlearn_row[k] == src && (!(src & MLEARN_NOTE) || (k >= NE && mlearn_note_ch(k - NE) == ch)))
                    mlearn_sel = (uint8_t)k;
        }
    }
    for (uint32_t k = 0; k < NE; k++)                   /* (the panel's own knobs: a controller's are messages) */
        if (fm1_enc_take(panel.enc[k])) {
            mlearn_sel = (uint8_t)k;
            mlearn_wait = 1;
            panel_moved = 1;
        }
#if BOARD_SMK37
    for (uint32_t id = 0; id < FM1_NBTN; id++)        /* (a row is the FM-1 function; the SMK's own four have none) */
        if (((pressed >> id) & 1u) && panel_btn_of(id) < NB) {
            mlearn_sel = (uint8_t)(NE + panel_btn_of(id));
            mlearn_wait = 1;
        }
#else
    for (uint32_t id = 0; id < NB; id++)
        if ((pressed >> id) & 1u) {
            mlearn_sel = (uint8_t)(NE + id);
            mlearn_wait = 1;
        }
#endif
}
#if BOARD_SMK37
static uint32_t mlearn_btn_bit(uint32_t b) { return 1u << panel.btn[b % NB]; }
#define MLEARN_LABEL(b) B_NAME[(b) % NB]
#else
#define MLEARN_LABEL(b) B_NAME[panel_btn_of(b)]
#endif

static int mlearn_cap(uint32_t r, char *lb, char *v, uint16_t *vc)
{
    uint32_t src = mlearn_valid() ? mlearn_row[r] : 0u;
    str_cpy(lb, r < NE ? E_NAME[r] : MLEARN_LABEL(r - NE), 16);
    *vc = T_TEXT;
    v[0] = 0;
    if (mlearn_wait && r == mlearn_sel) {
        str_cpy(v, r < NE ? "Send a CC" : "CC or note", 16);
    } else if (src) {
        str_cpy(v, src & MLEARN_NOTE ? "Note " : "CC ", 16);
        fmt_int(v + str_len(v), src & 127u);
    }
    return 0;
}
static void draw_mlearn_page(void)
{
    uint32_t sig = mlearn_sel * 131u + mlearn_wait * 1009u + song.rec * 65537u;
    for (uint32_t k = 0; k < sizeof mlearn_row; k++) sig = sig * 31u + mlearn_row[k];
    if (!ui.force && sig == mlearn_sig) {
        if (head_shared_status()) draw_head();
        return;
    }
    mlearn_sig = sig;
    menu_head();
    list_rows(MLEARN_N, mlearn_sel, mlearn_sel >= MENU_VISIBLE ? mlearn_sel - MENU_VISIBLE + 1u : 0u, MENU_VISIBLE, mlearn_cap);
}
