#!/usr/bin/env python3
r"""The first frames of an enemy's death dissolve, port against the rule's cartridge (the forest ambush: two Beetles and
an Avian Chaos, A held through the fight on both sides).

    python tools/dissolve_flash_cmp.py

The user, 2026-10-07: "when the enemy is about to do the dissolve, it flashes and sometimes even teleports to another
spot". CF/FE20 moves the corpse to OBJ palette 4 (`$993C = 08`, CF/FE57) and copies its own colours 1-12 there
(CF/FE61-FE72) in one main-loop pass; the NMI of that frame uploads the page and the OAM attribute follows a frame later
(the compose, then the next NMI), so the corpse keeps its colours until CD/00A5's record turns them red. The port drew the
corpse with the LIVE `$993C` against the UPLOADED page: one frame of palette 4 as the hurt flash had left it (white).

Cartridge: saves/battle_re/forest_wait_ambush_active.state under the rule, `--peek` with PEEK_ISOBG=4 (the OBJ layer
alone: this state's BG is not the room's) at every frame from the delist ($AF02 := FF, --monitor) -3 to +8. Port: the
same WRAM (forest_wait_ambush_active.lakewram), CT_BATTLE_MASHA, CT_SHOTS at the same window around each `death`
(CT_BATTLE_TRACE's battle frame + the `battlephase: begins at field fN` frame). In each side's box of the dying enemy
the pixels the hurt flash paints (all three channels >= 0xC0 -- the enemy's own colours have none) are counted per frame:

    dissolve-flash: <side> death<k> white px: <n> <n> ... (frames -3..+8)
    dissolve-flash: <side> death<k> <no flash|FLASH>

CT_DISSOLVE_FLASH_PORT=<exe> runs another Belthasar build (the cartridge's frames are kept in build/_dissolve_flash/).
"""
import os
import re
import subprocess
import sys

sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]   # tools/dis.py
import numpy as np  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.environ.get('CT_DISSOLVE_FLASH_PORT') or os.path.join(ROOT, 'build', 'Belthasar.exe')   # another build
OUT = os.path.join(ROOT, 'build', '_dissolve_flash')
CSTATE = 'saves/battle_re/forest_wait_ambush_active.state'
PSEED = 'saves/battle_re/forest_wait_ambush_active.lakewram'
UNIT = [('100', 2), ('0', 2)]                       # the cartridge's A, held 2 frames every 4
WIN = range(-3, 9)
# The dying enemy's box per death (x0, y0, x1, y1 on the 256x224 picture): Beetle (slot 1), Beetle (slot 2), Avian Chaos
# (slot 5). The two sides' battle cameras differ by this fixture's BG (the cartridge's state was spliced), not the OBJ
# positions, except the Avian's, whose camera row the cartridge took 13 lines lower.
PBOX = [(140, 125, 180, 160), (75, 80, 110, 108), (100, 70, 132, 100)]
CBOX = [(140, 125, 180, 160), (75, 80, 110, 108), (115, 88, 150, 122)]


def drive(f):
    out, t = [], 0
    while t < f:
        for b, n in UNIT:
            k = min(n, f - t)
            if k <= 0:
                break
            out.append('%s:%d' % (b, k)); t += k
    return ','.join(out)


def white(rgb, box):
    x0, y0, x1, y1 = box
    a = rgb[y0:y1, x0:x1]
    return int((a.min(axis=2) >= 0xC0).sum())


def cart_deaths():
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    r = subprocess.run([CT, '--monitor', '7EAF02:8', '--from', CSTATE, '--btnscript', drive(1500), '--frames', '1500',
                        '--diff'], cwd=ROOT, env=env, capture_output=True, text=True)
    deaths, prev = [], None
    for line in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+7EAF02=([0-9A-F]{16})', line)
        if not m:
            continue
        cur = m.group(2)
        if prev is not None:
            n_prev = sum(prev[i:i + 2] != 'FF' for i in range(0, 16, 2))
            n_cur = sum(cur[i:i + 2] != 'FF' for i in range(0, 16, 2))
            if n_cur < n_prev:
                deaths.append(int(m.group(1)))
        prev = cur
    return deaths


def cart_frame(f):
    raw = os.path.join(OUT, 'c%d.fb' % f)
    if not os.path.exists(raw):
        env = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_ISOBG='4')
        subprocess.run([CT, '--peek', CSTATE, raw, '--btnscript', drive(f)], cwd=ROOT, env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    d = np.frombuffer(open(raw, 'rb').read(), np.uint8).reshape(480, 512, 4)[::2, ::2]
    return d[8:232, :, [3, 2, 1]]


def port_frames():
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_BATTLE_MASHA='1',
               CT_BATTLE_TRACE='1', CT_SHOTS=os.path.join(OUT, 'probe'), CT_SHOTS_AT='1500', CT_SHOTS_EXIT='1')
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '19', '--loadwram', PSEED], cwd=ROOT, env=env,
                       capture_output=True, text=True)
    txt = r.stdout + r.stderr
    base = int(re.search(r'battlephase: begins at field f(\d+)', txt).group(1))
    deaths = [base + int(m) for m in re.findall(r'battletrace f(\d+) death ', txt)]
    at = sorted({d + k for d in deaths for k in WIN})
    env.update(CT_SHOTS=os.path.join(OUT, 'p'), CT_SHOTS_AT=','.join(map(str, at)))
    env.pop('CT_BATTLE_TRACE')
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '19', '--loadwram', PSEED], cwd=ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def load(f):
        d = open(os.path.join(OUT, 'p_f%d.raw' % f), 'rb').read()
        w, h = np.frombuffer(d[:8], np.uint32)
        return np.frombuffer(d[8:8 + w * h * 3], np.uint8).reshape(h, w, 3)
    return deaths, load


def report(side, k, counts):
    print('dissolve-flash: %s death%d white px: %s' % (side, k, ' '.join(map(str, counts))))
    before = counts[0]
    print('dissolve-flash: %s death%d %s' % (side, k, 'FLASH' if any(c > before + 16 for c in counts[1:]) else 'no flash'))


def main():
    os.makedirs(OUT, exist_ok=True)
    pd, pload = port_frames()
    cd = cart_deaths()
    if len(pd) != 3 or len(cd) != 3:
        print('dissolve-flash: deaths port %d cartridge %d (want 3 and 3)' % (len(pd), len(cd)))
        return 1
    for k in range(3):
        report('cartridge', k + 1, [white(cart_frame(cd[k] + i), CBOX[k]) for i in WIN])
        report('port', k + 1, [white(pload(pd[k] + i), PBOX[k]) for i in WIN])
    return 0


if __name__ == '__main__':
    sys.exit(main())
