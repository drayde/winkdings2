# CH32V203G6 examples

Bare-metal firmware for the WCH CH32V203G6 (QingKe V4B RISC-V core,
32K flash / 10K RAM). No vendor library needed — GPIO registers are
poked directly.

- **blink** — blinks a LED on PB5
- **heart** — scans an 8x12 heart across 8 LEDs, one column at a
  time at 400 Hz (POV-style)
- **winkdings2** — the real thing: cycles patterns from flash across
  the 8 LEDs at 400 Hz, USB-reprogrammable over CDC-ACM

```
.
├── Makefile            # build + flash targets
├── link.ld             # linker script for blink/heart
├── link_winkdings2.ld  # same, but reserves top 4K flash for patterns
└── src
    ├── startup.S       # reset entry, .data/.bss init
    ├── blink.c         # PB5 blink
    ├── heart.c         # 8-LED heart, column-scanned
    ├── winkdings2.c    # state machine: display / standby / programming
    ├── usb_cdc.c       # polled bare-metal USBD CDC-ACM
    └── flash_store.c   # pattern blob: validate, erase, program
```

## 1. Install the toolchain

You need a RISC-V bare-metal GCC that produces `rv32imac` code. Pick one:

### Option A — distro package (Debian/Ubuntu)

```sh
sudo apt install gcc-riscv64-unknown-elf make
# this is the Makefile default — just run `make`
```

### Option B — xPack riscv-none-elf-gcc (all platforms)

1. Download the latest `*-linux-x64.tar.gz` (or `-win32-x64.zip` / `-darwin-*`)
   from the [releases page](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases).
2. Extract it and add its `bin` folder to your `PATH`, e.g. on Linux:

   ```sh
   tar xzf xpack-riscv-none-elf-gcc-*-linux-x64.tar.gz
   export PATH="$PWD/xpack-riscv-none-elf-gcc-*/bin:$PATH"
   ```

3. Verify: `riscv-none-elf-gcc --version`
4. Build with: `make CROSS=riscv-none-elf-`

### Option C — Windows IDE

[MounRiver Studio](http://www.mounriver.com/) is WCH's Eclipse-based IDE and
bundles the same GCC toolchain plus a project wizard for CH32V20x. You can
still use this Makefile (`make CROSS=riscv-none-elf-`) if the bundled
`riscv-none-elf-gcc` is on your PATH.

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
make flash-heart     # or: wchisp flash build/heart.elf
```

### Method 2 — WCH-Link(E) debugger (works on every board)

Connect the WCH-LinkE's **SWDIO** to the chip's SWDIO pin, plus GND and
(optionally) 3V3, then use one of:

- [`minichlink`](https://github.com/cnlohr/ch32v003fun/tree/master/minichlink)
  (part of ch32v003fun — `git clone`, then `make -C minichlink`):

  ```sh
  make flash-link-heart    # or: minichlink -w build/heart.bin 0x08000000 -b
  ```

- [`wlink`](https://github.com/ch32-rs/wlink): `wlink flash build/heart.elf`
- WCH's OpenOCD fork or MounRiver Studio's download button on Windows.

## 3. Build

```sh
make              # builds all targets: build/{blink,heart,winkdings2}.*
make blink        # or just one firmware
make heart
make winkdings2
```

Flash with `make flash-blink` / `make flash-heart` /
`make flash-winkdings2` (plain `make flash` builds and flashes
`winkdings2`).

### Wiring

- **blink**: LED (with a series resistor, ~330R–1k) between **PB5** and
  GND — blinks at a few Hz.
- **heart / winkdings2**: 8 LEDs, each with a series resistor to GND.
  In LED1..LED8 order (top to bottom row of the pattern):
  **PB1, PB0, PA7, PA6, PA5, PA4, PA3, PB3**. The firmware releases PB3
  from JTAG via the AFIO `SWJ_CFG` bits.
- **winkdings2** additionally uses the button on **PA0** (external
  pulldown; PA0 is the WKUP pin so it can wake from standby).

## 4. winkdings2 operation

- **Cold power-on**: Larson sweep for ~2 s, then straight to standby
  (debug aid: proves the MCU runs). A standby wake (button press)
  skips the sweep and shows the first pattern.
- **Short press**: next pattern; wraps to the first after the last.
- **Idle 30 s**: enters standby (CR2032-friendly, ~µA).
- **Hold 2 s**: programming mode — Larson scanner on the LEDs and a
  CDC-ACM device appears (`/dev/ttyACM*`). Exit with a short press or
  the `EXIT` command.
- A button held at the wake edge only shows pattern 1 — long-press is
  only armed once the device is awake.

### Pattern blob format (stored at `0x08007000`, max 4096 bytes)

Little-endian:

```
u16  magic    'W','K' (0x4B57)
u8   version  = 1
u8   count    number of patterns (1..255)
...  records, packed back-to-back:
        u8  ncols (1..255)
        u8  cols[ncols]   one byte per column, bit0 = LED1 = top row
u16  crc16    CRC-16/CCITT (poly 0x1021, init 0xFFFF) over all
              preceding bytes; blob padded with 0xFF to even length
```

On boot the firmware validates the blob and, if invalid (e.g. first
flash), writes the built-in heart pattern.

### USB protocol (CDC-ACM, binary request/response)

Host sends `[cmd][args]`, device replies `[status][payload]`.
Status: `0x00` OK, `0x01` bad length, `0x02` bad CRC, `0x04` error.

```
0x00 INFO   -> 'W','K','2', fwver u8, region_size u16, count u8
0x01 READ   -> status, u16 len, blob[len]
0x02 WRITE  <- u16 len, blob[len]   -> status (blob validated first)
0x03 EXIT   -> status, device leaves programming mode
```

The VID/PID pair `0x0448:0x570B` in `usb_cdc.c` is a placeholder —
change it before shipping anything.

## Notes

- After reset the chip runs on the internal 8 MHz HSI oscillator; no clock
  setup is required for this example.
- `link.ld` is sized for the G6 (32K/10K). For other CH32V203 variants,
  adjust `LENGTH` in the `MEMORY` block.
- If `wchisp` finds no device, the firmware already in flash may disable
  the BOOT pin — hold BOOT during a fresh power-up, or recover via SWD.
