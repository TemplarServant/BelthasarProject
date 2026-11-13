#!/usr/bin/env python3
r"""Named ranges of a fight's WRAM, the port against the cartridge at one frame.

    python tools/battle_wram_cmp.py [name ...]

Both sides start from one cartridge state cut inside a playthrough leg BEFORE the fight (the port boots the fight
itself) and go on with the leg's own pad; at the end frame the ranges are compared byte for byte:

    battlewram: <name> leg <leg> f<at> +<n> frames of the leg's pad: <range> <d> of <n> bytes differ[, ...]

Cases (CASES below):

  boot-fill   what the fight's boot fills with FF and nothing rewrites before its first enemy turn:
              $AECC-$AEE2  FD/B78E-B799 `LDA #FF / STA $AECC,X / STA $AED8,X` for X < $0B -- the resolved-target list
                           and the eleven bytes after it (`$AED8` is the literal path's replay, C1/AC24 `LDA $AED8 /
                           STA $AECC`; its one writer in a fight is C1/AF13)
              $B242-$B249  C1/80D8-80E2 `LDA #FF / STA $B242,X` for X < 8, the end of the boot's two shuffles -- every
                           enemy slot's re-eval mark

Regression checks: `battle-boot-fill-vs-cartridge` (docs/TESTING.md).
"""
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import field_picture_cmp as F  # noqa: E402   (the states cut inside a leg, cached in its work folder)
import playthrough as P  # noqa: E402

WORK = os.path.join(ROOT, 'build', '_battlewram')
SCRATCH = os.path.join(WORK, 'run%d' % os.getpid())           # this run's own files (the suite runs checks side by side)

# name: the leg, the frame of the cut (in the field, before the fight's transfer), the frames played on, the ranges.
CASES = {
    # Zeal, leg 117's first fight (its boot at leg frame 683), 217 frames in: the first action's prologue, before any
    # enemy has taken a turn.
    'boot-fill': dict(leg='117_kajar_restock', at=600, frames=300, ranges=((0xAECC, 0xAEE2), (0xB242, 0xB249))),
}


def run(name):
    d = CASES[name]
    legs = P.route()
    i = P.find(legs, d['leg'])
    if i > 0:
        P.ensure_states(legs, i - 1)
    P.TMP = F.WORK
    os.makedirs(F.WORK, exist_ok=True)
    start = F.state_at(legs, i, d['at'])
    P.TMP = SCRATCH
    os.makedirs(SCRATCH, exist_ok=True)
    pad = F.span(legs[i]['pad'], d['at'], d['frames'])
    end = sum(q for _, q in pad)
    dump = os.path.join(SCRATCH, 'port.wram')
    keys, env = P.walkscript, dict(os.environ)
    prev = F.span(legs[i]['pad'], d['at'] - 1, 1)             # the port's first key: the leg's pad frame before the cut
    P.walkscript = lambda p: ','.join('H%X:%d' % (mk, n) if mk else '.:%d' % n for mk, n in prev + p)
    os.environ['CT_DUMPWRAM'] = dump
    os.environ['CT_DUMPWRAM_AT'] = str(end)
    try:
        P.port_timeline(start, pad)
    finally:
        P.walkscript = keys
        os.environ.clear()
        os.environ.update(env)
    P.oracle_picture(start, pad)                              # (leaves the cartridge's end.raw.wram beside it)
    leg = legs[i]['name'].split('_')[0]
    wf = os.path.join(SCRATCH, 'end.raw.wram')
    if not os.path.exists(dump) or not os.path.exists(wf):
        print('battlewram: %s leg %s: no WRAM (port %d, cartridge %d)' % (name, leg, os.path.exists(dump), os.path.exists(wf)))
        return 1
    p, w = open(dump, 'rb').read(), open(wf, 'rb').read()
    print("battlewram: %s leg %s f%d +%d frames of the leg's pad: %s" % (name, leg, d['at'], d['frames'], ', '.join(
        '$%04X-$%04X %d of %d bytes differ' % (a, b, sum(1 for k in range(a, b + 1) if p[k] != w[k]), b + 1 - a)
        for a, b in d['ranges'])))
    return 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    bad = [a for a in args if a not in CASES]
    if bad:
        print('battlewram: no case named %s (known: %s)' % (', '.join(bad), ', '.join(sorted(CASES))))
        return 1
    rc = 0
    try:
        for name in args or sorted(CASES):
            rc |= run(name)
    finally:
        shutil.rmtree(SCRATCH, ignore_errors=True)
    return rc


if __name__ == '__main__':
    sys.exit(main())
