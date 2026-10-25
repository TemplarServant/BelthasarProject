#!/usr/bin/env python3
"""f8_cursor_cmp.py -- the field's $C0:FE00 cursor $7E01F8 from power-on, the port against the rule's cartridge.

$01F8 is the index op 0x7F (C0/6D09), the mover's bounce (C0/AE29) and the blink (C0/A22F) step through the 256-byte
table at $C0:FE00. Its writers on the attract's first legs, MEASURED (PEEK_WRITEWATCH=01F8:1 from power-on): C0/0C70
`LDA $0400 / STA $F8` -- the field init C0/0B86 on every location load, seeding the cursor with the play clock's
frames byte -- and op 0x7F's `STA $F8` (C0/6D0E); nothing else before loc 0E6. So the cursor a location's scripts
draw from is the clock $0400 at its load, and the clock ticks once per NMI that reaches the pad latch C2/84D2
(field C0/EC15, world C2/031B, the fight's CD/088D once $7E = 1/2) -- one wrong tick anywhere before a load and
every later draw comes from another byte of the table (the 0E6 robots' enemy ids, `7F 06` in their fn0).

  cartridge  CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1 ct.exe --peek boot ... PEEK_WRITEWATCH=01F8:1
             (writer C0/0C75 = the seed) and --monitor 7E0100:2 (the location each seed belongs to)
  port       Belthasar.exe --boot on an erased cart (CT_SRAM_FRESH=1, the cartridge's power-on with no battery),
             CT_F8TRACE=1: one `f8seed: loc <L> C0/0C70 $01F8 = <v>` per location load

Both sides take the power-on route -- scene 0's boot script, then the attract (loc 1B1). `--scene 433` starts the
attract cold at 1B1 and skips scene 0's 17 ticks and 1B1's load and ramp: its clock is a constant 22 behind.

Prints one line per seed that differs, the cartridge's op-0x7F steps for reference, and

  f8-cursor: <n>/<m> field-init seeds of $01F8 equal (power-on -> loc <L>)

usage: python tools/f8_cursor_cmp.py [--until 0E6] [--show]
"""
import argparse, os, re, subprocess, sys, concurrent.futures as cf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', '_f8cmp')
RULE = {'CT_ORACLE_LOGICAL': '1', 'CT_ORACLE_VIRTUAL_BEAM': '1'}


def run(argv, env_add):
    env = dict(os.environ)
    env.update(env_add)
    p = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    return p.stdout + p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--until', default='0E6', help='the location whose seed ends the comparison (hex)')
    ap.add_argument('--cart-frames', type=int, default=7530)   # the 0E6 seed is c7485, its robots' op 0x7F c7515-7516
    ap.add_argument('--port-frames', type=int, default=7541)   # the port's 0E6 load is f7495 (from power-on)
    ap.add_argument('--show', action='store_true', help='print every seed, not only the ones that differ')
    a = ap.parse_args()
    until = int(a.until, 16)
    os.makedirs(OUT, exist_ok=True)
    ct = os.path.join('build', 'ct.exe')
    ctf = os.path.join('build', 'Belthasar.exe')
    srm = os.path.join(OUT, 'fresh.srm')
    with cf.ThreadPoolExecutor(3) as ex:
        fw = ex.submit(run, [ct, '--peek', 'boot', os.path.join(OUT, 'peek'), '--frames', str(a.cart_frames)],
                       dict(RULE, PEEK_WRITEWATCH='01F8:1'))
        fl = ex.submit(run, [ct, '--monitor', '7E0100:2', '--from', 'boot', '--frames', str(a.cart_frames), '--diff'],
                       RULE)
        fp = ex.submit(run, [ctf, '--assets', 'ct_assets.dat', '--boot'],
                       {'CT_F8TRACE': '1', 'CT_SRAM': srm, 'CT_SRAM_FRESH': '1', 'SDL_VIDEODRIVER': 'dummy',
                        'SDL_AUDIODRIVER': 'dummy', 'CT_NOPACE': '1', 'CT_AUDIO_LOCKSTEP': '1',
                        'CT_SHOTS': os.path.join(OUT, 'p'), 'CT_SHOTS_AT': str(a.port_frames), 'CT_SHOTS_EXIT': '1'})
        wtxt, ltxt, ptxt = fw.result(), fl.result(), fp.result()
    # the cartridge's location timeline (monitor frame -> $0100); a write-watch frame is the monitor's + 1
    locs = []
    for l in ltxt.splitlines():
        m = re.match(r'\s*f(\d+)\s+7E0100=([0-9A-F]{2})([0-9A-F]{2})', l)
        if m:
            locs.append((int(m.group(1)), int(m.group(3) + m.group(2), 16)))
    def loc_at(f):
        v = 0
        for lf, lv in locs:
            if lf > f:
                break
            v = lv
        return v
    cart, steps = [], {}
    for l in wtxt.splitlines():
        m = re.match(r'\s*writewatch f(\d+) \$01F8 = ([0-9A-F]{2}) by (C0/[0-9A-F]{4})', l)
        if not m:
            continue
        f, v, pc = int(m.group(1)), int(m.group(2), 16), m.group(3)
        if pc == 'C0/0C75':
            cart.append((loc_at(f - 1), v, f))
        elif pc == 'C0/6D10' and cart:
            steps.setdefault(len(cart) - 1, []).append(v)
    # Under the rule --boot starts at the power-on: the reset's own load is the one `reboot: JML C0:0000` pends, after the
    # port's start-up has built scene 0 once (its seed is not a C0/0B86 the cartridge runs) -- grade from the reboot on.
    po = re.search(r'^power-on:.*\n(?:.*\n)*?reboot: JML C0:0000.*$', ptxt, re.M)
    port = [(int(m.group(1), 16), int(m.group(2), 16)) for m in
            re.finditer(r'f8seed: loc ([0-9A-F]{3}) C0/0C70 \$01F8 = ([0-9A-F]{2})', ptxt[po.end():] if po else ptxt)]
    if not cart or not locs:
        print('f8-cursor: FAIL -- no cartridge seeds (%d) or no location timeline (%d)' % (len(cart), len(locs)))
        return 1
    if not port:
        print('f8-cursor: FAIL -- the port printed no f8seed line (CT_F8TRACE)')
        return 1
    end = next((i for i, c in enumerate(cart) if c[0] == until), None)
    if end is None:
        print('f8-cursor: FAIL -- the cartridge reached no load of loc %03X in %d frames' % (until, a.cart_frames))
        return 1
    m = end + 1
    eq = 0
    for i in range(m):
        cl, cv, cf_ = cart[i]
        pl, pv = port[i] if i < len(port) else (-1, -1)
        same = (cl, cv) == (pl, pv)
        eq += same
        if a.show or not same:
            print('  seed %2d cartridge c%-5d loc %03X $01F8 = %02X | port loc %s $01F8 = %s%s' % (
                i, cf_, cl, cv, ('%03X' % pl) if pl >= 0 else '---', ('%02X' % pv) if pv >= 0 else '--',
                '' if same else '   <-- differs'))
        if i in steps:
            print('  seed %2d cartridge op 0x7F (C0/6D0E) steps it: %s' % (i, ' '.join('%02X' % s for s in steps[i])))
    print('f8-cursor: %d/%d field-init seeds of $01F8 equal (power-on -> loc %03X)' % (eq, m, until))
    return 0 if eq == m else 1


if __name__ == '__main__':
    sys.exit(main())
