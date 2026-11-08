#!/usr/bin/env python3
"""Parse MOD0 -> .dynamic -> dynsym/dynstr of a flat NSO image (from nso2bin.py).
Prints 'addr size type bind name' for every symbol. Usage: dynsyms.py <img.bin>"""
import struct, sys
b = open(sys.argv[1], 'rb').read()
mod = struct.unpack_from('<I', b, 4)[0]
assert b[mod:mod+4] == b'MOD0'
dyn = mod + struct.unpack_from('<i', b, mod+4)[0]
tags = {}
o = dyn
while True:
    t, v = struct.unpack_from('<qQ', b, o); o += 16
    if t == 0: break
    tags.setdefault(t, v)
symtab, strtab, syment = tags[6], tags[5], tags.get(11, 24)
# symbol count: from DT_HASH nchain
nsym = struct.unpack_from('<I', b, tags[4] + 4)[0] if 4 in tags else (strtab - symtab) // syment
for i in range(nsym):
    name_off, info, other, shndx, val, size = struct.unpack_from('<IBBHQQ', b, symtab + i*syment)
    if not name_off: continue
    e = b.index(b'\0', strtab + name_off)
    name = b[strtab+name_off:e].decode('ascii', 'replace')
    print(f"{val:08x} {size:6x} {info&15} {info>>4} {'U' if shndx==0 else 'D'} {name}")
