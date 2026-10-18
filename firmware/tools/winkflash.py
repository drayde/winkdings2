#!/usr/bin/env python3
"""Flash an app image through the resident usbload CDC loader.

Usage: winkflash.py image.bin [--no-run] [--port /dev/ttyACM0]

Waits for the loader port, sends 'B' + len + crc16, streams the image,
expects 'D', then sends 'J' to run the app (unless --no-run).
Stdlib only — no pyserial dependency.
"""
import glob
import os
import select
import struct
import sys
import termios
import time
import tty


def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def find_port(preferred):
    if preferred:
        return preferred
    ports = sorted(glob.glob("/dev/ttyACM*"))
    return ports[0] if ports else None


def open_raw(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    return fd


def read_reply(fd, timeout=30.0):
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.25)
        if r:
            return os.read(fd, 1)
    return b""


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    port_arg = None
    for i, a in enumerate(sys.argv[1:], 1):
        if a == "--port" and i + 1 < len(sys.argv):
            port_arg = sys.argv[i + 1]
    args = [a for a in args if a != port_arg]
    if not args:
        sys.exit("usage: winkflash.py image.bin [--no-run] [--port P]")
    image = open(args[0], "rb").read()
    if len(image) & 1:
        image += b"\xff"
    if len(image) > 0x4000:
        sys.exit("image too large for app region (16K)")

    print("waiting for loader...", flush=True)
    fd = None
    deadline = time.time() + 60
    while time.time() < deadline:
        port = find_port(port_arg)
        if port:
            try:
                fd = open_raw(port)
                # clear any stale bytes, handshake
                termios.tcflush(fd, termios.TCIOFLUSH)
                os.write(fd, b"?")
                if read_reply(fd, 2.0) == b"L":
                    print(f"loader on {port}", flush=True)
                    break
            except OSError:
                fd = None
        time.sleep(0.2)
    if fd is None:
        sys.exit("no loader found")

    os.write(fd, b"B" + struct.pack("<IH", len(image), crc16(image)))
    if read_reply(fd, 10.0) != b"E":
        sys.exit("loader refused image")

    pos = 0
    while pos < len(image):
        chunk = image[pos:pos + 256]
        os.write(fd, chunk)
        pos += len(chunk)
        print(f"\r  {pos}/{len(image)} bytes", end="", flush=True)
        time.sleep(0.01)
    print()

    r = read_reply(fd, 30.0)
    if r != b"D":
        sys.exit(f"program failed ({r!r})")
    print("flashed ok")

    if "--no-run" not in flags:
        os.write(fd, b"J")
        print("jumping to app")
    os.close(fd)


if __name__ == "__main__":
    main()
