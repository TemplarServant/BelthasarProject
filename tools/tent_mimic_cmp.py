#!/usr/bin/env python3
r"""The tent of horrors (loc 1B2) after the mimic game is won, port against the rule's cartridge.

    python tools/tent_mimic_cmp.py

The user, 2026-10-08: "after winning the mimicry game in the tent of horrors, it got stuck" (build/ct_debug_020.state).
Bekkler (obj11) waits in his talk fn1 on the game, `0590: 04 00 33` (CALL obj0 fn3 and wait; his latch $7F0980 = 1).
The won game's end calls HIS fn3 at priority 0, `0091: 04 16 03`, and waits for it. The ROM pre-empts the waiter
(C0/6168-616D: return slot 1 := $1180 = the parked 04 byte), runs fn3 (`89 40 / 96 08 06`: he steps up to tile 8,6)
with the latch still set, and the RETURN lands back on the 04, which polls again (C0/61A7) and finds obj0 done. The
port polled the latch on every visit, whatever the object was running: fn3 never ran and the two objects waited on
each other for good. (ct_vm.c ssParked.)

The cartridge: saves/field_re/s1b2_mimic_won_lake.state (the game won on the logical oracle: the tent entered by its
door from tent_keeper_lake.state, Bekkler talked to, 40 points poked into $7F021A, every round's pose -- $7F0220 --
answered with its button), `--monitor` with A on f60, f182, f304, f426 (the texts). The port: the same WRAM
(--loadwram) and SPC (CT_AUDIO_SEEDSTATE), A one frame later (its key is the oracle's + 1), CT_FIELD_MEMMON. Each
event's frame on both sides:

    tent-mimic: <event> cartridge f<C> port f<P>
    tent-mimic: <n>/<m> events on the cartridge's frame                (or: ..., the rest differ)
(--monitor's f<N> is the frame after N + 1 frames; the port's fmemmon f<N> the same.)
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
STATE = os.path.join(ROOT, 'saves', 'field_re', 's1b2_mimic_won_lake.state')
OUT = os.path.join(ROOT, 'build', '_tent_mimic')
PRESSES = [60, 182, 304, 426]
FRAMES = 300
HOFF = 0x300                     # the script header: 24 objects x 16 function pointers x 2


def events(rows):
    """rows: [(frame, loc, ip[12], tilex11, tiley11)] -> {event: first frame}."""
    ev = {}

    def first(name, pred):
        for f, loc, ip, tx, ty in rows:
            if pred(loc, ip, tx, ty):
                ev[name] = f
                return
    first('obj0 calls Bekkler fn3 (0091)', lambda loc, ip, tx, ty: ip[0] == 0x0091)
    first('Bekkler leaves tile (11,9)', lambda loc, ip, tx, ty: (tx, ty) != (11, 9))
    first('Bekkler on tile (8,6)', lambda loc, ip, tx, ty: (tx, ty) == (8, 6))
    first('Bekkler back on his 04 (0590)', lambda loc, ip, tx, ty: ip[11] == 0x0590 and (tx, ty) == (8, 6))
    first('obj0 released (0094)', lambda loc, ip, tx, ty: ip[0] == 0x0094)
    first('obj0 past the prize (0097)', lambda loc, ip, tx, ty: ip[0] == 0x0097)
    first('location 005', lambda loc, ip, tx, ty: loc == 0x005)
    return ev


def parse(lines, rx, hexkey):
    rows, cur = [], {}
    for l in lines:
        m = rx.match(l)
        if not m:
            continue
        for k, v in re.findall(r'((?:7E)?[0-9A-F]{4})=([0-9A-F]+)\*?', l):
            cur[k.replace(hexkey, '')] = bytes.fromhex(v)
        if not all(k in cur for k in ('0100', '1180', '1801', '1881')):
            continue
        ip = [(cur['1180'][2 * o] | cur['1180'][2 * o + 1] << 8) - HOFF for o in range(12)]
        rows.append((int(m.group(1)), cur['0100'][0] | cur['0100'][1] << 8, ip, cur['1801'][22], cur['1881'][22]))
    return rows


def main():
    os.makedirs(OUT, exist_ok=True)
    rule = dict(os.environ, CT_ORACLE_LOGICAL='1')
    script, t = [], 0
    for p in PRESSES:
        script += ['0:%d' % (p - t), '100:2']
        t = p + 2
    script.append('0:%d' % (FRAMES - t))
    mon = subprocess.run([CT, '--monitor', '7E0100:2,7E1180:24,7E1801:24,7E1881:24', '--from', STATE, '--input',
                          ','.join(script)], cwd=ROOT, env=rule, capture_output=True, text=True, errors='replace').stdout
    cart = parse(mon.splitlines(), re.compile(r'\s+f(\d+)\s'), '7E')
    raw = os.path.join(OUT, 'seed.raw')
    subprocess.run([CT, '--peek', STATE, raw, '--btnscript', '0:0'], cwd=ROOT, env=rule, capture_output=True)
    lw = os.path.join(OUT, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + open(raw + '.wram', 'rb').read())
    walk, t = [], 0
    for p in PRESSES:
        walk += ['.:%d' % (p + 1 - t), 'a:1']
        t = p + 2
    walk.append('.:%d' % (FRAMES - t))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_SEEDSTATE=STATE,
               CT_FIELD_MEMMON='0100:2,1180:18,1801:18,1881:18')
    out = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '434', '--loadwram', lw, '--selftest',
                          os.path.join(OUT, 'p.raw'), '--frames', str(FRAMES), '--walkscript', ','.join(walk)],
                         cwd=ROOT, env=env, capture_output=True, text=True, errors='replace').stdout
    port = parse(out.splitlines(), re.compile(r'fmemmon f(\d+) '), '')
    ce, pe = events(cart), events(port)
    good = 0
    names = list(ce) + [k for k in pe if k not in ce]
    for k in names:
        c, p = ce.get(k), pe.get(k)
        good += c is not None and p is not None and p == c
        print('tent-mimic: %s cartridge %s port %s' % (k, 'f%d' % c if c is not None else 'never',
                                                         'f%d' % p if p is not None else 'never'))
    if good == len(names) == 7:
        print("tent-mimic: %d/%d events on the cartridge's frame" % (good, len(names)))
    else:
        print("tent-mimic: %d/%d events on the cartridge's frame, the rest differ" % (good, max(7, len(names))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
