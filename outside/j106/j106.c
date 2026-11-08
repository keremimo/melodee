/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* JUNO-106: a Juno-106-style part written for Saadet from the instrument's published architecture and MIDI
 * implementation, as a Melodee synth module built from the SDK alone (mod_sdk.h, mod_schema.h; ABI 3). Per voice: one
 * DCO (saw, pulse with PWM, a square one octave below, white noise), a resonant four-pole low-pass, and one ADSR shared
 * by the filter and the VCA. Per part: the triangle LFO with its delay, then, after the voices, the high-pass and the
 * chorus, which the instrument also has once for all its voices. The filter's control law is the instrument's own
 * (j106_coeff). Envelope times, the other ranges and the chorus are engineering estimates, not calibrated against an
 * instrument. No other implementation's code is used.
 *
 * Saadet's engine (firmware/src/eng_j106.c, Saadet d4ae975) on the module's views: the same arithmetic. What differs:
 * a Melodee part is one channel (ABI 3 has no stereo stage), so the chorus plays the middle of its two sides,
 * (L + R) / 2; no voice beyond the cap of six (Saadet's spare); its pages' dim hints do not look at the matrix.
 *
 * The module carries its patch (j106_patch.h), its schema (j106_schema.h: parameters, pages, roles, the sound
 * designer's recipes, SysEx and checks), its factory presets (j106_presets.h: the file's PRST chunk) and the
 * instrument's SysEx: patch and manual-mode messages (F0 41 30 / 31) import, single controls (F0 41 32) edit the
 * selected part's patch, the patch exports as a patch message.
 *     python3 sdk/mod_build.py j106.c -o j106.mmod */
#include "mod_sdk.h"
#include "mod_schema.h"
#include "j106_math.h"
#include "j106_patch.h"

#define J106_POLY 6u                     /* the instrument's six voices */
#define J106_CH_LEN 512u                 /* chorus line, a power of two: 11.6 ms */
#define J106_ENV_END (1.0f / 2048.0f)    /* about -66 dB: a released voice ends */
#define J106_FS 44100.0f
#define J106_CTL 32.0f
_Static_assert(MOD3_RATE == 44100u && MOD3_CTL == 32u, "Juno-106 rates assume 44.1 kHz and 32-sample blocks");

typedef struct {
    uint32_t phase;                      /* the DCO's ramp */
    int32_t noise;
    float z[4], coupling;                /* filter stages and output coupling's low-pass state */
    float env, env0;                     /* ADSR at the block's end and start, 0..1 */
    float gate, gate0;                   /* GATE-mode VCA, 0..1 */
    float g;                             /* filter coefficient at the end of the last block */
    uint8_t stage, sub, fresh;           /* stage: 0 off, 1 attack, 2 decay/sustain, 3 release */
    int32_t last_pcm;
} j106_voice_t;
/* Raw-to-DSP values of the whole part for one block, set before the voices and read-only while they render. */
typedef struct {
    float lfo0, lfo1;                    /* the LFO after its delay, -1..1, at the block's start and end */
    float vib0, vib1;                    /* DCO pitch ratios from the LFO and the wheel */
    float saw, pulse, sub, noise;        /* mixer gains */
    float pwm, pwm_lfo;                  /* PWM depth 0..1; 1: by the LFO */
    float cut, env_oct, lfo_oct, key;    /* cutoff in octaves above J106_F0; envelope, LFO amounts; keyboard follow */
    float k, level;
    float atk, dec, sus, rel;            /* envelope coefficients per block */
    uint32_t range;                      /* 0 16', 1 8', 2 4' */
    int32_t tune;                        /* the part's TUNE offset, semitones */
    uint8_t gate;                        /* VCA by gate */
} j106_render_t;
typedef struct {
    j106_voice_t voice[MOD3_NVOICE];     /* (by the voice's index: the host's voices, any of them) */
    j106_render_t r;
    uint32_t lfo, delay_n;               /* LFO phase; samples since the first key */
    float lfo_out;
    uint8_t keys, chorus, hpf, quiet;    /* keys held last block; the last chorus and HPF modes; post state clear */
    float hp_x, hp_y, boost;             /* high-pass and bass-boost one-poles */
    uint32_t ch_w, ch_lfo;
    float ch_lp[2];
    float line[J106_CH_LEN];
} j106_part_t;

static uint32_t j106_eng = 22;           /* the engine number this device gave the module (bind) */
static j106_part_t *j106_part(const mod_part_v3 *t) { return md_part(t, sizeof(j106_part_t)); }
static uint32_t j106_state(const mod_part_v3 *t) { (void)t; return sizeof(j106_part_t); }
static const uint8_t *j106_raw(const mod_part_v3 *t) { return mh->patch(t); }   /* (zeros while switching) */
static inline __attribute__((always_inline)) j106_voice_t *j106_voice(const mod_part_v3 *t, const mod_voice_v3 *v)
{
    j106_part_t *part = j106_part(t);
    return part ? &part->voice[v->index % MOD3_NVOICE] : 0;
}
/* a slider plus the part's offset E1+e */
static int32_t j106_value(const mod_part_v3 *t, const uint8_t *p, uint32_t k, uint32_t e)
{
    return md_clamp((int32_t)(p[k] & 127u) + (e < 8u ? md_e(t)[e] : 0), 0, 127);
}
/* the host's dither of this block, in LSBs: a row of MOD3_CTL values from k (dsp.c dith_row) */
static inline __attribute__((always_inline)) const float *j106_dith_row(uint32_t k)
{
    return mh->dither_f + (k & (MOD3_DITH_N - 1u));
}
/* x to the nearest whole sample with dither d (the add of 1.5 * 2^23 rounds); 0.0f stays 0 */
static inline __attribute__((always_inline)) int32_t j106_dith_cut(float x, float d)
{
    return x != 0.0f ? (int32_t)(x + d + 12582912.0f) - 12582912 : 0;
}

/* ------------------------------------------------------------ the voice --- */
/* Polynomial band-limited step at a phase wrap: the correction of a step of -2 (a ramp's reset), from the ramp's phase
 * and increment */
static inline __attribute__((always_inline)) float j106_blep(uint32_t ph, uint32_t inc)
{
    if (ph < inc) {
        float x = (float)ph / (float)inc;
        return x + x - x * x - 1.0f;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        float x = -(float)(0xFFFFFFFFu - ph) / (float)inc;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}
static inline __attribute__((always_inline)) float j106_ramp(uint32_t ph, uint32_t inc)
{
    return (float)ph * (1.0f / 2147483648.0f) - 1.0f - j106_blep(ph, inc);
}
/* The resonance limit: a cubic that reaches its limit, 1, at 1.5 */
static inline __attribute__((always_inline)) float j106_sat(float x)
{
    x = fm_clampf(x, -1.5f, 1.5f);
    return x - x * x * x * (4.0f / 27.0f);
}
/* The cutoff's control law. The instrument's CPU sums the slider, the envelope, the LFO and the keys into one 14-bit
 * DAC word per voice, and the filter chip's exponential converter turns it into the cutoff: 1143 counts an octave. A
 * slider's byte is 128 counts. The service manual calibrates the filter at FREQ 49 (6272 counts) with the keys at full
 * follow: self-oscillation at 248 Hz on C4 and 992 Hz on C6, so 0 counts is 5.53 Hz, the published 5 Hz end. (The
 * figures from the firmware are those of a published analysis of its ROM, the Ultramaster KR-106 project's.) Measured
 * instruments follow this law to within a semitone up to about 9 kHz, then bend down to the published 50 kHz at the
 * top; the cap here, below Nyquist, is reached first. */
#define J106_OCT 1143.0f                  /* DAC counts an octave */
#define J106_F0 5.528709f                 /* Hz at 0 counts: 248 Hz / 2^(6272 / 1143) */
#define J106_SLIDER (128.0f / J106_OCT)   /* octaves a slider step: its byte into the DAC word */
/* one-pole coefficient G = g / (1 + g), g = tan(pi f / FS), for f = J106_F0 * 2^oct, oct held to the DAC's 0..16383
 * counts */
static float j106_coeff(float oct)
{
    float f = J106_F0 * fm_exp2f(fm_clampf(oct, 0.0f, 16383.0f / J106_OCT));
    float g = fm_tanf(FM_PI * fm_minf(f, 18000.0f) * (1.0f / J106_FS));
    return g / (1.0f + g);
}

/* One block of the ADSR. The attack aims past full scale so that it arrives in its time with a rounded top; decay and
 * release approach exponentially. */
static void j106_env_tick(j106_voice_t *s, const j106_render_t *r, int gate)
{
    s->env0 = s->env;
    if (!gate && s->stage && s->stage < 3u) s->stage = 3;
    if (s->stage == 1u) {
        s->env += (1.2f - s->env) * r->atk;
        if (s->env >= 1.0f) { s->env = 1.0f; s->stage = 2; }
    } else if (s->stage == 2u) {
        s->env += (r->sus - s->env) * r->dec;
    } else if (s->stage == 3u) {
        s->env -= s->env * r->rel;
        if (s->env < J106_ENV_END) { s->env = 0.0f; s->stage = 0; }
    }
    s->gate0 = s->gate;
    s->gate = fm_clampf(s->gate + (gate ? 0.5f : -0.5f), 0.0f, 1.0f);   /* GATE: about 1.5 ms either way */
}

static void j106_note_on(mod_part_v3 *t, mod_voice_v3 *v)
{
    j106_voice_t *s = j106_voice(t, v);
    if (!s) return;
    if (!*mh->was) {
        uint8_t *b = (uint8_t *)s;
        for (uint32_t i = 0; i < sizeof *s; i++) b[i] = 0;
        s->phase = v->ph[0];                 /* the DCOs run free: a new voice finds them anywhere */
        s->noise = (int32_t)(0x6A1C3u + v->age * 31u + (uint32_t)v->index * 977u);
        s->sub = (uint8_t)(md_noise32(&s->noise) & 1u);
        s->fresh = 1;
        /* A circuit's own noise starts a resonant filter even with the mixer shut; a -80 dB seed does it here and is
         * otherwise inaudible. */
        s->z[0] = 1e-4f;
    }
    s->stage = 1;                            /* a retrigger rises from where the envelope is */
}
static int j106_done(mod_part_v3 *t, mod_voice_v3 *v)
{
    j106_voice_t *s = j106_voice(t, v);
    if (!s) return 1;
    if (v->gate) return 0;
    return j106_switch(j106_raw(t), J106_VCA_MODE) ? s->gate <= 0.0f && s->gate0 <= 0.0f : s->stage == 0u;
}

typedef struct {
    float vib0, dvib, w0, dw, g0, dg, a0, da, m0, dm;
    float saw, pulse, sub, noise, k, scale;
    float inc;
    const float *dither;                      /* the voice's row of the host's dither */
} j106_kernel_t;
static void j106_samples(j106_voice_t *restrict s, int32_t *restrict out, uint32_t n, const j106_kernel_t *restrict c)
{
    float z0 = s->z[0], z1 = s->z[1], z2 = s->z[2], z3 = s->z[3], dc = s->coupling;
    uint32_t phase = s->phase, sub = s->sub;
    int32_t last = 0;
    for (uint32_t i = 0; i < n; i++) {
        float time = (float)i * (1.0f / J106_CTL);
        uint32_t inc = (uint32_t)fm_clampf(c->inc * (c->vib0 + c->dvib * time), 1.0f, 1932735232.0f);
        float w = c->w0 + c->dw * time;
        float blep = j106_blep(phase, inc);
        float saw = (float)phase * (1.0f / 2147483648.0f) - 1.0f - blep;
        /* the pulse is the difference of two ramps: low for the first w of the cycle, a step of 2 as the ramp's; its
         * mean is zero at any width */
        float pulse = saw - j106_ramp(phase + (uint32_t)((1.0f - w) * 4294967296.0f), inc);
        float sq = (sub ? 1.0f : -1.0f) * (1.0f - fm_fabsf(blep));
        float noise = (float)(int32_t)md_noise32(&s->noise) * (1.0f / 2147483648.0f);
        float x = saw * c->saw + pulse * c->pulse + sq * c->sub + noise * c->noise;
        uint32_t next = phase + inc;
        if (next < phase) sub ^= 1u;         /* the sub-octave divider toggles at each reset */
        phase = next;
        float G = c->g0 + c->dg * time, G2 = G * G, G3 = G2 * G, G4 = G2 * G2;
        float sigma = (1.0f - G) * (G3 * z0 + G2 * z1 + G * z2 + z3);
        /* The linear solution predicts this sample's output; only the feedback taken from it is limited, so the mixer
         * reaches the stages unclipped. Limiting the whole input would clip a sub that sits on a resonant peak, and
         * PWM's narrow pulses would make it pump. The limit sets how loud full resonance whistles: 7/8 of the cubic's
         * range puts it at 1.07 times a pulse through the open filter, as recordings of the instrument have it (the
         * KR-106 project's). It also keeps a resonant peak that a PWM pulse feeds out of the cubic's curve, so the
         * curve does not pump the sub under it. */
        float u = (x - c->k * sigma) / (1.0f + c->k * G4);
        u = x - c->k * 0.875f * j106_sat((1.0f / 0.875f) * (G4 * u + sigma));
        float v;
        v = (u - z0) * G; u = v + z0; z0 = u + v;
        v = (u - z1) * G; u = v + z1; z1 = u + v;
        v = (u - z2) * G; u = v + z2; z2 = u + v;
        v = (u - z3) * G; u = v + z3; z3 = u + v;
        /* The resonance limit rectifies the asymmetric narrow pulse into an offset that follows PWM. Shared modulation
         * makes these offsets add coherently in chords. Couple at 10 Hz before the VCA so its envelope fades the
         * coupling state too, without leaving a step when a released voice ends. */
        dc += (u - dc) * 0.0014237444f;   /* 1 - exp(-2 pi 10 / FS) */
        float amp = (c->a0 + c->da * time) * (c->m0 + c->dm * time);
        last = j106_dith_cut((u - dc) * amp * c->scale, c->dither[i]);
        out[i] += last;
    }
    s->z[0] = fm_flush(z0); s->z[1] = fm_flush(z1); s->z[2] = fm_flush(z2); s->z[3] = fm_flush(z3);
    s->coupling = fm_flush(dc);
    s->phase = phase; s->sub = (uint8_t)sub; s->last_pcm = last;
}

static void j106_render(mod_part_v3 *t, mod_voice_v3 *v, int32_t *out, uint32_t n, const mod_vmod_v3 *m)
{
    j106_voice_t *s = j106_voice(t, v);
    j106_part_t *part = j106_part(t);
    if (!s || !part) return;
    /* A voice that has ended has one block left: fade its last sample, by then far below the voice, to zero rather
     * than run the synth for nothing. */
    if (!v->active && !m->amp1) {
        if (n > 1u) {
            int32_t step = s->last_pcm / (int32_t)(n - 1u);
            for (uint32_t i = 0; i < n - 1u; i++) out[i] += s->last_pcm - step * (int32_t)i;
        }
        s->last_pcm = 0;
        return;
    }
    const j106_render_t *r = &part->r;
    j106_env_tick(s, r, v->gate);
    /* pitch: the part's tune offset, the range and the shared vibrato */
    uint32_t base = r->tune ? md_cents_inc(m->pitch16 + r->tune * 16, 0, m->fine) : m->inc;
    float inc = (float)base * (r->range == 0u ? 0.5f : r->range == 2u ? 2.0f : 1.0f);
    /* pulse width: 0.5 of the cycle down to 0.04, by the slider or the LFO, plus the common SHAPE modulation */
    float shape = (float)(m->shape - (64 << 8)) * (1.0f / (127.0f * 256.0f));
    float pw0 = fm_clampf(r->pwm * (r->pwm_lfo ? (r->lfo0 + 1.0f) * 0.5f : 1.0f) + shape, 0.0f, 1.0f);
    float pw1 = fm_clampf(r->pwm * (r->pwm_lfo ? (r->lfo1 + 1.0f) * 0.5f : 1.0f) + shape, 0.0f, 1.0f);
    /* cutoff: the slider, the envelope, the LFO, the keys and the common modulation (127 << 8 is the slider's whole
     * range) */
    float key = r->key * ((float)m->pitch16 * (1.0f / 16.0f) - 60.0f) * (1.0f / 12.0f);
    float common = (float)m->cutoff * (J106_SLIDER / 256.0f);
    float oct1 = r->cut + s->env * r->env_oct + r->lfo1 * r->lfo_oct + key + common;
    float g1 = j106_coeff(oct1);
    if (s->fresh) { s->g = g1; s->fresh = 0; }
    float a0 = (r->gate ? s->gate0 : s->env0) * r->level, a1 = (r->gate ? s->gate : s->env) * r->level;
    float m0 = (float)m->amp0 * (1.0f / 32768.0f), m1 = (float)m->amp1 * (1.0f / 32768.0f);
    const j106_kernel_t c = {
        r->vib0, r->vib1 - r->vib0, 0.5f - 0.46f * pw0, 0.46f * (pw0 - pw1), s->g, g1 - s->g, a0, a1 - a0, m0, m1 - m0,
        r->saw, r->pulse, r->sub, r->noise, r->k, 2.0f * (float)MOD3_VOICE_FS, inc, j106_dith_row((uint32_t)m->amp1)
    };
    s->g = g1;
    void (*volatile run)(j106_voice_t *, int32_t *, uint32_t, const j106_kernel_t *) = j106_samples;
    run(s, out, n, &c);
}

/* ------------------------------------------------------------- the part --- */
static float j106_per_block(float seconds)    /* a one-pole's coefficient per block for a time constant */
{
    return 1.0f - fm_expf(-J106_CTL / (seconds * J106_FS));
}
/* An envelope time (attack to full, decay and release to 1 %) from its slider: the published 1.5 ms .. 3 s attack and
 * 1.5 ms .. 12 s decay and release, a square law between them. The law is an estimate fitted to the factory patches'
 * names (PIZZICATO's decay about 0.1 s, PIANO I's 3 s, FLUTES' 5 s); an exponential one turned their decays into
 * clicks. */
static float j106_time(uint32_t v, float most)
{
    float x = (float)(v & 127u) * (1.0f / 127.0f);
    return 0.0015f + most * x * x;
}
static float j106_tri(uint32_t phase)
{
    float x = (float)phase * (1.0f / 2147483648.0f);   /* 0..2 */
    return x < 1.0f ? x * 2.0f - 1.0f : 3.0f - x * 2.0f;
}
/* Once per block, before the voices: the LFO and its delay, and every slider as the voices use it. */
static void j106_block(mod_part_v3 *t)
{
    j106_part_t *P = j106_part(t);
    if (!P) return;
    const uint8_t *p = j106_raw(t);
    j106_render_t *r = &P->r;
    uint32_t keys = 0;
    for (uint32_t i = 0; i < MOD3_NVOICE; i++) keys |= mod3_voice(t, i)->gate;
    /* The LFO delay restarts with the first key after all were up: it waits half the time, then fades in over the
     * other half. */
    if (keys && !P->keys) P->delay_n = 0;
    P->keys = (uint8_t)!!keys;
    float delay = (float)p[J106_LFO_DELAY] * (1.0f / 127.0f);
    delay = delay * delay * 3.0f * J106_FS;
    if (P->delay_n < 0x7FFFFFFFu) P->delay_n += MOD3_CTL;
    float fade = delay <= 0.0f ? 1.0f : fm_clampf(((float)P->delay_n - delay * 0.5f) / (delay * 0.5f), 0.0f, 1.0f);
    float hz = 0.1f * fm_exp2f((float)(p[J106_LFO_RATE] & 127u) * (8.23f / 127.0f));   /* 0.1 .. 30 Hz, as published */
    P->lfo += (uint32_t)(hz * (4294967296.0f / J106_FS)) * MOD3_CTL;
    r->lfo0 = P->lfo_out;
    r->lfo1 = P->lfo_out = j106_tri(P->lfo) * fade;
    /* vibrato: the slider (square law, up to half an octave) and the wheel */
    float dco = (float)(p[J106_DCO_LFO] & 127u) * (1.0f / 127.0f);
    float depth = dco * dco * 0.5f + (float)t->mw * (0.1f / 127.0f);
    r->vib0 = fm_exp2f(r->lfo0 * depth);
    r->vib1 = fm_exp2f(r->lfo1 * depth);
    /* one waveform alone peaks near 0.45 */
    r->saw = j106_switch(p, J106_SAW) ? 0.45f : 0.0f;
    r->pulse = j106_switch(p, J106_PULSE) ? 0.45f : 0.0f;
    r->sub = (float)j106_value(t, p, J106_SUB, 6) * (0.45f / 127.0f);
    r->noise = (float)j106_value(t, p, J106_NOISE, 7) * (0.35f / 127.0f);
    r->pwm = (float)j106_value(t, p, J106_DCO_PWM, 5) * (1.0f / 127.0f);
    r->pwm_lfo = j106_switch(p, J106_PWM_MODE) ? 0.0f : 1.0f;
    /* The amounts as the firmware scales them (j106_coeff): ENV and LFO double their byte as a depth of 0..254 over
     * 256. The full envelope (16383 counts) moves the cutoff as far as the FREQ slider's whole range, 14.2 octaves; the
     * LFO's peak (8191 counts, over 512) up to 3.6. KYBD takes the pitch at 3/8 of a count per 1/256 semitone from C4:
     * 9 counts an octave a step, so 127 follows the keys one to one. */
    r->cut = (float)j106_value(t, p, J106_VCF_FREQ, 0) * J106_SLIDER;
    r->env_oct = (float)j106_value(t, p, J106_VCF_ENV, 3) * (16383.0f / 128.0f / J106_OCT) *
                 (j106_switch(p, J106_ENV_POL) ? -1.0f : 1.0f);
    r->lfo_oct = (float)j106_value(t, p, J106_VCF_LFO, 4) * (8191.0f / 256.0f / J106_OCT);
    r->key = (float)(p[J106_VCF_KYBD] & 127u) * (9.0f / J106_OCT);
    float res = (float)j106_value(t, p, J106_VCF_RES, 1) * (1.0f / 127.0f);
    r->k = 4.2f * res * fm_sqrtf(res);         /* self-oscillation from about 0.95 */
    /* the panel's -5..+5 trim, not a mute (five factory patches play at 0): about -6 dB at 0, unity at 64, about
     * +6 dB at 127; the span is an estimate */
    r->level = fm_exp2f(((float)(p[J106_VCA_LEVEL] & 127u) - 64.0f) * (1.0f / 63.5f));
    r->atk = j106_per_block(j106_time(p[J106_ENV_A], 3.0f) / 1.79f);
    r->dec = j106_per_block(j106_time(p[J106_ENV_D], 12.0f) / 4.6f);
    r->rel = j106_per_block(j106_time(p[J106_ENV_R], 12.0f) / 4.6f);
    r->sus = (float)(p[J106_ENV_S] & 127u) * (1.0f / 127.0f);
    r->range = j106_range(p);
    r->tune = md_e(t)[2];                       /* (here: a voice may render after the matrix restores the part) */
    r->gate = (uint8_t)j106_switch(p, J106_VCA_MODE);
}

/* The chorus: one delay line, two taps swept in opposite directions by a triangle, each tap mixed with the dry signal
 * for one side. I is slower and II faster; both lie between about 1.7 and 5.3 ms. The taps pass a gentle low-pass, as
 * a bucket-brigade line's filters would. On a host with a part's stereo output (5) the part is stereo, as Saadet's:
 * left holds L and out L + R; else (host 4, a render outside the mixer) out holds the middle, (L + R) / 2. */
static void j106_chorus_out(mod_part_v3 *t, j106_part_t *P, int32_t *out, uint32_t n)
{
    uint32_t mode = j106_chorus(j106_raw(t));
    if (!P->chorus && mode) {                 /* the line holds nothing older than this */
        for (uint32_t i = 0; i < J106_CH_LEN; i++) P->line[i] = 0.0f;
        P->ch_lp[0] = P->ch_lp[1] = 0.0f;
    }
    P->chorus = (uint8_t)mode;
    if (!mode) return;
    int32_t *left = mh->version >= 5u ? mh->stereo(t) : 0;
    const float rate = mode == 1u ? 0.5f : 0.83f, center = 3.5e-3f * J106_FS,
                depth = (mode == 1u ? 1.65e-3f : 1.8e-3f) * J106_FS;
    const float lp = 0.67f;                   /* 1 - exp(-2 pi 7.7 kHz / FS) */
    uint32_t w = P->ch_w, ph = P->ch_lfo, inc = (uint32_t)(rate * (4294967296.0f / J106_FS));
    const float *row = j106_dith_row((uint32_t)t->index * 8u + 6u);
    float l0 = P->ch_lp[0], l1 = P->ch_lp[1];
    for (uint32_t i = 0; i < n; i++) {
        float x = (float)out[i];
        P->line[w] = x;
        float tri = j106_tri(ph);
        float d0 = center + depth * tri, d1 = center - depth * tri;
        float r0 = (float)w - d0 + (float)J106_CH_LEN, r1 = (float)w - d1 + (float)J106_CH_LEN;
        uint32_t i0 = (uint32_t)r0, i1 = (uint32_t)r1;
        float f0 = r0 - (float)i0, f1 = r1 - (float)i1;
        float a = P->line[i0 & (J106_CH_LEN - 1u)], b = P->line[(i0 + 1u) & (J106_CH_LEN - 1u)];
        float c = P->line[i1 & (J106_CH_LEN - 1u)], d = P->line[(i1 + 1u) & (J106_CH_LEN - 1u)];
        l0 += (a + (b - a) * f0 - l0) * lp;
        l1 += (c + (d - c) * f1 - l1) * lp;
        float L = (x + l0) * 0.7071f, R = (x + l1) * 0.7071f;
        if (left) {
            left[i] = j106_dith_cut(L, row[i]);
            out[i] = j106_dith_cut(L + R, row[i]);
        } else
            out[i] = j106_dith_cut((L + R) * 0.5f, row[i]);
        w = (w + 1u) & (J106_CH_LEN - 1u);
        ph += inc;
    }
    P->ch_w = w; P->ch_lfo = ph; P->ch_lp[0] = l0; P->ch_lp[1] = l1;
}
/* After the voices: the high-pass (0 lifts the lows, 1 is flat, 2 and 3 cut progressively), as the instrument filters
 * its mixed voices once, then the chorus. */
static void j106_post(mod_part_v3 *t, int32_t *out, uint32_t n, uint32_t nr)
{
    j106_part_t *P = j106_part(t);
    if (!P) return;
    if (!nr) {
        if (!P->quiet) {
            P->hp_x = P->hp_y = P->boost = P->ch_lp[0] = P->ch_lp[1] = 0.0f;
            for (uint32_t i = 0; i < J106_CH_LEN; i++) P->line[i] = 0.0f;
            P->quiet = 1;
        }
        return;
    }
    P->quiet = 0;
    uint32_t hpf = j106_hpf(j106_raw(t));
    const float *d = j106_dith_row((uint32_t)t->index * 8u + 2u);
    if (hpf != P->hpf) { P->hp_x = P->hp_y = P->boost = 0.0f; P->hpf = (uint8_t)hpf; }
    if (hpf == 0u) {                          /* low shelf, about +4 dB below 80 Hz */
        const float a = 1.0f - 0.98866f;      /* 1 - exp(-2 pi 80 / FS) */
        float lp = P->boost;
        for (uint32_t i = 0; i < n; i++) {
            float x = (float)out[i];
            lp += (x - lp) * a;
            out[i] = j106_dith_cut(x + lp * 0.6f, d[i]);
        }
        P->boost = lp;
    } else if (hpf > 1u) {                    /* one-pole high-pass, about 240 Hz (2) or 720 Hz (3) */
        const float k = hpf == 2u ? 0.96639f : 0.90244f;   /* exp(-2 pi f / FS) */
        float x1 = P->hp_x, y1 = P->hp_y;
        for (uint32_t i = 0; i < n; i++) {
            float x = (float)out[i];
            y1 = (y1 + x - x1) * k;
            x1 = x;
            out[i] = j106_dith_cut(y1, d[i]);
        }
        P->hp_x = x1; P->hp_y = y1;
    }
    j106_chorus_out(t, P, out, n);
}
static uint32_t j106_cap(const mod_part_v3 *t) { (void)t; return J106_POLY; }

/* ------------------------------------------------------ the panel values --- */
/* the switches (the schema's MOD3_SK_CALL parameters: RANGE, CHORUS, HPF): their values from the patch */
static int32_t j106_pget(const mod_part_v3 *t, const uint8_t *patch, uint32_t id)
{
    (void)t;
    return id >= J106_RAW && id < J106_FIELDS ? (int32_t)j106_switch(patch, id) : id < J106_RAW ? patch[id] : 0;
}
static int j106_pput(mod_part_v3 *t, uint8_t *patch, uint32_t id, int32_t v)
{
    (void)t;
    if (id < J106_RAW || id >= J106_FIELDS || v < 0)
        return 0;
    uint8_t s1 = patch[J106_SW1], s2 = patch[J106_SW2];
    j106_switch_set(patch, id, (uint32_t)v);
    return s1 != patch[J106_SW1] || s2 != patch[J106_SW2];
}
/* Conservative signal-path hints, as on the Prophet's panel: dim a control whose waveform, destination or mode is off.
 * It stays editable. (Saadet's also stop hinting while a matrix route modulates: the SDK names no matrix parameter.) */
static int j106_pactive(const mod_part_v3 *t, const uint8_t *p, uint32_t id)
{
    int pulse = (p[J106_SW1] >> 3) & 1u, pwm = pulse && p[J106_DCO_PWM];
    int lfo = p[J106_DCO_LFO] || p[J106_VCF_LFO] || t->mw || (pwm && !(p[J106_SW2] & 1u));
    switch (id) {
    case J106_DCO_PWM: return pulse;
    case J106_PWM_MODE: return pwm;
    case J106_LFO_RATE: case J106_LFO_DELAY: return lfo;
    case J106_ENV_POL: return p[J106_VCF_ENV] != 0;
    case J106_ENV_A: case J106_ENV_D: case J106_ENV_S: case J106_ENV_R:
        return !(p[J106_SW2] & 4u) || p[J106_VCF_ENV];
    default: return 1;
    }
}

/* ----------------------------------------------------------------- SysEx --- */
/* The patch as the instrument's patch message (F0 41 30 0n pp, sixteen sliders, SW1, SW2, F7). No bank dump: the
 * instrument has none (the host sends user slots one by one). */
static uint32_t j106_sysex_dump(const uint8_t *patch, uint32_t what, uint32_t ch, uint8_t *out, uint32_t max)
{
    j106_patch_t p;
    if ((what & 255u) == MOD3_DUMP_BANK)
        return 0;
    for (uint32_t i = 0; i < J106_BLOB; i++) ((uint8_t *)&p)[i] = patch[i];
    return j106_patch_encode(&p, ch, out, max);
}
/* Roland frames F0 41 30 / 31 / 32 (the schema's SYSEX patterns): a patch or manual-mode message is offered for a
 * user slot (the USER page asks where); a single control (F0 41 32 0n cc vv F7) edits the selected part's patch when
 * it plays JUNO-106. */
static uint32_t j106_sysex(const uint8_t *f, uint32_t n, uint32_t ev)
{
    j106_patch_t patch;
    if (ev != MOD3_SX_END)
        return ev == MOD3_SX_ABORT ? MOD3_SXR_DONE : MOD3_SXR_MORE;
    if (n < 4u || f[1] != 0x41u)
        return MOD3_SXR_DONE;
    if (f[2] == 0x32u) {
        int k = mh->sx_part(j106_eng, MOD3_SX_SEL);
        const uint8_t *now = k >= 0 ? mh->part_patch((uint32_t)k, j106_eng) : 0;
        if (n == 7u && f[3] <= 15u && f[4] < J106_RAW && f[5] <= 127u && now) {
            uint8_t b[J106_BLOB];
            for (uint32_t i = 0; i < J106_BLOB; i++) b[i] = now[i];
            b[f[4]] = f[5];
            (void)mh->patch_put((uint32_t)k, j106_eng, b, J106_BLOB, MOD3_PATCH_EDIT);
        }
    } else if (j106_patch_decode(&patch, f, n)) {
        (void)mh->sx_stage(j106_eng, (const uint8_t *)&patch, 1, sizeof patch);
    } else {
        mh->message("INVALID JUNO-106");
    }
    return MOD3_SXR_DONE;
}

/* ---------------------------------------------------------------- engine --- */
static const mod_engine_v3 ENG_J106 = {
    .name = "JUNO-106", .page_title = {"FILTER/TUNE", "DCO/MIX"},
    .edit = {{"CUT", MOD3_F_INT, -63, 63, 0, 0, 0}, {"RES", MOD3_F_INT, -63, 63, 0, 0, 0},
             {"TUNE", MOD3_F_SEMI, -24, 24, 0, 0, 0}, {"ENV", MOD3_F_INT, -63, 63, 0, 0, 0},
             {"LFO", MOD3_F_INT, -63, 63, 0, 0, 0}, {"PWM", MOD3_F_INT, -63, 63, 0, 0, 0},
             {"SUB", MOD3_F_INT, -63, 63, 0, 0, 0}, {"NOISE", MOD3_F_INT, -63, 63, 0, 0, 0}},
    .npresets = 129, .poly = J106_POLY, .ownenv = 1,
    .knob = {MOD3_P_E0, MOD3_P_E0 + 1, MOD3_P_E0 + 3, MOD3_P_REL},
    .note_on = j106_note_on, .render = j106_render, .done = j106_done, .block = j106_block, .post = j106_post,
    .cap = j106_cap, .state = j106_state,
    .pget = j106_pget, .pput = j106_pput, .pactive = j106_pactive,
    .sysex = j106_sysex, .sysex_dump = j106_sysex_dump,
};

/* the host table, and the number this device gave the module (its SysEx names it) */
static int j106_bind(const mod_host_v3 *h, uint32_t self)
{
    j106_eng = MOD3_SELF_ENGINE(self);
    return md_bind(h, self);
}

#if defined(MOD_HOSTED) && !defined(MOD_IN_CORE)  /* (the schema: the builder's host build writes the SCHM chunk) */
#include "j106_schema.h"
MOD3_SCHEMA(J106_SCHEMA);
#endif
MOD3_IDENT("saadet", "J106");                     /* its UID: CRC-32 of "saadet/JUNO-106" */
#ifndef MOD_IN_CORE                               /* (the presets: the file's PRST chunk, never the image's) */
#include "j106_presets.h"
#else
#define J106_NPRESETS 129u
#endif
_Static_assert(J106_NPRESETS == 129u, "INIT and Roland's 128");
MOD3_EXPORT(mod_j106) = {
    .magic = MOD3_MAGIC, .abi = MOD3_ABI, .size = sizeof(mod_desc_v3), .host = MOD3_SDK_HOST,
    .eng_size = sizeof(mod_engine_v3), .key = 0, .bind = j106_bind,   /* (MOD3_SDK_HEAD, its own bind) */
    .engine = 22,                                 /* the number it asks for (a device gives the first free one if taken) */
    .name = "JUNO-106",
    .npresets = J106_NPRESETS,
    .eng = &ENG_J106,
};
