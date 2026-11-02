"""The oracle kept in one process (ct.exe / ct_oracle.exe --serve, src/main.c runServe), for authoring tools.

A state reloaded at a --peek end does not resume like the run that never stopped: the logical-frame rule's statics
(snes/snes.c snes_ruleStateSave) are not in the savestate, so a pad played in pieces of separate --peek runs parts, a few
beam cycles at a time, from the one run `playthrough.py play` makes. A session loads its state once and plays every piece
in the same machine; its snapshots keep the rule's statics, so `restore` resumes exactly where `snap` stood.

    with OracleSession('saves/playthrough/93_ozzie_way.state') as o:
        o.run([(0x10, 16), (0, 8)])      # the pad, as --btnscript holds it
        w = o.wram()                      # 128 KB, banks 7E-7F
        o.snap('a'); ...; o.restore('a')
        o.save('out.state')               # a CTLS state (the rule's statics are not in it)
"""
import os
import subprocess
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def exe():
    """CT_SERVE_EXE, else the oracle-only build, else build/ct.exe."""
    for p in (os.environ.get('CT_SERVE_EXE'), os.path.join(ROOT, 'build', 'ct_oracle.exe'),
              os.path.join(ROOT, 'build', 'ct.exe')):
        if p and os.path.exists(p):
            return p
    raise FileNotFoundError('no oracle binary (build/ct_oracle.exe or build/ct.exe)')


class OracleSession:
    def __init__(self, state, env=None):
        e = dict(os.environ, CT_ORACLE_LOGICAL='1')
        e.update(env or {})
        self.p = subprocess.Popen([exe(), '--serve', os.path.abspath(state)], cwd=ROOT, env=e, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.tmp = tempfile.mkdtemp(prefix='ctserve')
        first = self._line()
        if not first.startswith('ok'):
            raise RuntimeError('oracle session: %s' % first)

    def _line(self):
        line = self.p.stdout.readline()
        while line and not (line.startswith('ok') or line.startswith('err')):     # the oracle's own prints
            line = self.p.stdout.readline()
        if not line:
            raise RuntimeError('oracle session ended')
        return line.strip()

    def cmd(self, c, limit=120):
        """One command; the oracle is killed (RuntimeError) if it has not answered in `limit` seconds -- a hung machine
        fails the caller instead of holding it. CT_SERVE_LOG=<path> appends every command and its time."""
        t0 = time.time()
        dog = threading.Timer(limit, self.p.kill)
        dog.start()
        try:
            self.p.stdin.write(c + '\n')
            self.p.stdin.flush()
            r = self._line()
        except (RuntimeError, OSError):
            raise RuntimeError('oracle session: no answer to %s in %d s (killed)' % (c[:80], limit))
        finally:
            dog.cancel()
        if os.environ.get('CT_SERVE_LOG'):
            with open(os.environ['CT_SERVE_LOG'], 'a') as f:
                f.write('%.2f %s -> %s\n' % (time.time() - t0, c[:200], r))
        if r.startswith('err'):
            raise RuntimeError('oracle session: %s -> %s' % (c[:80], r))
        return r

    def run(self, pad):
        """Play the pad ([(buttons, frames)], LakeSnes serial order); returns the frames played."""
        pad = [(b, n) for b, n in pad if n > 0]
        if not pad:
            return 0
        frames = sum(n for _, n in pad)
        return int(self.cmd('run ' + ','.join('%X:%d' % (b, n) for b, n in pad), 60 + frames / 20).split()[1])

    def wram(self):
        path = os.path.join(self.tmp, 'w.bin')
        self.cmd('wram ' + path)
        with open(path, 'rb') as f:
            return f.read()

    def fb(self, path):
        self.cmd('fb ' + os.path.abspath(path))

    def save(self, path):
        self.cmd('save ' + os.path.abspath(path))

    def snap(self, name):
        self.cmd('snap ' + name)

    def restore(self, name):
        self.cmd('restore ' + name)

    def drop(self, name):
        self.cmd('drop ' + name)

    def close(self):
        if self.p.poll() is None:
            try:
                self.cmd('quit')
            except (RuntimeError, OSError):
                pass
            self.p.wait(timeout=30)

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()
