#!/usr/bin/env python
# Simulate the title sequence's overlay machine frame by frame and diff it against the oracle's
# live WRAM. This is the derivation harness for disasm/notes/title_intro.md section 6+: the $C3
# script interpreter (C3/0B03, DP = $0A00), the NMI's two hook lists, every task the title installs,
# the sprite-package drawers (C3/0D0E / C3/0D5E) and the ROM's palette-job runner (FD/E39C).
#
#   python tools/ti_sim.py [frames]
#
# It seeds from the oracle's own WRAM at boot frame 140 (the frame after the sequence's script
# installs its tasks) and then runs on its own; every value it prints is compared against a
# --monitor stream of the same cells.  A mismatch is a hole in the derivation, not a tolerance.
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '.')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import os
import re
import subprocess
import sys

ROM = 'Chrono_Trigger_USA_SNES.sfc'
EXE = os.path.abspath('build/ct.exe')
TMP = 'build/_ti'
os.makedirs(TMP, exist_ok=True)     # a fresh checkout has no build/_ti
SEED_FRAME = 140

rom = _romlib.load(ROM)
SIN = rom[0xF900:0xF900 + 1024]


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def sin_(a):
    """$7E:3465 -- returns (value16, carry, negative). The carry is the CPX #$0200 the routine
    itself leaves set, and it is what the callers' following ADC picks up."""
    idx = a & 0x3FF
    v = SIN[idx]
    c = 1 if idx >= 0x200 else 0
    if idx < 0x200:
        v = (-v) & 0xFFFF
    return v, c, (v & 0x8000) != 0


def cos_(a):
    return sin_((a + 0x100) & 0xFFFF)


class Machine:
    def __init__(self, w):
        u16 = lambda a: w[a] | (w[a + 1] << 8)
        self.reg = [u16(0x0A00 + i * 2) for i in range(16)]
        self.ip = u16(0x0A20)
        self.ret = u16(0x0A30)
        self.ctr = [u16(0x0A22 + i * 2) for i in range(16)]
        self.pend = [u16(0x0920 + i * 2) for i in range(16)]
        self.live = [u16(0x0950 + i * 2) for i in range(16)]
        self.vbl = [u16(0x0940 + i * 2) for i in range(8)]
        self.m = dict((a, u16(a)) for a in (0x08, 0x11, 0x13, 0x15, 0x17, 0x19, 0x44,
                                            0x5A, 0x5C, 0x5E, 0x60, 0x62, 0x64, 0x66,
                                            0x68, 0x6C, 0x6E, 0x70, 0x72, 0xFE))
        self.slide = [u16(0x46 + i * 2) for i in range(5)]
        self.tgt = [u16(0x50 + i * 2) for i in range(5)]
        self.f = bytearray(w[0xF0:0xFA])                 # the $F0..$F9 scratch
        self.wave = [u16(0x0C00 + i * 2) for i in range(64)]
        self.oam = bytearray(w[0x0700:0x0920])
        self.job = [bytearray(w[0x520 + r * 12:0x520 + r * 12 + 12]) for r in range(12)]
        self.pal0 = [u16(0x2000 + i * 2) for i in range(256)]
        self.palsh = [u16(0x2200 + i * 2) for i in range(256)]
        self.script = w                                   # the overlay image holds the byte-code
        self.mode, self.tm, self.ts = 7, 0x01, 0x10
        self.done = 0
        self.joy = 0
        self.exitflag = 0
        self.bri = [w[0x038D], w[0x038E], w[0x038F]]       # $038D brightness / $038E period / $038F
        self.mail = list(w[0x1E00:0x1E04])                 # the sound mailbox -- persistent RAM
        self.snd = [w[0x0388], w[0x0389], w[0x038A]]        # busy / song playing / song wanted

    # ---- the $F0 scratch, as words -------------------------------------------------------------
    def fw(self, a):
        return self.f[a - 0xF0] | (self.f[a - 0xF0 + 1] << 8)

    def fws(self, a, v):
        self.f[a - 0xF0] = v & 0xFF
        self.f[a - 0xF0 + 1] = (v >> 8) & 0xFF

    def mul(self):
        """$7E:31BF -- 16x16 -> 32 out of four hardware 8x8s. Note: it zeroes $F6 (and $F7) and then
        ROLs the word at $F7, so bit 7 of the stale $F8 rotates into the middle ADC as a carry, and
        $F8 is left shifted one place. The matrix depends on this quirk."""
        f0, f2 = self.fw(0xF0), self.fw(0xF2)
        p0 = (f2 & 0xFF) * (f0 & 0xFF)
        p1 = (f2 & 0xFF) * (f0 >> 8)
        p2 = (f2 >> 8) * (f0 & 0xFF)
        p3 = (f2 >> 8) * (f0 >> 8)
        self.fws(0xF4, p0)                                # $F4 = P0
        f8 = self.f[8]
        word = f8 << 8                                    # $F7 was just zeroed by `STZ $F6`
        a = (p1 + p2) & 0xFFFF
        carry = 1 if (p1 + p2) > 0xFFFF else 0
        rol = ((word << 1) | carry) & 0xFFFF              # ROL $F7  (16-bit)
        cout = (word >> 15) & 1                           # bit 7 of the old $F8
        self.f[7] = rol & 0xFF
        self.f[8] = (rol >> 8) & 0xFF
        t = a + self.fw(0xF5) + cout                      # ADC $F5
        self.fws(0xF5, t & 0xFFFF)
        c2 = 1 if t > 0xFFFF else 0
        t2 = p3 + self.fw(0xF6) + c2                      # ADC $F6
        self.fws(0xF6, t2 & 0xFFFF)

    def div(self):
        """$7E:339F -- 32/16 restoring division; quotient to $F8/$FA, remainder to $FC/$FE."""
        num = self.fw(0xF0) | (self.fw(0xF2) << 16)
        den = self.fw(0xF4) | (self.fw(0xF6) << 16)
        q, r = (0, 0) if den == 0 else (num // den, num % den)
        self.fws(0xF8, q & 0xFFFF)
        return q

    # ---- the sprite-package drawers ------------------------------------------------------------
    def draw(self, addr, large):
        """C3/0D0E (small) / C3/0D5E (large). Both append to the OAM shadow at the $11 cursor and
        shift the high-table byte at $13 two bits per sprite -- from the top for 0D5E (so every
        sprite it emits is 16x16), zeros from the top for 0D0E (so every one is 8x8)."""
        w = self.script
        n = w[addr] - 1
        y, x = self.m[0x11], self.m[0x13]
        p = addr + 1
        for _ in range(n):
            tile, attr, dx, dy = w[p], w[p + 1], w[p + 2], w[p + 3]
            p += 4
            self.oam[y + 2] = tile
            self.oam[y + 3] = attr
            if large:
                sx = (dx + self.m[0x15]) & 0xFFFF
                if sx >= 0x100:
                    sx = 0x9801
                self.oam[y] = sx & 0xFF
                cin = (sx >> 15) & 1                      # ASL A -> carry = bit 15
                b = self.oam[0x200 + (x - 0x900)]
                b = ((b >> 1) | (cin << 7)) & 0xFF        # ROR A with that carry
                b = ((b >> 1) | 0x80) & 0xFF              # SEC / ROR A  -> size = large
                self.oam[0x200 + (x - 0x900)] = b
                self.oam[y + 1] = (dy + self.m[0x17]) & 0xFF
            else:
                self.oam[y] = (dx + self.m[0x15]) & 0xFF
                self.oam[y + 1] = (dy + self.m[0x17]) & 0xFF
                b = self.oam[0x200 + (x - 0x900)]
                if b:
                    self.oam[0x200 + (x - 0x900)] = b >> 2
            y += 4
            if (y & 0x0F) == 0:
                x += 1
        self.m[0x11], self.m[0x13] = y, x

    # ---- the $C3 script interpreter (C3/0B03), DP = $0A00 --------------------------------------
    def script_run(self):
        w = self.script
        for _ in range(4096):                              # a frame's run always ends in a yield
            op = w[self.ip]
            self.ip += 1
            if not (op & 0x80):
                r, hi = op & 0x0F, op & 0xF0
                if hi & 0x20:
                    x = (w[self.ip] & 0x1E) >> 1
                    self.ip += 1
                    v = self.reg[x]
                    self.reg[r] = v if hi == 0x20 else (self.reg[r] + v) & 0xFFFF
                else:
                    v = w[self.ip] | (w[self.ip + 1] << 8)
                    self.ip += 2
                    self.reg[r] = v if hi == 0x00 else (self.reg[r] + v) & 0xFFFF
                continue
            hi, n = op & 0xF0, op & 0x0F
            if hi in (0x80, 0x90):
                lim = w[self.ip] | (w[self.ip + 1] << 8)
                tgt = w[self.ip + 2] | (w[self.ip + 3] << 8)
                c = self.ctr[n]
                if c != 0 and c >= lim:
                    self.ctr[n] = 0
                    self.ip += 4
                    if hi == 0x90:
                        return
                    continue
                self.ctr[n] = (c + 1) & 0xFFFF
                self.ip = tgt
                if hi == 0x90:
                    return
                continue
            imm16 = lambda: w[self.ip] | (w[self.ip + 1] << 8)
            if n == 0:
                self.call_sub(imm16())
                self.ip += 2
            elif n == 1:
                self.poke8(imm16(), w[self.ip + 2])
                self.ip += 3
            elif n == 2:
                self.poke16(imm16(), w[self.ip + 2] | (w[self.ip + 3] << 8))
                self.ip += 4
            elif n == 3:
                self.ret = self.ip + 2
                self.ip = imm16()
            elif n == 4:
                self.ip = self.ret
            elif n == 5:
                self.ip += 1                               # APU handshake: nothing to model here
            elif n == 6:
                self.ip += 4
            elif n == 7:
                self.listdel(self.vbl, imm16()); self.ip += 2
            elif n == 8:
                self.listadd(self.vbl, imm16()); self.ip += 2
            elif n == 9:
                self.listdel(self.pend, imm16()); self.ip += 2
            elif n == 0xA:
                self.listadd(self.pend, imm16()); self.ip += 2
            elif n == 0xB:
                self.ppureg(0x2100 + w[self.ip + 1], w[self.ip]); self.ip += 2
            elif n == 0xC:
                return
            elif n == 0xD:
                self.ip = imm16(); return
            elif n == 0xE:
                self.ip = imm16()
            elif n == 0xF:
                self.ip -= 1
                self.done = 1
                return
        raise RuntimeError('script did not yield')

    def listadd(self, lst, v):
        for i in range(len(lst)):
            if lst[i] == 0:
                lst[i] = v
                return

    def listdel(self, lst, v):
        for i in range(len(lst)):
            if lst[i] == v:
                lst[i] = 0
                return

    def listswap(self, lst, frm, to):                      # $7E:39B1 -- take over the same slot
        for i in range(len(lst)):
            if lst[i] == frm:
                lst[i] = to
                return

    def poke8(self, a, v):
        if a in self.m:
            self.m[a] = (self.m[a] & 0xFF00) | v
        elif a == 0x6E:
            self.m[0x6E] = v

    def poke16(self, a, v):
        self.m[a] = v

    def ppureg(self, a, v):
        pass

    def call_sub(self, a):
        if a == 0x5DA7:                                    # the mode switch: $2105 = 2, reg2 = BG1VOFS
            self.mode = 2
            self.reg[2] = 0xFFB9
        elif a == 0x5DB4:
            self.job[0][:] = bytes([0x50, 0xC0, 0x10, 0x04, 0x00, 0x01, 0x00, 0x00, 0xFF, 0, 0, 0])
        elif a == 0x5DE9:
            self.job[0][:] = bytes([0x40, 0xB0, 0x0B, 0x02, 0x00, 0x01, 0x03, 0x80, 0x00, 0, 0, 0])
        else:
            raise RuntimeError('unmodelled script call $%04X' % a)

    # ---- the tasks -----------------------------------------------------------------------------
    def t_5BCF(self):                                      # the mode-7 pendulum driver
        m = self.m
        m[0x66] = (m[0x66] + 7) & 0xFFFF
        v, c, neg = cos_(m[0x66])
        a = (-v) & 0xFFFF if neg else v
        self.fws(0xF0, a)
        self.fws(0xF2, m[0x6C])
        self.fws(0xF6, 0)
        self.mul()
        a = self.fw(0xF5) >> 2
        if neg:
            a = (-a) & 0xFFFF
        m[0xFE] = (a + 0x200) & 0xFFFF                     # CLC before the ADC: no carry in
        v2, c2, _ = cos_(m[0xFE])
        self.fws(0xF0, v2)
        self.fws(0xF2, self.reg[0])
        self.fws(0xF6, 0)
        self.mul()
        ma = (self.fw(0xF5) + 1) & 0xFFFF
        m[0x5A] = m[0x60] = ma
        v3, c3, neg3 = sin_(m[0xFE])
        a3 = (-v3) & 0xFFFF if neg3 else v3
        self.fws(0xF0, a3)
        self.fws(0xF2, self.reg[0])
        self.fws(0xF6, 0)
        self.mul()
        b = self.fw(0xF5)
        if neg3:
            b = (-b) & 0xFFFF
        m[0x5C] = b
        m[0x5E] = (-b) & 0xFFFF
        self.fws(0xF0, 0)
        self.fws(0xF6, 0)
        self.fws(0xF2, 1)
        self.fws(0xF4, self.reg[0])
        self.div()
        self.reg[2] = (self.fw(0xF8) - 0x100) & 0xFFFF     # SEC/SBC: borrow-free
        m[0x6C] = m[0x6C] - 1 if m[0x6C] - 1 > 0 else 1

    def t_5FFE(self):                                      # the offset-per-tile wave for BG1
        for i in range(0x3E, 0x1F, -1):
            self.wave[i + 1] = self.wave[i]
        v, c, neg = sin_((self.m[0x08] << 4) & 0xFFFF)
        a = (-v) & 0xFFFF if neg else v
        self.fws(0xF0, a)
        self.fws(0xF2, self.reg[6])
        self.fws(0xF6, 0)
        self.mul()
        a = self.fw(0xF5)
        if neg:
            a = (-a) & 0xFFFF
        a = (a - 0x4A) & 0xFFFF                            # an explicit SEC precedes this SBC
        self.wave[0x20] = (a & 0x3FF) | 0x2000

    def t_5C98(self):                                      # the clock's spiral-in
        m = self.m
        a = (m[0x68] - 2) & 0xFFFF
        if s16(a) < 0x70:
            self.tm, self.ts = 0x13, 0x00
            self.ip = 0x5B51
            m[0x62], m[0x64] = 0x38, 0x3A
            self.listdel(self.pend, 0x5C98)
            return
        m[0x68] = a
        v, c, neg = cos_(a)
        self.fws(0xF0, (-v) & 0xFFFF if neg else v)
        self.fws(0xF2, 0xD8)
        self.fws(0xF6, 0)
        self.mul()
        x = self.fw(0xF5)
        if neg:
            x = (-x) & 0xFFFF
        m[0x62] = (x + 0xFFE0 + c) & 0xFFFF
        v, c, neg = sin_(a)
        self.fws(0xF0, (-v) & 0xFFFF if neg else v)
        self.fws(0xF2, 0xC3)
        self.fws(0xF6, 0)
        self.mul()
        y = self.fw(0xF5)
        m[0x64] = (y + 0xFFC0 + c) & 0xFFFF

    def t_5D01(self):
        self.m[0x15] = self.m[0x62]
        self.m[0x17] = self.m[0x64]
        self.draw(0x603F, False)
        self.m[0x17] = (self.m[0x64] + 0x40) & 0xFFFF
        self.draw(0x60BC, False)

    def t_5D20(self):
        self.m[0x17] = 0x64
        self.m[0x15] = 0x3A
        self.draw(0x61B4, False)

    def t_5D32(self):
        self.m[0x15] = 0x90
        self.m[0x17] = 0xB0
        self.draw(0x61D9, True)
        self.m[0x72] = (self.m[0x72] - 1) & 0xFFFF
        if self.m[0x72] == 0:
            self.listdel(self.pend, 0x5D32)
            return
        if self.m[0x72] == 0x3C:
            self.listadd(self.pend, 0x5B99)

    def t_5D71(self):
        self.m[0x17] = 0x52
        PKG = (0x60F1, 0x610A, 0x6123, 0x6148, 0x6161)
        SPD = (4, 9, 14, 20, 24)
        for i in range(5):
            v = (self.slide[i] + SPD[i]) & 0xFFFF
            if v >= self.tgt[i]:
                v = self.tgt[i]
            self.slide[i] = v
            self.m[0x15] = (v >> 4) & 0xFF
            self.draw(PKG[i], False)

    def t_5E1E(self):
        self.m[0x17] = 0xD0
        self.m[0x15] = 0x78
        self.draw(0x6186, False)
        self.m[0x15] = 0xB8
        self.draw(0x61A7, False)

    def t_5BBF(self):
        if (self.m[0x08] & 0x1F) == 0:
            self.reg[6] = (self.reg[6] - 1) & 0xFFFF
            if self.reg[6] == 0:
                self.listdel(self.pend, 0x5BBF)

    def t_5B99(self):
        """$7E:5B99 -- leaves on any button, latches $0070, installs the fade, and only then lets
        `BIT #$30` (L or R) decide whether $7F0060 is left behind."""
        if self.m[0x6E] or self.m[0x70]:
            return
        if self.joy == 0:
            return
        self.m[0x70] = (self.m[0x70] & 0xFF00) | ((self.m[0x70] + 1) & 0xFF)
        self.listadd(self.pend, 0x34AB)
        if not (self.joy & 0x30):
            self.exitflag = 1
        self.listdel(self.pend, 0x5B99)

    def nmi_sound(self):
        """C3/09A4 -- the NMI's other half, called right after C3/08D0. A song-change watcher: when
        the song wanted ($038A) differs from the song playing ($0389) it loads the wanted one. The
        title latches both in its setup, so here it only ever finds them equal."""
        if self.snd[0]:
            return
        if 2 & 0x20:                                       # $09AB LDA $0384 / BIT #$20 -- never here
            return
        if self.snd[2] == self.snd[1]:
            return
        self.snd[1] = self.snd[2]
        self.snd[0] = 1
        self.mail[0] = 0x10
        self.mail[1] = self.snd[2]
        self.mail[2] = 0xFF
        self.mail[3] = 0xFF
        self.post()
        self.snd[0] = 0

    def post(self):
        """JSL $C7:0004. Everything >= $80 is the raw 4-byte port write C7/061C, and the service
        clears $1E00 on the way out ($C7:0192) -- which is why a frame boundary never shows the
        command byte, only the parameters it left behind."""
        self.mail[0] = 0

    def t_34AB(self):                                      # the exit: sound, then become the fade
        # `LDX #$0040 / STX $1E01` is 16-bit: $1E01 = $40 and $1E02 = $00. $1E03 is not touched.
        self.mail[1] = 0x40
        self.mail[2] = 0x00
        self.mail[0] = 0x81
        self.post()
        self.listswap(self.pend, 0x34AB, 0x34C2)

    def t_34C2(self):                                      # the brightness ramp, and the done flag
        self.bri[2] = (self.bri[2] - 1) & 0xFF
        if self.bri[2]:
            return
        self.bri[2] = self.bri[1]
        self.bri[0] = (self.bri[0] - 1) & 0xFF
        if self.bri[0]:
            return
        self.mail[0] = 0xF0                                # $34D5 writes the command byte alone
        self.post()
        self.done = 1
        self.listdel(self.pend, 0x34C2)

    TASKS = {0x3749: 'script', 0x5BCF: 't_5BCF', 0x37FA: None, 0x5FFE: 't_5FFE',
             0x5D32: 't_5D32', 0x5B99: 't_5B99', 0x5D01: 't_5D01', 0x5C98: 't_5C98',
             0x5D71: 't_5D71', 0x5D20: 't_5D20', 0x5BBF: 't_5BBF', 0x5E1E: 't_5E1E',
             0x34AB: 't_34AB', 0x34C2: 't_34C2'}

    # ---- the palette-job runner (FD/E39C) ------------------------------------------------------
    def jobs_run(self):
        for rec in self.job:
            if rec[0] == 0:
                continue
            rec[4] = (rec[4] + 1) & 0xFF
            if rec[4] != rec[5]:
                continue
            rec[4] = 0
            kind = rec[0] >> 4
            lvl, step, tgt = rec[7], rec[3], rec[8]
            if lvl < tgt:
                n = lvl + step
                if n > 0xFF or n >= tgt:
                    if n > 0xFF:
                        self.pal_restore(rec); rec[0] = 0; continue
                    rec[0] = 0
                    n = tgt
            else:
                n = lvl - step
                if n < 0:
                    self.pal_restore(rec); rec[0] = 0; continue
                if n < tgt:                                # BCS keeps the job alive at n == target
                    rec[0] = 0
                    n = tgt
            rec[7] = n
            self.pal_blend(rec, kind)

    def pal_restore(self, rec):
        for i in range(rec[2]):
            self.palsh[rec[1] + i] = self.pal0[rec[1] + i]

    def pal_blend(self, rec, kind):
        lvl = rec[7]
        for i in range(rec[2]):
            c = self.pal0[rec[1] + i]
            r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
            if kind == 5:                                  # FD/E64F: scale toward black
                r2 = (lvl * r) >> 8
                g2 = (lvl * g) >> 8
                b2 = (lvl * b) >> 8
            else:                                          # FD/EA5C: blend R,G toward 31; B := 0
                r2 = (r + ((lvl * (31 - r)) >> 8)) & 31
                g8 = (g * 8 + ((lvl * (248 - g * 8)) >> 8)) & 0xF8
                g2 = g8 >> 3
                b2 = 0
            self.palsh[rec[1] + i] = (b2 << 10) | (g2 << 5) | r2

    # ---- one NMI ------------------------------------------------------------------------------
    def frame(self):
        for h in self.vbl:                                 # the vblank hook list ($0940)
            if h == 0x380B:
                b = self.oam[0x200 + (self.m[0x13] - 0x900)]
                k = (self.m[0x11] >> 2) & 3
                while k != 4:
                    k += 1
                    b = ((b >> 2) | 0x40) & 0xFF           # SEC/ROR/LSR: x-high 1, size 0
                self.oam[0x200 + (self.m[0x13] - 0x900)] = b
                self.m[0x11], self.m[0x13] = 0, 0x900
            elif h == 0x3843:
                for i in range(0x200):
                    self.oam[i] = 0xE0
        self.m[0x08] = (self.m[0x08] + 1) & 0xFFFF
        self.live = list(self.pend)
        for t in self.live:
            if t == 0:
                continue
            fn = self.TASKS.get(t, 0)
            if fn == 0:
                raise RuntimeError('unmodelled task $%04X' % t)
            if fn == 'script':
                self.script_run()
            elif fn:
                getattr(self, fn)()
        self.jobs_run()
        self.nmi_sound()                                   # C3/09A4, the NMI's second half


def oracle_stream(nframes, btn='0:%d'):
    subprocess.run([EXE, ROM, '--fb', TMP + '/seed.raw', '--frames', str(SEED_FRAME)],
                   check=True, stdout=subprocess.DEVNULL,
                   env=dict(os.environ, PEEK_SAVE=TMP + '/seed.state'))
    spec = '7E0A00:14,7E005A:8,7E0062:2,7E0064:2,7E0066:2,7E0068:2,7E006C:2,'            '7E0072:2,7E0046:10,7E0008:2,7E0C40:2,7E0920:32,7E2360:22,7E2380:32,'            '7E038D:3,7E0002:2,7E0070:2,7F0060:1,7E1E00:4,7E0388:3'
    out = subprocess.run([EXE, ROM, '--monitor', spec, '--from', TMP + '/seed.state',
                          '--btnscript', btn % nframes if '%d' in btn else btn,
                          '--frames', str(nframes)],
                         check=True, capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        if not re.match(r'\s+f\d+\s', line):
            continue
        d = dict(re.findall(r'7E(\w{4})=(\w+)', line))
        d.update(dict(('7F' + a, v) for a, v in re.findall(r'7F(\w{4})=(\w+)', line)))
        rows.append(d)
    return rows


def le(h, off=0, n=2):
    return int(''.join(h[off + 2 * (n - 1 - i):off + 2 * (n - i)] for i in range(n)), 16)


def swap(v):
    return '%04X' % (((v & 0xFF) << 8) | (v >> 8))


def scenario(name, nframes, btn):
    """Run the machine against one oracle stream. The window ends where the sequence does: the frame
    $7E:34C2 sets $7E:0000. Past it the ROM has torn its own task list down (every slot reads $FFFF)
    and is loading whatever comes next, which is not this file's business -- so those rows are
    counted and reported separately instead of being blurred into the result."""
    rows = oracle_stream(nframes, btn)
    w = open(TMP + '/seed.raw.wram', 'rb').read()
    mach = Machine(w)
    bad = tail = checked = 0
    handover = SEED_FRAME + len(rows)
    for i, r in enumerate(rows):
        mach.joy = le(r['0002'])                           # the pad C3/0A8E latched this frame
        mach.frame()
        m = mach.m
        exp = [('reg0', le(r['0A00'], 0), mach.reg[0]), ('reg1', le(r['0A00'], 4), mach.reg[1]),
               ('reg2', le(r['0A00'], 8), mach.reg[2]), ('reg6', le(r['0A00'], 24), mach.reg[6]),
               ('m7A', le(r['005A'], 0), m[0x5A]), ('m7B', le(r['005A'], 4), m[0x5C]),
               ('m7C', le(r['005A'], 8), m[0x5E]), ('m7D', le(r['005A'], 12), m[0x60]),
               ('$62', le(r['0062']), m[0x62]), ('$64', le(r['0064']), m[0x64]),
               ('$66', le(r['0066']), m[0x66]), ('$68', le(r['0068']), m[0x68]),
               ('$6C', le(r['006C']), m[0x6C]), ('$72', le(r['0072']), m[0x72]),
               ('$08', le(r['0008']), m[0x08]), ('wave', le(r['0C40']), mach.wave[0x20]),
               ('$038D', r['038D'][0:2], '%02X' % mach.bri[0]),      # the brightness the NMI pushes
               ('$038E', r['038D'][2:4], '%02X' % mach.bri[1]),
               ('$038F', r['038D'][4:6], '%02X' % mach.bri[2]),
               ('$0070', le(r['0070']), m[0x70]),
               ('$7F0060', r['7F0060'], '%02X' % mach.exitflag)]
        # Note: the mailbox is compared only while the sequence owns it. On the frame $34C2 sets
        # $7E:0000 the ROM's main loop has already rebooted and staged its own command over the
        # $F0 -- the oracle reads `00 00 FF 05` there, which is the next stage's, not the title's.
        exp.append(('$0388', r['0388'], ''.join('%02X' % b for b in mach.snd)))
        if not mach.done:
            exp.append(('$1E00', r['1E00'], ''.join('%02X' % b for b in mach.mail)))
        for k in range(5):
            exp.append(('slide%d' % k, le(r['0046'], k * 4), mach.slide[k]))
        exp.append(('palB0', r['2360'], ''.join(swap(mach.palsh[0xB0 + j]) for j in range(11))))
        exp.append(('palC0', r['2380'], ''.join(swap(mach.palsh[0xC0 + j]) for j in range(16))))
        exp.append(('tasks', r['0920'], ''.join(swap(t) for t in mach.pend)))
        if mach.done and handover > SEED_FRAME + i:
            handover = SEED_FRAME + i                       # the frame $7E:0000 went up
        past = SEED_FRAME + i > handover
        for nm, o, sv in exp:
            if o == sv:
                if not past:
                    checked += 1
                continue
            if past:
                tail += 1
                continue
            checked += 1
            bad += 1
            if bad <= 20:
                print('f%-5d %-7s oracle=%s sim=%s' % (SEED_FRAME + i, nm, o, sv))
    print('ti-sim %-9s boot %d..%d, %d values checked, %d mismatches; %d past the teardown at %d'
          % (name + ':', SEED_FRAME + 1, min(SEED_FRAME + len(rows), handover), checked, bad,
             tail, handover))
    return bad, checked


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 2050
    # The two ways the sequence ends, both of them $7E:34AB: the script's own handover at boot 2132,
    # and a button at any point after $7E:5B99 is installed. The oracle tears the sequence down the
    # frame after $34C2 sets $7E:0000, so that frame is each window's boundary.
    bad, checked = scenario('scripted', n, '0:%d')
    b, c = scenario('button', 900, '0:600,1:4,0:296')
    bad += b
    checked += c
    # ...and the third way: a shoulder button. $7E:5B99 still leaves and still installs the fade,
    # but `BIT #$30` keeps $7F0060 at zero, which is what drops the boot into the attract demo
    # instead of the game. Mask $400 is L (measured: it puts $0020 in $7E:0002).
    b, c = scenario('shoulder', 900, '0:600,400:4,0:296')
    bad += b
    checked += c
    print('ti-sim: 3 scenarios, %d values checked, %d mismatches' % (checked, bad))
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
