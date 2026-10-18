#!/usr/bin/env python3
r"""The table pack: the generated src/data/*.h tables as a data file Belthasar loads at run time (docs/ROM.md).

    python tools/gen/pack_tables.py pack [--out ct_tables.dat]   the pack, from today's src/data/*.h
    python tools/gen/pack_tables.py headers                      write src/tables/ from the shapes (tools/gen/shapes/data)
    python tools/gen/pack_tables.py check                        src/tables/ matches the shapes (exit 1 if not)
    python tools/gen/pack_tables.py dump [--pack ct_tables.dat]  list a pack's tables

The engine includes every generated table through src/tables/<name>.h. A normal build (ct.exe, a developer's
Belthasar) gets the real header from there unchanged (#include "../data/<name>.h"). A DATA-FREE build (CMake
-DCT_DATAFREE=ON, Belthasar only) gets declarations instead -- `extern const T name[dims];` -- and one storage
definition per table (src/tables/tables_storage.c); src/datapack.c fills the storage from the pack at startup and refuses
to run on any mismatch. So a data-free Belthasar.exe holds no byte of the tables: the player's setup makes the pack from
their own ROM.

THE PACK is a CTAS container (src/assets.h; the same reader) with four entry types:
    64 id 0   the header: "CTTB", format version, pointer size, byte order, table count, define count,
              the FNV-1a 64 of every table's layout string (the set's layout hash)
    65 id i   one table: [name len u8][name][pad to 4][size u32][layout hash u64][reloc count u32]
              [reloc offsets u32 x n][the table's bytes, exactly as the compiler lays them out]
    66 id 0   the string pool (each string NUL-terminated)
    67 id 0   the numeric #defines the headers carry: [count u32] {[name len u8][name][value i64]} -- the
              data-free build compiles the shapes' values; the loader checks each against the pack
A table's bytes follow the C layout of the targets the release ships (x86-64 and arm64: little-endian, natural
alignment, 8-byte pointers; Windows' LLP64 and the others' LP64 agree because no table uses `long`), padding zeroed.
A pointer member (`const char* name`, the loader's chapter/era strings) holds, in the pack, the u64 offset of its string
in the pool (all ones for NULL); its offset in the table is in the reloc list, and the loader stores pool + offset.

THE LAYOUT STRING of a table is its name and its type spelled out: every member's name, type and offset, every array's
count, every struct's size. Its hash is compiled into the data-free build (tables_storage.c) from the shapes, and the
pack carries it from the real header; the loader refuses a pack whose hash differs, so a pack made by another version of
a generator (a member added, a count changed) never lands in the wrong place. tables_storage.c also asserts at compile
time that the compiler's sizeof/offsetof agree with this file's layout model.

Only what the 47 headers use is understood: integer types, `char` arrays (string initializers), `const char*` members,
structs (named by typedef or anonymous), arrays of any of them, initializers with brace elision, integer expressions
over the headers' own #defines. Anything else is an error, never a guess.
"""
import argparse
import glob
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shapes import strip_comments  # noqa: E402

DATA = os.path.join(ROOT, 'src', 'data')
SHAPES = os.path.join(ROOT, 'tools', 'gen', 'shapes', 'data')
TABLES = os.path.join(ROOT, 'src', 'tables')

FORMAT_VERSION = 1
PTR_SIZE = 8
T_HEADER, T_TABLE, T_POOL, T_DEFINES = 64, 65, 66, 67
NULL_OFF = 0xFFFFFFFFFFFFFFFF


class PackError(Exception):
    pass


# ------------------------------------------------------------------------------------------------------------- types
class Scalar:
    def __init__(self, kind, size, signed):
        self.kind, self.size, self.align, self.signed = kind, size, size, signed

    def desc(self):
        return self.kind


class Pointer:                     # const char* -- the only pointer a table holds
    size = align = PTR_SIZE

    def desc(self):
        return 'p'


class Array:
    def __init__(self, elem, n):
        self.elem, self.n = elem, n
        self.align = elem.align

    @property
    def size(self):
        return self.elem.size * self.n

    def desc(self):
        return '%s[%d]' % (self.elem.desc(), self.n)


class Struct:
    def __init__(self, fields):
        self.fields, off, self.align = [], 0, 1
        for name, t in fields:
            off = (off + t.align - 1) // t.align * t.align
            self.fields.append((name, t, off))
            off += t.size
            self.align = max(self.align, t.align)
        self.size = (off + self.align - 1) // self.align * self.align

    def desc(self):
        return '{%s}#%d' % (';'.join('%s:%s@%d' % (n, t.desc(), o) for n, t, o in self.fields), self.size)


SCALARS = {
    ('uint8_t',): Scalar('u8', 1, False), ('int8_t',): Scalar('i8', 1, True),
    ('uint16_t',): Scalar('u16', 2, False), ('int16_t',): Scalar('i16', 2, True),
    ('uint32_t',): Scalar('u32', 4, False), ('int32_t',): Scalar('i32', 4, True),
    ('uint64_t',): Scalar('u64', 8, False), ('int64_t',): Scalar('i64', 8, True),
    ('char',): Scalar('c8', 1, False), ('signed', 'char'): Scalar('i8', 1, True),
    ('unsigned', 'char'): Scalar('u8', 1, False),
    ('short',): Scalar('i16', 2, True), ('signed', 'short'): Scalar('i16', 2, True),
    ('unsigned', 'short'): Scalar('u16', 2, False), ('short', 'int'): Scalar('i16', 2, True),
    ('unsigned', 'short', 'int'): Scalar('u16', 2, False),
    ('int',): Scalar('i32', 4, True), ('signed',): Scalar('i32', 4, True), ('signed', 'int'): Scalar('i32', 4, True),
    ('unsigned',): Scalar('u32', 4, False), ('unsigned', 'int'): Scalar('u32', 4, False),
}
CHAR_KINDS = ('c8', 'u8', 'i8')


# ------------------------------------------------------------------------------------------------------------- lexer
TOKEN = re.compile(r'''
    (?P<ws>\s+)
  | (?P<num>0[xX][0-9A-Fa-f]+[uUlL]*|\d+[uUlL]*)
  | (?P<id>[A-Za-z_]\w*)
  | (?P<str>"(?:[^"\\\n]|\\.)*")
  | (?P<chr>'(?:[^'\\\n]|\\.)+')
  | (?P<op><<|>>|[{}\[\]();,=*+\-/%&|^~!?:.<>])
''', re.X)


def lex(text):
    out, i = [], 0
    while i < len(text):
        m = TOKEN.match(text, i)
        if not m:
            raise PackError('cannot lex near %r' % text[i:i + 40])
        i = m.end()
        if m.lastgroup != 'ws':
            out.append((m.lastgroup, m.group()))
    return out


ESC = {'n': 10, 't': 9, 'r': 13, '0': 0, '\\': 92, '"': 34, "'": 39, 'a': 7, 'b': 8, 'f': 12, 'v': 11, '?': 63}


def unescape(body):
    """A C literal's body (no quotes) -> its bytes. The source is UTF-8; a non-ASCII character is refused (the
    compilers would not agree on its bytes)."""
    out, i = bytearray(), 0
    while i < len(body):
        c = body[i]
        if ord(c) > 127:
            raise PackError('non-ASCII character in a literal: %r' % body)
        if c != '\\':
            out.append(ord(c)); i += 1; continue
        e = body[i + 1]
        if e == 'x':
            m = re.match(r'[0-9A-Fa-f]+', body[i + 2:])
            out.append(int(m.group(), 16) & 0xFF); i += 2 + len(m.group())
        elif e in '01234567':
            m = re.match(r'[0-7]{1,3}', body[i + 1:])
            out.append(int(m.group(), 8) & 0xFF); i += 1 + len(m.group())
        elif e in ESC:
            out.append(ESC[e]); i += 2
        else:
            raise PackError('unknown escape \\%s' % e)
    return bytes(out)


# -------------------------------------------------------------------------------------------------------- expressions
class Expr:
    """Integer constant expressions over the headers' object-like #defines."""

    def __init__(self, defines):
        self.defines = defines

    def value(self, toks, ctx=''):
        self.t, self.i, self.depth = list(toks), 0, 0
        v = self.cond()
        if self.i != len(self.t):
            raise PackError('%s: cannot evaluate %r' % (ctx, ' '.join(x[1] for x in toks)))
        return v

    def peek(self):
        return self.t[self.i][1] if self.i < len(self.t) else None

    def take(self):
        tok = self.t[self.i]; self.i += 1
        return tok

    def cond(self):
        v = self.binary(0)
        if self.peek() == '?':
            self.take(); a = self.cond()
            if self.take()[1] != ':':
                raise PackError('bad ?:')
            b = self.cond()
            v = a if v else b
        return v

    PREC = [('|',), ('^',), ('&',), ('<<', '>>'), ('+', '-'), ('*', '/', '%')]

    def binary(self, level):
        if level == len(self.PREC):
            return self.unary()
        v = self.binary(level + 1)
        while self.peek() in self.PREC[level]:
            op = self.take()[1]
            r = self.binary(level + 1)
            if op == '|': v |= r
            elif op == '^': v ^= r
            elif op == '&': v &= r
            elif op == '<<': v <<= r
            elif op == '>>': v >>= r
            elif op == '+': v += r
            elif op == '-': v -= r
            elif op == '*': v *= r
            elif op == '/': v = int(v / r)
            else: v = v - int(v / r) * r
        return v

    def unary(self):
        op = self.peek()
        if op in ('-', '+', '~', '!'):
            self.take()
            v = self.unary()
            return -v if op == '-' else v if op == '+' else ~v if op == '~' else int(not v)
        return self.primary()

    def primary(self):
        kind, s = self.take()
        if s == '(':
            v = self.cond()
            if self.take()[1] != ')':
                raise PackError('missing )')
            return v
        if kind == 'num':
            return int(s.rstrip('uUlL'), 0) if not re.fullmatch(r'0\d+[uUlL]*', s) else int(s.rstrip('uUlL'), 8)
        if kind == 'chr':
            b = unescape(s[1:-1])
            if len(b) != 1:
                raise PackError('multi-char literal %s' % s)
            return b[0]
        if kind == 'id' and s in self.defines:
            self.depth += 1
            if self.depth > 50:
                raise PackError('recursive #define %s' % s)
            sub = Expr(self.defines)
            return sub.value(self.defines[s], s)
        raise PackError('unknown token in an expression: %s' % s)


# ------------------------------------------------------------------------------------------------------------- parser
class Table:
    def __init__(self, name, ctype, init, header, quals, span):
        self.name, self.ctype, self.init, self.header = name, ctype, init, header
        self.quals = quals            # 'obj' (const object) or 'ptrconst' (const char* const X[])
        self.span = span              # (start, end) in the stripped text, for the declaration rewrite
        self.anon = None              # the member text of an anonymous struct type

    def layout(self):
        return '%s=%s' % (self.name, self.ctype.desc())


class Headers:
    """Every generated header parsed into one environment (the headers share types and #defines: battle_marleatk.h
    uses battle_anim.h's CtAnimObj, menu_shop.h menu_chrome.h's sizes)."""

    def __init__(self):
        self.defines = {}             # name -> tokens
        self.define_order = []        # (header, name)
        self.typedefs = {}
        self.typedef_text = {}
        self.tables = []
        self.passthrough = []         # (header, name) of statics that are not tables (pointer tables)
        self.expr = Expr(self.defines)

    def parse_file(self, path):
        text = strip_comments(open(path, encoding='latin-1').read())   # comments may hold cp1252; literals are checked
        self.parse_text(text, os.path.basename(path))
        return text

    def parse_text(self, text, header):
        # preprocessor lines first (a line starting with #, continued by backslash); they are blanked so the C
        # tokens keep their offsets
        body = list(text)
        for m in re.finditer(r'^[ \t]*#(?:[^\n]*\\\n)*[^\n]*', text, re.M):
            line = m.group().replace('\\\n', ' ').strip()
            for k in range(m.start(), m.end()):
                if body[k] != '\n':
                    body[k] = ' '
            d = re.match(r'#\s*define\s+(\w+)(\(?)\s*(.*)$', line)
            if d:
                if d.group(2):
                    raise PackError('%s: function-like macro %s' % (header, d.group(1)))
                name, val = d.group(1), d.group(3).strip()
                toks = lex(val)
                if name in self.defines and self.defines[name] != toks:
                    raise PackError('%s: #define %s differs from another header' % (header, name))
                if name not in self.defines:
                    self.define_order.append((header, name))
                self.defines[name] = toks
            elif not re.match(r'#\s*(ifndef|endif|include|pragma\s+once)\b', line):
                raise PackError('%s: unsupported preprocessor line %r' % (header, line))
        code = ''.join(body)
        self.toks = []
        for m in TOKEN.finditer(code):
            if m.lastgroup is None:
                continue
            if m.lastgroup != 'ws':
                self.toks.append((m.lastgroup, m.group(), m.start(), m.end()))
        # whatever TOKEN.finditer skipped must be whitespace only
        if re.sub(TOKEN.pattern, '', code, flags=re.X).strip():
            raise PackError('%s: cannot lex %r' % (header, re.sub(TOKEN.pattern, '', code, flags=re.X).strip()[:40]))
        self.p, self.header, self.code = 0, header, code
        while self.p < len(self.toks):
            self.declaration()

    # token helpers
    def peek(self, k=0):
        j = self.p + k
        return self.toks[j][1] if j < len(self.toks) else None

    def take(self, want=None):
        if self.p >= len(self.toks):
            raise PackError('%s: unexpected end' % self.header)
        tok = self.toks[self.p]
        if want is not None and tok[1] != want:
            raise PackError('%s: expected %r, found %r near offset %d' % (self.header, want, tok[1], tok[2]))
        self.p += 1
        return tok

    def type_spec(self):
        """-> (type, leading const?, member text of an anonymous struct or None)"""
        words, const, anon = [], False, None
        while True:
            s = self.peek()
            if s == 'const':
                self.take(); const = True
            elif s == 'struct':
                self.take()
                if self.peek() != '{':
                    raise PackError('%s: struct tags are not supported' % self.header)
                start = self.toks[self.p][2]
                fields = self.struct_body()
                anon = self.code[start:self.toks[self.p - 1][3]]
                return Struct(fields), const, anon
            elif s in ('unsigned', 'signed', 'char', 'short', 'int'):
                words.append(self.take()[1])
            elif s in ('long', 'float', 'double', '_Bool', 'bool'):
                raise PackError('%s: type %s is not supported (its size differs between the targets or it is not '
                                'an integer)' % (self.header, s))
            elif not words and s is not None and (s in self.typedefs or tuple([s]) in SCALARS):
                self.take()
                if s in self.typedefs:
                    return self.typedefs[s], const, None
                words.append(s)
                break
            else:
                break
        if not words or tuple(words) not in SCALARS:
            raise PackError('%s: unknown type %r near %r' % (self.header, ' '.join(words), self.peek()))
        return SCALARS[tuple(words)], const, None

    def struct_body(self):
        self.take('{')
        fields = []
        while self.peek() != '}':
            base, _, _ = self.type_spec()
            while True:
                t = base
                if self.peek() == '*':
                    self.take()
                    if self.peek() == 'const':
                        raise PackError('%s: a const pointer member' % self.header)
                    if not (isinstance(base, Scalar) and base.kind == 'c8'):
                        raise PackError('%s: only char pointers are supported' % self.header)
                    t = Pointer()
                name = self.take()[1]
                t = self.dims(t)
                fields.append((name, t))
                if self.peek() == ',':
                    self.take(); continue
                self.take(';'); break
        self.take('}')
        return fields

    def dims(self, t, allow_open=False):
        ns = []
        while self.peek() == '[':
            self.take('[')
            expr = []
            while self.peek() != ']':
                expr.append(self.take()[:2])
            self.take(']')
            if not expr:
                if not allow_open or ns:
                    raise PackError('%s: an open array dimension here' % self.header)
                ns.append(None)
            else:
                ns.append(self.expr.value(expr, self.header))
        for n in reversed(ns):
            t = Array(t, n)
        return t

    def declaration(self):
        start = self.toks[self.p][2]
        if self.peek() == 'typedef':
            self.take()
            t, _, _ = self.type_spec()
            name = self.take()[1]
            t = self.dims(t)
            self.take(';')
            if name in self.typedefs and self.typedefs[name].desc() != t.desc():
                raise PackError('%s: typedef %s differs from another header' % (self.header, name))
            self.typedefs[name] = t
            return
        if self.peek() != 'static':
            raise PackError('%s: expected a typedef or a static declaration, found %r' % (self.header, self.peek()))
        self.take('static')
        base, const, anon = self.type_spec()
        if not const:
            raise PackError('%s: a non-const static' % self.header)
        if self.peek() == '(':                        # a pointer table: (*const name[n])[m] = { other tables }
            depth = 0
            while True:
                s = self.take()[1]
                if s == '(':
                    depth += 1
                elif s == ')':
                    depth -= 1
                elif s == ';' and depth == 0:
                    break
            name = re.search(r'\(\s*\*\s*const\s+(\w+)', self.code[start:self.toks[self.p - 1][3]]).group(1)
            self.passthrough.append((self.header, name))
            return
        quals = 'obj'
        t = base
        if self.peek() == '*':
            self.take()
            if not (isinstance(base, Scalar) and base.kind == 'c8'):
                raise PackError('%s: only char pointers are supported' % self.header)
            t = Pointer()
            if self.peek() != 'const':
                raise PackError('%s: a pointer table must be `const char* const`' % self.header)
            self.take('const')
            quals = 'ptrconst'
        name = self.take()[1]
        t = self.dims(t, allow_open=True)
        if not isinstance(t, Array):
            raise PackError('%s: %s is not an array' % (self.header, name))
        self.take('=')
        init = self.initializer()
        self.take(';')
        if isinstance(t, Array) and t.n is None:
            t = Array(t.elem, count_elements(t.elem, init, name))
        tab = Table(name, t, init, self.header, quals, (start, self.toks[self.p - 1][3]))
        tab.anon = anon
        self.tables.append(tab)

    def initializer(self):
        """-> ('list', [...]) | ('str', bytes) | ('expr', tokens)"""
        if self.peek() == '{':
            self.take('{')
            items = []
            while self.peek() != '}':
                if self.peek() in ('.', '['):
                    raise PackError('%s: designated initializers are not supported' % self.header)
                items.append(self.initializer())
                if self.peek() == ',':
                    self.take()
                elif self.peek() != '}':
                    raise PackError('%s: expected , or } near %r' % (self.header, self.peek()))
            self.take('}')
            return ('list', items)
        if self.toks[self.p][0] == 'str':
            b = b''
            while self.p < len(self.toks) and self.toks[self.p][0] == 'str':
                b += unescape(self.take()[1][1:-1])
            return ('str', b)
        expr, depth = [], 0
        while True:
            s = self.peek()
            if s is None:
                raise PackError('%s: unexpected end in an initializer' % self.header)
            if depth == 0 and s in (',', '}', ';'):
                break
            if s == '(':
                depth += 1
            elif s == ')':
                depth -= 1
            expr.append(self.take()[:2])
        return ('expr', expr)


# -------------------------------------------------------------------------------------------------------- initializers
def is_char_array(t):
    return isinstance(t, Array) and isinstance(t.elem, Scalar) and t.elem.kind in CHAR_KINDS


def is_aggregate(t):
    return isinstance(t, (Array, Struct))


def subobjects(t):
    if isinstance(t, Array):
        for k in range(t.n):
            yield t.elem, k * t.elem.size
    else:
        for _, ft, off in t.fields:
            yield ft, off


def count_elements(elem, init, name):
    """The first dimension an open array `T x[] = {...}` gets: the elements its initializer fills (brace elision
    counted the way C counts it)."""
    if init[0] != 'list':
        raise PackError('%s: an open array needs a braced initializer' % name)
    items, i, n = init[1], 0, 0
    while i < len(items):
        it = items[i]
        if it[0] == 'list' or not is_aggregate(elem) or (it[0] == 'str' and is_char_array(elem)):
            i += 1
        else:
            i = Writer(None, None, None).fill(elem, 0, items, i, dry=True)
        n += 1
    return n


class Writer:
    def __init__(self, table, buf, relocs):
        self.table, self.buf, self.relocs = table, buf, relocs
        self.pool = None

    def err(self, msg):
        raise PackError('%s (%s): %s' % (self.table.name if self.table else '?', self.table.header if self.table else '?', msg))

    def scalar(self, t, off, item, dry=False):
        if dry:
            return
        if isinstance(t, Pointer):
            self.relocs.append(off)
            if item[0] == 'str':
                v = self.pool.add(item[1])
            elif item[0] == 'expr' and self.table.headers.expr.value(item[1], self.table.name) == 0:
                v = NULL_OFF
            else:
                self.err('a pointer member initialized from %r' % (item,))
            self.buf[off:off + 8] = struct.pack('<Q', v)
            return
        if item[0] != 'expr':
            self.err('scalar %s initialized from %s' % (t.desc(), item[0]))
        v = self.table.headers.expr.value(item[1], self.table.name)
        if not (-(1 << (8 * t.size - 1)) <= v < (1 << (8 * t.size))):   # C converts modulo 2^n; past that, an error
            self.err('value %d does not fit %s' % (v, t.desc()))
        self.buf[off:off + t.size] = (v & ((1 << (8 * t.size)) - 1)).to_bytes(t.size, 'little')

    def string(self, t, off, b, dry=False):
        if len(b) > t.n:
            self.err('string of %d bytes into char[%d]' % (len(b), t.n))
        if not dry:
            self.buf[off:off + len(b)] = b          # the rest (the NUL, the tail) stays zero

    def fill(self, t, off, items, i, dry=False):
        """Initialize aggregate t from items[i:] without braces of its own (brace elision); -> the next index."""
        for st, soff in subobjects(t):
            if i >= len(items):
                break
            it = items[i]
            if it[0] == 'list':
                self.braced(st, off + soff, it, dry); i += 1
            elif it[0] == 'str' and is_char_array(st):
                self.string(st, off + soff, it[1], dry); i += 1
            elif is_aggregate(st):
                i = self.fill(st, off + soff, items, i, dry)
            else:
                self.scalar(st, off + soff, it, dry); i += 1
        return i

    def braced(self, t, off, it, dry=False):
        items = it[1]
        if not is_aggregate(t):
            if len(items) != 1:
                self.err('a braced scalar with %d items' % len(items))
            self.scalar(t, off, items[0], dry)
            return
        if is_char_array(t) and len(items) == 1 and items[0][0] == 'str':
            self.string(t, off, items[0][1], dry)
            return
        n = self.fill(t, off, items, 0, dry)
        if n != len(items):
            self.err('%d excess initializers' % (len(items) - n))


class Pool:
    def __init__(self):
        self.data, self.at = bytearray(), {}

    def add(self, b):
        if b not in self.at:
            self.at[b] = len(self.data)
            self.data += b + b'\0'
        return self.at[b]


def fnv64(s):
    h = 0xcbf29ce484222325
    for c in s.encode():
        h = ((h ^ c) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def parse_dir(d):
    hs = Headers()
    paths = sorted(glob.glob(os.path.join(d, '*.h')))
    if not paths:
        raise PackError('no headers in %s' % d)
    texts = {}
    # battle_anim.h first: two headers include it for its types
    for p in sorted(paths, key=lambda p: os.path.basename(p) != 'battle_anim.h'):
        texts[os.path.basename(p)] = hs.parse_file(p)
    for t in hs.tables:
        t.headers = hs
    return hs, texts


def numeric_defines(hs):
    """The #defines whose value is an integer constant expression (an include guard has no value)."""
    out = []
    for header, name in hs.define_order:
        toks = hs.defines[name]
        if not toks:
            continue
        try:
            out.append((header, name, hs.expr.value(toks, name)))
        except PackError:
            raise PackError('%s: #define %s is not an integer constant' % (header, name))
    return out


# ---------------------------------------------------------------------------------------------------------------- pack
def element_of(t):
    """A table's element: its type with every array dimension stripped."""
    while isinstance(t, Array):
        t = t.elem
    return t


def pointer_offsets(t, base=0):
    """The offsets of every pointer inside type t."""
    if isinstance(t, Pointer):
        return [base]
    if isinstance(t, Array):
        return [o for k in range(t.n) for o in pointer_offsets(t.elem, base + k * t.elem.size)]
    if isinstance(t, Struct):
        return [o for _, ft, off in t.fields for o in pointer_offsets(ft, base + off)]
    return []


def pointer_slots(t):
    return pointer_offsets(t)


def build_pack(hs):
    pool = Pool()
    tables = []
    for t in sorted(hs.tables, key=lambda t: t.name):
        buf, relocs = bytearray(t.ctype.size), []
        w = Writer(t, buf, relocs)
        w.pool = pool
        w.braced(t.ctype, 0, t.init)
        slots = pointer_slots(t.ctype)
        for off in sorted(set(slots) - set(relocs)):   # a pointer the initializer left out is NULL
            buf[off:off + 8] = struct.pack('<Q', NULL_OFF)
        tables.append((t, bytes(buf), slots))
    names = [t.name for t, _, _ in tables]
    if len(set(names)) != len(names):
        raise PackError('two tables share a name')
    defines = numeric_defines(hs)
    entries = []
    set_hash = fnv64('\n'.join(t.layout() for t, _, _ in tables))
    entries.append((T_HEADER, 0, b'CTTB' + struct.pack('<IIIIIQ', FORMAT_VERSION, PTR_SIZE, 1, len(tables),
                                                         len(defines), set_hash)))
    for i, (t, data, relocs) in enumerate(tables):
        nm = t.name.encode()
        head = bytes([len(nm)]) + nm
        head += b'\0' * (-len(head) % 4)
        head += struct.pack('<IQI', len(data), fnv64(t.layout()), len(relocs))
        head += b''.join(struct.pack('<I', r) for r in relocs)
        entries.append((T_TABLE, i, head + data))
    entries.append((T_POOL, 0, bytes(pool.data)))
    db = struct.pack('<I', len(defines))
    for _, name, v in defines:
        db += bytes([len(name)]) + name.encode() + struct.pack('<q', v)
    entries.append((T_DEFINES, 0, db))
    return container(entries), tables


def container(entries):
    out = bytearray(b'CTAS' + struct.pack('<II', 1, len(entries)))
    off = 12 + 16 * len(entries)
    blobs = bytearray()
    for typ, eid, blob in entries:
        pad = -len(blobs) % 8                         # every blob 8-aligned in the file
        blobs += b'\0' * pad
        out += struct.pack('<IIII', typ, eid, off + len(blobs), len(blob))
        blobs += blob
    return bytes(out + blobs)


def read_pack(data):
    if data[:4] != b'CTAS':
        raise PackError('not a CTAS container')
    n = struct.unpack_from('<I', data, 8)[0]
    ents = []
    for k in range(n):
        typ, eid, off, ln = struct.unpack_from('<IIII', data, 12 + 16 * k)
        ents.append((typ, eid, data[off:off + ln]))
    return ents


# ------------------------------------------------------------------------------------------------------------ headers
TABLES_DOC = ('/* GENERATED by tools/gen/pack_tables.py headers -- do not edit. No data: declarations and sizes only.\n'
              '   A normal build includes the generated table below; a data-free build (CT_DATAFREE, docs/ROM.md)\n'
              '   declares the tables and src/datapack.c fills them from the pack made from your ROM. */\n')


def decl_text(t, text):
    """The data-free declaration of one table: storage-class and the object's own const are macros
    (src/tables/ct_tables.h), so the one storage TU (tables_storage.c) turns the declarations into definitions. The
    dimensions keep the header's spelling (an open one gets its count)."""
    s, e = t.span
    dims = re.search(r'\b' + re.escape(t.name) + r'\s*((?:\[[^\]]*\]\s*)+)=', text[s:e]).group(1)
    dims = re.sub(r'\s+(?=\[)', '', dims.strip())
    if dims.startswith('[]'):
        dims = '[%d]' % t.ctype.n + dims[2:]
    if t.quals == 'ptrconst':
        return 'CT_TBL_EXTERN const char* CT_TBL_CONST %s%s;' % (t.name, dims)
    return 'CT_TBL_EXTERN CT_TBL_CONST %s %s%s;' % (t.type_name, t.name, dims)


def element_type_name(t, text):
    """The element type as the header spells it (a typedef name or a scalar's words)."""
    s, e = t.span
    m = re.match(r'\s*static\s+const\s+(.*?)\s*\b' + re.escape(t.name) + r'\s*\[', text[s:e], re.S)
    return m.group(1)


def make_headers(hs, texts):
    """-> {filename: text} for src/tables/ from the shapes' parse."""
    files = {}
    by_header = {}
    for t in hs.tables:
        by_header.setdefault(t.header, []).append(t)
    pass_by = {}
    for h, n in hs.passthrough:
        pass_by.setdefault(h, []).append(n)
    checks = []
    for header, text in sorted(texts.items()):
        out, pos = [], 0
        for t in sorted(by_header.get(header, []), key=lambda t: t.span[0]):
            s, e = t.span
            out.append(text[pos:s])
            if t.anon is not None:
                tn = 'CtTbl_' + t.name
                out.append('typedef struct %s %s;\n' % (re.sub(r'\s+', ' ', t.anon), tn))
                t.type_name = tn
            elif t.quals == 'obj':
                t.type_name = element_type_name(t, text)
            out.append(decl_text(t, text))
            pos = e
        out.append(text[pos:])
        body = ''.join(out)
        for n in pass_by.get(header, []):             # a pointer table over other tables: not in the storage TU
            m = re.search(r'static\s+const[^;]*\(\s*\*\s*const\s+' + n + r'\b[^;]*;', body)
            body = body[:m.start()] + '#ifndef CT_TBL_STORAGE\n' + m.group() + '\n#endif' + body[m.end():]
        body = re.sub(r'[ \t]+\n', '\n', body)
        body = re.sub(r'\n\s*\n+', '\n', body).strip() + '\n'
        files[header] = (TABLES_DOC + '#ifndef CT_DATAFREE\n#include "../data/%s"\n#else\n#include "ct_tables.h"\n'
                         % header + body + '#endif\n')
    # the storage TU: every table defined once (zeroed; the pack fills it), the layout model asserted, the registry
    seen_types = {n: ty for n, ty in hs.typedefs.items() if isinstance(ty, Struct)}
    for t in hs.tables:
        if t.anon is not None:
            seen_types[t.type_name] = element_of(t.ctype)
    lines = ['/* GENERATED by tools/gen/pack_tables.py headers -- do not edit. No data: the storage of every table,\n'
             '   compiled only by a data-free build (CT_DATAFREE); src/datapack.c fills it from the pack made from\n'
             '   your ROM (docs/ROM.md). */']
    lines.append('#ifndef CT_DATAFREE\n#error "tables_storage.c is compiled only by a data-free build (CT_DATAFREE)"\n#endif')
    lines.append('#define CT_TBL_STORAGE')
    lines.append('#define CT_TBL_EXTERN')
    lines.append('#define CT_TBL_CONST')
    lines.append('#include <stddef.h>')
    order = sorted(texts, key=lambda h: h != 'battle_anim.h')
    for h in order:
        lines.append('#include "%s"' % h)
    lines.append('#include "../datapack.h"')
    lines.append('')
    lines.append('/* The layout model of tools/gen/pack_tables.py, checked against this compiler. */')
    lines.append('#define CT_TBL_CHECK(name, cond) typedef char ct_tbl_check_##name[(cond) ? 1 : -1]')
    k = 0
    for name, ty in sorted(seen_types.items()):
        lines.append('CT_TBL_CHECK(t%d, sizeof(%s) == %d);' % (k, name, ty.size)); k += 1
        for fname, ft, off in ty.fields:
            lines.append('CT_TBL_CHECK(t%d, offsetof(%s, %s) == %d);' % (k, name, fname, off)); k += 1
    for t in sorted(hs.tables, key=lambda t: t.name):
        lines.append('CT_TBL_CHECK(t%d, sizeof(%s) == %d);' % (k, t.name, t.ctype.size)); k += 1
    lines.append('')
    lines.append('/* Where each table holds pointers: the offsets inside one element (the type under every dimension). */')
    for t in sorted(hs.tables, key=lambda t: t.name):
        offs = pointer_offsets(element_of(t.ctype))
        if offs:
            lines.append('static const unsigned ctTblPtr_%s[] = { %s };' % (t.name, ', '.join(map(str, offs))))
    lines.append('')
    lines.append('const CtTableDesc ctTableDescs[] = {')
    for t in sorted(hs.tables, key=lambda t: t.name):
        el = element_of(t.ctype)
        offs = pointer_offsets(el)
        ptr = ('ctTblPtr_%s, %d' % (t.name, len(offs))) if offs else 'NULL, 0'
        lines.append('  { "%s", (void*)%s, sizeof %s, 0x%016XULL, %d, %s },'
                     % (t.name, t.name, t.name, fnv64(t.layout()), el.size, ptr))
    lines.append('};')
    lines.append('const int ctTableDescCount = (int)(sizeof ctTableDescs / sizeof ctTableDescs[0]);')
    lines.append('const unsigned long long ctTableSetHash = 0x%016XULL;'
                 % fnv64('\n'.join(t.layout() for t in sorted(hs.tables, key=lambda t: t.name))))
    lines.append('')
    lines.append('/* The #defines the shapes compile in; the pack carries the ROM\'s and the loader compares them. */')
    lines.append('const CtTableDefine ctTableDefines[] = {')
    for _, name, v in numeric_defines(hs):
        lines.append('  { "%s", (long long)(%s) },' % (name, name))
    lines.append('};')
    lines.append('const int ctTableDefineCount = (int)(sizeof ctTableDefines / sizeof ctTableDefines[0]);')
    files['tables_storage.c'] = '\n'.join(lines) + '\n'
    # the proof's other half: the tables compiled from the REAL headers (a developer tree only), by name
    real = ['/* GENERATED by tools/gen/pack_tables.py headers -- do not edit. No data: the tables compiled from the\n'
            '   generated headers themselves (src/data/*.h), for the byte-for-byte proof against a pack\n'
            '   (tools/gen/tables_proof.py). Builds only where src/data holds the real headers. */',
            '#include <stddef.h>']
    real += ['#include "../data/%s"' % h for h in order]
    real.append('typedef struct { const char* name; const void* data; size_t size; } CtTableReal;')
    real.append('const CtTableReal ctTablesReal[] = {')
    for t in sorted(hs.tables, key=lambda t: t.name):
        real.append('  { "%s", %s, sizeof %s },' % (t.name, t.name, t.name))
    real.append('};')
    real.append('const int ctTablesRealCount = (int)(sizeof ctTablesReal / sizeof ctTablesReal[0]);')
    files['tables_real.c'] = '\n'.join(real) + '\n'
    files['ct_tables.h'] = ('/* GENERATED by tools/gen/pack_tables.py headers -- do not edit. A data-free declaration is\n'
                            '   `CT_TBL_EXTERN CT_TBL_CONST T name[n];`: extern const everywhere, a plain definition in\n'
                            '   tables_storage.c (the one TU that defines both macros empty). */\n'
                            '#ifndef CT_TABLES_H\n#define CT_TABLES_H\n#include <stdint.h>\n'
                            '#ifndef CT_TBL_EXTERN\n#define CT_TBL_EXTERN extern\n#endif\n'
                            '#ifndef CT_TBL_CONST\n#define CT_TBL_CONST const\n#endif\n#endif\n')
    return files


def want_headers():
    hs, texts = parse_dir(SHAPES)
    return make_headers(hs, texts)


# ---------------------------------------------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('cmd', choices=('pack', 'headers', 'check', 'dump'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'ct_tables.dat'))
    ap.add_argument('--pack', default=os.path.join(ROOT, 'ct_tables.dat'))
    ap.add_argument('--data', default=DATA, help='the generated headers (default src/data)')
    a = ap.parse_args()
    try:
        if a.cmd == 'pack':
            hs, _ = parse_dir(a.data)
            shapes = [os.path.basename(p) for p in glob.glob(os.path.join(a.data, '*.h'))
                      if open(p, 'rb').read(64).startswith(b'/* SHAPE of a generated file')]
            if shapes:
                raise PackError('%d header(s) are still shapes, not generated from the ROM: %s'
                                % (len(shapes), ', '.join(sorted(shapes))))
            data, tables = build_pack(hs)
            want = {t.name: fnv64(t.layout()) for t in parse_dir(SHAPES)[0].tables}
            have = {t.name: fnv64(t.layout()) for t, _, _ in tables}
            if want != have:
                bad = sorted(n for n in set(want) | set(have) if want.get(n) != have.get(n))
                raise PackError('the headers\' layout differs from the shapes (python tools/gen/shapes.py make, then '
                                'pack_tables.py headers): %s' % ', '.join(bad))
            tmp = a.out + '.tmp'
            open(tmp, 'wb').write(data)
            os.replace(tmp, a.out)
            print('pack_tables: %d tables, %d bytes -> %s' % (len(tables), len(data), os.path.relpath(a.out, ROOT)))
            return 0
        if a.cmd == 'dump':
            for typ, eid, blob in read_pack(open(a.pack, 'rb').read()):
                if typ == T_TABLE:
                    n = blob[0]
                    name = blob[1:1 + n].decode()
                    o = 1 + n + (-(1 + n) % 4)
                    size, h, nr = struct.unpack_from('<IQI', blob, o)
                    print('%-28s %7d bytes  %3d relocs  layout %016X' % (name, size, nr, h))
                elif typ == T_HEADER:
                    ver, ptr, _, nt, nd, h = struct.unpack_from('<IIIIIQ', blob, 4)
                    print('header: version %d, pointer size %d, %d tables, %d defines, set hash %016X'
                          % (ver, ptr, nt, nd, h))
            return 0
        files = want_headers()
        if a.cmd == 'headers':
            os.makedirs(TABLES, exist_ok=True)
            for name, text in files.items():
                p = os.path.join(TABLES, name)
                old = open(p, encoding='utf-8').read() if os.path.exists(p) else None
                if old != text:
                    open(p, 'w', encoding='utf-8', newline='\n').write(text)
            stale = [f for f in os.listdir(TABLES) if f not in files]
            for f in stale:
                os.remove(os.path.join(TABLES, f))
            print('pack_tables: src/tables/ written (%d files)' % len(files))
            return 0
        bad = [n for n, text in files.items() if not os.path.exists(os.path.join(TABLES, n))
               or open(os.path.join(TABLES, n), encoding='utf-8').read() != text]
        bad += [f for f in os.listdir(TABLES) if f not in files]
        for n in bad:
            print('pack_tables: src/tables/%s is stale' % n)
        print('pack_tables: src/tables/ %s' % ('stale -- python tools/gen/pack_tables.py headers' if bad else 'current'))
        return 1 if bad else 0
    except PackError as e:
        print('pack_tables: FAIL: %s' % e)
        return 1


if __name__ == '__main__':
    sys.exit(main())
