"""A content-addressed cache of ORACLE runs (build/ct.exe --peek / --monitor / --hdma / --fb), so a re-run of the suite pays
only for what changed.

A run of those modes is a pure function of (the ct.exe binary, its arguments, its environment, the files it reads); every
one of them goes into the key:

  * sha256 of the binary that runs it, minus its link identity (_exe_sha) -- build/ct_oracle.exe when it is there (the
    oracle alone: main.c's four modes + snes/*.c + the CPU infra and state loader, CMakeLists.txt), so a rebuilt oracle
    (snes/*.c, src/main.c, ct_cpu_infra.c ...) misses and an edit to the port (battle.c, ct_vm.c, field.c ...) does not;
  * the arguments and the working directory, minus the OUTPUT paths (`--peek <state> <OUT>`, and the env outputs below);
  * the environment variables the binary can read: a name it holds as a C string (`getenv("CT_ORACLE_LOGICAL")` puts
    "CT_ORACLE_LOGICAL\0" in it; no oracle source builds a name at run time), minus the operating system's own (PATH,
    TEMP, ...). A variable only the port reads (CT_AUDIO_LOCKSTEP, CT_SHOTS ...) or the host sets (a per-command trace
    id) does not split the cache: before 2026-10-24 every variable was in, and a cloud container's per-command
    TRACEPARENT made every run of one tool miss the last;
  * the CONTENT of every argument or environment value that names an existing file (a state, a .lakewram, the ROM given
    by --rom, CT_ORACLE_SRAM ...), and of the ROM ct.exe opens by default in the working directory.

Its outputs are ENUMERATED, never guessed: `--peek`'s <OUT> and <OUT>.wram/.vram/.oam, `--fb`'s <OUT> and <OUT>.wram, PEEK_SAVE, PEEK_THUMBS, PEEK_PCM,
PEEK_ARAM, CT_DSP_RAWPCM, and PEEK_SHOTS' <prefix>_f<n>.raw (+ sidecars) for each n of PEEK_SHOTS_AT. They are stored
by ROLE (argument OUT, env PEEK_SAVE, ...) and a hit writes them to the paths THIS call names -- a tool that puts its pid
in a temporary name still hits. With stdout, stderr and the exit code, a hit returns the same CompletedProcess.

Opt-in by environment: CT_ORACLE_CACHE=<dir> turns it on (tools/regress.py sets build/_oracle_cache for its checks and
their tools, through tools/oracle_cache_site/sitecustomize.py; `--no-oracle-cache` turns it off, `--clear-oracle-cache`
empties it). Only subprocess.run / check_output calls are routed (a Popen is not), and only for the three modes above.
"""
import hashlib
import json
import os
import shutil
import subprocess
import time
import zlib

MODES = ('--peek', '--monitor', '--hdma', '--fb')


def mode_of(argv):
    """The run's mode and the index of its OUTPUT argument (None): `--peek <state> <OUT>`, `[rom] --fb <OUT>` (a ROM path
    may come first). A --fb that runs the play script (--script, harness_drive.c) is not cached."""
    for i, a in enumerate(argv[1:3], 1):
        if a in MODES:
            if a == '--peek':
                return a, (i + 2 if len(argv) > i + 2 else None)
            if a == '--fb':
                return (None, None) if '--script' in argv else (a, (i + 1 if len(argv) > i + 1 else None))
            return a, None
    return None, None
ENV_OUTS = ('PEEK_SAVE', 'PEEK_THUMBS', 'PEEK_PCM', 'PEEK_ARAM', 'CT_DSP_RAWPCM', 'PEEK_SHOTS')
_OS_VARS = {
    'PATH', 'PATHEXT', 'TEMP', 'TMP', 'TMPDIR', 'HOME', 'HOMEDRIVE', 'HOMEPATH', 'USERPROFILE', 'USERNAME', 'USERDOMAIN',
    'USERDOMAIN_ROAMINGPROFILE', 'COMPUTERNAME', 'APPDATA', 'LOCALAPPDATA', 'PROGRAMDATA', 'PROGRAMFILES',
    'PROGRAMFILES(X86)', 'PROGRAMW6432', 'COMMONPROGRAMFILES', 'COMMONPROGRAMFILES(X86)', 'COMMONPROGRAMW6432',
    'SYSTEMROOT', 'SYSTEMDRIVE', 'WINDIR', 'COMSPEC', 'OS', 'PROCESSOR_ARCHITECTURE', 'PROCESSOR_IDENTIFIER',
    'PROCESSOR_LEVEL', 'PROCESSOR_REVISION', 'NUMBER_OF_PROCESSORS', 'PUBLIC', 'ALLUSERSPROFILE', 'LOGONSERVER',
    'SESSIONNAME', 'PROMPT', 'PSMODULEPATH', 'PWD', 'OLDPWD', 'SHLVL', 'TERM', 'SHELL', 'LANG', 'MSYSTEM', 'ORIGINAL_PATH',
    'EXEPATH', 'MSYS', 'MINGW_PREFIX', 'MSYSTEM_PREFIX', 'HOSTNAME', 'INFOPATH', 'MANPATH', 'ACLOCAL_PATH',
    'PKG_CONFIG_PATH', 'CONFIG_SITE', 'DISPLAY', 'PYTHONPATH', 'PYTHONIOENCODING', 'PYTHONUTF8', 'PYTHONHASHSEED',
    'CT_ORACLE_CACHE', 'CT_ORACLE_VERIFY', 'ONEDRIVE', 'ONEDRIVECONSUMER', 'DRIVERDATA', 'CHROME_CRASHPAD_PIPE_NAME', '_', 'TERM_PROGRAM',
    'TERM_PROGRAM_VERSION', 'COLORTERM', 'WT_SESSION', 'WT_PROFILE_ID', 'VSCODE_GIT_IPC_HANDLE', 'GIT_ASKPASS',
    'SDL_VIDEODRIVER', 'SDL_AUDIODRIVER',
}
DEFAULT_ROM = 'Chrono_Trigger_USA_SNES.sfc'
ORACLE_REFUSED = (97, b'ct_oracle: not an oracle mode')   # src/main.c ctOracleOnlyRefuse
_hash_memo = {}


def _file_sha(path):
    st = os.stat(path)
    k = (os.path.abspath(path), st.st_size, st.st_mtime_ns)
    h = _hash_memo.get(k)
    if h is None:
        d = hashlib.sha256()
        with open(path, 'rb') as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b''):
                d.update(chunk)
        h = _hash_memo[k] = d.hexdigest()
    return h


def _pe_identity_spans(b):
    """[(offset, length)] of the bytes a Windows linker writes FRESH on every link of the same code: the COFF header's
    TimeDateStamp, the optional header's CheckSum, each debug directory entry's TimeDateStamp, the CodeView record's PDB
    GUID + age, and the REPRO entry's hash. With /Brepro and /INCREMENTAL:NO (CMakeLists.txt) these are the only bytes two
    links of the same objects differ in (measured: 69 bytes, 2026-10-16). None for a file that is not a PE32+ image."""
    u16 = lambda o: int.from_bytes(b[o:o + 2], 'little')
    u32 = lambda o: int.from_bytes(b[o:o + 4], 'little')
    if len(b) < 0x40 or b[:2] != b'MZ':
        return None
    pe = u32(0x3C)
    if b[pe:pe + 4] != b'PE\0\0':
        return None
    nsec, optsz, opt = u16(pe + 6), u16(pe + 20), pe + 24
    if u16(opt) != 0x20B:
        return None
    spans = [(pe + 8, 4), (opt + 64, 4)]
    drva, dsize = u32(opt + 112 + 6 * 8), u32(opt + 112 + 6 * 8 + 4)
    sec = opt + optsz
    def file_off(rva):
        for i in range(nsec):
            s = sec + 40 * i
            va, vsz, raw = u32(s + 12), max(u32(s + 8), u32(s + 16)), u32(s + 20)
            if va <= rva < va + vsz:
                return raw + (rva - va)
        return None
    d = file_off(drva) if dsize else None
    if d is None:
        return spans
    for e in range(d, d + dsize - 27, 28):
        spans.append((e + 4, 4))
        typ, size, ptr = u32(e + 12), u32(e + 16), u32(e + 24)
        if typ == 2 and b[ptr:ptr + 4] == b'RSDS':      # IMAGE_DEBUG_TYPE_CODEVIEW: 'RSDS' GUID[16] age[4] path
            spans.append((ptr + 4, 20))
        elif typ == 16:                                  # IMAGE_DEBUG_TYPE_REPRO: the linker's own content hash
            spans.append((ptr, size))
    return spans


def _exe_sha(path):
    """sha256 of an executable minus its link identity (_pe_identity_spans): two links of the same code key the same, so a
    rebuild that changed nothing keeps the cache. Any other file (not a PE32+) hashes whole, like _file_sha."""
    st = os.stat(path)
    k = ('exe', os.path.abspath(path), st.st_size, st.st_mtime_ns)
    h = _hash_memo.get(k)
    if h is None:
        with open(path, 'rb') as fh:
            b = bytearray(fh.read())
        for off, n in _pe_identity_spans(b) or []:
            b[off:off + n] = bytes(n)
        h = _hash_memo[k] = hashlib.sha256(b).hexdigest()
    return h


def _reads(path, name):
    """Can the executable at `path` read the environment variable `name`? Its name is in the binary as a C string."""
    st = os.stat(path)
    k = ('strings', os.path.abspath(path), st.st_size, st.st_mtime_ns)
    b = _hash_memo.get(k)
    if b is None:
        with open(path, 'rb') as fh:
            b = _hash_memo[k] = fh.read()
    return (name.encode('utf-8', 'replace') + b'\0') in b


def _abs(p, cwd):
    return p if os.path.isabs(p) else os.path.join(cwd, p)


def _outputs(argv, env, cwd):
    """{role: absolute path} of every file this run may write."""
    outs = {}
    mode, oi = mode_of(argv)
    if oi is not None:
        o = _abs(argv[oi], cwd)
        for ext in (('', '.wram', '.vram', '.oam') if mode == '--peek' else ('', '.wram')):
            outs['out' + ext] = o + ext
    for k in ENV_OUTS:
        v = env.get(k)
        if not v:
            continue
        if k == 'PEEK_SHOTS':
            side = env.get('PEEK_SHOTS_SIDECARS') not in (None, '', '0')
            for n in (env.get('PEEK_SHOTS_AT') or '').split(','):
                if n.strip().lstrip('-').isdigit():
                    for ext in (('', '.wram', '.vram', '.oam') if side else ('',)):
                        outs['shot%s%s' % (int(n), ext)] = _abs('%s_f%d.raw%s' % (v, int(n), ext), cwd)
        else:
            outs['env:' + k] = _abs(v, cwd)
    return outs


# The exact size of every dump whose format fixes it (src/main.c runPeek / runDumpFb / PEEK_SHOTS): 512x480 [pad,B,G,R],
# the 128 KB WRAM, the 64 KB VRAM, OAM 544 + CGRAM 512 (+ the high table: 2*512 + 32 + 2*256). A file of another size is
# a run that did not finish writing it (a full disk, 2026-10-08: 11 truncated dumps were stored and replayed as truth).
_SIZES = {'': 512 * 480 * 4, '.wram': 0x20000, '.vram': 0x10000, '.oam': 2 * 512 + 32 + 2 * 256}


def _want_size(role):
    if role.startswith('env:'):
        return None
    for ext in ('.wram', '.vram', '.oam'):
        if role.endswith(ext):
            return _SIZES[ext]
    return _SIZES['']


def _key(argv, env, cwd):
    oi = mode_of(argv)[1]
    out_args = {oi} if oi is not None else set()
    keyenv = {k: v for k, v in env.items() if k.upper() not in _OS_VARS and k not in ENV_OUTS and _reads(argv[0], k)}
    parts = {'exe': _exe_sha(argv[0]), 'argv': [a for i, a in enumerate(argv[1:], 1) if i not in out_args], 'cwd': cwd,
             'env': sorted(keyenv.items()), 'files': []}
    for tag, v in [('a%d' % i, a) for i, a in enumerate(argv[1:], 1) if i not in out_args] + sorted(keyenv.items()):
        if isinstance(v, str) and v and len(v) < 1024:
            p = _abs(v, cwd)
            if os.path.isfile(p):
                parts['files'].append((str(tag), _file_sha(p)))
    rom = os.path.join(cwd, DEFAULT_ROM)
    if os.path.isfile(rom):
        parts['files'].append(('rom', _file_sha(rom)))
    return hashlib.sha256(json.dumps(parts, sort_keys=True).encode()).hexdigest()


def _streams(kwargs, out, err):
    so, se, cap = kwargs.get('stdout'), kwargs.get('stderr'), kwargs.get('capture_output')
    want_out = cap or so == subprocess.PIPE
    want_err = cap or se == subprocess.PIPE
    if se == subprocess.STDOUT:
        out, err, want_err = (out or b'') + (err or b''), None, False
    text = kwargs.get('text') or kwargs.get('universal_newlines') or kwargs.get('encoding') or kwargs.get('errors')

    def dec(b):
        if b is None or not text:
            return b
        return b.decode(kwargs.get('encoding') or 'utf-8', kwargs.get('errors') or 'strict').replace('\r\n', '\n')
    return (dec(out) if want_out else None), (dec(err) if want_err else None)


def _finish(argv, kwargs, rc, out, err):
    o, e = _streams(kwargs, out, err)
    if kwargs.get('check') and rc:
        raise subprocess.CalledProcessError(rc, argv, o, e)
    return subprocess.CompletedProcess(argv, rc, o, e)


def _ct_run_snapshot(argv, outs, real_run, kw):
    """CT_ORACLE_VERIFY=1 (a measurement switch): run ct.exe itself first and keep what it printed and wrote."""
    t0 = time.time()
    r = real_run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kw)
    files = {}
    for role, p in outs.items():
        if os.path.isfile(p) and os.stat(p).st_mtime >= t0 - 2:
            with open(p, 'rb') as fh:
                files[role] = fh.read()
            os.remove(p)                                       # so the second run's file is its own
    return r.returncode, r.stdout or b'', r.stderr or b'', files


def _verify_report(argv, cwd, ref, r, outs, t0):
    """...then compare ct_oracle.exe's run of the same call with it, byte for byte; one line per call in
    build/_oracle_verify.log (SAME, or DIFF and what differed)."""
    rc, so, se, files = ref
    bad = []
    if rc != r.returncode:
        bad.append('rc %d/%d' % (rc, r.returncode))
    if so != (r.stdout or b''):
        bad.append('stdout')
    if se != (r.stderr or b''):
        bad.append('stderr')
    for role, p in outs.items():
        mine = None
        if os.path.isfile(p) and os.stat(p).st_mtime >= t0 - 2:
            with open(p, 'rb') as fh:
                mine = fh.read()
        if files.get(role) != mine:
            bad.append('file ' + role)
    line = '%s %s\n' % ('SAME' if not bad else 'DIFF[' + ','.join(bad) + ']', json.dumps(argv[1:]))
    with open(os.path.join(cwd, 'build', '_oracle_verify.log'), 'a', encoding='utf-8') as fh:
        fh.write(line)


def run(argv, kwargs, real_run, cache=None):
    """subprocess.run(argv, **kwargs) through the cache `cache` (default: CT_ORACLE_CACHE), when this is a cacheable
    oracle run."""
    cache = cache or os.environ.get('CT_ORACLE_CACHE')
    if (not cache or not isinstance(argv, (list, tuple)) or len(argv) < 2 or not isinstance(argv[0], str)
            or os.path.basename(argv[0]).lower() != 'ct.exe' or mode_of(argv)[0] is None or not os.path.isfile(argv[0])
            or not all(isinstance(a, str) for a in argv)):
        return real_run(argv, **kwargs)
    if any(kwargs.get(k) for k in ('stdin', 'input', 'shell', 'timeout')):
        return real_run(argv, **kwargs)
    if not all(kwargs.get(k) in (None, subprocess.PIPE, subprocess.DEVNULL, subprocess.STDOUT) for k in ('stdout', 'stderr')):
        return real_run(argv, **kwargs)
    argv = list(argv)
    env = kwargs.get('env')
    env = dict(env) if env is not None else dict(os.environ)
    cwd = os.path.abspath(kwargs.get('cwd') or os.getcwd())
    # THE ORACLE ALONE: the same four modes from ct_oracle.exe (CMakeLists.txt, src/main.c CT_ORACLE_ONLY), built beside
    # ct.exe from ct.exe's own main.c with every other mode compiled out -- it links none of the port, so an edit to the
    # port leaves its bytes, and so this key, alone. The run is made by the binary the key names. A mode whose flags take
    # another branch of main()'s chain gets ORACLE_REFUSED from it and is run by ct.exe itself, uncached.
    xargv = argv
    oracle = os.path.join(os.path.dirname(argv[0]), 'ct_oracle.exe')
    if os.path.isfile(oracle):
        xargv = [oracle] + argv[1:]
    try:
        key = _key(xargv, env, cwd)
    except OSError:
        return real_run(argv, **kwargs)
    outs = _outputs(argv, env, cwd)
    ent = os.path.join(cache, key[:2], key)
    meta = os.path.join(ent, 'meta.json')
    if os.path.isfile(meta):
        try:
            with open(meta, encoding='utf-8') as fh:
                m = json.load(fh)
            blobs = {}
            for role in m['roles']:
                with open(os.path.join(ent, 'r_' + role.replace(':', '_')), 'rb') as fh:
                    blobs[role] = zlib.decompress(fh.read())
                if _want_size(role) not in (None, len(blobs[role])):
                    raise ValueError('a truncated dump in the cache')
            for role, data in blobs.items():
                dst = outs[role]
                os.makedirs(os.path.dirname(dst) or '.', exist_ok=True)
                with open(dst, 'wb') as fh:
                    fh.write(data)
            with open(os.path.join(ent, 'stdout'), 'rb') as fh:
                out = fh.read()
            with open(os.path.join(ent, 'stderr'), 'rb') as fh:
                err = fh.read()
            os.utime(meta)                                     # the entry's last use, for prune()
            return _finish(argv, kwargs, m['rc'], out, err)
        except (OSError, ValueError, KeyError, zlib.error):
            shutil.rmtree(ent, ignore_errors=True)            # a bad entry is dropped and the run made for real
    kw = {k: v for k, v in kwargs.items() if k not in ('stdout', 'stderr', 'capture_output', 'text', 'universal_newlines',
                                                       'encoding', 'errors', 'check')}
    verify = None
    if xargv is not argv and os.environ.get('CT_ORACLE_VERIFY') == '1':
        verify = _ct_run_snapshot(argv, outs, real_run, kw)
    t0 = time.time()
    r = real_run(xargv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kw)
    if xargv is not argv and r.returncode == ORACLE_REFUSED[0] and ORACLE_REFUSED[1] in (r.stderr or b''):
        return real_run(argv, **kwargs)
    if verify is not None:
        _verify_report(argv, cwd, verify, r, outs, t0)
    roles, whole = [], True
    for role, p in outs.items():
        try:
            if os.path.isfile(p) and os.stat(p).st_mtime >= t0 - 2:
                roles.append(role)
                if _want_size(role) not in (None, os.stat(p).st_size):
                    whole = False                             # a dump cut short: never stored
        except OSError:
            whole = False
    if mode_of(argv)[1] is not None and 'out' not in roles:
        whole = False                                         # --peek / --fb wrote no dump: nothing to replay
    if not whole:
        return _finish(argv, kwargs, r.returncode, r.stdout, r.stderr)
    tmp = '%s.tmp%d_%d' % (ent, os.getpid(), int(t0 * 1e6) % 1000000)
    try:
        os.makedirs(tmp, exist_ok=True)
        for role in roles:
            with open(outs[role], 'rb') as fh:
                data = zlib.compress(fh.read(), 1)             # a frame buffer is mostly runs: ~1 MB -> ~30 KB
            with open(os.path.join(tmp, 'r_' + role.replace(':', '_')), 'wb') as fh:
                fh.write(data)
        with open(os.path.join(tmp, 'stdout'), 'wb') as fh:
            fh.write(r.stdout or b'')
        with open(os.path.join(tmp, 'stderr'), 'wb') as fh:
            fh.write(r.stderr or b'')
        with open(os.path.join(tmp, 'meta.json'), 'w', encoding='utf-8') as fh:
            json.dump({'rc': r.returncode, 'roles': roles, 'argv': argv, 'cwd': cwd}, fh)
        if os.path.exists(ent):
            shutil.rmtree(tmp, ignore_errors=True)
        else:
            os.makedirs(os.path.dirname(ent), exist_ok=True)
            os.replace(tmp, ent)
    except OSError:
        shutil.rmtree(tmp, ignore_errors=True)
    return _finish(argv, kwargs, r.returncode, r.stdout, r.stderr)


def install():
    """Route subprocess.run / check_output through the cache (tools/oracle_cache_site/sitecustomize.py calls this)."""
    if getattr(subprocess, '_ct_oracle_cache', False):
        return
    real_run = subprocess.run

    def cached_run(*args, **kwargs):
        if len(args) > 1:
            return real_run(*args, **kwargs)
        argv = args[0] if args else kwargs.pop('args')
        return run(argv, kwargs, real_run)

    def cached_check_output(*args, **kwargs):
        if len(args) > 1 or 'stdout' in kwargs:
            return real_check_output(*args, **kwargs)
        argv = args[0] if args else kwargs.pop('args')
        return cached_run(argv, stdout=subprocess.PIPE, check=True, **kwargs).stdout

    real_check_output = subprocess.check_output
    subprocess.run = cached_run
    subprocess.check_output = cached_check_output
    subprocess._ct_oracle_cache = True


def prune(cache, max_bytes):
    """Drop the least recently used entries until the cache holds at most max_bytes. Returns (entries, bytes) kept."""
    ents = []
    for d1 in (os.listdir(cache) if os.path.isdir(cache) else []):
        p1 = os.path.join(cache, d1)
        for d2 in (os.listdir(p1) if os.path.isdir(p1) else []):
            ent = os.path.join(p1, d2)
            meta = os.path.join(ent, 'meta.json')
            if not os.path.isfile(meta):
                if '.tmp' in d2 and time.time() - os.stat(ent).st_mtime > 3600:
                    shutil.rmtree(ent, ignore_errors=True)
                continue
            size = sum(os.stat(os.path.join(ent, f)).st_size for f in os.listdir(ent))
            ents.append((os.stat(meta).st_mtime, size, ent))
    ents.sort(reverse=True)
    total, kept = 0, 0
    for _, size, ent in ents:
        if total + size > max_bytes:
            shutil.rmtree(ent, ignore_errors=True)
            continue
        total += size
        kept += 1
    return kept, total
