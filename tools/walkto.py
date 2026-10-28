"""tools/walkto.py -- ORACLE-DRIVEN PATHFINDER for building fixtures.

Given a LakeSnes state, find a d-pad drive that walks the lead PC to a target pixel -- or through an
exit -- by probing the ORACLE itself: 16-frame steps, each one a real `ct.exe --peek` with PEEK_SAVE,
depth-first with a visited set and a toward-target ordering.  The route therefore comes from the ROM's
own collision, never from a guessed waypoint, and the end state is a genuine oracle state that went
through the real loaders (a --teleport'd state does not: `jmp` leaves the lead PC object inactive and
`req` keeps the old coordinates).  This is how saves/field_re/s5_bell_marle.state was made: scene
439's real exit into scene 5 (--exit, 228 probes), then 44 probes to the bell's talk tile with Marle
following.

usage: python tools/walkto.py <start.state> <tx> <ty> <out.state> [maxprobes] [--exit] [--lead=HEX] [--run] [--ow] [--astar] [--fight] [--pilot] [--dest=HEX]
  --step=N: frames a probe holds the d-pad (16): 8 reaches a world-map passage the 16-px grid steps over
  --exit : the target is an exit box; a step that changes $0100 is SUCCESS (the transition is the goal)
  --dest=HEX: with --exit, only a transition INTO that location ($0100 & 1FF) is the goal; one into another
           location is a wall (a door the route must not take)
  --pilot: as --fight, but a fight is played by tools/battle_pilot.py's Pilot (PILOT_* from the environment) and its pad
           goes into the drive; a scene still takes the A chunks
  --fight: a step that leaves the player without control ($011F = 0: a visible enemy touched, a scene) is followed by A
           presses (40-frame chunks of `100:2,0:18`) until control is back, and that stretch goes into the drive -- the
           visible enemies of a dungeon are fought through instead of walling the search off
Always run with SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy in the environment (set here by default).
"""
import os, re, subprocess, sys, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
ENV = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_ORACLE_LOGICAL='1')   # under the rule
LEAD = 0x02          # the lead PC's actor slot ($1800/$1880 + LEAD): --lead=HEX (the slot `recipes.py try` lists)
HOLD = 0             # extra buttons held with the d-pad: --run holds B (the dash, $00F8 bit1)
OW = False           # --ow: the overworld walker's position ($0283/$0285, whole pixels) instead of an actor slot
BTN = {'R': 0x80, 'L': 0x40, 'U': 0x10, 'D': 0x20}     # LakeSnes serial order (NOT the $00F2 layout)
OPP = {'R': 'L', 'L': 'R', 'U': 'D', 'D': 'U'}
STEP = 16
DEST = None          # --dest=HEX: the one location an --exit transition may go to
FIGHT = False        # --fight: mash A through what takes control away (the fights) and keep going
PILOT = False        # --pilot: as --fight, but a FIGHT is played by tools/battle_pilot.py's Pilot (its PILOT_* settings)
PADS = {}            # --pilot: probe state -> the pad its fight took (the playthrough grammar)
MASH = {}            # probe state -> the 40-frame A chunks its step needed
MASH_CHUNK = '100:2,0:18,100:2,0:18'


def read_pos(state):
    kx, ky = ('0283', '0285') if OW else ('%04X' % (0x1800 + LEAD), '%04X' % (0x1880 + LEAD))
    out = subprocess.run([CT, '--read', '7E0100+2,7E%s+2,7E%s+2' % (kx, ky), '--from', state],
                         cwd=ROOT, env=ENV, capture_output=True, text=True).stdout
    d = dict(re.findall(r'wram\[7E(\w+)\+\d\] = (\w+)', out))
    px = lambda k: int(d[k][2:4] + d[k][0:2], 16) >> (0 if OW else 4)
    return int(d['0100'][2:4] + d['0100'][0:2], 16) & 0x1FF, px(kx), px(ky)


def control(state):
    out = subprocess.run([CT, '--read', '7E011F+1', '--from', state], cwd=ROOT, env=ENV, capture_output=True,
                         text=True).stdout
    m = re.search(r'\] = ([0-9A-F]{2})', out)
    return int(m.group(1), 16) if m else 1


def step(state, btn, frames, outstate):
    env = dict(ENV, PEEK_SAVE=outstate)
    subprocess.run([CT, '--peek', state, outstate + '.fb', '--btnscript', '%X:%d' % (btn | HOLD, frames)],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if PILOT and control(outstate) == 0:
        import playthrough as P
        from battle_pilot import Pilot, in_fight
        pilot, loc0, pad, t = Pilot(), read_pos(outstate)[0], [], 0
        while t < 12000:                            # a fight by the pilot; a scene's frames by A, as --fight
            w = P.wram_of(outstate)
            if w[0x011F] or ((w[0x100] | (w[0x101] << 8)) & 0x1FF) != loc0:
                break
            st = pilot.step(w)[0] if in_fight(w) else P.expand(MASH_CHUNK)
            P.oracle_play(outstate, st, outstate + '.m'); shutil.move(outstate + '.m', outstate)
            pad += st; t += sum(n for _, n in st)
        if pad:
            PADS[outstate] = P.btnscript(pad); MASH[outstate] = 'P'
        return read_pos(outstate)
    if FIGHT and control(outstate) == 0:
        loc0 = read_pos(outstate)[0]
        k = 0
        while k < 200:                             # 8000 frames at most
            tmp = outstate + '.m'
            subprocess.run([CT, '--peek', outstate, tmp + '.fb', '--btnscript', MASH_CHUNK],
                           cwd=ROOT, env=dict(ENV, PEEK_SAVE=tmp), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            shutil.move(tmp, outstate)
            k += 1
            if control(outstate) != 0 or read_pos(outstate)[0] != loc0:
                break
        MASH[outstate] = k
    return read_pos(outstate)


def tok(nxt):
    """The script token for what a probe's step needed after it: a pilot's fight pad, or a count of A chunks."""
    if MASH.get(nxt) == 'P':
        return ['P:' + nxt]
    return ['M%d' % MASH[nxt]] if MASH.get(nxt) else []


def cell(x, y):
    return (x // 8, y // 8)


def main():
    global LEAD, HOLD, OW, FIGHT, PILOT, DEST, STEP
    for a in sys.argv[1:]:
        if a.startswith('--lead='): LEAD = int(a[7:], 16)
        if a == '--run': HOLD = 0x01
        if a == '--ow': OW = True
        if a.startswith('--step='): STEP = int(a[7:])
        if a == '--fight': FIGHT = True
        if a == '--pilot': PILOT = True
        if a.startswith('--dest='): DEST = int(a[7:], 16)
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    want_exit = '--exit' in sys.argv
    if len(args) < 4:
        print(__doc__); sys.exit(2)
    start, tx, ty, out = args[0], int(args[1]), int(args[2]), args[3]
    maxprobes = int(args[4]) if len(args) > 4 else 300
    tmpdir = out + '.probe'
    os.makedirs(tmpdir, exist_ok=True)
    loc0, x, y = read_pos(start)
    print('start loc=%03X (%d,%d) -> target (%d,%d)%s' % (loc0, x, y, tx, ty, ' [exit]' if want_exit else ''))
    visited = {cell(x, y)}
    stack = [(start, (x, y), [], set())]        # (state file, pos, script so far, directions tried)
    probes = 0
    best = None
    if '--astar' in sys.argv:                   # best-first (A*-like): shortest routes through crowds, more probes
        import heapq
        dist = lambda x, y: abs(tx - x) + abs(ty - y)
        heap = [(dist(x, y), 0, start, (x, y), [])]
        seq = 0
        while heap and probes < maxprobes:
            _, _, st, (x, y), script = heapq.heappop(heap)
            if not want_exit and abs(tx - x) <= 4 and abs(ty - y) <= 4:
                best = (st, (x, y), script); break
            for d in 'UDLR':
                probes += 1
                nxt = os.path.join(tmpdir, 'p%04d.state' % probes)
                loc1, nx, ny = step(st, BTN[d], STEP, nxt)
                if loc1 != loc0:
                    if want_exit and (DEST is None or (loc1 & 0x1FF) == DEST):
                        best = (nxt, (nx, ny), script + [d] + tok(nxt))
                        print('  transition -> loc %03X at (%d,%d)' % (loc1, nx, ny)); break
                    continue
                fought = MASH.get(nxt, 0)
                if not fought and ((nx, ny) == (x, y) or cell(nx, ny) in visited):
                    continue
                visited.add(cell(nx, ny))
                seq += 1
                ext = [d] + tok(nxt)
                heapq.heappush(heap, ((len(script) + 1) * 8 + 2 * dist(nx, ny), seq, nxt, (nx, ny), script + ext))
            if best:
                break
        if not best and not want_exit and heap:
            b = min(heap, key=lambda h: dist(*h[3]))
            best = (b[2], b[3], b[4])
        stack = []
    while stack and probes < maxprobes:
        st, (x, y), script, tried = stack[-1]
        dx, dy = tx - x, ty - y
        if not want_exit and abs(dx) <= 4 and abs(dy) <= 4:
            best = (st, (x, y), script); break
        prim = ('R' if dx > 0 else 'L') if abs(dx) >= abs(dy) else ('D' if dy > 0 else 'U')
        sec = ('D' if dy > 0 else 'U') if abs(dx) >= abs(dy) else ('R' if dx > 0 else 'L')
        order = [prim, sec, OPP[sec], OPP[prim]]
        cand = [d for d in order if d not in tried]
        if not cand:
            stack.pop(); continue
        d = cand[0]
        tried.add(d)
        probes += 1
        nxt = os.path.join(tmpdir, 'p%04d.state' % probes)
        loc1, nx, ny = step(st, BTN[d], STEP, nxt)
        if loc1 != loc0:
            if want_exit and (DEST is None or (loc1 & 0x1FF) == DEST):
                best = (nxt, (nx, ny), script + [d])
                print('  transition -> loc %03X at (%d,%d)' % (loc1, nx, ny)); break
            continue                                     # a step that leaves the scene is not a step
        fought = MASH.get(nxt, 0)
        if not fought and ((nx, ny) == (x, y) or cell(nx, ny) in visited):
            continue
        visited.add(cell(nx, ny))
        stack.append((nxt, (nx, ny), script + [d] + tok(nxt), set()))
    if not best and stack and not want_exit:
        best = stack[-1][:3]
    if not best:
        print('no route (%d probes)' % probes); return
    st, (x, y), script = best
    shutil.copyfile(st, out)
    merged = []
    for d in script:
        if d.startswith('M') or d.startswith('P:'):
            merged.append([d, 0])
        elif merged and merged[-1][0] == d: merged[-1][1] += STEP
        else: merged.append([d, STEP])
    print('end (%d,%d) after %d probes, %d steps' % (x, y, probes, len(script)))
    print('script:', ','.join(PADS[b[2:]] if b.startswith('P:') else ('(%s)*%d' % (MASH_CHUNK, int(b[1:])))
                              if b.startswith('M') else '%X:%d' % (BTN[b] | HOLD, f) for b, f in merged))
    print('state ->', out)
    shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == '__main__':
    main()
