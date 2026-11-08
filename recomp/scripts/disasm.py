#!/usr/bin/env python3
"""Disassemble one function (from its start to the next known symbol) with
adrp+add/ldr targets annotated (strings, symbols, data words).
Usage: disasm.py <img.bin> <addr_hex> [out.asm]"""
import bisect, re, struct, subprocess, sys
img = sys.argv[1]; base = img.rsplit('.', 1)[0]
b = open(img, 'rb').read()
funcs = [(int(a, 16), n) for a, n in (l.split() for l in open(base + '.funcs'))]
addrs = [a for a, _ in funcs]; byaddr = dict(funcs)
start = int(sys.argv[2], 16)
i = bisect.bisect_right(addrs, start)
end = addrs[i] if i < len(addrs) else start + 0x1000
asm = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', f'--start-address={start:#x}',
                      f'--stop-address={end:#x}', base + '.elf'], capture_output=True, text=True).stdout
def describe(a):
    if a in byaddr: return byaddr[a]
    if 0xfaa000 <= a < len(b):
        m = re.match(rb'[\x20-\x7e\t\n]{3,}\0', b[a:a+200])
        if m: return 'str ' + repr(m.group()[:-1].decode())
        if a + 8 <= len(b):
            q = struct.unpack_from('<Q', b, a)[0]
            if q in byaddr: return f'-> {byaddr[q]}'
    return None
# R_AARCH64_RELATIVE / GLOB_DAT relocations: slot -> target (or 'imp')
mod = struct.unpack_from('<I', b, 4)[0]; dyn = mod + struct.unpack_from('<i', b, mod+4)[0]
tags = {}; o = dyn
while True:
    t, v = struct.unpack_from('<qQ', b, o); o += 16
    if t == 0: break
    tags.setdefault(t, v)
rel = {}
def symname(i):
    no = struct.unpack_from('<I', b, tags[6] + i*24)[0]
    return b[tags[5]+no:b.index(b'\0', tags[5]+no)].decode()
for off in range(tags[7], tags[7] + tags[8], 24):
    r_off, r_info, add = struct.unpack_from('<QQq', b, off)
    if (r_info & 0xffffffff) == 1027: rel[r_off] = add
    elif r_info >> 32: rel[r_off] = symname(r_info >> 32)
def describe_slot(a):
    d = describe(a)
    if a in rel:
        t = rel[a]
        if isinstance(t, str): return f'&{t}'
        dd = describe(t)
        return f'-> {t:#x}' + (f' ({dd})' if dd else '')
    return d
pages = {}
out = []
for line in asm.splitlines()[asm.splitlines().index('Disassembly of section .text:')+1:] if 'Disassembly' in asm else []:
    m = re.match(r'\s*([0-9a-f]+):\s+(\w+)\s+(.*)', line)
    note = ''
    if m:
        op, args = m.group(2), m.group(3)
        src = None
        regs = re.findall(r'\b([xw]\d+|sp)\b', args)
        if op == 'adrp':
            pages[regs[0][1:]] = int(re.search(r'0x([0-9a-f]+)', args).group(1), 16)
        elif op in ('add', 'ldr', 'ldrb', 'ldrh', 'ldrsw', 'str', 'ldp', 'strb') and len(regs) >= 2:
            src = regs[1][1:] if op == 'add' else regs[-1][1:]
            imm = re.search(r'#(0x[0-9a-f]+|\d+)', args)
            if src in pages and imm:
                tgt = pages[src] + int(imm.group(1), 0)
                d = describe(tgt) if op == 'add' else describe_slot(tgt)
                note = f'   ; [{tgt:#x}]' + (f' {d}' if d else '')
                if op == 'add': pages[regs[0][1:]] = tgt; src = None
        dst = regs[0][1:] if m and op not in ('str', 'strb', 'stp', 'b', 'bl', 'ret', 'cbz', 'cbnz', 'cmp', 'tbz', 'tbnz') and regs else None
        if dst and op not in ('adrp', 'add') and dst in pages: pages.pop(dst, None)
        if op == 'bl': pages.clear()
    out.append(line + note)
text = '\n'.join(l for l in out if l.strip())
if len(sys.argv) > 3: open(sys.argv[3], 'w').write(text + '\n')
print(text)
