#!/usr/bin/env python3
"""winkdings2 pattern-protocol tool — talks to prog_proto over CDC-ACM.

usage:
  winkproto.py info
  winkproto.py read  out.bin          dump the stored pattern blob
  winkproto.py write blob.bin         program a blob (magic/recs/crc16)
  winkproto.py write-cols 06,0F,.. [;colspec ...]   build+write blob
  winkproto.py exit                   leave programming mode
  winkproto.py reset                  soft-reset (into usbload)
  winkproto.py selftest               read->write->verify->restore

Options: --port /dev/ttyACMx  --timeout N
Blob:  'WK' u8 ver u8 count (u8 ncols + cols)* u16 crc16, even pad.
"""
import glob
import os
import select
import struct
import sys
import termios
import time
import tty

INFO, READ, WRITE, EXIT, RESET = 0x00, 0x01, 0x02, 0x03, 0x04
ST_OK, ST_ERR_LEN, ST_ERR_CRC, ST_ERR = 0, 1, 2, 4


def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def make_blob(patterns):
    """patterns: list of column-byte lists."""
    b = bytearray(b"WK")
    b += bytes([2, len(patterns)])
    for cols in patterns:
        b.append(len(cols))
        b += bytes(cols)
    if len(b) & 1:
        b.append(0xFF)
    b += struct.pack("<H", crc16(b))
    return bytes(b)


def wait_port(preferred, timeout=30.0):
    end = time.time() + timeout
    while time.time() < end:
        if preferred and os.path.exists(preferred):
            return preferred
        ports = sorted(glob.glob("/dev/ttyACM*"))
        if ports:
            return ports[0]
        time.sleep(0.2)
    sys.exit("no /dev/ttyACM* — is the device in programming mode?")


def read_n(fd, n, timeout=10.0):
    out = b""
    end = time.time() + timeout
    while len(out) < n and time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.25)
        if r:
            out += os.read(fd, n - len(out))
    return out


def cmd(fd, c, payload=b"", quiet=False):
    os.write(fd, bytes([c]) + payload)
    if not quiet:
        time.sleep(0.02)


def main():
    argv = sys.argv[1:]
    port = None
    timeout = 30.0
    if "--port" in argv:
        i = argv.index("--port"); port = argv[i + 1]; del argv[i:i + 2]
    if "--timeout" in argv:
        i = argv.index("--timeout"); timeout = float(argv[i + 1]); del argv[i:i + 2]
    if not argv:
        sys.exit(__doc__)

    p = wait_port(port, timeout)
    fd = os.open(p, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    termios.tcflush(fd, termios.TCIOFLUSH)

    op = argv[0]
    if op == "info":
        cmd(fd, INFO)
        r = read_n(fd, 8)
        if len(r) == 8 and r[0] == ST_OK and r[1:4] == b"WK2":
            print(f"fw v{r[4]}  region {r[5]|r[6]<<8} B  patterns {r[7]}")
        else:
            sys.exit(f"bad INFO reply: {r!r}")
    elif op == "read":
        cmd(fd, READ)
        h = read_n(fd, 3)
        if len(h) != 3 or h[0] != ST_OK:
            sys.exit(f"READ failed: {h!r}")
        n = h[1] | h[2] << 8
        blob = read_n(fd, n, 10.0)
        if len(blob) != n:
            sys.exit(f"short read: {len(blob)}/{n}")
        open(argv[1], "wb").write(blob)
        print(f"read {n} B -> {argv[1]}")
    elif op in ("write", "write-cols"):
        if op == "write":
            blob = open(argv[1], "rb").read()
        else:
            pats = [[int(x, 16) for x in s.split(",")] for s in argv[1].split(";")]
            blob = make_blob(pats)
        if len(blob) & 1:
            blob += b"\xff"
        cmd(fd, WRITE, struct.pack("<H", len(blob)))
        os.write(fd, blob)
        r = read_n(fd, 1, 20.0)
        if r != bytes([ST_OK]):
            sys.exit(f"WRITE failed: status {r!r}")
        print(f"wrote {len(blob)} B")
    elif op == "exit":
        cmd(fd, EXIT)
        print("exit:", read_n(fd, 1).hex())
    elif op == "reset":
        cmd(fd, RESET)
        print("reset:", read_n(fd, 1).hex())
    elif op == "selftest":
        cmd(fd, INFO)
        r = read_n(fd, 8)
        assert len(r) == 8 and r[0] == ST_OK and r[1:4] == b"WK2", f"INFO: {r!r}"
        print(f"INFO ok: fw v{r[4]}, {r[7]} pattern(s)")
        cmd(fd, READ)
        h = read_n(fd, 3)
        assert len(h) == 3 and h[0] == ST_OK, f"READ hdr: {h!r}"
        n = h[1] | h[2] << 8
        orig = read_n(fd, n, 10.0)
        assert len(orig) == n, "READ short"
        print(f"READ ok: {n} B")
        # write a two-pattern test blob, read back, verify, restore
        test = make_blob([[0x55, 0xAA] * 4 + [0xFF], [0x01, 0x02, 0x04]])
        cmd(fd, WRITE, struct.pack("<H", len(test)))
        os.write(fd, test)
        assert read_n(fd, 1, 20.0) == bytes([ST_OK]), "WRITE failed"
        cmd(fd, READ)
        h = read_n(fd, 3)
        n2 = h[1] | h[2] << 8
        back = read_n(fd, n2, 10.0)
        assert back == test, f"verify mismatch:\n{back.hex()}\n{test.hex()}"
        print("WRITE+verify ok")
        cmd(fd, WRITE, struct.pack("<H", len(orig)))
        os.write(fd, orig)
        assert read_n(fd, 1, 20.0) == bytes([ST_OK]), "restore failed"
        print("restored original blob — ALL PASS")
    else:
        sys.exit(__doc__)
    os.close(fd)


if __name__ == "__main__":
    main()
