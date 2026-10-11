# MENU — open-items triage + derivation (2026-07-24)

Scope: the six X-menu sub-screens. Triage of every self-declared APPROXIMATION / placeholder in
`src/menu.c` + `src/menu.h` + the "Open questions" of `disasm/notes/menu_phase2.md`, and derivation
of everything that is closable NOW.

Every claim below cites either a ROM address disassembled with `python tools/dis.py <flat> <len> [m] [x]`
(flat file offset; bank C2 = 0x02xxxx, bank CC = 0x0Cxxxx, bank FF = 0x3Fxxxx) **or** an observed
oracle value (`build/ct.exe` mode + the exact command).

Artifacts written this pass (scratchpad `SP` =
`C:/Users/ICAROF~1/AppData/Local/Temp/claude/C--Users-Icaro-Ferris-Desktop-Crono-Trigger-Decompile/06e5d2d4-746b-4acf-abf6-006e461f263b/scratchpad/menu/`):
`save.raw(+.png/.wram/.vram/.oam)` = **the real entered SAVE screen**, `save_l1/l2/l4.raw` = its BG2/BG3/OBJ
layer isolations, `form.raw`, `hover4.raw`, `g_opt5.raw`, `cur1..cur3.raw.oam`.

---

## 0. HEADLINE

Two of the six sub-screens (**Form 5**, **Save 6**) have a **ROM ENTRY GATE that our engine does not
implement at all**. On every oracle state we own, the ROM **REFUSES to open both of them** — `$0067`
is set to `opt+1` by the A-handler for exactly one frame and the screen's own substate-0 handler writes
it straight back to 0. That is the real reason `menu_phase2.md` recorded "Form/Save flip $0067 back to 0
the next frame / never settle headlessly / task-spawned overlay". **There is no task-spawned overlay.**
The renders those dossiers captured are the ribbon's per-option **hover preview**, not an entered screen.

The Save screen *does* open — on the **overworld** — and this pass captured it byte-exactly for the
first time.

---

## 1. HONEST INVENTORY (per screen)

| # | Screen | Handler | Data layer | Render | Interaction |
|---|---|---|---|---|---|
| 1 | EQUIP/STATUS | C2/9C29 | byte-exact ($2600 sheet, offsets pinned) | byte-exact BG2 chrome (`ctMenuChromeEnteredTM[0]` + `ctMenuChromeEquipTM`); text layout oracle-measured | READ-ONLY — equip **change** deferred |
| 2 | ITEM | C2/AA34 | byte-exact ($2400/$2500 + name table — **now ROM-code-derived, §2**) | byte-exact chrome + own gradient | INTERACTIVE (Use/Move/Organize) |
| 3 | TECH | C2/B353 | byte-exact ($7E2837 → FF/F850 bitfield, $C2/BD40, $CC/15C4, $CC/253B) | byte-exact chrome + tabs; only Crono/Marle standing sprites baked | READ-ONLY, SINGLE techs only; Double/Triple, use, descriptions, "Sk Pts/Need", MP-halving gear deferred |
| 4 | CONFIG | C2/BF35 | byte-exact ($2990-92 unpack C2/92F4 + C2/C4BC indexing, validated vs live $0DCF) | layout-faithful; icon grid baked from oracle | READ-ONLY (toggling deferred); "Battle Speed" value blank because $0D91 has no packed source |
| 5 | **FORM** | C2/C621 | swap C2/C70C byte-exact; **roster/lock model NOW derived (§3)** | **APPROXIMATION** — and the screen it approximates *is not the entered screen*; it is the ribbon opt-4 hover preview. `ctMenuChromeEnteredTM[4]` == `ctMenuChromeRibbonTM[4]` (0 differing cells) | **NO ENTRY GATE** (§3.1) — our engine enters a screen the ROM refuses, and performs a swap the ROM forbids (§3.3) |
| 6 | **SAVE** | C2/CED4 | SRAM layout ROM-pinned; **2 offset errors found (§4.4)** | was "hover-preview-derived"; **the real entered screen is now captured (§4.2)** | **NO ENTRY GATE** (§4.1); save-WRITE deferred |

Inventory corrections to the task brief:

* `src/menu.c:719` — the comment *"placeholder for the sub-screens this phase does not build (Save 6)"*
  is **STALE**. `menu.h:256` is current: "all 1..6 are now built"; `CtMenuRenderPlaceholder` is only
  reachable for a `$0067` outside 1..6, which `field.c:5739` never produces. There is **no live
  placeholder screen**. The Save screen is a real read-only render.
* `menu_phase2.md` line ~645 ("key items packed variable-length from ~$CC/1486") is **FALSE** — see §2.
* `menu_phase2.md` "Form/Save … task-spawned overlay … never settled headlessly" is **FALSE** — see §3/§4.

---

## 2. ✅ CLOSED — the item-name table indexing (`menu_phase2.md` ~line 645)

The open question was "consumables fixed-width ~11 bytes from ~$CC/1373, key items packed
variable-length from ~$CC/1486; the id→offset indexing is not fully derived."

**It is a single uniform fixed-stride table, and the ROM builds an explicit pointer table for it.**

### 2.1 The builder — C2/98ED (`dis.py 0x0298ED 0x19 0 1`)

Raw bytes `[25 ROM bytes at 0x298ED: python tools/dis.py 0x298ED 25]`:

```
C2/98ED  A9 5E 0B    LDA #$0B5E        ; first record offset inside bank $CC
C2/98F0  85 00       STA $00
C2/98F2  A4 51       LDY $51           ; Y = 0
C2/98F4  A5 00       LDA $00
C2/98F6  99 00 7D    STA $7D00,Y       ; $7E7D00 + Y  (word table)
C2/98F9  18          CLC
C2/98FA  69 0B 00    ADC #$000B        ; ★ STRIDE = 11 bytes, UNCONDITIONALLY
C2/98FD  85 00       STA $00
C2/98FF  C8 C8       INY / INY
C2/9901  C0 E4 01    CPY #$01E4        ; 0x1E4 = 484 = 242*2  ->  0xF2 entries
C2/9904  90 EE       BCC ->C2/98F4
```

So `$7E7D00[id] = 0x0B5E + id*11`, for **all** ids `0x00..0xF1`. There is no second region, no
variable-length packing, no branch on item class.

### 2.2 The fetch — C2/F2E2 (`dis.py 0x02F2E2 0x11 0 1`)

Raw bytes `08 C2 30 29 FF 00 0A A8 B9 00 7D A8 A9 0B CC 28 60`:

```
C2/F2E2  PHP / REP #$30
C2/F2E5  AND #$00FF        ; A = item id
C2/F2E8  ASL A / TAY       ; Y = id*2
C2/F2EA  LDA $7D00,Y       ; the pointer built above
C2/F2ED  TAY               ; Y = the 16-bit name address
C2/F2EE  LDA #$CC0B        ; ★ A.high = BANK $CC, A.low = LENGTH 11
C2/F2F1  PLP / RTS
```

`C2/F2DC = JSR C2/F2E2 / JMP C2/EF65` (the string blitter); the same `LDA #$CC0B / JSR C2/EF65` pair
appears at C2/B05E. The single immediate `#$CC0B` pins **both** the bank **and** the record length.
Other consumers of the same table: `LDA $7D00,X` at D9/C4B4 and E7/A264.

### 2.3 Oracle proof (live table)

```
build/ct.exe --read "7E7D00+16" --from saves/menu_open_gato2.state --settle 4
  -> 5E0B 690B 740B 7F0B 8A0B 950B A00B AB0B
```
= 0x0B5E, 0B69, 0B74, 0B7F, 0B8A, 0B95, 0BA0, 0BAB = base + i*11. ✔

### 2.4 End-to-end decode proof (ROM bytes at `0x0C0B5E + id*11`)

```
id   1 $CC/0B69  20 B6 C8 C8 BD FF B2 D0 C8 CB BD   [icon20]"Wood Sword"
id   2 $CC/0B74  20 A8 CB C8 C7 FF A1 C5 BA BD BE   [icon20]"Iron Blade"
id 187 $CC/1367  29 AF CB C2 CC C6 B2 C9 BE BC CC   [icon29]"PrismSpecs"
id 189 $CC/137D  FF B3 C8 C7 C2 BC EF EF EF EF EF   " Tonic     "
id 200 $CC/13F6  FF B2 C1 BE C5 CD BE CB EF EF EF   " Shelter   "
id 213 $CC/1485  FF A1 C2 C4 BE FF AA BE D2 EF EF   " Bike Key  "   <- KEY ITEM, same stride
id 214 $CC/1490  FF AF BE C7 BD BA C7 CD EF EF EF   " Pendant   "   <- KEY ITEM, same stride
id 215 $CC/149B  FF A6 BA CD BE FF AA BE D2 EF EF   " Gate Key  "
id 224 $CC/14FE  FF B1 CE BB D2 FF AA C7 C2 BF BE   " Ruby Knife"
```

**Why the old dossier read "packed variable-length from ~$CC/1486":** record 213 starts at $CC/**1485**
with a leading `FF` (space) pad; a raw text search for the encoded "Bike Key" landed on $CC/1486, i.e.
one byte *into* a fixed-stride record. Likewise "Pendant" was found at $CC/1491 = record 214's base
$CC/1490 + 1. Consumable records lead with `FF` (space); equipment records lead with an
equip-type icon `0x20..0x2F`. That leading byte is the only difference.

### 2.5 Boundary proof

`0x0B5E + 0xF2*11 = 0x15C4` — the item table ends **exactly** where `menu.h`'s already-pinned tech-name
table `$CC/15C4` begins (C2/BDD9 `LDA #$15C4`). Two independently derived constants meet with zero gap.

**Verdict:** `src/menu.c` `ROM_ITEM_NAMES 0x0C0B5E` / `ROM_ITEM_STRIDE 11` are **CORRECT and now
ROM-code-derived** (previously only "verified via assets/text.py"). The `menu_phase2.md` open question
is closed; the note should be corrected so nobody re-opens it.

---

## 3. ✅ CLOSED (mechanism) / ❌ NOT DERIVED (entered geometry) — SCREEN 5, FORM

### 3.1 ★ THE ENTRY GATE — C2/C627 (`dis.py 0x02C627 0x30 1 1`)

`$0067=5`, `$0068=0` → dispatch C2/8292[5*2] = C2/C621, array word[0] = **C2/C627**:

```
C2/C627  SEP #$30
C2/C629  LDX $7F
C2/C62B  LDA $0F0B
C2/C62E  CLC
C2/C62F  ADC $0F0C
C2/C632  CMP #$02
C2/C634  BCS ->C2/C63D      ; ★ ($0F0B + $0F0C) >= 2  -> OPEN
C2/C636  STZ $67            ; ★ else REFUSE: screen back to 0 (the ribbon)
C2/C638  INC $68            ;   $0068 = 1 (ribbon idle substate)
C2/C63A  JMP C2/EACC
C2/C63D  JSR C2/C649        ; picker init (scan, $0F00/$0F01 = FF, INC $68)
C2/C640  REP #$10 / JSR C2/EAC2 / JMP C2/CAF3
```

**Oracle proof of the refusal + of the writer PC:**
```
build/ct.exe --monitor 0067:1 --from saves/menu_open_gato2.state \
  --btnscript "0:4,80:2,0:2,80:2,0:2,80:2,0:2,80:2,0:6,100:2,0:20" --writer
  f25  000067=05*(C2:9A83)     <- A-handler sets opt+1
  f26  000067=00*(C2:C638)     <- ★ the screen-5 init writes it back (STZ $67 @ C2/C636)
```

### 3.2 ★ WHAT $0F0B / $0F0C ARE — the roster builder C2/CD2B (`dis.py 0x02CD2B 0x70 1 1`)

```
C2/CD2B  PHP / REP #$30
C2/CD2E  LDA #$8080 / STA $0F02
C2/CD34  LDX #$0F02 / LDY #$0F04 / LDA #$0006 / MVN $7E,$7E   ; $0F02..$0F0A := 0x80 (empty)
C2/CD40  STZ $0F0B
C2/CD43  SEP #$30 / STZ $00 / LDX #$00 / TXY
C2/CD4A  LDX $00
C2/CD4C  LDA $2980,X          ; active-party slot 0..2
C2/CD4F  BMI ->C2/CD6A        ; bit7 = empty slot -> skip (no panel entry)
C2/CD51  INC $0F0B            ; provisionally COUNT it
C2/CD54  TAX                  ; X = char id
C2/CD55  LDA $7F01DF          ; ★ the party-LOCK bitfield
C2/CD59  AND $FFF9BB,X        ; ★ mask table FF/F9BB = 80 40 20 10 08 04 02 01  (= 0x80>>id)
C2/CD5D  BEQ ->C2/CD64
C2/CD5F  DEC $0F0B            ; ★ LOCKED -> un-count
C2/CD62  LDA #$40             ;    and mark the panel byte bit6
C2/CD64  ORA $00 / STA $0F02,Y / INY
C2/CD6A  INC $00 / LDA $00 / CMP #$03 / BCC ->C2/CD4A
C2/CD72  LDA $0D36 / BNE ->C2/CD99          ; reserve roster gated on $0D36
C2/CD7D  LDX #$03 / LDY #$00
C2/CD81  LDA $2980,X / BMI ->skip / TXA / ORA $00 / STA $0F05,Y / INY
C2/CD8D  INX / CPX #$09 / BCC ->C2/CD81     ; roster slots $2983..$2988 = RESERVE
C2/CD92  BIT $00 / BVS ->C2/CD99
C2/CD96  STY $0F0C                          ; ★ $0F0C = reserve count
```

So:
* `$0F02..$0F04` = active-party panel bytes = `(locked ? 0x40 : 0x00) | slotIndex`; `0x80` = unused entry.
* `$0F0B` = count of active members that are **NOT locked** (= exchangeable).
* `$0F0C` = count of reserve members ($7E2983..$2988).
* **`$7F01DF` = the per-character PARTY-LOCK bitfield**, tested with `FF/F9BB[charId] = 0x80>>charId`
  (the same table `menu.c mTechBits` already cites for `FFF9BB[cnt]`).

**Oracle proof (two independent states):**
```
build/ct.exe --read "7F01DF+2,rom:FF/F9BB+8" --from saves/menu_open_gato2.state --settle 4
  7F01DF = 8000        (bit 0x80 -> char 0 = Crono LOCKED)
  FF/F9BB = 80 40 20 10 08 04 02 01

build/ct.exe --monitor 0F02:14,... --from saves/menu_open_gato2.state --btnscript "<Right x4>,A"
  f17  $0F02..$0F0F = 40 01 80 80 80 80 80 80 80 | 01 | 00 | 00 00 00
       -> $0F02=0x40 (slot0 Crono, LOCKED)  $0F03=0x01 (slot1 Marle, free)
       -> $0F0B = 1, $0F0C = 0  ->  1 < 2  ->  REFUSE ✔

build/ct.exe --monitor 0067:1,0F02:14,7F01DF:1,2980:9 --from saves/ow_w0.state --btnscript "<X>,<Right x4>,A"
  f101 $0F02 = 40 80 80 ... , $0F0B = 0, $0F0C = 0   (party = Crono only)
  f111 $0067=05 -> f112 $0067=00                     -> REFUSE ✔
```

### 3.3 ★ CONSEQUENCE — the pinned `menuform` regression check asserts a canonically IMPOSSIBLE swap

`tools/regress_checks.json` → `field-menutest-gato` expects
`menuform: screen=5 party 00,01 -> 01,00 swap PASS`.

At storyline 08 the ROM (a) never opens screen 5 at all, and (b) even if it did, the picker's cursor
scan **skips locked panels**:
```
C2/C649  LDA $0F02,X / AND #$C0 / BNE ->C2/C648 (INX)      ; skip bit7 (empty) OR bit6 (LOCKED)
```
Crono's panel byte is `0x40` → the cursor can never rest on slot 0 → slot 0 can never be picked → the
`00 01 80 -> 01 00 80` exchange cannot happen. `CtMenuFormSwap` itself (= C2/C70C) is still byte-exact;
it is the *reachability* that is invented.

### 3.4 ✅ CAPTURED 2026-08-15 — the entered Form screen (was NOT DERIVED)

The blocking capture below arrived: **`saves/grupo_3_mapa.state`** (loc 082, storyline 15, party
`$7E2980-82` = `00,02,04`, free roam). The roster builder counts Crono then un-counts him (locked in
`$7F01DF`), leaving **`$0F0B` = 2**, so C2/C627 passes and `$0067 = 05` **PERSISTS** — verified with
`--monitor 0067:1,0F0B:1` (f111 onward), the first time this screen has ever settled here.

```
build/ct.exe <rom> --peek saves/grupo_3_mapa.state SP/form_entered.raw \
  --btnscript "0:20,200:2,0:60,80:2,0:4,80:2,0:4,80:2,0:4,80:2,0:8,100:2,0:150"
```

What it actually is: **three stacked character panels** (name + LV, HP/MP, and a portrait on the
right), a hand cursor on the picked panel, and an **"Exchange" button** across the bottom — nothing
like the ribbon hover preview `CtMenuRenderForm` reproduces. `ctMenuChromeEnteredTM[4]` is
regenerated from this capture (`tools/_gen_menuchrome_form.py`); the stored plane differed in
**128 cells (rows 12-19 x cols 8-23)**, the ribbon-box rectangle the refused capture had left, which
`CtMenuChrome` was skipping as a hole. The panel/text LAYOUT is still to be measured off the
isolations (`SP/form_l0/1/2/4.raw`) — the chrome is byte-exact, the content is not yet ported.

### 3.4b (superseded) — why it had been NOT DERIVED

**It cannot be derived from any state this repo owns**, because the ROM refuses to enter the screen on
all of them. What `CtMenuRenderForm` currently approximates is **the ribbon opt-4 HOVER PREVIEW**, and
that preview *is* byte-capturable and *is* what the player actually sees at storyline 08:

```
build/ct.exe --peek saves/menu_open_gato2.state SP/hover4.raw --btnscript "<Right x4>,0:100"      (no A)
build/ct.exe --peek saves/menu_open_gato2.state SP/f180.raw   --btnscript "<Right x4>,A,0:180"
  md5(hover4.raw) == md5(f180.raw) == 911009ec3b7e33b706ef37f0f75563bc   -> BYTE-IDENTICAL
  and identical at idle 40 / 90 / 180 / 300 frames -> the render SETTLES (contra menu_phase2.md)
```
Structural confirmation inside the repo's own captured data: `ctMenuChromeRibbonTM[4]` vs
`ctMenuChromeEnteredTM[4]` = **0 differing cells of 896** (the "entered" capture *is* the hover capture).

**Blocking capture:** a savestate whose `$0F0B + $0F0C >= 2` — i.e. ≥2 unlocked active members, or
1 unlocked + ≥1 reserve. Concretely: any state after the lock bitfield `$7F01DF` stops covering the
whole party (`$7F01DF & (0x80>>id) == 0` for two members), or after a 4th character is recruited so
`$7E2983..$2988` is non-empty. Mid-game (e.g. post-Lucca-join, party of 3) would do it.

---

## 4. ✅ CLOSED — SCREEN 6, SAVE

### 4.1 ★ THE ENTRY GATE — C2/CEDC (`dis.py 0x02CEDC 0x20 1 1`) + C2/9875

Screen-6 handler array C2/CED4 = words `CEDC, CEFD, CF61, CF92`. Substate 0 = **C2/CEDC**:

```
C2/CEDC  LDA $0D34 / ORA $0294 / ORA $0299 / ORA $029E
C2/CEE8  AND #$40
C2/CEEA  BEQ ->C2/CEF5     ; ★ bit6 clear -> OPEN
C2/CEEC  STZ $67           ; ★ else REFUSE
C2/CEEE  LDA #$01 / STA $68
C2/CEF2  JMP C2/EACC
C2/CEF5  JSR C2/EAC2 / INC $68 / JMP C2/D0E5   ; -> the 3-slot loader
```

`$0D34` is computed at menu-open by **C2/9875** (`dis.py 0x029875 0x40 1 1`):

```
C2/9895  STZ $0D32
C2/9898  STZ $0D34
C2/989B  LDA #$FFFF / STA $0D36
C2/98A1  LDA $0100          ; ★ the LOCATION word
C2/98A4  CMP #$01F0         ; ★ loc >= 0x1F0 == OVERWORLD
C2/98A7  BCS ->C2/98B5      ;   overworld -> $0D32 = $0D34 = 0   (SAVE ALLOWED)
C2/98A9  DEC $0D32          ;   field map -> $0D32 = 0xFFFF
C2/98AC  LDA $7F01CE        ; ★ the save-point flag
C2/98B0  BMI ->C2/98B5      ;   bit15 set -> leave $0D34 = 0  (SAVE ALLOWED at a save point)
C2/98B2  DEC $0D34          ;   else $0D34 = 0xFFFF           (SAVE BLOCKED)
```

This is CT's documented rule, now byte-derived: **save is allowed on the overworld, or on a field map
only when `$7F01CE` bit15 is set**. The `loc >= 0x1F0 = overworld` constant is the same one already
pinned in `ct-field-newgame-boot`.

**Oracle proof:**
```
build/ct.exe --read "0D34+1,0294+1,0299+1,029E+1,7F01CE+4,0100+2" --from saves/menu_open_gato2.state --settle 4
  $0D34 = FF   $0294 = 00   $0299 = 00   $029E = 00
  $7F01CE = 0001 (bit15 clear)   $0100 = 0006 (field, < 0x1F0)   -> $0D34 = FFFF -> REFUSE
build/ct.exe --monitor 0067:1,... --from saves/menu_open_gato2.state --btnscript "<Right x5>,A" --writer
  f29 $0067=06*(C2:9A83)  ->  f30 $0067=00*(C2:CEEE)      ✔ refused

build/ct.exe --monitor 0100:2,0A00:2,0D34:2 --from saves/ow_w0.state --btnscript "0:20,200:2,0:60"
  $0100 = 01F0 (overworld)  ->  f42 $0D34 = 0000           ✔ save allowed
build/ct.exe --monitor 0067:1,... --from saves/ow_w0.state --btnscript "0:20,200:2,0:60,<Right x5>,A,0:120"
  f117 $0067=06 AND IT PERSISTS  ✔ ★ THE SAVE SCREEN OPENS
```

### 4.2 ★ THE REAL ENTERED SAVE SCREEN — captured (first time)

```
SCRIPT="0:20,200:2,0:60,80:2,0:4,80:2,0:4,80:2,0:4,80:2,0:4,80:2,0:8,100:2,0:150"
build/ct.exe --peek saves/ow_w0.state SP/save.raw --btnscript "$SCRIPT"
PEEK_ISOBG=0|1|2|4 build/ct.exe --peek saves/ow_w0.state SP/save_l{...}.raw --btnscript "$SCRIPT"
build/ct.exe --ppuregs saves/ow_w0.state --btnscript "$SCRIPT"
```

`--ppuregs`: mode=1, brightness=15, BG1 map $5800/chr $0000, BG2 map $6000/chr $0000,
BG3 map $6800/**chr $7000**, OBJ chr $4000, objSize=0.

**LAYER ROLES (PEEK_ISOBG), and this CLOSES `menu_phase2.md`'s "BG3 role" open question:**
* BG1 (ISOBG=0) — **blank on screen**. Its tilemap nevertheless holds a *staged, not-displayed*
  overwrite-confirm dialog at $5800 cols 23.., rows 2..10 (tile id == CT charset code):
  ```
  row 2  A4 CB BA CC BE            "Erase"
  row 4  C8 C5 BD                  "old"
  row 6  CC BA CF BE DF            "save" + glyph 0xDF   (0xDF = the cursor/'?' icon glyph, cf. menu.c mCfgStr)
  row 8  FF FF B8 BE CC            "  Yes"
  row10  FF FF AD C8               "  No"
  ```
  That is the A-on-a-used-slot prompt (see §4.3).
* BG2 (ISOBG=1) — chrome: marble + the 3 slot buttons + the two bottom windows.
* **BG3 (ISOBG=2) — ALL the Save-screen TEXT.** The menu font is uploaded to chr $7000 for exactly this.
* OBJ (ISOBG=4) — exactly **one** live sprite: the hand cursor.

**BG3 tilemap ($6800), byte-exact (all cells palette 4; 0x100 = blank):**
```
row 3   cols 3-4 = 001 002      cols 10-25 = 120 121 ... 12F
row 4   cols 3-4 = 011 012      cols 10-25 = 130 131 ... 13F     <- SLOT 1
row 7   cols 3-4 = 003 004      cols 10-25 = 140 ... 14F
row 8   cols 3-4 = 013 014      cols 10-25 = 150 ... 15F         <- SLOT 2
row11   cols 3-4 = 005 006      cols 10-25 = 160 ... 16F
row12   cols 3-4 = 015 016      cols 10-25 = 170 ... 17F         <- SLOT 3
```
i.e. per slot i: a **2x2-tile (16x16 px) NUMBER block** at map cols 3-4, tiles `0x001+2i / 0x002+2i`
over `0x011+2i / 0x012+2i`; and a **16x2-tile (128x16 px) SUMMARY block** at map cols 10-25, tiles
`0x120+0x20*i .. +0x0F` over `0x130+0x20*i .. +0x0F`. Row pitch = 4 tile rows = **32 px**. These are
*dynamically built* 2bpp tiles (the text is rasterised into chr $7000), not charset-code cells.

**Measured pixel geometry (native 256x224 = peek rows 8..231):**
| item | value |
|---|---|
| slot text row pitch | **32 px** |
| slot 1 / 2 / 3 text band (glyph ink) | native y **28..36** / **60..68** / **92..100** |
| slot NUMBER glyph ink | x **28..34** (block cells = x24..39) |
| "No data" ink | x **120..164** (block cells = x80..207) |
| cursor-row text colour | **(231,231,231)**, shadow (24,24,24) |
| non-cursor rows | **(115,115,115)**, shadow (8,8,8) — *the cursor's whole row is bright; the others are dim* |

**HAND CURSOR (OAM, `tools/ct.py oam SP/save.raw.oam`):** exactly one live sprite —
`idx 28, X=8, Y=29, tile 0x020, attr 0x30 (prio 3, OBJ pal 0), size 1 (16x16), no flip`.
Pressing Down (`20:2,0:10` xN) moves it: **Y = 29 + 32*cursor** (29 / 61 / 93) and it **wraps 2→0**.
Rendered ink box (ISOBG=4) = native **x8..23, y31..42** (SNES renders OBJ +1 scanline, so the sprite's
top row is y30 and its first row is transparent).
[CORRECTED 2026-08-14: the ROM tile's ink is at LOCAL y2..13 (decoded from $FF/9CF0), so the ink box
y31..42 puts the sprite CELL top at y29 = the OAM Y with NO +1 in this peek path; the "+1, first row
transparent" reading was off by one. Anchor on the ink box, which is measured and unambiguous.]
Tile 0x020 is one 16x16 sprite before the ribbon arrow tile 0x022 = ROM FF/9D70, i.e. ROM **FF/9CF0** —
consistent with `menu.h`'s `handCursor` provenance.

**Cross-check against our render:** with `MI_HX = 8`, `CtMenuRenderSave`'s existing
`mText(num, MI_HX+16, y+12)` / `mText("No data", MI_HX+112, y+12)` with `y = 16 + i*32` place the
number at x24 and "No data" at x120, both on rows y28/60/92 — **byte-matching the oracle**. Only the
per-row bright/dim distinction and the hand anchor are wrong (see §7 step 4).

**BG2 tilemap ($6000) of the entered screen** — 3 slot buttons at rows 2-5 / 6-9 / 10-13 (cols 2-29,
tiles 0x300-0x313), marble rows 14-15, and **two bottom windows** rows 16-25 (cols 2-13 and cols 14-29,
9-slice tiles 0x3DC-0x3EF). No ribbon box.

### 4.3 The slot scan + the A-flow (validates `menu.h`)

`C2/D156` (`dis.py 0x02D156 0x50 1 1`):
```
C2/D168  LDX $79 / STZ $0D49,X
C2/D16D  JSL FF/F9C4                ; the validity check
C2/D171  LDA $00 / BEQ ->C2/D17F    ; 0 = USED
C2/D175  LDA #$1A / STA $020C / JSR C2/D296     ; EMPTY: string id 0x1A ("No data")
C2/D17F  INC $0D49,X / JSR C2/D19F              ; USED: $0D49[slot]=1 + read the summary
C2/D185  JSR C2/D2C4 / INC $79 / CMP #$03 / BCC ->C2/D168
```
`FF/F9C4` (`dis.py 0x3FF9C4 0x30 0 0`) — exactly as `menu.h` claims:
```
PEA $3030 / PLB / PLB        ; DB = $30 (the SRAM save-file image)
JSR FF/FBD3
LDA $79 / AND #$0003 / ASL / TAX
LDA $7FF8,X / CMP #$E41B / BNE -> A = $8080   (bad signature)
JSR FF/FBE7 / CMP $7FF0,X  / BNE -> A = $4040 (bad checksum)
else A = $0000 (VALID)
```
A-on-a-slot, `C2/CF0A`: `LDX $79 / LDA $0D49,X / BEQ ->C2/CF1E` — an **empty** slot goes straight to
the write path; a **used** slot goes `STA $0F00 / INC $68 / JMP C2/D4BB` = the "Erase old save?" confirm
staged on BG1 (§4.2). (Both are inside the DEFERRED save-WRITE boundary.)

### 4.4 ★ TWO ERRORS in `menu.h` / `CtMenuSaveSlot`, from C2/D19F (`dis.py 0x02D19F 0xB0 1 1`)

```
C2/D1BF  LDX $22                       ; $22 = slotBase + memberIdx
C2/D1C1  LDA $300580,X                 ; party member byte  -> $30:0580 + slot*0x800 + memberIdx   ✔ menu.h OK
C2/D1C5  STA $004202 / LDA #$50 / STA $004203        ; id * 0x50
C2/D1D3  LDA #$0200 / ADC $7B / ADC $004216 / TAX    ; ★ X = 0x0200 + slotBase + id*0x50
C2/D1DD  LDA $300012,X / STA $000F,Y                 ; ★ LEVEL = $30:(0212 + slot*0x800 + id*0x50)
C2/D1E4  LDA #$0008 / MVN $7E,$30                    ;   + 9 bytes of the sheet
C2/D1EC  LDA #$06 / STA $004203
C2/D1F4  LDA #$05B0 / ADC $7B / ADC $004216 / TAX
C2/D1FE  LDA #$0005 / MVN $7E,$30                    ; NAME = $30:05B0 + slot*0x800 + id*6  (6 bytes) ✔
C2/D21B  LDA $30059C,X / CMP #$03E8 / BCC / LDA #$03E7 ; ★ value at +0x059C is CAPPED at 999
C2/D22C  LDA $3005F3,X / AND #$01FF                  ; LOCATION, 9-bit                                ✔
C2/D238  LDA $7B / ADC #$05E0 / TAX / LDA #$0009 / MVN $7E,$30 ; CLOCK = +0x05E0, 10 bytes
C2/D247  LDA $30059E,X / STA $0000,Y                 ; ★ a further word at +0x059E   (menu.h omits)
C2/D24E  LDA $300793,X ...                           ; ★ a further word at +0x0793   (menu.h omits)
```

1. **CHAR-SHEET BASE IS `+0x0200`, NOT `+0x0000`.** `menu.h:227` says "char sheet `$+0x0000+id*0x50`
   (+0x12 LEVEL)" and `menu.c:1451` reads `sr[base + id*0x50 + 0x12]`. The ROM computes
   `$30:0012 + (0x0200 + slotBase + id*0x50)` → the sheet base inside a slot is **0x0200**, LEVEL at
   +0x12, i.e. `sram[slot*0x800 + 0x0200 + id*0x50 + 0x12]`. Our reader is off by 0x200. (Dormant —
   `h->sram` is NULL — but wrong the moment SRAM lands.)
2. **The +0x059C word is CAPPED at 0x3E7 (999) by the ROM before display.** `menu.h` calls it "gold";
   `CtMenuSaveSlot` reads it uncapped. The cap is byte-derived; the *identity* "gold" is **not**
   confirmed by any observed render (all our slots are empty, and the captured entered screen shows no
   gold field). Flagging as unverified, not fixing the label.
   > **RESOLVED 2026-08-15 — and the label was WRONG.** A real saved game arrived
   > (`saves/com_jogo_salvo.state`, SRAM slot 0 signed `0xE41B`) and the used-slot screen renders
   > **"1105G"** with **"Save # 1"**. In that slot `+0x059C = 1` and `+0x05E0 = 1105`, which also
   > equals the live WRAM gold `$7E2C53`. So `+0x059C` is the **SAVE NUMBER** (the 999 cap fits a
   > save counter) and **GOLD is the 24-bit head of the 10-byte block at `+0x05E0`** that C2/D238
   > MVNs -- the block `menu.h` called "play-time". The clock digits follow gold inside that same
   > block: `+0x05E7` = hours, `+0x05E6`/`+0x05E5` = minute tens/ones (screen "TIME 08:43" ->
   > 8 / 4 / 3). One sample, so the clock is a MATCH, not a derivation; `+0x05E3/+0x05E4` are still
   > unidentified. **Also validated by the same render: the char-sheet base +0x0200** -- the levels
   > read at `+0x0200 + id*0x50 + 0x12` are 3/3/5 and the screen shows Crono LV3 / Lucca LV3 /
   > Frog LV5, confirming against pixels what §4.4.1 had derived from the disassembly alone.
   > The used slot's 128x16 BG3 summary block shows the **event name** ("The Queen is Gone"),
   > not the leader's name.
3. The summary also copies words from **+0x059E** and **+0x0793** that `menu.h` does not mention.

---

## 5. ✅ CLOSED — a concrete chrome defect (`ctMenuChromeEnteredTM[4]` and `[5]`)

Diff of the repo's stored `ctMenuChromeEnteredTM[5]` against the **live entered Save screen** BG2
tilemap (VRAM word 0x6000 from `SP/save.raw.vram`): **96 differing cells of 896 — exactly BG2 rows
12-17 x cols 8-23**, i.e. the stored map still contains the **ribbon box** ($3A4-$3AC), because it was
captured from the REFUSED entry (which leaves the ribbon up).

`CtMenuChrome(fb, screen, opt, 0)` (`menu.c:95`) **skips** ribbon-box tiles in mode 0
(`if (mode == 0 && isBox) continue;`). `field.c:5737/5738` call it with mode 0 for screens 5 and 6.
Net effect: on our Form and Save screens a **128x48 px region at x64..191, y96..143 is never drawn** —
the real screen has the slot-3 button there (rows 12-13), marble (14-15) and the top of the bottom
window (16-17).

**The byte-exact replacement for those 96 cells** (from the live entered Save screen, `save.raw.vram`
word 0x6000; format = the same `(pal<<10)|tile` words the header stores):
```
row12 cols 8..23: 0F0F 0F12 0F13 0F0E 0F0F 0F12 0F13 0F0E 0F0F 0F12 0F13 0F0E 0F0F 0F12 0F13 0F0E
row13 cols 8..23: 0F0A 0F09 0F0A 0F09 0F0A 0F09 0F0A 0F09 0F0A 0F09 0F0A 0F09 0F0A 0F09 0F0A 0F09
row14 cols 8..23: 1FA0 1FA1 1FA0 1FA1 1FA0 1FA1 1FA0 1FA1 1FA0 1FA1 1FA0 1FA1 1FA0 1FA1 1FA0 1FA1
row15 cols 8..23: 1FA2 1FA3 1FA2 1FA3 1FA2 1FA3 1FA2 1FA3 1FA2 1FA3 1FA2 1FA3 1FA2 1FA3 1FA2 1FA3
row16 cols 8..23: 03DE 03DD 03DE 03DD 03DE 03DF 03DC 03DD 03DE 03DD 03DE 03DD 03DE 03DD 03DE 03DD
row17 cols 8..23: 03E9 03EC 03ED 03E8 03E9 03E1 03E0 03E8 03E9 03EC 03ED 03E8 03E9 03EC 03ED 03E8
```
The other 800 cells of `ctMenuChromeEnteredTM[5]` are already byte-identical to the live screen. ✔

For `[4]` (Form) the same 96-cell hole exists, but the correct fill is **NOT DERIVED** — see §3.4.

---

## 6. NOT DERIVED — and the exact capture that would close each

| Gap | Blocking capture |
|---|---|
| Entered FORM screen layout (the "centre-card geometry") + `ctMenuChromeEnteredTM[4]` | A savestate with `$0F0B + $0F0C >= 2`: ≥2 **unlocked** active members (`$7F01DF & (0x80>>id) == 0`) or 1 unlocked + ≥1 reserve (`$7E2983..$2988` non-empty). Then: `--peek <state> out.raw --btnscript "<X>,<Right x4>,A,0:150"` + `PEEK_ISOBG=0/1/2/4` + decode BG2 $6000 / BG3 $6800, same recipe as §4.2 |
| USED save-slot summary render (what the 128x16 BG3 block actually says: name/LV/location/time field positions) | An SRAM image with a valid slot (signature `$30:7FF8+slot*2 == 0xE41B` + FF/FBE7 checksum). Either a LakeSnes state carrying cart SRAM with a save in it, or drive an actual save on the overworld state (A on an empty slot → C2/CF1E) and re-enter the screen |
| Whether `+0x059C` really is GOLD (and why capped at 999) | The same used-slot state; read `$30:059C` and compare with the rendered field |
| The BG3 2bpp text palette (bright 231/231/231 vs dim 115/115/115) as CGRAM entries | `SP/save.raw.oam[1056:1568]` already holds the CGRAM at the entered Save screen — needs the BG3 sub-palette index decoded from the 2bpp tiles (not done this pass) |
| The overwrite-confirm dialog's on-screen geometry ("Erase old save? Yes/No") | Drive A on a **used** slot (needs the SRAM state above) so C2/D4BB actually composites it; it is only *staged* in BG1 otherwise |
| `$0294 / $0299 / $029E` (the 3 extra OR terms in the Save gate) | All three were 00 on both states, so only `$0D34` was exercised. Needs a state where one is non-zero; `tools/ct.py refs 0294` + a `--monitor --writer` on a state where saving is refused for a non-location reason |
| `$0D36` (gates the reserve-roster half of C2/CD2B) — C2/989B unconditionally sets it to 0xFFFF at menu-open, which would always skip the reserve loop | A state with reserve members; `--monitor 0D36:2 --writer` across menu-open |

---

## 7. IMPLEMENTATION PLAN (C side — none applied; this pass is derivation-only)

**STATUS 2026-08-14 — steps 1-5 + 7 LANDED** (step 1's Form half landed 2026-07-25, 69e1bbd):
step 1 = CtMenuFormCanOpen + CtMenuSaveCanOpen (menu.c) wired into the A-handler + the menutest
drive (`menugate:`/`menusavegate:` refusal signatures); step 2 = the +0x0200 sheet base + the
0x3E7 cap in CtMenuSaveSlot; step 3 = ctMenuChromeEnteredTM[5] regenerated from the OPENED
overworld screen (_gen_menuchrome_save.py; fresh capture == this pass's SP/save.raw.vram, 96-cell
diff exactly as §5); step 4 = the save text palette (CGRAM pal4, body 231), the hand anchored to
the ink box (ROM tile ink local y2..13 -> top-left (8, 29+32*cursor)), and the bright/dim rows;
step 5 = this file + menu_phase2.md corrections; step 7 = re-pins + the `menusave-ow-open` render
check. Byte-compare vs the oracle's entered screen: EVERYTHING except the slot-row text pixels is
byte-exact (1432/57344 px differ, all inside the summary-text ink: the ROM rasterises the summary
through the C2/EF65 blitter with its own advance model; our render reuses the fixed 8x8 menu font
— the documented font-provenance boundary, menu.h). NEW DERIVATIONS beyond this dossier, from the
cur1/cur2/cur3 + fresh captures: (a) the bright/dim banding is the PPU's WINDOWED HALF COLOR MATH
— VRAM/CGRAM are byte-identical across cursor moves, only the hand's OAM Y changes; composed-dim
== composed-bright with each 5-bit channel halved, over x16..239 x the non-cursor 32-row bands;
(b) the window gradient is a 16-step COLDATA ramp +8..-7 in 5-BIT space that RESETS AND
HEIGHT-NORMALIZES per window (2-row steps on the 32px buttons, 5-row steps on the 80px bottom
windows) — exact model conv5(clamp(pal5+d5[y])), table ctMenuSaveGrad5 (menu_saveform.h);
(c) the hover preview draws every row BRIGHT with NO hand (g_opt5 OAM = ribbon sprites only).
Still open here: §3.4 (entered Form geometry — blocked on a §6 capture) and §6's other rows.

Ordered by (user-visible impact x derivability). Every step names the file, the function and the
RAM/ROM datum it keys on. **No scene gates, no per-state constants.**

1. **`src/field.c` — implement the two screen-entry gates** (the biggest correctness gap).
   In the ribbon A-handler (`field.c:5524`, currently `menuScreen = menuOpt + 1;`) add the ROM's own
   substate-0 refusals, keyed on real RAM, before committing the screen:
   * screen 5: compute the roster exactly as **C2/CD2B** does from `mem[0x2980+i]` (i=0..8) and
     `mem7F[0x01DF] & (0x80 >> charId)`; refuse (stay on the ribbon) when
     `unlockedActive + reserveCount < 2` (**C2/C627**).
   * screen 6: compute `$0D34` exactly as **C2/9875** does — `loc >= 0x1F0 ? 0 : (($7F01CE & 0x8000) ? 0 : 0xFFFF)`
     — and refuse when `(($0D34 | $0294 | $0299 | $029E) & 0x40) != 0` (**C2/CEDC**).
   Risk: **medium** — it changes what the menu does at storyline 08 and will break the pinned
   `menuform:` line of `field-menutest-gato`; that check is asserting a canonically impossible swap
   (§3.3) and must be re-pinned to the refusal, not preserved.

2. **`src/menu.c` `CtMenuSaveSlot` — fix the char-sheet base** (`menu.c:1451`):
   `sr[base + 0x0200 + (leader&7)*0x50 + 0x12]` per **C2/D1D3-D1DD**. Also update the `menu.h:227`
   comment. Risk: **low** (dormant path, `h->sram == NULL`), but it is a wrong constant on the record.

3. **`src/data/menu_chrome.h` — replace the 96 ribbon-box cells of `ctMenuChromeEnteredTM[5]`** with the
   live entered-Save cells in §5 (regenerate the whole `[5]` plane from `SP/save.raw.vram` word 0x6000
   with the existing generator). Removes a 128x48 unpainted hole on the Save screen.
   Risk: **low** — pure data replay, changes `field-menutest-gato`'s `fnv32` (expected; re-pin).

4. **`src/menu.c` `CtMenuRenderSave` — two deltas only; the empty-slot text geometry is ALREADY CORRECT.**
   Cross-checked against §4.2: `MI_HX = 8`, so the existing `mText(num, MI_HX+16, y+12)` = **x24, y28/60/92**
   and `mText("No data", MI_HX+112, y+12)` = **x120, y28/60/92** — both match the oracle exactly
   (number cell x24 / ink x28..34; "No data" ink x120..164; rows y28/60/92, pitch 32). Down-wrap is
   already `% CT_MENU_SAVE_SLOTS` (`field.c:5662`) = correct. What is missing:
   * **cursor-row highlight**: the oracle draws the cursor's whole row BRIGHT (231,231,231) and the
     other two DIM (115,115,115) — `menu.c:1471-1485` draws all three the same. Route the two
     non-cursor rows through `mDim = 1` (the existing dim glyph path).
   * **hand-cursor anchor**: oracle OAM is `X=8, Y=29` (SNES +1 scanline → top row y30), ink box
     x8..23 / y31..42; `menu.c:1485` calls `mHandCursor(h, fb, MI_HX+2, y+12)` = top-left (10, 28+32i).
     The exact constant depends on the transparent margins inside `h->handCursor` — set it so the
     rendered ink box lands on **x8..23, y(31+32i)..(42+32i)**, i.e. verify against the ink box, not
     the OAM word.
   Risk: **low** (render-only), but it re-pins the menutest `fnv32`.

5. **`src/menu.h` / `menu.c` doc-truth pass** — record on `ROM_ITEM_NAMES`/`ROM_ITEM_STRIDE` that the
   base+stride are now derived from **C2/98ED** (`ADC #$000B`, `CPY #$01E4`) and **C2/F2E2**
   (`LDA #$CC0B` = bank $CC / length 11), not merely cross-checked with `assets/text.py`; and correct
   `disasm/notes/menu_phase2.md`'s "packed variable-length key items" open question (§2.5).
   Risk: **low** (comments/notes only).

6. **`src/menu.c` `CtMenuRenderForm`** — leave the layout as-is until the blocking state of §6 exists,
   but change the *label* on it: it reproduces the **ribbon opt-4 hover preview**, which is
   byte-capturable and byte-verified identical to what the ROM shows after A (§3.4) — it is not an
   approximation of an entered screen. Once step 1 lands, the entered Form screen is unreachable at
   storyline 08 anyway, so the honest boundary is "hover preview reproduced; entered screen NOT DERIVED".
   Risk: **low**.

7. **`tools/regress_checks.json`** — after steps 1/3/4: re-pin `field-menutest-gato`
   (`menuform:` → the refusal signature; `menusave:` → the refusal signature on the field state; new
   `fnv32`), and add a new check driving the **overworld** Save screen (`saves/ow_w0.state`,
   `--btnscript "0:20,200:2,0:60,80:2,0:4,x4...,100:2,0:150"`) so the *opened* Save screen is pinned too.
   Risk: **medium** — re-pinning must follow `add-regress-check`, and only after each change is
   oracle-validated individually.

---

## 8. Key addresses (new this pass)

* `C2/98ED` — item-name POINTER-TABLE builder: `$7E7D00[id] = 0x0B5E + id*11`, 0xF2 entries (`CPY #$01E4`)
* `C2/F2E2` — item-name FETCH: `Y = $7D00[id*2]`, `A = $CC0B` (bank $CC, **11-byte** record); `C2/F2DC = JSR F2E2 / JMP C2/EF65`
* `$7E7D00` — the live 242-entry item-name pointer table (oracle-verified: 0B5E 0B69 0B74 …)
* `$CC/0B5E .. $CC/15C4` — the item-name table, **uniform 11-byte records**, id 0..0xF1; ends exactly where the tech-name table `$CC/15C4` starts
* `C2/C621` — screen-5 substate array = {C2/C627 init, C2/C65D picker idle, C2/C6B9 second-pick idle}
* **`C2/C627`** — ★ FORM ENTRY GATE: `($0F0B + $0F0C) >= 2` else `STZ $67 / INC $68` (writer PC seen as C2:C638)
* `C2/CD2B` — the Form roster builder: `$0F02..$0F04` = `(locked?0x40:0)|slot`, `$0F0B` = unlocked-active count, `$0F05..` = reserve panels, `$0F0C` = reserve count
* **`$7F01DF`** — ★ per-character PARTY-LOCK bitfield; locked iff `$7F01DF & $FF/F9BB[charId]`, `FF/F9BB = 80 40 20 10 08 04 02 01`
* `C2/C649` — picker cursor scan: `LDA $0F02,X / AND #$C0 / BNE next` → skips EMPTY (bit7) **and LOCKED (bit6)**
* `C2/CED4` — screen-6 substate array = {C2/CEDC init, C2/CEFD idle, C2/CF61, C2/CF92}
* **`C2/CEDC`** — ★ SAVE ENTRY GATE: `(($0D34|$0294|$0299|$029E) & 0x40)` → `STZ $67 / $68=1` (writer PC C2:CEEE); else `INC $68 / JMP C2/D0E5`
* **`C2/9875`** — ★ save-availability: `$0D32 = (loc>=0x1F0)?0:FFFF`; `$0D34 = (loc>=0x1F0)?0:(($7F01CE & 0x8000)?0:FFFF)`; `$0D36 = FFFF`
* `$7F01CE` — the field save-point flag (bit15); `$0100` — the location word (`>= 0x1F0` = overworld)
* `C2/D156` — 3-slot scan; `$0D49[slot]` = 1 used / 0 empty; empty → `LDA #$1A / STA $020C / JSR C2/D296`
* `FF/F9C4` — slot validity (DB=$30): sig `$30:7FF8+slot*2 == 0xE41B`, checksum `FF/FBE7` vs `$30:7FF0+slot*2`; result 0000/8080/4040
* `C2/D19F` — slot summary: party `$30:0580+`, **sheet `$30:0200 + id*0x50` (+0x12 LEVEL)**, name `$30:05B0 + id*6`, `+0x059C` (capped 0x3E7), loc `+0x05F3 & 0x1FF`, clock `+0x05E0` (10 bytes), plus `+0x059E` and `+0x0793`
* `C2/CF0A` — A on a slot: `$0D49[slot]` 0 → C2/CF1E (write), 1 → `JMP C2/D4BB` ("Erase old save?" confirm)
* Save screen render: BG3 map $6800 (chr $7000) carries ALL the text; per slot a 2x2 number block at cols 3-4 + a 16x2 summary block at cols 10-25, row pitch 4; hand cursor = OBJ tile 0x020 (ROM FF/9CF0), pal0, prio3, X=8, **Y = 29 + 32*cursor**, wraps
