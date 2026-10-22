#!/usr/bin/env python3
r"""The attract's three demo battles (0AD, 07D, 087): the battle RNG stream each one boots from and hands back, graded
against the cartridge -- the turn order $B163 its boot shuffle draws, the first cursor pass's $B185, and the $26 it
hands back in $29AD.

    python tools/attract_battle_rng_cmp.py

ONE stream runs through the three fights. C1/800A seeds $26 from $29AD, the two sampling-with-retry shuffles
(C1/8072, C1/80AD) and the enemies' AI init (C1/80E8 -> C1/8C09) draw from it, and C1/8443 hands the final $26 back to
$29AD, which nothing in bank C0 touches between fights. So a draw one fight misses shifts every table entry the next
fight's shuffle reads, and the turn order comes out rotated: that is how the 0AD's lost AI-init draw (the eval ran
after C1/80EB put the demo bit back, so AF22 answered its base without advancing $26) wrote $B185 = 06 in the 07D
where the cartridge writes 05, and how the 07D's unported op 04 (C1/98C5, the random branch: its roll + the chosen
entry's resolver 05) left the 087 two draws behind.

Port: the live attract (`--scene 433 --selftest`) with CT_BATTLE_TRACE (the `battle-rng-cursor handback` line,
field.c) and CT_BATTLE_MEMMON over $B163/$B185 (a line per frame whose bytes changed; the fight's first line is its
frame 1). Cartridge: saves/attract_cena_inicial_rule.state under the rule, `--monitor 7EB163:11,7EB185:1,7E29AD:1 --diff --writer` --
each fight's first cursor pass is the first `$B185` write by C1:82EE after FD:A989 clears it (or C1:FBD4 fills $B163, the
writer a frame's sample names when both land in one frame), the handback is the
`$29AD` write by C1:8448. Values only, no frame is graded: the port's fights sit at other frames than the cartridge's.
"""
import os
import re
import subprocess

FIELD = os.path.abspath('build/Belthasar.exe')
CT = os.path.abspath('build/ct.exe')
STATE = 'saves/attract_cena_inicial_rule.state'   # the attract from power-on + 2159 under the rule (tools/fixtures/attract_rule.json;
RULE = {'CT_ORACLE_LOGICAL': '1', 'CT_ORACLE_VIRTUAL_BEAM': '1'}   # until 2026-10-22 the owner's real-time capture)
NAMES = ['0AD', '07D', '087']


def port_side():
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_BATTLE_TRACE='1',
               CT_BATTLE_MEMMON='B163:0B,B185:1')
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433', '--selftest', '--frames', '10200'],
                       env=env, capture_output=True, text=True, errors='replace', timeout=1800)
    fights = []
    for line in (r.stdout + r.stderr).splitlines():
        if re.match(r'battlesfx f\d+ battle-start ', line):            # CD/04FD, once per fight
            fights.append({})
            continue
        m = re.match(r'memmon f\d+ rel=-?\d+\s+B163=([0-9A-F]+)\*?\s+B185=([0-9A-F]{2})(\*?)', line)
        if m:
            if fights and m.group(3) and m.group(2) != '00' and 'b185' not in fights[-1]:
                fights[-1]['b163'] = m.group(1); fights[-1]['b185'] = m.group(2)
            continue
        m = re.match(r'battle-rng-cursor handback \$29AD=([0-9A-F]{2})', line)
        if m and fights and 'hand' not in fights[-1]:
            fights[-1]['hand'] = m.group(1)
    return fights


def oracle_side():
    r = subprocess.run([CT, '--monitor', '7EB163:11,7EB185:1,7E29AD:1', '--from', STATE, '--frames', '7900',
                        '--diff', '--writer'], env=dict(os.environ, **RULE), capture_output=True, text=True, errors='replace',
                       timeout=1800)
    fights = []
    for line in (r.stdout + r.stderr).splitlines():
        # one (value, changed, writer) per region: `7EB185=05*(C1:82EE)`
        f = {a: (v, ch == '*', w) for a, v, ch, w in
             re.findall(r'7E([0-9A-F]{4})=([0-9A-F]+)(\*?)(?:\(([0-9A-F]{2}:[0-9A-F]{4})\))?', line)}
        if not {'B163', 'B185', '29AD'} <= set(f):
            continue
        # the boot's clear of the battle page (FD:A989), or its FF fill of $B163 (C1:FBD4) when both land in one frame -- under
        # the rule the boot's NMI-on work is free, and a frame's sample names only its last writer
        if any(ch and w in ('FD:A989', 'C1:FBD4') for _, ch, w in f.values()):
            if not fights or 'b185' in fights[-1]:
                fights.append({})
            continue
        v, ch, w = f['B185']
        if fights and ch and w == 'C1:82EE' and 'b185' not in fights[-1]:
            fights[-1]['b163'] = f['B163'][0]; fights[-1]['b185'] = v
        v, ch, w = f['29AD']
        if fights and ch and w == 'C1:8448' and 'hand' not in fights[-1]:
            fights[-1]['hand'] = v
    return fights


def main():
    port, cart = port_side(), oracle_side()
    for i, name in enumerate(NAMES):
        p = port[i] if i < len(port) else {}
        c = cart[i] if i < len(cart) else {}
        for key, what in (('b163', 'turn order $B163'), ('b185', 'first cursor pass $B185'),
                          ('hand', 'handback $29AD')):
            pv, cv = p.get(key, '--'), c.get(key, '--')
            print('attract-battle-rng: %s %s port %s cartridge %s -> %d'
                  % (name, what, pv, cv, 1 if pv == cv and pv != '--' else 0))


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    main()
