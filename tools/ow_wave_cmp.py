#!/usr/bin/env python3
r"""World 1F5 after the Ocean Palace -- the wave over BG1 ($C6:E595 / E5C5) and the light's windows ($C6:E448 / E470 /
E498 / E4BE with the colour window) -- port against the rule's cartridge: pictures and tables.

    python tools/ow_wave_cmp.py [cartridge frame ...]       (default 1030 1040 1120 1340 1430 2100 2400)

From saves/field_re/pre_scene_19f.state (tools/ocean_aftermath_cmp.py's start), no input: the cartridge
reaches 1F5 at f869, the wave from f993 (the 224-line table a frame, HDMA channel 3 over $210E), window 1's beam f1024-1055
(channel 1 over $2126), both beams from f1303. Each cartridge `--peek` frame N against the port's shots N + d for d in
-8..+12: prints the best d and its differing pixels and lines. The world's WRAM runs 6 frames behind until 1EE and 10 after
the way back to 1F5 (the world entry's load clock), and its picture is P + 1 on that clock: d = +5, then +9.
Then the tables: the wave's two buffers ten frames after each side's install, and window 1's and 2's buffers ($8B56 /
$8DF6, $9176 / $9416) and pointers on every frame the beams draw, the port at the install's offset.
disasm/notes/ocean_palace_aftermath.md sec. 2.
"""
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = 'saves/field_re/pre_scene_19f.state'
LAKEWRAM = 'saves/field_re/pre_scene_19f.lakewram'
OUT = os.path.join(ROOT, 'build', '_ow_wave')


def main():
    os.chdir(ROOT)
    frames = [int(a) for a in sys.argv[1:]] or [1030, 1040, 1120, 1340, 1430, 2100, 2400]
    os.makedirs(OUT, exist_ok=True)
    shots = sorted({f + d for f in frames for d in range(-8, 13)})
    last = max(shots) + 2
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=STATE, CT_SHOTS=os.path.join(OUT, 'p'), CT_SHOTS_AT=','.join(map(str, shots)))
    subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '415', '--loadwram',
                    LAKEWRAM, '--walkscript', 'n:%d' % last, '--selftest', '--frames', str(last)], env=env, capture_output=True)
    cenv = dict(os.environ, CT_ORACLE_LOGICAL='1')
    for f in frames:
        craw = os.path.join(OUT, 'c%d.raw' % f)
        subprocess.run([os.path.abspath('build/ct.exe'), '--peek', STATE, craw, '--frames', str(f)], env=cenv, capture_output=True)
        o = open(craw, 'rb').read()
        cart = bytearray()
        for y in range(224):
            for x in range(256):
                i = ((y + 8) * 1024 + x * 2) * 4
                cart += bytes((o[i + 3], o[i + 2], o[i + 1]))
        best = None
        for d in range(-8, 13):
            fn = os.path.join(OUT, 'p_f%d.raw' % (f + d))
            if not os.path.exists(fn):
                continue
            pd = open(fn, 'rb').read()
            w = struct.unpack('<I', pd[:4])[0]
            p = pd[8:]
            rows, n = set(), 0
            for y in range(224):
                a = cart[y * 768:(y + 1) * 768]
                b = p[y * w * 3:y * w * 3 + 768]
                if a != b:
                    k = sum(1 for x in range(256) if a[x * 3:x * 3 + 3] != b[x * 3:x * 3 + 3])
                    n += k
                    rows.add(y)
            if best is None or n < best[1]:
                best = (d, n, rows)
        d, n, rows = best
        print('owwave: cartridge f%d best port f%d (%+d): %d px differ on %d line(s)' % (f, f + d, d, n, len(rows)))
    # The wave's table: each side's first frame with the install's `F0` at $8A4F, then both 224-line buffers ($9A36, $9796)
    # and the pointers $8A29 / $8A4F ten frames on.
    mon = '7E8A4F:7,7E8A29:4,7E9A36:448,7E9796:448,7E8A21:8,7E8B56:448,7E8DF6:448,7E9176:448,7E9416:448'
    r = subprocess.run([os.path.abspath('build/ct.exe'), '--monitor', mon, '--from', STATE, '--input', '0:1350', '--diff'],
                       env=cenv, capture_output=True, text=True, errors='replace').stdout
    cs, cst = {}, {}
    for line in r.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', line)
        if m:
            for a, v in re.findall(r'7E([0-9A-F]{4})=([0-9A-F]+)', m.group(2)): cst[a] = v
            cs[int(m.group(1))] = dict(cst)
    penv = dict(env, CT_FIELD_MEMMON='8A4F:7,8A29:4,9A36:1C0,9796:1C0,8A21:8,8B56:1C0,8DF6:1C0,9176:1C0,9416:1C0')
    penv.pop('CT_SHOTS', None); penv.pop('CT_SHOTS_AT', None)
    r = subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '415', '--loadwram',
                        LAKEWRAM, '--walkscript', 'n:1370', '--selftest', '--frames', '1370'], env=penv, capture_output=True,
                       text=True, errors='replace').stdout
    ps, pst = {}, {}
    for line in r.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\w+\s+(.*)', line)
        if m:
            for a, v in re.findall(r'\b([0-9A-F]{4})=([0-9A-F]+)', m.group(2)): pst[a] = v
            ps[int(m.group(1))] = dict(pst)
    def start(st):
        return next((f for f in sorted(st) if st[f].get('8A4F', '').startswith('F0')), None)
    c0, p0 = start(cs), start(ps)
    if c0 is None or p0 is None:
        print('owwave: table -- no install seen (cartridge %s, port %s)' % (c0, p0)); return
    def at(st, f):
        k = max(x for x in st if x <= f); return st[k]
    c, p = at(cs, c0 + 10), at(ps, p0 + 10)
    same = all(c.get(k) == p.get(k) for k in ('8A4F', '8A29', '9A36', '9796'))
    print('owwave: table -- install cartridge f%d port f%d; ten frames on: pointers %s/%s, both buffers %s' %
          (c0, p0, c.get('8A29'), p.get('8A29'), 'equal' if same else 'DIFFER'))
    # The beams' tables: every frame window 1's beam draws alone (f1024-1055) and both draw (f1303-1345), at the offset.
    off, n, bad = p0 - c0, 0, []
    for f in list(range(1024, 1056)) + list(range(1303, 1346)):
        c, p = at(cs, f), at(ps, f + off); n += 1
        if any(c.get(k) != p.get(k) for k in ('8A21', '8B56', '8DF6', '9176', '9416')): bad.append(f)
    print('owwave: beams -- %d frames, window tables and pointers %s' %
          (n, 'equal on all' if not bad else 'DIFFER on %d (first cartridge f%d)' % (len(bad), bad[0])))


if __name__ == '__main__':
    main()
