#!/usr/bin/env python3
"""Enumerate host-dispatch selectors used by a toy DLL.

The engine hands the toy a single dispatch function via MPRegisterCallback,
cached in a global g_host. Toys call it cdecl-style:
    push argN ... push arg1 push SELECTOR ; call [g_host] ; add esp, K
So SELECTOR = the immediate of the push immediately preceding the call,
and the arg count = K/4 (from the stack cleanup).

Usage:
  scan_host.py <dll>            summary histogram of selectors
  scan_host.py <dll> sites      every call site with selector + arg cleanup + preceding pushes
"""
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_IMM

def find_ghost(pe):
    """Locate g_host global from MPRegisterCallback body."""
    base = pe.OPTIONAL_HEADER.ImageBase
    exp = {s.name.decode(): s.address for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name}
    rva = exp['MPRegisterCallback']
    code = pe.get_data(rva, 64)
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail=True
    for insn in md.disasm(code, base+rva):
        if insn.mnemonic=='mov' and insn.op_str.startswith('dword ptr [0x'):
            # mov dword ptr [0xADDR], eax
            if insn.operands[0].type != CS_OP_IMM and insn.operands[0].mem.base==0:
                return insn.operands[0].mem.disp
        if insn.mnemonic=='ret': break
    raise SystemExit("could not find g_host")

def iter_text(pe):
    base = pe.OPTIONAL_HEADER.ImageBase
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail=True
    for sect in pe.sections:
        if not (sect.Characteristics & 0x20000000):
            continue
        data = sect.get_data(); srva = sect.VirtualAddress
        for insn in md.disasm(data, base+srva):
            yield insn

def scan(pe):
    base = pe.OPTIONAL_HEADER.ImageBase
    ghost = find_ghost(pe)
    ghost_va = ghost if ghost >= base else base+ghost
    insns = list(iter_text(pe))
    results = []  # (addr, selector, cleanupbytes, pushes)
    for i, insn in enumerate(insns):
        is_call = (insn.mnemonic in ('call','jmp') and
                   insn.op_str.replace('dword ptr ','').strip() in (f'[{ghost_va:#x}]', f'[0x{ghost_va:x}]'))
        if not is_call:
            continue
        # look back for pushes
        pushes = []
        j = i-1
        while j >= 0 and len(pushes) < 8:
            pj = insns[j]
            if pj.mnemonic == 'push':
                op = pj.operands[0]
                pushes.append(pj.imm if False else (op.imm if op.type==CS_OP_IMM else pj.op_str))
            elif pj.mnemonic in ('call','ret','jmp','je','jne','jl','jg','jle','jge','ja','jb'):
                break
            j -= 1
        selector = pushes[0] if pushes else None
        # cleanup: next add esp,N
        cleanup = None
        k = i+1
        while k < len(insns) and k < i+3:
            if insns[k].mnemonic=='add' and insns[k].op_str.startswith('esp'):
                try: cleanup = int(insns[k].op_str.split(',')[1].strip(),16)
                except: pass
                break
            k += 1
        results.append((insn.address-base, selector, cleanup, pushes))
    return ghost, results

def main():
    pe = pefile.PE(sys.argv[1])
    mode = sys.argv[2] if len(sys.argv)>2 else 'summary'
    ghost, results = scan(pe)
    print(f"; {sys.argv[1]}  g_host @ RVA {ghost:#x}   ({len(results)} call sites)")
    if mode=='sites':
        for addr, sel, cl, pushes in results:
            s = f"0x{sel:x}" if isinstance(sel,int) else str(sel)
            pstr = ", ".join(f"0x{p:x}" if isinstance(p,int) else str(p) for p in pushes)
            print(f"  rva {addr:#08x}  sel={s:<6} args={ (cl//4) if cl else '?'}   pushes[{pstr}]")
    else:
        from collections import defaultdict
        hist = defaultdict(lambda: [0,set()])
        for addr, sel, cl, pushes in results:
            key = sel if isinstance(sel,int) else -1
            hist[key][0]+=1
            if cl is not None: hist[key][1].add(cl//4)
        for sel in sorted(hist):
            cnt, args = hist[sel]
            s = f"0x{sel:02x}" if sel>=0 else "??(non-imm)"
            print(f"  sel {s} ({sel if sel>=0 else '?':>3})  calls={cnt:<4} argcounts={sorted(args)}")

if __name__=='__main__':
    main()
