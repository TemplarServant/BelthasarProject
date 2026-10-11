#!/usr/bin/env python3
r"""The effect's sprites against the cartridge: OBJ sheets, frame table, and a program's sprite cloud on screen.

    python tools/attract_fx_sprite_cmp.py <oracle_state> <oracle_frame> <port_frame> -- <ct_field args...>

The port (build/ct_field.exe) runs with CT_DUMPWRAM / CT_DUMPWRAM_AT = <port_frame> (its battle WRAM, taken before
that frame's tick, plus `.fxobj` = the effect's OBJ sheets as uploaded, VRAM words $0C00-$17FF) and CT_SHOTS at the
same frame; the oracle (build/ct.exe --peek) stops at <oracle_frame> and dumps its framebuffer, WRAM, VRAM, OAM and
CGRAM. Both sides run live (set CT_AUDIO_LOCKSTEP in the environment when the leg needs it). Prints

    fx-sprite-cmp: sheets (VRAM $0C00-$17FF) <same> / 6144 identical
    fx-sprite-cmp: frame table ($4500 $A05B $5D00 $A07B $987A-$9886) <same> / 6333 identical
    fx-sprite-cmp: port f<P> vs oracle f<O>: cloud (OAM 4-19) <n> sprites, visible pixels <same> / <total> identical

The cloud is drawn here from the CARTRIDGE's own OAM, VRAM and CGRAM (ppu.c's ppu_evaluateSprites rule, 16x16 for
the large bit, the OAM row = the port framebuffer row = the oracle row + 8); the pixels graded are those where the
cartridge's picture shows that sprite (a BG3 priority tile may cover a few). The frame table and the sheets are
static once the action has loaded them, so the WRAM taken before the port's tick grades them directly.
"""
import os
import struct
import subprocess
import sys

FIELD = os.path.abspath('build/ct_field.exe')
CT = os.path.abspath('build/ct.exe')


def orc_px(buf, x, y):
    i = ((y + 8) * 2 * 512 + x * 2) * 4
    return (buf[i + 3], buf[i + 2], buf[i + 1])


def cloud_pixels(oam, hi, vram, cg):
    img = {}
    for e in range(19, 3, -1):                         # the lower entry is in front: draw 19 first
        x, y, tile, attr = oam[4 * e:4 * e + 4]
        hb = (hi[e >> 2] >> (2 * (e & 3))) & 3
        size = 16 if hb & 2 else 8
        x |= (hb & 1) << 8
        if x > 255:
            x -= 512
        pal, page, hf, vf = (attr >> 1) & 7, attr & 1, attr & 0x40, attr & 0x80
        for r in range(size):
            fy = (y + r) & 0xFF
            row = size - 1 - r if vf else r
            for col in range(0, size, 8):
                uc = size - 1 - col if hf else col
                ut = ((((tile >> 4) + row // 8) << 4) | (((tile & 15) + uc // 8) & 15)) & 0xFF
                word = (0x1000 if page else 0) + ut * 16
                a1, a2 = 2 * (word + (row & 7)), 2 * (word + 8 + (row & 7))
                p1 = vram[a1] | (vram[a1 + 1] << 8)
                p2 = vram[a2] | (vram[a2 + 1] << 8)
                for px in range(8):
                    sh = px if hf else 7 - px
                    v = (((p1 >> sh) & 1) | (((p1 >> (8 + sh)) & 1) << 1) |
                         (((p2 >> sh) & 1) << 2) | (((p2 >> (8 + sh)) & 1) << 3))
                    sx = x + col + px
                    if not v or not 0 <= sx < 256 or fy >= 224:
                        continue
                    w = cg[2 * (128 + 16 * pal + v)] | (cg[2 * (128 + 16 * pal + v) + 1] << 8)
                    img[(sx, fy)] = tuple((((w >> k) & 31) << 3) | (((w >> k) & 31) >> 2) for k in (0, 5, 10))
    return img


def main():
    if '--' not in sys.argv or sys.argv.index('--') != 4:
        sys.exit(__doc__)
    st, of, pf = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    args = sys.argv[5:]
    tmp = 'build/_fxspr_%d' % os.getpid()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SHOTS=tmp, CT_SHOTS_AT=str(pf), CT_SHOTS_EXIT='1',
               CT_DUMPWRAM=tmp + '.wram', CT_DUMPWRAM_AT=str(pf))
    subprocess.run([FIELD] + args, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    shot = '%s_f%d.raw' % (tmp, pf)
    need = (shot, tmp + '.wram', tmp + '.wram.fxobj')
    if not all(os.path.exists(p) for p in need):
        sys.exit('fx-sprite-cmp: the port wrote no shot/dump at frame %d' % pf)
    d = open(shot, 'rb').read()
    w, h = struct.unpack('<II', d[:8])
    port = d[8:]
    pw = open(tmp + '.wram', 'rb').read()
    pobj = open(tmp + '.wram.fxobj', 'rb').read()
    for p in need:
        os.remove(p)
    orc = tmp + '_orc.raw'
    subprocess.run([CT, '--peek', st, orc, '--frames', str(of)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(orc + '.oam'):
        sys.exit('fx-sprite-cmp: the oracle wrote no frame')
    o = open(orc, 'rb').read()
    ow = open(orc + '.wram', 'rb').read()
    ov = open(orc + '.vram', 'rb').read()
    oo = open(orc + '.oam', 'rb').read()
    for ext in ('', '.wram', '.vram', '.oam'):
        try:
            os.remove(orc + ext)
        except OSError:
            pass
    same = sum(1 for i in range(0x1800) if pobj[i] == ov[0x1800 + i])
    print('fx-sprite-cmp: sheets (VRAM $0C00-$17FF) %d / 6144 identical' % same)
    same = total = 0
    for a, n in ((0x4500, 0x1800), (0xA05B, 0x20), (0x5D00, 0x80), (0xA07B, 0x10), (0x987A, 0x0D)):
        same += sum(1 for i in range(n) if pw[a + i] == ow[a + i])
        total += n
    print('fx-sprite-cmp: frame table ($4500 $A05B $5D00 $A07B $987A-$9886) %d / %d identical' % (same, total))
    img = cloud_pixels(oo[0:512], oo[1024:1056], ov, oo[1056:1568])
    vis = {k: c for k, c in img.items() if orc_px(o, *k) == c}
    same = sum(1 for (x, y), c in vis.items() if tuple(port[(y * w + x) * 3:(y * w + x) * 3 + 3]) == c)
    n = sum(1 for e in range(4, 20) if oo[4 * e + 1] < 0xE0)
    print('fx-sprite-cmp: port f%d vs oracle f%d: cloud (OAM 4-19) %d sprites, visible pixels %d / %d identical'
          % (pf, of, n, same, len(vis)))


if __name__ == '__main__':
    main()
