#!/usr/bin/env python
"""The overworld's place-name banner (class $5628, C2/5628), the port against the rule's cartridge (disasm/notes/vwf_engine.md
sec. 5-6).

saves/ow_w0.state (the cartridge) and saves/field_re/ow_w0.lakewram (the port) on 1F0 by Truce: left 72 frames, down 8, left 8
onto the Market's entrance cell, 60 frames standing, right 30 (off the cell), 60 standing. The port's world reads its first
pad 43 frames into the run; each phase is aligned on its own event -- the appear on the frame the scanner names the cell
($1B58 != 0), the leave on the right walk's first step ($0283 leaves its value) -- because the host's input latency is not the
subject. Graded, at each compared frame (the cartridge's `--peek` k against the port's frame k - 1 + offset, both its WRAM
dump and its picture):
  - the block's timeline: every change of its state, $1B58, its X/Y, the pen $0234 and the glyph count $0217;
  - WRAM: the anim script and frame $7E:8600-8620 and the strip $8621-$8A20, the engine's direct page $0200-$02A1, the
    banner's block (its frame counter +3/+4 left out: the two sides load at different frames; +3E too, the counter the
    port's C2/0E1D bumps on every block for its atlas picker -- the ROM's does not);
  - VRAM: the OBJ tiles $1E00-$1FFF the NMI's queue uploads;
  - OAM: the banner's entries in the shadow $7E:0700 (tiles $1E0-$1EB);
  - the picture inside the text's box (the pen's width, the pieces' rows; clouds drift through the rest of the 96 px).
Prints `ow-banner: timeline ...` and one `ow-banner: <phase> c<k> p<f> ...` line per compared frame.
"""
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', '_owbanner')
DRIVE_O = ['40:72', '20:8', '40:8', '0:60', '80:30', '0:60']
DRIVE_P = 'n:43,l:72,d:8,l:8,n:60,r:30,n:60'
MON = ['0EF2:1', '1B58:1', '0F04:2', '0F08:2', '0234:1', '0217:1', '0283:2']
BLOCK = 0x0EF0
SKIP = (3, 4, 0x3E)                       # the frame counter; +3E is the port's obAnimTick bookkeeping (every block)
APPEAR = (89, 90, 92, 94, 95, 96, 100, 130)
LEAVE = (150, 155, 156, 157, 158, 159, 160)


def oracle_events():
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--monitor', ','.join('7E' + m for m in MON), '--from',
                        os.path.join(ROOT, 'saves', 'ow_w0.state'), '--btnscript', ','.join(DRIVE_O), '--frames', '240',
                        '--diff'], cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True, text=True,
                       errors='replace')
    ev = []
    for l in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', l)
        if m and '7E0EF2=' in l:
            ev.append((int(m.group(1)), tuple(re.findall(r'7E[0-9A-F]{4}=([0-9A-F]+)', m.group(2)))))
    return ev


def port_run(frames):
    os.makedirs(OUT, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_FIELD_MEMMON=','.join(MON),
               CT_SHOTS=os.path.join(OUT, 'p'), CT_SHOTS_AT=','.join(str(f) for f in frames),
               CT_OWDUMP=os.path.join(OUT, 'pd'), CT_OWDUMP_AT='%d-%d' % (min(frames), max(frames)))
    r = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '1', '--loadwram',
                        os.path.join(ROOT, 'saves', 'field_re', 'ow_w0.lakewram'), '--selftest', '--frames', '250',
                        '--walkscript', DRIVE_P], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    ev = []
    for l in r.stdout.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\S+\s+(.*)', l)
        if m:
            ev.append((int(m.group(1)), tuple(re.findall(r'[0-9A-F]{4}=([0-9A-F]+)', m.group(2)))))
    return ev


def anchors(ev):
    """(the frame $1B58 turns nonzero, the right walk's first step)."""
    a = next((f for f, v in ev if v[1] != '00'), None)
    stop = None
    l = None
    for f, v in ev:
        if a is None or f <= a:
            continue
        if stop is None:
            stop = v[6]
        elif v[6] != stop:
            l = f; break
    return a, l


def timeline(ev, a, l):
    """The banner's changes (state, $1B58, X, Y, pen, glyphs) from each anchor, the pad's $0283 left out."""
    out, last = [], None
    for f, v in ev:
        if f < a:
            continue
        k = v[:6]
        if k == last:
            continue
        last = k
        tag = ('+%d' % (f - a)) if l is None or f < l else ('L+%d' % (f - l))
        out.append(tag + ':' + '/'.join(k))
    return out


def raw_port(p):
    d = open(p, 'rb').read(); w, h = struct.unpack('<II', d[:8])
    return lambda x, y: d[8 + (y * w + x) * 3: 11 + (y * w + x) * 3]


def raw_oracle(p):
    d = open(p, 'rb').read()
    return lambda x, y: bytes(d[((2 * (y + 8)) * 512 + 2 * x) * 4 + k] for k in (3, 2, 1))


def oam_banner(w):
    """The shadow's entries whose tile name is the banner's ($1E0-$1EB): (x, y, word) sorted."""
    hi = w[0x0900:0x0920]
    out = []
    for i in range(128):
        x, y, t, a = w[0x0700 + 4 * i: 0x0704 + 4 * i]
        name = t | ((a & 1) << 8)
        if 0x1E0 <= name <= 0x1EB and y != 0xE0:
            xb = (hi[i >> 2] >> ((i & 3) * 2)) & 1
            out.append((x | (xb << 8), y, t | (a << 8)))
    return sorted(out)


def main():
    oe = oracle_events()
    # one port run covering both phases; the frames are known only once its anchors are, so first a run without shots
    pe0 = port_run([300])
    oa, ol = anchors(oe)
    pa, pl = anchors(pe0)
    if None in (oa, ol, pa, pl):
        print('ow-banner: anchors not found (cartridge %s/%s, port %s/%s)' % (oa, ol, pa, pl)); return 1
    da, dl = pa - oa, pl - ol
    frames = [k - 1 + da for k in APPEAR] + [k - 1 + dl for k in LEAVE]
    pe = port_run(frames)
    to, tp = timeline(oe, oa, ol), timeline(pe, pa, pl)
    to = [e for e in to if not e.startswith('L+') or int(e[2:].split(':')[0]) <= max(LEAVE) - ol]
    tp = [e for e in tp if not e.startswith('L+') or int(e[2:].split(':')[0]) <= max(LEAVE) - ol]
    same = to == tp
    print('ow-banner: timeline %s -- %d changes from the name (cartridge f%d, port f%d) and the walk off (f%d / f%d)' % (
        'same' if same else 'DIFFERENT', len(to), oa, pa, ol, pl))
    if not same:
        for x, y in zip(to, tp):
            if x != y:
                print('ow-banner:   first difference cartridge %s port %s' % (x, y)); break
        if len(to) != len(tp): print('ow-banner:   %d changes on the cartridge, %d in the port' % (len(to), len(tp)))
    rows = []
    for phase, ks, d in (('appear', APPEAR, da), ('leave', LEAVE, dl)):
        for k in ks:
            f = k - 1 + d
            o_raw = os.path.join(OUT, 'o%d.raw' % k)
            bs, t = [], 0
            for it in DRIVE_O:
                b, n = it.split(':'); n = int(n)
                if t + n >= k: bs.append('%s:%d' % (b, k - t)); break
                bs.append(it); t += n
            subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--peek', os.path.join(ROOT, 'saves', 'ow_w0.state'), o_raw,
                            '--btnscript', ','.join(bs)], cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True)
            ow = open(o_raw + '.wram', 'rb').read(); ov = open(o_raw + '.vram', 'rb').read()
            pd = open(os.path.join(OUT, 'pd_f%d' % f), 'rb').read()
            pw, pv = pd[:0x20000], pd[0x20000:0x30000]
            nw = sum(ow[a] != pw[a] for a in range(0x8600, 0x8A21))
            nd = sum(ow[a] != pw[a] for a in range(0x0200, 0x02A2))
            nb = sum(ow[BLOCK + a] != pw[BLOCK + a] for a in range(0x40) if a not in SKIP)
            nv = sum(ov[a] != pv[a] for a in range(0x1E00 * 2, 0x2000 * 2))
            rows.append((phase, k, f, o_raw, nw, nd, nb, nv, oam_banner(ow), oam_banner(pw), ow[0x0234]))
    # The box: the text's columns (the pen $0234 from the first piece's X) by the pieces' 16 rows, from the cartridge's entries
    # of that frame -- or, on a frame without the banner, of the nearest frame with it (its absence is graded there too).
    shown = [r for r in rows if r[8]]
    for phase, k, f, o_raw, nw, nd, nb, nv, oo, po, pen in rows:
        ref = oo and (oo, pen) or (lambda r: (r[8], r[10]))(min(shown, key=lambda r: abs(r[1] - k)))
        x0 = ref[0][0][0]; y0 = ref[0][0][1]
        a_, b_ = raw_port(os.path.join(OUT, 'p_f%d.raw' % f)), raw_oracle(o_raw)
        npx = tot = 0
        for y in range(max(y0, 0), min(y0 + 17, 224)):
            for x in range(max(x0, 0), min(x0 + ref[1] + 1, 256)):
                tot += 1; npx += a_(x, y) != b_(x, y)
        print('ow-banner: %s c%d p%d wram %d+%d+%d bytes differ, vram %d, oam %d/%d entries %s, text box %d of %d px differ' % (
            phase, k, f, nw, nd, nb, nv, len(oo), len(po), 'same' if oo == po else 'DIFFERENT', npx, tot))
    return 0


if __name__ == '__main__':
    sys.exit(main())
