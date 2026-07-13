#!/usr/bin/env python3
"""Emulate SPADES.EXE's activation-code generator (seg2:0x84e2) with Unicorn.

Loads code segments 2 & 3, applies NE relocations (movable/fixed internal refs),
sets up a far-call stack frame, runs the generator on a 6-char serial, and stops
right before the sprintf("%04X", ...) to read the 16-bit checksum in AX.

Usage: ne_emu.py <ne> SERIAL6 [SERIAL6 ...]
"""
import sys, struct
from unicorn import *
from unicorn.x86_const import *

import nedis  # reuse the parser from the same dir

PARA = {2: 0x1000, 3: 0x3000, 'stack': 0x5000, 'buf': 0x6000, 'out': 0x6200}
GEN_OFF = 0x84e2
STOP_OFF = 0x8648  # after: xor ax,cx ; mov [bp-4],ax  (ax = checksum)

def apply_relocs(mem, b, segs, seg, ents):
    """Patch relocations into `mem` (dict linear->bytes not used; we edit bytearray)."""
    s = segs[seg-1]
    base_para = PARA[seg]
    data = bytearray(nedis.seg_bytes(b, s))
    for r in nedis.relocs(b, s):
        rt = r['rtype'] & 3
        # resolve target selector:offset
        if rt == 0 and r['t1'] == 0xff:            # internal, movable (entry ordinal)
            sg, off = ents[r['t2']]
            sel = PARA.get(sg, 0)
            val_off, val_seg = off, sel
        elif rt == 0:                               # internal, fixed segment
            sg = r['t1']; off = r['t2']
            sel = PARA.get(sg, 0)
            val_off, val_seg = off, sel
        else:                                       # imported / osfixup: dummy
            val_off, val_seg = 0, 0
        # walk chain (linked via the word stored at each source)
        cur = r['srcoff']; guard = 0
        while cur != 0xffff and cur + 1 < len(data) and guard < 20000:
            nxt = struct.unpack_from('<H', data, cur)[0]
            atype = r['atype']
            if atype == 3:      # far pointer: offset then segment
                struct.pack_into('<H', data, cur, val_off)
                if cur + 4 <= len(data):
                    struct.pack_into('<H', data, cur + 2, val_seg)
            elif atype == 2:    # segment/selector only
                struct.pack_into('<H', data, cur, val_seg)
            elif atype == 5:    # offset only
                struct.pack_into('<H', data, cur, val_off)
            else:
                struct.pack_into('<H', data, cur, val_off)
            if nxt == cur:
                break
            cur = nxt; guard += 1
    return bytes(data)

def run(b, segs, ents, serial6):
    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0x0, 0x200000)
    for seg in (2, 3):
        code = apply_relocs(None, b, segs, seg, ents)
        uc.mem_write(PARA[seg] << 4, code)
    # input serial buffer (10+ bytes; generator reads [0..5])
    sbytes = serial6.encode('latin1')[:6].ljust(6, b'\0')
    uc.mem_write(PARA['buf'] << 4, sbytes + b'\0')
    # stack frame for a far cdecl call
    ss = PARA['stack']
    sp = 0xFF00
    frame = struct.pack('<HHHHHH',
                        0xDEAD,            # ret IP
                        0xBEEF,            # ret CS (we stop before returning)
                        0x0000, PARA['out'],   # arg1: local_buf (dest) off,seg
                        0x0000, PARA['buf'])   # arg2: code (src) off,seg
    uc.mem_write((ss << 4) + sp, frame)
    uc.reg_write(UC_X86_REG_SS, ss)
    uc.reg_write(UC_X86_REG_SP, sp)
    uc.reg_write(UC_X86_REG_DS, ss)
    uc.reg_write(UC_X86_REG_ES, 0)
    uc.reg_write(UC_X86_REG_CS, PARA[2])
    uc.reg_write(UC_X86_REG_IP, GEN_OFF)
    stop_lin = (PARA[2] << 4) + STOP_OFF
    try:
        uc.emu_start((PARA[2] << 4) + GEN_OFF, stop_lin, count=200000)
    except UcError as e:
        print(f"  [uc error {e} at IP={uc.reg_read(UC_X86_REG_IP):#x}]")
        return None
    if uc.reg_read(UC_X86_REG_IP) != STOP_OFF:
        return None
    return uc.reg_read(UC_X86_REG_AX) & 0xFFFF

def main():
    b, ne, hdr, segs, shift = nedis.parse(sys.argv[1])
    ents = nedis.entry_table(b, ne, hdr)
    for serial in sys.argv[2:]:
        ck = run(b, segs, ents, serial)
        if ck is None:
            print(f"{serial!r}: <no result>")
        else:
            print(f"{serial:<6} -> {serial}{ck:04X}   (checksum {ck:#06x})")

if __name__ == '__main__':
    main()
