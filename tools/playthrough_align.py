#!/usr/bin/env python3
r"""Is a DIFFERENT leg only out of sync? The port replayed with its pad re-anchored on the cartridge's events.

    python tools/playthrough_align.py <leg>... [-jN] [--tol N] [--max N]   (a leg by its number or name)

The route's pad is the cartridge's, frame by frame. When the port reaches an event a frame or three away from the cartridge
(a fight's end, the control handed back, a gate's start: the architecture's timing, which CLAUDE.md does not reproduce),
every key after it lands at another moment of the game, and the leg runs apart from there although nothing the game does
differs. This tool tells the two apart. It plays the port, finds the first graded change (the `cmp` chains: location,
storyline, party, gold, HP, control) whose VALUE matches the cartridge's but whose frame is off by at most --tol frames
(default 12), and warps the pad there: the keys from that cartridge frame on move by the offset (an offset > 0 inserts
idle frames, < 0 drops the frames just before it). Then it plays again, until every chain matches in value and frame or
the first difference is a VALUE (a different place, story value, party, gold or HP, a control change the other side never
makes) or a frame further off than --tol: that is a divergence of the game itself, the one to fix.

One line a leg:
    align: <leg> SYNC-ONLY after <n> warp(s) [f<cart frame><offset> ...] -- end regions <same | ...>
    align: <leg> DIVERGES at cart f<N> <chain>: cartridge <v>@<f> / port <v>@<f> (after <n> warps) -- <the chains there>
A SYNC-ONLY leg is the user's acceptable class (2026-11-01): its DIFFERENT comes from the timing alone.
"""
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import playthrough as P
import playthrough_triage as T

TOL = 12
MAXW = 40


def frames_of(pad):
    out = []
    for b, n in pad:
        out.extend([b] * n)
    return out


def runs_of(fr):
    pad = []
    for b in fr:
        if pad and pad[-1][0] == b:
            pad[-1][1] += 1
        else:
            pad.append([b, 1])
    return [tuple(x) for x in pad]


def warp(fr, warps):
    """The cartridge's per-frame keys with the warps applied: [(cart frame g, offset d)], g ascending."""
    at = dict(warps)
    out = []
    for c, b in enumerate(fr):
        d = at.get(c, 0)
        if d > 0:
            out.extend([0] * d)
        elif d < 0:
            del out[max(0, len(out) + d):]
        out.append(b)
    return out


def to_port(f, warps):
    return f + sum(d for g, d in warps if g <= f)


def port_run(lg, start, tmp, pad, dump_at):
    """The port's chains for a pad (the cmp's monitors), and its WRAM at frame dump_at."""
    w = T.wram_of(start, tmp)
    lw = os.path.join(tmp, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + w)       # always: the start state may have been replayed since
    loc = (w[0x100] | (w[0x101] << 8)) & 0x1FF
    dump = os.path.join(tmp, 'align_end.wram')
    if os.path.exists(dump):
        os.remove(dump)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=start, CT_WS_BATTLE_LEAD='1', CT_SRAM_FRESH='1', CT_SRAM=os.path.join(tmp, 'port.srm'),
               CT_FIELD_MEMMON=','.join('%s:%d' % (p, n) for _, _, p, n in P.MON),
               CT_DUMPWRAM=dump, CT_DUMPWRAM_AT=str(dump_at),
               CT_SHOTS=os.path.join(tmp, 'p'), CT_SHOTS_AT=str(dump_at + 2), CT_SHOTS_EXIT='1')
    r = subprocess.run([P.FIELD, '--assets', 'ct_assets.dat', '--scene', str(loc), '--loadwram', lw,
                        '--walkscript', P.walkscript(pad)], cwd=P.ROOT, env=env, capture_output=True, text=True,
                       errors='replace')
    out = []
    for line in r.stdout.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\S+\s+(.*)', line)
        if not m:
            continue
        kv = {k: v.rstrip('*') for k, v in re.findall(r'\b([0-9A-F]+)=([0-9A-F*]+)', m.group(2))}
        if all(p in kv for _, _, p, _ in P.MON):
            out.append((int(m.group(1)), {k: kv[p] for k, _, p, _ in P.MON}))
    return chains(out), (open(dump, 'rb').read() if os.path.exists(dump) else None)


def chains(tl):
    """The cmp's chains, plus `lit`: the picture on or black ($0119 zero or not). A fade's start and end are where a
    location load ends, and a load's length is the CPU's (CLAUDE.md, NMI-off loads): the port can land a frame or two
    off it, every object's cadence with it, while the pad goes on at the cartridge's frames. `lit` lets the warp
    re-anchor there; its value never differs on its own."""
    ch = P.chains(tl)
    lit = []
    for f, kv in tl:
        v = '0' if kv['bri'] == '00' else '1'
        if not lit or lit[-1][1] != v:
            lit.append((f, v))
    ch['lit'] = lit
    return ch


DARK_TOL = 240


def dark(ch, f):
    """Is frame f inside a black stretch ($0119 = 0) on that side (its `lit` chain)?"""
    v = '1'
    for g, x in ch.get('lit', []):
        if g > f:
            break
        v = x
    return v == '0'


def deblip(ch, n=4):
    """A control chain without its runs shorter than n frames (a frame of control between two events that no player
    can act on: the cartridge's 25 hands it back for one frame at f467), the neighbours merged. Four, not three: a blip the
    cartridge holds two frames can last three on the port (51: f4334-4336 / f4331-4334), and one side's kept blip is no value."""
    out = []
    for i, (f, v) in enumerate(ch):
        nxt = ch[i + 1][0] if i + 1 < len(ch) else None
        if nxt is not None and nxt - f < n and i > 0:
            continue
        if out and out[-1][1] == v:
            continue
        out.append((f, v))
    return out


def first_mismatch(o, p, warps, end):
    """The earliest event (in cartridge frames) where the chains part: (cart frame, chain, cart ev, port ev)."""
    o = dict(o, ctrl=deblip(o['ctrl'])); p = dict(p, ctrl=deblip(p['ctrl']))
    best = None
    for k in o:
        co = [(f, v) for f, v in o[k] if f <= end]
        po = p[k]
        for i in range(max(len(co), len(po))):
            c = co[i] if i < len(co) else None
            q = po[i] if i < len(po) else None
            if c and q and c[1] == q[1] and to_port(c[0], warps) == q[0]:
                continue
            # The same change made on a black screen on both sides is a load's inside (an NMI-off stretch: the
            # cartridge's CPU time, which the port spends at once -- CLAUDE.md): equal, and no warp -- the load's end
            # (the `lit` rise after it) is where the two meet again.
            if c and q and c[1] == q[1] and dark(o, c[0]) and dark(p, q[0]) and abs(q[0] - to_port(c[0], warps)) <= DARK_TOL:
                continue
            if c is None and q is not None and q[0] > to_port(end, warps):
                break                                       # a change past the leg's (warped) end: not graded
            f = c[0] if c else end
            if q and not c:
                f = max(0, min(end, q[0] - sum(d for _, d in warps)))
            if best is None or f < best[0]:
                best = (f, k, c, q)
            break
    return best


def align(legs, i):
    lg = legs[i]
    start = P.start_of(legs, i)
    if lg['oracle_only'] or start == 'boot' or not os.path.exists(start):
        return 'align: %s skipped (no start state)' % lg['name']
    tmp = os.path.join(P.TMP, 'align_%s' % lg['name'])
    os.makedirs(tmp, exist_ok=True)
    fr = frames_of(lg['pad'])
    end = len(fr)
    o = chains(P.oracle_timeline(start, lg['pad']))
    cart_end = T.wram_of(P.state_of(lg['name']), tmp) if os.path.exists(P.state_of(lg['name'])) else None
    warps = []
    while True:
        pend = to_port(end, warps)
        p, w = port_run(lg, start, tmp, runs_of(warp(fr, warps)), pend)
        mm = first_mismatch(o, p, warps, end)
        ws = ' '.join('f%d%+d' % (g, d) for g, d in warps) or '-'
        if mm is None:
            rep = T.diff(cart_end, w) if (cart_end and w) else ['no end dump']
            return 'align: %s SYNC-ONLY after %d warp(s) [%s] -- end regions %s' % (
                lg['name'], len(warps), ws, 'same' if not rep else ' | '.join(rep))
        f, k, c, q = mm
        ev = lambda e: '%s@%d' % (e[1], e[0]) if e else '-'
        timing = c and q and c[1] == q[1] and abs(q[0] - to_port(c[0], warps)) <= TOL
        if not timing or len(warps) >= MAXW:
            near = lambda ch: ' '.join('%s@%d' % (v, x) for x, v in ch[k] if abs(x - f) < 3000)
            return ('align: %s DIVERGES at cart f%d %s: cartridge %s / port %s (after %d warp(s) [%s])\n'
                    '    %s cartridge %s\n    %s port      %s' % (lg['name'], f, k, ev(c), ev(q), len(warps), ws,
                                                                k, near(o), k, near(p)))
        d = q[0] - to_port(c[0], warps)
        at = dict(warps)
        at[c[0]] = at.get(c[0], 0) + d
        warps = sorted((g, x) for g, x in at.items() if x)


def main():
    global TOL, MAXW
    a = sys.argv[1:]
    jobs = 1
    for x in list(a):
        if x.startswith('-j'):
            jobs = int(x[2:] or os.cpu_count()); a.remove(x)
    for flag in ('--tol', '--max'):
        if flag in a:
            k = a.index(flag); v = int(a[k + 1]); del a[k:k + 2]
            if flag == '--tol':
                TOL = v
            else:
                MAXW = v
    if not a:
        sys.exit(__doc__)
    legs = P.route()
    def leg(x):
        hits = [i for i, lg in enumerate(legs) if lg['name'] == x or lg['name'].split('_')[0] == x]
        return hits[0] if hits else P.find(legs, x)
    with ThreadPoolExecutor(jobs) as ex:
        for line in ex.map(lambda i: align(legs, i), [leg(x) for x in a]):
            print(line, flush=True)


if __name__ == '__main__':
    main()
