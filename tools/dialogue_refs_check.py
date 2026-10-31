#!/usr/bin/env python
"""Check every dialogue box a location script can open against the string the ROM's text engine reads for it.

A box op carries a string byte (0xBB/0xC1/0xC2 `op idx`, 0xC0/0xC3/0xC4 `op idx cfg`); C0/3592 stores it in $2A, op
0xB8 puts the table in $2B-D (C0/3557), and C0/20F2 hands both to the text engine ($020C, $020D-F), whose entry C2/57E7
fetches the string with
    LDA $0C / AND #00FF / ASL / TAY / LDA [$0D],Y / STA $31     ; $33 = $0F
i.e. the u16 at table + 2*idx, in the table's own bank, with no count and no bound.

The check walks each location's event script (scene header -> script id, assets/script.py's op lengths, from every fn
pointer and on through internal RETURNs), pairs each box op with the table its script's 0xB8 sets, and compares the
ct_assets.dat dialogue entry (what Belthasar shows, CtAssetString) with that ROM read, decoded as the packer decodes
it. A missing entry is an empty window that never types, never reaches state 7 and never takes the press (e.g. loc
010's obj9 `BB 26`). Regression check: assets-dialogue-every-script-box-reads-the-rom.

    python tools/dialogue_refs_check.py [ct_assets.dat] [rom]
prints `dialogue-refs: N/N script boxes read the ROM's string (C2/57E7), T tables, M missing, D differ` twice: for
`dat` (the pack Belthasar loads) and for `extractor` (assets/text.py's scan_dialogue_tables run now, the packer's own
call) -- so a stale pack and a broken extractor both show.
"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '.')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools')); sys.path.insert(0, os.path.join(ROOT, 'assets'))
from ctlib import script as cs          # noqa: E402
import script as S                      # noqa: E402
import text                             # noqa: E402

T_DIALOGUE = 9
BOX_OPS = (0xBB, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4)


def packed_dialogue(path):
    d = open(path, 'rb').read()
    n = struct.unpack('<I', d[8:12])[0]
    out = {}
    for i in range(n):
        t, id_, o, ln = struct.unpack('<4I', d[12 + i * 16:28 + i * 16])
        if t != T_DIALOGUE:
            continue
        b = d[o:o + ln]; cnt = b[0] | (b[1] << 8); p = 2; strs = []
        for _ in range(cnt):
            sl = b[p] | (b[p + 1] << 8); strs.append(b[p + 2:p + 2 + sl]); p += 2 + sl
        out[id_] = strs
    return out


def script_ops(body, starts):
    """Every op reachable by a linear decode from each fn pointer, not stopping at a RETURN (a function's code after
    its first 00 is reached by jumps -- ctlib's iter_ops stops there)."""
    seen = {}
    for st in starts:
        o = st
        while 0 <= o < len(body) and o not in seen:
            try:
                ln = S.op_len(body, o)
            except IndexError:
                break
            seen[o] = (body[o], ln); o += max(1, ln)
    return seen


def script_boxes(rom):
    """[(loc, addr, op, idx, table-or-None)] for every box op of every location script, the table from its 0xB8."""
    done_scripts = set(); out = []
    for scene in range(512):
        try:
            objs, body, si = cs.load(rom, scene)
        except Exception:
            continue
        if not objs or si in done_scripts:
            continue
        done_scripts.add(si)
        starts = sorted(set(p for ob in objs for p in ob if 0 <= p < len(body)))
        ops = script_ops(body, starts)
        tables = sorted(set((body[o + 1] | (body[o + 2] << 8) | (body[o + 3] << 16)) - 0xC00000
                            for o, (op, ln) in ops.items() if op == 0xB8 and o + 3 < len(body)))
        for o, (op, ln) in sorted(ops.items()):
            if op in BOX_OPS and o + 1 < len(body):
                if len(tables) != 1:
                    print('dialogue-refs: loc %03X script %d @%04X op %02X idx %d -- %d tables in the script'
                          % (scene, si, o, op, body[o + 1], len(tables)))
                out.append((scene, o, op, body[o + 1], tables[0] if len(tables) == 1 else None))
    return out


def grade(label, boxes, packed, rom, subs):
    tables_used = set(); total = missing = differ = notable = 0
    for scene, o, op, idx, t in boxes:
        total += 1
        if t is None:
            notable += 1
            continue
        tables_used.add(t)
        ptr = rom[t + idx * 2] | (rom[t + idx * 2 + 1] << 8)                       # C2/57EE LDA [$0D],Y
        want = text.decode_string(rom, (t & 0xFF0000) + ptr, subs, '\f', True, '\x02' * 3).encode('utf-8')[:0xFFFF]
        have = packed.get(t)
        if have is None or idx >= len(have):
            missing += 1
            print('dialogue-refs %s: MISSING loc %03X @%04X op %02X table $%06X idx %d (packed %s) -- ROM: %r'
                  % (label, scene, o, op, t + 0xC00000, idx, 'none' if have is None else len(have), want[:48]))
        elif have[idx] != want:
            differ += 1
            print('dialogue-refs %s: DIFFER loc %03X @%04X table $%06X idx %d' % (label, scene, o, t + 0xC00000, idx))
    ok = total - missing - differ - notable
    print('dialogue-refs %s: %d/%d script boxes read the ROM\'s string (C2/57E7), %d tables, %d missing, %d differ'
          % (label, ok, total, len(tables_used), missing, differ))


def main():
    dat = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'ct_assets.dat')
    romp = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc')
    rom = _romlib.load(romp)
    subs = text.load_dict(rom)
    boxes = script_boxes(rom)
    grade('dat', boxes, packed_dialogue(dat), rom, subs)
    # the packer's own call (extract_assets.py pack_dialogue), serialized as _strlist does
    ext = text.scan_dialogue_tables(rom, subs, pb='\f', namesub=True, ind='\x02' * 3)
    grade('extractor', boxes, {t: [s.encode('utf-8')[:0xFFFF] for s in strs] for t, strs in ext.items()}, rom, subs)


if __name__ == '__main__':
    main()
