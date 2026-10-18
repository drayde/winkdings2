/*
 * Pattern blob storage for winkdings2 — top 4K of code flash.
 *
 * Blob layout (little-endian):
 *   u16 magic    'W','K' (0x4B57)
 *   u8  version  = 2
 *   u8  count    number of patterns (1..255)
 *   records, packed back-to-back:
 *      u8  ncols (1..255)
 *      u8  cols[ncols]   one byte per column, bit0 = LED1 = top row
 *   u16 crc16    CRC-16/CCITT (poly 0x1021, init 0xFFFF) over all
 *                preceding bytes; blob is padded with 0xFF to even
 *                length before the CRC
 */
#ifndef FLASH_STORE_H
#define FLASH_STORE_H

#include <stdint.h>

/* Keep in sync with link_winkdings2.ld (FLASH LENGTH = 28K). */
#define FS_REGION_BASE  0x08007000u
#define FS_REGION_SIZE  4096u
#define FS_MAGIC        0x4B57u
#define FS_VERSION      2u

typedef struct {
    const uint8_t *cols;
    uint8_t ncols;
} fs_pattern_t;

/* Validate the blob in flash; if invalid, write the built-in heart. */
void     fs_init(void);

/* Number of patterns in the blob (0 when the blob is invalid). */
uint8_t  fs_count(void);

/* Fetch pattern idx; returns 1 on success, 0 if out of range/invalid. */
int      fs_pattern(uint8_t idx, fs_pattern_t *out);

/* Total stored blob length in bytes (header + records + crc), or 0. */
uint16_t fs_blob_len(void);

/* Validate + erase region + program + verify. Returns 0 on success. */
int      fs_write(const uint8_t *blob, uint16_t len);

uint16_t fs_crc16(const uint8_t *data, uint16_t len);

#endif
