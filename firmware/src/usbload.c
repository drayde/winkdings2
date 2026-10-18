/*
 * usbload — resident USB-CDC firmware loader for CH32V203G6.
 *
 * Lives at flash base 0x08000000..0x08002FFF (three 4K pages) and runs
 * at every reset.  Application images are linked at 0x08003000
 * (link_app.ld); the pattern blob at 0x08007000+ is never touched.
 *
 *   reset reason          behaviour
 *   power-on (PORRSTF)    jump straight to app when one is present
 *   soft/pin/wdg reset    stay in loader -> Larson sweep, wait for host
 *
 * CDC-ACM interface, VID:PID 0448:570a.  Commands (host->device):
 *   '?'                      -> 'L'  (alive)
 *   'B' u32 len u16 crc16    -> 'E', then stream len bytes (len even)
 *                             -> 'D' done / 'X' error
 *   'J'                      -> jump to app
 *   'R'                      -> soft reset (lands back here)
 * Pressing the PA0 button also jumps to the app.
 */

#include <stdint.h>
#include "usb_cdc.h"

/* ---- memory map ---- */
#define APP_BASE    0x08003000u
#define APP_END     0x08007000u   /* pattern blob sits above */

/* ---- RCC / AFIO / GPIO ---- */
#define RCC_APB2PCENR   (*(volatile uint32_t *)0x40021018u)
#define RCC_RSTSCKR     (*(volatile uint32_t *)0x40021024u)
#define RCC_AFIOEN      (1u << 0)
#define RCC_IOPAEN      (1u << 2)
#define RCC_IOPBEN      (1u << 3)
#define RSTSCKR_RMVF    (1u << 24)
#define RSTSCKR_POR     (1u << 27)
#define AFIO_PCFR1      (*(volatile uint32_t *)0x40010004u)
#define AFIO_SWJ_CFG    (0x7u << 24)
#define AFIO_SWJ_JTAGDIS (0x2u << 24)
#define GPIOA_CFGLR     (*(volatile uint32_t *)0x40010800u)
#define GPIOA_BSHR      (*(volatile uint32_t *)0x40010810u)
#define GPIOA_INDR      (*(volatile uint32_t *)0x40010808u)
#define GPIOB_CFGLR     (*(volatile uint32_t *)0x40010C00u)
#define GPIOB_BSHR      (*(volatile uint32_t *)0x40010C10u)

#define GPIOA_LED_MASK  0xF8u
#define GPIOB_LED_MASK  0x0Bu

/* ---- FLASH registers (CH32V20x, standard program/erase) ---- */
#define FLASH_KEYR      (*(volatile uint32_t *)0x40022004u)
#define FLASH_STATR     (*(volatile uint32_t *)0x4002200Cu)
#define FLASH_CTLR      (*(volatile uint32_t *)0x40022010u)
#define FLASH_ADDR      (*(volatile uint32_t *)0x40022014u)
#define STATR_BSY       (1u << 0)
#define STATR_EOP       (1u << 5)
#define STATR_ERRS      ((1u << 2) | (1u << 4))
#define CTLR_PG         (1u << 0)
#define CTLR_PER        (1u << 1)
#define CTLR_STRT       (1u << 6)
#define CTLR_LOCK       (1u << 7)

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

/* ---- flash helpers ---- */
static void flash_unlock(void)
{
    if (FLASH_CTLR & CTLR_LOCK) {
        FLASH_KEYR = 0x45670123u;
        FLASH_KEYR = 0xCDEF89ABu;
    }
}

static int flash_wait(void)
{
    while (FLASH_STATR & STATR_BSY)
        ;
    uint32_t s = FLASH_STATR;
    FLASH_STATR = STATR_EOP | STATR_ERRS;
    return !(s & STATR_ERRS);
}

static int flash_erase4k(uint32_t addr)
{
    int ok;
    FLASH_CTLR = CTLR_PER;
    FLASH_ADDR = addr;
    FLASH_CTLR = CTLR_PER | CTLR_STRT;
    ok = flash_wait();
    FLASH_CTLR = 0;
    return ok;
}

static int flash_prog16(uint32_t addr, uint16_t hw)
{
    int ok;
    FLASH_CTLR = CTLR_PG;
    *(volatile uint16_t *)addr = hw;
    ok = flash_wait();
    FLASH_CTLR = 0;
    return ok;
}

/* ---- control transfer helpers ---- */
static int app_present(void)
{
    /* _start begins with `auipc t0,0` (0x00000297) — distinguishes a
     * real app image from erased/stale flash. */
    return *(volatile uint32_t *)APP_BASE == 0x00000297u;
}

static void __attribute__((noreturn)) jump_app(void)
{
    usb_cdc_deinit();
    __asm__ volatile ("jr %0" :: "r"(APP_BASE) : "memory");
    __builtin_unreachable();
}

static void __attribute__((noreturn)) soft_reset(void)
{
    usb_cdc_deinit();
    *(volatile uint32_t *)0xE000E048u = 0xBEEF0000u | (1u << 7);
    __builtin_unreachable();
}

static void reply(uint8_t c)
{
    usb_cdc_write(&c, 1);
}

/* ---- download protocol state ---- */
static uint8_t  hdr[6], hdr_n;
static uint32_t prog_addr, prog_left, erased_upto;
static uint16_t prog_crc, want_crc;
static uint8_t  pend_lo;
static int      have_pend;

static uint16_t crc16_byte(uint16_t crc, uint8_t b)
{
    crc ^= (uint16_t)b << 8;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                              : (uint16_t)(crc << 1);
    return crc;
}

enum { S_CMD, S_HDR, S_PROG };
static uint8_t state = S_CMD;
static uint8_t prog_fail;

static void feed(uint8_t c)
{
    switch (state) {
    case S_CMD:
        if (c == '?')
            reply('L');
        else if (c == 'R')
            soft_reset();
        else if (c == 'J') {
            if (app_present()) {
                reply('J');
                jump_app();
            } else
                reply('X');
        } else if (c == 'B') {
            hdr_n = 0;
            state = S_HDR;
        }
        break;
    case S_HDR:
        hdr[hdr_n++] = c;
        if (hdr_n == 6) {
            prog_left = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8)
                      | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
            want_crc  = (uint16_t)hdr[4] | ((uint16_t)hdr[5] << 8);
            if (!prog_left || (prog_left & 1) ||
                prog_left > APP_END - APP_BASE) {
                reply('X');
                state = S_CMD;
            } else {
                prog_addr = APP_BASE;
                erased_upto = APP_BASE;
                prog_crc = 0xFFFFu;
                have_pend = 0;
                prog_fail = 0;
                flash_unlock();
                reply('E');
                state = S_PROG;
            }
        }
        break;
    case S_PROG:
        prog_crc = crc16_byte(prog_crc, c);
        if (!have_pend) {
            pend_lo = c;
            have_pend = 1;
        } else {
            if (prog_addr >= erased_upto) {
                if (!flash_erase4k(prog_addr))
                    prog_fail = 1;
                erased_upto += 4096;
            }
            if (!prog_fail &&
                !flash_prog16(prog_addr,
                              (uint16_t)(pend_lo | ((uint16_t)c << 8))))
                prog_fail = 1;
            prog_addr += 2;
            have_pend = 0;
        }
        if (!--prog_left) {
            FLASH_CTLR = CTLR_LOCK;
            reply((!prog_fail && prog_crc == want_crc) ? 'D' : 'X');
            state = S_CMD;
        }
        break;
    }
}

int main(void)
{
    RCC_APB2PCENR |= RCC_AFIOEN | RCC_IOPAEN | RCC_IOPBEN;
    AFIO_PCFR1 = (AFIO_PCFR1 & ~AFIO_SWJ_CFG) | AFIO_SWJ_JTAGDIS;
    GPIOA_CFGLR = (GPIOA_CFGLR & ~(0xFFFFF000u | 0xFu)) | 0x33333000u | 0x4u;
    GPIOB_CFGLR = (GPIOB_CFGLR & ~0x0000F0FFu) | 0x00003033u;

    /* power-on reset -> straight to the app when one exists */
    uint32_t rst = RCC_RSTSCKR;
    RCC_RSTSCKR = RSTSCKR_RMVF;
    if ((rst & RSTSCKR_POR) && app_present())
        jump_app();

    usb_cdc_init();

    const uint32_t tick_delay = 48000000u / 400u / 3u;
    uint8_t pos = 0;
    int8_t dir = 1;
    uint32_t last = 0, t = 0;

    for (;;) {
        for (int k = 0; k < 16; k++) {
            delay_cycles(tick_delay / 16);
            usb_cdc_poll();
            int ch;
            while ((ch = usb_cdc_getchar()) >= 0)
                feed((uint8_t)ch);
        }
        t++;
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
        if ((GPIOA_INDR & 1u) && app_present())   /* PA0 pressed */
            jump_app();
    }
}
