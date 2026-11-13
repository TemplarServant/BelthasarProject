#!/usr/bin/env python3
"""The battle message window's typewriter step, frame by frame: where the cartridge's main thread comes back from a wait
WITHOUT `JSR CD/04DF`, against the frames the port holds the step on.

CD/04DF (one glyph of a typed box, the dwell `$CCEB` reloaded, `INC $CCED`) has two callers, both a wait's way back:
CD/3E89 -- the tail of bank CD's own wait CD/3E7D -- and CD/04DB, the end of the service CD/0009 that bank C1 calls
after its wait C1/007E (the main loop's C1/10A0 and C1/10C0, C1/10FA, C1/354D, 356D, 3689). A wait that hands back to
neither types nothing on the frame it returns in:

    C1/405F              an action's opening (C1/4058 `INC $A0FD / JSL CC/F06B / JSR C1/007E`), the frame before `INC $A013`
    C1/4841              after the sheets' eight packets (the rise + 9)
    C1/4116 .. C1/41B7   the action's close, from the frame after C1/40E4's service to `STZ $A013` (one frame, four with a
                         PC among the targets)
    C1/358E 3596 359E    the victory's three seat removals
    CF/FCFB              the module's exit CF/FBE5-FCE9, its own wait

(disasm/notes/battle_message_window.md sec. 6.)

  cartridge  `ct.exe --hookpc CD04DF` over the leg's pad: the visits of each frame, with the beam line of each. A visit
             at or after the NMI's line (225) belongs to the NEXT frame's return (the plain NMI ends inside its vblank);
             one before it to its own frame (an action's NMI waits for line 64 first, CD/09AA). A frame of the fight
             with no return of its own is a wait that typed nothing.
  port       its CT_BATTLE_TRACE lines `typeheld f<k> wait <w>` (src/battle.c battleTypewriterHeld), k counted from
             `battlephase: begins at field f<n>`.

usage: battle_typewriter_cmp.py <case> | --span <leg> <a> <b>      (cases below)      env PT_FIELD / PT_TMP as tools/playthrough.py
  typewriter: <case> leg <leg> f<a>-<b>: the cartridge returns from <n> waits with no CD/04DF, the port holds the step on <m> frames; <d> differ
  typewriter: <case> the port's waits: C1/405F <n>, C1/4841 <n>, C1/4116..41B7 <n>, C1/358E..359E <n>, CF/FCFB <n>
  typewriter: <case> the cartridge's frames: f.. f..

--span explores any leg: it counts only inside the fights (the runs of visited frames). Its frames are the hook's: a
load between the leg's start and a fight can leave them one off --monitor's rows (leg 70's second fight), so read a
span's differences against the fight's own `$A0FD` / `$A013` rows before calling them the port's.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
os.chdir(ROOT)
import playthrough as P  # noqa: E402

# name -> (leg, first frame, end frame): a span INSIDE one fight (the main loop live from its first frame to its last).
CASES = {
    # Zeal's Ocean Palace, leg 117's first fight from its first target box to its exit: twelve actions (two with a PC
    # among the targets: the close's four waits at f1712-f1715 and f2719-f2722), the victory (f3156-f3158) and the
    # module's exit (f3999-f4005).
    'action-waits': ('117_kajar_restock', 780, 4010),
}
NAMES = {1: 'C1/405F', 2: 'C1/4841', 3: 'C1/4116..41B7', 4: 'C1/358E..359E', 5: 'CF/FCFB'}


def cut(pad, cnt):
    out = []
    for bb, q in pad:
        if cnt <= 0:
            break
        out.append((bb, min(q, cnt)))
        cnt -= q
    return out


def cart_frames(start, pad, n):
    """{frame: visits} of CD/04DF over the first n frames, each visit on the frame its wait returns in."""
    r = subprocess.run([P.CT, P.ROM, '--hookpc', 'CD04DF', '--from', start, '--btnscript', P.btnscript(pad), '--frames', str(n)],
                       cwd=ROOT, env=P.RULE, capture_output=True, text=True, errors='replace')
    got = {}
    for ln in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+visits=(\d+)', ln)
        if not m or not int(m.group(2)):
            continue
        f = int(m.group(1))
        vp = re.search(r'vPos:\s*([0-9* ]+?)\s+hPos', ln)
        for v in (vp.group(1).split() if vp else []):
            line = int(v.rstrip('*'))
            lf = f + 1 if line >= 225 else f
            got[lf] = got.get(lf, 0) + 1
    return got


def port_held(start, pad):
    """{leg frame: wait} the port holds the step on."""
    err = []
    real = subprocess.run

    def run(*a, **k):
        r = real(*a, **k)
        e = getattr(r, 'stderr', None)
        if e:
            err.append(e.decode('utf-8', 'replace') if isinstance(e, bytes) else e)
        return r
    had = os.environ.get('CT_BATTLE_TRACE')
    os.environ['CT_BATTLE_TRACE'] = '1'
    P.subprocess.run = run
    try:
        P.MON = [('a013', '7EA013', 'A013', 1)]
        P.port_timeline(start, pad)
    finally:
        P.subprocess.run = real
        if had is None:
            os.environ.pop('CT_BATTLE_TRACE', None)
        else:
            os.environ['CT_BATTLE_TRACE'] = had
    base = None
    held = {}
    for e in err:
        for ln in e.splitlines():
            m = re.search(r'begins at field f(\d+)', ln)
            if m:
                base = int(m.group(1))
            m = re.match(r'typeheld f(\d+) wait (\d+)', ln)
            if m and base is not None:
                held[base + int(m.group(1))] = int(m.group(2))
    return held


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else ''
    if name == '--span' and len(sys.argv) == 5:          # exploring: any leg, any span
        name, (leg, a, b) = 'span', (sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))
    elif name in CASES:
        leg, a, b = CASES[name]
    else:
        print('usage: battle_typewriter_cmp.py <%s> | --span <leg> <a> <b>' % '|'.join(CASES))
        return 2
    legs = P.route()
    i = P.find(legs, leg)
    if i > 0:
        P.ensure_states(legs, i - 1)
    P.TMP = os.path.join(ROOT, 'build', '_typewriter', 'run%d' % os.getpid())   # this run's own files
    os.makedirs(P.TMP, exist_ok=True)
    lg = legs[i]
    start = P.start_of(legs, i)
    pad = cut(lg['pad'], b)
    got = cart_frames(start, pad, b)
    live = set(range(a, b))
    if name == 'span':                                   # (exploring: only inside the fights -- the runs of visited
        seen = sorted(f for f in got if a <= f < b)      #  frames, a gap of up to eight frames kept; a case names its span)
        live = set()
        for k, f in enumerate(seen):
            live.add(f)
            if k and f - seen[k - 1] <= 9:
                live.update(range(seen[k - 1], f))
    quiet = [f for f in range(a, b) if f in live and not got.get(f)]
    held = port_held(start, pad)
    mine = sorted(f for f in held if f in live)
    diff = sorted(set(quiet) ^ set(mine))
    print('typewriter: %s leg %s f%d-%d: the cartridge returns from %d waits with no CD/04DF, the port holds the step on %d frames; %d differ'
          % (name, lg['name'], a, b, len(quiet), len(mine), len(diff)))
    print("typewriter: %s the port's waits: %s" % (name, ', '.join(
        '%s %d' % (NAMES[w], sum(1 for f in mine if held[f] == w)) for w in sorted(NAMES))))
    print("typewriter: %s the cartridge's frames: %s" % (name, ' '.join('f%d' % f for f in quiet) or '-'))
    for f in diff[:24]:
        print('typewriter: %s   f%d: the cartridge %s, the port %s' % (name, f, 'types' if got.get(f) else 'does not',
                                                                      'holds (%s)' % NAMES.get(held[f], '?') if f in held else 'types'))
    import shutil
    shutil.rmtree(P.TMP, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
