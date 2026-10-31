#!/usr/bin/env python3
r"""Make the README's screenshots and clip (docs/media/) from the port running the attract demo.

    python tools/release/make_media.py [--out docs/media]

Every picture is the C engine's own output (build/Belthasar.exe --boot, headless), never the oracle's: three runs of
the cold start through the attract demo, one with every option at its default (CT_SHOTS) and two with every graphics
mod on (WIDE_INI: the widescreen at 16:9, the Steam margins, the scene adjustments, the bottom lines filled;
CT_WIDESHOTS: first the stills, then every frame of the clip). The Steam margins need ct_wide.dat in the repository's
root (tools/setup.py makes it from the owner's Steam copy); without it the margins stay off and the sides show the room
alone. The frames picked are listed in SHOTS and CLIP; each is scaled 2x with nearest-neighbour. The clip (the Zombor
fight's Luminaire, widescreen) is encoded with ffmpeg, which must be on PATH. The F1 overlay shot comes from
CT_OVERLAY_SHOT. See docs/DEBUG_SURFACE.md for the variables.

These files are the one place the public tree carries pictures of the game; tools/release/scan_rom_leaks.py accepts
binaries only under docs/media/ (docs/ROM.md, "What is never distributed").
"""
import argparse
import glob
import os
import shutil
import struct
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHOTS = {                                       # name: (run, frame) -- 'v' vanilla, 'w' widescreen
    'title': ('v', 1980), 'battle-tech': ('v', 6900),
    'widescreen-zeal': ('w', 2340), 'widescreen-battle': ('w', 9450),
    'widescreen-forest': ('w', 4680), 'widescreen-castle': ('w', 5670),
    'widescreen-black-omen': ('w', 9000),
}
CLIP = (9610, 9892)                             # widescreen frames [from, to) of the clip
OVERLAY = 9910                                  # the F1 overlay's Video tab over the widescreen picture; ends the run
# The frames-between and internal-resolution mods change no still (CT_WIDESHOTS takes the engine's own 1x frame).
WIDE_INI = '[video]\nwidescreen=16:9\nsteam_margins=1\nscene_adjustments=1\nfill_bottom_lines=1\n'


def load_raw(path):
    d = open(path, 'rb').read()
    w, h = struct.unpack('<II', d[:8])
    return Image.frombytes('RGB', (w, h), d[8:8 + w * h * 3])


def x2(im):
    return im.resize((im.width * 2, im.height * 2), Image.NEAREST)


def run(env, tmp, log):
    e = dict(os.environ, SDL_VIDEODRIVER='dummy', **env)
    with open(os.path.join(tmp, log), 'w') as f:
        subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--boot'],
                       cwd=ROOT, env=e, stdout=f, stderr=subprocess.STDOUT, check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(ROOT, 'docs', 'media'))
    a = ap.parse_args()
    if not shutil.which('ffmpeg'):
        sys.exit('make_media: ffmpeg is not on PATH')
    os.makedirs(a.out, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix='ct_media_')
    none_ini, wide_ini = os.path.join(tmp, 'none.ini'), os.path.join(tmp, 'wide.ini')
    open(wide_ini, 'w').write(WIDE_INI)
    vf = sorted({f for r, f in SHOTS.values() if r == 'v'})
    run({'CT_SETTINGS': none_ini, 'CT_SHOTS': os.path.join(tmp, 'v'), 'CT_SHOTS_AT': ','.join(map(str, vf)),
         'CT_SHOTS_EXIT': '1'}, tmp, 'vanilla.log')
    os.makedirs(os.path.join(tmp, 'w'))                      # the widescreen stills: every 90th frame (each 'w' frame in SHOTS is one)
    run({'CT_SETTINGS': wide_ini, 'CT_WIDESHOTS': os.path.join(tmp, 'w', 'w'), 'CT_WIDESHOTS_EVERY': '90',
         'CT_OVERLAY_SHOT': '%s@%d:0' % (os.path.join(tmp, 'overlay.bmp'), OVERLAY)}, tmp, 'wide.log')
    os.makedirs(os.path.join(tmp, 'c'))                      # the clip: every frame of its window
    run({'CT_SETTINGS': wide_ini, 'CT_WIDESHOTS': os.path.join(tmp, 'c', 'c'), 'CT_WIDESHOTS_EVERY': '1',
         'CT_WIDESHOTS_FROM': str(CLIP[0]), 'CT_OVERLAY_SHOT': '%s@%d:9' % (os.path.join(tmp, 'end.bmp'), CLIP[1])},
        tmp, 'clip.log')
    for name, (r, f) in SHOTS.items():
        src = os.path.join(tmp, 'v_f%d.raw' % f) if r == 'v' else os.path.join(tmp, 'w', 'w_f%d.raw' % f)
        x2(load_raw(src)).save(os.path.join(a.out, name + '.png'), optimize=True)
    Image.open(os.path.join(tmp, 'overlay.bmp')).convert('RGB').save(os.path.join(a.out, 'options-overlay.png'),
                                                                     optimize=True)
    seq = os.path.join(tmp, 'seq')
    os.makedirs(seq)
    for k, f in enumerate(range(*CLIP)):
        x2(load_raw(os.path.join(tmp, 'c', 'c_f%d.raw' % f))).save(os.path.join(seq, '%04d.png' % k))
    pat = os.path.join(seq, '%04d.png')
    ff = ['ffmpeg', '-loglevel', 'error', '-y', '-framerate', '60', '-i', pat]
    subprocess.run(ff + ['-c:v', 'libx264', '-crf', '18', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
                         os.path.join(a.out, 'widescreen-battle.mp4')], check=True)
    subprocess.run(ff + ['-vf', 'fps=30,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];'
                                '[b][p]paletteuse=dither=none:diff_mode=rectangle',
                         os.path.join(a.out, 'widescreen-battle.gif')], check=True)
    shutil.rmtree(tmp)
    for p in sorted(glob.glob(os.path.join(a.out, '*'))):
        print('make_media: %-28s %8d bytes' % (os.path.basename(p), os.path.getsize(p)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
