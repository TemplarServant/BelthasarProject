#!/usr/bin/env python3
r"""The sound's data from the asset container against the ROM: the same run must write the same S-DSP registers.

    python tools/snd_assets_cmp.py [frames]     (default 4000: the attract's first songs, its battles' and effects')

Runs Belthasar on the attract (`--scene 433`, CT_AUDIO_LOCKSTEP=1) twice to the same frame with CT_DSP_LOG and
CT_AUDIO_PCM (the output the mixer makes -- where a sample's or an echo's bytes would show): once with the
driver, songs, samples, instruments and effect sets from ct_assets.dat (CT_ASSET_SND_*, assets/extract_assets.py
pack_sound), once from the ROM (CT_AUDIO_FROM_ROM=1). Every write -- the frame, the SPC's cycle, the register, the value
-- and every output sample must be the same:

    snd-assets: f<frames> <n> S-DSP writes and <m> output samples from the container == the ROM's (sha1 <hash>)

The data themselves are compared item by item by `ct.exe --sndassets ct_assets.dat` (src/audio.c CtAudioDataCompare).

A container without the sound's entries runs from the ROM on both sides and the tool says so.
"""
import hashlib
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, 'build', 'Belthasar.exe')


def run(frames, from_rom, out):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_SHOTS=os.path.join(ROOT, 'build', '_snd_assets_cmp', 'shot'), CT_SHOTS_AT=str(frames),
               CT_SHOTS_EXIT='1', CT_DSP_LOG=out, CT_AUDIO_PCM=out + '.pcm')
    env.pop('CT_AUDIO_FROM_ROM', None)
    if from_rom:
        env['CT_AUDIO_FROM_ROM'] = '1'
    p = subprocess.run([EXE, '--assets', 'ct_assets.dat', '--scene', '433'], cwd=ROOT, env=env, capture_output=True,
                       text=True, errors='replace')
    src = 'container' if 'from the asset container' in p.stderr + p.stdout else 'ROM'
    return src


def main():
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 4000
    d = os.path.join(ROOT, 'build', '_snd_assets_cmp')
    os.makedirs(d, exist_ok=True)
    a, b = os.path.join(d, 'assets.dsp'), os.path.join(d, 'rom.dsp')
    sa = run(frames, False, a)
    sb = run(frames, True, b)
    if sa != 'container':
        print('snd-assets: ct_assets.dat holds no sound entries (CT_ASSET_SND_*): both runs read the ROM')
        return 1
    da, db = open(a, 'rb').read(), open(b, 'rb').read()
    pa, pb = open(a + '.pcm', 'rb').read(), open(b + '.pcm', 'rb').read()
    n = da.count(b'\n')
    if pa != pb:
        k = next((i for i in range(0, min(len(pa), len(pb)), 4) if pa[i:i + 4] != pb[i:i + 4]), min(len(pa), len(pb)))
        print('snd-assets: f%d the output differs from sample %d (frame %d)' % (frames, k // 4, k // 4 // 534))
        return 1
    if da != db:
        la, lb = da.split(b'\n'), db.split(b'\n')
        k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
        print('snd-assets: f%d the container and the ROM differ at write %d: %s | %s'
              % (frames, k, la[k].decode() if k < len(la) else '-', lb[k].decode() if k < len(lb) else '-'))
        return 1
    if not pa:
        print('snd-assets: no output written (CT_AUDIO_PCM)')
        return 1
    print('snd-assets: f%d %d S-DSP writes and %d output samples from the container == the ROM\'s (sha1 %s)'
          % (frames, n, len(pa) // 4, hashlib.sha1(da + pa).hexdigest()[:12]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
