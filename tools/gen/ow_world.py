#!/usr/bin/env python3
"""The overworld captures assets/extract_assets.py pack_world packs as T_WORLD (17) and T_WORLDINTROSEED (27).

    python tools/gen/ow_world.py [--capture] [--root DIR]

Writes, under DIR (default: the repository root):
  saves/ow_w0.live.wram, saves/ow_w0.intro.wram   the new-game pan (loc 496): `ct.exe --peek <pan> x --btnscript 0:1`
                                                  -> x.wram. The map data of world 0 and the intro seed (the flock +
                                                  fair spawner master pc=06CD at $0B30) come from here.
  saves/ow_w0.live.wram.bak, saves/ow_w0.live.vram  the interactive 1000 A.D. overworld: `--peek <w0> x 0:1` ->
                                                  x.wram (the BASE object seed: no master, the lead PC) and x.vram
                                                  (the char and BG3 of world 0).
  saves/ow_w<n>.live.wram/.vram, n = 1-7         `ct.exe --owforce <w0> <n> x` (re-runs the C2 loader with
                                                  $0100 = 0x1F0+n and 240 frames) -> x.wram, x.vram.
The two oracle states:
  default    the recipes ow_intro_pan (stands in for start_scene.state) and ow_nav_w0 (stands in for saves/ow_w0.state)
  --capture  the owner's captures start_scene.state and saves/ow_w0.state
The captures were dumped by the plain oracle (no CT_ORACLE_LOGICAL; they predate the rule), and these dumps keep
doing so: a dump is a read of a state, and --owforce's world load is an NMI-off load the rule prices differently
(the forced world's ambient objects would run a frame apart).
"""
import argparse
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import ROOT, CT, ROM, recipe, peek, scratch  # noqa: E402


def owforce(state, world, out):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    for ext in ('.wram', '.vram'):
        if os.path.exists(out + ext):
            os.remove(out + ext)
    p = subprocess.run([CT, ROM, '--owforce', state, str(world), out], cwd=ROOT, env=env, capture_output=True,
                       text=True, errors='replace')
    if p.returncode or not os.path.exists(out + '.wram'):
        sys.exit('ow_world: ct.exe --owforce %s %d failed: %s' % (state, world, (p.stdout + p.stderr)[-600:]))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--capture', action='store_true', help='read start_scene.state and saves/ow_w0.state')
    ap.add_argument('--root', default=ROOT, help='write saves/... under this directory')
    a = ap.parse_args()
    os.environ.pop('CT_ORACLE_LOGICAL', None)          # the dumps read the plain oracle (the recipes set the rule themselves)
    if a.capture:
        pan, w0 = os.path.join(ROOT, 'start_scene.state'), os.path.join(ROOT, 'saves', 'ow_w0.state')
    else:
        pan, w0 = recipe('ow_intro_pan'), recipe('ow_nav_w0')
    saves = os.path.join(a.root, 'saves')
    os.makedirs(saves, exist_ok=True)

    x = peek(pan, scratch('ow_intro'), '0:1', rule=False)
    shutil.copyfile(x + '.raw.wram', os.path.join(saves, 'ow_w0.live.wram'))
    shutil.copyfile(x + '.raw.wram', os.path.join(saves, 'ow_w0.intro.wram'))
    x = peek(w0, scratch('ow_w0'), '0:1', rule=False)
    shutil.copyfile(x + '.raw.wram', os.path.join(saves, 'ow_w0.live.wram.bak'))
    shutil.copyfile(x + '.raw.vram', os.path.join(saves, 'ow_w0.live.vram'))
    for n in range(1, 8):
        x = owforce(w0, n, scratch('ow_w%d' % n))
        shutil.copyfile(x + '.wram', os.path.join(saves, 'ow_w%d.live.wram' % n))
        shutil.copyfile(x + '.vram', os.path.join(saves, 'ow_w%d.live.vram' % n))
    print('ow_world: worlds 0-7 -> %s' % saves)


if __name__ == '__main__':
    main()
