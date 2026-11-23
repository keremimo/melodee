/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Slot pages (docs/design: mock/r6_popups PROJECT, README "Popups"): PROJECT, SAVE > USER and a synth's STORE page (FM6's,
 * the Prophet's: MOD3_W_STORE) as the list of their slots alone, no action chips. KNOB 1 / 2 the slot, OCT+ its sheet (the page's own actions: act_do
 * in ui_input.c does them, as the chips did), OCT- Stage. The slot picked is a card: its name larger, a line about it,
 * PROJECT's Boot chip on the one power-on loads. PROJECT's last row, New song: OCT+ starts it. Included by ui_draw.c */
enum { SK_NONE, SK_PROJECT, SK_USER, SK_STORE,
       SK_SYNTH, SK_FLASH };                            /* GLO > SYNTHS (the synths installed, their RAM), STORAGE (what
                                                         * the flash keeps): their first row the room left */
#define SL_Y0 Y_LABEL                                   /* the rows from y 32 (large type): 34 px, the card 58 */
#define SL_ROW 34
#define SL_CARD 58
static struct { uint32_t sig; uint16_t first; uint8_t save_page, syn, fl; } slv;   /* save_page: USER reached by SAVE
                                                                  * (+1): its sheet on Save here; syn, fl: SYNTHS',
                                                                  * STORAGE's row */
static void name_rename(void);                          /* ui_name.c */
static void act_do(void);                               /* ui_input.c */
/* STORAGE's rows (storage_items.c, after the stores): row 0 the room left, then each thing kept, kind by kind */
enum { STI_FREE, STI_PROJECT, STI_KIT, STI_SOUNDS, STI_SYNTH, STI_SYSTEM };   /* (the template: PROJECT's) */
static uint32_t sti_count(void);                        /* the rows */
static uint32_t sti_at(uint32_t r);                     /* row r: its kind << 8 | its index */
static uint32_t sti_bytes(uint32_t it);                 /* the flash it takes (whole sectors); STI_FREE: the room left */
static void sti_label(uint32_t it, char *tag, char *nm);   /* its tag (5) and name (24) */
static void sti_line(uint32_t it, char *b);             /* the card's line (24) */
static int sti_erasable(uint32_t it);
static void sti_ask(uint32_t it);                       /* its question (after the checks), or why not */
static void sl_kb(char *e, uint32_t bytes)              /* "35 KB" at e */
{
    fmt_int(e, (int32_t)((bytes + 1023u) / 1024u));
    str_cpy(e + str_len(e), " KB", 4);
}

/* ---------------------------------------------- SYNTHS: the synth modules --- */
#if MELODEE_MODULES
static uint32_t syn_count(void)                         /* the rows: RAM, then each slot holding a file */
{
    uint32_t n = 1;
    for (uint32_t k = 0; k < NMOD; k++)
        n += mods[k].len != 0;
    return n;
}
static uint32_t syn_slot(uint32_t r)                    /* row r's slot (r >= 1), NMOD none */
{
    for (uint32_t k = 0; k < NMOD; k++)
        if (mods[k].len && !--r)
            return k;
    return NMOD;
}
static uint32_t syn_parts(uint32_t k)                   /* the tracks playing slot k's synth (a bit each) */
{
    uint32_t m = 0, n = mods[k].engine;
    if (k >= NMOD || !mod_listed(k))
        return 0;
    for (uint32_t p = 0; p < NPART && p < trk_n(); p++)
        if (trk[p].eng_req == n || trk[p].engine == n)
            m |= 1u << p;
    return m;
}
static uint32_t syn_ram(void)                           /* the synths' SRAM in use */
{
    uint32_t n = 0;
    for (uint32_t k = 0; k < NMOD; k++)
        if (mods[k].state == MS_LOADED)
            n += resource[RES_MODULE0 + k].size;
    return n;
}
static void syn_label(uint32_t k, char *tag, char *nm)
{
    uint32_t i;
    for (i = 0; i < 4u && mods[k].tag[i]; i++)
        tag[i] = mods[k].tag[i];
    tag[i] = 0;
    str_cpy(nm, mods[k].name[0] ? mods[k].name : "Synth", 17);
    if (!tag[0])
        str_cpy(tag, nm, 5);
}
static void syn_line(uint32_t k, char *b)               /* the card's line (a sheet's too: 19 at most) */
{
    switch (mods[k].state) {
    case MS_LOADED: str_cpy(b, "In RAM, ", 24); sl_kb(b + 8, resource[RES_MODULE0 + k].size); return;
    case MS_STORED: str_cpy(b, "Not loaded, ", 24); sl_kb(b + 12, mods[k].need); return;
    case MS_NORAM:                                      /* "Needs 35 KB RAM" (RAM's row: the room there is) */
        str_cpy(b, "Needs ", 24);
        sl_kb(b + 6, mods[k].need);
        str_cpy(b + str_len(b), " RAM", 5);
        return;
    case MS_SKIPPED: str_cpy(b, "Left out at start", 24); return;
    case MS_STALE: str_cpy(b, "For older Melodee", 24); return;
    case MS_NEWER: str_cpy(b, "Needs newer Melodee", 24); return;
    case MS_CLASH: str_cpy(b, "Another has its ID", 24); return;
    default: str_cpy(b, "Damaged file", 24); return;
    }
}
static int syn_trouble(uint32_t k) { return mods[k].state != MS_LOADED && mods[k].state != MS_STORED; }
#endif

static uint32_t slot_kind(void)
{
    const page_t *pg;
    if (ui.home || ui.menu || ui.layer)
        return SK_NONE;
    pg = cur_page();
    return pg->graph == GR_SLOTS ? SK_PROJECT : pg->graph == GR_USER ? SK_USER : pg->graph == GR_STORE ? SK_STORE :
#if MELODEE_MODULES
           pg->graph == GR_SYNTHS ? SK_SYNTH :
#endif
           pg->graph == GR_FLASH ? SK_FLASH : SK_NONE;
}
static int slot_bulk(uint32_t k) { return k == SK_USER || k == SK_STORE; }   /* (presets: a range picked, SysEx in) */
static uint32_t slot_count(uint32_t k)
{
#if MELODEE_MODULES
    if (k == SK_SYNTH) return syn_count();
#endif
    return k == SK_FLASH ? sti_count() : k == SK_PROJECT ? (uint32_t)PROJ_TMPL + 1u : UP_SLOTS;
}
static uint32_t slot_cur(uint32_t k)                    /* the row picked */
{
    switch (k) {
    case SK_PROJECT: return ui.proj_new ? (uint32_t)PROJ_TMPL : (uint32_t)song.g[G_SLOT] - 1u;
    case SK_USER: return ui.uslot % UP_SLOTS;
    case SK_SYNTH:
    case SK_FLASH: {                                    /* (kept in range: rows come and go) */
        uint8_t *c = k == SK_SYNTH ? &slv.syn : &slv.fl;
        uint32_t n = slot_count(k);
        if (*c >= n) *c = (uint8_t)(n - 1u);
        return *c;
    }
    default: return sch_store_slot[eng_idx(TSEL->eng_req)] % UP_SLOTS;   /* (a synth's STORE page) */
    }
}
static uint32_t slot_boot(void)                         /* the row power-on loads (+1), 0 none */
{
    return settings_boot && graph_project_used(settings_boot - 1u) ? settings_boot
           : template_used() ? (uint32_t)PROJ_TMPL : 0u;
}
/* row r: its tag ("A", "T", "+", "U07", "F012"), its name (24), used (2: New song, always) */
static int slot_row(uint32_t k, uint32_t r, char *tag, char *nm)
{
#if MELODEE_MODULES
    if (k == SK_SYNTH) {                                /* RAM: "22 KB free"; a synth: its tag, its name */
        if (!r) {
            str_cpy(tag, "RAM", 6);
            sl_kb(nm, resource_code_room());
            str_cpy(nm + str_len(nm), " free", 6);
            return 1;
        }
        syn_label(syn_slot(r), tag, nm);
        return 1;
    }
#endif
    if (k == SK_FLASH) {
        sti_label(sti_at(r), tag, nm);
        return 1;
    }
    if (k == SK_PROJECT) {
        int used;
        if (r == (uint32_t)PROJ_TMPL) {
            str_cpy(tag, "+", 4);
            str_cpy(nm, "New song", 16);
            return 2;
        }
        if (r + 1u == (uint32_t)PROJ_TMPL) { tag[0] = 'T'; tag[1] = 0; }
        else { tag[0] = (char)('0' + (r + 1u) / 10u); tag[1] = (char)('0' + (r + 1u) % 10u); tag[2] = 0; }
        used = r + 1u == (uint32_t)PROJ_TMPL ? template_used() || template_old() : graph_project_used(r);
        str_cpy(nm, r + 1u == (uint32_t)PROJ_TMPL ? (template_old() ? "OLD FORMAT" : "Template") : used && graph_project_name(r)[0] ? graph_project_name(r)
                    : used ? "Saved" : "--", 16);
        return used;
    }
    up_slot_label(tag, r);
    if (!up_used(r) && !up_old(r)) {
        str_cpy(nm, "--", 16);
        return 0;
    }
    up_name(r, nm);
    return 1;
}
static void slot_detail(uint32_t k, uint32_t r, int used, char *b)   /* the card's second line */
{
#if MELODEE_MODULES
    if (k == SK_SYNTH && !r) {                          /* "Synths use 80 KB" */
        str_cpy(b, "Synths use ", 24);
        sl_kb(b + 11, syn_ram());
    } else if (k == SK_SYNTH)
        syn_line(syn_slot(r), b);
    else
#endif
    if (k == SK_FLASH)
        sti_line(sti_at(r), b);
    else if (k == SK_PROJECT) {
        int pct = project_free_pct();
        str_cpy(b, used == 2 ? "Starts empty" : r + 1u == (uint32_t)PROJ_TMPL ? (used ? "Saved" : "Empty")
                   : used ? "Saved" : "Empty", 24);
        if (used != 2 && r + 1u != (uint32_t)PROJ_TMPL && pct >= 0) {   /* the shared pool's room left, in KB */
            uint32_t n = str_len(b);
            char *e = b + n;
            if (!pct) str_cpy(e, ", full", 24 - n);
            else {
                *e++ = ','; *e++ = ' ';
                if (pct > 999) pct = 999;
                if (pct > 99) *e++ = (char)('0' + pct / 100);
                if (pct > 9) *e++ = (char)('0' + pct / 10 % 10);
                *e++ = (char)('0' + pct % 10);
                str_cpy(e, " KB free", 9);
            }
        }
    }
    else if (!used)
        str_cpy(b, "Empty", 24);
    else
        str_cpy(b, eng_name(eng_idx(up_engine(r))), 24);
}

/* ------------------------------------------------------- the sheets --- */
static void sl_do(uint32_t col)                         /* the page's action in column col (act_do), done now */
{
    ui.act = (uint8_t)(col + 1u);
    act_do();
    ui.act = 0;
}
/* the columns (act_do): PROJECT SLOT BOOT LOAD SAVE; USER SLOT LOAD ERASE SAVE; the STOREs SLOT STORE SEND INIT */
static void sl_load(void) { sl_do(slot_kind() == SK_PROJECT ? 2u : 1u); }
static void sl_save(void) { sl_do(slot_kind() == SK_PROJECT || slot_kind() == SK_USER ? 3u : 1u); }
static void sl_erase(void)
{
    uint32_t r = slot_cur(SK_PROJECT);
    if (slot_kind() != SK_PROJECT)
        sl_do(2u);                                      /* (USER: act_do asks ERASE U07?) */
    else if (!graph_project_used(r))
        ui_message("EMPTY SLOT");
    else if (transport_busy())
        ui_message("STOP TO SAVE");
    else
        confirm_open(CF_ERASE_PROJ, r);
}
static void sl_send(void) { sl_do(2u); }
static void sl_init(void) { sl_do(3u); }
static void sl_boot_v(char *b) { str_cpy(b, settings_boot == song.g[G_SLOT] ? "On" : "Off", 8); }
static void sl_boot(void)
{
    settings_boot = (uint8_t)(settings_boot == song.g[G_SLOT] ? 0 : song.g[G_SLOT]);
    settings_save();
    ui.force = 1;
}
static const sheet_row_t SHEET_PROJECT[] = {{"Load", 0, 0, sl_load, 0}, {"Save here", 0, 0, sl_save, 0},
    {"Rename", 0, 0, name_rename, 0}, {"Boot", sl_boot_v, 0, sl_boot, 0}, {"Erase", 0, 0, sl_erase, CF_ERASE_PROJ}};
static const sheet_row_t SHEET_TEMPLATE[] = {{"Load template", 0, 0, sl_load, 0}, {"Save as template", 0, 0, sl_save, 0}};
static const sheet_row_t SHEET_IMPORT[] = {{"Import here",0,0,sysex_choose,0},{"Cancel import",0,0,sysex_cancel,0}};
static const sheet_row_t SHEET_BULK[] = {{"Send selected",0,0,bulk_send,0},{"Erase selected",0,0,bulk_erase_ask,CF_BULK_ERASE},{"Clear selection",0,0,bulk_clear,0}};
static const sheet_row_t SHEET_USER[] = {{"Load", 0, 0, sl_load, 0}, {"Save here", 0, 0, sl_save, 0},
    {"Rename", 0, 0, name_rename, 0}, {"Send",0,0,library_send_current,0}, {"Erase", 0, 0, sl_erase, CF_ERASE_USER}};
static const sheet_row_t SHEET_STORE[] = {{"Save here", 0, 0, sl_save, 0}, {"Send", 0, 0, sl_send, 0},
    {"Init sound", 0, 0, sl_init, 0}};
/* SYNTHS / STORAGE: a row's Delete (its question, after the checks: sti_ask) */
static void sl_delete(void)
{
    uint32_t k = slot_kind();
#if MELODEE_MODULES
    if (k == SK_SYNTH) {
        sti_ask(STI_SYNTH << 8 | syn_slot(slot_cur(k)));
        return;
    }
#endif
    sti_ask(sti_at(slot_cur(k)));
}
static const sheet_row_t SHEET_DELETE[] = {{"Delete", 0, 0, sl_delete, SR_RED}};

/* OCT+ on a slot: its sheet (New song: starts it) */
static void slot_enter(void)
{
    uint32_t k = slot_kind(), r;
    char tag[6], nm[24], t[24];
    int used;
    if (!k)
        return;
    r = slot_cur(k);
    if(k==SK_USER && sx_import.count){sysex_import_name(nm);sheet_open("Import SysEx",nm,SHEET_IMPORT,NELEM(SHEET_IMPORT));return;}
    if(slot_bulk(k) && bulk.on){sheet_open("Selected presets","USB SysEx / erase",SHEET_BULK,NELEM(SHEET_BULK));return;}
    if (k == SK_SYNTH || k == SK_FLASH) {               /* a synth, a thing kept: Delete (the room row: nothing) */
        uint32_t it = 0;
        if (!r)
            return;
#if MELODEE_MODULES
        if (k == SK_SYNTH)
            it = STI_SYNTH << 8 | syn_slot(r);
        else
#endif
            it = sti_at(r);
        if (!sti_erasable(it))
            return;
        sti_label(it, tag, nm);
        str_cpy(t, tag, sizeof t);
        str_cpy(t + str_len(t), " \xB7 ", 4);
        str_cpy(t + str_len(t), nm, sizeof t - str_len(t));
        slot_detail(k, r, 1, nm);
        sheet_open(t, nm, SHEET_DELETE, NELEM(SHEET_DELETE));
        return;
    }
    used = slot_row(k, r, tag, nm);
    if (k == SK_PROJECT && r < PROJ_SLOTS) {            /* (the name as stored now, not the list's cached one) */
        used = project_name(r, nm);
        if (used && !nm[0])
            str_cpy(nm, "Saved", 16);
    }
    if (used == 2) {
        sl_do(2u);                                      /* (act_do: LOAD with NEW picked) */
        return;
    }
    str_cpy(t, tag, sizeof t);
    str_cpy(t + str_len(t), " \xB7 ", 4);
    str_cpy(t + str_len(t), used ? nm : "Empty", sizeof t - str_len(t));
    if (k == SK_PROJECT && r + 1u == (uint32_t)PROJ_TMPL)
        sheet_open(t, used ? "saved" : "empty", SHEET_TEMPLATE, NELEM(SHEET_TEMPLATE));
    else if (k == SK_PROJECT)
        sheet_open(t, used ? "saved" : "empty", SHEET_PROJECT, NELEM(SHEET_PROJECT));
    else if (k == SK_USER)
        sheet_open(t, used ? "used" : "empty", SHEET_USER, NELEM(SHEET_USER));
    else
        sheet_open(t, used ? "used" : "empty", SHEET_STORE, NELEM(SHEET_STORE));
    if (k == SK_USER && slv.save_page == ui.page + 1u)  /* (reached by SAVE: on Save here) */
        pop.sel = 1;
    slv.save_page = 0;
}
/* SAVE, the sound sheet's Save as user..: SAVE > USER shown, nothing popped up (Kerem, 2026-10-10); its slot's sheet,
 * when OCT+ opens it, on Save here */
static void slot_save_sheet(void)
{
    slv.save_page = (uint8_t)(ui.page + 1u);
}

/* ----------------------------------------------------------- drawing --- */
/* row r's mark at the right: PROJECT's Boot; SYNTHS: the tracks playing it, a digit each in its track's colour (*parts);
 * STORAGE: the flash it takes. Its width (0: none) */
static int32_t slot_right(uint32_t k, uint32_t r, uint32_t boot, char *b, uint32_t *parts)
{
    const aafont_t *lf = TY_LB;
    *parts = 0;
    b[0] = 0;
    if (boot == r + 1u)
        str_cpy(b, "Boot", 8);
#if MELODEE_MODULES
    else if (k == SK_SYNTH && r) {
        uint32_t m = syn_parts(syn_slot(r)), n = 0;
        *parts = m;
        for (uint32_t p = 0; p < NPART; p++)
            if (m >> p & 1u) { b[n++] = (char)('1' + p); b[n++] = ' '; }
        b[n ? n - 1u : 0u] = 0;
    }
#endif
    else if (k == SK_FLASH && r)
        sl_kb(b, sti_bytes(sti_at(r)));
    return b[0] ? text_w(lf, b) + 10 : 0;
}
static void slot_right_draw(int32_t y, const char *b, uint32_t parts, uint16_t c, uint16_t bg)
{
    const aafont_t *lf = TY_LB;
    if (!parts) {
        cv_text_r(228, ty_y(y, lf), lf, b, c, bg);
        return;
    }
    int32_t x = 228 - text_w(lf, b);                    /* the tracks' digits, each in its colour */
    for (uint32_t i = 0; b[i]; i++) {                   /* ("1 2 3": a step of 2 ran past the last digit's end) */
        if (b[i] == ' ')
            continue;
        char d[2] = {b[i], 0};
        cv_text_on(x, ty_y(y, lf), lf, d, T_TRK((uint32_t)(b[i] - '1')), bg);
        x += text_w(lf, d) + text_w(lf, " ");
    }
}
/* a row's tag colour: STORAGE's by its kind (the tracks' colours: projects, kits, sounds, synths), a synth in trouble
 * red, the room rows the theme's */
static uint16_t slot_tag_c(uint32_t k, uint32_t r, uint16_t c)
{
    if ((k == SK_SYNTH || k == SK_FLASH) && !r)
        return T_THEME;
#if MELODEE_MODULES
    if (k == SK_SYNTH)
        return syn_trouble(syn_slot(r)) ? T_REC : c;
#endif
    if (k == SK_FLASH) {
        uint32_t kind = sti_at(r) >> 8;
        return kind == STI_SYSTEM ? T_MID : T_TRK(kind == STI_PROJECT ? 0u : kind == STI_KIT ? 1u
                                                  : kind == STI_SOUNDS ? 3u : 2u);
    }
    return c;
}
static void slot_draw(void)
{
    uint32_t k = slot_kind(), n = slot_count(k), cur = slot_cur(k), boot = k == SK_PROJECT ? slot_boot() : 0u;
    uint32_t vis = 1u + (uint32_t)((PV_SOUND_Y - 2 - SL_Y0 - SL_CARD) / SL_ROW), r, sig, parts;
    const aafont_t *tf = TY_TX, *lf = TY_LB;
    int32_t y, nx = 12 + text_w(tf, k == SK_PROJECT ? "88" : "F000") + 10;   /* the names after the widest tag */
    char tag[6], nm[24], rt[16], d[24];
    int bulky = slot_bulk(k);
    if (ui.force) {
        lcd_fill(0, H_HEAD, 240, PV_SOUND_Y - H_HEAD, T_BG);
        ui.graph_sig = 0;
    }
    draw_head();
    pv_sound();
    if (vis > n)
        vis = n;
    slv.first = (uint16_t)clamp((int32_t)cur - (int32_t)vis / 2, 0, (int32_t)(n - vis));
    sig = cur * 7u + slv.first * 131u + boot * 1031u + k * 40503u + ux.gen * 977u + ux.pal * 31u + song.sel * 13u +
          up_gen * 7919u + library_engine() * 104729u + bulk.on * 19u + bulk.anchor * 23u + bulk.end * 29u + sx_import.count * 37u;
    for (r = slv.first; r < slv.first + vis; r++) {
        sig = str_hash(sig + (uint32_t)slot_row(k, r, tag, nm), nm);
        if (k == SK_SYNTH || k == SK_FLASH) {           /* (their rows change by themselves: loads, room, tracks) */
            slot_detail(k, r, 1, d);
            sig = str_hash(str_hash(sig, d), tag) + slot_tag_c(k, r, 0);
            (void)slot_right(k, r, boot, rt, &parts);
            sig = str_hash(sig, rt);
        }
    }
    if (!ui.force && sig == slv.sig)
        return;
    slv.sig = sig;
    for (r = slv.first, y = SL_Y0; r < slv.first + vis; r++) {
        /* (every lookup before the canvas: a store's may borrow its RAM, as a host build's project check does) */
        int used = slot_row(k, r, tag, nm), on = r == cur;
        int32_t h = on ? SL_CARD : SL_ROW, rw = slot_right(k, r, boot, rt, &parts);
        uint16_t tc = slot_tag_c(k, r, on ? T_THEME : used ? T_MID : T_DIM);
        if (on) {
            slot_detail(k, r, used, d);
            if(bulky && sx_import.count){fmt_int(d,sx_import.count);str_cpy(d+str_len(d)," incoming patches",20);}
            else if(bulky && bulk_has(r))str_cpy(d,"Selected",24);
        }
        cv_begin(240, (uint32_t)h, T_BG);
        if (on) {                                       /* the card: tinted in the track's colour, a line about it */
            uint16_t bg = ux_mix(T_BG, T_THEME, 18);
            int32_t nw = 236 - nx - 8 - rw;
            cv_rrect(BOX_X, 0, BOX_W, h - 2, 8, bg, T_BG);
            cv_text_on(12, ty_y(17, tf), tf, tag, tc, bg);
            cv_free_text(nx, ty_y(17, ty_fit(tf, nm, nw)), ty_fit(tf, nm, nw), used ? nm : "Empty", used ? T_TEXT : T_MID, bg, nw);
            cv_free_text(nx, ty_y(41, lf), lf, d, T_MID, bg, 236 - nx - 8);
            if (rw)
                slot_right_draw(17, rt, parts, boot == r + 1u ? T_THEME : T_SEC, bg);
        } else {
            if(bulky && bulk_has(r))cv_rect(BOX_X,6,3,SL_ROW-14,T_THEME);
            cv_text_on(12, ty_y(SL_ROW / 2 - 1, tf), tf, tag, tc, T_BG);
            {
                int32_t nw = 228 - nx - (rw ? rw - 2 : 0);
                const aafont_t *f = ty_fit(tf, nm, nw);
                cv_free_text(nx, ty_y(SL_ROW / 2 - 1, f), f, nm, used ? T_SEC : T_DIM, T_BG, nw);
            }
            if (rw)
                slot_right_draw(SL_ROW / 2 - 1, rt, parts, T_MID, T_BG);
        }
        if (n > vis) {                                  /* where the rows are in the list: a bar at the right edge */
            int32_t span = (int32_t)(SL_CARD + (vis - 1u) * SL_ROW), th = span * (int32_t)vis / (int32_t)n;
            int32_t ty = (span - th) * slv.first / (int32_t)(n - vis) - (y - SL_Y0);
            cv_rect(238, 0, 2, h, T_LINE);
            if (ty < h && ty + th > 0)
                cv_rect(238, ty, 2, th, T_THEME);
        }
        cv_blit(0, (uint32_t)y);
        y += h;
    }
}
