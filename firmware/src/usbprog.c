/*
 * usbprog — protocol test firmware for the winkdings2 dev loop.
 *
 * Built as an -app image (link_app.ld, base 0x08003000) and flashed
 * through usbload (tools/winkflash.py).  Enters programming mode
 * immediately on boot — Larson sweep + CDC-ACM (0448:570b) — and
 * serves the pattern blob protocol (prog_proto.c).  Returns to the
 * loader on EXIT/RESET commands or after ~15 s idle, so the loop
 *  make usbprog-app && python3 tools/winkflash.py build/usbprog-app.bin
 * never needs the BOOT button or a manual reset.
 */

#include <stdint.h>
#include "usb_cdc.h"
#include "flash_store.h"
#include "prog_proto.h"

#define RCC_APB2PCENR   (*(volatile uint32_t *)0x40021018u)
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

static void delay_cycles(uint32_t cycles)
{
    __asm__ volatile (
        "1: addi %0, %0, -1\n"
        "   bnez %0, 1b\n"
        : "+r" (cycles) :: "cc");
}

static void show_column(uint8_t col)
{
    uint32_t pa = (col & 0x40u) >> 3
                | (col & 0x20u) >> 1
                | (col & 0x10u) << 1
                | (col & 0x08u) << 3
                | (col & 0x04u) << 5;
    uint32_t pb = (col & 0x01u) << 1
                | (col & 0x02u) >> 1
                | (col & 0x80u) >> 4;
    GPIOA_BSHR = pa | ((GPIOA_LED_MASK & ~pa) << 16);
    GPIOB_BSHR = pb | ((GPIOB_LED_MASK & ~pb) << 16);
}

static void __attribute__((noreturn)) soft_reset(void)
{
    /* let a pending reply packet drain (SIE sends autonomously) */
    delay_cycles(800000);                    /* ~50 ms at 48 MHz */
    usb_cdc_deinit();
    *(volatile uint32_t *)0xE000E048u = 0xBEEF0000u | (1u << 7);
    __builtin_unreachable();
}

int main(void)
{
    RCC_APB2PCENR |= RCC_AFIOEN | RCC_IOPAEN | RCC_IOPBEN;
    AFIO_PCFR1 = (AFIO_PCFR1 & ~AFIO_SWJ_CFG) | AFIO_SWJ_JTAGDIS;
    GPIOA_CFGLR = (GPIOA_CFGLR & ~(0xFFFFF000u | 0xFu)) | 0x33333000u | 0x4u;
    GPIOB_CFGLR = (GPIOB_CFGLR & ~0x0000F0FFu) | 0x00003033u;

    fs_init();          /* ensure the pattern blob is valid */
    usb_cdc_init();
    proto_init();

    /* tick @ ~400 Hz; ~15 s idle -> back to loader */
    const uint32_t tick_delay = 48000000u / 400u / 3u;
    const uint32_t timeout    = 6000u;
    uint32_t t = 0, last = 0, idle0 = 0, last_rx = 0;
    int8_t pos = 0, dir = 1;

    show_column(1);
    while (t - idle0 < timeout) {
        for (int k = 0; k < 16; k++) {
            delay_cycles(tick_delay / 16);
            usb_cdc_poll();
            int ev = proto_poll();
            if (ev == PROTO_EV_EXIT || ev == PROTO_EV_RESET)
                soft_reset();
        }
        t++;
        if (usb_cdc_rx_total != last_rx) {  /* host traffic: stay up */
            last_rx = usb_cdc_rx_total;
            idle0 = t;
        }
        uint8_t leds = (uint8_t)(1u << pos);
        if (usb_cdc_seen)
            leds |= 0x80u;
        show_column(leds);
        if (t - last >= 30) {
            last = t;
            pos = (uint8_t)(pos + dir);
            if (pos >= 7) { pos = 7; dir = -1; }
            if (pos <= 0) { pos = 0; dir = 1; }
        }
    }

    show_column(0);
    soft_reset();
}
