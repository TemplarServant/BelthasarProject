#!/usr/bin/env python3
"""Population atlas: per scene, find the global-flag state that most fully populates it.

Scenes gate NPC placement on global story flags (`ifg`/0x16). At game start most flags
are 0, so many rooms are empty; the game sets them as you play. This greedily searches
the flags a scene checks and keeps each one that increases the placed-actor count, giving
the fullest reachable population per scene. (For a real story state, capture the flags
from the oracle: ct --dumpflags.)

Usage:
  python assets/atlas.py <rom> build [out.json]      # build the atlas
  python assets/atlas.py <rom> render <scene> <out>  # render a scene at its atlas state
"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '../tools')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import script
from script import Mem, GLB


def _placed(objs, body, gmem):
    mem = Mem()
    if gmem:
        for i in range(0x200):
            mem.w8(GLB + i, gmem[i])
    actors = script._new_actors(objs)
    for n in range(len(objs)):
        script.run_func(body, objs, n, objs[n][0], mem, actors)
    return [a for a in actors if a['defined'] and a['draw'] == 'draw' and not a['dead'] and a['pos'] is not None]


def _relevant_flags(objs, body):
    """Global byte offsets the scene tests via ifg (0x16): the search space."""
    offs = set()
    for o in range(len(objs)):
        for addr, op, ln in script.iter_ops(body, objs[o][0]):
            if op == 0x16:
                li = body[addr + 1]
                if body[addr + 3] & 0x80:
                    li += 0x100
                if li < 0x200:
                    offs.add(li)
    return sorted(offs)


def scene_gmem(rom, scene):
    """Greedily build the flag state that maximizes placed actors for `scene`.
    Returns (gmem bytearray, base_count, atlas_count)."""
    objs, body = script.load_script(rom, rom[0x360000 + scene * 14 + 8] | (rom[0x360000 + scene * 14 + 9] << 8))
    base = len(_placed(objs, body, None))
    gm = bytearray(0x200)
    cur = base
    for off in _relevant_flags(objs, body):
        gm[off] = 0xFF
        n = len(_placed(objs, body, gm))
        if n > cur:
            cur = n
        else:
            gm[off] = 0
    return gm, base, cur


def build(rom, out):
    atlas = {}
    bt = at = 0
    for s in range(512):
        try:
            gm, base, cur = scene_gmem(rom, s)
        except Exception:
            continue
        bt += base; at += cur
        if cur > base:
            atlas[s] = {'flags': {i: gm[i] for i in range(0x200) if gm[i]}, 'base': base, 'atlas': cur}
    data = {'game_start_total': bt, 'atlas_total': at, 'gained_scenes': len(atlas),
            'scenes': {str(s): v for s, v in atlas.items()}}
    if out:
        json.dump(data, open(out, 'w'))
    print("population atlas: game-start=%d atlas=%d (+%d), %d scenes gained%s"
          % (bt, at, at - bt, len(atlas), " -> " + out if out else ""))
    return data


def render(rom, scene, out):
    import engine as E
    import field
    from palettes import write_png_rgb
    gm, base, cur = scene_gmem(rom, scene)
    eng = E.SceneEngine(rom, scene, gmem=bytes(gm))
    placed = [a for a in eng.actors if a['visible'] and a['sprite'] is not None and a['pos'] is not None]
    px, W, H, _ = field.compose_scene(rom, scene, actors=False)
    import sprites
    for a in placed:
        f = sprites.pick_frame(rom, a['sprite'], a['facing'], False, 0)
        field.blit_sprite(rom, px, W, H, a['sprite'], a['pos'], f)
    h = rom[0x360000 + scene * 14: 0x360000 + scene * 14 + 14]
    if h[10] != 0x80:
        cx0, cy0 = max(0, h[10] * 16), max(0, h[11] * 16)
        cx1, cy1 = min(W, (h[12] + 1) * 16), min(H, (h[13] + 1) * 16)
        cwp, chp = cx1 - cx0, cy1 - cy0
        if cwp > 0 and chp > 0 and (cwp, chp) != (W, H):
            crop = bytearray(cwp * chp * 3)
            for y in range(chp):
                s = ((cy0 + y) * W + cx0) * 3
                crop[y * cwp * 3:(y + 1) * cwp * 3] = px[s:s + cwp * 3]
            px, W, H = crop, cwp, chp
    write_png_rgb(out, px, W, H)
    print("scene %d atlas render: %d placed (was %d) -> %s" % (scene, cur, base, out))


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    rom = _romlib.load(sys.argv[1])
    if sys.argv[2] == 'build':
        build(rom, sys.argv[3] if len(sys.argv) > 3 else None)
    elif sys.argv[2] == 'render':
        render(rom, int(sys.argv[3]), sys.argv[4])
