#!/usr/bin/env python3
"""Build a symbolized ELF from a flat NSO image so llvm-objdump gives readable output.
 - exported dynsyms keep their names
 - PLT stubs are named imp_<symbol> (resolved via DT_JMPREL)
 - every BL target in .text becomes sub_<addr>
Writes <img>.elf and <img>.funcs (sorted 'addr name').
Usage: analyze.py <img.bin> <text_size_hex>"""
import struct, sys
path = sys.argv[1]; text_end = int(sys.argv[2], 16)
b = open(path, 'rb').read()
mod = struct.unpack_from('<I', b, 4)[0]; dyn = mod + struct.unpack_from('<i', b, mod+4)[0]
tags = {}; o = dyn
while True:
    t, v = struct.unpack_from('<qQ', b, o); o += 16
    if t == 0: break
    tags.setdefault(t, v)
symtab, strtab = tags[6], tags[5]
def sym(i):
    no, info, _, shndx, val, size = struct.unpack_from('<IBBHQQ', b, symtab + i*24)
    e = b.index(b'\0', strtab + no)
    return b[strtab+no:e].decode(), val, shndx, info & 15
names = {}
nsym = struct.unpack_from('<I', b, tags[4] + 4)[0]
for i in range(1, nsym):
    n, v, sh, ty = sym(i)
    if sh and ty == 2 and v < text_end: names[v] = n
# GOT slot -> import name
got = {}
for off in range(tags[23], tags[23] + tags[2], 24):
    r_off, r_info, _ = struct.unpack_from('<QQq', b, off)
    got[r_off] = sym(r_info >> 32)[0]
# also GLOB_DAT style relocations in RELA (calls through GOT)
words = struct.unpack_from(f'<{text_end//4}I', b, 0)
for i in range(len(words) - 3):
    w = words[i]
    if (w & 0x9f00001f) == 0x90000010 and (words[i+1] & 0xffc003ff) == 0xf9400211 and words[i+3] == 0xd61f0220:
        pc = i * 4
        imm = ((w >> 29) & 3) | (((w >> 5) & 0x7ffff) << 2)
        if imm & (1 << 20): imm -= 1 << 21
        page = (pc & ~0xfff) + (imm << 12)
        slot = page + ((words[i+1] >> 10) & 0xfff) * 8
        if slot in got: names.setdefault(pc, 'imp_' + got[slot])
for i, w in enumerate(words):
    if (w & 0xfc000000) == 0x94000000:
        imm = w & 0x3ffffff
        if imm & (1 << 25): imm -= 1 << 26
        t = i*4 + imm*4
        if 0 <= t < text_end: names.setdefault(t, f'sub_{t:x}')
# code pointers stored in data (vtables, callbacks): R_AARCH64_RELATIVE addends into .text
for off in range(tags[7], tags[7] + tags[8], 24):
    r_off, r_info, add = struct.unpack_from('<QQq', b, off)
    if (r_info & 0xffffffff) == 1027 and 0 < add < text_end and add % 4 == 0 and r_off >= text_end:
        names.setdefault(add, f'sub_{add:x}')
items = sorted(names.items())
with open(path.rsplit('.', 1)[0] + '.funcs', 'w') as f:
    for a, n in items: f.write(f'{a:08x} {n}\n')
# ---- write minimal ELF: one PT_LOAD, .text section covering whole image, symtab
strt = bytearray(b'\0'); syms = bytearray(24)
for idx, (a, n) in enumerate(items):
    nxt = items[idx+1][0] if idx+1 < len(items) else text_end
    syms += struct.pack('<IBBHQQ', len(strt), 0x12, 0, 1, a, nxt - a)
    strt += n.encode() + b'\0'
shstr = b'\0.text\0.symtab\0.strtab\0.shstrtab\0'
data_off = 0x1000
body = bytearray(b)
off_sym = data_off + len(body); body += syms
off_str = data_off + len(body); body += strt
off_shs = data_off + len(body); body += shstr
while len(body) % 8: body += b'\0'
off_sh = data_off + len(body)
eh = struct.pack('<16sHHIQQQIHHHHHH', b'\x7fELF\x02\x01\x01' + b'\0'*9, 2, 183, 1, 0, 64, off_sh, 0, 64, 56, 1, 64, 5, 4)
ph = struct.pack('<IIQQQQQQ', 1, 5, data_off, 0, 0, len(b), len(b), 0x1000)
def sh(name, ty, flags, addr, off, size, link=0, info=0, align=1, ent=0):
    return struct.pack('<IIQQQQIIQQ', name, ty, flags, addr, off, size, link, info, align, ent)
shs = sh(0,0,0,0,0,0) + sh(1,1,6,0,data_off,len(b),align=4) + \
      sh(7,2,0,0,off_sym,len(syms),3,1,8,24) + sh(15,3,0,0,off_str,len(strt)) + sh(23,3,0,0,off_shs,len(shstr))
out = bytearray(eh) + ph; out += b'\0' * (data_off - len(out)); out += body + shs
open(path.rsplit('.', 1)[0] + '.elf', 'wb').write(out)
print(f'{len(items)} symbols, {sum(1 for _,n in items if n.startswith("imp_"))} imports')
