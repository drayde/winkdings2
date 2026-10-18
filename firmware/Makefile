# Toolchain prefix. Works with the Debian/Ubuntu
# gcc-riscv64-unknown-elf package out of the box. For xPack or
# MounRiver toolchains use:  make CROSS=riscv-none-elf-
CROSS    ?= riscv64-unknown-elf-
CC        = $(CROSS)gcc
OBJCOPY   = $(CROSS)objcopy
SIZE      = $(CROSS)size

BUILD     = build
PROGS     = blink heart winkdings2

# Per-target sources and linker script (default: src/<name>.c, link.ld)
SRCS_winkdings2 = src/winkdings2.c src/usb_cdc.c src/flash_store.c
LD_winkdings2   = link_winkdings2.ld

CFLAGS    = -march=rv32imac_zicsr -mabi=ilp32 -msmall-data-limit=8 \
            -Os -ffunction-sections -fdata-sections -fno-common \
            -fno-builtin -Wall -Wextra -nostdlib
LDFLAGS   = -nostdlib -Wl,--gc-sections -Wl,-Map=$(BUILD)/$*.map

all: $(PROGS)

blink:      $(BUILD)/blink.elf      $(BUILD)/blink.bin      $(BUILD)/blink.hex
heart:      $(BUILD)/heart.elf      $(BUILD)/heart.bin      $(BUILD)/heart.hex
winkdings2: $(BUILD)/winkdings2.elf $(BUILD)/winkdings2.bin $(BUILD)/winkdings2.hex

$(BUILD):
	mkdir -p $(BUILD)

.SECONDEXPANSION:
$(BUILD)/%.elf: $$(or $$(SRT_$$*),src/startup.S) $$(or $$(SRCS_$$*),src/$$*.c) $$(or $$(LD_$$*),link.ld) | $(BUILD)
	$(CC) $(CFLAGS) -T $(or $(LD_$*),link.ld) $(LDFLAGS) $(or $(SRT_$*),src/startup.S) $(or $(SRCS_$*),src/$*.c) -o $@
	$(SIZE) $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex $< $@

# Flash via the built-in USB bootloader (chip must be in BOOT mode)
flash: flash-winkdings2
flash-%: %
	wchisp flash $(BUILD)/$*.elf

# Flash via WCH-Link(E) debug adapter using minichlink
flash-link: flash-link-winkdings2
flash-link-%: %
	minichlink -w $(BUILD)/$*.bin 0x08000000 -b

clean:
	rm -rf $(BUILD)

.PHONY: all blink heart winkdings2 flash flash-% flash-link flash-link-% clean
