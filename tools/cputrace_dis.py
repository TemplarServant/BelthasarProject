#!/usr/bin/env python
# Listing of the code a `ct.exe --cputrace` run actually executed.
#
#   python tools/cputrace_dis.py <trace.txt> [--frames A-B] [--bank 7E] [--range 3000-6FFF] [--calls]
#
# Every (bank, pc) is decoded from the bytes the trace recorded at execution with the M/X flags the CPU
# really had, so widths are never guessed and self-modifying code shows every version it ran as:
# an address whose bytes (or widths) differed between executions prints each variant, marked `*SMC*`.
# Addresses are grouped into runs; a gap in the listing is code that did not execute in the window.
# --calls prints every JSR/JSL/JMP target with the addresses that reached it.
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import disasm65816 as D

LINE = re.compile(r'^([0-9A-F]{2}):([0-9A-F]{4}) ((?:[0-9A-F?]{2}){4}) A=([0-9A-F]{4}) X=([0-9A-F]{4}) '
                  r'Y=([0-9A-F]{4}) S=([0-9A-F]{4}) D=([0-9A-F]{4}) B=([0-9A-F]{2}) P=(\S{8})')

def decode(bank, pc, raw, m, x, dp):
    b = [int(raw[i:i + 2], 16) if raw[i:i + 2] != '??' else 0 for i in range(0, 8, 2)]
    mnem, mode = D.TABLE[b[0]]
    ln = D.oplen(mode, m, x)
    text, tgt, reg = D.fmt(mnem, mode, bank, pc, b[1:1 + ln], m, x, dp)
    if mode == 'rel' or mode == 'rell':
        text = '%s ->%04X' % (mnem, tgt & 0xFFFF)
    return ' '.join('%02X' % v for v in b[:1 + ln]), text, tgt, reg, mnem, mode

def main():
    a = sys.argv[1:]
    path = a[0]
    fa, fb = 0, 1 << 30
    bank = None
    ra, rb = 0, 0xFFFF
    calls = '--calls' in a
    for i, t in enumerate(a):
        if t == '--frames': fa, fb = [int(v) for v in a[i + 1].split('-')]
        if t == '--bank': bank = int(a[i + 1], 16)
        if t == '--range': ra, rb = [int(v, 16) for v in a[i + 1].split('-')]
    frame = 0
    seen = {}      # (bank, pc) -> {(raw, m, x): count}
    dps = {}
    callers = {}
    for ln in open(path):
        if ln.startswith('-- frame'):
            frame = int(ln.split()[2]); continue
        if not (fa <= frame <= fb): continue
        mo = LINE.match(ln)
        if not mo: continue
        k = int(mo.group(1), 16); pc = int(mo.group(2), 16)
        p = mo.group(10)
        m = 1 if p[2] == 'M' else 0
        x = 1 if p[3] == 'X' else 0
        dp = int(mo.group(8), 16)
        raw = mo.group(3)
        if calls:
            op = int(raw[:2], 16)
            if op in (0x20, 0x22, 0x4C, 0x5C, 0xFC, 0x7C, 0x6C, 0xDC):
                _, text, tgt, _, _, _ = decode(k, pc, raw, m, x, dp)
                key = tgt if tgt is not None else ('%02X:%04X %s' % (k, pc, text))
                callers.setdefault(key, set()).add((k, pc, text))
        if bank is not None and k != bank: continue
        if not (ra <= pc <= rb): continue
        # only the bytes the instruction really has matter for the variant key
        mnem, mode = D.TABLE[int(raw[:2], 16)]
        raw = raw[:2 * (1 + D.oplen(mode, m, x))]
        seen.setdefault((k, pc), {}).setdefault((raw, m, x), 0)
        seen[(k, pc)][(raw, m, x)] += 1
        dps.setdefault((k, pc), set()).add(dp)
    prev = None
    for key in sorted(seen):
        k, pc = key
        if prev is not None and (prev[0] != k or pc > prev[1] + 4):
            print()
        vs = seen[key]
        smc = len({r for (r, m, x) in vs}) > 1
        for (raw, m, x), cnt in sorted(vs.items(), key=lambda kv: -kv[1]):
            b, text, tgt, reg, mnem, mode = decode(k, pc, (raw + '????????')[:8], m, x, min(dps[key]))
            nb = len(b.split())
            note = ''
            if reg: note += '  ; ' + reg
            if len(dps[key]) > 1 or min(dps[key]) != 0: note += '  D=' + '/'.join('%04X' % d for d in sorted(dps[key]))
            if smc: note += '  *SMC*'
            print('%02X:%04X  %-12s %-22s m%d x%d  x%-6d%s' % (k, pc, b, text, m, x, cnt, note))
        prev = (k, pc + nb)
    if calls:
        print('\n== call targets ==')
        for t in sorted(callers, key=lambda v: (isinstance(v, str), v)):
            name = ('%02X:%04X' % (t >> 16, t & 0xFFFF)) if isinstance(t, int) else t
            print(name, '<-', ' '.join(sorted('%02X:%04X' % (c[0], c[1]) for c in callers[t])))

if __name__ == '__main__':
    main()
