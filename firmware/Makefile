# Toolchain prefix. Works with the Debian/Ubuntu
# gcc-riscv64-unknown-elf package out of the box. For xPack or
# MounRiver toolchains use:  make CROSS=riscv-none-elf-
CROSS    ?= riscv64-unknown-elf-
CC        = $(CROSS)gcc
OBJCOPY   = $(CROSS)objcopy
SIZE      = $(CROSS)size

TARGET    = hello
BUILD     = build

CFLAGS    = -march=rv32imac_zicsr -mabi=ilp32 -msmall-data-limit=8 \
            -Os -ffunction-sections -fdata-sections -fno-common \
            -Wall -Wextra -nostdlib
LDFLAGS   = -T link.ld -nostdlib -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map

SRCS      = src/startup.S src/main.c

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/$(TARGET).elf: $(SRCS) link.ld | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -o $@
	$(SIZE) $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex $< $@

# Flash via the built-in USB bootloader (chip must be in BOOT mode)
flash: all
	wchisp flash $(BUILD)/$(TARGET).elf

# Flash via WCH-Link(E) debug adapter using minichlink
flash-link: all
	minichlink -w $(BUILD)/$(TARGET).bin 0x08000000 -b

clean:
	rm -rf $(BUILD)

.PHONY: all flash flash-link clean
