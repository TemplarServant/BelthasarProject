#!/usr/bin/env python3
r"""The textbox machine's bytes, port against the rule's cartridge, every frame (the alignment of ct-frame-alignment-p2).

    python tools/textbox_bytes_cmp.py <drive>...

The box machine C0/1F87 -- $0129 its state (1-5 the open ramp, 6 the typewriter, 7 waiting, 0D the close ramp), $0126
the window, $0131 the LINE cursor C0/2126 steps, $0134 the accepted-press latch -- read on both sides from the same
state with the same presses: the cartridge on `ct.exe --monitor` (CT_ORACLE_LOGICAL=1), the port on CT_FIELD_MEMMON
from the state's WRAM with its sound processor seeded from the state (CT_AUDIO_SEEDSTATE: a song load costs the SPC
time it costs the cartridge). The field's WRAM aligns at the SAME frame number, and the port's frame N carries the
cartridge's pad of frame N-1, so the port's presses are the cartridge's + 1 (as door_list_cmp.py / dash_run_cmp.py
drive them).

Why not the lockstep projection (CT_FIELDLOCK + CT_PROJECT=box): there the oracle runs its whole frame before the
port's and both get the same pad, so on a state saved at a frame's end (every recipe state) every transition the
script makes by itself reads one frame apart (the oracle's late commit against the port's not-yet), and every press
reaction reads together. The owner's s439 capture had the opposite phase (its first frame ran no main-loop iteration),
which is what made that projection CLEAN on it.

  s439   saves/field_re/s439_marle_choice_lake.state: Marle's pendant dialogue (scene 439), A every 100 frames from
         f100 -- str 0x29's pages and decision, str 0x2B's, the join's song load (45 frames of SPC on both sides), the
         boxes after it.

    textbox-bytes: <drive> f1-f<N>: <k> of <N> frame(s) differ [frames]
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
WATCH = [('0129', 1), ('0126', 1), ('0131', 1), ('0134', 2)]

# name: (cartridge state, port WRAM seed, scene, the cartridge's press frames (A), frames)
DRIVES = {
    's439': ('saves/field_re/s439_marle_choice_lake.state', 'saves/field_re/s439_marle_choice.lakewram', 439,
             [100, 200, 300, 400, 500, 600, 700], 1100),
}


def script(presses, total, shift, btn, idle, sep):
    out, t = [], 0
    for f in presses:
        f += shift
        if f > t:
            out.append(idle % (f - t))
        out.append(btn)
        t = f + 1
    out.append(idle % (total - t))
    return sep.join(out)


def cartridge(st, presses, total):
    p = subprocess.run([CT, '--monitor', ','.join('7E%s:%d' % w for w in WATCH), '--from', st, '--btnscript',
                        script(presses, total, 0, '100:1', '0:%d', ','), '--frames', str(total)], cwd=ROOT,
                       env=dict(os.environ, CT_ORACLE_LOGICAL='1', SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'),
                       capture_output=True, text=True, errors='replace')
    rows = {}
    for line in p.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', line)
        if m:
            rows[int(m.group(1))] = tuple(v.rstrip('*') for v in re.findall(r'7E[0-9A-F]{4}=([0-9A-F]+\*?)', m.group(2)))
    return rows


def port(st, seed, scene, presses, total):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_SEEDSTATE=st,
               CT_FIELD_MEMMON=','.join('%s:%d' % w for w in WATCH))
    p = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', str(scene), '--loadwram', seed, '--selftest',
                        '--frames', str(total), '--walkscript', script(presses, total, 1, 'a:1', 'n:%d', ',')],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    rows, cur = {}, None
    ch = {}
    for line in p.stdout.splitlines() + p.stderr.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\w+\s+(.*)', line)
        if m:
            ch[int(m.group(1))] = tuple(v.rstrip('*') for v in re.findall(r'[0-9A-F]{4}=([0-9A-F]+\*?)', m.group(2)))
    for f in range(total):
        cur = ch.get(f, cur)
        rows[f] = cur
    return rows


def main():
    names = sys.argv[1:] or list(DRIVES)
    for name in names:
        if name not in DRIVES:
            sys.exit('textbox-bytes: no drive %s' % name)
        st, seed, scene, presses, total = DRIVES[name]
        c, p = cartridge(st, presses, total), port(st, seed, scene, presses, total)
        frames = [f for f in range(1, total) if f in c]
        bad = [f for f in frames if c[f] != p.get(f)]
        print('textbox-bytes: %s f1-f%d: %d of %d frame(s) differ%s' % (name, total - 1, len(bad), len(frames),
              (' ' + ','.join(map(str, bad[:20]))) if bad else ''))
        for f in bad[:4]:
            print('  f%d cartridge %s port %s' % (f, ' '.join(c[f]), ' '.join(p.get(f) or ('-',))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
