"""The ROM's LZ decompressor (C3/0557), the Python twin of src/lz.c ct_lz_decompress_bounded -- for tools that need a
decompressed packet (a location's property header, its map) without running the port."""


def hirom(addr):
    """a 24-bit CPU address in a HiROM bank -> the file offset"""
    return ((addr >> 16) & 0x3F) << 16 | (addr & 0xFFFF)


def _copy(data, out, bp, small):
    b0, b1 = data[bp], data[bp + 1]
    count = (b1 >> 3 if small else b1 >> 4) + 2
    off = ((b1 << 8) | b0) & (0x07FF if small else 0x0FFF)
    if off > len(out):
        raise ValueError('back-reference before the start')
    for _ in range(count + 1):
        out.append(out[len(out) - off])
    return bp + 2


def decompress(data, start):
    size = data[start] | (data[start + 1] << 8)
    bp = start + 2
    after = bp + size
    small = (data[after] & 0xC0) != 0
    out = bytearray()
    bits = 8
    while True:
        if bp == after:
            cur = data[bp] & 0x3F
            if cur == 0:
                return bytes(out)
            bits = cur
            after = start + (data[bp + 1] | (data[bp + 2] << 8))
            bp += 3
            continue
        cur = data[bp]
        if cur == 0:
            out += data[bp + 1:bp + 9]
            bp += 9
            continue
        bp += 1
        mem = cur
        for i in range(bits):
            if mem & 1:
                bp = _copy(data, out, bp, small)
            else:
                out.append(data[bp])
                bp += 1
            mem >>= 1
        bits = 8
