#!/usr/bin/env python3
r"""The party EXCHANGE (menu mode 5, screen $07), port against the rule's cartridge, every frame.

    python tools/menu_exchange_cmp.py

GATO's field -- the recipe gato_wait (tools/recipes.py), its state on the cartridge and its WRAM dump on the port, so both
sides start from one image, the SPC's included (CT_AUDIO_SEEDSTATE: the song task's frames depend on the driver's phase) --
with the storyline at 0x49 and Robo in the reserve -- `7F0000=49, 2982=02, 2983=03` and `0126=00` (no window side left
over, or Y only flips the window: C0/1985) -- on both sides. The drive: Y (C0/1916 -> C0/1985 -> C0/19CE `JSL C2/8000` with
A = 5), Right (the reserve column), A (Robo picked, C2/C674), Left (Marle), A (the swap C2/C70C: Robo in, Marle out), B (the
close C2/CC0E -> C2/82B2, the field's C0/19D2). The port's SPC is the state's own and runs in lockstep (CT_AUDIO_SEEDSTATE,
CT_AUDIO_LOCKSTEP): the screen's song task -- `14 3E`, `81 01 FF`, and at the close `81 08 01`, the `$2141` wait, `11 42`,
`81 10 FF` -- prices frames on both sides.

Compared, frame for frame, each byte while the screen owns it: the party $2980-$2988 and the field's $0119 from the press
to the end; the picks $0F00-$0F01, the roster $0F02-$0F0C, $0F0D, the song bytes $0F0E-$0F0F and the menu's brightness
$0D10 from C2/940D's fill (C + 1, f37) to the field's C0/0905 (f281); $0067 from C2/807F (C + 15, f51); the cursor
$54 from C2/C649 (P, f86). (C2/940D's fill of $0000-$00FF is not the port's: the field keeps that page live, and the
cartridge's reload rebuilds it -- so $54 / $67 hold the field's bytes until the screen writes them. $68 is each task's
own, swapped by the scheduler at C2/81FF / C2/8211: not compared.) Prints
`menu-exchange: <n> frame-fields, <d> differing` and the cartridge's frames of each milestone.
disasm/notes/menu2_form_screen.md 2.14.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = 'build/recipes/gato_wait.state'
WRAM = 'build/recipes/gato_wait.raw.wram'
POKES = {0x10000: 0x49, 0x2982: 0x02, 0x2983: 0x03, 0x0126: 0x00}
REGS = [(0x2980, 9), (0x0067, 1), (0x0054, 1), (0x0F00, 2), (0x0F02, 11), (0x0F0D, 1), (0x0F0E, 2), (0x0D10, 1),
        (0x0119, 1), (0x0F10, 14), (0x0D0C, 4), (0x9240, 32), (0x0DA0, 1)]
SPAN = {0x2980: (20, 330), 0x0119: (20, 330), 0x0067: (51, 280), 0x0054: (86, 280), 0x0F00: (37, 280), 0x0F02: (37, 280),
        0x0F0D: (37, 280), 0x0F0E: (37, 280), 0x0D10: (37, 280),
        0x0F10: (86, 280), 0x0D0C: (86, 280), 0x9240: (86, 280), 0x0DA0: (146, 230)}   # (the BG3 wave: C2/CE86 / C2/CE9D at P, FF/F716 after)
# (button, the cartridge's press frame): Y, Right, A, Left, A, B. The port presses a frame later (the cartridge latches the
# pad a frame after it goes down); Y goes through CT_HOLDBTN ($00F2 bit 2), the rest through the drive.
PRESSES = [(0x002, 20), (0x080, 124), (0x100, 146), (0x040, 168), (0x100, 190), (0x001, 232)]
END = 330
OUT = os.path.join(ROOT, 'build', '_menu_exchange')


def oracle():
    parts, t = [], 0
    for m, f in PRESSES:
        if f > t:
            parts.append('0:%d' % (f - t))
        parts.append('%X:2' % m)
        t = f + 2
    parts.append('0:%d' % (END - t))
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_POKE=','.join('%X=%02X' % kv for kv in sorted(POKES.items())))
    mon = ','.join('7E%04X:%d' % r for r in REGS)
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc'),
                        '--monitor', mon, '--from', os.path.join(ROOT, STATE), '--input', ','.join(parts), '--diff'],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    return rows(r.stdout, r'\s*f(\d+)\s+(.*)')


def port():
    w = bytearray(open(os.path.join(ROOT, WRAM), 'rb').read())
    for a, v in POKES.items():
        w[a] = v
    b = b'#lakewram\nRAM:131072:' + bytes(w)
    os.makedirs(OUT, exist_ok=True)
    seed = os.path.join(OUT, 'seed%d.lakewram' % os.getpid())
    open(seed, 'wb').write(bytes(b))
    walk, t, hold = [], 0, []
    key = {0x080: 'r', 0x040: 'l', 0x100: 'a', 0x001: 'b'}
    for m, f in PRESSES:
        pf = f + 1
        if m == 0x002:
            hold.append('4@%d:2' % pf)
            continue
        if pf > t:
            walk.append('n:%d' % (pf - t))
        walk.append('%s:1' % key[m])
        t = pf + 1
    walk.append('n:%d' % (END - t))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=os.path.join(ROOT, STATE), CT_HOLDBTN=','.join(hold),
               CT_FIELD_MEMMON=','.join('%04X:%X' % r for r in REGS))
    r = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '6',
                        '--loadwram', seed, '--selftest', '--frames', str(END), '--walkscript', ','.join(walk)],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    try:
        os.remove(seed)
    except OSError:
        pass
    return rows(r.stdout, r'fmemmon f(\d+) loc=\w+\s+(.*)')


def rows(text, pat):
    out = {}
    for line in text.splitlines():
        m = re.match(pat, line)
        if m:
            out[int(m.group(1))] = {int(a, 16) & 0xFFFF: v for a, v in re.findall(r'\b(?:7E)?([0-9A-F]{4})=([0-9A-F]+)', m.group(2))}
    return out


def carried(r):
    st, out = {}, {}
    for f in range(0, END + 1):
        st.update(r.get(f, {}))
        out[f] = dict(st)
    return out


def first(c, addr, pred, lo=0):
    for f in range(lo, END + 1):
        v = c[f].get(addr)
        if v is not None and pred(v):
            return f
    return -1


def main():
    if subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'recipes.py'), 'make', 'gato_wait'], cwd=ROOT,
                      stdout=subprocess.DEVNULL).returncode:
        sys.exit('menu-exchange: the recipe gato_wait failed')
    o, p = carried(oracle()), carried(port())
    diff, n = [], 0
    for a, _ in REGS:
        lo, hi = SPAN[a]
        for f in range(lo, hi + 1):
            n += 1
            if o[f].get(a) != p[f].get(a):
                diff.append('f%d %04X c=%s p=%s' % (f, a, o[f].get(a), p[f].get(a)))
    print('menu-exchange: %d frame-fields, %d differing' % (n, len(diff)))
    for d in diff[:12]:
        print('  ' + d)
    print('menu-exchange: cartridge mode 5 open f%d, cursor f%d, fade-in done f%d, swap f%d (%s), field back f%d' % (
        first(o, 0x0119, lambda v: v == '00', 21), first(o, 0x0054, lambda v: v == '01', 40),
        first(o, 0x0D10, lambda v: v == '0F', 60), first(o, 0x2980, lambda v: v != o[0][0x2980], 1),
        o[END].get(0x2980), first(o, 0x0119, lambda v: v == '0F', 250)))


if __name__ == '__main__':
    main()
