#!/usr/bin/env python
# Disassemble code that lives in WRAM (the $C3 overlay decompresses 34 KB to $7E:3000, and
# tools/dis.py -- a ROM disassembler -- cannot read it). Reuses dis.py's decoder verbatim by
# swapping its ROM buffer for a 128 KB WRAM image (the `.wram` sidecar --fb/--peek write:
# $7E at offset 0, $7F at 0x10000), so the listings match dis.py's byte for byte.
#
#   python tools/wdis.py <wram.bin> <hexaddr> <len> [m] [x]
#     <hexaddr>  $7E-relative (e.g. 5BCF = $7E:5BCF); >= 0x10000 reads the $7F page.
#     m,x        initial widths (0 = 16-bit, 1 = 8-bit); REP/SEP toggle them as decoded.
#
# ⚠ JSR/JMP labels print as "C0/xxxx" (dis.py derives the bank from the offset); inside the
# overlay they are $7E addresses. Linear decoder: M/X can desync after an unfollowed branch.
import importlib.util, os, sys

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("_ct_dis", os.path.join(_here, "dis.py"))
_dis = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_dis)

if __name__ == "__main__":
    _dis.ROM = open(sys.argv[1], "rb").read()
    a = int(sys.argv[2], 16)
    n = int(sys.argv[3], 0)
    m = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    x = int(sys.argv[5]) if len(sys.argv) > 5 else 1
    _dis.dis(a, n, m, x)
