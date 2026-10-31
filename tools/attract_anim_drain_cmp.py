#!/usr/bin/env python3
r"""The anim drain's raster deadline in the attract (C0/CA76, the virtual beam that advances) against the cartridge.

    python tools/attract_anim_drain_cmp.py [port_shift]

port_shift (optional, default 0) moves the port's side of the leg pairs and its stop frame: the port's numbering moves
with the attract's clock, the cartridge's does not.

The drain stops at the ROM's own line $6B against the virtual beam (src/ct_vbeam.h). Without the deadline the queue
empties in one pass and the objects past the fourth arena slot are popped with CLC and lose their frame every step:
the bats at the entrance of Magus's castle (0A5) fly with other poses and Zombor's legs on the bridge (087) move less
(disasm/notes/anim_drain_cad9.md sec. 8).

One interactive attract run (--scene 433, CT_AUDIO_LOCKSTEP=1) is watched with CT_FIELD_MEMMON and shoots the legs'
frames; the cartridge under the rule (CT_ORACLE_LOGICAL / VIRTUAL_BEAM, from saves/attract_cena_inicial_rule.state:
the attract from power-on + 2159, made by the recipe tools/fixtures/attract_rule.json) is watched with
`ct.exe --monitor` and peeked at the pairs.

* 0A5, the bats obj15-22 from their take-off ($1680 = 02) to anim 3: per bat, the poses it flies through and its longest
  hold of one pose, both counted from the bat's first flap. The cartridge's real beam gives another schedule than the
  virtual one, so the frames do not pair; the property graded is that every bat flaps through the cartridge's wing
  poses and none holds a pose longer than the cartridge's longest hold (the failure: a bat that stays in the hanging
  pose 07 and never flaps).
* 087, Zombor's legs obj12 from the torso's first pose 02 to the battle: the sequence of committed leg poses on both
  sides, and the legs' box (64,60)-(152,130) at five pairs (port = cartridge + 2066 under the rule, before the shift).

Regression check: attract-anim-drain-bats-and-legs-vs-cartridge.
"""
import os
import re
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

FIELD = os.path.abspath('build/Belthasar.exe')
CT = os.path.abspath('build/ct.exe')
STATE = 'saves/attract_cena_inicial_rule.state'
RULE = {'CT_ORACLE_LOGICAL': '1', 'CT_ORACLE_VIRTUAL_BEAM': '1'}
BATS = range(15, 23)
LEG_PAIRS = [(7122, 9266), (7138, 9282), (7162, 9306), (7178, 9322), (7194, 9338)]   # (cartridge, port before the shift)
LEG_BOX = (64, 60, 152, 130)
PORT_END = 9351
CART_END = 7208


def parse(lines, rx):
    """{frame: {addr: bytes}} from monitor / memmon lines, carrying unchanged frames forward."""
    out, last = {}, None
    for l in lines:
        m = rx.match(l)
        if not m:
            continue
        f = int(m.group(1))
        cur = {}
        for a, v in re.findall(r'([0-9A-F]{4,6})=([0-9A-F]+)', l):
            cur[int(a, 16) & 0xFFFF] = bytes.fromhex(v)
        out[f] = cur
        last = f
    return out


def timeline(snap, f0, f1):
    """Frame-by-frame view f0..f1, each frame the latest snapshot at or before it."""
    keys = sorted(snap)
    res, j, cur = {}, 0, None
    for f in range(f0, f1 + 1):
        while j < len(keys) and keys[j] <= f:
            cur = snap[keys[j]]; j += 1
        res[f] = cur
    return res


def b(frame, addr, off):
    return frame[addr][off]


def bats(tl, lo, hi, tag):
    start = next(f for f in range(lo, hi) if tl[f] and b(tl[f], 0x0100, 0) == 0xA5 and b(tl[f], 0x169E, 0) == 0x02)
    end = next(f for f in range(start, hi) if any(b(tl[f], 0x169E, 2 * k) == 0x03 for k in range(8)))
    res = {}
    for k, ob in enumerate(BATS):
        seq = [b(tl[f], 0x0F1E, 2 * k + 1) for f in range(start, end)]
        first = next((i for i in range(1, len(seq)) if seq[i] != seq[i - 1]), 0)
        seq = seq[first:]                          # from the first flap: before it the bat still shows its hanging pose
        run = longest = 1; changes = 0
        for i in range(1, len(seq)):
            if seq[i] == seq[i - 1]:
                run += 1
            else:
                run = 1; changes += 1
            longest = max(longest, run)
        res[ob] = (sorted(set(seq)), changes, longest)
    return start, end, res


def legs(tl, lo, hi):
    start = next(f for f in range(lo, hi) if tl[f] and b(tl[f], 0x0100, 0) == 0x87 and b(tl[f], 0x0F16, 1) == 0x02)
    seq, prev = [], None
    for f in range(start, hi + 1):
        if not tl[f] or b(tl[f], 0x0100, 0) != 0x87:
            break
        v = b(tl[f], 0x0F16, 3)
        if v != prev and v != 0xFF:
            seq.append(v)
        prev = v
    return start, seq


def box(port_raw, cart_raw):
    d = open(port_raw, 'rb').read(); w = struct.unpack('<I', d[:4])[0]; port = d[8:]
    o = open(cart_raw, 'rb').read()
    x0, y0, x1, y1 = LEG_BOX
    same = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            i = ((y + 8) * 1024 + x * 2) * 4; j = (y * w + x) * 3
            same += (o[i + 3], o[i + 2], o[i + 1]) == (port[j], port[j + 1], port[j + 2])
    return same, (x1 - x0) * (y1 - y0)


def main():
    global LEG_PAIRS, PORT_END
    if len(sys.argv) > 1:
        sh = int(sys.argv[1])
        LEG_PAIRS = [(c, p + sh) for c, p in LEG_PAIRS]
        PORT_END += sh
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    tmp = os.path.join('build', '_animdrain')
    os.makedirs(tmp, exist_ok=True)
    pre = os.path.join(tmp, 'p')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_FIELD_MEMMON='0F16:4,0F1E:10,169E:10,0100:2', CT_SHOTS=pre,
               CT_SHOTS_AT=','.join(str(p) for _, p in LEG_PAIRS) + ',%d' % PORT_END, CT_SHOTS_EXIT='1')

    def port_run():
        return subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433'], env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True).stdout

    def cart_mon():
        return subprocess.run([CT, '--monitor', '7E0F16:4,7E0F1E:16,7E169E:16,7E0100:2', '--from', STATE,
                               '--frames', str(CART_END)], env=dict(os.environ, **RULE), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                              text=True).stdout

    def cart_peek(c):
        subprocess.run([CT, '--peek', STATE, os.path.join(tmp, 'o%d.raw' % c), '--frames', str(c)],
                       env=dict(os.environ, **RULE), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    for c, p in LEG_PAIRS:
        for f in (os.path.join(tmp, 'o%d.raw' % c), '%s_f%d.raw' % (pre, p)):
            if os.path.exists(f):
                os.remove(f)
    with ThreadPoolExecutor(8) as ex:
        fp = ex.submit(port_run); fc = ex.submit(cart_mon)
        list(ex.map(cart_peek, [c for c, _ in LEG_PAIRS]))
        pout, cout = fp.result(), fc.result()
    psnap = parse(pout.splitlines(), re.compile(r'fmemmon f(\d+) '))
    csnap = parse(cout.splitlines(), re.compile(r'\s+f(\d+)\s'))
    if not psnap or not csnap:
        sys.exit('anim-drain: REFUSING -- no memory timeline (port %d frames, cartridge %d)' % (len(psnap), len(csnap)))
    ptl = timeline(psnap, min(psnap), PORT_END)
    ctl = timeline(csnap, min(csnap), CART_END)

    # ---- 0A5, the bats
    cs, ce, cb = bats(ctl, 3000, 4300, 'cart')
    ps, pe, pb = bats(ptl, 5000, 6400, 'port')
    worst = max(v[2] for v in cb.values())
    ok = 0
    for ob in BATS:
        (cpo, cch, clo), (ppo, pch, plo) = cb[ob], pb[ob]
        good = cpo == ppo and plo <= worst
        ok += good
        print('anim-drain: 0A5 bat obj%d: cart c%d-c%d poses %s changes %d longest %d | port p%d-p%d poses %s changes %d '
              'longest %d%s' % (ob, cs, ce, '/'.join('%02X' % v for v in cpo), cch, clo, ps, pe,
                                '/'.join('%02X' % v for v in ppo), pch, plo, '' if good else '  <-- differs'))
    print('anim-drain: 0A5 bats: %d / 8 fly through the cartridge\'s poses, none holding one longer than its longest (%d)'
          % (ok, worst))

    # ---- 087, Zombor's legs
    c0, cseq = legs(ctl, 6900, CART_END)
    p0, pseq = legs(ptl, 9000, PORT_END)
    print('anim-drain: 087 legs from the torso\'s pose 02: cart c%d %s | port p%d %s' %
          (c0, ' '.join('%02X' % v for v in cseq), p0, ' '.join('%02X' % v for v in pseq)))
    for c, p in LEG_PAIRS:
        ps_, cs_ = '%s_f%d.raw' % (pre, p), os.path.join(tmp, 'o%d.raw' % c)
        if not (os.path.exists(ps_) and os.path.exists(cs_)):
            sys.exit('anim-drain: REFUSING -- a shot is missing (port f%d / cart c%d)' % (p, c))
        same, tot = box(ps_, cs_)
        print('anim-drain: 087 legs port f%d vs cart c%d box (%d,%d)-(%d,%d): %d / %d identical' %
              ((p, c) + LEG_BOX + (same, tot)))


if __name__ == '__main__':
    main()
