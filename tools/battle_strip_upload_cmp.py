#!/usr/bin/env python3
r"""The battle HUD strip's upload pipeline, port against the rule's cartridge, frame by frame.

    python tools/battle_strip_upload_cmp.py main | item | cancel

disasm/notes/battle_hud_upload_pipeline.md: the strip reaches the screen as WRAM tilemap images the menu pass writes and
POSTS ($0CC0 the command level, $A6E1 the Tech list built from $99E3, $0E80 the item list -- BG3 rows 1..6 through the
one-slot record and `$A0`; $0B40 the window frames -- BG1 rows 16..21 through `$99E2`), and the next NMI DMAs (CF/E300).
Both sides run GATO's fight from the same WRAM (saves/battle_re/gato_wait.state, Crono alone with every tech and 99 MP:
the dossier's PEEK_POKE), the dossier's drives:

    main    Down f640, A f652 (Tech), A f664 (Cyclone), A f676 (the target)
    item    Down f640, Down f652, A f664 (the item list), Down f676
    cancel  Down f640, A f652 (Tech), B f664

  cartridge  build/ct.exe --monitor under the rule (CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1): the images and the VRAM
             regions $7820 / $7A00 (`V` regions), and --peek PEEK_THUMBS every frame;
  port       build/ct_field.exe --scene 6 --loadwram (the same WRAM), the battle's buttons at the cartridge's frames
             (CT_BATTLE_SCRIPT_ABS), CT_FIELD_MEMMON of the same images and of its strip VRAM image (`V` regions), and
             CT_SHOTS_THUMBS every frame.

Graded from the band's settled frame (f296: the iris opens c281-c293, and its frames' colour ramp is the band's own
business, disasm/notes/battle_hud_band_anim.md) to the drive's end:
  the images     port frame f against cartridge frame f (the same logical frame);
  the VRAM       port frame f against cartridge frame f + 1 (the port's upload is the NMI that opens the next frame);
  the picture    the strip's thumbnail rows (80..103) of port frame P against the cartridge's P + 2 (the frame alignment).
And the frames where the strip's picture changes, on each side (the events: a key's effect a pass later, the HP refresh).
"""
import os
import re
import struct
import subprocess
import sys

sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]
import numpy as np  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', '_strip_upload')
STATE = 'saves/battle_re/gato_wait.state'
POKE = '2607=63,2608=00,2609=63,260A=00,2830=08,2837=FF,2980=00,2981=80,2982=80'
REC = 6 + 128 * 112 * 3
RULE = {'CT_ORACLE_LOGICAL': '1', 'CT_ORACLE_VIRTUAL_BEAM': '1'}
# (frame, button mask, hold) after the A that starts the fight (frames 0-7); LakeSnes bits B 001, Down 020, A 100
DRIVES = {
    'main':   ([(640, 0x20), (652, 0x100), (664, 0x100), (676, 0x100)], 840),
    'item':   ([(640, 0x20), (652, 0x20), (664, 0x100), (676, 0x20)], 720),
    'cancel': ([(640, 0x20), (652, 0x100), (664, 0x001)], 700),
}
PORT_BTN = {0x20: 'd', 0x100: 'a', 0x001: 'b'}
IMAGES = [('0CC0', 0x180), ('A6E1', 0x180), ('0E80', 0x180), ('0B40', 0x180), ('99E3', 0x200)]
VRAM = [('7820', 0x180), ('7A00', 0x180)]
FROM = 296


def oracle_input(drive, total):
    parts, f = ['100:8'], 8
    for at, b in drive:
        parts.append('0:%d' % (at - f)); parts.append('%x:2' % b); f = at + 2
    parts.append('0:%d' % (total - f))
    return ','.join(parts)


def port_script(drive, start, total):
    parts, f = [], 0
    for at, b in drive:
        bf = at - start
        parts.append('n:%d' % (bf - f)); parts.append('%s:2' % PORT_BTN[b]); f = bf + 2
    parts.append('n:%d' % max(1, total - start - f))
    return ','.join(parts)


def thumbs(path):
    if not os.path.exists(path): return {}
    d = open(path, 'rb').read(); out = {}
    for i in range(len(d) // REC):
        f, loc = struct.unpack('<IH', d[i * REC:i * REC + 6])
        out[f] = np.frombuffer(d[i * REC + 6:(i + 1) * REC], np.uint8).reshape(112, 128, 3).astype(np.int16)
    return out


def ffill(rows, total):
    out, last = {}, None
    for f in range(total):
        if f in rows: last = rows[f]
        out[f] = last
    return out


def run_cartridge(drive, total, tag):
    spec = ','.join(['7E%s:%d' % (a, n) for a, n in IMAGES] + ['V%s:%d' % (a, n) for a, n in VRAM])
    inp = oracle_input(drive, total)
    env = dict(os.environ, **RULE, PEEK_POKE=POKE)
    ct = os.path.abspath('build/ct.exe')
    o = subprocess.run([ct, '--monitor', spec, '--from', STATE, '--input', inp, '--diff'], env=env, capture_output=True,
                       text=True, errors='replace').stdout
    rows = {}
    for l in o.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', l)
        if not m: continue
        rows[int(m.group(1))] = {a[2:]: bytes.fromhex(v) for a, v in re.findall(r'\b([0-9A-F]{6})=([0-9A-F]+)', m.group(2))}
    th = os.path.join(OUT, tag + '_c.thumbs')
    if os.path.exists(th): os.remove(th)
    subprocess.run([ct, '--peek', STATE, os.path.join(OUT, tag + '_c.raw'), '--btnscript', inp],
                   env=dict(env, PEEK_THUMBS=th, PEEK_THUMBS_EVERY='1'), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return ffill(rows, total), thumbs(th)


def run_port(drive, total, tag):
    w = bytearray(open(os.path.join(ROOT, STATE), 'rb').read()[0x45:0x45 + 0x20000])
    for kv in POKE.split(','):
        a, v = kv.split('='); w[int(a, 16)] = int(v, 16)
    lw = os.path.join(OUT, tag + '.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + bytes(w))
    th = os.path.join(OUT, tag + '_p.thumbs')
    mm = ','.join(['%s:%X' % (a, n) for a, n in IMAGES] + ['V%s:%X' % (a, n) for a, n in VRAM])
    start, p = 220, ''
    for attempt in range(2):
        if os.path.exists(th): os.remove(th)
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
                   CT_AUDIO_SEEDSTATE=STATE, CT_BATTLE_SCRIPT_ABS='1', CT_BATTLE_SCRIPT=port_script(drive, start, total),
                   CT_FIELD_MEMMON=mm, CT_SHOTS_THUMBS=th, CT_SHOTS_THUMBS_EVERY='1', CT_SHOTS=os.path.join(OUT, tag + '_p'),
                   CT_SHOTS_AT=str(total - 1), CT_SHOTS_EXIT='1')
        p = subprocess.run([os.path.abspath('build/ct_field.exe'), '--assets', 'ct_assets.dat', '--scene', '6', '--loadwram',
                            lw, '--walkscript', 'a:8'], env=env, capture_output=True, text=True, errors='replace').stdout
        m = re.search(r'battlephase: begins at field f(\d+)', p)
        if not m or int(m.group(1)) == start: break
        start = int(m.group(1))
    rows = {}
    for l in p.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\w+\s+(.*)', l)
        if not m: continue
        rows[int(m.group(1))] = {(a[2:] if len(a) == 6 else a): bytes.fromhex(v)
                                 for a, v in re.findall(r'\b([0-9A-F]{4,6})=([0-9A-F]+)', m.group(2))}
    return ffill(rows, total), thumbs(th)


def strip_changes(th, lo, hi):
    """The frames whose strip rows (thumbnail 80..103) differ from the frame before."""
    out = []
    for f in range(lo, hi):
        a, b = th.get(f - 1), th.get(f)
        if a is not None and b is not None and int((np.abs(a[80:104] - b[80:104]).max(axis=2) > 0).sum()):
            out.append(f)
    return out


def main():
    os.chdir(ROOT)
    if len(sys.argv) < 2 or sys.argv[1] not in DRIVES: print(__doc__); return 1
    name = sys.argv[1]
    drive, total = DRIVES[name]
    os.makedirs(OUT, exist_ok=True)
    co, cth = run_cartridge(drive, total, name)
    po, pth = run_port(drive, total, name)
    last = total - 2
    ni = nv = nt = n = 0
    bad = []
    for f in range(FROM, last):
        c, p, c1 = co.get(f), po.get(f), co.get(f + 1)
        if not c or not p or not c1: continue
        n += 1
        im = [a for a, k in IMAGES if c.get(a) != p.get(a)]
        vr = [a for a, k in VRAM if c1.get(a) != p.get(a)]
        ni += not im; nv += not vr
        if (im or vr) and len(bad) < 6: bad.append('f%d %s' % (f, ' '.join(im + ['V' + a for a in vr])))
    pics = 0
    for f in range(FROM, last - 2):
        a, b = pth.get(f), cth.get(f + 2)
        if a is None or b is None: continue
        pics += 1
        nt += int((np.abs(a[80:104] - b[80:104]).max(axis=2) > 0).sum()) == 0
    cch = strip_changes(cth, 560, last)
    pch = [f + 2 for f in strip_changes(pth, 558, last - 2)]
    print('strip-upload %s: images %s %d/%d frames identical, vram $7820/$7A00 %d/%d (port f = cartridge f+1), '
          'strip %d/%d thumbnails identical (port P = cartridge P+2)' %
          (name, '/'.join(a for a, k in IMAGES), ni, n, nv, n, nt, pics))
    print('strip-upload %s: the strip changes at cartridge %s; port (+2) %s -- %s' %
          (name, ' '.join('c%d' % f for f in cch), ' '.join('c%d' % f for f in pch), 'SAME' if cch == pch else 'DIFFER'))
    for b in bad: print('   ' + b)
    return 0


if __name__ == '__main__':
    sys.exit(main())
