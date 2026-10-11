#!/usr/bin/env python
r"""Translate a stage's 65816 PROGRAM into C -- src/xlat/<name>.c, driven by src/seq_xlat.h.

User decision 2026-09-14 (memory: ct-seq-race-translation-decision): a $C3 sequence stage whose output depends on
the CPU CLOCK (the motorcycle race: its bike frames land in VRAM when the main loop's decompression finishes, and
its transition reads the PPU's H/V counters) is ported as its program translated instruction by instruction, over
the VENDORED snes.c/dma.c/ppu.c clock. The program -- which instruction sits at which address, and where control
goes -- is C; each instruction's semantics and bus timing are cpu.c's (cpu_xlatExec), so the clock the program
drives is the cartridge's.

Where the instructions come from:
  * every instruction a `ct.exe --cputrace` run EXECUTED (CPUTRACE_UNIQUE=1 files): the bytes and the M/X widths the
    CPU really had, so no boundary is guessed;
  * plus a STATIC extension INSIDE routines: the untaken side of each executed branch is decoded from the ROM / the
    WRAM image with the widths of the branch, until it rejoins executed code, returns or jumps (48 instructions at
    most). Calls into routines that never ran are not decoded. A wrong static boundary cannot pass silently: the
    runtime checks every boundary and every opcode byte, and an address with no translation stops the run.

  python tools/gen/xlat.py --name seq_c3 --wram <128KB wram> --trace <unique trace> [--frames A-B] \
      [--from C3:0000] [--stop C0:0000] [--rom Chrono_Trigger_USA_SNES.sfc]

The five files in src/xlat/ are made from the user's ROM by tools/gen/xlat_all.py: it runs the oracle traces from the
recipe states (tools/recipes/) or the captures they still need, then this tool with the options each file was made
with (tools/gen/manifest.json; `python tools/setup.py --check` proves them byte for byte).

`--trace` may be given more than once (the union is translated). `--from K:PPPP` starts collecting at the first
execution of that address, so a trace may begin in the host's code -- and
collecting re-arms after every `--stop`, so a routine the host calls once a frame (the window wipe, C3:000E -> C0:22FE)
is gathered from every call. `--stop K:PPPP` marks addresses where the program hands control back to the host (the race's `JML $C0:0000`): the
translation returns CT_XLAT_EXIT when control reaches one.
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))      # tools/
import disasm65816 as D
from ctlib import rom as romlib

NL = chr(10)
LINE = re.compile(r'^([0-9A-F]{2}):([0-9A-F]{4}) ((?:[0-9A-F?]{2}){4}) .* P=(\S{8})')

BRANCH = {0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0}
ENDS = {0x60, 0x6B, 0x40, 0x6C, 0x7C, 0xDC, 0x80, 0x82, 0x4C, 0x5C, 0x00, 0x02, 0xDB, 0xCB, 0x42}
CALLS = {0x20, 0x22}


def main():
    a = sys.argv[1:]
    opt = {'--rom': 'Chrono_Trigger_USA_SNES.sfc', '--frames': None, '--stop': [], '--from': []}
    traces = []
    i = 0
    while i < len(a):
        if a[i] == '--trace': traces.append([a[i + 1], None]); i += 2; continue
        if a[i] == '--wram' and traces: traces[-1][1] = a[i + 1]; i += 2; continue
        if a[i] == '--stop': opt['--stop'].append(a[i + 1]); i += 2; continue
        if a[i] == '--from': opt['--from'].append(a[i + 1]); i += 2; continue
        opt[a[i]] = a[i + 1]; i += 2
    name = opt['--name']
    rom = romlib.load(opt['--rom'])                          # verified: only the supported dump
    # Each trace decodes its static extension from ITS OWN WRAM image (`--trace T --wram W` pairs): two stages share
    # the overlay's code but not the data around it. A lone `--wram` before any `--trace` serves every trace.
    images = {}
    for t in traces:
        wp = t[1] or opt.get('--wram')
        if wp not in images: images[wp] = open(wp, 'rb').read()
    wram = None
    fa, fb = (0, 1 << 30) if not opt['--frames'] else [int(v) for v in opt['--frames'].split('-')]
    stops = {int(s.replace(':', ''), 16) for s in opt['--stop']}

    def mem(adr):
        """A byte of the program as the static decoder sees it (None where there is no code store)."""
        k, p = adr >> 16, adr & 0xFFFF
        wram = images[cur_image[0]]
        if k in (0x7E, 0x7F): return wram[((k & 1) << 16) | p]
        if (k < 0x40 or 0x80 <= k < 0xC0) and p < 0x2000: return wram[p]
        if p >= 0x8000 or (k & 0x7F) >= 0x40: return rom[(((k & 0x3F) << 16) | p) % len(rom)]
        return None

    def ram_code(adr):
        k, p = adr >> 16, adr & 0xFFFF
        return k in (0x7E, 0x7F) or ((k < 0x40 or 0x80 <= k < 0xC0) and p < 0x2000)

    # ---- the executed instructions -----------------------------------------------------------------------------
    sites = {}      # adr -> dict(op, bytes(tuple), m, x, traced, smc)
    frame = 0
    starts = {int(f.replace(':', ''), 16) for f in opt['--from']}   # every entry the host calls (a routine with two)
    start = starts if starts else None
    cur_image = [None]
    for path, wp in traces:
        wp = wp or opt.get('--wram')
        live = start is None                                  # --from K:PPPP: the program begins at its first run
        for ln in open(path):
            if ln.startswith('-- frame'):
                frame = int(ln.split()[2]); continue
            if not (fa <= frame <= fb): continue
            mo = LINE.match(ln)
            if not mo: continue
            adr = (int(mo.group(1), 16) << 16) | int(mo.group(2), 16)
            if not live:
                if adr not in starts: continue
                live = True
            if adr in stops:                                  # ...and ends where it hands control back --
                if start is None: break                       #   then, with --from, waits for the next entry
                live = False; continue                        #   (a routine called many times: every call's paths)
            raw = mo.group(3)
            if '?' in raw[:2]: continue
            p = mo.group(4)
            m = 1 if p[2] == 'M' else 0
            x = 1 if p[3] == 'X' else 0
            op = int(raw[:2], 16)
            mnem, mode = D.TABLE[op]
            ln_ = 1 + D.oplen(mode, m, x)
            b = tuple(int(raw[2 * j:2 * j + 2], 16) for j in range(min(ln_, 4)))
            s = sites.get(adr)
            if s is None:
                sites[adr] = dict(op=op, bytes=b, len=ln_, m=m, x=x, traced=True, smc=False, variants={b}, image=wp)
            else:
                if s['op'] != op:
                    sys.exit('xlat: REFUSING -- %06X executed two different opcodes (%02X, %02X)' % (adr, s['op'], op))
                s['variants'].add(b)
                if len(s['variants']) > 1: s['smc'] = True
                if ln_ != s['len']:
                    sys.exit('xlat: REFUSING -- %06X ran with two instruction lengths' % adr)

    for st in stops: sites.pop(st, None)                      # an exit is the host's, not the program's
    traced = set(sites)

    # ---- the static extension -----------------------------------------------------------------------------------
    def decode(adr, m, x):
        b0 = mem(adr)
        if b0 is None: return None
        mnem, mode = D.TABLE[b0]
        ln_ = 1 + D.oplen(mode, m, x)
        b = []
        for j in range(ln_):
            v = mem(adr + j)
            if v is None: return None
            b.append(v)
        return b0, tuple(b), ln_

    def succ(adr, s):
        """(targets, falls-through, widths-after) of one instruction."""
        op, b, ln_ = s['op'], s['bytes'], s['len']
        k, pc = adr >> 16, adr & 0xFFFF
        m, x = s['m'], s['x']
        if op == 0xC2:
            if b[1] & 0x20: m = 0
            if b[1] & 0x10: x = 0
        if op == 0xE2:
            if b[1] & 0x20: m = 1
            if b[1] & 0x10: x = 1
        nxt = (k << 16) | ((pc + ln_) & 0xFFFF)
        tg = []
        if op in BRANCH or op == 0x80:
            rel = b[1] - 256 if b[1] >= 0x80 else b[1]
            tg.append((k << 16) | ((pc + 2 + rel) & 0xFFFF))
        if op == 0x82:
            rel = (b[1] | b[2] << 8); rel -= 0x10000 if rel >= 0x8000 else 0
            tg.append((k << 16) | ((pc + 3 + rel) & 0xFFFF))
        if op in (0x20, 0x4C): tg.append((k << 16) | b[1] | (b[2] << 8))
        if op in (0x22, 0x5C): tg.append(b[1] | (b[2] << 8) | (b[3] << 16))
        falls = op not in ENDS
        return tg, falls, nxt, m, x

    # Only INSIDE a routine: the untaken side of a branch, followed until it rejoins executed code, returns or jumps
    # -- at most STATIC_RUN instructions a chain. A call or jump into a routine that never ran is not decoded (its
    # widths on entry are not known here); reaching one at run time stops with CT_XLAT_MISS and its address.
    STATIC_RUN = 48
    work = []
    for adr, s in list(sites.items()):
        tg, falls, nxt, m, x = succ(adr, s)
        if s['op'] in BRANCH:
            for t in tg: work.append((t, m, x, 0, s['image']))
            if falls: work.append((nxt, m, x, 0, s['image']))
    static = 0
    while work:
        adr, m, x, depth, img = work.pop()
        if adr in sites or adr in stops or depth >= STATIC_RUN: continue
        cur_image[0] = img
        d = decode(adr, m, x)
        if d is None: continue
        op, b, ln_ = d
        if op in (0x00, 0x02, 0xDB, 0xCB, 0x42): continue     # not code a stage runs: leave the miss to the runtime
        sites[adr] = dict(op=op, bytes=b, len=ln_, m=m, x=x, traced=False, smc=False, variants={b}, image=img)
        static += 1
        if op == 0x28 or op == 0xFB: continue                  # PLP / XCE: the widths after are not known here
        tg, falls, nxt, m2, x2 = succ(adr, sites[adr])
        if op in BRANCH or op in (0x80, 0x82):
            for t in tg: work.append((t, m2, x2, depth + 1, img))
        if falls: work.append((nxt, m2, x2, depth + 1, img))

    # ---- emit ---------------------------------------------------------------------------------------------------
    out = []
    w = out.append
    w('// GENERATED by tools/gen/xlat.py -- do not edit. Regenerate from the oracle trace; see the tool\'s docstring.')
    w('// %d instructions (%d executed on the oracle, %d decoded statically), %d with a self-modified operand.'
      % (len(sites), len(traced), static, sum(1 for s in sites.values() if s['smc'])))
    w('// Each label is one instruction at its K:PPPP. The program is here; the semantics and the clock are cpu.c\'s.')
    w('#include "../seq_xlat.h"')
    w('')
    w('int CtXlat_%s(CtXlat* x) {' % name)
    w('  Cpu* c = x->cpu;')
    w('dispatch:')
    w('  switch (((uint32_t)c->k << 16) | c->pc) {')
    for adr in sorted(sites):
        w('    case 0x%06X: goto L_%06X;' % (adr, adr))
    for adr in sorted(stops):
        w('    case 0x%06X: return CT_XLAT_EXIT;' % adr)
    w('    default: { int r_ = ctXlatInterp(x, 1); if (r_) return r_; goto dispatch; }')
    w('  }')
    for adr in sorted(sites):
        s = sites[adr]
        op, b, ln_ = s['op'], s['bytes'], s['len']
        k, pc = adr >> 16, adr & 0xFFFF
        mnem, mode = D.TABLE[op]
        text, tgt, reg, = D.fmt(mnem, mode, k, pc, list(b[1:]), s['m'], s['x'], 0)
        if mode in ('rel', 'rell'): text = '%s ->%04X' % (mnem, tgt & 0xFFFF)
        cm = text + ('  ; ' + reg if reg else '') + ('' if s['traced'] else '  [static]') + ('  [SMC operand]' if s['smc'] else '')
        verify = ram_code(adr) and not s['smc']
        pad = list(b) + [0] * (4 - len(b))
        if verify:
            ex = 'X_EXEC_RAM(0x%02X, %d, 0x%02X, 0x%02X, 0x%02X);' % (op, ln_, pad[1], pad[2], pad[3])
        else:
            ex = 'X_EXEC(0x%02X);' % op
        tg, falls, nxt, _, _ = succ(adr, s)
        flow = ''
        if tg:
            t = tg[0]
            if t in sites: flow = ' if (X_PC == 0x%06X) goto L_%06X;' % (t, t)
            elif t in stops: flow = ' if (X_PC == 0x%06X) return CT_XLAT_EXIT;' % t
        if not falls:
            flow += ' goto dispatch;'
        elif op in (0x54, 0x44):                              # MVN/MVP repeat themselves byte by byte
            flow += ' if (X_PC == 0x%06X) goto L_%06X;' % (adr, adr)
        elif nxt not in sites:
            flow += ' goto dispatch;'
        w('L_%06X: X_BOUNDARY(0x%06X); %s%s  // %s' % (adr, adr, ex, flow, cm))
    w('}')
    w('')
    os.makedirs('src/xlat', exist_ok=True)
    path = 'src/xlat/%s.c' % name
    open(path, 'w', newline=NL).write(NL.join(out))
    print('xlat: %s -- %d instructions (%d executed, %d static, %d SMC)'
          % (path, len(sites), len(traced), static, sum(1 for s in sites.values() if s['smc'])))


if __name__ == '__main__':
    main()
