#!/usr/bin/env python3
"""Find little-endian 32-bit references to a VA/RVA in a PE, disassemble around each.

Usage: find_ref.py <pe> 0xVA [ctxbytes]
Scans ALL sections' raw bytes for the 4-byte LE value, reports RVA of each hit,
and if the hit is in an executable section, disassembles a window ending near it.
"""
import sys, struct
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

def main():
    pe = pefile.PE(sys.argv[1])
    base = pe.OPTIONAL_HEADER.ImageBase
    val = int(sys.argv[2], 16)
    va = val if val >= base else base + val
    needle = struct.pack('<I', va)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    for sect in pe.sections:
        data = sect.get_data()
        srva = sect.VirtualAddress
        execu = bool(sect.Characteristics & 0x20000000)
        start = 0
        while True:
            idx = data.find(needle, start)
            if idx < 0: break
            hit_rva = srva + idx
            name = sect.Name.rstrip(b'\0').decode(errors='replace')
            print(f"\n== hit in {name} at RVA {hit_rva:#08x} (VA {base+hit_rva:#010x}) exec={execu} ==")
            if execu:
                # disassemble a window starting up to 24 bytes before, to catch the instruction
                for back in range(1, 16):
                    wstart = idx - back
                    if wstart < 0: break
                    ok = False
                    for insn in md.disasm(data[wstart:idx+8], base+srva+wstart):
                        if insn.address <= base+hit_rva < insn.address+insn.size:
                            print(f"   {insn.address-base:#08x}  {insn.mnemonic:<7} {insn.op_str}")
                            ok = True
                            break
                    if ok: break
            start = idx + 1

if __name__=='__main__':
    main()
