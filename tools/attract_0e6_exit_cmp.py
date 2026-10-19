#!/usr/bin/env python3
r"""The attract's loc 0E6 exit, port against the rule's cartridge, whole pictures.

    python tools/attract_0e6_exit_cmp.py [--port <Belthasar.exe>]

The cartridge: build/ct.exe --peek saves/attract_cena_inicial.state --frames N under CT_ORACLE_LOGICAL=1, one run per
frame (in parallel). The port: Belthasar --boot on an erased cart with CT_AUDIO_LOCKSTEP=1 (the attract audit's boot
vehicle), one run that shoots every frame below. In this stretch the port's frame is the cartridge's + 2103 (the field's
P+2 on the boot vehicle's offset: `$0117 = 80` at the cartridge's --monitor f5672 and the port's fmemmon f7777).

Two things are graded, each against the cartridge's own picture, pixel for pixel over 256x224:
  robots  c5577-5581: obj10 and obj11 stand on one column; the draw list ($0E00, C0/B2A5) has obj11 in front, and the
          host's lead (obj3, `$0F00 = 00`, off screen) is not in it -- the ROM does not draw him (field_draw_list_order.md).
  fade    c5674-5679: the exit's drain C0/0C97 `DEC $19` through a pan; the picture is that frame's $0119.
Prints one line per frame and a summary:

    0e6-exit: robots c5577..5581 0 px; fade c5674..5679 0 px (6 frames, $0119 14 -> 9)
"""
import os
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

CT = os.path.abspath('build/ct.exe')
STATE = 'saves/attract_cena_inicial.state'
OFFSET = 2103
ROBOTS = list(range(5577, 5582))
FADE = list(range(5674, 5680))
TMP = os.path.abspath('build/_0e6_exit')


def cart(f):
    out = os.path.join(TMP, 'c%d.raw' % f)
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    subprocess.run([CT, '--peek', STATE, out, '--frames', str(f)], env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    o = open(out, 'rb').read()
    px = bytearray()
    for y in range(224):
        for x in range(256):
            i = ((y + 8) * 1024 + x * 2) * 4
            px += bytes((o[i + 3], o[i + 2], o[i + 1]))
    return f, bytes(px)


def main():
    port_exe = os.path.abspath('build/Belthasar.exe')
    if '--port' in sys.argv:
        port_exe = os.path.abspath(sys.argv[sys.argv.index('--port') + 1])
    os.makedirs(TMP, exist_ok=True)
    frames = ROBOTS + FADE
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_SRAM=os.path.join(TMP, 'fresh.srm'), CT_SRAM_FRESH='1', CT_SHOTS=os.path.join(TMP, 'p'),
               CT_SHOTS_AT=','.join(str(f + OFFSET) for f in frames), CT_SHOTS_EXIT='1')
    with ThreadPoolExecutor(max_workers=len(frames) + 1) as ex:
        pj = ex.submit(subprocess.run, [port_exe, '--assets', 'ct_assets.dat', '--boot'], env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        carts = dict(ex.map(cart, frames))
        pj.result()
    diff = {}
    for f in frames:
        shot = os.path.join(TMP, 'p_f%d.raw' % (f + OFFSET))
        if not os.path.exists(shot):
            sys.exit('0e6-exit: the port never presented frame %d' % (f + OFFSET))
        d = open(shot, 'rb').read()
        w = struct.unpack('<I', d[:4])[0]
        p = b''.join(d[8 + y * w * 3:8 + y * w * 3 + 768] for y in range(224))
        c = carts[f]
        diff[f] = sum(1 for k in range(0, len(c), 3) if c[k:k + 3] != p[k:k + 3])
        print('0e6-exit: c%d ~ p%d %d px differ' % (f, f + OFFSET, diff[f]))
    wr = [open(os.path.join(TMP, 'c%d.raw.wram' % f), 'rb').read()[0x119] for f in FADE]
    print('0e6-exit: robots c%d..%d %d px; fade c%d..%d %d px (%d frames, $0119 %d -> %d)' % (
        ROBOTS[0], ROBOTS[-1], sum(diff[f] for f in ROBOTS), FADE[0], FADE[-1], sum(diff[f] for f in FADE),
        len(FADE), wr[0], wr[-1]))


if __name__ == '__main__':
    main()
