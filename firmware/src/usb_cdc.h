/*
 * Minimal polled USB CDC-ACM device for CH32V203 (STM32F1-style USBD
 * peripheral + 512B packet memory). Only meant to run while the device
 * is in programming mode — call usb_cdc_init() to switch the system
 * clock to 48 MHz (HSI/PLL) and enumerate, usb_cdc_deinit() to return
 * to 8 MHz HSI.
 */
#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>

void usb_cdc_init(void);     /* switch sysclk to 48 MHz, init USBD */
void usb_cdc_deinit(void);   /* shut USBD down, back to 8 MHz HSI  */
void usb_cdc_poll(void);     /* service pending USB events         */

/* Next byte from the host, or -1 when the RX fifo is empty. */
int  usb_cdc_getchar(void);

/* Queue bytes for the host; blocks while the 64B packet drains. */
void usb_cdc_write(const void *data, uint16_t len);

#endif
