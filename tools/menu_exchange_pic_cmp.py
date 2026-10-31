#!/usr/bin/env python3
r"""The party exchange's picture (menu mode 5, screen $07), port against the cartridge under the frame rule, per pixel.

    python tools/menu_exchange_pic_cmp.py [frame ...]        # cartridge --peek frames (default 100 120 140 160 180)
    python tools/menu_exchange_pic_cmp.py --drive [frame ...] # with menu_exchange_cmp's drive: Right, A, Left, A, B

The recipe of tools/menu_exchange_cmp.py (gato_wait, storyline 0x49, Robo in reserve, Y at f20), both sides from one image
and one SPC. Each cartridge `--peek` frame N is graded against the port's frame N - 2 (EXCH_PIC_SHIFT, the menus' P + 2:
the port's frame N - 2 has the WRAM of the cartridge's peek N; tools/menu_exchange_cmp.py grades that frame for frame).
Prints `exchange-picture: cartridge fN port fM: <d> px differ, <o> outside the figure`; the figure is the hooded member
of the prompt box (x 20-43, y 150-209, C2/F422's block 7). disasm/notes/menu2_form_screen.md sec. 2.14.
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = 'build/recipes/gato_wait.state'
WRAM = 'build/recipes/gato_wait.raw.wram'
POKES = {0x10000: 0x49, 0x2982: 0x02, 0x2983: 0x03, 0x0126: 0x00}
OUT = os.path.join(ROOT, 'build', '_menu_exchange_pic')
# menu_exchange_cmp's drive, the cartridge's press frames after Y (f20): Right, A, Left, A, B -- the port presses a frame later
SHIFT = int(os.environ.get('EXCH_PIC_SHIFT', '2'))
PEEK_LAG = int(os.environ.get('EXCH_PIC_PEEKLAG', '0'))   # the port's frame N - SHIFT against the cartridge's peek N
PRESSES = [(0x080, 124), (0x100, 146), (0x040, 168), (0x100, 190), (0x001, 232)]


def main():
    os.chdir(ROOT)
    drive = '--drive' in sys.argv
    frames = [int(a) for a in sys.argv[1:] if a != '--drive'] or [100, 120, 140, 160, 180]
    presses = PRESSES if drive else []
    key = {0x080: 'r', 0x040: 'l', 0x100: 'a', 0x001: 'b'}
    if subprocess.run([sys.executable, 'tools/recipes.py', 'make', 'gato_wait'], stdout=subprocess.DEVNULL).returncode:
        sys.exit('exchange-picture: the recipe gato_wait failed')
    os.makedirs(OUT, exist_ok=True)
    w = bytearray(open(WRAM, 'rb').read())
    for a, v in POKES.items():
        w[a] = v
    seed = os.path.join(OUT, 'seed.lakewram')
    open(seed, 'wb').write(b'#lakewram\nRAM:131072:' + bytes(w))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=STATE, CT_HOLDBTN='4@21:2', CT_SHOTS=os.path.join(OUT, 'p'),
               CT_SHOTS_AT=','.join(str(f - SHIFT) for f in frames))
    last = max(frames)
    walk, t = [], 0
    for m, f in presses:
        if f + 1 > t: walk.append('n:%d' % (f + 1 - t))
        walk.append('%s:1' % key[m]); t = f + 2
    walk.append('n:%d' % max(1, last + 2 - t))
    subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '6', '--loadwram', seed,
                    '--selftest', '--frames', str(last + 2), '--walkscript', ','.join(walk)], env=env, capture_output=True)
    for f in frames:
        cenv = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_POKE=','.join('%X=%02X' % kv for kv in sorted(POKES.items())))
        craw = os.path.join(OUT, 'c%d.raw' % f)
        parts, t = ['0:20', '2:2'], 22
        for m, pf in presses:                          # (EXCH_PIC_PEEKLAG moves the cartridge's presses: measurement only)
            pf += PEEK_LAG
            if pf >= f: break
            parts.append('0:%d' % (pf - t)); parts.append('%X:2' % m); t = pf + 2
        parts.append('0:%d' % max(1, f - t))
        subprocess.run([os.path.abspath('build/ct.exe'), '--peek', STATE, craw, '--btnscript', ','.join(parts)],
                       env=cenv, capture_output=True)
        o = open(craw, 'rb').read()
        d = open(os.path.join(OUT, 'p_f%d.raw' % (f - SHIFT)), 'rb').read()
        pw = struct.unpack('<I', d[:4])[0]
        p = d[8:]
        n = outside = 0
        for y in range(224):
            for x in range(256):
                i = ((y + 8) * 2 * 512 + x * 2) * 4
                if bytes((o[i + 3], o[i + 2], o[i + 1])) != p[(y * pw + x) * 3:(y * pw + x) * 3 + 3]:
                    n += 1
                    if not (20 <= x < 44 and 150 <= y < 210):
                        outside += 1
        print('exchange-picture: cartridge f%d port f%d: %d px differ, %d outside the figure' % (f, f - SHIFT, n, outside))


if __name__ == '__main__':
    main()
