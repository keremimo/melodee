/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* CZ behavior checks against the manual's rate/target and waveform rules.
 * These are architectural regressions, not hardware audio parity measurements. */
#define main hostsim_main
#include "hostsim.c"
#undef main
static int fails;
static void check(const char *s, int ok) { printf("CZ: %s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }
int main(void)
{
    cz_env_def_t d = {0}; cz_env_t e = {0};
    d.end = 7; d.sustain = 3;
    for (uint32_t k = 0; k < 8; k++) d.rate[k] = 1 << 24;
    d.level[0] = 1 << 23; d.level[1] = 1 << 22;
    d.level[2] = 1 << 23; d.level[3] = 1 << 22;
    d.level[4] = 3 << 22; d.level[5] = 1 << 22; d.level[6] = 1 << 23;
    for (uint32_t k = 0; k < 10; k++) cz_env_tick(&e, &d, 1);
    check("arbitrary up/down points reach and hold sustain", e.stage == 3 && e.level == d.level[3]);
    d.rate[4] = 1 << 20;
    cz_env_tick(&e, &d, 0);
    check("release can rise before falling", e.level > d.level[3] && e.stage >= 4);
    for (uint32_t k = 0; k < 20; k++) cz_env_tick(&e, &d, 0);
    check("END forces zero without an extra release", e.stage == 8 && e.level == 0);
    memset(&e, 0, sizeof e); e.gate = 1; e.level = 1 << 20;
    d.rate[4] = 0;
    cz_env_tick(&e, &d, 0);
    check("early key-off skips to first post-sustain point", e.stage == 4 && e.level == 1 << 20);
    memset(&e, 0, sizeof e); memset(&d, 0, sizeof d);
    d.end = 2; d.sustain = 1; d.rate[0] = d.rate[1] = 1 << 24;
    d.level[0] = 1 << 22; d.level[1] = 1 << 23;
    cz_env_tick(&e, &d, 1);
    check("short segments carry time across points", e.stage == 1 && e.level == 1 << 23);
    memset(&e, 0, sizeof e); d.sustain = 255; d.rate[2] = 1 << 24;
    for (uint32_t k = 0; k < 5; k++) cz_env_tick(&e, &d, 1);
    check("no-sustain envelope completes with key held", e.stage == 3 && !e.level);
    int maxerr = 0, bounded = 1;
    for (uint32_t w = 0; w < 8; w++) {
        pd_t b; pd_setup(&b, w, 0);
        for (uint32_t ph = 0; ph < 65536; ph += 31) {
            int err = abs(pd_wave(&b, ph) - pd_cos(ph)); if (err > maxerr) maxerr = err;
        }
        for (uint32_t dcw = 0; dcw <= 65535; dcw += 21845) {
            pd_setup(&b, w, dcw);
            for (uint32_t ph = 0; ph < 65536; ph += 13)
                bounded &= abs(pd_wave(&b, ph)) <= 32767;
        }
    }
    printf("CZ: zero-DCW cosine maximum error %d Q15 units\n", maxerr);
    check("all eight waveforms become cosine at zero DCW", maxerr <= 8);
    check("oscillator corners stay bounded", bounded);
    pd_t b; pd_setup(&b, 0, 65535);
    check("saw reads its first half-cycle faster (Casio PD diagram)", b.x < 32768 && pd_wave(&b, b.x) > 32760);
    for (uint32_t w = 5; w < 8; w++) {
        pd_setup(&b, w, 65535);
        check("resonant sync returns to its baseline", abs(pd_wave(&b, 65535) + 32767) <= 4);
        int smooth = 1;
        for (uint32_t dcw = 0; dcw < 65536; dcw += 4096) {
            pd_setup(&b, w, dcw);
            smooth &= abs(pd_wave(&b, 0) - pd_wave(&b, 65535)) < 8;
        }
        check("fractional resonant cores join smoothly at sync", smooth);
    }
    memset(trk, 0, sizeof trk);trk_voices_bind(); host_tracks_init();
    track_t *t = &trk[0]; int32_t out[CTL];
    host_preset(t, 2, 0); t->p[P_E7] = 1; t->p[P_E4] = 12;
    t->p[P_FM1_ATK] = 100; t->p[P_FM2_ATK] = 0;
    t->p[P_FM3_ATK] = 100; t->p[P_FM4_ATK] = 80;
    trk_note_on(t, 60, 100); voice_t *v = &t->v[0];
    track_render(t, out, CTL); cz_voice_t *c = cz_voice(t, v);
    check("two DCW envelopes progress independently", c->eg[0][1].level < c->eg[1][1].level);
    check("second DCA progresses independently", c->eg[1][2].level < c->eg[0][2].level);
    check("pitch envelope progresses independently", c->eg[0][0].level != c->eg[0][2].level);
    int32_t source = phase_env_source(t, v);
    t->p[P_M1SRC] = MS_ENV; t->p[P_M1DST] = MD_SHP; t->p[P_M1AMT] = 32;
    mod_begin(t); track_render(t, out, CTL); mod_end(t);
    check("matrix ENV follows the native DCA", source > 0 && t->m_env == source);
    int32_t untouched = c->eg[0][2].level;
    memset(&trk[1].v[0], 0, sizeof trk[1].v[0]); phase_note_on(&trk[1], &trk[1].v[0]);
    check("envelope state is isolated per track", untouched == c->eg[0][2].level);
    trk_note_off(t, 60);
    for (uint32_t k = 0; k < FS * 12 / CTL; k++) track_render(t, out, CTL);
    check("voice lifetime follows both native DCAs", !v->active);
    host_preset(t, 2, 0); trk_note_on(t, 60, 100); track_render(t, out, CTL);
    voice_kill(&t->v[0]); track_render(t, out, CTL);
    check("shared voice stealing still frees a native-envelope voice", !t->v[0].active);
    check("split descriptors keep stored ranges/defaults", PH_ED[0].max == TP[P_FM1_ATK].max && PH_ED[4].def == TP[P_FM1_LEVEL].def);
    check("PHASE remains its own engine with six presets", ENGINES[2] == &ENG_PHASE && !strcmp(ENG_PHASE.name,"PHASE") && ENG_PHASE.npresets == 6);
    memset(trk,0,sizeof trk);trk_voices_bind(); host_tracks_init(); t=&trk[0]; cz_init(); eng_state_clear(0);
    host_preset(t, ENGI_CZ, 0);
    check("CZ-1 factory defaults to native playback", ENGINES[ENGI_CZ] == &ENG_CZ &&
        !strcmp(ENG_CZ.name,"CZ-1") && t->p[P_E7] == CZ_NATIVE && cz_patch_valid(cz_patch_of(0)->raw));
    cz_patch_of(0)->raw[128]='X';
    host_preset(&trk[1],2,0);
    check("selecting PHASE on another track leaves native CZ tone intact",cz_patch_of(0)->raw[128]=='X' && trk[1].eng_req==2);
    host_preset(t,ENGI_CZ,0);
    check("factory INIT replaces previous imported tone",cz_patch_of(0)->raw[128]=='I');
    uint8_t *raw=cz_patch_of(0)->raw;raw[20]=0xF7;
    for(uint32_t k=0;k<8;k++){raw[21+2*k]=119;raw[22+2*k]=(uint8_t)(k%2?60:127);}raw[28]|=128;
    trk_note_on(t,60,100);v=&t->v[0];
    for(uint32_t k=0;k<30;k++){memset(out,0,sizeof out);track_render(t,out,CTL);}
    c=cz_voice(t,v);check("imported eight-point native DCA sustains at fourth point",c->native_eg[0][2].stage==3 && c->native_eg[0][2].level==cz_hw_target(60,2));
    check("chip rate law and separate DCW/DCA domains",fabsf(cz_hw_rate(119,1)-cz_hw_rate(119,2)*4.0f)<1e-5f);
    trk_note_off(t,60);for(uint32_t k=0;k<100;k++){memset(out,0,sizeof out);track_render(t,out,CTL);}
    check("native key-off plays remaining points then frees voice",!v->active);
    cz_hw_pd_t hw;int valid=1;for(uint32_t w=0;w<8;w++)for(uint32_t win=0;win<8;win++)for(uint32_t depth=0;depth<=1023;depth+=341){cz_native_pd(&hw,(w<<13)|(win<<6),depth);for(uint32_t ph=0;ph<2048;ph+=17)valid &= fabsf(cz_native_wave(&hw,ph<<21,0))<=1.000001f;}
    check("native hidden wave/window combinations stay bounded",valid);
    return fails != 0;
}
