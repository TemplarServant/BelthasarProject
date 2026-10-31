#!/usr/bin/env python3
r"""The attract 07D's Frog Squash: playAnim on track 4 arms the whole $A018 list -- against the logical-frame oracle
(the battle main loop's two-frame iteration, CLAUDE.md's zelda3 model; disasm/notes/battle_playanim_list.md).

    python tools/attract_playanim_list_cmp.py

The cartridge: saves/attract_07d_battle_f4540.state on CT_ORACLE_LOGICAL=1, `--monitor` of the pose engine's mode
$A124, the published pose $9907 and the cursor $A13A (eleven combatants each). The port: the attract (scene 433,
--selftest) with CT_BATTLE_ASCTRACE -- its `ascposearm` (C1/4D08's arm) and `ascpose` (C1/424C's publish) lines of
the tech's action (the `asc arm ... sel=28` window). The two frame counters are tied at the action's first arm (Frog's
anim 10, the first $A124 0 -> non-zero edge on the cartridge); from there, per frame to the action's end:

  * every arm: the frame, the combatants and the mode (on the cartridge: $A124 := 1/2, $A13A := FF);
  * every published pose: $9907 of combatants 0-7 as each frame leaves it.

Tech $28's script plays `06 05` and `06 03` on track 3 (the primary) and track 4 (the $A018 list, C1/4D79): on the
cartridge all five enemies arm together, 3 and 4 hold one pose ($A150 = 1 on $A018[0] and the primary) and 5/6/7 walk
the anim through. Arming only role 4 leaves three enemies in their idle pose.

Regression check: attract-07d-playanim-list-vs-logical-oracle.
"""
import os
import re
import subprocess
import sys


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    os.chdir(root)
    env = dict(os.environ, CT_BATTLE_ASCTRACE='1', CT_NOPACE='1', SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy')
    env.pop('CT_ORACLE_LOGICAL', None)
    pr = subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '433',
                         '--selftest', '--frames', '7400'], env=env, capture_output=True, text=True, errors='replace')
    oenv = dict(os.environ, CT_ORACLE_LOGICAL='1')
    orr = subprocess.run([os.path.abspath('build/ct.exe'), '--monitor', '7EA124:11,7E9907:11,7EA13A:11',
                          '--from', 'saves/attract_07d_battle_f4540.state', '--frames', '620', '--diff'],
                         env=oenv, capture_output=True, text=True, errors='replace')

    # ---- the port: the sel=28 action window
    parms, ppubs, pend, on = {}, {}, None, False
    for l in (pr.stdout + pr.stderr).splitlines():
        if 'asc arm ' in l:
            on = (' sel=28 ' in l)
            continue
        if not on:
            continue
        m = re.search(r'ascposearm f(\d+) cmb(\d+) anim=\w+ mode=(\d)', l)
        if m:
            parms.setdefault(int(m.group(1)), []).append((int(m.group(2)), int(m.group(3))))
        m = re.search(r'ascpose f(\d+) cmb(\d+) anim=\w+ cur=\d+ pose=([0-9A-F]{2})', l)
        if m:
            ppubs.setdefault(int(m.group(1)), []).append((int(m.group(2)), int(m.group(3), 16)))
        m = re.search(r'asc off f(\d+)', l)
        if m:
            pend = int(m.group(1)); on = False
    # ---- the cartridge: rows on change -> the value as every frame leaves it
    rows = {}
    for l in (orr.stdout + orr.stderr).splitlines():
        m = re.match(r'\s*f(\d+)\s+7EA124=([0-9A-F]{22})\*?\s+7E9907=([0-9A-F]{22})\*?\s+7EA13A=([0-9A-F]{22})', l)
        if m:
            rows[int(m.group(1))] = tuple([int(m.group(k)[i:i + 2], 16) for i in range(0, 22, 2)] for k in (2, 3, 4))
    if not parms or not rows or pend is None:
        sys.exit('playanim-list-cmp: a side printed nothing (port arms %d, end %s; oracle rows %d)' % (
            len(parms), pend, len(rows)))
    last = 619                                                       # --frames 620: rows print on change only
    O, cur = {}, None
    for f in range(0, last + 1):
        if f in rows:
            cur = rows[f]
        O[f] = cur
    oarms = {}
    for f in range(1, last + 1):
        if O[f] is None or O[f - 1] is None:
            continue
        # an arm (C1/4D5C-4D6C): $A124 := mode, $A13A := FF -- seen as $A124 leaving 0 or changing mode, or, for a
        # re-arm in the same mode while the anim still plays, as the cursor $A13A going back to FF
        a = [(c, O[f][0][c]) for c in range(11) if O[f][0][c] != 0 and
             (O[f][0][c] != O[f - 1][0][c] or (O[f][2][c] == 0xFF and O[f - 1][2][c] != 0xFF))]
        if a:
            oarms[f] = a
    p0 = min(parms)
    o0 = min(oarms)
    off = p0 - o0
    if pend - off > last:
        sys.exit('playanim-list-cmp: the action ends at oracle f%d, past the monitored f%d' % (pend - off, last))

    # ---- arms
    fr = sorted(set(parms) | set(f + off for f in oarms if f + off <= pend))
    bad = [f for f in fr if sorted(parms.get(f, [])) != sorted(oarms.get(f - off, []))]
    print('playanim-list-cmp: arms %d/%d identical (frame, combatants, mode)' % (len(fr) - len(bad), len(fr)))
    for f in bad[:6]:
        print('playanim-list-cmp:   port f%d %s / oracle f%d %s' % (f, sorted(parms.get(f, [])), f - off,
                                                                     sorted(oarms.get(f - off, []))))
    lists = [f for f in fr if len(parms.get(f, [])) > 1]
    for f in lists:
        print('playanim-list-cmp: list arm #%d -> cmb %s mode %d (the cartridge: cmb %s)' % (
            lists.index(f) + 1, ' '.join(str(c) for c, _ in sorted(parms[f])), parms[f][0][1],
            ' '.join(str(c) for c, _ in sorted(oarms.get(f - off, [])))))
    # ---- published poses
    pose = list(O[p0 - off - 1][1])
    bad = []
    for f in range(p0, pend + 1):
        for c, p in ppubs.get(f, []):
            pose[c] = p
        if pose[:8] != O[f - off][1][:8]:
            bad.append(f)
    n = pend - p0 + 1
    if not bad:
        print('playanim-list-cmp: published poses $9907[0..7] identical on all %d frames of the action' % n)
    else:
        f = bad[0]
        print('playanim-list-cmp: published poses differ on %d/%d frames, first port f%d / oracle f%d' % (
            len(bad), n, f, f - off))


if __name__ == '__main__':
    main()
