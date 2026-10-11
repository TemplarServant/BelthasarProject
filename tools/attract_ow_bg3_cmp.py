#!/usr/bin/env python3
r"""The attract's 1F0 overworld leg, port against cartridge: the BG3 (cloud) scroll and the picture.

    python tools/attract_ow_bg3_cmp.py [<port_frame> <oracle_frame>]

The leg (loc 1F0, the pan over 1000 A.D. that closes the attract) renders BLACK under `--selftest`, so the port side
is the INTERACTIVE run with CT_AUDIO_LOCKSTEP=1 -- the same vehicle tools/attract_survey.py drives -- stopped at
`port_frame` with CT_SHOTS/CT_SHOTS_EXIT; the cartridge side is `--peek saves/attract_cena_inicial.state --frames
<oracle_frame>`. Both sides' BG3 scroll is read out ($24/$2A: the port's from CT_OWCAMTRACE, the cartridge's from the
peek's .wram) and the pictures are compared pixel for pixel over the whole 256x224 frame.

The leg has no BG3 ANIMATOR object (class $75C3 / $75FD), so the cartridge's $24/$2A stay 0000 for its whole 270
frames; the port used to tick the animator in every world and drifted 49 px left and 24 down across the leg
(disasm/notes/ow_bg3_animator.md).
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIELD = os.path.join(ROOT, 'build', 'ct_field.exe')
CT = os.path.join(ROOT, 'build', 'ct.exe')
STATE = 'saves/attract_cena_inicial.state'


def main():
    os.chdir(ROOT)
    pf = int(sys.argv[1]) if len(sys.argv) > 1 else 10922
    of = int(sys.argv[2]) if len(sys.argv) > 2 else 8856
    tmp = 'build/_owbg3_%d' % os.getpid()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_AUDIO_LOCKSTEP='1', CT_OWCAMTRACE='1', CT_SHOTS=tmp, CT_SHOTS_AT=str(pf), CT_SHOTS_EXIT='1')
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433'], env=env,
                       capture_output=True, text=True, errors='replace')
    shot = '%s_f%d.raw' % (tmp, pf)
    if not os.path.exists(shot):
        sys.exit('attract-ow-bg3-cmp: the port never presented frame %d' % pf)
    cam = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('owcam ')]
    pbg3 = cam[-1].split('$24=')[1].split()[0] + ' ' + cam[-1].split('$2A=')[1].split()[0] if cam else '?? ??'
    d = open(shot, 'rb').read()
    w, h = struct.unpack('<II', d[:8])
    port = d[8:]
    orc = tmp + '_orc.raw'
    subprocess.run([CT, '--peek', STATE, orc, '--frames', str(of)], capture_output=True)
    if not os.path.exists(orc + '.wram'):
        sys.exit('attract-ow-bg3-cmp: the oracle wrote no frame')
    o = open(orc, 'rb').read()
    ow = open(orc + '.wram', 'rb').read()
    obg3 = '%04X %04X' % (ow[0x24] | (ow[0x25] << 8), ow[0x2A] | (ow[0x2B] << 8))
    dy = int(os.environ.get('ATTRACT_CMP_DY', '0'))
    same = total = 0
    for y in range(224):
        for x in range(256):
            i = ((y + 8 + dy) * 2 * 512 + x * 2) * 4
            c = (o[i + 3], o[i + 2], o[i + 1])
            p = tuple(port[(y * w + x) * 3:(y * w + x) * 3 + 3])
            total += 1
            same += (c == p)
    print('attract-ow-bg3-cmp: port f%d $24/$2A = %s | oracle f%d $24/$2A = %s' % (pf, pbg3, of, obg3))
    print('attract-ow-bg3-cmp: %d / %d pixels identical' % (same, total))
    for p in (shot, orc, orc + '.wram', orc + '.vram', orc + '.oam'):
        try:
            os.remove(p)
        except OSError:
            pass


if __name__ == '__main__':
    main()
