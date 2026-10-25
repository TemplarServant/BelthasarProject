#!/usr/bin/env python3
r"""The overworld's map tables across the X menu, port against the rule's cartridge.

    python tools/ow_menu_scratch_cmp.py

The menu uses $7E:3000.. as its own scratch (C2:9641 / C2:971B / C2:97B9 .. while it builds its screens); C2/2E72's six
stash blocks do not cover it, and C2/2C93's rebuild decompresses it again from the world's packets (C3:07BB). The port
left the menu's bytes in the block definitions, and every map column streamed in after the menu was built from them --
roofs in the forest, the castle's stairs gone; in widescreen (a mod) the sides at once (the user, 2026-10-13: "after
opening and closing the menu on the overworld, some places were damaged").

Cartridge: saves/ow_w0.state under the rule, `--peek` before the menu (f70) and after the return (f300; X at f80, B at
f202, B at f264). Port: saves/field_re/ow_w0.lakewram, X at f80, B at f162, CT_OWDUMP (the world's own WRAM) at f70 and
f330. Per side, the bytes that differ between the two images in each table:

    ow-menu-scratch: <table> cartridge <n> byte(s) changed, port <m>
"""
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OUT = os.path.join(ROOT, 'build', '_ow_menu_scratch')
TABLES = (('$7E:3000-37FF (BG1 block definitions)', 0x3000, 0x3800), ('$7E:3800-3FFF (BG2 block definitions)', 0x3800, 0x4000),
          ('$7E:4000-6FFF (the block grids)', 0x4000, 0x7000), ('$7E:7000-71FF', 0x7000, 0x7200))
CART = [('0', 80), ('200', 2), ('0', 120), ('1', 2), ('0', 60), ('1', 2), ('0', 200)]


def cart(n):
    out, t = [], 0
    for b, k in CART:
        k = min(k, n - t)
        if k <= 0:
            break
        out.append('%s:%d' % (b, k)); t += k
    raw = os.path.join(OUT, 'c%d.raw' % n)
    subprocess.run([CT, '--peek', 'saves/ow_w0.state', raw, '--btnscript', ','.join(out)], cwd=ROOT,
                   env=dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1'),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return open(raw + '.wram', 'rb').read()


def port(n):
    dump = os.path.join(OUT, 'p%d.bin' % n)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_OWDUMP=dump,
               CT_OWDUMP_AT=str(n), CT_SHOTS=os.path.join(OUT, 'p'), CT_SHOTS_AT=str(n + 2), CT_SHOTS_EXIT='1')
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--loadwram', 'saves/field_re/ow_w0.lakewram', '--walkscript',
                    'n:80,m:2,n:80,b:2,n:170'], cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return open(dump, 'rb').read()


def main():
    os.makedirs(OUT, exist_ok=True)
    c0, c1, p0, p1 = cart(70), cart(300), port(70), port(330)
    for name, lo, hi in TABLES:
        cn = sum(1 for i in range(lo, hi) if c0[i] != c1[i])
        pn = sum(1 for i in range(lo, hi) if p0[i] != p1[i])
        print('ow-menu-scratch: %s cartridge %d byte(s) changed, port %d' % (name, cn, pn))


if __name__ == '__main__':
    main()
