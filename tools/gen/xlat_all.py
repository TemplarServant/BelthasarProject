#!/usr/bin/env python3
r"""Regenerate src/xlat/*.c -- the five ROM programs that run translated -- from the user's ROM.

    python tools/gen/xlat_all.py [seq_c3 wipe_c3 owwin_c6 gwarp_fd hdma_fd]    (default: all five)
    python tools/gen/xlat_all.py --list                                         (each program's states and runs)

Each file is tools/gen/xlat.py over unique CPU traces (`ct.exe --cputrace`, CPUTRACE_UNIQUE=1) of the oracle -- the
cartridge under the rule, CT_ORACLE_LOGICAL=1, as tools/recipes.py builds every state -- plus a WRAM image the static
extension decodes the untaken side of a branch from (only code in WRAM reads it: the $C3 overlay at $7E:3000). The runs
are described in disasm/notes/sequence_table.md 11.2 and 11.2b, field_window_wipe.md 5 and 6, gate_warp_fd_family.md 9
and battle_ai_interp.md "The exit's HDMA"; they use recipe states where the demo reaches the same code from power-on:

  seq_c3    the race (C3 entry 0) and entry 3 from tools/recipes attract_race_handoff / attract_entry3_handoff (the
            attract under the rule, 5 and 17 frames before C3:0000), each plain and with an A press at three points
            (the abort paths); the WRAM images on the stage's last frame (the overlay is still in $7E:3000).
  wipe_c3   the field's window wipe C3:000E -> C3/01E4 in 16E and 01C (recipes attract_wipe_16e / attract_wipe_01c).
  owwin_c6  the overworld's window object, op 4E -> C6/E241 / C6/E27E (recipe attract_ow_window).
  gwarp_fd  the gate warp FD:FFF1 / FFEB / FFE8 / FFE5: the Millennial Fair's Telepod (scene 8, Lucca's telepod
            demonstration; Marle's pendant has opened the gate), recipe field_telepod_gate (it stands in for
            saves/field_re/scene8_gate_cutscene.state: Crono about to touch the left pod, obj11), the cutscene's A mash;
            the other three routines from the same cutscene 2300 frames in with the script's operand poked
            ($7F:269C = 81 / 82 / 83).
  hdma_fd   the HDMA table builder FD/C2C1-D52C, every line of the family any of three runs executed: the whole attract
            from power-on (recipe attract_boot: the builder with no message box, $0126 = 0), the GATO fight at the
            Millennial Fair (recipe gato_wait, waiting for GATO's first turn: the builder's $0126 = 2 pair FD/CFCF +
            FD/D27E, which no other run reaches; it stands in for saves/battle_re/gato_wait.state and gives the same
            file) and scene 8's Telepod cutscene (recipe field_telepod_gate, as gwarp_fd, with A every 30 frames: the
            $0126 = 1 pair FD/C995 + FD/CD0C). Note: against saves/field_re/scene8_gate_cutscene.state, a real-time
            capture whose main-loop park ($0152 = 0) lasts one frame less, the message window opens on the other
            parity of the double-buffered builder ($53 bit 0), so the recipe runs FD/C99A and not FD/CD11 (the capture
            the reverse). The code is the same; only those two lines' [static] marks differ.

A run that needs a capture which is not in the tree stops with the capture's name (tools/gen/manifest.json lists them
as "needs"). The traces are kept in build/xlat/<name>/ and re-made every run (they are cheap next to the recipes, which
tools/recipes.py caches).
"""
import argparse
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CT = os.path.join(ROOT, 'build', 'ct.exe')
ROM = 'Chrono_Trigger_USA_SNES.sfc'
RULE = {'CT_ORACLE_LOGICAL': '1'}

# The drives (LakeSnes serial order: A = 100).
def _press(at):
    return '0:%d,100:10,0:400' % at

S8_MASH = '0:15,' + ','.join(['100:3,0:27'] * 120)         # scene 8's cutscene: A for 3 frames every 30


def _cut(script, frames):
    """A --btnscript truncated to `frames` frames (--peek runs a script to its end)."""
    out, at = [], 0
    for tok in script.split(','):
        b, n = tok.split(':')
        n = int(n)
        if at + n >= frames:
            out.append('%s:%d' % (b, frames - at))
            break
        out.append(tok)
        at += n
    return ','.join(out)


GATO_6E_POKES = ('1111=6E,2607=63,2608=00,2609=63,260A=00,2657=63,2658=00,2659=63,265A=00,26A7=63,26A8=00,26A9=63,'
                 '26AA=00,2830=08,2831=08,2832=08,2837=FF,2838=FF,2839=FF,2980=00,2981=01,2982=02')
HDMA_RETURNS = ['C0:00CB', 'C0:00E4', 'C0:01AE', 'C0:0B15', 'C0:0B19', 'C0:0B32', 'C0:1909', 'C0:1951', 'C0:19BA']

# states: name -> ('recipe', recipe) | ('capture', path) | ('peek', state, btnscript, extra env)
# traces: (out, state, frames, extra env)            -- every trace is CPUTRACE_UNIQUE=1 under the rule
# images: name -> (state, btnscript)                 -- the .raw.wram a --peek leaves
# gen:    [(trace, image)...] + the options          -- tools/gen/xlat.py's command line
PROGRAMS = {
    'seq_c3': {
        'states': {'race': ('recipe', 'attract_race_handoff'), 'e3': ('recipe', 'attract_entry3_handoff')},
        'traces': [('race', 'race', 445, {'CPUTRACE_FROM': 'C30000'}),
                   ('e3', 'e3', 330, {'CPUTRACE_FROM': 'C30000'}),
                   ('race_a40', 'race', 200, {'CPUTRACE_FROM': 'C30000', 'CPUTRACE_BTN': _press(40)}),
                   ('race_a170', 'race', 300, {'CPUTRACE_FROM': 'C30000', 'CPUTRACE_BTN': _press(170)}),
                   ('race_a350', 'race', 440, {'CPUTRACE_FROM': 'C30000', 'CPUTRACE_BTN': _press(350)}),
                   ('e3_a50', 'e3', 200, {'CPUTRACE_FROM': 'C30000', 'CPUTRACE_BTN': _press(50)}),
                   ('e3_a100', 'e3', 250, {'CPUTRACE_FROM': 'C30000', 'CPUTRACE_BTN': _press(100)}),
                   ('e3_a250', 'e3', 330, {'CPUTRACE_FROM': 'C30000', 'CPUTRACE_BTN': _press(250)})],
        # the stage's last frame: the race's JML $C0:0000 runs in its frame 430, entry 3's in its frame 316
        'images': {'race': ('race', '0:430'), 'e3': ('e3', '0:300')},
        'gen': {'pairs': [('race', 'race'), ('e3', 'e3'), ('race_a40', 'race'), ('race_a170', 'race'),
                          ('race_a350', 'race'), ('e3_a50', 'e3'), ('e3_a100', 'e3'), ('e3_a250', 'e3')],
                'args': ['--from', 'C3:0000', '--stop', 'C0:0000']},
    },
    'wipe_c3': {
        'states': {'16e': ('recipe', 'attract_wipe_16e'), '01c': ('recipe', 'attract_wipe_01c')},
        'traces': [('16e', '16e', 34, {}), ('01c', '01c', 34, {})],
        'images': {'16e': ('16e', '0:0')},
        'gen': {'pairs': [('16e', '16e'), ('01c', '16e')], 'args': ['--from', 'C3:000E', '--stop', 'C0:22FE']},
    },
    'owwin_c6': {
        'states': {'ow': ('recipe', 'attract_ow_window')},
        'traces': [('ow', 'ow', 120, {})],
        'images': {'ow': ('ow', '0:0')},
        'gen': {'pairs': [('ow', 'ow')], 'args': ['--from', 'C6:E241', '--from', 'C6:E27E', '--stop', 'C2:1830']},
    },
    'gwarp_fd': {
        'states': {'s8': ('recipe', 'field_telepod_gate'),
                   's8_f2300': ('peek', 's8', _cut(S8_MASH, 2300), {})},
        'traces': [('w80', 's8', 3230, {'CPUTRACE_FROM': 'FDFFF1', 'CPUTRACE_BTN': S8_MASH}),
                   ('w81', 's8_f2300', 420, {'CPUTRACE_FROM': 'FDFFEB', 'PEEK_POKE': '7F269C=81'}),
                   ('w82', 's8_f2300', 320, {'CPUTRACE_FROM': 'FDFFE8', 'PEEK_POKE': '7F269C=82'}),
                   ('w83', 's8_f2300', 930, {'CPUTRACE_FROM': 'FDFFE5', 'PEEK_POKE': '7F269C=83'})],
        'images': {'s8': ('s8', S8_MASH)},
        'gen': {'pairs': [('w80', 's8'), ('w81', 's8'), ('w82', 's8'), ('w83', 's8')],
                'args': ['--from', 'FD:FFF1', '--from', 'FD:FFEB', '--from', 'FD:FFE8', '--from', 'FD:FFE5',
                         '--stop', 'C0:0000']},
    },
    'hdma_fd': {
        # Enemy 6E's exit tail (gato_wait.state with GATO_6E_POKES, 898 frames of '100:8,0:890', then 17 traced)
        # reaches no line of the family these three runs miss, so it is not run. The attract runs from power-on;
        # saves/attract_cena_inicial.state (11200 frames from the opening scene) gives the same instructions.
        'states': {'boot': ('recipe', 'attract_boot'),
                   'gato': ('recipe', 'gato_wait'),
                   's8': ('recipe', 'field_telepod_gate')},
        # the attract first: from power-on its first line of the family is the entry FD:C2C1, which --from needs
        'traces': [('att', 'boot', 13600, {}), ('gato', 'gato', 220, {}),
                   ('s8', 's8', 400, {'CPUTRACE_BTN': _cut(S8_MASH, 165)})],
        'images': {'boot': ('boot', '0:0')},
        # FD/C2C1 is called from the host's code: the family's own lines (FD:C2C1-D52C), each first seen once
        'family': (0xFDC2C1, 0xFDD52C),
        'gen': {'pairs': [('family', 'boot')],'args': ['--from', 'FD:C2C1'] + sum([['--stop', a] for a in HDMA_RETURNS], [])},
    },
}


def say(msg):
    print('xlat_all: ' + msg, flush=True)


def ct(args, env, log):
    e = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', **RULE)
    e.update(env)
    p = subprocess.run([CT] + args, cwd=ROOT, env=e, capture_output=True, text=True, errors='replace')
    with open(log, 'a', encoding='utf-8') as f:
        f.write('$ %s ct.exe %s\n%s%s\n' % (' '.join('%s=%s' % kv for kv in env.items()), ' '.join(args),
                                             p.stdout[-2000:], p.stderr[-1000:]))
    return p


def recipe_state(names):
    if names and subprocess.run([sys.executable, 'tools/recipes.py', 'make'] + names, cwd=ROOT).returncode:
        sys.exit('xlat_all: a recipe failed: %s' % ' '.join(names))
    return {n: os.path.join(ROOT, 'build', 'recipes', n + '.state') for n in names}


def needs(name):
    return [s[1] for s in PROGRAMS[name]['states'].values() if s[0] == 'capture']


def make(name):
    P = PROGRAMS[name]
    out = os.path.join(ROOT, 'build', 'xlat', name)
    os.makedirs(out, exist_ok=True)
    log = os.path.join(out, 'run.log')
    open(log, 'w').close()
    missing = [c for c in needs(name) if not os.path.exists(os.path.join(ROOT, c))]
    if missing:
        sys.exit('xlat_all: %s needs %s (a capture not yet replaced by a recipe)' % (name, ', '.join(missing)))
    st = {}
    st.update(recipe_state([s[1] for s in P['states'].values() if s[0] == 'recipe']))
    path = {}
    for k, s in P['states'].items():
        if s[0] == 'recipe':
            path[k] = st[s[1]]
        elif s[0] == 'capture':
            path[k] = os.path.join(ROOT, s[1])
    for k, s in P['states'].items():                          # derived states: a --peek from another one
        if s[0] == 'peek':
            path[k] = os.path.join(out, k + '.state')
            ct(['--peek', path[s[1]], os.path.join(out, k + '_peek.raw'), '--btnscript', s[2]],
               dict(s[3], PEEK_SAVE=path[k]), log)
            if not os.path.exists(path[k]):
                sys.exit('xlat_all: %s: the state %s was not made (see %s)' % (name, k, log))
    jobs = []
    for t, s, frames, env in P['traces']:
        tp = os.path.join(out, 't_%s.txt' % t)
        jobs.append((['--cputrace', path[s], str(frames), tp], dict(env, CPUTRACE_UNIQUE='1'), tp))
    for i, (s, script) in P['images'].items():
        jobs.append((['--peek', path[s], os.path.join(out, 'img_%s.raw' % i), '--btnscript', script], {},
                     os.path.join(out, 'img_%s.raw.wram' % i)))
    with ThreadPoolExecutor(max_workers=max(1, min(len(jobs), os.cpu_count() or 4))) as ex:
        for args, env, want in jobs:
            if os.path.exists(want):
                os.remove(want)
        list(ex.map(lambda j: ct(j[0], j[1], log), jobs))
    for args, env, want in jobs:
        if not os.path.exists(want):
            sys.exit('xlat_all: %s: %s was not written (see %s)' % (name, os.path.relpath(want, ROOT), log))
    if 'family' in P:                                         # the family's own lines out of every trace
        lo, hi = P['family']
        seen, n = set(), 0
        with open(os.path.join(out, 't_family.txt'), 'w') as f:
            for t, _, _, _ in P['traces']:
                for ln in open(os.path.join(out, 't_%s.txt' % t)):
                    if ln[2:3] != ':' or ln.startswith('--'):
                        continue
                    adr = int(ln[0:2], 16) << 16 | int(ln[3:7], 16)
                    if not lo <= adr <= hi:
                        continue
                    key = ln[:16] + ln[ln.index(' P='):ln.index(' P=') + 12]
                    if key not in seen:
                        seen.add(key)
                        f.write(ln)
                        n += 1
        first = open(os.path.join(out, 't_family.txt')).readline()
        if not first.startswith('%02X:%04X ' % (lo >> 16, lo & 0xFFFF)):
            sys.exit('xlat_all: %s: the family does not begin at its entry (%s)' % (name, first[:7]))
        say('%s: %d lines of FD/%04X-%04X' % (name, n, lo & 0xFFFF, hi & 0xFFFF))
    cmd = [sys.executable, 'tools/gen/xlat.py', '--name', name, '--rom', ROM]
    for t, i in P['gen']['pairs']:
        cmd += ['--trace', os.path.join(out, 't_%s.txt' % t), '--wram', os.path.join(out, 'img_%s.raw.wram' % i)]
    cmd += P['gen']['args']
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    print(p.stdout, end='', flush=True)
    if p.returncode:
        sys.exit('xlat_all: %s: tools/gen/xlat.py failed: %s' % (name, p.stderr[-600:]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('names', nargs='*')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    names = a.names or list(PROGRAMS)
    for n in names:
        if n not in PROGRAMS:
            sys.exit('xlat_all: no program %s (%s)' % (n, ', '.join(PROGRAMS)))
    if a.list:
        short = lambda v: v if len(v) <= 48 else v[:40] + '... (%d chars)' % len(v)
        for n in names:
            P = PROGRAMS[n]
            print('%s:' % n)
            for k, s in P['states'].items():
                print('  state %-9s %s' % (k, ' '.join(short(str(x)) for x in s if x != {})))
            for t, s, frames, env in P['traces']:
                print('  trace %-9s from %s, %d frames %s' % (t, s, frames, ' '.join('%s=%s' % (k, short(v)) for k, v in env.items())))
        return 0
    if not os.path.exists(CT):
        sys.exit('xlat_all: build the oracle first (python tools/build.py ct)')
    for n in names:
        make(n)
    return 0


if __name__ == '__main__':
    sys.exit(main())
