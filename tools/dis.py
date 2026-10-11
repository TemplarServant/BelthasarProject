#!/usr/bin/env python
# Minimal but correct 65816 disassembler for the CT ROM (HiROM file offsets).
# Usage:  python tools/dis.py <hexaddr> <len> [m] [x]
#   <hexaddr>  ROM file offset. HiROM: file offset == the bank-C0 CPU offset, so
#              bank-C0 routines are addressed directly (e.g. 0x305D = C0/305D);
#              other banks add (bank-0xC0)<<16 (e.g. 0x2164B = C2/164B).
#   <len>      number of bytes to decode.
#   m,x        optional initial M/X widths (0 = 16-bit, 1 = 8-bit). Default 8-bit/8-bit
#              (the field engine runs SEP #$30). REP/SEP toggle them as decoded.
# The opcode matrix comes from tools/disasm65816.py (full 256-entry table) -- no more
# '?? XX' on TSB/TRB/(dp)/sr/(abs,X) modes, and MVP/MVN/PER decode at their true length
# (the old partial table desynced on them). JSR/JMP absolute labels resolve the REAL
# bank of the address being disassembled (bank = 0xC0 + offset>>16) -- previously every
# label printed 'C0/' even inside bank-C2 code; JSL/JML use their operand's own bank.
# This is a *linear* decoder (no branch following) -- M/X width state can desync after
# an unfollowed branch; read the BNE/BCS targets.
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '.')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import disasm65816 as D

ROM = _romlib.load("Chrono_Trigger_USA_SNES.sfc")

def dis(base, n, m=1, x=1):
    p, end = base, base + n
    while p < end:
        op = ROM[p]
        mnem, mode = D.TABLE[op]
        ln = D.oplen(mode, m, x)
        ob = ROM[p+1:p+1+ln]
        val = 0
        for i, by in enumerate(ob):
            val |= by << (8 * i)
        bank = 0xC0 + (p >> 16)          # the bank actually being disassembled
        if op == 0xC2:
            s = "REP  #%02X" % ob[0]
            if ob[0] & 0x20: m = 0
            if ob[0] & 0x10: x = 0
        elif op == 0xE2:
            s = "SEP  #%02X" % ob[0]
            if ob[0] & 0x20: m = 1
            if ob[0] & 0x10: x = 1
        elif mnem in ("JSR", "JMP") and mode == "abs":
            s = "%-4s %02X/%04X" % (mnem, bank, val)
        elif mode in ("immM", "immX", "imm8"):
            s = ("%-4s #%02X" % (mnem, val)) if ln == 1 else ("%-4s #%04X" % (mnem, val))
        elif mode == "imp":  s = mnem
        elif mode == "acc":  s = mnem + " A"
        elif mode == "rel":
            rel = val - 0x100 if val >= 0x80 else val
            s = "%-4s ->%04X" % (mnem, p + 2 + rel)
        elif mode == "rell":
            rel = val - 0x10000 if val >= 0x8000 else val
            s = "%-4s ->%04X" % (mnem, p + 3 + rel)
        elif mode == "abs":  s = "%-4s $%04X"      % (mnem, val)
        elif mode == "abx":  s = "%-4s $%04X,X"    % (mnem, val)
        elif mode == "aby":  s = "%-4s $%04X,Y"    % (mnem, val)
        elif mode == "abl":  s = ("%-4s %06X" % (mnem, val)) if mnem in ("JSL", "JML") \
                                 else ("%-4s $%06X" % (mnem, val))
        elif mode == "alx":  s = "%-4s $%06X,X"    % (mnem, val)
        elif mode == "dp":   s = "%-4s $%02X"      % (mnem, val)
        elif mode == "dpx":  s = "%-4s $%02X,X"    % (mnem, val)
        elif mode == "dpy":  s = "%-4s $%02X,Y"    % (mnem, val)
        elif mode == "idp":  s = "%-4s ($%02X)"    % (mnem, val)
        elif mode == "idx":  s = "%-4s ($%02X,X)"  % (mnem, val)
        elif mode == "idy":  s = "%-4s ($%02X),Y"  % (mnem, val)
        elif mode == "idl":  s = "%-4s [$%02X]"    % (mnem, val)
        elif mode == "idly": s = "%-4s [$%02X],Y"  % (mnem, val)
        elif mode == "sr":   s = "%-4s $%02X,S"    % (mnem, val)
        elif mode == "isy":  s = "%-4s ($%02X,S),Y" % (mnem, val)
        elif mode == "ind":  s = "%-4s ($%04X)"    % (mnem, val)
        elif mode == "iax":  s = "%-4s ($%04X,X)"  % (mnem, val)
        elif mode == "ial":  s = "%-4s [$%04X]"    % (mnem, val)
        elif mode == "bm":   s = "%-4s $%02X,$%02X" % (mnem, ob[0], ob[1])  # dst,src banks
        else:                s = "?? %02X" % op
        print("  %04X: %s" % (p, s))
        p += 1 + ln

if __name__ == "__main__":
    a = int(sys.argv[1], 16); n = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x40
    m = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    x = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    dis(a, n, m, x)
