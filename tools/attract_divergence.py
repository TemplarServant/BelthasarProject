#!/usr/bin/env python3
r"""Every frame of the attract demo, port against cartridge: WHERE they diverge, and of WHAT kind.

    python tools/attract_divergence.py [--out DIR] [--vehicle lockstep|selftest|boot] [--fresh-port] [--fresh-cart]
                                        [--visit LOC] [--selftest]

The two sides are captured once per frame, as 128x112 thumbnails (the same record both harnesses write:
[u32 frame][u16 $0100][128*112*3 RGB], 2x2 SNES pixels averaged):

    cartridge  build/ct.exe --peek saves/attract_cena_inicial_rule.state --frames 9110   PEEK_THUMBS / _EVERY=1, CT_ORACLE_LOGICAL=1
               (the attract from power-on + 2159 under the rule, made by a recipe: tools/fixtures/attract_rule.json; until
               2026-10-22 the owner's real-time capture saves/attract_cena_inicial.state, another history)
    port       build/Belthasar.exe --assets ct_assets.dat --scene 433                 CT_SHOTS_THUMBS / _EVERY=1
               (interactive, CT_AUDIO_LOCKSTEP=1, stopped at f11400 by CT_SHOTS_EXIT -- the numbering
               tools/frame_region_cmp.py and every attract check use)

The cartridge capture never changes and is cached under build/_divergence/cache; the port's is re-taken
whenever build/Belthasar.exe or ct_assets.dat changes (or with --fresh-port).

--vehicle selftest captures the port from `--selftest --frames 11400` instead. That run orders a transition's frames
differently from the interactive one -- the curtain-close blink (disasm/notes/field_window_wipe.md) showed ONLY
there -- but it fights the demo battles synchronously, so its battle legs come out "not comparable". Run both when
hunting a one-frame glitch the user saw.

Then, for every pair of visits (consecutive frames with the same $0100, paired in order by location):

1. ALIGN. The two timelines are not frame-aligned -- the port boots through the title, a leg can hold a load
   window or an action for more or fewer frames -- so each visit is aligned by dynamic time warping: a
   monotonic path through (cartridge frame, port frame) that minimises the picture difference, a hold on either
   side costing a small penalty so that equal pictures (black, a still map) stay on the diagonal. Every frame is
   in the search, so an ODD offset is found (the survey's every-second-frame thumbnails could not see one).
2. TIMING. The path's offset, port frame minus cartridge frame, at every cartridge frame: where a leg's clock
   drifts, and by how much. A timing difference is NOT reported as a picture difference.
3. PICTURE. At the aligned pair, the share of thumbnail pixels whose largest channel differs by more than 24.
   Runs of frames above the threshold become RENDER events, each with its worst frame, the screen region the
   difference sits in, and a hint: black-vs-picture, brightness (same picture, other level), colour (same
   shape, other palette) or shape.
4. FLASH. A one-frame brightness spike on one side that the other side does not have within two aligned
   frames -- the class of the curtain blinks (a whole lit frame where the cartridge is black).

Writes <out>/report.txt, <out>/events.json and <out>/report.html (the timeline, the visits and every event
with the cartridge frame, the port frame and their difference, embedded). Each event carries the command that
confirms it at full resolution with tools/frame_region_cmp.py.

--selftest runs the alignment and the detectors on a synthetic pair (a known hold, a known recolour and a
known one-frame flash) and prints `attract-divergence: selftest PASS` or what it missed.

The assistant that drives this is .claude/agents/attract-divergence.md.
"""
import argparse
import base64
import json
import os
import struct
import subprocess
import sys
import time
import zlib

# tools/dis.py would shadow the standard library's `dis`, which numpy's import chain needs (as tools/attract_survey.py).
sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join('build', 'ct.exe')
FIELD = os.path.join('build', 'Belthasar.exe')
# ATTRACT_DIV_STATE / ATTRACT_DIV_CART_FRAMES: another start for the cartridge's side -- `boot` (power-on, ct.exe's own) with
# ~11500 frames covers the port's 11400 where the attract state is not at hand
STATE = os.environ.get('ATTRACT_DIV_STATE', 'saves/attract_cena_inicial_rule.state')
ROM = 'Chrono_Trigger_USA_SNES.sfc'
ASSETS = 'ct_assets.dat'
CART_FRAMES = int(os.environ.get('ATTRACT_DIV_CART_FRAMES', '9110'))
PORT_FRAMES = 11400
DT = np.dtype([('f', '<u4'), ('loc', '<u2'), ('img', 'u1', (112, 128, 3))])

PIX_T = 24          # a thumbnail pixel differs when its largest channel differs by more than this
FRAC_T = 0.03       # a frame is a RENDER divergence when more than this share of its pixels differ
WIN = 8             # ...or when one WIN x WIN window of thumbnail pixels (16x16 SNES pixels) differs by more than
BLK_T = 0.5         # this share -- the local test that catches a sprite-sized difference
FLASH_T = 8.0       # a one-frame spike of the mean RGB byte bigger than this is a flash candidate...
FLASH_REL = 0.25    # ...if it is also this share of the brightest of its three frames (a stepped fade is not one)
MERGE = 24          # divergent runs of one visit closer than this many frames are one event (a chronic difference)
CARDS = 40          # report.html draws this many events, the most severe; report.txt lists them all
BAND = 72           # the alignment searches this many frames either side of the proportional diagonal
PENALTY = 1.0       # the extra cost of holding one side (keeps equal pictures on the diagonal)


# ----------------------------------------------------------------------------------------------------------- capture
def _stamp(*paths):
    out = []
    for p in paths:
        try:
            st = os.stat(p)
            out.append('%s:%d:%d' % (p, st.st_size, int(st.st_mtime)))
        except OSError:
            out.append('%s:missing' % p)
    return '|'.join(out)


def _cached(path, key):
    meta = path + '.key'
    return os.path.exists(path) and os.path.exists(meta) and open(meta).read() == key


def capture_cart(cache, fresh):
    path = os.path.join(cache, 'cart_every1.bin')
    key = _stamp(STATE, ROM) + '|frames=%d|rule=1' % CART_FRAMES   # (the rule's cartridge: a cache taken without it is stale)
    if not fresh and _cached(path, key):
        return path
    print('attract-divergence: capturing the cartridge, every frame to f%d ...' % CART_FRAMES, flush=True)
    t0 = time.time()
    # THE RULE'S CARTRIDGE (CLAUDE.md, the zelda3 model): the logical oracle is the truth every check grades against;
    # a real-time capture puts the battles' lag frames into the timing (the 07D fight came out 56 frames 'longer').
    env = dict(os.environ, PEEK_THUMBS=path, PEEK_THUMBS_EVERY='1', CT_ORACLE_LOGICAL='1')
    subprocess.run([CT, '--peek', STATE, os.path.join(cache, 'cart_last.raw'), '--frames', str(CART_FRAMES)],
                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(path) or os.path.getsize(path) < DT.itemsize * 100:
        sys.exit('attract-divergence: the oracle wrote no thumbnails (%s)' % path)
    open(path + '.key', 'w').write(key)
    print('attract-divergence: cartridge captured in %.0f s' % (time.time() - t0), flush=True)
    return path


def capture_port(cache, fresh, vehicle):
    path = os.path.join(cache, 'port_every1_%s.bin' % vehicle)
    key = _stamp(FIELD, ASSETS) + '|frames=%d|vehicle=%s' % (PORT_FRAMES, vehicle)
    if not fresh and _cached(path, key):
        return path
    print('attract-divergence: capturing the port (%s), every frame to f%d ...' % (vehicle, PORT_FRAMES), flush=True)
    t0 = time.time()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SHOTS_THUMBS=path, CT_SHOTS_THUMBS_EVERY='1')
    argv = [FIELD, '--assets', ASSETS, '--scene', '433']
    if vehicle == 'boot':      # from power-on on an erased cart: the play clock and the seeds the cartridge's demo has
        argv = [FIELD, '--assets', ASSETS, '--boot']
        env.update(CT_AUDIO_LOCKSTEP='1', CT_SRAM=os.path.join(cache, 'fresh.srm'), CT_SRAM_FRESH='1',
                   CT_SHOTS=os.path.join(cache, 'port_last'), CT_SHOTS_AT=str(PORT_FRAMES), CT_SHOTS_EXIT='1')
    elif vehicle == 'lockstep':
        env.update(CT_AUDIO_LOCKSTEP='1', CT_SHOTS=os.path.join(cache, 'port_last'), CT_SHOTS_AT=str(PORT_FRAMES),
                   CT_SHOTS_EXIT='1')
    else:
        argv += ['--selftest', '--frames', str(PORT_FRAMES)]
    subprocess.run(argv, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(path) or os.path.getsize(path) < DT.itemsize * 100:
        sys.exit('attract-divergence: the port wrote no thumbnails (%s)' % path)
    open(path + '.key', 'w').write(key)
    print('attract-divergence: port captured in %.0f s' % (time.time() - t0), flush=True)
    return path


# ---------------------------------------------------------------------------------------------------------- analysis
def visits(locs):
    out, s = [], 0
    for i in range(1, len(locs) + 1):
        if i == len(locs) or locs[i] != locs[s]:
            out.append((int(locs[s]), s, i))
            s = i
    return out


def pair(cv, pv):
    """The two visit lists paired in order by location (a forward greedy match, as tools/attract_survey.py)."""
    pairs, j = [], 0
    for ci, (loc, a, b) in enumerate(cv):
        for k in range(j, min(j + 6, len(pv))):
            if pv[k][0] == loc:
                pairs.append((ci, k))
                j = k + 1
                break
    return pairs


def features(imgs):
    """28x32 RGB, 4x4 thumbnail pixels averaged: what the alignment compares."""
    n = len(imgs)
    return imgs.reshape(n, 28, 4, 32, 4, 3).mean(axis=(2, 4), dtype=np.float32)


def align(A, B, band=BAND, penalty=PENALTY):
    """Monotonic alignment (DTW) of feature stacks A (n) and B (m). Returns (path, cost matrix)."""
    n, m = len(A), len(B)
    inf = np.inf
    C = np.full((n, m), inf)
    for i in range(n):
        c = i * (m - 1) / max(n - 1, 1)
        lo, hi = max(0, int(c) - band), min(m, int(c) + band + 1)
        C[i, lo:hi] = np.abs(B[lo:hi] - A[i]).mean(axis=(1, 2, 3))
    D = np.full((n, m), inf)
    for i in range(n):
        row = np.where(np.isfinite(C[i]))[0]
        lo, hi = row[0], row[-1] + 1
        c = C[i, lo:hi]
        if i == 0:
            T = np.full(hi - lo, inf)
            if lo == 0:
                T[0] = c[0]
        else:
            prev = D[i - 1]
            diag = np.full(hi - lo, inf)
            js = np.arange(lo, hi)
            ok = js >= 1
            diag[ok] = prev[js[ok] - 1]
            T = c + np.minimum(diag, prev[lo:hi] + penalty)
        # the within-row hold, D[j] = min(T[j], c[j] + penalty + D[j-1]), as a running minimum
        S = np.concatenate(([0.0], np.cumsum(c[1:] + penalty)))
        with np.errstate(invalid='ignore'):
            D[i, lo:hi] = S + np.minimum.accumulate(T - S)
    path, i, j = [], n - 1, m - 1
    if not np.isfinite(D[i, j]):
        return None, C
    while True:
        path.append((i, j))
        if i == 0 and j == 0:
            break
        here = D[i, j] - C[i, j]
        cands = []
        if i > 0 and j > 0:
            cands.append((D[i - 1, j - 1], 0, i - 1, j - 1))
        if i > 0:
            cands.append((D[i - 1, j] + penalty, 1, i - 1, j))
        if j > 0:
            cands.append((D[i, j - 1] + penalty, 2, i, j - 1))
        best = min(cands, key=lambda t: (abs(t[0] - here), t[1]))
        i, j = best[2], best[3]
    path.reverse()
    return path, C


def maps(path, C, n, m):
    """For every cartridge frame its best port frame on the path, and for every port frame its cartridge frame."""
    jm = np.full(n, -1)
    im = np.full(m, -1)
    for i, j in path:
        if jm[i] < 0 or C[i, j] < C[i, jm[i]]:
            jm[i] = j
        if im[j] < 0 or C[i, j] < C[im[j], j]:
            im[j] = i
    return jm, im


def luma_img(x):
    return (x[..., 0] * 77 + x[..., 1] * 150 + x[..., 2] * 29) / 256.0


def region(mask):
    """The screen box (SNES pixels) of the densest part of a thumbnail difference mask."""
    h, w = mask.shape
    bh, bw = 8, 8
    grid = mask[:h - h % bh, :w - w % bw].reshape(h // bh, bh, w // bw, bw).mean(axis=(1, 3))
    hot = np.argwhere(grid > 0.25)
    if len(hot) == 0:
        hot = np.argwhere(grid > 0)
    if len(hot) == 0:
        return None
    y0, x0 = hot.min(axis=0)
    y1, x1 = hot.max(axis=0) + 1
    return [int(x0 * bw * 2), int(y0 * bh * 2), int(x1 * bw * 2), int(y1 * bh * 2)]


def window_max(mask, k=WIN):
    """For each frame of a (n, 112, 128) mask: the largest share of differing pixels in any k x k window, and where.
    A sprite-sized difference is invisible in a whole-frame share (a 16x16 OBJ is under half a percent of the
    screen) but fills a window."""
    n, h, w = mask.shape
    I = np.zeros((n, h + 1, w + 1), np.int32)
    I[:, 1:, 1:] = mask.astype(np.int32).cumsum(1).cumsum(2)
    S = I[:, k:, k:] - I[:, :-k, k:] - I[:, k:, :-k] + I[:, :-k, :-k]
    flat = S.reshape(n, -1)
    arg = flat.argmax(axis=1)
    ys, xs = np.divmod(arg, S.shape[2])
    return flat.max(axis=1) / float(k * k), ys, xs


def spikes(l):
    """Signed one-frame spikes of a luma series: + for a bright frame between two darker, - for a dark one."""
    s = np.zeros(len(l))
    if len(l) >= 3:
        up = l[1:-1] - np.maximum(l[:-2], l[2:])
        dn = np.minimum(l[:-2], l[2:]) - l[1:-1]
        # a spike is a frame above BOTH neighbours or below both -- a monotonic ramp (a fade) is neither
        s[1:-1] = np.where(up > 0, up, np.where(dn > 0, -dn, 0.0))
    return s


def strong(l, s, i):
    """A spike worth calling a flash: big in absolute terms AND against the brightest of its three frames."""
    if abs(s[i]) < FLASH_T:
        return False
    top = max(l[max(0, i - 1):i + 2])
    return abs(s[i]) >= FLASH_REL * max(top, 1.0)


def _flash_unmatched(s_here, idx, s_other, centre, span):
    """A spike on one side is a flash of THAT side unless the other side spikes the same way within two frames."""
    lo, hi = max(0, centre - 2), min(span, centre + 3)
    other = s_other[lo:hi]
    if not len(other):
        return True
    k = int(np.argmax(np.abs(other)))
    return not (abs(other[k]) >= abs(s_here[idx]) / 2 and np.sign(other[k]) == np.sign(s_here[idx]))


def hint_for(a, b, lc, lp, mad, madY):
    lo, hi = min(lc, lp), max(lc, lp)
    if lo < 1.5 and hi > 6:
        return 'black-vs-picture'
    ya, yb = luma_img(a), luma_img(b)
    za, zb = ya - ya.mean(), yb - yb.mean()
    den = np.sqrt((za * za).sum() * (zb * zb).sum())
    corr = float((za * zb).sum() / den) if den > 0 else 0.0
    if abs(lc - lp) > 0.2 * hi and corr > 0.9:
        return 'brightness'
    if madY < 0.45 * mad:
        return 'colour'
    return 'shape'


def analyse_visit(ci, cimg, cfr, pimg, pfr):
    n, m = len(cimg), len(pimg)
    path, C = align(features(cimg), features(pimg), band=max(BAND, abs(n - m) + BAND // 2))
    if path is None:
        return None
    jm, im = maps(path, C, n, m)
    a = cimg.astype(np.int16)
    b = pimg[jm].astype(np.int16)
    d = np.abs(a - b)
    dmax = d.max(axis=3)
    mask = dmax > PIX_T
    frac = mask.mean(axis=(1, 2))
    blk, by, bx = window_max(mask)
    mad = d.mean(axis=(1, 2, 3))
    madY = np.abs(luma_img(a) - luma_img(b)).mean(axis=(1, 2))
    lc = a.mean(axis=(1, 2, 3))
    lp_all = pimg.astype(np.int16).mean(axis=(1, 2, 3))
    lp = lp_all[jm]
    off = jm - np.arange(n)
    score = np.maximum(frac / FRAC_T, blk / BLK_T)
    res = dict(n=n, m=m, jm=jm, im=im, frac=frac, blk=blk, score=score, mad=mad, lc=lc, lp=lp, off=off, events=[])

    # ---- FLASH events first: a one-frame spike on one side the aligned other side does not have
    sc, sp = spikes(lc), spikes(lp_all)
    flash_i = set()
    for j in range(m):
        if strong(lp_all, sp, j) and _flash_unmatched(sp, j, sc, im[j], n):
            i = int(im[j])
            bj = pimg[j].astype(np.int16)
            dj = np.abs(a[i] - bj)
            res['events'].append(dict(kind='flash-port', cart=[int(cfr[i])] * 2, port=[int(pfr[j])] * 2, frames=1,
                                      worst_i=i, worst_j=int(j), cart_f=int(cfr[i]), port_f=int(pfr[j]),
                                      frac=float((dj.max(axis=2) > PIX_T).mean()), blk=1.0, mad=float(dj.mean()),
                                      luma=[float(lc[i]), float(lp_all[j])], hint='port spike %+.0f' % sp[j],
                                      region=None, offset=int(j - i), sev=float(4 * abs(sp[j]) / FLASH_T)))
            flash_i.update(range(max(0, i - 1), min(n, i + 2)))
    for i in range(n):
        if strong(lc, sc, i) and _flash_unmatched(sc, i, sp, int(jm[i]), m):
            j = int(jm[i])
            res['events'].append(dict(kind='flash-cart', cart=[int(cfr[i])] * 2, port=[int(pfr[j])] * 2, frames=1,
                                      worst_i=int(i), worst_j=j, cart_f=int(cfr[i]), port_f=int(pfr[j]),
                                      frac=float(frac[i]), blk=float(blk[i]), mad=float(mad[i]),
                                      luma=[float(lc[i]), float(lp[i])], hint='cartridge spike %+.0f' % sc[i],
                                      region=None, offset=int(off[i]), sev=float(4 * abs(sc[i]) / FLASH_T)))
            flash_i.update(range(max(0, i - 1), min(n, i + 2)))

    # ---- RENDER events: runs of frames above either threshold, the flash frames already accounted for
    bad = (score > 1.0) & ~np.isin(np.arange(n), list(flash_i))
    k = 0
    while k < n:
        if not bad[k]:
            k += 1
            continue
        s = e = k
        while k < n and (bad[k] or (k - e) <= MERGE):
            if bad[k]:
                e = k
            k += 1
        w = s + int(np.argmax(np.where(bad[s:e + 1], score[s:e + 1], -1)))
        if frac[w] > FRAC_T:
            reg = region(mask[w])
        else:                                              # a small, dense difference: the window that found it
            reg = [int(bx[w] * 2), int(by[w] * 2), int((bx[w] + WIN) * 2), int((by[w] + WIN) * 2)]
        res['events'].append(dict(kind='render', cart=[int(cfr[s]), int(cfr[e])], port=[int(pfr[jm[s]]), int(pfr[jm[e]])],
                                  frames=int(e - s + 1), worst_i=int(w), worst_j=int(jm[w]),
                                  cart_f=int(cfr[w]), port_f=int(pfr[jm[w]]), frac=float(frac[w]), blk=float(blk[w]),
                                  mad=float(mad[w]), luma=[float(lc[w]), float(lp[w])],
                                  hint=hint_for(a[w], b[w], lc[w], lp[w], mad[w], madY[w]), region=reg,
                                  offset=int(off[w]), bad_frames=int(bad[s:e + 1].sum()),
                                  segments=int(np.count_nonzero(np.diff(np.concatenate(([0], bad[s:e + 1].astype(int)))) == 1)),
                                  sev=float(np.where(bad[s:e + 1], score[s:e + 1], 0).sum())))
    return res


# ------------------------------------------------------------------------------------------------------------ output
def png_bytes(rgb):
    h, w, _ = rgb.shape
    raw = b''.join(b'\x00' + rgb[y].tobytes() for y in range(h))

    def ch(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
            ch(b'IDAT', zlib.compress(raw, 6)) + ch(b'IEND', b''))


def triptych(ca, pb):
    """cartridge | port | difference, each doubled back to 256x224, the difference in red over a dimmed port."""
    a = ca.astype(np.int16)
    b = pb.astype(np.int16)
    mask = np.abs(a - b).max(axis=2) > PIX_T
    diff = (b // 3).astype(np.uint8)
    diff[mask] = [255, 40, 40]
    up = lambda x: np.repeat(np.repeat(x, 2, axis=0), 2, axis=1)
    gap = np.full((224, 4, 3), 60, np.uint8)
    return np.concatenate([up(ca), gap, up(pb), gap, up(diff)], axis=1)


VEHICLE = 'lockstep'


def confirm_cmd(ev):
    r = ev.get('region') or [0, 0, 256, 224]
    if VEHICLE == 'lockstep':
        return ('CT_AUDIO_LOCKSTEP=1 python tools/frame_region_cmp.py %s %d %d %d %d %d %d -- --assets %s --scene 433'
                % (STATE, ev['cart_f'], ev['port_f'], r[0], r[1], r[2], r[3], ASSETS))
    return ('python tools/frame_region_cmp.py %s %d %d %d %d %d %d -- --assets %s --scene 433 --selftest --frames %d'
            % (STATE, ev['cart_f'], ev['port_f'], r[0], r[1], r[2], r[3], ASSETS, ev['port_f'] + 1))


def write_reports(out, rows, events, cimgs, pimgs, cinfo, pinfo, timeline):
    # ---- text
    L = []
    L.append('attract-divergence: cartridge %s (%d frames) vs port %s (%d frames)' % (cinfo[0], cinfo[1], pinfo[0], pinfo[1]))
    L.append('thresholds: pixel > %d; a frame diverges above %.0f%% of its pixels OR %.0f%% of one %dx%d window; flash > %.0f;'
             ' band %d, hold penalty %.1f' % (PIX_T, FRAC_T * 100, BLK_T * 100, WIN, WIN, FLASH_T, BAND, PENALTY))
    L.append('')
    L.append('VISITS  (offset = port frame - cartridge frame inside the visit; bad = frames above the threshold)')
    L.append('  #  loc  cartridge          port               len c/p     offset start/min/max/end   median   bad  events')
    for r in rows:
        if r.get('skip'):
            L.append(' %2d  %03X  f%-6d %5d f      f%-6d %5d f     -- %s' % (r['idx'], r['loc'], r['c0'], r['n'], r['p0'], r['m'], r['skip']))
            continue
        L.append(' %2d  %03X  f%-6d..f%-6d  f%-6d..f%-6d  %4d/%-4d   %+4d %+4d %+4d %+4d        %5.1f%%  %4d  %d'
                 % (r['idx'], r['loc'], r['c0'], r['c1'], r['p0'], r['p1'], r['n'], r['m'], r['off0'], r['offmin'],
                    r['offmax'], r['off1'], r['medfrac'] * 100, r['bad'], r['nev']))
    L.append('')
    L.append('EVENTS, in attract order (%d; sev = how far past the threshold, summed over the divergent frames)'
             % len(events))
    for k, ev in enumerate(events):
        span = ('  %d/%d f diverge in %d run(s)' % (ev['bad_frames'], ev['frames'], ev['segments'])
                if ev['kind'] == 'render' else '')
        L.append(' [%2d] %-10s loc %03X  sev %6.1f  cart f%d..f%d  port f%d..f%d%s' % (
            k, ev['kind'], ev['loc'], ev['sev'], ev['cart'][0], ev['cart'][1], ev['port'][0], ev['port'][1], span))
        L.append('        worst cart f%d ~ port f%d: %4.1f%% px, window %3.0f%%, mad %5.1f, brightness %5.1f/%5.1f, '
                 'offset %+d -- %s%s' % (ev['cart_f'], ev['port_f'], ev['frac'] * 100, ev.get('blk', 0) * 100, ev['mad'],
                                         ev['luma'][0], ev['luma'][1], ev['offset'], ev['hint'],
                                         ('  region %s' % ev['region']) if ev.get('region') else ''))
        L.append('        confirm: ' + confirm_cmd(ev))
    open(os.path.join(out, 'report.txt'), 'w', encoding='utf-8').write('\n'.join(L) + '\n')
    json.dump(dict(visits=[{k: v for k, v in r.items()} for r in rows], events=events),
              open(os.path.join(out, 'events.json'), 'w'), indent=1)

    # ---- html
    W, H = 1400, 170
    tl = timeline
    total = len(tl['score'])
    buckets = W
    per = max(1, total // buckets + (1 if total % buckets else 0))
    fr = np.array(tl['score'])
    pad = (-len(fr)) % per
    frb = np.concatenate([fr, np.zeros(pad)]).reshape(-1, per).max(axis=1)
    bars = []
    for x, v in enumerate(frb):
        hh = min(1.0, v / 4.0) * (H - 30)
        if hh <= 0:
            continue
        cls = 'bad' if v > 1.0 else 'ok'
        bars.append('<rect class="%s" x="%d" y="%.1f" width="1" height="%.1f"/>' % (cls, x, H - 18 - hh, hh))
    marks = []
    for r in rows:
        if 'x0' not in r:
            continue
        x = r['x0'] / per
        marks.append('<line class="vis" x1="%.1f" x2="%.1f" y1="0" y2="%d"/>' % (x, x, H - 18))
        marks.append('<text class="lab" x="%.1f" y="%d">%03X</text>' % (x + 2, H - 4, r['loc']))
    for k, ev in enumerate(events):
        if 'x' in ev:
            x = ev['x'] / per
            marks.append('<a href="#ev%d"><circle class="%s" cx="%.1f" cy="8" r="4"/></a>'
                         % (k, 'fl' if ev['kind'].startswith('flash') else 'rd', x))
    thr = H - 18 - (1.0 / 4.0) * (H - 30)
    svg = ('<svg viewBox="0 0 %d %d" class="tl" role="img" aria-label="share of differing pixels per aligned frame">'
           '<line class="thr" x1="0" x2="%d" y1="%.1f" y2="%.1f"/>%s%s</svg>' % (W, H, W, thr, thr, ''.join(bars), ''.join(marks)))
    vrows = []
    for r in rows:
        if r.get('skip'):
            vrows.append('<tr class="dim"><td>%d</td><td>%03X</td><td>f%d</td><td>f%d</td><td>%d / %d</td>'
                         '<td colspan="4">%s</td></tr>' % (r['idx'], r['loc'], r['c0'], r['p0'], r['n'], r['m'], r['skip']))
        else:
            vrows.append('<tr><td>%d</td><td>%03X</td><td>f%d</td><td>f%d</td><td>%d / %d</td><td>%+d .. %+d</td>'
                         '<td>%.1f%%</td><td>%d</td><td>%d</td></tr>'
                         % (r['idx'], r['loc'], r['c0'], r['p0'], r['n'], r['m'], r['offmin'], r['offmax'],
                            r['medfrac'] * 100, r['bad'], r['nev']))
    order = sorted(range(len(events)), key=lambda k: -events[k]['sev'])
    carded = set(order[:CARDS])
    erows = []
    for k, ev in enumerate(events):
        name = ('<a href="#ev%d">#%d</a>' % (k, k)) if k in carded else '#%d' % k
        erows.append('<tr><td>%s</td><td><span class="k %s">%s</span></td><td>%03X</td><td>%.1f</td><td>f%d..f%d</td>'
                     '<td>f%d..f%d</td><td>%s</td></tr>'
                     % (name, 'fl' if ev['kind'].startswith('flash') else 'rd', ev['kind'], ev['loc'], ev['sev'],
                        ev['cart'][0], ev['cart'][1], ev['port'][0], ev['port'][1], ev['hint']))
    cards = []
    for k in order[:CARDS]:
        ev = events[k]
        img = triptych(cimgs[ev['gi']], pimgs[ev['gj']])
        b64 = base64.b64encode(png_bytes(img)).decode('ascii')
        cards.append('<section class="ev" id="ev%d"><h3><span class="k %s">%s</span> #%d &middot; loc %03X &middot; cartridge f%d '
                     '&harr; port f%d</h3><p>%d frame(s), cartridge f%d..f%d, port f%d..f%d &middot; %.1f%% of pixels differ, '
                     'mean %.1f &middot; brightness %.1f / %.1f &middot; offset %+d &middot; <b>%s</b>%s</p>'
                     '<img alt="cartridge, port and their difference" src="data:image/png;base64,%s">'
                     '<pre>%s</pre></section>'
                     % (k, 'fl' if ev['kind'].startswith('flash') else 'rd', ev['kind'], k, ev['loc'], ev['cart_f'],
                        ev['port_f'], ev['frames'], ev['cart'][0], ev['cart'][1], ev['port'][0], ev['port'][1],
                        ev['frac'] * 100, ev['mad'], ev['luma'][0], ev['luma'][1], ev['offset'], ev['hint'],
                        (' &middot; region %s' % ev['region']) if ev.get('region') else '', b64, confirm_cmd(ev)))
    css = """
:root{--bg:#f7f7f5;--fg:#1d1d1b;--mut:#6b6b66;--card:#fff;--line:#d8d8d2;--ok:#9aa7b4;--bad:#c2410c;--fl:#7c3aed;--rd:#c2410c}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#15161a;--fg:#e8e8e3;--mut:#9a9a93;--card:#1e2026;--line:#33353d;--ok:#56606b;--bad:#f97316;--fl:#a78bfa;--rd:#f97316}}
:root[data-theme="dark"]{--bg:#15161a;--fg:#e8e8e3;--mut:#9a9a93;--card:#1e2026;--line:#33353d;--ok:#56606b;--bad:#f97316;--fl:#a78bfa;--rd:#f97316}
body{background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif;margin:0;padding:24px 16px}
main{max-width:1440px;margin:0 auto}h1{font-size:20px;margin:0 0 4px}h2{font-size:16px;margin:26px 0 6px}p.sub{color:var(--mut);margin:0 0 18px}
.tl{width:100%;height:auto;background:var(--card);border:1px solid var(--line);border-radius:6px}
.tl .ok{fill:var(--ok)}.tl .bad{fill:var(--bad)}.tl .vis{stroke:var(--line)}.tl .lab{fill:var(--mut);font-size:10px}
.tl .thr{stroke:var(--mut);stroke-dasharray:3 3}.tl .rd{fill:var(--rd)}.tl .fl{fill:var(--fl)}
.wrap{overflow-x:auto}table{border-collapse:collapse;margin:18px 0;min-width:720px}td,th{border-bottom:1px solid var(--line);padding:4px 10px;text-align:left;font-variant-numeric:tabular-nums}
tr.dim td{color:var(--mut)}.ev{background:var(--card);border:1px solid var(--line);border-radius:6px;padding:12px 14px;margin:14px 0}
.ev h3{margin:0 0 6px;font-size:15px}.ev img{max-width:100%;image-rendering:pixelated;display:block;margin:8px 0}
.ev pre{white-space:pre-wrap;word-break:break-all;color:var(--mut);font-size:12px;margin:0}
.k{display:inline-block;padding:1px 7px;border-radius:10px;color:#fff;font-size:12px}.k.rd{background:var(--rd)}.k.fl{background:var(--fl)}
"""
    html = ('<!doctype html><html lang="pt"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
            '<title>Attract divergence</title><style>%s</style></head><body><main><h1>Attract divergence</h1>'
            '<p class="sub">Port against cartridge, every frame, aligned per visit. Bars: how far each aligned frame is past '
            'the divergence threshold (dashed line; %.0f%% of the frame or half of one sprite-sized window). Dots: events (orange = picture, violet = flash); each card is cartridge | '
            'port | difference.</p><div class="wrap">%s</div><div class="wrap"><table><tr><th>#</th><th>loc</th><th>cartridge</th>'
            '<th>port</th><th>frames c/p</th><th>offset</th><th>median</th><th>bad</th><th>events</th></tr>%s</table></div>'
            '<h2>Events</h2><div class="wrap"><table><tr><th>#</th><th>kind</th><th>loc</th><th>sev</th><th>cartridge</th>'
            '<th>port</th><th>hint</th></tr>%s</table></div><h2>The %d most severe</h2>%s</main></body></html>'
            % (css, FRAC_T * 100, svg, ''.join(vrows), ''.join(erows), min(CARDS, len(events)), ''.join(cards)))
    open(os.path.join(out, 'report.html'), 'w', encoding='utf-8').write(html)


# --------------------------------------------------------------------------------------------------------------- run
def run(cpath, ppath, out, only_loc=None):
    C = np.fromfile(cpath, dtype=DT)
    P = np.fromfile(ppath, dtype=DT)
    cv, pv = visits(C['loc']), visits(P['loc'])
    rows, events = [], []
    timeline = dict(score=[])
    cimgs, pimgs = C['img'], P['img']
    xcur = 0
    for ci, pj in pair(cv, pv):
        loc, ca, cb = cv[ci]
        _, pa, pb = pv[pj]
        n, m = cb - ca, pb - pa
        r = dict(idx=ci, loc=loc, c0=int(C['f'][ca]), c1=int(C['f'][cb - 1]), p0=int(P['f'][pa]), p1=int(P['f'][pb - 1]),
                 n=n, m=m)
        if only_loc is not None and loc != only_loc:
            continue
        if cb == len(C) or pb == len(P):                   # a capture ends inside this visit: the common start only
            k = min(n, m)
            cb, pb, n, m = ca + k, pa + k, k, k
            r.update(c1=int(C['f'][cb - 1]), p1=int(P['f'][pb - 1]), n=n, m=m, cut=True)
        if min(n, m) < 4 or max(n, m) > 3 * min(n, m):
            r['skip'] = 'not comparable (lengths %d / %d -- one side carries a mode the other capture does not)' % (n, m)
            rows.append(r)
            continue
        if cimgs[ca:cb].max() < 8 and pimgs[pa:pb].max() < 8:
            r['skip'] = 'black on both sides'
            rows.append(r)
            continue
        res = analyse_visit(ci, cimgs[ca:cb], C['f'][ca:cb], pimgs[pa:pb], P['f'][pa:pb])
        if res is None:
            r['skip'] = 'no alignment inside the band'
            rows.append(r)
            continue
        off = res['off']
        r.update(off0=int(off[0]), off1=int(off[-1]), offmin=int(off.min()), offmax=int(off.max()),
                 medfrac=float(np.median(res['frac'])), bad=int((res['score'] > 1.0).sum()), nev=len(res['events']),
                 x0=xcur)
        rows.append(r)
        for ev in res['events']:
            ev['loc'] = loc
            ev['visit'] = ci
            ev['gi'] = int(ca + ev['worst_i'])
            ev['gj'] = int(pa + ev['worst_j'])
            ev['x'] = xcur + ev['worst_i']
            events.append(ev)
        timeline['score'].extend(float(v) for v in res['score'])
        xcur += n
        print('attract-divergence: visit %2d loc %03X  %4d/%-4d frames  offset %+d..%+d  median %.1f%%  events %d'
              % (ci, loc, n, m, r['offmin'], r['offmax'], r['medfrac'] * 100, r['nev']), flush=True)
    events.sort(key=lambda e: (e['x'], e['kind']))
    os.makedirs(out, exist_ok=True)
    write_reports(out, rows, events, cimgs, pimgs, (cpath, len(C)), (ppath, len(P)), timeline)
    for e in events:
        for k in ('gi', 'gj'):
            e.pop(k, None)
    return rows, events


# ----------------------------------------------------------------------------------------------------------- selftest
def selftest():
    """A synthetic pair with three planted differences: a 3-frame hold, a recolour, a one-frame flash."""
    rng = np.random.default_rng(7)
    n = 160
    base = np.zeros((n, 112, 128, 3), np.uint8)
    for k in range(n):
        base[k, :, :] = (20, 30, 60)
        y, x = 20 + (k % 60), 10 + (k * 3) % 100
        base[k, y:y + 16, x:x + 16] = (220, 200, 90)                       # a moving sprite
        base[k, 90:100, (k * 5) % 118:(k * 5) % 118 + 10] = (60, 200, 60)  # a second, faster one
    cart = base.copy()
    port = np.concatenate([base[:50], np.repeat(base[50:51], 3, axis=0), base[50:]])    # the port holds 3 frames at 50
    m = len(port)
    for k in range(103, 111):                                              # cartridge 100..107: sprite recoloured
        mask = (port[k] == (220, 200, 90)).all(axis=2)
        port[k][mask] = (90, 200, 220)
    port[140] = 230                                                        # one white frame the cartridge lacks
    port = port + rng.integers(0, 3, port.shape, dtype=np.uint8)           # capture noise
    res = analyse_visit(0, cart, np.arange(n), port, np.arange(m))
    ok = True
    late = res['off'][60:95]
    if not (np.median(late) == 3):
        print('attract-divergence: selftest MISSED the hold (offset after it %s)' % np.unique(late)); ok = False
    rd = [e for e in res['events'] if e['kind'] == 'render']
    if not any(e['cart'][0] <= 101 and e['cart'][1] >= 106 for e in rd):
        print('attract-divergence: selftest MISSED the recolour (render events %s)' % [e['cart'] for e in rd]); ok = False
    if any(e['cart'][1] < 95 for e in rd):
        print('attract-divergence: selftest REPORTED the hold as a picture difference %s' % [e['cart'] for e in rd]); ok = False
    fl = [e for e in res['events'] if e['kind'] == 'flash-port']
    if not any(e['port'][0] == 140 for e in fl):
        print('attract-divergence: selftest MISSED the flash (flash events %s)' % [e['port'] for e in fl]); ok = False
    if any(e['kind'] == 'flash-cart' for e in res['events']):
        print('attract-divergence: selftest reported a cartridge flash that is not there'); ok = False
    # a fade is not a flash: a monotonic ramp has no spike, the curtain blink's 0 -> 46 -> 4 has one of +42
    ramp = spikes(np.array([0.0, 14.0, 28.7, 43.0, 57.0, 71.0]))
    blink = spikes(np.array([0.0, 0.0, 46.0, 4.0, 4.0]))
    if np.any(ramp != 0) or blink[2] != 42.0:
        print('attract-divergence: selftest spikes() is wrong (ramp %s, blink %s)' % (ramp.tolist(), blink.tolist())); ok = False
    hints = [e['hint'] for e in rd if e['cart'][0] <= 101 and e['cart'][1] >= 106]
    print('attract-divergence: selftest %s (hold -> offset %+d, recolour -> %s %s, flash -> port f%s)'
          % ('PASS' if ok else 'FAIL', int(np.median(late)), [e['cart'] for e in rd], hints,
             [e['port'][0] for e in fl]))
    return 0 if ok else 1


def main():
    os.chdir(ROOT)
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join('build', '_divergence', 'latest'))
    ap.add_argument('--cache', default=os.path.join('build', '_divergence', 'cache'))
    ap.add_argument('--fresh-port', action='store_true')
    ap.add_argument('--fresh-cart', action='store_true')
    ap.add_argument('--vehicle', choices=('lockstep', 'selftest', 'boot'), default='lockstep',
                    help='lockstep (default): the interactive run with CT_AUDIO_LOCKSTEP=1, the numbering every attract '
                         'check uses. selftest: `--selftest --frames N`, which orders a transition\'s frames differently '
                         '(the curtain-close blink showed only there) but fights the demo battles synchronously, so its '
                         'battle legs are not comparable. boot: `--boot` on an erased cart, so the port reaches the '
                         'demo with the cartridge play clock ($0400, the F8 cursor) -- a cold `--scene 433` starts 22 '
                         'ticks behind, which put e.g. the 01C auto-scroll shake (C0/91FD reads $0400) out of phase.')
    ap.add_argument('--visit', help='only this location (hex, e.g. 0AD)')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    os.makedirs(a.cache, exist_ok=True)
    cpath = capture_cart(a.cache, a.fresh_cart)
    global VEHICLE
    VEHICLE = a.vehicle
    ppath = capture_port(a.cache, a.fresh_port, a.vehicle)
    rows, events = run(cpath, ppath, a.out, int(a.visit, 16) if a.visit else None)
    kinds = {}
    for e in events:
        kinds[e['kind']] = kinds.get(e['kind'], 0) + 1
    print('attract-divergence: %d visits compared, %d events (%s) -> %s'
          % (sum(1 for r in rows if not r.get('skip')), len(events),
             ', '.join('%d %s' % (v, k) for k, v in sorted(kinds.items())) or 'none',
             os.path.join(a.out, 'report.html')))
    return 0


if __name__ == '__main__':
    sys.exit(main())
