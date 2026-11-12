#!/usr/bin/env python3
r"""The battle's message window as the port draws it, against the cartridge's -- its bytes and its picture.

    python tools/battle_msg_window_cmp.py [name ...] [--png]

The window (a target's name, a tech's, "Can't run away", a reward line) is 24 lines of the band preset `$CCEC = $10`
selects: BG1's frame rows and BG3's text rows of the tilemap at VRAM $7800, each at the scroll its HDMA record gives,
the frame through the line's colour math and the text in CGRAM 9-11. The cartridge keeps

    $7E:7FFD-$81FC   the rows' image, built once by the boot (CC/E4FC-E563) and uploaded; VRAM keeps the rows, and the
                     fight takes the bytes for its grid copies (C1/54A1) from its first action on
    $7E:B400-$B7FF   the text's tile strip, drawn by the text engine (C2/0003 / C2/0009 from CD/025E, CD/027F, CD/04DF)
    $0200-$023F      the engine's block and state (the id, the table, the pen, the cursors)

and the port keeps the same bytes (battle.c battleMsgWindowImage, battleMsgVwfBegin / battleMsgVwfRun on src/vwf.c) and
draws the window from them (field.c battleMsgWindowDraw; the rows from battle.c CtBattleMsgWindowRows, what VRAM holds).
disasm/notes/battle_message_window.md. The image is compared where the fight has not reused it yet (`image=True`).

Both sides start from one cartridge state cut inside a playthrough leg BEFORE the fight (the port boots the fight
itself) and go on with the leg's own pad to a frame where the window stands. Then:

    msgwindow: <name> leg <leg> f<at> +<n> frames of the leg's pad: [image <d> of 512 bytes differ, ]strip <d> of 1024, engine <d> of 64
    msgwindow: <name> the window's 24 lines (rows <y0>-<y1>): <d> px differ at port <lag>; text pixels <t>

`text pixels` counts the cartridge's pixels of the window that are the text's two visible colours (CGRAM 9 and 11 of the
battle's page, as the cartridge shows them), so a window with no text in it cannot pass for one that matches. The lag is
the port frame whose window is nearest the cartridge's (a fight runs a frame or two off the field's alignment).
--png writes build/_msgwindow/<name>.png: the cartridge, the port, the pixels apart.

Regression checks: `battle-message-window-*` (docs/TESTING.md).
"""
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import field_picture_cmp as F  # noqa: E402   (the states cut inside a leg, cached in its work folder)
import playthrough as P  # noqa: E402

WORK = os.path.join(ROOT, 'build', '_msgwindow')
SCRATCH = os.path.join(WORK, 'run%d' % os.getpid())           # this run's own files (the suite runs checks side by side)

# name: the leg, the frame of the cut (in the field, before the fight's transfer), the frames played on, and the
# window's first row on the screen (184: the HUD strip above, band preset $12; 8: the strip below, preset $13).
WINDOWS = {
    # Zeal, the fight of leg 117's first room (the boot's image at leg frame 699): the Att picker's target box, message
    # 0C of CE:EF11 (`12 01`: the enemy's name, typed a character a frame) -- "Red Scout".
    'target-name': dict(leg='117_kajar_restock', at=600, frames=300, top=184, image=True),
    # The same leg's second fight, its first reward line: message 00 of CE:EF11 with the number at $0200 (FD/AD26) --
    # "Got 468 experience point(s)!", a line that crosses the strip's two halves.
    'reward-line': dict(leg='117_kajar_restock', at=5100, frames=2700, top=184),
}
AREAS = (('image', 0x7FFD, 0x200), ('strip', 0xB400, 0x400), ('engine', 0x0200, 0x40))


def run(name, want_png):
    d = WINDOWS[name]
    legs = P.route()
    i = P.find(legs, d['leg'])
    if i > 0:
        P.ensure_states(legs, i - 1)
    P.TMP = F.WORK
    os.makedirs(F.WORK, exist_ok=True)
    start = F.state_at(legs, i, d['at']) if d['at'] else P.start_of(legs, i)
    P.TMP = SCRATCH
    os.makedirs(SCRATCH, exist_ok=True)
    pad = F.span(legs[i]['pad'], d['at'], d['frames'])
    end = sum(q for _, q in pad)
    dump = os.path.join(SCRATCH, 'port.wram')
    keys, env, shots = P.walkscript, dict(os.environ), P.SHOTS
    if d['at']:                                               # the port's first key: the leg's pad frame before the cut
        prev = F.span(legs[i]['pad'], d['at'] - 1, 1)
        P.walkscript = lambda p: ','.join('H%X:%d' % (mk, n) if mk else '.:%d' % n for mk, n in prev + p)
    os.environ['CT_DUMPWRAM'] = dump
    os.environ['CT_DUMPWRAM_AT'] = str(end)
    P.SHOTS = tuple(range(-6, 7))
    try:
        _, pics = P.port_timeline(start, pad)
    finally:
        P.walkscript, P.SHOTS = keys, shots
        os.environ.clear()
        os.environ.update(env)
    c = P.oracle_picture(start, pad)                          # (leaves the cartridge's end.raw.wram beside it)
    leg = legs[i]['name'].split('_')[0]
    wf = os.path.join(SCRATCH, 'end.raw.wram')
    if not pics or not os.path.exists(dump) or not os.path.exists(wf):
        print('msgwindow: %s leg %s: no picture or no WRAM (port pictures %d, port WRAM %d, cartridge WRAM %d)' % (
            name, leg, len(pics), os.path.exists(dump), os.path.exists(wf)))
        return 1
    p, w = open(dump, 'rb').read(), open(wf, 'rb').read()
    print("msgwindow: %s leg %s f%d +%d frames of the leg's pad: %s" % (name, leg, d['at'], d['frames'], ', '.join(
        '%s %d of %d bytes differ' % (nm, sum(1 for k in range(a, a + n) if p[k] != w[k]), n) for nm, a, n in AREAS
        if nm != 'image' or d.get('image'))))
    y0 = d['top']
    lo, hi = y0 * 768, (y0 + 24) * 768
    band = c[lo:hi]
    best = None
    for k in sorted(pics, key=lambda k: (abs(k), k)):
        q = pics[k][lo:hi]
        n = sum(1 for j in range(0, len(band), 3) if band[j:j + 3] != q[j:j + 3])
        if best is None or n < best[1]:
            best = (k, n)
    # the text's colours on the cartridge's page: CGRAM 9 (the shadow) and 11 (the body), as the PPU shows a 15-bit word
    def rgb(word):
        return bytes(((word >> (5 * ch)) & 31) << 3 | ((word >> (5 * ch)) & 31) >> 2 for ch in range(3))
    tones = {rgb(w[0x2200 + 2 * ci] | (w[0x2201 + 2 * ci] << 8)) for ci in (9, 11)}
    text = sum(1 for j in range(0, len(band), 3) if band[j:j + 3] in tones)
    print("msgwindow: %s the window's 24 lines (rows %d-%d): %d px differ at port %+d; text pixels %d" % (
        name, y0, y0 + 23, best[1], best[0], text))
    if want_png:
        out = os.path.join(WORK, name + '.png')
        F.png(out, c, pics[best[0]])
        print('  -> %s' % out)
    return 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    bad = [a for a in args if a not in WINDOWS]
    if bad:
        print('msgwindow: no case named %s (known: %s)' % (', '.join(bad), ', '.join(sorted(WINDOWS))))
        return 1
    rc = 0
    try:
        for name in args or sorted(WINDOWS):
            rc |= run(name, '--png' in sys.argv)
    finally:
        shutil.rmtree(SCRATCH, ignore_errors=True)
    return rc


if __name__ == '__main__':
    sys.exit(main())
