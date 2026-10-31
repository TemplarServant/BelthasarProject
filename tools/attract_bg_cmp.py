#!/usr/bin/env python3
r"""Compare the port's attract against the cartridge on rows no sprite reaches.

    python tools/attract_bg_cmp.py <port_frame> <oracle_frame> <row0> <row1> [<row0> <row1> ...]

The port runs the attract from its own boot (`--scene 433`) and dumps the presented frame with
CT_SHOTS; the oracle (build/ct.exe) runs the cartridge under the rule (CT_ORACLE_LOGICAL /
CT_ORACLE_VIRTUAL_BEAM, CLAUDE.md) from saves/attract_cena_inicial_rule.state -- the attract from power-on
+ 2159 frames, made by a recipe (tools/fixtures/attract_rule.json) -- and dumps its framebuffer with --peek.
The two numberings differ by a constant on the field (the vehicle's frame p is the state's p - 2067: --scene
433 plays the power-on, docs/TESTING.md 'The attract vehicle'), but --selftest runs the fights synchronously,
so after a battle the pair is found inside the same leg; only row bands where no sprite passes are compared.
Both sides are measured live every run.

Oracle framebuffer = 512x480 [pad,B,G,R], downsampled 2x; the port's row y is the oracle's row y+8.
ATTRACT_CMP_DY=<n> adds n to that row mapping -- a measuring knob only (the BG line rule: disasm/notes/ow_bg_vline.md).
Open: loc 16E's one-line residual (9.23).
"""
import os
import struct
import subprocess
import sys

FIELD = os.path.abspath('build/Belthasar.exe')
CT = os.path.abspath('build/ct.exe')
STATE = 'saves/attract_cena_inicial_rule.state'
RULE = {'CT_ORACLE_LOGICAL': '1', 'CT_ORACLE_VIRTUAL_BEAM': '1'}


def main():
    if len(sys.argv) < 5 or (len(sys.argv) - 3) % 2:
        sys.exit(__doc__)
    pf, of = int(sys.argv[1]), int(sys.argv[2])
    bands = [(int(sys.argv[i]), int(sys.argv[i + 1])) for i in range(3, len(sys.argv), 2)]
    tmp = 'build/_attbg_%d' % os.getpid()

    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SHOTS=tmp, CT_SHOTS_AT=str(pf))
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433', '--frames', str(pf + 1),
                    '--selftest'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    shot = '%s_f%d.raw' % (tmp, pf)
    if not os.path.exists(shot):
        sys.exit('attract-bg-cmp: the port never presented frame %d' % pf)
    d = open(shot, 'rb').read()
    w, h = struct.unpack('<II', d[:8])
    port = d[8:]
    os.remove(shot)

    orc = tmp + '_orc.raw'
    subprocess.run([CT, '--peek', STATE, orc, '--frames', str(of)], env=dict(os.environ, **RULE),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(orc):
        sys.exit('attract-bg-cmp: the oracle wrote no frame')
    o = open(orc, 'rb').read()
    for ext in ('', '.wram', '.vram', '.oam'):
        try:
            os.remove(orc + ext)
        except OSError:
            pass

    dy = int(os.environ.get('ATTRACT_CMP_DY', '0'))
    for y0, y1 in bands:
        same = total = 0
        for y in range(y0, y1):
            for x in range(w):
                i = ((y + 8 + dy) * 2 * 512 + x * 2) * 4
                j = (y * w + x) * 3
                total += 1
                if (o[i + 3], o[i + 2], o[i + 1]) == (port[j], port[j + 1], port[j + 2]):
                    same += 1
        print('attract-bg-cmp: port f%d vs oracle f%d rows %d-%d%s -> %d / %d identical'
              % (pf, of, y0, y1, (' dy%+d' % dy) if dy else '', same, total))


if __name__ == '__main__':
    main()
