#!/usr/bin/env python3
r"""The defeat's exit against the rule's cartridge (disasm/notes/battle_defeat_flow.md, "The module's exit after a defeat").

    python tools/battle_defeat_exit_cmp.py

tools/battle_ab.py's enemy B6 on GATO's arena (`enemies B6`: it wipes the party; $2A1F bit5 set -> the scripted-loss
continue C0/0304), both sides from the same WRAM, watching the exit C1/8164 -> C1/8423 -> CF/FBE5 -> CD/03B4 ->
CD/0415-0438 -> C0/1BE6 -> the ack:

  29AD    the RNG cursor handed back        -- C1/8443 (the module's tail)
  A4      CF/FBE5's loop counter           -- the NMI's CF/E477 on each $E5 frame
  2603    Crono's sheet curHP              -- C1/B442's writeback (0), then CF/FCCF's floor (1)
  2653    Marle's sheet curHP              -- the same
  CE16    CD/0516                          -- CD/03B4, after CD/039E's fifteen waits
  CA25    the band's commands 0C / 0E / 08 -- CD/0516, CD/03E3, CD/040D
  1E10    C0/1BE6's first store            -- CD/043E, after CD/0428-0436's SPC poll
  ack     $0118 -> 01                      -- the field's C0/18C1

Every change from f900 (the wipe is at f807-f911) to f970, on each side:

    defeat-exit: <q> cartridge f<N>=<v> .. port f<M>=<v> .. -> SAME | DIFFER
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import battle_ab as B  # noqa: E402

REGS = [(0x29AD, 1), (0x00A4, 1), (0x2603, 2), (0x2653, 2), (0xCE16, 1), (0xCA25, 1), (0x1E10, 1), (0x0118, 1)]
NAMES = {0x29AD: '29AD', 0x00A4: 'A4', 0x2603: '2603', 0x2653: '2653', 0xCE16: 'CE16', 0xCA25: 'CA25', 0x1E10: '1E10',
         0x0118: 'ack'}
ENEMY, LO, HI = 0xB6, 900, 970


def main():
    os.chdir(B.ROOT)
    B.regions = lambda: REGS
    B.fields = lambda: [(NAMES[a], a, 0, n) for a, n in REGS]
    pk = B.pokes_for([0, 1, 2], extra={0x1111: ENEMY})
    regs, otl, ptl, cth, pth, plog = B.run(pk, [(HI + 20, 'n', 1)], HI + 10, 'defeat_exit')
    same = 0
    for a, n in REGS:
        q = NAMES[a]
        o = [(f, v) for f, v in otl[q] if LO <= f <= HI]
        p = [(f, v) for f, v in ptl[q] if LO <= f <= HI]
        ok = o == p
        same += ok
        print('defeat-exit: %s cartridge %s | port %s -> %s' % (
            q, ' '.join('f%d=%X' % e for e in o) or '-', ' '.join('f%d=%X' % e for e in p) or '-',
            'SAME' if ok else 'DIFFER'))
    print('defeat-exit: %d / %d quantities frame-exact' % (same, len(REGS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
