#!/usr/bin/env python3
"""Decode Chrono Trigger (SNES/USA) text — the dialogue dictionary + tokenized
strings, and the fixed-width "mapped" strings (item names). Reverse-engineered from
CTViewer's text_decoder.rs, verified against the ROM.

Two encodings:
  - Tokenized / dictionary ("huffman"): dialogue, world names, textbox tables.
    Token bytes: 0=end, 1/2=literal next char, 3=wait(+2 bytes), 0x21-0x9F=dictionary
    substring[v-0x21], >0x9F=parse_character, 0x04-0x20=control code.
  - Mapped: one byte -> one FONT_8_MAP entry (item names; fixed-size records).

Dictionary: 128 substrings @ 0x1EFA00 (u16 LE pointers - 0xFA00, then [len][tokens]).

Usage:
  python assets/text.py <rom.sfc> dict                 # dump the 128 dictionary words
  python assets/text.py <rom.sfc> items                # item names (mapped)
  python assets/text.py <rom.sfc> world                # world + exit names
  python assets/text.py <rom.sfc> table <hexaddr> [n]  # decode a dialogue string table
  python assets/text.py <rom.sfc> dialogue [out.txt]   # all dialogue (scan scripts for op 0xB8)
"""
import sys

DICT_BASE = 0x1EFA00
DICT_COUNT = 128

def parse_character(v):
    if 0xA0 <= v <= 0xB9: return chr(0x41 + (v - 0xA0))   # A-Z
    if 0xBA <= v <= 0xD3: return chr(0x61 + (v - 0xBA))   # a-z
    if 0xD4 <= v <= 0xDD: return chr(0x30 + (v - 0xD4))   # 0-9
    return {0xDE: '!', 0xDF: '?', 0xE0: '/', 0xE1: '"', 0xE2: '"', 0xE3: ':', 0xE4: '&',
            0xE5: '(', 0xE6: ')', 0xE7: "'", 0xE8: '.', 0xE9: ',', 0xEA: '=', 0xEB: '-',
            0xEC: '+', 0xED: '%', 0xEE: '♫', 0xEF: ' ', 0xF0: '♥', 0xF1: '…',
            0xF2: '∞', 0xF3: '#'}.get(v, '�')

# Control codes 0x04-0x20, rendered readably (names = their default). Returns
# (text, extra_bytes_consumed). 0x12 (tech name) consumes one inline byte.
# 0x13-0x20 are NAME SUBSTITUTIONS: the ROM's text decoder copies them at render time from
# the RAM character-name buffers at $7E:2C23 (token handler C2/5B15, pointer table C2/5FD8;
# party slots 0x1B-0x1D via the active party at $7E:2980, C2/5B70). To keep the names as true
# VARIABLES (the original design -- a future naming screen edits them), the asset packer emits
# a MARKER '\x01'+<code> that the field engine resolves from its own name table, instead of
# baking a default. The standalone CLI dumps (namesub=False) keep readable default names.
_NAME_CODES = {0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x20}
_NAME_DEFAULT = {0x13: 'Crono', 0x14: 'Marle', 0x15: 'Lucca', 0x16: 'Robo', 0x17: 'Frog',
                 0x18: 'Ayla', 0x19: 'Magus', 0x1A: 'Crono', 0x1B: '{pt1}', 0x1C: '{pt2}',
                 0x1D: '{pt3}', 0x20: 'Epoch'}
# THE THREE SPACES ARE PART OF THE BREAK TOKEN. Codes 0x06/0x08/0x0A are ONE token meaning
# "line break, then indent three"; 0x0C is the page break's indented twin. 0x05/0x07/0x09/0x0B
# are their bare siblings. The ROM's typewriter (C0/2126) spends exactly ONE iteration on a
# control code -- the indent it carries is never typed -- so a consumer that expands the token
# into four characters and charges a frame each runs three frames late per indented line.
# `ind` is what the indent expands to: the packer passes a MARKER (see pack_dialogue) so the
# field engine can still tell the token's own indent from literal spaces in the text; every
# other caller keeps the readable three spaces.
_IND = '\x02'                                 # per-space marker char (packer only)
_NAMES = {0x05: '\n', 0x06: '\n%s', 0x07: '\n', 0x08: '\n%s', 0x09: '\n', 0x0A: '\n%s',
          0x0B: '\n———\n', 0x0C: '\n———\n%s',
          0x0D: '{num}', 0x0E: '{num}', 0x0F: '{num}',
          0x1E: 'Nadia', 0x1F: '{item}'}     # 0x1E = the ROM's fixed string $C2:6146 = "Nadia"

def special(v, rom, o, namesub=False, ind='   '):
    if v == 0x12:
        return '', 1                          # tech-name substitution; consumes 1 byte
    if v in _NAME_CODES:
        return (('\x01' + chr(v)) if namesub else _NAME_DEFAULT[v]), 0
    # {num}: 0x0D/0x0E/0x0F print the 1/2/3-byte value at the text's number cursor (C2/5953/5985/59C8 read
    # [$3D], which C2/0003's init C2/5815 points at $00:0200 once per string). RAM, so the packer emits the
    # same render-time marker as the names and the field engine reads $7E:0200 when the box opens.
    if namesub and v in (0x0D, 0x0E, 0x0F):
        return '\x01' + chr(v), 0
    # {item}: 0x1F prints the name of the item whose id is the script local $7F:0200 (C2/5B9B) -- RAM too, so the same
    # marker; the field engine reads the ROM's name table at render time.
    if namesub and v == 0x1F:
        return '\x01' + chr(v), 0
    t = _NAMES.get(v, '')
    return ((t % ind) if '%s' in t else t), 0

def _decode_tokens(rom, o, subs, length=None, pb='\n———\n', namesub=False, ind='   '):
    """Decode a token stream. If `length` is None, run to a NUL terminator (dialogue);
    otherwise iterate exactly `length` tokens (dictionary substrings, no nested refs).
    `pb` is what the page-break codes 0x0B/0x0C render to (default the readable '———'
    separator; the asset packer passes '\\f' so the field engine can break pages there).
    `namesub`=True makes name codes 0x13-0x20 emit a render-time marker (see `special`)."""
    out = []
    count = 0
    while True:
        if length is not None and count >= length:
            break
        count += 1
        v = rom[o]; o += 1
        if v == 0:
            break
        elif v == 1 or v == 2:
            out.append(parse_character(rom[o])); o += 1
        elif v == 3:
            delay = rom[o]; o += 1
            if delay == 0:
                break
            o += 1                             # tick byte; render nothing
        elif v == 0x0B:                        # wait-for-input + clear: a hard page break
            out.append(pb)
        elif v in (0x09, 0x0A) and pb == '\f': # the packer: the page break that turns ITSELF (C0/2181 -> 21D8 leaves
            out.append('\v' + (ind if v == 0x0A else ''))   # $0134 at 0, state 7 advances with no press) -- field.c
                                               # TB_AUTOPG. Readable dumps keep them as line breaks.
        elif v == 0x0C:
            out.append(pb + ind)              # page break + the token's OWN indent
        elif length is None and 0x21 <= v <= 0x9F:
            out.append(subs[v - 0x21])         # dictionary word (top level only)
        elif (length is None and v > 0x9F) or (length is not None and v >= 0x9F):
            # the packer keeps the CLOSING quote 0xE2 apart from the opening 0xE1 (two glyphs in the ROM's font; the
            # field engine maps U+201D back to 0xE2) -- readable dumps print both as '"'
            out.append('”' if (namesub and v == 0xE2) else parse_character(v))
        else:
            txt, extra = special(v, rom, o, namesub, ind); out.append(txt); o += extra
    return ''.join(out), o

def load_dict(rom):
    subs = []
    for i in range(DICT_COUNT):
        p = (rom[DICT_BASE + i * 2] | (rom[DICT_BASE + i * 2 + 1] << 8)) - 0xFA00
        o = DICT_BASE + p
        ln = rom[o]
        subs.append(_decode_tokens(rom, o + 1, [], ln)[0])
    return subs

def decode_string(rom, off, subs, pb='\n———\n', namesub=False, ind='   '):
    return _decode_tokens(rom, off, subs, None, pb, namesub, ind)[0]

# ---- mapped strings (item names): one byte -> one FONT_8_MAP entry ----
def _font8():
    m = [''] * 256
    icons = ['<BLADE>', '<BOW>', '<GUN>', '<ARM>', '<SWORD>', '<FIST>', '<SCYTHE>', '<HELM>',
             '<ARMOR>', '<RING>', '<H>', '<M>', '<P>', '<:>', '<SHIELD>', '<STAR>']
    for i, s in enumerate(icons):
        m[0x20 + i] = s
    for i, s in enumerate(['<LEFT>', '<RIGHT>', '(', ')', ':']):
        m[0x5B + i] = s
    hands = ['<HAND1>', '<HAND2>', '<HAND3>', '<HAND4>', '<H>', '<M>', '<P>']
    for i, s in enumerate(hands):
        m[0x60 + i] = s
    for i in range(9):
        m[0x67 + i] = '<HP%d>' % i
    m[0x72] = '°'
    for i in range(10):
        m[0x73 + i] = chr(0x30 + i)
    m[0x7D], m[0x7E], m[0x7F] = '<D>', '<Z>', '<UP>'
    for v in range(0xA0, 0xF4):               # letters/digits/punct = parse_character
        m[v] = parse_character(v)
    m[0xFF] = ' '
    return m

FONT_8_MAP = _font8()

def decode_mapped(data):
    return ''.join(FONT_8_MAP[b] for b in data).rstrip()

def item_names(rom):
    data = rom[0xC0B5E:0xC0B5E + 0xA71]
    return [decode_mapped(data[i * 11:i * 11 + 11]) for i in range(len(data) // 11)]

def world_names(rom, subs):
    return read_string_list(rom, 0x6F4D4, 6, subs)

def location_names(rom, subs):
    return read_string_list(rom, 0x6F400, 112, subs)

def read_string_list(rom, ptr_start, count, subs):
    seg = ptr_start & 0xFF0000
    out = []
    for i in range(count):
        ptr = rom[ptr_start + i * 2] | (rom[ptr_start + i * 2 + 1] << 8)
        out.append(decode_string(rom, seg + ptr, subs))
    return out

def read_textbox_table(rom, addr, subs, max_strings=256, max_len=512, pb='\n———\n', namesub=False, ind='   '):
    """Decode a dialogue string table: up to 256 page-relative u16 pointers, stopping
    when consecutive pointers stop looking like valid string bounds (CTViewer heuristic).
    ⚠ NOT the ROM's read: the bound test needs a NEXT pointer, so the table's LAST string always fails it (the word
    after the array is string 0's text). Readable dumps and the packer's B8 filter only -- the packed strings are
    read_dialogue_table's."""
    page = addr & 0xFF0000
    out = []
    for i in range(max_strings):
        ptr = rom[addr + i * 2] | (rom[addr + i * 2 + 1] << 8)
        nxt = rom[addr + i * 2 + 2] | (rom[addr + i * 2 + 3] << 8)
        if nxt < ptr or nxt - ptr > max_len:
            break
        out.append(decode_string(rom, page + ptr, subs, pb, namesub, ind))
    return out

def read_dialogue_table(rom, addr, subs, pb='\n———\n', namesub=False, ind='   '):
    """A dialogue string table AS THE ROM READS IT. The box machine hands the text engine the op's string byte and the
    table (C0/20F2: $020C = $2A, $020D-F = $2B-D) and C2/57E7 fetches the string with
        LDA $0C / AND #00FF / ASL / TAY / LDA [$0D],Y / STA $31      ; $33 = $0F, the table's own bank
    -- string i is the u16 at table + 2*i, in the table's bank. The ROM keeps no count and tests no bound, and the index
    is a byte, so a table answers all 256 indices. The old CTViewer bound (read_textbox_table) dropped every table's
    LAST string, and 16 script boxes ask for exactly that one (loc 010's obj9 `BB 26`: an empty window that never
    typed, so never reached state 7 and never took the press)."""
    page = addr & 0xFF0000
    return [decode_string(rom, page + (rom[addr + i * 2] | (rom[addr + i * 2 + 1] << 8)), subs, pb, namesub, ind)
            for i in range(256)]

def scan_dialogue_tables(rom, subs, pb='\n———\n', namesub=False, ind='   '):
    """Find dialogue tables by scanning decompressed scene scripts for op 0xB8 (set
    string table) = a 24-bit SNES pointer. Returns {file_addr: [strings]}."""
    sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
    from lz import lz_decompress
    from sprites import get_entries_3byte
    script_ptrs = get_entries_3byte(rom, 0x3CF9F0, 513)
    tables = {}
    for sp in script_ptrs:
        if sp == 0 or sp >= len(rom):
            continue
        try:
            scr = lz_decompress(rom, sp)
        except Exception:
            continue
        # Scan for op 0xB8 followed by a plausible 24-bit dialogue-table pointer.
        for i in range(len(scr) - 3):
            if scr[i] != 0xB8:
                continue
            snes = scr[i + 1] | (scr[i + 2] << 8) | (scr[i + 3] << 16)
            if snes < 0xC00000 or snes > 0xFFFFFF:
                continue
            addr = snes - 0xC00000
            if addr in tables or addr + 512 >= len(rom):
                continue
            strs = read_textbox_table(rom, addr, subs, pb=pb, namesub=namesub, ind=ind)
            if len(strs) >= 4:                # require a few valid strings to accept
                tables[addr] = read_dialogue_table(rom, addr, subs, pb=pb, namesub=namesub, ind=ind)
    return tables

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    rom = open(sys.argv[1], 'rb').read()
    mode = sys.argv[2]
    subs = load_dict(rom)
    if mode == 'dict':
        for i, s in enumerate(subs):
            print("%3d: %r" % (i, s))
    elif mode == 'items':
        data = rom[0xC0B5E:0xC0B5E + 0xA71]
        for i in range(len(data) // 11):
            print("%3d: %s" % (i, decode_mapped(data[i * 11:i * 11 + 11])))
    elif mode == 'world':
        names = read_string_list(rom, 0x6F4D4, 6, subs)
        exits = read_string_list(rom, 0x6F400, 112, subs)
        print("== world names =="); [print("%3d: %r" % (i, s)) for i, s in enumerate(names)]
        print("== exit names ==");  [print("%3d: %r" % (i, s)) for i, s in enumerate(exits)]
    elif mode == 'table':
        addr = int(sys.argv[3], 16)
        n = int(sys.argv[4]) if len(sys.argv) > 4 else 256
        for i, s in enumerate(read_textbox_table(rom, addr, subs, n)):
            print("%3d: %s" % (i, s.replace('\n', '\\n')))
    elif mode == 'dialogue':
        tables = scan_dialogue_tables(rom, subs)
        total = sum(len(v) for v in tables.values())
        out = sys.argv[3] if len(sys.argv) > 3 else None
        lines = []
        for addr in sorted(tables):
            lines.append("=== table @0x%X (%d strings) ===" % (addr, len(tables[addr])))
            for i, s in enumerate(tables[addr]):
                lines.append("[%d] %s" % (i, s))
        text = '\n'.join(lines)
        if out:
            open(out, 'w', encoding='utf-8').write(text)
            print("%d tables, %d strings -> '%s'" % (len(tables), total, out))
        else:
            print(text[:4000])
            print("\n... %d tables, %d strings total" % (len(tables), total))
    else:
        print("unknown mode:", mode); sys.exit(1)
