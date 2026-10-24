#!/usr/bin/env python3
r"""The ORBIT op of the battle animation script (op $C1 -> C1/6F31 / C1/6E8F, mover mode 3 = CF/F68D) against the cartridge,
frame by frame, on the attract's Zombor triple tech (loc 087, tech $3A: four scratch objects circling the stored point in
three rounds -- radius 12 for 32 steps, 10 for 16, 8 for 16).

    python tools/zombor_orbit_cmp.py

  * cartridge: saves/attract_087_leg_rule.state (the 087 leg's start, the power-on + 9112 under the rule, made by a recipe:
    tools/fixtures/attract_rule.json; until 2026-10-22 the owner's real-time capture run to f7085, build/_z_f7085.state),
    then `ct.exe --monitor` under the rule (CT_ORACLE_LOGICAL / VIRTUAL_BEAM) prints the mover's arrays on every frame of the leg: $A240/$A250 (the objects' X/Y, slot*2), $AAA5
    (angles), $AAC3 (16-bit counts), $A1E0 (done), $AA7F/$AA92 (centres), $A342 (active);
  * port: the pre-battle fixture saves/attract_zombor_pre.lakewram (the cartridge's WRAM 9 frames before the leg's `D8`,
    `--scene 135 --loadwram`) with CT_BATTLE_ORBTRACE=1: the same arrays after every frame's mover pass.

The two runs are aligned on the first frame the mover services slot 1 (its count leaves the armed value). Graded per
frame from there: the eight angles, the eight counts, the eight done flags, the eight active flags, and the objects'
positions RELATIVE to their centres -- the centres themselves are the stored point, which is where the party stood when
the battle was staged (C0/04DE's $29B0 records), a field-position residual of the 087 leg reported separately.
"""
import os
import re
import subprocess

CT = os.path.abspath('build/ct.exe')
FIELD = os.path.abspath('build/Belthasar.exe')
FIX = 'saves/attract_zombor_pre.lakewram'
STATE = SRC = 'saves/attract_087_leg_rule.state'
RULE = {'CT_ORACLE_LOGICAL': '1', 'CT_ORACLE_VIRTUAL_BEAM': '1'}
REGS = ['A240', 'A250', 'AAA5', 'AAC3', 'A1E0', 'AA7F', 'AA92', 'A342']


def run(argv, env=None, timeout=1200):
    return subprocess.run(argv, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout).stdout


def parse_orc(text):
    rows = {}
    for ln in text.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', ln)
        if not m: continue
        d = {}
        for mo in re.finditer(r'7E([0-9A-F]{4})=([0-9A-F]+)', m.group(2)): d[mo.group(1)] = bytes.fromhex(mo.group(2))
        for k in ('A240', 'A250', 'AAC3'):                       # 16-byte arrays: keep the low byte of each slot, as the port prints
            if k in d and len(d[k]) == 16: d[k] = bytes(d[k][2 * s] for s in range(8))
        if all(k in d for k in REGS): rows[int(m.group(1))] = d
    return rows


def parse_port(text):
    rows = {}
    for ln in text.splitlines():
        m = re.match(r'orbit f(\d+) (.*)', ln)
        if not m: continue
        d = {}
        for mo in re.finditer(r'([0-9A-F]{4})=([0-9A-F]+)', m.group(2)): d[mo.group(1)] = bytes.fromhex(mo.group(2))
        if all(k in d for k in REGS): rows[int(m.group(1))] = d
    return rows


def first_service(rows, slot=1, need_clear=True):
    cleared = not need_clear                                              # the battle's FD/A982 clear: every slot idle
    for f in sorted(rows):
        if not rows[f]['A342'].strip(bytes(1)): cleared = True; continue
        if not cleared: continue
        cnt = rows[f]['AAC3'][slot]
        if rows[f]['A342'][slot] and cnt and cnt != 0x20: return f
    return None


def rel(d):
    x = d['A240']; y = d['A250']; cx = d['AA7F']; cy = d['AA92']; on = d['A342']
    return tuple((((x[s] - cx[s]) & 0xFF, (y[s] - cy[s]) & 0xFF) if on[s] else None) for s in range(8))   # idle slots hold leftovers


def main():
    if not os.path.exists(SRC) or not os.path.exists(FIX):
        print('zombor-orbit: SKIP -- missing %s or %s' % (SRC, FIX)); return
    orc = parse_orc(subprocess.run([CT, '--monitor', ','.join('7E%s:%d' % (r, 16 if r in ('A240', 'A250', 'AAC3') else 8) for r in REGS),
                                    '--from', STATE, '--frames', '800'], env=dict(os.environ, **RULE), stdout=subprocess.PIPE,
                                   stderr=subprocess.DEVNULL, text=True, timeout=1200).stdout)
    pt = run([FIELD, '--assets', 'ct_assets.dat', '--scene', '135', '--loadwram', FIX, '--frames', '900'],
             env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_BATTLE_ORBTRACE='1',
                      CT_SHOTS='build/_zorb', CT_SHOTS_AT='900', CT_SHOTS_EXIT='1'))
    port = parse_port(pt)
    fo, fp = first_service(orc), first_service(port, need_clear=False)   # the fixture starts inside the cleared battle
    if fo is None or fp is None:
        print('zombor-orbit: FAIL -- no orbit service found (cartridge %s, port %s)' % (fo, fp)); return
    n = 0; same = {'angles': 0, 'counts': 0, 'done': 0, 'active': 0, 'relpos': 0}
    while (fo + n) in orc and (fp + n) in port:
        o, p = orc[fo + n], port[fp + n]
        same['angles'] += o['AAA5'] == p['AAA5']; same['counts'] += o['AAC3'] == p['AAC3']
        same['done'] += o['A1E0'] == p['A1E0']; same['active'] += o['A342'] == p['A342']
        same['relpos'] += rel(o) == rel(p)
        if os.environ.get('ORBIT_DEBUG') and (o['AAC3'] != p['AAC3'] or o['A342'] != p['A342'] or rel(o) != rel(p)):
            print('  diff +%d cart f%d cnt=%s on=%s done=%s | port f%d cnt=%s on=%s done=%s' % (n, fo + n, o['AAC3'].hex(), o['A342'].hex(), o['A1E0'].hex(), fp + n, p['AAC3'].hex(), p['A342'].hex(), p['A1E0'].hex()))
        n += 1
        if not p['A342'].strip(b'\0') and not o['A342'].strip(b'\0'): break
    co, cp = orc[fo]['AA7F'], port[fp]['AA7F']
    print('zombor-orbit: aligned at cartridge f%d / port f%d, %d frames: angles %d, counts %d, done %d, active %d, positions-relative-to-centre %d identical'
          % (fo, fp, n, same['angles'], same['counts'], same['done'], same['active'], same['relpos']))
    print('zombor-orbit: centres cartridge (%d,%d) port (%d,%d) -- the stored point, the party\'s staged position (C0/04DE)'
          % (co[1], orc[fo]['AA92'][1], cp[1], port[fp]['AA92'][1]))


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    main()
