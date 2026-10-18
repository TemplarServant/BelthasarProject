#!/usr/bin/env python3
r"""Survey the whole attract, port against cartridge, for VISUAL differences.

    python tools/attract_survey.py <oracle_thumbs.bin> <port_thumbs.bin> [<outdir>]

Both files are the 128x112 thumbnail records `[u32 frame][u16 $0100][128*112*3 RGB]`:
    oracle  build/ct.exe --peek saves/attract_cena_inicial.state <out> --frames 9100      with PEEK_THUMBS=<file>
    port    build/Belthasar.exe --assets ct_assets.dat --scene 433 (interactive, CT_AUDIO_LOCKSTEP=1)
            with CT_SHOTS_THUMBS=<file> CT_SHOTS=<p> CT_SHOTS_AT=<end> CT_SHOTS_EXIT=1

The two timelines are not frame-aligned (the port boots through the title; its legs have their own residual lengths), so
the survey aligns them VISIT BY VISIT: consecutive records with the same $0100 form a visit, the two visit lists are
paired in order by location, and every sampled cartridge frame is compared with the best-matching port frame inside the
paired visit, searched around the same offset from the visit's start. A sample whose BEST match still differs is a visual
difference of the port, not a timing one. What is reported per visit: the lengths, the median best difference, and the
worst sample; the worst samples overall are written as PNG strips (cartridge | port | |difference| x4).

The metric is the mean absolute RGB difference over the thumbnail (0..255) and the share of thumbnail pixels off by more
than 32. It measures pictures only -- it does not say why they differ.
"""
import os
import struct
import sys

# tools/dis.py would shadow the standard library's `dis`, which numpy's import chain needs.
sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]
import numpy as np

REC = 6 + 128 * 112 * 3


def load(path):
    data = open(path, 'rb').read()
    n = len(data) // REC
    frames = np.empty(n, dtype=np.int64)
    locs = np.empty(n, dtype=np.int64)
    imgs = np.empty((n, 112, 128, 3), dtype=np.uint8)
    for i in range(n):
        o = i * REC
        frames[i], locs[i] = struct.unpack_from('<IH', data, o)
        imgs[i] = np.frombuffer(data, dtype=np.uint8, count=128 * 112 * 3, offset=o + 6).reshape(112, 128, 3)
    return frames, locs, imgs


def visits(frames, locs):
    out = []
    s = 0
    for i in range(1, len(locs) + 1):
        if i == len(locs) or locs[i] != locs[s]:
            out.append((int(locs[s]), s, i))          # loc, first record, one past last
            s = i
    return out


def pair(ov, pv):
    """Pair the two visit lists in order by location (a forward greedy match)."""
    pairs, j = [], 0
    for oi, (loc, a, b) in enumerate(ov):
        for k in range(j, min(j + 6, len(pv))):
            if pv[k][0] == loc:
                pairs.append((oi, k))
                j = k + 1
                break
    return pairs


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    outdir = sys.argv[3] if len(sys.argv) > 3 else 'build/_survey'
    os.makedirs(outdir, exist_ok=True)
    of, ol, oi = load(sys.argv[1])
    pf, pl, pi = load(sys.argv[2])
    ov, pv = visits(of, ol), visits(pf, pl)
    step_o = int(of[1] - of[0]) if len(of) > 1 else 1
    worst_all = []
    per_visit = []
    print('attract-survey: %d cartridge / %d port records, %d / %d visits' % (len(of), len(pf), len(ov), len(pv)))
    for oi_, pj in pair(ov, pv):
        loc, oa, ob = ov[oi_]
        _, pa, pb = pv[pj]
        olen = int(of[ob - 1] - of[oa]) + step_o
        plen = int(pf[pb - 1] - pf[pa]) + step_o
        if olen < 20:
            continue
        win = max(40, abs(olen - plen) + 30)
        pstart = int(pf[pa])
        poffs = pf[pa:pb] - pstart
        bests = []
        for k in range(oa, ob, 5):                       # every 5th cartridge record (10 frames at the default step)
            d = int(of[k] - of[oa])
            sel = np.nonzero(np.abs(poffs - d) <= win)[0]
            if len(sel) == 0:
                continue
            a = oi[k].astype(np.int16)
            if a.max() < 12:                             # a black cartridge frame says nothing about pictures
                continue
            cand = pi[pa + sel].astype(np.int16)
            diffs = np.abs(cand - a[None]).mean(axis=(1, 2, 3))
            b = int(np.argmin(diffs))
            pk = pa + int(sel[b])
            share = float((np.abs(pi[pk].astype(np.int16) - a).max(axis=2) > 32).mean())
            bests.append((float(diffs[b]), share, int(of[k]), int(pf[pk])))
        if not bests:
            print('  %03X  cart f%-5d %4df | port f%-5d %4df | (all black)' % (loc, of[oa], olen, pstart, plen))
            continue
        med = float(np.median([x[0] for x in bests]))
        w = max(bests)
        print('  %03X  cart f%-5d %4df | port f%-5d %4df | median %5.1f | worst %5.1f (%4.1f%% px) cart f%d ~ port f%d'
              % (loc, of[oa], olen, pstart, plen, med, w[0], 100 * w[1], w[2], w[3]))
        worst_all += [(x[0], x[1], loc, x[2], x[3]) for x in bests]
        per_visit.append((w[0], w[1], loc, w[2], w[3], oi_))
    worst_all.sort(reverse=True)
    try:
        from PIL import Image
    except ImportError:
        return
    oidx = {int(f): i for i, f in enumerate(of)}
    pidx = {int(f): i for i, f in enumerate(pf)}

    def strip(ofr, pfr):
        a, b = oi[oidx[ofr]], pi[pidx[pfr]]
        diff = np.clip(np.abs(a.astype(np.int16) - b.astype(np.int16)) * 4, 0, 255).astype(np.uint8)
        s = np.concatenate([a, np.full((112, 4, 3), 255, np.uint8), b, np.full((112, 4, 3), 255, np.uint8), diff], axis=1)
        return Image.fromarray(s).resize((s.shape[1] * 2, s.shape[0] * 2), Image.NEAREST)

    for dv, share, loc, ofr, pfr, vi in per_visit:   # the worst sample of EVERY visit, for a pass through the legs in order
        strip(ofr, pfr).save(os.path.join(outdir, 'visit_%02d_%03X_c%d_p%d.png' % (vi, loc, ofr, pfr)))
    seen = set()
    written = 0
    for dv, share, loc, ofr, pfr in worst_all:
        key = (loc, ofr // 60)
        if key in seen:
            continue
        seen.add(key)
        a, b = oi[oidx[ofr]], pi[pidx[pfr]]
        diff = np.clip(np.abs(a.astype(np.int16) - b.astype(np.int16)) * 4, 0, 255).astype(np.uint8)
        strip = np.concatenate([a, np.full((112, 4, 3), 255, np.uint8), b, np.full((112, 4, 3), 255, np.uint8), diff], axis=1)
        name = os.path.join(outdir, 'worst_%02d_%03X_c%d_p%d.png' % (written, loc, ofr, pfr))
        Image.fromarray(strip).resize((strip.shape[1] * 2, strip.shape[0] * 2), Image.NEAREST).save(name)
        print('attract-survey: worst %2d  %03X  diff %5.1f (%4.1f%% px)  cart f%d  port f%d  -> %s'
              % (written, loc, dv, 100 * share, ofr, pfr, name))
        written += 1
        if written >= 24:
            break


if __name__ == '__main__':
    main()
