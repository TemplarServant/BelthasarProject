#!/usr/bin/env python3
r"""Diff the PORT's HDMA walker against the ORACLE's own per-scanline trace, line for line.

    python tools/seq_hdma_cmp.py [boot frame]

Both sides are derived live, so nothing here is a pinned constant:

  * the oracle is booted to a frame inside sequence-table entry 0 (the motorcycle race, six HDMA
    channels -- disasm/notes/sequence_table.md section 7.1), and `ct.exe --hdma` prints the colour
    math state of every scanline of that frame. That is the cartridge's own answer.
  * the same run's `.wram` sidecar gives the 64 KB $7E page the channels' tables live in, and
    `ct_field --seqhdma` replays the walker over it and prints the same bands.

⚠ The oracle numbers its trace 0..224 and its first band is the state BEFORE the frame's first
transfer; the port's starts at line 1, the first line the SNES draws. Both are expanded to a
per-line map over 1..224 before comparing, so the numbering cannot paper over a real difference.

⚠ No capture is committed: `saves/` is gitignored on purpose (the repo regenerates ROM-derived
captures rather than storing them), so the page image is rebuilt into build/ on every run.
"""
import os
import re
import subprocess
import sys

ROM = 'Chrono_Trigger_USA_SNES.sfc'
CT = os.path.abspath('build/ct.exe')
FIELD = os.path.abspath('build/ct_field.exe')
BOOT = int(sys.argv[1]) if len(sys.argv) > 1 else 8600
RAW = 'build/_seqhdma.raw'
PAGE = 'build/_seqhdma_page.bin'
LINES = 224

ORC = re.compile(r'lines\s+(\d+)\.\.\s*(\d+)\s+fixed=\(\s*(\d+),\s*(\d+),\s*(\d+)\)\s+'
                 r'math=([0-9A-F]{2})\s+half=(\d)\s+sub=(\d)\s+TM=([0-9A-F]{2})\s+TS=([0-9A-F]{2})')
OUR = re.compile(r'seqhdma: lines\s+(\d+)\.\.\s*(\d+)\s+math=([0-9A-F]{2})\s+half=(\d)\s+sub=(\d)\s+'
                 r'fixed=\(\s*(\d+),\s*(\d+),\s*(\d+)\)\s+TM=([0-9A-F]{2})\s+TS=([0-9A-F]{2})')


def run(args, **kw):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL, **kw).stdout.decode('utf-8', 'replace')


def expand(text, rx, order):
    """Explode banded output into {line: tuple}, for lines 1..LINES."""
    out = {}
    for m in rx.finditer(text):
        g = m.groups()
        a, b = int(g[0]), int(g[1])
        key = tuple(g[i] for i in order)
        for ln in range(a, b + 1):
            if 1 <= ln <= LINES:
                out[ln] = key
    return out


for p in (RAW, RAW + '.wram', PAGE):
    if os.path.exists(p):
        os.remove(p)

state = 'build/_seqhdma.state'
run([CT, ROM, '--fb', RAW, '--frames', str(BOOT)], env=dict(os.environ, PEEK_SAVE=state))
if not os.path.exists(RAW + '.wram'):
    sys.exit('seq-hdma-cmp: the oracle wrote no .wram sidecar at boot frame %d' % BOOT)

wram = open(RAW + '.wram', 'rb').read()
if wram[0x384] != 0x60:
    sys.exit('seq-hdma-cmp: boot f%d is in $0384=%02X, not entry 0 ($60)' % (BOOT, wram[0x384]))
if wram[0x38C] != 0x3F:
    sys.exit('seq-hdma-cmp: boot f%d has $038C=%02X, not the six channels ($3F)'
             % (BOOT, wram[0x38C]))
open(PAGE, 'wb').write(wram[:0x10000])

# the cartridge's answer: math order is (math, half, sub, r, g, b, TM, TS)
ref = expand(run([CT, ROM, '--hdma', state]), ORC, (5, 6, 7, 2, 3, 4, 8, 9))
ours = expand(run([FIELD, '--assets', 'ct_assets.dat', '--seqhdma', PAGE]), OUR,
              (2, 3, 4, 5, 6, 7, 8, 9))

if len(ref) != LINES:
    sys.exit('seq-hdma-cmp: the oracle trace covered %d of %d lines' % (len(ref), LINES))
if len(ours) != LINES:
    sys.exit('seq-hdma-cmp: the walker covered %d of %d lines' % (len(ours), LINES))

bad = [ln for ln in range(1, LINES + 1) if ref[ln] != ours[ln]]
if bad:
    ln = bad[0]
    print('seq-hdma-cmp: FIRST DIFFERENCE at line %d -- oracle %s, ours %s' % (ln, ref[ln], ours[ln]))
changes = sum(1 for ln in range(2, LINES + 1) if ref[ln] != ref[ln - 1])
print('seq-hdma-cmp: entry 0 boot f%d -- %d of %d scanlines match the oracle (%d band changes)'
      % (BOOT, LINES - len(bad), LINES, changes))
sys.exit(0 if not bad else 1)
