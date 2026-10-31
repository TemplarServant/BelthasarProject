#!/usr/bin/env python3
"""The overworld sprite atlases assets/extract_assets.py pack_world packs as T_WORLDSPR (18) ids 0-3.

    python tools/gen/ow_sprites.py [--capture [--leenes S9X_STATE]] [--root DIR]

Writes, under DIR (default: the repository root):
  saves/ow_crono.spr     (id 0) `ct.exe --owsprite <w0> x`: the lead's 16x16 tiles, keyed by OBJ tile name.
  saves/ow_boat.spr      (id 2) `ct.exe --owboat <w0> x`: the coastal boat (script 0E04) bob frames.
  saves/ow_gulls.spr     (id 1) the gull frames of `ct.exe --owbirds <w0> x`, kept in the atlas's own (first-sighting)
                         order: the frames that are, pixel for pixel, a tile of the gull flap sequence C3:E6A2
                         (descriptors E77A/E77F/E784 -> OBJ names 1CA, 1CC, 1CE) decoded from the same state's VRAM in
                         the gull's palette (attr BA -> OBJ palette 5); the other birds --owbirds sees are dropped and
                         the kept frames renumbered from 0 (disasm/notes/ow_objvm.md; the same pixel matching
                         tools/gen/ow_gullmap.py uses).
  saves/ow_balloons.spr  (id 3) the Leene's Square fair sprites of the new-game pan: the booth explosion puffs (OBJ
                         chars 00 02 04 06 08, palette 1) and the rising bunches (char 0A, palettes 1/2/3), decoded from
                         the pan 625 frames after the capture instant -- the moment of the owner's
                         anim_leenes_square.state (its object blocks equal the pan's there byte for byte), where the
                         puffs are in VRAM; the record layout of tools/owbpal0.py.
The states:
  default    the recipes ow_nav_w0 (stands in for saves/ow_w0.state) and ow_intro_pan (for start_scene.state)
  --capture  saves/ow_w0.state, and for the balloons the snes9x state anim_leenes_square.state (--leenes PATH)
The --ow* captures run the plain oracle.
"""
import argparse
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import ROOT, CT, ROM, recipe, peek, scratch, cgram_of  # noqa: E402

GULL_NAMES = (0x1CA, 0x1CC, 0x1CE)       # C3:E6A2's op-4 descriptors E77A/E77F/E784
GULL_PAL = (0xBA >> 1) & 7               # the gull object's attr +0F = BA -> OBJ palette 5
BALLOON_FRAME = 625                      # frames after ow_intro_pan = anim_leenes_square.state's instant
EXPL = (0x00, 0x02, 0x04, 0x06, 0x08)    # the explosion puff chars (field.c: anim 0xE671.. -> char fr*2)


def ow_tool(mode, state, out):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    if os.path.exists(out + '.spr'):
        os.remove(out + '.spr')
    p = subprocess.run([CT, ROM, mode, state, out], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    if not os.path.exists(out + '.spr'):
        sys.exit('ow_sprites: ct.exe %s %s wrote nothing: %s' % (mode, state, (p.stdout + p.stderr)[-600:]))
    return out + '.spr'


def spr_frames(data):
    out, o = [], 1
    for _ in range(data[0]):
        w, h = data[o + 3], data[o + 4]
        out.append((data[o:o + 7], data[o + 7:o + 7 + w * h * 4]))
        o += 7 + w * h * 4
    if o != len(data):
        sys.exit('ow_sprites: a malformed .spr (%d of %d bytes)' % (o, len(data)))
    return out


def c5(x):
    return (x << 3) | (x >> 2)


def decode16(vram, cg, name, pal, base=0):
    """A 16x16 OBJ (rows of 16 names, OBSEL name base `base` bytes, the second table at +0x2000) -> RGBA."""
    px = bytearray(16 * 16 * 4)
    for py in range(16):
        for x in range(16):
            t = ((name & 0xFF) + (x >> 3) + (py >> 3) * 0x10) & 0xFF
            ba = base + t * 32 + (0x2000 if name & 0x100 else 0)
            row, kk = py & 7, 7 - (x & 7)
            b0, b1 = vram[ba + row * 2], vram[ba + row * 2 + 1]
            b2, b3 = vram[ba + 16 + row * 2], vram[ba + 16 + row * 2 + 1]
            v = ((b0 >> kk) & 1) | (((b1 >> kk) & 1) << 1) | (((b2 >> kk) & 1) << 2) | (((b3 >> kk) & 1) << 3)
            if not v:
                continue
            cw = cg[128 + pal * 16 + v]
            d = (py * 16 + x) * 4
            px[d:d + 4] = bytes((c5(cw & 31), c5((cw >> 5) & 31), c5((cw >> 10) & 31), 255))
    return bytes(px)


def words(cgram_bytes):
    return [cgram_bytes[i * 2] | cgram_bytes[i * 2 + 1] << 8 for i in range(256)]


def gulls(birds_spr, w0):
    x = peek(w0, scratch('ow_gullpk'), '0:40', rule=False)
    vram, cg = open(x + '.raw.vram', 'rb').read(), words(cgram_of(x + '.raw'))
    tiles = {decode16(vram, cg, nm, GULL_PAL): nm for nm in GULL_NAMES}
    keep = [px for _, px in spr_frames(open(birds_spr, 'rb').read()) if px in tiles]
    if sorted(tiles[px] for px in keep) != list(GULL_NAMES):
        sys.exit('ow_sprites: the --owbirds atlas does not hold each gull tile exactly once: %s'
                 % ' '.join('%03X' % tiles[px] for px in keep))
    out = bytearray([len(keep)])
    for k, px in enumerate(keep):
        out += bytes((k, 0, 0, 16, 16, 0xF8, 0xF8)) + px
    return bytes(out)


def balloons(vram, cg, base=0):
    frames = [(ch, 1) for ch in EXPL] + [(0x0A, pal) for pal in (1, 2, 3)]
    out = bytearray([len(frames)])
    for i, (ch, pal) in enumerate(frames):
        out += bytes((i, ch, pal, 16, 16, 0xF8, 0xF8)) + decode16(vram, cg, ch, pal, base)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--capture', action='store_true', help='read saves/ow_w0.state and the snes9x Leene state')
    ap.add_argument('--leenes', default=os.path.join(ROOT, 'anim_leenes_square.state'),
                    help='--capture: the snes9x anim_leenes_square.state')
    ap.add_argument('--root', default=ROOT, help='write saves/... under this directory')
    a = ap.parse_args()
    os.environ.pop('CT_ORACLE_LOGICAL', None)          # the captures read the plain oracle
    saves = os.path.join(a.root, 'saves')
    os.makedirs(saves, exist_ok=True)
    w0 = os.path.join(ROOT, 'saves', 'ow_w0.state') if a.capture else recipe('ow_nav_w0')

    shutil.copyfile(ow_tool('--owsprite', w0, scratch('ow_crono')), os.path.join(saves, 'ow_crono.spr'))
    shutil.copyfile(ow_tool('--owboat', w0, scratch('ow_boat')), os.path.join(saves, 'ow_boat.spr'))
    open(os.path.join(saves, 'ow_gulls.spr'), 'wb').write(gulls(ow_tool('--owbirds', w0, scratch('ow_birds')), w0))

    if a.capture:
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        from parse_ppu_state import parse_state_file
        res, blk = parse_state_file(a.leenes)
        spr = balloons(blk['VRA'], res['ppu']['cgdata'], res['ppu']['objNameBase'] * 2)
    else:
        x = peek(recipe('ow_intro_pan'), scratch('ow_balloons'), '0:%d' % BALLOON_FRAME)
        spr = balloons(open(x + '.raw.vram', 'rb').read(), words(cgram_of(x + '.raw')))
    open(os.path.join(saves, 'ow_balloons.spr'), 'wb').write(spr)
    print('ow_sprites: ow_crono ow_boat ow_gulls ow_balloons -> %s' % saves)


if __name__ == '__main__':
    main()
