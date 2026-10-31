"""Mutation labs: persistent copies of the working tree that `tools/regress.py --mutate` runs its mutants in, so N
mutants build and run at once and the working tree itself is never edited.

A lab is build/_mutlabs/<k>: a mirror of the repo root's working tree (uncommitted edits and the untracked and ignored
inputs too: the ROM, ct_assets.dat, saves/, third_party/, src/xlat/, src/data/, the loose states at the root) with its
own build/. Checks and tools resolve their inputs against their own root, so a check runs in a lab as it runs at the root.

Left out of the mirror: the root's build trees (the lab builds its own; only build/recipes/, made by tools/recipes.py,
is carried over), .git (a lab is not a checkout; it lives under the root's ignored build/, so git there sees the root's
repository), .claude, the Android project, the vendored upstream clones and the scratch folders.

A lab keeps a manifest (.mutlab.json): each mirrored file's stat at the root and in the lab when last copied. A file whose
two stats still match is left alone; any other is copied again (a fresh mtime, so ninja recompiles it), and a file gone
from the root is removed from the lab. A lab left with a mutation in it (a killed run) is healed by the next sync.
"""
import json
import os
import shutil

LABS_DIR = os.path.join('build', '_mutlabs')
DENY_TOP = {'build', 'build_alt', '.git', '.claude', '.vs', 'android', 'private_history', 'scratchpad', 'arquivos_criados'}
CARRY = [os.path.join('build', 'recipes')]          # the parts of build/ a tool may read as an input
MANIFEST = '.mutlab.json'


def _walk(root, rel):
    """Every file under root/rel (rel '' = the whole mirror), as paths relative to root."""
    top = os.path.join(root, rel) if rel else root
    for dp, dns, fns in os.walk(top):
        if not rel and os.path.normcase(dp) == os.path.normcase(root):
            dns[:] = [d for d in dns if d not in DENY_TOP and not d.startswith('_vendor')]
            fns = [f for f in fns if f != '.git']           # a worktree's .git file: git in a lab must not find a repo
        dns[:] = [d for d in dns if d != '__pycache__']
        for f in fns:
            yield os.path.relpath(os.path.join(dp, f), root)


def _stat(p):
    try:
        st = os.stat(p)
        return [st.st_size, st.st_mtime_ns]
    except OSError:
        return None


class Lab:
    def __init__(self, src_root, k):
        self.src = src_root
        self.root = os.path.join(src_root, LABS_DIR, str(k))
        self.k = k
        self.manifest = {}

    def _load(self):
        try:
            with open(os.path.join(self.root, MANIFEST), encoding='utf-8') as fh:
                self.manifest = json.load(fh)
        except (OSError, ValueError):
            self.manifest = {}

    def save(self):
        tmp = os.path.join(self.root, MANIFEST + '.tmp')
        with open(tmp, 'w', encoding='utf-8') as fh:
            json.dump(self.manifest, fh)
        os.replace(tmp, os.path.join(self.root, MANIFEST))

    def sync(self):
        """Mirror the root's working tree into the lab. Returns the number of files copied or removed."""
        os.makedirs(self.root, exist_ok=True)
        self._load()
        seen, n = set(), 0
        rels = list(_walk(self.src, ''))
        for c in CARRY:
            if os.path.isdir(os.path.join(self.src, c)):
                rels += list(_walk(self.src, c))
        for rel in rels:
            seen.add(rel)
            s, d = os.path.join(self.src, rel), os.path.join(self.root, rel)
            ss = _stat(s)
            if ss is None:
                continue
            ent = self.manifest.get(rel)
            if ent and ent[0] == ss and ent[1] == _stat(d):
                continue
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copyfile(s, d)
            self.manifest[rel] = [ss, _stat(d)]
            n += 1
        for rel in [r for r in self.manifest if r not in seen]:
            try:
                os.remove(os.path.join(self.root, rel))
            except OSError:
                pass
            del self.manifest[rel]
            n += 1
        self.save()
        return n

    def touched(self, rel):
        """The lab's copy of rel was rewritten with the root's content (a mutation restored): keep it in the manifest as
        it is now, so the next sync does not copy it again."""
        ent = self.manifest.get(rel)
        if ent:
            ent[1] = _stat(os.path.join(self.root, rel))


def labs(src_root, n):
    return [Lab(src_root, k) for k in range(n)]
