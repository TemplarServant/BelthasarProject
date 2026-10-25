#!/usr/bin/env python3
r"""Fire Whirl against the Naga-ettes: the enemies' published poses, port against the rule's cartridge, frame by frame.

    python tools/naga_firewhirl_pose_cmp.py

The Manoria cathedral fight (saves/field_re/m81_nuns_ppu_lake.state: the user's snes9x capture animacao_freiras_naga_ettes
spliced with --ppu onto `--teleport 129 7 11 0`, so the oracle draws the room right; the port runs
saves/field_re/m81_nuns_lake.lakewram, the same WRAM). Both sides get battle_ab's combo pokes for 3C (Crono + Lucca,
every single learned, MP 99, only Fire Whirl's dual bit) and its drive: Down, A, eight Downs, A, A once both are ready.
The port's keys are the cartridge's frames less the battle's start (CT_BATTLE_TRACE's `battlephase: begins at field fN`).
  cartridge  build/ct.exe --monitor 7E1D0F:4,7E1D26:4,7E990A:4 (the rule: CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1)
  port       CT_BATTLE_POSTRACE (each combatant's live foot and each enemy's published pose, idlePub = $990A,slot)
Aligned on each side's first enemy step; prints the action's rise on each side and
    naga-pose: <n> frames, <k> differ [first port bf<P> cart f<C>: <cart> | <port>]
What it grades: C1/4058's two waits are the action state, which never reaches the idle tick's dispatch C1/10A9 -- the
cartridge's `$990A` holds 04 07 04 04 from C1/405B (f858) through C1/40A9 (f860) and the whole tech; a walk that ran in
those two frames froze the Naga-ettes in other poses for the tech (battle.c battleIterFrameA, pcThinkDelay).
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import naga_walk_cmp as nw  # noqa: E402
import battle_ab as b  # noqa: E402  (removes tools/ from sys.path: import it last)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = 'saves/field_re/m81_nuns_ppu_lake.state'
SEED = 'saves/field_re/m81_nuns_lake.lakewram'
OUT = os.path.join(ROOT, 'build', '_naga_pose')


def main():
    os.makedirs(OUT, exist_ok=True)
    b.STATE = STATE
    pk = b.combo_pokes(0x3C)
    ready = b.combo_ready(pk, 2)
    drive = b.drive_combo(ready)
    total = drive[-1][0] + 600
    env = dict(os.environ, **b.RULE, PEEK_POKE=','.join('%X=%02X' % (a, v) for a, v in sorted(pk.items())))
    c = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--monitor', '7E1D0F:4,7E1D26:4,7E990A:4,7EA013:1', '--diff',
                        '--from', STATE, '--input', b.oracle_input(drive, total)], cwd=ROOT, env=env, capture_output=True,
                       text=True).stdout.splitlines()
    d = open(os.path.join(ROOT, SEED), 'rb').read()
    i = d.index(b'RAM:131072:') + 11
    w = bytearray(d[i:i + 0x20000])
    for a, v in pk.items():
        w[a] = v
    lw = os.path.join(OUT, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + bytes(w))
    start, p = 0, []
    for attempt in range(3):
        penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
                    CT_AUDIO_SEEDSTATE=STATE, CT_BATTLE_SCRIPT_ABS='1', CT_BATTLE_SCRIPT=b.port_script(drive, start, total),
                    CT_BATTLE_POSTRACE='1', CT_BATTLE_TRACE='1', CT_SHOTS=os.path.join(OUT, 'p'),
                    CT_SHOTS_AT=str(total), CT_SHOTS_EXIT='1')
        # (the interactive path, ended by CT_SHOTS_EXIT: --selftest fights the battle synchronously and prints no
        #  `battlephase:` line, and the keys would then land at the cartridge's field frames, not its battle's)
        r = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '129',
                            '--loadwram', lw], cwd=ROOT, env=penv, capture_output=True, text=True, errors='replace')
        p = r.stderr.splitlines()
        m = re.search(r'battlephase: begins at field f(\d+)', r.stderr)
        if not m or int(m.group(1)) == start:
            break
        start = int(m.group(1))
    C, P, CP, PP = {}, {}, {}, {}
    for l in c:
        m = re.match(r'\s+f(\d+)\s+7E1D0F=([0-9A-F]{8})\*?\s+7E1D26=([0-9A-F]{8})\*?\s+7E990A=([0-9A-F]{8})', l)
        if m:
            f = int(m.group(1))
            C[f] = tuple((int(m.group(2)[2 * k:2 * k + 2], 16), int(m.group(3)[2 * k:2 * k + 2], 16)) for k in range(4))
            CP[f] = ' '.join(m.group(4)[2 * k:2 * k + 2] for k in range(4))
    for l in p:
        if l.startswith('bpos f') and ' pose ' in l:
            q = l.split()
            f = int(q[1][1:])
            P[f] = tuple((int(x[:2], 16), int(x[2:], 16)) for x in q[5:9])
            j = q.index('pose')
            PP[f] = ' '.join(x.split('/')[0] for x in q[j + 1:j + 5])
    if not C or not P:
        print('naga-pose: FAILED -- a side produced nothing (cartridge %d, port %d rows)' % (len(C), len(P)))
        return 1
    C, P, CP, PP = nw.dense(C), nw.dense(P), nw.dense(CP), nw.dense(PP)
    cf, pf = nw.first_move(C), nw.first_move(P)
    if cf is None or pf is None:
        print('naga-pose: FAILED -- no enemy step (cartridge %s, port %s)' % (cf, pf))
        return 1
    off = cf - pf
    n, bad, first = 0, 0, None
    for k in range(0, 1100):
        a, q = CP.get(pf + k + off), PP.get(pf + k)
        if a is None or q is None:
            break
        n += 1
        if a != q:
            bad += 1
            if first is None:
                first = ' [first port bf%d cart f%d: %s | %s]' % (pf + k, pf + k + off, a, q)
    print('naga-pose: %d frames, %d differ%s' % (n, bad, first or ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
