#!/usr/bin/env python
# Decode a $C3 SEQUENCE SCRIPT -- the byte-code the interpreter at C3/0B03 runs with DP = $0A00
# (so its "registers" ARE the overlay's shadow cells: reg0 = $0A00, reg1 = $0A02, ...; the script
# pointer is $0A20, the CALL return slot $0A30 and the 16 loop counters $0A22+2n).
#
#   python tools/c3script.py <wram.bin> <hexaddr> [len]
#
# The opcode set is derived in disasm/notes/title_intro.md section 6 (C3/0B03-0C9x):
#   0R iiii  reg[R] = iiii        1R iiii  reg[R] += iiii
#   2R bb    reg[R] = reg[bb>>1]  3R bb    reg[R] += reg[bb>>1]   (bb masked &1E)
#   A0/B0/.. 0 aaaa  $0AF0 = aaaa; JSL $7E:37F3
#   ..1 aaaa vv      byte  [$00:aaaa] = vv          ..2 aaaa vvvv   word [$00:aaaa] = vvvv
#   ..3 aaaa         CALL aaaa    ..4  RETURN       ..5 vv  wait APU $2142 low nibble < vv
#   ..6 cccc pppp    sound command ($1E00/$1E02 -> C7/0004)
#   ..7 aaaa / ..8 aaaa   remove / add aaaa in the $0940 VBLANK list
#   ..9 aaaa / ..A aaaa   remove / add aaaa in the $0920 list
#   ..B vv rr        $21rr = vv (PPU register)
#   ..C              YIELD (end this frame's run)
#   ..D aaaa         JUMP aaaa + yield      ..E aaaa  JUMP aaaa (keep running)
#   ..F              END: $7E:0000 = 1 (the sequence's done flag)
#   8n cccc tttt     loop counter n: ++, jump tttt until it reaches cccc, then fall through (no yield)
#   9n cccc tttt     the same, but yields every iteration -- i.e. WAIT cccc FRAMES
import sys

W = open(sys.argv[1], "rb").read()
A0 = int(sys.argv[2], 16)
N = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x100


def w16(p):
    return W[p] | (W[p + 1] << 8)


LIST = {7: ('VBL.remove', 0x0940), 8: ('VBL.add', 0x0940),
        9: ('L920.remove', 0x0920), 0xA: ('L920.add', 0x0920)}

p, end, seen = A0, A0 + N, set()
while p < end:
    op = W[p]
    out = "%04X: %02X " % (p, op)
    if not (op & 0x80):
        r, hi = op & 0x0F, op & 0xF0
        if hi == 0x00:
            print(out + "%04X      reg%d = $%04X" % (w16(p + 1), r, w16(p + 1))); p += 3
        elif hi == 0x20:
            print(out + "%02X        reg%d = reg%d" % (W[p + 1], r, (W[p + 1] & 0x1E) >> 1)); p += 2
        elif hi & 0x20:
            print(out + "%02X        reg%d += reg%d" % (W[p + 1], r, (W[p + 1] & 0x1E) >> 1)); p += 2
        else:
            print(out + "%04X      reg%d += $%04X" % (w16(p + 1), r, w16(p + 1))); p += 3
        continue
    hi, n = op & 0xF0, op & 0x0F
    if hi in (0x80, 0x90):
        print(out + "%04X %04X loop%d: %s until %d, else -> %04X"
              % (w16(p + 1), w16(p + 3), n, "yield" if hi == 0x90 else "run", w16(p + 1), w16(p + 3)))
        p += 5
        continue
    if n == 0:
        print(out + "%04X      $0AF0 = $%04X; JSL $7E:37F3" % (w16(p + 1), w16(p + 1))); p += 3
    elif n == 1:
        print(out + "%04X %02X   byte [$%04X] = $%02X" % (w16(p + 1), W[p + 3], w16(p + 1), W[p + 3])); p += 4
    elif n == 2:
        print(out + "%04X %04X word [$%04X] = $%04X" % (w16(p + 1), w16(p + 3), w16(p + 1), w16(p + 3))); p += 5
    elif n == 3:
        print(out + "%04X      CALL $%04X" % (w16(p + 1), w16(p + 1))); p += 3
    elif n == 4:
        print(out + "          RETURN"); p += 1
    elif n == 5:
        print(out + "%02X        wait APU $2142 lownib < $%02X" % (W[p + 1], W[p + 1])); p += 2
    elif n == 6:
        print(out + "%04X %04X SOUND $1E00=%04X $1E02=%04X" % (w16(p + 1), w16(p + 3), w16(p + 1), w16(p + 3))); p += 5
    elif n in LIST:
        nm, base = LIST[n]
        print(out + "%04X      %s $%04X" % (w16(p + 1), nm, w16(p + 1))); p += 3
    elif n == 0xB:
        print(out + "%02X %02X     $21%02X = $%02X" % (W[p + 1], W[p + 2], W[p + 2], W[p + 1])); p += 3
    elif n == 0xC:
        print(out + "          YIELD"); p += 1
    elif n == 0xD:
        print(out + "%04X      JUMP $%04X (yield)" % (w16(p + 1), w16(p + 1))); p += 3
    elif n == 0xE:
        print(out + "%04X      JUMP $%04X" % (w16(p + 1), w16(p + 1))); p += 3
    elif n == 0xF:
        print(out + "          END ($7E:0000 = 1)"); p += 1
    else:
        print(out + "?? unreachable"); p += 1
