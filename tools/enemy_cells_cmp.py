#!/usr/bin/env python3
r"""The port's enemy sprites against the cartridge's OBJ layer, on the cartridge's own WRAM.

    python tools/enemy_cells_cmp.py <oracle_state> <frame> [--btnscript S]

1. build/ct.exe --peek <state> --frames <frame> with PEEK_ISOBG=4: the cartridge's OBJ layer alone, plus its
   128 KB WRAM and its OAM (.wram / .oam sidecars).
2. build/Belthasar.exe with CT_BATTLE_RENDERWRAM=<that .wram>: the port draws every combatant from the same WRAM
   (the published poses $9907, the staged records $29BF, the live CGRAM page $2200) on a black screen.
3. For every staged enemy slot, the 16x16 rectangles of its OAM entries in the cartridge's shadow -- the entries
   whose names are that combatant's run of the OAM template (CC:F84D -> CC:FAF3, one entry per cell of its record
   type) -- are compared pixel for pixel. Pixels the cartridge's OBJ layer leaves at the backdrop colour count as
   black on both sides; the rows the vehicle's HUD strip covers (its `renderwram: hudband` line) and its selection
   bracket's four corner cells (`renderwram: bracket`) are skipped.

Prints, per slot and in total:

    enemy-cells-cmp: slot0 id=B3 type=2 pose=00 cells=12 px 3072/3072 identical
    enemy-cells-cmp: f330 total 5632/5632 identical

(disasm/notes/battle_enemy_cells.md; the law is src/battle.c CtBattleEnemyCells.)
"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '.')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import collections
import os
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import oam as ctoam  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    os.chdir(ROOT)
    state, frame = sys.argv[1], int(sys.argv[2])
    extra = []
    if '--btnscript' in sys.argv:
        extra = ['--btnscript', sys.argv[sys.argv.index('--btnscript') + 1]]
    rom = _romlib.load('Chrono_Trigger_USA_SNES.sfc')
    if state == 'build/_z_f7085.state' and not os.path.exists(state):   # the attract 087's leg (fxobj_cmp.py)
        subprocess.run([CT, '--peek', 'saves/attract_cena_inicial.state', 'build/_ztmp.raw', '--frames', '7085'],
                       env=dict(os.environ, PEEK_SAVE=state), capture_output=True)
    tmp = 'build/_ecc_%d' % os.getpid()
    peek = tmp + '.raw'
    env = dict(os.environ, PEEK_ISOBG='4')
    r = subprocess.run([CT, '--peek', state, peek, '--frames', str(frame)] + extra, env=env,
                       capture_output=True, text=True, errors='replace')
    if not os.path.exists(peek + '.wram'):
        sys.exit('enemy-cells-cmp: the oracle peek failed\n' + r.stdout[-400:] + r.stderr[-400:])
    ofb = open(peek, 'rb').read()
    wram = open(peek + '.wram', 'rb').read()
    sprites = ctoam.decode(ctoam.load(peek + '.oam'))
    out = tmp + '_port.raw'
    penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
                CT_BATTLE_RENDERWRAM=peek + '.wram', CT_BATTLE_RENDEROUT=out)
    pr = subprocess.run([FIELD, '--assets', 'ct_assets.dat'], env=penv, capture_output=True, text=True, errors='replace')
    if not os.path.exists(out):
        sys.exit('enemy-cells-cmp: the port render failed')
    # The vehicle draws the HUD strip over the sprites and reports the rows it changed; the cartridge's isolated
    # OBJ layer has no HUD, so those rows are not compared.
    band = re.search(r'renderwram: hudband .* rows=(\d+)\.\.(\d+)', pr.stdout)
    hud = range(int(band.group(1)), int(band.group(2)) + 1) if band else range(0)
    d = open(out, 'rb').read()
    pw, ph = struct.unpack('<II', d[:8])
    pfb = d[8:]

    def orc(x, y):
        i = ((y + 8) * 2 * 512 + x * 2) * 4
        return (ofb[i + 3], ofb[i + 2], ofb[i + 1])

    counts = collections.Counter(orc(x, y) for y in range(0, 224, 4) for x in range(0, 256, 4))
    backdrop = counts.most_common(1)[0][0]
    keep = os.environ.get('ENEMY_CELLS_KEEP')

    def compare(rects, label, skip):
        # A 16x16 sprite at OAM (x, y) is port rows y..y+15 and cartridge rows (y+8..y+23)*2 (the port's row r is the
        # cartridge's r+8, the same convention as tools/frame_region_cmp.py).
        same = tot = 0
        seen = set()
        for (x0, y0) in rects:
            for dy in range(16):
                for dx in range(16):
                    x, y = x0 + dx, y0 + dy
                    if x < 0 or x >= 256 or y >= 224 or (x, y) in seen or y in hud or (x, y) in skip:
                        continue
                    seen.add((x, y))
                    o = orc(x, y)
                    if o == backdrop:
                        o = (0, 0, 0)
                    p = tuple(pfb[(y * pw + x) * 3:(y * pw + x) * 3 + 3])
                    tot += 1
                    same += (o == p)
                    if o != p and keep:
                        print('  %s (%d,%d) oracle %s port %s' % (label, x, y, o, p))
        return same, tot

    # The selection bracket: the cartridge's OAM entries 0-3 when they carry its name (06, C1/1115).
    brk_rects = [(sp['x'], sp['y']) for sp in sprites[:4] if sp['tile'] == 0x06 and sp['onscreen']]
    brk = set((x + i, y + j) for (x, y) in brk_rects for i in range(16) for j in range(16))
    rd16 = lambda a: rom[a] | (rom[a + 1] << 8)
    total_same = total = 0
    for slot in range(8):
        rec = wram[0x29BF + slot * 12:0x29BF + slot * 12 + 12]
        if rec[6] != 0:
            continue
        cmb = 3 + slot
        typ = rec[7] & 7
        if typ > 3:
            continue
        ncell = rd16(0x0CF40A + typ * 2) // 10                          # C1/1C69: CC:F40A stride / 10
        k0 = (rd16(0x0CF84D + (0x0C + cmb) * 2) - 0x11B0) // 4           # CF/E8E3 -> the template entry
        names = [rom[0x0CFAF3 + 2 * k] for k in range(k0, min(k0 + ncell, 44))]
        rects = [(sp['x'], sp['y']) for sp in sprites[4:] if sp['tile'] in names and sp['onscreen']]
        same, tot = compare(rects, 'slot%d' % slot, brk)            # (the bracket's pixels are graded on their own line)
        pose = wram[0x9907 + cmb]
        print('enemy-cells-cmp: slot%d id=%02X type=%d pose=%02X cells=%d on-screen=%d px %d/%d identical' % (
            slot, rec[5], typ, pose, ncell, len(rects), same, tot))
        total_same += same
        total += tot
    print('enemy-cells-cmp: f%d total %d/%d identical' % (frame, total_same, total))
    if brk_rects:
        same, tot = compare(brk_rects, 'bracket', set())
        print('enemy-cells-cmp: bracket seat%d A4EF=%02X cells=%d px %d/%d identical' % (
            wram[0x95D5], wram[0xA4EF], len(brk_rects), same, tot))
    if os.environ.get('ENEMY_CELLS_KEEP'):                            # debugging: keep both frames
        print('enemy-cells-cmp: kept %s (oracle, OBJ only) and %s (port)' % (peek, out))
        return
    for p in (peek, peek + '.wram', peek + '.vram', peek + '.oam', out):
        try:
            os.remove(p)
        except OSError:
            pass


if __name__ == '__main__':
    main()
