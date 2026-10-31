#!/usr/bin/env python3
r"""Crono's Cyclone on the Manoria Naga-ettes while they walk: the victims are the enemies around the target's cell AT THE
DISPATCH, not the ones the target select lit. Port against the rule's cartridge.

    python tools/cyclone_moved_target_cmp.py

The select (C1/1FF8[12] -> C1/2701) lights the enemies within radius^2 9 of the cursor's enemy on the frame it builds;
the confirm only records the target. The dispatch runs the area pass again (C1/C1DD -> C1/C586, area 03 = CC/2B1C
`01 / 01 01 09` -> C1/C7A7: centre $9605 = the target $B2AE, $9607 = 09 -> C1/2701) on the feet `$1D0C / $1D23 >> 4` of
ITS frame, and C1/C600 copies that $99C0 into $AD8E. An enemy that walks a cell between the two changes who is hit
(battle_multitarget.md sec. 9 / 12). The user, 2026-10-31: "the cursor showed 3 enemies, they moved before the attack
and only 2 were hit" -- the cartridge does the same.

The fixture: saves/field_re/m81_nuns_lake.state (the four nuns' fight, tools/recipes/fxr_m81_nuns.json), no input until
Crono's menu; at f520 Down, A (Tech), A (Cyclone), [Up], A. Crono is ready at f518 and the Naga-ette cmb3 steps from
cell (9,5) to (9,6) at f583, one frame before the dispatch (f584):
  primary cmb3: the select lights 03 04, the dispatch hits 03 06 (cmb4 is now dx 3 dy 1 = 10 > 9, cmb6 dx 3 dy 0 = 9);
  primary cmb4 (Up): the select lights 04 03, the dispatch hits 04 alone.
Both sides take the same pad on the same field frames (the port's battle script counts from its battle's frame 0).

    cyclone-moved: <case> select cartridge <ids> port <ids>
    cyclone-moved: <case> dispatch cartridge f<C> <ids> port f<P> <ids>
    cyclone-moved: <case> cmb<k> took damage cartridge <yes|no> port <yes|no>
    cyclone-moved: <n>/<m> identical
    cyclone-moved-info: <case> cmb<k> HP cartridge <a>-><b> port <c>-><d>     (not graded: the damage's RNG)
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
STATE = os.path.join(ROOT, 'saves', 'field_re', 'm81_nuns_lake.state')
LAKEWRAM = os.path.join(ROOT, 'saves', 'field_re', 'm81_nuns_lake.lakewram')
OUT = os.path.join(ROOT, 'build', '_cyclone_moved')
KEYS = 520                    # the menu's Down (Crono ready f518 on both sides)
FRAMES = 1000
# the Naga-ettes' curHP blocks (combatants 3..6)
HP = [(3, 0x5FB0), (4, 0x6030), (5, 0x60B0), (6, 0x6130)]
REGS = [(0xA62D, 4), (0x960D, 1), (0xAD8D, 5), (0xAE93, 1)] + [(a, 2) for _, a in HP]
CASES = [('primary-cmb3', ''), ('primary-cmb4', 'u')]
PAD_C = {'u': 0x10}


def parse(text, port):
    rx = re.compile(r'fmemmon f(\d+) loc=\w+\s+(.*)') if port else re.compile(r'\s*f(\d+)\s+(.*)')
    rows = []
    for l in text.splitlines():
        m = rx.match(l)
        if m:
            rows.append((int(m.group(1)), {int(k, 16) & 0xFFFF: v for k, v in
                                           re.findall(r'([0-9A-F]{4,6})=([0-9A-F]+)', m.group(2))}))
    return rows


def ids(h):
    return [int(h[i:i + 2], 16) for i in range(0, len(h), 2) if h[i:i + 2] != 'FF']


def outcome(rows):
    """The select's lit set (the last $A62D of group 12 before the dispatch), the dispatch (the first frame $AE93 = 01,
    Cyclone: its $AD8D list) and each Naga-ette's HP there and 340 frames later."""
    sel, disp, hp0, hp1 = None, None, {}, {}
    for f, v in rows:
        if 0xAE93 not in v:
            continue
        hp = {c: int(v[a][2:4] + v[a][0:2], 16) for c, a in HP}
        if disp is None:
            if v[0xAE93] == '01':
                n = int(v[0xAD8D][:2], 16)
                disp, hp0 = (f, ids(v[0xAD8D][2:])[:n]), hp
            elif v[0x960D] == '12' and v[0xA62D] not in ('FFFFFFFF', '00000000'):
                sel = ids(v[0xA62D])
        elif f <= disp[0] + 340:
            hp1 = hp
    return sel, disp, hp0, hp1


def main():
    os.makedirs(OUT, exist_ok=True)
    spec = ','.join('7E%04X:%d' % (a, n) for a, n in REGS)
    good = total = 0
    fmt = lambda l: ' '.join('%02X' % x for x in l) if l else 'none'
    for name, d in CASES:
        cs = '0:%d,20:2,0:12,100:2,0:12,100:2,0:12,%s100:2,0:%d' % (
            KEYS, ('%X:2,0:12,' % PAD_C[d]) if d else '', FRAMES)
        mon = subprocess.run([CT, '--monitor', spec, '--diff', '--from', STATE, '--btnscript', cs, '--frames', str(FRAMES)],
                             cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True, text=True,
                             errors='replace').stdout
        cart = outcome(parse(mon, False))
        port = None
        start = 388                                       # the port's battle frame 0 on the field's clock
        for _ in range(2):
            ps = 'n:%d,d:2,n:12,a:2,n:12,a:2,n:12,%sa:2,n:%d' % (KEYS - start, ('%s:2,n:12,' % d) if d else '', FRAMES)
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
                       CT_AUDIO_SEEDSTATE=STATE, CT_BATTLE_SCRIPT_ABS='1', CT_BATTLE_SCRIPT=ps, CT_BATTLE_TRACE='1',
                       CT_FIELD_MEMMON=','.join('%04X:%X' % (a, n) for a, n in REGS),
                       CT_SHOTS=os.path.join(OUT, 'p_' + name), CT_SHOTS_AT=str(FRAMES), CT_SHOTS_EXIT='1')
            r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '129', '--loadwram', LAKEWRAM], cwd=ROOT,
                               env=env, capture_output=True, text=True, errors='replace', timeout=600)
            port = outcome(parse(r.stdout, True))
            m = re.search(r'battlephase: begins at field f(\d+)', r.stderr)
            if not m or int(m.group(1)) == start:
                break
            start = int(m.group(1))
        (csel, cdisp, ch0, ch1), (psel, pdisp, ph0, ph1) = cart, port
        total += 2
        good += (csel == psel) + (cdisp == pdisp)
        print('cyclone-moved: %s select cartridge %s port %s' % (name, fmt(csel), fmt(psel)))
        print('cyclone-moved: %s dispatch cartridge %s port %s' % (name, *(
            'f%d %s' % (x[0], fmt(x[1])) if x else 'none' for x in (cdisp, pdisp))))
        for c, _ in HP:
            ct_ = c in ch0 and c in ch1 and ch1[c] < ch0[c]
            pt_ = c in ph0 and c in ph1 and ph1[c] < ph0[c]
            total += 1
            good += ct_ == pt_
            print('cyclone-moved: %s cmb%d took damage cartridge %s port %s' % (
                name, c, 'yes' if ct_ else 'no', 'yes' if pt_ else 'no'))
        for c, _ in HP:
            print('cyclone-moved-info: %s cmb%d HP cartridge %s->%s port %s->%s' % (
                name, c, ch0.get(c), ch1.get(c), ph0.get(c), ph1.get(c)))
    print('cyclone-moved: %d/%d identical' % (good, total))
    return 0


if __name__ == '__main__':
    sys.exit(main())
