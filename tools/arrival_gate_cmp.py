#!/usr/bin/env python3
r"""A location's "where did the party come from" gate, port against cartridge.

    python tools/arrival_gate_cmp.py

Scene 65's obj0 fn0 is

    49 05 01 7E 0B     loc[0B] = the WORD at $7E:0105 (op 0x49, C0/67C7 -- the previous location)
    13 0B F0 01 00 07  if loc[0B] == $01F0 (op 0x13, C0/6313 `LDA $7F0200,X` in 16-bit mode) ...
    65 83 A3 / 66 84 A3      ... set $7F01A3 bit 3, clear bit 4
    13 0B F1 01 00 07  if loc[0B] == $01F1 ...
    65 84 A3 / 66 83 A3      ... set bit 4, clear bit 3

(twelve locations open the same way: 65 97 99 334 335 355 357 401 411 417 418 449). Both halves live on one local
word, so it grades the VM's local byte order (little-endian, as the 65816 stores it) AND op 0x49, which the event VM
did not run at all before 2026-09-24.

THE CARTRIDGE: saves/ow_w0.state (the 1000 A.D. overworld) with $0105 poked to the arrival, then `ct.exe --teleport 65
... --method jmp` -- the ROM's own location load runs obj0's fn0. THE PORT: that post-load WRAM with the graded bytes
cleared ($7F01A3 and the locals $7F0200-03FF), loaded as scene 65, then CT_CHANGELOC reloads location 65 on frame 3
with $0105 still in the $7E page -- the port's own init pass has to derive them again. (`--loadwram` alone cannot:
the init pass runs before the seed overlays $7E.) One line per arrival:

    arrival-gate: prev=01F0 oracle 01A3=08 loc0B=F001 port 01A3=08 loc0B=F001 -> MATCH
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import lake  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OW = os.path.join(ROOT, 'saves', 'ow_w0.state')


def oracle(prev):
    p, _ = lake.read_payload(OW)
    p = bytearray(p)
    p[lake.WRAM_OFF + 0x105] = prev & 0xFF
    p[lake.WRAM_OFF + 0x106] = prev >> 8
    src = os.path.join(ROOT, 'build', '_arrival_ow_%04X.state' % prev)
    out = os.path.join(ROOT, 'build', '_arrival_o_%04X.state' % prev)
    lake.write_wrapped(src, bytes(p))
    subprocess.run([CT, '--teleport', '65', '10', '10', '0', '--from', src, '--method', 'jmp', '--frames', '120',
                    '--out', out], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    w = lake.wram(out)
    return w, w[0x101A3], w[0x10216] | (w[0x10217] << 8)


def port(w, prev):
    w = bytearray(w)
    w[0x101A3] = 0
    for i in range(0x10200, 0x10400):
        w[i] = 0
    seed = os.path.join(ROOT, 'build', '_arrival_p_%04X.lakewram' % prev)
    with open(seed, 'wb') as fh:
        fh.write(b'#lakewram\nRAM:131072:' + bytes(w))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_CHANGELOC='65,10,10,0', CT_FIELD_MEMMON='101A3:1,10216:2,0105:2')
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '65', '--loadwram', seed, '--selftest',
                        os.path.join(ROOT, 'build', '_arrival.raw'), '--frames', '60'],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    flags = loc = None
    for line in (r.stdout + r.stderr).splitlines():
        m = re.search(r'101A3=([0-9A-F]{2})\*?\s+10216=([0-9A-F]{4})', line)
        if m:
            flags, loc = int(m.group(1), 16), int(m.group(2), 16)
    return flags, loc


def main():
    for prev in (0x01F0, 0x01F1):
        w, of, ol = oracle(prev)
        pf, pl = port(w, prev)
        # the port prints the local as its two bytes in address order ("F001" = F0 then 01), like the oracle's
        ol_s = '%02X%02X' % (ol & 0xFF, ol >> 8)
        pl_s = '%04X' % pl if pl is not None else '----'
        pf_s = '%02X' % pf if pf is not None else '--'
        ok = pf == of and pl_s == ol_s
        print('arrival-gate: prev=%04X oracle 01A3=%02X loc0B=%s port 01A3=%s loc0B=%s -> %s'
              % (prev, of, ol_s, pf_s, pl_s, 'MATCH' if ok else 'DIFFER'))


if __name__ == '__main__':
    main()
