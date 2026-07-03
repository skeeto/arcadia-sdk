#!/usr/bin/env python3
"""Annotating disassembler for Arcadia.exe.

Resolves, per instruction:
  - operands that point into a string  -> ; "the string"
  - call targets that are IAT thunks   -> ; DLL:Symbol
  - direct call targets                -> ; sub_XXXX (raw)

Usage:
  annot.py <pe> range 0xRVA 0xLEN
  annot.py <pe> func  0xRVA [maxbytes]      (stop at common ret pattern)
"""
import sys, struct
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_IMM, CS_OP_MEM

class Ann:
    def __init__(self, path):
        self.pe = pefile.PE(path)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.md = Cs(CS_ARCH_X86, CS_MODE_32); self.md.detail=True
        self._strings = {}
        self._iat = {}
        self._build_strings()
        self._build_iat()
    def _build_strings(self):
        import re
        for sect in self.pe.sections:
            nm = sect.Name.rstrip(b'\0')
            if nm not in (b'.rdata', b'.data'): continue
            data = sect.get_data(); srva = sect.VirtualAddress
            for m in re.finditer(rb'[\x20-\x7e]{4,}', data):
                self._strings[self.base+srva+m.start()] = m.group().decode(errors='replace')
    def _build_iat(self):
        if hasattr(self.pe,'DIRECTORY_ENTRY_IMPORT'):
            for e in self.pe.DIRECTORY_ENTRY_IMPORT:
                dll=e.dll.decode()
                for imp in e.imports:
                    nm = imp.name.decode() if imp.name else f"ord{imp.ordinal}"
                    self._iat[imp.address] = f"{dll}:{nm}"
    def note(self, insn):
        notes=[]
        for op in insn.operands:
            v=None
            if op.type==CS_OP_IMM: v=op.imm
            elif op.type==CS_OP_MEM and op.mem.base==0 and op.mem.index==0: v=op.mem.disp
            if v is None: continue
            if v in self._strings:
                s=self._strings[v]
                notes.append(f'"{s[:60]}"')
            elif v in self._iat:
                notes.append(self._iat[v])
        # call/jmp to direct target
        if insn.mnemonic in ('call','jmp'):
            for op in insn.operands:
                if op.type==CS_OP_IMM:
                    t=op.imm
                    if self.base <= t < self.base+0x200000:
                        notes.append(f"sub_{t-self.base:x}")
        return ("  ; "+" | ".join(notes)) if notes else ""
    def disr(self, rva, ln):
        data=self.pe.get_data(rva, ln)
        for insn in self.md.disasm(data, self.base+rva):
            print(f"  {insn.address-self.base:#08x}  {insn.mnemonic:<8} {insn.op_str}{self.note(insn)}")

def main():
    a=Ann(sys.argv[1]); cmd=sys.argv[2]
    if cmd=='range':
        a.disr(int(sys.argv[3],16), int(sys.argv[4],16))
    elif cmd=='func':
        a.disr(int(sys.argv[3],16), int(sys.argv[4],16) if len(sys.argv)>4 else 0x200)

if __name__=='__main__':
    main()
