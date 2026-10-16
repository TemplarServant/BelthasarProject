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

ALIGNMENT: on that drive the field's WRAM is the cartridge's frame for frame (--monitor f = CT_FIELD_MEMMON f: every
object's position, frame and script pointer, the camera and the play clock, f1100-f1700), and the port's PICTURE of frame
P is the cartridge's --peek P+2 (the project's one alignment, memory ct-frame-alignment-p2: the NMI shows the HDMA set the
loop before built, field_window_wipe.md) -- 0 px different in the tank box on f150-163, f1196-1212 and f1432-1449. The
arrival is graded so, port 1438 against cartridge 1440. In the fight the pad one frame behind makes it P+1 (the hurt
flash's pose changes on port 2156/2159, cartridge 2157/2160): port 2159 against 2160.

PRINTS
    dragon-tank: arrival c1440 tank box <n>/<N> px identical
    dragon-tank: hit flash c2160 tank box <n>/<N> px identical
    dragon-tank: Crono's attack anims cart <list> port <list>
    dragon-tank: results lines cart <list> port <list>
    dragon-tank: railing c7400 vram holds <k> chips the wram map emptied
    dragon-tank: railing c7400 box <n>/<N> px identical
The arrival and the flash are the class-3 body (sprite 403, obj13 / combatant 3) the port drew as nothing (field.c's frame
loop and battleEnemyGenRender); the anims are C1/4A71's walk verdict (battle.c battleTargetWalkable); the lines are the
TP line's $B2AF bit 4 and the no-celebration box (battleBuildResults, field.c's results render gate).
The railing: at the fight's return obj0 fn1 runs `E4 07 0C 0B 0D 07 09 41`, which empties BG1 cells 7-11 x 9-10 in the WRAM
map; op 0xE4 uploads nothing, so the VRAM keeps the railing in front of the wreck (the cartridge's held chips: the $6000
ring through $1D99/$1D9A against the WRAM's metatile words) and the port's picture must too (field.c fieldTileCopyDrain,
scene.c CtFieldScenePicHold; disasm/notes/field_tile_copy_e4.md).
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
SHOTS = ((1440, 2), (2160, 1))               # (the cartridge's --peek frame, the port's frame behind it): see ALIGNMENT
RAIL = 7400                                  # after the E4 (the cartridge's c7363), before the camera's pan (c7528)
RAIL_BOX = (32, 128, 112, 160)               # BG1 cells 7-11 x 9-10 at the camera $1D0A = 0A, $1D0E = 02
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


def held_chips(path):
    """BG1 chips of cells 7-11 x 9-10 whose VRAM word is not the WRAM map's: the $6000 tilemap is a 64x32 ring the
    camera streams (slot = chip - $1D0A/$1D0E + the origin $1D99/$1D9A), a chip's word the metatile's at $7E:B000 + id * 8."""
    w = open(path + '.wram', 'rb').read()
    v = open(path + '.vram', 'rb').read()
    a, e, ox, oy = w[0x1D0A], w[0x1D0E], w[0x1D99], w[0x1D9A]
    n = 0
    for cy in range(18, 22):
        for cx in range(14, 24):
            o = (cy >> 1) * 256 + (cx >> 1)
            mid = w[0x3000 + o] | ((w[0x30C0 + o] & 1) << 8)
            q = (cy & 1) * 2 + (cx & 1)
            exp = w[0xB000 + mid * 8 + q * 2] | (w[0xB000 + mid * 8 + q * 2 + 1] << 8)
            sx, sy = (cx - a + ox) & 63, (cy - e + oy) & 31
            adr = 0x6000 + sy * 32 + (sx & 31) + (0x400 if sx >= 32 else 0)
            n += (v[adr * 2] | (v[adr * 2 + 1] << 8)) != exp
    return n


def main():
    for p in (STATE, SEED, CT, FIELD):
        if not os.path.exists(p):
            sys.exit('dragon-tank: SKIP -- missing %s' % p)
    os.makedirs(OUT, exist_ok=True)
    bs = drive()
    # the cartridge: the frames, and $9877 / $CCEC per frame
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_SHOTS=os.path.join(OUT, 'c'),
               PEEK_SHOTS_AT=','.join(str(f) for f, _ in SHOTS + ((RAIL, 0),)), PEEK_SHOTS_SIDECARS='1')
    subprocess.run([CT, '--peek', STATE, os.path.join(OUT, 'c_end.raw'), '--frames', str(RAIL), '--btnscript', bs],
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
               CT_SHOTS_AT=','.join(str(f) for f in sorted(set([c - k for c, k in SHOTS] + [RAIL, END]))), CT_SHOTS_EXIT='1', CT_BATTLE_TRACE='1',
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
    for f, k in SHOTS:
        cp, pp = os.path.join(OUT, 'c_f%d.raw' % f), os.path.join(OUT, 'p_f%d.raw' % (f - k))
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
        print('dragon-tank: %s c%d tank box %d/%d px identical' % ('arrival' if f == SHOTS[0][0] else 'hit flash', f, same, tot))
    print("dragon-tank: Crono's attack anims cart %s port %s" % (' '.join(c_anims), ' '.join(p_anims)))
    names = {'0': 'XP', '1': 'TP', '2': 'Gold', '3': 'item'}
    p_lines = [names.get(k.split(':')[0], k) for k in seq.split(',')] if seq != '?' else []
    print('dragon-tank: results lines cart %d port %s' % (c_lines, '+'.join(p_lines)))
    cp, pp = os.path.join(OUT, 'c_f%d.raw' % RAIL), os.path.join(OUT, 'p_f%d.raw' % RAIL)
    if os.path.exists(cp + '.vram') and os.path.exists(cp + '.wram'):
        print('dragon-tank: railing c%d vram holds %d chips the wram map emptied' % (RAIL, held_chips(cp)))
    if os.path.exists(cp) and os.path.exists(pp):
        d = open(pp, 'rb').read()
        w = struct.unpack('<II', d[:8])[0]
        same = tot = 0
        for y in range(RAIL_BOX[1], RAIL_BOX[3]):
            for x in range(RAIL_BOX[0], RAIL_BOX[2]):
                o = 8 + (y * w + x) * 3
                tot += 1
                same += tuple(d[o:o + 3]) == cart_px(cp, x, y)
        print('dragon-tank: railing c%d box %d/%d px identical' % (RAIL, same, tot))
    else:
        print('dragon-tank: railing frame %d missing (cart %d port %d)' % (RAIL, os.path.exists(cp), os.path.exists(pp)))


if __name__ == '__main__':
    main()
