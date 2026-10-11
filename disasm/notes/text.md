# Text / dialogue (SNES/USA)

Reverse-engineered from CTViewer (`text_decoder.rs`), verified against the ROM.
Implemented in [`assets/text.py`](../../assets/text.py). Two encodings:

## Tokenized / dictionary ("huffman") — dialogue, world/exit names, textbox tables
A byte stream decoded token by token (`decode_string` / `_decode_tokens`):
- `0x00` — end of string.
- `0x01`, `0x02` — the next byte is a literal character (`parse_character`).
- `0x03` — wait: read a delay byte (`0` ends the string), else read one more tick byte.
- `0x04`–`0x20` — **control codes**: `0x05`=line break, `0x06`=break+indent,
  `0x09`/`0x0A`=auto page, `0x0B`/`0x0C`=page (wait), `0x0D`–`0x0F`=number,
  `0x12`=tech name (consumes 1 byte), `0x13`–`0x20`=name substitutions
  (`Crono Marle Lucca Robo Frog Ayla Magus`, party `{pt1..3}`, `Leene`, `{item}`, `Epoch`).
- `0x21`–`0x9F` — **dictionary** word: `substrings[v-0x21]`.
- `0xA0`–`0xFF` — `parse_character`: `0xA0-0xB9`=`A-Z`, `0xBA-0xD3`=`a-z`,
  `0xD4-0xDD`=`0-9`, `0xDE-0xF3`=punctuation (`! ? / " " : & ( ) ' . , = - + % ♫ ♥ … ∞ #`).

### Dictionary / substring table
`0x1EFA00`..`0x1EFF00` (0x500 bytes), **128 entries**. The table starts with 128
little-endian `u16` pointers; each minus `0xFA00` = offset (relative to `0x1EFA00`)
to a substring `[len byte][len tokens]`. A substring is decoded like a string but
has no nested dictionary refs. Decoded values are the common CT word fragments
(`the`, `th`, `he`, `you`, `ing`, `here`, …).

### Dialogue string tables
There is no single global table — each scene's script sets one with op `0xB8`
(a 24-bit SNES pointer → file `addr - 0xC00000`). A table is up to 256 page-relative
`u16` pointers (string data at `(addr & 0xFF0000) + ptr`); CTViewer stops when two
consecutive pointers stop bounding a `<512`-byte string. `text.py dialogue` finds
these by decompressing the 513 scene scripts (`get_entries 0x3CF9F0`) and scanning
for op `0xB8` — a heuristic (a few false-positive tables are possible), but it pulls
out ~5000 real strings (e.g. the Cyrus/Glenn backstory at `0x18D000`).

World names `0x6F4D4` (6), exit/location names `0x6F400` (112), via page-relative
`u16` pointers (`read_string_list`).

## Mapped — item names (fixed-size records, no control flow)
One byte → one `FONT_8_MAP[256]` entry (`decode_mapped`). Item names: `0xC0B5E`,
length `0xA71`, 11-byte records (247 items) — `Wood Sword`, `Iron Blade`, … with a
leading item-type icon tag (`<BLADE>` etc.).

## CLI
```
python assets/text.py <rom> dict                 # 128 dictionary words
python assets/text.py <rom> items                # item names
python assets/text.py <rom> world                # world + location names
python assets/text.py <rom> table <hexaddr> [n]  # decode one dialogue table
python assets/text.py <rom> dialogue [out.txt]   # all dialogue (script scan)
```
The dialogue dump is ROM-derived (copyrighted) and git-ignored.

## Packed into ct_assets.dat
`extract_assets.py` packs the decoded text as string-list blobs (`[count u16][
{u16 len, UTF-8} ...]`): `CT_ASSET_STRINGS` id 0=dictionary, 1=items, 2=world,
3=locations; `CT_ASSET_DIALOGUE` keyed by table file address. The C engine reads
them with `CtAssetString(type, id, index)` — `ct.exe --textdump ct_assets.dat`
prints them back with no ROM (validated: item/world/location names + the Cyrus
dialogue at `0x18D000`).

## TODO
- Parse the scene-script opcode lengths to find op `0xB8` precisely (drop the
  scan false-positives); reproduce the textbox layout (line wrap / pages).
