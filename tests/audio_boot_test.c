/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Exercise the actual HAL against a codec that requires settled silent I2S
 * before its control line is released, including a warm DMA restart. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __attribute__(x)
#define __asm__
#define volatile(...)
#include "../firmware/hal/fm1_time.h"
#include "../firmware/hal/fm1_irq.h"
#include "../firmware/hal/fm1_gpio.h"
#undef __asm__
#undef volatile
const char fm1_fatal_stubs[1]={0};
static void fm1_fault(const fm1_crash_t *c){(void)c;}
static uint32_t regs[0x30/4],ports[8][8],clk,map,settled,attached,bad;
static int32_t samples[128];
static void *audio_reg(uint32_t addr)
{
    if((ports[FM1_PC][FM1_OUT/4]&(1u<<6)) && (!(regs[0]&0x800u)||settled<50u))bad++;
    if(addr==0x10014u)return &clk;
    if(addr==0x51030u)return &map;
    return &regs[(addr-0x12e00u)/4];
}
static void delay(uint32_t ms)
{
    if(ports[FM1_PC][FM1_OUT/4]&(1u<<6))bad++;
    if(regs[0]&0x800u){
        if(!attached||regs[0x1c/4]!=(uint32_t)(uintptr_t)samples)bad++;
        for(uint32_t i=0;i<128;i++)if(samples[i])bad++;
        settled+=ms;
    }
}
static void attach(uint32_t irq,void (*isr)(void),uint32_t prio){(void)isr;attached=irq==FM1_IRQ_ALNK0&&prio==3;}
#undef FM1_PR
#define FM1_PR(p,r) ports[p][(r)/4]
#define FM1_AUDIO_REG(type,addr) (*(type *)audio_reg(addr))
#define fm1_delay_ms delay
#define fm1_irq_attach attach
#include "../firmware/hal/fm1_audio.h"
static void isr(void){}
int main(void)
{
    fm1_audio_init(samples,64,isr,3);
    int ok=!bad&&settled>=50u&&!(ports[FM1_PC][FM1_OUT/4]&(1u<<6))&&(regs[0]&0x800u);
    printf("boot audio: init leaves the codec quiet on settled silent I2S %s\n",ok?"ok":"FAIL");
    fm1_audio_unmute();ok&=!bad&&(ports[FM1_PC][FM1_OUT/4]&(1u<<6))&&(regs[0]&0x800u);
    printf("boot audio: unmute releases the codec on running silent I2S %s\n",ok?"ok":"FAIL");
    fm1_audio_stop();ok&=!(ports[FM1_PC][FM1_OUT/4]&(1u<<6))&&!(regs[0]&0x800u);
    settled=attached=0;fm1_audio_init(samples,64,isr,3);ok&=!bad&&settled>=50u&&!(ports[FM1_PC][FM1_OUT/4]&(1u<<6));
    printf("boot audio: stop/restart holds codec quiet during DMA setup %s\n",ok?"ok":"FAIL");
    return !ok;
}
