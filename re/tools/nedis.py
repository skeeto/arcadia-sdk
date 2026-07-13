#!/usr/bin/env python3
"""16-bit NE (New Executable) disassembly helper for SPADES.EXE.

Usage:
  nedis.py <ne> info                      dump NE header + segment table
  nedis.py <ne> strings [min]             dump strings in DATA segs with DS offset
  nedis.py <ne> dis <seg> 0xOFF 0xLEN     disassemble seg:off for len bytes
  nedis.py <ne> ds 0xOFF [n]              show bytes/string at DS(autodata) offset
  nedis.py <ne> reloc <seg>               dump per-segment relocation records
  nedis.py <ne> imm <0xVAL>               search all code segs for LE16 immediate
"""
import sys, struct, re
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

def parse(path):
    b = open(path, 'rb').read()
    ne = struct.unpack_from('<I', b, 0x3c)[0]
    hdr = {}
    (hdr['entry_off'], hdr['entry_len'], hdr['crc'], hdr['flags'], hdr['autodata'],
     hdr['heap'], hdr['stack'], hdr['csip'], hdr['sssp'], hdr['ncseg'], hdr['nmod'],
     hdr['nnonres'], hdr['seg_off'], hdr['rsrc_off'], hdr['resnam_off'], hdr['modref_off'],
     hdr['imp_off'], hdr['nonres_off'], hdr['nmove'], hdr['align'], hdr['nresseg'],
     hdr['exetype']) = struct.unpack_from('<HHIHHHHIIHHHHHHHHIHHHB', b, ne+4)
    shift = hdr['align'] or 9
    segs = []
    so = ne + hdr['seg_off']
    for i in range(hdr['ncseg']):
        sector, slen, sflags, minalloc = struct.unpack_from('<HHHH', b, so+i*8)
        foff = sector << shift
        if slen == 0 and sector != 0:
            slen = 0x10000
        segs.append({'id': i+1, 'file': foff, 'len': slen, 'flags': sflags,
                     'data': bool(sflags & 1), 'has_reloc': bool(sflags & 0x100),
                     'minalloc': minalloc})
    return b, ne, hdr, segs, shift

def seg_bytes(b, seg):
    return b[seg['file']:seg['file']+seg['len']]

def relocs(b, seg):
    """Parse relocation table appended after seg raw data."""
    if not seg['has_reloc']:
        return []
    off = seg['file'] + seg['len']
    n = struct.unpack_from('<H', b, off)[0]
    off += 2
    out = []
    for i in range(n):
        atype, rtype, srcoff, t1, t2 = struct.unpack_from('<BBHHH', b, off+i*8)
        out.append({'atype': atype, 'rtype': rtype, 'srcoff': srcoff, 't1': t1, 't2': t2})
    return out

def entry_table(b, ne, hdr):
    """Return dict ordinal(1-based) -> (seg, off)."""
    off = ne + hdr['entry_off']
    ordinal = 1; res = {}
    while off < len(b):
        cnt = b[off]; ind = b[off+1]
        if cnt == 0:
            break
        off += 2
        for k in range(cnt):
            if ind == 0xff:
                seg = b[off+3]; o = struct.unpack_from('<H', b, off+4)[0]; off += 6
            else:
                seg = ind; o = struct.unpack_from('<H', b, off+1)[0]; off += 3
            res[ordinal] = (seg, o)
            ordinal += 1
    return res

def modref_names(b, ne, hdr):
    """Return list of imported module names (1-based)."""
    names = []
    mt = ne + hdr['modref_off']
    imp = ne + hdr['imp_off']
    for i in range(hdr['nmod']):
        nameoff = struct.unpack_from('<H', b, mt + i*2)[0]
        p = imp + nameoff
        ln = b[p]
        names.append(b[p+1:p+1+ln].decode(errors='replace'))
    return names

def resolve_chains(b, seg):
    """Return dict src_off -> reloc target string, following NE fixup chains."""
    out = {}
    for r in relocs(b, seg):
        rt = r['rtype'] & 3
        if rt == 0:  # internal ref
            if r['t1'] == 0xff:
                tgt = f"@ord{r['t2']:#x}"  # movable, entry ordinal in t2
            else:
                tgt = f"seg{r['t1']}:{r['t2']:#x}"  # fixed segment:offset
            additive = False
        elif rt == 1:  # imported by ordinal
            tgt = f"mod{r['t1']}.ord{r['t2']}"
            additive = False
        elif rt == 2:  # imported by name
            tgt = f"mod{r['t1']}.name@{r['t2']:#x}"
            additive = False
        else:
            tgt = f"osfixup{r['t2']:#x}"
            additive = False
        # walk chain
        data = seg_bytes(b, seg)
        cur = r['srcoff']
        guard = 0
        while cur != 0xffff and cur + 2 <= len(data) and guard < 10000:
            out[cur] = tgt
            nxt = struct.unpack_from('<H', data, cur)[0]
            if nxt == cur:
                break
            cur = nxt
            guard += 1
    return out

def cmd_info(b, ne, hdr, segs, shift):
    print(f"NE @ 0x{ne:x}  flags=0x{hdr['flags']:x} autodata=seg{hdr['autodata']} "
          f"align_shift={shift} nmod={hdr['nmod']}")
    cs = hdr['csip'] >> 16; ip = hdr['csip'] & 0xffff
    print(f"entry CS:IP = seg{cs}:0x{ip:x}")
    for s in segs:
        print(f"seg{s['id']}: file=0x{s['file']:x} len=0x{s['len']:x} "
              f"flags=0x{s['flags']:x} {'DATA' if s['data'] else 'CODE'} "
              f"reloc={s['has_reloc']}")

def cmd_strings(b, ne, hdr, segs, shift, minlen=4):
    minlen = int(minlen)
    autodata = segs[hdr['autodata']-1]
    base = autodata['file']
    data = seg_bytes(b, autodata)
    for m in re.finditer(rb'[\x20-\x7e]{%d,}' % minlen, data):
        print(f"ds:0x{m.start():04x}  {m.group().decode(errors='replace')}")

def cmd_ds(b, ne, hdr, segs, shift, off, n=64):
    off = int(off, 16); n = int(n)
    autodata = segs[hdr['autodata']-1]
    data = seg_bytes(b, autodata)
    chunk = data[off:off+n]
    s = bytes(c if 0x20 <= c < 0x7f else 0x2e for c in chunk).decode()
    print(f"ds:0x{off:x}: {chunk.hex()}")
    print(f"           {s!r}")

def cmd_dis(b, ne, hdr, segs, shift, seg, off, ln):
    seg = int(seg); off = int(off, 16); ln = int(ln, 16)
    s = segs[seg-1]
    ents = entry_table(b, ne, hdr)
    mods = modref_names(b, ne, hdr)
    chains = resolve_chains(b, s)
    data = seg_bytes(b, s)[off:off+ln]
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = False
    for insn in md.disasm(data, off):
        note = ""
        # find any fixup source within this instruction's bytes
        for k in range(insn.address, insn.address + insn.size):
            if k in chains:
                tgt = chains[k]
                if tgt.startswith('@ord'):
                    ordn = int(tgt[4:], 16)
                    if ordn in ents:
                        sg, o = ents[ordn]
                        tgt = f"seg{sg}:{o:#x} (ord{ordn:#x})"
                elif tgt.startswith('mod'):
                    try:
                        mi = int(tgt[3:tgt.index('.')])
                        rest = tgt[tgt.index('.')+1:]
                        mn = mods[mi-1] if 0 < mi <= len(mods) else f"mod{mi}"
                        if rest.startswith('name@'):
                            noff = int(rest[5:], 16)
                            p = ne + hdr['imp_off'] + noff
                            nm = b[p+1:p+1+b[p]].decode(errors='replace')
                            tgt = f"{mn}.{nm}"
                        else:
                            tgt = f"{mn}.{rest}"
                    except Exception:
                        pass
                note = f"   ; -> {tgt}"
                break
        print(f"  seg{seg}:0x{insn.address:04x}  {insn.bytes.hex():<14} "
              f"{insn.mnemonic:<7} {insn.op_str}{note}")

def cmd_reloc(b, ne, hdr, segs, shift, seg):
    seg = int(seg)
    s = segs[seg-1]
    rs = relocs(b, s)
    print(f"seg{seg}: {len(rs)} relocs")
    for r in rs:
        # rtype low nibble: 0=internal ref,1=imported ordinal,2=imported name,3=osfixup
        kind = {0: 'INTERNAL', 1: 'IMPORTORD', 2: 'IMPORTNAME', 3: 'OSFIXUP'}.get(r['rtype'] & 3, '?')
        print(f"  src=0x{r['srcoff']:04x} atype={r['atype']} rtype=0x{r['rtype']:x}({kind}) "
              f"t1={r['t1']} t2=0x{r['t2']:x}")

def cmd_imm(b, ne, hdr, segs, shift, val):
    val = int(val, 16)
    needle = struct.pack('<H', val & 0xffff)
    for s in segs:
        if s['data']:
            continue
        data = seg_bytes(b, s)
        i = 0
        while True:
            j = data.find(needle, i)
            if j < 0:
                break
            print(f"seg{s['id']}:0x{j:x}")
            i = j+1

def main():
    b, ne, hdr, segs, shift = parse(sys.argv[1])
    cmd = sys.argv[2]; a = sys.argv[3:]
    fn = {'info': cmd_info, 'strings': cmd_strings, 'ds': cmd_ds, 'dis': cmd_dis,
          'reloc': cmd_reloc, 'imm': cmd_imm}[cmd]
    fn(b, ne, hdr, segs, shift, *a)

if __name__ == '__main__':
    main()
