"""Which command-line flags and environment variables the C sources read that no document names (read-only).

The flags are the "--name" string literals of src/main.c (ct.exe, ct_oracle.exe) and src/field.c (Belthasar); the
variables are every getenv("NAME") in src/ and snes/. A name counts as documented when it appears in any docs/*.md
(docs/DEBUG_SURFACE.md holds most of them). The script reads files and prints; it changes nothing.

    python tools/doc_surface_check.py            # the counts and every undocumented name
    python tools/doc_surface_check.py --count    # the counts only

Exit 0 always: the listing is a to-do for docs/DEBUG_SURFACE.md, not a gate.
"""
import argparse
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FLAG_SOURCES = {"ct.exe": "src/main.c", "Belthasar": "src/field.c"}
ENV_DIRS = ("src", "snes")
FLAG_RE = re.compile(r'"(--[a-z0-9][a-z0-9_-]*)"')
ENV_RE = re.compile(r'\bgetenv\("([A-Za-z_][A-Za-z0-9_]*)"\)')
# Names the host's runtime reads for itself (the OpenMP runtime, the platform's data folder), not debug switches.
NOT_DEBUG = {"HOME", "XDG_DATA_HOME", "OMP_WAIT_POLICY"}


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def doc_text():
    return "\n".join(read(p) for p in sorted(glob.glob(os.path.join(ROOT, "docs", "*.md"))))


def flags():
    out = {}
    for prog, rel in FLAG_SOURCES.items():
        out[prog] = sorted(set(FLAG_RE.findall(read(os.path.join(ROOT, rel)))))
    return out


def env_names():
    names = set()
    for d in ENV_DIRS:
        for dirpath, _, files in os.walk(os.path.join(ROOT, d)):
            for fn in files:
                if fn.endswith((".c", ".h", ".cpp", ".m")):
                    names.update(ENV_RE.findall(read(os.path.join(dirpath, fn))))
    return sorted(names - NOT_DEBUG)


def documented(name, text):
    # A whole-word match: --sweep must not count as documenting --sweepops, nor CT_RUN as documenting CT_RUNX.
    return re.search(r"(?<![A-Za-z0-9_-])%s(?![A-Za-z0-9_])" % re.escape(name), text) is not None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--count", action="store_true", help="print the counts only")
    args = ap.parse_args()
    text = doc_text()
    report = []
    for prog, names in flags().items():
        missing = [n for n in names if not documented(n, text)]
        report.append(("%s flags (%s)" % (prog, FLAG_SOURCES[prog]), names, missing))
    names = env_names()
    report.append(("environment variables (getenv in src/, snes/)", names, [n for n in names if not documented(n, text)]))
    for title, names, missing in report:
        print("%s: %d, %d not in docs/*.md" % (title, len(names), len(missing)))
        if not args.count:
            for n in missing:
                print("  " + n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
