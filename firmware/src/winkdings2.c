/*
 * winkdings2 for CH32V203G6 — POV display: scans user patterns across
 * 8 LEDs, one column at a time at 400 Hz.
 *
 * Button on PA0 (external pulldown, also the WKUP pin):
 *   - press            -> wake from standby / next pattern (wraps)
 *   - hold 2 s         -> USB programming mode (Larson scanner)
 *   - idle 30 s        -> standby
 * Programming mode speaks a tiny binary protocol over CDC-ACM.
 *
 * Register-level bare metal, no vendor library required.
 */

#include <stdint.h>
#include "flash_store.h"
#include "usb_cdc.h"
#include "prog_proto.h"

/* RCC */
#define RCC_BASE        0x40021000u
#define RCC_APB2PCENR   (*(volatile uint32_t *)(RCC_BASE + 0x18u))
#define RCC_APB1PCENR   (*(volatile uint32_t *)(RCC_BASE + 0x1Cu))
#define RCC_AFIOEN      (1u << 0)
#define RCC_IOPAEN      (1u << 2)
#define RCC_IOPBEN      (1u << 3)
#define RCC_PWREN       (1u << 28)          /* APB1 */

/* AFIO remap: PB3 is a JTAG pin after reset; SWJ_CFG = 010 keeps
 * SWD enabled and frees PB3/PB4/PA15 for GPIO. */
#define AFIO_PCFR1       (*(volatile uint32_t *)0x40010004u)
#define AFIO_SWJ_CFG     (0x7u << 24)
#define AFIO_SWJ_JTAGDIS (0x2u << 24)

/* GPIOA / GPIOB */
#define GPIOA_BASE      0x40010800u
#define GPIOB_BASE      0x40010C00u
#define GPIOA_CFGLR     (*(volatile uint32_t *)(GPIOA_BASE + 0x00u))
#define GPIOA_INDR      (*(volatile uint32_t *)(GPIOA_BASE + 0x08u))
#define GPIOA_BSHR      (*(volatile uint32_t *)(GPIOA_BASE + 0x10u))
#define GPIOB_CFGLR     (*(volatile uint32_t *)(GPIOB_BASE + 0x00u))
#define GPIOB_BSHR      (*(volatile uint32_t *)(GPIOB_BASE + 0x10u))

/* PWR — standby mode */
#define PWR_CTLR        (*(volatile uint32_t *)0x40007000u)
#define PWR_CSR         (*(volatile uint32_t *)0x40007004u)
#define PWR_PDDS        (1u << 1)
#define PWR_CWUF        (1u << 2)
#define PWR_CSBF        (1u << 3)
#define PWR_SBF         (1u << 1)   /* in PWR_CSR: standby flag */

#define PWR_EWUP        (1u << 8)

/* QingKe PFIC system control register — SLEEPDEEP */
#define PFIC_SCTLR      (*(volatile uint32_t *)0xE000ED10u)
#define PFIC_SLEEPDEEP  (1u << 2)

/* LED order, pattern bit -> pin (same as heart.c):
 *   LED1 bit0 -> PB1   LED2 bit1 -> PB0   LED8 bit7 -> PB3
 *   LED3 bit2 -> PA7   LED4 bit3 -> PA6   LED5 bit4 -> PA5
 *   LED6 bit5 -> PA4   LED7 bit6 -> PA3
 */
#define GPIOA_LED_MASK  0xF8u           /* PA3..PA7 */
#define GPIOB_LED_MASK  0x0Bu           /* PB0, PB1, PB3 */

/* Timing: the column step is the software tick (~2.5 ms). */
#define DEB_TICKS       8u              /* ~20 ms debounce        */
#define LONG_TICKS      800u            /* 2 s long press         */
#define IDLE_TICKS      12000u          /* 30 s sleep timeout     */
#define LARSON_TICKS    30u             /* 75 ms per sweep step   */

#define COL_DELAY_8M    (8000000u / 400u / 3u)
#define COL_DELAY_48M   (48000000u / 400u / 3u)

enum { EV_NONE, EV_SHORT, EV_LONG, EV_SLEEP };

static uint32_t tick;
static uint32_t col_delay = COL_DELAY_8M;

static uint8_t  btn_stable, btn_deb, btn_armed, btn_pressing;
static uint32_t press_t0, last_act;

/* ~3 core clocks per iteration (addi+bnez). Kept in asm so the
 * timing doesn't depend on codegen — the column rate and all
 * derived timeouts rely on it. */
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

/* Button poll, call once per tick. Returns EV_SHORT / EV_LONG. */
static int poll_button(void)
{
    uint8_t raw = GPIOA_INDR & 1u;
    if (raw != btn_stable) {
        if (++btn_deb >= DEB_TICKS) {
            btn_deb = 0;
            btn_stable = raw;
            last_act = tick;
            if (raw) {
                if (btn_armed) {
                    btn_pressing = 1;
                    press_t0 = tick;
                }
            } else {
                if (!btn_armed)
                    btn_armed = 1;
                else if (btn_pressing) {
                    btn_pressing = 0;
                    return EV_SHORT;
                }
            }
        }
    } else
        btn_deb = 0;

    if (!btn_armed)
        last_act = tick;            /* held at wake: stay awake, never arms */
    if (btn_pressing && tick - press_t0 >= LONG_TICKS) {
        btn_pressing = 0;
        return EV_LONG;
    }
    if (tick - last_act >= IDLE_TICKS)
        return EV_SLEEP;
    return EV_NONE;
}

/* Single lit LED bouncing 0..7, PB5 blinking in step. Runs for at
 * least `ticks` column-steps, then exits at the end of a full
 * 0->7->0 sweep.  The button is ignored — PA0 floats when no
 * external pulldown is fitted and would abort the sweep early. */
static void larson(uint32_t ticks, uint32_t cdly)
{
    int8_t pos = 0, dir = 1;
    uint32_t t = 0, last = 0;
    uint8_t pb5 = 0;

    show_column(1);                 /* LED1 on immediately */
    for (;;) {
        delay_cycles(cdly);
        t++;
        if (t - last >= LARSON_TICKS) {
            last = t;
            pb5 ^= 1;
            GPIOB_BSHR = pb5 ? (1u << 5) : (1u << (5 + 16));
            pos += dir;
            if (pos >= 7) { pos = 7; dir = -1; }
            if (pos <= 0) { pos = 0; dir = 1; }
            show_column((uint8_t)(1u << pos));
            if (t >= ticks && pos == 0 && dir == 1)
                break;              /* full sweep done */
        }
    }
    GPIOB_BSHR = 1u << (5 + 16);    /* PB5 off */
}

static void enter_standby(void)
{
    show_column(0);
    RCC_APB1PCENR |= RCC_PWREN;
    PWR_CTLR |= PWR_PDDS;           /* standby, not stop           */
    PWR_CTLR |= PWR_CWUF;           /* clear wakeup flag           */
    PWR_CSR  |= PWR_EWUP;           /* PA0/WKUP rising edge wakes  */
    PFIC_SCTLR |= PFIC_SLEEPDEEP;
    __asm__ volatile ("wfi");
    /* STANDBY_RST=1: standby exit does NOT reset — execution resumes
     * here. Callers fall back into the display loop, which is the
     * desired wake behaviour. If a config ever enables standby RST,
     * this return path is simply never taken. */
    btn_stable = btn_deb = btn_pressing = 0;
    btn_armed = !(GPIOA_INDR & 1u); /* suppress the waking press    */
}

/* ---- programming mode ---- */

static void __attribute__((noreturn)) soft_reset(void)
{
    /* let a pending reply packet drain (SIE sends autonomously) */
    delay_cycles(800000);                    /* ~50 ms at 48 MHz */
    usb_cdc_deinit();
    *(volatile uint32_t *)0xE000E048u = 0xBEEF0000u | (1u << 7);
    __builtin_unreachable();
}

/* Programming mode: Larson scanner + USB protocol until EXIT or
 * a button press (release edge after re-arming). */
static void prog_mode(void)
{
    usb_cdc_init();
    col_delay = COL_DELAY_48M;
    proto_init();

    /* local button tracking: if the button is still held from the
     * long-press that got us here, wait for a release first */
    uint8_t armed = !(GPIOA_INDR & 1u), stable = 0, deb = 0;
    uint32_t last_step = tick, press_t = 0;
    int8_t pos = 0, dir = 1;
    int done = 0;

    /* USB needs polling at ~150 us intervals (usbprog proves slower
     * breaks enumeration with -71), so each tick's delay is split
     * into 16 sub-polls.  Button/Larson logic stays per-tick. */
    while (!done) {
        for (int k = 0; k < 16 && !done; k++) {
            delay_cycles(col_delay / 16);
            usb_cdc_poll();
            int ev = proto_poll();
            if (ev == PROTO_EV_EXIT)
                done = 1;
            else if (ev == PROTO_EV_RESET)
                soft_reset();       /* lands in usbload */
        }
        if (done)
            break;
        tick++;

        /* Larson scanner — single lit LED bouncing 0..7 */
        if (tick - last_step >= LARSON_TICKS) {
            last_step = tick;
            pos += dir;
            if (pos >= 7) { pos = 7; dir = -1; }
            if (pos <= 0) { pos = 0; dir = 1; }
            show_column((uint8_t)(1u << pos));
        }

        uint8_t raw = GPIOA_INDR & 1u;
        if (raw != stable) {
            if (++deb >= DEB_TICKS) {
                deb = 0;
                stable = raw;
                if (stable)
                    press_t = tick;
                else if (!armed)
                    armed = 1;
                else
                    done = 1;       /* short press -> exit */
            }
        } else
            deb = 0;
        /* holding the button >=2 s inside prog mode resets into the
         * loader — a recovery path that works even if USB is dead */
        if (armed && stable && tick - press_t >= LONG_TICKS)
            soft_reset();
    }

    usb_cdc_deinit();
    col_delay = COL_DELAY_8M;
    show_column(0);
}

/* Display one pattern until a button event. Returns EV_*. */
static int display_pattern(uint8_t idx)
{
    fs_pattern_t pat;
    if (!fs_pattern(idx, &pat))
        return EV_SLEEP;

    for (;;) {
        for (uint8_t c = 0; c < pat.ncols; c++) {
            show_column(pat.cols[c]);
            delay_cycles(col_delay);
            tick++;
            int ev = poll_button();
            if (ev != EV_NONE)
                return ev;
        }
    }
}

int main(void)
{
    /* Still on 8 MHz HSI out of reset. GPIOA (PA3..PA7) + GPIOB
     * (PB0/PB1/PB3 + PB5 blink) LED outputs, push-pull 50 MHz;
     * PA0 input float. */
    RCC_APB2PCENR |= RCC_AFIOEN | RCC_IOPAEN | RCC_IOPBEN;
    AFIO_PCFR1 = (AFIO_PCFR1 & ~AFIO_SWJ_CFG) | AFIO_SWJ_JTAGDIS;
    GPIOA_CFGLR = (GPIOA_CFGLR & ~(0xFFFFF000u | 0xFu))
                | 0x33333000u | 0x4u;
    GPIOB_CFGLR = (GPIOB_CFGLR & ~0x00F0F0FFu) | 0x00303033u;

    /* Startup sweep: ~5 s (2000 ticks @ ~400 Hz), ends on a full
     * 0->7->0 pass; a button press skips it. */
    larson(2000, COL_DELAY_8M);

    RCC_APB1PCENR |= RCC_PWREN;
    PWR_CTLR |= PWR_CSBF;           /* clear standby flag */

    fs_init();
    uint8_t count = fs_count();
    uint8_t idx = 0;

    /* If the button is still held from the wakeup edge it is not
     * armed until released — wake always just shows pattern 1. */
    btn_armed = !(GPIOA_INDR & 1u);

    for (;;) {
        if (!count)
            enter_standby();        /* no valid patterns: nothing to do */

        switch (display_pattern(idx)) {
        case EV_SHORT:
            idx = (idx + 1) % count;
            break;
        case EV_LONG:
            prog_mode();
            idx = 0;
            count = fs_count();
            btn_armed = !(GPIOA_INDR & 1u);
            btn_stable = btn_deb = btn_pressing = 0;
            break;
        default:
            enter_standby();        /* EV_SLEEP */
            break;
        }
    }
}
