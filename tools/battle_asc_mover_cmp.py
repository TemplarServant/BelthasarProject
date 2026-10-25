#!/usr/bin/env python3
r"""The PC techs' anim-script movers, port against the rule's cartridge -- per-frame WRAM over each tech's span.

    python tools/battle_asc_mover_cmp.py            # every case below, in parallel
    python tools/battle_asc_mover_cmp.py haste ice  # some of them

The synthetic battle of tools/battle_ab.py (GATO's gato_wait made into a solo party by pokes; the same WRAM on both
sides, the tech picked through the menu), aligned on each side's own `$A013` rise, and the named WRAM read every frame
on both sides: the cartridge `build/ct.exe --monitor` under the rule (CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1), the
port `build/Belthasar.exe` with CT_FIELD_MEMMON (the field host's $7E is the battle's). Each case grades one mechanism of
the bank-CE interpreter (src/battle.c battleAscTick) or its movers (battleAscMoverFrame, battleEffectMoverFrame):

  haste  Marle's Haste (tech 0D): CE/1221 `A8 32` on track 8 -- the effect object's move-forward stops on the arena's
         edge tile (CF/F61F's own test) with its count still at 1B: effect objects $A240/$A250 and counts $A644.
  laser  Robo's Laser Spin (tech 1B): the bound points $5DA0 (C1/4212) follow effect object 0, which a `10 FF 01` slide
         moves -- the movers tick before C1/4212 (CF/EFAF in the NMI), so the points are this frame's.
  kiss   Ayla's Kiss (tech 29): track 8's `15 FF 0C`, the homing move (C1/521C, mover mode 7 CF/F884).
  water  Frog's Water (tech 23): `9C 00 0C` / `9D`, the quarter-goal hop-slides (C1/6549, count x 4) with the effect
         object's own hop (CF/F503), and the resolver's mode 18 (the arena's centre) of `12 18`.
  ice    Marle's Ice (tech 0B): op 01 (C1/4CF7) parks while a ShowDamage is up ($A3D1): the gap from $A3D1's rise to
         `$9876 = 1`, both sides.
  slash  Crono's Slash (tech 02): the whole span (the resolver's modes 12-18 / 32-3F, C1/762E).

Prints per case the two spans, and per WRAM field `identical on all N frames` or where it first differs.
"""
import concurrent.futures
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import battle_ab as B  # noqa: E402

OUT = os.path.join(ROOT, 'build', '_asc_mover')
# name: (char, k, [(label, addr, bytes, stride)], extra regions)
CASES = {
    'haste': (1, 4, [('$A240/$A250 x8', [(0xA240, 16), (0xA250, 16)]), ('$A644 x8', [(0xA644, 8)])]),
    'laser': (3, 2, [('$5DA0 x12', [(0x5DA0, 12)]), ('$A240/$A250 x8', [(0xA240, 16), (0xA250, 16)])]),
    'kiss':  (5, 0, [('$A240/$A250 x8', [(0xA240, 16), (0xA250, 16)])]),
    'water': (4, 2, [('obj%d' % o, [(0xA240 + 2 * o, 1), (0xA250 + 2 * o, 1)]) for o in range(4)]),
    'ice':   (1, 2, []),
    'slash': (0, 1, []),
}
EXTRA = [(0xA013, 1), (0xAE93, 1), (0xA3D1, 1), (0x9876, 1)]


def regions(case):
    regs = list(EXTRA)
    for lab, rs in CASES[case][2]:
        for a, n in rs:
            if (a, n) not in regs: regs.append((a, n))
    return regs


def run(case):
    c, k, fields = CASES[case]
    os.makedirs(OUT, exist_ok=True)
    regs = regions(case)
    pokes, drive, total = B.pokes_for([c]), B.drive_tech(k), 1400
    env = dict(os.environ, **B.RULE, PEEK_POKE=','.join('%X=%02X' % (a, v) for a, v in sorted(pokes.items())))
    o = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--monitor', ','.join('7E%04X:%d' % r for r in regs),
                        '--from', B.STATE, '--input', B.oracle_input(drive, total), '--diff'], env=env, cwd=ROOT,
                       capture_output=True, text=True, errors='replace').stdout
    w = bytearray(open(os.path.join(ROOT, B.STATE), 'rb').read()[0x45:0x45 + 0x20000])
    for a, v in pokes.items(): w[a] = v
    lw = os.path.join(OUT, case + '.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + bytes(w))
    start = 220
    for attempt in range(2):
        penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
                    CT_BATTLE_SCRIPT_ABS='1', CT_BATTLE_SCRIPT=B.port_script(drive, start, total), CT_BATTLE_TRACE='1',
                    CT_FIELD_MEMMON=','.join('%04X:%X' % r for r in regs), CT_SHOTS=os.path.join(OUT, case + '_p'),
                    CT_SHOTS_AT=str(total), CT_SHOTS_EXIT='1')
        r = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '6',
                            '--loadwram', lw, '--walkscript', 'a:8'], env=penv, cwd=ROOT, capture_output=True,
                           text=True, errors='replace')
        p = r.stdout
        # The battle's frame 0 on the field's clock: src/field.c prints it on STDERR, and only under CT_BATTLE_TRACE (read
        # from stdout it was never found and `start` stayed 220 -- see tools/battle_ab.py run()).
        m = re.search(r'battlephase: begins at field f(\d+)', r.stderr)
        if not m or int(m.group(1)) == start: break
        start = int(m.group(1))
    return grade(case, fill(o, r'\s*f(\d+)\s+(.*)'), fill(p, r'fmemmon f(\d+) loc=\w+\s+(.*)'), drive, B.tech_id(c, k))


def fill(text, pre):
    """{frame: {addr: bytes}} with every frame between the first and last line carried forward."""
    rows = {}
    for l in text.splitlines():
        m = re.match(pre, l)
        if m:
            rows[int(m.group(1))] = {int(a, 16) & 0xFFFF: bytes.fromhex(v)
                                     for a, v in re.findall(r'\b(?:7E)?([0-9A-F]{4})=([0-9A-F]+)', m.group(2))}
    out, last = {}, {}
    if rows:
        for f in range(min(rows), max(rows) + 1):
            if f in rows: last.update(rows[f])
            out[f] = dict(last)
    return out


def byte(row, a):
    """The byte at address a out of a frame's regions."""
    for base, v in row.items():
        if base <= a < base + len(v): return v[a - base]
    return None


def action(full, drive, tid):
    """(rise, fall) of the PC action: the first $A013 rise after the drive's last press whose $AE93 is the tech."""
    fs, prev = sorted(full), None
    for f in fs:
        v = byte(full[f], 0xA013)
        if f >= drive[-1][0] - 4 and v == 1 and prev == 0 and tid in [byte(full.get(f + d, {}), 0xAE93) for d in range(-4, 5)]:
            fall = next((g for g in fs if g > f and byte(full[g], 0xA013) == 0), None)
            return f, fall
        prev = v
    return None, None


def grade(case, cf, pf, drive, tid):
    c0, c1 = action(cf, drive, tid)
    p0, p1 = action(pf, drive, tid)
    lines = ['ascmover %s (tech %02X): span cartridge %s port %s' % (case, tid, (c1 - c0) if c1 else None,
                                                                    (p1 - p0) if p1 else None)]
    if c0 is None or p0 is None or c1 is None or p1 is None: return lines
    n = min(c1 - c0, p1 - p0)
    for lab, rs in CASES[case][2]:
        first = None; ndiff = 0
        for r in range(n):
            cv = [byte(cf[c0 + r], a + i) for a, ln in rs for i in range(ln)]
            pv = [byte(pf[p0 + r], a + i) for a, ln in rs for i in range(ln)]
            if cv != pv:
                ndiff += 1
                if first is None: first = (r, cv, pv)
        if first is None:
            lines.append('ascmover %s: %s identical on all %d frames' % (case, lab, n))
        else:
            r, cv, pv = first
            lines.append('ascmover %s: %s differs on %d of %d frames, first at +%d: cartridge %s port %s' % (
                case, lab, ndiff, n, r, ''.join('%02X' % (x or 0) for x in cv), ''.join('%02X' % (x or 0) for x in pv)))
    if case == 'ice':                                              # C1/4CF7: the end waits for $A3D1
        def gap(full, a, b):
            up = next((f for f in range(a, b) if byte(full[f], 0xA3D1) and not byte(full[f - 1], 0xA3D1)), None)
            end = next((f for f in range(a, b + 1) if byte(full[f], 0x9876)), None)
            return up, end
        cu, ce = gap(cf, c0, c1); pu, pe = gap(pf, p0, p1)
        lines.append('ascmover ice: $A3D1 up +%s / +%s, $9876 = 1 +%s / +%s: the end waits %s / %s frames' % (
            cu - c0 if cu else None, pu - p0 if pu else None, ce - c0 if ce else None, pe - p0 if pe else None,
            ce - cu if cu and ce else None, pe - pu if pu and pe else None))
    return lines


def main():
    os.chdir(ROOT)
    names = [a for a in sys.argv[1:] if a in CASES] or list(CASES)
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(names)) as ex:
        for lines in ex.map(run, names):
            for l in lines: print(l, flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
