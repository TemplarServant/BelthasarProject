"""Rename the playable host ct_field -> Belthasar across the tracked tree (re-runnable; idempotent).

    python tools/rename_ct_field.py .     (run it on a branch made before the rename, after merging it)

ct_field.exe -> Belthasar.exe, and the bare word ct_field -> Belthasar. Identifiers that only start with ct_field
(ct_field_reload_cycles, ...) are left alone: `_` is a word character, so the word boundary does not match them.
"""
import re
import subprocess
import sys

root = sys.argv[1]
OLD = r'\bct_' + r'field\b'           # in two parts, so a re-run finds nothing here
KEEP = {'tools/rename_ct_field.py', 'tools/build.py'}   # build.py keeps the old name as an alias (TARGET_ALIASES)
files = subprocess.run(['git', 'grep', '-lI', OLD], cwd=root, capture_output=True, text=True).stdout.split()
pat = re.compile(OLD.encode())
n_files = n_hits = 0
for f in files:
    if f in KEEP:
        continue
    p = root + '/' + f
    b = open(p, 'rb').read()
    nb, k = pat.subn(b'Belthasar', b)
    if k:
        open(p, 'wb').write(nb)
        n_files += 1
        n_hits += k
print('renamed %d occurrences in %d files' % (n_hits, n_files))
