#!/usr/bin/env python3
r"""The main loop's head across a song's upload, the port against the cartridge from one poked state.

    python tools/song_park_cmp.py [-v]

A script's `EA nn` calls `JSL $C7:0004` from inside the runner (C0/00C4 JSR C0/5A46). While the upload's handshakes wait
nothing of the field's loop runs, and on the frame the call returns the SAME iteration goes on from the visit; the
head -- C0/0097's pad latches, C0/0C76 (the `$0117` drain and the two fades), C0/881E (the camera and the layers'
drift), C0/1F87, C0/21E1, C0/274D -- ran before the call and does not run again (disasm/notes/song_load_freeze.md
sec. 9).

The state is the cartridge's on frame 1560 of playthrough leg 11 (the Telepod's room, loc 008; made from the route by
tools/leg_window_cmp.py, keyed on the route and the oracle), nine frames before the script's `EA 1F`, with C0/1F24's
brightness fade armed by a poke -- `$0118` bit 1, `$011B` = 05 (the target), `$011C` = 01 (a step every other frame) --
and X pressed for two frames inside the park. Both sides run 60 frames from it; the cells are the fade (`$0118`,
`$0119`, `$011D`), BG3's drift remainders (`$1D3F`, `$1D40`), the builder's parity `$0153`, and the pad's accumulator
`$0151` with the new-press byte `$00F0`.

    songpark: fade armed and X pressed across `EA 1F`: <n> frames, <d> differing; the fade holds <h> frames at <v>

Regression check: song-park-holds-the-loops-head-vs-cartridge (docs/TESTING.md).
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import playthrough as P  # noqa: E402
import leg_window_cmp as L  # noqa: E402

LEG, AT, FRAMES = '11_', 1560, 60
CELLS = [('7E0118', 1), ('7E0119', 1), ('7E011D', 1), ('7E1D3F', 2), ('7E0153', 1), ('7E0151', 1), ('7E00F0', 1)]
PAD = [(0, 20), (0x200, 2), (0, FRAMES - 22)]                 # X on frames 20-21: inside the park


def main():
    verbose = '-v' in sys.argv[1:]
    legs = P.route()
    i = next(k for k, lg in enumerate(legs) if lg['name'].startswith(LEG))
    work = os.path.join(L.SCRATCH, 'songpark')
    os.makedirs(work, exist_ok=True)
    base = L.cart_state(legs, i, AT)
    raw = os.path.join(work, 'seed.raw')
    subprocess.run([P.CT, '--peek', base, raw, '--btnscript', '0:0'], cwd=ROOT, env=P.RULE, capture_output=True)
    b118 = open(raw + '.wram', 'rb').read()[0x0118]
    poked = os.path.join(work, 'poked.state')
    if os.path.exists(poked):
        os.remove(poked)
    env = dict(P.RULE, PEEK_POKE='7E0118=%02X,7E011B=05,7E011C=01,7E011D=00' % (b118 | 0x02), PEEK_SAVE=poked)
    subprocess.run([P.CT, '--peek', base, os.path.join(work, 'p.raw'), '--btnscript', '0:0'], cwd=ROOT, env=env, capture_output=True)
    if not os.path.exists(poked):
        print('songpark: the cartridge wrote no poked state')
        return
    mon = [('c%d' % k, a, L.port_key(a), n) for k, (a, n) in enumerate(CELLS)]
    cart = dict(P.oracle_timeline(poked, PAD, mon))
    port = dict(L.port_timeline(poked, [(0, 1)], PAD, CELLS, work))
    if not cart or not port:
        print('songpark: no timeline (cartridge %d event(s), port %d)' % (len(cart), len(port)))
        return
    cc = pp = None
    bad, n, hold, run, held = 0, 0, 0, 0, None
    for f in range(FRAMES):
        if f in cart:
            cc = tuple(cart[f]['c%d' % k] for k in range(len(CELLS)))
        if f in port:
            pp = tuple(port[f])
        if cc is None or pp is None:
            continue
        n += 1
        if cc != pp:
            bad += 1
        if verbose:
            print('  f%d  cartridge %s | port %s%s' % (AT + f, ' '.join(cc), ' '.join(pp), '' if cc == pp else '  <--'))
        if held is not None and cc[1] == held[1] and cc[0] == held[0]:
            run += 1
        else:
            run = 1
        held = cc
        if run > hold and cc[0] != '00':
            hold, at = run, cc[1]
    print('songpark: fade armed and X pressed across `EA 1F`: %d frames, %d differing; the fade holds %d frames at %s'
          % (n, bad, hold, at if hold else '--'))


if __name__ == '__main__':
    main()
