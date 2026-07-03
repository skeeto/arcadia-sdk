#!/usr/bin/env python3
"""Resolve IAT thunk addresses to import names, and provide xref-with-IAT.

Usage:
  iat.py <pe> map                 print IAT VA -> symbol
  iat.py <pe> xref 0xRVA          find code referencing an RVA/VA (with import annotation)
  iat.py <pe> callsxref 0xADDR    show call/mov sites touching 0xADDR (VA form) w/ context
"""
import sys, re
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_MEM, CS_OP_IMM

def load(path):
    pe = pefile.PE(path)
    return pe

def iat_map(pe):
    m = {}
    if hasattr(pe, 'DIRECTORY_ENTRY_IMPORT'):
        for e in pe.DIRECTORY_ENTRY_IMPORT:
            dll = e.dll.decode()
            for imp in e.imports:
                nm = imp.name.decode() if imp.name else f"{dll}#ord{imp.ordinal}"
                if imp.address is not None:
                    m[imp.address] = f"{dll}:{nm}"   # imp.address is the VA of the IAT slot? actually it's the bound address; use imp.address
    return m

def iat_slots(pe):
    """Return dict: VA_of_IAT_slot -> symbol name."""
    m = {}
    base = pe.OPTIONAL_HEADER.ImageBase
    if hasattr(pe, 'DIRECTORY_ENTRY_IMPORT'):
        for e in pe.DIRECTORY_ENTRY_IMPORT:
            dll = e.dll.decode()
            for imp in e.imports:
                nm = imp.name.decode() if imp.name else f"{dll}#ord{imp.ordinal}"
                # imp.address = VA where the imported pointer is expected (thunk slot)
                m[imp.address] = f"{dll}:{nm}"
    return m

def cmd_map(pe):
    for va, nm in sorted(iat_slots(pe).items()):
        print(f"{va:#010x}  {nm}")

def cmd_xref(pe, addr_s):
    target = int(addr_s, 16)
    base = pe.OPTIONAL_HEADER.ImageBase
    va = target if target >= base else base + target
    slots = iat_slots(pe)
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    hits = []
    for sect in pe.sections:
        if not (sect.Characteristics & 0x20000000):
            continue
        data = sect.get_data(); srva = sect.VirtualAddress
        for insn in md.disasm(data, base+srva):
            for op in insn.operands:
                hit = False
                if op.type == CS_OP_IMM and op.imm == va: hit=True
                if op.type == CS_OP_MEM and op.mem.disp == va: hit=True
                if hit:
                    note = f"  ; {slots[va]}" if va in slots else ""
                    print(f"{insn.address:#010x} (rva {insn.address-base:#06x})  {insn.mnemonic:<7} {insn.op_str}{note}")
                    break

def main():
    pe = load(sys.argv[1]); cmd = sys.argv[2]; a = sys.argv[3:]
    if cmd=='map': cmd_map(pe)
    elif cmd=='xref': cmd_xref(pe, a[0])
    else: print("?")

if __name__=='__main__':
    main()
