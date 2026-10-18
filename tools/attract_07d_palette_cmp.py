#!/usr/bin/env python3
r"""The field's colours after the attract's 07D demo battle (the Frog Squash), port against cartridge.

    python tools/attract_07d_palette_cmp.py [port_frame]

port_frame (optional, default 7238) is the port's dump frame; the port's numbering moves with the attract's clock (the
rule's migration: the 07D's return 59 frames earlier under the infinitely fast CPU), the cartridge's f5228 does not.

The user's F5 at loc 07D (2026-09-27): after Frog's tech the candles' glow turned GREEN. The glow is BG3 on the sub-screen
added to BG1/BG2 (CGADSUB 03, TS 04), drawn in colours 24-31 -- the rows the battle's effect loads its own palette into
(CD/101D, CD/1045) and CD/0453's CD/0E23 puts back from `$CCEE`. The demo's last action ends the battle, so that CD/0453
is the END PATH's (CD/03B4 -> CD/03C6), which the port counted and never ran.

Both sides at the same moment of the demo, back on the field: the cartridge's saves/attract_cena_inicial.state --peek to
f5228 (real time, the divergence tool's numbering) and the port's interactive attract (--scene 433, CT_AUDIO_LOCKSTEP=1)
dumped at f7238 (CT_DUMPWRAM_AT, the F5's frame). Compared: the live page `$7E:2200`,
  * the BG rows 0-7, colours 1-15 (index 0 of a 4bpp row is transparent and not drawn);
  * colours 24-31 on their own (the glow, BG3 palettes 6-7);
  * the sprite colours the battle's staging leaves: OBJ rows 0 and 4 colours 0-3 (ShowDamage's C1/5B03-5B1D copy of the
    boot's `$999F`), rows 2 and 3 colours 13-15 (CC/EF16-EF32).
"""
import os
import subprocess
import sys


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    os.chdir(root)
    os.makedirs('build', exist_ok=True)
    pw, cw = os.path.join('build', '_07dpal_port.wram'), os.path.join('build', '_07dpal_cart.raw')
    for p in (pw, cw, cw + '.wram'):
        if os.path.exists(p):
            os.remove(p)
    pf = int(sys.argv[1]) if len(sys.argv) > 1 else 7238
    env = dict(os.environ, CT_AUDIO_LOCKSTEP='1', CT_DUMPWRAM=pw, CT_DUMPWRAM_AT=str(pf),
               CT_SHOTS=os.path.join('build', '_07dpal_shot'), CT_SHOTS_AT=str(pf + 1), CT_SHOTS_EXIT='1',
               SDL_AUDIODRIVER='dummy')
    subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '433'],
                   env=env, capture_output=True)
    oenv = dict(os.environ)
    oenv.pop('CT_ORACLE_LOGICAL', None)
    subprocess.run([os.path.abspath('build/ct.exe'), '--peek', 'saves/attract_cena_inicial.state', cw, '--frames', '5228'],
                   env=oenv, capture_output=True)
    if not os.path.exists(pw) or not os.path.exists(cw + '.wram'):
        sys.exit('07d-palette: a side wrote nothing (port %s, cartridge %s)' % (os.path.exists(pw), os.path.exists(cw + '.wram')))
    p, c = open(pw, 'rb').read(), open(cw + '.wram', 'rb').read()

    def col(b, i):
        return b[0x2200 + 2 * i] | (b[0x2201 + 2 * i] << 8)

    def grade(name, idx):
        bad = [i for i in idx if col(p, i) != col(c, i)]
        if not bad:
            print('07d-palette: %s %d/%d identical' % (name, len(idx), len(idx)))
        else:
            print('07d-palette: %s %d/%d identical; first colour %d port %04X cartridge %04X' % (
                name, len(idx) - len(bad), len(idx), bad[0], col(p, bad[0]), col(c, bad[0])))
    grade('BG rows 0-7 (colours 1-15)', [r * 16 + k for r in range(8) for k in range(1, 16)])
    grade('the glow, colours 24-31', list(range(24, 32)))
    grade('OBJ rows 0/4 colours 0-3 (ShowDamage)', list(range(128, 132)) + list(range(192, 196)))
    grade('OBJ rows 2/3 colours 13-15 (CC/EF16)', list(range(173, 176)) + list(range(189, 192)))


if __name__ == '__main__':
    main()
