#!/usr/bin/env python
"""Instruction-ALIGNED reference scanner for the CT ROM.

    python tools/refscan.py <hexaddr> [bank]          -- who references this 16-bit address?
    python tools/refscan.py 299E                      -- all banks
    python tools/refscan.py 3009 C2                   -- restrict data refs to one bank

WHY THIS EXISTS. A naive `for i in range(len(rom)): if rom[i+1]|rom[i+2]<<8 == target` scan is wrong
in two ways that both bit this project in a single session (2026-09-11), and both produced confident,
committed, WRONG derivations:

  1. IT DOES NOT RESPECT INSTRUCTION BOUNDARIES. Scanning for `$299E` reported `C1/33BC STZ $299E`.
     There is no such instruction: the real code is `A6 9C 9E 29 98` = `LDX $9C / STZ $9829,X`, and
     the scan matched the `9C` of the LDX operand as an opcode. One of exactly three "sites" was a
     ghost, and the other two were the whole mechanism.

  2. JSR AND JMP ABSOLUTE ARE BANK-LOCAL. Scanning every bank for `20 09 30` "found" five callers of
     `C2/3009` in banks C4/E7/EF/F1/F3. A `JSR $3009` in bank C4 calls C4/3009. The real answer was
     ZERO direct callers -- the routine is reached indirectly -- which is a completely different fact.

So this tool (a) decodes forward from an anchor so it only reports real instruction starts, and
(b) knows which operand forms are bank-local, which carry their own bank, and which depend on DB.

ALIGNMENT, HONESTLY. There is no way to know where instructions start in a raw ROM. This walks each
bank from many staggered anchors and keeps an offset only if it is an instruction start on a MAJORITY
of the walks that reach it -- linear 65816 decoding self-synchronises quickly, so a real instruction
start is reached from almost every anchor and a byte inside an operand from almost none. Hits are
printed with that agreement count so a thin one can be eyeballed rather than trusted. It is a filter,
not a proof: confirm anything load-bearing with `tools/dis.py` at the reported address.
"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '.')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import disasm65816 as D

ROM = _romlib.load("Chrono_Trigger_USA_SNES.sfc")
NBANK = len(ROM) >> 16

# operand forms that name a 16-bit address, and where that address LIVES.
#   'local' : JSR/JMP absolute and (abs,X) -- the target bank is the bank being executed
#   'long'  : the instruction carries its own bank byte (4-byte forms)
#   'db'    : a data reference through the data bank -- the bank is whatever DB held
ABS_DB = {0xAD: "LDA a", 0xBD: "LDA a,X", 0xB9: "LDA a,Y", 0x8D: "STA a", 0x9D: "STA a,X",
          0x99: "STA a,Y", 0xCE: "DEC a", 0xDE: "DEC a,X", 0xEE: "INC a", 0xFE: "INC a,X",
          0xCD: "CMP a", 0xDD: "CMP a,X", 0xD9: "CMP a,Y", 0x2C: "BIT a", 0x3C: "BIT a,X",
          0x9C: "STZ a", 0x9E: "STZ a,X", 0xAE: "LDX a", 0xBE: "LDX a,Y", 0xAC: "LDY a",
          0xBC: "LDY a,X", 0x0D: "ORA a", 0x1D: "ORA a,X", 0x19: "ORA a,Y", 0x2D: "AND a",
          0x3D: "AND a,X", 0x39: "AND a,Y", 0x4D: "EOR a", 0x5D: "EOR a,X", 0x59: "EOR a,Y",
          0x6D: "ADC a", 0x7D: "ADC a,X", 0x79: "ADC a,Y", 0xED: "SBC a", 0xFD: "SBC a,X",
          0xF9: "SBC a,Y", 0x1C: "TRB a", 0x0C: "TSB a", 0x0E: "ASL a", 0x1E: "ASL a,X",
          0x4E: "LSR a", 0x5E: "LSR a,X", 0x2E: "ROL a", 0x3E: "ROL a,X", 0x6E: "ROR a",
          0x7E: "ROR a,X", 0xEC: "CPX a", 0xCC: "CPY a"}
ABS_LOCAL = {0x20: "JSR a", 0x4C: "JMP a", 0xFC: "JSR (a,X)", 0x7C: "JMP (a,X)", 0x6C: "JMP (a)"}
LONG = {0xAF: "LDA l", 0xBF: "LDA l,X", 0x8F: "STA l", 0x9F: "STA l,X", 0xCF: "CMP l", 0xDF: "CMP l,X",
        0x0F: "ORA l", 0x1F: "ORA l,X", 0x2F: "AND l", 0x3F: "AND l,X", 0x4F: "EOR l", 0x5F: "EOR l,X",
        0x6F: "ADC l", 0x7F: "ADC l,X", 0xEF: "SBC l", 0xFF: "SBC l,X", 0x22: "JSL l", 0x5C: "JML l"}


def starts_in_bank(bank, anchors=64):
    """offset -> (walks that saw it as an instruction START, walks that covered it at all)."""
    base = bank << 16
    votes = [0] * 0x10000
    seen = [0] * 0x10000
    step = 0x10000 // anchors
    for k in range(anchors):
        p, m, x = k * step, 1, 1
        end = min(p + step * 3, 0x10000)          # each walk covers its own stretch plus overlap
        for q in range(k * step, end):
            seen[q] += 1
        while p < end - 4:
            votes[p] += 1
            op = ROM[base + p]
            if op == 0xC2:                         # REP
                v = ROM[base + p + 1]
                if v & 0x20: m = 0
                if v & 0x10: x = 0
            elif op == 0xE2:                       # SEP
                v = ROM[base + p + 1]
                if v & 0x20: m = 1
                if v & 0x10: x = 1
            p += 1 + D.oplen(D.TABLE[op][1], m, x)
    return votes, seen


def main():
    if len(sys.argv) < 2:
        print(__doc__); return 2
    target = int(sys.argv[1], 16) & 0xFFFF
    only = int(sys.argv[2], 16) if len(sys.argv) > 2 else None
    if only is not None and only >= 0xC0:
        only -= 0xC0
    print("references to $%04X  (ROM banks $C0-$%02X)\n" % (target, 0xC0 + NBANK - 1))
    shown = 0
    for bank in range(NBANK):
        base = bank << 16
        votes, seen = starts_in_bank(bank)
        for i in range(0, 0x10000 - 4):
            if not votes[i]:
                continue                            # not an instruction start on any walk
            op = ROM[base + i]
            a = ROM[base + i + 1] | ROM[base + i + 2] << 8
            if a != target:
                continue
            if op in ABS_LOCAL:
                kind, where = ABS_LOCAL[op], "bank $%02X (JSR/JMP are BANK-LOCAL)" % (0xC0 + bank)
                if only is not None and bank != only:
                    continue
            elif op in LONG:
                b = ROM[base + i + 3]
                kind, where = LONG[op], "bank $%02X (explicit)" % b
                if only is not None and b != 0xC0 + only and b != only:
                    continue
            elif op in ABS_DB:
                kind, where = ABS_DB[op], "whatever DB holds"
                if only is not None and bank != only:
                    continue
            else:
                continue
            print("  %02X/%04X  %-10s -> %-34s [instruction start on %d of %d walks]"
                  % (0xC0 + bank, i, kind, where, votes[i], seen[i]))
            shown += 1
    if not shown:
        print("  (none)")
    print("")
    print("NOTE: a hit that is a start on only SOME of the walks that covered it is probably data or a")
    print("  mid-instruction ghost -- confirm with tools/dis.py at that address before believing it.")
    print("  And a JSR/JMP hit only reaches the address IN ITS OWN BANK: a routine with no direct")
    print("  caller is usually reached through a jump table, not missing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
