#!/usr/bin/env python3
r"""The block constants of the location load's walk (src/field_cost.c: ct_field_load_cycles, ct_vm_op_cycles and the
pass's class-1 arms), derived from the ROM's paths on the beam traces' costs -- the twin that checks the C file's numbers.

    python tools/load_cost_consts.py [section ...]       sections: ops cmp loaders runner c2 fd 9dc3 map arena class1

It reads build/_loadcost/*.trace (beam traces of real loads: `python tools/load_cost_cmp.py --cycles --keep <legs>`;
the C's numbers were made from the loads of loc 002 / 070 / 082 / 1B7), prices each path with tools/ctlib/pathcost.py
(an executed PC's own cost, else an executed instruction's with the same key) and prints `NAME = value` -- the values
field_cost.c and lz.c carry. The init runner's per-object and per-op costs (8190 + 1124 N, 460 N + 12, 152 an op) are
read off whole traces instead: `runner` prints them per trace.
"""
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib.costtab import CostTab  # noqa: E402
from ctlib.pathcost import PathCost, decided, rb, ROM  # noqa: E402
from ctlib import beam  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRACES = sorted(glob.glob(os.path.join(ROOT, 'build', '_loadcost', '*.trace')))
T = CostTab(TRACES)
P = PathCost(T)
DISP = T.base(0xC05910)           # the runner's `JSR ($5D6E,X)`, inside an op's cost


def tab(op):
    return 0xC00000 | rb(0x5D6E + 2 * op) | rb(0x5D6F + 2 * op) << 8


def op(o, decide=lambda p, n: False, **kw):
    return DISP + P.cost(tab(o), decide, **kw)


def out(name, v):
    print('%-22s = %s' % (name, v))


def ops():
    straight, fall = {}, {}
    for o in range(256):
        try:
            seen, moves = [], []
            c = op(o, lambda p, n: seen.append(p) or False, mvn=lambda pc: moves.append(pc) or 1)
            if not moves:
                (fall if seen else straight)[o] = c
        except Exception:
            pass
    out('VM_OP_STRAIGHT', ' '.join('%02X:%d' % kv for kv in sorted(straight.items())))
    out('VM_OP_FALLTHROUGH', ' '.join('%02X:%d' % kv for kv in sorted(fall.items())))
    # 82 / 83: C0/5C90's two scans, 92 a step; $73's three stores
    def o82(kind, k, a):
        def d(p, n):
            p &= 0xFFFF
            return {0x5C98: kind == 'hit' and n == k, 0x5C9E: n < 15 if kind == 'miss' else True, 0x5CA8: n == k,
                    0x5CAE: True, 0x4629: kind == 'miss', 0x463F: a == 0, 0x4641: a == 1, 0x468A: a == 0,
                    0x468C: a == 1, 0x46CD: True}[p]
        return op(0x82, d)
    out('OP82 hit k / arms', [o82('hit', 0, a) for a in (0, 1, 2)] + ['+%d a step' % (o82('hit', 1, 0) - o82('hit', 0, 0))])
    out('OP82 miss j / arms', [o82('miss', 0, a) for a in (0, 1, 2)] + ['+%d a step' % (o82('miss', 1, 0) - o82('miss', 0, 0))])
    def o83(kind, k, neg=False):
        d = lambda p, n: {0x477C: kind != 'direct', 0x5C98: kind == 'hit' and n == k, 0x5C9E: n < 15 if kind == 'miss' else True,
                          0x5CA8: n == k, 0x5CAE: True, 0x4784: kind == 'hit', 0x4836: True, 0x47E3: True,
                          0x47EE: not neg}[p & 0xFFFF]
        return op(0x83, d)
    out('OP83 hit/miss/direct', [o83('hit', 0), o83('miss', 0), o83('direct', 0), 'E1 bit 7 +%d' % (o83('hit', 0, True) - o83('hit', 0))])
    for o, b in ((0x8B, 0x4B9B), (0x8C, 0x4BFF), (0x8D, 0x4C45), (0x8E, 0x4C81), (0x18, 0x644C), (0x0B, 0x627C),
                 (0x1A, 0x6574), (0xAC, 0x2F85), (0xF0, 0x3CDB)):
        out('OP%02X nt / taken' % o, (op(o, decided({b: False})), op(o, decided({b: True}))))
    out('OP33', op(0x33, mvn=lambda pc: 24))
    out('OP6F n=1..3', [op(0x6F, lambda p, n, c=c: n < c - 1) for c in (1, 2, 3)])
    out('OP4E 7E/7F, a byte', (op(0x4E, decided({0x6859: True}), mvn=lambda pc: 0), op(0x4E, decided({0x6859: False}), mvn=lambda pc: 0), 46))
    # the PC loaders: the entry, then the party slot (C0/426A)
    out('PC entry 57..6C / 6D', (op(0x57, stop=0xC0426A), op(0x6D, stop=0xC0426A)))
    chain = [0x4228, 0x4232, 0x423C, 0x4246, 0x4250, 0x425A, 0x4264]
    out('PC entry 80 v=0..7', [op(0x80, lambda p, n, v=v: chain.index(p & 0xFFFF) != v, stop=0xC0426A) for v in range(8)])
    def slot(s, b3=False, b2=False, r78=False, own=False):
        d = lambda p, n: {0x4270: s != 0, 0x427A: s != 1, 0x4285: s != 2, 0x42EC: b3, 0x439F: b3, 0x4452: b3, 0x42FE: b2,
                          0x43B1: b2, 0x4464: b2, 0x4330: not r78, 0x43E3: not r78, 0x435E: own, 0x4411: own}[p & 0xFFFF]
        return P.cost(0xC0426A, d)
    out('PC none/lead/slot1/slot2', (slot(3), slot(0), slot(1, own=True), slot(2, own=True)))
    out('PC copy, $78, b3, b2', (slot(1) - slot(1, own=True), slot(1, r78=True, own=True) - slot(1, own=True),
                                 slot(0, b3=True) - slot(0), slot(0, b2=True) - slot(0)))
    # D2: the scan's slot, against C2/8004's leaf 11
    head = DISP + P.cost(0xC038E8, lambda p, n: False, stop=0xC28004)
    def leaf0(i):
        return P.cost(0xC28C75, lambda p, n: (n == i) if (p & 0xFFFF) == 0x8CDB else True if i < 3 else n < 2, m=1, x=1)
    tail = lambda found: P.cost(0xC038FA, lambda p, n: not found)
    out('OPD2 over C2/8004', [head + leaf0(i) - T.base(0xC28C5A) + tail(i < 3) for i in range(4)])


CMPS = [rb(0x6467 + 2 * i) | rb(0x6468 + 2 * i) << 8 for i in range(16)]


def rels(k):
    return ('z', 'nz', 'nz') if k % 8 in (6, 7) else ('lt', 'eq', 'gt')


def cmpc(k, rel):
    def d(p, n):
        ins = rb(p)
        return {0xF0: rel in ('eq', 'z'), 0xD0: rel in ('lt', 'gt', 'nz'), 0xB0: rel in ('eq', 'gt'), 0x90: rel == 'lt'}[ins]
    tr = []
    c = P.cost(0xC00000 | CMPS[k], d, m=1, x=1, trace=tr)
    return c, any(q & 0xFFFF in (0x64B3, 0x651F) for q, _ in tr)


def cmp_():
    out('VM_CMP_C', [[cmpc(k, r)[0] for r in rels(k)] for k in range(16)])
    out('VM_CMP_T', [[int(cmpc(k, r)[1]) for r in rels(k)] for k in range(16)])
    for o, (jsr, tail) in ((0x12, (0x6307, 0x630A)), (0x13, (0x633D, 0x6340)), (0x14, (0x6398, 0x639B)),
                           (0x15, (0x63D7, 0x63DA)), (0x16, (0x6433, 0x6436))):
        k = 8 if o in (0x13, 0x15) else 0
        def run(holds):
            def d(p, n):
                if (p & 0xFFFF) == tail:
                    return (not holds) if rb(p) == 0xB0 else holds
                if (p & 0xFFFF) == 0x63FA:
                    return True
                return holds                  # comparator 0 (C0/6487): its BEQ is taken when the values are equal
            return op(o, d, ind=lambda pc: CMPS[k])
        out('VM_CMP_OP %02X hold/fail' % o, (run(True) - cmpc(k, 'eq')[0], run(False) - cmpc(k, 'lt')[0]))


def loaders():
    out('1B53 7F01ED / FF / song', [P.cost(0xC01B53, decided({0x1B57: e == 0, 0x1B7D: s}), m=1, x=0)
                                    for e, s in ((1, False), (0, True), (0, False))])
    out('LOAD_9DC3 (C0/5926 -> 56C8), v = 0..31', [nine(v, 0) for v in range(32)])


BF5 = [rb(0x9BF5 + i) for i in range(0x16)]; C0B = [rb(0x9C0B + i) for i in range(0x16)]; C21 = [rb(0x9C21 + i) for i in range(0x16)]


def nine(v, a6a, d30=0):
    s = (v - 2) & 0xFF
    def walk():
        if v < 2:
            return v == 0
        if s < 0x16:
            t = BF5[s]; a = 0x80
            for _ in range(t & 0x7F):
                a >>= 1
            if t & 0x80: a ^= 0xFF
            a = ((((a + C0B[s]) & 0xFF) - 0x3F) & 0xFF) ^ C21[s]
            return not (a & 0x80)
    def d(p, n):
        p &= 0xFFFF
        tbl = {0x9AE9: v == 0, 0x9AEC: v == 1, 0x9AF3: s < 0x16, 0x9B86: s == 0x16, 0x9BE3: d30 == 0, 0x9BE5: bool(d30 & 0x80),
               0x9DFA: a6a, 0x9BA4: False, 0x9BB2: False, 0x9BCA: False, 0x9BC3: True, 0x9BD8: True, 0x9BAB: False,
               0x9BB9: False, 0x9BD1: True}
        if p in tbl: return tbl[p]
        if p == 0x9AFD: return bool(BF5[s] & 0x80)
        if p in (0x9B04, 0x9B25): return n < (BF5[s] & 0x7F) - 1
        if p in (0x9B1A, 0x9B3D): return walk()
        for q, val in ((0x9B8A, 0x17), (0x9B8E, 0x18), (0x9B92, 0x19), (0x9B96, 0x1A), (0x9B9A, 0x1B), (0x9B9E, 0x1C)):
            if p == q: return s == val
        raise KeyError(hex(p))
    return P.cost(0xC05926, d, m=1, x=0)


def runner():
    for tr in TRACES:
        rows = beam.load(tr)
        try:
            a = next(k for k, r in enumerate(rows) if r[0] == 'C0:5709')
            b = next(k for k in range(a, len(rows)) if rows[k][0] == 'C0:58ED')
        except StopIteration:
            continue
        own = sum(beam.nonrefresh(rows[k], rows[k + 1]) for k in range(a, b)
                  if not (beam.nonrefresh(rows[k], rows[k + 1]) > 200 and rows[k][1] not in ('54', '44')))
        print('%-40s C0/5709 -> 58ED without its DMAs: %d  (8190 + 1124 N)' % (os.path.basename(tr), own))


def c2():
    L = lambda a, b, m=1, x=1: P.cost(a, lambda p, n: False, m=m, x=x, stop=b)
    out('C2 slot empty / full', (P.cost(0xC2835E, decided({0x8363: True}), m=1, x=0, stop=0xC28379),
                                 P.cost(0xC2835E, decided({0x8363: False, 0xF5FE: False}), m=1, x=0, stop=0xC28379,
                                        mvn=lambda pc: 6)))
    out('C2 FF:F958 pair skip/tech hit/miss', (P.cost(0xFFF976, decided({0xF980: True}), m=1, x=1, d=0x0400, stop=0xFFF9B0),
                                               P.cost(0xFFF98C, decided({0xF9A5: False, 0xF9AA: False}), m=1, x=1, d=0x0400, stop=0xFFF9AC),
                                               P.cost(0xFFF98C, decided({0xF9A5: True}), m=1, x=1, d=0x0400, stop=0xFFF9B0)))


def fd():
    import itertools  # noqa: F401
    def de98(ents):
        st = {'e': 0}
        def d(p, n):
            p &= 0xFFFF
            b = ents[st['e']]
            if p == 0xDED9: return b != 0x80
            if p == 0xDF00: return b != 2
            if p == 0xDF67: return b != 4
            if p in (0xDF36, 0xDF5C, 0xDF92, 0xDFB8): return n % 4 < 3
            if p in (0xDF97, 0xDFBD): return n % 2 == 0
            if p in (0xDFF1, 0xE009): return n % 8 < 7
            if p == 0xE019:
                st['e'] += 1
                return st['e'] == 12
            raise KeyError(hex(p))
        return P.cost(0xFDFFFA, d, m=1, x=0, d=0x0100, db=0x00)
    base = de98([0x80] * 12)
    out('DE98 empty / +2 / +4 / +other', (base, de98([2] + [0x80] * 11) - base, de98([4] + [0x80] * 11) - base,
                                         de98([1] + [0x80] * 11) - base))
    def e292(ents):
        st = {'e': 0}
        def d(p, n):
            p &= 0xFFFF
            h = ents[st['e']] & 0xF0
            if p == 0xE2D0: return h == 0
            if p == 0xE2D4: return h == 0x10
            if p == 0xE2D8: return h == 0x80
            if p == 0xE34E: return h != 0x80
            if p == 0xE37F:
                st['e'] += 1
                return st['e'] == 6
            raise KeyError(hex(p))
        return P.cost(0xFDFFF4, d, m=1, x=0, d=0x0100, db=0x00)
    b2 = e292([0] * 6)
    out('E292 empty / +10 / +80 / +other', (b2, e292([0x10] + [0] * 5) - b2, e292([0x80] + [0] * 5) - b2, e292([0x20] + [0] * 5) - b2))


def mapc():
    def cost(b2, b3):
        def d(p, n):
            p &= 0xFFFF
            if p == 0xA461: return b2 == 0
            if p == 0xA4B1: return b3 == 0
            for base, b in ((0xA469, b2), (0xA4B9, b3)):
                off = p - base
                if off == 0: return (b & 7) == 0
                if off == 0x6: return n == (b & 7) - 1
                if off == 0x11: return not (b & 8)
                if off == 0x1E: return (b & 0x70) == 0
                if off == 0x28: return n == ((b >> 4) & 7) - 1
                if off == 0x33: return not (b & 0x80)
            raise KeyError(hex(p))
        return P.cost(0xC0A45D, d, m=1, x=0, d=0x1D00, stop=0xC0A4FD)
    z = cost(0, 0)
    out('A463 byte / lo / hi', (cost(0x08, 0) - z, cost(0x01, 0) - cost(0x08, 0), cost(0x10, 0) - cost(0x08, 0)))


def arena():
    def grant(entry, ar):
        st = {'x': 0}
        def d(p, n):
            p &= 0xFFFF
            if entry == 0xE952:
                return not (ar[n] & 0x80) if p == 0xE957 else n < 3
            if p == 0xE97F: return not (ar[st['x']] & 0x80)
            if p == 0xE984: return not (ar[st['x'] + 1] & 0x80)
            st['x'] += 1
            return st['x'] < 3
        return P.cost(0xC00000 | entry, d, m=1, x=1)
    F, U = 0x80, 0
    out('E952 slot 0 / step / none', (grant(0xE952, [F] * 4), grant(0xE952, [U, F, F, F]) - grant(0xE952, [F] * 4),
                                      grant(0xE952, [U] * 4)))
    out('E97A pair 0 / none', (grant(0xE97A, [F] * 4), grant(0xE97A, [U] * 4)))


def class1():
    L = lambda a, b, m=1, x=1: P.cost(0xC00000 | a, lambda p, n: False, m=m, x=x, stop=0xC00000 | b)
    c = lambda a, m=1, x=1: L(a, a + 1 + __import__('disasm65816').oplen(__import__('disasm65816').TABLE[rb(0xC00000 | a)][1], m, x), m, x)
    out('CAD9_GATE1', P.cost(0xC0CAD9, decided({0xCADE: True, 0xCAE5: False, 0xCAE7: False, 0xCAEC: False, 0xCAF3: False,
                                                0xCAF7: False}), m=1, x=1, stop=0xC0CEF5))
    out('CEF5_HEAD (-> D28A / -> D30D)', (P.cost(0xC0CEF5, decided({0xCF10: True, 0xCF19: True}), m=1, x=1, stop=0xC0D28A),
                                          P.cost(0xC0CEF5, decided({0xCF10: True, 0xCF19: False}), m=1, x=1, stop=0xC0D30D)))
    allpos = lambda p, n: (False if (p & 0xFFFF) == 0xD35A else n < 15 if (p & 0xFFFF) in (0xD363, 0xD36E) else True)
    tot = P.cost(0xC0D30D, allpos, m=0, x=0, skip={0xC0E687, 0xC0E534})
    wrap = 15 * 86 + 16 * (48 + 18) + 15 * (126 + 18) + (126 + 12 + 18)
    out('D30D rest (16 [$D3] reads at 48 and 16 at 42 out)', tot - wrap - 16 * 42)
    def bcdc(c6, c5, disp):
        st = {'e': -1}
        def d(p, n):
            p &= 0xFFFF
            if p in (0xBD1C, 0xBD5C): return n % 4 == 3
            if p == 0xBD74: return c6 == 0
            if p == 0xBD7B: return bool(c5 & 0x80)
            ins = rb(0xC00000 | p)
            if ins == 0x10:
                st['e'] += 1; return ((disp[st['e']] + c5) & 0xFF) < 0x80
            if ins == 0xB0: return ((disp[st['e']] + c5) & 0xFF) >= 0xE0
            if ins == 0x90:
                st['e'] += 1
                return ((disp[st['e']] + c5) & 0xFF) < 0xE0 if c6 == 0 else disp[st['e']] + c5 < 0x100
            raise KeyError(hex(p))
        return P.cost(0xC0BCDC, d, m=1, x=0)
    out('BCDC arms A / B / C', (bcdc(1, 0x10, [0] * 8), bcdc(0, 0xA0, [0] * 8), bcdc(0, 0x20, [0] * 8)))
    def b309c1(g):
        def d(p, n):
            p &= 0xFFFF
            t = {0xB708: False, 0xB70C: False, 0xB73A: True, 0xB73E: False, 0xB742: False, 0xEA0A: False, 0xB30C: True,
                 0xB316: False, 0xB31A: False, 0xB3EF: g == 0, 0xB3F3: g == 1}
            if p in t: return t[p]
            if p == 0xEA0F: return n < 2
            return n < 7
        return P.cost(0xC0B309, d, m=1, x=1, skip={0xC0BCDC})
    out('B309 class 1 by group', [b309c1(g) for g in (0, 1, 2)])
    def e12a(neg):
        def d(p, n):
            p &= 0xFFFF
            if p == 0xE180: return False
            if p in (0xE189, 0xE194): return n < 95
            return not (neg and p == 0xE1D8)
        return P.cost(0xC0E12A, d, m=1, x=0, db=0x00, skip={0xC0E687, 0xC0E534})
    w96 = 95 * 86 + 96 * (48 + 18) + 95 * (126 + 18) + (126 + 12 + 18)
    out('E12A rest (96 pieces and 48 reads at 42 out; the DMA apart)', e12a(False) - w96 - 48 * 42)
    out('E12A negative row', e12a(True) - e12a(False))


SECTIONS = {'ops': ops, 'cmp': cmp_, 'loaders': loaders, 'runner': runner, 'c2': c2, 'fd': fd, 'map': mapc,
            'arena': arena, 'class1': class1}

if __name__ == '__main__':
    if not TRACES:
        sys.exit('load_cost_consts: no traces in build/_loadcost (tools/load_cost_cmp.py --cycles --keep <legs>)')
    for s in (sys.argv[1:] or SECTIONS):
        print('---', s)
        SECTIONS[s]()
