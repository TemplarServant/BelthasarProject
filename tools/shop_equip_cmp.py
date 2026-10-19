#!/usr/bin/env python3
r"""The SHOP's equip sub-mode (substates 6-8, disasm/notes/shop_screen.md section 8) against the cartridge, EVERY frame.

    python tools/shop_equip_cmp.py [--keep] [--peek F ...]

The fair merchant (saves/field_re/shop90_lake.state for the oracle, saves/field_re/shop90.lakewram for the port) with a
Power Ring and a Third Eye poked into bag slots 1/2, one drive on both sides: Down (the party row), Right, Left, A (the
gear rows, Crono), R and L (C2/E108: Marle, back to Crono), Up (the accessory row, wrapping), A (the candidate list),
Down, Up (the live preview C2/E162: Power up / Speed down, the Third Eye's doubled Evade), A (the commit C2/D792), Down,
B, B (back to Buy/Sell), A (the buy list: the party panel after the mode), B, B (the shop closes).

Both sides write a 128x112 thumbnail of every frame (the oracle's PEEK_THUMBS, the port's CT_SHOTS_THUMBS -- the same
records, see docs/DEBUG_SURFACE.md), so the whole drive is two runs. The cartridge side is the LOGICAL oracle (the
real-time one has a lag frame inside C2/DACA; CLAUDE.md grades a lag frame against the logical mode). A frame's state
reaches the screen through its NMI, so the cartridge DISPLAYS frame P's state during P+1, which is thumbnail P+2: the
port's frame P is the oracle's thumbnail P + SHIFT (tools/menu_audit_cmp.py measured it on every kind of change). Prints

    shop-equip-cmp: f<A>-f<B>: <N> of <M> frame(s) differ [first ...]

and one line per run of differing frames with the thumbnail's bounding box (screen pixels). `--peek F` also compares the
full-resolution frame F (oracle --peek F+SHIFT against the port's CT_SHOTS F) and writes PNGs under build/_shopeq/.
"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import fb  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OSTATE = 'saves/field_re/shop90_lake.state'
PWRAM = os.path.join(ROOT, 'saves', 'field_re', 'shop90.lakewram')
OUT = os.path.join(ROOT, 'build', '_shopeq')
POKE = {0x2401: 0x9D, 0x2501: 0x01, 0x2402: 0xA8, 0x2502: 0x01}   # a Power Ring and a Third Eye
# (oracle LakeSnes mask, oracle press frame): A=100 B=1 Up=10 Down=20 Left=40 Right=80 L=400 R=800
DRIVE = [(0x100, 10), (0x20, 60), (0x80, 90), (0x40, 120), (0x100, 150), (0x800, 190), (0x400, 220), (0x10, 250),
         (0x100, 280), (0x20, 310), (0x10, 340), (0x100, 370), (0x20, 410), (0x1, 440), (0x1, 470), (0x100, 500),
         (0x1, 540), (0x1, 580)]
END = 650                                                           # the run's length: past the shop's close (B at 580)
KEY = {0x100: 'a', 0x1: 'b', 0x10: 'u', 0x20: 'd', 0x40: 'l', 0x80: 'r'}
HOLD = {0x400: 0x20, 0x800: 0x10}                                   # L / R in CT_HOLDBTN's $00F2 layout
HOLD_LEAD = 0                                                       # the pad latch runs one frame behind
PRESS = 1                                                           # port press frame = oracle press frame + PRESS
SHIFT = 2
F0, F1 = 50, 575
MODE_END = 501                                                      # the last frame before the buy list reopens
REC = 6 + 128 * 112 * 3


def oracle_script(total):
    parts, t = [], 0
    for m, f in DRIVE:
        if f >= total:
            break
        if f > t:
            parts.append('0:%d' % (f - t))
        n = min(2, total - f)
        parts.append('%X:%d' % (m, n))
        t = f + n
    if total > t:
        parts.append('0:%d' % (total - t))
    return ','.join(parts)


def oracle_env():
    return dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_POKE=','.join('%04X=%02X' % kv for kv in POKE.items()))


def run_oracle(thumbs):
    env = dict(oracle_env(), PEEK_THUMBS=thumbs, PEEK_THUMBS_EVERY='1')
    subprocess.run([CT, '--peek', OSTATE, os.path.join(OUT, '_o_end.raw'), '--btnscript', oracle_script(END)],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def port_seed():
    b = bytearray(open(PWRAM, 'rb').read())
    o = b.index(b'RAM:131072:') + 11
    for a, v in POKE.items():
        b[o + a] = v
    seed = os.path.join(OUT, '_seed.lakewram')
    open(seed, 'wb').write(bytes(b))
    return seed


def run_port(thumbs, shots=()):
    walk, t, hold = [], 0, []
    for m, f in DRIVE:
        if m in HOLD:
            hold.append('%X@%d:1' % (HOLD[m], f + PRESS - HOLD_LEAD))
            continue
        pf = f + PRESS                               # the talk A too (every key the cartridge's + 1; until 2026-10-19 the talk kept its frame)
        if pf > t:
            walk.append('n:%d' % (pf - t))
        walk.append('%s:1' % KEY[m])
        t = pf + 1
    walk.append('n:%d' % (END - t))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SHOTS_THUMBS=thumbs, CT_SHOTS_THUMBS_EVERY='1', CT_HOLDBTN=','.join(hold),
               CT_DUMPWRAM=os.path.join(OUT, '_p_end.wram'), CT_DUMPWRAM_AT=str(END - 4))
    if shots:
        env.update(CT_SHOTS=os.path.join(OUT, '_p'), CT_SHOTS_AT=','.join(str(f) for f in shots))
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '5', '--loadwram', port_seed(), '--selftest',
                    os.path.join(OUT, '_p_end.raw'), '--frames', str(END), '--walkscript', ','.join(walk)],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def load_thumbs(path):
    data = open(path, 'rb').read()
    out = {}
    for i in range(len(data) // REC):
        fr, = struct.unpack_from('<I', data, i * REC)
        out[fr] = data[i * REC + 6:(i + 1) * REC]
    return out


def bbox(a, b):
    xs, ys = [], []
    for i in range(0, len(a), 3):
        if a[i:i + 3] != b[i:i + 3]:
            p = i // 3
            xs.append(p % 128)
            ys.append(p // 128)
    return (min(xs) * 2, min(ys) * 2, max(xs) * 2 + 1, max(ys) * 2 + 1) if xs else None


def main():
    os.makedirs(OUT, exist_ok=True)
    global PRESS, HOLD_LEAD
    if os.environ.get('SHOPEQ_PRESS'): PRESS = int(os.environ['SHOPEQ_PRESS'])
    if os.environ.get('SHOPEQ_HOLD'): HOLD_LEAD = int(os.environ['SHOPEQ_HOLD'])
    peeks = [int(a) for a in sys.argv[sys.argv.index('--peek') + 1:]] if '--peek' in sys.argv else []
    ot, pt = os.path.join(OUT, 'o.thumbs'), os.path.join(OUT, 'p.thumbs')
    run_oracle(ot)
    run_port(pt, peeks)
    o, p = load_thumbs(ot), load_thumbs(pt)
    if '--scan' in sys.argv:
        for sh in range(-1, 5):
            same = sum(1 for f in range(F0, F1 + 1) if f in p and f + sh in o and p[f] == o[f + sh])
            print('shop-equip-cmp: shift %d -> %d identical' % (sh, same))
    bad = []
    for f in range(F0, F1 + 1):
        if f not in p or f + SHIFT not in o:
            bad.append((f, 'missing'))
        elif p[f] != o[f + SHIFT]:
            bad.append((f, bbox(o[f + SHIFT], p[f])))
    mode = [f for f, _ in bad if f <= MODE_END]
    print('shop-equip-cmp: equip mode f%d-f%d: %d of %d frame(s) differ%s' % (F0, MODE_END, len(mode),
          MODE_END - F0 + 1, (' first ' + str(mode[:8])) if mode else ''))
    print('shop-equip-cmp: buy list after it f%d-f%d: frames that differ %s' % (MODE_END + 1, F1,
          [f for f, _ in bad if f > MODE_END]))
    # the RAM the mode wrote: the bag and Crono's sheet, back on the field after the shop's close (the menu's WRAM
    # reaches the port's field image only when the menu hands it back)
    ow = open(os.path.join(OUT, '_o_end.raw.wram'), 'rb').read()
    pw = open(os.path.join(OUT, '_p_end.wram'), 'rb').read() if os.path.exists(os.path.join(OUT, '_p_end.wram')) else b''
    for name, lo, hi in (('bag $2400-$25FF', 0x2400, 0x2600), ('Crono sheet $2600-$264F', 0x2600, 0x2650)):
        n = sum(1 for a in range(lo, hi) if not pw or ow[a] != pw[a])
        print('shop-equip-cmp: %s: %d byte(s) differ' % (name, n))
    run = None
    for f, bb in bad + [(None, None)]:
        if run and f == run[1] + 1 and bb == run[2]:
            run[1] = f
            continue
        if run:
            print('  f%d-f%d box %s' % (run[0], run[1], run[2]))
        run = [f, f, bb] if f is not None else None
    for f in peeks:
        of = os.path.join(OUT, '_o_%d.raw' % f)
        subprocess.run([CT, '--peek', OSTATE, of, '--btnscript', oracle_script(f + SHIFT)], cwd=ROOT,
                       env=oracle_env(), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        pf = os.path.join(OUT, '_p_f%d.raw' % f)
        ow, oh, opx = fb.load(of)
        pw, ph, ppx = fb.load(pf)
        n = sum(1 for y in range(224) for x in range(256)
                if opx[((y + 8) * ow + x) * 3:((y + 8) * ow + x) * 3 + 3] != ppx[(y * pw + x) * 3:(y * pw + x) * 3 + 3])
        fb.fb_to_png(of, os.path.join(OUT, 'o_%d.png' % f))
        fb.fb_to_png(pf, os.path.join(OUT, 'p_%d.png' % f))
        print('shop-equip-cmp: peek f%d -> %d px differ' % (f, n))


if __name__ == '__main__':
    main()
