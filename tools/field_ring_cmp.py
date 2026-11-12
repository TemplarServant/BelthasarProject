#!/usr/bin/env python3
r"""The field's BG tilemaps as the port keeps them, against the cartridge's VRAM -- and the picture drawn from them.

    python tools/field_ring_cmp.py [name ...] [--png]

A field BG is a 64x32 tilemap in VRAM ($6000 / $6800 / $7000, `$1D7C` / `$1D7E` / `$1D80`). The ROM fills it at the
load (C0/0A50), rebuilds a 32x32 half of it on op 0xE5's styles (C0/179A) and otherwise only edits the row or column
the camera crosses (C0/93E1 -> the builders C0/8243-8442 -> the NMI's twelve uploaders C0/8445-87EF). BG2 and BG3 take
the CAMERA's window in those edits, whatever their own scroll: a layer a script moved away from the camera gets the
camera's columns where its own would be, and the picture shows them. The port keeps the same tilemaps (move.c
CtVmBgEntryFill / CtVmBgEdgeStream / CtVmBgEdgeNmi, ct_vm.c's style frames; field.c's VRAM image and g_ringKnown) and
draws the cells on the screen whose word is not the one the map puts there from the word (scene.c RingOv,
CtFieldSceneSetRing). disasm/notes/field_bg_tilemap.md sec. 7.

Both sides start from one cartridge state cut inside a playthrough leg (the leg's pad played that far on the cartridge,
cached with the route's key) BEFORE the room's load -- the port only knows the words its own uploads wrote -- and go on
with the leg's own pad. Then:

    fieldring: <name> leg <leg>[ f<at>] +<n> frames of the leg's pad: BG1 <k> known words, <d> not the cartridge's; BG2 ...
    fieldring: <name> picture: <d> px differ at port <lag>; cells drawn from the tilemap BG1 <a> BG2 <b> BG3 <c>

A word is `known` when one of the port's uploads wrote it (CT_SHOTS_RING dumps the image and the flags); BG3 is listed
when the room has a layer 3 (`$0BCA` bit 0, `$01BB` = 0). The cells are the port's last `ringov:` line (CT_RINGDBG).
--png writes build/_fieldring/<name>.png: the cartridge, the port, the pixels apart.

Regression checks: the `field-ring-*` family (docs/TESTING.md).
"""
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import field_picture_cmp as F  # noqa: E402   (the states cut inside a leg, cached in its work folder)
import playthrough as P  # noqa: E402

WORK = os.path.join(ROOT, 'build', '_fieldring')
SCRATCH = os.path.join(WORK, 'run%d' % os.getpid())           # this run's own files (the suite runs checks side by side)

# name: the leg, the frame of the cut (before the load of the room the picture is taken in; 0 = the leg's start) and
# the frames played on.
RINGS = {
    # Mystic Mountain's cutscene, loc 0A3 (leg 86, the load at f15146): op 0xE6 drifts BG1 alone 80 px right (f15860 on),
    # op 0xE5 rebuilds BG1 over the mountain's opening, then the three layers go 16 px left and 31 up (f16431-16462).
    # BG2 stands 80 px behind BG1 by then, so its tilemap columns 62-63 come on at the screen's left -- and the left
    # edge's builder C0/83E5 gave them the CAMERA's chip columns 9 and 8 (`$58` = `$1D0A`), not the map's wrap: 269 px
    # of the picture before the port kept the tilemaps.
    'camera-columns': dict(leg='86_magic_cave', at=15100, frames=1400),
    # Arris dome, loc 0DD (leg 40, the room loaded inside the leg): BG2 in the half-camera mode (`$0BC9` bit 7), its
    # own window `$12-$18` and committers C0/95E3-9683. As the camera goes, one frame in every 16 shows cells of BG2's
    # tilemap that are not the map's at its scroll (73 px on leg frame 2846: the cartridge's, to the pixel).
    'half-camera-edge': dict(leg='40_guardian', at=0, frames=2846),
}


def words(vram):
    return [vram[(0x6000 + k) * 2] | (vram[(0x6000 + k) * 2 + 1] << 8) for k in range(0x1800)]


def run(name, want_png):
    d = RINGS[name]
    legs = P.route()
    i = P.find(legs, d['leg'])
    if i > 0:
        P.ensure_states(legs, i - 1)
    P.TMP = F.WORK
    os.makedirs(F.WORK, exist_ok=True)
    # (a cut inside the leg is cached: the cartridge plays the leg that far once)
    start = F.state_at(legs, i, d['at']) if d['at'] else P.start_of(legs, i)
    P.TMP = SCRATCH
    os.makedirs(SCRATCH, exist_ok=True)
    pad = F.span(legs[i]['pad'], d['at'], d['frames'])
    keys, env = P.walkscript, dict(os.environ)
    if d['at']:                                               # the port's first key: the leg's pad frame before the cut
        prev = F.span(legs[i]['pad'], d['at'] - 1, 1)
        P.walkscript = lambda p: ','.join('H%X:%d' % (mk, n) if mk else '.:%d' % n for mk, n in prev + p)
    os.environ['CT_SHOTS_RING'] = '1'
    os.environ['CT_RINGDBG'] = '1'
    try:
        _, pics = P.port_timeline(start, pad)
    finally:
        P.walkscript = keys
        os.environ.clear()
        os.environ.update(env)
    c = P.oracle_picture(start, pad)                          # (leaves the cartridge's end.raw.vram / .wram beside it)
    leg = legs[i]['name'].split('_')[0]
    if not pics:
        print('fieldring: %s leg %s: the port drew no picture' % (name, leg))
        return 1
    k, n = P.picture_grade(c, pics)
    end = sum(q for _, q in pad)
    rf = os.path.join(SCRATCH, 'p_f%d.ring' % (end + k))
    vf, wf = os.path.join(SCRATCH, 'end.raw.vram'), os.path.join(SCRATCH, 'end.raw.wram')
    if not (os.path.exists(rf) and os.path.exists(vf) and os.path.exists(wf)):
        print('fieldring: %s leg %s: no tilemap dump (port %d, cartridge %d)' % (name, leg, os.path.exists(rf), os.path.exists(vf)))
        return 1
    ring, cw, w = open(rf, 'rb').read(), words(open(vf, 'rb').read()), open(wf, 'rb').read()
    pw = [ring[j * 2] | (ring[j * 2 + 1] << 8) for j in range(0x1800)]
    kn = ring[0x3000:0x4800]
    parts = []
    for L in range(3 if (w[0x0BCA] & 1) and not w[0x01BB] else 2):
        b = L * 0x800
        parts.append('BG%d %d known words, %d not the cartridge\'s' % (
            L + 1, sum(kn[b:b + 0x800]), sum(1 for j in range(b, b + 0x800) if kn[j] and pw[j] != cw[j])))
    print("fieldring: %s leg %s%s +%d frames of the leg's pad: %s" % (
        name, leg, ' f%d' % d['at'] if d['at'] else '', d['frames'], '; '.join(parts)))
    cells = (0, 0, 0)
    for line in (P.PORT_STDOUT or '').splitlines():
        m = re.match(r'ringov: f(\d+) cells BG1 (\d+) BG2 (\d+) BG3 (\d+)', line)
        if m and int(m.group(1)) <= end + k:
            cells = tuple(int(m.group(j)) for j in (2, 3, 4))
    print('fieldring: %s picture: %d px differ at port %+d; cells drawn from the tilemap BG1 %d BG2 %d BG3 %d' % (
        (name, n, k) + cells))
    if want_png:
        out = os.path.join(WORK, name + '.png')
        F.png(out, c, pics[k])
        print('  -> %s' % out)
    return 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    bad = [a for a in args if a not in RINGS]
    if bad:
        print('fieldring: no case named %s (known: %s)' % (', '.join(bad), ', '.join(sorted(RINGS))))
        return 1
    rc = 0
    try:
        for name in args or sorted(RINGS):
            rc |= run(name, '--png' in sys.argv)
    finally:
        shutil.rmtree(SCRATCH, ignore_errors=True)
    return rc


if __name__ == '__main__':
    sys.exit(main())
