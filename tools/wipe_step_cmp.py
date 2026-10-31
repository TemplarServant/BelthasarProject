#!/usr/bin/env python3
r"""The field's window wipe (C3/01E4 through C0/22FA, op 0xFF 0x92-0x95) against the cartridge, two ways:

  * one call on the cartridge's own inputs: `ct.exe --wipedump <state> <k> in out` runs the oracle to the k-th call of
    C3/000E, dumps its 128 KB WRAM and the beam there (vPos, hPos), runs to the RTL's landing C0:22FE and dumps the WRAM
    again; `Belthasar --wipecall in port` runs the port's native routine (src/wipe.c CtWipeRoutine, with the registers the
    field's ct_vm.c ctWipeFrame hands it) on that WRAM. Graded: the two HDMA tables + the working copy at $7F:17C8-$19A7
    (the routine's output), DP $0300's page ($50-$57: the counters and the seed pointer) and $0386 (C3/02DD's
    accumulator), byte for byte, and the whole 128 KB but the 32 stack bytes below the caller's S (dead_stack).
  * the mask: `Belthasar --wipemask out` gives the window-2 edge the field's HDMA channel 7 hands every scanline from that
    WRAM (field.c fieldWipeWindow: FD/C477's table choice, FD/C1EE's $2128/$2129 by $013C, the E4/F0 line split), and
    `--wipedump` also samples the cartridge's window 2 on every visible line after the routine returns (the frame
    its table masks), as `ct.exe --hdma`'s scanline tracer samples.

    python tools/wipe_step_cmp.py

Both wipes of the attract's opening are graded: loc 16E's `FF 94` (mode 1, the right edge, from f574 of
saves/attract_cena_inicial_rule.state -- the attract from power-on + 2159 under the rule, made by a recipe:
tools/fixtures/attract_rule.json) and loc 01C's `FF 93` (mode 2, the right edge, from f765).

The virtual beam (the zelda3 model, CLAUDE.md): the seed C3/02DD reads the beam counters, whose real values depend on
the CPU's cycle-exact history. Both sides read the virtual beam (snes.c
g_ctVirtualBeam: `$213C` = `$0351`, `$213D` = 00): the port's call always, the oracle with CT_ORACLE_VIRTUAL_BEAM=1 from
the state on (the 16E wipe's `$0386` reaches the 01C wipe, so the states are the virtual run's). The state itself is the
rule's run from power-on; the frames after it run in real time with the virtual beam, so the routine returns inside the
visible frame and the lines after it can be sampled (under the whole rule the field's main loop runs from the vblank's
start and its call returns before line 0). The two states are this oracle's, made on every run (~4 s): a copy kept
from an older oracle outlives its rule (field_window_wipe.md "The virtual beam").
"""
import os
import re
import subprocess
import sys

ROM = 'Chrono_Trigger_USA_SNES.sfc'
CT = os.path.abspath('build/ct.exe')
FIELD = os.path.abspath('build/Belthasar.exe')
SRC = 'saves/attract_cena_inicial_rule.state'
WIPES = [('16E', 573, 3), ('01C', 764, 5)]      # (loc, frame the state is saved at, which call to grade)


def run(argv, env=None, timeout=900):
    return subprocess.run(argv, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout).stdout


# The stack below the caller's S: the routine's pushes (PHB / PHD / PHP / PHA / PHX / PHY, the fill's PHB) and C3/02DD's
# return address, dead once it returns -- the native routine has no CPU stack and leaves those bytes alone. 32 bytes
# cover every push the call makes (13 on the field's call, 16 on the world's: E27E's JSL first).
def dead_stack(s):
    return set(range(s - 31, s + 1))


VB = dict(os.environ, CT_ORACLE_VIRTUAL_BEAM='1')    # the oracle side reads the virtual beam (the docstring)


def state_at(frame):
    st = 'build/_wipe_vb_f%d.state' % frame
    if os.path.exists(st): os.remove(st)        # made on every run: the state carries the oracle's rule (the docstring)
    run([CT, '--peek', SRC, 'build/_wipetmp.raw', '--frames', str(frame)], env=dict(VB, PEEK_SAVE=st))
    return st if os.path.exists(st) else None


def main():
    if not os.path.exists(SRC):
        print('wipe-step: SKIP -- missing %s' % SRC); return
    for loc, f0, k in WIPES:
        st = state_at(f0)
        if not st: print('wipe-step: FAIL -- no state at f%d' % f0); continue
        inp, outp, portp = 'build/_wipe_%s_in.bin' % loc, 'build/_wipe_%s_out.bin' % loc, 'build/_wipe_%s_port.bin' % loc
        maskp = 'build/_wipe_%s_mask.bin' % loc
        d = run([CT, '--wipedump', st, str(k), inp, outp, maskp], env=VB)
        m = re.search(r'frame (\d+) .* v=(\d+) h=(\d+)', d)
        if not m or not os.path.exists(inp) or not os.path.exists(outp):
            print('wipe-step: FAIL -- wipedump on %s: %s' % (loc, d.strip().splitlines()[-1:])); continue
        frame, v, h = int(m.group(1)), int(m.group(2)), int(m.group(3))
        sp = int(re.search(r' S=([0-9A-F]{4}) ', d).group(1), 16); dead = dead_stack(sp)
        if os.path.exists(portp): os.remove(portp)
        p = run([FIELD, '--assets', 'ct_assets.dat', '--wipecall', inp, portp],
                env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'))
        if not os.path.exists(portp):
            print('wipe-step: FAIL -- wipecall on %s: %s' % (loc, p.strip().splitlines()[-1:])); continue
        o = open(outp, 'rb').read(); q = open(portp, 'rb').read(); i0 = open(inp, 'rb').read()
        def same(a, b): return sum(1 for j in range(a, b) if o[j] == q[j])
        tab = same(0x117C8, 0x119A8); pg = same(0x0300, 0x0400); acc = o[0x0386] == q[0x0386]
        whole = sum(1 for j in range(0x20000) if o[j] != q[j] and j not in dead)
        changed = sum(1 for j in range(0x117C8, 0x119A8) if o[j] != i0[j])
        print('wipe-step: %s call %d (mode %d, v=%d h=%d) -> tables $7F:17C8-19A7 %d / 480 bytes identical (%d changed by the '
              'call), page $0300 %d / 256, $0386 %s, %d byte(s) differ in all 128 KB but the dead stack below S=%04X'
              % (loc, k, o[0x0350], v, h, tab, changed, pg, 'same' if acc else 'DIFFERENT', whole, sp))
        # The mask: the cartridge's window 2 on the visible lines after the routine returned (wipedump samples them as
        # --hdma's tracer does, from the line the RTL lands on to 224 -- the frame the HDMA channel draws from this call's
        # table), against the edges the port's channel model gives from the WRAM as it stands after C0/00C7 -- the
        # point the port's frame renders from ($0153 flipped, this call's tables).
        orc = {}
        for mo in re.finditer(r'wipedump: line (\d+) (?:W1=\d+\.\.\d+ )?W2=(\d+)\.\.(\d+)', d): orc[int(mo.group(1))] = (int(mo.group(2)), int(mo.group(3)))
        if not os.path.exists(maskp): print('wipe-mask: FAIL -- no %s' % maskp); continue
        pm = run([FIELD, '--assets', 'ct_assets.dat', '--wipemask', maskp],
                 env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'))
        port = {}
        for mo in re.finditer(r'wipemask: line (\d+) W2=(\d+)\.\.(\d+)', pm): port[int(mo.group(1))] = (int(mo.group(2)), int(mo.group(3)))
        lines = sorted(orc)
        agree = [ln for ln in lines if ln in port and orc[ln] == port[ln]]
        first_bad = next((ln for ln in lines if not (ln in port and orc[ln] == port[ln])), None)
        print('wipe-mask: %s after call %d -> window 2 edges agree on %d / %d lines (%d..224)%s'
              % (loc, k, len(agree), len(lines), lines[0] if lines else 0,
                 '' if first_bad is None else ' (first difference line %d: cartridge %s, port %s)'
                 % (first_bad, orc.get(first_bad), port.get(first_bad))))


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    main()
