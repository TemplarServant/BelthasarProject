#!/usr/bin/env python3
r"""The port's battle HUD strip against the cartridge's, on the cartridge's own WRAM.

    python tools/battle_hud_strip_cmp.py <oracle_state> <frame> [--btnscript S] [--label L]
    python tools/battle_hud_strip_cmp.py --suite

1. build/ct.exe --peek <state> --frames <frame> [--btnscript S]: the cartridge's picture and its 128 KB WRAM.
   (With --btnscript the peek runs the whole script and ignores --frames: give the script's length as <frame>.)
2. build/ct_field.exe with CT_BATTLE_RENDERWRAM=<that .wram>: the port draws the strip from the same bytes -- the
   queue $A6D9, the frame rows $0B40, the names $ACBC, the party, the band $C867.
3. The 48 strip lines ($2989 bit 1: renderY 160..207, else 8..55) are compared pixel for pixel, and the columns of
   every differing pixel are listed as 8-px cells.

Prints:

    hud-strip-cmp: <label> $A6D9=00FFFF $95D5=00 $9609=00 strip y8..55 px 12288/12288 identical

--suite runs SUITE below: the queue states that tell the ROM's law (C1/0785, C1/081E, C1/08A2) from the port's old
pictures and $960A gates. A residual cell at cols 26..29 is a running ATB gauge -- the cartridge's picture is its last
HUD rebuild, a few frames old (ct-hud-rebuild-lag-vs-wram). (disasm/notes/battle_hud_command_windows.md.)
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'ct_field.exe')
FOREST = 'saves/battle_re/forest_wait_ambush_active.state'
SUITE = [
    ('forest-empty', FOREST, 400, None),                                         # the base: names + party windows
    ('forest-crono', FOREST, 440, None),                                         # seat 0 alone: "Chaos" at cols 7..11
    ('forest-both', FOREST, 515, None),                                          # seats 0 and 1
    ('forest-crono-picker', FOREST, 540, '0:520,100:2,0:18'),                    # $9609 = 1: the windows stay, no name
    ('forest-crono-dequeued', FOREST, 954, '0:520,100:2,0:30,100:2,0:400'),      # seat 1 alone: "Beetl" / "Avian"
    ('gato-marle-alone', 'saves/gato_inbattle.state', 824, '0:600,100:2,0:20,100:2,0:200'),  # the old "picker" bake
    ('bat9-frog-active', 'saves/battle_re/bat9_lake.state', 189, '0:21,80:2,0:6,80:2,0:12,80:2,0:140'),  # seats 0 + 2
]


def compare(state, frame, script, label):
    extra = ['--btnscript', script] if script else []
    tmp = 'build/_hsc_%d' % os.getpid()
    peek = tmp + '.raw'
    r = subprocess.run([CT, '--peek', state, peek, '--frames', str(frame)] + extra,
                       capture_output=True, text=True, errors='replace')
    if not os.path.exists(peek + '.wram'):
        return ['hud-strip-cmp: %s the oracle peek failed %s' % (label, (r.stdout + r.stderr)[-200:])]
    ofb = open(peek, 'rb').read()
    wram = open(peek + '.wram', 'rb').read()
    out = tmp + '_port.raw'
    penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
                CT_BATTLE_RENDERWRAM=peek + '.wram', CT_BATTLE_RENDEROUT=out)
    subprocess.run([FIELD, '--assets', 'ct_assets.dat'], env=penv, capture_output=True, text=True, errors='replace')
    if not os.path.exists(out):
        return ['hud-strip-cmp: %s the port render failed' % label]
    d = open(out, 'rb').read()
    pw, ph = struct.unpack('<II', d[:8])
    pfb = d[8:]

    def orc(x, y):                                    # the port's row r is the cartridge's r+8 (frame_region_cmp.py)
        i = ((y + 8) * 2 * 512 + x * 2) * 4
        return (ofb[i + 3], ofb[i + 2], ofb[i + 1])

    y0 = 160 if wram[0x2989] & 0x02 else 8
    same, cells = 0, {}
    for y in range(y0, y0 + 48):
        for x in range(256):
            p = tuple(pfb[(y * pw + x) * 3:(y * pw + x) * 3 + 3])
            if p == orc(x, y):
                same += 1
            else:
                cells.setdefault((y - y0) // 8, set()).add(x // 8)
    lines = ['hud-strip-cmp: %s $A6D9=%s $95D5=%02X $9609=%02X strip y%d..%d px %d/%d identical' % (
        label, wram[0xA6D9:0xA6DC].hex().upper(), wram[0x95D5], wram[0x9609], y0, y0 + 47, same, 48 * 256)]
    for row in sorted(cells):
        lines.append('hud-strip-cmp:   %s strip row %d differs in cols %s' % (label, row, sorted(cells[row])))
    if os.environ.get('HUD_STRIP_KEEP'):
        lines.append('hud-strip-cmp: kept %s (oracle) and %s (port)' % (peek, out))
        return lines
    for p in (peek, peek + '.wram', peek + '.vram', peek + '.oam', out):
        try:
            os.remove(p)
        except OSError:
            pass
    return lines


def main():
    os.chdir(ROOT)
    if len(sys.argv) > 1 and sys.argv[1] == '--suite':
        for label, state, frame, script in SUITE:
            print('\n'.join(compare(state, frame, script, label)))
        return
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    state, frame = sys.argv[1], int(sys.argv[2])
    script = sys.argv[sys.argv.index('--btnscript') + 1] if '--btnscript' in sys.argv else None
    label = sys.argv[sys.argv.index('--label') + 1] if '--label' in sys.argv else 'f%d' % frame
    print('\n'.join(compare(state, frame, script, label)))


if __name__ == '__main__':
    main()
