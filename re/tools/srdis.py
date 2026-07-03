#!/usr/bin/env python3
"""Small reverse-engineering helper for Arcadia toy DLLs / Arcadia.exe.

Usage:
  dis.py <pe> exports                 list exports (name -> RVA)
  dis.py <pe> imports                 list imports grouped by DLL
  dis.py <pe> func <name|0xRVA> [n]    linear disassemble a function (stop at ret/n insns)
  dis.py <pe> range 0xRVA 0xLEN        disassemble a raw RVA range
  dis.py <pe> strings [min]           dump ASCII strings with RVA
  dis.py <pe> xref 0xRVA               find code that references an address (immediate)
  dis.py <pe> vtcalls <name|0xRVA>     disassemble fn, annotate `call [reg+disp]` (vtable/callback calls)

RVAs are relative virtual addresses (not file offsets, not VA-with-imagebase).
"""
import sys, re
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_MEM, CS_OP_IMM

def load(path):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],
        pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT'],
    ])
    return pe

def exports(pe):
    out = {}
    if hasattr(pe, 'DIRECTORY_ENTRY_EXPORT'):
        for s in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            if s.name:
                out[s.name.decode()] = s.address  # already RVA
    return out

def rva_to_off(pe, rva):
    return pe.get_offset_from_rva(rva)

def get_code(pe, rva, size):
    return pe.get_data(rva, size)

def md():
    m = Cs(CS_ARCH_X86, CS_MODE_32)
    m.detail = True
    return m

def resolve(pe, target):
    if target.lower().startswith('0x'):
        return int(target, 16)
    exp = exports(pe)
    if target in exp:
        return exp[target]
    raise SystemExit(f"unknown symbol {target}")

def disasm_func(pe, rva, maxn=400):
    base = pe.OPTIONAL_HEADER.ImageBase
    code = get_code(pe, rva, maxn*8)
    m = md()
    out = []
    n = 0
    for insn in m.disasm(code, base+rva):
        out.append(insn)
        n += 1
        if insn.mnemonic == 'ret' or n >= maxn:
            break
    return out, base

def fmt(insn, base):
    return f"  {insn.address-base:#08x}  {insn.mnemonic:<7} {insn.op_str}"

def cmd_exports(pe):
    for n, r in sorted(exports(pe).items(), key=lambda x: x[1]):
        print(f"{r:#08x}  {n}")

def cmd_imports(pe):
    if hasattr(pe, 'DIRECTORY_ENTRY_IMPORT'):
        for e in pe.DIRECTORY_ENTRY_IMPORT:
            names = [imp.name.decode() if imp.name else f"ord{imp.ordinal}" for imp in e.imports]
            print(f"\n-- {e.dll.decode()} ({len(names)}) --")
            print(", ".join(names))

def cmd_func(pe, target, maxn=400):
    rva = resolve(pe, target)
    insns, base = disasm_func(pe, rva, maxn)
    print(f"; {target} @ RVA {rva:#x}  (imagebase {base:#x})")
    for insn in insns:
        print(fmt(insn, base))

def cmd_range(pe, rva_s, len_s):
    rva = int(rva_s,16); ln = int(len_s,16)
    code = get_code(pe, rva, ln); base = pe.OPTIONAL_HEADER.ImageBase
    for insn in md().disasm(code, base+rva):
        print(fmt(insn, base))

def cmd_vtcalls(pe, target, maxn=600):
    rva = resolve(pe, target)
    insns, base = disasm_func(pe, rva, maxn)
    print(f"; {target} @ RVA {rva:#x}")
    for insn in insns:
        mark = ""
        if insn.mnemonic in ('call','jmp') and '[' in insn.op_str and 'rip' not in insn.op_str:
            mark = "   <-- indirect"
        print(fmt(insn, base) + mark)

def cmd_strings(pe, minlen=4):
    minlen = int(minlen)
    for sect in pe.sections:
        data = sect.get_data()
        base = sect.VirtualAddress
        for m in re.finditer(rb'[\x20-\x7e]{%d,}' % minlen, data):
            print(f"{base+m.start():#08x}  {m.group().decode(errors='replace')}")

def cmd_xref(pe, addr_s):
    target = int(addr_s,16)
    base = pe.OPTIONAL_HEADER.ImageBase
    m = md()
    for sect in pe.sections:
        if not (sect.Characteristics & 0x20000000):  # MEM_EXECUTE
            continue
        data = sect.get_data(); srva = sect.VirtualAddress
        for insn in m.disasm(data, base+srva):
            for op in insn.operands:
                if op.type == CS_OP_IMM and op.imm in (target, base+target):
                    print(fmt(insn, base))
                elif op.type == CS_OP_MEM and op.mem.disp in (target, base+target):
                    print(fmt(insn, base))

def main():
    if len(sys.argv) < 3:
        print(__doc__); return
    pe = load(sys.argv[1]); cmd = sys.argv[2]; a = sys.argv[3:]
    if cmd=='exports': cmd_exports(pe)
    elif cmd=='imports': cmd_imports(pe)
    elif cmd=='func': cmd_func(pe, a[0], int(a[1]) if len(a)>1 else 400)
    elif cmd=='range': cmd_range(pe, a[0], a[1])
    elif cmd=='vtcalls': cmd_vtcalls(pe, a[0])
    elif cmd=='strings': cmd_strings(pe, a[0] if a else 4)
    elif cmd=='xref': cmd_xref(pe, a[0])
    else: print("unknown cmd", cmd)

if __name__=='__main__':
    main()
