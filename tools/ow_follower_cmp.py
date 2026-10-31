#!/usr/bin/env python3
r"""The overworld's party follower (class $3AE2, C2/3AE2) against the rule's cartridge, frame by frame.

    python tools/ow_follower_cmp.py [frames] [party3]

Crono and Lucca leave the 600 A.D. cathedral (saves/field_re/catedral_user.state, story 0x12: loc 0x81 at the
door) and walk the overworld: down out of the door, then south, east, back west and north with pauses -- far
enough that Lucca leaves her 16-px box and walks the trail (phases 2 and 3, the corners, the re-decisions). On world 1 the
world's bootstrap puts the lead (class $3404) in block $0DF0 and the two follower blocks (class $3AE2, +24 = the
party slot) in $0E30 / $0E70; Lucca is slot 1. The follower walks the lead's trail ring ($1B5A / $1B5B / $1B7B,
pushed by C2/3934 on every step arrival) one entry behind, re-deciding every 8 frames on a cell edge.

  cartridge  build/ct.exe --monitor (the rule: CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1) from
             saves/field_re/catedral_user.state with the drive's --btnscript
  port       build/Belthasar.exe --scene 129 --loadwram saves/field_re/catedral_user.lakewram (the same WRAM) with
             the same drive as a --walkscript, CT_FIELD_MEMMON on the same bytes

`party3` adds a third member: Marle in party slot 2 (`$2982 = 01`, PEEK_POKE on the cartridge, the same byte in
saves/field_re/catedral_party3.lakewram for the port). Her follower block is $0E70 (slot 2's constants: the 32-px box,
the ring entry before Lucca's, `$02B3`), compared on the same fields.

Both sides are aligned on the lead's first X change on the overworld (loc $1F1), and compared over `frames`:
the lead's X/Y, the follower's X/Y, facing +28, phase +02 and anim pointer +0B, and the ring cursor.

    ow-follower: aligned c<C> p<P>; <n> frames, <k> differ [first at +<d>: field ...]
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', '_ow_follower')
# (name, address, length) -- the lead block $0DF0 and Lucca's $0E30 on world 1
FIELDS = [('leadX', 0x0E04, 2), ('leadY', 0x0E08, 2), ('folX', 0x0E44, 2), ('folY', 0x0E48, 2),
          ('folFace', 0x0E58, 1), ('folPhase', 0x0E32, 1), ('folAnim', 0x0E3B, 2), ('ring', 0x1B5A, 1),
          ('loc', 0x0100, 2)]
# the drive: (direction, frames); '.' = no button
DRIVE = [('d', 80), ('.', 120), ('d', 48), ('r', 64), ('.', 20), ('d', 32), ('l', 40), ('.', 12), ('u', 24),
         ('r', 24), ('.', 60), ('l', 56), ('u', 16), ('.', 40)]
FIELDS3 = [('fol2X', 0x0E84, 2), ('fol2Y', 0x0E88, 2), ('fol2Face', 0x0E98, 1), ('fol2Phase', 0x0E72, 1),
           ('fol2Anim', 0x0E7B, 2)]
MASK = {'u': 0x10, 'd': 0x20, 'l': 0x40, 'r': 0x80, '.': 0}


def parse(lines, rx):
    rows, cur = [], {}
    for l in lines:
        m = re.match(rx, l)
        if not m:
            continue
        f = int(m.group(1))
        for k, v in re.findall(r'(?:7E)?([0-9A-F]{4})=([0-9A-F]+)', l):
            cur[k] = v
        rows.append((f, dict(cur)))
    return rows


def dense(rows):
    out, last = {}, {}
    if not rows:
        return out
    j = 0
    for f in range(rows[0][0], rows[-1][0] + 1):
        while j < len(rows) and rows[j][0] <= f:
            last = rows[j][1]
            j += 1
        out[f] = last
    return out


def main():
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    party3 = 'party3' in sys.argv[2:]
    fields = FIELDS + (FIELDS3 if party3 else [])
    lw = 'saves/field_re/catedral_party3.lakewram' if party3 else 'saves/field_re/catedral_user.lakewram'
    os.makedirs(OUT, exist_ok=True)
    def run(drive, port):
        total = sum(n for _, n in drive) + 60
        if not port:
            mon = ','.join('7E%04X:%d' % (a, n) for _, a, n in fields)
            btn = ','.join('%X:%d' % (MASK[d], n) for d, n in drive) + ',0:60'
            env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1')
            if party3:
                env['PEEK_POKE'] = '2982=01'
            return subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--monitor', mon, '--diff', '--from',
                                   'saves/field_re/catedral_user.state', '--btnscript', btn],
                                  cwd=ROOT, env=env, capture_output=True, text=True).stdout.splitlines()
        pmon = ','.join('%04X:%X' % (a, n) for _, a, n in fields)
        ws = ','.join('%s:%d' % (d, n) for d, n in drive) + ',.:60'
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_FIELD_MEMMON=pmon)
        return subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene',
                               '129', '--loadwram', lw, '--walkscript', ws,
                               '--selftest', os.path.join(OUT, 'p.raw'), '--frames', str(total)],
                              cwd=ROOT, env=env, capture_output=True, text=True).stdout.splitlines()

    def world_init(D):
        # the frame the follower block is first placed on loc $1F1 (its init, C2/3BC2)
        for f in sorted(D):
            if D[f].get('0100') == 'F101' and D[f].get('0E44') not in (None, '8080', '0000'):
                return f
        return None
    # The field -> overworld entry itself is not this check's subject (the port's comes up 31 frames sooner than the
    # cartridge's from this door): each side's walk starts the same number of frames after its own world init.
    c = run(DRIVE, False)
    ci = world_init(dense(parse(c, r'\s+f(\d+)\s')))
    p0 = run(DRIVE[:2], True)
    pi = world_init(dense(parse(p0, r'fmemmon f(\d+)\s')))
    if ci is None or pi is None:
        print('ow-follower: FAILED -- no world init (cartridge %s, port %s)' % (ci, pi))
        return 1
    pd = list(DRIVE)
    # +1: the port takes a --walkscript press on its own frame, the cartridge on the next auto-joypad read
    # (docs/DEBUG_SURFACE.md, CT_9923DBG) -- so the port's walk is queued one frame later to press on the same frame.
    pd[1] = ('.', DRIVE[1][1] - (ci - pi) + 1)
    p = run(pd, True)
    C = dense(parse(c, r'\s+f(\d+)\s'))
    P = dense(parse(p, r'fmemmon f(\d+)\s'))

    def first_move(D):
        # the lead's first step on loc $1F1: its X or Y changes from a placed position (not the blocks' 8080 / 0000 fill)
        prev = None
        for f in sorted(D):
            d = D[f]
            x = (d.get('0E04'), d.get('0E08'))
            if d.get('0100') != 'F101' or x[0] in (None, '8080', '0000'):
                prev = None
                continue
            if prev is not None and x != prev:
                return f
            prev = x
        return None
    cf, pf = first_move(C), first_move(P)
    if cf is None or pf is None:
        print('ow-follower: FAILED -- no lead move on loc 1F1 (cartridge %s, port %s)' % (cf, pf))
        return 1
    n, bad = 0, []
    for d in range(-8, frames):
        if cf + d not in C or pf + d not in P:
            break
        n += 1
        a, b = C[cf + d], P[pf + d]
        for name, addr, _ in fields:
            k = '%04X' % addr
            if a.get(k) != b.get(k):
                bad.append((d, name, a.get(k), b.get(k)))
                break
    first = (' first at %+d: %s cartridge %s port %s' % bad[0]) if bad else ''
    print('ow-follower%s: aligned c%d p%d; %d frames, %d differ%s' % ('-party3' if party3 else '', cf, pf, n, len(bad), first))
    return 0


if __name__ == '__main__':
    sys.exit(main())
