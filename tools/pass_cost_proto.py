#!/usr/bin/env python3
r"""The field's C0/286C pass, priced by walking the ROM's control flow over a WRAM image -- the Python twin of
src/field_cost.c (the development model; the C one is what the port runs and what the suite grades).

    python tools/pass_cost_proto.py <wram at C0/286C> [--objects]

<wram> is the cartridge's whole WRAM at the pass's first instruction (`ct.exe --hookpc 286C` with
CT_HOOK_DUMPWRAM). Every instruction is charged LakeSnes's cost as the beam traces read it (tools/ctlib/costtab.py:
the pass sub-traces in build/_menuinit/*_pass.txt); a branch is 12 not taken / 18 taken; a data access whose cost
depends on where it lands (ROM / WRAM) is priced here by the bank it reads.
"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '.')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib.costtab import CostTab  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = _romlib.load(os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc'))
TRACES = ([os.path.join(ROOT, 'build', '_menuinit', t + '_pass.txt') for t in ('p3_close', 'shop_close', 'door468')]
          + [os.path.join(ROOT, 'build', '_menuinit', t + '_pre.txt') for t in ('p3_close', 'shop_close')])
_T = None


def T():
    global _T
    if _T is None:
        _T = CostTab([p for p in TRACES if os.path.exists(p)])
    return _T


def L(a, b, over=None):
    """straight-line C0/a .. C0/b (b excluded)"""
    return T().lin(0xC00000 | a, 0xC00000 | b, over)


def long_read(bank, nbytes):
    """`LDA [dp],Y` / `LDA [dp]` with D's low byte 0: opcode + operand (12), the 3-byte pointer (24), the data"""
    return 36 + nbytes * rd_cost(bank, 0x8000)


def mean_cost(pc):
    """a DMA's trigger site: LakeSnes aligns the transfer's start to the master clock's multiple of 8 and its end to
    the CPU clock (snes_syncCycles), so its cost carries the absolute phase -- a few cycles either way, the same
    jitter stage 1 left (src/menu_cost.h). The mean of what the traces measured."""
    d = T().dist[0xC00000 | pc]
    n = sum(d.values())
    return int(round(sum(k * v for k, v in d.items()) / n))


def c(pc):
    return T().base(0xC00000 | pc)


def br(pc, taken):
    return T().tk(0xC00000 | pc) if taken else T().nt(0xC00000 | pc)


def mvn(pc, n):
    """a block move of n bytes: every byte re-runs the whole instruction (LakeSnes cpu.c case 0x54, `pc -= 3`)"""
    return n * T().base(0xC00000 | pc)


def rom8(bank, addr):
    return ROM[((bank & 0x3F) << 16) | addr]


def rd_cost(bank, addr):
    """LakeSnes snes_getAccessTime for a data read (MEMSEL set: FastROM)."""
    if bank >= 0xC0:
        return 6
    if 0x40 <= bank < 0x80:
        return 8
    if addr < 0x2000:
        return 8
    if addr < 0x4000:
        return 6
    if addr < 0x4200:
        return 12
    if addr < 0x6000:
        return 6
    if addr < 0x8000:
        return 8
    return 6 if bank >= 0x80 else 8


class Wram:
    def __init__(self, data):
        self.m = bytearray(data)

    def b(self, a):
        return self.m[a & 0x1FFFF]

    def w(self, a):
        return self.m[a & 0x1FFFF] | (self.m[(a + 1) & 0x1FFFF] << 8)

    def sb(self, a, v):
        self.m[a & 0x1FFFF] = v & 0xFF

    def sw(self, a, v):
        self.sb(a, v)
        self.sb(a + 1, v >> 8)

    def mem(self, bank, addr):
        """a byte as the CPU reads it at bank:addr"""
        if bank in (0x7E, 0x7F):
            return self.m[((bank - 0x7E) << 16) | addr]
        if (bank < 0x40 or 0x80 <= bank < 0xC0) and addr < 0x2000:
            return self.m[addr]
        return rom8(bank, addr)


# ---------------------------------------------------------------------------------------------------------------
# C0/B127 -- the camera-window test; visible objects get their draw state reset and C0/7170 + C0/6F9A.
def b127(W, x):
    cyc = L(0xB127, 0xB12A)
    a = W.b(0x1A81 + x)
    if a == 0:
        return cyc + br(0xB12A, 1) + L(0xB190, 0xB192), False
    cyc += br(0xB12A, 0)
    if a & 0x80:
        return cyc + br(0xB12C, 1) + L(0xB190, 0xB192), False
    cyc += br(0xB12C, 0) + L(0xB12E, 0xB134)
    c3 = (W.b(0x1D0A) - 4) & 0xFF
    if c3 & 0x80:
        cyc += br(0xB134, 0) + c(0xB136)
        c3 = 0
    else:
        cyc += br(0xB134, 1)
    cyc += L(0xB138, 0xB145)
    c4 = (W.b(0x1D0C) + 4) & 0xFF
    c5 = W.b(0x1D0E)
    if c5 & 0x80:
        cyc += br(0xB145, 0) + c(0xB147)
        c5 = 0
    else:
        cyc += br(0xB145, 1)
    cyc += L(0xB149, 0xB15B)
    c6 = (W.b(0x1D10) + 5) & 0xFF
    px = (W.b(0x1801 + x) << 1) & 0xFF
    py = (W.b(0x1881 + x) << 1) & 0xFF
    if px < c3:
        return cyc + br(0xB15B, 1) + L(0xB190, 0xB192), False
    cyc += br(0xB15B, 0) + c(0xB15D)
    if px >= c4:
        return cyc + br(0xB15F, 1) + L(0xB190, 0xB192), False
    cyc += br(0xB15F, 0) + L(0xB161, 0xB167)
    if py < c5:
        return cyc + br(0xB167, 1) + L(0xB190, 0xB192), False
    cyc += br(0xB167, 0) + c(0xB169)
    if py >= c6:
        return cyc + br(0xB16B, 1) + L(0xB190, 0xB192), False
    cyc += br(0xB16B, 0) + L(0xB16D, 0xB190)            # the resets, JSR 7170, JSR 6F9A, SEC, RTS
    for a in (0x0E80, 0x0E81, 0x0F00):
        W.sb(a + x, 0x80)
    W.sb(0x1681 + x, 0)
    W.sb(0x1601 + x, 0)
    W.sb(0x1B00 + x, 0)
    W.sb(0x0F01 + x, 0xFF)
    cyc += pal7170(W, x) + vram6f9a(W, x)
    return cyc, True


# C0/7170 -> ($7181,X): the OBJ palette allocator (the port's is ct_vm.c palAlloc7170).
def pal7170(W, x):
    cyc = L(0x7170, 0x7172)
    if W.b(0x0054) & 0x80:
        return cyc + br(0x7172, 1) + c(0x7180)
    cyc += br(0x7172, 0) + L(0x7174, 0x7179)
    t = W.b(0x1100 + x)
    if t & 0x80:
        return cyc + br(0x7179, 1) + c(0x7180)
    cyc += br(0x7179, 0) + L(0x717B, 0x7180) + c(0x7180)   # ASL/TAX/JSR (x) ... RTS
    if t in (0, 1, 2):
        cyc += (c(0x7199) if t == 0 else L(0x7212, 0x7217) if t == 1 else L(0x7217, 0x721C))
        slot = 5 + t
        own = W.b(0x0B80 + slot)
        cyc += L(0x719B, 0x719E)
        if own & 0x80:                                     # free: claim
            cyc += br(0x719E, 0) + L(0x71A0, 0x71D6) + mvn(0x71D6, 24) + L(0x71D9, 0x71E3) + mvn(0x71E3, 24)
            cyc += L(0x71E6, 0x71EB)
            pal_claim(W, x, slot)
            return cyc
        raise NotImplementedError('7199 share path (slot %d owned by %02X)' % (slot, own))
    if t in (3, 4, 7, 8, 9, 10, 11):
        return cyc + pal_scan(W, x, 0, 5, 0x721C)
    if t in (5, 6):
        return cyc + pal_scan(W, x, 1, 4, 0x72B4)
    raise NotImplementedError('7170 class %d' % t)


def pal_claim(W, x, slot):
    W.sb(0x0B00 + x, 0x80)
    W.sb(0x0B01 + x, 0x80)
    W.sb(0x0B80 + slot, x)
    W.sb(0x0F81 + x, (slot << 1) & 0x0E)


def pal_scan(W, x, lo, end, base):
    """C0/721C (lo 0, end 5) / C0/72B4 (lo 1, end 4): pass 1 shares a slot whose owner has the same $1400 key,
    pass 2 claims the first free one."""
    if base == 0x721C:
        tail = L(0x723D, 0x7265) + mvn(0x7265, 24) + L(0x7268, 0x7272) + mvn(0x7272, 24) + L(0x7275, 0x727A)
        P = dict(ini=0x721C, top=0x721E, bpl=0x7221, inx=0x7223, cpx_bmi=0x7226, ini2=0x7228, top2=0x722A,
                 bpl2=0x722D, claim=0x722F, claim_end=0x723D, tail=0x723D, tail_end=0x727A, nxt=0x727A,
                 nxt_bmi=0x7281, cmp=0x7285, cmp_bne=0x7294, share=0x7296, share_bra=0x72A8, inx2=0x72AA,
                 cpx2_bcs=0x72AD, brl2=0x72AF)
    else:
        tail = L(0x72D5, 0x72FD) + mvn(0x72FD, 24) + L(0x7300, 0x730A) + mvn(0x730A, 24) + L(0x730D, 0x7312)
        P = dict(ini=0x72B4, top=0x72B6, bpl=0x72B9, inx=0x72BB, cpx_bmi=0x72BE, ini2=0x72C0, top2=0x72C2,
                 bpl2=0x72C5, claim=0x72C7, claim_end=0x72D5, tail=0x72D5, tail_end=0x7312, nxt=0x7312,
                 nxt_bmi=0x7319, cmp=0x731D, cmp_bne=0x732C, share=0x732E, share_bra=0x7340, inx2=0x7342,
                 cpx2_bcs=0x7345, brl2=0x7347)
    cyc = c(P['ini'])
    s = lo
    while True:
        own = W.b(0x0B80 + s)
        cyc += L(P['top'], P['bpl'])
        if not (own & 0x80):                               # in use: compare its key
            cyc += br(P['bpl'], 1) + L(P['cmp'], P['cmp_bne'])
            if W.w(0x1400 + own) == W.w(0x1400 + x):
                cyc += br(P['cmp_bne'], 0) + L(P['share'], P['share_bra']) + c(P['share_bra'])
                cyc += tail
                W.sb(0x0B00 + x, own)
                W.sb(0x0B01 + x, 0x80)
                W.sb(0x0B01 + own, x)
                W.sb(0x0B80 + s, x)
                W.sb(0x0F81 + x, (s << 1) & 0x0E)
                return cyc
            cyc += br(P['cmp_bne'], 1) + L(P['nxt'], P['nxt_bmi'])
            s += 1
            if s < end:
                cyc += br(P['nxt_bmi'], 1)
                continue
            cyc += br(P['nxt_bmi'], 0)
            break
        cyc += br(P['bpl'], 0) + L(P['inx'], P['cpx_bmi'])
        s += 1
        if s < end:
            cyc += br(P['cpx_bmi'], 1)
            continue
        cyc += br(P['cpx_bmi'], 0)
        break
    # pass 2: the first free slot
    cyc += c(P['ini2'])
    s = lo
    while True:
        own = W.b(0x0B80 + s)
        cyc += L(P['top2'], P['bpl2'])
        if own & 0x80:
            cyc += br(P['bpl2'], 0) + L(P['claim'], P['claim_end']) + tail
            pal_claim(W, x, s)
            return cyc
        cyc += br(P['bpl2'], 1) + L(P['inx2'], P['cpx2_bcs'])
        s += 1
        if s >= end:
            raise NotImplementedError('palette table full')
        cyc += br(P['cpx2_bcs'], 0) + c(P['brl2'])


# C0/6F9A -> ($6FA7,X): classes 3/4 take a VRAM slot from $0B88 (C0/6FBA), everyone else RTSes.
def vram6f9a(W, x):
    t = W.b(0x1100 + x)
    cyc = L(0x6F9A, 0x6F9F) + br(0x6F9F, 0) + L(0x6FA1, 0x6FA6) + c(0x6FA6)
    if t not in (3, 4):
        return cyc + c(0x6FB9)
    cyc += L(0x6FBA, 0x6FC1)
    if W.b(0x1201 + x) & 3:
        raise NotImplementedError('6FBA with $1201&3 != 0')
    cyc += br(0x6FC1, 1) + c(0x6FC5)
    k = 0x19
    while True:
        cyc += L(0x6FC7, 0x6FCA)
        if W.b(0x0B88 + k) & 0x80:
            cyc += br(0x6FCA, 0) + L(0x6FCC, 0x7003)
            W.sb(0x0B88 + k, x)
            W.sw(0x0D00 + x, ((k & 0xFE) << 4) + ((k & 1) << 3))
            W.sw(0x1700 + x, (k & 0xFF) << 5)
            return cyc
        cyc += br(0x6FCA, 1) + c(0x7003)
        k -= 1
        if k == 0:
            raise NotImplementedError('$0B88 exhausted')
        cyc += br(0x7004, 1)


# C0/AB45 -- the screen position ($0A00/$0A80 from $1800/$1880 and the camera).
def ab45(W, x):
    dd = ((W.b(0x1D0A) << 3) + (W.w(0x1D87) & 7)) & 0xFFFF
    df = ((W.b(0x1D0E) << 3) + (W.w(0x1D89) & 7)) & 0xFFFF
    cyc = L(0xAB45, 0xAB7F)
    sx = (W.w(0x1800 + x) >> 4) - dd
    if sx < 0:
        cyc += br(0xAB7F, 0) + c(0xAB81)
        sx = (sx & 0xFFFF) | 0x100
    else:
        cyc += br(0xAB7F, 1)
    W.sw(0x0A00 + x, sx & 0x1FF)
    cyc += L(0xAB84, 0xAB94)
    sy = (W.w(0x1880 + x) >> 4) - df
    if sy < 0:
        cyc += br(0xAB94, 0) + c(0xAB96)
        sy = (sy & 0xFFFF) | 0x100
    else:
        cyc += br(0xAB94, 1)
    W.sw(0x0A80 + x, sy & 0x1FF)
    return cyc + L(0xAB99, 0xABA2)


# C0/A9CD -- link into the draw list's bucket ($0E00 by screen Y / 2).
def a9cd(W, x):
    cyc = L(0xA9CD, 0xA9D4)
    if not (W.b(0x0E01 + x) & 0x80):
        return cyc + br(0xA9D4, 1) + c(0xAA06)
    cyc += br(0xA9D4, 0) + L(0xA9D6, 0xA9E0)
    y = (W.b(0x0A80 + x) >> 1) & 0xFE
    head = W.b(0x0E00 + y)
    if head & 0x80:                                        # empty bucket
        cyc += br(0xA9E0, 0) + L(0xA9E2, 0xA9F2)
        W.sb(0x0E00 + y, x)
        W.sb(0x0E01 + x, 0)
        W.sb(0x0E80 + x, 0x80)
        W.sb(0x0E81 + x, 0x80)
        return cyc
    cyc += br(0xA9E0, 1) + L(0xA9F2, 0xAA07)
    W.sb(0x0E80 + head, x)
    W.sb(0x0E81 + x, head)
    W.sb(0x0E00 + y, x)
    W.sb(0x0E01 + x, 0)
    return cyc


def LR(a, b, m, x, d, db=0, over=None):
    """a straight-line run no trace needs to have executed (ctlib/costtab.py lin_rom): priced by key"""
    return T().lin_rom(0xC00000 | a, 0xC00000 | b, m, x, d, db, over)


def one(pc, m, x, d, db=0):
    """one instruction at C0/pc, executed or not (by key when not)"""
    import disasm65816 as D
    op = ROM[pc]
    ln = 1 + D.oplen(D.TABLE[op][1], m, x)
    return LR(pc, pc + ln, m, x, d, db)


# C0/7506 + C0/7553 -- the camera from the arrival tile ($0102/$0103) against the map's bounds ($1D1A-$1D1D);
# D = $1D00, M 8-bit. Returns the cost of both (their RTSes included).
def camera(W):
    cyc = L(0x7506, 0x7512)
    lo, hi, t = W.b(0x1D1A), W.b(0x1D1B), W.b(0x0102)
    s52 = (lo + 8) & 0xFF
    if t == s52:
        cyc += br(0x7512, 1) + L(0x7522, 0x752C) + c(0x752C)
        a = (lo << 1) & 0xFF
        W.sb(0x1D0A, a)
        W.sb(0x1D0C, (a + 0x20) & 0xFF)
    elif t > s52:
        cyc += br(0x7512, 0) + br(0x7514, 1) + L(0x752E, 0x753C)
        a = ((t - 8) << 1) & 0xFF
        W.sb(0x1D0A, a)
        if ((hi - 8) & 0xFF) >= t:
            cyc += br(0x753C, 1) + L(0x754B, 0x7552)
            W.sb(0x1D0C, (a + 0x20) & 0xFF)
        else:
            cyc += br(0x753C, 0) + LR(0x753E, 0x7549, 1, 0, 0x1D00) + LR(0x7549, 0x754B, 1, 0, 0x1D00)
            e = ((hi + 1) << 1) & 0xFF
            W.sb(0x1D0C, e)
            W.sb(0x1D0A, (e - 0x20) & 0xFF)
    else:
        cyc += br(0x7512, 0) + br(0x7514, 0) + LR(0x7516, 0x7522, 1, 0, 0x1D00)
        a = (lo << 1) & 0xFF
        W.sb(0x1D0A, a)
        W.sb(0x1D0C, (a + 0x20) & 0xFF)
    cyc += c(0x7552) + L(0x7553, 0x755F)
    lo, hi, t = W.b(0x1D1C), W.b(0x1D1D), W.b(0x0103)
    s52 = (lo + 8) & 0xFF
    if t == s52:
        cyc += br(0x755F, 1) + LR(0x756F, 0x757B, 1, 0, 0x1D00)
        a = (lo << 1) & 0xFF
        W.sb(0x1D0E, a)
        W.sb(0x1D10, (a + 0x1C) & 0xFF)
    elif t > s52:
        cyc += br(0x755F, 0) + br(0x7561, 1) + L(0x757B, 0x7589)
        a = ((t - 8) << 1) & 0xFF
        W.sb(0x1D0E, a)
        if ((hi - 6) & 0xFF) >= t:
            cyc += br(0x7589, 1) + L(0x7598, 0x759F)
            W.sb(0x1D10, (a + 0x1C) & 0xFF)
        else:
            cyc += br(0x7589, 0) + LR(0x758B, 0x7598, 1, 0, 0x1D00)
            e = ((hi + 1) << 1) & 0xFF
            W.sb(0x1D10, e)
            W.sb(0x1D0E, (e - 0x1C) & 0xFF)
    else:
        cyc += br(0x755F, 0) + br(0x7561, 0) + LR(0x7563, 0x756F, 1, 0, 0x1D00)
        a = (lo << 1) & 0xFF
        W.sb(0x1D0E, a)
        W.sb(0x1D10, (a + 0x1C) & 0xFF)
    return cyc + c(0x759F)


# C0/CAD9 -- the drain's visit; class 0 ($1201 & 3 = 0) is C0/CBDC: frame lookup (C0/CB3A), the arena grant
# (C0/E952), the 16 pieces (C0/E534 for a bit-14 word, C0/E687 otherwise), the OAM template.
def cad9(W, x):
    cyc = L(0xCAD9, 0xCADE)
    if W.b(0x1100 + x) & 0x80:
        raise NotImplementedError('CAD9 on a dead object')
    cyc += br(0xCADE, 1) + L(0xCAE2, 0xCAE5)
    a = W.b(0x1A81 + x)
    if a == 0 or a & 0x80 or W.b(0x0F00 + x) == 0:
        raise NotImplementedError('CAD9 gate refused')
    cyc += br(0xCAE5, 0) + br(0xCAE7, 0) + L(0xCAE9, 0xCAEC) + br(0xCAEC, 0) + L(0xCAEE, 0xCAF3)
    cls = W.b(0x1201 + x) & 3
    if cls != 0:
        raise NotImplementedError('CAD9 class %d' % cls)
    cyc += br(0xCAF3, 1) + c(0xCB01)
    # C0/CBDC
    cyc += L(0xCBDC, 0xCBF7)
    cf = W.b(0x1200 + x)
    if W.b(0x1780 + x) == 3:
        raise NotImplementedError('CBF9 ($1780 = 3)')
    cyc += br(0xCBF7, 1) + c(0xCBFE)
    k, frame = cb3a(W, x)
    cyc += k + br(0xCC01, 1)
    cyc += L(0xCC04, 0xCC07)
    if frame == W.b(0x0F01 + x):
        raise NotImplementedError('CC09: frame unchanged')
    cyc += br(0xCC07, 1) + L(0xCC0B, 0xCC10)
    cyc += e952(W, x) + br(0xCC10, 0)
    W.sb(0x0F01 + x, frame)
    fbank = W.b(0x1300 + x)
    d3 = (frame * 0x28 + W.w(0x1380 + x)) & 0xFFFF
    cyc += L(0xCC12, 0xCC53) + c(0xCC53)
    for k in range(16):
        if k:
            cyc += L(0xCC55, 0xCC5D)
        word = W.mem(fbank, (d3 + 2 * k) & 0xFFFF) | (W.mem(fbank, (d3 + 2 * k + 1) & 0xFFFF) << 8)
        cyc += long_read(fbank, 2) + L(0xCC5F, 0xCC62)
        last = k == 15
        if word & 0x4000:
            cyc += br(0xCC62, 1) + c(0xCC6F) + e534(cf) + L(0xCC72, 0xCC76) + br(0xCC76, not last)
        else:
            cyc += br(0xCC62, 0) + c(0xCC64) + e687(cf) + L(0xCC67, 0xCC6B) + br(0xCC6B, not last)
            if last:
                cyc += c(0xCC6D)

    # C0/CC78: the upload queue entry and the OAM template ($7F:4800 + $1700,X)
    def rd8(pc, op):
        return long_read(fbank, 1) if op == 0xB7 else None
    tb = [W.mem(fbank, (d3 + 0x20 + i) & 0xFFFF) for i in range(8)]
    cyc += L(0xCC78, 0xCCCB, rd8)
    t = W.w(0x1700 + x)
    for i, (bpl, zero, nxt, end) in enumerate(((0xCCCB, 0xCCD1, 0xCCD3, 0xCCE5), (0xCCE5, 0xCCEB, 0xCCED, 0xCCFF),
                                               (0xCCFF, 0xCD05, 0xCD07, 0xCD19), (0xCD19, 0xCD1F, 0xCD21, 0xCD2C))):
        if tb[2 * i] & 0x80:
            cyc += br(bpl, 0) + L(bpl + 2, zero)            # LDA #FF / BRA
        else:
            cyc += br(bpl, 1) + c(zero)                      # LDA #00
        cyc += L(nxt, end, rd8)
    for i in range(8):
        W.sb(0x14802 + t + (i // 2) * 8 + (i & 1) * 2, tb[i])
    cyc += L(0xCD2C, 0xCD55)
    for i, (bcc, lo, join, nxt) in enumerate(((0xCD55, 0xCD62, 0xCD6B, 0xCD71), (0xCD71, 0xCD7E, 0xCD87, 0xCD8D),
                                              (0xCD8D, 0xCD9A, 0xCDA3, 0xCDA9), (0xCDA9, 0xCDB6, 0xCDBF, None))):
        if tb[2 * i + 1] < 0xE8:
            cyc += br(bcc, 1) + L(lo, join)
        else:
            cyc += br(bcc, 0) + L(bcc + 2, lo)             # ... ORA $0C01,Y / STA / BRA
        if nxt:
            cyc += L(join, nxt)
    cyc += L(0xCDBF, 0xCDC8)
    W.sb(0x1B00 + x, W.b(0x1B00 + x) + 1)
    q = W.b(0x0079)                                          # C0/CCAC-CCB4: the upload queue's entry
    W.sb(0x09A0 + q, W.b(0x09A0 + q) + 1)
    W.sb(0x09A2 + q, 0)
    W.sb(0x0079, q + 2)
    return cyc


def cb3a(W, x):
    cyc = L(0xCB3A, 0xCB3D)
    m = W.b(0x1600 + x)
    b1480, b1500 = W.w(0x1480 + x), W.w(0x1500 + x)
    if m == 0:
        cyc += br(0xCB3D, 1) + L(0xCB52, 0xCB59) + c(0xCB59)
        d6 = b1500
    else:
        cyc += br(0xCB3D, 0) + L(0xCB3F, 0xCB41)
        if m == 2:
            cyc += br(0xCB41, 1) + L(0xCB6E, 0xCB7A)
            d6 = b1480 * 2 + b1500
        else:
            cyc += br(0xCB41, 0)
            if not (m & 0x80) and m > 2:
                cyc += br(0xCB43, 1) + L(0xCB5B, 0xCB6C) + c(0xCB6C)
                d6 = b1480 * 3 + b1500
            else:
                cyc += br(0xCB43, 0) + L(0xCB45, 0xCB50) + c(0xCB50)
                d6 = b1480 + b1500
    cyc += L(0xCB7A, 0xCB8B)
    if W.b(0x1780 + x) == 2:
        raise NotImplementedError('CB8D ($1780 = 2)')
    cyc += br(0xCB8B, 1) + L(0xCBB1, 0xCBC6)
    d6 = (d6 + W.b(0x1680 + x) * 4 + W.b(0x1681 + x)) & 0xFFFF
    frame = rom8(0xE4, d6)
    if frame == 0xFF:
        raise NotImplementedError('CBC8: the sequence terminator')
    return cyc + br(0xCBC6, 1) + L(0xCBDA, 0xCBDC), frame


def e952(W, x):
    cyc = L(0xE952, 0xE957)
    if not (W.b(0x0BC0) & 0x80):
        raise NotImplementedError('E952: slot 0 taken')
    cyc += br(0xE957, 0) + L(0xE959, 0xE973)
    W.sb(0x0BC0, x)
    W.sw(0x0D80 + x, 0x3800)
    return cyc


def e534(cf):
    def rd(pc, op):
        return long_read(cf, 1) if op == 0xB7 else None
    return L(0xE534, 0xE687, rd)


def e687(cf):
    cyc = L(0xE687, 0xE69D)
    if cf == 0x7F:
        return cyc + br(0xE69D, 0) + c(0xE69F) + L(0xE8C2, 0xE935)
    cyc += br(0xE69D, 1) + L(0xE6A2, 0xE6A5)
    if cf == 0xD2:
        return cyc + br(0xE6A5, 0) + c(0xE6A7) + L(0xE7BC, 0xE83F)
    cyc += br(0xE6A5, 1) + c(0xE6AA)
    if cf == 0xD3:
        return cyc + br(0xE6AB, 0) + c(0xE6AD) + L(0xE83F, 0xE8C2)
    cyc += br(0xE6AB, 1) + c(0xE6B0)
    if cf == 0xD4:
        return cyc + br(0xE6B1, 0) + c(0xE6B3) + L(0xE739, 0xE7BC)
    raise NotImplementedError('E687 bank %02X' % cf)


# C0/B1B2 -- the upload: two DMAs of $0100 bytes per queued object ($0980/$0990 -- C0/CCA3 queues exactly that).
def b1b2(W):
    cyc = L(0xB1B2, 0xB1D1)
    for k in range(0, 16, 2):
        cyc += L(0xB1D1, 0xB1D3)
        if W.b(0x09A0 + k) == 0:
            cyc += br(0xB1D3, 1) + L(0xB201, 0xB204)
            W.sb(0x09A0, 0)
            return cyc
        cyc += br(0xB1D3, 0) + L(0xB1D5, 0xB1E9) + mean_cost(0xB1E9) + L(0xB1EB, 0xB1FD) + mean_cost(0xB1FD)
        cyc += L(0xB1FE, 0xB201)
    raise NotImplementedError('B1B2 queue overflow')


# C0/EA42 -- the arena release (class 0 holds one slot, granted at 0 by C0/E952).
def ea42(W, x):
    cyc = L(0xEA42, 0xEA49)
    if W.b(0x0BC0) != x:
        raise NotImplementedError('EA42: slot 0 not ours')
    cyc += br(0xEA49, 1) + L(0xEA51, 0xEA59) + br(0xEA59, 0) + L(0xEA5B, 0xEA60)
    W.sb(0x0BC0, 0x80)
    if W.b(0x0BC1) == x:
        raise NotImplementedError('EA42: two slots')
    return cyc + br(0xEA60, 0) + c(0xEA62)


# C0/B0E6 -- the pass's per-object sweep (without its BRL C0/B271 tail).
def b0e6(W, detail=None):
    n = W.b(0x12000) * 2
    cyc = L(0xB0E6, 0xB0F1)
    x = 0
    while True:
        o = L(0xB0F1, 0xB0F6)
        t = W.b(0x1100 + x)
        if t & 0x80:
            o += br(0xB0F6, 1)
        else:
            o += br(0xB0F6, 0) + L(0xB0F8, 0xB0FA)
            if t == 7:
                o += br(0xB0FA, 1)
            else:
                o += br(0xB0FA, 0) + c(0xB0FC)
                k, vis = b127(W, x)
                o += k
                if not vis:
                    o += br(0xB0FF, 1)
                else:
                    W.sb(0x0079, 0)                          # C0/B107 `STZ $79`: the queue cursor
                    o += br(0xB0FF, 0) + c(0xB101) + ab45(W, x) + c(0xB104) + a9cd(W, x) + L(0xB107, 0xB109)
                    o += c(0xB109) + cad9(W, x) + br(0xB10C, 0)
                    o += L(0xB10E, 0xB111) + c(0xB111) + b1b2(W) + L(0xB114, 0xB117) + c(0xB117) + ea42(W, x)
        o += L(0xB11A, 0xB120)
        x += 2
        more = x < n
        o += br(0xB120, more)
        if detail is not None:
            detail.append((x // 2 - 1, o))
        cyc += o
        if not more:
            break
    return cyc + L(0xB122, 0xB124) + c(0xB124)


# C0/B8CA -- class 0's OAM emitter (instantiate): X half (4 pieces, fixed), then the Y half by the screen Y's range
# ($C6 = bit 8, $C5 = the low byte): >= $100 arm A (B922, five checks -- the fifth reads $4BE4,X), $80-$FF arm B
# (B9A5: carry, then < E0), < $80 arm C (B9F7: sign, then >= E0); every arm clamps a piece to E0.
def b8ca(W, x):
    cyc = L(0xB8CA, 0xB8EC)
    t = W.w(0x1700 + x)
    sy = W.w(0x0A80 + x) & 0x1FF
    c5, c6 = sy & 0xFF, sy >> 8
    for k in range(4):                                       # X from $1700+$18 down to $1700
        cyc += L(0xB8EC, 0xB908)
        if k == 3:
            cyc += br(0xB908, 1)
        else:
            cyc += br(0xB908, 0) + L(0xB90A, 0xB913) + c(0xB913)
    cyc += L(0xB915, 0xB920)
    rec = 0x14800 + t                                        # $7F:4800 + $1700,X (the staged template)
    disp = [W.b(rec + 4 + 8 * i) for i in range(4)]
    if c6:
        cyc += br(0xB920, 0)
        vals = disp + [W.b(0x14BE4 + t)]
        for i, (st, bcc, bcs, clamp) in enumerate(((0xB922, 0xB92B, 0xB92F, 0xB931), (0xB936, 0xB93F, 0xB943, 0xB945),
                                                   (0xB94A, 0xB953, 0xB957, 0xB959), (0xB95E, 0xB967, 0xB96B, 0xB96D),
                                                   (0xB972, 0xB978, 0xB97C, 0xB97E))):
            cyc += LR(st, bcc, 1, 0, 0x0100, 0x7F)
            s = vals[i] + c5
            if s < 0x100:                                    # BCC -> LDA #E0
                cyc += br(bcc, 1) + LR(clamp, clamp + 2, 1, 0, 0x0100, 0x7F)
            else:
                cyc += br(bcc, 0) + LR(bcc + 2, bcs, 1, 0, 0x0100, 0x7F)
                if (s & 0xFF) >= 0xE0:
                    cyc += br(bcs, 1)
                else:
                    cyc += br(bcs, 0) + LR(clamp, clamp + 2, 1, 0, 0x0100, 0x7F)
            cyc += LR(clamp + 2, clamp + 5, 1, 0, 0x0100, 0x7F)   # STA $4BCx,X
        return cyc + LR(0xB983, 0xB9A1, 1, 0, 0x0100, 0x7F)
    cyc += br(0xB920, 1) + L(0xB9A1, 0xB9A3)
    if c5 & 0x80:                                            # arm B
        cyc += br(0xB9A3, 0)
        for i, (st, bcc, bcc2, clamp, store) in enumerate(((0xB9A5, 0xB9AE, 0xB9B2, 0xB9B4, 0xB9B6),
                                                          (0xB9B9, 0xB9C2, 0xB9C6, 0xB9C8, 0xB9CA),
                                                          (0xB9CD, 0xB9D6, 0xB9DA, 0xB9DC, 0xB9DE),
                                                          (0xB9E1, 0xB9EA, 0xB9EE, 0xB9F0, 0xB9F2))):
            cyc += L(st, bcc)
            s = disp[i] + c5
            if s < 0x100:
                cyc += br(bcc, 1)
            else:
                cyc += br(bcc, 0) + L(bcc + 2, bcc2)
                if (s & 0xFF) < 0xE0:
                    cyc += br(bcc2, 1)
                else:
                    cyc += br(bcc2, 0) + LR(clamp, store, 1, 0, 0x0100, 0x7F)
            cyc += L(store, store + 3)
        cyc += c(0xB9F5)
    else:                                                    # arm C
        cyc += br(0xB9A3, 1)
        for i, (st, bpl, bcs, clamp, store) in enumerate(((0xB9F7, 0xBA00, 0xBA04, 0xBA06, 0xBA08),
                                                         (0xBA0B, 0xBA14, 0xBA18, 0xBA1A, 0xBA1C),
                                                         (0xBA1F, 0xBA28, 0xBA2C, 0xBA2E, 0xBA30),
                                                         (0xBA33, 0xBA3C, 0xBA40, 0xBA42, 0xBA44))):
            cyc += L(st, bpl)
            s = (disp[i] + c5) & 0xFF
            if not (s & 0x80):
                cyc += br(bpl, 1)
            else:
                cyc += br(bpl, 0) + LR(bpl + 2, bcs, 1, 0, 0x0100, 0x7F)
                if s >= 0xE0:
                    cyc += br(bcs, 1)
                else:
                    cyc += br(bcs, 0) + LR(clamp, store, 1, 0, 0x0100, 0x7F)
            cyc += L(store, store + 3)
    return cyc + L(0xBA47, 0xBA65)


# C0/B309 -- per drawn object: C0/B701 (class 0: C0/B8CA + C0/E9E2, $1B00 := 80), then stage 2: the high-table
# byte and 4 OAM entries into the group $0F80,X & 0C picks (0 -> $0181/$01DB, bit 2 -> $0185/$01DD, else $0189/$01DF).
def b309(W, x):
    cyc = c(0xB309) + L(0xB701, 0xB708)
    if W.b(0x1201 + x) & 3:
        raise NotImplementedError('B701 class %d' % (W.b(0x1201 + x) & 3))
    cyc += br(0xB708, 1) + L(0xB716, 0xB719)
    s = W.b(0x1B00 + x)
    if s == 0 or s & 0x80:
        raise NotImplementedError('B701 with $1B00 = %02X' % s)
    cyc += br(0xB719, 1) + br(0xB71D, 0) + c(0xB71F) + b8ca(W, x) + c(0xB722) + e9e2(W, x) + L(0xB725, 0xB72E)
    W.sb(0x1B00 + x, 0x80)
    cyc += br(0xB30C, 1) + L(0xB30F, 0xB316) + br(0xB316, 1) + L(0xB329, 0xB339)
    g = W.b(0x0F80 + x) & 0x0C
    if g == 0:
        cyc += br(0xB339, 1)
        cur = 0x01DB
        cyc += L(0xB341, 0xB363)
    else:
        cyc += br(0xB339, 0) + LR(0xB33B, 0xB33D, 1, 0, 0x2100)
        if g & 4:
            cyc += br(0xB33D, 1) + LR(0xB397, 0xB3BB, 1, 0, 0x2100)
            cur = 0x01DD
        else:
            cyc += br(0xB33D, 0) + LR(0xB33F, 0xB341, 1, 0, 0x2100) + LR(0xB3BB, 0xB3DF, 1, 0, 0x2100)
            cur = 0x01DF
    W.sw(cur, W.w(cur) + 0x10)
    cyc += L(0xB363, 0xB36E)
    for k in range(4):
        cyc += L(0xB36E, 0xB393) + br(0xB393, k < 3)
    return cyc + L(0xB395, 0xB397)


def e9e2(W, x):
    cyc = L(0xE9E2, 0xE9E8)
    for k in range(4):
        if W.b(0x0BC0 + k) == x:
            raise NotImplementedError('E9E2 finds a slot')
        cyc += L(0xE9E8, 0xE9ED) + br(0xE9ED, 0) + L(0xE9EF, 0xE9F2) + br(0xE9F2, k < 3)
    return cyc + L(0xE9F4, 0xE9F7)


# C0/B271 -- the draw pass: 64 buckets of $0E00 high to low, each chain through $0E81,X, then the three
# hide-the-stale-tail loops up to last frame's marks ($017B / $017D / $017F).
def b271(W):
    cyc = L(0xB271, 0xB2A8)
    for c0, cur in ((0x0770, 0x01DB), (0x08A0, 0x01DD), (0x0710, 0x01DF)):
        W.sw(cur, c0)
    for y in range(0x7E, -2, -2):
        cyc += L(0xB2A8, 0xB2AB)
        head = W.b(0x0E00 + y)
        if head & 0x80:
            cyc += br(0xB2AB, 1)
        else:
            cyc += br(0xB2AB, 0) + L(0xB2AD, 0xB2B2) + b309(W, head)
            o = head
            while True:
                cyc += L(0xB2B2, 0xB2B7)
                nx = W.b(0x0E81 + o)
                if nx & 0x80:
                    cyc += br(0xB2B7, 1)
                    break
                cyc += br(0xB2B7, 0) + L(0xB2B9, 0xB2BE) + b309(W, nx) + c(0xB2BE)
                o = nx
        cyc += L(0xB2C0, 0xB2C2) + br(0xB2C2, y > 0)
    cyc += L(0xB2C4, 0xB2C8)
    for cur, mark, end, top, bcs, lp, bcc, tail in ((0x01DB, 0x017B, 0x08A0, 0xB2C8, 0xB2CA, 0xB2CC, 0xB2D6, 0xB2D8),
                                                     (0x01DD, 0x017D, 0x0900, 0xB2DE, 0xB2E0, 0xB2E2, 0xB2EC, 0xB2EE),
                                                     (0x01DF, 0x017F, 0x0770, 0xB2F4, 0xB2F6, 0xB2F8, 0xB302, 0xB304)):
        p, prev = W.w(cur), W.w(mark)
        while True:
            cyc += L(top, bcs)
            if p >= prev:
                cyc += br(bcs, 1)
                break
            cyc += br(bcs, 0) + L(lp, bcc)
            p += 4
            if p < end:
                cyc += br(bcc, 1)
            else:
                cyc += br(bcc, 0)
                break
        W.sw(mark, W.w(cur))
        cyc += L(tail, (0xB2DE, 0xB2F4, 0xB309)[(0x01DB, 0x01DD, 0x01DF).index(cur)])
    return cyc


# C0/28E1 -- re-apply the opened-door list ($7F:1CC8, packed, ends at the first negative word) to the map
def door28e1(W):
    cyc = L(0x28E1, 0x28E6)
    for k in range(16):
        v = W.w(0x11CC8 + 2 * k)
        cyc += L(0x28E6, 0x28EA)
        if v & 0x8000:
            return cyc + br(0x28EA, 1) + L(0x28F6, 0x28F9)
        cyc += br(0x28EA, 0) + c(0x28EC) + L(0x28F9, 0x2901)
        tile = W.b(0x3000 + v)
        arms = ((0xE6, 0x2903, 0x291C, 0x2931), (0xEC, 0x2907, 0x2933, 0x2956), (0xEE, 0x290B, 0x2958, 0x297B),
                (0xFA, 0x290F, 0x297D, 0x29B7))
        for i, (tv, beq, arm, bra) in enumerate(arms):
            cyc += LR(beq - 2, beq, 1, 0, 0x0100)             # CMP #xx
            if tile == tv:
                cyc += br(beq, 1) + LR(arm, bra + (3 if tv == 0xFA else 2), 1, 0, 0x0100)
                break
            cyc += br(beq, 0)
        else:
            raise NotImplementedError('28F9 tile %02X' % tile)   # FC / other: not traced
        cyc += L(0x2918, 0x291C) + L(0x28EF, 0x28F4)
        if k == 15:
            return cyc + br(0x28F4, 0) + L(0x28F6, 0x28F9)
        cyc += br(0x28F4, 1)
    return cyc


# C0/A712 -- the $7E:70C0 page reset (64 bytes + 63 MVNs of 64), then the location's chests ($F5:F000 ranges):
# each writes its index to $7E:70C0 + (its word & $1D1E) and C0/A7BC tests its flag bit at $7F:0001+.
def a712(W, loc):
    cyc = L(0xA712, 0xA717)
    for i in range(0x40):
        cyc += L(0xA717, 0xA71F) + br(0xA71F, i < 0x3F)
    cyc += L(0xA721, 0xA726)
    for i in range(63):
        cyc += L(0xA726, 0xA72D) + mvn(0xA72D, 64) + L(0xA730, 0xA73A) + br(0xA73A, i < 62)
    cyc += L(0xA73C, 0xA756)
    rd = lambda a: ROM[0x350000 + a] | (ROM[0x350001 + a] << 8)   # $F5:xxxx
    base = rd(0xF000)
    lo, hi = rd(0xF000 + 2 * (loc & 0x1FF)), rd(0xF002 + 2 * (loc & 0x1FF))
    if lo == hi:
        return cyc + br(0xA756, 1) + L(0xA7B9, 0xA7BC)
    cyc += br(0xA756, 0) + L(0xA758, 0xA761)
    if rd(lo) == 0:
        raise NotImplementedError('A763: a zero first chest word')
    cyc += br(0xA761, 1) + L(0xA769, 0xA76D)
    x, idx = lo, 0
    while True:
        cyc += L(0xA76D, 0xA76F)
        if x >= hi:
            return cyc + br(0xA76F, 1) + L(0xA7B9, 0xA7BC)
        cyc += br(0xA76F, 0) + L(0xA771, 0xA785)
        # C0/A7BC: the flag bit ((x - base) / 4) of $7F:0001
        n = ((x - base) >> 2) & 0xFFFF
        byte, bit = (n >> 3) & 0x3F, ROM[0xFF20 + (n & 7)]
        cyc += L(0xA7BC, 0xA7E1)
        if W.b(0x10001 + byte) & bit:
            raise NotImplementedError('A7E6: an opened chest')
        cyc += br(0xA7E1, 0) + L(0xA7E3, 0xA7E6) + br(0xA785, 1) + L(0xA7A9, 0xA7B7) + c(0xA7B7)
        x += 4
        idx += 1


def pass_resets(W):
    """the pass's table resets before C0/B0E6: C0/6F79 ($0B88), C0/7155 ($0B80), C0/E935 ($0BC0), C0/B204 (the
    $0E00 page, then $0F01/$0F02 per object), C0/A7E9 ($0F00/$0F01 per object)"""
    for k in range(0x1E):
        W.sb(0x0B88 + k, 0x80)
    for k in range(8):
        W.sb(0x0B80 + k, 0x80)
        W.sb(0x0BC0 + k, 0x80)
    for k in range(0x100):
        W.sb(0x0E00 + k, 0x80)
    n = W.b(0x12000)
    for k in range(n):
        W.sb(0x0F01 + 2 * k, 0xFF)
        W.sb(0x0F02 + 2 * k, 0x00)
    for k in range(n):
        W.sb(0x0F00 + 2 * k, 0x00)
        W.sb(0x0F01 + 2 * k, 0xFF)


def a7e9(n):
    cyc = L(0xA7E9, 0xA805)
    for i in range(n):
        cyc += L(0xA805, 0xA80A) + br(0xA80A, i < n - 1)
    return cyc + L(0xA80C, 0xA810)


def b204(n):
    cyc = L(0xB204, 0xB21B)
    for i in range(16):
        cyc += L(0xB21B, 0xB220) + br(0xB220, i < 15)
    cyc += L(0xB222, 0xB22E) + mvn(0xB22E, 32) + L(0xB231, 0xB237) + mvn(0xB237, 64) + L(0xB23A, 0xB240)
    cyc += mvn(0xB240, 128) + L(0xB243, 0xB259)
    for i in range(n):
        cyc += L(0xB259, 0xB25E) + br(0xB25E, i < n - 1)
    return cyc + L(0xB260, 0xB262)


def walked(W):
    """the pass's walked legs, in the ROM's order; what they do not cover is the constant remainder"""
    n = W.b(0x12000)
    parts = {}
    parts['28E1 doors'] = door28e1(W)
    parts['A712 chests'] = a712(W, W.w(0x0100))
    parts['7506/7553 camera'] = camera(W)
    bg3 = (W.b(0x0BCA) & 1) and not W.b(0x01BB)
    parts['0A50 BG3 arm'] = 0
    if bg3:
        if W.b(0x1D35) & 4:
            raise NotImplementedError('BG3 upload 7F77')
        parts['0A50 BG3 arm'] = 298554 + 17000 + 248      # C0/7CB5, C0/7F62, C0/0AD8-0AF3 (see the C model)
    parts['A7E9'] = a7e9(n)
    parts['B204'] = b204(n)
    pass_resets(W)
    parts['B0E6'] = b0e6(W)
    parts['B271'] = b271(W)
    parts['FD:C1EE select'] = 18 if (W.b(0x0153) & 0x0F) == 0 else 30
    return parts


MEASURED = {'p3': 1728472, 'shop': 1924502, 'door': 1198974}


def main():
    args = sys.argv[1:]
    W = Wram(open(args[0], 'rb').read())
    parts = walked(W)
    for k, v in parts.items():
        print('  %-18s %8d' % (k, v))
    tot = sum(parts.values())
    tag = os.path.basename(args[0]).split('_')[0]
    m = MEASURED.get(tag)
    print('walked %d%s' % (tot, (', measured %d, remainder %d' % (m, m - tot)) if m else ''))


if __name__ == '__main__':
    main()
