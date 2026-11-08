#!/usr/bin/env python3
"""Decompress a Switch NSO into a flat memory image (text|rodata|data laid out at
their memory offsets, bss zero-filled). Pure Python, no deps.
Usage: nso2bin.py <in.nso> <out.bin>"""
import struct, sys

def lz4_block(src: bytes, out_size: int) -> bytes:
    dst = bytearray(); i = 0; n = len(src)
    while i < n:
        tok = src[i]; i += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1; lit += b
                if b != 255: break
        dst += src[i:i+lit]; i += lit
        if i >= n: break
        off = src[i] | (src[i+1] << 8); i += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1; ml += b
                if b != 255: break
        ml += 4
        start = len(dst) - off
        if off >= ml:
            dst += dst[start:start+ml]
        else:
            for k in range(ml): dst.append(dst[start+k])
    assert len(dst) == out_size, (len(dst), out_size)
    return bytes(dst)

def main(inp, outp):
    d = open(inp, 'rb').read()
    assert d[:4] == b'NSO0', 'not an NSO'
    flags = struct.unpack_from('<I', d, 0xC)[0]
    segs = [struct.unpack_from('<III', d, o) for o in (0x10, 0x20, 0x30)]
    bss = struct.unpack_from('<I', d, 0x3C)[0]
    fsz = struct.unpack_from('<III', d, 0x60)
    end = segs[2][1] + segs[2][2] + bss
    img = bytearray(end)
    for idx, ((foff, moff, size), csz) in enumerate(zip(segs, fsz)):
        raw = d[foff:foff+csz]
        data = lz4_block(raw, size) if flags & (1 << idx) else raw[:size]
        img[moff:moff+size] = data
        print(f"{'text rodata data'.split()[idx]:7s} mem=0x{moff:08x} size=0x{size:08x}")
    print(f"bss     size=0x{bss:08x}  total=0x{end:08x}")
    open(outp, 'wb').write(img)

if __name__ == '__main__':
    main(*sys.argv[1:3])
