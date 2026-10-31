#!/usr/bin/env python3
r"""Sequence-table entry 3 (the attract's Blackbird / Zeal mirror leg, loc 06B's `FF 63`) against the cartridge:
the VRAM + CGRAM the field hands the translated stage, and the stage's picture, pixel for pixel.

    python tools/entry3_stage_cmp.py [port frame] [oracle +frames]

Both sides are derived live, nothing here is a pinned constant:

  * the cartridge under the rule: the attract from power-on with the virtual beam and logical frames, 18 frames before
    `FF 63` (C3:0000 at f18) -- by default this ct.exe's own (stage_rule_cmp.py), with --committed the recipe-made
    saves/attract_entry3_handoff_f6750.state (tools/fixtures/attract.json: power-on + 8776 under the rule). `ct.exe --peek <state> out --frames 18`
    (with CT_ORACLE_VIRTUAL_BEAM=1 CT_ORACLE_LOGICAL=1) dumps the PPU's VRAM at the hand-off (out.vram; out.oam = OAM
    then CGRAM) and `--frames 151` the picture 133 stage frames later (out: 512 x 2*lines [pad,B,G,R], line y at image
    row (y+8)*2 -- ctlib/fb.py's decode).
  * the port: the attract from scene 433 with CT_STAGE_HANDOFF_VRAM (the 64 KB image + the 256 CGRAM words the host
    built for the stage: ct_lz_load_tiles, CtFieldSceneVramTilemaps, fieldPalettePages -- field.c) and CT_SHOTS_AT
    on the frame that presents the same stage frame (PORT_FRAME, argv[1], under the suite's CT_AUDIO_LOCKSTEP=1 /
    CT_NOPACE=1 env, which this tool sets -- without the lockstep the audio's post price moves the attract by a frame
    from run to run; sequence_table.md 11.11).

Graded region by region: the six tile packets ($2000-$4F7F words), the BG3 tileset ($5000-$57FF), the three 64x32
tilemaps ($6000 / $6800 / $7000) and the CGRAM, then the whole 256x224 picture and its two bands. The OBJ region
($0000-$1FFF) is reported, not graded: the stage rewrites it before it draws (11.8).

Then every stage frame from the hand-off's own (+18) to the graded one, as 128x112 thumbnails (ct.exe --peek
PEEK_THUMBS against the port's shots averaged the same way, 2x2 of 256x224 = 4x4 of 512x480), since a single graded
frame can be right while others are wrong. The host stops the stage where snes_runFrame stops (seq_xlat.c stageHook;
sequence_table.md 11.12). The frames before +62 are the opening transition C3/02DD draws; they are graded because the
seed's whole history is the cartridge's under the rule (11.13, tools/seed_history_cmp.py).
Regression check: attract-entry3-stage-picture-vs-oracle.
"""
import os
import struct
import subprocess
import sys

# Under the rule (the zelda3 model, CLAUDE.md; sequence_table.md 11.13): the cartridge from power-on with the virtual
# beam and logical frames (CT_ORACLE_VIRTUAL_BEAM=1 CT_ORACLE_LOGICAL=1), saved 18 frames before its `FF 63` (recipe
# attract_entry3_handoff, power_on 8776; --xlatentries on it: C3:0000 f18 with $0386 = E4). The port's seed history is
# the cartridge's call for call (seed_history_cmp.py), so every stage frame from the hand-off's own is graded.
STATE = 'saves/attract_entry3_handoff_f6750.state'
# A recipe's state belongs to the oracle that made it (stage_rule_cmp.py's docstring), and the stage's colours follow
# the palette records the field hands over (sequence_table.md 11.12). So by default the state is this oracle's, made
# from power-on by stage_rule_cmp.py (cached under build/_stagerule by ct.exe's sha1; C3:0000 on its f18 as well);
# `--committed` reads the file above. The port's picture P is the cartridge's P+2 (docs/TESTING.md, the alignment).
if '--committed' in sys.argv: sys.argv.remove('--committed')
else:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    _here = os.getcwd(); os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    import stage_rule_cmp
    _st = stage_rule_cmp.cartridge_state('entry3', 18, 0x63)[0]
    os.chdir(_here)
    if _st: STATE = _st
RULE = {'CT_ORACLE_VIRTUAL_BEAM': '1', 'CT_ORACLE_LOGICAL': '1'}
HANDOFF_FRAMES = 18       # C3:0000 runs on this frame of the state (--xlatentries, above)
PORT_FRAME = int(sys.argv[1]) if len(sys.argv) > 1 else 8896
ORC_FRAMES = int(sys.argv[2]) if len(sys.argv) > 2 else 151
FRAMES_FROM = int(sys.argv[3]) if len(sys.argv) > 3 else HANDOFF_FRAMES   # the thumbnails from the hand-off's frame
REGIONS = [('tiles $2000-$4F7F', 0x2000, 0x4F80, 1), ('bg3 tiles $5000-$57FF', 0x5000, 0x5800, 1),
           ('bg1 map $6000-$67FF', 0x6000, 0x6800, 1), ('bg2 map $6800-$6FFF', 0x6800, 0x7000, 1),
           ('bg3 map $7000-$77FF', 0x7000, 0x7800, 1), ('obj $0000-$1FFF', 0x0000, 0x2000, 0)]


def oracle(frames, tag):
    # Cached per oracle frame rule: the same state runs another stage under the measurement switches
    # (CT_ORACLE_NMION_HOLD / CT_ORACLE_LINE64 / CT_ORACLE_HOLDAPU / CT_ORACLE_IFAST, CLAUDE.md), so the cache is keyed
    # on them as stage_rule_cmp.py keys its own.
    rule = ''.join(k for k, e in (('h', 'CT_ORACLE_NMION_HOLD'), ('l', 'CT_ORACLE_LINE64'), ('a', 'CT_ORACLE_HOLDAPU'),
                                  ('i', 'CT_ORACLE_IFAST')) if os.environ.get(e, '0' if e == 'CT_ORACLE_HOLDAPU' else '1') not in ('', '0'))
    out = 'build/_e3orc_rule_%s_%s%s.raw' % (os.path.splitext(os.path.basename(STATE))[0], tag, ('_' + rule) if rule else '')
    if not all(os.path.exists(out + ext) for ext in ('', '.vram', '.oam', '.thumbs')):
        env = dict(os.environ, PEEK_THUMBS=out + '.thumbs', PEEK_THUMBS_EVERY='1', **RULE)
        subprocess.run([os.path.abspath('build/ct.exe'), '--peek', STATE, out, '--frames', str(frames)], env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900)
    return out


def thumbs(path):
    d = open(path, 'rb').read(); n = 6 + 128 * 112 * 3; out = {}
    for i in range(0, len(d) - n + 1, n):
        out[struct.unpack('<I', d[i:i + 4])[0]] = d[i + 6:i + n]
    return out


def thumb_of(shot):                  # field.c's CT_SHOTS_THUMBS: each 2x2 block of the 256x224 frame averaged
    d = open(shot, 'rb').read(); w, h = struct.unpack('<II', d[:8]); px = d[8:]; t = bytearray(128 * 112 * 3)
    for ty in range(112):
        for tx in range(128):
            for c in range(3):
                i = ((ty * 2) * w + tx * 2) * 3 + c
                t[(ty * 128 + tx) * 3 + c] = (px[i] + px[i + 3] + px[i + w * 3] + px[i + w * 3 + 3]) // 4
    return bytes(t)


def port():
    vram = 'build/_e3port_handoff.vram'; shots = 'build/_e3port'
    frames = range(PORT_FRAME - (ORC_FRAMES - FRAMES_FROM), PORT_FRAME + 1)
    for p in [vram] + ['%s_f%d.raw' % (shots, f) for f in frames]:
        if os.path.exists(p): os.remove(p)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_STAGE_HANDOFF_VRAM=vram, CT_SHOTS=shots, CT_SHOTS_AT=','.join(str(f) for f in frames),
               CT_SHOTS_EXIT='1')
    subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '433'],
                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1200)
    return vram, '%s_f%d.raw' % (shots, PORT_FRAME), {f - (PORT_FRAME - ORC_FRAMES): '%s_f%d.raw' % (shots, f) for f in frames}


def main():
    if not os.path.exists(STATE):
        print('entry3-stage: SKIP -- missing %s' % STATE); return
    ovr = oracle(HANDOFF_FRAMES, 'handoff'); opic = oracle(ORC_FRAMES, 'f%d' % ORC_FRAMES)
    pvram, ppic, pframes = port()
    for p in (pvram, ppic, ovr + '.vram', ovr + '.oam', opic):
        if not os.path.exists(p): print('entry3-stage: FAIL -- missing %s' % p); return
    p = open(pvram, 'rb').read(); o = open(ovr + '.vram', 'rb').read()
    for name, a, b, graded in REGIONS:
        same = sum(1 for w in range(a, b) if p[2 * w:2 * w + 2] == o[2 * w:2 * w + 2])
        print('entry3-vram: %s -> %d / %d words identical%s' % (name, same, b - a, '' if graded else ' (not graded)'))
    cg = open(ovr + '.oam', 'rb').read()[1024 + 32:1024 + 32 + 512]
    rows = [sum(1 for i in range(r * 16, r * 16 + 16) if p[0x10000 + 2 * i:0x10000 + 2 * i + 2] == cg[2 * i:2 * i + 2])
            for r in range(16)]
    print('entry3-cgram: %d / 256 colours identical, per row %s' % (sum(rows), ' '.join(str(x) for x in rows)))
    d = open(ppic, 'rb').read(); oo = open(opic, 'rb').read()
    w, h = struct.unpack('<II', d[:8]); pix = d[8:]
    for y0, y1, name in ((0, 224, 'whole'), (80, 224, 'mountains rows 80-224'), (0, 80, 'sky rows 0-80')):
        same = total = 0; dsum = 0
        for y in range(y0, min(y1, h)):
            for x in range(w):
                i = ((y + 8) * 2 * 512 + x * 2) * 4; j = (y * w + x) * 3
                pr = (pix[j], pix[j + 1], pix[j + 2]); orc = (oo[i + 3], oo[i + 2], oo[i + 1])
                total += 1
                if pr == orc: same += 1
                dsum += abs(pr[0] - orc[0]) + abs(pr[1] - orc[1]) + abs(pr[2] - orc[2])
        print('entry3-picture: port f%d vs cartridge +%d %s -> %d / %d identical, mean abs diff %.2f'
              % (PORT_FRAME, ORC_FRAMES, name, same, total, dsum / (3.0 * total)))
    ot = thumbs(opic + '.thumbs'); bad = []
    for of in sorted(pframes):
        pt = thumb_of(pframes[of]) if os.path.exists(pframes[of]) else None
        if pt is None or ot.get(of) != pt: bad.append(of)
    print('entry3-frames: port f%d..f%d vs cartridge +%d..+%d -> %d / %d stage frames identical (thumbnails)%s'
          % (PORT_FRAME - (ORC_FRAMES - FRAMES_FROM), PORT_FRAME, FRAMES_FROM, ORC_FRAMES, len(pframes) - len(bad),
             len(pframes), (', differ at +' + ' +'.join(str(f) for f in bad[:12])) if bad else ''))
    for f in pframes.values():
        if os.path.exists(f): os.remove(f)


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    main()
