#!/usr/bin/env python3
r"""The HOP-SLIDE family of the battle animation script -- op D2 (C1/705A: mover mode 4, CF/F6E2 for a scratch object and
CF/F23D for a combatant), op D3 (C1/720E: the slide to the midpoint, mode 0 = CF/F4B2), op D7 (C1/740B: the point nn steps
toward a target) and the waits D4/D5/D6 -- against the cartridge, frame by frame, on the attract's Zombor triple tech
(loc 087, tech $3A): five scratch objects and the actor hop from the actor's foot to the victim's centre, and the sixth
object slides to the hop's midpoint.

    python tools/zombor_hopslide_cmp.py

  * cartridge: saves/attract_087_leg_rule.state (the 087 leg's start under the rule, made by a recipe: tools/fixtures/
    attract_rule.json; until 2026-10-22 the owner's real-time capture run to f7085, build/_z_f7085.state), then `ct.exe
    --monitor` prints the movers' arrays on every frame of the leg UNDER THE LOGICAL-FRAME RULE (CT_ORACLE_LOGICAL=1
    CT_ORACLE_VIRTUAL_BEAM=1, CLAUDE.md: no lag frame exists; cached as build/_z_orc_m4b_logical.txt). The real-beam
    cartridge lags inside the leg (the tracks' `20 0D`/`20 10` pauses released 1-2 frames late, the D8 shake 3 late),
    and a per-mover alignment cannot absorb a lag frame inside a mover's run;
  * port: the pre-battle fixture saves/attract_zombor_pre.lakewram with CT_BATTLE_M4TRACE=1: the same arrays after every
    action frame.

Three movers, each aligned on its own arm so the script's sync residuals (a `24 05` released a frame apart) do not
mask the mover's law:
  * slots 1-5 (op D2, mode 4): aligned on the first frame slot 4 is in mode 4 (C1/71CA); graded per frame -- positions
    ($A240/$A250, ABSOLUTE: the party's staged records are byte-identical since C0/1770's align wait), modes, phases
    ($A9F4), heights ($A99A high words), done, active, the arrival latches $AB0B -- while the cartridge keeps any of
    them active;
  * slot 0 (op D3, mode 0): aligned on its first step ($ABF6 leaving the armed count); positions, counts, done, active;
  * combatant 0 (op D2 on the actor, CF/F23D): aligned on the first frame its foot moves in mode 4; feet ($1D0C/$1D23),
    draw offsets ($A4AF), phases ($A9DE), done ($A19D), arrival ($AB00) while the cartridge's $A86D is 4.
The second line reports the per-mover frame offsets between the two runs (the script residual, not the mover's).
"""
import hashlib
import os
import re
import subprocess

CT = os.path.abspath('build/ct.exe')
FIELD = os.path.abspath('build/Belthasar.exe')
SRC = 'saves/attract_087_leg_rule.state'
FIX = 'saves/attract_zombor_pre.lakewram'
STATE = SRC
ORC = 'build/_z_orc_m4b_logical.txt'
# The oracle's cache is per model: under the rule's switches (CT_ORACLE_NMION_HOLD=1 + CT_ORACLE_IFAST=1) the same
# --monitor from the same state is another run (the boot's frames are not the valve's), and a cache made under one model
# must not be read under the other.
if os.environ.get('CT_ORACLE_NMION_HOLD', '1') != '0' and os.environ.get('CT_ORACLE_IFAST', '1') != '0':
    ORC = 'build/_z_orc_m4b_rule.txt'
MON = ('7EA240:16,7EA250:16,7EA1E0:8,7EA342:8,7EA878:8,7EA9F4:16,7EA998:32,7EAAFC:4,7E1D0C:11,7E1D23:11,7EA4AF:11,'
       '7EA19D:11,7EA86D:11,7EA9DE:22,7EAAF9:2,7E5D9E:2,7EAB0B:8,7EAB00:11,7EABF6:8,7EA337:11')


def run(argv, env=None, timeout=1200):
    return subprocess.run(argv, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout).stdout


def norm_orc(d):
    o = {}
    o['A240'] = bytes(d['A240'][2 * y] for y in range(8)); o['A250'] = bytes(d['A250'][2 * y] for y in range(8))
    o['A9F4'] = bytes(d['A9F4'][2 * y] for y in range(8))
    o['A99A'] = bytes(b for y in range(8) for b in (d['A998'][4 * y + 2], d['A998'][4 * y + 3]))
    o['A9DE'] = bytes(d['A9DE'][2 * c] for c in range(11))
    o['LX'] = d['1D0C']; o['LY'] = d['1D23']
    for k in ('A1E0', 'A342', 'A878', 'A4AF', 'A19D', 'A86D', 'AB0B', 'AB00', 'ABF6', 'A337'): o[k] = d[k]
    return o


def parse_orc(text):
    rows = {}
    for ln in text.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', ln)
        if not m: continue
        d = {}
        for mo in re.finditer(r'7E([0-9A-F]{4})=([0-9A-F]+)', m.group(2)): d[mo.group(1)] = bytes.fromhex(mo.group(2))
        if 'A998' in d and 'A337' in d: rows[int(m.group(1))] = norm_orc(d)
    return rows


def parse_port(text):
    rows = {}
    for ln in text.splitlines():
        m = re.match(r'm4 f(\d+) (.*)', ln)
        if not m: continue
        d = {}
        for mo in re.finditer(r'([0-9A-F]{4}|LX|LY)=([0-9A-F]+)', m.group(2)): d[mo.group(1)] = bytes.fromhex(mo.group(2))
        if 'AB00' in d: rows[int(m.group(1))] = d
    return rows


def first(rows, pred):
    for f in sorted(rows):
        if pred(rows[f]): return f
    return None


def slot_view(r, y):
    return {'pos': (r['A240'][y], r['A250'][y]), 'mode': r['A878'][y], 'phase': r['A9F4'][y], 'height': r['A99A'][2 * y:2 * y + 2],
            'done': r['A1E0'][y], 'active': r['A342'][y], 'arrival': r['AB0B'][y], 'count': r['ABF6'][y]}


def cmb_view(r, c):
    return {'feet': (r['LX'][c], r['LY'][c]), 'offset': r['A4AF'][c], 'phase': r['A9DE'][c], 'done': r['A19D'][c],
            'arrival': r['AB00'][c], 'mode': r['A86D'][c]}


def grade(orc, port, fo, fp, view, keys, alive):
    n = 0; same = {k: 0 for k in keys}; seen = False
    while (fo + n) in orc and (fp + n) in port:
        o, p = view(orc[fo + n]), view(port[fp + n])
        a = alive(orc[fo + n]); seen = seen or a
        if seen and not a: break                       # the mover ran and has finished on the cartridge
        for k in keys: same[k] += (o[k] == p[k])
        if os.environ.get('HOPSLIDE_DEBUG') and any(o[k] != p[k] for k in keys):
            print('  +%d cart f%d %s | port f%d %s' % (n, fo + n, {k: o[k] for k in keys if o[k] != p[k]}, fp + n, {k: p[k] for k in keys if o[k] != p[k]}))
        n += 1
    return n, same


def main():
    if not os.path.exists(SRC) or not os.path.exists(FIX):
        print('zombor-hopslide: SKIP -- missing %s or %s' % (SRC, FIX)); return
    # ...and per BINARY and per switch: a trace kept across an oracle rebuild is another oracle's. The suite passed on a
    # trace made before CT_ORACLE_RDNMI became the default (2026-10-09: the cartridge's f502 where the rule's is f500).
    sw = ','.join('%s=%s' % (k, os.environ.get(k, '')) for k in ('CT_ORACLE_RDNMI', 'CT_ORACLE_LINE64', 'CT_ORACLE_VIRTUAL_BEAM'))
    orc_path = '%s_%s.txt' % (ORC[:-4], hashlib.sha256(open(CT, 'rb').read() + sw.encode() + STATE.encode()).hexdigest()[:12])
    if not os.path.exists(orc_path):
        # stdout only: the logical oracle prints its `logical-frame:` summary on stderr at exit, and merged into the same
        # pipe it can land inside the last --monitor row (it did under the rule's switches: the cache's f794 row ended
        # `0logical-frame: ...` and parse_orc's fromhex failed).
        open(orc_path, 'w').write(subprocess.run([CT, '--monitor', MON, '--from', STATE, '--frames', '800'],
                                                 env=dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1'),
                                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                                 timeout=1200).stdout)
    orc = parse_orc(open(orc_path).read())
    pt = run([FIELD, '--assets', 'ct_assets.dat', '--scene', '135', '--loadwram', FIX, '--frames', '900'],
             env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_BATTLE_M4TRACE='1',
                      CT_SHOTS='build/_zhop', CT_SHOTS_AT='900', CT_SHOTS_EXIT='1'))
    port = parse_port(pt)
    # (1) slots 1-5: the D2 arms
    fo, fp = first(orc, lambda r: r['A878'][4] == 4), first(port, lambda r: r['A878'][4] == 4)
    if fo is None or fp is None:
        print('zombor-hopslide: FAIL -- no mode-4 arm found (cartridge %s, port %s)' % (fo, fp)); return
    keys = ('pos', 'mode', 'phase', 'height', 'done', 'active', 'arrival')
    tot = {k: 0 for k in keys}; frames = 0
    for y in range(1, 6):
        n, same = grade(orc, port, fo, fp, lambda r, y=y: slot_view(r, y), keys,
                        lambda r: any(r['A342'][s] and r['A878'][s] == 4 for s in range(1, 6)))
        frames = max(frames, n)
        for k in keys: tot[k] += same[k]
    line = 'zombor-hopslide: slots 1-5 (D2, mode 4) aligned at cartridge f%d / port f%d, %d frames x 5: %s identical' % (
        fo, fp, frames, ', '.join('%s %d' % (k, tot[k]) for k in keys))
    # (2) slot 0: the D3 slide, aligned on its first step
    armed = lambda r: r['A878'][0] == 0 and r['A342'][0] and r['ABF6'][0]
    so = first(orc, lambda r: r['ABF6'][0] and r['A342'][0] and r['A878'][0] == 0 and (r['A240'][0], r['A250'][0]) != (0x8F, 0xA0))
    sp = first(port, lambda r: r['ABF6'][0] and r['A342'][0] and r['A878'][0] == 0 and (r['A240'][0], r['A250'][0]) != (0x8F, 0xA0))
    keys0 = ('pos', 'count', 'done', 'active')
    if so is not None and sp is not None:
        n0, s0 = grade(orc, port, so, sp, lambda r: slot_view(r, 0), keys0, lambda r: r['A342'][0])
        line += '; slot 0 (D3, mode 0) %d frames: %s identical' % (n0, ', '.join('%s %d' % (k, s0[k]) for k in keys0))
    else: n0 = s0 = None
    # (3) combatant 0: the actor's hop, aligned on its first mode-4 step
    co = first(orc, lambda r: r['A86D'][0] == 4 and r['A9DE'][0] == 0 and r['A4AF'][0] not in (0,))
    cp = first(port, lambda r: r['A86D'][0] == 4 and r['A9DE'][0] == 0 and r['A4AF'][0] not in (0,))
    keysc = ('feet', 'offset', 'phase', 'done', 'arrival')
    if co is not None and cp is not None:
        nc, sc = grade(orc, port, co, cp, lambda r: cmb_view(r, 0), keysc, lambda r: r['A86D'][0] == 4)
        line += '; combatant 0 (D2, CF/F23D) %d frames: %s identical' % (nc, ', '.join('%s %d' % (k, sc[k]) for k in keysc))
    print(line)
    print('zombor-hopslide: script offsets (port - cartridge, frames): slot 0 first step %s, combatant 0 first step %s'
          % ('n/a' if so is None or sp is None else (sp - fp) - (so - fo), 'n/a' if co is None or cp is None else (cp - fp) - (co - fo)))


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    main()
