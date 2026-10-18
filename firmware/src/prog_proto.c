/*
 * Programming-mode protocol — shared between winkdings2 (button-
 * activated) and usbprog (test firmware).
 *
 * Bytes over CDC-ACM:
 *   host->dev   0x00 INFO   -> [OK,'W','K','2',ver,size_lo,size_hi,count]
 *               0x01 READ   -> [OK,len_lo,len_hi] + blob bytes
 *               0x02 WRITE  u16 len + blob -> [OK] / [err]
 *               0x03 EXIT   -> [OK]  (proto_poll returns 1)
 *               0x04 RESET  -> [OK]  then caller soft-resets
 * Anything else -> [ST_ERR].  A second command may not arrive in the
 * same packet — the state machine is byte-serial and waits.
 */
#include <stdint.h>
#include "flash_store.h"
#include "usb_cdc.h"
#include "prog_proto.h"

#define FW_VER      1u
#define ST_OK       0x00u
#define ST_ERR_LEN  0x01u
#define ST_ERR_CRC  0x02u
#define ST_ERR      0x04u

static uint8_t  rx_buf[FS_REGION_SIZE];
static uint8_t  rx_state;           /* 0 cmd, 1 len_lo, 2 len_hi, 3 data */
static uint16_t rx_len, rx_pos;
static uint8_t  exit_flag, reset_flag;

void proto_init(void)
{
    rx_state = 0;
    exit_flag = reset_flag = 0;
}

int proto_poll(void)
{
    int ch;
    while ((ch = usb_cdc_getchar()) >= 0) {
        switch (rx_state) {
        case 0: {
            uint8_t r[8] = { ST_OK, 'W', 'K', '2', FW_VER,
                             FS_REGION_SIZE & 0xFF, FS_REGION_SIZE >> 8,
                             fs_count() };
            switch (ch) {
            case PROTO_CMD_INFO:
                usb_cdc_write(r, sizeof(r));
                break;
            case PROTO_CMD_READ: {
                uint16_t n = fs_blob_len();
                uint8_t h[3] = { n ? ST_OK : ST_ERR, n & 0xFF, n >> 8 };
                usb_cdc_write(h, 3);
                if (n)
                    usb_cdc_write((const void *)FS_REGION_BASE, n);
                break;
            }
            case PROTO_CMD_WRITE:
                rx_state = 1;
                break;
            case PROTO_CMD_EXIT: {
                uint8_t ok = ST_OK;
                usb_cdc_write(&ok, 1);
                exit_flag = 1;
                break;
            }
            case PROTO_CMD_RESET: {
                uint8_t ok = ST_OK;
                usb_cdc_write(&ok, 1);
                reset_flag = 1;
                break;
            }
            default: {
                uint8_t e = ST_ERR;
                usb_cdc_write(&e, 1);
                break;
            }
            }
            break;
        }
        case 1:
            rx_len = ch;
            rx_state = 2;
            break;
        case 2:
            rx_len |= (uint16_t)ch << 8;
            rx_pos = 0;
            if (!rx_len) {
                uint8_t e = ST_ERR_LEN;
                usb_cdc_write(&e, 1);
                rx_state = 0;
            } else
                rx_state = 3;
            break;
        case 3:
            if (rx_pos < sizeof(rx_buf))
                rx_buf[rx_pos] = ch;
            rx_pos++;
            if (rx_pos >= rx_len) {
                int st = ST_ERR_LEN;
                if (rx_len <= sizeof(rx_buf))
                    st = fs_write(rx_buf, rx_len);
                uint8_t r = st ? (st == 2 ? ST_ERR_CRC : ST_ERR) : ST_OK;
                usb_cdc_write(&r, 1);
                rx_state = 0;
            }
            break;
        }
    }
    if (exit_flag)
        return PROTO_EV_EXIT;
    if (reset_flag)
        return PROTO_EV_RESET;
    return 0;
}
