#!/usr/bin/env python3
r"""The attract's loc 0E6: the init pass draws the robots' variants (op 0x7F, C0/6D09), port against the rule's cartridge.

    python tools/attract_0e6_init_draws_cmp.py

Objs 18-22's fn0 at loc 0E6 is `7F 06 / 12 06 80 03 06 / 83 6A 03 / 10 04 / 83 69 03`: local[6] := $FE00[++$01F8], and
the enemy loaded is 69 when it is >= 80, else 6A. The init runner C0/58ED dispatches it like any op; the port's init
pass (ct_vm.c runFunc) stepped over 0x7F by its length until 2026-10-08 (disasm/notes/live_vm_op_gaps.md), so local[6]
stayed 00 and all five robots loaded 6A while the cursor never moved.

Both sides run from power-on (the cartridge under the rule, `--monitor --from boot`; the port `--boot` on an erased
cart, CT_SRAM_FRESH=1, with CT_FIELD_MEMMON -- the route tools/f8_cursor_cmp.py grades the cursor's seeds on; a cold
`--scene 433` starts 22 play-clock ticks behind, field_f8_cursor_play_clock.md). The cursor $01F8 the draws start from is NOT the same on the two sides -- it drifts from the boot
(cartridge f50 = 11, port 00), an open matter of its own -- so each side is graded against the ROM's table from ITS
OWN cursor at the 0E6 load: the five draws $FE00[c+1..c+5] give local[6] (the last) and the five ids. Prints

    init-draws: cartridge cursor CC -> DD, local[6] LL, ids I I I I I, $FE00 from CC+1: V V V V V -> consistent|WRONG
    init-draws: port      cursor .. -> DD, local[6] LL, ids I I I I I, $FE00 from DD-4: V V V V V -> consistent|WRONG
    init-draws: <n>/2 sides draw the robots from the ROM table at the load

(The port's whole load lands in one presented frame, so its cursor before the draws is read as the one after, less 5.)
"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '.')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
ROM = os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc')
ORACLE_FRAMES = 7700
PORT_LAST = 7600
IDS = [0x1125, 0x1127, 0x1129, 0x112B, 0x112D]        # $1101,X of objs 18-22 (C0/4564, op 0x83's id)


def rows(lines, rx, key):
    """{frame: {addr: hexstr}} from `--monitor --diff` / fmemmon lines (carrying values forward)."""
    out, cur = [], {}
    for ln in lines:
        m = re.search(rx, ln)
        if not m:
            continue
        for a, v in re.findall(r'([0-9A-F]{4,6})=([0-9A-F]+)', ln):
            cur[key(a)] = int(v, 16)
        out.append((int(m.group(1)), dict(cur)))
    return out


def grade(name, before, after, local, ids, table):
    draws = [table[0xFE00 + ((before + k) & 0xFF)] for k in range(1, 6)]
    want = [0x69 if d >= 0x80 else 0x6A for d in draws]
    ok = (after - before) & 0xFF == 5 and local == draws[-1] and ids == want
    print('init-draws: %-9s cursor %02X -> %02X, local[6] %02X, ids %s, $FE00 from %02X: %s -> %s'
          % (name, before, after, local, ' '.join('%02X' % i for i in ids), (before + 1) & 0xFF,
             ' '.join('%02X' % d for d in draws), 'consistent' if ok else 'WRONG'))
    return ok


def main():
    rom = _romlib.load(ROM)
    watch = '7E0100:2,7E01F8:1,7F020C:1,' + ','.join('7E%04X:1' % a for a in IDS)
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1')
    r = subprocess.run([CT, '--monitor', watch, '--from', 'boot', '--frames', str(ORACLE_FRAMES), '--diff'],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    cart = rows(r.stdout.splitlines(), r'^\s+f(\d+)\s', lambda a: int(a, 16) & 0xFFFF if len(a) == 6 and a[:2] == '7E'
                else 0x10000 | (int(a, 16) & 0xFFFF))
    # the cartridge's draws: the first change of local[6] at loc 0E6, the cursor on the row before it
    seen = None
    for i, (f, v) in enumerate(cart):
        if v.get(0x0100) == 0xE600 and v.get(0x1020C, 0) != cart[i - 1][1].get(0x1020C, 0):
            seen = i
            break
    n_ok = 0
    sides = {}
    if seen is None:
        print('init-draws: cartridge never drew local[6] at loc 0E6 in %d frames' % ORACLE_FRAMES)
    else:
        before = cart[seen - 1][1][0x01F8]
        last = [v for f, v in cart if v.get(0x0100) == 0xE600][-1]
        n_ok += grade('cartridge', before, last[0x01F8], last[0x1020C], [last[a] for a in IDS], rom)
        sides['cartridge'] = (before, last[0x01F8], last[0x1020C], [last[a] for a in IDS])

    os.makedirs(os.path.join(ROOT, 'build', '_0e6draws'), exist_ok=True)
    penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
                CT_SRAM=os.path.join(ROOT, 'build', '_0e6draws', 'fresh.srm'), CT_SRAM_FRESH='1',
                CT_FIELD_MEMMON='0100:2,01F8:1,1020C:1,' + ','.join('%04X:1' % a for a in IDS),
                CT_SHOTS=os.path.join(ROOT, 'build', '_0e6draws'), CT_SHOTS_AT=str(PORT_LAST), CT_SHOTS_EXIT='1')
    p = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--boot'], cwd=ROOT, env=penv,
                       capture_output=True, text=True, errors='replace')
    shot = os.path.join(ROOT, 'build', '_0e6draws_f%d.raw' % PORT_LAST)
    if os.path.exists(shot):
        os.remove(shot)
    port = rows((p.stdout + '\n' + p.stderr).splitlines(), r'fmemmon f(\d+) ',
                lambda a: (0x10000 | (int(a, 16) & 0xFFFF)) if len(a) == 5 else int(a, 16))
    at = [v for f, v in port if v.get(0x0100) == 0xE600]
    if not at:
        print('init-draws: the port never reached loc 0E6 by f%d' % PORT_LAST)
    else:
        last = at[-1]
        after = last[0x01F8]
        n_ok += grade('port', (after - 5) & 0xFF, after, last[0x1020C], [last[a] for a in IDS], rom)
        sides['port'] = ((after - 5) & 0xFF, after, last[0x1020C], [last[a] for a in IDS])
    print('init-draws: %d/2 sides draw the robots from the ROM table at the load' % n_ok)
    if len(sides) == 2:
        print('init-draws: cursor, local[6] and ids %s on both sides' %
              ('IDENTICAL' if sides['cartridge'] == sides['port'] else 'DIFFERENT'))


if __name__ == '__main__':
    sys.exit(main())
