# Toolchain prefix. Works with the Debian/Ubuntu
# gcc-riscv64-unknown-elf package out of the box. For xPack or
# MounRiver toolchains use:  make CROSS=riscv-none-elf-
CROSS    ?= riscv64-unknown-elf-
CC        = $(CROSS)gcc
OBJCOPY   = $(CROSS)objcopy
SIZE      = $(CROSS)size

BUILD     = build
PROGS     = blink heart

CFLAGS    = -march=rv32imac_zicsr -mabi=ilp32 -msmall-data-limit=8 \
            -Os -ffunction-sections -fdata-sections -fno-common \
            -Wall -Wextra -nostdlib
LDFLAGS   = -T link.ld -nostdlib -Wl,--gc-sections -Wl,-Map=$(BUILD)/$*.map

all: $(PROGS)

blink: $(BUILD)/blink.elf $(BUILD)/blink.bin $(BUILD)/blink.hex
heart: $(BUILD)/heart.elf $(BUILD)/heart.bin $(BUILD)/heart.hex

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.elf: src/%.c src/startup.S link.ld | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) src/startup.S $< -o $@
	$(SIZE) $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex $< $@

# Flash via the built-in USB bootloader (chip must be in BOOT mode)
flash: flash-heart
flash-%: %
	wchisp flash $(BUILD)/$*.elf

# Flash via WCH-Link(E) debug adapter using minichlink
flash-link: flash-link-heart
flash-link-%: %
	minichlink -w $(BUILD)/$*.bin 0x08000000 -b

clean:
	rm -rf $(BUILD)

.PHONY: all blink heart flash flash-% flash-link flash-link-% clean
