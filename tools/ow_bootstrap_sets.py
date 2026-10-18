#!/usr/bin/env python3
r"""Run the overworld's OBJECT BOOTSTRAP for every world and report the SET it builds.

    python tools/ow_bootstrap_sets.py

The ROM does not restore an object array when it enters an overworld: it allocates ONE object
(class $0F63, script $7F:0400) and that bootstrap script builds the rest
(disasm/notes/sequence_table.md section 9). This drives the port's own object VM through that
bootstrap in each of the eight worlds and asks whether the multiset of CLASSES it produces is the
one the cartridge produced -- objSnap, the live oracle capture each world asset was packed from.

⚠ SET, not placement. Which SLOT an object lands in is decided by C2/048A's first-fit, and that
depends on how the spawns interleave across frames; worlds 1 and 4 still differ there. The six
per-slot checks (`ow-object-bootstrap*-vs-oracle`) cover placement where it is exact. This one
covers content, in all eight.
"""
import os
import subprocess
import sys

FIELD = os.path.abspath('build/Belthasar.exe')
FRAMES = '90'


def main():
    bad = 0
    for w in range(8):
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   CT_OWBOOTSTRAP=str(w), CT_OWBOOTSTRAP_FRAMES=FRAMES)
        out = subprocess.run([FIELD, '--assets', 'ct_assets.dat'], env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout
        line = [l.strip() for l in out.decode('utf-8', 'replace').splitlines()
                if ' SET ' in l]
        if not line:
            print('owboot: world %d produced no SET line' % w)
            bad += 1
            continue
        print(line[0])
        if 'MATCHES' not in line[0]:
            bad += 1
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
