#!/usr/bin/env python3
r"""The field's sprite data, port against cartridge, frame by frame: the OAM mirror and the OBJ tiles.

    python tools/field_objdata_cmp.py <leg> <f0> <f1> [--at N] [--off K] [--vram LO-HI] [-v]

Both sides play leg <leg>'s pad from its start state (or, with --at N, from the cartridge's state cut on frame N of the
leg), and on every frame f0..f1 the tool compares

    the OAM mirror   $7E:0700-$091F   (what C0/B309's stage 2 builds and the NMI ships to OAM)
    the OBJ tiles    the 8x8 tiles the cartridge's visible OAM entries use (OBSEL 00 in the field: 8 / 16 pixel sprites,
                     names from VRAM word 0, tile t at word t * 16), and the whole area VRAM words LO-HI as a second
                     number (default 0000-1FFF: what C0/B1B2's queue uploads, unused leftovers included)

The cartridge's dumps are --peek's PEEK_SHOTS_SIDECARS (.wram / .vram after frame f); the port's are Belthasar's
CT_DUMPLIST (.wram / .vram on its frame f + K, K = --off, default -1: the port's dump is a tick on, as
build/_pt77/scratch_tools/s28_wramdiff.py found). The port's VRAM image starts as the cartridge's (CT_SEED_VRAM: a WRAM
seed carries none). Prints

    objdata: <leg> f<f0>-f<f1>[ from f<N>]: OAM <n> of <m> frames equal, OBJ tiles <n> of <m> frames equal[; first ...]

This is the gate of the field sprite renderer's rewrite (stage 1): draw from these only where they are exact.
"""
import os
import subprocess
import sys


def visible(oam, e):
    """Whether OAM entry e of a 544-byte image reaches the 256 x 224 picture."""
    x, y = oam[e * 4], oam[e * 4 + 1]
    hi = (oam[0x200 + (e >> 2)] >> ((e & 3) * 2)) & 3
    size = 16 if hi & 2 else 8
    x |= (hi & 1) << 8
    return (y < 224 or y + size > 256) and (x < 256 or x + size > 512)


def entry(oam, e):
    return bytes(oam[e * 4:e * 4 + 4]) + bytes([(oam[0x200 + (e >> 2)] >> ((e & 3) * 2)) & 3])


def used_words(oam):
    """The VRAM word addresses of the 8x8 tiles the visible entries of a 544-byte OAM image use (OBSEL 00)."""
    out = set()
    for e in range(128):
        x, y, name, attr = oam[e * 4], oam[e * 4 + 1], oam[e * 4 + 2], oam[e * 4 + 3]
        hi = (oam[0x200 + (e >> 2)] >> ((e & 3) * 2)) & 3
        size = 16 if hi & 2 else 8
        x |= (hi & 1) << 8
        if not (y < 224 or y + size > 256):
            continue
        if not (x < 256 or x + size > 512):
            continue
        t = name | ((attr & 1) << 8)
        for dy in range(size // 8):
            for dx in range(size // 8):
                col = ((t & 0x0F) + dx) & 0x0F
                row = (((t >> 4) & 0x0F) + dy) & 0x0F
                base = (t & 0x100) * 16 + (row * 16 + col) * 16
                out.update(range(base, base + 16))
    return out

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
os.chdir(ROOT)
import playthrough as P            # noqa: E402
import leg_window_cmp as L         # noqa: E402

WORK = os.environ.get('OBJDATA_TMP') or os.path.join(ROOT, 'build', '_objdata')


def arg(name, default):
    if name in sys.argv:
        k = sys.argv.index(name)
        return sys.argv[k + 1]
    return default


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    leg, f0, f1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    at = int(arg('--at', '0')); off = int(arg('--off', '-1'))
    vlo, vhi = (int(v, 16) for v in arg('--vram', '0000-1FFF').split('-'))
    verbose = '-v' in sys.argv
    os.makedirs(WORK, exist_ok=True)
    for fn in os.listdir(WORK):
        if fn.startswith(('c_f', 'p_f')):
            os.remove(os.path.join(WORK, fn))
    legs = P.route()
    i = P.find(legs, leg); lg = legs[i]
    if at:
        state = L.cart_state(legs, i, at)
        pad = L.cut(lg['pad'], at, f1 - at + 8)
        prev = L.cut(lg['pad'], at - 1, 1)
        keys = L.port_keys(prev, pad)
        rel = lambda f: f - at                                      # noqa: E731
    else:
        state = P.start_of(legs, i)
        pad = L.cut(lg['pad'], 0, f1 + 8)
        keys = None
        rel = lambda f: f                                           # noqa: E731
    seed = os.path.join(WORK, 'seed.raw')
    subprocess.run([P.CT, '--peek', state, seed, '--btnscript', '0:0'], cwd=ROOT, env=P.RULE, capture_output=True)
    w = open(seed + '.wram', 'rb').read()
    lw = os.path.join(WORK, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram' + bytes([10]) + b'RAM:131072:' + w)
    loc = (w[0x100] | (w[0x101] << 8)) & 0x1FF
    frames = list(range(f0, f1 + 1))
    pfr = [rel(f) + off for f in frames]
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=state, CT_WS_BATTLE_LEAD='1', CT_SRAM_FRESH='1', CT_SRAM=os.path.join(WORK, 'port.srm'),
               CT_SEED_VRAM=seed + '.vram', CT_DUMPLIST=os.path.join(WORK, 'p'),
               CT_DUMPLIST_AT='%d-%d' % (min(pfr), max(pfr)),
               CT_SHOTS=os.path.join(WORK, 'shot'), CT_SHOTS_AT=str(max(pfr) + 2), CT_SHOTS_EXIT='1')
    if P.seed_in_load_fade(state, w):
        env['CT_SEED_LOADFADE'] = '1'
    if keys is None:
        wf = os.path.join(WORK, 'walk.txt')
        open(wf, 'w').write(P.walkscript(pad, P.last_key(legs, i)))
        pargs = ['--walkfile', wf]
    else:
        pargs = ['--walkscript', keys]
    pp = subprocess.Popen([P.FIELD, '--assets', 'ct_assets.dat', '--scene', str(loc), '--loadwram', lw] + pargs, cwd=ROOT,
                          env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    envc = dict(P.RULE, PEEK_SHOTS=os.path.join(WORK, 'c'), PEEK_SHOTS_AT=','.join(str(rel(f)) for f in frames),
                PEEK_SHOTS_SIDECARS='1')
    subprocess.run([P.CT, '--peek', state, os.path.join(WORK, 'c_end.raw'), '--frames', str(rel(f1) + 2),
                    '--btnscript', P.btnscript(pad) or '0:0'], cwd=ROOT, env=envc, capture_output=True)
    pp.wait()
    oam_ok = vr_ok = all_ok = vis_ok = n = 0
    first_oam = first_vr = None
    for f in frames:
        cw = os.path.join(WORK, 'c_f%d.raw.wram' % rel(f)); cv = os.path.join(WORK, 'c_f%d.raw.vram' % rel(f))
        pw = os.path.join(WORK, 'p_f%d.wram' % (rel(f) + off)); pv = os.path.join(WORK, 'p_f%d.vram' % (rel(f) + off))
        if not all(os.path.exists(x) for x in (cw, cv, pw, pv)):
            print('objdata: f%d missing a dump (%s)' % (f, ', '.join(os.path.basename(x) for x in (cw, cv, pw, pv) if not os.path.exists(x))))
            continue
        n += 1
        c, p = open(cw, 'rb').read(), open(pw, 'rb').read()
        od = [a for a in range(0x0700, 0x0920) if c[a] != p[a]]
        cvb, pvb = open(cv, 'rb').read(), open(pv, 'rb').read()
        co, po = c[0x0700:0x0920], p[0x0700:0x0920]
        if all(entry(co, e) == entry(po, e) for e in range(128) if visible(co, e) or visible(po, e)):
            vis_ok += 1
        allv = [a for a in range(vlo * 2, (vhi + 1) * 2) if cvb[a] != pvb[a]]
        vd = sorted(2 * wa + k for wa in used_words(c[0x0700:0x0920]) for k in (0, 1) if cvb[2 * wa + k] != pvb[2 * wa + k])
        if not allv:
            all_ok += 1
        if not od:
            oam_ok += 1
        elif first_oam is None:
            first_oam = (f, od)
        if not vd:
            vr_ok += 1
        elif first_vr is None:
            first_vr = (f, vd)
        if verbose and (od or vd):
            print('  f%d: OAM %d byte(s) differ%s; OBJ tiles %d byte(s)%s' % (
                f, len(od), (' (first $%04X cart %02X port %02X)' % (od[0], c[od[0]], p[od[0]])) if od else '',
                len(vd), (' (first word %04X)' % (vd[0] // 2)) if vd else ''))
    tail = ''
    if first_oam:
        a = first_oam[1][0]; e = (a - 0x0700) // 4 if a < 0x0900 else None
        tail += '; first OAM difference f%d at $%04X%s' % (first_oam[0], a, (' (entry %d)' % e) if e is not None else ' (high table)')
    if first_vr:
        tail += '; first OBJ tile difference f%d at word %04X' % (first_vr[0], first_vr[1][0] // 2)
    print('objdata: %s f%d-f%d%s: OAM %d of %d frames equal, OBJ tiles in use %d of %d frames equal%s' % (
        lg['name'].split('_')[0], f0, f1, (' from f%d' % at) if at else '', oam_ok, n, vr_ok, n, tail))
    print('objdata: (the visible entries: %d of %d frames equal; the whole OBJ area %04X-%04X: %d of %d frames equal)' % (
        vis_ok, n, vlo, vhi, all_ok, n))


if __name__ == '__main__':
    main()
