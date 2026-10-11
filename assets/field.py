#!/usr/bin/env python3
"""Render a Chrono Trigger field scene WITH its objects/actors — the static map
(maps.py) plus every NPC/object placed at the sprite + position its scene-script
init function assigns (script.py). A first step of the M2 field engine.

Usage: python assets/field.py <rom.sfc> <scene> <out.png> [--frame N]
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import maps
import script
import sprites
from palettes import write_png_rgb

def blit_sprite(rom, px, W, H, sprite, pos, frame=0):
    """Composite a sprite assembly `frame` (compose_frame) at pixel `pos` (anchor O)."""
    try:
        cv, pal, S, O = sprites.compose_frame(rom, sprite, frame)
    except Exception:
        return False
    ax, ay = int(pos[0]), int(pos[1])
    any_px = False
    for y in range(S):
        row = ay - O + y
        if row < 0 or row >= H:
            continue
        base = y * S
        for x in range(S):
            v = cv[base + x]
            if v == 0:
                continue
            dx = ax - O + x
            if 0 <= dx < W:
                r, g, b = pal[v]
                o = (row * W + dx) * 3
                px[o], px[o + 1], px[o + 2] = r, g, b
                any_px = True
    return any_px

def compose_scene(rom, scene, frame=0, extra=None, actors=True):
    """Render the whole map (uncropped); if `actors`, also every visible actor (faced
    standing) and any `extra` sprites. Returns (px, W, H, actors). The live viewer uses
    this as a base; pass actors=False to composite engine-driven actors yourself."""
    cap = {}
    def grab(_p, px, w, h):
        cap['v'] = (bytearray(px), w, h)
    orig = maps.write_png_rgb
    maps.write_png_rgb = grab
    maps.render_scene(rom, scene, 'x', True, True, frame)     # blend, full=True
    maps.write_png_rgb = orig
    px, W, H = cap['v']
    actor_list, si = script.extract_actors(rom, scene)
    if actors:
        for a in actor_list:
            if not a['visible'] or a['sprite'] is None or a['pos'] is None:
                continue
            f = sprites.pick_frame(rom, a['sprite'], a['facing'], False, 0)    # standing, faced
            blit_sprite(rom, px, W, H, a['sprite'], a['pos'], f)
    for item in (extra or []):
        blit_sprite(rom, px, W, H, item[0], item[1], item[2] if len(item) > 2 else 0)
    return px, W, H, actor_list

def render_field(rom, scene, out, frame=0, extra=None):
    px, W, H, actors = compose_scene(rom, scene, frame, extra)
    # Crop to the scroll mask (camera view), like maps.render_scene.
    h = rom[0x360000 + scene * 14: 0x360000 + scene * 14 + 14]
    if h[10] == 0x80:
        cx0, cy0, cx1, cy1 = 0, 0, W, H
    else:
        cx0, cy0, cx1, cy1 = h[10] * 16, h[11] * 16, (h[12] + 1) * 16, (h[13] + 1) * 16
    cx0, cy0 = max(0, cx0), max(0, cy0)
    cx1, cy1 = min(W, cx1), min(H, cy1)
    cw, ch = cx1 - cx0, cy1 - cy0
    if (cw, ch) != (W, H) and cw > 0 and ch > 0:
        crop = bytearray(cw * ch * 3)
        for y in range(ch):
            s = ((cy0 + y) * W + cx0) * 3
            crop[y * cw * 3:(y + 1) * cw * 3] = px[s:s + cw * 3]
        px, W, H = crop, cw, ch
    write_png_rgb(out, px, W, H)
    drawn = sum(1 for a in actors if a['visible'] and a['sprite'] is not None and a['pos'] is not None)
    print("scene %d: %d objects, %d visible drawn -> %dx%d '%s'"
          % (scene, len(actors), drawn, W, H, out))

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print(__doc__); sys.exit(1)
    rom = open(sys.argv[1], 'rb').read()
    frame = int(sys.argv[sys.argv.index('--frame') + 1]) if '--frame' in sys.argv else 0
    render_field(rom, int(sys.argv[2]), sys.argv[3], frame)
