#include "flash_store.h"

/* CH32V20x FLASH registers (0x40022000, STM32F1-style + WCH ext) */
#define FLASH_KEYR      (*(volatile uint32_t *)0x40022004u)
#define FLASH_STATR     (*(volatile uint32_t *)0x4002200Cu)
#define FLASH_CTLR      (*(volatile uint32_t *)0x40022010u)
#define FLASH_ADDR      (*(volatile uint32_t *)0x40022014u)

#define STATR_BSY       (1u << 0)
#define STATR_PGERR     (1u << 2)
#define STATR_WRPRTERR  (1u << 4)
#define STATR_EOP       (1u << 5)
#define STATR_ERRS      (STATR_PGERR | STATR_WRPRTERR)

#define CTLR_PG         (1u << 0)
#define CTLR_PER        (1u << 1)
#define CTLR_STRT       (1u << 6)
#define CTLR_LOCK       (1u << 7)

#define FLASH_KEY1      0x45670123u
#define FLASH_KEY2      0xCDEF89ABu

/* The factory pattern — same heart as heart.c (incl. blank
 * wrap column). */
static const uint8_t heart_cols[] = {
    0x06, 0x0F, 0x1F, 0x3E, 0x7E, 0xFC,
    0x7C, 0x3E, 0x1F, 0x0F, 0x06, 0x04,
    0x00,
};

uint16_t fs_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    while (len--) {
        crc ^= (uint16_t)*data++ << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                : (uint16_t)(crc << 1);
    }
    return crc;
}

/* Walk the record list; returns offset just past the last record
 * (unpadded), or 0 if the blob is structurally invalid. */
static uint16_t blob_records_end(const uint8_t *b, uint16_t size)
{
    if (*(const volatile uint16_t *)b != FS_MAGIC || b[2] != FS_VERSION)
        return 0;
    uint8_t count = b[3];
    if (count == 0)
        return 0;
    uint16_t p = 4;
    for (uint8_t i = 0; i < count; i++) {
        if (p + 1 >= size)
            return 0;
        uint8_t ncols = b[p++];
        if (ncols == 0 || p + ncols > size)
            return 0;
        p += ncols;
    }
    return p;
}

/* Full validation incl. padding + CRC. Returns blob length or 0. */
static uint16_t blob_validate(const uint8_t *b, uint16_t size)
{
    uint16_t end = blob_records_end(b, size);
    if (!end)
        return 0;
    if (end & 1)
        end++;                      /* 0xFF pad byte */
    if (end + 2 > size)
        return 0;
    uint16_t want = (uint16_t)b[end] | ((uint16_t)b[end + 1] << 8);
    if (fs_crc16(b, end) != want)
        return 0;
    return end + 2;
}

static int flash_unlock(void)
{
    if (FLASH_CTLR & CTLR_LOCK) {
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
    return !(FLASH_CTLR & CTLR_LOCK);
}

static int flash_wait(void)
{
    while (FLASH_STATR & STATR_BSY)
        ;
    uint32_t s = FLASH_STATR;
    FLASH_STATR = STATR_EOP | STATR_ERRS;
    return !(s & STATR_ERRS);
}

void fs_init(void)
{
    if (blob_validate((const uint8_t *)FS_REGION_BASE, FS_REGION_SIZE))
        return;

    /* Fresh/corrupt region: install the default heart pattern */
    uint8_t buf[4 + 1 + sizeof(heart_cols) + 1 + 2];
    uint16_t n = 0;
    buf[n++] = FS_MAGIC & 0xFF;
    buf[n++] = FS_MAGIC >> 8;
    buf[n++] = FS_VERSION;
    buf[n++] = 1;                   /* count */
    buf[n++] = sizeof(heart_cols);
    for (unsigned i = 0; i < sizeof(heart_cols); i++)
        buf[n++] = heart_cols[i];
    if (n & 1)
        buf[n++] = 0xFF;            /* pad */
    uint16_t crc = fs_crc16(buf, n);
    buf[n++] = crc & 0xFF;
    buf[n++] = crc >> 8;
    fs_write(buf, n);
}

uint8_t fs_count(void)
{
    const uint8_t *b = (const uint8_t *)FS_REGION_BASE;
    if (!blob_validate(b, FS_REGION_SIZE))
        return 0;
    return b[3];
}

uint16_t fs_blob_len(void)
{
    return blob_validate((const uint8_t *)FS_REGION_BASE, FS_REGION_SIZE);
}

int fs_pattern(uint8_t idx, fs_pattern_t *out)
{
    const uint8_t *b = (const uint8_t *)FS_REGION_BASE;
    if (!blob_validate(b, FS_REGION_SIZE) || idx >= b[3])
        return 0;
    uint16_t p = 4;
    for (uint8_t i = 0; i < idx; i++)
        p += 1 + b[p];
    out->ncols = b[p];
    out->cols = b + p + 1;
    return 1;
}

int fs_write(const uint8_t *blob, uint16_t len)
{
    if (len < 7 || len > FS_REGION_SIZE || (len & 1))
        return 1;
    if (blob_validate(blob, len) != len)
        return 2;
    if (!flash_unlock())
        return 3;

    /* Standard erase: one 4K sector covers the whole region */
    FLASH_CTLR = CTLR_PER;
    FLASH_ADDR = FS_REGION_BASE;
    FLASH_CTLR = CTLR_PER | CTLR_STRT;
    if (!flash_wait())
        goto fail;

    /* Standard program: one halfword at a time */
    FLASH_CTLR = CTLR_PG;
    volatile uint16_t *dst = (volatile uint16_t *)FS_REGION_BASE;
    for (uint16_t i = 0; i < len / 2; i++) {
        dst[i] = (uint16_t)blob[2 * i] | ((uint16_t)blob[2 * i + 1] << 8);
        if (!flash_wait())
            goto fail;
    }
    FLASH_CTLR = 0;

    for (uint16_t i = 0; i < len; i++) {
        if (((const volatile uint8_t *)FS_REGION_BASE)[i] != blob[i])
            goto fail;
    }
    FLASH_CTLR = CTLR_LOCK;
    return 0;

fail:
    FLASH_CTLR = CTLR_LOCK;
    return 4;
}
