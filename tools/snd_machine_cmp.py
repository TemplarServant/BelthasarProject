#!/usr/bin/env python3
"""The game's sound machine (src/sound/machine.h: the sound processor's program in C) against the vendored SPC700 it
replaced, run for run: what the player hears, sample for sample, and everything the run prints.

    python tools/snd_machine_cmp.py attract          the whole attract (Belthasar --scene 433, to f11200)
    python tools/snd_machine_cmp.py quick [--jobs N]  every Belthasar run of the regression suite's quick tier

Each run twice, CT_AUDIO_SPC=ref (the vendored SPC700 and S-DSP, snes/apu.c) and CT_AUDIO_SPC=machine (the game's
default), with CT_AUDIO_PCM (src/audio.c: the frames as played, 534 samples each); the two PCM files must be equal byte
for byte, the two stdouts equal line for line (but the line naming the engine), and the attract's last picture equal.
A borrowed check's own output files are moved to build/_sndmach/<mode>/ so the runs never collide.

    snd-machine: attract: 5982834 samples identical / 5982834, stdout identical, picture identical
    snd-machine: quick: 412 runs (120 with sound), PCM identical in 412 / 412 (21630204 samples), stdout identical in 412 / 412
"""
import concurrent.futures
import filecmp
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join('build', '_sndmach')
EXE = os.path.join('build', 'Belthasar.exe')
ATTRACT = ([EXE, '--assets', 'ct_assets.dat', '--scene', '433'],
           dict(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
                CT_SHOTS='build/_sndmach_attract', CT_SHOTS_AT='11200', CT_SHOTS_EXIT='1'))


def moved(mode, name, argv, env):
    def mv(v):
        if isinstance(v, str) and v.startswith('build/_'):
            return os.path.join(OUT, mode, name + '_' + v[len('build/_'):])
        return v
    return [mv(a) for a in argv], {k: mv(v) for k, v in env.items()}


def run(mode, name, argv, env, timeout):
    argv, env = moved(mode, name, argv, env)
    pcm = os.path.join(OUT, mode, name + '.pcm')
    if os.path.exists(pcm):
        os.remove(pcm)
    e = dict(os.environ, **env)
    e['CT_AUDIO_SPC'] = mode
    e['CT_AUDIO_PCM'] = pcm
    # The sound's own clock: one frame of sound a frame of the game (CT_AUDIO_LOCKSTEP, the paced loop's lockstep
    # vehicle) and no wall-clock pacing -- otherwise SDL's audio thread takes as many frames as the host had time for and
    # no two runs are the same, the reference's against itself neither.
    e['CT_NOPACE'] = '1'
    e['CT_AUDIO_LOCKSTEP'] = '1'
    try:
        p = subprocess.run([os.path.join(ROOT, argv[0])] + argv[1:], env=e, cwd=ROOT, capture_output=True, timeout=timeout)
        out = p.stdout.decode('utf-8', 'replace')
    except subprocess.TimeoutExpired:
        out = 'TIMEOUT'
    # the lines that cannot be equal: the engine's name, the sound RAM's hash (the machine's RAM keeps no code and no
    # stack: the driver is C), the host's wall-clock pacing, the run's own output paths
    lines = []
    for l in out.splitlines():
        if l.startswith('[audio]') or l.startswith('audiopace:'):
            continue
        l = re.sub(r'aramfnv=[0-9A-F]+', 'aramfnv=*', l)
        lines.append(l.replace(os.path.join(OUT, mode), os.path.join(OUT, '<mode>')))
    return pcm, lines, argv, env


def pair(name, argv, env, timeout):
    a = run('ref', name, argv, env, timeout)
    b = run('machine', name, argv, env, timeout)
    pa, pb = a[0], b[0]
    na = os.path.getsize(pa) if os.path.exists(pa) else -1
    nb = os.path.getsize(pb) if os.path.exists(pb) else -1
    same = (na == nb == -1) or (na == nb and filecmp.cmp(pa, pb, shallow=False))   # (no frame played: nothing written)
    return name, same, max(na, 0) // 4, a[1] == b[1], a, b, na >= 0


def main():
    what = [x for x in sys.argv[1:] if not x.startswith('--')]
    jobs = 8
    if '--jobs' in sys.argv:
        jobs = int(sys.argv[sys.argv.index('--jobs') + 1])
        what = [x for x in what if x != str(jobs)]
    os.chdir(ROOT)
    for m in ('ref', 'machine'):
        os.makedirs(os.path.join(OUT, m), exist_ok=True)
    bad = 0
    if not what or 'attract' in what:
        name, same, n, out_same, a, b, _ = pair('attract', ATTRACT[0], ATTRACT[1], 900)
        pic = lambda r: r[3]['CT_SHOTS'] + '_f11200.raw'
        pic_same = os.path.exists(pic(a)) and os.path.exists(pic(b)) and filecmp.cmp(pic(a), pic(b), shallow=False)
        print('snd-machine: attract: %d samples %s / %d, stdout %s, picture %s'
              % (n if same else 0, 'identical' if same else 'DIFFER', n, 'identical' if out_same else 'DIFFERS',
                 'identical' if pic_same else 'DIFFERS'))
        bad += not (same and out_same and pic_same)
    if 'quick' in what:
        with open(os.path.join(ROOT, 'tools', 'regress_checks.json'), encoding='utf-8') as f:
            d = json.load(f)
        runs = [c for c in d['checks'] if c.get('tier') == 'quick' and c.get('argv') and
                c['argv'][0].replace('\\', '/') == 'build/Belthasar.exe' and
                all(os.path.exists(os.path.join(ROOT, i)) for i in c.get('inputs', []))]
        res = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
            futs = [ex.submit(pair, c['name'], list(c['argv']), dict(c.get('env') or {}), c.get('timeout_s', 120) * 3)
                    for c in runs]
            for f in concurrent.futures.as_completed(futs):
                res.append(f.result())
        pcm_ok = sum(1 for r in res if r[1])
        out_ok = sum(1 for r in res if r[3])
        for r in sorted(res):
            if not (r[1] and r[3]):
                print('snd-machine: quick: %s: PCM %s, stdout %s' % (r[0], 'identical' if r[1] else 'DIFFERS',
                                                                       'identical' if r[3] else 'DIFFERS'))
        print('snd-machine: quick: %d runs (%d with sound), PCM identical in %d / %d (%d samples), stdout identical in %d / %d'
              % (len(res), sum(1 for r in res if r[6]), pcm_ok, len(res), sum(r[2] for r in res if r[1]), out_ok, len(res)))
        bad += (pcm_ok != len(res)) + (out_ok != len(res))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
