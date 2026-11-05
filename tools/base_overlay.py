# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Checked adaptations of the pinned base; upstream files keep their notices.

Two bases are pinned (tools/build_base.py): upstream's for the FM-1, and the same commit with the SMK-37 board layer on
top. A patch that no longer matches its base fails the build. Where the boards differ, a patch is skipped on purpose,
and each such place says so.
"""
from pathlib import Path
import shutil


def replace(s, old, new):
    if old not in s:
        raise ValueError(f"Pinned fm1-base input changed: {old[:90]!r}")
    return s.replace(old, new)


def require(s, text):
    """the newer base already does what the FM-1's patch adds: it must still say so"""
    if text not in s:
        raise ValueError(f"Pinned fm1-base input changed: {text[:90]!r}")


def function(s, name, body):
    start = s.index(name)
    start = s.rfind('\n', 0, start) + 1
    opening = s.index('{', start)
    level = 1
    end = opening + 1
    while level:
        if s[end] == '{': level += 1
        if s[end] == '}': level -= 1
        end += 1
    return s[:start] + body.rstrip() + s[end:]


def patch_usb(s, wide=False):
    # wide: Saadet's 24-bit streams beside the 16-bit ones (twice the rings). Melodee streams 16-bit, as it always
    # did: only its rings move to the pool, where its arena's room is counted.
    if not wide:
        s = replace(s, 'static uint32_t uo_ring[UO_N];', 'static uint32_t uo_ring[UO_N] __attribute__((section(".pool")));')
    else:
        # Native stereo frames throughout both rings; format conversion at USB only.
        s = replace(s, '#define UAC_MAXP (UA_MAXF * 4u)', '#define UAC_MAXP (UA_MAXF * 6u)')
        s = replace(s, 'static uint32_t ua_ring[UA_N] FELUCCA_UAC_RING_ATTR;',
            'typedef struct { int32_t l, r; } ml_pcm_frame;\nstatic ml_pcm_frame ua_ring[UA_N] __attribute__((section(".pool")));')
        s = replace(s, 'static uint32_t ep4tx[UA_MAXF]', 'static uint32_t ep4tx[(UAC_MAXP + 3u) / 4u]')
        s = replace(s, 'uint32_t last;               /* the last frame sent, repeated on an underrun */',
            'ml_pcm_frame last;           /* native frame held on underrun */')
        s = replace(s, '#define UO_MAXP (UO_MAXF * 4u)', '#define UO_MAXP (UO_MAXF * 6u)')
        s = replace(s, 'static uint32_t uo_ring[UO_N];', 'static ml_pcm_frame uo_ring[UO_N] __attribute__((section(".pool")));')
        s = replace(s, 'static uint32_t ep4rx[UO_MAXF + 1u]', 'static uint32_t ep4rx[(UO_MAXP + 3u) / 4u + 1u]')
        # Both widths are selectable per direction; all formats remain 44.1 kHz.
        alt = '''#define ML_AS24(s, terminal, ep, attr) \\
        9, 4, (s), 2, 1, 1, 2, 0, 0, \\
        7, 0x24, 1, (terminal), 1, 1, 0, \\
        11, 0x24, 2, 1, 2, 3, 24, 1, 0x44, 0xAC, 0, \\
        9, 5, (ep), (attr), (UA_MAXF*6u)&255u, (UA_MAXF*6u)>>8, 1, 0, 0, \\
        7, 0x25, 1, ((ep)&128u)?1:0, 0, 0, 0,
    #if FELUCCA_UAC
    #define ML_CAP24(s) ML_AS24(s,2,0x84,0x05)
    #else
    #define ML_CAP24(s)
    #endif
    #if FELUCCA_UAC_OUT
    #define ML_PLAY24(s) ML_AS24(s,3,0x04,0x09)
    #else
    #define ML_PLAY24(s)
    #endif
    #define D_AUDIO(a) D_AC(a) D_MIDI((a) + 1) D_AS((a) + 2) ML_CAP24((a)+2) D_ASO((a) + 3) ML_PLAY24((a)+3)'''
        # The base numbers the interfaces (IF_AS, IF_ASO, IF_MIDI) and may put MIDI last; its AC header takes the terminals'
        # length as a parameter.
        alt = alt[:alt.rindex('#define D_AUDIO')]
        s = replace(s, '/* audio + MIDI from interface a, in the order FELUCCA_UAC_MIDI_LAST picks */',
                    alt + '/* audio + MIDI from interface a, in the order FELUCCA_UAC_MIDI_LAST picks */')
        s = replace(s, '#define D_AUDIO(a) D_AC(a) D_AS((a) + IF_AS) D_ASO((a) + IF_ASO) D_MIDI((a) + IF_MIDI)',
                    '#define D_AUDIO(a) D_AC(a) D_AS((a) + IF_AS) ML_CAP24((a)+IF_AS) D_ASO((a) + IF_ASO) ML_PLAY24((a)+IF_ASO) D_MIDI((a) + IF_MIDI)')
        s = replace(s, '#define D_AUDIO(a) D_AC(a) D_MIDI((a) + IF_MIDI) D_AS((a) + IF_AS) D_ASO((a) + IF_ASO)',
                    '#define D_AUDIO(a) D_AC(a) D_MIDI((a) + IF_MIDI) D_AS((a) + IF_AS) ML_CAP24((a)+IF_AS) D_ASO((a) + IF_ASO) ML_PLAY24((a)+IF_ASO)')
        s = replace(s, '#define AUD_LEN (92 + (63 + FMT_LEN) * FELUCCA_UAC + 74 * FELUCCA_UAC_OUT - (FELUCCA_UAC_MIDI_LAST == 2))',
                    '#define AUD_LEN (92 + (106 + FMT_LEN) * FELUCCA_UAC + 117 * FELUCCA_UAC_OUT - (FELUCCA_UAC_MIDI_LAST == 2))')
        s = replace(s, 'wvalue <= 1u && usb.config', 'wvalue <= 2u && usb.config')
        s = replace(s, 'uac.last = 0;', 'uac.last = (ml_pcm_frame){0,0};')
        s = replace(s, 'ua_ring[(r + i) & (UA_N - 1u)] = 0;', 'ua_ring[(r + i) & (UA_N - 1u)] = (ml_pcm_frame){0,0};')
        helper = '''static int32_t ml_pcm_read(const uint8_t *p, uint32_t width)
    {
        uint32_t v=p[0]|(uint32_t)p[1]<<8;
        if(width==2u)return (int32_t)(int16_t)v*256;
        v|=(uint32_t)p[2]<<16;
        return (v&0x800000u)?(int32_t)v-16777216:(int32_t)v;
    }
    static uint32_t ml_pcm_rng=0x913ab267u;
    static uint32_t ml_pcm_random(void){uint32_t x=ml_pcm_rng;x^=x<<13;x^=x>>17;x^=x<<5;return ml_pcm_rng=x;}
    static void ml_pcm_write(uint8_t *p,uint32_t width,int32_t v)
    {
        v=v>8388607?8388607:v<-8388608?-8388608:v;
        if(width==2u){
            if(v)v+=(int32_t)(ml_pcm_random()&255u)-(int32_t)(ml_pcm_random()&255u)+128;
            v>>=8;v=v>32767?32767:v<-32768?-32768:v;
        }
        uint32_t q=(uint32_t)v;p[0]=(uint8_t)q;p[1]=(uint8_t)(q>>8);
        if(width==3u)p[2]=(uint8_t)(q>>16);
    }
    '''
        s = replace(s, 'static inline int32_t uac_s16(', helper + '\nstatic inline int32_t uac_s16(')
        s = function(s, 'void uac_tap(const int32_t *out', '''static __attribute__((noinline)) void uac_tap(const int32_t *out,uint32_t n)
    {
        uint32_t w=ua_w;if(!uac.feed)return;
        if(UA_N-(w-ua_r)<n){uac.overruns++;return;}
        for(uint32_t i=0;i<n;i++)ua_ring[(w+i)&(UA_N-1u)]=(ml_pcm_frame){out[2u*i],out[2u*i+1u]};
        RING_PUBLISH();ua_w=w+n;
    }''')
        s = function(s, 'uint32_t uac_packet(uint32_t *d)', '''static uint32_t uac_packet(uint32_t *d)
    {
        uint32_t n=44u,width=uac.alt==2u?3u:2u;
        uac.acc+=100u;if(uac.acc>=1000u){uac.acc-=1000u;n++;}
        uint32_t r=ua_r,fill=ua_w-r,k=0;
        if(uac.go){
            if(uac.fill_min>UA_HI){n++;uac.adj_up++;}
            else if(uac.fill_min<UA_LO){n--;uac.adj_down++;}
            k=fill<n?fill:n;if(k<n)uac.underruns++;
        }
        RING_PUBLISH();
        ml_pcm_frame last=uac.go?uac.last:(ml_pcm_frame){0,0};
        uint8_t *p=(uint8_t *)d;
        for(uint32_t i=0;i<n;i++){
            if(i<k)last=ua_ring[(r+i)&(UA_N-1u)];
            ml_pcm_write(p+2u*i*width,width,last.l);
            ml_pcm_write(p+(2u*i+1u)*width,width,last.r);
        }
        uac.last=last;RING_PUBLISH();ua_r=r+k;
        uac.pkts++;uac.frames+=n;return n;
    }''')
        s = replace(s, 'n = (sie_rd(S_RXCOUNT1) | (sie_rd(S_RXCOUNT2) << 8)) / 4u;',
            'uint32_t width=uo.alt==2u?3u:2u;\n    n = (sie_rd(S_RXCOUNT1) | (sie_rd(S_RXCOUNT2) << 8)) / (2u*width);')
        s = replace(s, 'uo_ring[(w + i) & (UO_N - 1u)] = ep4rx[i];',
            'uo_ring[(w+i)&(UO_N-1u)]=(ml_pcm_frame){ml_pcm_read((uint8_t *)ep4rx+2u*i*width,width),ml_pcm_read((uint8_t *)ep4rx+(2u*i+1u)*width,width)};')
        s = replace(s, '(float)gain_q12 * (1.0f / 16.0f)', '(float)gain_q12 * (1.0f / 4096.0f)')
        s = replace(s, 'uint32_t k = pos >> 16, a = uo_ring[(r + k) & (UO_N - 1u)], b = uo_ring[(r + k + 1u) & (UO_N - 1u)];',
            'uint32_t k=pos>>16;ml_pcm_frame a=uo_ring[(r+k)&(UO_N-1u)],b=uo_ring[(r+k+1u)&(UO_N-1u)];')
        s = replace(s, '(float)(int16_t)a + ((float)(int16_t)b - (float)(int16_t)a)', '(float)a.l + ((float)b.l - (float)a.l)')
        s = replace(s, '(float)(int16_t)(a >> 16) + ((float)(int16_t)(b >> 16) - (float)(int16_t)(a >> 16))', '(float)a.r + ((float)b.r - (float)a.r)')
        s = replace(s, 'fm1_usb_ep4_send(ep4tx, n * 4u);', 'fm1_usb_ep4_send(ep4tx, n * (uac.alt==2u?6u:4u));')
    # Melodee's standard playback master mute/volume stays available to the OS.
    s=replace(s,'D_AC_HEADER((a), 42)','D_AC_HEADER((a), 52)')
    s=replace(s,'9, 0x24, 3, 4, 0x01, 0x03, 0, 3, 0,', '10, 0x24, 6, 5, 3, 1, 3, 0, 0, 0, 9, 0x24, 3, 4, 0x01, 0x03, 0, 5, 0,')
    s=replace(s,'117 * FELUCCA_UAC_OUT', '127 * FELUCCA_UAC_OUT') if wide else replace(s,'74 * FELUCCA_UAC_OUT', '84 * FELUCCA_UAC_OUT')   # (+ the feature unit)
    s=replace(s,'static void ep0_service(void)', '#if FELUCCA_UAC_OUT\n#include "usb_volume.h"\n#endif\nstatic void ep0_service(void)')
    s=replace(s,'    for (i = 0; i < 8u; i++)\n        s[i] = ep0buf[i];', '#if FELUCCA_UAC_OUT\n    if(ml_volume_pending){\n        int ok=ml_volume_data(ep0buf,sie_rd(S_COUNT0));if(ok)goto ack;goto stall;\n    }\n#endif\n    for (i = 0; i < 8u; i++)\n        s[i] = ep0buf[i];')
    s=replace(s,'    usb.setups++;', '    usb.setups++;\n#if FELUCCA_UAC_OUT\n    if(ml_volume_setup(s))return;\n#endif')
    s=replace(s,'        uac.e0_rx = 0;', '        uac.e0_rx = 0;\n#if FELUCCA_UAC_OUT\n        ml_volume_pending=0;\n#endif')
    s=replace(s,'    uo_ep4_reset();\n    uo_stream(0);', '    uo_ep4_reset();\n    uo_stream(0);\n    ua_host_volume_reset();')
    # These reset routines precede the control implementation.
    s=replace(s,'static void uo_ep4_reset(void)', 'static void ua_host_volume_reset(void);\nstatic void uo_ep4_reset(void)')
    s=replace(s,'        float l = (float)'+('a.l' if wide else '(int16_t)a'), '        int32_t target=ua_host_mute?0:ua_host_target_q15;\n        ua_host_gain_q15+=(target-ua_host_gain_q15)/128;\n        if(ua_host_gain_q15!=target&&target-ua_host_gain_q15<128&&target-ua_host_gain_q15>-128)ua_host_gain_q15=target;\n        float volume=(float)ua_host_gain_q15*(1.0f/32768.0f);\n        float l = (float)'+('a.l' if wide else '(int16_t)a'))
    s=replace(s,'(int32_t)(l * g)', '(int32_t)(l * g * volume)')
    s=replace(s,'(int32_t)(rr * g)', '(int32_t)(rr * g * volume)')
    # Raw streams reach the existing bounded native-bank parsers even when too
    # large for the base's app/editor frame collector. Real-time bytes survive.
    s = replace(s, 'static void sysex_byte(uint8_t b)\n{', 'static void sysex_byte(uint8_t b)\n{\n#ifndef FELUCCA_LOADER\n    mlb_sysex_byte(b);\n#endif')
    # Match Melodee's USB receive fix (998ca1a3, by Kerem Kilic): hosts can
    # deliver any SysEx byte as CIN F, including at a USB packet-list boundary.
    # Route those through both collectors before status handling can cancel F0/F7.
    s=replace(s,'    if (st >= 0x80u && st < 0xF8u) {', '''    if (cin == 0xFu && st < 0xF8u && (usb.sx_on || st == 0xF0u)) {
        sysex_byte((uint8_t)st);
        return;
    }
    if (st >= 0x80u && st < 0xF8u) {''')
    s=replace(s,'    if (st >= 0x80u && st < 0xF8u) {','    if (st >= 0x80u && st < 0xF8u) {\n#ifndef FELUCCA_LOADER\n        mlb_sysex_byte((uint8_t)st); /* cancel an interrupted native bank */\n#endif')
    return s


def smk37_toml(source, model, name):
    """platform/fm1.toml for one SMK-37 model: its board and model, its USB product (the base adds " SMK-37")."""
    text = replace(source, 'usb_product = "Melodee FM-1"', f'usb_product = "Melodee {name}"')
    text = replace(text, 'o2_cflags = ["-DMELODEE_RAM_AUDIO=1", "-Os"]', 'o2_cflags = ["-DMELODEE_RAM_AUDIO=1", "-Os", "-DMELODEE_SMK37=1"]')
    return f'board = "smk37"\nmodel = "{model}"\n' + text


def smk37_linker(source):
    """platform/app.ld (Melodee's map) with the SMK's app slot (0x4000..0x9CFFF, as the base's device/app_smk37.ld has
    it) and its resource region (hal/fm1_flash_map.h FL_RES: 0x9F000..0xAEFFF)."""
    text = replace(source, 'XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x9DFBC   /* the app slot (0x8DFBC) and 64 KB',
                   'XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0xA7FBC   /* the app slot (0x97FBC) and 64 KB')
    text = replace(text, 'RES    (r)   : ORIGIN = 0x020C7020, LENGTH = 0x14FE0', 'RES    (r)   : ORIGIN = 0x0209B020, LENGTH = 0xFFE0')
    return replace(text, '_app_xip_end = ORIGIN(XIP) + 0x8DFBC;', '_app_xip_end = ORIGIN(XIP) + 0x97FBC;')


def apply(work, board="fm1", model=None, product="Melodee"):
    smk = board == "smk37"
    if smk:
        # The main loop starts after the jack's settling window (the boot's own delays are most of it), so the rest level
        # was never read: it stayed 0, any reading above the step read as the pedal down, sustain on for the first minute.
        # The first look after settling, as the pitch wheel's centre.
        p=work/'device/plat_smk37.c';s=p.read_text()
        s=replace(s,'    if (fm1_in.frames < SMK_SETTLE_FRAMES) {\n        smk_pedal_rest = r;\n        return;\n    }\n    if (!smk_pc) {\n',
            '    if (fm1_in.frames < SMK_SETTLE_FRAMES)\n        return;\n    if (!smk_pc) {\n        smk_pedal_rest = r;\n')
        # K5 is the virtual MASTER, except while Melodee's 8 KNOBS bank has it (firmware/src/ui_smk.c): then the app takes
        # its detents through plat_k5_lend and the level stays where it is.
        s=replace(s,'static void smk_main_poll(void)','''/* Melodee: K5 lent to the app (on), its detents since the last call; lent, it turns no MASTER */
static uint8_t smk_k5_lent;
int32_t plat_k5_lend(int on)
{
    smk_k5_lent = (uint8_t)(on != 0);
    return on ? fm1_enc_take(SMK_MASTER_ENC) : 0;
}
static void smk_main_poll(void)''')
        s=replace(s,'    int32_t s = fm1_enc_take(SMK_MASTER_ENC);\n','    int32_t s = smk_k5_lent ? 0 : fm1_enc_take(SMK_MASTER_ENC);\n')
        p.write_text('/* Melodee pedal rest fix and K5 lending Copyright (C) 2026 Kerem Kilic (Ellic Studio) */\n'+s)
        p=work/'include/plat.h';s=p.read_text()
        s=replace(s,"void plat_master_set(uint32_t q12);    /* the virtual MASTER's level, 0..4096 */\n",
                  "void plat_master_set(uint32_t q12);    /* the virtual MASTER's level, 0..4096 */\n"
                  "int32_t plat_k5_lend(int on);         /* Melodee: on, K5 turns no MASTER; its detents since the last call */\n")
        p.write_text('/* Melodee K5 lending Copyright (C) 2026 Kerem Kilic (Ellic Studio) */\n'+s)
    p=work/'tools/build.py';s=p.read_text()
    s=replace(s,'    img = bytearray((OUT / "text.bin").read_bytes())', '    tc("common/bin/objcopy", "-O", "binary", "-j", ".audio_text", elf, OUT / "audio.bin")\n    img = bytearray((OUT / "text.bin").read_bytes())')
    s=replace(s,'    (OUT / (NAME + ".dis")).write_text(dis)', '    (OUT / (NAME + ".dis")).write_text(dis)\n    (OUT / (NAME + ".symbols")).write_text(syms)')
    # Melodee's resource region (firmware/src/res.c): .mres out of the image as platform/build/melodee-res.bin, its
    # length and CRC patched into res_want before the image is packed (tools/melodee_res.py, tools/build.py's resources)
    s=replace(s,'    img = bytearray((OUT / "text.bin").read_bytes())\n    if CONF.get("boot_pack"):',
              '    img = bytearray((OUT / "text.bin").read_bytes())\n'
              '    tc("common/bin/objcopy", "-O", "binary", "-j", ".mres", elf, OUT / "mres.bin")\n'
              f'    sys.path.insert(0, {str(Path(__file__).resolve().parent)!r})\n'
              '    import melodee_res\n'
              f'    melodee_res.patch(img, syms, OUT, APP_XIP, {board!r})\n'
              '    if CONF.get("boot_pack"):')
    if smk:
        # The generated fm1.toml (tools/build_base.py), and each model's outputs apart from the FM-1's: tests and the
        # FM-1's tools read platform/build's melodee.bin.
        s=replace(s,'CONF = read_conf(APP / "fm1.toml")','CONF = read_conf(SRC / "melodee-fm1.toml")')
        s=replace(s,'    OUT = APP / "build"\n','    OUT = APP / "build" / f"smk37-{MODEL}"\n')
    p.write_text(s)
    # boot_pack: the resident audio code is a load image too, and much the largest. Its own record after .data's.
    p=work/'tools/boot_pack.py';s=p.read_text()
    s=replace(s,"SECTIONS = ('ramtext.bin', 'data.bin')","SECTIONS = ('ramtext.bin', 'data.bin', 'audio.bin')")
    p.write_text(s)
    p=work/'bsp/chip/fm1_guard.h';s=p.read_text()
    s=replace(s,'re = (uint32_t)(uintptr_t)_rt_end;', 're = (uint32_t)(uintptr_t)_sd_exec_end;')
    s=replace(s,'static void fm1_guard_enable(', 'extern uint32_t _sd_exec_end[];\nstatic void fm1_guard_enable(')
    p.write_text(s)
    p=work/'bsp/chip/usb.c';s=patch_usb(p.read_text());p.write_text('/* Melodee integration modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */\n'+s)
    p=work/'device/main_fm1.c';s=p.read_text()
    # Use Melodee's complete stock V15 supply sequence and checked result,
    # rather than the base's two-rail-only raises. CPU1 stays held until the
    # DCDC/LDO mode, IO supplies and CPU/SRAM supplies have all verified.
    start=s.index('        for (rails_tries = 1;')
    # The base starts the stock 360 MHz clock here too: that stays, and only the supplies are Melodee's.
    end=s.index('        fm1_clock_read(clock_boot);',start)
    s=s[:start]+'        rails_tries = 1;\n        ml_power_init(); /* Melodee stock V15 power setup; CPU1 held, IRQs off */\n'+s[end:]
    # Melodee firmware/src/main.c defers the PC limits until CPU1's bounded
    # handshake returns: CPU1 first executes mask-ROM outside our XIP/SRAM
    # windows. Enabling them in cstart faults that bootstrap (DBG bit 10).
    # Keep stack/write/bus guards on throughout, and arm PC limits even when
    # app_init skips CPU1 or the handshake stops it on timeout.
    s=replace(s,'    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);',
        '    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS);')
    s=replace(s,'    app_init();                                     /* the app\'s engine and its saved state (or defaults) */',
        '    app_init();                                     /* the app\'s engine and its saved state (or defaults) */\n'
        '    fm1_guard_enable(FM1_GUARD_PC);                  /* CPU1 has left mask-ROM, or is held after timeout */')
    # Include the already-recorded causes, so an on-device CPU exception can
    # be distinguished from a stack, PC-limit or invalid-bus fault over USB.
    s=replace(s,'        for (k = (int)FM1_CRASH_CTX_N - 1;',
        '        { char cause[]="emu 00000000 dbg 00000000";\n'
        '          hexs(cause+4,fm1_crash.emu); cause[12]=\' \';\n'
        '          hexs(cause+17,fm1_crash.dbg); log_line_send(cause); }\n'
        '        for (k = (int)FM1_CRASH_CTX_N - 1;')
    s=replace(s,'static void render_block(int32_t *o)','#include "ceiling.h"\nstatic void render_block(int32_t *o)')
    ceiling='            ml_ceiling(o + 2u * k, REND_FRAMES); /* physical -6 dB ceiling after native USB capture/playback */\n'
    # The base's device/uac_mix.h already takes the capture before the computer's audio: only the ceiling.
    s=replace(s,'#if FM1B_BT_AUDIO\n#ifdef APP_BT_FRAMES\n', ceiling+'#if FM1B_BT_AUDIO\n#ifdef APP_BT_FRAMES\n')
    s=replace(s,'    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;', '    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;\n    mlb_deadline(us,budget);')
    # The stack's counters on the log are BLE_DIAG's, which Melodee builds without (Melodee's setting).
    require(s,"#if FM1B_BLE_OWN\n#if BLE_DIAG ")             # (upstream since 3c4a57e: the line only with the counters)
    # The platform's render/tick closure includes P33 and matrix helpers also
    # used during early boot. Initialize their code AND far-call pointers before
    # the first platform helper executes; clear POOL before copying code into it.
    start=s.index('void fm1_cstart(void)')
    clear=s.index('    for (d = _bss_start;',start)
    end=s.index('    fm1_mailbox_clear();',clear)
    init=s[clear:end]
    # Every load image is packed (fm1.toml boot_pack), and a build that is not fails at its checks: the plain copy goes.
    # .audio_text lies in the pool: the decoder's tables at its start are cleared after, around the code just unpacked.
    i=init.index('    if (_rt_load[0] == BOOT_LZMA_MAGIC) {')
    init=init[:i]+'''    {
        const uint32_t *at = boot_image(_rt_load, _rt_start, _rt_end);
        at = boot_image(at, _data_start, _data_end);
        (void)boot_image(at, _sd_audio_start, _sd_audio_end);
    }
    for (d = _pool_start; d < _sd_audio_start; d++)
        *d = 0;
    for (d = _sd_audio_end; d < _pool_end; d++)
        *d = 0;
'''
    s=s[:clear]+s[end:]
    at=s.index('    fm1_time_init();',start)
    s=s[:at]+init+s[at:]
    s=replace(s,'extern uint32_t _data_start[]', 'extern uint32_t _sd_audio_start[],_sd_audio_end[];\nextern uint32_t _data_start[]')
    s=replace(s,'    uint32_t *s, *d, p3, src, wdt;\n#if FM1B_CPU2\n    fm1_cpu1_stop();','    uint32_t *d, p3, src, wdt;\n#if FM1B_CPU2\n    fm1_cpu1_stop();')
    p.write_text('/* Melodee integration modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */\n'+s)

    p=work/'bsp/chip/fm1_irq.h';s=p.read_text()
    # Match Melodee's CPU0 fp32 policy (firmware/hal/fm1_irq.h,
    # 97357ab). CPU1 already uses it in fm1_core1_main. Inexact arithmetic
    # and quiet filter tails are normal, not reasons to reset the synth.
    s=replace(s,'    FM1_EMU_CON &= ~(1u << 2);',
        '    FM1_EMU_CON &= ~((1u << 2) | (31u << 16)); /* Melodee: div0 and fp32 traps off, stack guard retained */')
    s=replace(s,'void fm1_fault_c(uint32_t *f)\n{','void fm1_fault_c(uint32_t *f)\n{\n    if(ml_fault_frame(f))return;')
    p.write_text('/* Melodee CPU1 recovery modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */\n'+s)
    require((work/'bsp/chip/fm1_vec.S').read_text(),'fm1_fault_c returns only after a fault of the second core')  # upstream has the resume

    if not smk:
        # Skipped on the SMK: its LEDs take the board's own levels (platform/app.c ml_lights), and the base's panel
        # map alone holds the 18 buttons (the UI keeps the FM-1's default ids, so calibration has nothing to tell it).
        p=work/'bsp/boards/fm1/fm1_input.h';s=p.read_text()
        s=replace(s,'#define FM1__LIT(c) (fm1_led[c] |', 'static volatile uint8_t ml_led_planes[3][11],ml_led_masks[3];\nstatic uint8_t ml_led_phase;\n#define ML_LED_PLANE(c,p) ((ml_led_masks[p]!=255u && !(ml_led_phase&ml_led_masks[p]))?ml_led_planes[p][c]:0u)\n#define FM1__LIT(c) (fm1_led[c] | ML_LED_PLANE(c,0) | ML_LED_PLANE(c,1) | ML_LED_PLANE(c,2) |')
        s=replace(s,'if (p == 0u)\n        fm1__breath_frame();','if (p == 0u) { ml_led_phase++; fm1__breath_frame(); }')
        p.write_text('/* Melodee LED planes Copyright (C) 2026 Kerem Kilic (Ellic Studio) */\n'+s)
        p=work/'device/panel.c';s=p.read_text()
        # Melodee keeps the calibration in its own settings (firmware/src/panel.c): the base's setup screen hands it
        # over, and the base's copy comes from Melodee at power-on (platform/app.c ml_panel_set)
        s=replace(s,'    panel.magic = PANEL_MAGIC;\n    plat_store_save(OBJ_PANEL, &panel, sizeof panel);\n',
                  '    panel.magic = PANEL_MAGIC;\n    mlb_panel_calibrated(panel.btn,panel.enc,panel.dir);\n')
        p.write_text('/* Melodee calibration bridge Copyright (C) 2026 Kerem Kilic (Ellic Studio) */\n'+s)

    # The store is Melodee's (firmware/src/storage.c, through platform/app.c ml_read / ml_erase / ml_write, in the
    # windows it always had): every FM-1 sector the base's objects could take is already one of its own, so the base's
    # store answers "none" and writes nothing.
    p=work/'device/plat_fm1.c';s=p.read_text()
    s=replace(s,'int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load(obj, dst, max) : -1; }\n'
                'int plat_store_head(uint32_t obj, void *dst, uint32_t n) { return flash_ok ? st_load_head(obj, dst, n) : -1; }\n'
                'int plat_store_size(uint32_t obj) { return flash_ok ? st_size(obj) : -1; }\n'
                'int plat_store_save(uint32_t obj, const void *src, uint32_t len) { return flash_ok ? st_save(obj, src, len) : -9; }\n',
              'int plat_store_load(uint32_t obj, void *dst, uint32_t max) { (void)obj; (void)dst; (void)max; return -1; }\n'
              'int plat_store_head(uint32_t obj, void *dst, uint32_t n) { (void)obj; (void)dst; (void)n; return -1; }\n'
              'int plat_store_size(uint32_t obj) { (void)obj; return -1; }\n'
              'int plat_store_save(uint32_t obj, const void *src, uint32_t len) { (void)obj; (void)src; (void)len; return -9; }\n')
    p.write_text('/* Melodee: the store is the app\'s, Copyright (C) 2026 Kerem Kilic (Ellic Studio) */\n'+s)

    # Bluetooth is the own stack (fm1.toml ble_stack = "own"): JieLi's libraries, shim and MIDI service are not built. Its
    # files are Melodee's copy with fm1-base's link fixes, kept in platform/ble (docs/BLE_MIGRATION.md): they replace the
    # base's bt/ble and the two radio drivers Melodee changed. fm1_rf_cal.h and the start-up tables stay felucca-bsp's,
    # which are the same.
    ble=Path(__file__).resolve().parents[1]/'platform/ble'
    shutil.rmtree(work/'bt/ble')
    (work/'bt/ble').mkdir()
    for f in ble.iterdir():
        if f.is_file():
            shutil.copyfile(f,work/'bt/ble'/f.name)
    for f in (ble/'hal').iterdir():
        shutil.copyfile(f,work/'bsp/chip'/f.name)
    # Melodee's settings for the stack (firmware/src/midi_ble.h): the read before the subscription, stock's radio-tested
    # 27-octet PDUs, no diagnostics, and its big buffers in the pool. Melodee's name, as it has always been.
    p=work/'device/bt_own.c';s=p.read_text()
    s=replace(s,'#define BLE_DEVICE_NAME "FM-1" ',f"""#define BLE_NAME_HEX 0
#define BLE_DIAG 0
#define BLE_MIDI_INITIAL_READ 1
#define BLE_LL_MAX_OCTETS 27u
#define BLE_BARRIER() RING_PUBLISH()
#define BLE_DMA_MEMORY __attribute__((section(".pool"), aligned(4)))
#define FM1_RF_CAL_MEMORY __attribute__((section(".pool")))
#define BLE_DEVICE_NAME "{product}" """)
    s=replace(s,'#include "ble_stack.c"\n','#include "ble_stack.c"\n#include "ble_vm.c"\n')
    # the calibration: stored first, then measured (ble_own.h); its copy kept with the address
    s=replace(s,"""    fm1_rf_cal_on = 1;                            /* (our own measurement, not a stored 187) */
    ble_hw_wl82_start(&OWN_TRIMS);                /* the radio's start-up, its calibration (crystal ramp from RAM) */
    own_cal_log();
""","""    {
        struct ble_rf_trims trims;
        ml_own_trims(&trims);                     /* stored, else measured (fm1_rf_cal_on) */
        ble_hw_wl82_start(&trims);                /* the radio's start-up, its calibration (crystal ramp from RAM) */
    }
    if (fm1_rf_cal_on)
        own_cal_log();
""")
    s=replace(s,'static void ble_start(void)\n','static void ml_own_trims(struct ble_rf_trims *use);\nstatic void ble_start(void)\n')
    # off while connecting out: the link layer stops initiating and does not tell the host, which would wait for ever
    s=replace(s,"""        own_held(1);
        ble_enable(on);
        own_held(0);
    }
    if (!on)
        shim_log("switched off");""","""        own_held(1);
        if (!on)
            ble_central_cancel();
        ble_enable(on);
        own_held(0);
    }
    if (!on)
        shim_log("switched off");""")
    s=replace(s,'#include "bt_own_central.c"\n','#include "bt_own_central.c"\n#include "mlb_ble_own.h"\n')
    p.write_text('/* Melodee settings, name and bridge Copyright (C) 2026 Kerem Kilic (Ellic Studio) */\n'+s)
    (work/'device/mlb_ble_own.h').write_text((Path(__file__).resolve().parents[1]/'platform/ble_own.h').read_text())
    # The last device and the attempts are the bridge's (ble_own.h): its plat_ble_scan and plat_ble_connect, its rounds
    # of scanning hidden from the page, which would stop them.
    p=work/'device/bt_own_central.c';s=p.read_text()
    s=replace(s,"    (void)k;                                      /* (a device's bond is not kept: it pairs again next time) */\n",
              '    ml_own_keys(k);\n')
    s=replace(s,'static struct ble_scan_tab own_scan;',"""static void ml_own_keys(const struct ble_keys *k);
static void ml_own_poll(void);
static uint8_t ml_relink_scanning(void);
static struct ble_scan_tab own_scan;""")
    s=replace(s,"""static void own_central_poll(void)                /* ble_poll: the scan's reports into the table */
{
""","""static void own_central_poll(void)                /* ble_poll: the scan's reports into the table */
{
    ml_own_poll();
""")
    for name in ('void plat_ble_scan(int on)','int plat_ble_connect(int i)'):
        s=function(s,name,'')
    s=replace(s,'return ble_ll_scanning() ? BLE_DEV_SCANNING : BLE_DEV_OFF;','return ble_ll_scanning() && !ml_relink_scanning() ? BLE_DEV_SCANNING : BLE_DEV_OFF;')
    p.write_text('/* Melodee bridge calls Copyright (C) 2026 Kerem Kilic (Ellic Studio) */\n'+s)
    # A scan wished for while a computer was connected shows as off, so leaving the page must end it too: else the wish
    # stays, and the radio scans instead of advertising when the computer goes.
    p=work/'lib/btdev.c';s=p.read_text()
    s=replace(s,'    } else if (!up && d == BLE_DEV_SCANNING) {\n','    } else if (!up && (d == BLE_DEV_SCANNING || bd.up)) {\n')
    p.write_text('/* Melodee: the end of a scan wish Copyright (C) 2026 Kerem Kilic (Ellic Studio) */\n'+s)

    p=work/'host/plat_host.c';s=p.read_text()
    s=replace(s,'        } else if (!strcmp(cmd, "midi")) {', '''        } else if (!strcmp(cmd, "mididev")) {
            min_q[mi_w % MQ] = mk_pkt(a, b, c) | 0x10u;
            min_port[mi_w++ % MQ] = PORT_BLE;
            run_ms(2);
        } else if (!strcmp(cmd, "midi")) {''')
    # The base's expect_no_out is the same check; BLE stays either link, as the scripts mean it.
    s=replace(s,'uint32_t to = !strcmp(port, "BLE-DEV") ? TO_BLE_DEV : !strcmp(port, "USBH") ? TO_USBH_SIM : p >= 0 ? 1u << p : 0u, i;',
              'uint32_t to = !strcmp(port, "BLE-DEV") ? TO_BLE_DEV : !strcmp(port, "BLE") ? TO_BLE : !strcmp(port, "USBH") ? TO_USBH_SIM : p >= 0 ? 1u << p : 0u, i;')
    if smk:                                       # (the host has no K5: the UI tests turn it themselves)
        s=replace(s,'void plat_master_set(uint32_t q12) { master = q12 > 4096u ? 4096u : q12; }\n',
                  'void plat_master_set(uint32_t q12) { master = q12 > 4096u ? 4096u : q12; }\n'
                  'int32_t plat_k5_lend(int on) { (void)on; return 0; }\n')
    s=replace(s,"""        } else if (!strcmp(cmd, "expect_out") || !strcmp(cmd, "expect_no_out")) {""","""        } else if (!strcmp(cmd, "expect_unsent")) {
            fails += expect_out(a, mk_pkt(b, c, d), 0);
        } else if (!strcmp(cmd, "expect_out") || !strcmp(cmd, "expect_no_out")) {""")
    p.write_text('/* Melodee dual BLE ingress and thru fixture Copyright (C) 2026 Kerem Kilic (Ellic Studio) */\n'+s)

    p=work/'bsp/chip/fm1_sys.h';s=p.read_text()
    for name in ('fm1_reboot','fm1_enter_uboot'):
        marker=f'static void {name}(void)\n{{'
        s=replace(s,marker,marker+'\n#ifdef APP_CSTART\n    ml_worker_stop();\n#endif')
    s=replace(s,'FM1_INLINE void fm1_core_reset(void)\n{','FM1_INLINE void fm1_core_reset(void)\n{\n#ifdef APP_CSTART\n    ml_worker_stop();\n#endif')
    p.write_text('/* Melodee CPU1 reset lifecycle Copyright (C) 2026 Kerem Kilic (Ellic Studio) */\n'+s)
