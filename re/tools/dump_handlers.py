#!/usr/bin/env python3
"""Dump every host-dispatch selector handler from Arcadia.exe, annotated.

Reads the 68-entry jump table at RVA 0x18bf7 (selectors 9..76), bounds each
handler by the next handler's start address (in address order), and prints an
annotated disassembly per selector.
"""
import struct, sys
import pefile
sys.path.insert(0, __import__('os').path.dirname(__file__))
from annot import Ann

JT_RVA = 0x18bf7
N = 0x44
DEFAULT = 0x17be6

def main():
    a = Ann("Arcadia.exe")
    base = a.base
    jt = a.pe.get_data(JT_RVA, N*4)
    handlers = {}  # sel -> rva
    for i in range(N):
        tgt = struct.unpack_from('<I', jt, i*4)[0]
        handlers[9+i] = tgt - base
    # boundaries: sort unique handler rvas
    all_rvas = sorted(set(handlers.values()) | {JT_RVA})
    def end_of(rva):
        for r in all_rvas:
            if r > rva:
                return r
        return rva+0x80
    for sel in range(9, 9+N):
        rva = handlers[sel]
        if rva == DEFAULT:
            continue
        ln = min(end_of(rva)-rva, 0x200)
        print(f"\n===== selector 0x{sel:02x} ({sel})  handler RVA {rva:#08x}  len {ln:#x} =====")
        a.disr(rva, ln)

if __name__=='__main__':
    main()
