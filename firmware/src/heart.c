/*
 * POV heart for CH32V203G6 — displays an 8x12 heart on 8 LEDs,
 * one column at a time, advancing to the next column at 400 Hz.
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
#define RCC_AFIOEN      (1u << 0)   /* AFIO clock enable (for PB3 remap) */
#define RCC_IOPAEN      (1u << 2)   /* GPIOA clock enable */
#define RCC_IOPBEN      (1u << 3)   /* GPIOB clock enable */

/* AFIO remap: PB3 is a JTAG pin after reset. SWJ_CFG = 010 keeps
 * SWD enabled and frees PB3/PB4/PA15 for GPIO. */
#define AFIO_BASE        0x40010000u
#define AFIO_PCFR1       (*(volatile uint32_t *)(AFIO_BASE + 0x04u))
#define AFIO_SWJ_CFG     (0x7u << 24)
#define AFIO_SWJ_JTAGDIS (0x2u << 24)

/* GPIOA / GPIOB */
#define GPIOA_BASE      0x40010800u
#define GPIOB_BASE      0x40010C00u
#define GPIOA_CFGLR     (*(volatile uint32_t *)(GPIOA_BASE + 0x00u))
#define GPIOA_BSHR      (*(volatile uint32_t *)(GPIOA_BASE + 0x10u))
#define GPIOB_CFGLR     (*(volatile uint32_t *)(GPIOB_BASE + 0x00u))
#define GPIOB_BSHR      (*(volatile uint32_t *)(GPIOB_BASE + 0x10u))

/* LED order, pattern bit -> pin:
 *   LED1 bit0 -> PB1   LED2 bit1 -> PB0   LED8 bit7 -> PB3
 *   LED3 bit2 -> PA7   LED4 bit3 -> PA6   LED5 bit4 -> PA5
 *   LED6 bit5 -> PA4   LED7 bit6 -> PA3
 */
#define GPIOA_LED_MASK  0xF8u           /* PA3..PA7 */
#define GPIOB_LED_MASK  0x0Bu           /* PB0, PB1, PB3 */

/* The heart, 8 rows x 12 columns. One byte per column,
 * bit0 = LED1 = top row:
 *   .**.....**..
 *   ****...****.
 *   ***********.
 *   .*********..
 *   ..*******...
 *   ...*****....
 *   ....***.....
 *   .....*......
 */
static const uint8_t HEART[] = {
    0x06, 0x0F, 0x1F, 0x3E, 0x7E, 0xFC,
    0x7C, 0x3E, 0x1F, 0x0F, 0x06, 0x04,
};

/* 8 MHz HSI / 400 Hz column rate; delay() is ~3 core clocks/iter */
#define COL_DELAY       (8000000u / 400u / 3u)

static void delay(volatile uint32_t cycles)
{
    while (cycles--) {
        __asm__ volatile ("nop");
    }
}

int main(void)
{
    /* Enable AFIO (for the PB3 remap), GPIOA and GPIOB clocks */
    RCC_APB2PCENR |= RCC_AFIOEN | RCC_IOPAEN | RCC_IOPBEN;
    AFIO_PCFR1 = (AFIO_PCFR1 & ~AFIO_SWJ_CFG) | AFIO_SWJ_JTAGDIS;

    /* PA3..PA7 = CFGLR nibbles 3-7, PB0/PB1/PB3 = nibbles 0,1,3.
     * MODE = 11 -> output, 50 MHz max
     * CNF  = 00 -> general purpose push-pull */
    GPIOA_CFGLR = (GPIOA_CFGLR & ~0xFFFFF000u) | 0x33333000u;
    GPIOB_CFGLR = (GPIOB_CFGLR & ~0x0000F0FFu) | 0x00003033u;

    for (;;) {
        for (uint32_t c = 0; c < sizeof(HEART); c++) {
            uint8_t col = HEART[c];

            /* Scatter pattern bits onto the physical pins */
            uint32_t pa = (col & 0x40u) >> 3  /* bit6 -> PA3 */
                        | (col & 0x20u) >> 1  /* bit5 -> PA4 */
                        | (col & 0x10u)       /* bit4 -> PA5 */
                        | (col & 0x08u) << 3  /* bit3 -> PA6 */
                        | (col & 0x04u) << 5; /* bit2 -> PA7 */
            uint32_t pb = (col & 0x01u) << 1  /* bit0 -> PB1 */
                        | (col & 0x02u) >> 1  /* bit1 -> PB0 */
                        | (col & 0x80u) >> 4; /* bit7 -> PB3 */

            /* BSHR: low word sets, high word resets (LED pins only) */
            GPIOA_BSHR = pa | ((GPIOA_LED_MASK & ~pa) << 16);
            GPIOB_BSHR = pb | ((GPIOB_LED_MASK & ~pb) << 16);
            delay(COL_DELAY);
        }
    }
}
