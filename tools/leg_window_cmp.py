#!/usr/bin/env python3
r"""A window of a playthrough leg, the port against the cartridge step by step.

    python tools/leg_window_cmp.py [name ...] [-v]

The playthrough's legs (tools/playthrough.py) grade a handful of bytes that tell whether the story went the same way. A
window grades a mechanism inside a leg: it starts both sides from the cartridge's own state at a frame of the leg (made
here from the leg's start state and its pad, cached under build/_legwin/ with the route's key), plays the leg's pad from
there, and compares the named cells:

    legwindow: <name> leg <leg> f<start>+<frames>: <n> steps compared, <d> differing
      first: step <k> cartridge f<c> <cells> x<len>, port f<p> <cells> x<len>
      free:  cartridge f<a>-f<b>: both come back on <cells>, the port <+/-d> frame(s)

A STEP is a run of frames on which the cells do not change (their values and the run's length). While an object moves,
every frame is a step, so the comparison is frame for frame: the port's k-th step against the cartridge's k-th, value
and length. A window that crosses a fight or a load names the cartridge's frames of it (`free`, up to the frame before
the cells move again): nothing is graded inside them, and past them the port's steps are taken from its first one that
holds the cartridge's first value there -- how long a fight's exit or a load takes is the timing class the playthrough's
ledger keeps apart (disasm/notes/playthrough_leg_ledger.md, and the `free:` line says by how much); what is graded is
what the objects do before it and after it. The port's keys: the cartridge's one frame later, as in the playthrough,
the window's first frame taking the leg's own frame before it (port_keys).

Regression checks: the `leg-window-*` family (docs/TESTING.md).
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import playthrough as P  # noqa: E402

WORK = os.path.join(ROOT, 'build', '_legwin')                 # the cartridge's states of the windows (cached)
SCRATCH = os.environ.get('LEGWIN_TMP') or WORK                # LEGWIN_TMP: a second run's own scratch
P.TMP = SCRATCH                                               # (oracle_play's end.raw: not the playthrough's own)
TAIL = 240                                                    # a window with a free span: the port's extra frames

# name: the leg (its prefix in tools/playthrough/route.txt), the window's first frame and its length in the leg's frames,
# the cells (cartridge address, bytes), and the cartridge's frames (of the leg) that are not graded (`free`).
WINDOWS = {
    # Ocean Palace, loc 199 (scene 409): the launcher (object 23) calls objects 11 and 12 (`02 16 14`, `02 18 14`, on a
    # draw of $01F8 every fourth frame), which SETCOORD to tile (04, 48) and fly at the party's tile with `97 11 12`
    # (C0/50B9). A fight starts while they fly: they cross the map's left edge on the ticket they had ($1816 0x002A ->
    # 0xFFFD at f35337) and stop at $1816 = FF2B / $1818 = FF67 (f35351). After the fight (f39422) the op issues again
    # from the tile BYTE the ROM reads ($1801,X = FF): toward tile 03 the heading is 03 - FF, a borrow, so both fly LEFT
    # across the whole 256-tile coordinate (about 1190 frames) and arrive at f40607 / f40611. The cells: each object's
    # event PC ($1180 + 2n: the caller's op 02 writes it, C0/6027) and its position words ($1800 / $1880 + 2n).
    'hazard-wrap': dict(leg='118_', start=35190, frames=5510, free=[(35351, 39421)],
                        cells=[('7E1196', 2), ('7E1816', 2), ('7E1896', 2), ('7E1198', 2), ('7E1818', 2), ('7E1898', 2)]),
    # Ocean Palace, loc 19B (the elevator room): the menu's Form screen puts Lucca in Ayla's seat ($2980-82 00 01 05 ->
    # 00 01 02) and the menu returns at f1524. The return's reconcile C0/1A03 respawns the characters' objects and
    # C0/1A6F-1A97 stores, by slot, the three words the open stashed ($7F1D09,X / $7F1D0F,X / $7F1D15,X) into the object
    # the table $0197 names NOW: Lucca's object 3 takes slot 2's position whole, $1806 = 0800 and $1886 = 0DFF (the
    # 1/16 px nibble with it), and its priority pair 3030. Then the party walks to the elevator (f1636-f1720, the two
    # followers on the ring from f1660) and to its fight's places (f1836-). The cells: the slot table, the party
    # shadow $0194-96, and objects 1-3's position words and priority pairs. The menu is not graded, to f1524, the frame
    # the cartridge's cells come back (the port's page shows them from f1506, the menu's last frames): the still step
    # from there to the walk is cut by it, and what it holds is in every step of the walk -- object 3 stands on the
    # restored words until its turn on the ring.
    'menu-return': dict(leg='119_', start=20, frames=1880, free=[(36, 1524)],
                        cells=[('7E0197', 6), ('7E0194', 3), ('7E1802', 6), ('7E1882', 6), ('7E0C02', 6)]),
    # Ocean Palace, loc 1A0: the fight with a Barghest (59) and three Thrashers (6D), Crono / Marle / Lucca, from the field
    # before it (f16200) through its first 5800 frames. The cells are the PC pose machine's: the window cursors $968C-8E
    # (C1/2EA9), the visits' deferral flags $9922-24 (C1/2E18, cleared by a publish C1/1C5A), the re-aim latch $A098
    # (C1/2F22) and the facings $96DB-DD. Three things the cartridge does in it:
    #   f20016  seat 2 re-aims at enemy seat 6 on the frame the NMI's stepper moved its foot (9B,97) -> (9C,96): the facing
    #           of the new foot, 02 (C1/2F54; the stepper CF/F978 runs before the walk dispatch C1/10A9)
    #   f20063  Lucca's shot at seat 3 opens: C1/49FF turns her to her target, $96DB+2 00 -> 02 (C1/4A3D), and her next
    #           re-aim (f20267) finds another facing, publishes, and defers seat 0's visit ($9922 01 00 00)
    #   f21445  the close of enemy tech 67 (+1E bit 2 on the three PCs): C1/305C runs the scan C1/2F97 whole, the new
    #           icon restarts the three windows ($968C FF FF FF) and the next pass starts them at entry 0 (f22045)
    # Not graded: the fight's boot to its first window (the port's boot is 13 frames shorter: the first-window class) and
    # the close's scan frame itself (the port's close service runs on the frame $A013 falls, three frames after C1/4127).
    'fight-poses': dict(leg='119_', start=16200, frames=5900, free=[(16201, 16420), (21440, 21449)],
                        cells=[('7E968C', 3), ('7E9922', 3), ('7EA098', 1), ('7E96DB', 3)]),
}


def cart_state(legs, i, start):
    """The cartridge's state at frame `start` of leg i, from the leg's start state and its pad (cached with the key of
    what made it: every pad from power-on through the leg, the oracle, the frame)."""
    lg = legs[i]
    path = os.path.join(WORK, 'leg%s_f%d.state' % (lg['name'].split('_')[0], start))
    key = hashlib.sha1(('%s|%d' % (P.key_of(legs, i), start)).encode()).hexdigest()
    if os.path.exists(path) and os.path.exists(path + '.key') and open(path + '.key').read().strip() == key:
        return path
    if os.path.exists(path):
        os.remove(path)
    if not P.oracle_play(P.start_of(legs, i), cut(lg['pad'], 0, start), path):
        sys.exit('legwindow: the cartridge did not make %s' % path)
    open(path + '.key', 'w').write(key)
    return path


def cut(pad, lo, n):
    """Frames lo..lo+n-1 of a pad, as a pad."""
    out, c = [], 0
    for b, k in pad:
        a, e = max(c, lo), min(c + k, lo + n)
        if e > a:
            out.append((b, e - a))
        c += k
    return out


def port_key(a):
    return ('1' if a[:2] == '7F' else '') + a[2:]


def port_keys(prev, pad):
    """The port's keys are the cartridge's one frame later (tools/playthrough.py walkscript). A leg's start is a still
    moment, so there the frame before is an idle token; a window starts anywhere, and its frame before is the leg's own
    (a held direction stays held across the window's first frame, as it does on the cartridge)."""
    return ','.join('H%X:%d' % (mk, n) if mk else '.:%d' % n for mk, n in prev + pad)


def port_timeline(state, prev, pad, cells, work):
    raw = os.path.join(work, 'seed.raw')
    subprocess.run([P.CT, '--peek', state, raw, '--btnscript', '0:0'], cwd=ROOT, env=P.RULE, capture_output=True)
    w = open(raw + '.wram', 'rb').read()
    lw = os.path.join(work, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + w)
    loc = (w[0x100] | (w[0x101] << 8)) & 0x1FF
    frames = sum(n for _, n in pad)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=state, CT_WS_BATTLE_LEAD='1', CT_SRAM_FRESH='1', CT_SRAM=os.path.join(work, 'port.srm'),
               CT_FIELD_MEMMON=','.join('%s:%X' % (port_key(a), n) for a, n in cells),
               CT_SHOTS=os.path.join(work, 'p'), CT_SHOTS_AT=str(frames), CT_SHOTS_EXIT='1')
    r = subprocess.run([P.FIELD, '--assets', 'ct_assets.dat', '--scene', str(loc), '--loadwram', lw,
                        '--walkscript', port_keys(prev, pad)], cwd=ROOT, env=env, capture_output=True, text=True,
                       errors='replace')
    out = []
    for line in r.stdout.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\S+\s+(.*)', line)
        if not m:
            continue
        kv = {k: v.rstrip('*') for k, v in re.findall(r'\b([0-9A-F]+)=([0-9A-F*]+)', m.group(2))}
        if all(port_key(a) in kv for a, _ in cells):
            out.append((int(m.group(1)), tuple(kv[port_key(a)] for a, _ in cells)))
    return out


def steps(events, first, last):
    """Change events [(frame, row)] -> the steps [(frame, row, length)] over frames first..last."""
    out, cur, j = [], None, 0
    for f in range(first, last + 1):
        while j < len(events) and events[j][0] <= f:
            cur = events[j][1]; j += 1
        if cur is None:
            continue
        if out and out[-1][1] == cur:
            out[-1][2] += 1
        else:
            out.append([f, cur, 1])
    return out


def segments(sc, free):
    """The cartridge's steps between the free spans: [[step, ...], ...], whole steps only."""
    segs, lo = [], None
    for a, b in sorted(free) + [(None, None)]:
        segs.append([s for s in sc if (lo is None or s[0] > lo) and (a is None or s[0] + s[2] - 1 < a)])
        lo = b
    return segs


def run(name, d, verbose):
    legs = P.route()
    i = next(k for k, lg in enumerate(legs) if lg['name'].startswith(d['leg']))
    lg = legs[i]
    work = os.path.join(SCRATCH, name)
    os.makedirs(work, exist_ok=True)
    start, n, cells = d['start'], d['frames'], d['cells']
    free = [(a - start, b - start) for a, b in d.get('free', [])]
    tail = TAIL if free else 0                                # the port's frames past the window (it may come back late)
    state = cart_state(legs, i, start)
    mon = [('c%d' % k, a, port_key(a), b) for k, (a, b) in enumerate(cells)]
    cart = [(f, tuple(kv['c%d' % k] for k in range(len(cells))))
            for f, kv in P.oracle_timeline(state, cut(lg['pad'], start, n), mon)]
    port = port_timeline(state, cut(lg['pad'], start - 1, 1) if start else [(0, 1)], cut(lg['pad'], start, n + tail),
                         cells, work)
    if not cart or not port:
        print('legwindow: %s leg %s: no timeline (cartridge %d event(s), port %d)' % (name, lg['name'], len(cart), len(port)))
        return
    f0 = max(cart[0][0], port[0][0])
    sc, sp = steps(cart, f0, n - 1), steps(port, f0, n + tail - 1)
    fmt = lambda s: 'f%d %s x%d' % (s[0] + start, ' '.join(s[1]), s[2])
    segs = segments(sc, free)
    total, bad, first, notes, j = 0, 0, None, [], 0
    for q, seg in enumerate(segs):
        if not seg:
            continue
        if q:                                                 # past a free span: the port's first step with this value
            k = next((k for k in range(j, len(sp)) if sp[k][1] == seg[0][1]), None)
            if k is None:
                total += len(seg); bad += len(seg)
                first = first or '  first: after the free span the port never shows cartridge %s' % fmt(seg[0])
                notes.append('cartridge f%d-f%d: the port does not come back' % (free[q - 1][0] + start, free[q - 1][1] + start))
                break
            notes.append('cartridge f%d-f%d: both come back on %s, the port %+d frame(s)'
                         % (free[q - 1][0] + start, free[q - 1][1] + start, ' '.join(seg[0][1]), sp[k][0] - seg[0][0]))
            j = k
        for x, a in enumerate(seg):
            b = sp[j] if j < len(sp) else None
            cutlast = (q == len(segs) - 1 and x == len(seg) - 1)      # the window's end cuts the cartridge's last step
            same = b is not None and a[1] == b[1] and (a[2] == b[2] or cutlast)
            total += 1
            if not same:
                bad += 1
                first = first or '  first: step %d cartridge %s, port %s' % (total - 1, fmt(a), fmt(b) if b else '(none)')
                if verbose and bad <= 60:
                    print('    step %d | cartridge %s | port %s' % (total - 1, fmt(a), fmt(b) if b else '-'))
            j += 1
    print('legwindow: %s leg %s f%d+%d: %d steps compared, %d differing' % (name, lg['name'].split('_')[0], start, n, total, bad))
    if first:
        print(first)
    for t in notes:
        print('  free:  ' + t)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('names', nargs='*', help='the windows (default: all)')
    ap.add_argument('-v', action='store_true', help='every differing or free step')
    a = ap.parse_args()
    for name in a.names or sorted(WINDOWS):
        if name not in WINDOWS:
            sys.exit('legwindow: no window %s (%s)' % (name, ', '.join(sorted(WINDOWS))))
        run(name, WINDOWS[name], a.v)


if __name__ == '__main__':
    main()
