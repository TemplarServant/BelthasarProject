#!/usr/bin/env python3
r"""The menu's opening sheet pass and the item heal's clamp, port against cartridge.

    python tools/menu_open_sheets_cmp.py

Every menu opens through C2/8139 -> C2/9875, which cleans the bag (C2/9988) and recomputes all seven character sheets
and writes their derived block back (C2/99B2 -> C2/8816 + C2/9137: +$03 and +$36..+$40). +$3F/+$40 is the effective max
HP the item appliers clamp to (C2/8E9E `CMP $003F,Y`), and outside the menu it holds something else -- on
saves/battle_re/party3_field.state Crono (25/96) carries 1029 there until C2/9160 writes 0060 at f35. Without this pass
a Tonic heals past the maximum.

Both sides get the same seed (the oracle state; the port its WRAM, saves/battle_re/party3_field.lakewram) with a Tonic
(id BD) poked into bag slot 1, and the same drive, pressed on the frames the cartridge reads them: X, then Right (Item),
A, Down (the Tonic), A, A (Use), Down (Lucca, 40/88), A. Printed:

    menu-open-sheets: char <c> +03 <hex> +36..+40 <hex> <MATCH|DIFFER>        (seven lines, after the open)
    menu-item-tonic: Lucca HP oracle <n> port <n> -> <MATCH|DIFFER>
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OSTATE = 'saves/battle_re/party3_field.state'
PSEED = 'saves/battle_re/party3_field.lakewram'
POKE = {0x2401: 0xBD, 0x2501: 0x01}
# the oracle's btnscript (LakeSnes masks: X 200, Right 80, A 100, Down 20) and the port's walkscript, the same presses
OBTN = '0:4,200:2,0:54,80:2,0:18,100:2,0:28,20:2,0:18,100:2,0:28,100:2,0:28,20:2,0:28,100:2,0:30'
PWALK = 'n:5,m:1,n:55,r:1,n:19,a:1,n:29,d:1,n:19,a:1,n:29,a:1,n:29,d:1,n:29,a:1,n:30'
OPEN_F = 50            # the menu is open and its init chain done on both sides
LUCCA_HP = 0x2603 + 2 * 0x50


def oracle_wram(frames, extra):
    out = os.path.join(ROOT, 'build', '_mos_o.raw')
    cut, left = [], frames
    for tok in OBTN.split(','):
        m, n = tok.split(':'); n = int(n)
        if left <= 0:
            break
        cut.append('%s:%d' % (m, min(n, left))); left -= n
    if left > 0:
        cut.append('0:%d' % left)
    env = dict(os.environ, PEEK_POKE=','.join('%04X=%02X' % kv for kv in POKE.items()))
    subprocess.run([CT, '--peek', OSTATE, out, '--btnscript', ','.join(cut)], cwd=ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return open(out + '.wram', 'rb').read()


def port_mem(addrs):
    b = bytearray(open(os.path.join(ROOT, PSEED), 'rb').read())
    o = b.index(b'RAM:131072:') + 11
    for a, v in POKE.items():
        b[o + a] = v
    seed = os.path.join(ROOT, 'build', '_mos_seed.lakewram')
    open(seed, 'wb').write(bytes(b))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_FIELD_MEMMON=','.join('%04X:%X' % (a, n) for a, n in addrs))
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '130', '--loadwram', seed, '--selftest',
                        os.path.join(ROOT, 'build', '_mos.raw'), '--frames', '262', '--walkscript', PWALK],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    hist = []                                        # (frame, {addr: hex})
    for l in r.stdout.splitlines():
        m = re.match(r'fmemmon f(\d+)', l)
        if m:
            hist.append((int(m.group(1)), dict(re.findall(r'([0-9A-F]{4})=([0-9A-F]+)\*?', l))))
    return hist


def at(hist, f):
    cur = {}
    for fr, v in hist:
        if fr <= f:
            cur.update(v)
    return cur


def main():
    addrs = []
    for c in range(7):
        addrs += [(0x2603 + c * 0x50, 2), (0x2636 + c * 0x50, 11)]
    hist = port_mem(addrs)
    ow = oracle_wram(OPEN_F + 1, None)
    popen = at(hist, OPEN_F)
    for c in range(7):
        a3, a36 = 0x2603 + c * 0x50, 0x2636 + c * 0x50
        o = ow[a3:a3 + 2].hex().upper() + ' ' + ow[a36:a36 + 11].hex().upper()
        p = (popen.get('%04X' % a3, '?') + ' ' + popen.get('%04X' % a36, '?'))
        print('menu-open-sheets: char %d +03/+36..+40 %s -> %s' % (c, o, 'MATCH' if o == p else 'DIFFER (port %s)' % p))
    ow2 = oracle_wram(260, None)
    ohp = ow2[LUCCA_HP] | (ow2[LUCCA_HP + 1] << 8)
    pv = at(hist, 260).get('%04X' % LUCCA_HP, '0000')
    php = int(pv[0:2], 16) | (int(pv[2:4], 16) << 8)
    print('menu-item-tonic: Lucca HP oracle %d port %d -> %s' % (ohp, php, 'MATCH' if ohp == php else 'DIFFER'))


if __name__ == '__main__':
    main()
