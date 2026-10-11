#!/usr/bin/env python3
r"""A battle ACTION, port against the rule's cartridge, from the same WRAM -- the synthetic-battle harness.

    python tools/battle_ab.py tech <char> <k>          # char 0..6's k-th single tech (all eight learned), solo party
    python tools/battle_ab.py item <itemId>            # a consumable on the default target, solo Crono
    python tools/battle_ab.py item <itemId> marle [dead]   # Crono + Marle, the item on Marle (hurt; or dead: 0 HP, $80)
        [inv=2400:BD,2500:05,...] [down=N]            #   another bag (':' pairs), N Downs in the item list
    python tools/battle_ab.py census [chars] [-jN]     # every single tech of the listed chars (default 0-6), N at once
    python tools/battle_ab.py items [-jN]              # every battle consumable, Crono solo at 20 HP / 1 MP
    python tools/battle_ab.py enemies [ids] [-tN] [-jN]  # any enemy (hex ids, default 00-FF) in GATO's arena, the party
                                                       #   idle for N frames (1500): every monitored change, both sides
    python tools/battle_ab.py combos [ids] [-jN]       # every dual / triple tech (hex ids, default 39-74), its
                                                       #   participants as the party, only its bit learned
    python tools/battle_ab.py cursor <party> <keys> [pokes]   # the command menu's own bytes over a drive (party e.g.
                                                       #   0,1,2; keys e.g. d,r,a,b: one press every 12 frames from
                                                       #   f640; pokes e.g. 2400=BD,2500=05: an item in the bag)
    python tools/battle_ab.py strip <party> <keys> <frame> [pokes]   # the HUD strip's pixels at that frame, both sides
    (-v on any of them: both sides' effect lists, and each change at its frame from the action's $A013 rise)

The battle is GATO's (saves/battle_re/gato_wait.state, the field before the A that starts it), made into ANY fight by
poking the field WRAM before the trigger, on both sides alike -- the party $2980-$2982, the learned-tech seeds
$2837+char (+ the learn count $2830+char), MP 99 on the sheet ($2600 + char*0x50 + 7 / + 9), an item into $2400.
The battle's own staging then reads them exactly as it reads a real save.

  cartridge  build/ct.exe under the rule (CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1): `--monitor` of every
             combatant's curHP (+03), curMP (+07) and status (+1D..+22: the ailments +1D..+1F, the buffs +20..+22) in the $5E2D + c*0x80 blocks and the action
             bytes ($A013, $95DB, $9609, $AE93, $9877); `--peek` with PEEK_THUMBS every frame;
  port       build/ct_field.exe --scene 6 --loadwram <the same WRAM> --walkscript a:8 (the A that starts the fight), the
             battle's buttons through CT_BATTLE_SCRIPT with CT_BATTLE_SCRIPT_ABS=1 at the cartridge's frames minus the
             port's `battlephase: begins at field fN`; CT_FIELD_MEMMON of the same bytes (the field host's $7E IS the
             battle's), CT_SHOTS_THUMBS every frame.

Prints, per side, the PC action's MP spend and span ($A013 up -> down), the change sequence of every monitored byte
from the dispatch to the span's end (the effect: damage, heal, status), and the picture over the action -- the port's
frame P against the cartridge's thumbnail P + L for the lag L in -8..8 with the fewest differing pixels (a pixel
differs when a channel differs by more than 24), the differing frames at that lag and their worst.
"""
import os
import re
import struct
import subprocess
import sys

sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]
import numpy as np  # noqa: E402  (tools/dis.py would shadow the stdlib's dis that numpy imports)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', '_battle_ab')
STATE = 'saves/battle_re/gato_wait.state'
REC = 6 + 128 * 112 * 3
RULE = {'CT_ORACLE_LOGICAL': '1', 'CT_ORACLE_VIRTUAL_BEAM': '1'}
BTN = {'a': 0x100, 'b': 0x01, 'u': 0x10, 'd': 0x20, 'l': 0x40, 'r': 0x80, 'L': 0x400, 'R': 0x800, 'n': 0}   # l/r the pad, L/R the shoulders
COMBATANTS = 11
VERBOSE = False
MENU_AT = 640         # the drive's first button: after GATO's first action (c453-c606) on every party measured


def regions():
    """The monitored regions (the oracle's --monitor takes at most 32): per combatant +03..+08 (curHP, maxHP, curMP)
    and +1D..+22 (status: the ailments +1D..+1F, the buffs +20 Haste / +21 Protect-Barrier-Shield / +22), then the action
    bytes."""
    out = []
    for c in range(COMBATANTS):
        b = 0x5E2D + c * 0x80
        out += [(b + 3, 6), (b + 0x1D, 6)]
    return out + [(0xA013, 1), (0x95DB, 1), (0x9609, 1), (0xAE93, 1), (0x9877, 1), (0x2500, 1)]


def fields():
    """(label, region address, byte offset in it, size): what the reports read out of the regions."""
    out = []
    for c in range(COMBATANTS):
        b = 0x5E2D + c * 0x80
        out += [('c%d.HP' % c, b + 3, 0, 2), ('c%d.MP' % c, b + 3, 4, 2), ('c%d.ST' % c, b + 0x1D, 0, 3), ('c%d.BF' % c, b + 0x1D, 3, 3)]
    return out + [('$%04X' % a, a, 0, 1) for a in (0xA013, 0x95DB, 0x9609, 0xAE93, 0x9877, 0x2500)]


def pokes_for(party, learned=None, mp=99, extra=None):
    p = {0x2980 + i: (party[i] if i < len(party) else 0x80) for i in range(3)}
    for c in party:
        p[0x2837 + c] = 0xFF if learned is None else learned.get(c, 0xFF)
        p[0x2830 + c] = 8
        sh = 0x2600 + c * 0x50
        p[sh + 7], p[sh + 8], p[sh + 9], p[sh + 10] = mp & 0xFF, mp >> 8, mp & 0xFF, mp >> 8
    p.update(extra or {})
    return p


def drive_tech(k):
    """Down (Att -> Tech), A (the list), Down x k, A (the tech), A (its target): (frame, button, hold) from MENU_AT."""
    seq, f = [], MENU_AT
    for b in ['d', 'a'] + ['d'] * k + ['a', 'a']:
        seq.append((f, b, 2)); f += 12
    return seq


def drive_item(slot, down=0):
    """Down Down (Att -> Item), A (the bag), Down x slot, A (the item), Down x down (the target cursor), A."""
    seq, f = [], MENU_AT
    for b in ['d', 'd', 'a'] + ['d'] * slot + ['a'] + ['d'] * down + ['a']:
        seq.append((f, b, 2)); f += 12
    return seq


def oracle_input(drive, total):
    parts, f = ['100:8'], 8
    for at, b, hold in drive:
        if at > f: parts.append('0:%d' % (at - f)); f = at
        parts.append('%x:%d' % (BTN[b], hold)); f += hold
    parts.append('0:%d' % max(1, total - f))
    return ','.join(parts)


def port_script(drive, start, total):
    parts, f = [], 0
    for at, b, hold in drive:
        bf = at - start
        if bf > f: parts.append('n:%d' % (bf - f)); f = bf
        parts.append('%s:%d' % (b, hold)); f += hold
    parts.append('n:%d' % max(1, total - start - f))
    return ','.join(parts)


def thumbs(path):
    if not os.path.exists(path): return {}
    d = open(path, 'rb').read(); out = {}
    for i in range(len(d) // REC):
        f, loc = struct.unpack('<IH', d[i * REC:i * REC + 6])
        out[f] = np.frombuffer(d[i * REC + 6:(i + 1) * REC], np.uint8).reshape(112, 128, 3).astype(np.int16)
    return out


def parse_mon(text, prefix):
    rows = {}
    for l in text.splitlines():
        m = re.match(prefix, l)
        if not m: continue
        f = int(m.group(1))
        rows[f] = {int(a, 16) & 0xFFFF: v for a, v in re.findall(r'\b(?:7E)?([0-9A-F]{4})=([0-9A-F]+)', m.group(2))}
    return rows


def timeline(rows, regs):
    """{label: [(frame, value)]}: every change of every field, values as ints (little endian)."""
    tl = {lab: [] for lab, a, o, n in fields()}
    last = {}
    for f in sorted(rows):
        for lab, a, o, n in fields():
            v = rows[f].get(a)
            if v is None or len(v) < 2 * (o + n): continue
            iv = int.from_bytes(bytes.fromhex(v[2 * o:2 * (o + n)]), 'little')
            if last.get(lab) != iv:
                tl[lab].append((f, iv)); last[lab] = iv
    return tl


def run(pokes, drive, total, tag):
    os.makedirs(OUT, exist_ok=True)
    tag += os.environ.get('BATTLE_AB_TAG', '')            # (BATTLE_AB_TAG=_x: keep two runs' files apart)
    regs = regions()
    mon = ','.join('7E%04X:%d' % (a, n) for a, n in regs)
    pk = ','.join('%X=%02X' % (a, v) for a, v in sorted(pokes.items()))
    inp = oracle_input(drive, total)
    env = dict(os.environ, **RULE, PEEK_POKE=pk)
    ct = os.path.abspath('build/ct.exe')
    o = subprocess.run([ct, '--monitor', mon, '--from', STATE, '--input', inp, '--diff'], env=env,
                       capture_output=True, text=True, errors='replace').stdout
    oth = os.path.join(OUT, tag + '_c.thumbs')
    if os.path.exists(oth): os.remove(oth)
    subprocess.run([ct, '--peek', STATE, os.path.join(OUT, tag + '_c.raw'), '--btnscript', inp],
                   env=dict(env, PEEK_THUMBS=oth, PEEK_THUMBS_EVERY='1'), stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    # the port: the same WRAM, the A that starts the fight, then the battle's buttons
    w = bytearray(open(os.path.join(ROOT, STATE), 'rb').read()[0x45:0x45 + 0x20000])
    for a, v in pokes.items(): w[a] = v
    lw = os.path.join(OUT, tag + '.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + bytes(w))
    pth = os.path.join(OUT, tag + '_p.thumbs')
    start = 220
    for attempt in range(2):
        if os.path.exists(pth): os.remove(pth)
        # CT_AUDIO_SEEDSTATE: the port's SPC is the state's own (src/field.c fieldSeedStateLoad; read only under the rule,
        # CT_ORACLE_NMION_HOLD=1 + CT_ORACLE_IFAST=1): the WRAM image is that state's, so its driver is the console's and the
        # boot's song upload is priced on the rule's clock -- a cold driver keeps the old clock, the battle NMI's work
        # charged, and the field-started fight ran 6 frames behind the rule's cartridge.
        penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
                    CT_AUDIO_SEEDSTATE=STATE,
                    CT_BATTLE_SCRIPT_ABS='1', CT_BATTLE_SCRIPT=port_script(drive, start, total),
                    CT_FIELD_MEMMON=','.join('%04X:%X' % (a, n) for a, n in regs),
                    CT_SHOTS_THUMBS=pth, CT_SHOTS_THUMBS_EVERY='1', CT_SHOTS=os.path.join(OUT, tag + '_p'),
                    CT_SHOTS_AT=str(total), CT_SHOTS_EXIT='1')
        p = subprocess.run([os.path.abspath('build/ct_field.exe'), '--assets', 'ct_assets.dat', '--scene', '6',
                            '--loadwram', lw, '--walkscript', 'a:8'], env=penv, capture_output=True, text=True,
                           errors='replace').stdout
        m = re.search(r'battlephase: begins at field f(\d+)', p)
        if not m or int(m.group(1)) == start: break
        start = int(m.group(1))
    orows = parse_mon(o, r'\s*f(\d+)\s+(.*)')
    prows = parse_mon(p, r'fmemmon f(\d+) loc=\w+\s+(.*)')
    cth, pth_ = thumbs(oth), thumbs(pth)
    # The every-frame thumbnails are ~65 MB a side and read into memory just above: left behind they filled the disk
    # (2026-10-08: 1101 of them, 68 GB under build/_battle_ab, 100 GB more in one worktree). The oracle's sidecars and the
    # seed go too; the two final frames strip_report reads (<tag>_c.raw, <tag>_p_f<total>.raw) stay. BATTLE_AB_KEEP=1 keeps all.
    if not os.environ.get('BATTLE_AB_KEEP'):
        for f in (oth, pth, lw) + tuple(os.path.join(OUT, tag + '_c.raw' + e) for e in ('.wram', '.vram', '.oam')):
            try:
                os.remove(f)
            except OSError:
                pass
    return regs, timeline(orows, regs), timeline(prows, regs), cth, pth_, p


def value_at(ch, f):
    v = None
    for cf, cv in ch:
        if cf > f: break
        v = cv
    return v


def pc_action(tl, expect, after):
    """The PC action: the first $A013 rise after the drive's last press (`after`) whose action id $AE93 is `expect` (the
    tech or the item id, set at the dispatch just before the rise), and the fall after it -- the span. The first frame
    of the window is the rise's frame; `f0` is kept for the report."""
    rises = [f for f, v in tl['$A013'] if v == 1 and f >= after - 4]
    falls = [f for f, v in tl['$A013'] if v == 0]
    for up in rises:
        ids = [value_at(tl['$AE93'], up + k) for k in range(-4, 5)]
        if expect in ids:
            dn = min((f for f in falls if f > up), default=None)
            return up, up, dn
    return None


def effect(tl, act):
    if not act: return []
    f0, up, dn = act
    out = []
    for lab, a, o, n in fields():
        if lab.startswith('$'): continue                        # the action bytes: their order is not the effect
        for f, v in tl[lab]:
            if f0 - 8 <= f <= (dn if dn else f0 + 600):
                out.append((f, lab, v))
    return sorted(out)


def picture(cth, pth, act):
    if not act or not act[2]: return None
    f0, up, dn = act
    best = None
    for lag in range(-8, 9):
        tot, nd, worst = 0, 0, 0
        for pf in range(up, dn + 1):
            a, b = pth.get(pf), cth.get(pf + lag)
            if a is None or b is None: continue
            k = int((np.abs(a - b).max(axis=2) > 24).sum())
            tot += k; nd += k > 0; worst = max(worst, k)
        if best is None or tot < best[1]: best = (lag, tot, nd, worst)
    return best


def report(label, regs, otl, ptl, cth, pth, expect, after):
    oa, pa = pc_action(otl, expect, after), pc_action(ptl, expect, after)
    oe, pe = effect(otl, oa), effect(ptl, pa)
    ov = [(n, v) for f, n, v in oe]
    pv = [(n, v) for f, n, v in pe]
    pic = picture(cth, pth, pa)
    span = lambda a: (a[2] - a[1]) if a and a[2] else None
    same = ov == pv
    out = ['%s: cartridge %s span %s | port %s span %s | effect %s%s' % (
        label, 'up c%d' % oa[1] if oa else 'NO ACTION', span(oa),
        'up p%d' % pa[1] if pa else 'NO ACTION', span(pa),
        'SAME' if same else 'DIFFERS', ('; picture lag %+d: %d differing frame(s), worst %d px' % (pic[0], pic[2], pic[3]))
        if pic else '')]
    if not same or VERBOSE:
        out.append('   cartridge: ' + ' '.join('%s=%X' % x for x in ov[:24]))
        out.append('   port     : ' + ' '.join('%s=%X' % x for x in pv[:24]))
    if VERBOSE:                                                   # -v: each change at its frame from the action's rise
        out.append('   cartridge (rel): ' + (' '.join('%+d:%s=%X' % (f - oa[1], n, v) for f, n, v in oe[:24]) if oa else ''))
        out.append('   port      (rel): ' + (' '.join('%+d:%s=%X' % (f - pa[1], n, v) for f, n, v in pe[:24]) if pa else ''))
    return '\n'.join(out)


def job(label, pokes, drive, total, tag, expect):
    regs, otl, ptl, cth, pth, plog = run(pokes, drive, total, tag)
    return report(label, regs, otl, ptl, cth, pth, expect, drive[-1][0])


# ANY ENEMY in GATO's arena (memory ct-enemy-id-poke-fixture): C0/0557 stages the fight's enemy ids from the formation's
# field objects (`LDA $1101,Y / STA $7E29C4,X`), and GATO is object 8 of gato_wait, so poking `$1111` makes the fight load
# that enemy's stats, AI scripts and techs from the ROM on both sides. Valid for the AI, the effects and the timing -- the
# sprite is GATO's field graphics with the new id's frames (garbage on both sides), so no picture is graded here.
def enemy_report(label, otl, ptl, start, total):
    """Every monitored change from `start` on, as (frame, field, value), on both sides: the first frame they differ."""
    def ev(tl):
        return sorted((f, lab, v) for lab, a, o, n in fields() for f, v in tl[lab] if start <= f <= total)
    oe, pe = ev(otl), ev(ptl)
    acts = lambda tl: [f for f, v in tl['$A013'] if v == 1 and f >= start]
    first = None
    for i in range(max(len(oe), len(pe))):
        a = oe[i] if i < len(oe) else None
        b = pe[i] if i < len(pe) else None
        if a != b: first = (i, a, b); break
    head = '%s: cartridge %d changes, %d action(s) %s | port %d changes, %d action(s) %s | %s' % (
        label, len(oe), len(acts(otl)), acts(otl)[:6], len(pe), len(acts(ptl)), acts(ptl)[:6],
        'IDENTICAL' if first is None else 'first difference at change #%d: cartridge %s, port %s' % first)
    return head


def enemy_job(eid, party=(0, 1, 2), total=1500):
    pk = pokes_for(list(party), extra={0x1111: eid})
    drive = [(total + 10, 'n', 1)]                                 # no press: the party waits, the enemy acts
    regs, otl, ptl, cth, pth, plog = run(pk, drive, total, 'enemy_%02X' % eid)
    return enemy_report('enemy %02X' % eid, otl, ptl, 280, total)


# the battle-usable consumables (CC/05CC effect records; CD/5C26 configs), Tonic..Shield
ITEMS = [0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE]


def tech_id(c, k):
    """C2/BD40[char] + k: the char's k-th single tech (every one learned)."""
    return [1, 9, 17, 25, 33, 41, 49][c] + k


def item_pokes(iid):
    """Crono solo, hurt (curHP 20) and drained (curMP 1), five of the item in the first bag slot."""
    return pokes_for([0], extra={0x2400: iid, 0x2500: 5, 0x2603: 20, 0x2604: 0, 0x2607: 1, 0x2608: 0})


def item_pokes_marle(iid, dead=False):
    """Crono + Marle, Marle hurt (curHP 20, curMP 1) or dead (curHP 0, status $80 -- the sheet's $266D, as the
    revive.state seed), five of the item in the first bag slot."""
    ex = {0x2400: iid, 0x2500: 5, 0x2653: 20, 0x2654: 0, 0x2657: 1, 0x2658: 0}
    if dead: ex.update({0x2653: 0, 0x2654: 0, 0x266D: 0x80})
    return pokes_for([0, 1], extra=ex)


# The dual / triple techs (disasm/notes/battle_dual_triple_techs.md sec. 1): the participants, CC/1BEB[id*11] & 7F ->
# CC/249F[group*3 ..] (battle_dual_triple_combo_table.txt) -- participant 0 is the actor.
CHARS = ['Crono', 'Marle', 'Lucca', 'Robo', 'Frog', 'Ayla', 'Magus']
COMBO_GROUP = {}
for _ids, _grp in (((0x39,), (1, 0)), ((0x3A, 0x3B), (0, 1)), ((0x3C, 0x3D, 0x3E), (0, 2)), ((0x3F, 0x41), (0, 3)),
                   ((0x40,), (3, 0)), ((0x42,), (0, 4)), ((0x43, 0x44), (4, 0)), ((0x45, 0x46, 0x47), (5, 0)),
                   ((0x48, 0x49, 0x4A), (1, 2)), ((0x4B, 0x4D), (1, 3)), ((0x4C,), (3, 1)), ((0x4E, 0x4F), (4, 1)),
                   ((0x50,), (1, 4)), ((0x51, 0x52, 0x53), (5, 1)), ((0x54, 0x55), (3, 2)), ((0x56,), (2, 3)),
                   ((0x57, 0x58, 0x59), (5, 2)), ((0x5A, 0x5B), (3, 4)), ((0x5C,), (4, 3)), ((0x5D, 0x5E, 0x5F), (5, 3)),
                   ((0x60, 0x62), (4, 5)), ((0x61,), (5, 4)), ((0x63, 0x64, 0x65), (4, 2)),
                   ((0x66,), (0, 1, 2)), ((0x67,), (1, 0, 3)), ((0x68,), (0, 4, 1)), ((0x69,), (5, 0, 1)),
                   ((0x6A,), (3, 0, 2)), ((0x6B,), (0, 2, 4)), ((0x6C,), (5, 0, 2)), ((0x6D,), (0, 3, 4)),
                   ((0x6E,), (0, 3, 5)), ((0x6F,), (5, 0, 4)), ((0x70,), (1, 2, 6)), ((0x71,), (2, 3, 6)),
                   ((0x72,), (4, 3, 5)), ((0x73,), (1, 2, 5)), ((0x74,), (4, 1, 3))):
    for _t in _ids: COMBO_GROUP[_t] = _grp


def combo_pokes(t):
    """The combo's participants as the party (participant 0 in seat 0), every single learned, MP 99, and ONLY this
    combo's learned bit -- a dual: $283E + (t-39)/3, bit 7 - (t-39)%3; a triple: $284D + (t-66) bit 7 (C1/CA1A,
    C1/CCCB) -- so each seat's list is its eight singles, one header and this combo (CC/E755): eight Downs reach it."""
    party = list(COMBO_GROUP[t])
    ex = {}
    if t < 0x66:
        ex[0x283E + (t - 0x39) // 3] = 0x80 >> ((t - 0x39) % 3)
    else:
        ex[0x284D + (t - 0x66)] = 0x80
    return pokes_for(party, extra=ex)


def combo_ready(pokes, n):
    """The cartridge's first frame with every participant ready ($A6DE, the ready count -- C1/1B2E), without a press:
    an open submenu freezes the ATBs in Wait mode ($99E0 = $99E1, C1/1172) and a partner not ready greys the combo (C1/0EE1
    rule 4), so the drive opens the list only after it."""
    env = dict(os.environ, **RULE, PEEK_POKE=','.join('%X=%02X' % (a, v) for a, v in sorted(pokes.items())))
    o = subprocess.run([os.path.abspath('build/ct.exe'), '--monitor', '7EA6DE:1', '--from', STATE, '--input', '100:8,0:1400',
                        '--diff'], env=env, capture_output=True, text=True, errors='replace').stdout
    for f, r in sorted(parse_mon(o, r'\s*f(\d+)\s+(.*)').items()):
        v = int(r.get(0xA6DE, '0'), 16)
        if n <= v < 0x80:                                 # (the field's byte reads 80 before the fight's boot clears it)
            return f
    return None


def drive_combo(ready):
    """From the later of MENU_AT and every participant ready (+12): Down (Att -> Comb), A, eight Downs (the singles, the
    header skipped by C1/13F4), A (the combo), A (its target)."""
    seq, f = [], max(MENU_AT, (ready or MENU_AT) + 12)
    for b in ['d', 'a'] + ['d'] * 8 + ['a', 'a']:
        seq.append((f, b, 2)); f += 12
    return seq


# The command menu's bytes (disasm/notes/battle_menu_pass.md): the chooser $95D5, the submenu level $95DB, each seat's command
# row $95DC,X and its memory $9916,X, the head cursor $A6DD, the item list's row / top $95E5 / $95E6, the target select
# $9609, the rebuild mark $A6DF and the stashed submenu $A86B.
CURSOR_REGS = [(0x95D5, 1), (0x95DB, 1), (0x95DC, 3), (0x9916, 3), (0xA6DD, 1), (0x95E5, 2), (0x9609, 1), (0xA6DF, 1),
               (0xA86B, 1)]
CURSOR_FIELDS = [('95D5', 0x95D5, 0, 1), ('95DB', 0x95DB, 0, 1), ('95DC', 0x95DC, 0, 1), ('95DD', 0x95DC, 1, 1),
                 ('95DE', 0x95DC, 2, 1), ('9916', 0x9916, 0, 1), ('9917', 0x9916, 1, 1), ('9918', 0x9916, 2, 1),
                 ('A6DD', 0xA6DD, 0, 1), ('95E5', 0x95E5, 0, 1), ('95E6', 0x95E5, 1, 1), ('9609', 0x9609, 0, 1),
                 ('A6DF', 0xA6DF, 0, 1), ('A86B', 0xA86B, 0, 1)]


def cursor_report(party, keys, otl, ptl, first):
    """Each byte's changes from the drive's first press on, as values and as frames from that side's first change (the
    two sides' menu passes may sit at another phase against the press -- the battle's entry, battle_menu_pass.md
    sec. 9 -- so the timing is graded relative)."""
    def rel(tl):
        ch = {lab: [(f, v) for f, v in tl[lab] if f >= first] for lab, a, o, n in CURSOR_FIELDS}
        fs = [f for c in ch.values() for f, v in c]
        t0 = min(fs) if fs else 0
        return {lab: [(f - t0, v) for f, v in c] for lab, c in ch.items()}
    o, p = rel(otl), rel(ptl)
    out, nv, nt = [], 0, 0
    for lab, a, of, n in CURSOR_FIELDS:
        sv = [v for f, v in o[lab]] == [v for f, v in p[lab]]
        st = o[lab] == p[lab]
        nv += sv; nt += st
        if not st or VERBOSE:
            out.append('   %s %s  cartridge %s' % (lab, 'same' if st else 'VALUES SAME, TIMING DIFFERS' if sv else 'DIFFERS',
                                                   ' '.join('%+d:%X' % x for x in o[lab])))
            out.append('   %s %s  port      %s' % (lab, ' ' * len('same' if st else 'VALUES SAME, TIMING DIFFERS' if sv else 'DIFFERS'),
                                                   ' '.join('%+d:%X' % x for x in p[lab])))
    return '\n'.join(['cursor party %s keys %s: %d bytes, values %d/%d same, timing %d/%d same' % (
        party, keys, len(CURSOR_FIELDS), nv, len(CURSOR_FIELDS), nt, len(CURSOR_FIELDS))] + out)


def raw_rgb(path):
    """A frame dump: the cartridge's --peek raw (512 x 480 [pad,B,G,R]) or the port's CT_SHOTS raw ([w][h] RGB)."""
    d = open(path, 'rb').read()
    if len(d) == 512 * 480 * 4:                            # the oracle's 512x480 [pad,B,G,R], pixel-doubled (ctlib/fb.py)
        f = np.frombuffer(d, np.uint8).reshape(480, 512, 4)[::2, ::2]
        return f[:, :, [3, 2, 1]].astype(np.int16)
    w, h = struct.unpack('<II', d[:8])
    return np.frombuffer(d[8:8 + w * h * 3], np.uint8).reshape(h, w, 3).astype(np.int16)


def strip_report(party, keys, frame, tag):
    """The strip's rows (the port's 152..213, the HUD band and its frame -- the sprites above it left out: the selection
    bracket's grey pulse $A4EF runs from the band's arm, which the port reaches 3 frames early, battle_menu_pass.md sec. 9)
    against the cartridge's at the vertical offset that fits best (the oracle's dump has 8 overscan lines on top)."""
    c = raw_rgb(os.path.join(OUT, tag + '_c.raw'))
    pp = os.path.join(OUT, '%s_p_f%d.raw' % (tag, frame))
    if not os.path.exists(pp): return 'strip party %s keys %s f%d: NO PORT SHOT' % (party, keys, frame)
    p = raw_rgb(pp)
    ps = p[152:214]
    best = None
    for dy in range(0, 17):
        cs = c[152 + dy:214 + dy]
        if cs.shape != ps.shape: continue
        n = int((np.abs(cs - ps).max(axis=2) > 0).sum())
        if best is None or n < best[1]: best = (dy, n)
    return 'strip party %s keys %s f%d: offset %d, %d differing px of %d' % (party, keys, frame, best[0], best[1],
                                                                            ps.shape[0] * ps.shape[1])


def main():
    import concurrent.futures
    os.chdir(ROOT)
    global VERBOSE
    VERBOSE = '-v' in sys.argv[1:]
    a = [x for x in sys.argv[1:] if not x.startswith('-j') and not x.startswith('-t') and x != '-v']
    jobs = int(next((x[2:] for x in sys.argv[1:] if x.startswith('-j')), '1') or 1)
    if not a: print(__doc__); return 1
    work = []
    if a[0] == 'tech':
        c, k = int(a[1]), int(a[2])
        work.append(('tech char%d #%d' % (c, k), pokes_for([c]), drive_tech(k), 1500, 'tech_%d_%d' % (c, k), tech_id(c, k)))
    elif a[0] == 'census':
        chars = [int(x) for x in a[1].split(',')] if len(a) > 1 else range(7)
        for c in chars:
            for k in range(8):
                work.append(('tech char%d #%d' % (c, k), pokes_for([c]), drive_tech(k), 1500, 'tech_%d_%d' % (c, k),
                             tech_id(c, k)))
    elif a[0] == 'item':
        iid = int(a[1], 16)
        # inv=<addr=val,...>: the bag instead of the item in slot 0 (e.g. gaps, a spent stack); down=<n>: Downs in the list
        opt = dict(x.split('=', 1) for x in a[2:] if '=' in x)
        a = [x for x in a if '=' not in x]
        inv = {int(k, 16): int(v, 16) for k, v in (kv.split(':') if ':' in kv else kv.split('=') for kv in
                                                 opt['inv'].split(','))} if 'inv' in opt else None
        down = int(opt.get('down', '0'))
        if len(a) > 2 and a[2] == 'marle':
            dead = len(a) > 3 and a[3] == 'dead'
            pk = item_pokes_marle(iid, dead)
            if inv is not None: pk = {k: v for k, v in pk.items() if not (0x2400 <= k < 0x2600)}; pk.update(inv)
            work.append(('item %02X on Marle%s%s' % (iid, ' (dead)' if dead else '', ' (down %d)' % down if down else ''), pk,
                         drive_item(down, 1), 1500, 'item_%02X_m%s%d' % (iid, 'd' if dead else '', down), iid))
        else:
            work.append(('item %02X' % iid, item_pokes(iid), drive_item(0), 1400, 'item_%02X' % iid, iid))
    elif a[0] == 'cursor':
        global regions, fields
        regions, fields = (lambda: CURSOR_REGS), (lambda: CURSOR_FIELDS)
        party = [int(x) for x in a[1].split(',')]
        drive = [(MENU_AT + 12 * i, b, 2) for i, b in enumerate(a[2].split(','))]
        extra = {int(k, 16): int(v, 16) for k, v in (kv.split('=') for kv in a[3].split(','))} if len(a) > 3 else {}
        regs, otl, ptl, cth, pth, plog = run(pokes_for(party, extra=extra), drive, drive[-1][0] + 120,
                                             'cursor_' + a[1].replace(',', '') + '_' + a[2].replace(',', ''))
        print(cursor_report(a[1], a[2], otl, ptl, MENU_AT))
        return 0
    elif a[0] == 'strip':
        party = [int(x) for x in a[1].split(',')]
        drive = [(MENU_AT + 12 * i, b, 2) for i, b in enumerate(a[2].split(','))]
        frame = int(a[3])
        extra = {int(k, 16): int(v, 16) for k, v in (kv.split('=') for kv in a[4].split(','))} if len(a) > 4 else {}
        tag = 'strip_' + a[1].replace(',', '') + '_' + a[2].replace(',', '')
        run(pokes_for(party, extra=extra), drive, frame, tag)
        print(strip_report(a[1], a[2], frame, tag))
        return 0
    elif a[0] == 'items':
        for iid in ITEMS:
            work.append(('item %02X' % iid, item_pokes(iid), drive_item(0), 1400, 'item_%02X' % iid, iid))
    elif a[0] in ('enemy', 'enemies'):
        ids = [int(x, 16) for x in a[1].split(',')] if len(a) > 1 else list(range(0x00, 0xFF))   # (FF: no enemy, the oracle spins)
        total = int(next((x[2:] for x in sys.argv[1:] if x.startswith('-t')), '1500'))
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as ex:
            for text in ex.map(lambda e: enemy_job(e, total=total), ids):
                print(text, flush=True)
        return 0
    elif a[0] in ('combo', 'combos'):
        ids = ([int(x, 16) for x in a[1].split(',')] if len(a) > 1 else list(range(0x39, 0x75)))
        def one(t):
            pk = combo_pokes(t)
            drive = drive_combo(combo_ready(pk, len(COMBO_GROUP[t])))
            return job('combo %02X %s' % (t, '+'.join(CHARS[c] for c in COMBO_GROUP[t])), pk, drive,
                       drive[-1][0] + 1200, 'combo_%02X' % t, t)
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as ex:
            for text in ex.map(one, ids):
                print(text, flush=True)
        return 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as ex:
        for text in ex.map(lambda w: job(*w), work):
            print(text, flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
