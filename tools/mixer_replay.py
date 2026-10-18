#!/usr/bin/env python3
"""The native mixer (src/sound/mixer.c) against the vendored S-DSP (snes/dsp.c), sample for sample, over real runs.

    python tools/mixer_replay.py [RUN ...] [--keep] [--list]

Each RUN is Belthasar run with CT_MIXER_CAPTURE=<file> (src/audio.c: the S-DSP's state and the SPC's RAM at the start,
every register write, RAM change and register read at its APU cycle, the live stream's hashes), then
`build/ct.exe --mixreplay <file>`: both mixers from the same state, the same events at the same sample. Prints per run

    mixer: <run>: N samples identical / M
    mixer: <run>: reads N identical / M, sound RAM identical at N / M segment ends
    replay: <run>: the reference matches the live run in N / M chunks
    census: <run>: echo-in=... (what the run exercised)

and the same totals over all runs as `mixer: all: ...`. The runs: `attract` = the whole attract (`--scene 433`, the
paced loop's lockstep vehicle to f11400, as tools/attract_divergence.py runs it); the others borrow a quick-tier check's
own argv and environment (tools/regress_checks.json) -- battles, the save screen, a shop, the menu's jingle, the world
map. With no RUN all are run. The capture files go to build/_mixer/ and are deleted unless --keep.
(disasm/notes/audio_mixer.md sec. 9.)
"""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BELTHASAR = os.path.join('build', 'Belthasar.exe')
CT = os.path.join('build', 'ct.exe')
OUT = os.path.join('build', '_mixer')

# run name -> a quick-tier check whose argv/env it borrows (None = its own argv below)
RUNS = [
    ('attract', None),
    ('gato-battle', 'battle-end-pc-stands-where-the-fight-left-it-vs-rule-cartridge'),
    ('nuns-battle', 'battle-flame-toss-band-vs-rule-cartridge'),
    ('magus-battle', 'battle-xstrike-death-fade-waits-for-the-effect-vs-rule-cartridge'),
    ('save-screen', 'field-savepoint-a-opens-the-save-screen-vs-rule-cartridge'),
    ('shop', 'shop-trade-sound-keys-twice-like-the-cartridge'),
    ('menu-jingle', 'field-menu-save-jingle-and-write'),
    ('world-song', 'field-ow-entry-song-reaches-service'),
    ('fuzz', 'FUZZ'),
]
FUZZ_CASES, FUZZ_SEED = '200', '1'   # ct.exe --mixfuzz: random state, RAM and writes (GAIN, saturation: no game run has them)
ATTRACT = ([BELTHASAR, '--assets', 'ct_assets.dat', '--scene', '433'],
           dict(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
                CT_SHOTS=os.path.join(OUT, 'attract_last'), CT_SHOTS_AT='11400', CT_SHOTS_EXIT='1'))


def own_files(name, argv, env):
    """A borrowed check's own output files (build/_x...) moved to build/_mixer/<run>_x..., so the two never collide."""
    def mv(v):
        return os.path.join(OUT, name + '_' + v[len('build/_'):]) if isinstance(v, str) and v.startswith('build/_') else v
    return [mv(a) for a in argv], {k: mv(v) for k, v in env.items()}


def check_run(name):
    with open(os.path.join(ROOT, 'tools', 'regress_checks.json'), encoding='utf-8') as f:
        d = json.load(f)
    for c in (d['checks'] if isinstance(d, dict) else d):
        if c['name'] == name:
            return list(c['argv']), dict(c.get('env') or {})
    sys.exit('mixer_replay: no check %s in tools/regress_checks.json' % name)


def parse(out):
    got = {}
    for line in out.splitlines():
        if line.startswith('mixer: ') and 'samples identical' in line:
            a, b = line.split(':', 1)[1].split('samples identical /')
            got['same'], got['samples'] = int(a), int(b)
        elif line.startswith('mixer: reads '):
            a, b = line[len('mixer: reads '):].split(' identical / ')
            got['rsame'], got['reads'] = int(a), int(b)
        elif line.startswith('mixer: sound RAM identical at '):
            a, b = line[len('mixer: sound RAM identical at '):].split(' segment ends')[0].split(' / ')
            got['ramsame'], got['segs'] = int(a), int(b)
        elif line.startswith('replay: ') and 'chunks of' in line:
            t = line.split('matches the live run in ')[1].split(' chunks')[0]
            a, b = t.split(' / ')
            got['csame'], got['chunks'] = int(a), int(b)
        elif line.startswith('census:'):
            got['census'] = dict(kv.split('=') for kv in line.split()[1:])
    return got


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    keep = '--keep' in sys.argv
    if '--list' in sys.argv:
        for n, c in RUNS:
            print('%-20s %s' % (n, c or 'Belthasar --scene 433, lockstep, to f11400'))
        return 0
    want = [r for r in RUNS if not args or r[0] in args]
    if args and len(want) != len(args):
        sys.exit('mixer_replay: unknown run (know: %s)' % ', '.join(n for n, _ in RUNS))
    os.chdir(ROOT)
    os.makedirs(OUT, exist_ok=True)
    tot = dict(same=0, samples=0, rsame=0, reads=0, ramsame=0, segs=0, csame=0, chunks=0)
    bad = 0
    for name, check in want:
        if check == 'FUZZ':
            p = subprocess.run([os.path.join(ROOT, CT), '--mixfuzz', FUZZ_CASES, FUZZ_SEED], capture_output=True, text=True,
                               timeout=900)
            for line in p.stdout.splitlines():
                print(line if line.startswith('mixfuzz:') else line.replace('census:', 'census: fuzz:', 1))
            if p.returncode != 0 or 'mixfuzz:' not in p.stdout:
                bad += 1
            continue
        argv, env = ATTRACT if check is None else own_files(name, *check_run(check))
        cap = os.path.join(OUT, name + '.cap')
        if os.path.exists(cap):
            os.remove(cap)
        e = dict(os.environ, **env)
        e['CT_MIXER_CAPTURE'] = cap
        argv = [os.path.join(ROOT, argv[0])] + argv[1:]
        subprocess.run(argv, env=e, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900)
        if not os.path.exists(cap) or os.path.getsize(cap) < 8:
            print('mixer: %s: the run wrote no capture' % name)
            bad += 1
            continue
        p = subprocess.run([os.path.join(ROOT, CT), '--mixreplay', cap], capture_output=True, text=True, timeout=900)
        for line in p.stdout.splitlines():
            if 'first difference' in line or 'left the live run' in line or 'different sound RAM' in line \
                    or 'live chunks reached' in line or line.startswith('mixreplay:'):
                print('%s: %s' % (name, line))
        g = parse(p.stdout)
        if 'samples' not in g:
            print('mixer: %s: the replay printed no result (rc %d)' % (name, p.returncode))
            bad += 1
            continue
        for k in tot:
            tot[k] += g.get(k, 0)
        print('mixer: %s: %d samples identical / %d' % (name, g['same'], g['samples']))
        print('mixer: %s: reads %d identical / %d, sound RAM identical at %d / %d segment ends'
              % (name, g['rsame'], g['reads'], g['ramsame'], g['segs']))
        print('replay: %s: the reference matches the live run in %d / %d chunks' % (name, g['csame'], g['chunks']))
        print('census: %s: %s' % (name, ' '.join('%s=%s' % kv for kv in g.get('census', {}).items())))
        if p.returncode != 0:
            bad += 1
        if not keep:
            os.remove(cap)
    exact = not bad and tot['same'] == tot['samples'] and tot['rsame'] == tot['reads'] and tot['ramsame'] == tot['segs'] \
        and tot['csame'] == tot['chunks']
    print('mixer: all: %d samples identical / %d' % (tot['same'], tot['samples']))
    print('mixer: all: reads %d identical / %d, sound RAM identical at %d / %d segment ends, live chunks %d / %d'
          % (tot['rsame'], tot['reads'], tot['ramsame'], tot['segs'], tot['csame'], tot['chunks']))
    print('mixer: verdict: %s' % ('SAMPLE-EXACT' if exact else 'DIFFERENT'))
    return 0 if exact else 1


if __name__ == '__main__':
    sys.exit(main())
