#!/usr/bin/env python3
r"""The table pack's proof: a data-free build loads exactly the tables the real headers compile to.

    python tools/gen/tables_proof.py
    python tools/gen/tables_proof.py --exe build/Belthasar.exe [<a normal build's Belthasar.exe>]

In a developer tree (src/data holds the generated headers):
  1. src/tables/ is current with the shapes (pack_tables.py check);
  2. the pack is made from src/data/*.h (pack_tables.py pack -> build/tables_proof/ct_tables.dat);
  3. ct_tables_proof is built (tools/build.py ct_tables_proof): the data-free storage (src/tables/tables_storage.c) and
     loader (src/datapack.c) Belthasar links with CT_DATAFREE, beside the same tables compiled from the real headers
     (src/tables/tables_real.c);
  4. it loads the pack and compares every table byte for byte (padding included; a pointer member by its string), and
     every #define with the real header's value (a run-time one as the loader filled its variable);
  5. the controls: packs with one table byte flipped or a run-time #define changed must DIFFER, and packs with a wrong
     pointer size, a wrong layout hash or a compiled-in #define changed must be REFUSED -- so the comparison and the
     loader's checks are not vacuous.
Prints `tables-proof: ... IDENTICAL` and `tables-proof: controls OK`, exit 0; anything else exits 1.

--exe scans an executable for the tables' bytes (every 32-byte window of a table with at least 8 distinct values): a
data-free Belthasar.exe must hold none; a normal build's, given second, must hold some (the scan's own control).
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_tables as pt  # noqa: E402

BUILD = os.path.join(ROOT, os.environ.get('CT_BUILD_DIR') or 'build')
OUT = os.path.join(BUILD, 'tables_proof')
EXE = os.path.join(BUILD, 'ct_tables_proof' + ('.exe' if os.name == 'nt' else ''))


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)


def rewrite(data, fn):
    """The pack with entry blobs changed by fn(type, id, blob) -> blob."""
    return pt.container([(typ, eid, fn(typ, eid, blob)) for typ, eid, blob in pt.read_pack(data)])


WINDOW = 32
# Hits that are not the table compiled in, each with its reason (checked by hand; a new hit is a finding).
KNOWN = {
    'gen_opt0_pal0': "src/menu.c mDetailRight's eqIconDimPal, the dimmed copy of this palette written in the engine's "
                     "source, ends with the same 8 colours (32 bytes)",
}


def table_windows(pack_bytes):
    """{32-byte window: table name} for every table run with at least 8 distinct values (a zero fill is no content),
    windows taken every 16 bytes -- so any 48-byte stretch of a table found in a file hits at least one."""
    out = {}
    for typ, _, blob in pt.read_pack(pack_bytes):
        if typ != pt.T_TABLE:
            continue
        n = blob[0]
        name = blob[1:1 + n].decode()
        o = 1 + n + (-(1 + n) % 4)
        size, _, nrel = struct.unpack_from('<IQI', blob, o)
        data = blob[o + 16 + 4 * nrel:]
        for k in range(0, max(0, size - WINDOW + 1), 16):
            w = data[k:k + WINDOW]
            if len(set(w)) >= 8:
                out.setdefault(w, name)
    return out


def scan_exe(path, windows):
    """The tables some 32-byte window of which occurs in the file."""
    data = open(path, 'rb').read()
    found = set()
    for k in range(len(data) - WINDOW + 1):
        w = data[k:k + WINDOW]
        if w in windows:
            found.add(windows[w])
    return found


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--exe':
        # python tools/gen/tables_proof.py --exe <data-free exe> [<normal exe>]: no table's bytes in the first; the
        # second (a normal build) is the control that the scan finds them where they are
        pack = os.path.join(OUT, 'ct_tables.dat')
        if not os.path.exists(pack):
            os.makedirs(OUT, exist_ok=True)
            if run([sys.executable, 'tools/gen/pack_tables.py', 'pack', '--out', pack]).returncode:
                return 1
        windows = table_windows(open(pack, 'rb').read())
        hit = scan_exe(sys.argv[2], windows)
        for name in sorted(hit & set(KNOWN)):
            print('tables-proof: known coincidence, not the table: %s -- %s' % (name, KNOWN[name]))
        hit -= set(KNOWN)
        print('tables-proof: %s holds %d of the tables (%s)' % (os.path.relpath(sys.argv[2], ROOT), len(hit),
                                                                 ', '.join(sorted(hit)[:8]) or 'none'))
        ok = not hit
        if len(sys.argv) > 3:
            ctl = scan_exe(sys.argv[3], windows)
            print('tables-proof: control %s holds %d of the tables' % (os.path.relpath(sys.argv[3], ROOT), len(ctl)))
            ok &= len(ctl) > 0
        return 0 if ok else 1
    p = run([sys.executable, 'tools/gen/pack_tables.py', 'check'])
    if p.returncode:
        print(p.stdout, end='')
        print('tables-proof: FAIL: src/tables/ is stale')
        return 1
    os.makedirs(OUT, exist_ok=True)
    pack = os.path.join(OUT, 'ct_tables.dat')
    p = run([sys.executable, 'tools/gen/pack_tables.py', 'pack', '--out', pack])
    print(p.stdout, end='')
    if p.returncode:
        return 1
    p = run([sys.executable, 'tools/build.py', 'ct_tables_proof'])
    if p.returncode or 'Build OK' not in p.stdout:
        print(p.stdout[-3000:], end='')
        print('tables-proof: FAIL: ct_tables_proof did not build')
        return 1
    p = run([EXE, pack])
    print(p.stdout, end='')
    if p.returncode or 'IDENTICAL' not in p.stdout:
        return 1

    # the controls
    data = open(pack, 'rb').read()
    first = [True]

    def flip(typ, eid, blob):                         # one byte of the first table's data
        if typ == pt.T_TABLE and first[0]:
            first[0] = False
            b = bytearray(blob)
            b[-1] ^= 0x01
            return bytes(b)
        return blob

    def ptrsize(typ, eid, blob):
        return blob[:8] + struct.pack('<I', 4) + blob[12:] if typ == pt.T_HEADER else blob

    def layout(typ, eid, blob):                       # the first table's layout hash
        if typ == pt.T_TABLE and eid == 0:
            o = 1 + blob[0] + (-(1 + blob[0]) % 4)
            b = bytearray(blob)
            b[o + 4] ^= 0x01
            return bytes(b)
        return blob

    hs, texts = pt.parse_dir(pt.SHAPES)
    keep = pt.compiled_in(hs, texts)

    def define(runtime):                              # the value of the first #define of that kind
        def fn(typ, eid, blob):
            if typ != pt.T_DEFINES:
                return blob
            b, o = bytearray(blob), 4
            for _ in range(struct.unpack_from('<I', blob, 0)[0]):
                n = blob[o]
                if (blob[o + 1:o + 1 + n].decode() not in keep) == runtime:
                    b[o + 1 + n] ^= 0x01
                    return bytes(b)
                o += 1 + n + 8
            raise SystemExit('tables-proof: FAIL: the pack has no %s #define' % ('run-time' if runtime else 'compiled-in'))
        return fn

    ok = True
    for name, fn, want in (('one table byte flipped', flip, 'DIFFER'), ('4-byte pointers', ptrsize, 'did not load'),
                           ('a layout hash changed', layout, 'did not load'),
                           ('a compiled-in #define', define(False), 'did not load'),
                           ('a run-time #define', define(True), 'DIFFER')):
        bad = os.path.join(OUT, 'control.dat')
        open(bad, 'wb').write(rewrite(data, fn))
        q = run([EXE, bad])
        hit = q.returncode != 0 and want in q.stdout
        print('tables-proof: control %-24s %s' % (name + ':', 'caught' if hit else 'NOT CAUGHT -- ' + q.stdout.strip()))
        ok &= hit
    print('tables-proof: controls %s' % ('OK' if ok else 'FAILED'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
