#!/usr/bin/env python3
r"""A PC PHYSICAL runs its own bank-CE script on the anim interpreter -- graded against the LOGICAL-FRAME oracle
(CLAUDE.md's one exception; disasm/notes/oracle_logical_frames.md, disasm/notes/battle_pc_physical_script.md).

    python tools/pc_physical_logical_cmp.py press|crit|nocrit

C1/432C loads the physical exactly as C1/4617 loads a tech: $9877 = CD/4000 (far) / CD/4007 (close) / CD/400E (crit,
`$AE9B` bit 7) [char], the script CD/5EC6[$9877] in bank CE, and its op 01 ends the action (C1/40CE). Three scenarios:

  press   the cartridge: gato_press_action.state, A held 20 of every 60 frames. Crono's slash twice (sel 00: the op-18
          grid walk, the weapon sounds 7C/7D, the return super CE/D776 with its zero-step `98 02 19` from home and, the
          second time, a real slide home) and Marle's crossbow crit twice (sel 0F: the bolt's second flight from the
          point op 44 stores). The port: gato_t0.raw.wram, A pressed every 41 frames.
  crit    the cartridge: saves/battle_re/bat3crit.state (crit rate $FF), Crono's crit on the forest's Hetake (sel 01,
          CD/401F's config, the weighted leap CE/D795). The port: forest_pre.raw.wram on scene 19, CT_BATTLE_CRITRATE=255.
  nocrit  the cartridge: the press run saved at its f300 -- after the boot's FD/B233 computed the crit rates, which a
          poke at load would not survive -- with both rates poked to 0 ($5E72, $5EF2): Marle's far shot (sel 02). The
          port: as press, CT_BATTLE_CRITRATE=0.

The two fights are not the same fight (another fixture, another input), so the actions are paired by ORDER and each is
graded against its own $A013 rise (the port's pc-act): the span (rise -> fall; pc-act -> act-end) and, on tracks 0, 3
and 8, every change of the track's cursor $5DBD + 4t (the port's CT_BATTLE_ASCIP) inside it.
"""
import os
import re
import subprocess
import sys

TRACKS = (0, 3, 8)


def press_script(n, lead=0):
    """A held 20 of every 60 frames; `lead` frames sooner from the second press on (the rule's shorter boot, below)."""
    return ','.join(['100:20,0:%d' % (40 - lead)] + ['100:20,0:40'] * (n - 1))


def port_press_script():
    return 'n:40,a:1,n:40,a:1,' + ','.join(['n:40,a:1'] * 40) + ',n:600'


# Under the rule (CT_ORACLE_NMION_HOLD=1 + CT_ORACLE_IFAST=1, CLAUDE.md) the cartridge is the rule's oracle: + the virtual
# beam (tools/gato_logical_cmp.py says why -- with every CPU cycle free the real beam stands while the CPU works). Its
# boot from gato_press_action.state (the field before GATO, as gato_wait: C1/0000 at f222 in every model) reaches the band
# seed 17 frames sooner (`$CA1E = FF` f279 against f296, the first iteration head f355 against f372), so the presses
# after the one that starts the fight come LEAD frames sooner: the same fight at the same moments, 17 frames earlier.
RULE = os.environ.get('CT_ORACLE_NMION_HOLD', '1') != '0' and os.environ.get('CT_ORACLE_IFAST', '1') != '0'
LEAD = 17 if RULE else 0

SCEN = {
    'press': dict(state='gato_press_action.state', input=press_script(70, LEAD), frames=1880, poke=None, pre=0,
                  argv=['--battletest', 'saves/battle_re/gato_t0.raw.wram'],
                  env=dict(CT_BATTLE_SCRIPT=port_press_script(), CT_BATTLE_DEBUGEND='1800')),
    'crit': dict(state='saves/battle_re/bat3crit.state', input=','.join(['100:8,0:8'] * 16), frames=210, poke=None, pre=0,
                 argv=['--scene', '19', '--battletest', 'saves/battle_re/forest_pre.raw.wram'],
                 env=dict(CT_BATTLE_SCRIPT='n:20,a:1,n:3,a:1,n:400', CT_BATTLE_DEBUGEND='400', CT_BATTLE_CRITRATE='255')),
    'nocrit': dict(state='gato_press_action.state', input=press_script(20), frames=800, poke='5E72=00,5EF2=00', pre=300,
                   argv=['--battletest', 'saves/battle_re/gato_t0.raw.wram'],
                   env=dict(CT_BATTLE_SCRIPT=port_press_script(), CT_BATTLE_DEBUGEND='900', CT_BATTLE_CRITRATE='0')),
}


def run_port(sc):
    env = dict(os.environ, CT_BATTLE_TRACE='1', CT_BATTLE_ASCIP='1', CT_NOPACE='1', SDL_AUDIODRIVER='dummy',
               SDL_VIDEODRIVER='dummy', **sc['env'])
    r = subprocess.run([os.path.abspath('build/ct_field.exe'), '--assets', 'ct_assets.dat'] + sc['argv'],
                       env=env, capture_output=True, text=True, errors='replace')
    acts, cur = [], None
    ips = {t: [] for t in TRACKS}
    feet = {}                                      # cmb -> [(frame, (x, y))], the CT_BATTLE_ASCIP `ascfoot` lines
    for l in (r.stdout + '\n' + r.stderr).splitlines():
        m = re.match(r'battletrace f(\d+) (pc-act\S*|act-end)\b.*?\b9877=([0-9A-F]{2})', l)
        if m:
            f = int(m.group(1))
            if m.group(2).startswith('pc-act'):
                cur = dict(rise=f, fall=None, sel=None); acts.append(cur)
            elif cur is not None and cur['fall'] is None:
                cur['fall'] = f; cur['sel'] = m.group(3)      # the act-end line carries the action's own $9877
            continue
        m = re.match(r'ascip f(\d+) rel=\S+ t(\d+) ([0-9A-F]{4})', l)
        if m and int(m.group(2)) in ips:
            ips[int(m.group(2))].append((int(m.group(1)), m.group(3)))
            continue
        m = re.match(r'ascfoot f(\d+) rel=\S+ cmb(\d+) \((\d+),(\d+)\)', l)
        if m:
            feet.setdefault(int(m.group(2)), []).append((int(m.group(1)), (int(m.group(3)), int(m.group(4)))))
    return [a for a in acts if a['fall'] is not None], ips, feet


def run_oracle(sc, tmp):
    oenv = dict(os.environ, CT_ORACLE_LOGICAL='1')
    if RULE:
        oenv['CT_ORACLE_VIRTUAL_BEAM'] = '1'
    state = sc['state']
    if sc['pre']:                                  # the post-boot state: a --peek of `pre` frames, saved at its end
        pre = os.path.join(tmp, 'pre.state')
        subprocess.run([os.path.abspath('build/ct.exe'), '--peek', state, os.path.join(tmp, 'pre.raw'), '--btnscript',
                        press_script(sc['pre'] // 60, LEAD)], env=dict(oenv, PEEK_SAVE=pre), capture_output=True)
        state = pre
    if sc['poke']:
        oenv['PEEK_POKE'] = sc['poke']
    r = subprocess.run([os.path.abspath('build/ct.exe'), '--monitor',
                        '7E5DBD:36,7EA013:1,7EA014:1,7EA017:1,7E9877:1,7E1D0C:11,7E1D23:11',
                        '--from', state, '--input', sc['input'], '--frames', str(sc['frames']), '--diff'],
                       env=oenv, capture_output=True, text=True, errors='replace')
    rows = []
    for l in r.stdout.splitlines():               # stdout only: the logical-frame summary goes to stderr
        m = re.match(r'\s*f(\d+)\s', l)
        v = dict(re.findall(r'7E([0-9A-F]{4})=([0-9A-F]+)', l))
        if m and len(v) == 7:
            h = v['5DBD']
            rows.append(dict(f=int(m.group(1)), cur=[h[i * 8 + 2:i * 8 + 4] + h[i * 8:i * 8 + 2] for i in range(9)],
                             a013=int(v['A013'], 16), a014=int(v['A014'], 16), a017=int(v['A017'], 16),
                             sel=v['9877'], x=bytes.fromhex(v['1D0C']), y=bytes.fromhex(v['1D23'])))
    acts, ips = [], {t: [] for t in TRACKS}
    prev, cur = None, None
    for w in rows:
        if prev is not None:
            if w['a013'] and not prev['a013'] and w['a014'] < 3:
                cur = dict(rise=w['f'], fall=None, sel=w['sel'], actor=w['a014'], victim=w['a017']); acts.append(cur)
            elif not w['a013'] and prev['a013'] and cur is not None and cur['fall'] is None:
                cur['fall'] = w['f']
            for t in TRACKS:
                if w['cur'][t] != prev['cur'][t] and w['cur'][t] != '0000':
                    ips[t].append((w['f'], w['cur'][t]))
        prev = w
    return [a for a in acts if a['fall'] is not None], ips, rows


def window(ips, a):
    return {t: [(f - a['rise'], c) for f, c in ips[t] if a['rise'] < f <= a['fall']] for t in TRACKS}


def oracle_foot(rows, cmb, f):
    """The combatant's foot as the cartridge's frame f leaves it (--diff prints a row only on a change)."""
    last = None
    for w in rows:
        if w['f'] > f:
            break
        last = w
    return (last['x'][cmb], last['y'][cmb]) if last else None


def cmp_feet(name, k, who, cmb, o, p, rows, pfeet):
    """From the port's first foot line of the action (the script's arm) to the fall, frame by frame. Where the two
    fights left the combatant somewhere else before the action (GATO wanders between turns, and these are two
    different fights), what is graded is its MOVE: the displacement from its own foot at that first frame."""
    ser = [(f, xy) for f, xy in pfeet.get(cmb, []) if p['rise'] < f <= p['fall']]
    if not ser:
        print('pcphys %s #%d: %s cmb%d -- the port printed no foot' % (name, k, who, cmb)); return
    first = ser[0][0] - p['rise']
    p0, o0 = ser[0][1], oracle_foot(rows, cmb, o['rise'] + first)
    rel_mode = p0 != o0
    bad, cur, i = None, None, 0
    for rel in range(first, p['fall'] - p['rise'] + 1):
        while i < len(ser) and ser[i][0] - p['rise'] <= rel:
            cur = ser[i][1]; i += 1
        ox = oracle_foot(rows, cmb, o['rise'] + rel)
        pv, ov = cur, ox
        if rel_mode:
            pv = (cur[0] - p0[0], cur[1] - p0[1]); ov = (ox[0] - o0[0], ox[1] - o0[1])
        if pv != ov and bad is None:
            bad = (rel, ov, pv)
    what = 'foot' if not rel_mode else 'move (from its own start: oracle %s port %s)' % (o0, p0)
    if bad is None:
        print('pcphys %s #%d: %s cmb%d %s identical rel %d-%d (%d moves)' % (name, k, who, cmb, what, first,
                                                                          p['fall'] - p['rise'], len(ser) - 1))
    else:
        print('pcphys %s #%d: %s cmb%d %s differs first at rel %d: oracle %s port %s' % (name, k, who, cmb, what,
                                                                                       *bad))


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    os.chdir(root)
    name = sys.argv[1] if len(sys.argv) > 1 else 'press'
    sc = SCEN[name]
    tmp = os.path.join('build', '_pcphys_' + name)
    os.makedirs(tmp, exist_ok=True)
    pa, pips, pfeet = run_port(sc)
    oa, oips, rows = run_oracle(sc, tmp)
    print('pcphys %s: the cartridge\'s PC actions %s' % (
        name, ' '.join('f%d-f%d(sel %s)' % (a['rise'], a['fall'], a['sel']) for a in oa) or 'none'))
    if not pa or not oa:
        print('pcphys %s: a side ran no PC physical (port %d, oracle %d)' % (name, len(pa), len(oa)))
        return
    for k in range(max(len(pa), len(oa))):
        if k >= len(pa) or k >= len(oa):
            print('pcphys %s #%d: only the %s has it' % (name, k + 1, 'port' if k < len(pa) else 'oracle'))
            continue
        p, o = pa[k], oa[k]
        ps, os_ = p['fall'] - p['rise'], o['fall'] - o['rise']
        print('pcphys %s #%d: sel %s / %s, span %s' % (name, k + 1, o['sel'], p['sel'],
                                                     '%d both' % ps if ps == os_ else 'oracle %d port %d' % (os_, ps)))
        pw, ow = window(pips, p), window(oips, o)
        for t in TRACKS:
            if pw[t] == ow[t]:
                print('pcphys %s #%d: t%d identical, %d cursor changes' % (name, k + 1, t, len(ow[t])))
                continue
            i = next(i for i in range(max(len(pw[t]), len(ow[t])))
                     if i >= len(pw[t]) or i >= len(ow[t]) or pw[t][i] != ow[t][i])
            show = lambda w: '%s@%d' % (w[i][1], w[i][0]) if i < len(w) else 'none'
            print('pcphys %s #%d: t%d differs at change %d: oracle %s port %s' % (name, k + 1, t, i + 1, show(ow[t]),
                                                                              show(pw[t])))
        cmp_feet(name, k + 1, 'actor', o['actor'], o, p, rows, pfeet)
        cmp_feet(name, k + 1, 'victim', o['victim'], o, p, rows, pfeet)


if __name__ == '__main__':
    main()
