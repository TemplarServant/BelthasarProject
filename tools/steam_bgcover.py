#!/usr/bin/env python3
"""Which rooms' backgrounds the Steam release's widescreen draws over, side by side -> the table kSteamCovers (src/field.c).

    python tools/steam_bgcover.py ["<Steam's Chrono Trigger folder>"] [--csv out.csv]

The widescreen (a mod) carries a room's BACKGROUND layers past the room's edge -- the layers the map header marks
(md[1..3], C0/A385-A4FB: md1 bit 4 / 5 = BG2 / BG3 detached ($1D36), md2 / md3 != 0 = BG2 / BG3 scrolling on its own
($1D35)). Where the Steam release's own widescreen draws scenery over them beside the edge, a background going on there
is no plausible continuation, and without the Steam margins drawn the side ends black instead (docs/WIDESCREEN.md).
This reads the player's own Steam copy (tools/steam_wide.py's reader; its folder found as the setup finds it) and, per
location and side, the room's rows whose first column past the edge holds a margin cell:

  no background                 the room has no background layer
  Steam leaves it               the Steam release has no margins there: the background goes on
  Steam fill                    its margins are one cell everywhere (the dark around an interior): the background goes on
  Steam draws over it / part    its margins draw over the background on every row / some rows: black past the edge

and prints the locations of the last two classes as kSteamCovers' lines (1 the left side, 2 the right, 3 both). Only
those numbers are in the tree; the Steam data stay on the player's machine. Checked 2026-10-24: 42 locations.
"""
import argparse
import csv
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import steam_wide as S  # noqa: E402


def census(folder):
    out = []
    with open(os.path.join(folder, 'resources.bin'), 'rb') as f:
        files = S.directory(f)
        marg = S.margins(f, files)
        for loc in range(S.LOCATIONS):
            info = files.get('Game/field/Mapinfo/mapinfo_%d.dat' % loc)
            if not info:
                continue
            mi = S._block(f, *info)
            hd = struct.unpack('<11H', mi[:22])
            left, top, right, bottom = hd[10] & 255, hd[10] >> 8, mi[22], mi[23]
            mt = files.get('Game/field/MapTable/MapTable_%04d.dat' % hd[6])   # the SNES map: its header md[0..3]
            md = S._block(f, *mt)[:4] if mt else b'\0\0\0\0'
            md1, md2, md3 = md[1], md[2], md[3]
            det = (4 if md1 & 0x20 else 0) | (2 if md1 & 0x10 else 0)
            bg = det | (2 if md2 else 0) | (4 if md3 else 0)
            if not md1 & 0x80:                                 # (no BG3 in this map)
                bg &= ~4
            m = marg.get(loc)
            w = h = 0
            planes = []
            fill = False
            if m:
                w, h, pl = m[0], m[1], m[2]
                n = w * h
                planes = [struct.unpack('<%dH' % n, pl[2 * n * k:2 * n * (k + 1)]) for k in range(4)] if n else []
                fill = len({v for pp in planes for v in pp if v}) <= 1
            x = (w - (right + 1)) // 2 if w else 0

            def cell(c, r):
                i = c + x
                return 0 <= i < w and 0 <= r < h and any(p[r * w + i] for p in planes)
            rows = bottom - top + 1
            for side, c0 in (('left', left - 1), ('right', right + 1)):
                cov = sum(1 for r in range(top, bottom + 1) if cell(c0, r))
                cls = ('no background' if not bg else 'Steam leaves it' if not m or cov == 0 else 'Steam fill' if fill
                       else 'Steam draws over it' if cov == rows else 'Steam draws over part')
                out.append(dict(location='%03X' % loc, side=side, md='%02X %02X %02X' % (md1, md2, md3), background=bg,
                                margins='none' if not m else ('fill' if fill else 'art'), margin_rows='%d/%d' % (cov, rows),
                                cls=cls))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('steam', nargs='?')
    ap.add_argument('--csv')
    a = ap.parse_args()
    folder = a.steam or S.find_steam()
    if not folder:
        sys.exit('steam-bgcover: no Steam copy of Chrono Trigger found (name its folder)')
    rows = census(folder)
    if a.csv:
        with open(a.csv, 'w', newline='') as o:
            wr = csv.DictWriter(o, fieldnames=list(rows[0]))
            wr.writeheader()
            wr.writerows(rows)
    covers = {}
    for r in rows:
        if r['cls'].startswith('Steam draws over'):
            covers[int(r['location'], 16)] = covers.get(int(r['location'], 16), 0) | (1 if r['side'] == 'left' else 2)
    print('steam-bgcover: %d locations the Steam release draws over a background beside the edge' % len(covers))
    items = ['{0x%03X, %d}' % kv for kv in sorted(covers.items())]
    for i in range(0, len(items), 10):
        print('  ' + ', '.join(items[i:i + 10]) + ',')
    return 0


if __name__ == '__main__':
    sys.exit(main())
