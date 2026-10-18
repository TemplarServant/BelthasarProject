#!/usr/bin/env python3
r"""C0/21E1's entries 4 and 5 -- the circle as the COLOUR window (`FF 97` / `FF 98`) -- port against the rule's cartridge.

    python tools/field_colcircle_cmp.py

From saves/field_re/shop90_lake.state (loc 005, no window effect of its own), with the operands of scene 051's
`FF 97 87 98 60 / F1 9F 00 / ... / FF 98` (Leene's Square, script $055 at 0x093C): at frame 150 both sides take the stores
those ops make, by WRAM pokes (ct.exe PEEK_POKE_AT, Belthasar CT_POKEWRAM / CT_POKEWRAM_AT) -- the centre $013A / $013B =
87 / 98, the radius $013C, the target $013D = 60, and F1 9F 00's $0121 = $0122 = 9F, $0BE0 = 17 -- and C0/21E1 runs the
ROM's entry from there:

  open   $0139 = 04, $013C = 00: C0/232A grows the circle a step a frame through C3/0077's argument 1 (each line pair
         once: half as tall) into the buffer $0153 picks ($7F:17C8 / $7F:1AC8), WOBJSEL 20 / CGWSEL 12, `TSB $28`;
  close  $0139 = 05, $013C = 40: C0/238A shrinks it, and at 0 C0/23E2 tears it down.

Every frame from the poke to 120 on (the cartridge's --monitor, the port's CT_FIELD_MEMMON, the same frame number -- the
field's WRAM clock): $0139, $013C, $0153, $0128, the window registers' shadows and both 448-byte buffers. Prints, per run,
the frames compared and the first that differs; then three pictures a run, each cartridge `--peek` frame N against the
port's shots N + d for d in -4..+4 (the field's picture is P + 2: d = -2): the best d and its differing pixels. Loc 005,
like scene 051, keeps BG3 on the sub screen (TM 13 / TS 04): the circle limits that sum, and adds the fixed blue where the
sub screen is transparent (field.c fieldColWindowApply).
docs/ROADMAP.md F19, disasm/notes/field_iris_window.md.
"""
import os
import re
import struct
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = 'saves/field_re/shop90_lake.state'
LAKEWRAM = 'saves/field_re/shop90.lakewram'
SCENE = '5'
AT = 150
SPAN = 120
COMMON = '7E013A=87,7E013B=98,7E013D=60,7E0121=9F,7E0122=9F,7E0BE0=17'
RUNS = (('open', COMMON + ',7E013C=00,7E0139=04', (AT + 30, AT + 60, AT + 100)),
        ('close', COMMON + ',7E013C=40,7E0139=05', (AT + 20, AT + 50, AT + 66)))
CART = '7E0139:1,7E013C:1,7E0153:1,7E0128:1,7E0BD9:3,7E0BDE:1,7E0121:2,7F1522:2,7F17C8:448,7F1AC8:448'
PORT = '0139:1,013C:1,0153:1,0128:1,0BD9:3,0BDE:1,0121:2,11522:2,117C8:1C0,11AC8:1C0'


def series(lines, rx, key):
    """Each frame's last value of every region, keyed by its flat offset (7E0139 -> 0x00139, 7F17C8 -> 0x117C8)."""
    out, cur = {}, {}
    for line in lines:
        m = re.match(rx, line)
        if m:
            for a, v in re.findall(key, m.group(2)): cur[int(a, 16) & 0x1FFFF] = v
            out[int(m.group(1))] = dict(cur)
    return out


def cart_rgb(path):
    o = open(path, 'rb').read()
    return b''.join(bytes((o[i + 3], o[i + 2], o[i + 1])) for y in range(224) for i in
                    (((y + 8) * 1024 + x * 2) * 4 for x in range(256)))


def port_rgb(path):
    d = open(path, 'rb').read()
    w = struct.unpack('<I', d[:4])[0]
    return b''.join(d[8 + y * w * 3:8 + y * w * 3 + 768] for y in range(224))


def at(st, f):
    k = [x for x in st if x <= f]
    return st[max(k)] if k else {}


def main():
    os.chdir(ROOT)
    last = AT + SPAN
    out = os.path.join(ROOT, 'build', '_colcircle')
    os.makedirs(out, exist_ok=True)
    for name, poke, pics in RUNS:
        shots = sorted({f + d for f in pics for d in range(-4, 5)})
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
                   CT_AUDIO_SEEDSTATE=STATE, CT_POKEWRAM=poke, CT_POKEWRAM_AT=str(AT), CT_FIELD_MEMMON=PORT,
                   CT_SHOTS=os.path.join(out, name), CT_SHOTS_AT=','.join(map(str, shots)))
        r = subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', SCENE, '--loadwram',
                            LAKEWRAM, '--walkscript', 'n:%d' % (last + 2), '--selftest', '--frames', str(last + 2)], env=env,
                           capture_output=True, text=True, errors='replace').stdout
        ps = series(r.splitlines(), r'fmemmon f(\d+) loc=\w+\s+(.*)', r'\b([0-9A-F]{4,5})=([0-9A-F]+)')
        cenv = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_POKE_AT='%d:%s' % (AT, poke))
        r = subprocess.run([os.path.abspath('build/ct.exe'), '--monitor', CART, '--from', STATE, '--input', '0:%d' % (last + 2),
                            '--diff'], env=cenv, capture_output=True, text=True, errors='replace').stdout
        cs = series(r.splitlines(), r'\s*f(\d+)\s+(.*)', r'(7[EF][0-9A-F]{4})=([0-9A-F]+)')
        n, bad = 0, None
        for f in range(AT, last):
            c, p = at(cs, f), at(ps, f)
            n += 1
            if c != p and bad is None:
                diff = sorted(k for k in set(c) | set(p) if c.get(k) != p.get(k))
                bad = (f, diff[0], c.get(diff[0], '')[:24], p.get(diff[0], '')[:24])
        for f in pics:
            craw = os.path.join(out, 'c_%s_%d.raw' % (name, f))
            subprocess.run([os.path.abspath('build/ct.exe'), '--peek', STATE, craw, '--frames', str(f)], env=cenv,
                           capture_output=True)
            cart, best = cart_rgb(craw), None
            for d in range(-4, 5):
                fn = os.path.join(out, '%s_f%d.raw' % (name, f + d))
                if os.path.exists(fn):
                    port = port_rgb(fn)
                    px = sum(1 for k in range(0, len(cart), 3) if cart[k:k + 3] != port[k:k + 3])
                    if best is None or px < best[1]: best = (d, px)
            print('colcircle: %s picture cartridge f%d best port f%d (%+d): %d px differ' % (name, f, f + best[0], best[0], best[1]))
        st = at(cs, last - 1)
        print('colcircle: %s f%d-%d %s; at the end $0139 %s $013C %s' % (name, AT, last - 1,
              '%d frames, state and both window buffers equal on every one' % n if bad is None else
              'DIFFER from f%d at %05X (cartridge %s, port %s)' % bad, st.get(0x139), st.get(0x13C)))


if __name__ == '__main__':
    main()
