#!/usr/bin/env python3
"""ct.py -- multi-command CLI over the ctlib RE-tooling package (observation only).

Subcommands:
  fb2png <in.raw|.fb> [out.png]     oracle framebuffer -> 256x240 PNG
  oam    <file.oam|s9x.state>       decode + print the 128-sprite OAM table
  objs   <state|.wram>              field object-array table ($18xx/$1B01/...)
  owamb  <state|.wram>              overworld ambient blocks ($0B30-$1B30)
  refs   <addr>... [--rom PATH]     JSR/JSL callers + STA/STZ writers of addr
  script <scene> [--obj N] [--rom PATH]   event-VM fn table / annotated listing
  wram   <state> <out.bin>          extract the 128KB WRAM from a savestate

States are auto-detected: raw 128KB .wram sidecar / LakeSnes .state (ct.exe) /
snes9x '#!s9xsnp:0011' .state (gzip'd or raw).

Run from the repo root (the default ROM Chrono_Trigger_USA_SNES.sfc is looked
up in the CWD, then the repo root).
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ctlib  # noqa: F401
from ctlib import fb, lake, oam, rom, s9x, script, wram


def cmd_fb2png(args):
    """Decode a ct.exe --peek framebuffer (512x480 [pad,B,G,R], R at +3) and
    write a 2x-downsampled 256x240 PNG."""
    out = args.out or args.fb + '.png'
    w, h, rgb = fb.load(args.fb)
    fb.save_png(out, w, h, rgb)
    print('wrote %s (%dx%d)' % (out, w, h))


def cmd_oam(args):
    """Print the 128-sprite OAM table from a raw 544-byte .oam sidecar or a
    snes9x state (PPU-block OAMData). size = the raw high-table bit
    (0=small 1=large; pixel dims depend on OBSEL, not stored here)."""
    data = oam.load(args.file)
    sprites = oam.decode(data)
    print('=== %s ===' % args.file)
    print('idx    X    Y  tile attr prio pal size vf hf')
    shown = 0
    for sp in sprites:
        if not args.all and sp['tile'] == 0 and sp['attr'] == 0 and sp['y'] in (0, 224, 240):
            continue  # parked/unused rows (same filter as tools/_oambird.py)
        mark = '*' if sp['onscreen'] else ' '
        print('%s%3d %4d %4d  %03X   %02X   %d    %d   %d   %d  %d'
              % (mark, sp['idx'], sp['x'], sp['y'], sp['tile'], sp['attr'],
                 sp['prio'], sp['pal'], sp['size'], sp['vflip'], sp['hflip']))
        shown += 1
    print('(%d rows shown, * = on-screen; --all for all 128)' % shown)


def cmd_objs(args):
    """Print the field object-array table (tile/foot/solid/active/status/prio/
    scriptptr/anim/cadence/speed/ticket/facing) + player obj and control locks."""
    kind = wram.detect(args.state)
    w = wram.load(args.state)
    p = wram.player(w)
    print('=== %s (%s) ===' % (args.state, kind))
    print('player obj index $0197 = %d -> obj %d' % (p['playerObjRaw'], p['playerObj']))
    print('$0162(cutscene lock)=%d $011F(ctrl)=%d $0120(playerpath)=%d'
          % (p['lock0162'], p['ctrl011F'], p['path0120']))
    print('obj | tile    foot     | sol act st pr | scrptr am | cad/ph sp tk | fac')
    for r in wram.objs(w, count=args.count):
        print('obj%2d | (%2d,%2d) f(%3d,%3d) | %d %02X %02X %02X | 0x%04X %02X | %2d/%-3d %02X %02X | %02X'
              % (r['obj'], r['xtile'], r['ytile'], r['xfoot'], r['yfoot'],
                 r['solid'], r['active'], r['status'], r['prio'],
                 r['scriptptr'], r['anim'], r['cadence'], r['cadphase'],
                 r['speed'], r['ticket'], r['facing']))


def cmd_owamb(args):
    """Print the live overworld ambient-object blocks ($0B30-$1B30 stride 0x40,
    sig 0x0F63): pc, attr, pos, vel per block. Only meaningful for OVERWORLD-mode
    WRAM (the field engine reuses this RAM; stale sig matches possible)."""
    kind = wram.detect(args.state)
    w = wram.load(args.state)
    blocks = wram.ow_ambient(w)
    print('==== oracle ambient blocks %s (%s): %d live' % (args.state, kind, len(blocks)))
    for b in blocks:
        print('  [%04X pc=%04X a=%02X pos=(%d,%d) v=(%04X,%04X)]'
              % (b['off'], b['pc'], b['attr'], b['x'], b['y'], b['vx'], b['vy']))


def cmd_refs(args):
    """Scan the ROM for callers (JSR abs same-bank / JSL long) and writers
    (STA/STZ abs[,X/,Y]) of each target address. 16-bit addr = bank-agnostic;
    24-bit addr (e.g. C05A93) = that bank only.
    Heuristic byte-pattern scan (can false-positive on data)."""
    r = rom.load(args.rom)
    for t in args.addr:
        a = int(t, 16)
        res = rom.refs(r, a)
        print('=== $%X === (%s)' % (a, res['note']))
        for c in res['callers']:
            extra = ' (JSL bank %02X)' % c['target_bank'] if c['kind'] == 'JSL' else ''
            print('  %s $%04X from $%02X/%04X%s' % (c['kind'], a & 0xFFFF, c['bank'], c['addr'], extra))
        for wr in res['writers']:
            print('  %-9s $%04X at $%02X/%04X' % (wr['kind'], a & 0xFFFF, wr['bank'], wr['addr']))
        if not res['callers'] and not res['writers']:
            print('  (no hits)')


def cmd_script(args):
    """Disassemble a scene's event-VM script: fn-pointer table for every object,
    or (with --obj N) the annotated listing of object N's unique functions."""
    r = rom.load(args.rom)
    script.disassemble(r, args.scene, obj=args.obj)


def cmd_wram(args):
    """Extract the 128KB WRAM from a savestate (LakeSnes or snes9x) or copy a
    raw .wram sidecar; writes exactly 0x20000 bytes."""
    kind = wram.detect(args.state)
    w = wram.load(args.state)
    with open(args.out, 'wb') as fh:
        fh.write(w)
    print('wrote %s (%d bytes) from %s state %s' % (args.out, len(w), kind, args.state))


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog='ct.py', description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)

    p = sub.add_parser('fb2png', help='oracle framebuffer -> PNG',
                       description=cmd_fb2png.__doc__)
    p.add_argument('fb', help='.fb/.raw framebuffer dump (512x480x4)')
    p.add_argument('out', nargs='?', help='output PNG (default <in>.png)')
    p.set_defaults(fn=cmd_fb2png)

    p = sub.add_parser('oam', help='decode a 544-byte OAM blob',
                       description=cmd_oam.__doc__)
    p.add_argument('file', help='.oam sidecar (ct.exe --peek) or snes9x .state')
    p.add_argument('--all', action='store_true', help='print all 128 rows')
    p.set_defaults(fn=cmd_oam)

    p = sub.add_parser('objs', help='field object-array table from WRAM',
                       description=cmd_objs.__doc__)
    p.add_argument('state', help='.wram sidecar / LakeSnes .state / snes9x .state')
    p.add_argument('--count', type=int, default=30, help='objects to print (default 30)')
    p.set_defaults(fn=cmd_objs)

    p = sub.add_parser('owamb', help='overworld ambient blocks from WRAM',
                       description=cmd_owamb.__doc__)
    p.add_argument('state', help='.wram sidecar / LakeSnes .state / snes9x .state')
    p.set_defaults(fn=cmd_owamb)

    p = sub.add_parser('refs', help='ROM xref scan: callers + writers of an addr',
                       description=cmd_refs.__doc__)
    p.add_argument('addr', nargs='+', help='hex target(s), 16-bit (1A00) or 24-bit (C05A93)')
    p.add_argument('--rom', default=None,
                   help='ROM path (default %s in CWD/repo root)' % rom.DEFAULT_ROM)
    p.set_defaults(fn=cmd_refs)

    p = sub.add_parser('script', help='event-VM scene-script disassembly',
                       description=cmd_script.__doc__)
    p.add_argument('scene', type=int, help='scene number (0-511)')
    p.add_argument('--obj', type=int, default=None, help='object to disassemble')
    p.add_argument('--rom', default=None,
                   help='ROM path (default %s in CWD/repo root)' % rom.DEFAULT_ROM)
    p.set_defaults(fn=cmd_script)

    p = sub.add_parser('wram', help='extract 128KB WRAM from a savestate',
                       description=cmd_wram.__doc__)
    p.add_argument('state', help='.wram sidecar / LakeSnes .state / snes9x .state')
    p.add_argument('out', help='output .bin (0x20000 bytes)')
    p.set_defaults(fn=cmd_wram)

    args = ap.parse_args(argv)
    args.fn(args)


if __name__ == '__main__':
    main()
