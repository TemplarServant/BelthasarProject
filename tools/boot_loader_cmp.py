#!/usr/bin/env python3
r"""The boot Load screen (screen $0E) with a given battery, the port against the cartridge, pixel for pixel.

    python tools/boot_loader_cmp.py <battery.srm>

  * the cartridge: `ct.exe --fb` boots from power-on with CT_ORACLE_SRAM = the battery for 120 frames and saves the state
    (PEEK_SAVE); `--peek` from it presses Start at +13, +115 and +217 (the title, the menu) and dumps +257: the Load
    screen, settled. $0D79..$0D7B (C2/D266: each valid save's window colour) is read from that frame's WRAM.
  * the port: `Belthasar.exe --boot` with CT_SRAM = the same battery, Start at 133 (the boot checks' own drive), the frame
    at 260 (--selftest): the list is the shell's screen $0E, lit (C2/83B2's fade-in) by its f243.
The two are compared over the whole 256 x 224 with the oracle's 240-line frame 7 lines down -- the Load screen's own
convention (disasm/notes/boot_loader.md: its BG V scroll puts the port's line y at the frame's y + 7).
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import fb  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    srm = sys.argv[1] if len(sys.argv) > 1 else 'saves/field_re/colsave3.srm'
    tag = os.path.splitext(os.path.basename(srm))[0]
    os.chdir(ROOT)
    os.makedirs('build/_bootldr', exist_ok=True)
    # a run that writes nothing must not be graded on a previous run's frames (a stale _p.raw from an older build would
    # read as a pass)
    for f in os.listdir('build/_bootldr'):
        if f.startswith(tag + '_'):
            os.remove(os.path.join('build/_bootldr', f))
    st = 'build/_bootldr/%s_f120.state' % tag
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_SRAM=srm)
    subprocess.run([os.path.abspath('build/ct.exe'), '--fb', 'build/_bootldr/%s_f120.raw' % tag, '--frames', '120'],
                   env=dict(env, PEEK_SAVE=st), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    oraw = 'build/_bootldr/%s_o.raw' % tag
    subprocess.run([os.path.abspath('build/ct.exe'), '--peek', st, oraw, '--btnscript',
                    '0:13,8:2,0:100,8:2,0:100,8:2,0:40'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=600)
    praw = 'build/_bootldr/%s_p.raw' % tag
    penv = dict(os.environ, CT_SRAM=srm, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--boot', '--selftest', praw,
                    '--frames', '325', '--walkscript', 'n:198,s:1,n:126'], env=penv, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, timeout=600)
    w = open(oraw + '.wram', 'rb').read()
    ow, oh, o = fb.load(oraw)
    pw, ph, p = fb.load(praw)
    same = 0
    for y in range(ph):
        for x in range(pw):
            if p[(y * pw + x) * 3:(y * pw + x) * 3 + 3] == o[((y + 7) * ow + x) * 3:((y + 7) * ow + x) * 3 + 3]:
                same += 1
    print('boot-loader: %s screen $%02X $0D79=%s -- port vs cartridge %d / %d identical'
          % (tag, w[0x67], w[0x0D79:0x0D7C].hex(' '), same, pw * ph))


if __name__ == '__main__':
    main()
