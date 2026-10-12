#!/usr/bin/env python3
"""shadow_route.py -- build a shadow route (tools/shadow_routes/*.btn) one leg at a time.

A route is the oracle's pad, frame by frame, from power-on under the logical rule (docs/SHADOW_PROPOSAL.md, S2): its
file is the --btnscript grammar (`hexbtn:frames`, LakeSnes serial order: B=1 Y=2 Sel=4 Start=8 Up=10 Down=20 Left=40
Right=80 A=100 X=200 L=400 R=800) over many lines, `#` comments. Every leg below plays one stretch from the state the
previous leg left and shows where it ended, so the route is written by looking, never by guessing.

  leg  <from.state | power-on> "<btnscript>" <frames> <out-prefix> [--sram FILE]
       runs `ct.exe --shadow` from the state (or from power-on, with the battery save), pads the script with an idle
       pad to <frames>, writes <out-prefix>.state and <out-prefix>.png (the oracle's picture at the end) and prints the
       shadow's totals, the frame and the location it ended at, and the padded script -- the line to append to the route.
  check <route.btn> <frames> [--sram FILE]
       plays the whole route from power-on and prints the shadow's report (what the suite check pins).

Run from the repo root.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')


def script_frames(script):
    n = 0
    for tok in filter(None, script.split(',')):
        n += int(tok.split(':')[1]) if ':' in tok else 1
    return n


def padded(script, frames):
    used = script_frames(script)
    if used > frames:
        sys.exit('leg: the script holds %d frames, more than the leg\'s %d' % (used, frames))
    return ','.join(filter(None, [script, '0:%d' % (frames - used) if frames > used else '']))


def run(args, sram):
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    if sram:
        env['CT_ORACLE_SRAM'] = sram
    return subprocess.run([CT] + args, cwd=ROOT, env=env, capture_output=True, text=True).stdout


def cmd_leg(a):
    src, script, frames, out = a[0], a[1], int(a[2]), a[3]
    sram = a[a.index('--sram') + 1] if '--sram' in a else None
    script = padded(script, frames)
    args = ['--shadow', '--frames', str(frames), '--btnscript', script, '--stateout', out + '.state']
    if src != 'power-on':
        args[1:1] = ['--loadstate', src]
    rep = run(args, sram)
    for line in rep.splitlines():
        if line.startswith('shadow:') or line.startswith('  mismatch') or line.startswith('  exit'):
            print(line)
    run(['--peek', out + '.state', out + '.raw', '--frames', '1'], None)
    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'ct.py'), 'fb2png', out + '.raw', out + '.png'],
                   cwd=ROOT, capture_output=True)
    print('picture: %s.png' % out)
    print('leg script: %s' % script)


def cmd_check(a):
    route, frames = a[0], a[1]
    sram = a[a.index('--sram') + 1] if '--sram' in a else None
    sys.stdout.write(run(['--shadow', '--frames', frames, '--btnfile', route], sram))


if __name__ == '__main__':
    if len(sys.argv) < 2 or sys.argv[1] not in ('leg', 'check'):
        sys.exit(__doc__)
    {'leg': cmd_leg, 'check': cmd_check}[sys.argv[1]](sys.argv[2:])
