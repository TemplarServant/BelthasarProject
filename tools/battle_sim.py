#!/usr/bin/env python3
r"""Any battle, port against the rule's cartridge: every enemy formation the ROM stages, any party, any bag.

    python tools/battle_sim.py catalog [enemy]               # every formation in the scene scripts (enemy: hex id or name)
    python tools/battle_sim.py fight <enemies> [options]     # one fight, both sides, graded
    python tools/battle_sim.py fight native [options]        # gato_wait's own fight, unstaged: the baseline
    python tools/battle_sim.py sweep [ids|all] [options] [-jN]   # every enemy alone (default: every id a script loads)
    python tools/battle_sim.py formations [-jN] [options]    # every catalogued formation
    python tools/battle_sim.py techs <enemies> [chars] [-jN]  # every char (default 0-6) alone, each of its 8 techs
    python tools/battle_sim.py items <enemies> [-jN]         # every battle consumable (battle_ab.ITEMS), Crono alone

  <enemies>  hex ids `3E,3F,31` (each with the slot operand its own op 0x83 has in the ROM, placed in a row), or a
             catalogued formation `@048:9` (scene 048, the formation holding object 9: ids, slot operands, layout)
  party=0,1,2      the party (char ids 0-6; default 0,1,2), every tech learned, 99 MP (battle_ab.pokes_for)
  inv=BD:05,C2:01  the bag ($2400 ids / $2500 counts, in that order); default the state's own
  drive=mash       A every 12 frames from f640 (the default: every PC attacks the first target)
  drive=tech:K     the active PC's K-th tech (battle_ab.drive_tech), then the mash from 300 frames later
  drive=item:ID    item ID (hex) in the bag's first row x5, used on the default target, then the mash
  drive=combo:ID   the dual / triple tech ID (hex) by its participants (the party: battle_ab.COMBO_GROUP, only this
                   combo learned) once all are ready, on the default target, then the mash
  drive=f:k,...    keys at cartridge frames (k in a b u d l r L R), e.g. drive=640:d,652:a,664:a,676:a
  hp=N             every staged enemy's curHP := N at frame hpat (default off): makes any enemy killable
  hpat=F           that frame (default 600, the fight booted and its first menu not yet up)
  poke=0026:80,..  more WRAM bytes ($7E addr:val, hex) set at hpat on both sides (e.g. the battle RNG's index $0026)
  frames=N         run length (default 1500)
  mon=CCEA:1,...   more WRAM regions ($7E addr:len) graded with the rest (the cartridge's --monitor takes 32 in all)

Why a new staging: CT has no formation table: the fight takes the enemy objects on screen (C0/0557 reads `$1101,Y`),
and op 0x83 (C0/46DF) is what makes an object an enemy -- its id, its slot (`$0D00` from operand 2), its mode (operand 2
bit 7 -> mode 6 -> record +7 bit 6, the bit CF/FE4B tests before the death's palette copy), and its sprite and palette
through the field's allocators. battle_ab's `enemies` pokes only `$1111` (GATO's id): the fight then runs that enemy's
stats and AI but on GATO's field graphics and palette (a Blue Shield in GATO's red and white). Here the field's
own op 0x83 runs on both sides: the bytes `83 id op2 / 8B x y / 89 20 / AA 03 / B2` are written into the script image
at $7F:3000+ (the ROM's runner reads ops from $7F:2001 + IP; the port's seed takes the image from the WRAM,
ct_vm.c CtSceneSeedFromWram) and an object's IP `$1180,X` is pointed there. The objects are gato_wait's five non-party
PC objects 3-7 (parked on B0, `$1100` = 80 -> 00 so the runner visits them, C0/5ACC), fresh to the allocators; GATO
(object 8) leaves the field (`$1110` = 80: C0/0617 stages only mode 5/6). Hence at most five enemies (five hosts;
C0/052E itself takes six candidates). The fight then starts from the A that closes TABAN's box.

The catalog reads every scene script's object fn0 for `83 id op2` and the coordinate op after it (8B tiles / 8D
pixels): a formation is the enemies of one scene whose places chain within 7 tiles of each other, fit one screen (16 x 14 tiles) and
whose slots differ -- the objects a fight at that spot would stage. A layout is kept but recentred on GATO's tile.

Reports per fight, both sides from the frame both have staged ($AF02):
    sim <label>: staged cartridge [ids] f<n>, port [ids] f<n>
    sim <label>: wram same values in the same order on both sides | $<addr> change #<k> differs: ...
                 (each monitored byte's sequence of values -- HP/MP/status per combatant, the action bytes, $AF02,
                 $993C, $2380 -- compared without its frames; then the frame offsets of the matching changes)
    sim <label>: port frame - cartridge frame per change: <offset> x<count> ...
    sim <label>: deaths cartridge [f..] port [f..]
    sim <label>: death palette (OBJ pal 4, $2380): ... the same sequence | first differs at state #k
    sim <label>: picture <n> frames exact at lags ...; <m> with no exact match   (port P vs cartridge P+lag, |lag| <= 12)

CT_BUILD_DIR=build_alt runs another build (battle_ab.BUILD). Frames are the cartridge's; the work files go to
build/_battle_ab (battle_ab.run).
"""
import json
import os
import re
import sys

sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]
import numpy as np  # noqa: E402
TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import battle_ab as ab  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc')
CACHE = os.path.join(ROOT, 'build', '_battle_sim', 'catalog.json')
HOSTS = [3, 4, 5, 6, 7]               # gato_wait's non-party PC objects: the enemies' field objects
PROG = 0x13000                        # $7F:3000, inside the script image, past every shipped script (<= 0x16FA)
ARENA = (8, 8)                        # GATO's tile (scene 6 obj8 fn0 `8B 08 08`)
SCREEN = (16, 14)
PACK = 7                              # a pack's members chain within 7 tiles (the forest's: 5 x 4, 3 x 3; Lavos's core 7)
NAMES = None


def names():
    global NAMES
    if NAMES is None:
        r = open(ROM, 'rb').read()
        def ch(b):
            if 0xA0 <= b <= 0xB9: return chr(65 + b - 0xA0)
            if 0xBA <= b <= 0xD3: return chr(97 + b - 0xBA)
            if 0xD4 <= b <= 0xDD: return chr(48 + b - 0xD4)
            return ' ' if b == 0xEF else ''
        NAMES = [''.join(ch(b) for b in r[0xC6500 + i * 11:0xC6500 + i * 11 + 11]).strip() for i in range(256)]
    return NAMES


# ---------------------------------------------------------------------------------------------------- the catalog
def scan_scripts():
    """[(scene, obj, id, op2, tx, ty)]: every op 0x83 in an object's fn0 and the coordinate op that follows it."""
    if TOOLS not in sys.path: sys.path.append(TOOLS)       # (battle_ab drops tools/ from the path: tools/dis.py)
    from ctlib import rom, script
    r = rom.load(ROM)
    out = []
    for sc in range(0x200):
        hdr = []
        try:
            script.disassemble(r, sc, out=hdr.append)
            nobj = int(hdr[0].split(':')[1].split()[0])
        except Exception:
            continue
        for o in range(nobj):
            lines = []
            try:
                script.disassemble(r, sc, obj=o, out=lines.append)
            except Exception:
                continue
            fn0, cur = [], None
            for ln in lines:
                if ln.startswith('==== '):
                    cur = ln.split()[2].split('/')[0] == 'fn0'
                    continue
                if cur:
                    m = re.match(r'\s*0x[0-9A-F]+: ((?:[0-9A-F]{2} )+)', ln + ' ')
                    if m: fn0.append([int(b, 16) for b in m.group(1).split()])
            for i, op in enumerate(fn0):
                if op[0] != 0x83 or len(op) < 3: continue
                tx = ty = None
                for nx in fn0[i + 1:i + 6]:
                    if nx[0] == 0x8B and len(nx) >= 3 and nx[1] != 0xFF: tx, ty = nx[1], nx[2]; break
                    if nx[0] == 0x8D and len(nx) >= 5: tx, ty = (nx[1] | nx[2] << 8) >> 8, (nx[3] | nx[4] << 8) >> 8; break
                    if nx[0] == 0x83: break
                out.append((sc, o, op[1], op[2], tx, ty))
    return out


def formations(rows):
    """Group each scene's placed enemies: chained within a screen of each other, distinct slots, at most five."""
    by = {}
    for sc, o, i, op2, tx, ty in rows:
        if tx is None: continue
        by.setdefault(sc, []).append(dict(obj=o, id=i, op2=op2, x=tx, y=ty))
    out = []
    for sc, es in sorted(by.items()):
        left = sorted(es, key=lambda e: e['obj'])
        while left:
            grp = [left.pop(0)]
            grew = True
            while grew:
                grew = False
                for e in list(left):
                    slots = {g['op2'] & 0x7F for g in grp}
                    xs = [g['x'] for g in grp] + [e['x']]; ys = [g['y'] for g in grp] + [e['y']]
                    near = any(max(abs(e['x'] - g['x']), abs(e['y'] - g['y'])) <= PACK for g in grp)
                    fits = max(xs) - min(xs) < SCREEN[0] and max(ys) - min(ys) < SCREEN[1]
                    if near and fits and (e['op2'] & 0x7F) not in slots and len(grp) < len(HOSTS):
                        grp.append(e); left.remove(e); grew = True
            out.append(dict(scene=sc, enemies=grp))
    return out


def catalog(refresh=False):
    if not refresh and os.path.exists(CACHE):
        return json.load(open(CACHE))
    rows = scan_scripts()
    cat = dict(rows=rows, formations=formations(rows))
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    json.dump(cat, open(CACHE, 'w'))
    return cat


def own_op2(eid, cat):
    """The slot operand the enemy's own op 0x83 carries (the first in scene order); 0x03 for an id no script loads."""
    for sc, o, i, op2, tx, ty in cat['rows']:
        if i == eid: return op2
    return 0x03


def fmt_formation(f):
    n = names()
    return '%03X: ' % f['scene'] + ', '.join('obj%d %02X %s (op2 %02X, %d,%d)' % (e['obj'], e['id'], n[e['id']], e['op2'],
                                                                              e['x'], e['y']) for e in f['enemies'])


# ---------------------------------------------------------------------------------------------------- the staging
def layout(enemies):
    """The formation recentred on GATO's tile; a list of ids (no layout) goes in a row two tiles apart."""
    if all(e.get('x') is not None for e in enemies):
        xs = [e['x'] for e in enemies]; ys = [e['y'] for e in enemies]
        cx, cy = (min(xs) + max(xs)) // 2, (min(ys) + max(ys)) // 2
        return [(e['id'], e['op2'], ARENA[0] + e['x'] - cx, ARENA[1] - 1 + e['y'] - cy) for e in enemies]
    n = len(enemies)
    return [(e['id'], e['op2'], ARENA[0] - (n - 1) + 2 * k, ARENA[1] - 1) for k, e in enumerate(enemies)]


def staging_pokes(placed):
    """The WRAM bytes that make gato_wait stage `placed` [(id, op2, tx, ty)] through the field's own op 0x83."""
    if not 1 <= len(placed) <= len(HOSTS):
        raise SystemExit('battle_sim: 1..%d enemies, got %d' % (len(HOSTS), len(placed)))
    pk = {0x1110: 0x80}                                       # GATO (obj8) leaves: C0/0617 stages only mode 5/6
    for k, (eid, op2, tx, ty) in enumerate(placed):
        o = HOSTS[k]
        base = PROG + k * 0x10
        for j, b in enumerate([0x83, eid, op2, 0x8B, tx & 0xFF, ty & 0xFF, 0x89, 0x20, 0xAA, 0x03, 0xB2]):
            pk[base + j] = b
        ip = base - 0x12001                                   # IP = offset from $7F:2001
        pk[0x1180 + 2 * o], pk[0x1181 + 2 * o] = ip & 0xFF, ip >> 8
        pk[0x1100 + 2 * o] = 0x00                             # C0/5A46 / 5ACC skip a mode with bit 7
    return pk


def parse_enemies(spec, cat):
    if spec.startswith('@'):
        sc, o = spec[1:].split(':')
        sc, o = int(sc, 16), int(o)
        for f in cat['formations']:
            if f['scene'] == sc and any(e['obj'] == o for e in f['enemies']):
                return f['enemies'], 'f%03X_%d' % (sc, o)
        raise SystemExit('battle_sim: no catalogued formation holds scene %03X obj %d' % (sc, o))
    ids = [int(x, 16) for x in spec.split(',')]
    return [dict(id=i, op2=own_op2(i, cat)) for i in ids], 'e' + '_'.join('%02X' % i for i in ids)


# ---------------------------------------------------------------------------------------------------- one fight
def cmb_base(c):
    return 0x5E2D + c * 0x80


def regions():
    out = []
    for c in range(ab.COMBATANTS):
        out += [(cmb_base(c) + 3, 6), (cmb_base(c) + 0x1D, 6)]
    out += [(0xA013, 1), (0x95DB, 1), (0xAE93, 1), (0x9877, 1)]
    out += [(0xAF02, 8), (0x993C, 11), (0x2380, 32)]          # the staged ids, each combatant's OBJ palette, pal 4
    return out


def state_at(rows, f):
    st = {}
    for k in sorted(rows):
        if k > f: break
        st.update(rows[k])
    return st


def series(rows, addr, total):
    """{frame: hex} for every frame 0..total (forward-filled from the --diff rows)."""
    out, cur, keys = {}, None, sorted(rows)
    i = 0
    for f in range(total + 1):
        while i < len(keys) and keys[i] <= f:
            if addr in rows[keys[i]]: cur = rows[keys[i]][addr]
            i += 1
        out[f] = cur
    return out


def ids_of(h):
    return [h[i:i + 2] for i in range(0, 16, 2) if h[i:i + 2] != 'FF'] if h else []


def changes(rows, addr, f0, total):
    """[(frame, hex)]: the value at f0 and every change after it, up to total."""
    s = series(rows, addr, total)
    out = [(f0, s[f0])]
    for f in range(f0 + 1, total + 1):
        if s[f] != s[f - 1]: out.append((f, s[f]))
    return out


def staged_at(rows, total):
    s = series(rows, 0xAF02, total)                           # FD/B2A9: the slots' ids, FF in the empty ones
    return next((f for f in range(total + 1) if s[f] and 'FF' in [s[f][i:i + 2] for i in range(0, 16, 2)]), None)


def grade(label, rows_c, rows_p, cth, pth, total, regs=None):
    """Both sides from the frame both have staged: the value sequences compared without their frames (the first
    field whose sequence differs), then the frame offsets of the matching changes, then the picture frame by frame."""
    lines = []
    sc0, sp0 = staged_at(rows_c, total), staged_at(rows_p, total)
    if sc0 is None or sp0 is None:
        return ['sim %s: staged cartridge f%s port f%s -- no fight' % (label, sc0, sp0)]
    f0 = max(sc0, sp0) + 2
    st_c, st_p = state_at(rows_c, f0), state_at(rows_p, f0)
    lines.append('sim %s: staged cartridge %s f%d, port %s f%d' % (label, ids_of(st_c.get(0xAF02)), sc0,
                                                                ids_of(st_p.get(0xAF02)), sp0))
    offs, diffs = [], []
    for a, n in (regs or regions()):
        cc, pp = changes(rows_c, a, f0, total), changes(rows_p, a, f0, total)
        vc, vp = [v for f, v in cc], [v for f, v in pp]
        k = next((i for i in range(min(len(vc), len(vp))) if vc[i] != vp[i]), None)
        if k is None and len(vc) != len(vp): k = min(len(vc), len(vp))
        if k is not None:
            diffs.append((cc[k][0] if k < len(cc) else total, a, k, cc[k] if k < len(cc) else (None, '(none)'),
                          pp[k] if k < len(pp) else (None, '(none)')))
        for (x, _), (y, _) in list(zip(cc, pp))[1:(k if k is not None else None)]:
            offs.append(y - x)
    if not diffs:
        lines.append('sim %s: wram same values in the same order on both sides' % label)
    for f, a, k, (fc, vc_), (fp, vp_) in sorted(diffs):
        lines.append('sim %s: wram $%04X change #%d differs: cartridge f%s %s, port f%s %s' % (label, a, k, fc, vc_, fp, vp_))
    if offs:
        h = {}
        for d in offs: h[d] = h.get(d, 0) + 1
        lines.append('sim %s: port frame - cartridge frame per change: %s' % (
            label, ' '.join('%+d x%d' % kv for kv in sorted(h.items()))))
    def deaths(rows):
        s, out, prev = series(rows, 0xAF02, total), [], None
        for f in range(f0, total + 1):
            cur = len(ids_of(s[f]))
            if prev is not None and cur < prev: out.append(f)
            prev = cur
        return out
    lines.append('sim %s: deaths cartridge %s port %s' % (label, deaths(rows_c), deaths(rows_p)))
    # the death palette: OBJ palette 4's page over the frames a combatant draws with it ($993C = 08), as a sequence
    def pal4(rows):
        p, o = series(rows, 0x2380, total), series(rows, 0x993C, total)
        seq = []
        for f in range(f0, total + 1):
            if o[f] and '08' in [o[f][i:i + 2] for i in range(6, 22, 2)] and (not seq or seq[-1] != p[f]): seq.append(p[f])
        return seq
    qc, qp = pal4(rows_c), pal4(rows_p)
    k = next((i for i in range(min(len(qc), len(qp))) if qc[i] != qp[i]), None)
    lines.append('sim %s: death palette (OBJ pal 4, $2380): cartridge %d states, port %d, %s' % (
        label, len(qc), len(qp), 'the same sequence' if k is None and len(qc) == len(qp) else
        'first differs at state #%s: cartridge %s port %s' % (k, qc[k] if k is not None else '-', qp[k] if k is not None else '-')))
    # the picture: each port frame against the cartridge's within -12..+12 frames
    lag_h, bad = {}, []
    for f in range(f0, total + 1):
        a = pth.get(f)
        if a is None: continue
        best = None
        for L in range(-12, 13):
            b = cth.get(f + L)
            if b is None: continue
            d = int((np.abs(a - b).max(axis=2) > 24).sum())
            if best is None or d < best[0] or (d == best[0] and abs(L - 2) < abs(best[1] - 2)): best = (d, L)
        if best is None: continue
        if best[0] == 0: lag_h[best[1]] = lag_h.get(best[1], 0) + 1
        else: bad.append((f, best[0], best[1]))
    lines.append('sim %s: picture %d frames exact at lags %s; %d with no exact match%s' % (
        label, sum(lag_h.values()), ' '.join('%+d:%d' % kv for kv in sorted(lag_h.items())), len(bad),
        '' if not bad else ' (worst port f%d: %d px)' % max(bad, key=lambda t: t[1])[:2]))
    return lines


def fight(placed, tag, party=(0, 1, 2), inv=None, drive='mash', hp=None, hpat=600, total=1500, verbose=False, mon=(), poke=()):
    # placed == []: gato_wait's own fight (GATO as the scene loaded it) -- the baseline the staging is measured against
    combo = int(drive[6:], 16) if drive.startswith('combo:') else None
    if combo is not None:
        party = ab.COMBO_GROUP[combo]
    ex = dict(staging_pokes(placed)) if placed else {}
    if combo is not None:                                    # battle_ab.combo_pokes: only this combo's learned bit
        if combo < 0x66: ex[0x283E + (combo - 0x39) // 3] = 0x80 >> ((combo - 0x39) % 3)
        else: ex[0x284D + (combo - 0x66)] = 0x80
    pk = ab.pokes_for(list(party), extra=ex)
    if inv:
        for k in range(0x2400, 0x2600): pk.pop(k, None)
        for i, (iid, cnt) in enumerate(inv):
            pk[0x2400 + i], pk[0x2500 + i] = iid, cnt
    mash = lambda f0: [(f, 'a', 2) for f in range(f0, total - 20, 12)]
    if drive == 'mash':
        drv = mash(ab.MENU_AT)
    elif drive.startswith('tech:'):                          # the active PC's k-th tech, then the party attacks
        drv = ab.drive_tech(int(drive[5:]))
        drv += mash(drv[-1][0] + 300)
    elif drive.startswith('item:'):                          # the item in the bag's first row, used, then attacks
        iid = int(drive[5:], 16)
        for k in range(0x2400, 0x2600): pk.pop(k, None)
        pk[0x2400], pk[0x2500] = iid, 5
        drv = ab.drive_item(0)
        drv += mash(drv[-1][0] + 300)
    elif combo is not None:
        drv = ab.drive_combo(ab.combo_ready(pk, len(party)))
        drv += mash(drv[-1][0] + 300)
    else:
        drv = [(int(f), k, 2) for f, k in (x.split(':') for x in drive.split(','))]
    timed = None
    if hp is not None:
        timed = (hpat, {})
        for eid, op2, tx, ty in (placed or [(0x92, 0x86, 0, 0)]):   # the slot op 0x83 gives it: (operand2 & 7F) + 1 - 4 (C0/47AC,
            b = cmb_base(3 + (op2 & 0x7F) - 3) + 3      #   C0/0560), combatant = slot + 3; curHP at +3
            timed[1][b], timed[1][b + 1] = hp & 0xFF, hp >> 8
    if poke:
        if timed is None: timed = (hpat, {})
        for a, v in poke: timed[1][a] = v
    want = regions() + [r for r in mon if r not in regions()]
    # (the scratch files are named by the run's options too: two checks on one formation run side by side in the suite)
    import hashlib
    key = hashlib.sha1(repr((party, inv, drive, hp, hpat, total, sorted(mon), sorted(poke))).encode()).hexdigest()[:8]
    regs, rows_c, rows_p, cth, pth, plog = ab.run(pk, drv, total, 'sim_' + tag + '_' + key, regs=want, raw=True, timed=timed)
    label = tag + ' [' + (' '.join('%02X %s' % (p[0], names()[p[0]]) for p in placed) or 'GATO as loaded') + ']'
    out = grade(label, rows_c, rows_p, cth, pth, total, regs=want)
    if verbose:
        for a in (0xAF02, 0x993C, 0x2380) + tuple(r[0] for r in mon):
            for side, rows in (('cartridge', rows_c), ('port', rows_p)):
                prev = None
                for f in sorted(rows):
                    v = rows[f].get(a)
                    if v is not None and v != prev:
                        out.append('    %s f%d $%04X=%s' % (side, f, a, v)); prev = v
    return '\n'.join(out)


def options(args):
    o = dict(x.split('=', 1) for x in args if '=' in x)
    kw = {}
    if 'party' in o: kw['party'] = tuple(int(x) for x in o['party'].split(','))
    if 'inv' in o: kw['inv'] = [tuple(int(v, 16) for v in kv.split(':')) for kv in o['inv'].split(',')]
    if 'drive' in o: kw['drive'] = o['drive']
    if 'hp' in o: kw['hp'] = int(o['hp'])
    if 'hpat' in o: kw['hpat'] = int(o['hpat'])
    if 'frames' in o: kw['total'] = int(o['frames'])
    if 'poke' in o: kw['poke'] = [(int(a, 16) & 0xFFFF, int(v, 16)) for a, v in (x.split(':') for x in o['poke'].split(','))]
    if 'mon' in o: kw['mon'] = [(int(a, 16) & 0xFFFF, int(n)) for a, n in (x.split(':') for x in o['mon'].split(','))]
    return kw


def main():
    import concurrent.futures
    os.chdir(ROOT)
    argv = sys.argv[1:]
    jobs = int(next((x[2:] for x in argv if x.startswith('-j')), '1') or 1)
    verbose = '-v' in argv
    a = [x for x in argv if not x.startswith('-')]
    if not a:
        print(__doc__); return 1
    cat = catalog(refresh='--refresh' in argv)
    if a[0] == 'catalog':
        want = a[1] if len(a) > 1 else None
        n = names()
        sel = cat['formations']
        if want:
            ids = {int(want, 16)} if re.fullmatch(r'[0-9A-Fa-f]{1,2}', want) else \
                {i for i in range(256) if n[i].lower() == want.lower()}
            sel = [f for f in sel if any(e['id'] in ids for e in f['enemies'])]
        for f in sel: print(fmt_formation(f))
        print('catalog: %d formations, %d op-0x83 objects, %d distinct ids' % (
            len(sel), len(cat['rows']), len({r[2] for r in cat['rows']})))
        return 0
    kw = options(a[1:])
    if a[0] == 'fight':
        if a[1] == 'native':
            print(fight([], 'native', verbose=verbose, **kw), flush=True)
            return 0
        enemies, tag = parse_enemies(a[1], cat)
        print(fight(layout(enemies), tag, verbose=verbose, **kw), flush=True)
        return 0
    if a[0] == 'sweep':
        spec = a[1] if len(a) > 1 and '=' not in a[1] else 'all'
        ids = sorted({r[2] for r in cat['rows']}) if spec == 'all' else [int(x, 16) for x in spec.split(',')]
        work = [([dict(id=i, op2=own_op2(i, cat))], 'e%02X' % i) for i in ids]
    elif a[0] in ('techs', 'items'):
        enemies, tag = parse_enemies(a[1], cat)
        placed = layout(enemies)
        if a[0] == 'techs':
            chars = [int(x) for x in a[2].split(',')] if len(a) > 2 and '=' not in a[2] else range(7)
            work = [(placed, '%s_c%dt%d' % (tag, c, k), dict(kw, party=(c,), drive='tech:%d' % k))
                    for c in chars for k in range(8)]
        else:
            work = [(placed, '%s_i%02X' % (tag, i), dict(kw, party=(0,), drive='item:%02X' % i)) for i in ab.ITEMS]
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as ex:
            for text in ex.map(lambda w: fight(w[0], w[1], verbose=verbose, **w[2]), work):
                print(text, flush=True)
        return 0
    elif a[0] == 'formations':
        work = [(f['enemies'], 'f%03X_%d' % (f['scene'], f['enemies'][0]['obj'])) for f in cat['formations']]
    else:
        print(__doc__); return 1
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as ex:
        for text in ex.map(lambda w: fight(layout(w[0]), w[1], verbose=verbose, **kw), work):
            print(text, flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
