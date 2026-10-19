#!/usr/bin/env python3
r"""The ROM-free runtime's run-level proof: the same run from ct_assets.dat and from the ROM shows and plays the same.

    python tools/rom_free_cmp.py [frames] [every]      (default 11200 frames, a thumbnail every 2nd)

Runs Belthasar on the attract (`--scene 433`, CT_AUDIO_LOCKSTEP=1) twice: once as it is -- every system the ROM-free
work packages moved reads ct_assets.dat -- and once with every one of them sent back to the ROM (FROM_ROM below; each
package adds its switch). Both runs record a 128x112 thumbnail of every `every`-th presented frame (CT_SHOTS_THUMBS) and
the audio output (CT_AUDIO_PCM); they must be equal byte for byte:

    rom-free-cmp: f<frames> <n> thumbnails and <m> output samples identical, container vs ROM (sha1 <hash>)

The item-by-item checks are ct.exe --sndassets / --lzassets / --tabassets (and each package's own).
"""
import hashlib
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FROM_ROM = ['CT_AUDIO_FROM_ROM', 'CT_LZ_FROM_ROM', 'CT_TABLES_FROM_ROM']   # one switch per moved system


def run(frames, every, from_rom, tag):
    d = os.path.join(ROOT, 'build', '_rom_free_cmp')
    os.makedirs(d, exist_ok=True)
    th, pcm = os.path.join(d, tag + '.thumbs'), os.path.join(d, tag + '.pcm')
    for p in (th, pcm):
        if os.path.exists(p):
            os.remove(p)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_SHOTS=os.path.join(d, tag), CT_SHOTS_AT=str(frames), CT_SHOTS_EXIT='1', CT_SHOTS_THUMBS=th,
               CT_SHOTS_THUMBS_EVERY=str(every), CT_AUDIO_PCM=pcm)
    for k in FROM_ROM:
        env.pop(k, None)
        if from_rom:
            env[k] = '1'
    subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '433'],
                   cwd=ROOT, env=env, capture_output=True)
    t = open(th, 'rb').read() if os.path.exists(th) else b''
    a = open(pcm, 'rb').read() if os.path.exists(pcm) else b''
    return t, a


def main():
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 11200
    every = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    ta, pa = run(frames, every, False, 'container')
    tb, pb = run(frames, every, True, 'rom')
    rec = 4 + 2 + 128 * 112 * 3
    if not ta or not pa:
        print('rom-free-cmp: the run wrote no thumbnails or no audio')
        return 1
    if ta != tb:
        k = next((i for i in range(0, min(len(ta), len(tb)), rec) if ta[i:i + rec] != tb[i:i + rec]), min(len(ta), len(tb)))
        f = int.from_bytes(ta[k:k + 4], 'little') if k + 4 <= len(ta) else -1
        print('rom-free-cmp: f%d the picture differs from thumbnail %d (frame %d)' % (frames, k // rec, f))
        return 1
    if pa != pb:
        k = next((i for i in range(0, min(len(pa), len(pb)), 4) if pa[i:i + 4] != pb[i:i + 4]), min(len(pa), len(pb)))
        print('rom-free-cmp: f%d the audio differs from sample %d (frame %d)' % (frames, k // 4, k // 4 // 534))
        return 1
    print('rom-free-cmp: f%d %d thumbnails and %d output samples identical, container vs ROM (sha1 %s)'
          % (frames, len(ta) // rec, len(pa) // 4, hashlib.sha1(ta + pa).hexdigest()[:12]))
    for p in os.listdir(os.path.join(ROOT, 'build', '_rom_free_cmp')):
        if p.endswith('.thumbs'):
            os.remove(os.path.join(ROOT, 'build', '_rom_free_cmp', p))   # (hundreds of MB; the hash says it)
    return 0


if __name__ == '__main__':
    sys.exit(main())
