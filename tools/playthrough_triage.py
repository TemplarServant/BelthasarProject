#!/usr/bin/env python3
r"""Triage of the playthrough's DIFFERENT legs: does the port leave the GAMEPLAY where the cartridge leaves it?

    python tools/playthrough_triage.py <leg>... [-jN] [--first]   (a leg by its number or name; `all` = every leg)

`playthrough.py cmp` grades the chains (location, storyline, party, gold, HP, control) and the last picture; a picture
off by a sync artefact (the architecture's timing: a frame early, a fade a step behind) reads DIFFERENT while the game
goes on the same. This tool answers the other question: at the leg's end, is the state the next leg plays from the same?
From the leg's start state the port plays the same pad as `cmp` (Belthasar --loadwram, the same environment) and dumps
its WRAM at the leg's last frame and the one after (CT_DUMPWRAM_AT); the cartridge's is the leg's end state
(saves/playthrough/<leg>.state). Compared, region by region:

    story   $7F:0000-$7F:01FF  the storyline and the event / chest / visit flags
    items   $7E:2400-$7E:25FF  the bag (ids, counts)
    stats   $7E:2600-$7E:287F  the characters' blocks (HP, MP, level, exp, equipment ...)
    party   $7E:2980-$7E:2987  who is in the party
    gold    $7E:2C53-$7E:2C55
    loc     $7E:0100            the location
    tile    the lead's tile ($1801 / $1881 + the lead object $0197)

One line a leg: `triage: <leg> GAMEPLAY-SAME` (every region equal at either frame) or `triage: <leg> GAMEPLAY-DIFFERENT
-- <region>: <n> byte(s) [first $addr cart/port] ...`. GAMEPLAY-SAME with a DIFFERENT `cmp` = a sync-only leg (the
user's rule, 2026-11-01: acceptable, counted as SAME); GAMEPLAY-DIFFERENT = a port divergence to fix before the route
goes on. --first then bisects that leg for the first frame whose regions differ (the cartridge's `--peek` cut at f
against the port's dump at f or f+1): where the investigation starts.
"""
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

FIRST = False                                              # --first: bisect a GAMEPLAY-DIFFERENT leg's first divergence

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import playthrough as P

REGIONS = [('story', 0x10000, 0x10200), ('items', 0x2400, 0x2600), ('stats', 0x2600, 0x2880),
           ('party', 0x2980, 0x2988), ('gold', 0x2C53, 0x2C56), ('loc', 0x0100, 0x0102)]


def wram_of(state, tmp):
    """A state's WRAM through a peek of 0 frames, in the leg's own scratch (P.wram_of's is shared: threads, a running cmp)."""
    os.makedirs(tmp, exist_ok=True)
    raw = os.path.join(tmp, 'w_%s.raw' % os.path.basename(state))
    subprocess.run([P.CT, '--peek', state, raw, '--btnscript', '0:0'], cwd=P.ROOT, env=P.RULE, capture_output=True)
    return open(raw + '.wram', 'rb').read()


def lead_tile(w):
    lead = w[0x197]
    return w[0x1801 + lead], w[0x1881 + lead]


def port_at(lg, start, tmp, f):
    """The port's WRAM at frame f of the leg (a flat 128 KB image), from the leg's start state; None if it never got there."""
    w = wram_of(start, tmp)
    lw = os.path.join(tmp, 'seed.lakewram')
    if not os.path.exists(lw):
        open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + w)
    loc = (w[0x100] | (w[0x101] << 8)) & 0x1FF
    dump = os.path.join(tmp, 'port_f%d.wram' % f)
    if os.path.exists(dump):
        os.remove(dump)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=start, CT_WS_BATTLE_LEAD='1', CT_SRAM_FRESH='1',
               CT_SRAM=os.path.join(tmp, 'port.srm'), CT_DUMPWRAM=dump, CT_DUMPWRAM_AT=str(f),
               CT_SHOTS=os.path.join(tmp, 'p'), CT_SHOTS_AT=str(f + 1), CT_SHOTS_EXIT='1')
    subprocess.run([P.FIELD, '--assets', 'ct_assets.dat', '--scene', str(loc), '--loadwram', lw,
                    '--walkscript', P.walkscript(lg['pad'])], cwd=P.ROOT, env=env, capture_output=True)
    return open(dump, 'rb').read() if os.path.exists(dump) else None


def oracle_at(lg, start, tmp, f):
    """The cartridge's WRAM after frame f of the leg (the rule's oracle, the leg's pad cut at f)."""
    pad, n = [], 0
    for b, k in lg['pad']:
        if n >= f:
            break
        pad.append((b, min(k, f - n))); n += min(k, f - n)
    raw = os.path.join(tmp, 'orc_f%d.raw' % f)
    subprocess.run([P.CT, '--peek', start, raw, '--frames', str(f), '--btnscript', P.btnscript(pad) or '0:0'],
                   cwd=P.ROOT, env=P.RULE, capture_output=True)
    return open(raw + '.wram', 'rb').read()


def first_divergence(lg, start, tmp):
    """Bisect the leg for the first frame whose gameplay regions differ on both sides (the port at f or f+1)."""
    def differs(f):
        c = oracle_at(lg, start, tmp, f)
        reps = [diff(c, p) for p in (port_at(lg, start, tmp, f), port_at(lg, start, tmp, f + 1)) if p is not None]
        return min(reps, key=len) if reps and all(reps) else ([] if reps else ['port: no dump'])
    lo, hi = 0, sum(n for _, n in lg['pad'])
    rep = differs(hi)
    if not rep:
        return None
    while hi - lo > 1:
        mid = (lo + hi) // 2
        r = differs(mid)
        if r:
            hi, rep = mid, r
        else:
            lo = mid
    return hi, rep


def diff(c, p):
    rep = []
    for name, a, b in REGIONS:
        d = [i for i in range(a, b) if c[i] != p[i]]
        if d:
            rep.append('%s: %d byte(s) [first $%s%04X %02X/%02X]' % (name, len(d), '7F:' if d[0] >= 0x10000 else '',
                                                                    d[0] & 0xFFFF, c[d[0]], p[d[0]]))
    if lead_tile(c) != lead_tile(p):
        rep.append('tile: %s/%s' % (lead_tile(c), lead_tile(p)))
    return rep


def triage(legs, i):
    lg = legs[i]
    start = P.start_of(legs, i)
    if lg['oracle_only'] or start == 'boot':
        return 'triage: %s oracle-only' % lg['name']
    st = P.state_of(lg['name'])
    if not os.path.exists(st) or not os.path.exists(start):
        return 'triage: %s no state (run `playthrough.py play %s`)' % (lg['name'], lg['name'])
    tmp = os.path.join(P.TMP, 'tri_%s' % lg['name'])
    c = wram_of(st, tmp)
    end = sum(n for _, n in lg['pad'])
    ends = [port_at(lg, start, tmp, end), port_at(lg, start, tmp, end + 1)]
    best = None
    for k, p in enumerate(ends):
        if p is None:
            continue
        rep = diff(c, p)
        if not rep:
            return 'triage: %s GAMEPLAY-SAME (port end +%d)' % (lg['name'], k)
        if best is None or len(rep) < len(best):
            best = rep
    if best is None:
        return 'triage: %s NO PORT DUMP (the port did not reach the leg\'s last frame)' % lg['name']
    line = 'triage: %s GAMEPLAY-DIFFERENT -- %s' % (lg['name'], ' | '.join(best))
    if FIRST:
        fd = first_divergence(lg, start, tmp)
        line += '\n  first divergence: %s' % ('f%d -- %s' % (fd[0], ' | '.join(fd[1])) if fd else 'none at a frame boundary')
    return line


def main():
    a = sys.argv[1:]
    jobs = 1
    global FIRST
    FIRST = '--first' in a
    if FIRST:
        a.remove('--first')
    for x in list(a):
        if x.startswith('-j'):
            jobs = int(x[2:] or os.cpu_count()); a.remove(x)
    legs = P.route()
    if not a:
        sys.exit(__doc__)
    def leg(x):                                            # a name, or its number prefix (`07`)
        hits = [i for i, lg in enumerate(legs) if lg['name'] == x or lg['name'].split('_')[0] == x]
        return hits[0] if hits else P.find(legs, x)
    idx = range(len(legs)) if a == ['all'] else [leg(x) for x in a]
    with ThreadPoolExecutor(jobs) as ex:
        for line in ex.map(lambda i: triage(legs, i), idx):
            print(line, flush=True)


if __name__ == '__main__':
    main()
