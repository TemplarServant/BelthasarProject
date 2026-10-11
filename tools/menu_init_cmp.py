#!/usr/bin/env python3
r"""The menu shell's init chain (C2/8000 -> C2/8096, NMI off) against the cartridge, cycle for cycle, per battery.

    python tools/menu_init_cmp.py [battery.srm ...]      (no argument: the built-in fixture set)

For each battery the cartridge boots from power-on with it (CT_ORACLE_SRAM, the logical oracle), Start at +13 and +115
from f120 -- the title's two -- so loc 0's `C8 41` opens the load shell (mode 0) at f198 (or, on a blank cart, takes
C2/8000's short circuit: no init chain, nothing to measure). C2/8000's frame is FOUND (`--hookpc C28000`: the boot's
timeline moves with the oracle's frame rule, CT_ORACLE_NMION_HOLD), and the init is traced from three frames before it
(`--cputrace`, CPUTRACE_BEAM=1; C0/0B5D = the last one before C2/8000) and
measured with tools/ctlib/beam.py: C2/D156 (the save-slot scan, 8127 -> 812A) and the whole NMI-off stretch
(C0/0B5D, the caller's `STA $4200` that turns the NMI off, -> C2/8087's that turns it on). Printed against the port's model, src/menu_cost.c (`ct_field --menucost`; its Python twin is tools/vwf_cost_proto.py):

    menu-init: <battery> slots 0000 8080 8080: D156 1737858 / model 1737858, stretch 5915532 = 17.053 F -> S0 = C+17

The fixtures are built from the two real batteries: slot 0 copied into all three slots (checksums recomputed the way
FF/FBE7 sums), a slot whose signature holds and whose sum does not (FF/F9C4's 4040), and a mix of the two saves.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import menu_audit_cmp as M  # noqa: E402
import vwf_cost_proto as V  # noqa: E402
from ctlib import beam  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', '_menuinit')


def put_slot(dst, slot, src, sslot):
    dst[0x0A00 * slot:0x0A00 * (slot + 1)] = src[0x0A00 * sslot:0x0A00 * (sslot + 1)]
    dst[0x1FF8 + 2 * slot:0x1FFA + 2 * slot] = bytes([0x1B, 0xE4])
    s = V.slot_sum(bytes(dst), slot)
    dst[0x1FF0 + 2 * slot:0x1FF2 + 2 * slot] = bytes([s & 0xFF, s >> 8])


def fixtures():
    cs = open(os.path.join(ROOT, 'saves/field_re/colsave3.srm'), 'rb').read()
    rs = open(os.path.join(ROOT, 'saves/ct_real_save.srm'), 'rb').read()
    os.makedirs(OUT, exist_ok=True)
    fx = {'colsave3': cs, 'ct_real_save': rs}
    tri = bytearray(cs)
    put_slot(tri, 1, cs, 0)
    put_slot(tri, 2, cs, 0)
    fx['triple'] = bytes(tri)
    bad = bytearray(cs)
    put_slot(bad, 1, cs, 0)
    bad[0x1FF2] ^= 0x01                                    # slot 1's stored sum off by one: 4040
    fx['badsum'] = bytes(bad)
    mix = bytearray(cs)
    put_slot(mix, 2, rs, 0)
    fx['mixed'] = bytes(mix)
    paths = []
    for k, v in fx.items():
        p = os.path.join(OUT, k + '.srm')
        open(p, 'wb').write(v)
        paths.append(p)
    return paths


SCRIPT = '0:13,8:2,0:100,8:2,0:80'   # from f120: the title's two Starts, then loc 0 runs to its `C8 41`
LEAD = 3                             # the trace starts this many frames before C2/8000's (C0/0B5D is the frame before)
SPAN = 24                            # ... and runs past C2/8087 (19.45 frames on three saves)


def cut(script, n):
    """The first n frames of a --btnscript."""
    out, left = [], n
    for tok in script.split(','):
        m, k = tok.split(':')
        if left <= 0:
            break
        out.append('%s:%d' % (m, min(int(k), left)))
        left -= int(k)
    if left > 0:
        out.append('0:%d' % left)
    return ','.join(out)


def shell_frame(st120, env):
    """The monitor frame of C2/8000's first visit from st120 under SCRIPT (--hookpc; a visit on line >= 225 is the next
    monitor frame, docs/DEBUG_SURFACE.md). Found, not assumed: the boot's own timeline depends on the frame rule the
    oracle runs (CT_ORACLE_NMION_HOLD=1 holds the title's NMI-on stretches, so loc 0 -- and its `C8 41` -- comes sooner)."""
    out = subprocess.run([M.CT, '--hookpc', 'C28000',
                          '--from', st120, '--btnscript', SCRIPT, '--frames', '220'],
                         cwd=ROOT, env=env, capture_output=True, text=True).stdout
    for ln in out.splitlines():
        w = ln.split()
        if len(w) > 2 and w[0].startswith('f') and w[1].startswith('visits=') and w[1] != 'visits=0':
            h = int(w[0][1:])
            v = int(ln.split('vPos:')[1].split()[0].rstrip('*'))
            return h + (1 if v >= 225 else 0)
    return None


def measure(srm):
    tag = os.path.splitext(os.path.basename(srm))[0]
    st120 = os.path.join(OUT, tag + '_f120.state')
    stpre = os.path.join(OUT, tag + '_pre.state')
    tr = os.path.join(OUT, tag + '_init.txt')
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_SRAM=srm)
    subprocess.run([M.CT, '--fb', os.path.join(OUT, '_f120.raw'), '--frames', '120'], cwd=ROOT,
                   env=dict(env, PEEK_SAVE=st120), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    j = shell_frame(st120, env)
    if j is None:
        return None
    subprocess.run([M.CT, '--peek', st120, os.path.join(OUT, '_x.raw'), '--btnscript', cut(SCRIPT, j - LEAD)],
                   cwd=ROOT, env=dict(env, PEEK_SAVE=stpre), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([M.CT, '--cputrace', stpre, str(SPAN), tr], cwd=ROOT, env=dict(env, CPUTRACE_BEAM='1'),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t = beam.load(tr)

    def at(pc, start=0):
        for i in range(start, len(t)):
            if t[i][0] == pc:
                return i
        return None
    j8000 = at('C2:8000')
    if j8000 is None:
        return None
    c = None                                               # the caller's C0/0B5D: the last one before the JSL
    for i in range(j8000, -1, -1):
        if t[i][0] == 'C0:0B5D':
            c = i
            break
    a = at('C2:8127', j8000)
    b = at('C2:812A', a) if a is not None else None
    d = at('C2:8087', j8000)
    if None in (a, b, c, d):
        return None
    return beam.nonrefresh(t[a], t[b]), beam.nonrefresh(t[c], t[d])


def main():
    srms = sys.argv[1:] or fixtures()
    for srm in srms:
        sram = open(srm, 'rb').read()
        verdicts = ' '.join('%04X' % V.slot_verdict(sram, k) for k in range(3))
        m = measure(srm)
        out = subprocess.run([M.FIELD, '--menucost', srm], cwd=ROOT, capture_output=True, text=True).stdout
        model = int(out.split('scan ')[1]) + 40 if 'scan ' in out else -1   # the C model (src/menu_cost.c), + 8124's JSR
        tag = os.path.splitext(os.path.basename(srm))[0]
        if m is None:
            print('menu-init: %s slots %s: no init chain traced (a blank cart takes the short circuit)' % (tag, verdicts))
            continue
        d156, stretch = m
        print('menu-init: %s slots %s: D156 %d / model %d, stretch %d = %.3f F -> S0 = C+%d'
              % (tag, verdicts, d156, model, stretch, stretch / beam.F, stretch // beam.F))


if __name__ == '__main__':
    main()
