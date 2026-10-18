#!/usr/bin/env python3
r"""The overworld's SPRITE PIPELINE against the cartridge, on the attract's 2300 A.D. walkers.

    python tools/ow_objlayer_cmp.py

What an overworld sprite is on the cartridge, and what the port now builds the same way:
  * the per-frame DRAW LIST in WRAM ($7E:B000 nodes, head $B500; C2/0C4D empties it, the anim sub-VM's op 4 enqueues
    through C2/0B53 in descending Y) and the OAM SHADOW C2/0CEA -> C2/0D08 decodes it into ($7E:0700/$0900);
  * the OBJ GRAPHICS and PALETTES the world loader takes from the ROM for whoever is in the party (C2/2768, C2/2B61,
    C2/2882, C2/28ED, C2/2D13) -- the captured world blob had the wrong members in OBJ palettes 5 and 6;
  * the op-0x34 native C2/786F, which gives the party's ids to the walkers' scripts ($1BA3-$1BA5): without it Robo left
    the dome on Crono's script.

The cartridge side is saves/attract_2300ad_f5800.state + 131 frames (the three walkers mid-stride). The port side is the
attract run from scene 433 with CT_OWDUMP over its whole 2300 A.D. visit; the frame whose walker blocks (class $0F63,
attr $A8/$AA/$AC: script pc, X, Y, anim pointer, dwell) equal the cartridge's is the aligned one -- found, not assumed,
so a shift in the attract's timeline elsewhere does not break the grade. At that frame all 128 OAM entries (a parked
entry is only its Y), VRAM $2000-$3FFF (the OBJ name table the walkers use) and the 256 colours at $7F:8C00 must be equal.
"""
import os
import subprocess
import sys

CT = os.path.abspath('build/ct.exe')
FIELD = os.path.abspath('build/Belthasar.exe')
STATE = 'saves/attract_2300ad_f5800.state'
ORACLE_FRAME = 131
PORT_WINDOW = (6678, 6948)
TMP = 'build/_owobjcmp'


def walkers(w):
    out = []
    for b in range(0x0B30, 0x1B30, 0x40):
        if (w[b] | (w[b + 1] << 8)) == 0x0F63 and w[b + 0x0F] in (0xA8, 0xAA, 0xAC):
            out.append((b, bytes(w[b + 7:b + 9]), bytes(w[b + 0x14:b + 0x16]), bytes(w[b + 0x18:b + 0x1A]),
                        bytes(w[b + 0x0B:b + 0x0F])))
    return out


def main():
    os.makedirs(TMP, exist_ok=True)
    orc = os.path.join(TMP, 'orc.raw')
    subprocess.run([CT, '--peek', STATE, orc, '--btnscript', '0:%d' % ORACLE_FRAME],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    ow = open(orc + '.wram', 'rb').read()
    ov = open(orc + '.vram', 'rb').read()
    ow_walk = walkers(ow)
    if len(ow_walk) != 3:
        sys.exit('owobj-cmp: REFUSING -- the cartridge has %d walker blocks at f%d, not 3' % (len(ow_walk), ORACLE_FRAME))
    for f in os.listdir(TMP):
        if f.startswith('p_f'):
            os.remove(os.path.join(TMP, f))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_OWDUMP=os.path.join(TMP, 'p'), CT_OWDUMP_AT='%d-%d' % PORT_WINDOW)
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433', '--frames', str(PORT_WINDOW[1] + 1), '--selftest'],
                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900)
    hit = None
    for fr in range(PORT_WINDOW[0], PORT_WINDOW[1] + 1):
        p = os.path.join(TMP, 'p_f%d' % fr)
        if not os.path.exists(p):
            continue
        d = open(p, 'rb').read()
        if all(t in walkers(d[:0x20000]) for t in ow_walk):     # the port also keeps a parked $0D70 ($0F63 at 0,0)
            hit = (fr, d)
            break
    if not hit:
        print('owobj-cmp: no port frame in f%d-f%d has the cartridge\'s three walker blocks' % PORT_WINDOW)
        return 1
    fr, d = hit
    pw, pv = d[:0x20000], d[0x20000:0x30000]
    # C2/034D parks an entry by its Y alone ($E0) and leaves X, tile and attribute as the last frame that used it left
    # them, which is history; an entry is equal when both are parked, or when all four bytes and its two high-table
    # bits agree.
    def entry(w, i):
        if w[0x701 + i * 4] == 0xE0:
            return None
        return (bytes(w[0x700 + i * 4:0x704 + i * 4]), (w[0x900 + (i >> 2)] >> ((i & 3) * 2)) & 3)
    oam = sum(1 for i in range(128) if entry(pw, i) == entry(ow, i))
    shown = sum(1 for i in range(128) if entry(ow, i) is not None)
    vram = sum(1 for i in range(0x2000, 0x4000) if pv[i] == ov[i])
    pal = sum(1 for c in range(256) if pw[0x18C00 + c * 2:0x18C02 + c * 2] == ow[0x18C00 + c * 2:0x18C02 + c * 2])
    print('owobj-cmp: port f%d holds the cartridge f%d walkers (3 blocks) -> OAM %d/128 entries (%d shown), VRAM OBJ %d/8192, '
          'palette %d/256 equal' % (fr, ORACLE_FRAME, oam, shown, vram, pal))
    return 0 if (oam == 128 and shown > 0 and vram == 8192 and pal == 256) else 1


if __name__ == '__main__':
    sys.exit(main())
