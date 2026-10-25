#!/usr/bin/env python3
r"""The 600 A.D. inn's bedroom with Crono and Lucca, port against the rule's cartridge, frame by frame.

    python tools/inn_wake_cmp.py [frames]

The bedroom (loc 0x75) puts each PC in the bed `$7E1102/04/06` names and obj0 wakes them with the PARTY-SLOT
calls `05 00 13` / `07 02 13` (C0/61E9 / 6214): the target is the object HOSTING slot n, `$0197 + n` -- with
Crono and Lucca, slot 1 is obj3 ($0199 = 06), not obj2 (Marle's empty object). The live runner used to take
1 + n / 2, so Lucca never ran her fn3 (sit up, walk out) and the release op 0xDA walked her from the bed.
The user's report: "when sleeping at the inn with lucca in the party, there was a bug" (saves/field_re/inn_lucca_007.state).

  cartridge  build/ct.exe --peek saves/field_re/inn600_yesno.state (the rule): the inn's night (loc 0x10F)
             after Yes, A pressed once, PEEK_THUMBS every frame. The state (tools/recipes/fxr_inn600_yesno.json)
             is the Cathedral's south door (story 0x12, Crono + Lucca) teleported to loc 0x74 (--teleport 0x74 11 28
             0 --method jmp --frames 240 -> inn600_tele.state), then `10:16,0:30,100:2,0:120,100:2,0:120,0:10`.
  port       build/Belthasar.exe --loadstate saves/field_re/inn_lucca_007.state (its own CtScene state at the
             "Register? Yes" prompt, made from fxr_inn600_prompt's WRAM; until 2026-10-22 the user's F5 state),
             A at f5, CT_SHOTS_THUMBS every frame, the SPC seeded from
             inn600_tele.state under the rule (CT_AUDIO_SEEDSTATE): the song load `EA 02` costs the SPC's
             44 frames on both sides (a cold driver priced it at 4).

Aligned on each side's first lit frame of loc 0x75:

    inn-wake: first lit c<C> p<P>; <n> frames compared, <k> differ [first at +<d>]
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', '_inn_wake')
REC = 6 + 128 * 112 * 3


def load(path):
    d = open(path, 'rb').read()
    return [(struct.unpack_from('<IH', d, i), d[i + 6:i + REC]) for i in range(0, len(d) - REC + 1, REC)]


def first_lit(recs, loc):
    for k, ((f, l), px) in enumerate(recs):
        if l == loc and sum(px[::97]) > 2000:
            return k
    return None


def main():
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 800
    os.makedirs(OUT, exist_ok=True)
    ct, pt = os.path.join(OUT, 'c.th'), os.path.join(OUT, 'p.th')
    for f in (ct, pt):
        if os.path.exists(f):
            os.remove(f)
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1', PEEK_THUMBS=ct, PEEK_THUMBS_EVERY='1')
    subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--peek', 'saves/field_re/inn600_yesno.state',
                    os.path.join(OUT, 'c.raw'), '--btnscript', '100:2,0:%d' % (frames + 300)],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_ORACLE_NMION_HOLD='1', CT_ORACLE_IFAST='1', CT_AUDIO_SEEDSTATE='saves/field_re/inn600_tele.state',
               CT_SHOTS_THUMBS=pt, CT_SHOTS_THUMBS_EVERY='1')
    subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '116',
                    '--loadstate', 'saves/field_re/inn_lucca_007.state', '--walkscript', 'n:5,a:1,n:%d' % (frames + 400),
                    '--selftest', os.path.join(OUT, 'p.raw'), '--frames', str(frames + 400)],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(ct) or not os.path.exists(pt):
        print('inn-wake: FAILED -- a side wrote no thumbnails')
        return 1
    c, p = load(ct), load(pt)
    ci, pi = first_lit(c, 0x75), first_lit(p, 0x75)
    if ci is None or pi is None:
        print('inn-wake: FAILED -- no lit loc 075 (cartridge %s, port %s)' % (ci, pi))
        return 1
    n = min(frames, len(c) - ci, len(p) - pi)
    diff = [d for d in range(n) if c[ci + d][1] != p[pi + d][1]]
    print('inn-wake: first lit c%d p%d; %d frames compared, %d differ%s'
          % (c[ci][0][0], p[pi][0][0], n, len(diff), (' first at +%d' % diff[0]) if diff else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
