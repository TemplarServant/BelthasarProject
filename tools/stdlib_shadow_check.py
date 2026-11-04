"""No script of the tree may be named like a module of Python's standard library.

A script run as `python tools/x.py` has tools/ as sys.path[0], so a tools/<name>.py whose name is a standard module is
imported in its place by anything that imports that module -- the standard library itself included. tools/dis.py (now
tools/romdis.py) did that: Python 3.14's argparse imports inspect, inspect imports dis, and every argparse script under
tools/ died on the ROM disassembler's import, the setup of the release packages first (GitHub issue #8). Modules inside a
package (a folder with __init__.py) are imported by their dotted name and shadow nothing.

    python tools/stdlib_shadow_check.py
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    files = subprocess.run(['git', 'ls-files', '*.py'], cwd=ROOT, capture_output=True, text=True).stdout.split()
    std = set(sys.stdlib_module_names)
    scripts, bad = 0, []
    for f in files:
        d = os.path.dirname(f)
        if os.path.exists(os.path.join(ROOT, d, '__init__.py')):
            continue
        scripts += 1
        name = os.path.splitext(os.path.basename(f))[0]
        if name in std:
            bad.append(f)
    for f in bad:
        print('stdlib-shadow: SHADOWS %s -- rename it (a script beside it imports it for the standard module)' % f)
    print('stdlib-shadow: %d of %d script(s) named like a standard module' % (len(bad), scripts))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
