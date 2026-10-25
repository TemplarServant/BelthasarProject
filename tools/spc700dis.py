#!/usr/bin/env python3
r"""A disassembler for the SPC700, the sound processor -- to read the Akao sound driver that runs on it.

    python tools/spc700dis.py driver [--from ADDR] [--to ADDR] [--follow ADDR,...] [--table ADDR:COUNT,...]
                                     [--coverage FILE]
    python tools/spc700dis.py lengths          the opcode table's instruction lengths, one per opcode (for tests)
    python tools/spc700dis.py report --coverage 'FILE,GLOB*,...'   what the union of coverage files ran: the code,
                                     and each dispatch table's entries (DISPATCH below) -- run or not

`driver` reads boot block 0 of the upload at $C7:0000 from your ROM (the source and destination tables at $C7:0010 /
$C7:001C, each block a size word then its bytes: disasm/notes/audio_boot.md) and lists it at its ARAM address ($0200).
Nothing of the driver is kept in this file or in the repository: the listing is made from your ROM each time.

  --from / --to     the ARAM range listed linearly (default: the whole block)
  --follow          list only what is reachable from these entry points (branches, jumps, calls followed; a
                    `PUSH A / PUSH A / RET` style dispatch is not, so give its tables with --table)
  --table           word tables of code addresses (ADDR = the table, COUNT = words): each word is an entry point
  --coverage        a file of `ADDR COUNT` lines (CT_SPC_COVERAGE, src/audio.c): executed instructions are marked
                    with their count, the rest with `.`

The opcode table is the SPC700's (Sony's mnemonics, as fullsnes writes them). Its lengths are graded against the
vendored core (tests/spc700_lengths.c runs every non-control-flow opcode on snes/spc.c and compares the PC it leaves).
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

# Operand formats. d = direct page byte, a = absolute word, i = immediate, r = relative branch (signed byte),
# mb = 13-bit address + 3-bit bit, ds/dd = two direct page bytes (the SOURCE comes first in the instruction).
# Each entry: (mnemonic with {..} slots, operand kinds in byte order). A slot name is the kind it formats.
OPS = {}


def op(code, text, *kinds):
    OPS[code] = (text, kinds)


_ROWS8 = ['OR', 'AND', 'EOR', 'CMP', 'ADC', 'SBC']
for k, m in enumerate(_ROWS8):                                       # the x4-x8 ALU columns, A as the destination
    base = k * 0x20
    op(base + 0x04, m + ' A,{d}', 'd')
    op(base + 0x14, m + ' A,{d}+X', 'd')
    op(base + 0x05, m + ' A,{a}', 'a')
    op(base + 0x15, m + ' A,{a}+X', 'a')
    op(base + 0x06, m + ' A,(X)')
    op(base + 0x16, m + ' A,{a}+Y', 'a')
    op(base + 0x07, m + ' A,[{d}+X]', 'd')
    op(base + 0x17, m + ' A,[{d}]+Y', 'd')
    op(base + 0x08, m + ' A,#{i}', 'i')
    op(base + 0x18, m + ' {d},#{i}', 'i', 'd')
    op(base + 0x09, m + ' {dd},{ds}', 'ds', 'dd')
    op(base + 0x19, m + ' (X),(Y)')
# the MOV columns of rows C-F
op(0xC4, 'MOV {d},A', 'd');        op(0xD4, 'MOV {d}+X,A', 'd')
op(0xC5, 'MOV {a},A', 'a');        op(0xD5, 'MOV {a}+X,A', 'a')
op(0xC6, 'MOV (X),A');             op(0xD6, 'MOV {a}+Y,A', 'a')
op(0xC7, 'MOV [{d}+X],A', 'd');    op(0xD7, 'MOV [{d}]+Y,A', 'd')
op(0xC8, 'CMP X,#{i}', 'i');       op(0xD8, 'MOV {d},X', 'd')
op(0xC9, 'MOV {a},X', 'a');        op(0xD9, 'MOV {d}+Y,X', 'd')
op(0xE4, 'MOV A,{d}', 'd');        op(0xF4, 'MOV A,{d}+X', 'd')
op(0xE5, 'MOV A,{a}', 'a');        op(0xF5, 'MOV A,{a}+X', 'a')
op(0xE6, 'MOV A,(X)');             op(0xF6, 'MOV A,{a}+Y', 'a')
op(0xE7, 'MOV A,[{d}+X]', 'd');    op(0xF7, 'MOV A,[{d}]+Y', 'd')
op(0xE8, 'MOV A,#{i}', 'i');       op(0xF8, 'MOV X,{d}', 'd')
op(0xE9, 'MOV X,{a}', 'a');        op(0xF9, 'MOV X,{d}+Y', 'd')
# column 0: flags and branches
for c, t in [(0x00, 'NOP'), (0x20, 'CLRP'), (0x40, 'SETP'), (0x60, 'CLRC'), (0x80, 'SETC'), (0xA0, 'EI'), (0xC0, 'DI'),
             (0xE0, 'CLRV')]:
    op(c, t)
for c, t in [(0x10, 'BPL'), (0x30, 'BMI'), (0x50, 'BVC'), (0x70, 'BVS'), (0x90, 'BCC'), (0xB0, 'BCS'), (0xD0, 'BNE'),
             (0xF0, 'BEQ')]:
    op(c, t + ' {r}', 'r')
for n in range(16):
    op(n * 0x10 + 0x01, 'TCALL %d' % n)                                # column 1
    if n % 2 == 0:                                                       # columns 2 and 3: bit set/clear, bit branches
        op(n * 0x10 + 0x02, 'SET1 {d}.%d' % (n // 2), 'd')
        op(n * 0x10 + 0x03, 'BBS {d}.%d,{r}' % (n // 2), 'd', 'r')
    else:
        op(n * 0x10 + 0x02, 'CLR1 {d}.%d' % (n // 2), 'd')
        op(n * 0x10 + 0x03, 'BBC {d}.%d,{r}' % (n // 2), 'd', 'r')
# column A
op(0x0A, 'OR1 C,{mb}', 'mb');      op(0x1A, 'DECW {d}', 'd')
op(0x2A, 'OR1 C,/{mb}', 'mb');     op(0x3A, 'INCW {d}', 'd')
op(0x4A, 'AND1 C,{mb}', 'mb');     op(0x5A, 'CMPW YA,{d}', 'd')
op(0x6A, 'AND1 C,/{mb}', 'mb');    op(0x7A, 'ADDW YA,{d}', 'd')
op(0x8A, 'EOR1 C,{mb}', 'mb');     op(0x9A, 'SUBW YA,{d}', 'd')
op(0xAA, 'MOV1 C,{mb}', 'mb');     op(0xBA, 'MOVW YA,{d}', 'd')
op(0xCA, 'MOV1 {mb},C', 'mb');     op(0xDA, 'MOVW {d},YA', 'd')
op(0xEA, 'NOT1 {mb}', 'mb');       op(0xFA, 'MOV {dd},{ds}', 'ds', 'dd')
# column B: shifts and inc/dec on direct page, Y moves
for k, m in enumerate(['ASL', 'ROL', 'LSR', 'ROR', 'DEC', 'INC']):
    op(k * 0x20 + 0x0B, m + ' {d}', 'd')
    op(k * 0x20 + 0x1B, m + ' {d}+X', 'd')
    op(k * 0x20 + 0x0C, m + ' {a}', 'a')
    op(k * 0x20 + 0x1C, m + ' A')
op(0xCB, 'MOV {d},Y', 'd');        op(0xDB, 'MOV {d}+X,Y', 'd')
op(0xEB, 'MOV Y,{d}', 'd');        op(0xFB, 'MOV Y,{d}+X', 'd')
op(0xCC, 'MOV {a},Y', 'a');        op(0xDC, 'DEC Y')
op(0xEC, 'MOV Y,{a}', 'a');        op(0xFC, 'INC Y')
# column D
for c, t in [(0x0D, 'PUSH PSW'), (0x1D, 'DEC X'), (0x2D, 'PUSH A'), (0x3D, 'INC X'), (0x4D, 'PUSH X'), (0x5D, 'MOV X,A'),
             (0x6D, 'PUSH Y'), (0x7D, 'MOV A,X'), (0x9D, 'MOV X,SP'), (0xBD, 'MOV SP,X'), (0xDD, 'MOV A,Y'),
             (0xED, 'NOTC'), (0xFD, 'MOV Y,A')]:
    op(c, t)
op(0x8D, 'MOV Y,#{i}', 'i');       op(0xAD, 'CMP Y,#{i}', 'i');      op(0xCD, 'MOV X,#{i}', 'i')
# column E
op(0x0E, 'TSET1 {a}', 'a');        op(0x1E, 'CMP X,{a}', 'a')
op(0x2E, 'CBNE {d},{r}', 'd', 'r'); op(0x3E, 'CMP X,{d}', 'd')
op(0x4E, 'TCLR1 {a}', 'a');        op(0x5E, 'CMP Y,{a}', 'a')
op(0x6E, 'DBNZ {d},{r}', 'd', 'r'); op(0x7E, 'CMP Y,{d}', 'd')
op(0x8E, 'POP PSW');               op(0x9E, 'DIV YA,X')
op(0xAE, 'POP A');                 op(0xBE, 'DAS A')
op(0xCE, 'POP X');                 op(0xDE, 'CBNE {d}+X,{r}', 'd', 'r')
op(0xEE, 'POP Y');                 op(0xFE, 'DBNZ Y,{r}', 'r')
# column F
op(0x0F, 'BRK');                   op(0x1F, 'JMP [{a}+X]', 'a')
op(0x2F, 'BRA {r}', 'r');          op(0x3F, 'CALL {a}', 'a')
op(0x4F, 'PCALL {u}', 'u');        op(0x5F, 'JMP {a}', 'a')
op(0x6F, 'RET');                   op(0x7F, 'RETI')
op(0x8F, 'MOV {d},#{i}', 'i', 'd'); op(0x9F, 'XCN A')
op(0xAF, 'MOV (X)+,A');            op(0xBF, 'MOV A,(X)+')
op(0xCF, 'MUL YA');                op(0xDF, 'DAA A')
op(0xEF, 'SLEEP');                 op(0xFF, 'STOP')

assert len(OPS) == 256, len(OPS)
SIZE = {'d': 1, 'ds': 1, 'dd': 1, 'i': 1, 'r': 1, 'u': 1, 'a': 2, 'mb': 2}
# control flow: (falls through, target operand kinds)
ENDS = {0x5F, 0x1F, 0x6F, 0x7F, 0x2F, 0x0F, 0xEF, 0xFF}               # no fall-through


def length(code):
    return 1 + sum(SIZE[k] for k in OPS[code][1])


def decode(mem, base, pc):
    """(text, length, targets, falls_through) of the instruction at ARAM pc (mem holds ARAM from `base`)."""
    b = lambda a: mem[a - base] if 0 <= a - base < len(mem) else 0
    code = b(pc)
    text, kinds = OPS[code]
    n = length(code)
    vals, p, targets = {}, pc + 1, []
    for k in kinds:
        if k in ('a', 'mb'):
            w = b(p) | b(p + 1) << 8
            p += 2
            vals[k] = '$%04X' % w if k == 'a' else '$%04X.%d' % (w & 0x1FFF, w >> 13)
            if k == 'a' and code in (0x5F, 0x3F):
                targets.append(w)
        else:
            v = b(p)
            p += 1
            if k == 'r':
                t = (pc + n + (v - 256 if v & 0x80 else v)) & 0xFFFF
                vals[k] = '$%04X' % t
                targets.append(t)
            elif k == 'u':
                vals[k] = '$FF%02X' % v
                targets.append(0xFF00 | v)
            else:
                vals[k] = '$%02X' % v
    out = text
    for k, v in vals.items():
        out = out.replace('{%s}' % k, v)
    return out, n, targets, code not in ENDS


def driver_block(rom):
    """Block 0's ARAM base and bytes, from the upload tables at $C7:0010 / $C7:001C (audio_boot.md)."""
    c7 = 0x70000
    src = rom[c7 + 0x10] | rom[c7 + 0x11] << 8
    dst = rom[c7 + 0x1C] | rom[c7 + 0x1D] << 8
    size = rom[c7 + src] | rom[c7 + src + 1] << 8
    return dst, bytes(rom[c7 + src + 2:c7 + src + 2 + size])


# The driver's dispatch tables (PUSH hi / PUSH lo / RET; disasm/notes/audio_spc_driver.md): name -> (ARAM, words,
# the first code the entry stands for)
DISPATCH = {
    'event': (0x1554, 60, 0xC4),        # sequence events $C4-$FF (operand counts at $15CC)
    'lookahead': (0x1608, 60, 0xC4),    # the note look-ahead's events (0 = none)
    'cmd10': (0x1680, 16, 0x10),        # commands $10-$1F
    'cmd10pre': (0x16A0, 16, 0x10),     # ... and the routine run before each (0 = none)
    'cmd80F0': (0x16C0, 32, None),      # commands $80-$8F and $F0-$FF (index = command & $1F)
    'upload': (0x1544, 8, 0),           # upload kinds
}
ENTRY = 0x0200


def load_cov(spec):
    import glob
    cov = {}
    for part in spec.split(','):
        for path in glob.glob(part) or [part]:
            for line in open(path):
                p = line.split()
                if len(p) >= 2:
                    a = int(p[0], 16)
                    cov[a] = cov.get(a, 0) + int(p[1])
    return cov


def table_words(mem, base, adr, n):
    return [mem[adr - base + 2 * k] | mem[adr - base + 2 * k + 1] << 8 for k in range(n)]


def report(mem, base, cov):
    starts = [ENTRY]
    for adr, n, _ in DISPATCH.values():
        starts += [w for w in table_words(mem, base, adr, n) if w]
    seen, todo, nbytes = set(), list(starts), 0
    while todo:
        pc = todo.pop()
        while pc not in seen and base <= pc < base + len(mem):
            seen.add(pc)
            _, n, tg, ft = decode(mem, base, pc)
            nbytes += n
            todo.extend(tg)
            if not ft:
                break
            pc += n
    run = sum(1 for a in seen if a in cov)
    print('spc-driver: %d instructions (%d bytes) reachable from $%04X and the dispatch tables; %d run (%.0f%%)'
          % (len(seen), nbytes, ENTRY, run, 100.0 * run / len(seen)))
    for name, (adr, n, first) in DISPATCH.items():
        ws = table_words(mem, base, adr, n)
        live = [(k, w) for k, w in enumerate(ws) if w]
        miss = [k for k, w in live if w not in cov]
        lab = (lambda k: '%02X' % (first + k)) if first is not None else (lambda k: '%02X' % (0x80 + k if k < 16 else 0xE0 + k))
        print('spc-driver: %-9s $%04X: %d entries, %d run; not run: %s'
              % (name, adr, len(live), len(live) - len(miss), ' '.join(lab(k) for k in miss) or '-'))


def listing(mem, base, starts, cov, linear):
    lines = {}
    if linear is not None:
        pc, end = linear
        while pc < end:
            t, n, _, _ = decode(mem, base, pc)
            lines[pc] = (t, n)
            pc += n
    else:
        todo, seen = list(starts), set()
        while todo:
            pc = todo.pop()
            while pc not in seen and base <= pc < base + len(mem):
                seen.add(pc)
                t, n, tg, ft = decode(mem, base, pc)
                lines[pc] = (t, n)
                todo.extend(tg)
                if not ft:
                    break
                pc += n
    for pc in sorted(lines):
        t, n = lines[pc]
        raw = ' '.join('%02X' % mem[pc - base + i] for i in range(n) if pc - base + i < len(mem))
        mark = ('%7d' % cov[pc]) if cov and pc in cov else ('      .' if cov else '')
        print('%s %04X  %-9s %s' % (mark, pc, raw, t))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('what', choices=['driver', 'lengths', 'report'])
    ap.add_argument('--from', dest='frm')
    ap.add_argument('--to')
    ap.add_argument('--follow')
    ap.add_argument('--table')
    ap.add_argument('--coverage')
    a = ap.parse_args()
    if a.what == 'lengths':
        print(' '.join(str(length(c)) for c in range(256)))
        return 0
    from ctlib import rom as romlib
    base, mem = driver_block(romlib.load(os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc')))
    cov = load_cov(a.coverage) if a.coverage else {}
    if a.what == 'report':
        report(mem, base, cov)
        return 0
    if a.follow or a.table:
        starts = [int(x, 16) for x in a.follow.split(',')] if a.follow else []
        for t in (a.table.split(',') if a.table else []):
            adr, cnt = t.split(':')
            adr = int(adr, 16)
            for k in range(int(cnt)):
                starts.append(mem[adr - base + 2 * k] | mem[adr - base + 2 * k + 1] << 8)
        listing(mem, base, starts, cov, None)
    else:
        lo = int(a.frm, 16) if a.frm else base
        hi = int(a.to, 16) if a.to else base + len(mem)
        listing(mem, base, [], cov, (lo, hi))
    return 0


if __name__ == '__main__':
    sys.exit(main())
