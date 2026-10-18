#!/usr/bin/env python3
r"""The overworld's WINDOW OBJECT (op 4E -> $C6:E241 / $C6:E27E, the attract's world-5 left-edge wipe) against the
cartridge, on the cartridge's own inputs (field_window_wipe.md sec. 6):

  * `ct.exe --calldump <state> <k> <entry> C21830 in out` runs the oracle to the k-th execution of the entry, dumps the
    128 KB WRAM and prints the registers, the beam and PPU2's latch state there (hSecond, bus2 -- the world engine never
    reads the counter ports itself, so C3/02DD's own toggling and the last byte it read are what the next call sees),
    runs to the far call's landing C2:1830 and dumps again, then samples window 1 on every visible line that follows;
  * `Belthasar --owwincall in <entry> v h hSecond bus2 port` runs the port's translated routine (src/xlat/owwin_c6.c)
    through the host's call glue (seq_xlat.c CtXlatOwWindowCall) on that WRAM and that state;
  * `Belthasar --owwinmask out <ch2 on> <wh0> <wh1>` walks the port's channel-2 model (field.c owWin1Line) over the WRAM
    after the call, with the channel's state and WH0/WH1 as the last NMI pushed them (--calldump's `regs now` line: the
    shadows at the call are already the script's next values, and a channel the call enables starts at the next NMI).

    python tools/owwin_step_cmp.py

Graded: the whole 128 KB after the install call ($C6:E241, the first C3/01E4 call: the fill) and after the first
per-frame call ($C6:E27E's second execution -- its first is the pass-through inside E241): byte for byte, the object's
table ($7E:9176.., 672 bytes) named; and window 1's edges on every sampled line.

THE VIRTUAL BEAM (CLAUDE.md's one exception, the zelda3 model, 2026-09-18): the seed C3/02DD reads the H/V counters --
the world VM's position in the frame, which no C engine has -- so both sides read the virtual beam (snes.c
g_ctVirtualBeam: `$213C` = `$0351`, `$213D` = 00): the port's call always, the oracle with CT_ORACLE_VIRTUAL_BEAM=1.
The hSecond / bus2 inputs the harness still passes no longer reach the seed.
"""
import os
import re
import subprocess

CT = os.path.abspath('build/ct.exe')
FIELD = os.path.abspath('build/Belthasar.exe')
SRC = 'saves/attract_cena_inicial.state'
F0 = 100                                     # the state: the attract 6 frames before the world-5 leg's op 4E
CALLS = [('install', 'C6E241', 1), ('frame', 'C6E27E', 2), ('later', 'C6E27E', 3)]   # 'later': its edges on lines 101-112 straddle a run
                                                                                  # boundary, which the 112-line split must place


def run(argv, env=None, timeout=900):
    return subprocess.run(argv, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout).stdout


def main():
    if not os.path.exists(SRC):
        print('owwin-step: SKIP -- missing %s' % SRC); return
    vb = dict(os.environ, CT_ORACLE_VIRTUAL_BEAM='1')  # the oracle side reads the virtual beam (the docstring)
    # The f100 state is THIS oracle's, made on every run (0.5 s): it carries the call's beam and PPU2 bus from the field's
    # frames before the leg, so a copy kept from an older oracle outlives its rule -- the 2026-09-18 one kept printing
    # h=768 bus2=04 for three days after the drain's and the runner's virtual beam (a07ca0f, 30ccdce) moved them.
    st = 'build/_ow_vb_f%d.state' % F0
    if os.path.exists(st): os.remove(st)
    run([CT, '--peek', SRC, 'build/_owtmp.raw', '--frames', str(F0)], env=dict(vb, PEEK_SAVE=st))
    if not os.path.exists(st): print('owwin-step: FAIL -- no state at f%d' % F0); return
    for name, entry, k in CALLS:
        inp, outp, portp = 'build/_owwin_%s_in.bin' % name, 'build/_owwin_%s_out.bin' % name, 'build/_owwin_%s_port.bin' % name
        d = run([CT, '--calldump', st, str(k), entry, 'C21830', inp, outp], env=vb)
        m = re.search(r'v=(\d+) h=(\d+) latched=\d hSecond=(\d) vSecond=\d bus2=([0-9A-F]{2})', d)
        if not m or not os.path.exists(inp) or not os.path.exists(outp):
            print('owwin-step: FAIL -- calldump %s: %s' % (name, d.strip().splitlines()[-1:])); continue
        v, h, hs, b2 = int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4)
        if os.path.exists(portp): os.remove(portp)
        p = run([FIELD, '--assets', 'ct_assets.dat', '--owwincall', inp, entry, str(v), str(h), str(hs), b2, portp],
                env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'))
        if not os.path.exists(portp):
            print('owwin-step: FAIL -- owwincall %s: %s' % (name, p.strip().splitlines()[-1:])); continue
        i0 = open(inp, 'rb').read(); o = open(outp, 'rb').read(); q = open(portp, 'rb').read()
        x = i0[0x4E] | (i0[0x4F] << 8); T = i0[x + 0x24] | (i0[x + 0x25] << 8)
        tab = sum(1 for j in range(T, T + 0x2A0) if o[j] == q[j]); changed = sum(1 for j in range(T, T + 0x2A0) if o[j] != i0[j])
        whole = sum(1 for j in range(0x20000) if o[j] != q[j])
        print('owwin-step: %s $%s (k=%d, v=%d h=%d hSecond=%d bus2=%s) -> table $7E:%04X %d / 672 bytes identical (%d changed by the '
              'call), $0386 %s, %d byte(s) differ in all 128 KB' % (name, entry, k, v, h, hs, b2, T, tab, changed,
              'same' if o[0x0386] == q[0x0386] else 'DIFFERENT', whole))
        orc = {}
        for mo in re.finditer(r'wipedump: line (\d+) W1=(\d+)\.\.(\d+)', d): orc[int(mo.group(1))] = (int(mo.group(2)), int(mo.group(3)))
        rg = re.search(r'regs now -- W1=(\d+)\.\.(\d+) W2=\d+\.\.\d+ hdma ch2=(\d)', d)
        if not rg: print('owwin-mask: FAIL -- no regs line for %s' % name); continue
        pm = run([FIELD, '--assets', 'ct_assets.dat', '--owwinmask', outp, rg.group(3), rg.group(1), rg.group(2)],
                 env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'))
        port = {}
        for mo in re.finditer(r'owwinmask: line (\d+) W1=(\d+)\.\.(\d+)', pm): port[int(mo.group(1))] = (int(mo.group(2)), int(mo.group(3)))
        lines = sorted(orc)
        agree = [ln for ln in lines if ln in port and orc[ln] == port[ln]]
        bad = next((ln for ln in lines if not (ln in port and orc[ln] == port[ln])), None)
        print('owwin-mask: %s after the call -> window 1 edges agree on %d / %d lines (%d..224)%s'
              % (name, len(agree), len(lines), lines[0] if lines else 0,
                 '' if bad is None else ' (first difference line %d: cartridge %s, port %s)' % (bad, orc.get(bad), port.get(bad))))


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    main()
