#!/usr/bin/env python3
r"""The FIELD's animation under a battle, port against the cartridge: the attract's 07D fight, its entry and its exit tail.

    python tools/battle_field_anim_cmp.py

The arena is the field's own layers, and the field's machines keep running under the fight (the derivation is in the
comments of src/battle.c battleNmiTail / CtBattleTailFrame and src/field.c fieldBattleIrqTiles):
  - the TILES: the battle's IRQ D1/FE97 runs FD/E022 on every frame whose $7C is odd (and $CD02 clear), $7C being the
    count the fight's NMI D1/E77C steps (D1/E7E7), zeroed where D1/F1B0 installs that NMI (D1/F253);
  - the COLOURS: FD/E39C, the objslot records at $0520.., from the fight's NMI (CD/09BF) and from the exit tail's NMI
    D1/F4C0 (D1/F54D), which also drifts the layers and leaves $7C alone;
  - $0153 bit 0, which the field's C0/1895 flips once on the way in (C0/00DE -> FD/C2C1) and the field's IRQ reads again
    after the ack.
The two frames between the staging and the fight's NMI are the field's: tick 1 = C0/1895 (the drift, the flip, FD/E39C),
tick 2 = C1/0000's first frame (D1/F1B0: $7C = 0, one drift, no FD/E39C).

Both sides run the whole attract from power-on to the 07D fight's return: the cartridge UNDER THE RULE (build/ct.exe
--monitor --from boot, CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1 -- the real cartridge's lag frames step $7C and
the records through NMIs the port does not have, and carry $0153's parity into the fight) and the port (build/ct_field.exe
--scene 433, CT_AUDIO_LOCKSTEP=1, CT_FIELD_MEMMON). The fight itself is not graded frame for frame -- its length is its
own question (the port's is 10 frames short: the boot's CPU frames CT_BATTLE_BOOT_CPU and more) -- only two windows,
each aligned on its own event:
  entry  the staging frame (the port's `battlephase: begins`, the cartridge's install frame - 2) and the 60 after it
  tail   the ack ($0117 -> 00) and the 16 frames before it
Per window and per quantity (tile steps = $051D changes, $7C -- in the tail its parity and whether it moved, the fight's
length being in its value --, colour steps = record 0's counter changes, $0153):
  battle-field-anim: <window> <quantity> <n> frame(s): same | DIFFER at +<k> (cartridge .. port ..)
  battle-field-anim: -> same | DIFFER
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'ct_field.exe')
ORACLE_FRAMES = 7330          # the rule's cartridge from power-on: the 07D fight's ack is near c7294
PORT_LAST = 7215              # the port's 07D ack is near f7195 (the attract's lockstep numbering)
ENTRY_N, TAIL_N = 60, 16
KEYS = ('051D', '007C', '0153', '0117', '0520')


def parse(lines, pat):
    rows = {}
    for line in lines:
        m = re.search(pat, line)
        if not m:
            continue
        f = int(m.group(1))
        vals = {}
        for k in KEYS + ('0500',):
            mm = re.search(r'(?:7E)?%s=([0-9A-F]+)' % k, line)
            if mm:
                vals[k] = mm.group(1)
        rows[f] = vals
    return rows


def fill(rows, last):
    cur, out = {}, []
    for f in range(last + 1):
        if f in rows:
            cur = dict(cur, **rows[f])
        out.append(dict(cur))
    return out


def run_oracle():
    watch = '7E0500:8,7E051D:1,7E007C:1,7E0153:1,7E0117:1,7E0520:12'
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1')
    r = subprocess.run([CT, '--monitor', watch, '--from', 'boot', '--frames', str(ORACLE_FRAMES), '--diff'],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    return fill(parse(r.stdout.splitlines(), r'^\s+f(\d+)\s'), ORACLE_FRAMES)


def run_port():
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_BATTLE_TRACE='1', CT_FIELD_MEMMON='051D:1,007C:1,0153:1,0117:1,0520:C',
               CT_SHOTS=os.path.join(ROOT, 'build', '_bfanim'), CT_SHOTS_AT=str(PORT_LAST), CT_SHOTS_EXIT='1')
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433'], cwd=ROOT, env=env,
                       capture_output=True, text=True, errors='replace')
    shot = os.path.join(ROOT, 'build', '_bfanim_f%d.raw' % PORT_LAST)
    if os.path.exists(shot):
        os.remove(shot)
    lines = (r.stdout + '\n' + r.stderr).splitlines()
    begins = [int(m.group(1)) for m in (re.search(r'battlephase: begins at field f(\d+)', l) for l in lines) if m]
    return fill(parse(lines, r'fmemmon f(\d+) '), PORT_LAST), begins


def quantities(ser, f, tail=False):
    a, b = ser[f], ser[f - 1]
    rec = lambda s: int(s.get('0520', '00' * 12)[8:10], 16)
    c7 = a.get('007C')
    return {'tile steps': int(a.get('051D') != b.get('051D')),
            '$7C': ('odd' if int(c7 or '0', 16) & 1 else 'even') + ('/held' if c7 == b.get('007C') else '/moved')
                   if tail else c7,
            'colour steps': int(rec(a) != rec(b)),
            '$0153': a.get('0153')}


def window(name, o, p, of, pf, n):
    ok = True
    for q in ('tile steps', '$7C', 'colour steps', '$0153'):
        bad = None
        for k in range(n):
            a, b = quantities(o, of + k, name == 'tail')[q], quantities(p, pf + k, name == 'tail')[q]
            if a != b:
                bad = (k, a, b)
                break
        if bad:
            ok = False
            print('battle-field-anim: %s %s %d frame(s): DIFFER at +%d (cartridge f%d %s, port f%d %s)'
                  % (name, q, n, bad[0], of + bad[0], bad[1], pf + bad[0], bad[2]))
        else:
            print('battle-field-anim: %s %s %d frame(s): same' % (name, q, n))
    return ok


def main():
    o = run_oracle()
    p, begins = run_port()
    # the cartridge: the second install of D1/E77C (the 0AD fight is the first), the tail's D1/F4C0 after it, the ack
    inst = [f for f in range(1, len(o)) if o[f].get('0500', '').startswith('5C7CE7D1')
            and not o[f - 1].get('0500', '').startswith('5C7CE7D1')]
    if len(inst) < 2 or len(begins) < 2:
        sys.exit('battle-field-anim: the 07D fight not found (cartridge installs %s, port begins %s)' % (inst, begins))
    oe, pe = inst[1] - 2, begins[1]
    oack = next((f for f in range(inst[1], len(o)) if o[f].get('0117') == '00' and o[f - 1].get('0117') != '00'), None)
    pack = next((f for f in range(pe, len(p)) if p[f].get('0117') == '00' and p[f - 1].get('0117') != '00'), None)
    if oack is None or pack is None:
        sys.exit('battle-field-anim: no ack (cartridge %s, port %s)' % (oack, pack))
    print('battle-field-anim: entry cartridge f%d port f%d; ack cartridge f%d port f%d' % (oe, pe, oack, pack))
    ok = window('entry', o, p, oe, pe, ENTRY_N + 1)
    ok = window('tail', o, p, oack - TAIL_N, pack - TAIL_N, TAIL_N + 1) and ok
    print('battle-field-anim: -> %s' % ('same' if ok else 'DIFFER'))


if __name__ == '__main__':
    main()
