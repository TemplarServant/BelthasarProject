#!/usr/bin/env python3
r"""The field NMI's tilemap arm skips the CGRAM upload: port against the logical oracle.

    python tools/nmi_cgram_cmp.py

disasm/notes/field_tile_copy_e5.md sec. 2. The field NMI starts `EAD1 LDA $46 / BNE`: when C0/179A's style branch
left a tilemap in $46 (1-4, or bits 3/4) the NMI uploads it and `BRL EC15`s past C0/EB79-EC13 -- no CGRAM upload
(C0/712E, $7E:2200 x $200), no OAM, no VRAM queue, no text line. FD/E39C keeps stepping the page during the hold
(C0/00DE's FD:FFF7), so a location whose colours animate shows the colours of the frame before the first upload for as
long as the NMIs are taken.

  lever  saves/field_re/s236_lever.state / .lakewram (scene 236, loc $0EC): A twice at the lever obj9, whose fn1 runs
         four E5 copies of style 3 (mask 3B) -- the room's lights (CGRAM 55-59) flip bright/dim every 2 frames on the
         page. Per frame: the NMIs that do not reach C0/712E (the cartridge's `--hookpc 712E`, the port's
         CT_XFERTRACE `nmi:` lines), the frames on which the page differs from what CGRAM holds (the cartridge's page
         against its NMIs), and the picture: the port's P against the cartridge's P+2 (the field's display alignment).

Lines:
  nmi-cgram: lever no-cgram <runs> cartridge|port
  nmi-cgram: lever page-held <frames> cartridge      (a skipped NMI left CGRAM behind a changed page)
  nmi-cgram: lever picture f0-fN: <k> frame(s) differ [runs]
"""
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OUT = os.path.join(ROOT, 'build', '_nmicgram')

STATE = 'saves/field_re/s236_lever.state'
SEED = 'saves/field_re/s236_lever.lakewram'
BTN = '0:10,100:4,0:100,100:4,0:130'           # the oracle; the port's drive runs one frame later (A at f11, f115)
WALK = 'n:11,a:4,n:100,a:4,n:130'
LAST = 240


def runs(fs):
    out = []
    for f in sorted(fs):
        if out and f == out[-1][1] + 1:
            out[-1][1] = f
        else:
            out.append([f, f])
    return ', '.join(('f%d' % a) if a == b else ('f%d-f%d' % (a, b)) for a, b in out) or '-'


def thumbs(path):
    rec = 6 + 128 * 112 * 3
    data = open(path, 'rb').read() if os.path.exists(path) else b''
    return {struct.unpack('<I', data[i * rec:i * rec + 4])[0]: data[i * rec + 6:(i + 1) * rec]
            for i in range(len(data) // rec)}


def main():
    os.makedirs(OUT, exist_ok=True)
    tp, tc = os.path.join(OUT, '_lever_port.thumbs'), os.path.join(OUT, '_lever_cart.thumbs')
    for p in (tp, tc):
        if os.path.exists(p):
            os.remove(p)
    logical = dict(os.environ, CT_ORACLE_LOGICAL='1')

    # The cartridge's NMIs: C0/712E once per frame, none on a frame whose NMI a style tilemap took.
    r = subprocess.run([CT, '--hookpc', '712E', '--from', STATE, '--btnscript', BTN, '--frames', str(LAST + 1)],
                       cwd=ROOT, env=logical, capture_output=True, text=True, errors='replace')
    visits = {int(m.group(1)): int(m.group(2)) for m in re.finditer(r'^\s*f(\d+)\s+visits=(\d+)', r.stdout, re.M)}
    cskip = [f for f in range(LAST + 1) if visits.get(f, 1) == 0]

    # The cartridge's page per frame, and what its NMIs leave in CGRAM.
    r = subprocess.run([CT, '--monitor', '7E2200:512', '--from', STATE, '--btnscript', BTN], cwd=ROOT, env=logical,
                       capture_output=True, text=True, errors='replace')
    page, cur = {}, None
    for l in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+7E2200=([0-9A-F]+)', l)
        if m:
            page[int(m.group(1))] = m.group(2)
    held, cg = [], None
    for f in range(LAST + 1):
        if f in page:
            cur = page[f]
        if f not in cskip:
            cg = cur
        elif cur != cg:
            held.append(f)

    # The port: the NMIs its trace says a tilemap took, and its picture.
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_XFERTRACE='1',
               CT_SHOTS_THUMBS=tp, CT_SHOTS_THUMBS_EVERY='1')
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '236', '--loadwram', SEED, '--selftest',
                        os.path.join(OUT, '_end.raw'), '--frames', str(LAST + 1), '--walkscript', WALK],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    pskip = [int(m.group(1)) for m in re.finditer(r'^nmi: f(\d+) \$46=[0-9A-F]{2} tilemap', r.stdout, re.M)]

    print('nmi-cgram: lever no-cgram %s cartridge' % runs(cskip))
    print('nmi-cgram: lever no-cgram %s port' % runs(pskip))
    print('nmi-cgram: lever page-held %s cartridge' % (' '.join('f%d' % f for f in held) or '-'))

    subprocess.run([CT, '--peek', STATE, os.path.join(OUT, '_cart.raw'), '--btnscript', BTN], cwd=ROOT,
                   env=dict(logical, PEEK_THUMBS=tc, PEEK_THUMBS_EVERY='1'), capture_output=True, text=True,
                   errors='replace')
    po, pc = thumbs(tp), thumbs(tc)
    end = LAST - 2
    bad = [f for f in range(end + 1) if f not in po or f + 2 not in pc or po[f] != pc[f + 2]]
    print('nmi-cgram: lever picture f0-f%d: %d frame(s) differ%s' % (end, len(bad), (' [' + runs(bad) + ']') if bad else ''))


if __name__ == '__main__':
    main()
