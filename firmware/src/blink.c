/*
 * Hello World for CH32V203G6 — blinks a LED on PB5.
 *
 * Register-level bare metal, no vendor library required.
 * The CH32V20x GPIO/RCC register map is documented in the
 * CH32FV2x_V3x reference manual (RM).
 */

#include <stdint.h>

/* RCC (same base address as STM32F1) */
#define RCC_BASE        0x40021000u
/* APB2 peripheral clock enable register */
#define RCC_APB2PCENR   (*(volatile uint32_t *)(RCC_BASE + 0x18u))
#define RCC_IOPBEN      (1u << 3)   /* GPIOB clock enable */

/* GPIOB */
#define GPIOB_BASE      0x40010C00u
#define GPIOB_CFGLR     (*(volatile uint32_t *)(GPIOB_BASE + 0x00u))
#define GPIOB_BSHR      (*(volatile uint32_t *)(GPIOB_BASE + 0x10u))

#define LED_PIN         5u          /* PB5 */

static void delay(volatile uint32_t cycles)
{
    /* ~cycles/3 core clocks; core runs at 8 MHz (HSI) after reset */
    while (cycles--) {
        __asm__ volatile ("nop");
    }
}

int main(void)
{
    /* Enable the GPIOB peripheral clock */
    RCC_APB2PCENR |= RCC_IOPBEN;

    /* PB5 lives in CFGLR, nibble 5 (bits [23:20]).
     * MODE5 = 11 -> output, 50 MHz max
     * CNF5  = 00 -> general purpose push-pull */
    GPIOB_CFGLR = (GPIOB_CFGLR & ~(0xFu << (LED_PIN * 4u)))
                |  (0x3u << (LED_PIN * 4u));

    for (;;) {
        GPIOB_BSHR = (1u << LED_PIN);            /* PB5 high  */
        delay(800000u);
        GPIOB_BSHR = (1u << (LED_PIN + 16u));    /* PB5 low   */
        delay(800000u);
    }
}
