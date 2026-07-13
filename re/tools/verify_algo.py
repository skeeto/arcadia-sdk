#!/usr/bin/env python3
"""Cross-check the pure-Python reimplementation of the SPADES checksum against
the Unicorn emulation of the real binary generator over random serials."""
import sys, random, string
import nedis, ne_emu

def s16(x):
    x &= 0xffff
    return x - 0x10000 if x & 0x8000 else x

def s32(x):
    x &= 0xffffffff
    return x - 0x100000000 if x & 0x80000000 else x

def mul32(a, b):
    return s32((a & 0xffffffff) * (b & 0xffffffff))

def cdiv(a, b):  # C truncation toward zero
    q = abs(a) // abs(b)
    return -q if (a < 0) ^ (b < 0) else q

def checksum(serial):
    raw = serial.encode('latin1')[:6].ljust(6, b'\0')
    c = [x - 256 if x >= 128 else x for x in raw]  # signed char
    A = c[0] + c[1]
    B = s16((c[1] * c[5]) & 0xffff)
    C = c[2] - c[0]
    D = s16((c[3] * c[4]) & 0xffff)
    E = c[4] + c[2]
    F = s16((c[5] * c[3]) & 0xffff)
    acc = A
    acc = mul32(acc, B)
    acc = acc + C
    acc = mul32(acc, D)
    acc = cdiv(acc, 3)
    acc = mul32(acc, E)
    acc = mul32(acc, F)
    acc &= 0xffffffff
    return ((acc >> 16) & 0xffff) ^ (acc & 0xffff)

def main():
    b, ne, hdr, segs, shift = nedis.parse(sys.argv[1])
    ents = nedis.entry_table(b, ne, hdr)
    alphabet = string.ascii_uppercase + string.digits + "!@# .-_"
    random.seed(1)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 500
    bad = 0
    for i in range(n):
        serial = ''.join(random.choice(alphabet) for _ in range(6))
        emu = ne_emu.run(b, segs, ents, serial)
        mine = checksum(serial)
        if emu != mine:
            bad += 1
            if bad <= 20:
                print(f"MISMATCH {serial!r}: emu={emu:#06x} mine={mine:#06x}")
    print(f"checked {n} serials, mismatches={bad}")

if __name__ == '__main__':
    main()
