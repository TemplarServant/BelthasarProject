#!/usr/bin/env python3
r"""The field's reload after a menu close (C0/0B5D -> C0/0B38), priced by the port (src/field_cost.c) against the
cartridge, leg by leg.

    python tools/reload_cost_cmp.py [--make]

Each fixture is a menu close on the LOGICAL oracle (CT_ORACLE_LOGICAL=1), from a committed save and a button script
up to a few frames before the close's RTL. The tool dumps the cartridge's WRAM at C0/0B5D (the NMI-off) and at
C0/286C (the pass) with `ct.exe --hookpc` + CT_HOOK_DUMPWRAM, and grades:
  * the pass, `ct_field --passcost` over the WRAM at C0/286C;
  * the whole stretch, `ct_field --reloadcost` over the WRAM at the RTL with C0/0905's restore applied
    ($7F:2000 -> $7E:0920, $14E0 bytes -- the state the port holds there);
against the non-refresh cycles the cartridge's beam trace measured (tools/ctlib/beam.py), stored below; `--make`
re-traces (`ct.exe --cputrace`, CPUTRACE_BEAM=1, ~100 MB a fixture) and re-measures them. The frames the stretch
spans are floor(C / 346,886), + 1 when C0/0B38's `LDA $4210` lands past that frame's vblank (RDNMI is cleared at line 0):
the stretch starts at phase 0, so its remainder is measured from a vblank's start.

    reload: p3 loc 082 (x): pass 1728455 / 1728472 (-17), total 13244133 / 13245448 (-1315): 39 / 39 frames

The residual is the map build's model (lz.c ct_map_build_cycles, its MBC_ONESHOT) and the DMA legs' sync.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import beam  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'ct_field.exe')
OUT = os.path.join(ROOT, 'build', '_menuinit')

# tag, save, btnscript to a few frames before the close's RTL, frames to trace from there, caller,
# measured (C0/0B5D -> C0/0B38, C0/286C -> C0/0B38) non-refresh cycles on the logical oracle
FIXTURES = [
    ('p3', 'saves/battle_re/party3_field.state', '0:4,200:2,0:54,1:2', 50, 'x', (13245448, 1728472)),
    # (re-measured 2026-10-10 on the recipe-made shop90_lake.state, tools/fixtures/menu.json; the capture's: 12552248, 1924502)
    ('shop', 'saves/field_re/shop90_lake.state', '0:10,100:2,0:418,1:2', 55, 'c8', (12556860, 1929108)),
    # party3_field again, the X pressed right after a walk: the remainder lands inside a vblank (C0/0B38 falls through)
    ('walk', 'saves/battle_re/party3_field.state', '0:4,80:16,200:2,0:52,1:2,0:2', 55, 'x', (13189458, 1672476)),
]


def wait_frames(c):
    """ct_nmi_wait_frames (src/field_cost.h): floor(C / F), + 1 when C0/0B38's read lands past the vblank's 37 lines"""
    return c // beam.F + (1 if c % beam.F + 18 >= 37 * 1324 else 0)


def env():
    return dict(os.environ, CT_ORACLE_LOGICAL='1')


def state_of(tag, save, script):
    """THIS oracle's state a few frames before the close, made on every run (~0.6 s): one kept from an older oracle
    would replay the rule that made it (the 2026-09-30 note in field_window_wipe.md, "The virtual beam")."""
    st = os.path.join(OUT, tag + '_close.state')
    os.makedirs(OUT, exist_ok=True)
    if os.path.exists(st):
        os.remove(st)
    subprocess.run([CT, '--peek', save, os.path.join(OUT, '_x.raw'), '--btnscript', script], cwd=ROOT,
                   env=dict(env(), PEEK_SAVE=st), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return st


def dumps(tag, st, frames):
    for pc, name in (('0B5D', '_0b5d.wram'), ('286C', '_286c.wram')):
        subprocess.run([CT, '--hookpc', pc, '--from', st, '--frames', str(frames)], cwd=ROOT,
                       env=dict(env(), CT_HOOK_DUMPWRAM=os.path.join(OUT, tag + name)),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def remeasure(tag, st, frames):
    tr = os.path.join(OUT, tag + '_close.txt')
    subprocess.run([CT, '--cputrace', st, str(frames), tr], cwd=ROOT, env=dict(env(), CPUTRACE_BEAM='1'),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t = beam.load(tr)

    def at(pc, start=0):
        for i in range(start, len(t)):
            if t[i][0] == pc:
                return i
        return None
    a = at('C0:0B5D')
    p = at('C0:286C', a)
    w = at('C0:0B38', p)
    return beam.nonrefresh(t[a], t[w]), beam.nonrefresh(t[p], t[w])


def port(mode, wram, loc, caller=None):
    args = [FIELD, '--' + mode, wram, '%03X' % loc] + ([caller] if caller else [])
    out = subprocess.run(args, cwd=ROOT, capture_output=True, text=True).stdout
    line = [x for x in out.splitlines() if x.startswith(mode)][0]
    return int(line.split(' total ')[1].split()[0]), int(line.split(' pass ')[1].split()[0]), line


def main():
    for tag, save, script, frames, caller, meas in FIXTURES:
        st = state_of(tag, save, script)
        dumps(tag, st, frames)
        mx, mp = remeasure(tag, st, frames) if '--make' in sys.argv else meas
        w = bytearray(open(os.path.join(OUT, tag + '_0b5d.wram'), 'rb').read())
        w[0x0920:0x0920 + 0x14E0] = w[0x12000:0x12000 + 0x14E0]          # C0/0905's MVN
        rtl = os.path.join(OUT, tag + '_rtl.wram')
        open(rtl, 'wb').write(w)
        loc = (w[0x100] | (w[0x101] << 8)) & 0x1FF
        pp = port('passcost', os.path.join(OUT, tag + '_286c.wram'), loc)[1]
        tot, _, line = port('reloadcost', rtl, loc, caller)
        why = line.split(' -- NOT WALKED: ')[1] if ' -- NOT WALKED: ' in line else ''
        print('reload: %s loc %03X (%s): pass %d / %d (%+d), total %d / %d (%+d): %s / %d frames%s'
              % (tag, loc, caller, pp, mp, pp - mp, tot, mx, tot - mx,
                 wait_frames(tot) if tot > 0 else "-", wait_frames(mx), (' -- ' + why) if why else ''))


if __name__ == '__main__':
    main()
