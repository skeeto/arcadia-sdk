#!/usr/bin/env python3
"""Generate Golden Soul activation codes for Arcadia.

Format: SOUL + 12 hex chars = 16 total
  - code[0..3] = "SOUL"
  - code[4..11] = name (8 hex chars)  
  - code[12..15] = checksum (4 hex chars)

For progress >= 0xce4 (3300), a second 4-hex-char subCode is needed.
The combined 32-bit value (subCode << 16 | ck) must satisfy:
  combined32 - hash >= 1

Usage:
  uv run re/tools/soul_gen.py
  uv run re/tools/soul_gen.py DEADBEEF
"""

import sys

TABLE_LOW  = (11, 17, 3, 101, 7, 312, 12, 13, 19, 23, 15, 27)
TABLE_MID  = (11, 17, 3, 101, 52, 11, 97, 33, 17, 103, 41, 77)
TABLE_HIGH = (87, 31, 6, 44, 52, 11, 88, 47, 17, 11, 41, 94)


def to_s32(x: int) -> int:
    return x if x < 0x80000000 else x - 0x100000000


def s_rem(d: int, div: int) -> int:
    return d % div if d >= 0 else -((-d) % div)


def activation_hash(code12: bytes, table: tuple[int, ...], *, raw_31bit: bool = False) -> int:
    esi = 0xd1e7
    for i in range(12):
        c = code12[i]
        eax = c & 0x7f
        edx = eax
        eax_shifted = eax << i
        edx = (edx * esi) & 0xFFFFFFFF
        esi = (esi ^ edx) & 0xFFFFFFFF
        esi = (esi + eax_shifted) & 0xFFFFFFFF
        rem = s_rem(to_s32(esi), 100)
        esi = (esi - (table[i] + rem)) & 0xFFFFFFFF

    esi &= 0x7fffffff

    # off-by-1 correction applies to both paths since the inner
    # signed-remainder loop has it baked in
    if raw_31bit:
        return (esi - 1) & 0x7fffffff

    rem = s_rem(to_s32(esi), 0xff2f)
    return (rem - 1) & 0xFFFF


def progress_from_name(name: str) -> int:
    r = 0
    for c in name:
        if c < '0' or c > '9':
            break
        r = r * 10 + (ord(c) - 48)
    return r


def generate(name_hex: str) -> tuple[str, str | None]:
    name = name_hex.upper()
    p = progress_from_name(name)
    if p < 0x708:
        table = TABLE_LOW
    elif p < 0xce4:
        table = TABLE_MID
    else:
        table = TABLE_HIGH

    code12 = b"SOUL" + name.encode()
    raw_31bit = p >= 0xce4
    h = activation_hash(code12, table, raw_31bit=raw_31bit)

    if raw_31bit:
        full = (h + 1) & 0xFFFFFFFF
        ck = full & 0xFFFF
        sub = (full >> 16) & 0xFFFF
        return (f"SOUL{name}{ck:04X}", f"{sub:04X}")
    else:
        ck = (h + 1) & 0xFFFF
        return (f"SOUL{name}{ck:04X}", None)


def main():
    raw = [s.upper().removeprefix('SOUL') for s in sys.argv[1:]]
    names = [(s[:8]) for s in raw]

    for n in names:
        code, sub = generate(n)
        if sub:
            print(f"{code} {sub}")
        else:
            print(f"{code}")


if __name__ == '__main__':
    main()
