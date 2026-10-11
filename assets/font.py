#!/usr/bin/env python3
"""Render Chrono Trigger dialogue text with the real in-ROM 12px font.

The dialogue font glyphs live at ROM 0x3F2F60 as SNES 2bpp planar tiles, 8px wide x
12 rows tall, 24 bytes/glyph; glyph_index = dialogue_byte - 0xA0 (so 0xA0 = 'A').
text.py decodes a dialogue stream to a Unicode string; this module maps each character
back to its glyph (inverting text.parse_character) and blits it into an RGB frame with
the CT light-on-dark colour + a 1px drop shadow. The textbox metrics match CTViewer
(line height 16, light text [231,231,231]).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import text

FONT12 = 0x3F2F60
LINE_H = 16
SPACE_W = 5
# 2bpp glyph colour -> RGB: 1/3 = letter body (light), 2 = built-in shadow (dark)
BODY = (248, 248, 248)
SHADE = (64, 72, 120)
DROP = (24, 24, 56)                      # faked SNES drop shadow (1px right/down)


def _char2glyph():
    m = {}
    for b in range(0xA0, 0xF4):
        ch = text.parse_character(b)
        if ch and ch != '�' and ch not in m:
            m[ch] = b - 0xA0
    return m

CHAR2GLYPH = _char2glyph()


def glyph_rows(rom, gi):
    """Decode glyph `gi` to a 12x8 array of 2bpp colours (0=transparent)."""
    o = FONT12 + gi * 24
    return [[((rom[o + y * 2] >> (7 - x)) & 1) | (((rom[o + y * 2 + 1] >> (7 - x)) & 1) << 1)
             for x in range(8)] for y in range(12)]


def glyph_width(rows):
    """Inked width (rightmost lit column + 1) for proportional advance."""
    w = 0
    for row in rows:
        for x in range(7, -1, -1):
            if row[x]:
                w = max(w, x + 1); break
    return w


def _px(frame, W, H, x, y, rgb):
    if 0 <= x < W and 0 <= y < H:
        o = (y * W + x) * 3
        frame[o], frame[o + 1], frame[o + 2] = rgb


def draw_text(rom, frame, W, H, s, x0, y0, max_w, max_lines=3):
    """Render string `s` (with '\\n' line breaks + word wrap) into the RGB `frame` at
    (x0, y0), wrapped to `max_w` px, up to `max_lines`. Returns the number of lines drawn."""
    x, y, line = x0, y0, 0
    for word in _tokens(s):
        if word == '\n':
            x = x0; y += LINE_H; line += 1
            if line >= max_lines:
                break
            continue
        w = _word_width(rom, word)
        if x > x0 and x - x0 + w > max_w:             # wrap
            x = x0; y += LINE_H; line += 1
            if line >= max_lines:
                break
        for ch in word:
            x += _draw_glyph(rom, frame, W, H, ch, x, y)
        x += SPACE_W                                  # space after the word
    return line + 1


def _tokens(s):
    """Split into words and explicit newlines, preserving the '\\n' markers."""
    out = []
    for ln in s.split('\n'):
        out.extend(ln.split(' '))
        out.append('\n')
    if out and out[-1] == '\n':
        out.pop()
    return out


def _word_width(rom, word):
    w = 0
    for ch in word:
        gi = CHAR2GLYPH.get(ch)
        if gi is None:
            w += SPACE_W
        else:
            w += glyph_width(glyph_rows(rom, gi)) + 1
    return w


def _draw_glyph(rom, frame, W, H, ch, x, y):
    gi = CHAR2GLYPH.get(ch)
    if gi is None:
        return SPACE_W
    rows = glyph_rows(rom, gi)
    for ry in range(12):
        for rx in range(8):
            c = rows[ry][rx]
            if c == 0:
                continue
            _px(frame, W, H, x + rx + 1, y + ry + 1, DROP)     # drop shadow first
    for ry in range(12):
        for rx in range(8):
            c = rows[ry][rx]
            if c == 0:
                continue
            _px(frame, W, H, x + rx, y + ry, SHADE if c == 2 else BODY)
    return glyph_width(rows) + 1
