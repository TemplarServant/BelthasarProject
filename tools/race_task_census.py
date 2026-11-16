#!/usr/bin/env python3
r"""The race's tasks and hooks, as its overlay can install them, against the native stage's cases.

    python tools/race_task_census.py [-v]

The race (sequence entries 0 / 9, `$7E:3B48`) runs in tasks (`$0920`) and vblank hooks (`$0940`) that its own code and its
two scripts put on the lists. The native stage (src/seq_race.c, src/seq_racepic.c, on src/seq_stage.c) runs a task or a hook
only if it has a `case` for its address; one it does not know is skipped without a word. That is how the win's three tasks
went unported until a player won the race (GitHub issue 10, 2026-11-15): the finish swapped them in and the stage ran
nothing in their slots.

This lists, from the overlay as it sits in WRAM during the race (the cartridge played from saves/race_re/race_user.state):

  * what the race's CODE installs -- in `$7E:3B48`..`$7E:53BA` (entry 4's setup starts at `$53BB`), every `LDX #addr`
    followed by `JSR $395E` (a task), `JSR $3974` (a hook), `JMP $39B1` (this task becomes) or `JMP $39E8` (this hook
    becomes);
  * what its SCRIPTS install -- `taskadd` / `hookadd` in every script reachable from the IPs the code stores into `$0A20` /
    `$0B20` (`LDX` / `LDA #addr`, `STX` / `STA $0A20`) and from the two words of `$3F59`'s table at `$7E:3F66`
    (tools/race_script_walk.py);

and checks each address against the `case 0x....` labels of the three sources. Prints

    racecensus: <t> tasks and <h> hooks the race's code and scripts install, <n> without a native case

and each one without a case (`-v`: every one, with where it is installed).
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
ROM = os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc')
STATE = os.path.join(ROOT, 'saves', 'race_re', 'race_user.state')
OUT = os.path.join(ROOT, 'build', '_racecensus')
CODE = (0x3B48, 0x53BB)
SOURCES = ['src/seq_race.c', 'src/seq_racepic.c', 'src/seq_stage.c']
INSTALL = {bytes([0x20, 0x5E, 0x39]): 'task', bytes([0x20, 0x74, 0x39]): 'hook',
           bytes([0x4C, 0xB1, 0x39]): 'task', bytes([0x4C, 0xE8, 0x39]): 'hook'}


def overlay():
    os.makedirs(OUT, exist_ok=True)
    raw = os.path.join(OUT, 'race_%d.raw' % os.getpid())
    subprocess.run([CT, ROM, '--peek', STATE, raw, '--btnscript', '0:30,100:2,0:268'], cwd=ROOT,
                   env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True)
    w = open(raw + '.wram', 'rb').read()
    return w, raw + '.wram'


def main():
    verbose = '-v' in sys.argv[1:]
    w, path = overlay()
    found = {}                                             # (kind, addr) -> where

    def note(kind, t, where):
        found.setdefault((kind, t), []).append(where)
    starts = set()
    for a in range(CODE[0], CODE[1] - 6):
        t = w[a + 1] | w[a + 2] << 8
        nxt = bytes(w[a + 3:a + 6])
        if w[a] == 0xA2 and nxt in INSTALL:                # LDX #addr, then the list routine
            note(INSTALL[nxt], t, 'code %04X' % a)
        elif w[a] in (0xA2, 0xA9) and nxt[0] in (0x8E, 0x8D) and nxt[1] == 0x20 and nxt[2] in (0x0A, 0x0B):
            starts.add(t)                                  # LDX / LDA #addr, STX / STA $0A20 / $0B20: a script's start
    for a in (0x3F66, 0x3F68):                             # $3F59's table: the two camera intros (the call's own step: + 2)
        starts.add(((w[a] | w[a + 1] << 8) + 2) & 0xFFFF)
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'race_script_walk.py'), path] +
                       ['%04X' % s for s in sorted(starts)], cwd=ROOT, capture_output=True, text=True, errors='replace').stdout
    for line in r.splitlines():
        m = re.match(r'(tasks|hooks):\s*(.*)', line)
        if m:
            for t in re.findall(r'add ([0-9A-F]{4})', m.group(2)):
                if int(t, 16) >= 0x3000:                   # (below the overlay: the walk ran over `$3F59`'s inline code)
                    note('task' if m.group(1) == 'tasks' else 'hook', int(t, 16), 'script')
    cases = set()
    for f in SOURCES:
        cases |= set(int(x, 16) for x in re.findall(r'case 0x([0-9A-Fa-f]{4})\b', open(os.path.join(ROOT, f), encoding='utf-8', errors='replace').read()))
    missing = sorted(k for k in found if k[1] not in cases)
    nt = len([k for k in found if k[0] == 'task']); nh = len([k for k in found if k[0] == 'hook'])
    print('racecensus: %d tasks and %d hooks the race\'s code and scripts install, %d without a native case'
          % (nt, nh, len(missing)))
    for kind, t in sorted(found, key=lambda k: (k[1], k[0])):
        if verbose or (kind, t) in missing:
            print('  %s $%04X (%s)%s' % (kind, t, ', '.join(sorted(set(found[(kind, t)]))),
                                         '' if (kind, t) not in missing else ' -- NO NATIVE CASE'))
    try:
        os.remove(path); os.remove(path[:-5])
    except OSError:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
