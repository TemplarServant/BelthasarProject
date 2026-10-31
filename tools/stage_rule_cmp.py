#!/usr/bin/env python3
"""stage_rule_cmp.py -- a translated $C3 stage of the attract, every frame from its hand-off to its exit, against the
cartridge under the rule (the zelda3 model, CLAUDE.md; sequence_table.md 11.13).

    python tools/stage_rule_cmp.py race|entry3 [--frames N]

Both sides are run here, nothing is a pinned frame number:
  * the cartridge: from power-on with the virtual beam and logical frames (CT_ORACLE_VIRTUAL_BEAM=1 CT_ORACLE_LOGICAL=1),
    `ct.exe --xlatentries boot` names the frame P C3:0000 runs on for the stage (the entry whose 7E:3000 shows $0384 =
    6x), and `PEEK_SAVE=<state> ct.exe --peek boot out --frames P-H+1` saves the state H frames before it (a state's own
    count starts one later than power-on's); `--xlatentries <state>` re-measures H on it (HANDOFF below).
    `--peek <state> --frames HANDOFF+N` with PEEK_THUMBS_EVERY=1 gives every frame as a 128x112 thumbnail. The state is
    this oracle's: both runs start at power-on (~60 s each), so they are cached under build/_stagerule/ named by ct.exe's
    sha1 and made again for the next exe (the linker stamps the exe, so every link is a new one); the first run after a
    build pays for the two runs. The committed saves/attract_*_handoff_rule_*.state are not used: a state outlives the
    oracle that made it (field_window_wipe.md "The virtual beam").
  * the port: the attract from scene 433 (CT_NOPACE, CT_AUDIO_LOCKSTEP, as entry3_stage_cmp.py), CT_STAGE_HANDOFF_WRAM to
    print the frame the host opens the stage on (`stage-handoff: frame F $0384=6x`: the native stage, seq_race.c /
    seq_entry3.c), CT_SHOTS_THUMBS over a window that contains it -- the same thumbnail records (field.c, 2x2 blocks
    averaged).
Port frame F + k is compared with cartridge frame HANDOFF + k + PIC, k = 0 .. N-1 (PIC = 2, the alignment in
docs/TESTING.md: the port's frame P picture is the logical oracle's `--peek` P+2 while the WRAM aligns at the same number;
the native stages draw on the field's clock like every other picture). N is the stage's own length (the port's
`entry n (native) ended after N frames from C3:0000`), else --frames. The port's seed history equals the cartridge's for
every call (seed_history_cmp.py), so the stage's opening transition (C3/02DD's) is graded like the rest.

  stage-rule-state: <stage> C3:0000 at power-on f<P> ($0386 = <s>) -> state f<P-H+1>, its f<H>, ct.exe <sha1>
  stage-rule-cmp: <stage> port f<F>..f<F+N-1> vs cartridge +<H>..+<H+N-1> under the rule -> <n> / <N> frames identical
"""
import hashlib, os, re, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAGES = {
    # name: (H = the frame of the state C3:0000 runs on, $0384, the port's thumbnail window)
    'race':   (19, 0x60, (7900, 8900)),
    'entry3': (18, 0x63, (8500, 9300)),
}
REC = 6 + 128 * 112 * 3
RULE = {'CT_ORACLE_VIRTUAL_BEAM': '1', 'CT_ORACLE_LOGICAL': '1'}
CACHE = 'build/_stagerule'
PIC = 2           # the picture alignment (above)
SEARCH = 9300     # power-on frames --xlatentries walks: past entry 3's hand-off (about f8863)


def exe_tag(path):
    h = hashlib.sha1()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    # ...and the oracle's frame rule: the same ct.exe makes another attract under CT_ORACLE_NMION_HOLD / CT_ORACLE_LINE64
    # (measurement switches, CLAUDE.md), so a state made under one is not the other's.
    rule = ''.join(k for k, e in (('h', 'CT_ORACLE_NMION_HOLD'), ('l', 'CT_ORACLE_LINE64'), ('a', 'CT_ORACLE_HOLDAPU'),
                                  ('i', 'CT_ORACLE_IFAST'))
                   if os.environ.get(e, '0' if e == 'CT_ORACLE_HOLDAPU' else '1') not in ('', '0'))
    return h.hexdigest()[:12] + (('-' + rule) if rule else '')


def cartridge_state(name, H, eid):
    """(state, hand-off frame from power-on, $0386 there) for this ct.exe -- see the docstring. Every file is written
    under a temporary name and renamed into place: the race and entry 3 run in parallel in the suite and share the
    --xlatentries walk."""
    ct = os.path.abspath('build/ct.exe')
    tag = exe_tag(ct)
    os.makedirs(CACHE, exist_ok=True)
    for f in os.listdir(CACHE):                    # another exe's: never read again
        if not f.startswith(tag):
            try: os.remove(os.path.join(CACHE, f))
            except OSError: pass
    xe = os.path.join(CACHE, '%s_xlatentries.txt' % tag)
    if not os.path.exists(xe):
        out = subprocess.run([ct, '--xlatentries', 'boot', str(SEARCH)], env=dict(os.environ, **RULE),
                             capture_output=True, text=True, errors='replace', timeout=1800).stdout
        tmp = '%s.%d' % (xe, os.getpid())
        open(tmp, 'w').write(out)
        os.replace(tmp, xe)
    handoff = seed = entry = None
    for line in open(xe):
        m = re.match(r'xlatentries: f(\d+) C3:0000 .*\$0386=([0-9A-F]{2})', line)
        if m:
            entry = (int(m.group(1)), m.group(2))
            continue
        m = re.match(r'xlatentries: f\d+ 7E:3000 \$0384=([0-9A-F]{2})', line)
        if m and entry and int(m.group(1), 16) == eid:
            handoff, seed = entry
            break
    if handoff is None:
        return None, None, None
    st = os.path.join(CACHE, '%s_%s_f%d.state' % (tag, name, handoff - H + 1))
    if not os.path.exists(st):
        tmp = '%s.%d' % (st, os.getpid())
        subprocess.run([ct, '--peek', 'boot', os.path.join(CACHE, '%s_%s.raw' % (tag, name)), '--frames',
                        str(handoff - H + 1)], env=dict(os.environ, PEEK_SAVE=tmp, **RULE),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1800)
        if os.path.exists(tmp):
            os.replace(tmp, st)
    return (st if os.path.exists(st) else None), handoff, seed


def thumbs(path):
    d = open(path, 'rb').read(); out = {}
    for i in range(0, len(d) - REC + 1, REC):
        out[struct.unpack('<I', d[i:i + 4])[0]] = d[i + 6:i + REC]
    return out


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in STAGES:
        print(__doc__); return 2
    name = sys.argv[1]
    nfr = int(sys.argv[sys.argv.index('--frames') + 1]) if '--frames' in sys.argv else 0
    lead, eid, (lo, hi) = STAGES[name]
    os.chdir(ROOT)
    state, P, seed = cartridge_state(name, lead, eid)
    if P is None:
        print('stage-rule-cmp: FAIL -- the cartridge never hands entry $%02X over in %d frames from power-on' % (eid, SEARCH))
        return 1
    if not state:
        print('stage-rule-cmp: FAIL -- no state at power-on f%d' % (P - lead + 1)); return 1
    xs = subprocess.run([os.path.abspath('build/ct.exe'), '--xlatentries', state, str(lead + 2)], env=dict(os.environ, **RULE),
                        capture_output=True, text=True, errors='replace', timeout=600).stdout
    m = re.search(r'xlatentries: f(\d+) C3:0000 ', xs)
    if not m:
        print('stage-rule-cmp: FAIL -- C3:0000 does not run in the first %d frames of %s' % (lead + 2, state)); return 1
    H = int(m.group(1))
    print('stage-rule-state: %s C3:0000 at power-on f%d ($0386 = %s) -> state f%d, its f%d, ct.exe %s'
          % (name, P, seed, P - lead + 1, H, os.path.basename(state).split('_')[0]))
    pth = 'build/_stagerule_%s_port.thumbs' % name
    if os.path.exists(pth): os.remove(pth)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_STAGE_HANDOFF_WRAM='build/_stagerule_handoff.wram', CT_SHOTS_THUMBS=pth, CT_SHOTS_THUMBS_EVERY='1',
               CT_SHOTS_THUMBS_FROM=str(lo), CT_SHOTS_THUMBS_TO=str(hi), CT_SHOTS='build/_stagerule_shot',
               CT_SHOTS_AT=str(hi), CT_SHOTS_EXIT='1')
    log = subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '433'],
                         env=env, capture_output=True, text=True, errors='replace', timeout=1800).stdout
    F = None
    for m in re.finditer(r'stage-handoff: frame (\d+) \$0384=([0-9A-F]{2})', log):
        if int(m.group(2), 16) == eid and lo <= int(m.group(1)) <= hi:
            F = int(m.group(1))
    if F is None:
        print('stage-rule-cmp: FAIL -- the port never handed entry $%02X over inside f%d..f%d' % (eid, lo, hi)); return 1
    m = re.search(r'sequence: entry %d \(native\) ended after (\d+) frames from C3:0000' % (eid - 0x60), log)
    N = nfr or (int(m.group(1)) if m else 0)
    if not N or F + N - 1 > hi:
        N = min(N or hi - F + 1, hi - F + 1)
    ot = 'build/_stagerule_%s_orc.raw' % name
    if os.path.exists(ot + '.thumbs'): os.remove(ot + '.thumbs')
    subprocess.run([os.path.abspath('build/ct.exe'), '--peek', state, ot, '--frames', str(H + N + PIC)],
                   env=dict(os.environ, PEEK_THUMBS=ot + '.thumbs', PEEK_THUMBS_EVERY='1', **RULE),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1800)
    pt, oth = thumbs(pth), thumbs(ot + '.thumbs')
    bad = [k for k in range(N) if pt.get(F + k) is None or oth.get(H + k + PIC) != pt.get(F + k)]
    print('stage-rule-cmp: %s port f%d..f%d vs cartridge +%d..+%d under the rule -> %d / %d frames identical%s'
          % (name, F, F + N - 1, H + PIC, H + N - 1 + PIC, N - len(bad), N,
             (', differ at +' + ' +'.join(str(H + k + PIC) for k in bad[:16]) + (' ...' if len(bad) > 16 else '')) if bad else ''))
    for p in (pth, ot + '.thumbs'):
        if os.path.exists(p): os.remove(p)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
