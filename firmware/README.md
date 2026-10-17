# Hello World — CH32V203G6

Bare-metal blink on **PB5** for the WCH CH32V203G6 (QingKe V4B RISC-V core,
32K flash / 10K RAM). No vendor library needed — GPIO registers are poked
directly.

```
.
├── Makefile          # build + flash targets
├── link.ld           # linker script (adjust FLASH/RAM for other variants)
└── src
    ├── startup.S     # reset entry, .data/.bss init
    └── main.c        # PB5 blink
```

## 1. Install the toolchain

You need a RISC-V bare-metal GCC that produces `rv32imac` code. Pick one:

### Option A — xPack riscv-none-elf-gcc (recommended, all platforms)

1. Download the latest `*-linux-x64.tar.gz` (or `-win32-x64.zip` / `-darwin-*`)
   from the [releases page](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases).
2. Extract it and add its `bin` folder to your `PATH`, e.g. on Linux:

   ```sh
   tar xzf xpack-riscv-none-elf-gcc-*-linux-x64.tar.gz
   export PATH="$PWD/xpack-riscv-none-elf-gcc-*/bin:$PATH"
   ```

3. Verify: `riscv-none-elf-gcc --version`

### Option B — distro package (Debian/Ubuntu)

```sh
sudo apt install gcc-riscv64-unknown-elf make
# the prefix differs, so build with:
make CROSS=riscv64-unknown-elf-
```

### Option C — Windows IDE

[MounRiver Studio](http://www.mounriver.com/) is WCH's Eclipse-based IDE and
bundles the same GCC toolchain plus a project wizard for CH32V20x. You can
still use this Makefile if the bundled `riscv-none-elf-gcc` is on your PATH.

## 2. Install a flashing tool

There are two ways to get the firmware onto the chip over USB:

### Method 1 — built-in USB bootloader + `wchisp` (no extra hardware)

The CH32V203 ships with a ROM bootloader that speaks USB (and UART).
[`wchisp`](https://github.com/ch32-rs/wchisp) is an open-source clone of
WCH's WCHISPTool.

- **Prebuilt binary:** grab the latest for your OS from the
  [nightly release page](https://github.com/ch32-rs/wchisp/releases/tag/nightly).
- **From source** (needs Rust and libusb):

  ```sh
  sudo apt install libusb-1.0-0-dev        # Linux only
  cargo install wchisp --git https://github.com/ch32-rs/wchisp
  ```

- **Linux udev rule** (so you don't need sudo) — create
  `/etc/udev/rules.d/99-wch-isp.rules` containing:

  ```
  SUBSYSTEM=="usb", ATTR{idVendor}=="4348", ATTR{idProduct}=="55e0", MODE="0666"
  ```

  then `sudo udevadm control --reload`.

- **Windows:** use `wchisp` as above, or WCH's official
  [WCHISPTool](https://www.wch.cn/downloads/WCHISPTool_Setup_exe.html).

**Entering bootloader mode:** hold the **BOOT** button (or jumper BOOT0 to
3V3) while plugging in USB, or hold BOOT and tap RST. The USB bootloader
uses the USB device pins (typically **PA11 = D-, PA12 = D+**; on packages
without them, **PB6/PB7**). If your board doesn't wire these pins to a USB
connector, use Method 2.

Then:

```sh
wchisp info          # should print "Chip: CH32V203..."
make flash           # or: wchisp flash build/hello.elf
```

### Method 2 — WCH-Link(E) debugger (works on every board)

Connect the WCH-LinkE's **SWDIO** to the chip's SWDIO pin, plus GND and
(optionally) 3V3, then use one of:

- [`minichlink`](https://github.com/cnlohr/ch32v003fun/tree/master/minichlink)
  (part of ch32v003fun — `git clone`, then `make -C minichlink`):

  ```sh
  make flash-link    # or: minichlink -w build/hello.bin 0x08000000 -b
  ```

- [`wlink`](https://github.com/ch32-rs/wlink): `wlink flash build/hello.elf`
- WCH's OpenOCD fork or MounRiver Studio's download button on Windows.

## 3. Build

```sh
make            # produces build/hello.elf, .bin, .hex
```

Wire an LED (with a series resistor, ~330R–1k) between **PB5** and GND —
it should blink at a few Hz after flashing and resetting.

## Notes

- After reset the chip runs on the internal 8 MHz HSI oscillator; no clock
  setup is required for this example.
- `link.ld` is sized for the G6 (32K/10K). For other CH32V203 variants,
  adjust `LENGTH` in the `MEMORY` block.
- If `wchisp` finds no device, the firmware already in flash may disable
  the BOOT pin — hold BOOT during a fresh power-up, or recover via SWD.
