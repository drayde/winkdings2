/*
 * usbtest — minimal USB CDC bring-up test for CH32V203G6.
 *
 * Boots straight into programming mode (48 MHz PLL + USBD +
 * internal pull-up) so enumeration can be checked with lsusb
 * without touching the button. Received bytes are echoed back.
 * A Larson scanner runs on the LEDs while enumerating.
 *
 * After ~30 s the firmware soft-resets, so enumeration cycles:
 * watch `lsusb` to see 0448:570b appear ~1 s after each reset.
 */

#include <stdint.h>
#include "usb_cdc.h"

/* RCC / GPIO (same pin map as winkdings2) */
#define RCC_APB2PCENR   (*(volatile uint32_t *)0x40021018u)
#define RCC_APB1PCENR   (*(volatile uint32_t *)0x4002101Cu)
#define RCC_AFIOEN      (1u << 0)
#define RCC_IOPAEN      (1u << 2)
#define RCC_IOPBEN      (1u << 3)
#define AFIO_PCFR1      (*(volatile uint32_t *)0x40010004u)
#define AFIO_SWJ_CFG    (0x7u << 24)
#define AFIO_SWJ_JTAGDIS (0x2u << 24)
#define GPIOA_CFGLR     (*(volatile uint32_t *)0x40010800u)
#define GPIOA_BSHR      (*(volatile uint32_t *)0x40010810u)
#define GPIOB_CFGLR     (*(volatile uint32_t *)0x40010C00u)
#define GPIOB_BSHR      (*(volatile uint32_t *)0x40010C10u)

#define GPIOA_LED_MASK  0xF8u
#define GPIOB_LED_MASK  0x0Bu

/* ~3 clk/iter — keep in asm (see delay_cycles in winkdings2.c). */
static void delay_cycles(uint32_t cycles)
{
    __asm__ volatile (
        "1: addi %0, %0, -1\n"
        "   bnez %0, 1b\n"
        : "+r" (cycles) :: "cc");
}

static void show_column(uint8_t col)
{
    uint32_t pa = (col & 0x40u) >> 3  /* bit6 -> PA3 */
                | (col & 0x20u) >> 1  /* bit5 -> PA4 */
                | (col & 0x10u) << 1  /* bit4 -> PA5 */
                | (col & 0x08u) << 3  /* bit3 -> PA6 */
                | (col & 0x04u) << 5; /* bit2 -> PA7 */
    uint32_t pb = (col & 0x01u) << 1  /* bit0 -> PB1 */
                | (col & 0x02u) >> 1  /* bit1 -> PB0 */
                | (col & 0x80u) >> 4; /* bit7 -> PB3 */
    GPIOA_BSHR = pa | ((GPIOA_LED_MASK & ~pa) << 16);
    GPIOB_BSHR = pb | ((GPIOB_LED_MASK & ~pb) << 16);
}

/* Software-reset the chip: PFIC_CFGR (0xE000E048) = KEY3|SYSRESET.
 * (The ROM ISP can't be entered from software on CH32V20x — it is
 * linked at the 0x00000000 boot alias which only BOOT0/1 select.)
 * Never returns. */
static void soft_reset(void)
{
    usb_cdc_deinit();
    __asm__ volatile ("csrci mstatus, 8\n");
    *(volatile uint32_t *)0xE000E048u = 0xBEEF0000u | (1u << 7);
    __builtin_unreachable();
}

int main(void)
{
    RCC_APB2PCENR |= RCC_AFIOEN | RCC_IOPAEN | RCC_IOPBEN;
    AFIO_PCFR1 = (AFIO_PCFR1 & ~AFIO_SWJ_CFG) | AFIO_SWJ_JTAGDIS;
    GPIOA_CFGLR = (GPIOA_CFGLR & ~(0xFFFFF000u | 0xFu))
                | 0x33333000u | 0x4u;
    GPIOB_CFGLR = (GPIOB_CFGLR & ~0x0000F0FFu) | 0x00003033u;

    /* LED1 on immediately = firmware is alive */
    GPIOB_BSHR = 0x02;

    usb_cdc_init();                 /* 48 MHz + enumerate */

    /* 48 MHz: tick @ ~400 Hz = 48000000/400/3 delay iterations.
     * 30 s = 12000 ticks. */
    const uint32_t tick_delay = 48000000u / 400u / 3u;
    const uint32_t timeout    = 12000u;
    uint32_t t = 0, last = 0;
    int8_t pos = 0, dir = 1;

    show_column(1);
    while (t < timeout) {
        /* poll every ~150 us instead of once per 2.5 ms tick */
        for (int k = 0; k < 16; k++) {
            delay_cycles(tick_delay / 16);
            usb_cdc_poll();
        }
        t++;

        /* echo any host bytes back */
        int ch;
        while ((ch = usb_cdc_getchar()) >= 0) {
            uint8_t b = (uint8_t)ch;
            usb_cdc_write(&b, 1);
        }

        /* Larson sweep; LED8 also stays lit once the host has
         * issued a bus reset / transfer (usb_cdc_seen) — i.e. once
         * the pull-up was actually seen on the wire. */
        uint8_t leds = (uint8_t)(1u << pos);
        if (usb_cdc_seen)
            leds |= 0x80u;
        show_column(leds);
        if (t - last >= 30) {
            last = t;
            pos += dir;
            if (pos >= 7) { pos = 7; dir = -1; }
            if (pos <= 0) { pos = 0; dir = 1; }
        }
    }

    show_column(0);
    soft_reset();
}
