#!/usr/bin/env python3
r"""The ROM-free runtime's gate (package H): Belthasar with no dump plays the attract as it does with one.

    python tools/rom_free_gate.py [frames] [every]      (default 11200 frames, a thumbnail every 2nd)

Runs Belthasar on the attract (`--scene 433`, CT_AUDIO_LOCKSTEP=1) twice from ct_assets.dat: once as it is (the dump
beside it, every system reading the container anyway) and once with CT_NO_ROM=1 -- no dump at all: the engine takes the
empty image (src/battle.c CtBattleUseEmptyRom), zeros it must never read, guarded by CT_ROM_CENSUS. Both runs record a
128x112 thumbnail of every `every`-th presented frame and the audio output; they must be equal byte for byte, and the
empty image must have had no page read:

    rom-free-gate: f<frames> <n> thumbnails and <m> output samples identical with no dump; 0 pages of the empty image read (sha1 <hash>)
"""
import hashlib
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REC = 4 + 2 + 128 * 112 * 3


def run(frames, every, no_rom, tag):
    d = os.path.join(ROOT, 'build', '_rom_free_gate')
    os.makedirs(d, exist_ok=True)
    th, pcm, cen = (os.path.join(d, tag + x) for x in ('.thumbs', '.pcm', '.census'))
    for p in (th, pcm, cen):
        if os.path.exists(p):
            os.remove(p)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_SHOTS=os.path.join(d, tag), CT_SHOTS_AT=str(frames), CT_SHOTS_EXIT='1', CT_SHOTS_THUMBS=th,
               CT_SHOTS_THUMBS_EVERY=str(every), CT_AUDIO_PCM=pcm)
    for k in ('CT_AUDIO_FROM_ROM', 'CT_LZ_FROM_ROM', 'CT_TABLES_FROM_ROM', 'CT_NO_ROM', 'CT_ROM_CENSUS'):
        env.pop(k, None)
    if no_rom:
        env['CT_NO_ROM'] = '1'
        env['CT_ROM_CENSUS'] = cen
    p = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '433'],
                       cwd=ROOT, env=env, capture_output=True, text=True)
    t = open(th, 'rb').read() if os.path.exists(th) else b''
    a = open(pcm, 'rb').read() if os.path.exists(pcm) else b''
    pages = None
    if no_rom:
        if 'no dump: running from' not in p.stderr:
            return t, a, -1
        pages = sum(1 for line in open(cen) if line.strip() and not line.startswith('#')) if os.path.exists(cen) else 0
    return t, a, pages


def main():
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 11200
    every = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    ta, pa, _ = run(frames, every, False, 'dump')
    tb, pb, pages = run(frames, every, True, 'nodump')
    if not ta or not pa:
        print('rom-free-gate: the run with the dump wrote no thumbnails or no audio')
        return 1
    if pages < 0:
        print('rom-free-gate: with no dump Belthasar did not take the empty image (not a ROM-free ct_assets.dat?)')
        return 1
    if ta != tb:
        k = next((i for i in range(0, min(len(ta), len(tb)), REC) if ta[i:i + REC] != tb[i:i + REC]), min(len(ta), len(tb)))
        f = int.from_bytes(ta[k:k + 4], 'little') if k + 4 <= len(ta) else -1
        print('rom-free-gate: f%d the picture differs with no dump from thumbnail %d (frame %d)' % (frames, k // REC, f))
        return 1
    if pa != pb:
        k = next((i for i in range(0, min(len(pa), len(pb)), 4) if pa[i:i + 4] != pb[i:i + 4]), min(len(pa), len(pb)))
        print('rom-free-gate: f%d the audio differs with no dump from sample %d (frame %d)' % (frames, k // 4, k // 4 // 534))
        return 1
    print('rom-free-gate: f%d %d thumbnails and %d output samples identical with no dump; %d pages of the empty image '
          'read (sha1 %s)' % (frames, len(ta) // REC, len(pa) // 4, pages, hashlib.sha1(ta + pa).hexdigest()[:12]))
    for p in os.listdir(os.path.join(ROOT, 'build', '_rom_free_gate')):
        if p.endswith('.thumbs'):
            os.remove(os.path.join(ROOT, 'build', '_rom_free_gate', p))
    return 0 if pages == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
