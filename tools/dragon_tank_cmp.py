#!/usr/bin/env python3
r"""The Dragon Tank's bridge (loc 01C), port against the rule's cartridge: the tank's arrival, its battle and the end.

    python tools/dragon_tank_cmp.py

THE SEED: saves/field_re/tank/tele01c.state -- the jackie save ch05 (the prison cell, saves/downloaded/ctjackie) with the
story page of the user's own run at the bridge (storyline 2E) and the party Crono + Lucca, the tank's two flags cleared
($7F0198 bit 2, $7F0199 bit 2: the first walk-on and the arrival), then `ct.exe --teleport 28 44 9 2 --method jmp
--frames 60` (the ROM's own load). Its .lakewram is the port's seed (--loadwram). The rule's cartridge (CT_ORACLE_LOGICAL=1).

THE DRIVE: Left until the first trigger (Lucca's "What was that?"), A every 16 frames through the box, Left again into the
second (the tank rolls in: obj5 fn4), then A every 16 frames through the fight (Attack, the default target) and the end.
The port takes the same buttons one frame later: its field answers a press one frame sooner than the console (field.c,
fieldPadNmi's PHASE note), so this lines the field up and leaves the fight's pad (g_padAutoRead) one frame behind.

PRINTS
    dragon-tank: arrival c1440 tank box <n>/<N> px identical
    dragon-tank: hit flash c2160 tank box <n>/<N> px identical
    dragon-tank: Crono's attack anims cart <list> port <list>
    dragon-tank: results lines cart <list> port <list>
The arrival and the flash are the class-3 body (sprite 403, obj13 / combatant 3) the port drew as nothing (field.c's frame
loop and battleEnemyGenRender); the anims are C1/4A71's walk verdict (battle.c battleTargetWalkable); the lines are the
TP line's $B2AF bit 4 and the no-celebration box (battleBuildResults, field.c's results render gate).
"""
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'ct_field.exe')
STATE = os.path.join(ROOT, 'saves', 'field_re', 'tank', 'tele01c.state')
SEED = os.path.join(ROOT, 'saves', 'field_re', 'tank', 'tele01c.lakewram')
OUT = os.path.join(ROOT, 'build', '_dragon_tank')
END = 7400                                   # past the fight's exit (the cartridge's c7349)
SHOTS = (1440, 2160)
BOX = (0, 40, 128, 168)                      # the tank, render coordinates (x0, y0, x1, y1)


def drive():
    a16 = lambda n: ['100:2,0:14'] * n
    return ','.join(['0:20', '40:310'] + a16(44) + ['40:400'] + a16(450))


def port_script(bs):
    m = {'0': '.', '40': 'l', '100': 'a'}
    return '.:1,' + ','.join(m[t.split(':')[0]] + ':' + t.split(':')[1] for t in bs.split(','))


def cart_px(path, x, y):
    with open(path, 'rb') as f:
        f.seek(((y * 2 + 16) * 512 + x * 2) * 4)
        b = f.read(4)
    return (b[3], b[2], b[1])


def main():
    for p in (STATE, SEED, CT, FIELD):
        if not os.path.exists(p):
            sys.exit('dragon-tank: SKIP -- missing %s' % p)
    os.makedirs(OUT, exist_ok=True)
    bs = drive()
    # the cartridge: the frames, and $9877 / $CCEC per frame
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_SHOTS=os.path.join(OUT, 'c'),
               PEEK_SHOTS_AT=','.join(str(f) for f in SHOTS))
    subprocess.run([CT, '--peek', STATE, os.path.join(OUT, 'c_end.raw'), '--frames', str(max(SHOTS)), '--btnscript', bs],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    mon = subprocess.run([CT, '--monitor', '7E9877:1,7EB18B:1,7ECCEC:1', '--from', STATE, '--btnscript', bs,
                          '--frames', str(END), '--diff'], cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'),
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True).stdout
    c_anims, c_lines, prev = [], 0, {}
    for ln in mon.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', ln)
        if not m or int(m.group(1)) > END:
            continue
        d = {a: int(v, 16) for a, v in re.findall(r'7E(\w{4})=(\w\w)', m.group(2))}
        if d.get('9877') != prev.get('9877') and d.get('B18B') == 0 and int(m.group(1)) > 1972:
            c_anims.append('%02X' % d['9877'])
        if d.get('CCEC') == 0x10 and prev.get('CCEC') != 0x10 and int(m.group(1)) > 7100:
            c_lines += 1
        prev = d
    # the port
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=STATE, CT_SHOTS=os.path.join(OUT, 'p'),
               CT_SHOTS_AT=','.join(str(f) for f in SHOTS + (END,)), CT_SHOTS_EXIT='1', CT_BATTLE_TRACE='1',
               CT_BATTLE_MEMMON='9877:1,B18B:1')
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '28', '--loadwram', SEED,
                        '--walkscript', port_script(bs)], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, errors='replace')
    p_anims, last, seq = [], None, '?'
    for ln in r.stdout.splitlines():
        m = re.search(r'memmon f\d+ .*9877=(\w\w)\*?\s+B18B=(\w\w)', ln)
        if m:
            a, atk = int(m.group(1), 16), int(m.group(2), 16)
            if '9877=%02X*' % a in ln and atk == 0 and last is not None:
                p_anims.append('%02X' % a)
            last = a
        m = re.match(r'battle-results seq=(\S+)', ln)
        if m:
            seq = m.group(1)
    for f in SHOTS:
        cp, pp = os.path.join(OUT, 'c_f%d.raw' % f), os.path.join(OUT, 'p_f%d.raw' % f)
        if not os.path.exists(cp) or not os.path.exists(pp):
            print('dragon-tank: frame %d missing (cart %d port %d)' % (f, os.path.exists(cp), os.path.exists(pp)))
            continue
        d = open(pp, 'rb').read()
        w = struct.unpack('<II', d[:8])[0]
        same = tot = 0
        for y in range(BOX[1], BOX[3]):
            for x in range(BOX[0], BOX[2]):
                o = 8 + (y * w + x) * 3
                tot += 1
                same += tuple(d[o:o + 3]) == cart_px(cp, x, y)
        print('dragon-tank: %s c%d tank box %d/%d px identical' % ('arrival' if f == SHOTS[0] else 'hit flash', f, same, tot))
    print("dragon-tank: Crono's attack anims cart %s port %s" % (' '.join(c_anims), ' '.join(p_anims)))
    names = {'0': 'XP', '1': 'TP', '2': 'Gold', '3': 'item'}
    p_lines = [names.get(k.split(':')[0], k) for k in seq.split(',')] if seq != '?' else []
    print('dragon-tank: results lines cart %d port %s' % (c_lines, '+'.join(p_lines)))


if __name__ == '__main__':
    main()
