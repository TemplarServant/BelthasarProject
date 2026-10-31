#!/usr/bin/env python3
r"""The naming screen (op C8 -> C2/8000 mode 3, screen $0D) against the cartridge, every frame of a drive.

    python tools/naming_cmp.py <drive> [--peek F ...]

Two kinds of drive.

`type` -- the keyboard. The cartridge starts from saves/crononame.state: the new game's Crono naming screen, settled,
its prompt box up (nothing on the screen moves once it is; the thumbnails are identical from its third frame on).
The port starts the new game (`--newgame`), confirms the ATB selector with A at its f70 and joins the drive at frame
BASE, where the prompt is up and static too: port frame BASE + P is the cartridge's thumbnail P + 2 (the menu's display
alignment), and a key the cartridge gets at frame t the port gets at BASE + t + 1 (the menu reads a press a frame after
the pad latch).

`boot*` / `loadlist` -- from a cartridge's power-on (`ct.exe --fb` to f120 with CT_ORACLE_SRAM = a zeroed battery, or
the drive's battery; Start at +13 and +115 -- the title takes the second) against the port's `--boot` (a fresh battery or
a copy of the drive's; Start at 198, the power-on's title: boot_loader.md sec. 10). The two are aligned on the park of
an op, `C8 C0` (the naming; boot, bootfast) or `C8 41` (the load shell; bootldr, loadlist): the cartridge's from
`--monitor` ($0125 / $0117), the port's from CT_NAMETRACE, with D = cartridge park - port park. Port frame p is the
cartridge's frame p + D, its thumbnail p + D + 2, and a key the cartridge gets at t the port gets at t - D + 1. Presses
are listed in the cartridge's frames (from the f120 state). The load list sits 7 lines off the thumbnail grid (its BG V
scroll: tools/boot_loader_cmp.py grades its settled picture), so `loadlist` grades its timing, not its pixels.

`marle` -- a later naming (C8 C1) from a live scene, park-aligned the same way (run_park).

Same output as tools/menu_audit_cmp.py:

    naming-cmp: <drive> f<A>-f<B>: <N> of <M> frame(s) differ
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import menu_audit_cmp as M  # noqa: E402
from ctlib import fb  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = 'saves/crononame.state'
BASE = 100
BOOT_START = [(0x8, 13), (0x8, 115)]          # the cartridge's title: the second Start is the one it takes
KEY = {0x100: 'a', 0x1: 'b', 0x10: 'u', 0x20: 'd', 0x40: 'l', 0x80: 'r', 0x8: 's'}

# name: (presses [(mask, cartridge frame)], F0, F1)
DRIVES = {
    # A dismisses the box; Right x3 (to D), A (types D), Down (row 2), A, B (backspace), Left, A, Start (accept)
    'type': ([(0x100, 10), (0x80, 40), (0x80, 55), (0x80, 70), (0x100, 90), (0x20, 115), (0x100, 135), (0x1, 160),
              (0x40, 185), (0x100, 205), (0x8, 240)], 10, 300),
    # From power-on: the selector opens (the park f217, C f235, S0 = P f251), types its description; Down (WAIT: the
    # description again), Up, Down, A (WAIT confirmed: $2990 bit 6 clear) -> C2/83CA, C2/E40E, the prompt; A (the prompt
    # away), Right, A, Down, A, B, Start -> the close, the reload, the field's fade-in.
    'boot': ([(0x20, 280), (0x10, 300), (0x20, 320), (0x100, 350), (0x100, 430), (0x80, 450), (0x100, 470),
              (0x20, 490), (0x100, 510), (0x1, 530), (0x8, 560)], 212, 640),
    # A on the selector's first frames: under the fade-in (P+2 .. P+9) and in mid-description (the kill of slot 2).
    'bootfast': ([(0x100, 256)], 212, 330),
    # 'boot' aligned on the loader's park instead (C8 41 in loc 0's first visit): the blank cart's short circuit at C,
    # the reboot, loc 0 again, the naming -- the black stretch between the two parks is what this one times.
    'bootldr': ([(0x20, 280), (0x10, 300), (0x20, 320), (0x100, 350), (0x100, 430)], 170, 460),
    # A cart holding a save (saves/field_re/colsave3.srm): the load list (screen $0E through the shell), Down, Up, A on
    # slot 0 -> C2/8320's fade-out, C2/832F's reboot into the save, its location's fade-in.
    'loadlist': ([(0x20, 250), (0x10, 270), (0x100, 300)], 170, 420),
    # Marle's naming at the fair (`C8 C1`: $299F | $71 non-zero, so C2/E1ED goes straight to C2/E201 -- no selector, no
    # prompt, $68 0 -> 1, P = S0 + 4). Both sides mash the field A through the dialog until the op parks; the drive is
    # aligned on the parks (see run_park) and runs Right, Down, Start ("Marle" accepted) -> the close, the reload, the
    # field's fade-in. Frames are the cartridge's from saves/field_re/s439_marle_choice_lake.state (the recipe twin; its
    # park is f603, which includes the cartridge's 45-frame hold on the join's song load `10 06`). The window opens at
    # the end of C0/0CC7's fade, f619 = park + 16 (under the fade the two sides dim different field pictures: the
    # dialogs do not keep step), and ends before the field's return (one frame early in the port: the reload after the
    # menu, an open item).
    'marle': ([(0x80, 661), (0x20, 681), (0x8, 711)], 619, 753),
}
# name: (cartridge state, port WRAM seed, port scene, the last frame the cartridge's dialog is mashed on)
# boot drives: (battery, the op whose park aligns the two sides); a drive not listed is (None, 'naming')
BOOT = {'bootldr': (None, 'loader'), 'loadlist': ('saves/field_re/colsave3.srm', 'loader')}
PARK = {'marle': ('saves/field_re/s439_marle_choice_lake.state', 'saves/field_re/s439_marle_choice.lakewram', 439, 610)}


def run(name, peeks):
    if name.startswith('boot') or name in BOOT:
        return run_boot(name, peeks)
    if name in PARK:
        return run_park(name, peeks)
    presses, f0, f1 = DRIVES[name]
    end = f1 + 10
    out = os.path.join(ROOT, 'build', '_namingcmp', name)
    os.makedirs(out, exist_ok=True)
    ot, pt = os.path.join(out, 'o.thumbs'), os.path.join(out, 'p.thumbs')
    for f in (ot, pt):
        if os.path.exists(f):
            os.remove(f)
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_THUMBS=ot, PEEK_THUMBS_EVERY='1')
    subprocess.run([M.CT, '--peek', STATE, os.path.join(out, '_o_end.raw'), '--btnscript',
                    M.oracle_script(presses, end)], cwd=ROOT, env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    walk, t = ['n:70', 'a:1'], 71                   # the port's selector first (P f45, lit by f54): A, ACTIVE
    for m, f in presses:
        pf = BASE + f + 1
        if pf > t:
            walk.append('n:%d' % (pf - t))
        walk.append('%s:1' % KEY[m])
        t = pf + 1
    walk.append('n:%d' % max(1, BASE + end - t))
    penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
                CT_SHOTS_THUMBS=pt, CT_SHOTS_THUMBS_EVERY='1', CT_SRAM=os.path.join(out, '_p.srm'), CT_SRAM_FRESH='1')
    if peeks:
        penv.update(CT_SHOTS=os.path.join(out, '_p'), CT_SHOTS_AT=','.join(str(BASE + f) for f in peeks))
    subprocess.run([M.FIELD, '--assets', 'ct_assets.dat', '--newgame', '--selftest', os.path.join(out, '_p_end.raw'),
                    '--frames', str(BASE + end), '--walkscript', ','.join(walk)], cwd=ROOT, env=penv,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    O, P = M.load_thumbs(ot), M.load_thumbs(pt)
    # The naming screen sits at the cartridge's own lines: the port's line y is the oracle frame's line y (offset 0
    # matches; 1 and the other menus' 8 do not), so the oracle's thumbnail, made past the 8-line head, is the port's
    # four thumbnail rows lower: rows 0-107 of the one against rows 4-111 of the other
    report(name, f0, f1, lambda f: O.get(f + M.SHIFT), lambda f: P.get(BASE + f))
    for f in peeks:
        of = os.path.join(out, '_o_%d.raw' % f)
        env2 = dict(os.environ, CT_ORACLE_LOGICAL='1')
        subprocess.run([M.CT, '--peek', STATE, of, '--btnscript', M.oracle_script(presses, f + M.SHIFT)], cwd=ROOT,
                       env=env2, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        peek_cmp(out, f, of, os.path.join(out, '_p_f%d.raw' % (BASE + f)))


def report(name, f0, f1, ofn, pfn):
    R = 128 * 3
    diff = []
    for f in range(f0, f1 + 1):
        o, p = ofn(f), pfn(f)
        if o is not None and p is not None and o[:108 * R] != p[4 * R:112 * R]:
            diff.append(f)
    print('naming-cmp: %s f%d-f%d: %d of %d frame(s) differ' % (name, f0, f1, len(diff), f1 - f0 + 1))
    runs = []
    for f in diff:
        if runs and f == runs[-1][1] + 1:
            runs[-1][1] = f
        else:
            runs.append([f, f])
    for a, b in runs[:20]:
        print('  f%d-f%d' % (a, b))


def peek_cmp(out, f, of, pf):
    if not (os.path.exists(of) and os.path.exists(pf)):
        print('naming-cmp: peek f%d missing' % f)
        return
    ow, oh, o = fb.load(of)
    pw, ph, p = fb.load(pf)
    n = sum(1 for y in range(ph) for x in range(pw)                           # (offset 0: see above)
            if p[(y * pw + x) * 3:(y * pw + x) * 3 + 3] != o[(y * ow + x) * 3:(y * ow + x) * 3 + 3])
    print('naming-cmp: peek f%d -> %d px differ' % (f, n))
    fb.fb_to_png(of, os.path.join(out, 'o_%d.png' % f))
    fb.fb_to_png(pf, os.path.join(out, 'p_%d.png' % f))


def boot_state(d, battery=None):
    """The cartridge at f120 from power-on with `battery` (None = a zeroed one), made in the drive's directory `d` on
    every run (~0.5 s): a state kept from an older oracle replays the rule that made it (field_window_wipe.md, "The
    virtual beam"), and a shared one would be rebuilt under a drive the suite runs in parallel."""
    srm = os.path.join(d, 'blank.srm')
    open(srm, 'wb').write(bytes(8192))
    tag = os.path.splitext(os.path.basename(battery))[0] if battery else 'boot'
    if os.environ.get('CT_ORACLE_NMION_HOLD', '1') != '0':   # the rule's oracle makes its own f120 (the migration switch)
        tag += '_hold'
    st = os.path.join(d, '%s_f120.state' % tag)
    if os.path.exists(st):
        os.remove(st)
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_SRAM=battery or srm, PEEK_SAVE=st)
    subprocess.run([M.CT, '--fb', os.path.join(d, '_%s_f120.raw' % tag), '--frames', '120'], cwd=ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return st


def oracle_park(st, anchor, script, frames):
    """The first frame `--monitor` shows op C8 parked: $0117 bit 6 with $0125 = 41 (the loader) or >= C0 (a naming)."""
    mon = subprocess.run([M.CT, '--monitor', '7E0125:1,7E0117:1', '--from', st, '--btnscript', script, '--frames',
                          str(frames)], cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True,
                         text=True).stdout
    for line in mon.splitlines():
        s = line.split()
        if s and s[0].startswith('f') and s[0][1:].isdigit() and len(s) >= 3:
            v125, v117 = int(s[1].split('=')[1].rstrip('*'), 16), int(s[2].split('=')[1].rstrip('*'), 16)
            if v117 & 0x40 and ((anchor == 'loader' and v125 == 0x41) or (anchor == 'naming' and v125 >= 0xC0)):
                return int(s[0][1:])
    return None


def run_boot(name, peeks):
    """A drive from power-on. The two sides are aligned on the park of the anchor's op (C8 41 the loader, C8 C0 the
    naming): the cartridge's from `--monitor`, the port's from CT_NAMETRACE, D = cartridge park - port park."""
    presses, f0, f1 = DRIVES[name]
    battery, anchor = BOOT.get(name, (None, 'naming'))
    end = f1 + 10
    out = os.path.join(ROOT, 'build', '_namingcmp', name)
    os.makedirs(out, exist_ok=True)
    st = boot_state(out, battery)
    ot, pt = os.path.join(out, 'o.thumbs'), os.path.join(out, 'p.thumbs')
    for f in (ot, pt):
        if os.path.exists(f):
            os.remove(f)
    opark = oracle_park(st, anchor, M.oracle_script(BOOT_START, 300), 300)
    srm = os.path.join(out, '_p.srm')
    penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_NAMETRACE='1',
                CT_SRAM=srm)
    if battery:
        open(srm, 'wb').write(open(os.path.join(ROOT, battery), 'rb').read())
    else:
        penv['CT_SRAM_FRESH'] = '1'
    base = [M.FIELD, '--assets', 'ct_assets.dat', '--boot', '--selftest', os.path.join(out, '_p_end.raw')]
    r = subprocess.run(base + ['--frames', '465', '--walkscript', 'n:198,s:1,n:266'], cwd=ROOT, env=penv,
                       capture_output=True, text=True).stdout
    ppark = next((int(l.split()[1][1:]) for l in r.splitlines()
                  if l.startswith(anchor + ':') and 'parked' in l), None)
    if opark is None or ppark is None:
        print('naming-cmp: %s: no %s park (cartridge %s, port %s)' % (name, anchor, opark, ppark))
        return
    D = opark - ppark
    if battery:
        open(srm, 'wb').write(open(os.path.join(ROOT, battery), 'rb').read())
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_THUMBS=ot, PEEK_THUMBS_EVERY='1')
    subprocess.run([M.CT, '--peek', st, os.path.join(out, '_o_end.raw'), '--btnscript',
                    M.oracle_script(BOOT_START + presses, end)], cwd=ROOT, env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    walk, t = ['n:198', 's:1'], 199
    for m, f in presses:
        pf = f - D + 1
        if pf > t:
            walk.append('n:%d' % (pf - t))
        walk.append('%s:1' % KEY[m])
        t = pf + 1
    walk.append('n:%d' % max(1, end - D - t))
    penv.update(CT_SHOTS_THUMBS=pt, CT_SHOTS_THUMBS_EVERY='1')
    if peeks:
        penv.update(CT_SHOTS=os.path.join(out, '_p'), CT_SHOTS_AT=','.join(str(f - D) for f in peeks))
    subprocess.run(base + ['--frames', str(end - D), '--walkscript', ','.join(walk)], cwd=ROOT, env=penv,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    O, P = M.load_thumbs(ot), M.load_thumbs(pt)
    print('naming-cmp: %s %s parks: cartridge f%d, port f%d (D = %d)' % (name, anchor, opark, ppark, D))
    report(name, f0, f1, lambda f: O.get(f + M.SHIFT), lambda f: P.get(f - D))
    for f in peeks:
        of = os.path.join(out, '_o_%d.raw' % f)
        env2 = dict(os.environ, CT_ORACLE_LOGICAL='1')
        subprocess.run([M.CT, '--peek', st, of, '--btnscript', M.oracle_script(BOOT_START + presses, f + M.SHIFT)],
                       cwd=ROOT, env=env2, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        peek_cmp(out, f, of, os.path.join(out, '_p_f%d.raw' % (f - D)))


def mash(until, step=10, first=10):
    return [(0x100, f) for f in range(first, until, step)]


def run_park(name, peeks):
    """A naming opened from a live scene: the dialog before it is mashed (field A every 10 frames) on both sides until
    op C8 parks, and the two are aligned on their parks -- the cartridge's is the first frame `--monitor` shows $0125 >= C0
    with $0117 bit 6, the port's the CT_NAMETRACE park line. Port frame p is the cartridge's p + D with
    D = cartridge park - port park; a menu key the cartridge gets at t the port gets at t - D + 1. The frames before
    the park are not compared (the dialogs need not keep step)."""
    presses, f0, f1 = DRIVES[name]
    ost, seed, scene, mash_end = PARK[name]
    end = f1 + 10
    out = os.path.join(ROOT, 'build', '_namingcmp', name)
    os.makedirs(out, exist_ok=True)
    om = mash(mash_end)
    mon = subprocess.run([M.CT, '--monitor', '7E0125:1,7E0117:1', '--from', ost, '--btnscript',
                          M.oracle_script(om, mash_end + 40), '--frames', str(mash_end + 40)], cwd=ROOT,
                         env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True, text=True).stdout
    opark = None
    for line in mon.splitlines():
        s = line.split()
        if s and s[0].startswith('f') and s[0][1:].isdigit() and len(s) >= 3:
            v125, v117 = int(s[1].split('=')[1].rstrip('*'), 16), int(s[2].split('=')[1].rstrip('*'), 16)
            if v125 >= 0xC0 and v117 & 0x40:
                opark = int(s[0][1:])
                break
    # the port's sound processor is the cartridge state's (CT_AUDIO_SEEDSTATE): the dialog's song load costs the SPC time
    # it costs the cartridge from that state
    penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_NAMETRACE='1',
                CT_SRAM=os.path.join(out, '_p.srm'), CT_SRAM_FRESH='1', CT_AUDIO_SEEDSTATE=ost)
    base = [M.FIELD, '--assets', 'ct_assets.dat', '--scene', str(scene), '--loadwram', seed, '--selftest',
            os.path.join(out, '_p_end.raw')]

    def walk(mfr, keys, total):
        w, t = [], 0
        for f, k in sorted([(f, 'a') for _, f in mfr] + keys):
            if f > t:
                w.append('n:%d' % (f - t))
            w.append('%s:1' % k)
            t = f + 1
        w.append('n:%d' % max(1, total - t))
        return ','.join(w)
    r = subprocess.run(base + ['--frames', '2000', '--walkscript', walk(mash(1990), [], 2000)], cwd=ROOT, env=penv,
                       capture_output=True, text=True).stdout
    ppark = next((int(l.split()[1][1:]) for l in r.splitlines() if l.startswith('naming:') and 'parked' in l), None)
    if opark is None or ppark is None:
        print('naming-cmp: %s: no park (cartridge %s, port %s)' % (name, opark, ppark))
        return
    D = opark - ppark
    pt = os.path.join(out, 'p.thumbs')
    ot = os.path.join(out, 'o.thumbs')
    for f in (ot, pt):
        if os.path.exists(f):
            os.remove(f)
    keys = [(f - D + 1, KEY[m]) for m, f in presses]
    penv.update(CT_SHOTS_THUMBS=pt, CT_SHOTS_THUMBS_EVERY='1')
    if peeks:
        penv.update(CT_SHOTS=os.path.join(out, '_p'), CT_SHOTS_AT=','.join(str(f - D) for f in peeks))
    subprocess.run(base + ['--frames', str(end - D), '--walkscript', walk(mash(ppark + 1), keys, end - D)], cwd=ROOT,
                   env=penv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_THUMBS=ot, PEEK_THUMBS_EVERY='1')
    subprocess.run([M.CT, '--peek', ost, os.path.join(out, '_o_end.raw'), '--btnscript',
                    M.oracle_script(om + presses, end)], cwd=ROOT, env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    O, P = M.load_thumbs(ot), M.load_thumbs(pt)
    print('naming-cmp: %s parks: cartridge f%d, port f%d (D = %d)' % (name, opark, ppark, D))
    report(name, f0, f1, lambda f: O.get(f + M.SHIFT), lambda f: P.get(f - D))
    for f in peeks:
        of = os.path.join(out, '_o_%d.raw' % f)
        subprocess.run([M.CT, '--peek', ost, of, '--btnscript', M.oracle_script(om + presses, f + M.SHIFT)],
                       cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'), stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
        peek_cmp(out, f, of, os.path.join(out, '_p_f%d.raw' % (f - D)))


if __name__ == '__main__':
    name = sys.argv[1] if len(sys.argv) > 1 else 'type'
    peeks = []
    if '--peek' in sys.argv:
        peeks = [int(a) for a in sys.argv[sys.argv.index('--peek') + 1:]]
    run(name, peeks)
