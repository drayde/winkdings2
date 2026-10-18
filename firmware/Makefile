# Toolchain prefix. Works with the Debian/Ubuntu
# gcc-riscv64-unknown-elf package out of the box. For xPack or
# MounRiver toolchains use:  make CROSS=riscv-none-elf-
CROSS    ?= riscv64-unknown-elf-
CC        = $(CROSS)gcc
OBJCOPY   = $(CROSS)objcopy
SIZE      = $(CROSS)size

BUILD     = build
PROGS     = blink heart winkdings2 usbload

# Per-target sources and linker script (default: src/<name>.c, link.ld)
SRCS_winkdings2 = src/winkdings2.c src/usb_cdc.c src/flash_store.c src/prog_proto.c
LD_winkdings2   = link_winkdings2.ld
SRCS_usbtest    = src/usbtest.c src/usb_cdc.c
SRCS_usbload    = src/usbload.c src/usb_cdc.c
DEFS_usbload    = -DUSB_CDC_PID=0x570Au
SRCS_usbprog    = src/usbprog.c src/usb_cdc.c src/flash_store.c src/prog_proto.c

# '<name>-app' variants link at 0x08003000 for flashing via usbload
SRCS_usbtest-app    = src/usbtest.c src/usb_cdc.c
LD_usbtest-app      = link_app.ld
DEFS_usbtest-app    = -DUSB_CDC_PID=0x570Bu
SRCS_winkdings2-app = src/winkdings2.c src/usb_cdc.c src/flash_store.c src/prog_proto.c
LD_winkdings2-app   = link_app.ld
DEFS_winkdings2-app = -DUSB_CDC_PID=0x570Bu
SRCS_usbprog-app    = src/usbprog.c src/usb_cdc.c src/flash_store.c src/prog_proto.c
LD_usbprog-app      = link_app.ld
DEFS_usbprog-app    = -DUSB_CDC_PID=0x570Bu

CFLAGS    = -march=rv32imac_zicsr -mabi=ilp32 -msmall-data-limit=8 \
            -Os -ffunction-sections -fdata-sections -fno-common \
            -fno-builtin -Wall -Wextra -nostdlib
LDFLAGS   = -nostdlib -Wl,--gc-sections -Wl,-Map=$(BUILD)/$*.map

all: $(PROGS)

blink:      $(BUILD)/blink.elf      $(BUILD)/blink.bin      $(BUILD)/blink.hex
heart:      $(BUILD)/heart.elf      $(BUILD)/heart.bin      $(BUILD)/heart.hex
winkdings2: $(BUILD)/winkdings2.elf $(BUILD)/winkdings2.bin $(BUILD)/winkdings2.hex
usbtest:    $(BUILD)/usbtest.elf    $(BUILD)/usbtest.bin    $(BUILD)/usbtest.hex
usbload:    $(BUILD)/usbload.elf    $(BUILD)/usbload.bin    $(BUILD)/usbload.hex
usbtest-app:    $(BUILD)/usbtest-app.elf    $(BUILD)/usbtest-app.bin
winkdings2-app: $(BUILD)/winkdings2-app.elf $(BUILD)/winkdings2-app.bin
usbprog-app:    $(BUILD)/usbprog-app.elf    $(BUILD)/usbprog-app.bin

$(BUILD):
	mkdir -p $(BUILD)

.SECONDEXPANSION:
$(BUILD)/%.elf: $$(or $$(SRT_$$*),src/startup.S) $$(or $$(SRCS_$$*),src/$$*.c) $$(or $$(LD_$$*),link.ld) | $(BUILD)
	$(CC) $(CFLAGS) $(DEFS_$*) -T $(or $(LD_$*),link.ld) $(LDFLAGS) $(or $(SRT_$*),src/startup.S) $(or $(SRCS_$*),src/$*.c) -o $@
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

# Send an app image to the resident usbload loader (no BOOT pin needed)
app-flash-%: $(BUILD)/%-app.bin
	python3 tools/winkflash.py $(BUILD)/$*-app.bin

clean:
	rm -rf $(BUILD)

.PHONY: all blink heart winkdings2 usbload usbtest-app winkdings2-app \
        usbprog-app flash flash-% flash-link flash-link-% app-flash-% clean
