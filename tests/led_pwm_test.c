/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Exercise the real HAL initialization and column scan against MMIO storage.
 * This verifies register ownership, routing, blanking and duty selection; it
 * cannot establish the electrical PWM waveform or visible device behavior. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/hal/fm1_gpio.h"
#include "../firmware/hal/fm1_time.h"
static volatile uint32_t ports[8][16], pwm[64], iomap[64];
static uint32_t now;
static int force_pad_conflict;
static uint32_t ticks(void) { return now++; }
static volatile uint32_t *reg(uint32_t a)
{
    if (a >= 0x15000u && a < 0x15100u)
        return &pwm[(a - 0x15000u) / 4u];
    if (a >= 0x51000u && a < 0x51100u)
        return &iomap[(a - 0x51000u) / 4u];
    abort();
}
static volatile uint32_t *port_reg(uint32_t p, uint32_t r)
{
    if (r == FM1_IN && p == FM1_PA) {
        uint32_t fixed = 0;
        /* Peripheral pad enable overrides DIR, including when the requested
         * duty is zero. Model the failure the previous register-only test missed. */
        if (*reg(0x150E4u) & (1u << 1)) {
            if ((*reg(0x15074u) & 4u) || force_pad_conflict) fixed |= 1u << 7;
            if ((*reg(0x15074u) & 8u) || force_pad_conflict) fixed |= 1u << 8;
        }
        ports[p][r / 4u] = 0xFFFFFFFFu & ~fixed;
    }
    return &ports[p][r / 4u];
}
#undef FM1_PR
#define FM1_PR(p, r) (*port_reg(p, r))
#define FM1_LED_PWM_REG(a) (*reg(a))
#define fm1_ticks() ticks()
#include "../firmware/hal/fm1_input.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static uint32_t duty(uint32_t r)
{
    static const uint32_t cmp[4] = {0x1509Cu, 0x150A0u, 0x1507Cu, 0x15080u};
    return *reg(cmp[r]);
}
int main(void)
{
    uint32_t i, r, frame, a0, h0, im1, im3, global;
    for (i = 0; i < FM1_NCOL; i++)
        CHECK(fm1__column_word(i) == (0xFFFFu ^ (1u << i) ^ (i < 2u ? 1u << (11u + i) : 0u)));
    for (i = 0; i < 64; i++) {
        pwm[i] = 0x12345678u;
        iomap[i] = 0x87654321u;
    }
    for (i = 0; i < 8; i++)
        for (r = 0; r < 16; r++)
            ports[i][r] = 0xABCD0000u;
    im1 = *reg(0x51020u); im3 = *reg(0x51028u); global = *reg(0x150E4u);
    fm1_input_init();
    CHECK(*reg(0x51020u) == ((im1 & ~0xF00u) | 0xC00u));
    CHECK(*reg(0x51028u) == ((im3 & ~0xFFF00000u) | 0xBAD00000u));
    CHECK(*reg(0x150E4u) == (global | 0xA0Au));
    CHECK(pwm[0] == 0x12345678u && pwm[6] == 0x12345678u); /* timers 0/2 untouched */
    CHECK(*reg(0x15064u) == 0x12345678u && *reg(0x15084u) == 0x12345678u);
    CHECK(*reg(0x1500Cu) == 1u && *reg(0x15024u) == 1u); /* no PWM IRQ enable */
    CHECK(*reg(0x15018u - 4u) == 64u && *reg(0x1502Cu) == 64u);
    CHECK(fm1_led_pwm_active);
    CHECK((*reg(0x15074u) & 12u) == 0u && (*reg(0x15094u) & 12u) == 0u);
    CHECK(*reg(0x15074u) == 2u && *reg(0x15094u) == 2u);
    CHECK(*reg(0x15078u) == 0x4100u && *reg(0x15098u) == 0x4300u);
    CHECK((FM1_PR(FM1_PA, FM1_DIR) & 0x180u) == 0x180u); /* key rows stay inputs */
    CHECK(FM1_PR(FM1_PH, FM1_DIR) == (0xABCD0000u & ~0x240u)); /* no fixed pad changes */
    CHECK((FM1_PR(FM1_PA, FM1_OUT) & 0x600u) == 0u);
    CHECK((FM1_PR(FM1_PA, FM1_DIE) & 0x600u) == 0x400u);
    CHECK((FM1_PR(FM1_PH, FM1_OUT) & 0x240u) == 0x240u);
    CHECK((FM1_PR(FM1_PH, FM1_DIE) & 0x240u) == 0x200u);
    CHECK((FM1_PR(FM1_PA, FM1_PU) & FM1_PR(FM1_PA, FM1_PD) & 0x600u) == 0x600u);
    CHECK((FM1_PR(FM1_PH, FM1_PU) & FM1_PR(FM1_PH, FM1_PD) & 0x240u) == 0x240u);
    for (r = 0; r < 4; r++) CHECK(duty(r) == 0u);
    a0 = FM1_PR(FM1_PA, FM1_OUT) & 0x600u;
    h0 = FM1_PR(FM1_PH, FM1_OUT);

    /* Every column gets quarter-duty idle buttons on every frame; neither
     * frame position nor an arbitrarily delayed scan turns dim rows full on. */
    for (i = 0; i < FM1_NCOL; i++) {
        fm1_led[i] = 2u;
        fm1_led_dim[0][i] = 4u;
        fm1_led_dim[1][i] = 8u;
        fm1_led_dim[2][i] = 16u;
    }
    fm1_led_dim_mask[0] = 7u;
    fm1_led_dim_mask[1] = 3u;
    fm1_led_dim_mask[2] = 1u;
    FM1_PR(FM1_PA, FM1_IN) = FM1_PR(FM1_PB, FM1_IN) = 0xFFFFFFFFu;
    for (frame = 0; frame < 64u; frame++)
        for (i = 0; i < FM1_NCOL; i++) {
            now += (frame & 1u) ? 240000u : 2400u; /* delayed 10 ms vs usual 100 us */
            fm1_input_tick();
            CHECK(duty(0) == 64u && duty(1) == 8u && duty(2) == 16u && duty(3) == 32u);
            CHECK((FM1_PR(FM1_PA, FM1_OUT) & 0x600u) == a0);
            CHECK(FM1_PR(FM1_PH, FM1_OUT) == h0); /* PWM routing never toggles */
        }
    CHECK(fm1_in.frames == 64u && fm1_in_stat.ticks == 64u * FM1_NCOL);
    CHECK(fm1_in.notes == 0u && fm1_in.buttons == 0u);
    /* Overlapping planes select the brightest, bright wins, and dark stays dark. */
    for (r = 1u; r <= 4u; r++) {
        const uint8_t dim[3] = {(uint8_t)(1u << r), (uint8_t)(1u << r), (uint8_t)(1u << r)};
        const uint8_t masks[3] = {7, 3, 1}, off[3] = {255, 255, 255};
        CHECK(fm1_led_pwm_level(1u << r, 0u, dim, masks) == 32u);
        CHECK(fm1_led_pwm_level(1u << r, 1u << r, dim, off) == 64u);
        CHECK(fm1_led_pwm_level(1u << r, 0u, dim, off) == 0u);
        CHECK(fm1_led_pwm_level(1u << r, 0u, dim, (const uint8_t[3]){0, 3, 7}) == 64u);
    }
    fm1__led_lines(30u);
    for (r = 0; r < 4; r++) CHECK(duty(r) == 64u);
    now = 0xFFFFFFE0u;
    fm1__led_lines(0u); /* carrier settling wait also works across TIMER4 wrap */
    CHECK(now < 0x100u);
    for (r = 0; r < 4; r++) CHECK(duty(r) == 0u);

    /* Prove this model catches the old configuration: DIR=input does not stop
     * PWM1 from reading both octave buttons pressed through the real scan. */
    *reg(0x15074u) = 14u;
    CHECK((FM1_PR(FM1_PA, FM1_DIR) & 0x180u) == 0x180u);
    CHECK((fm1__rows() & 0x18u) == 0x18u);
    for (i = 0; i < 3u * FM1_NCOL; i++) fm1_input_tick();
    CHECK((fm1_in.buttons & 3u) == 3u);
    *reg(0x15074u) = 2u;
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    memset((void *)&fm1_in_stat, 0, sizeof fm1_in_stat);
    fm1__tick_col = 0;

    /* Inject a hardware conflict despite correct gate bits. Initialization must
     * remove the peripheral and restore GPIO dimming before any button scan. */
    force_pad_conflict = 1;
    fm1_input_init();
    CHECK(!fm1_led_pwm_active);
    CHECK((*reg(0x150E4u) & 0xA0Au) == 0u);
    CHECK((FM1_PR(FM1_PA, FM1_PU) & 0x600u) == 0u);
    CHECK((FM1_PR(FM1_PH, FM1_PD) & 0x240u) == 0u);
    CHECK((FM1_PR(FM1_PA, FM1_DIE) & 0x600u) == 0x600u);
    CHECK((FM1_PR(FM1_PH, FM1_OUT) & 0x240u) == 0u);
    fm1__led_lines(30u);
    CHECK((FM1_PR(FM1_PA, FM1_OUT) & 0x600u) == 0x600u);
    CHECK((FM1_PR(FM1_PH, FM1_OUT) & 0x240u) == 0x240u);
    for (i = 0; i < 3u * FM1_NCOL; i++) fm1_input_tick();
    CHECK(fm1_in.buttons == 0u && fm1_in.notes == 0u);
    force_pad_conflict = 0;
    fm1_input_init();
    CHECK(fm1_led_pwm_active);
    printf("LED hardware PWM: %s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
