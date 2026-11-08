/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6: six-operator FM that plays DX7 voices the way Dexed plays them. The synthesis is fm6_core.c; this file
 * is the Melodee engine around it.
 *
 * The patch is the sound: every track has one (fm6_patch, the 155-byte single-voice layout: six operators with
 * their 4-rate / 4-level envelopes, keyboard level and rate scaling, velocity, ratio or fixed frequency,
 * detune; 32 algorithms, feedback, LFO, pitch envelope, transpose). It is edited on the device (the FM6 pages),
 * in the web editor (EDITOR_PROTOCOL.md FM6_*) and over USB-MIDI as DX7 SysEx (fm6_store.c), loaded from the
 * PATCH slots and saved inside projects. The FM6 function settings (bend, portamento, the controllers, ENGINE:
 * fm6_core.c fm6_fn) are the track's, as each Dexed instance has its own, saved with the project and the
 * template (project.c), edited on the FM pages and by DX7 function SysEx. On the device the seven EDIT
 * values are macros on top of the patch, neutral at 0 (then the native patch is unmodified):
 *   ALG   PAT = the patch's algorithm, 1..32 another one
 *   FB    added to the patch's feedback (0..7)
 *   MLVL  the output level of every operator that is not a carrier (-36 .. +36 dB): the brightness
 *   MRAT  added to the coarse ratio of the modulators (ratio mode only)
 *   MEG   the modulators' envelope times: + slower (rates down to 40 steps), - faster
 *   VMOD  added to the modulators' velocity sensitivity (0..7)
 *   DTUN  spreads the carriers apart in pitch (up to about +-36 cents between the outer ones)
 * User presets and projects retain the whole voice; the preset browser loads factory voices.
 * The operator envelopes are the voice's amplitude and end it (engine_t.ownenv / done): the track's ADSR, ENV
 * DEST and the matrix's ENV do nothing here. The track's FLT moves MLVL (ENV / LFO -> FLT, the matrix's CUT), SHP
 * the feedback, PIT the pitch (glide, LFO and ENV pitch, unison detune, the matrix's: vmod_t.plog).
 *
 * Voices are chosen and handed over as Dexed does (16 of them, engine_t.alloc / legato / mono_key); a voice that
 * has ended keeps its control path running as Dexed keeps its voices computing (fm6_ghost), so its next note
 * starts where Dexed's would. Their state lives in the part's engine state (engines.c eng_state).
 * The part's patch is its module's bytes in the firmware (modules/fm6/fm6_patch.h: the voice packed, the
 * functions, the switches; mod_patch); this engine is a synth module on the device (firmware/modules/fm6), built in
 * on the host (MELODEE_FM6_BUILTIN). */
#include "fm6_patch.h"
#if !MOD_SIDE
#include "fm6_presets.c"   /* its factory voices (host builds: the module's file has them) */
#endif
#include "fm6_core.c"
static volatile uint8_t fm6_pgen[NPART];         /* +1 after each write of a part's patch (fm6_patch_changed) */
static volatile uint8_t fm6_lgen[NPART];         /* +1 when the write was another voice (a load): the notes stop */
static struct {                                  /* the patch through the macros: the audio ISR's copy */
    uint8_t raw[FP_SIZE + 1u];                   /* the patch's voice unpacked (when fm6_pgen moved) */
    uint8_t p[FP_SIZE + 1u];
    uint8_t gen, ok, on;                         /* (on: the patch's switches as raw was made) */
    int16_t e[7];                                /* P_E0..P_E6 it was made with */
    int32_t dt[6];                               /* DTUN: per operator, Q24 log2 */
} fm6_eff[NPART];
#undef fm6_op_on                                 /* (the switches the voices play: the copy the block made) */
#define fm6_op_on(t, k) ((fm6_eff[trk_ix(t) % NPART].on >> (k)) & 1u)

/* -------------------------------------------------------------- macros --- */
/* the patch through E0..E6 (audio ISR; cheap when nothing changed). With every macro at 0 it is the patch.
 * 1 = made again (the patch or a macro changed) */
static int fm6_eff_build(const track_t *t)
{
    uint32_t tr = trk_ix(t), k, j, car, n = 0;
    const int16_t *e = &t->p[P_E0];
    int32_t meg, step;
    if (tr >= NPART)
        return 0;
    if (fm6_eff[tr].ok && fm6_eff[tr].gen == fm6_pgen[tr]) {
        for (k = 0; k < 7u && fm6_eff[tr].e[k] == e[k]; k++)
            ;
        if (k == 7u)
            return 0;
    }
    if (!fm6_eff[tr].ok || fm6_eff[tr].gen != fm6_pgen[tr]) {
        const uint8_t *b = mod_patch(t);
        fm6_eff[tr].gen = fm6_pgen[tr];
        fm6_unpack(b, fm6_eff[tr].raw);              /* (sanitized: the voice as fm6_put_patch kept it) */
        fm6_eff[tr].on = b[FM6_B_ON] & FM6_ON_ALL;
    }
    memcpy(fm6_eff[tr].p, fm6_eff[tr].raw, FP_SIZE);
    for (k = 0; k < 7u; k++)
        fm6_eff[tr].e[k] = e[k];
    {
        uint8_t *p = fm6_eff[tr].p;
        uint32_t alg = e[0] >= 1 && e[0] <= 32 ? (uint32_t)e[0] - 1u : p[FP_ALG] & 31u;
        p[FP_ALG] = (uint8_t)alg;
        p[FP_FB] = (uint8_t)clamp(p[FP_FB] + e[1], 0, 7);
        car = fm6_carriers(alg);
        meg = clamp(e[4], -64, 63) * 40 / 64;               /* MEG: rate steps (+ slower) */
        step = clamp(e[6], 0, 127) * (12 * 13981) / 127;    /* DTUN: up to 12 cents a step (1 cent = 13981) */
        for (k = 0; k < 6u; k++) {
            uint8_t *op = p + (5u - k) * FP_OP;              /* OP1 first: carriers in their order */
            uint32_t ki = 5u - k;
            fm6_eff[tr].dt[ki] = 0;
            if ((car >> ki) & 1u) {                          /* carriers 0, +1, -1, +2, -2, +3 steps apart */
                static const int8_t SPREAD[6] = {0, 1, -1, 2, -2, 3};
                fm6_eff[tr].dt[ki] = SPREAD[n++] * step;
                continue;
            }
            if (!op[FP_MODE])
                op[FP_FC] = (uint8_t)clamp(op[FP_FC] + e[3], 0, 31);
            for (j = 0; j < 4u; j++)
                op[FP_R1 + j] = (uint8_t)clamp(op[FP_R1 + j] - meg, 0, 99);
            op[FP_KVS] = (uint8_t)clamp(op[FP_KVS] + e[5], 0, 7);
        }
    }
    fm6_eff[tr].ok = 1;
    return 1;
}

/* ----------------------------------------------- the part (Dexed's) --- */
typedef struct {
    uint32_t ph, dly;                                    /* LFO phase; delay ramp (DX7 two-slope) */
    int32_t val, depth;                                  /* Q24, 0..1 << 24 */
    uint8_t rnd;
} fm6_lfo_t;
typedef struct {
    int32_t pb;                                          /* pitch bend, Q24 */
    int32_t pmod, amod, emod;                            /* the controllers' modulation (Dexed Controllers) */
    int32_t prate;                                       /* portamento step per block */
    uint32_t clock;                                      /* Dexed blocks */
    uint32_t hash;                                       /* of the voice, to see edits */
    uint8_t tick, trig, pon, rr, lav, steal, trn, lgen;  /* rr: chooseNote's start; lav: last voice + 1 */
    uint8_t on;                                          /* the operator switches the hash was made with */
    uint8_t mcur, mlav, mnew, mlive;                     /* MONO: chooseNote's start, last keyed, taken (+ 1), live */
    int32_t mseq;
    uint32_t amd, pt[4];                                 /* AMS: the shares for this modulation (pok: figured) */
    uint8_t pok;
    int32_t shared_pm, shared_amd, shared_tune;
    uint32_t shared_clock, shared_valid;
    uint8_t ghost_quiet;
} fm6_ctl_t;
/* MONO / LEGATO: Dexed keys one of its 16 voices for every key and hands the sounding state from voice to
 * voice (transferSignal / transferState). FM6 plays the note on one voice and keeps what else Dexed's voices
 * hold: the note, its key-down order, whether it counts as playing, its portamento pitch and feedback memory
 * (fm6_key, fm6_note_on, fm6_legato) */
typedef struct {
    uint8_t note, fplay;                                 /* note + 1 (0: never keyed); playing, as it was left */
    int32_t seq, porta[6];
    float fb[2];
    fm6_peg_t pe;
} fm6_mono_t;
typedef struct {                                         /* a part's (engines.c eng_state) */
    fm6_voice_t v[NVOICE];
    fm6_mono_t ms[16];
    fm6_lfo_t lfo;
    fm6_ctl_t pt;
    int32_t dc_x1, dc_y1;                                /* the DC filter: the last input; the last output << 8 */
} fm6_part_t;
static fm6_part_t *fm6_part(uint32_t part);             /* engines.c */
#define FM6P(t) fm6_part(trk_ix(t))

static void fm6_lfo_step(fm6_lfo_t *l, const uint8_t *ed)   /* one block of the LFO: Lfo::getsample, getdelay */
{
    uint32_t inc = FM6_LFO_INC[ed[FP_LFS]], ph = l->ph + inc, a = 99u - (uint32_t)ed[FP_LFD], d1, d2, d;
    int32_t x;
    l->ph = ph;
    switch (ed[FP_LFW]) {
    case 0:                                              /* triangle */
        x = (int32_t)((ph >> 7) ^ (uint32_t)-(int32_t)(ph >> 31)) & ((1 << 24) - 1);
        break;
    case 1:                                              /* saw down */
        x = (int32_t)((~ph ^ (1u << 31)) >> 8);
        break;
    case 2:                                              /* saw up */
        x = (int32_t)((ph ^ (1u << 31)) >> 8);
        break;
    case 3:                                              /* square */
        x = (int32_t)(((~ph) >> 7) & (1u << 24));
        break;
    case 4:                                              /* sine */
        x = (int32_t)((0.5f + fm6_sin(ph >> 8) * 0.5f) * 16777216.0f);
        break;
    default:                                             /* sample & hold */
        if (ph < inc)
            l->rnd = (uint8_t)(l->rnd * 179u + 17u);
        x = ((l->rnd ^ 0x80) + 1) << 16;
        break;
    }
    l->val = x;
    if (a == 99u) {                                      /* DELAY 0 */
        d1 = d2 = 0xFFFFFFFFu;
    } else {
        a = (16u + (a & 15u)) << (1u + (a >> 4));
        d1 = FM6_LFO_UNIT * a;
        d2 = FM6_LFO_UNIT * (a & 0xFF80u ? a & 0xFF80u : 0x80u);
    }
    d = l->dly + (l->dly < (1u << 31) ? d1 : d2);
    if (d < l->dly) {                                    /* past the top */
        l->depth = 1 << 24;
        return;
    }
    l->dly = d;
    l->depth = d < (1u << 31) ? 0 : (int32_t)((d >> 7) & ((1u << 24) - 1u));
}

/* a controller's share (Controllers::applyMod: CC x RANGE / 100, as Dexed's float figures it) */
static int32_t fm6_ctl(int32_t cc, int32_t range)
{
    return cc * range / 100 - ((cc == 100 && (range == 53 || range == 59)) || (cc == 75 && range == 84));
}

static void fm6_ghost(track_t *t, voice_t *v, fm6_voice_t *s);
static void fm6_prepare_controls(track_t *t, fm6_part_t *P, int32_t tune);
static void fm6_ghosts_dispatch(track_t *t, fm6_part_t *P, uint32_t mask);
static int fm6_quiet(const fm6_part_t *P,const uint8_t *ed);

/* per part and block: the controllers, and every other block (or at a key-down retrigger) the LFO */
static void fm6_ctl_block(track_t *t, const uint8_t *ed)
{
    fm6_part_t *P = FM6P(t);
    fm6_ctl_t *c = &P->pt;
    uint32_t k, egs = 0;
    int32_t raw = t->bend_raw, cc[4], m;
    cc[0] = t->mw;
    cc[1] = t->foot;
    cc[2] = t->breath;
    cc[3] = t->at;
    c->shared_valid=0;
    c->pmod = c->amod = c->emod = 0;
    for (k = 0; k < 4u; k++) {                           /* wheel, foot, breath, aftertouch */
        uint32_t tg = FM6F(t)[FN_MWA + 2u * k];
        m = fm6_ctl(cc[k], FM6F(t)[FN_MWR + 2u * k]);
        if (tg & 1u)
            c->pmod = m > c->pmod ? m : c->pmod;
        if (tg & 2u)
            c->amod = m > c->amod ? m : c->amod;
        if (tg & 4u)
            c->emod = m > c->emod ? m : c->emod;
        egs |= tg & 4u;
    }
    if (!egs)
        c->emod = 127;
    if (!raw) {                                          /* pitch bend */
        c->pb = 0;
    } else if (!FM6F(t)[FN_PBSTEP]) {
        c->pb = fm6_f32(raw * 2048 * (raw > 0 ? FM6F(t)[FN_PBUP] : FM6F(t)[FN_PBDN])) / 12;
    } else {
        int32_t stp = 12 / FM6F(t)[FN_PBSTEP];
        c->pb = ((raw * stp / 8191) * (8191 / stp)) * 2048;
    }
    c->pon = (uint8_t)(FM6F(t)[FN_PMODE] || t->porta);
    c->prate = !c->pon ? FM6_PORTA[0] : FM6F(t)[FN_GLISS] ? FM6_GLISS[FM6F(t)[FN_PTIME]] : FM6_PORTA[FM6F(t)[FN_PTIME]];
    if (c->trig || !(c->tick++ & 1u)) {                  /* Dexed's 64-sample grid, restarted by a retrigger */
        c->trig = 0;
        c->tick = 1;
        c->clock++;
        P->pt.shared_valid=0;
        P->pt.ghost_quiet=(uint8_t)fm6_quiet(P,ed);
        fm6_lfo_step(&P->lfo, ed);
        uint32_t ghosts=0;
        for (k = 0; k < NVOICE; k++)                     /* voices over, as Dexed runs them on */
            if (P->v[k].played && !P->v[k].frozen && !trk_voice(t, k)->active && P->v[k].still!=2u)
                ghosts|=1u<<k;
        if (ghosts) {
            if (audio_batch_active) fm6_ghosts_dispatch(t,P,ghosts);
            else for(k=0;k<NVOICE;k++)if((ghosts>>k)&1u)fm6_ghost(t,trk_voice(t, k),&P->v[k]);
        }
    }
}

/* ------------------------------------------------------------ the voice --- */
static fm6_voice_t *fm6_state(const track_t *t, const voice_t *v) { return &FM6P(t)->v[voice_ix(t, v)]; }
static inline __attribute__((always_inline)) const uint8_t *fm6_ed(const track_t *t) { return fm6_eff[trk_ix(t) % NPART].p; }
static uint32_t fm6_note(const track_t *t, const voice_t *v)   /* the note the DX7 rules see: + TRANSPOSE */
{
    return (uint32_t)clamp((int32_t)v->note + fm6_ed(t)[FP_TRNSP] - 24, 0, 127);
}

/* the operators' pitch, level and rate scaling for the voice's note and velocity (Dx7Note::init / update) */
static void fm6_keyed(fm6_voice_t *s, const uint8_t *ed, uint32_t note)
{
    uint32_t k;
    for (k = 0; k < 6u; k++) {
        const uint8_t *op = &ed[k * FP_OP];
        s->ol[k] = (int16_t)fm6_outlevel(op, note, s->vel);
        s->rs[k] = (int8_t)fm6_rscale(op, note);
        s->base[k] = fm6_logfreq(op, note);
    }
}

/* Dexed's "playing" (Dx7Note::isPlaying: an output operator's envelope not done) */
static int fm6_playing(const track_t *t, uint32_t i)
{
    const fm6_voice_t *s = &FM6P(t)->v[i];
    const uint8_t *ed = fm6_ed(t);
    uint32_t k;
    if (!s->played)
        return 0;
    for (k = 0; k < 6u; k++)
        if ((FM6_ALG[ed[FP_ALG] & 31][k] & 4u) && (s->eg[k].ix < 4u || ed[k * FP_OP + FP_L1 + 3] > 0))
            return 1;
    return 0;
}

static uint32_t trk_nvoice(const track_t *t);
/* POLY: the voice for a key (chooseNote): free over key-up over the same note, the oldest key first */
static uint32_t fm6_alloc(track_t *t, uint32_t note)
{
    fm6_part_t *P = FM6P(t);
    uint32_t np=trk_nvoice(t),k=P->pt.rr%np,best=k,i;
    int32_t bs = -1;
    for (i = 0; i < np; i++) {
        int32_t sc = (fm6_playing(t, k) ? 0 : 4) + (trk_voice(t, k)->gate ? 0 : 2) + (P->v[k].played && trk_voice(t, k)->note == note);
        if (sc > bs || (sc == bs && trk_voice(t, k)->age < trk_voice(t, best)->age)) {
            best = k;
            bs = sc;
        }
        k = (k + 1u) % np;
    }
    P->pt.rr = (uint8_t)((best + 1u) % np);
    P->pt.steal = (uint8_t)fm6_playing(t, best);
    return best;
}

static void fm6_sync(track_t *t, const voice_t *self);

/* where portamento starts, as Dexed's initPortamento finds it: the last voice keyed (still going:
 * its pitch now; after a MONO key-up handed the note on, as it was then); 0 = no portamento */
static int fm6_psrc(const track_t *t, fm6_part_t *P, int32_t *src)
{
    uint32_t k, l = P->pt.lav;
    if (!l || !P->v[l - 1u].played || !P->pt.pon || FM6F(t)[FN_PTIME] <= 0)
        return 0;
    for (k = 0; k < 6u; k++)
        src[k] = P->v[l - 1u].porta[k];
    return 1;
}

/* MONO: Dexed's voice c is keyed and its key is down (but for the key going down now, + 1) */
static int fm6_mkd(const track_t *t, uint32_t c, uint32_t now)
{
    const fm6_part_t *P = FM6P(t);
    uint32_t i;
    if (!P->ms[c].note || P->ms[c].note == now)
        return 0;
    for (i = 0; i < 16u; i++)                            /* the key's latest voice */
        if (P->ms[i].note == P->ms[c].note && P->ms[i].seq > P->ms[c].seq)
            return 0;
    for (i = 0; i < t->nmono; i++)
        if (t->mono_stack[i] + 1u == P->ms[c].note)
            return 1;
    return 0;
}

/* MONO: the sounding state leaves Dexed's live voice for voice c (+ 1) */
static void fm6_mhand(track_t *t, uint32_t c)
{
    fm6_part_t *P = FM6P(t);
    uint32_t l = P->pt.mlive, k;
    fm6_voice_t *s = &P->v[0];
    if (l) {                                             /* the one left keeps what it had */
        P->ms[l - 1u].fplay = (uint8_t)(trk_voice(t, 0)->active || s->played ? fm6_playing(t, 0) : 0);
        for (k = 0; k < 6u; k++)
            P->ms[l - 1u].porta[k] = s->porta[k];
        P->ms[l - 1u].fb[0] = s->fb[0];
        P->ms[l - 1u].fb[1] = s->fb[1];
        P->ms[l - 1u].pe = s->pe;
    }
    P->pt.mlive = (uint8_t)c;
    P->pt.mnew = (uint8_t)c;
}

static void fm6_mtake(track_t *t, fm6_voice_t *s)        /* MONO: the voice plays Dexed's voice mnew */
{
    fm6_part_t *P = FM6P(t);
    uint32_t c = P->pt.mnew, k;
    if (!c)
        return;
    for (k = 0; k < 6u; k++)
        s->porta[k] = P->ms[c - 1u].porta[k];
    s->fb[0] = P->ms[c - 1u].fb[0];
    s->fb[1] = P->ms[c - 1u].fb[1];
    s->pe = P->ms[c - 1u].pe;
    P->pt.mnew = 0;
}

/* a key goes down in MONO / LEGATO: Dexed keys a voice for it (chooseNote, init, initPortamento);
 * it takes the sound if it is the only key or above the one sounding (else it waits, keyed) */
static void fm6_key(track_t *t, uint32_t note)
{
    fm6_part_t *P = FM6P(t);
    uint32_t i, k, c, l, nt;
    const uint8_t *ed;
    int32_t bs = -1;
    fm6_sync(t, 0);
    ed = fm6_ed(t);
    k = P->pt.mcur % 16u;
    c = k;
    l = P->pt.mlive;
    for (i = 0; i < 16u; i++) {
        int32_t sc = ((l == k + 1u ? fm6_playing(t, 0) : P->ms[k].fplay) ? 0 : 4) +
                     (fm6_mkd(t, k, note + 1u) ? 0 : 2) +
                     (P->ms[k].note == note + 1u);
        if (sc > bs || (sc == bs && P->ms[k].seq < P->ms[c].seq)) {
            c = k;
            bs = sc;
        }
        k = (k + 1u) % 16u;
    }
    P->pt.mcur = (uint8_t)((c + 1u) % 16u);
    nt = (uint32_t)clamp((int32_t)note + ed[FP_TRNSP] - 24, 0, 127);
    for (k = 0; k < 6u; k++)                             /* init: the note's pitch */
        P->ms[c].porta[k] = fm6_logfreq(&ed[k * FP_OP], nt);
    if (P->pt.mlav && P->ms[P->pt.mlav - 1u].note && P->pt.pon && FM6F(t)[FN_PTIME] > 0) {
        uint32_t a = P->pt.mlav - 1u;                    /* initPortamento: from the last keyed voice */
        for (k = 0; k < 6u; k++)
            P->ms[c].porta[k] = l == a + 1u && a != c ? P->v[0].porta[k] : P->ms[a].porta[k];
    }
    P->ms[c].note = (uint8_t)(note + 1u);
    P->ms[c].seq = ++P->pt.mseq;
    P->ms[c].fplay = 1;                                  /* keyed: its envelopes start (Dexed counts it playing) */
    fm6_peg_set(&P->ms[c].pe, ed);
    P->pt.mnew = 0;
    if (l && l != c + 1u && fm6_mkd(t, l - 1u, 0) && P->ms[l - 1u].note > note + 1u)
        return;                                          /* a higher key sounds: this one waits */
    fm6_mhand(t, c + 1u);
    P->pt.mlav = (uint8_t)(c + 1u);
}

/* a key goes down (DexedAudioProcessor::keydown, Dx7Note::init) */
static void fm6_note_on(track_t *t, voice_t *v)
{
    fm6_part_t *P = FM6P(t);
    uint32_t k, i, vi = voice_ix(t, v), note, vel;
    fm6_voice_t *s = fm6_state(t, v);
    const uint8_t *ed;
    int mono = t->p[P_VOICE] != V_POLY, steal = P->pt.steal || (mono && s->played), ps;
    int32_t src[6];
    P->pt.steal = 0;
    P->pt.shared_valid=0;
    fm6_sync(t, v);
    ed = fm6_ed(t);
    ps = fm6_psrc(t, P, src);                               /* before this voice takes the note */
    for (i = 0; i < NVOICE; i++)                         /* the first key down: the LFO restarts */
        if (i != vi && trk_voice(t, i)->gate)
            break;
    if (i == NVOICE) {
        if (ed[FP_LKS])
            P->lfo.ph = (1u << 31) - 1u;
        P->lfo.dly = 0;
        P->pt.trig = 1;
    }
    vel = v->mvel ? v->mvel : v->vel;                    /* (the note's: UNISON lowers v->vel) */
    s->vel = (uint8_t)(FM6F(t)[FN_VNORM] ? vel * 7874015u / 10000000u : vel);
    s->note = v->note;
    note = fm6_note(t, v);
    fm6_keyed(s, ed, note);
    for (k = 0; k < 6u; k++) {
        s->eg[k].level = 0;
        fm6_eg_go(&s->eg[k], &ed[k * FP_OP], s->ol[k], s->rs[k], 0);
        s->porta[k] = s->base[k];
    }
    fm6_peg_set(&s->pe, ed);
    s->down = 1;
    s->midi_release = 0;
    if (ed[FP_OKS] && !steal)                            /* KEY SYNC (not on a stolen voice: no click) */
        for (k = 0; k < 6u; k++)
            s->ph[k] = 0, s->gout[k] = 0;
    if (ps && !mono)                                     /* portamento from the last voice's pitch */
        for (k = 0; k < 6u; k++)
            s->porta[k] = src[k];
    if (mono)
        fm6_mtake(t, s);
    if (!mono && !ed[FP_OKS])                            /* the same note sounding: its phases */
        for (i = 0; i < NVOICE; i++)
            if (i != vi && fm6_playing(t, i) && P->v[i].played && trk_voice(t, i)->note == v->note) {
                for (k = 0; k < 6u; k++)
                    s->ph[k] = P->v[i].ph[k];
                break;
            }
    P->pt.lav = (uint8_t)(vi + 1u);
    s->played = 1;
    s->sub = 0;
    s->quiet = 0;
    s->frozen = 0;
    s->still = 0;
}

/* MONO / LEGATO without a new attack: the note changes, the envelopes go on (Dexed's mono: the
 * voice keyed for the note takes over the sounding one's state, transferState), with that voice's
 * portamento pitch and feedback memory: a new key (fm6_key), or the highest key still down after
 * a key-up */
static void fm6_legato(track_t *t, voice_t *v)
{
    fm6_part_t *P = FM6P(t);
    uint32_t k, c;
    fm6_voice_t *s = fm6_state(t, v), keep = *s;
    const uint8_t *ed;
    fm6_sync(t, v);
    ed = fm6_ed(t);
    if (!P->pt.mnew) {                                   /* a key-up: the voice keyed for this key */
        for (c = 0; c < 16u && !(P->ms[c].note == v->note + 1u && fm6_mkd(t, c, 0)); c++)
            ;
        if (c < 16u)
            fm6_mhand(t, c + 1u);
    }
    s->note = v->note;
    fm6_keyed(s, ed, fm6_note(t, v));
    for (k = 0; k < 6u; k++) {                           /* the old note's levels and rates stay */
        s->ol[k] = keep.ol[k];
        s->rs[k] = keep.rs[k];
        s->porta[k] = s->base[k];
    }
    fm6_peg_set(&s->pe, ed);
    fm6_mtake(t, s);
}

/* the voice is over: released, and every output operator's envelope (and the feedback operator's) gone under
 * what any engine renders, for good (voice.c engine_t.done). Dexed keeps computing such a voice; FM6 keeps
 * only its control path running (fm6_ghost) */
static int fm6_done(track_t *t, voice_t *v) { return fm6_state(t, v)->quiet >= 2u; }

/* CPU0 publishes part-wide modulation before any voice job. Voice jobs
 * consume these caches without writing shared part state. */
static void fm6_prepare_controls(track_t *t, fm6_part_t *P, int32_t tune)
{
    if (P->pt.shared_valid && P->pt.shared_clock == P->pt.clock) return;
    const uint8_t *ed = fm6_ed(t);
    int32_t lfo=P->lfo.val, dly=P->lfo.depth, sens, pm, pbase, amd, a1, a2;
    uint32_t pmd=((uint32_t)ed[FP_LPMD]*165u)>>6;
    /* pitch: the LFO (PMD x PMS, after the delay, or a controller's), the pitch envelope, bend */
    sens = FM6_PMS[ed[FP_LPMS]] * (lfo - (1 << 23));
    {
        int32_t p1 = (int32_t)((float)pmd * (float)dly * (float)sens * (1.0f / 549755813888.0f));
        int32_t p2 = (int32_t)((float)P->pt.pmod * (float)sens * (1.0f / 16384.0f));
        p1 = p1 < 0 ? -p1 : p1;
        p2 = p2 < 0 ? -p2 : p2;
        pm = p1 > p2 ? p1 : p2;
        pm = sens < 0 ? -pm : pm;
    }
    pbase = P->pt.pb + tune;
    pm += pbase;                               /* + Melodee's glide, LFO / ENV pitch, unison */
    /* amplitude: the LFO (AMD, after the delay) or a controller's, at least the EG bias */
    lfo = (1 << 24) - lfo;
    a1 = (int32_t)((float)(((uint32_t)ed[FP_LAMD] * 165u) >> 6) * (float)dly * (float)lfo * (1.0f / 4294967296.0f));
    a2 = (int32_t)((float)P->pt.amod * (float)lfo * (1.0f / 128.0f));
    amd = a1 > a2 ? a1 : a2;
    a1 = (1 << 24) - ((P->pt.emod + 1) << 17);
    amd = (uint32_t)a1 > (uint32_t)amd ? a1 : amd;
    P->pt.shared_pm=pm;P->pt.shared_amd=amd;P->pt.shared_tune=pbase;
    P->pt.shared_clock=P->pt.clock;P->pt.shared_valid=1;
    if (P->pt.amd != (uint32_t)amd) {
        P->pt.amd = (uint32_t)amd; P->pt.pok = 0;
    }
    for (uint32_t k=0; k<6; k++) {
        uint32_t a=ed[k*FP_OP+FP_AMS];
        if (fm6_op_on(t,k) && FM6_AMS[a] && !((P->pt.pok>>a)&1u)) {
            uint32_t sa=(uint32_t)((float)amd*((float)FM6_AMS[a]*(1.0f/16777216.0f)));
            P->pt.pt[a]=sa?fm6_ams_pt(sa):198789u;
            P->pt.pok|=(uint8_t)(1u<<a);
        }
    }
}

/* a new Dexed block: key-up, the envelopes, pitch and gains (Dx7Note::compute), the routing */
static __attribute__((noinline,section(".audio_text"))) void fm6_control_ram(track_t *t, voice_t *v, fm6_voice_t *s, const vmod_t *m, const fm6_part_t *P, int32_t brightness)
{
    uint32_t tr = trk_ix(t) % NTRK, alg, k, dead = 1;
    const uint8_t *ed = fm6_ed(t);
    int32_t lv[6], pm, pbase, amd, fbv;
    /* MLVL and FLT: the modulators' level, 0.75 dB / 32 steps (microsteps, as the output levels), Q24 */
    int32_t mod = (clamp(brightness, -64, 63) * 24 + clamp(m->cutoff >> 8, -150, 150) * 16) * (1 << 16);
    alg = (uint32_t)ed[FP_ALG] & 31u;
    if (!v->gate && s->down) {                           /* key up: every envelope to its fourth segment */
        s->down = 0;
        s->midi_release = 0;                           /* native R4 is installed below */
        for (k = 0; k < 6u; k++)
            fm6_eg_go(&s->eg[k], &ed[k * FP_OP], s->ol[k], s->rs[k], 3);
        fm6_peg_go(&s->pe, 3);
    }
    if (!s->down && midi_verb[tr] != s->midi_release) {
        /* The native EG stays in its logarithmic domain. Change only R4's
         * running increment; the raw DX7 patch and held stages stay intact. */
        int32_t release = midi_verb[tr] ? 99 - midi_verb[tr] * 99 / 127 : -1;
        for (k = 0; k < 6u; k++) if (s->eg[k].ix == 3u) {
            int32_t rate = release >= 0 ? release : ed[k * FP_OP + FP_R1 + 3u];
            int32_t q = clamp(((rate * 41) >> 6) + s->rs[k], 0, 63);
            s->eg[k].inc = (4 + (q & 3)) << (2 + FM6_LG_N + (q >> 2));
        }
        s->midi_release = midi_verb[tr];
    }
    pm=fm6_peg_step(&s->pe,s->down)+P->pt.shared_pm+m->plog;
    amd=P->pt.shared_amd;pbase=P->pt.shared_tune;
    for (k = 0; k < 6u; k++) {
        const uint8_t *op = &ed[k * FP_OP];
        int car = fm6_carrier(alg, 6u - k);
        int32_t rs = s->rs[k], level;
        if (!fm6_op_on(t, k)) {                          /* switched off: the envelope runs on, silent */
            fm6_eg_step(&s->eg[k], op, s->ol[k], rs, s->down);
            lv[k] = 0;
            continue;
        }
        int32_t freq;
        if (op[FP_MODE]) {
            freq = s->base[k] + pbase + fm6_eff[tr].dt[k];
        } else {
            int32_t b = s->base[k];
            if (s->porta[k] != s->base[k]) {             /* portamento towards the note */
                int32_t cur = s->porta[k], up = cur < s->base[k], np = cur + (up ? P->pt.prate : -P->pt.prate);
                b = cur;
                if (FM6F(t)[FN_GLISS])
                    b -= (b - 50857777) % ((1 << 24) / 12);
                if ((up && np > s->base[k]) || (!up && np < s->base[k]))
                    np = s->base[k];
                s->porta[k] = np;
            }
            freq = b + pm + fm6_eff[tr].dt[k];
        }
        if(!((s->freq_valid>>k)&1u) || s->freq_log[k]!=freq){
            s->freq_log[k]=freq;s->fq[k]=fm6_freq(freq);s->freq_valid|=(uint8_t)(1u<<k);
        }
        level = fm6_eg_step(&s->eg[k], op, s->ol[k], rs, s->down);
        if (FM6_AMS[op[FP_AMS]]) {                       /* AMS: the modulation takes a part of the level */
            uint32_t pt=P->pt.pt[op[FP_AMS]];
            level -= (int32_t)((float)level * ((float)pt * (1.0f / 16777216.0f)));
        }
        if (!car && mod)
            level = clamp(level + mod, 0, 20 << 24);
        lv[k] = level;
    }
    for (k = 0; k < 6u; k++) {                           /* the output operators all gone for good? */
        const fm6_eg_t *e = &s->eg[k];               /* (and the feedback one: its memory outlives the note) */
        if ((fm6_carrier(alg, 6u - k) || ((FM6_ALG[alg][k] & 0xc0u) == 0xc0u && ed[FP_FB])) &&
            (s->down || ed[k * FP_OP + FP_L1 + 3] > 0 || e->level >= FM6_SILENT || e->ix < 3u ||
             (e->ix == 3u && e->rising)))
            dead = 0;
    }
    s->quiet = dead ? (uint8_t)(s->quiet + (s->quiet < 2u)) : 0;
    fbv = clamp(ed[FP_FB] + ((m->shape - (64 << 8)) >> 11), 0, 7);   /* SHP moves the feedback */
    fm6_plan(s, lv, alg, FM6F(t)[FN_ENGINE], fbv ? 8u - (uint32_t)fbv : 16u);
}

/* Immediate serial/legacy path. Batch jobs use the same resident control
 * body after CPU0 has prepared the shared read-only part caches. */
static void fm6_control(track_t *t, voice_t *v, fm6_voice_t *s, const vmod_t *m)
{
    fm6_part_t *P=FM6P(t);
    fm6_prepare_controls(t,P,song.g[G_TUNE]*13981+tuning_log());
    void (*volatile run)(track_t *, voice_t *, fm6_voice_t *, const vmod_t *, const fm6_part_t *, int32_t)=fm6_control_ram;
    AP_START(start);run(t,v,s,m,P,t->p[P_E2]);AP_END(AP_FM6_CONTROL,start);
}

/* nothing moves a voice at rest: no LFO or controller pitch (PMS 0, or no depth), no amplitude
 * modulation where an operator has AMS */
static int fm6_quiet(const fm6_part_t *P, const uint8_t *ed)
{
    uint32_t k, ams = 0;
    int32_t dly = P->lfo.depth;
    for (k = 0; k < 6u; k++)
        ams |= (uint32_t)ed[k * FP_OP + FP_AMS];
    return (!ed[FP_LPMS] || (!P->pt.pmod && (!ed[FP_LPMD] || !dly))) &&
           (!ams || (!P->pt.amod && P->pt.emod == 127 && (!ed[FP_LAMD] || !dly)));
}

/* a voice that is over: its envelopes, pitch, gains and phases go on a Dexed block, without the samples (none
 * of them sounds), so that its next note starts where Dexed's would (KEY SYNC off, a voice taken while playing) */
static void fm6_ghost_state(track_t *t, voice_t *v, fm6_voice_t *s, const fm6_part_t *P, int32_t pb, int32_t brightness, int poly)
{
    static const vmod_t m0 = {.shape = 64 << 8};
    const uint8_t *ed = fm6_ed(t);
    uint32_t k;
    int quiet;
    if (s->still == 2u)
        return;                                          /* key sync, not playing: its next note starts afresh */
    quiet = P->pt.ghost_quiet;
    if (s->still && quiet && pb == s->spb) {             /* at rest: only time goes on */
        for (k = 0; k < 6u; k++)
            s->ph[k] += (uint32_t)s->fq[k] << FM6_LG_N;
        return;
    }
    void (*volatile control)(track_t *,voice_t *,fm6_voice_t *,const vmod_t *,const fm6_part_t *,int32_t)=fm6_control_ram;
    control(t,v,s,&m0,P,brightness);
    for (k = 0; k < 6u; k++)
        if ((s->plan[k] & FM6_P_RUN) || (k && k < s->loop && (s->plan[0] & FM6_P_RUN)))
            s->ph[k] += (uint32_t)s->fq[k] << FM6_LG_N;
    s->sub = 0;
    s->still = 0;
    s->spb = pb;
    if (quiet && !s->down && s->pe.pix >= 4u) {          /* every envelope done, portamento there */
        for (k = 0; k < 6u && s->eg[k].ix >= 4u && s->porta[k] == s->base[k]; k++)
            ;
        if (k == 6u)
            s->still = (uint8_t)(ed[FP_OKS] && poly && !fm6_playing(t, voice_ix(t, v)) ? 2 : 1);
    }
}

static void fm6_ghost(track_t *t, voice_t *v, fm6_voice_t *s)
{
    fm6_part_t *P=FM6P(t);int32_t tune=song.g[G_TUNE]*13981+tuning_log();
    fm6_prepare_controls(t,P,tune);
    AP_START(start);
    fm6_ghost_state(t,v,s,P,P->pt.pb+tune,t->p[P_E2],t->p[P_VOICE]==V_POLY);
    AP_END(AP_FM6_CONTROL,start);
}
/* A part's inactive native states are disjoint from its sounding voice jobs.
 * Part-wide caches and parameters are fixed before publication, just as for
 * sounding controls. The dummy PCM is always zero and joins in queue order. */
typedef struct {
    track_t *track; fm6_part_t *part; uint32_t mask;
    int32_t pb,brightness; uint32_t poly;
} fm6_ghost_batch_t;
_Static_assert(sizeof(fm6_ghost_batch_t)<=AUDIO_BATCH_BYTES,"FM6 ghost descriptor");
static void fm6_ghost_batch_kernel(const void *data,int32_t *pcm)
{
    (void)pcm;const fm6_ghost_batch_t *j=data;
    for(uint32_t k=0;k<NVOICE;k++)if((j->mask>>k)&1u)
        fm6_ghost_state(j->track,trk_voice(j->track, k),&j->part->v[k],j->part,j->pb,j->brightness,j->poly);
}
static void fm6_ghosts_dispatch(track_t *t, fm6_part_t *P, uint32_t mask)
{
    int32_t tune=song.g[G_TUNE]*13981+tuning_log(),pb=P->pt.pb+tune;
    fm6_prepare_controls(t,P,tune);
    uint32_t cost=1;
    for(uint32_t k=0;k<NVOICE;k++)if((mask>>k)&1u)
        cost+=P->v[k].still && P->pt.ghost_quiet && pb==P->v[k].spb?24u:350u;
    const fm6_ghost_batch_t job={t,P,mask,pb,t->p[P_E2],t->p[P_VOICE]==V_POLY};
    static int32_t discard[CTL];
    audio_batch_push_cost(fm6_ghost_batch_kernel,&job,sizeof job,discard,CTL,AP_FM6_CONTROL,cost);
}

static inline __attribute__((always_inline)) float fm6_output_clip(float x)
{ return x < -16.0f ? -16.0f : x > 16.0f ? 16.0f : x; }

static __attribute__((noinline,section(".dsp_text"))) void fm6_output(const float *sum, int32_t *out, uint32_t n, const vmod_t *m, int32_t gain, int add)
{
    float scale = (float)gain * (1.0f / 131072.0f);
    const float *d = dith_row((uint32_t)m->amp1);
    for (uint32_t i = 0; i < n; i++) {
        int32_t y = dith_cutf_d(fm6_output_clip(sum[i]) * (float)amp_at(m, i) * scale, d[i]);
        if (add) out[i] += y; else out[i] = y;
    }
}

static inline void fm6_output_run(const float *sum, int32_t *out, uint32_t n, const vmod_t *m, int32_t gain, int add)
{
    void (*volatile run)(const float *, int32_t *, uint32_t, const vmod_t *, int32_t, int) = fm6_output;
    run(sum, out, n, m, gain, add);
}

#if MELODEE_DUAL_CORE
/* Legacy non-batch pairing sends the planned operator kernel and output
 * gain to CPU1. The batch path below also distributes per-voice controls. */
static struct {
    fm6_voice_t *voice;
    vmod_t modulation;
    uint32_t n;
    int32_t gain;
    float bus[2][CTL], sum[CTL];
    int32_t pcm[CTL];
} fm6_job;
static int fm6_pending;
static int32_t *fm6_pending_out;

static void fm6_worker_kernel(void *unused)
{
    (void)unused;
    void (*volatile run)(fm6_voice_t *, uint32_t, float [2][CTL], float *) = fm6_run_into;
    run(fm6_job.voice, fm6_job.n, fm6_job.bus, fm6_job.sum);
    fm6_output_run(fm6_job.sum, fm6_job.pcm, fm6_job.n, &fm6_job.modulation, fm6_job.gain, 0);
}
static void fm6_join(void)
{
    if (!fm6_pending) return;
    fm6_pending = 0;
    if (!audio_worker_join()) return;                    /* CPU1 lost: this voice's block is dropped */
    /* Voice addition order is identical to serial FM6, including rounding. */
    for (uint32_t i = 0; i < fm6_job.n; i++) fm6_pending_out[i] += fm6_job.pcm[i];
}
#else
static inline void fm6_join(void) {}
#endif

typedef struct {
    fm6_voice_t *voice; vmod_t modulation; uint32_t n; int32_t gain;
    track_t *track; voice_t *key; const fm6_part_t *part;
    int32_t brightness; uint32_t control;
} fm6_batch_t;
_Static_assert(sizeof(fm6_batch_t)<=AUDIO_BATCH_BYTES,"FM6 batch descriptor");
static void fm6_batch_kernel(const void *data,int32_t *pcm)
{
    const fm6_batch_t *j=data;float sum[CTL];
    if (j->control) {
        void (*volatile control)(track_t *,voice_t *,fm6_voice_t *,const vmod_t *,const fm6_part_t *,int32_t)=fm6_control_ram;
        control(j->track,j->key,j->voice,&j->modulation,j->part,j->brightness);
    }
#if MELODEE_DUAL_CORE
    float bus[2][CTL];
    void (*volatile run)(fm6_voice_t *,uint32_t,float [2][CTL],float *)=fm6_run_into;
    run(j->voice,j->n,bus,sum);
#else
    /* The serial operator kernel also lives beyond XIP's direct-call range. */
    void (*volatile run)(fm6_voice_t *,uint32_t)=fm6_run;
    run(j->voice,j->n);memcpy(sum,fm6_sum,j->n*sizeof *sum);
#endif
    fm6_output_run(sum,pcm,j->n,&j->modulation,j->gain,0);
}

static void fm6_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    fm6_voice_t *s = fm6_state(t, v);
    int32_t k = t->p[P_VOICE] == V_UNISON ? VOICE_FS * 2 / 5 : VOICE_FS;
#ifndef FM6_TAP
    if(audio_batch_active){
        fm6_part_t *P=FM6P(t);
        if (!s->sub) fm6_prepare_controls(t,P,song.g[G_TUNE]*13981+tuning_log());
        const fm6_batch_t job={s,*m,n,k,t,v,P,t->p[P_E2],!s->sub};
        s->sub ^= 1u;
        audio_batch_push(fm6_batch_kernel,&job,sizeof job,out,n,AP_FM6);return;
    }
#endif
    if (!s->sub) fm6_control(t, v, s, m);
    s->sub ^= 1u;
#if MELODEE_DUAL_CORE && !defined(FM6_TAP)
    /* The first voice runs on CPU1; CPU0 prepares AND renders the second.
     * Join before adding either voice and before any post/next-block work. */
    uint32_t next = voice_ix(t, v) + 1u;
    while (next < NVOICE && !trk_voice(t, next)->active) next++;
    if (!fm6_pending && audio_worker_online && n <= CTL && next < NVOICE) {
        fm6_job.voice = s; fm6_job.modulation = *m; fm6_job.n = n; fm6_job.gain = k;
        if (audio_worker_submit(fm6_worker_kernel, 0)) {
            fm6_pending = 1; fm6_pending_out = out;
            return;
        }
    }
#endif
#if MELODEE_DUAL_CORE
    fm6_run(s, n);                                      /* wrapper makes the far call into RAM */
#else
    void (*volatile run)(fm6_voice_t *, uint32_t) = fm6_run;
    run(s, n);
#endif
#ifdef FM6_TAP
    FM6_TAP(fm6_sum, n);
#endif
#if MELODEE_DUAL_CORE
    if (fm6_pending) eng_paired++;   /* (the CPU lab's count: host paired) */
    fm6_join();
    fm6_output_run(fm6_sum, out, n, m, k, 1);
#else
    fm6_output_run(fm6_sum, out, n, m, k, 1);
#endif
}

/* the part's patch against what the voices play: a new voice (fm6_lgen) or another TRANSPOSE: the notes stop,
 * as on a DX7 program change; any other edit (the patch, the macros, the switches) reaches the sounding voices
 * (Dx7Note::update: held notes go on from L3). Before a key-down (but for its own voice) and at every block */
static void fm6_sync(track_t *t, const voice_t *self)
{
    fm6_part_t *P = FM6P(t);
    uint32_t tr = trk_ix(t) % NPART, i, h = 2166136261u;
    const uint8_t *ed;
    int panic;
    if (!fm6_eff_build(t) && P->pt.hash && P->pt.on == fm6_eff[tr].on && P->pt.lgen == fm6_lgen[tr])
        return;                                          /* nothing changed (every block: keep it cheap) */
    ed = fm6_ed(t);
    for (i = 0; i < FP_SIZE; i++)
        h = (h ^ ed[i]) * 16777619u;
    h = (h ^ fm6_eff[tr].on) * 16777619u;
    panic = P->pt.lgen != fm6_lgen[tr] || (P->pt.hash && ed[FP_TRNSP] != P->pt.trn);
    if (h == P->pt.hash && !panic)
        return;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = trk_voice(t, i);
        fm6_voice_t *s = &P->v[i];
        uint32_t k;
        if (v == self || !(v->active || (s->played && !s->frozen)))
            continue;                                    /* (voices over too: Dexed keeps them live) */
        s->still = 0;
        if (panic) {                                     /* the notes stop (one block's fade), as Dexed's panic */
            if (v->active) {
                v->gate = 0;
                v->stage = 4;
            }
            s->frozen = 1;
            for (k = 0; k < 6u; k++)
                s->ph[k] = 0, s->gout[k] = 0;
            continue;
        }
        fm6_keyed(s, ed, fm6_note(t, v));
        /* Patch/rate scaling edits invalidate an installed MIDI release.
         * Native released envelopes otherwise retain their existing rate. */
        if (s->midi_release) s->midi_release = UINT8_MAX;
        if (s->down)
            for (k = 0; k < 6u; k++)
                fm6_eg_go(&s->eg[k], &ed[k * FP_OP], s->ol[k], s->rs[k], 2);
    }
    P->pt.shared_valid=0;
    P->pt.hash = h;
    P->pt.trn = ed[FP_TRNSP];
    P->pt.lgen = fm6_lgen[tr];
    P->pt.on = fm6_eff[tr].on;
}

static void fm6_block(track_t *t)                        /* per part and block: the patch, controllers, LFO */
{
    fm6_sync(t, 0);
    fm6_ctl_block(t, fm6_ed(t));
}

/* The DC filter runs once per sample even when the operator envelopes have
 * decayed. Keep its original wide arithmetic in the resident sample path. */
static __attribute__((noinline,section(".dsp_text"))) void fm6_dc_samples(fm6_part_t *P, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t x1 = P->dc_x1, y = P->dc_y1;
    for (i = 0; i < n; i++) {
        int32_t x = out[i];
        y = (int32_t)(((int64_t)(x - x1) * 256 + (((int64_t)y * 1070673990) >> 30)));
        x1 = x;
        out[i] = (y + 128) >> 8;
    }
    P->dc_x1 = x1;
    P->dc_y1 = y;
}

/* the part after its voices: Dexed's DC filter on the voices' sum (PluginFx: y = x - x' + (1 - 126 / FS) y',
 * about 20 Hz); with no voice it starts afresh */
static void fm6_post(track_t *t, int32_t *out, uint32_t n, uint32_t nr)
{
    fm6_join();
    fm6_part_t *P = FM6P(t);
    if (!nr) {
        P->dc_x1 = P->dc_y1 = 0;
        return;
    }
    void (*volatile run)(fm6_part_t *, int32_t *, uint32_t) = fm6_dc_samples;
    run(P, out, n);
}

/* --------------------------------------------------------- the engine --- */
static const char *const N_FM6_ALG[] = {"PAT", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
                                        "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25",
                                        "26", "27", "28", "29", "30", "31", "32", 0};
/* {ALG, FB, MLVL, MRAT, MEG, VMOD, DTUN, unused}: neutral macros, DTUN on the pad. */
#if !MOD_SIDE                    /* (a synth module's presets are in its file: tools/mod_presets.py) */
#define FM6_PR(name, mono) {name, {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, mono}
static const preset_t FM6_PRESETS[] = {
    {"TINE EP", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0},
    {"BELL", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0},
    {"FM BASS", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 1},
    {"BRASS", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0},
    {"PAD", {0, 0, 0, 0, 0, 0, 30, 0}, {0, 0, 127, 0}, 0, 0},
    {"MARIMBA", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0},
    {"ORGAN", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0},
    {"PLUCK", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0},
    FM6_PR("DX TINE", 0),
    FM6_PR("BRASS SECT", 0),
    FM6_PR("SOLID BASS", 1),
    FM6_PR("BELLS", 0),
    FM6_PR("DX MARIMBA", 0),
    FM6_PR("CLAVINET", 0),
    FM6_PR("DRAWBARS", 0),
    FM6_PR("STRINGS", 0),
    FM6_PR("GLASS PAD", 0),
    FM6_PR("SYNC LEAD", 1),
    FM6_PR("HARP", 0),
    FM6_PR("KALIMBA", 0),
    FM6_PR("FLUTE", 1),
    FM6_PR("STEEL DRUM", 0),
    FM6_PR("SAW BASS", 1),
    FM6_PR("TUBULAR", 0),
    FM6_PR("INIT VOICE", 0),          /* the DX7's init voice, dry (after F24: the stores keep the
                                                         * factory's numbers) */
};
#undef FM6_PR
_Static_assert(NELEM(FM6_PRESETS) == FM6_NFAC + 1u, "FM6: F1..F24, then INIT VOICE");
#endif

static uint32_t fm6_pstate(const track_t *t) { (void)t; return sizeof(fm6_part_t); }   /* (its part memory) */
#include "fm6_ui.c"   /* its pages: an operator's frequency as the schema's columns show it */
#include "fm6_sysex.c"   /* the DX7's SysEx */
/* the part's patch written (main loop; Motion: the audio): the voice sanitized as the DX7's ranges have it, the
 * functions Dexed's when a stored set is out of range; another voice (a new sound, or a whole patch with another name
 * or TRANSPOSE: a DX7 program change) switches every operator on and stops the notes */
static uint8_t fm6_seen[NPART][11];              /* the name and TRANSPOSE last written (SET: another voice?) */
static uint8_t fm6_known;                        /* bit k: fm6_seen[k] holds them */
static void fm6_patch_changed(track_t *t, uint8_t *b, uint32_t how, uint32_t id)
{
    uint32_t tr = trk_ix(t) % NPART;
    uint8_t v[FP_SIZE + 1u];
    (void)id;
    if (how != MOD3_PATCH_MOTION) {
        fm6_unpack(b, v);                                /* (in range: the copy the ISR makes is the same) */
        fm6_pack(v, b);
        if (!fm6_fn_ok(b + FM6_B_FN))
            memcpy(b + FM6_B_FN, FM6_FNDEF, FM6_NFN);
        b[FM6_B_ON] &= FM6_ON_ALL;
        if (how == MOD3_PATCH_NEW || ((how == MOD3_PATCH_SET || how == MOD3_PATCH_LOAD) && (!((fm6_known >> tr) & 1u) ||
            memcmp(fm6_seen[tr], v + FP_NAME, 10) || fm6_seen[tr][10] != v[FP_TRNSP]))) {
            b[FM6_B_ON] = FM6_ON_ALL;
            fm6_lgen[tr]++;
        }
        memcpy(fm6_seen[tr], v + FP_NAME, 10);
        fm6_seen[tr][10] = v[FP_TRNSP];
        fm6_known |= (uint8_t)(1u << tr);
    }
    RING_PUBLISH();
    fm6_pgen[tr]++;
}
/* the part's memory as it is (bytes 0: none): a voice sounds or runs on (CPU0 keeps its larger service reserve while
 * CPU1 renders: mix.c mix_prepare_batch) */
static int fm6_reserve(const void *state, uint32_t bytes)
{
    const fm6_part_t *f = state;
    if (bytes != sizeof(fm6_part_t))
        return 0;
    for (uint32_t v = 0; v < NVOICE; v++)
        if (f->v[v].played && !f->v[v].still && !f->v[v].frozen)
            return 1;
    return 0;
}

static const engine_t ENG_FM6 = {
    .name = "FM6",
    .page_title = {"OPS", "PATCH"},
    .edit = {
        {"ALG", F_INT, 0, 32, 0, N_FM6_ALG, 0},
        {"FB", F_INT, -7, 7, 0, 0, 0},
        {"MLVL", F_BIPCT, -64, 63, 0, 0, 0},
        {"MRAT", F_INT, -16, 16, 0, 0, 0},
        {"MEG", F_BIPCT, -64, 63, 0, 0, 0},
        {"VMOD", F_INT, -7, 7, 0, 0, 0},
        {"DTUN", F_PCT, 0, 127, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
#if !MOD_SIDE
    .presets = FM6_PRESETS,
#endif
    .npresets = FM6_NFAC + 1u,
    .note_on = fm6_note_on,
    .render = fm6_render,
    .knob = {P_E2, P_E3, P_E4, P_E6},
    .poly = NVOICE,                                      /* Dexed's 16: a voice takes one budget unit */
    .block = fm6_block,
    .ownenv = 1,
    .done = fm6_done,
    .alloc = fm6_alloc,
    .legato = fm6_legato,
    .mono_key = fm6_key,
    .post = fm6_post,
    .state = fm6_pstate,
    .wclass = 3,
#if MELODEE_DUAL_CORE
    .join = fm6_join,            /* (a voice paired on CPU1 outside the queue: in before the spread is summed) */
#endif
    .reserve = fm6_reserve,
    .caps = MOD3_CAP_WHEEL,      /* (its DX7 controllers read the wheel: no SMK-37 vibrato) */
    .patch_changed = fm6_patch_changed, .pformat = fm6_pformat, .pget = fm6_pget, .pput = fm6_pput,
    .sysex = fm6_sysex, .sysex_dump = fm6_sysex_dump,
};
