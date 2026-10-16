#!/usr/bin/env python3
r"""An action's STAGING, write for write against the rule's cartridge -- C1/4833-48EB, frame by frame (src/battle.c
battleFxActionLoadFrame): the caster's sheets (CD/0015 / CD/002A), the effect load CD/0018 -> CD/0D28 on its frames,
the arm (CD/0D33), and the frame table after it (C1/48EC, C1/486A, C1/4943 x8, C1/48B3).

    python tools/fx_load_stage_cmp.py [char k last_rel] ...     (default: 6 7 45  2 7 45 -- Dark Matter, Flare)

Each tech is tools/battle_ab.py's synthetic battle (gato_wait made into a solo party by pokes, the tech driven from
the menu): the cartridge on CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1 with `--monitor` of every byte the staging
writes, the port with CT_FIELD_MEMMON of the same, both aligned on the action's own $A013 rise (rel 0). Each region is
graded frame by frame from the first rel either side writes it to last_rel -- before that both hold their history from
earlier actions (the fill area, an earlier frame table), which is not the staging's -- and from rel 1 at the earliest:
the port's PC dispatch opens the action after the staging's pass in its frame, so rel 0's CD/1323 lands on rel 1.
Prints per tech `fx-load-stage: char C k K (fx FX): N regions (M written by the staging), F region-frames on rels 1-R,
all the cartridge's`, or each divergent region's first differing rel and values. The effect-load frames the CPU work spills over on the cartridge (CD/107D's
blocks, CD/1052's fill; battle_fx_program_census.md sec. 8) are NOT ported -- a layout that spills would move every
write after it -- so the defaults are single-block effects (0 spill frames on gato_wait's battle).
"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '.')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import importlib.util
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
_spec = importlib.util.spec_from_file_location('battle_ab', os.path.join(ROOT, 'tools', 'battle_ab.py'))
ab = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ab)
OUT = os.path.join(ROOT, 'build', '_fx_load_stage')

# every byte the staging writes that the program's first frames do not (the effect's first op is at rel 29 + w)
REG = [(0xCACE, 3), (0xCC9D, 4), (0x2D00, 16), (0x2FE0, 4), (0xCCA2, 1), (0xCD28, 1), (0xCD3C, 1), (0xCC91, 6),
       (0xCD38, 4), (0xBE89, 4), (0xBE91, 2), (0xBC87, 1), (0xBE99, 1), (0xCD0C, 1), (0xCD04, 1), (0xC9FC, 6),
       (0xCA1B, 2), (0xCA33, 2), (0xCD1F, 4), (0xCA93, 4), (0xD000, 4), (0xD7FE, 2), (0x2032, 6), (0x203A, 6),
       (0xCA32, 1), (0xCA42, 1), (0xA280, 4), (0xA2A0, 4), (0x0306, 2), (0xA650, 4), (0xA05B, 8), (0x4500, 8),
       (0x5D00, 8), (0xA07B, 4), (0x5D9B, 1), (0xA013, 1)]


def oracle(pk, inp, regs):
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1', PEEK_POKE=pk)
    mon = ','.join('7E%04X:%d' % r for r in regs)
    return subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--monitor', mon, '--from', ab.STATE, '--input', inp,
                           '--diff'], env=env, capture_output=True, text=True, errors='replace', cwd=ROOT).stdout


def port(c, k, pokes, drive, total):
    w = bytearray(open(os.path.join(ROOT, ab.STATE), 'rb').read()[0x45:0x45 + 0x20000])
    for a, v in pokes.items(): w[a] = v
    lw = os.path.join(OUT, 'stage_%d_%d.lakewram' % (c, k))
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + bytes(w))
    start = 220
    for attempt in range(2):
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
                   CT_BATTLE_SCRIPT_ABS='1', CT_BATTLE_SCRIPT=ab.port_script(drive, start, total), CT_BATTLE_TRACE='1',
                   CT_FIELD_MEMMON=','.join('%04X:%X' % r for r in REG), CT_SHOTS=os.path.join(OUT, 'stage_p'),
                   CT_SHOTS_AT=str(total), CT_SHOTS_EXIT='1')
        r = subprocess.run([os.path.join(ROOT, 'build', 'ct_field.exe'), '--assets', 'ct_assets.dat', '--scene', '6',
                            '--loadwram', lw, '--walkscript', 'a:8'], env=env, capture_output=True, text=True,
                           errors='replace', cwd=ROOT)
        out = r.stdout
        # The battle's frame 0 on the field's clock: src/field.c prints it on STDERR, and only under CT_BATTLE_TRACE (read
        # from stdout it was never found and `start` stayed 220 -- see tools/battle_ab.py run()).
        m = re.search(r'battlephase: begins at field f(\d+)', r.stderr)
        if not m or int(m.group(1)) == start: return out
        start = int(m.group(1))
    return out


def rows(text, rx):
    out = {}
    for l in text.splitlines():
        m = re.match(rx, l)
        if not m: continue
        d = out.setdefault(int(m.group(1)), {})
        d.update({int(a, 16) & 0xFFFF: v for a, v in re.findall(r'\b(?:7E)?([0-9A-F]{4})=([0-9A-F]+)', m.group(2))})
    return out


def carry(rw):
    fs = sorted(rw); cur = {}; out = {}
    for f in range(fs[0], fs[-1] + 1):
        if f in rw: cur.update(rw[f])
        out[f] = dict(cur)
    return out


def first_write(rw, up, last, a):
    """The first rel in 0..last at which region a's value changes from the frame before (None: never)."""
    for r in range(0, last + 1):
        v, pv = rw.get(up + r, {}).get(a), rw.get(up + r - 1, {}).get(a)
        if v is not None and v != pv: return r
    return None


def graded(cr, uc, pr, up, last, a):
    """Region a, frame by frame from the first rel either side writes it (max 1) to `last`: [(rel, cart, port)] that
    differ, and the number of frames graded. Before that first write both sides hold their history, not the staging's."""
    fs = [r for r in (first_write(cr, uc, last, a), first_write(pr, up, last, a)) if r is not None]
    if not fs: return [], 0
    bad = []; n = 0
    for r in range(max(1, min(fs)), last + 1):
        cv, pv = cr.get(uc + r, {}).get(a), pr.get(up + r, {}).get(a)
        if cv is None or pv is None: continue
        n += 1
        if cv != pv: bad.append((r, cv, pv))
    return bad, n


def one(c, k, last):
    pokes = ab.pokes_for([c]); drive = ab.drive_tech(k); total = drive[-1][0] + last + 60
    pk = ','.join('%X=%02X' % (a, v) for a, v in sorted(pokes.items()))
    inp = ab.oracle_input(drive, total)
    o = oracle(pk, inp, REG[:30]) + '\n' + oracle(pk, inp, REG[30:])
    p = port(c, k, pokes, drive, total)
    cr = carry(rows(o, r'\s*f(\d+)\s+(.*)')); pr = carry(rows(p, r'fmemmon f(\d+) loc=\w+\s+(.*)'))
    after = drive[-1][0] - 4
    rise = lambda rw: min((f for f in rw if f >= after and int(rw[f].get(0xA013, '0'), 16) == 1), default=None)
    uc, up = rise(cr), rise(pr)
    tid = ab.tech_id(c, k)
    fx = _romlib.load(os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc'))[0x0D45A6 + tid * 7 + 6]
    if uc is None or up is None:
        print('fx-load-stage: char %d k %d (fx %02X): FAIL -- no $A013 rise (cartridge %s, port %s)' % (c, k, fx, uc, up))
        return
    res = {a: graded(cr, uc, pr, up, last, a) for a, n in REG}
    nf = sum(n for b, n in res.values())
    nwr = sum(1 for a, n in REG if first_write(cr, uc, last, a) is not None)
    bad = [(a, res[a][0]) for a, n in REG if res[a][0]]
    if not bad:
        print('fx-load-stage: char %d k %d (fx %02X): %d regions (%d written by the staging), %d region-frames on rels '
              '1-%d, all the cartridge\'s' % (c, k, fx, len(REG), nwr, nf, last))
        return
    print('fx-load-stage: char %d k %d (fx %02X): %d of %d regions differ' % (c, k, fx, len(bad), len(REG)))
    for a, b in bad[:8]:
        print('fx-load-stage:   $%04X from rel %d: cartridge %s port %s (%d frames)' % (a, b[0][0], b[0][1], b[0][2], len(b)))


def main():
    os.makedirs(OUT, exist_ok=True)
    a = [int(x) for x in sys.argv[1:]]
    jobs = []
    if not a: jobs = [(6, 7, 45), (2, 7, 45)]
    if len(a) % 3: sys.exit(__doc__)
    jobs += [tuple(a[i:i + 3]) for i in range(0, len(a), 3)]
    for c, k, last in jobs:
        one(c, k, last)


if __name__ == '__main__':
    main()
