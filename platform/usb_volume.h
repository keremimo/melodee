/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee playback volume retained on the base USB endpoints. */
#define UA_VOLUME_MIN (-64 * 256)
/* Standard UAC1 volume: signed 1/256 dB, -64..0 dB in one-dB steps.
 * Q15 gain table: round(32768 * 10^(-dB/20)); no floating point in USB service. */
static const uint16_t ua_volume_gain[65] = {
    32768, 29205, 26029, 23198, 20675, 18427, 16423, 14637,
    13045, 11627, 10362, 9235, 8231, 7336, 6538, 5827,
    5193, 4629, 4125, 3677, 3277, 2920, 2603, 2320,
    2068, 1843, 1642, 1464, 1305, 1163, 1036, 924,
    823, 734, 654, 583, 519, 463, 413, 368,
    328, 292, 260, 232, 207, 184, 164, 146,
    130, 116, 104, 92, 82, 73, 65, 58,
    52, 46, 41, 37, 33, 29, 26, 23,
    21,
};
static int16_t ua_host_volume_q8;
static uint8_t ua_host_mute;
static int32_t ua_host_target_q15 = 32768, ua_host_gain_q15 = 32768;

static void ua_host_volume_reset(void)
{
    ua_host_volume_q8 = ua_host_mute = 0;
    ua_host_target_q15 = ua_host_gain_q15 = 32768;
}

static void ua_host_volume_set(int16_t volume)
{
    if (volume == (int16_t)0x8000) {         /* UAC1 CUR's explicit -infinity code */
        ua_host_volume_q8 = volume;
        ua_host_target_q15 = 0;
        return;
    }
    int32_t attenuation = volume >= 0 ? 0 : volume <= UA_VOLUME_MIN ? 64 : (-(int32_t)volume + 128) / 256;
    ua_host_volume_q8 = (int16_t)(-attenuation * 256);
    ua_host_target_q15 = ua_volume_gain[attenuation];
}

static uint8_t ml_volume_pending,ml_volume_reply[2];
static int ml_volume_setup(const uint8_t *s){
    uint32_t selector=s[3],n=selector==1u?1u:2u;int16_t v;
    if(!usb.config||s[4]!=UAC_AS_IF-2u||s[5]!=5u||s[2]||s[7]||s[6]!=n||(selector!=1u&&selector!=2u))return 0;
    if(s[0]==0x21u&&s[1]==1u){ml_volume_pending=(uint8_t)selector;sie_wr(S_INDEX,0);sie_wr(S_CSR0,0x40);return 1;}
    if(s[0]!=0xA1u)return 0;
    if(selector==1u){if(s[1]!=0x81u)return 0;v=ua_host_mute;}
    else switch(s[1]){case 0x81:v=ua_host_volume_q8;break;case 0x82:v=UA_VOLUME_MIN;break;case 0x83:v=0;break;case 0x84:v=256;break;default:return 0;}
    ml_volume_reply[0]=(uint8_t)v;ml_volume_reply[1]=(uint8_t)((uint16_t)v>>8);
    e0_send(ml_volume_reply,(uint16_t)n,(uint16_t)n);return 1;
}
static int ml_volume_data(const uint8_t *p,uint32_t n){
    uint8_t selector=ml_volume_pending;ml_volume_pending=0;
    if(!usb.config)return 0;
    if(selector==1u){if(n!=1u||p[0]>1u)return 0;ua_host_mute=p[0];return 1;}
    if(selector!=2u||n!=2u)return 0;
    ua_host_volume_set((int16_t)(p[0]|(uint16_t)p[1]<<8));return 1;
}
