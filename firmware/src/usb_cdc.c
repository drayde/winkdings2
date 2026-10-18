/*
 * Bare-metal polled USB CDC-ACM for CH32V203.
 *
 * The G6U6 carries only the STM32F1-style USBD block (the USBFS
 * host/device block is not pinned out on this package):
 *   endpoint regs  EPR[0..7]            @ 0x40005C00 + 4*n
 *   CNTR/ISTR/DADDR/BTABLE              @ +0x40/0x44/0x4C/0x50
 *   packet memory (PMA, 512B, CAN-shared) @ 0x40006000,
 *   16-bit words on a 32-bit stride:  cpu_addr = base + pma_off * 2
 *
 * Register usage verified against WCH EVT USBLIB and cnlohr's ch32fun
 * usbd.c.  USB pins are PA11(DM)/PA12(DP); pull-up via
 * EXTEN_USBD_PU_EN; USB clock = PLLCLK / USBPRE, USBPRE[1:0]=00 for
 * PLLCLK=48MHz (DIV1).  Polled (no IRQs): call usb_cdc_poll() often.
 */

#include <stdint.h>
#include "usb_cdc.h"

/* ---- RCC / EXTEN / GPIOA ---- */
#define RCC_CTLR        (*(volatile uint32_t *)0x40021000u)
#define RCC_CFGR0       (*(volatile uint32_t *)0x40021004u)
#define RCC_APB2PCENR   (*(volatile uint32_t *)0x40021018u)
#define RCC_APB1PCENR   (*(volatile uint32_t *)0x4002101Cu)

#define RCC_CTLR_PLLON      (1u << 24)
#define RCC_CTLR_PLLRDY     (1u << 25)
#define RCC_CFGR0_SW        0x3u
#define RCC_CFGR0_SW_PLL    0x2u
#define RCC_CFGR0_SWS       (0x3u << 2)
#define RCC_CFGR0_SWS_PLL   (0x2u << 2)
#define RCC_CFGR0_PLLMULL   (0xFu << 18)
#define RCC_CFGR0_USBPRE    (0x3u << 22)  /* USBPRE[1:0]; 00 = PLLCLK/1 */
#define RCC_CFGR0_PPRE1_DIV2 (0x4u << 8)  /* APB1 = HCLK/2              */
#define RCC_APB1_USBEN      (1u << 23)
#define RCC_APB2_IOPAEN     (1u << 2)

#define EXTEN_CTR           (*(volatile uint32_t *)0x40023800u)
#define EXTEN_USBD_PU_EN    (1u << 1)    /* internal D+ pull-up       */
#define EXTEN_PLL_HSI_PRE   (1u << 4)    /* PLL source = HSI, not /2  */

#define GPIOA_CFGHR     (*(volatile uint32_t *)0x40010804u)
#define GPIOA_BSHR      (*(volatile uint32_t *)0x40010810u)
/* PA11 = D-, PA12 = D+ -> CFGHR nibbles 3,4 */

/* ---- USBD registers ---- */
#define USBD_BASE       0x40005C00u
#define USBD_EPR(n)     (*(volatile uint16_t *)(USBD_BASE + 4u * (n)))
#define USBD_CNTR       (*(volatile uint16_t *)(USBD_BASE + 0x40u))
#define USBD_ISTR       (*(volatile uint16_t *)(USBD_BASE + 0x44u))
#define USBD_DADDR      (*(volatile uint16_t *)(USBD_BASE + 0x4Cu))
#define USBD_BTABLE     (*(volatile uint16_t *)(USBD_BASE + 0x50u))

#define CNTR_FRES       0x0001u
#define CNTR_PDWN       0x0002u

#define ISTR_EP_ID      0x000Fu
#define ISTR_ESOF       0x0100u
#define ISTR_SOF        0x0200u
#define ISTR_RESET      0x0400u
#define ISTR_SUSP       0x0800u
#define ISTR_WKUP       0x1000u
#define ISTR_ERR        0x2000u
#define ISTR_PMAOVR     0x4000u
#define ISTR_CTR        0x8000u

/* EPR bits: CTR_* clear on write-0, DTOG_ and STAT_ toggle on write-1 */
#define EPR_CTR_RX      0x8000u
#define EPR_DTOG_RX     0x4000u
#define EPR_STAT_RX     0x3000u
#define EPR_RX_DIS      0x0000u
#define EPR_RX_STALL    0x1000u
#define EPR_RX_NAK      0x2000u
#define EPR_RX_ACK      0x3000u
#define EPR_SETUP       0x0800u
#define EPR_EP_TYPE     0x0600u
#define EPR_TYPE_BULK   0x0000u
#define EPR_TYPE_CTRL   0x0200u
#define EPR_KIND        0x0100u
#define EPR_CTR_TX      0x0080u
#define EPR_DTOG_TX     0x0040u
#define EPR_STAT_TX     0x0030u
#define EPR_TX_DIS      0x0000u
#define EPR_TX_STALL    0x0010u
#define EPR_TX_NAK      0x0020u
#define EPR_TX_ACK      0x0030u
#define EPR_EA          0x000Fu

#define EPR_FIXED       (EPR_EA | EPR_EP_TYPE | EPR_KIND)

#define DADDR_EF        0x80u

/* PMA map: BDT at offset 0 (8 B/EP), buffers above 0x40 */
#define PMA_BASE        0x40006000u
#define PMA16(off)      (*(volatile uint16_t *)(PMA_BASE + (off) * 2u))
#define BDT(ep, field)  PMA16((ep) * 8u + (field))
#define BDT_ADD_TX      0u
#define BDT_CNT_TX      2u
#define BDT_ADD_RX      4u
#define BDT_CNT_RX      6u

#define PMA_EP0_TX      0x40u
#define PMA_EP0_RX      0x80u
#define PMA_EP1_TX      0xC0u
#define PMA_EP2_RX      0x100u
#define PMA_EP3_TX      0x140u

#define EP_PKTSZ        64u
/* COUNT_RX for a 64-byte buffer: BLSIZE | (NUM_BLOCK<<10) */
#define RX_COUNT_64     (0x8000u | (1u << 10))

#define EP_CDC_OUT      2
#define EP_CDC_IN       3

#define USB_GET_STATUS      0x00u
#define USB_SET_ADDRESS     0x05u
#define USB_GET_DESCRIPTOR  0x06u
#define USB_GET_CONFIG      0x08u
#define USB_SET_CONFIG      0x09u
#define USB_GET_INTERFACE   0x0Au
#define USB_SET_INTERFACE   0x0Bu

#define REQ_TYPE        0x60u
#define REQ_STANDARD    0x00u
#define REQ_CLASS       0x20u

/* ---- state ---- */
static uint8_t  dev_addr;         /* pending SET_ADDRESS value */
static uint8_t  ctl_req;
static uint16_t ctl_len;          /* remaining IN data-stage bytes */
static uint8_t  ctl_status;       /* send a zero-length status IN */
static const uint8_t *ctl_ptr;
static uint8_t  configured;
static volatile uint8_t ep_busy[8];

#define RX_FIFO_SIZE    256u
static uint8_t  rx_fifo[RX_FIFO_SIZE];
static volatile uint8_t rx_head, rx_tail;

volatile uint8_t usb_cdc_seen;      /* diag: host traffic observed */
volatile uint32_t usb_cdc_rx_total; /* bytes received on the CDC OUT ep */

/* ---- helpers ---- */
static void delay_short(volatile uint32_t n)
{
    while (n--)
        __asm__ volatile ("nop");
}

static void pma_write(uint16_t off, const uint8_t *src, uint16_t len)
{
    for (uint16_t i = 0; i < len; i += 2) {
        uint16_t w = src[i];
        if (i + 1 < len)
            w |= (uint16_t)src[i + 1] << 8;
        PMA16(off + i) = w;
    }
}

/* set STAT bits (toggle semantics), keep CTR bits, EA/TYPE/KIND */
static void ep_stat(int ep, uint16_t mask, uint16_t val)
{
    uint16_t r = USBD_EPR(ep);
    USBD_EPR(ep) = (r & EPR_FIXED) | ((r ^ val) & mask)
                 | EPR_CTR_RX | EPR_CTR_TX;
}

static void ep_clear_rx(int ep)   /* clear CTR_RX, keep rest */
{
    uint16_t r = USBD_EPR(ep);
    USBD_EPR(ep) = (r & EPR_FIXED) | EPR_CTR_TX;
}

static void ep_clear_tx(int ep)   /* clear CTR_TX, keep rest */
{
    uint16_t r = USBD_EPR(ep);
    USBD_EPR(ep) = (r & EPR_FIXED) | EPR_CTR_RX;
}

static uint8_t ep0_stalled;

/* After a SETUP the data stage must start at DATA1 on both
 * directions: force DTOG_TX/DTOG_RX to 1 (toggle-on-write-1). */
static void ep0_dtok_sync(void)
{
    uint16_t r = USBD_EPR(0);
    USBD_EPR(0) = (r & EPR_FIXED) | EPR_CTR_RX | EPR_CTR_TX
                | (~r & (EPR_DTOG_RX | EPR_DTOG_TX));
}

static void ep0_stall(void)
{
    ep0_stalled = 1;
    ep_stat(0, EPR_STAT_TX, EPR_TX_STALL);
    ep_stat(0, EPR_STAT_RX, EPR_RX_STALL);
}

/* ---- descriptors ---- */
static const uint8_t desc_dev[18] = {
    18, 1, 0x00, 0x02,              /* USB 2.00 */
    0x02, 0x00, 0x00,               /* class CDC, sub 0, proto 0 */
    EP_PKTSZ,
    0x48, 0x04,                     /* VID  0x0448 */
    USB_CDC_PID & 0xFF, USB_CDC_PID >> 8,
    0x00, 0x01,                     /* bcdDevice 1.00 */
    1, 2, 3, 1,
};

static const uint8_t desc_cfg[75] = {
    9, 2, 75, 0, 2, 1, 0, 0xC0, 50,         /* cfg, 100 mA       */
    8, 0x0B, 0, 2, 0x02, 0x02, 0x00, 0,     /* IAD: ifaces 0+1   */
    9, 4, 0, 0, 1, 0x02, 0x02, 0x00, 0,     /* intf0: CDC ctl    */
    5, 0x24, 0x00, 0x10, 0x01,              /*   header v1.10    */
    5, 0x24, 0x01, 0x00, 0x01,              /*   call mgmt       */
    4, 0x24, 0x02, 0x06,                    /*   ACM caps        */
    5, 0x24, 0x06, 0, 1,                    /*   union 0->1      */
    7, 5, 0x81, 0x03, 16, 0, 10,            /*   EP1 IN intr 16B */
    9, 4, 1, 0, 2, 0x0A, 0, 0, 0,           /* intf1: CDC data   */
    7, 5, 0x02, 0x02, 64, 0, 0,             /*   EP2 OUT bulk    */
    7, 5, 0x83, 0x02, 64, 0, 0,             /*   EP3 IN  bulk    */
};

static const uint8_t desc_str0[] = { 4, 3, 0x09, 0x04 };
static const uint8_t desc_str1[] = {
    20, 3, 'w',0,'i',0,'n',0,'k',0,'d',0,'i',0,'n',0,'g',0,'s',0 };
static const uint8_t desc_str2[] = {
    20, 3, 'w',0,'i',0,'n',0,'k',0,'d',0,'i',0,'n',0,'g',0,'s',0 };
static const uint8_t desc_str3[] = { 10, 3, '0',0,'0',0,'0',0,'1',0 };

static const struct {
    uint16_t index;         /* wValue = (type<<8)|index */
    const uint8_t *data;
    uint16_t len;
} descs[] = {
    { 0x0100, desc_dev,  sizeof(desc_dev)  },
    { 0x0200, desc_cfg,  sizeof(desc_cfg)  },
    { 0x0300, desc_str0, sizeof(desc_str0) },
    { 0x0301, desc_str1, sizeof(desc_str1) },
    { 0x0302, desc_str2, sizeof(desc_str2) },
    { 0x0303, desc_str3, sizeof(desc_str3) },
};

static const uint8_t line_coding[7] = {
    0x00, 0xC2, 0x01, 0x00,         /* 115200 baud */
    0x00, 0x00, 0x08,               /* 1 stop, no parity, 8 bits */
};

/* ---- endpoint/transfer machinery ---- */
static void usb_reset(void)
{
    USBD_BTABLE = 0;

    for (int ep = 0; ep < 4; ep++) {
        USBD_EPR(ep) = ep;
        ep_stat(ep, EPR_EP_TYPE, EPR_TYPE_BULK);
        ep_stat(ep, EPR_STAT_RX, EPR_RX_ACK);
        ep_stat(ep, EPR_STAT_TX, EPR_TX_NAK);
        ep_busy[ep] = 0;
    }
    ep_stat(0, EPR_EP_TYPE, EPR_TYPE_CTRL);

    /* clear EP0 data toggles (WCH USBLIB does the same on reset) */
    {
        uint16_t r = USBD_EPR(0);
        USBD_EPR(0) = (r & EPR_FIXED) | EPR_CTR_RX | EPR_CTR_TX
                    | (r & (EPR_DTOG_RX | EPR_DTOG_TX));
    }

    BDT(0, BDT_ADD_TX) = PMA_EP0_TX;
    BDT(0, BDT_CNT_TX) = 0;
    BDT(0, BDT_ADD_RX) = PMA_EP0_RX;
    BDT(0, BDT_CNT_RX) = RX_COUNT_64;
    BDT(1, BDT_ADD_TX) = PMA_EP1_TX;
    BDT(2, BDT_ADD_RX) = PMA_EP2_RX;
    BDT(2, BDT_CNT_RX) = RX_COUNT_64;
    BDT(3, BDT_ADD_TX) = PMA_EP3_TX;

    /* single-direction endpoints: disable the unused side */
    ep_stat(1, EPR_STAT_RX, EPR_RX_DIS);
    ep_stat(2, EPR_STAT_TX, EPR_TX_DIS);
    ep_stat(3, EPR_STAT_RX, EPR_RX_DIS);

    dev_addr = 0;
    ctl_len = 0;
    ctl_status = 0;
    ctl_req = 0xFF;
    configured = 0;
    rx_head = rx_tail = 0;

    USBD_DADDR = DADDR_EF;      /* enable, address 0 */
}

static void handle_setup(void)
{
    uint8_t req[8];
    for (int i = 0; i < 8; i += 2) {
        uint16_t w = PMA16(PMA_EP0_RX + i);
        req[i] = w & 0xFF;
        req[i + 1] = w >> 8;
    }
    uint8_t  rtype  = req[0], rq = req[1];
    uint16_t wvalue = (uint16_t)req[2] | ((uint16_t)req[3] << 8);
    uint16_t wlen   = (uint16_t)req[6] | ((uint16_t)req[7] << 8);

    ctl_req = rq;
    ctl_ptr = 0;
    ctl_len = 0;
    ctl_status = 0;

    if ((rtype & REQ_TYPE) == REQ_STANDARD) {
        switch (rq) {
        case USB_GET_STATUS: {
            static const uint8_t z[2] = { 0, 0 };
            ctl_ptr = z;
            ctl_len = 2;
            break;
        }
        case USB_SET_ADDRESS:
            dev_addr = wvalue & 0xFF;
            ctl_status = 1;
            break;
        case USB_GET_DESCRIPTOR:
            for (unsigned i = 0; i < sizeof(descs) / sizeof(descs[0]); i++) {
                if (descs[i].index == wvalue) {
                    ctl_ptr = descs[i].data;
                    ctl_len = descs[i].len;
                    if (ctl_len > wlen)
                        ctl_len = wlen;
                    break;
                }
            }
            if (!ctl_ptr && rq == USB_GET_DESCRIPTOR)
                ep0_stall();
            break;
        case USB_GET_CONFIG:
            ctl_ptr = &configured;
            ctl_len = 1;
            break;
        case USB_SET_CONFIG:
            configured = wvalue & 0xFF;
            ctl_status = 1;
            break;
        case USB_GET_INTERFACE: {
            static const uint8_t z[1] = { 0 };
            ctl_ptr = z;
            ctl_len = 1;
            break;
        }
        case USB_SET_INTERFACE:
            ctl_status = 1;
            break;
        default:
            ctl_status = 1;         /* CLEAR/SET_FEATURE etc.: just ACK */
            break;
        }
    } else if ((rtype & REQ_TYPE) == REQ_CLASS) {
        if (rq == 0x21) {               /* GET_LINE_CODING */
            ctl_ptr = line_coding;
            ctl_len = 7;
        } else                          /* SET_LINE_CODING, SET_CONTROL_.. */
            ctl_status = 1;
    } else
        ep0_stall();
}

void usb_cdc_poll(void)
{
    uint16_t istr = USBD_ISTR;
    if (!istr)
        return;
    if (istr & (ISTR_RESET | ISTR_CTR))
        usb_cdc_seen = 1;

    if (istr & ISTR_CTR) {
        int ep = istr & ISTR_EP_ID;
        uint16_t epr = USBD_EPR(ep);

        if (epr & EPR_CTR_RX) {
            ep0_stalled = 0;
            if (ep == 0 && (epr & EPR_SETUP)) {
                ep0_dtok_sync();
                handle_setup();
            } else {
                uint16_t n = BDT(ep, BDT_CNT_RX) & 0x3FF;
                if (ep == EP_CDC_OUT) {
                    usb_cdc_rx_total += n;
                    for (uint16_t i = 0; i < n; i++) {
                        uint8_t b = (uint8_t)
                            (PMA16(PMA_EP2_RX + (i & ~1u)) >> ((i & 1u) << 3));
                        uint8_t next = rx_head + 1;
                        if (next != rx_tail) {
                            rx_fifo[rx_head] = b;
                            rx_head = next;
                        }
                    }
                    /* SIE overwrote COUNT_RX with the byte count and
                     * NAKed RX — restore buffer size and re-arm */
                    BDT(ep, BDT_CNT_RX) = RX_COUNT_64;
                    ep_stat(ep, EPR_STAT_RX, EPR_RX_ACK);
                } else if (ep == 0 && n) {
                    ctl_status = 1;   /* control-write data stage done */
                }
            }
            ep_clear_rx(ep);
        }

        /* arm EP0 TX with the next data chunk or pending status ZLP,
         * and always keep EP0 RX listening (status OUT / next SETUP) */
        if (ep == 0 && !ep0_stalled) {
            if (ctl_len || ctl_status) {
                uint16_t l = ctl_len > EP_PKTSZ ? EP_PKTSZ : ctl_len;
                if (ctl_ptr) {
                    pma_write(PMA_EP0_TX, ctl_ptr, l);
                    ctl_ptr += l;
                }
                BDT(0, BDT_CNT_TX) = l;
                ctl_len -= l;
                ctl_status = 0;
                ep_stat(0, EPR_STAT_TX, EPR_TX_ACK);
            }
            /* COUNT_RX holds the last received length — rewrite the
             * buffer-size descriptor before listening again */
            BDT(0, BDT_CNT_RX) = RX_COUNT_64;
            ep_stat(0, EPR_STAT_RX, EPR_RX_ACK);
        }

        if (epr & EPR_CTR_TX) {
            if (ep == 0 && dev_addr) {
                USBD_DADDR = DADDR_EF | dev_addr;
                dev_addr = 0;
            }
            ep_busy[ep] = 0;
            ep_clear_tx(ep);
        }
        USBD_ISTR = (uint16_t)~ISTR_CTR;
        return;
    }

    if (istr & ISTR_RESET) {
        USBD_ISTR = (uint16_t)~ISTR_RESET;
        usb_reset();
        return;
    }

    /* don't care: clear the rest */
    USBD_ISTR = (uint16_t)~(ISTR_ESOF | ISTR_SOF | ISTR_WKUP | ISTR_SUSP |
                            ISTR_ERR | ISTR_PMAOVR);
}

/* ---- public API ---- */
int usb_cdc_getchar(void)
{
    if (rx_head == rx_tail)
        return -1;
    uint8_t b = rx_fifo[rx_tail];
    rx_tail = rx_tail + 1;
    return b;
}

void usb_cdc_write(const void *data, uint16_t len)
{
    const uint8_t *p = data;
    while (len) {
        /* wait for the previous packet; bail if the host went away */
        for (volatile uint32_t t = 0; ep_busy[EP_CDC_IN]; t++) {
            if (t > 48000000u)      /* ~1s */
                return;
            usb_cdc_poll();
        }
        uint16_t l = len > EP_PKTSZ ? EP_PKTSZ : len;
        pma_write(PMA_EP3_TX, p, l);
        BDT(EP_CDC_IN, BDT_CNT_TX) = l;
        ep_busy[EP_CDC_IN] = 1;
        ep_stat(EP_CDC_IN, EPR_STAT_TX, EPR_TX_ACK);
        p += l;
        len -= l;
    }
}

void usb_cdc_init(void)
{
    /* 48 MHz sysclk from HSI: EXTEN_PLL_HSI_PRE makes the PLL input
     * the undivided 8 MHz HSI; PLL x6 -> 48 MHz; USBPRE[1:0]=00 ->
     * USBCLK = PLLCLK = 48 MHz. */
    RCC_APB2PCENR |= RCC_APB2_IOPAEN;
    EXTEN_CTR |= EXTEN_PLL_HSI_PRE;
    RCC_CFGR0 = (RCC_CFGR0 & ~RCC_CFGR0_PLLMULL & ~RCC_CFGR0_SW
                 & ~RCC_CFGR0_USBPRE)
              | ((6u - 2u) << 18) | RCC_CFGR0_PPRE1_DIV2;
    RCC_CTLR |= RCC_CTLR_PLLON;
    while (!(RCC_CTLR & RCC_CTLR_PLLRDY))
        ;
    RCC_CFGR0 = (RCC_CFGR0 & ~RCC_CFGR0_SW) | RCC_CFGR0_SW_PLL;
    while ((RCC_CFGR0 & RCC_CFGR0_SWS) != RCC_CFGR0_SWS_PLL)
        ;

    /* drive PA11/PA12 low briefly so the host sees a clean attach,
     * then release to floating inputs for the USB transceiver */
    GPIOA_CFGHR = (GPIOA_CFGHR & ~((0xFu << 12) | (0xFu << 16)))
                | (0x2u << 12) | (0x2u << 16);
    GPIOA_BSHR = (1u << (16 + 11)) | (1u << (16 + 12));
    delay_short(400000);
    GPIOA_CFGHR = (GPIOA_CFGHR & ~((0xFu << 12) | (0xFu << 16)))
                | (0x4u << 12) | (0x4u << 16);

    RCC_APB1PCENR |= RCC_APB1_USBEN;
    USBD_CNTR = CNTR_FRES;
    USBD_CNTR = 0;
    delay_short(1000);
    usb_reset();

    EXTEN_CTR |= EXTEN_USBD_PU_EN;    /* D+ pull-up: enumerate */
    USBD_CNTR = 0x9C00u;              /* CTRM|WKUPM|SUSPM|RESETM (polled,
                                          but mask bits must be set) */
    USBD_ISTR = 0;
}

void usb_cdc_deinit(void)
{
    EXTEN_CTR &= ~EXTEN_USBD_PU_EN;
    USBD_CNTR = CNTR_FRES | CNTR_PDWN;
    RCC_APB1PCENR &= ~RCC_APB1_USBEN;

    /* PA11/PA12 stay floating inputs */

    /* sysclk back to 8 MHz HSI */
    RCC_CFGR0 &= ~RCC_CFGR0_SW;
    while (RCC_CFGR0 & RCC_CFGR0_SWS)
        ;
    RCC_CTLR &= ~RCC_CTLR_PLLON;
    EXTEN_CTR &= ~EXTEN_PLL_HSI_PRE;
}
