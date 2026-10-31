#!/usr/bin/env python3
r"""Regenerate src/data/seq_credits.h -- the data sequence-table entry 1 (the staff credits,
$7E:6B04) runs on.  disasm/notes/sequence_table.md section 6.

Entry 1 is not a picture.  The port runs its setup and its three tasks, and builds the BG1
tilemap and the OAM the way $7E:6C4D and $7E:6C71 do -- by streaming the page's own text out
of the overlay through the WMDATA port.  So only what cannot be computed is generated here:

  * the fonts.  The boot decompresses a 512-byte packet (ROM index $AE, via $7E:3691 -> C3/0557)
    into VRAM words $0000..$00FF -- 32 tiles read as 2bpp in BG mode 0 -- and $7E:66B0 expands a
    2bpp source at $7F:0300 into VRAM words $0100..$045F, padding each tile's upper two planes
    with zeroes so the sprites can use the same glyphs at 4bpp.  Captured from the oracle's
    VRAM; the script refuses a capture that does not match the derivation.
  * the overlay's own data: the five pages' text streams ($7E:6CC0..$6E41) and the two pointer
    tables ($7E:6CAC BG, $7E:6CB6 OAM), read out of the decompressed overlay image.

Not here: the tilemap, the OAM, the palette and the timing.  The four greys are
immediates in $7E:6BF9; the tilemap and OAM are what the two stream decoders write; the page
counter, the 15-frame fade in, the 90-frame hold and the 15-frame fade out are the machine's.

Run from the repo root; needs the ROM + build/ct.exe.
"""
import os
import subprocess
import sys

ROM = sys.argv[1] if len(sys.argv) > 1 else 'Chrono_Trigger_USA_SNES.sfc'
EXE = os.path.abspath('build/ct.exe')
OUT = 'src/data/seq_credits.h'
TMP = 'build/_seqcred'
NL = chr(10)

# The two capture frames.  A plain boot plays the title to its end and falls into the attract; boot
# frame 2840 is 82 frames into the credits' first page, and 1700 frames further on is 68 frames
# into its second.  Two different pages show that the fonts are a boot asset and not something
# the page builds.
BOOT_FRAMES = 2840
PAGE2_AFTER = 1700

TEXT_BASE = 0x6CC0        # $7E:6CC0 -- page 1's BG stream, the first byte of the block
TEXT_END = 0x6E42         # one past page 5's OAM terminator
BG_TABLE = 0x6CAC         # $7E:6CAC -- five words, the pages' BG streams
OAM_TABLE = 0x6CB6        # $7E:6CB6 -- five words, the pages' OAM streams
FONT_WORDS = 0x400        # VRAM $0000..$03FF: the 2bpp BG font and the 4bpp OBJ font


def run(args, **kw):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL, **kw)


def die(msg):
    sys.exit('gen_seq_credits: REFUSING -- ' + msg)


os.makedirs('build', exist_ok=True)

# ---- the captures ------------------------------------------------------------------------------
run([EXE, ROM, '--fb', TMP + '_a.raw', '--frames', str(BOOT_FRAMES)],
    env=dict(os.environ, PEEK_SAVE=TMP + '_a.state'))
run([EXE, ROM, '--peek', TMP + '_a.state', TMP + '_p1.raw', '--btnscript', '0:1'])
run([EXE, ROM, '--peek', TMP + '_a.state', TMP + '_p2.raw', '--btnscript', '0:%d' % PAGE2_AFTER])

wram = open(TMP + '_p1.raw.wram', 'rb').read()
wram2 = open(TMP + '_p2.raw.wram', 'rb').read()
vram = open(TMP + '_p1.raw.vram', 'rb').read()
vram2 = open(TMP + '_p2.raw.vram', 'rb').read()


def w16(buf, a):
    return buf[a] | (buf[a + 1] << 8)


# ---- the refusals ------------------------------------------------------------------------------
# Both captures must be inside the credits, on two different pages, with the page timer
# still installed -- otherwise the VRAM below is some other stage's.
for tag, buf, want in (('capture A', wram, 1), ('capture B', wram2, 2)):
    if buf[0x384] != 0x61:
        die('%s: $0384 = %02X, not $61 (sequence-table entry 1)' % (tag, buf[0x384]))
    if buf[0x382] != want:
        die('%s: $0382 = %d, expected page %d' % (tag, buf[0x382], want))
    if w16(buf, 0x922) != 0x6BE5:
        die('%s: the page timer $7E:6BE5 is not in the task list ($0922 = %04X)'
            % (tag, w16(buf, 0x922)))

font = vram[:FONT_WORDS * 2]
if font != vram2[:FONT_WORDS * 2]:
    die('the font words $0000..$%03X differ between page 1 and page 2 -- they are not a boot asset'
        % (FONT_WORDS - 1))

# $7E:66B0 writes eight words of 2bpp data and then eight zero words per tile, so every tile from
# $10 up must have empty upper planes.  If one does not, the expansion is not what $66B0 does.
for tile in range(0x10, 0x40):
    if any(font[tile * 32 + 16 + k] for k in range(16)):
        die('OBJ font tile $%02X has non-empty planes 2/3 -- $7E:66B0 pads them with zeroes' % tile)
if not any(font[:0x200]):
    die('the BG font (VRAM words $0000..$00FF) is empty -- packet $AE did not land')

# The text block and the tables have to agree: every pointer must land inside the block, and the
# streams must tile it end to end with no gap and no overlap.
text = wram[TEXT_BASE:TEXT_END]
bgp = [w16(wram, BG_TABLE + 2 * i) for i in range(5)]
oamp = [w16(wram, OAM_TABLE + 2 * i) for i in range(5)]
for p in bgp + oamp:
    if not (TEXT_BASE <= p < TEXT_END):
        die('a page pointer ($%04X) falls outside the text block $%04X..$%04X'
            % (p, TEXT_BASE, TEXT_END - 1))


def walk(a, pairs):
    """Step one stream the way its decoder does; return the address just past its $FF."""
    while True:
        if wram[a] == 0xFF:
            return a + 1
        a += pairs                       # the record's two header bytes
        while wram[a]:                   # the string, NUL-terminated
            a += 1
        a += 1


cursor = TEXT_BASE
for i in range(5):
    if bgp[i] != cursor:
        die('page %d BG stream starts at $%04X, but the previous stream ended at $%04X'
            % (i + 1, bgp[i], cursor))
    cursor = walk(bgp[i], 2)
    if oamp[i] != cursor:
        die('page %d OAM stream starts at $%04X, but its BG stream ended at $%04X'
            % (i + 1, oamp[i], cursor))
    cursor = walk(oamp[i], 2)
if cursor != TEXT_END:
    die('the five pages end at $%04X, not $%04X' % (cursor, TEXT_END))

# ---- emit --------------------------------------------------------------------------------------
def rows(vals, per, fmt):
    out = []
    for i in range(0, len(vals), per):
        out.append('  ' + ' '.join(fmt % v for v in vals[i:i + per]))
    return NL.join(out)


fontw = [font[2 * i] | (font[2 * i + 1] << 8) for i in range(FONT_WORDS)]

h = []
h.append('// GENERATED by tools/gen/seq_credits.py -- do not edit.')
h.append('// Sequence-table ENTRY 1, the staff credits ($7E:6B04). disasm/notes/sequence_table.md.')
h.append('//')
h.append('// Only two things here: the FONTS the boot built (VRAM words $0000..$%03X -- the BG half'
         % (FONT_WORDS - 1))
h.append('// is read as 2bpp in mode 0, the OBJ half as 4bpp with zeroed upper planes) and the')
h.append('// overlay\'s own TEXT STREAMS. The tilemap, the OAM, the palette and every frame count')
h.append('// are the machine\'s, and src/title_intro.c runs them.')
h.append('#ifndef CT_DATA_SEQ_CREDITS_H')
h.append('#define CT_DATA_SEQ_CREDITS_H')
h.append('')
h.append('#define CT_CRED_FONT_WORDS 0x%03X' % FONT_WORDS)
h.append('#define CT_CRED_TEXT_BASE  0x%04X' % TEXT_BASE)
h.append('#define CT_CRED_PAGES      5')
h.append('')
h.append('static const unsigned short ctCredFont[%d] = {' % FONT_WORDS)
h.append(rows(fontw, 12, '0x%04X,'))
h.append('};')
h.append('')
h.append('// $7E:6CAC and $7E:6CB6: where each page\'s BG and OAM stream begins.')
h.append('static const unsigned short ctCredBgPtr[5]  = { %s };'
         % ' '.join('0x%04X,' % p for p in bgp))
h.append('static const unsigned short ctCredOamPtr[5] = { %s };'
         % ' '.join('0x%04X,' % p for p in oamp))
h.append('')
h.append('// $7E:%04X..$%04X. A BG record is <vword lo> <vword hi> then a NUL-terminated string;'
         % (TEXT_BASE, TEXT_END - 1))
h.append('// an OAM record is <x> <y> then the string. $FF ends a stream.')
h.append('static const unsigned char ctCredText[%d] = {' % len(text))
h.append(rows(list(text), 16, '0x%02X,'))
h.append('};')
h.append('')
h.append('#endif')
h.append('')

open(OUT, 'w', newline=NL).write(NL.join(h))
print('gen_seq_credits: %s -- %d font words, %d text bytes, 5 pages'
      % (OUT, FONT_WORDS, len(text)))
