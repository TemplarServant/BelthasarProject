#!/usr/bin/env python3
r"""The attract 07D's battle staging against the cartridge: the five enemy records $29BF + 12*slot, byte for byte.

    python tools/attract_07d_staging_cmp.py

The field writes them at the battle's entry (C0/0557-0616): tile = $1801,Y - $F0 / $1881,Y - $F1 and fine =
$0A00,Y & 0F / $0A80,Y & 0F of each enemy's field object, then the pose, id, mode byte and tail. So they are the
enemies' FIELD positions at that moment -- which the 07D's script sets with op 0x8C (C0/4BC3) out of scene vars
filled by op 0x21 (disasm/notes/field_op8c_place_from_vars.md), and the battle's $1D0C/$1D23 are 16*tile + fine
of these bytes (CC/EA82-EA9B). The cartridge, under the rule (CT_ORACLE_LOGICAL / VIRTUAL_BEAM):
saves/attract_cena_inicial_rule.state (the attract from power-on + 2159, made by a recipe -- tools/fixtures/attract_rule.json),
`--monitor 7E29BF:60` over the write (C0:0589, f4425; f4496 on the owner's real-time capture saves/attract_cena_inicial.state,
which this tool read until 2026-10-22). The port: the attract (scene 433, --selftest) with CT_BATTLE_MEMMON, the first line of the
battle whose formation $AF02 is the 07D's (05 05 51 51 57).
"""
import os
import re
import subprocess
import sys


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    os.chdir(root)
    env = dict(os.environ, CT_BATTLE_MEMMON='29BF:3C,AF02:8', CT_NOPACE='1', SDL_AUDIODRIVER='dummy',
               SDL_VIDEODRIVER='dummy')
    env.pop('CT_ORACLE_LOGICAL', None)
    pr = subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '433',
                         '--selftest', '--frames', '6600'], env=env, capture_output=True, text=True, errors='replace')
    orr = subprocess.run([os.path.abspath('build/ct.exe'), '--monitor', '7E29BF:60', '--from',
                          'saves/attract_cena_inicial_rule.state', '--settle', '4410', '--frames', '30', '--diff'],
                         env=dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1'),
                         capture_output=True, text=True, errors='replace')
    port = None
    for l in (pr.stdout + pr.stderr).splitlines():
        m = re.search(r'memmon f\d+ rel=\S+\s+29BF=([0-9A-F]{120})\*?\s+AF02=0505515157FFFFFF', l)
        if m:
            port = bytes.fromhex(m.group(1))
            break
    cart = None
    for l in (orr.stdout + orr.stderr).splitlines():
        m = re.match(r'\s*f\d+\s+7E29BF=([0-9A-F]{120})', l)
        if m and m.group(1)[:2] != '00':
            cart = bytes.fromhex(m.group(1))
    if port is None or cart is None:
        sys.exit('staging-cmp: a side printed nothing (port %s, cartridge %s)' % (port is not None, cart is not None))
    same = 0
    for s in range(5):
        p, c = port[s * 12:s * 12 + 12], cart[s * 12:s * 12 + 12]
        ok = p == c
        same += ok
        print('staging-cmp: slot%d port %s (%d,%d) cartridge %s (%d,%d) %s' % (
            s, p.hex().upper(), 16 * p[0] + p[2], 16 * p[1] + p[3], c.hex().upper(), 16 * c[0] + c[2], 16 * c[1] + c[3],
            'identical' if ok else 'DIFFER'))
    print('staging-cmp: the 07D enemy records $29BF: %d / 5 identical' % same)


if __name__ == '__main__':
    main()
