#!/usr/bin/env python3
"""Chrono Trigger LZ decompressor (Evil Peer / Michael Springer routine).

Faithful Python port of the canonical algorithm (CTViewer's lz_decompress.rs),
matching src/lz.c byte-for-byte. Used by the asset extractors and as a
cross-check reference for the C port.

CLI: python assets/lz.py <rom.sfc> <hex-offset>
     -> prints decompressed size and an FNV-1a/32 hash of the output.
"""
import sys

def _copy_bytes(data, out, byte_pos, work_pos, small):
    count = data[byte_pos + 1]
    count >>= 3 if small else 4
    count += 2
    off = ((data[byte_pos + 1] << 8) | data[byte_pos])
    off &= 0x07FF if small else 0x0FFF
    for i in range(count + 1):
        out[work_pos + i] = out[work_pos - off + i]
    return work_pos + count + 1, byte_pos + 2

def lz_decompress(data, start):
    """Return the decompressed bytes of the block whose size header is at `start`."""
    compressed_size = (data[start + 1] << 8) | data[start]
    byte_pos = start + 2
    byte_after = byte_pos + compressed_size
    work_pos = 0
    bits_count = 8
    small = (data[byte_after] & 0xC0) != 0
    out = bytearray(1 << 18)  # grown as needed below

    def ensure(n):
        nonlocal out
        if n >= len(out):
            out.extend(b'\x00' * (n - len(out) + (1 << 16)))

    while True:
        if byte_pos == byte_after:
            cur = data[byte_pos] & 0x3F
            if cur == 0:
                return bytes(out[:work_pos])
            bits_count = cur
            byte_after = start + ((data[byte_pos + 2] << 8) | data[byte_pos + 1])
            byte_pos += 3
        else:
            cur = data[byte_pos]
            if cur == 0:
                ensure(work_pos + 8)
                for i in range(1, 9):
                    out[work_pos] = data[byte_pos + i]; work_pos += 1
                byte_pos += 9
            else:
                byte_pos += 1
                carry = cur & 0x01
                cur >>= 1
                mem = cur
                ensure(work_pos + 4096)
                if carry:
                    work_pos, byte_pos = _copy_bytes(data, out, byte_pos, work_pos, small)
                else:
                    out[work_pos] = data[byte_pos]; work_pos += 1; byte_pos += 1
                while True:
                    bits_count -= 1
                    if bits_count == 0:
                        bits_count = 8
                        break
                    carry = mem & 0x01
                    mem >>= 1
                    ensure(work_pos + 4096)
                    if carry:
                        work_pos, byte_pos = _copy_bytes(data, out, byte_pos, work_pos, small)
                    else:
                        out[work_pos] = data[byte_pos]; work_pos += 1; byte_pos += 1

def _fnv32(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    rom = open(sys.argv[1], 'rb').read()
    off = int(sys.argv[2], 16)
    data = lz_decompress(rom, off)
    print("LZ @0x%X: decompressed %d bytes, fnv32=%08X" % (off, len(data), _fnv32(data)))
