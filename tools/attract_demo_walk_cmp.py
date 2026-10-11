#!/usr/bin/env python3
r"""The attract's demo battles: how often the idle tick's walk dispatch (C1/10A9 -> C1/2D9F) runs, port against cartridge.

    python tools/attract_demo_walk_cmp.py

The user, 2026-09-27 (an F5 in the 07D Frog Squash battle): "one bat has a different animation from the other, that does not
happen on the cartridge". A demo battle is ONE pass of the main loop: its first iteration's idle tick visits the walk once,
then the ATB pass takes the demo's turn (C1/8461 -> C1/B725 -> C1/B967) and only returns once the action has run -- into
the loop tail C1/83AE, whose `LDA $2989 / BIT #20` ends the fight instead of `JMP C1/8120`. So no walk runs after the
demo's action. The port ran one more frame A between the action's end and its demo end, and at the 07D that dispatch
committed the right Vamp's idle window and published pose 00 over the tech's closing 01 for the ~36 frames of the
teardown (src/battle.c battleIterFrameA, h->demoPass).

Port: the interactive attract (--scene 433, CT_AUDIO_LOCKSTEP=1) with CT_BATTLE_WALKTRACE, one `idlewalk` line per
dispatch; cartridge: `ct.exe --hookpc C12DA4` (after `LDA $A4`: A = the group) from saves/attract_cena_inicial.state.
The dispatches are counted per demo battle, in order.
"""
import os
import re
import subprocess
import sys

FIELD = os.path.abspath('build/ct_field.exe')
CT = os.path.abspath('build/ct.exe')
STATE = 'saves/attract_cena_inicial.state'


def main():
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_BATTLE_WALKTRACE='1', CT_BATTLE_TRACE='1', CT_SHOTS=os.path.join('build', '_demowalk'),
               CT_SHOTS_AT='9960', CT_SHOTS_EXIT='1')
    pr = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433'], env=env, capture_output=True, text=True,
                        errors='replace')
    port, cur = [], None
    for l in (pr.stdout + pr.stderr).splitlines():
        m = re.match(r'battlephase: begins at field f(\d+)', l)
        if m:
            cur = [int(m.group(1)), []]; port.append(cur); continue
        m = re.match(r'idlewalk f(\d+) grp=(\d+)', l)
        if m and cur is not None:
            cur[1].append((int(m.group(1)), int(m.group(2))))
    cr = subprocess.run([CT, '--hookpc', 'C12DA4', '--from', STATE, '--frames', '7700'], capture_output=True, text=True,
                        errors='replace')
    visits = []
    for l in (cr.stdout + cr.stderr).splitlines():
        m = re.match(r'\s+f(\d+)\s+visits=(\d+)\s.*A/X/Y: (\S+)', l)
        if m and int(m.group(2)) > 0:
            visits.append((int(m.group(1)), int(m.group(3).split('/')[0], 16) & 0xFF))
    cart = []                                             # one battle = a run of dispatches with no gap above 600 frames
    for f, a in visits:
        if not cart or f - cart[-1][-1][0] > 600:
            cart.append([])
        cart[-1].append((f, a))
    if not port or not cart:
        sys.exit('demo-walk: REFUSING -- a side printed nothing (port %d battles, cartridge %d)' % (len(port), len(cart)))
    same = 0
    for k in range(max(len(port), len(cart))):
        p = port[k][1] if k < len(port) else []
        c = cart[k] if k < len(cart) else []
        ok = [g for _, g in p] == [g for _, g in c]
        same += ok
        print('demo-walk: battle %d port %d dispatch(es) %s | cartridge %d dispatch(es) %s%s' % (
            k + 1, len(p), ' '.join('grp%d' % g for _, g in p), len(c), ' '.join('grp%d' % g for _, g in c),
            '' if ok else '  <-- differs'))
    print('demo-walk: %d / %d demo battles walk as the cartridge does' % (same, max(len(port), len(cart))))


if __name__ == '__main__':
    main()
