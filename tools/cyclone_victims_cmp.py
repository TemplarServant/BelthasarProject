#!/usr/bin/env python3
r"""Crono's Cyclone on Blue Imps + Roly (loc 070), every victim's damage, port against the rule's cartridge.

    python tools/cyclone_victims_cmp.py

The user, 2026-10-09: "in the battle against roly and blue imps, when using cyclone on two enemies, only one of them took
damage". Bit 1 of $B1FC is the one-victim latch: an enemy's physical attack sets it (AI op 01, C1/99C2) and C1/D357 reads
it after every victim of a kind-3 damage loop. The PC command's tech arm clears it (C1/BB66-BB6B `LDA $B1FC / AND #FD /
STA $B1FC`); the port did that only for the demo's commands, so a Cyclone dispatched after a Blue Imp's attack stopped
at its first victim (the list is processed last-first: the Roly took the damage, the Imps none). The victims themselves
are the $99C0 the dispatch's area pass rebuilds around the action's target (C1/C586 -> C1/C7A7 + C1/2701) and
C1/C600-C63E copies into $AD8E / $AECC (battle_multitarget.md sec. 3.1, tools/radius_dispatch_cmp.py).

The cartridge: saves/battle_re/loc070_imps_roly_pre_lake.state -- loc 070 reloaded by the ROM's own loader after the
bat6 fight was won (its group of two Blue Imps + a Roly respawned), Crono 60 frames into a walk down onto them. Down for
60 frames (the ambush, obj22's fn1), then Down, A, A, A from the cartridge frame 331 (Tech, Cyclone, the primary =
enemy slot 2): the select confirms while a Blue Imp's attack is running (its AI op 01 sets $B1FC bit 1), and the
Cyclone dispatches after it. The port: the same WRAM (saves/battle_re/loc070_imps_roly_pre.lakewram, --loadwram) and
SPC, the same walk and the same keys on the same frames.

The ambush itself is graded first (2026-10-09): obj22's fn1 `E3 00 / DA / E3 00 / AD 05 ...` meets op 0xDA with no
follower in party slot 1, and C0/3463 `INX / CLC` ENDS the visit there; the live VM kept running, reached the wait 4
frames early (obj22 runs every 4th frame) and the fight began 8 frames early. Per side: obj22's IP ($11AC), the three
enemies' X/Y, loc[6] / loc[7] ($7F020C / $7F020E), the D8's $2989 and Crono's ready ($A6D9), change by change:

    cyclone-victims: ambush <region> identical (<n> changes)      (or: differs from f<F>: cartridge .. port ..)
    cyclone-victims: Crono ready ($A6D9 = 00) cartridge f<C> port f<P>

    cyclone-victims: list cartridge <ids> port <ids>
    cyclone-victims: cmb<k> took damage cartridge <yes|no> port <yes|no>  (dead: cartridge <y|n> port <y|n>)
    cyclone-victims: <n>/<m> victims identical
    cyclone-victims-info: cmb<k> HP cartridge <a>-><b> port <c>-><d>       (not graded: the RNG's cursor)
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'ct_field.exe')
STATE = os.path.join(ROOT, 'saves', 'battle_re', 'loc070_imps_roly_pre_lake.state')
LAKEWRAM = os.path.join(ROOT, 'saves', 'battle_re', 'loc070_imps_roly_pre.lakewram')
OUT = os.path.join(ROOT, 'build', '_cyclone_victims')
WALK = 60                     # Down onto the group (the ambush at f42 on both sides)
KEYS_C = 331                  # the cartridge frame of the menu's Down (Crono ready f271 + 60)
KEYS_P = KEYS_C               # the port's: the same frame (Crono ready f271 on both sides)
FRAMES = 1100
# the three enemy blocks' curHP: slot 2 / slot 3 (the Blue Imps) and slot 6 (the Roly) -> combatants 5, 6, 9
HP = [(5, 0x60B0), (6, 0x6130), (9, 0x62B0)]


# Graded up to the D8 (f130): after it the battle's boot rewrites these arrays on its own schedule (the port stages the
# fight's WRAM at once, the cartridge over its boot's frames -- nothing on screen differs; Crono's ready is an event).
# obj22's IP from f42: on f38 the cartridge mirrors the called fn1's first byte (035B), the $1180 mirror no check grades.
AMBUSH = [('obj22 ip', 0x11AC, 2, 42), ('obj23 x', 0x182E, 2, 0), ('obj23 y', 0x18AE, 2, 0), ('obj24 x', 0x1830, 2, 0),
          ('obj24 y', 0x18B0, 2, 0), ('obj25 x', 0x1832, 2, 0), ('obj25 y', 0x18B2, 2, 0), ('loc[6]', 0x1020C, 1, 0),
          ('loc[7]', 0x1020E, 1, 0), ('D8 $2989', 0x2989, 1, 0)]
AMBUSH_END = 131


def regs():
    return [(0xAD8D, 1), (0xAD8E, 4)] + [(a, 2) for _, a in HP] + [(a, n) for _, a, n, _ in AMBUSH] + [(0xA6D9, 1)]


def parse(text, port):
    rx = re.compile(r'fmemmon f(\d+) loc=\w+\s+(.*)') if port else re.compile(r'\s+f(\d+)\s+(.*)')
    rows = []
    for l in text.splitlines():
        m = rx.match(l)
        if not m:
            continue
        vals = {}
        for k, v in re.findall(r'([0-9A-F]+)=([0-9A-F]+)', m.group(2)):
            a = int(k, 16)
            vals[(a & 0xFFFF) | (0x10000 if (a >> 16) in (0x01, 0x7F) else 0)] = v
        rows.append((int(m.group(1)), vals))
    return rows


def word(v):
    return int(v[2:4] + v[0:2], 16)


def outcome(rows, after):
    """The Cyclone's dispatch: the first $AD8E list (all enemies, $AD8D of them) from frame `after` on; each block's HP
    on that frame (the damage lands at the span's end) and at the run's end."""
    lst, at_hp, cur = None, {}, {}
    for f, v in rows:
        for c, a in HP:
            if a in v:
                cur[c] = word(v[a])
        if lst is None and f >= after and 0xAD8E in v and 0xAD8D in v:
            n = int(v[0xAD8D], 16)
            ids = [int(v[0xAD8E][i:i + 2], 16) for i in range(0, 8, 2)][:n]
            if n >= 1 and all(3 <= x < 11 for x in ids):
                lst, at_hp = ids, dict(cur)
    return lst, at_hp, cur


def main():
    os.makedirs(OUT, exist_ok=True)
    keys = '20:2,0:10,100:2,0:10,100:2,0:10,100:2'
    cs = '20:%d,0:%d,%s,0:%d' % (WALK, KEYS_C - WALK, keys, FRAMES - KEYS_C - 38)
    spec = ','.join('%06X:%d' % ((0x7F0000 | (a & 0xFFFF)) if a >= 0x10000 else (0x7E0000 | a), n) for a, n in regs())
    rule = dict(os.environ, CT_ORACLE_LOGICAL='1')
    mon = subprocess.run([CT, '--monitor', spec, '--diff', '--from', STATE, '--btnscript', cs, '--frames', str(FRAMES)],
                         cwd=ROOT, env=rule, capture_output=True, text=True, errors='replace').stdout
    cart = parse(mon, False)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_SEEDSTATE=STATE,
               CT_BATTLE_SCRIPT_ABS='1',
               CT_BATTLE_SCRIPT='n:%d,d:2,n:10,a:2,n:10,a:2,n:10,a:2,n:%d' % (KEYS_P, FRAMES - KEYS_P - 38),
               CT_FIELD_MEMMON=','.join(('%05X:%X' if a >= 0x10000 else '%04X:%X') % (a, n) for a, n in regs()),
               CT_SHOTS=os.path.join(OUT, 'p'), CT_SHOTS_AT=str(FRAMES), CT_SHOTS_EXIT='1')
    out = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '112', '--loadwram', LAKEWRAM,
                          '--walkscript', 'd:%d' % WALK], cwd=ROOT, env=env, capture_output=True, text=True,
                         errors='replace', timeout=600).stdout
    port = parse(out, True)
    good_a = 0
    for name, a, _, start in AMBUSH:
        seqs = []
        for rows in (cart, port):
            q = []
            for f, v in rows:
                if start <= f <= AMBUSH_END and a in v and (not q or q[-1][1] != v[a]):
                    q.append((f, v[a]))
            seqs.append(q)
        c, p = seqs
        if c == p:
            good_a += 1
            print('cyclone-victims: ambush %s identical (%d changes)' % (name, len(c) - 1))
            continue
        k = 0
        while k < min(len(c), len(p)) and c[k] == p[k]:
            k += 1
        cf_ = c[k] if k < len(c) else None
        pf_ = p[k] if k < len(p) else None
        f = min(x[0] for x in (cf_, pf_) if x)
        print('cyclone-victims: ambush %s differs from f%d: cartridge %s port %s' % (
            name, f, '%s@f%d' % (cf_[1], cf_[0]) if cf_ else 'none', '%s@f%d' % (pf_[1], pf_[0]) if pf_ else 'none'))
    print('cyclone-victims: ambush %d/%d regions identical' % (good_a, len(AMBUSH)))
    ready = [next((f for f, v in rows if f > AMBUSH_END and v.get(0xA6D9) == '00'), None) for rows in (cart, port)]
    print('cyclone-victims: Crono ready ($A6D9 = 00) cartridge %s port %s' % tuple(
        'f%d' % r if r is not None else 'never' for r in ready))
    cl, cf, cz = outcome(cart, KEYS_C)
    pl, pf, pz = outcome(port, KEYS_P)
    fmt = lambda l: ' '.join('%02X' % x for x in l) if l else 'none'
    print('cyclone-victims: list cartridge %s port %s' % (fmt(cl), fmt(pl)))
    good = 0
    for c, _ in HP:
        ch = cf.get(c) is not None and cz.get(c, 0) < cf[c]
        ph = pf.get(c) is not None and pz.get(c, 0) < pf[c]
        cd, pd = cz.get(c) == 0, pz.get(c) == 0
        if ch == ph and cd == pd:
            good += 1
        print('cyclone-victims: cmb%d took damage cartridge %s port %s  (dead: cartridge %s port %s)' % (
            c, 'yes' if ch else 'no', 'yes' if ph else 'no', 'y' if cd else 'n', 'y' if pd else 'n'))
    print('cyclone-victims: %d/%d victims identical' % (good, len(HP)))
    for c, _ in HP:
        print('cyclone-victims-info: cmb%d HP cartridge %s->%s port %s->%s' % (c, cf.get(c), cz.get(c), pf.get(c), pz.get(c)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
