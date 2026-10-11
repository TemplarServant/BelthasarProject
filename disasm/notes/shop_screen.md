# The SHOP screen (menu mode 2, screen `$0067 = $0C`) — derivation

Sources: the ROM (`python tools/dis.py <off> <len> 1 0` — the screen runs **M=8 / X=16**; routines that
`REP #30` were re-read with `0 0`) and the oracle (`build/ct.exe`, LakeSnes). Fixture:
`build/_sp/shop90_t0.state` (loc 5, Crono under the fair merchant whose activate is `C8 90` = shop `$10`
= `[5C Karate Gi, 7D BronzeHelm]`; party Crono+Marle; bag = one `D6` Pendant; 400 G).
Scratch artifacts (not in the repo): `%TEMP%\claude\...\scratchpad\shop_re\` (monitor logs, PNGs, traces).

Conventions used below:
* `--btnscript` masks are LakeSnes serial order: B=1 Y=2 Sel=4 St=8 Up=10 Down=20 Left=40 Right=80
  A=100 X=200 L=400 R=800 (docs/DEBUG_SURFACE.md). `--peek` + `--btnscript` ignores `--frames`, so peeks
  below use scripts cut to the frame.
* `$0068` is task-local; the real substate is task slot 1's saved copy **`$7E:00BE`** (menu2_shell_exit.md).
  "fN" = the `--monitor` row label.
* Menu shared machinery reused here and NOT re-derived: input prologue `C2/E984`/`E9E2` + nav graph
  `$7E:9380` (6 B/cell `x,y,up,dn,lf,rt`, bit7 = edge), page scroller `C2/93BF`, window params `C2/EA81`,
  bag ops `C2/8791`/`87D5`/`87FA`, recompute `C2/916E`/`9137`, equip predicate `C2/93A8`
  (all menu2_equip_change.md); SFX wrappers `C2/EAC2` (id 00 blip) / `EACC` (id 01 buzz) / `EABA` (id in A)
  and the `$0D76` throttle (menu_sfx.md); fade `C2/83B2`/`83CA` (menu2_shell_exit.md §2.5); descriptions
  `C2/F2F3` (menu_item_descriptions.md); number printer `C2/F114` (menu2_tech_screen.md AV).

---

## 1. Entry and exit

| step | ROM | effect |
|---|---|---|
| op `C8 xx`, xx `$80..$BF` | C0/3757 → C0/0CEA → C0/0D09 | `X = xx & $3F` (shop no.), `A = (xx ROL×3)&3 = 2`; `JSL C2/8000` at C0/0D18 |
| menu entry | C2/8000 | `$0A00=02`, `$0A01=X`, `$0071 ← $0A01`, `$0067 ← C2/808F[2] = $0C` (measured `f42 $0067=0C (C2:807F)`) |
| substate 0 | C2/D52B | `JSR C2/DACA` (init §3); `$0D9C=0B`; `JSR C2/8A98` (spawn task slot 6 = blinking ▼, §7.4); `$0DA6=F8`, `$0DA1=B022`; `INC $68`; `JMP C2/83B2` (fade-in task) |
| exit | substate 1 B → `JMP C2/82B2` | `REP#10/SEP#20; LDA #FF; JSR C2/8255` (kill all tasks), `C2/834D`, **`C2/83CA` fade-out**, force blank, `LDX $0920/TXS`, RTL |
| field resume | C0/0D1C | `C0/0B4E`, `0B64`, `0B75`, D=$0100, `C0/01A5` reload, `C0/B192`, `$17 &= ~40`, `$18 |= 40`, `BRL C0/2824` (fade-in) |

Shop table: `$CC:2C6D` = 24 word pointers (shops `$00..$17`; entry `$18` onward is list data), each list
0-terminated item ids. The shipped operands are `C8 80..97`. Shop `$12` is EMPTY (`$CC:2D3A = 00`).
Out-of-range shop numbers (`$18..$3F`) would read list bytes as pointers — not exercised.

Timing (fixture, `bs_a` §10): A f10-11 → `$0125=90` f11 → `$0A00=02` f27 → `$0067=0C` f42 → `$54=08` f43
(C2:DAD5) → **`$00BE=01` f48**. B at f430-431 → `C2/82B2` runs in frame 430 → fade-out → RTL reaches
**C0/0D1C at f438** (`--hookpc 0D1C`: `f438 visits=1`) → `$0A00=00` f439 (C0:0914).

---

## 2. Substate table (`C2/D519` = `2B D5 46 D5 8B D5 45 D6 90 D6 18 D6 C3 D6 15 D7 78 D7`)

All handlers (except 0) start `JSR C2/E984`; `$0D1D` bit7 = A (`$2993` mask), bit6 = B (`$2994`), bit1 = R,
bit0 = L; `$5A` = d-pad edge (`$00F5`: b0 Right, b1 Left, b2 Down, b3 Up).

| # | addr | role | A (bit7) | B (bit6) | other |
|---|---|---|---|---|---|
| 0 | D52B | build (§1) | — | — | → 1 |
| 1 | D546 | **Buy / Sell** (cells 8/9) | `$E0=0` (stop ▼), `$83=FF`, blip; `$54==8` → `$68=2`, `JMP C2/DC7B` (buy list); else `$68=3`, `JMP C2/DE98` (sell list) | `JMP C2/82B2` **EXIT, silent** | `$5A&0C` (Up/Down) → `$E0=0`, `JSR C2/E017` → 6. L/R move 8↔9 via graph, blip if `$81≠$54`. `$0D75=01` each frame |
| 2 | D58B | **buy list** | §5.1 → 5, or buzz | `$54=8`, `$04C9=0`, `C2/DD7C`, blip, `C2/D605`, `ED31 $FF:C2B0`, `8385 FBE3`, kill task slot 4 (`LDA #10/JSR 8255`), `$E0=2`, `$68=1`, `JMP C2/E012` → 1 | `JMP C2/D7CF` (cursor §4.4) |
| 3 | D645 | **sell list** | §5.2 → 4, or buzz | `$54=9`, `JMP C2/D5DD` (same tail) → 1 | `JMP C2/D8B2` |
| 4 | D690 | **sell quantity** | `C2/87D5` (bag −qty), `C2/FD97` (gold +total), `LDA #55/JSR EABA` | skip commit | `$0D1F=FF`; else `JMP C2/DA01`. Tail (A or B): `STZ $0D97`, blip, `$68=3`, `STZ $0D9A`, `C2/DFCF`, `C2/DECC`, `JMP C2/DF31` → 3 |
| 5 | D618 | **buy quantity** | `C2/8791` (bag +qty), `C2/FDBB` (gold −total), `C2/DCDA`, `LDA #55/JSR EABA` | skip commit | `$0D1F=FF`; else `JMP C2/DA01`. Tail: blip, `STZ $0D9A`, `$68=2`, `JMP C2/DFCF` → 2 |
| 6 | D6C3 | **party member select** (cells `$0C..`) | blip, `JSR C2/E0A5`, `INC $68` → 7 | → back | `$5A&0C` → back. Back (D6E6): blip, `C2/F5A7`, `E058` with `$00=0,$02=001C`, `INC $0D15`, `$79=$54`, `$54=$80`, `$E0=2`, `$68=1`, `ED31 C2B0`, `8385 FBE3` → 1. L/R = graph, blip |
| 7 | D715 | **gear slot select** (equip sub-mode) | `$54=$81`; `$0FC6==0` → `JMP EACC` (buzz); else blip, `$54=4`, `INC $68` → 8 | blip, restore nav graph `$9990→$9380` (0x48 B, MVN), `E072` (`$02=0004`), `$54=$71+0C`, `DEC $68` → 6, `$0D13=2F`, `INC $0D0B`, kill slots 4+6 (`LDA #50`), `8385 FBE3` | `JSR C2/E108` (L/R member switch), `JSR C2/E0F1` (slot label + candidate list), blip if moved |
| 8 | D778 | **gear candidate list** | EQUIP COMMIT (§8) | tail only | `C2/9F05` (list cursor), `C2/E162` (preview), blip if moved. Tail: blip, `C2/8820`, `STZ $04C9`, `C2/E18A`, `DEC $68` → 7, `$54=$0077` |

Measured transitions (fixture, `bs_a`, `$00BE`): 1@f48, 2@f101, 5@f143, 2@f185, 1@f221, 1(`$54` 08→09)@f251,
3@f281, 4@f321, 3@f361, 1@f401, exit f430→RTL f438. Equip sub-mode (`bs_d`, §10): 6@f61 (`$54=0C`),
`$54=0D`@f91 (Right), `0C`@f121, 7@f152 (`$71=0`, `$54=0`), `$54/$0077=1`@f191, `=2`@f221 (`$0FC6=01`),
8@f251 (`$54=04`), commit+7@f291 (`$54=02`), 6@f331 (`$54=0C`), 1@f371 (`$54=08`), exit.

---

## 3. Init `C2/DACA` (M=8/X=16)

`$79=0C` (party cursor), `$54=08` (Buy); `C2/984A` (OAM shadow `$0700`=E000.., `$0900`=AAAA..); `ED31 $FF:C213`
(windows + string `$3C` + nav graph, §9); `C2/DBE8`; `$1040=$1042=0` (16-bit STZ → `$1040..$1043`);
nav-graph backup `MVN $9380→$9990` (0x48 B); **item list `MVN $CC:[$CC:2C6D+2*$71] → $7E:1000` (0x20 B)**;
`C2/DCBE`; `STZ $0D77`; `C2/E012` (`LDA $51 / JMP F2F3` = blank description); FF:CBAC→`$94C0`,`$94C8`,
FF:9C70→`$9560`,`$9568` (8 B each); `C2/A0E7` (Y=`$9500`); `$5D42..$5D57 = 61FF` ×11 (BG3 OPT row, §9.3);
`JSL FF/F548`; `C2/DC1E`; `C2/F643`; `STZ $04C9`; `C2/DD7C`; `STZ $04CA`; `INC $0D15`; `C2/86DD`;
**`C2/DB9F` (price table)**; `8385` records `FC3E, FBCE, FBF8, $C2:DB98`; `$0D13=2F`.

* `C2/DBE8`: `$104D[]` = the non-negative entries of `$0D5F[0..8]` (packed roster charIds), `FF`-terminated,
  `$1055` = count. Nav wrap: `$93CC` (cell 12 Left) `= $85-1+0C`; `$93CD + 6*($85-1)` (last member's Right) `=0C`.
  Measured: cells 12/13 = `08 AB 0C 0C 0D 0D` / `28 AB 0D 0D 0C 0C` (ROM source has `…12 0D`/`…0C 0E`).
* `C2/DCBE`: `$104A` = index of the first `00` in `$1000[]` (= item count), then `$1000[count..$1F] = 0`.
* `$0D5F`/`$85`/`$73` semantics as in menu2_equip_change.md (`$85` = list length used for the party cells).

---

## 4. The BUY list

### 4.1 Price table `C2/DB9F` → `$7E:6E00[id*2]`, only for the ids in `$1000[]`

```
M   = word $7F:01CD          ; C2/DBA2 LDA $7F01CD / STA $04 ; $02 = 2 (multiplier bytes)
P   = word $7E:7800[id*2]    ; = word $CC:0001+X, the item record's price (built at menu open, C2/98DD)
Q   = P * M                  ; 32-bit, C2/FD58 (16x16 via $4202/$4203 into $3E..$41)
if (Q >> 24) != 0      -> price = 65000 ($FDE8)              ; C2/DBC5 LDA $41 / AND #FF / BNE
else price = (Q >> 8) + ((Q & 0xFF) != 0)   ; ceil(Q/256): INC $3F is a 16-bit INC
     if price == 0 (16-bit) -> price = 1                      ; C2/DBDC BNE / INC A
```
Quirk: `INC $3F` wraps `FFFF→0000` when `Q>>8 == FFFF` and `Q&FF != 0` → price 1 (reproduce verbatim).
`$7F:01CD` = `0x0100` from the new-game script (`4B CD 01 7F 00 01`, boot_loader.md, scene 0 @0x00A2); every
game state in `saves/` + `build/_sp/` holds `0100` (105 states; the 13 attract states hold `0000`). No code
store to `$7F01CD` exists (byte scan: only C2/DBA2 reads it; any other writer would be an event op on the
`$7F0000` page — none located). NB `$7F01CE` (high byte, =01) is NOT the save-point flag: C2/98AC reads the
WORD `$7F01CE`, whose bit15 = `$7F01CF` bit7.

Oracle (PEEK_POKE on the fixture; `$6EB8` = Karate Gi 300, `$6EFA` = BronzeHelm 200):

| `$7F01CD` word | Karate Gi | BronzeHelm | formula |
|---|---|---|---|
| 0100 (real) | 300 | 200 | P |
| 00AB | 201 (`C9`) | 134 (`86`) | ceil(51300/256), ceil(34200/256) |
| 0200 | 600 | 400 | |
| 0001 | 2 | 1 | ceil(1.17), ceil(0.78) |
| 0000 | 1 | 1 | the `==0 → 1` rule |
| FFFF | 65000 (`FDE8`) | 51200 (`C800`) | `Q>>24`=1 → cap; 13107000>>8 = 51199.2 → 51200 |

### 4.2 Owned counts `C2/DCDA` → `$1020[k]`
For each `$1000[k]` (until 0): scan bag slots X=0..255 for `$2400,X == id`; on a hit, `$2500,X != 0` →
`$1020[k] = count`; if the hit has count 0 the scan continues — **but A now holds 0**, so it then matches
EMPTY slots (`$2400==0`) instead of the id (ROM quirk: a zero-count ghost of the id before its real stack
hides the real stack). No hit → `$1020[k]=0`. Run at list entry (C2/DCB0) and after a purchase (D630).

### 4.3 Row drawing
`C2/DD02`: rows r=0..7, `$22=r`, `$61 = $2F5C + $80*r` (BG1 buffer, row 5+2r, col 14) until `$61>=$335C`.
`C2/DD20`: `$04C9 = $1000[$1041+r]`, `$1044 = $6E00[id*2]`, `C2/DD56`: id≠0 → item name (`C2/F2DC`: 11 B from
`$CC:0B5E+11*id` via `$7D00`) then `F114` fmt `$42` (2 bytes, 5 digits) = price; id==0 → 16 blanks
(`EF65` Y=`$9B76`, A=`7E10`). Sell rows use the same `DD56` with `$1044` = sell price (§6).

### 4.4 Cursor / scroll model (buy: `$1040` row, `$1041` scroll, `$104A` count; sell: `$1042/$1043/$1049`)
* Nav cells 0..7 = rows (`x=60`, `y = 33 + 0C*r`); Up/Down only; Left/Right map to self (graph, §9.4).
* Entry `C2/DC7B`: `$0D78=FF`, `$54=0`, `$0D92=2AF0`, `ED31 C2C1` (label "  Buy    "); **`$0D88` (config
  "Save Menu Cursor") == 0 → `$1040=$1041=0`**; `$54=$1040`, `$0D95=$1041`; `DD02`; `C2/DD40` (`$00=8`,
  `$0DDB=0`, `$4204=5400`, `JSR EA81` with Y=count → `$0DD8=8` page, `$0DDC=min(n,8)-1` last cell,
  `$57=$0DDA=max(n-8,0)` scroll range, `$0D98` = scrollbar step); `DCDA`; `C2/8B93`; `8385 FBE3`.
  Sell entry `C2/DE98` identical with `ED31 C2CB` ("  Sell   "), `C2/DECC`, `C2/DF31`.
* Per frame `C2/D7CF`: `$0DD9=$1041`; `C2/93BF` (L = +page, R = −page, clamped; a clamp puts `$54` on
  `$0DDB`/`$0DDC`); moved → `$1041=$0DD9`, `DD02`, 1 yield. Else `C2/EA53`; edge (N) → `C2/D82B`:
  refuse unless `$54+$1041-1 < $104A-2` (i.e. a row exists beyond the edge); then `$9696=9083`,
  `$9698=0025`, `$0DAA=9`, `C2/EB9B` pixel slide, `$1041 ±1` (`$5A&04` = Down → +1), redraw the exposed row
  (`$61=$335C`, `DD20`), blip, `C2/D995` tilemap shift (16/14 row-pair MVN/MVP), `$0D11=0`, `$9694=9710`,
  `$0D13|=80`.
* `C2/D7F1` (every frame): `$1040=$54`; `X = $54+$1041 → $0D26`; `$04CA = $1020[X]` (owned),
  `$04C9 = $1000[X]`; `C2/F2F3` (description); **`$0D26 ≠ $83` → `$83=$0D26`, `C2/DD7C`** (Owned/Equipped
  box + party panel, §7); blip if `$54≠$81`; `8385 FBE3`.

Measured (shop forced to `$09` = 16 items via PEEK_POKE `7F2866=89`, `bs_e` §10): `$0DD8..DC = 08 00 08 00 07`,
`$57=08`; Down×7 → rows 1..7 (f131..f311); Down at row 7 → `$1041` 0→1 (f341; `$0DD9` catches up f345 =
4-frame slide), →2 (f371); **L → scroll 8** (f401, clamp to `$0DDA`); L again: no change; **R → 0** (f461, row
stays 7); R again → row 0 scroll 0 (f491, clamp `$54=$0DDB`); Up at row 0/scroll 0: nothing.

---

## 5. Quantity (`$04C9` id, `$04CA` = max+1, `$04CB` qty, `$04CC` class, `$1044..46` total, `$1047` unit)

### 5.1 Buy (substate 2 A, C2/D598)
```
$54 = $1040
room = 0x62 - ($04CA_owned + $0D82_equipped)  ; borrow -> BUZZ (C2/D5D6 JMP EACC)
$04CA = room + 1                              ; = 99 - owned - equipped
C2/FCB2: $04CB=1; $1046=0; $1044=$1047=$04=$6E00[id*2]; $00..$02 = gold $2C53..55; C2/FD1D
         (24-bit / 16-bit restoring divide, 24 iterations -> quotient $36..$38)
if quotient >= 256 -> keep ; elif quotient == 0 -> BUZZ ; elif quotient < $04CA -> $04CA = quotient
blip; INC $04CA (now max+1); DEC $0D9A (hide scrollbar); $68 = 5; JMP C2/DF76 (open the window)
```
**max = min(99 − owned − equipped, gold / unitPrice)**, buzz when either is 0.

### 5.2 Sell (substate 3 A, C2/D652)
```
$54 = $1042 ; $04C9 == 0 -> BUZZ ; $04CA (stack count) == 0 -> BUZZ
C2/FCE1: $04CB=1; $1046=0; unit = ($7800[id*2] >> 1) + carry + $51   ; = ceil(base/2), $51 = 0000
         $1044=$1047=$04=unit; $00..$02 = 0x98967F - gold (9,999,999 - gold); borrow -> skip divide (stale $36..38)
same quotient rule against $04CA ; INC $04CA ; DEC $0D9A ; $68 = 4 ; JMP C2/DF76
```
**max = min(owned, (9,999,999 − gold) / sellPrice)**.

### 5.3 The picker `C2/DA01` (substates 4/5; `$0D1F=FF` blocks the nav mover)
`$04=$1047` (unit), `$06=0`, `$02=3`; `$0D1E` (0 R,1 L,2 D,3 U; `80`=none) → `JSR ($DA35,X)`,
`C2/DA35` = `3D DA 50 DA 83 DA 64 DA`:

| dir | handler | rule (bound = `$04CA`) | total update |
|---|---|---|---|
| Right | DA3D | `q+1 < bound` → q+1 else no-op | +1·unit |
| Left | DA50 | `q-1 ≠ 0` and `< bound` → q−1 else no-op | −1·unit |
| Up | DA64 | `q+10 < bound` → q+10 else **q = bound−1** (delta = bound−1−q) | +delta·unit |
| Down | DA83 | `q−10` (8-bit) ∈ [1,bound) → q−10 else **q = 1** (delta = q−1) | −delta·unit |

Total: `C2/FD68` = delta × 24-bit (`$04..$06`, negated + `$06=FF` for decrements) → `$1044 += $3E`
(16-bit), `$1046 += $40 + C`. Then `C2/E001` (redraw string `$37`, `8385 FBEA`) and blip if `$04CB≠$83`
(`$83` = pre-move qty, C2/DA1E).

Oracle, BronzeHelm with gold 9999 (`bs_b` §10, PEEK_POKE `2C53=0F,2C54=27,2C55=00`): open f165 q=1
`$04CA=32` (max 49 = 9999/200) total 200; Up→11 (2200) f187; Up→21 f209; Right→22 f231; Down→12 f253;
Left→11 f275; Up→21,31,41 → **49** (f363, total `2648`=9800); Up at 49: no row; Down→39,29,19,9 → **1**
(f495); Down at 1: nothing; Left at 1: nothing; Up→11 (f539); A f561: gold `270F→1E77` (9999−2200),
`$2401=7D $2501=0B`.

### 5.4 Commit and close
* Buy (D62A): `C2/8791` add (`$04C9`,`$04CB`; stack cap 99; sentinels `5A/7B/94` refused; FULL → A=FF,
  **return value ignored**), `C2/FDBB` gold −= `$1044..46` (borrow on the top byte → `$2C55=98` then
  `C2/FDDF` clamp → 9,999,999; unreachable because q ≤ gold/unit), `C2/DCDA`, SFX `$55`.
  **Full-bag quirk measured**: 256 slots of `BD` → A on Karate Gi: gold 400→100 (C2:FDD3), bag unchanged,
  `$1E00=18` shipped.
* Sell (D6A2): `C2/87D5` remove (count 0 → `STZ $2400,X`), `C2/FD97` gold += total, carry out of the top
  byte → `$2C55=98`; `C2/FDDF` clamps gold to **≤ 0x98967F**. SFX `$55`.
* `C2/DFCF` (close): `$54=$7F` (saved list cursor), zero `$5D5A..$5D77` (OPT, §9.3), **`STA $83` with A=FFFF
  after the MVN → `$83=$84=FF`** (forces the next `D7F1` to re-run `DD7C`), gold redraw `F114 X=$3046 Y=$2C53
  fmt 63`, `8385 FC3E`, `8385 FBE3`.
* Sell tail `C2/DECC` rebuilds the list, re-runs `DD40`, clamps `$54 ≤ $0DDC` and `$1043 ≤ $57`,
  `$0DD9=$0D95=$1043`.

Caps (oracle, gold 9999): owned 98 (`2501=62`) + Crono wearing Karate Gi (`2628=5C`) → `$0D82=01`,
`$1056[0]=01`, A → **buzz** (`$1E01=01`, substate stays 2). Owned 97 → window with `$04CA=02` (max 1).
Price 600 vs 400 G → buzz. Sell Tonic×5 with gold 9,999,990: `$04CA=02` (9/5=1), Up: none; A → gold
9,999,995, count 4 (f199); A again → buzz (4/5=0).

---

## 6. The SELL list `C2/DECC` → `$5600[]` ids, `$5700[]` counts, `$1049` count

```
$5600[0..5] = 0
for X in 0..255: $5700[Y] = $2500[X]; id = $2400[X]
    skip if id == 0 or id >= 0xF2 or ($7E:7700[id] & 0x04)
    $5600[Y++] = id
$1049 = Y ; $5600[Y] = 0
```
`$7700[id] = $CC:0000+X & 0x1E` (C2/98C0). **Bit 2 = unsellable** — ROM byte0&4 and the live `$7700` list are
identical: `10 3D 42 50 51 52 54 79 7A 8B 90 95..BB D0..F1` (i.e. every accessory id `95..BB`, all key items
`D0..F1`, plus Slasher, both Masamune, Bent Sword/`51 52 54`, Taban Vest/`7A`, Taban Helm, OzziePants).
Measured: Pendant `D6` (`7700=1C`) never appears; Karate Gi does.
Order = bag slot order (no sort). Row price = `ceil($7800[id*2]/2)` (C2/DF51: `LSR / ADC $51`).
Measured: Karate Gi 300 → 150 (shown "150"; sale 100→250 G); item `44` (price 1) → 1 (`$1044=01`, 400→401 G).

---

## 7. Owned / Equipped box and the party panel

### 7.1 `C2/DD7C` (runs on every row change and on init/exit with `$04C9=0`)
1. `$2E84..$2EBB = 2000` (clear), `STZ $3192/$3194/$3254`, `$71=$61=0`.
2. `C2/8881` (class: `id < 5A` 0 weapon, `<7B` 1 armor, `<94` 2 helm, `<BC` 3 accessory, else 4), `C2/8900`.
3. `$0DAD=$04CC`; `$22 = C2/DE1D[class]` = `29 28 27 2A` (**class 4 reads the next byte, `08`**).
4. id≠0: `F114 X=$3192 Y=$04CA fmt 11` (Owned, 2 digits), `F114 X=$3254 Y=$0D82 fmt 01` (Equipped, 1 digit).
5. per member k in `$104D[]`: `C2/8816` (sheet → `$9A90`), `$9B23/24 = $9ACD/CE` (Atk/Def = sheet
   `+3D/+3E`), `$1811+charId*40 = 0`; if `C2/93A8` (equippable) → `$1811.. = 0B`, `$9A90+$22 = id`,
   `C2/916E` (recompute → `$9B23/$9B24`); `C2/DE21` with Y=`$61`; `$61 += 8`.

`C2/8900` (# Equipped): `$00 = C2/894F[class]` = `29 28 27 2A` (**class 4 → `$28953` = `08`**, the PHP of
C2/8953); for k < 7 while `$104D[k]` ≥ 0: `$1056[k]=0`; if `$2600 + charId*50 + $00 == id` → `$1056[k]=1`,
`$0D82++`. (`LDA $29AF/STA $02` there is dead.) **Consumable quirk measured**: shop `$11` (PEEK_POKE
`7F2866=91`) with Crono sheet `+08` (curMP high byte) poked to `BD` → Tonic shows `$0D82=01`,
`$1056[0]=01` (C2:8947). With real MP (< 256) it is always 0 — reproduce the read anyway (it also lowers
the buy room of §5.1).

### 7.2 `C2/DE21` / `C2/DE56` — per member, two rows at BG1 `$3404+8k` (weapon) and `$3444+8k` (armor)
`$1056[k]≠0` → `$2E84,Y = 38` = the glyph **"E"**, which the band scroll (§9.3) shows at the top-left of that
member's panel cell (measured: PEEK_POKE `2401=5C,2501=03,2628=5C`, buy list f125 → "E" over Crono, Owned 3,
Equipped 1, Crono's Defense "21" white = equal). Row 1 compares new `$9B23` vs old `$9ACD` (Attack), row 2 `$9B24`
vs `$9ACE` (Defense):
```
attr = new==old ? 0x21 : new<old ? 0x3D : 0x29        ; C2/DE62..DE6E, stored to both cells' attr bytes
cellA tile = 0x6E + new/10          (attr bit0 -> tile 0x16E + new/10)
cellB tile = new >= 10 ? new % 100  (-> 0x100 + new%100, a TWO-digit glyph)
                       : 0x64 + new (-> 0x164 + new, a one-digit glyph)
```
(`$4204/$4205` = new, `/10` then `/100` without rewriting the dividend; LakeSnes keeps `divideA`.)
Measured f125 (Karate Gi highlighted): Crono `$3404.. = 2020 216E 216C` ("8", white), `$3444.. = 002E 2970 2915`
(**"21"** cyan, tile `0x115` is the glyph "21"); Marle `… 296F 2913` ("19"); tiles `0x16F/0x170` carry the
tens digit's left-overhang pixels (rendered VRAM: `0x16F` rows 1,2,6,7 col 7). Palette 2 = better (cyan),
7 = worse, 0 = equal. Current values were 16/14 (screen f60); Karate Gi Defense 10 vs Hide Tunic.

### 7.3 Party sprites `C2/DC1E` (init)
`C2/F588`, `$0D75=07`; per member k: `Y = charId*40` (`C2/F626`), `$1800,Y = charId`, `$180F,Y = C2/DC74[k]`
(`20 40 60 80 A0 C0 E0`), `$180E,Y = C0`; BG1 `$3404+8k = charId|20` attr `20` (the char's weapon icon),
`$3444+8k = 2E`. `$1811,Y` = anim: **0B while the highlighted item is equippable by that member, else 00**
(measured f60 00/00, f125 0B/0B). `C2/D605` zeroes `$1811` for charIds 0..6 on leaving a list.

### 7.4 The ▼ arrow (task slot 6, `$E0` = its status byte)
`C2/8A98` spawns `C2/8AB2` in slot 6 (`X=30`); per frame `C2/8B69` blinks (`$0DA3 &= ~$0DA6(F8)`, DEC, on
underflow `$0DA0 ^= 1`; drawn when bit0 = 0 → 8 frames on / 8 off) and writes OAM slot 29 (`$0774/$0776`) at
nav cell `$0D9C=0B` → `(x+2, y−1)` = `(5E,2A)`, tile/attr `$0DA1 = 22/B0`. Measured OAM 29 at f60: x=94 y=42
tile 22 attr B0, large. `STZ $E0` stops it (lists, party mode), `LDA #02 / STA $E0` restarts it (back to 1).

---

## 8. Equip sub-mode (substates 6-8)

* Enter (1 → 6) `C2/E017`: `$80=$54`, `$68=6`, `$54=$79`, `$02=1004`; for roster indices `$85..$1055-1`
  → `JSL FF/F628`, `E058` (dim 4×2 cells: `C2/EC93` sets attr = `(attr&0F)|$03`); `ED31 C2D5` (label " Equip   "
  + string `$37`), `8385 FBE3`.
* 6 → 7 `C2/E0A5`: clear `$0D4D..$0D53`; `$71=$54−0C`; `C2/8820`; `ED31 C309` (string `$03` "PWR./SPD./…",
  **nav graph replaced from `$FF:B6B0`** — hence the `$9380↔$9990` backup), `C2/A6F0 X=3664`, 1 yield,
  `$54=0`, `C2/E0F1` (`$0077=$54`, `A3ED X=3624`, `$0D92=2A68`, `C2/A463` = candidate list `C2/8953` for slot
  `$0077`, `$0D26 = C2/A4AB[$0077]`), `$0D13=55`, `$0D0B=0`, `DEC $0D78`, `F2F3(0)`, `E072` (`$02=1004`:
  dim all members except `$71`).
* 7: `C2/E108` = L/R cycles `$71` over `0..$85−1` (wrap), blip, reload, `ED31 C309`, `A6F0`, `E072`, `E150`, yield.
* 8: `C2/9F05` cursor, `C2/E162` preview (`$04C9 = $0F00[$0FC8+$54−4]`, `$9A90+$0D26 = id`, `C2/E18A`).
  `C2/E18A`: `916E`; accessory `A8` doubles `$9B21`; 7 stat compares over `C2/E1DC` = `36 38 3B 39 37 3A 3C`
  → `$0D4D[i]` = `00` equal / `08` up / `1C` down; `DE21` for member `$71`; `ED31 C318`; `F2F3($04C9)`.
* **Commit (D792)** = the equip screen's C2/9CC1 minus the weapon side effect: `$04CB=1`; `Y=$0D26`;
  old `($6F),Y` → `C2/8791`; new `$9A90,Y` → `STA ($6F),Y` → `C2/87D5`; `LDA #B5/JSR EABA`; `C2/9137`;
  `A6F0 X=3664`. **No `JSR C2/9CFC`** (the `$7F:00B4` clear is not done here).
  Measured (`bs_d`, PEEK_POKE `2401=5C,2501=01`): f291 `$2628` 5B→5C (C2:D7AA), `$2400.. = D6 00 5B`
  (Hide Tunic added to slot 2 first, Karate Gi removed from slot 1), `$1E00=18` shipped.

---

## 9. Display lists and strings

### 9.1 ED31 lists (`$FF:xxxx`; cmd len table `$FF:BD18`; `08`=set base `$61`, `06`=string `[id16, x, y, flags]`
at `$61 + y*40 + x*2`, `0E`=copy `[ptr]` (count byte then data) to `$61`)

| list | used by | content |
|---|---|---|
| C213 | init | base `$2E00`; `00[00 30 00 00]`; string `$3C` @(3,6); base `$3E00` (BG2) + 9 window frames `04`/`02` (not decoded); base `$4E00` + 6 `0C/0E` blocks; **base `$9380` ← `$FF:B63D` (0x72 B = 19 nav cells)**; base `$9680` ← `$FF:BCD4`; base `$9D99` ← `$FF:BC52` |
| C2B0 | back to 1 | `02[0E 03 0E 04 10 10]`; string `$36` @(3,6) |
| C2C1 / C2CB | buy / sell entry | string `$34` / `$35` @(3,6) |
| C2D5 | party mode | string `$33` @(3,6); string `$37` @(0,0) |
| C2DE | `C2/E001` | string `$37` @(0,0) |
| C2E8 / C2F5 / C302 | qty window by class (`C2/DFC5` = `C2E8 C2F5 C2F5 C302 C302`) | `$39` "Attack: " @(17,61) + `$3B` @(26,61) / `$3A` "Defense:" + `$3B` / `$38` 16 blanks @(14,61) |
| C309 / C318 | equip sub-mode | base `$33C2` + `$03` @(15,18), nav ← `$FF:B6B0`, `$05` @(22,6) / `$05` only |

### 9.2 Strings (`$FF:C457` word table; control codes: `02` goto `$61`+word, `03` number `[addr16 fmt bank]`,
`04` include `[addr16 bank]`, `08 n` blanks, `0A` attr; digits = tile `D4+d`, leading zeros blank unless fmt bit7)

| id | addr | text |
|---|---|---|
| $33 | FF:C92D | `" Equip   "` |
| $34 | FF:C941 | `"  Buy    "` |
| $35 | FF:C937 | `"  Sell   "` |
| $36 | FF:C94B | `"Buy  Sell"` |
| $37 | FF:C955 | goto+0DF6 `{$7E:04CB fmt11}` goto+0EAA `{$7E:1044 fmt63}` `G` |
| $38 | FF:C967 | 16 blanks |
| $39 | FF:C971 | `"Attack: "` (0x2D = the colon glyph) |
| $3A | FF:C97A | `"Defense:"` |
| $3B | FF:C8BE | `{$7E:0D4D fmt21}` (3 digits; `$0D4D = $7E:7C00[id]`, set by C2/DF7C) |
| $3C | FF:C983 | `{FF:C94B}` goto+0246 `{$7E:2C53 fmt63}` `G` goto+0344 `EE EF "Owned"` goto+0404 `EE EF "Equipped"` (glyph `EE` renders "#") |

`F114` fmt: bits0-1 = byte count, digits = ((fmt^80)>>4 & 7)+1: `11`→2, `01`→1, `42`→5, `63`→7, `21`→3.

### 9.3 BG1 buffer map (`$2E00` = BG1 map 32×64, VRAM `$5800`; records `C2:FBCE/FBE3/FBEA/FBF1`)
Coordinates are BUFFER (col,row), not screen: the menu's BG1 vscroll HDMA (ch1 `$210E`, table `$7E:9080`,
12-line bands with +4 steps = the 12-px list pitch) remaps rows per band — e.g. `$2E84` (row 2) is displayed
inside the party panel. Label (3,6); gold `$3046` (3,9); "# Owned" (2,13), count `$3192` (9,14); "# Equipped" (2,16), count `$3254`
(10,17); list rows `$2F5C+$80r` (14, 5+2r); party panel rows 24/25 (`$3404`/`$3444`); qty window: name
`$3BDE` (15,55), qty `$3BF6` (27,55), total `$3CAA` (21,58), stat (17/26,61). The window is brought on
screen by **BG mode 2 offset-per-tile** (HDMA ch5 `$2105` = `02` on lines 40..151, BG3 vscroll 480 = OPT rows
60/61 = `$5D00` buffer → VRAM `$6F80` via `C2:FC3E`): `C2/DF76` fills `$5D5A..$5D77` (cols 13..27) with
`216F` (BG1 V-offset 0x16F), `C2/DFCF` zeroes them. The BG1 vscroll HDMA table `$7E:9080` is identical in
the list and qty frames (measured f125/f170).

### 9.4 Nav graph (`$FF:B63E`, measured identical in `$9380` except the two wrap patches)
cells 0-7 rows (`60, 33+0C·r`, up r−1 (0: `80` edge), dn r+1 (7: `87` edge), lf/rt self); 8 Buy `(08,2F)`
lf/rt 9; 9 Sell `(30,2F)` lf/rt 8; 10 qty `(68,47)`; 11 ▼ `(5C,2B)`; 12..18 members `(08+20k, AB)` lf/rt k∓1.

---

## 10. Oracle measurements (commands; all `--from build/_sp/shop90_t0.state`)

`bs_a` = `0:10,100:2,0:88,100:2,0:40,100:2,0:40,100:2,0:34,1:2,0:28,80:2,0:28,100:2,0:38,100:2,0:38,100:2,0:38,1:2,0:28,1:2,0:90`
(A f10 talk, A f100 Buy, A f142 Karate Gi, A f184 confirm, B f220, Right f250, A f280 Sell, A f320, A f360
confirm, B f400, B f430 exit).
`ct.exe --monitor 7E0067:1,7E00BE:1,7E0054:1,7E2C53:3,7E04C9:4,7E1040:4,7E1044:3,7E1047:4,7E2400:4,7E2500:4,7E5600:4,7E5700:4,7E0D82:1,7E1020:2,7E1056:2,7E0A00:1 --btnscript bs_a --frames 530 --diff [--writer]`

| f | `$00BE` | `$54` | `$2C53` | `$2400/$2500` | other |
|---|---|---|---|---|---|
| 42 | (00) | 00 | 900100 | D6 00 / 01 00 | `$0067=0C` (C2:807F) |
| 48 | 01 | 08 | | | |
| 101 | 02 | 00 | | | f102 `$04C9..CC = 5C 00 00 01` |
| 143 | 05 | 0A | | | `$04C9..CC = 5C 02 01 01`, `$1044=012C` (C2:FCCB), `$04CA` (C2:D5CC) |
| 185 | 02 | 00 | **640000** (C2:FDD3) | **D6 5C / 01 01** (C2:87BD, C2:87D0) | `$1E00=18 00 80` (C2:EB0B), `$1020=01` |
| 221 | 01 | 08 (C2:D5DD) | | | |
| 251 | 01 | 09 (C2:EA4A) | | | |
| 281 | 03 | 00 | | | `$5600=5C 00`, `$5700=01`, `$1049=01` |
| 321 | 04 | 0A | | | `$04CA=02`, `$1044=0096` (C2:FCFD) |
| 361 | 03 | 00 | **FA0000** (C2:FDAF) | **D6 00 / 01 00** (C2:87F3, C2:87D0) | `$1E00=18`, sell list empty |
| 401 | 01 | 09 (C2:D68D) | | | |
| 438/439 | | | | | RTL C0/0D1C / `$0A00=00` (C0:0914) |

SFX verified by `CPUTRACE_FROM=C2D62A` over the f184 commit: `C2/D633 LDA #55 → EABA → EB03`: `$1E00=18`,
`$1E01=55`, `$0D76=0 → 03`, `$1E02=80`, `JSL C7:0004`; the following `C2/EAC2` stages `$1E01=00` but is
throttled (`BNE` at EB0E) — so a monitor shows `$1E01=00` after a purchase although `$55` was shipped.
Pictures (peek): f60 top ("Buy Sell▼", 400G, members idle 8/16, 8/14), f125 buy list, f170 qty window
("Karate Gi 1 / 300G / Defense: 10"), f205 after purchase (100G, Owned 1), f300 sell list (150), f340.
Other scripts: `bs_b` (§5.3), `bs_d` = `0:10,100:2,0:48,20:2,0:28,80:2,0:28,40:2,0:28,100:2,0:38,20:2,0:28,20:2,0:28,100:2,0:38,100:2,0:38,1:2,0:38,1:2,0:38,1:2,0:40`
(§8), `bs_e` (§4.4), `bs_c` (§5.4 sell caps).

---

## 11. What the C port needs

**Data (ROM):** shop lists `$CC:2C6D` (24 ptrs, 0-terminated, ≤ 32 B copied); item records via
`$FF:CF0E/CF18` (price = word +1, byte0 & 4 = unsellable, `$7A00` equip mask = +3, `$7C00` power byte);
names `$CC:0B5E + 11·id`; strings `$FF:C457` ids `$33..$3C`; display lists §9.1; nav graph `$FF:B63E`
(0x72 B) + the equip-mode graph `$FF:B6B0`; tables `C2/DA35` (picker), `C2/DE1D` + `C2/894F` (class →
sheet offset, 5th entry = the following opcode byte `08`), `C2/DC74` (sprite x), `C2/DFC5`, `C2/E1DC`.

**Formulas:** buy price = §4.1 (ceil(P·M/256), ≥1, >2^24 → 65000); sell price = ceil(P/2) from the RAW price;
buy max = min(99 − owned − equipped, ⌊gold/price⌋) (buzz if 0); sell max = min(count, ⌊(9999999−gold)/sell⌋);
picker ±1 / ±10 with clamp-to-bound / clamp-to-1 (§5.3); total kept incrementally mod 2^24; gold clamp
≤ 9,999,999 on both add and subtract-underflow.

**RAM written:** `$1000/$104A` list, `$6E00` prices, `$1020` owned, `$5600/$5700/$1049` sell list,
`$1040..$1043` cursors (reset unless `$0D88`), `$04C9..$04CC`, `$1044..$1047`, `$0D82/$1056`, `$9A90`/`$9B23`
previews, `$2400/$2500` bag (ADD before/independent of gold; bag-full ignored), `$2C53..55` gold,
`$1E00..02` SFX (`$55` trade, `$B5` equip, `00` blip, `01` buzz), sheet `+27..2A` on an equip commit (+`9137`
derived block), `$5D5A..$5D77` OPT window, `$1811+40·c` anims, OAM slot 29 ▼, `$9380/$9990` nav graph.

**NOT derived (open):**
1. The ED31 window-frame opcodes `00/02/04/0C` (C213's frames on BG2 `$3E00`, `$4E00` blocks) — geometry
   not decoded; take the pixels from the oracle or decode C2/ED77, EF05, EDAA, F337.
2. The source of the BG1 narrow-digit glyph tiles `0x100..0x187` (whether `JSL FF/F548` at init builds them
   or the menu font upload does) — only the tile-index formula and the rendered result are measured.
3. `C2/D995`/`C2/EB9B` scroll animation beyond "4 frames per row step" (measured once), and `C2/8B93`.
4. `FF/F628` (roster → panel index) and `C2/F588`/`F643`/`86DD`/`A0E7`/`A6F0` internals (sprite/portrait setup).
5. Any event-script writer of `$7F:01CD` other than the new-game `4B CD 01 7F 00 01` — none located (compressed
   scripts were not scanned).
6. `$85` vs `$1055` semantics (active vs roster count) — the fixture has 2 = 2, so the reserve-dimming loop
   of `C2/E017` was not exercised.
7. `src/ct_vm.c:3100` comments mode 6 (screen `$0B`) as "the shop screen"; it is the save-point save — noted
   only, not changed.

---

## 12. The port (2026-09-24) -- what is in, what was measured on the way, what is open

**In** (src/menu.c `CtMenuShop*`, src/field.c's menu host, src/ct_vm.c's op 0xC8 arm): the VM parks `C8 80..BF` like
the naming screen (`CtSceneShop`, released at the end of the resume fade = C0/0D33's handshake); the host opens the
menu with screen $0C (no `$0407 = FF` on this path); substates 0-5 as above (list, prices, owned/equipped, the
per-member Attack/Defense preview, the picker, both commits, the sell list, the sounds); the whole picture.
Checks: `shop-buy-sell-ram-vs-oracle` (RAM + sounds, 6 mutations) and `shop-screens-vs-oracle`
(`tools/shop_screen_cmp.py`: eleven whole frames at 0 px against the oracle, 7 mutations).

**Measured while porting** (none of it was in §1-11):
* `$7A00` is ZEROED for ids BC-F1 right after C2/98B7's loop (C2/9906 `STZ $7ABC` + a 0x34-byte MVN), so a
  consumable is equippable by no one: the Tonic leaves `$1811` at 00 (oracle, shop $11). Reading +3 of the 3-byte
  record (the next record's byte 0) made Crono cheer over a Tonic.
* The screen's BG1 is composed per scanline (HDMA_TRACE_BG on the three states, identical): V scroll bands L1-48 207,
  49-60 499, 61-72 503, 73-84 507, 85-96 -1, 97-108 3, 109-120 7, 121-132 11, 133-144 15, 145-160 63, 161-176 367,
  177+ -1; H scroll 252 from L193; mode 2 on L41-152 with offset-per-tile entry e applying to screen column e+1:
  entries 1-11 = 61FF (C2/DB49: STA + a 20-byte MVN) and 13-28 = 216F under the quantity window (C2/DF8C: STA + a
  30-byte MVN -- 16 words, so column 29 is covered and the list's last price digit is hidden). OBJ prio 2 (the party)
  sits between BG1 prio 0 (the shield cells, attr 00) and prio 1 (every other text cell).
* Every string comes out of `$FF:C457` ($33-$3C, interpreted: 02 goto / 03 number / 04 include / 08 blanks); C2/F114
  blanks leading zeros with $FF and always prints the last digit.
* The description (C2/F2F3 -> C2/FA49) is a VWF render into BG3 tiles 0x101.. at buffer $4E84, palette 4
  (231/24/49): pixel-exact with the load screen's dialogue VWF at (24,20).
* (Corrected in section 13: the pose shows on the frame of its tick -- the lag below was the old alignment's.) The menu
  sprite machine (shared by every menu screen): a NEW pose's tiles go out through C2/F75C's NMI DMA queue,
  so the picture shows it one frame after the OAM of the same tick (shop: Crono 6C -> 6D at logic f179, shown from
  f181; Form: Lucca's walk at logic f117/124/131, shown from f119/126/133); and at most one block takes a new pose
  per frame -- each PC block costs (block+2 & 0x0F) << 5 = 0x20 against `$0D75` 1..7 (Marle starts 0x0B one frame
  after Crono, f165 vs f164).
* The hand's and the arrow's OBJ mid grey is (13,12,12) -> 107,99,99 through the framebuffer's (v<<3)|(v>>2); both
  shared assets had it baked as 106,98,98.

**Open:**
1. ~~The equip sub-mode (substates 6-8)~~ -- PORTED 2026-09-24, section 13.
2. ~~The menu SHELL's own timing~~ -- PORTED 2026-09-24 (menu_shell_timing.md section 9): the port now opens and
   closes on the logical oracle's frames; tools/shop_screen_cmp.py's OPEN is 1 (the real-time oracle's lag frame).
3. The list's one-row scroll animation (C2/EB9B/D995, ~4 frames) and the one-frame yield after a page (C2/D7E7):
   the port scrolls instantly. No capture here has a list longer than 8.
4. The menu reads a press one frame after the pad latch on the cartridge; the port's host on the press frame (the
   same for every menu screen).
5. The narrow panel digits (tiles 0x100-0x187) and the window chrome are CAPTURED (menu_shop.h), not built.

---

## 13. The equip sub-mode, ported (2026-09-24) -- and the three mechanisms its picture needed

Check `shop-equip-mode-vs-oracle` (tools/shop_equip_cmp.py): the fair merchant with a Power Ring and a Third Eye in the
bag, a drive through every substate-6/7/8 path (party row L/R, A, member switch R/L, Up to the accessory row, the list,
Down/Up over the two candidates, the commit, Down, B, B, the buy list, B, B = close); a thumbnail of every frame on both
sides; f50-f501 = 0 of 452 frames differ; the bag and Crono's sheet after the close = 0 bytes differ. The logic is §8
verbatim (src/menu.c: mShopEquipEnter / mShopEquipOpen1+2 / mShopEqList / mShopPreview / mShopCompare /
mShopEquipCommit / mShopEquipBack8 / mShopMemberSwitch / mShopEqCursor; substates 6/7/8 in mShopFrameSub). Measured
on the way (none of it was in §1-12):

* **`$0D13` is the NMI's HDMA enable mask** (C2/8442 `LDA $13 / AND #7F / STA $420C`; bit7 = the queued MVN of
  menu2_save_rest.md 3.3). $2F = channels 0/1/2/3/5, $55 = 0/2/4/6. C2:C213 installs SIX tables from the ROM (its `0C`
  command sets a channel's B-bus register and mode, `0E` copies a count-prefixed table): ch1 BG1 V `$FF:B8A4`, ch2
  BG1 H `$FF:B87D`, ch3 BG3 V `$FF:B8CA`, ch5 $2105 `$FF:BB33` (the main set), ch4 BG1 V `$FF:B9CA`, ch6 BG3 V
  `$FF:B9F0` (the equip set). The port now reads every band from these tables (shopHdma), the main set included.
* **BG2 V = `$0D0A | $0D0B << 8`**: C2/85D6 copies `$0D04-$0D0F` to $210D-$2112 every NMI. `$0D0A` = FF; `$0D0B` = FF
  on the open, 00 at C2/E0DC (V 255: map rows 32-59), `INC` at C2/D76A (01: V 511 = -1). C2:C213 draws BOTH window
  layouts into $3E00 at init; the second (one left box, the right box from x 120) is menu_shop.h's
  ctMenuChromeShopEquipTM (map rows 32-59, the same capture as the first).
* **A buffer write shows only through a C2/8385 record** queued the same frame (records: FBCE rows 0-47, FBE3 rows 2-26,
  FBF1 rows 24-47, FC1B BG3 rows 0-25). C2/D792 + C2/D7BB queue none, so the commit's gear/stat/panel redraw appears
  the frame after (with the next substate-7 frame's FBF1); C2/E108's palettes (FF/F628, INC $0D15) change a frame
  before its text. The port keeps the buffer the ROM reads back (CtMenuShop.b1/b3) and what VRAM holds (v1/v3).
* **The attr state**: C2/EC93 (via C2/E058) = `(attr & 0F) | hi` -- hi 10 dims a member (palette | 4, priority off),
  hi 00 keeps the palette's low bits and clears the priority (the panel cells stay priority 0 after the mode); C2/A3ED
  rewrites the gear rows' attr (00 the row $0077, 1C the others); the string writer's attr byte `$7E` is 0 on every
  equip-mode path (A59A's `$7D = 2000` is undone by its own STZ); BG palettes 0-7 captured (4/6/7 are the dim tones).
* **The description TYPES**: C2/FA49 spawns C2/FA9F in task slot 8, which runs `JSL C2:0009` once per frame -- one
  DECODED character (a dictionary code costs one frame per character it expands to: "Power +6" = 6 codes, 8 frames,
  `$0217` = 1..8 from the E162 frame, `$0215` = 0 on the ninth) -- starting in the frame C2/F2F3 is called. F2F3 redraws
  only on a new id (`$0D77`) or a force (`$0D78` bit7: DC7B, C2/E0DF). The buy and sell lists type the same way.
* **Colours 13-15 of the party's OBJ palettes are the WEAPON's**: C2/F5A7's second loop (C2/F5ED) loads three words from
  `$D1:(4B00 + 6*CD6CEC[sheet +0x29])` per charId; the shop's init never runs it (they stay black), C2/D6E6 does, so
  after the equip mode a pose that shows the weapon shows its colours. FF/F628's dim halves colours 1-12 only. The
  port's party art carries the colour index in its alpha byte for this (field.c's compose, menu.c mBlitSprAnchored).
* The alignment (corrected the same day, memory ct-frame-alignment-p2): both shop tools now run the LOGICAL oracle
  (the real-time one has a lag frame inside C2/DACA, so its "P+2" was the logical P+1), press on the port one frame
  after the oracle's drive, and compare the port's frame P with the oracle's `--peek`/thumbnail P+2 -- the cartridge
  displays frame P's state during P+1. The substate changes land on the same frame numbers on both sides.

* The ▼ task (slot 6) runs AFTER the handler (slot 1) every frame, and from the frame C2/D52B spawns it (P, the fade-in's
  spawn frame): a STZ $E0 in the handler stops that frame's blink step, and the blink's phase starts at P
  (CtMenuShopSpawnFrame).
* The menu sprite machine shows a new pose on the frame of its tick (the "one frame later" of section 12 was the old
  alignment's compensation): the whole drive, the buy list after the mode included, is 0 frames differ.

**Open** (after this section):
1. ~~The menu sprite machine's pose changes~~ -- CLOSED: an alignment artifact (above).
2. C2/9F05's one-row slide (C2/EB9B rewrites the ch6 table $9303 per frame) with more than 8 candidates: the rows move
   at once in the port (as the buy list's, item 3 above).
3. The first frame of a substate drops A/B/L/R (C2/E984 `$0D17`) -- now modelled in CtMenuShopFrame (d17).

## 14. PORTED (2026-09-25): the lists' scrollbar, one-row slide and the page's late rows

`tools/menu_audit_cmp.py shopscroll` (shop $09: `$7F:2866` = 89 on the cartridge, CT_SHOPNO=09 on the port -- the
tool's PORT_ENV; the talk a field A, not shifted) and `shopsell` (fourteen weapons in the bag) are 0 frames from f150.

* **The scrollbar** is the menu's shared task (menu2_equip_change.md section 10): the Buy entry `C2/DC7B` seeds `$0D92`
  = 2AF0, `$0D95` = `$1041`, `C2/DD40` (`$0DDB` 0, 8 a page, `$4204` = 5400 over the count) and spawns it (`C2/DCB3`);
  the Sell entry `C2/DE98` the same with `C2/DECC`'s tail (`C2/DF10`: the window, `$54` <= `$0DDC`, `$1043` <= `$57`,
  `$0DD9 = $0D95 = $1043`). The quantity picker hides it (`DEC $0D9A`, C2/D5CC / C2/D67C) and its close shows it
  (`STZ $0D9A`, C2/D63B / C2/D6B7); B out of a list kills it (C2/D5F5 `LDA #10 / JSR C2/8255`). It is priority 3, behind
  the hand and the ▼ (lower OAM entries) and under BG3's priority-1 text. The arrow reaches x 252, past the chrome, whose
  last columns are transparent: the PPU shows the backdrop there (black), which the port now paints first -- it had kept
  the last frame's pixels.
* **The one-row slide** (`C2/D82B` buy / `C2/D90E` sell): MEASURED with `ct.exe --hdma`, the list is BG1 on channel 1
  (`$210E`): 48 lines at 0CF, then eight bands of 12 from line 48 at 1F3 + 4k -- mid-slide 9 lines at 1F6, bands of 12,
  3 lines at 016 before line 144. C2/EB9B, `$1041` +-1, `$0D95` = the new scroll, the incoming row drawn at `$335C` (slot
  8, C2/DD20), FBE3, the blip, then the loop (`C2/D880`: `$0DAB` only) and on the fourth frame `C2/D995` (the buffer a
  slot), `$0D22` := 0, `$9694 = $9710` -- and the handler goes on into `C2/D7F1` (the row, the description, the box).
  The port's `shopBg1V` reads the moved bands (slot 8 past either end) and the buffer holds the old window plus slot 8.
* **The page's rows are a frame late.** `C2/93BF` moved -> `$1041 = $0DD9`, `C2/DD02` redraws the rows in the buffer and
  `C2/D7E7` yields; the upload (FBE3) is the next frame's `C2/D7F1`, with the description, the Owned box and the members'
  "can equip" poses. The port runs C2/D7F1 after the yield (`resume` 3) and draws the rows of the window the last FBE3 /
  FBCE took up (`shownBuyScroll` / `shownSellScroll`).

Regress: `menu-shop-buy-list-scrollbar-slide-page-vs-oracle` (5/5 mutations), `menu-shop-sell-list-scrollbar-slide-page-
vs-oracle` (1/1). This closes items 3 of the first open list and 2 of the second above.

## 15. PORTED (2026-09-25): the equip sub-mode's list -- scrollbar, one-row slide, page

The equip sub-mode's candidate list (substate 8) is the Equip screen's machinery at another place. `C2/E0F1` (every
frame of the gear rows, and on the way into the list) sets `$0D92` = 2A68 and runs the Equip module's own `C2/A463`:
`$0FC8` := 0, first cell 4, C2/EA81 with 1500 over the count - 8, `$0D94 x 4`, `$0D95` = 0, the scrollbar task
spawned. The list's `C2/9F05` pages (C2/93BF, `$0D95` = the scroll x 4) and scrolls a row with `C2/EB9B` + `C2/9F90`'s
loop exactly as on the Equip screen (menu2_equip_change.md section 11): the rows are BG3 (`$4E00`: slot k at tile
row 7 + 2k, the incoming one in slot 8 at row 23) on channel 6 -- MEASURED with `ct.exe --hdma` (shopeqscroll): 48
lines at FFFF, eight bands of 12 from line 48 at 003 + 4k, and mid-slide 9 lines at 006, bands of 12, 3 lines at 026
before line 144 -- and C2/E162's preview waits for the loop's end. B from the gear rows to the party row (`C2/D745`)
kills slots 4 and 6 (`LDA #50 / JSR C2/8255`): the bar is gone there. `tools/menu_audit_cmp.py shopeqscroll` 0 of 501
frames (regress `menu-shop-equip-list-scrollbar-slide-vs-oracle`). This closes item 2 of the list after section 13.
