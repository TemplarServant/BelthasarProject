# BATTLE ITEMS — the use is a bank-CE ANIM SCRIPT, and a RESTORE number is GREEN

User report (translated from Portuguese): *"the animation when using items, the sounds, the HP colour is not right"* — the animation
when using items, the sounds, and the HP colour are all wrong.

Three separate mechanisms, all derived from the ROM + the oracle. Companion to `battle_item_slice.md`
(which pinned the item's WRAM effects and spans and explicitly left the *presentation* as a boundary),
`battle_items_nonhp.md`, `battle_sfx_ops_inputs.md` and `battle_actionfx.md`.

---

## A. THE ANIMATION — an item use is a multi-track bank-CE script, exactly like a tech

`C1/475B` (`tools/dis.py 0x01475B 0xB0 1 0`), entered with `$AE93` = the item id:

```
1475B  SEC / LDA $AE93 / SBC #BC              ; sub = id - 0xBC
14765  ASL A ×3 / SEC / SBC $80 / TAX         ; X = sub*7
1476F  LDA $CD5C26,X / STA $9877              ; the anim SELECTOR   (the port already did this bit)
14776..1479D   +1 -> $987A  +2 -> $987B  +3 -> $987E  +4 -> $9881  +5 -> $9884  +6 -> $987C
147A2  LDA $987E / ASL / TAX / LDA $D10060,X / STA $987F      ; tile/graphics pointer (bank D1)
147AE  LDA $9881 / ASL / TAX / LDA $CD6C2C,X / STA $9882
147BA  LDA $9884 / ASL / TAX / LDA $CD656E,X / STA $9885
147C6  LDA $9877 / ASL / TAX / LDA $CD63F0,X / STA $9878       ; ** THE SCRIPT POINTER **
147D7  LDA $CE0000,X …                                          ; ** header read from BANK CE **
```

`CD/63F0[$9877*2]`, and the bank is CE — the same two instructions as the PC tech path (`C1/4617 LDA
$CD5EF0,X` + `C1/4623 LDA $CE0000,X`) one table over. Dumped:

`[00]=8294 [01]=829C [02]=82FB [03]=8340 [04]=8385 [05]=83D0 [06]=8409 [07]=844E [08]=84C8 [09]=8547
[0A]=867D [0B]=8729 [0E]=876A [0F]=8817 [10]=8843`, everything else `8294`.

Walked with the ROM's own opcode lengths (dispatch `C1/7A6B`, tail `A9 len 4C BB 75`), e.g. **Tonic
`CE/829C`, header mask `90C0` → tracks 0, 3, 8, 9**:

```
t0 (ACTOR)  CE/82AA: 72 15 | 03 15 | 24 02 | 78 06 | 50 | 06 03 | 2E | 01
t3 (TARGET) CE/82B8: 20 05 | 03 24 | 03 24 | 02 24 | 36 | 06 03 | 00
t8 (fx obj) CE/82C4: 0C | 1B 0C | 72 03 | 73 03 | 02 00 | 60 00 | 70 | 78 04 | C0 0C 1C A0 01 | 20 02 | 12 0C | 71 | 36 | 00
t9 (fx obj) CE/82DE: 0C | 1B 0C | 72 03 | 73 03 | 02 00 | 70 | C0 0C 9C E0 01 | 20 02 | 12 0C | 71 | 00
```

Elixir `CE/844E` (mask `90F0` → tracks 0,3,8,9,10,11) carries **two** ShowDamage ops (`50 | 51`) and an
op-0x79 sound on track 10; Heal `CE/867D` (mask `90FC`, 8 tracks) has **no** ShowDamage and six staggered
sparkle objects; Revive `CE/8729` (mask `9080`) fires op **0x7A**.

**Roles** are the usual `$A014` binding (oracle `--monitor "7EA014:16" --writer` → `f457 = 00 00 00 01 FF…`,
writer CC:F1A7): track 0 = the ACTOR, track 3 = the TARGET, ≥8 = scratch effect objects.

**The span is TARGET-DEPENDENT, not a per-item constant** (measured `--monitor "7EA013:1" --diff`): Tonic
on Crono 166 frames, on Marle 142; Elixir 225 / 201; Heal 125; Revive 218. Tracks 8/9 do `1B 0C`
(teleport-to-target) → `C0 …` → `12 0C` (slide-to-target) → `36`, and track 0 is parked on `24 02` until
that sync, so the tail of the action moves with the actor↔target distance. ~~Our port keeps the pinned
per-item span~~ — the port closes an item on its script's op 01 (d45cff6; battle_asc_inert_ops.md §5): on the
logical oracle Tonic 165 on Crono / 141 on Marle, Ether 141, Elixir 200, Heal 124, Revive 217 (the real-time
numbers above carry a lag frame), every one the port's too (tools/battle_ab.py item, 2026-09-30).

## B. THE SOUNDS — they are the script's own op 0x78 / 0x79 / 0x7A

Handlers (`tools/dis.py 0x01619E 0x60 1 0`):

```
C1/619E (ops 78/79): INY / LDA [$E6],Y / STA $1E01
                     SEC / LDA $9895 / SBC #78 / CLC / ADC #18 / STA $1E00     ; cmd = op-0x78+0x18
                     LDA #80 / STA $1E02 / JSL C7/0004 / …                      ; pan = 0x80 (centre)
C1/61C1 (ops 7A/7B): INY / LDA [$E6],Y / STA $1E01
                     INY / LDA [$E6],Y / JSR C1/762E / LDA $A2B0 / STA $1E02   ; pan = the RESOLVED target's X
                     SEC / LDA $9895 / SBC #7A / CLC / ADC #18 / STA $1E00 / …
```

ORACLE (`--monitor "7E1E01:1,7E1E02:1" --diff --writer`; writer C1:61A4 is the op-0x78 handler's own store):

| item | posts (rel, id, pan, op) |
|---|---|
| Tonic 0xBD | `04` @rel27 t8 · `06` @rel75 t0 |
| Ether 0xC0 | `04` @rel27 t8 · `07` @rel75 t0 |
| **Elixir 0xC3** | `06` @rel27 t8 · **`04` @rel30 via op 0x79** (cmd `$19`) t10 · `06` @rel75 t0 |
| Heal 0xC6 | `8E` @rel57 t8 · the script's own `78 FF` @rel117 t0 |
| **Revive 0xC7** | **`AD` @rel29 via op 0x7A**, pan `B0` = the TARGET's `$1D0C` (writer C1:7643 → `$A2B0`) |

This is the **first live firing of op 0x7A anywhere in the project** and closes
`battle_sfx_ops_inputs.md` §7.3 ("ops 0x78/0x79/0x7A/0x7B never fired… disasm-only, unconfirmed").

**What the port did instead:** with no script armed, an item fell through to the PC-PHYSICAL lane and posted
the caster's WEAPON hit — `CD/6D4C[$1C48[5*slot]]` = **id 0x4F, Crono's Wood Sword**, at rel 24 — for every
item, on every fixture (`gato_item`, `ether_restore`, `gato_elixir`, `heal_cure`, `revive`).

**Shipped:** `battleAscArm`'s PC side takes a third `kind` (0 physical / 1 tech / **2 item** →
`ROM_PC_ITM_ANIM_TBL = CD/63F0`), the action tick arms + ticks it for `h->actItemId > 0` exactly as it does
for a tech, and the physical lane gained the missing `h->actItemId == 0` term. Ours now posts, on the same
tracks with the same opcodes and one frame earlier (the `CT_PC_ACT_PREROLL` boundary): Tonic `04`@26 +
`06`@74; Ether `04`@26 + `07`@74; Elixir `06`@26 + `04`@29 (op79, cmd 19, track 10) + `06`@74.

## C. "HP COLOUR" — the restore number is GREEN, and the palette comes from a KIND byte

`C1/5ADB` (ShowDamage) builds the popup's OAM attribute:

```
15B49  LDA #31 / STA $8C            ; default attr 0x31 -> prio 3, OBJ palette 0
15B70  LDX $80 / LDA $AD9E,X        ; ** the popup KIND **
15B7A  CMP #05 / BNE                ;   5 -> the two "Miss" glyphs
15B99  CMP #03 / BEQ ->15BB1        ;   3 -> ordinary damage: KEEP 0x31
15B9D  CMP #01 / BEQ ->15BAD        ;   1 -> HP restore
15BA1  LDA #01 / STA $A0DC,Y        ;   (kinds 2 and 4 also raise the SECOND-ROW flag)
15BA6  LDA $AD9E,X / CMP #02 / BNE  ;   2 -> MP restore
15BAD  LDA #39 / STA $8C            ; ** attr 0x39 -> prio 3, OBJ PALETTE 4 **
```

Kind writers: `C1/DA92`→`C1/DAC9` (item HP restore) `#01`; `C1/DB30`→`C1/DB54` (item MP restore) `#02`;
`C1/DD9E` (healing TECH) `#01`; `C1/DE6A` (ordinary damage) `#03`; `C1/E904/E935/E95E` (miss) `#05`.

The two 4-colour digit sub-palettes are adjacent in ROM and each occurs **exactly once in the whole 4 MB**:

| | idx1 (ink) | idx2 | idx3 (outline) |
|---|---|---|---|
| **D1/4D80** damage (OBJ pal0) | `0x7FFF` white | `0x29EF` | `0x0CA7` |
| **D1/4D88** restore (OBJ pal4) | `0x4BF0` **green** = RGB (132,255,148) | `0x35A5` | `0x0CA7` |

ORACLE, live: `gato_elixir.state` driven to the Elixir on Marle — `--monitor "AD9C:8,ADC8:8,A3D1:1" --diff
--writer` gives `f455 $AD9C = 00 00 00 00 41 00 01 00 (C1:DACC)` = value 0x41 (65) **kind 01**, and
`$ADC8 = 00 00 00 00 0C 00 02 00 (C1:DB57)` = 12 **kind 02** — and the RENDERED frame at f571 has the "65"
in green, 39 opaque ink pixels of (132,255,148).

**Shipped:** `CtFxPopup` carries the ROM's `romKind`; the spawn sites set 1/2 for an item restore (by
`h->actItemMp`), 1 for a healing tech (a negative delta — the ROM's own C1/DD9E rule), 3 for damage and 5
for a miss; the digit blit picks `ctBattleFxHealPal` (the D1/4D88 head) for kinds 1/2. The ten digit tiles
index only colours 0-3 (counted: 159/159/93/229 pixels), so the 4-entry head is the whole palette that
matters — OBJ pal4's upper half is dual-use (the all-white hurt flash on a damage frame, the heal-effect
sprite colours on a heal frame) and is deliberately not baked.

## OPEN

- ~~The Elixir's SECOND (MP) number is still missing.~~ **CLOSED.** It is op 51, ShowDamage's record set 1
  (d45cff6), and `$A0DC`'s consumer is the popup animator CF/E781 (`LDA $A0DC,Y / BNE` -> D1/5E49 instead of
  D1/5E10: net -33 px against -15, 18 px higher at rest), cleared by CC/F06B's `STZ $A0DC,X` at the action's close
  -- ported 2026-09-30 ([battle_asc_inert_ops.md](battle_asc_inert_ops.md) §4). The Ether's single kind-2 number
  takes that row too.
- ~~The span stays the pinned per-item constant.~~ **CLOSED** (§A above; battle_asc_inert_ops.md §5).
- **`$987A/$987B/$987E/$9881/$9884/$987C`** (the other six config fields C1/475B loads, incl. the bank-D1
  graphics pointer at `D1/0060`) are read but not modelled — the item's own effect SPRITES (the sparkle,
  the revive flash) are still absent.
- Heal's `78 FF` is a **stop**, and Revive's `7A AD 03` is the only op-0x7A firing in the corpus; both are
  now executed by the interpreter but neither has a pinned check of its own yet.
