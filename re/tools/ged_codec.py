#!/usr/bin/env python3
"""Reference implementation of Arcadia's GED payload codec (selector 0x0f).

Mirrors Arcadia.exe sub_1798c (encode) and sub_17a36 (decode). The wire form is
a NUL-terminated byte stream with three markers:
    0x82           -> literal 0x00
    0x80 <b+0x20>  -> literal byte b, for b < 0x20 or b in {0x80,0x81,0x82}
    0x81 <N+0x20>  -> repeat the next (decoded) byte N times   (N in 1..128)
    other bytes    -> literal (0x20..0x7f and 0x83..0xff)
It is lossless for arbitrary binary, so ar_send() is binary-safe.

Usage: ged_codec.py selftest        # round-trip fuzz + edge cases
"""
import sys, random

MARK_ESC, MARK_RUN, MARK_NUL = 0x80, 0x81, 0x82

def encode(data: bytes) -> bytes:
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        b = data[i]
        run = 1
        while i + run < n and data[i + run] == b and run < 0x80:
            run += 1
        if run > 3:
            out.append(MARK_RUN)
            out.append((run + 0x20) & 0xff)
            i += run - 1            # the byte itself is emitted below, once
        # emit the byte value
        if b == 0:
            out.append(MARK_NUL)
        elif b >= 0x20 and b not in (MARK_ESC, MARK_RUN, MARK_NUL):
            out.append(b)
        else:
            out.append(MARK_ESC)
            out.append((b + 0x20) & 0xff)
        i += 1
    out.append(0)                    # NUL terminator
    return bytes(out)

def decode(wire: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(wire) and wire[i] != 0:
        count = 1
        a = wire[i]; i += 1
        if a == MARK_RUN:
            count = wire[i] - 0x20; i += 1
            a = wire[i]; i += 1
        if a == MARK_NUL:
            a = 0
        elif a == MARK_ESC:
            a = (wire[i] - 0x20) & 0xff; i += 1
        out.extend(bytes([a]) * count)
    return bytes(out)

def selftest():
    cases = [b"", b"hello", b"\x00", bytes(range(256)),
             b"\x00"*10, b"\x80\x81\x82", b"AAAAAAAA", b"\x01\x1f\x20\x7f\xff",
             b"a"*200, bytes([0]*128)]
    for _ in range(2000):
        cases.append(bytes(random.getrandbits(8) for _ in range(random.randint(0, 80))))
    bad = 0
    for c in cases:
        w = encode(c)
        d = decode(w)
        if d != c:
            bad += 1
            print(f"FAIL len={len(c)} {c[:16]!r}... -> wire {w[:20]!r} -> {d[:16]!r}")
        if w[-1] != 0:
            print(f"FAIL not NUL-terminated: {c[:16]!r}")
            bad += 1
    print(f"{len(cases)} cases, {bad} failures")
    # size note
    worst = max((len(encode(c)) - len(c)) for c in cases if c)
    print(f"max expansion observed: +{worst} bytes; malloc bound is len*3+2")

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "selftest":
        random.seed(1)
        selftest()
    else:
        print(__doc__)
