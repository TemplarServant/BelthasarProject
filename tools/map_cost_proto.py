#!/usr/bin/env python3
r"""C0/A33B, the map load, priced by walking the ROM over the map packet -- the Python twin of lz.c's
ct_map_load_cycles (the decompression is lz.c's exact model; this is everything else: the header parse, C0/7399's
masks, C0/A521's three plane copies and the RLE plane, C0/A66B's fills and MVNs and the $E5:0000 list).

    python tools/map_cost_proto.py <loc hex> [$01BB -- default: record byte 2 = FF, as C0/0BE8 + C0/6DCF leave it]

Costs are the beam traces' (ctlib/costtab.py via tools/pass_cost_proto.py's table); arms no trace ran are priced by
key. Graded against the cartridge: C0/A33B from its first instruction to its RTS, minus the packet's decompression
(C3:0002 = 18 + lz.c's cost), on loc $082 (2,765,586) and $005 (2,123,762).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pass_cost_proto as P  # noqa: E402
from ctlib import lz  # noqa: E402

L, c, br, LR, mvn = P.L, P.c, P.br, P.LR, P.mvn
ROM = P.ROM


def map_image(loc):
    rec = 0x360000 + loc * 14
    mid = ROM[rec + 4] | ROM[rec + 5] << 8
    me = 0x361E00 + mid * 3
    src = ROM[me] | ROM[me + 1] << 8 | ROM[me + 2] << 16
    return lz.decompress(ROM, lz.hirom(src))


def head(m, bb):
    """C0/A33B .. the JSL (excluded: the decompression), then C0/A36A .. C0/A508 without the three JSRs' bodies"""
    k = L(0xA33B, 0xA36A)                                   # includes the JSL instruction itself
    k += L(0xA36A, 0xA38B)
    b1 = m[1]
    if b1 & 0x80:
        k += br(0xA38B, 0) + L(0xA38D, 0xA392) + c(0xA392)
    else:
        k += br(0xA38B, 1) + LR(0xA394, 0xA397, 1, 0, 0x1D00)
    k += L(0xA397, 0xA39B)
    if b1 & 0x40:
        k += br(0xA39B, 1) + LR(0xA3A2, 0xA3A7, 1, 0, 0x1D00)
    else:
        k += br(0xA39B, 0) + L(0xA39D, 0xA3A0) + c(0xA3A0)
    k += L(0xA3A7, 0xA3AB)
    if b1 & 0x20:
        k += br(0xA3AB, 1) + LR(0xA3B1, 0xA3B5, 1, 0, 0x1D00)
    else:
        k += br(0xA3AB, 0) + L(0xA3AD, 0xA3AF) + c(0xA3AF)
    k += L(0xA3B5, 0xA3B9)
    if b1 & 0x10:
        k += br(0xA3B9, 0) + LR(0xA3BB, 0xA3C1, 1, 0, 0x1D00)
    else:
        k += br(0xA3B9, 1)
    k += L(0xA3C1, 0xA42C)
    if bb:
        k += br(0xA42C, 0) + LR(0xA42E, 0xA436, 1, 0, 0x1D00)
    else:
        k += br(0xA42C, 1)
    k += L(0xA436, 0xA44F)
    if bb:
        k += br(0xA44F, 0) + LR(0xA451, 0xA459, 1, 0, 0x1D00)
    else:
        k += br(0xA44F, 1)
    k += L(0xA459, 0xA461)
    if m[2]:
        raise NotImplementedError('A463: map byte 2 != 0')
    k += br(0xA461, 1) + L(0xA4AD, 0xA4B1)
    if m[3]:
        raise NotImplementedError('A4B3: map byte 3 != 0')
    k += br(0xA4B1, 1) + L(0xA4FD, 0xA508)                 # JSR 7399 / JSR A521 / JSR A66B / PLD / RTS
    return k


def masks(m, loc):
    """C0/7399: each of the six plane dimensions -> a mask ($40 / $20 / $30 / other), then the record's bounds
    (bytes 10-13 into $1D1A-$1D1D, or -- byte 10 negative -- the whole plane's, C0/7495)"""
    dims = [((m[0] & 3) + 1) << 4, ((m[0] & 0x0C) + 4) << 2, (m[0] & 0x30) + 0x10, ((m[0] & 0xC0) >> 2) + 0x10,
            ((m[1] & 3) + 1) << 4, ((m[1] & 0x0C) + 4) << 2]
    k = 0
    for i, d in enumerate(dims):
        base = 0x7399 + i * 0x25
        k += L(base, base + 5)                             # LDA $0BCx / CMP #40
        if d == 0x40:
            k += br(base + 5, 1) + LR(base + 0x21, base + 0x25, 1, 0, 0x1D00)
            continue
        k += br(base + 5, 0) + LR(base + 7, base + 9, 1, 0, 0x1D00)
        if d == 0x20:
            k += br(base + 9, 1) + LR(base + 0x15, base + 0x1B, 1, 0, 0x1D00)
            continue
        k += br(base + 9, 0) + LR(base + 0xB, base + 0xD, 1, 0, 0x1D00)
        if d == 0x30:
            k += br(base + 0xD, 1) + LR(base + 0x1B, base + 0x21, 1, 0, 0x1D00)
            continue
        k += br(base + 0xD, 0) + LR(base + 0xF, base + 0x15, 1, 0, 0x1D00)
    k += L(0x7477, 0x747E)
    if ROM[0x360000 + loc * 14 + 10] & 0x80:
        return k + br(0x747E, 1) + LR(0x7495, 0x74A6, 1, 0, 0x1D00)
    return k + br(0x747E, 0) + L(0x7480, 0x7495)


def planes(m):
    """C0/A521: the three copies (the third only with map byte 1 bit 7), then the RLE plane"""
    dims = [(((m[0] & 3) + 1) << 4, ((m[0] & 0x0C) + 4) << 2), ((m[0] & 0x30) + 0x10, ((m[0] & 0xC0) >> 2) + 0x10),
            (((m[1] & 3) + 1) << 4, ((m[1] & 0x0C) + 4) << 2)]
    k = 0
    for i, (hd, body, bcc, rowend, bcc2, nxt) in enumerate(((0xA521, 0xA548, 0xA552, 0xA554, 0xA55F, 0xA561),
                                                          (0xA561, 0xA57D, 0xA587, 0xA589, 0xA594, 0xA596),
                                                          (0xA59D, 0xA5B9, 0xA5C3, 0xA5C5, 0xA5D0, 0xA5D2))):
        if i == 2:
            k += L(0xA596, 0xA59B)
            if not (m[1] & 0x80):
                k += br(0xA59B, 1)
                break
            k += br(0xA59B, 0)
        cols, rows = dims[i]
        k += L(hd, body)
        for r in range(rows):
            k += cols * L(body, bcc) + (cols - 1) * br(bcc, 1) + br(bcc, 0)
            k += L(rowend, bcc2) + br(bcc2, r + 1 < rows)
    # the RLE plane: $0BCB x $0BCD cells, a byte with bit 7 opens a run
    cols, rows = dims[0]
    p = 6 + dims[0][0] * dims[0][1] + dims[1][0] * dims[1][1] + (dims[2][0] * dims[2][1] if m[1] & 0x80 else 0)
    k += L(0xA5D2, 0xA5F0)
    row = col = 0

    def advance():
        """INX / CPX $54 / BCC (A607-A60A), the row end (A60C-A617); True when the plane is done"""
        nonlocal row, col, k
        k += L(0xA607, 0xA60A)
        col += 1
        if col < cols:
            k += br(0xA60A, 1)
            return False
        k += br(0xA60A, 0) + L(0xA60C, 0xA617)
        col = 0
        row += 1
        if row < rows:
            k += br(0xA617, 1)
            return False
        k += br(0xA617, 0) + c(0xA619)
        return True
    while True:
        b = m[p]
        k += L(0xA5F0, 0xA5F3)
        if not (b & 0x80):
            k += br(0xA5F3, 0) + L(0xA5F5, 0xA607)
            p += 3
            if advance():
                return k
            continue
        k += br(0xA5F3, 1) + L(0xA61A, 0xA639)
        ln = m[p + 3]
        p += 4
        if ln:
            k += br(0xA639, 1) + c(0xA640)                 # BNE -> TAY
            y = ln
        else:
            k += br(0xA639, 0) + LR(0xA63B, 0xA640, 1, 0, 0x1D00)   # a zero length is 256: LDY #0100 / BRA past the TAY
            y = 0x100
        while True:
            k += c(0xA641)                                 # DEY
            y -= 1
            if y == 0:
                k += br(0xA642, 1)
                if advance():
                    return k
                break
            k += br(0xA642, 0) + L(0xA644, 0xA647)
            col += 1
            if col < cols:
                k += br(0xA647, 1)
            else:
                k += br(0xA647, 0) + L(0xA649, 0xA654)
                col = 0
                row += 1
                if row >= rows:
                    k += br(0xA654, 0) + P.one(0xA656, 1, 0, 0x1D00)
                    return k
                k += br(0xA654, 1)
            k += L(0xA657, 0xA669) + c(0xA669)


def a66b(loc):
    """C0/A66B: the $7E:7080 fill, 63 MVNs of 64, then the location's $E5:0000 list (7-byte entries)"""
    k = L(0xA66B, 0xA670) + 0x40 * L(0xA670, 0xA678) + 0x3F * br(0xA678, 1) + br(0xA678, 0) + L(0xA67A, 0xA67F)
    k += 63 * (L(0xA67F, 0xA686) + mvn(0xA686, 64) + L(0xA689, 0xA693)) + 62 * br(0xA693, 1) + br(0xA693, 0)
    k += L(0xA695, 0xA6AF)
    rd = lambda a: ROM[0x250000 + a] | (ROM[0x250001 + a] << 8)      # $E5:xxxx
    x, end = rd(2 * loc), rd(2 * loc + 2)
    while True:
        k += L(0xA6AF, 0xA6B1)
        if x >= end:
            return k + br(0xA6B1, 1) + L(0xA70F, 0xA712)
        k += br(0xA6B1, 0) + L(0xA6B3, 0xA6BC)
        n = ROM[0x250000 + x + 2]
        if n & 0x80:
            # a COLUMN run: the same write a row ($100) further down each time (A6F7-A6FF)
            k += br(0xA6BC, 1) + L(0xA6DC, 0xA6E7)
            y = n & 0x7F
            while True:
                # every pass 16-bit: the column step's own REP #20 (A6F8) precedes the BRA back
                k += L(0xA6E7, 0xA6F5, lambda pc, op: 28 if pc == 0xC0A6E7 else None)
                if y == 0:
                    k += br(0xA6F5, 1)
                    break
                k += br(0xA6F5, 0) + LR(0xA6F7, 0xA701, 1, 0, 0x1D00)
                y -= 1
            k += L(0xA701, 0xA70D) + c(0xA70D)
            x += 7
            continue
        k += br(0xA6BC, 0) + L(0xA6BE, 0xA6C7)
        y = n
        first = True
        while True:
            # `AND $1E` is 16-bit on the first pass (after A6BF's REP) and 8-bit after A6CA's SEP
            k += L(0xA6C7, 0xA6D5, lambda pc, op, f=first: (28 if f else 20) if pc == 0xC0A6C7 else None)
            first = False
            if y == 0:
                k += br(0xA6D5, 1)
                break
            k += br(0xA6D5, 0) + L(0xA6D7, 0xA6DA) + c(0xA6DA)
            y -= 1
        k += L(0xA701, 0xA70D) + c(0xA70D)
        x += 7


def build(loc, bb=None):
    """$01BB: C0/0BE8 clears it at every location entry, C0/6DCF's FF arm (record byte 2 = FF) sets it"""
    if bb is None:
        bb = int(ROM[0x360000 + loc * 14 + 2] == 0xFF)
    m = map_image(loc)
    return head(m, bb) + masks(m, loc) + planes(m) + a66b(loc) + 18   # + the C3:0002 vector's JMP


if __name__ == '__main__':
    loc = int(sys.argv[1], 16)
    bb = int(sys.argv[2], 16) if len(sys.argv) > 2 else None
    print('map build: loc %03X %d' % (loc, build(loc, bb)))
