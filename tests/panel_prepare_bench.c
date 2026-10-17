/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#ifndef MELODEE_PANEL_FAST
#define MELODEE_PANEL_FAST 1
#endif
#define main pwm_test_main
#include "led_pwm_test.c"
#undef main
#include <assert.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#include <unistd.h>
#endif
/* Exact 64/(mask+1), including nonstandard stored mask bytes. Values above
 * 63 are all dark, so raw mask edits need not rebuild identical combinations. */
static const uint8_t FM1_LED_DUTY[256] = {
    64, 32, 21, 16, 12, 10, 9, 8, 7, 6, 5, 5, 4, 4, 4, 4,
    3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
/* UI writers edit every array directly. Key only exact plane duties, and read
 * each column live; bit 24 marks a valid snapshot. No writer hooks required. */
static struct {
    uint32_t duties;
    uint8_t level[8];
} fm1_led_cache;
static uint32_t fm1__led_level(uint32_t row, uint32_t bright, const uint8_t dim[3])
{
    if (bright & row) return FM1_LED_PWM_PERIOD;
    uint32_t i = ((dim[0] & row) ? 1u : 0u) | ((dim[1] & row) ? 2u : 0u) | ((dim[2] & row) ? 4u : 0u);
    return fm1_led_cache.level[i];
}
static uint64_t instructions(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 r;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&r)) return r.ri_instructions;
#endif
    return 0;
}
/* Rejected brightness-cache probe: correctness is retained so its steady and
 * sparse/changing costs remain reproducible. This is NOT firmware code. */
/* Melodee's original PWM column path at 01fb13b; no cache helper used. */
static __attribute__((noinline)) void reference(uint32_t p)
{
    uint8_t dim[3] = {fm1_led_dim[0][p], fm1_led_dim[1][p], fm1_led_dim[2][p]};
    uint32_t bright = fm1_led[p];
    if (!fm1_led_pwm_active) {
        uint32_t f = fm1_in.frames + p;
        fm1__led_lines(bright | ((f & fm1_led_dim_mask[0]) ? 0u : dim[0]) |
                       ((f & fm1_led_dim_mask[1]) ? 0u : dim[1]) |
                       ((f & fm1_led_dim_mask[2]) ? 0u : dim[2]));
        return;
    }
    fm1_led_pwm_write(fm1_led_pwm_level(2u, bright, dim, fm1_led_dim_mask),
                      fm1_led_pwm_level(4u, bright, dim, fm1_led_dim_mask),
                      fm1_led_pwm_level(8u, bright, dim, fm1_led_dim_mask),
                      fm1_led_pwm_level(16u, bright, dim, fm1_led_dim_mask));
}
static __attribute__((noinline)) void brightness_probe(uint32_t p)
{
    uint8_t dim[3] = {fm1_led_dim[0][p], fm1_led_dim[1][p], fm1_led_dim[2][p]};
    uint32_t bright = fm1_led[p];
    if (!fm1_led_pwm_active) { reference(p); return; }
    if ((bright & 30u) == 30u) {
        fm1_led_pwm_write(FM1_LED_PWM_PERIOD, FM1_LED_PWM_PERIOD, FM1_LED_PWM_PERIOD, FM1_LED_PWM_PERIOD);
        return;
    }
    if (!((bright | dim[0] | dim[1] | dim[2]) & 30u)) {
        fm1_led_pwm_write(0, 0, 0, 0);
        return;
    }
    uint32_t a = FM1_LED_DUTY[fm1_led_dim_mask[0]], b = FM1_LED_DUTY[fm1_led_dim_mask[1]];
    uint32_t c = FM1_LED_DUTY[fm1_led_dim_mask[2]];
    uint32_t duties = a | b << 8 | c << 16 | 1u << 24;
    if (fm1_led_cache.duties != duties) {
        fm1_led_cache.level[1] = (uint8_t)a;
        fm1_led_cache.level[2] = (uint8_t)b;
        fm1_led_cache.level[3] = (uint8_t)(a > b ? a : b);
        fm1_led_cache.level[4] = (uint8_t)c;
        fm1_led_cache.level[5] = (uint8_t)(a > c ? a : c);
        fm1_led_cache.level[6] = (uint8_t)(b > c ? b : c);
        a = a > b ? a : b;
        fm1_led_cache.level[7] = (uint8_t)(a > c ? a : c);
        fm1_led_cache.duties = duties;
    }
    fm1_led_pwm_write(fm1__led_level(2u, bright, dim), fm1__led_level(4u, bright, dim),
                      fm1__led_level(8u, bright, dim), fm1__led_level(16u, bright, dim));
}
static uint32_t random_state = 0x92743817u;
static uint32_t next_u(void) { random_state = random_state * 1664525u + 1013904223u; return random_state; }
static void compare(uint32_t p)
{
    uint32_t expected[64], saved[64], expected_ports[8][16], saved_ports[8][16];
    memcpy(saved, (const void *)pwm, sizeof saved);
    memcpy(saved_ports, (const void *)ports, sizeof saved_ports);
    reference(p);
    memcpy(expected, (const void *)pwm, sizeof expected);
    memcpy(expected_ports, (const void *)ports, sizeof expected_ports);
    memcpy((void *)pwm, saved, sizeof saved);
    memcpy((void *)ports, saved_ports, sizeof saved_ports);
    brightness_probe(p);
    assert(!memcmp(expected, (const void *)pwm, sizeof expected));
    assert(!memcmp(expected_ports, (const void *)ports, sizeof expected_ports));
}
static void parity(void)
{
    /* Cold zero key, every mask byte, overlapping planes, ignored bits and all
     * brightness/plane bits. Per-column revisits catch stale entries. */
    memset(&fm1_led_cache, 0, sizeof fm1_led_cache);
    memset(fm1_led, 0, sizeof fm1_led);
    memset(fm1_led_dim, 0, sizeof fm1_led_dim);
    memset(fm1_led_dim_mask, 0, sizeof fm1_led_dim_mask);
    for (uint32_t m = 0; m < 256; m++) assert(FM1_LED_DUTY[m] == 64u / (m + 1));
    fm1_led_pwm_active = 1;
    for (uint32_t p = 0; p < FM1_NCOL; p++) compare(p);
    for (uint32_t plane = 0; plane < 3; plane++)
        for (uint32_t mask = 0; mask < 256; mask++)
            for (uint32_t pattern = 0; pattern < 256; pattern++) {
                uint32_t p = pattern % FM1_NCOL;
                fm1_led[p] = (uint8_t)(pattern >> 3);
                for (uint32_t j = 0; j < 3; j++) fm1_led_dim[j][p] = (uint8_t)(pattern << j);
                fm1_led_dim_mask[plane] = (uint8_t)mask;
                compare(p); compare(p);
            }
    for (uint32_t i = 0; i < 200000; i++) {
        uint32_t p = next_u() % FM1_NCOL;
        fm1_led_pwm_active = i % 7 != 0;
        fm1_in.frames = next_u();
        switch (i % 6) {
        case 0: fm1_led[p] = (uint8_t)(next_u() >> 24); break;
        case 1: case 2: case 3: fm1_led_dim[i % 6 - 1][p] = (uint8_t)(next_u() >> 24); break;
        case 4: fm1_led_dim_mask[next_u() % 3] = (uint8_t)(next_u() >> 24); break;
        case 5: fm1_led_key(next_u() % FM1_NKEY, (int)(next_u() & 1u)); break;
        }
        for (p = 0; p < FM1_NCOL; p++) compare(p);
    }
    puts("Rejected panel cache: all mask bytes/planes, 200000 edits, all columns and PWM/GPIO transitions exact");
}
static void benchmark(void)
{
    puts("pattern,reference_instructions_per_column,rejected_probe_instructions_per_column");
    fm1_led_pwm_active = 1;
    for (uint32_t mode = 0; mode < 10; mode++) {
        double cost[2];
        for (uint32_t fast = 0; fast < 2; fast++) {
            memset(fm1_led, mode == 1 ? 30 : 0, sizeof fm1_led);
            for (uint32_t j = 0; j < 3; j++) {
                memset(fm1_led_dim[j], mode > 1 ? 30 : 0, FM1_NCOL);
                fm1_led_dim_mask[j] = (uint8_t)((1u << (j + 1)) - 1);
            }
            if (mode == 6 || mode == 7) {
                memset(fm1_led_dim, 0, sizeof fm1_led_dim);
                memset(fm1_led_dim[0], mode == 6 ? 2 : 30, FM1_NCOL);
            }
            if (mode == 8) memset(fm1_led, 14, sizeof fm1_led);
            void (*volatile run)(uint32_t) = fast ? brightness_probe : reference;
            uint64_t start = instructions();
            for (uint32_t i = 0; i < 1000000; i++) {
                uint32_t p = i % FM1_NCOL;
                if (mode == 3) fm1_led[p] = (uint8_t)(i & 30);
                if (mode == 4) fm1_led_dim_mask[2] = (uint8_t)i;
                if (mode == 9) {
                    static const uint8_t masks[] = {0, 1, 3, 7, 255};
                    fm1_led_dim_mask[2] = masks[i % 5];
                }
                if (mode == 5) { fm1_led[p] = (uint8_t)(i & 30); fm1_led_dim_mask[1] = (uint8_t)i; }
                run(p);
            }
            cost[fast] = (double)(instructions() - start) / 1000000;
        }
        printf("%u,%.2f,%.2f\n", mode, cost[0], cost[1]);

    }
}
static __attribute__((noinline)) uint32_t column_reference(uint32_t p)
{ return 0xFFFFu ^ (1u << p) ^ (p < 2u ? 1u << (11u + p) : 0u); }
static __attribute__((noinline)) uint32_t column_candidate(uint32_t p)
{ return fm1__column_word(p); }
static void column_benchmark(void)
{
    double cost[2];
    volatile uint32_t checksum = 0;
    for (uint32_t fast = 0; fast < 2; fast++) {
        uint32_t (*volatile run)(uint32_t) = fast ? column_candidate : column_reference;
        uint64_t start = instructions();
        for (uint32_t i = 0; i < 1000000; i++) checksum += run(i % FM1_NCOL);
        cost[fast] = (double)(instructions() - start) / 1000000;
    }
    printf("Fixed column word: reference %.2f, candidate %.2f host instructions/column (loop/call included)\n", cost[0], cost[1]);
}
int main(int argc, char **argv)
{
    assert(!pwm_test_main()); parity(); if (argc == 1) { benchmark(); column_benchmark(); } return 0;
}
