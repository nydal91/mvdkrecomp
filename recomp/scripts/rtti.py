#!/usr/bin/env python3
"""Resolve the C++ RTTI class name of a vtable (address of vtable start, i.e. offset-to-top slot)
Usage: rtti.py <img.bin> <vtable_hex>..."""
import struct, sys
b = open(sys.argv[1], 'rb').read()
mod = struct.unpack_from('<I', b, 4)[0]; dyn = mod + struct.unpack_from('<i', b, mod+4)[0]
tags = {}; o = dyn
while True:
    t, v = struct.unpack_from('<qQ', b, o); o += 16
    if t == 0: break
    tags.setdefault(t, v)
rel = {}
for off in range(tags[7], tags[7] + tags[8], 24):
    r_off, r_info, add = struct.unpack_from('<QQq', b, off)
    if (r_info & 0xffffffff) == 1027: rel[r_off] = add
def cstr(a): return b[a:b.index(b'\0', a)].decode('ascii', 'replace')
for v in sys.argv[2:]:
    vt = int(v, 16)
    ti = rel.get(vt + 8)
    name = cstr(rel[ti + 8]) if ti and ti + 8 in rel else '?'
    slots = [rel.get(vt + 16 + i*8) for i in range(12)]
    print(f'{vt:#x} typeinfo={ti and hex(ti)} name={name}')
    print('   ', ' '.join(hex(s) if s else '-' for s in slots))
