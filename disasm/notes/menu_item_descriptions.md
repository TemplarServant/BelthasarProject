# The item/equipment DESCRIPTIONS are stored strings, and the port was writing its own

The menu built these lines out of the effect record and the equipment bonus mask, with a source
comment claiming the ROM has no stored string for them. It has one. This note records where, how
the entry count was read out of the data rather than assumed, and exactly what the synthesiser got
wrong.

---

## 1. WHERE THE ROM KEEPS THEM — C2/F2F3

```
2F2F3  PHP / SEP #20 / REP #10
2F2F8  BIT $0D78 / BMI ->2F302        ; a dirty-flag cache: skip if the id is unchanged
2F2FD  CMP $0D77 / BEQ ->2F330
2F302  STA $0D77 / STZ $0D78
2F308  STA $020C                      ; <- THE ITEM ID
2F30B  LDX #4E84 / STX $0DC5
2F311  LDX #6840 / STX $0DCC
2F317  LDX #2EB1 / STX $020D          ; \
2F31D  LDA #CC   / STA $020F          ;  > the 24-bit source pointer $CC:2EB1
2F322  LDA #01   / STA $0DC9
2F327  LDX #0080 / STX $0DD0
2F32D  JSR C2/FA49                    ; render
2F330  PLP / RTS
```

`$CC:2EB1` is a table of **16-bit local pointers into bank $CC, indexed by item id**.

⚠ The routine only decodes correctly with **M=8 / X=16** (`SEP #20 / REP #10` at 2F2F4). Read with
8-bit index registers it desyncs into `BRK`/`COP` nonsense two instructions in, which is how the
`LDX #2EB1` — the whole point — can be missed.

### The entry count is READ OUT OF THE DATA

The smallest pointer in the table is `$3095`, and

```
0x2EB1 + 242*2 == 0x3095
```

exactly — the strings begin immediately where the table ends. So the table is **242 entries**
(ids `$00..$F1`, which is also the item id space), and no assumption is needed. 105 of the 242 are
non-empty; an empty entry is the ROM's answer, not a gap, which is why the Pendant's description
box is blank. Decoded with the repo's own dictionary decoder (`assets/text.py`) and baked by
`tools/_gen_menudesc.py` into `src/data/menu_desc.h`.

> The baker is UNTRACKED -- `.gitignore` line 95 is `tools/_*`, the repo's convention for the
> `_gen_*` family (`_gen_menutech.py`, `_gen_menuromdata.py`, ... are all untracked too; the baked
> header is what ships). The whole recipe is therefore written down here and in the header:
> `ptr = ROM[0x0C2EB1 + id*2]` (16-bit LE) -> `assets/text.py decode_string(ROM, 0x0C0000 + ptr,
> load_dict(ROM))`, for `id` in `0..241`, and assert `min(ptr) == 0x2EB1 + 242*2` before trusting it.

---

## 2. WHAT THE SYNTHESISER GOT WRONG

`mItemDesc` produced `"Restores %d HP"` / `"<Stat> +1"` / `"Restores all ..."` from the baked effect
record, and `CtMenuEquipDesc` produced `"<Stat> +N"` from the bonus mask with the vocabulary
`Power / Stamina / Speed / Magic / Hit / Evade / M.Def`. Against the ROM:

| id | ROM | the synthesiser |
|---|---|---|
| `BD` Tonic | `Restores 50 HP` | same — the one it matched |
| `C6` | `Restores Status` | nothing (no effect-kind for it) |
| `C7` | `Revives an ally` | nothing |
| `C8` | `Recovers HP/MP at Save Pts.` | nothing |
| `C9` | `Recovers from "?"` | nothing |
| `CB` | `Cuts magic hits by 1/3` | nothing |
| `C3`/`C4`/`C5` | `Restores HP/MP` / `Restores MP` / `Restores HP/MP for all` | `Restores all HP/MP` |
| `CD`/`CE`/`CF` | `"Power" +1` — **with quotes** | `Power +1` |
| `96` | `Strike +2` | `Hit +2` |
| `98` | `Vigor +2` | `Power +2` |
| `B6`/`B7` | *(empty)* | fabricated text |
| `74` | `Absorbs "Fire" hits` (35 chars is the table's longest) | nothing, and would have been TRUNCATED |

The two callers held `char[24]` buffers, sized for the synthesised lines; both are now `char[40]`
(35 + NUL).

⚠ The old comment credited **C2/B0AB** as the generator. C2/B0AB is real but it is the EFFECT
staging — `LDA $04C9 / JSR C2/8881 / LDX $04CE / LDA $CC1BB5,X ... LDA $CC0001,X / ORA $0DBD` — it
reads the item RECORD ($CC:0000 stride 11) and builds `$0DBD`. It writes no text.

The bonus mask is still the right source for the EFFECT (the Equip screen's stat deltas read it).
It was only ever wrong as a source of TEXT.

---

## 3. HOW IT IS GRADED — and why a new check was unavoidable

**Swapping the whole thing from fabricated text to real text turned NO existing check red.** The
item and equip checks pin trace lines, MP caps and SFX posts; not one of them renders a description.
That is the exact shape of an ungraded change, so `menu-item-descriptions-from-rom` was added:
`Belthasar --desctest` calls **`CtMenuEquipDesc` itself** (not the table) for eleven ids chosen to
span every arm above, and prints the table's shape as an anti-vacuity line
(`table 242 entries, 105 non-empty`).

Mutation: returning a fixed `"Power +1"` from `CtMenuEquipDesc` takes the check red.

Cross-check on the decoder: entry `BD` decodes to `Restores 50 HP`, which is the string a previous
session confirmed against the ORACLE for Tonic — so the dictionary decode agrees with the running
game on at least one known entry.

---

## 4. OPEN

* `C2/FA49`, the renderer C2/F2F3 hands the pointer to, is not ported — the port draws the decoded
  ASCII with its own `mText`. Line breaking and the VWF are therefore ours, not the ROM's, for any
  string long enough to wrap.
* The dirty-flag cache (`$0D77`/`$0D78`) and the four staging words `$0DC5`, `$0DCC`, `$0DC9`,
  `$0DD0` that C2/F2F3 sets are not modelled; nothing in the port reads them yet.

## 5. The CLOSING quote (2026-09-25)

The table holds two quote codes: 0xE1 opens, 0xE2 closes (`"Power" +1` = `E1 AF 62 26 E2 EF EC D5`), two different
glyphs in the ROM's font. 32 of the 105 lines close a quote. The first bake decoded both to ASCII `"`, so the VWF drew
the OPENING glyph at both ends -- the Item screen's line for a Tab differed from the cartridge by the closing glyph's
shape and advance (tools/menu_audit_cmp.py itemtab, f117-f252). tools/_gen_menudesc.py now decodes with
`namesub=True` (text.py keeps 0xE2 as U+201D there; no description holds a name or number code -- asserted) and bakes it
as the raw byte 0xE2, which the VWF's `cGlyph` renders as the font's own glyph (codes 0xA0-0xF3). `--desctest` prints a
raw code as `{E2}`, so menu-item-descriptions-from-rom grades it.
