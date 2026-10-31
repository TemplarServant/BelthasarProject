#!/usr/bin/env python3
r"""The menu's WRAM around the X menu, frame by frame, port against the logical oracle.

    python tools/menu_wram_cmp.py

saves/battle_re/party3_field.state (loc $082), X at f4 (the handler sees it at f5), B at f60 (seen at f61); the port
from the same WRAM (party3_field.lakewram), pressing on the frames the cartridge reads them. Bytes graded every frame
f0..f69 -- f69 is the RTL, where the field's C0/0905 puts its own $7E:0920-1DFF back:
  $0D00  C2/8ECC's RNG cursor, +1 in the menu's NMI (C2/842F) from S0 through the close's fade-out
  $0D10  the brightness shadow (C2/83F5 / C2/83E5): 2,4..14,15 over P+2..P+9, then 13..1, 0
  $0D38  Crono's HP-bar scale (C2/8163, the open's sheet pass), the field's 6800 back at the RTL
(The init's WRAM fill C2/940D -- $0C00-1DFF zeroed at the switch -- is not modelled, so bytes the fill clears and the
menu never writes are left out.) One line per byte:

    menu-wram: <addr> f0-f69: <N> frame(s) differ [...]
"""
import os
import re
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
BYTES = [('0D00', 1), ('0D10', 1), ('0D38', 2)]
LAST = 69


def series(lines, key):
    out, cur = {}, None
    for f, vals in lines:
        if key in vals:
            cur = vals[key]
        out[f] = cur
    return out


def oracle():
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    spec = ','.join('7E%s:%d' % (a, n) for a, n in BYTES)
    r = subprocess.run([CT, '--monitor', spec, '--from', 'saves/battle_re/party3_field.state',
                        '--btnscript', '0:4,200:2,0:54,1:2,0:20'], cwd=ROOT, env=env, capture_output=True, text=True,
                       errors='replace')
    rows = []
    for l in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', l)
        if m:
            rows.append((int(m.group(1)), dict(re.findall(r'7E([0-9A-F]{4})=([0-9A-F]+)', m.group(2)))))
    return rows


def port():
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_FIELD_MEMMON=','.join('%s:%d' % (a, n) for a, n in BYTES))
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '130', '--loadwram',
                        'saves/battle_re/party3_field.lakewram', '--selftest', os.path.join(ROOT, 'build', '_mwram.raw'),
                        '--frames', str(LAST + 3), '--walkscript', 'n:5,m:1,n:55,b:1,n:12'],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    rows = []
    for l in r.stdout.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\S+\s+(.*)', l)
        if m:
            rows.append((int(m.group(1)), dict(re.findall(r'([0-9A-F]{4})=([0-9A-F]+)', m.group(2)))))
    return rows


def main():
    o, p = oracle(), port()
    for a, _n in BYTES:
        so, sp = series(o, a), series(p, a)
        bad = []
        vo = vp = None
        for f in range(0, LAST + 1):
            vo = so.get(f, vo); vp = sp.get(f, vp)
            if vo != vp:
                bad.append('f%d %s/%s' % (f, vo, vp))
        print('menu-wram: $%s f0-f%d: %d frame(s) differ%s' % (a, LAST, len(bad), (' [' + ', '.join(bad[:6]) + ']') if bad else ''))


if __name__ == '__main__':
    main()
