#!/usr/bin/env python3
r"""The MENU SHELL's open and close, frame by frame, against the cartridge (disasm/notes/menu_shell_timing.md).

    python tools/menu_shell_cmp.py

What is graded is the screen's BRIGHTNESS LEVEL on every frame from the press to the RTL: the caller's 15-frame fade
(C0/193E / C0/0CCE), the switch and the 16 black frames of the init chain (C2/800E..808C), the screen's substate 0
(the ribbon builder's yields / the shop's C2/DACA), the fade-in C2/83B2 -> C2/83D0 (+2 a frame, 15 at P+9), the
close C2/82B2 -> C2/83CA (-2 a frame over the frozen image, RTL the frame after 0), and after the RTL the field's
reload (black: C0/0B5D -> C0/0B38 priced by src/field_cost.c, W = floor(C / F) + 1 frames) and C0/2824's fade-in. The level is the frame's mean luma
over the full frame of the same phase (the field before the press, the menu once lit), scaled to 0..15.

The cartridge side is the LOGICAL oracle (CT_ORACLE_LOGICAL=1, CLAUDE.md's lag-frame rule: the shop's C2/DACA and
the ribbon's first tab build overrun the NMI on real time). A frame's logic reaches the screen through that frame's
NMI, so the cartridge DISPLAYS frame F's state during frame F+1, and a --peek after L frames is the picture displayed
during frame L-1: port frame F is `--peek` at F+2 (re-aligned 2026-09-24 with tools/menu_audit_cmp.py -- every content
change of the menu and the field walk agree on it; the old F+1 needed an extra brightness lag in the port that no
tile or sprite change has). The drives press on the frame the cartridge reads the press (its pad latch, C2/84D2,
is read one frame after the button goes down), so port frame F is oracle frame F. The x-menu and shop-close drives run
through the reload and the field's fade-in (disasm/notes/menu_shell_timing.md section 10).

    menu-shell: <drive> f<A>-f<B>: <N> frame(s) differ [f..: oracle x port y, ...]
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import fb, peekshots  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
CT = os.path.join(ROOT, 'build', 'ct.exe')

# name, oracle state, port seed, scene, oracle btnscript (LakeSnes masks), port walkscript, frames, the full-field
# reference frame, the lit-menu reference frame, the first frame the menu owns the picture (the switch) and the RTL
DRIVES = [
    ('x-menu', 'saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
     '0:4,200:2,0:54,1:2,0:100', 'n:5,m:1,n:55,b:1,n:100', (3, 127), 3, 55, 20, 69),
    ('shop', 'saves/field_re/shop90_lake.state', 'saves/field_re/shop90.lakewram', 5,
     '0:10,100:2,0:418,1:2,0:40', 'n:10,a:1,n:420,b:1,n:40', (9, 62), 9, 60, 27, None),
    ('shop-close', 'saves/field_re/shop90_lake.state', 'saves/field_re/shop90.lakewram', 5,
     '0:10,100:2,0:418,1:2,0:100', 'n:10,a:1,n:420,b:1,n:100', (428, 499), 9, 428, 27, 439),
    # the X menu on the OVERWORLD (menu_audit_cmp.py's owsave presses): the world's fade object, C2/2E23's stash, the
    # shell, the Save tab, then B, B -- the menu's fade, C2/2E72 + C2/2C93's black window, the world's fade-in object.
    # The world's own picture differs (the port's seed enters it through a load), so the level is each side's own.
    ('ow-menu', 'saves/ow_w0.state', 'saves/field_re/ow_w0.lakewram', 1,
     '0:80,200:2,0:48,40:2,0:38,100:2,0:78,1:2,0:38,1:2,0:68', 'n:81,m:1,n:49,l:1,n:39,a:1,n:79,b:1,n:39,b:1,n:68',
     (78, 356), 78, 200, 101, 299),
]


def luma(path, oracle):
    w, h, p = fb.load(path)
    off = 8 if oracle else 0
    tot = 0.0
    for y in range(224):
        row = (y + off) * w * 3
        for x in range(256):
            i = row + x * 3
            tot += 0.299 * p[i] + 0.587 * p[i + 1] + 0.114 * p[i + 2]
    return tot / (256 * 224)


def oracle_frames(state, script, frames, tag):
    """Peek after F+2 frames = the cartridge's display of frame F's state, on the logical oracle -- every frame from ONE
    run (PEEK_SHOTS, tools/ctlib/peekshots.py: the same bytes as a --peek cut to F+2)."""
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    got = peekshots.shots(state, script, [f + 2 for f in frames], env=env,
                          prefix=os.path.join(ROOT, 'build', '_mshell_o_%s' % tag))
    out = {f: luma(got[f + 2], True) for f in frames}
    peekshots.clean(got)
    return out


def port_frames(seed, scene, walk, frames, tag):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SHOTS=os.path.join(ROOT, 'build', '_mshell_p_%s' % tag), CT_SHOTS_AT=','.join(str(f) for f in frames))
    last = max(frames) + 2
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', str(scene), '--loadwram', seed, '--selftest',
                    os.path.join(ROOT, 'build', '_mshell.raw'), '--frames', str(last), '--walkscript', walk],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return {f: luma(os.path.join(ROOT, 'build', '_mshell_p_%s_f%d.raw' % (tag, f)), False) for f in frames}


def main():
    for name, ostate, seed, scene, obtn, pwalk, (a, b), fref, mref, sw, rtl in DRIVES:
        frames = sorted(set(list(range(a, b + 1)) + [fref, mref]))
        ol = oracle_frames(ostate, obtn, frames, name)
        pl = port_frames(seed, scene, pwalk, frames, name)
        bad = []
        for f in range(a, b + 1):
            menu = f >= sw and (rtl is None or f < rtl)
            ro, rp = (ol[mref], pl[mref]) if menu else (ol[fref], pl[fref])
            lo = round(15 * ol[f] / ro) if ro else 0
            lp = round(15 * pl[f] / rp) if rp else 0
            if lo != lp:
                bad.append('f%d: oracle %d port %d' % (f, lo, lp))
        print('menu-shell: %s f%d-f%d: %d frame(s) differ%s' % (name, a, b, len(bad), (' [' + ', '.join(bad) + ']') if bad else ''))


if __name__ == '__main__':
    main()
