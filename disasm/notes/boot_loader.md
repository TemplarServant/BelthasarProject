# THE NATURAL LOADER — reset → title → load a save (or start a new game)

Derives the whole cold-boot chain of Chrono Trigger (USA): what the reset does, where the title
screen actually lives, how the game decides between "new game" and "continue", where the save-slot
screen comes from, and how a chosen save gets back into WRAM and into the world.

Every claim cites a ROM address (`python tools/dis.py <fileoffset>`, fileoffset = (bank−0xC0)<<16 |
addr) or an oracle measurement. ⭐ = a fact that changed a wrong assumption; ⚠ = a trap.

---

## 0. The chain, end to end

```
RESET $00:FF00 ──► JML $FD:C000   (PPU/CPU register init — ported, CtBootHwInit)
                   JML $C0:000E   (WRAM clears + NMI/IRQ trampolines — ported, CtBootInitMain)
   C0:0053  JSL $C7:0000                     the SPC700 driver upload
 ⭐C0:0057  LDA #09 / JSL $C2:8004            leaf 9 = C2/8D6A = THE SRAM SCAN  (§1)
   C0:005D  JSR 0B4E / 0B64 / 0B75
   C0:0066  LDX $0100 / CPX #01F0 / BMI      loc ≥ $1F0 → JML $C2:0000 (overworld)
   C0:007D  DP=$0100, the FIELD main loop     else → load location $0100

  location 0  (WRAM was cleared, so this is where every cold boot lands) — scene 0's script (§2)
      glb[$61]==1 ─► NEW GAME setup: names, party, gold, `C8 C0` (ATB select + Crono naming),
                     then CHANGELOC 496 = the 1000 A.D. intro pan
      glb[$60]==0 ─► CHANGELOC 433 = the TITLE / ATTRACT location (§3)
      else        ─► `DD 00 00 00 00` then `C8 41` = menu MODE 0 = THE LOADER (§4)

  location 433 — the attract state machine on glb[$62] (§3)
      stage 0 ─► glb[$62]=1 ; `EA 00` (song) ; ⭐`FF 02` = THE TITLE SCREEN, a $C3 sequence
      later stages ─► the demo cutscenes
      glb[$60]!=0 ─► reset glb[$57]/[$62]/[$1ED] and CHANGELOC 0

  menu mode 0 (C2/8000) (§4)
      $7F0061 == 0 ─► C2/956E wipe, $7F0061 = 1, and REBOOT — the screen never draws
      else         ─► screen $0E, the SAVE-SLOT list; A on a slot = `JSL $FF:FAA3` = THE LOAD (§5)
      both exits   ─► C2/8320 …`JML $C0:0000`  = back to C0/0066 with the new $0100
```

---

## 1. ⭐ `$7F0061` IS THE CART'S OWN VERDICT — the boot SRAM scan

`C0:0057` is `A9 09 22 04 80 C2` = `LDA #09 / JSL $C2:8004`. `$C2:8004` is `80 04` → `$C2:800A`
`JSR $C2:8C36`, the $C2 gate, whose leaf table is at `$C2:8C5B` (`C2/8C4A JSR ($8C5B,X)`); entry 9
is **`C2/8D6A`**:

```
2D8D6A  SEP #30
        STZ $79                       ; slot = 0
  loop: JSL FF/F9C4                   ; the slot validity gate (below)
        ORA #00 / BEQ ->8D85          ; A == 0 -> VALID
        INC $79 / LDA $79 / CMP #03 / BCC loop
        JSR C2/956E                   ; NO valid slot: the FULL new-game wipe
        LDA #00 / BRA ->8D8A
  8D85: JSR C2/95BB                   ; >= 1 valid slot: the LIGHT wipe
        LDA #02
  8D8A: STA $7F0061
```

⭐ So `$7F0061` is **0 on a blank cart and 2 when the cart holds at least one valid save** — and 1 is
a third value, written later (C2/804B and C2/E662) meaning "a new game has been started". Scene 0's
boot script branches on exactly these.

**FF/F9C4, the validity gate** (`tools/dis.py 0x3FF9C4`): DB = $30, `JSR FF/FBD3` puts the slot base
in `$7B`, then

* `LDA $7FF8,X / CMP #E41B / BNE` → `A = $8080` (bad signature)
* `JSR FF/FBE7 / CMP $7FF0,X / BNE` → `A = $4040` (bad checksum)
* else `A = $0000` = USED.

⚠ The cart is 8 KB, so bank $30 mirrors every `0x2000`: `$7FF8`/`$7FF0` land at image `0x1FF8`/`0x1FF0`
(measured on `saves/ct_real_save.srm`, a game the ROM itself saved).

**C2/95BB, the light wipe** — `$7F:0000-01FF = 0`, `$7E:0400-04FF = 0`, `$7E:2990-29AF = 0`, then 12
bytes from `$C2:FCA6` → `$7E:2990` (`84 A0 01 80 08 40 08 02 04 10 20 40`) and 9 bytes from
`$C2:FCA9` → `$7E:0408`. ⭐ `$2990 = 0x84` is exactly the pre-confirm byte `battle_atbmode.md`
measured on the earliest new-game state (bit 6 clear until the Battle-Mode screen sets it).

**C2/956E, the full wipe** — the light wipe, then `$7E:2400-25FF = 0`, `$CC:0000 + 0x280` →
`$7E:2600` (the char-sheet template), gold `$2C53/54 = 0`, `$2980-2988 = 0x80` and `$29AF = 0x80`,
`$7E:2C7C-2C99 = 0`.

---

## 2. Scene 0 — the boot script (script 23, 210 bytes)

`python tools/ct.py script 0` truncates at fn0's first RETURN (`ct-script-dump-truncates-fn0`); the
listing below is the whole body walked linearly by `assets/script.py op_len`. The autorun
continuation at `stopAddr+1` (`ct-field-vm-startup`) is what runs 0x0014 onward.

```
0x0000  B8 00 BA FC          STRINGTABLE @FC:BA00
0x0004  4A A1 02 7E 00       $7E:02A1 = 0
0x0009  4A 10 01 00 01       $00:0110 = 1        (DP $10 -- the field's own flags)
0x000E  4A 11 01 00 01       $00:0111 = 1
0x0013  00                   RETURN              -- fn0 ends; the continuation runs from 0x0014
0x0014  16 61 01 00 03       if glb[$61] == 1 -> 0x0019      else -> 0x001B
0x0019  10 1A                JUMPFWD +26 -> 0x0034           NEW GAME
0x001B  16 60 00 00 0E       if glb[$60] == 0 -> 0x0020      else -> 0x002D
0x0020  56 00 ED 01          glb[$1ED] = 0
0x0024  56 00 62 00          glb[$62]  = 0
0x0028  E0 B1 03 00 00       CHANGELOC $1B1 = 433            THE TITLE / ATTRACT
0x002D  16 61 00 00 98       if glb[$61] == 0 -> 0x0032      else -> 0x00C9
0x0032  C8 41                menu MODE 0  (short-circuits: §4)
0x0034  4A 7E 02 00 00       $027E = 0                       ---- the NEW-GAME setup ----
0x0039  4A 7F 02 00 00       $027F = 0
0x003E  4E 8F 02 7E 12 00    $7E:028F += 16 bytes `00 A0 00 A0 01 80 00 ...`  (the overworld anchor)
0x0054  4E 80 29 7E 0B 00    $7E:2980 += `00 80 80 80 80 80 80 80 80`   (Crono active, rest empty)
0x0063  4E 23 2C 7E 32 00    $7E:2C23 += the 8 DEFAULT NAMES (50 bytes)
0x0099  4E 53 2C 7E 05 00    $7E:2C53 += `90 01 00`          = 400 gold
0x00A2  4B CD 01 7F 00 01    $7F:01CD = 0x0100
0x00A8  4A DF 01 7F 80       $7F:01DF = 0x80
0x00AD  56 00 57 00          glb[$57]  = 0
0x00B1  56 00 62 00          glb[$62]  = 0
0x00B5  56 00 ED 01          glb[$1ED] = 0
0x00B9  56 00 60 00          glb[$60]  = 0
0x00BD  65 07 54             SETFLAG glb[$54].bit7
0x00C0  C8 C0                menu MODE 3 = screen $0D: ATB Battle-Mode select, then Crono NAMING
0x00C2  E0 F0 81 3F 38       CHANGELOC $1F0 = 496 tile(0x3F,0x38)   the 1000 A.D. intro pan
0x00C7  10 08                JUMPFWD +8 -> 0x00D0
0x00C9  DD 00 00 00 00       CHANGELOC 0 (staged; see ⚠ below)
0x00CE  C8 41                menu MODE 0  ->  THE LOAD SCREEN
0x00D0  00                   RETURN
```

⚠ **`DD 00 00 00 00` is a staged no-op that must never commit.** Its commit would reload location 0
and restart this script before `C8 41` at 0x00CE ever runs. In the ROM it cannot: `C8 41`'s launcher
(§4) does `JSR C0/0B4E` and `LDX #06FF / TXS` before `JSL $C2:8000` — it LEAVES the field main loop,
so the pending change's `$0119` drain never runs again. Our port reproduces that by refusing to poll
the pending change while an op-0xC8 or op-0xFF request is parked; without the gate the boot loops
forever (observed, one reload per frame).

---

## 3. Location 433 — the TITLE and the ATTRACT chain

`python tools/ct.py script 433` (script 70, 272 bytes). obj0's continuation is the attract state
machine, keyed on **glb[$62]**:

```
0x000F  16 60 00 00 B0   if glb[$60] == 0 -> 0x0014      else -> 0x00C3
0x0014  16 62 00 00 09   if glb[$62] == 0 -> 0x0019
0x0019  56 01 62 00      glb[$62] = 1
0x001D  EA 00            (song)
0x001F  FF 02            ⭐ THE TITLE SCREEN
0x0021  16 62 01 00 04   if glb[$62] == 1 -> 0x0026
0x0026  04 02 33         CALL obj1 fn3   = the DEMO setup (party 1..7, then CHANGELOC $1F5)
... stages $2C/$38/$39/$40/$48/$4A/$4C/$50/$58 = the other demo scenes ...
0x00B1  AD 28            WAIT 40
0x00C1  11 B3            JUMPBACK -179 -> 0x000F
0x00C3  EA 00 / 56 00 57 00 / 56 00 62 00 / 56 00 ED 01
0x00D1  E0 00 02 00 00   CHANGELOC 0            <- START was pressed
```

### 3.1 ⭐ `FF 02` is not a screen effect — it LEAVES the field engine

The op-0xFF handler `C0/3FAF` splits on the operand at `$90` (`3FB7 CMP #90 / BCS ->4000`). Only the
`>= $90` half is the small jump table (`4009 JSR ($400E,X)`, the iris/park subs). The `< $90` half:

```
3FBB  the $19 (INIDISP brightness) ramp to black, one step per frame
3FD3  JSR C0/0B4E            quiesce
3FD6  LDX #06FF / TXS        RESET THE STACK
3FDC  LDA $DF / BMI ->3FE4   operand >= $80?
3FE0  JML $C3:0000           ⭐ operand < $80: the special-SEQUENCE engine, A = the operand
3FE8/3FF0/3FF8/3FFC          $80/$81/$82/else -> JML $FD:FFF1 / FFEB / FFE8 / FFE5
```

`$C3:0000` (`BRA ->C3:0014`) stores A at **`$0384`**, then LZ-decompresses `$C3:1313` — **34175
bytes** — into `$7E:3000` and `JML $7E:3000`. That overlay is a WRAM-resident program:

```
7E3000  JSR $3063
7E3003  LDA $0384 / AND #$1F / ASL / TAX
7E300A  JSR ($3043,X)          the SEQUENCE TABLE (32 entries at $7E:3043)
7E303B  LDX #$06FF / TXS
7E303F  JML $C0:0000           every sequence ends by REBOOTING the field engine
```

Table entry **2 = `$7E:5978` = the title screen**. Measured: `$0384 = 0x02` for the whole title on the
oracle (`--read wram:0384+2 --from <title state>`).

The title routine loads five compressed blobs through `$7E:3691` (`LDA $FE:0000,X` = a 3-byte pointer
table, X = a BYTE offset) and DMAs them to VRAM:

| $FE index | pointer | size | destination |
|---|---|---|---|
| 0x48 | FE/6002 | 5632 | VRAM `$4000` — OBJ chr (the pendulum) |
| 0x45 | FE/5307 | 6144 | VRAM `$5000` — BG chr |
| 0x4B | FE/6C4F | 2048 | VRAM `$6000` — BG2 tilemap (via `JSR $5AB2`) |
| 0x4E | FE/6D6D | 256  | VRAM `$6400` — BG1 tilemap |
| 0x51 | FE/6E34 | 480  | `$7E:2000` |

PPU at rest on the title: **BG mode 2**, BG1 map `$6400`/chr `$5000`, BG2 map `$6000`/chr `$5000`
(`ct.exe --ppuregs`).

### 3.2 ⭐ What START leaves behind

```
7E5B99  LDA $6E / BNE ->5BBE     ⚠ after the script's own handover, this task does NOTHING
7E5B9D  LDA $70 / BNE ->5BBE     and it can only fire once
7E5BA1  LDA $02 / BEQ ->5BBE     ⭐ ANY button, not START -- $0002 is the whole auto-read word
7E5BA7  INC $70
7E5BA9  LDX #$34AB / JSR $395E   ⭐ install the FADE-OUT (title_intro.md §6.6)
7E5BAF  LDA $02
7E5BB1  BIT #$30
7E5BB3  BNE ->5BBB               ⭐ L or R held -> SKIP the flag (the boot falls into the demo)
7E5BB5  LDA #$01
7E5BB7  STA $7F0060          ⭐ glb[$60] = 1
7E5BBB  JMP $39A7            delete me; the sequence's own exit is $34C2's, sixteen frames later
```

Confirmed live: `--monitor 7F0060:3 --from <title state> --btnscript "0:10,8:6,0:60"` reports
`f11 7F0060=01…*(7E:5BBB)` — a writer PC inside WRAM, i.e. the overlay. The reboot then re-enters
location 433, whose 0x000F test now takes the `glb[$60] != 0` arm and changes to location 0.

⚠ **Two things this section used to leave implicit, and the port used to get wrong.** First, the
task does not exist for the sequence's first fifty frames — the copyright timer installs it when
`$0072` counts down to 60 — and a button before that does NOTHING (measured: pressed at boot 150,
`$7F0060` and `$0070` are still zero 120 frames later). Second, START does not end the sequence: it
installs `$7E:34AB`, which becomes `$34C2` and walks the brightness `$038D` from 15 to 0 before
setting `$7E:0000`. The "sixteen frames later" this file has always quoted IS that fade, and it is
now ported and graded frame by frame (title_intro.md §6.6).

---

## 4. The op-0xC8 launcher, and the mode-0 short circuit

`C0/3757` (op 0xC8) only *stages*: the operand goes to `$25` (DP $0100 ⇒ `$0125`) and a request bit
is set. The field's deferred handler is `C0/0CC7`:

```
0CC7  BIT #40 / BNE ->0CCE          (no request -> BRL 0D3E)
0CCE  the $19 brightness ramp to black
0CEA  JSR C0/0B4E / JSR C0/011B     quiesce
0CF2  LDA $25                       the operand
0CF4  BEQ ->0D04     operand $00    -> LDA #05 / BRL C0/19C7      mode 5  (screen $07)
0CF6  BMI ->0D09     operand >= $80 -> mode = (operand ROL x3) & 3  ($C0+id -> 3, screen $0D)
0CF8  BIT #01 / BNE ->0D00   ODD    -> ⭐ LDA #00 = MODE 0        (screen $0E, THE LOADER)
0CFC  LDA #06        EVEN            -> mode 6 (screen $0B, the shop)
0D18  JSL $C2:8000
```

⚠ Reading only the `ROL ×3` arm gives mode 1 for `$41` and is WRONG. Measured with `--hookpc`:
`C8 41` reaches **C0/0D00** (f94) and `C8 C0` reaches **C0/0D09** (f131). Mode→screen is
`C2/808F` = `0E 00 0C 0D 0F 07 0B`, so mode 0 → screen `$0E`, handler `C2/E60B`.

A scan of all 504 shipped scripts finds **exactly two** odd-operand op-0xC8 sites, both in scene 0
(0x0032 and 0x00CE). This screen belongs to the boot and to nothing else.

### 4.1 ⭐ THE SHORT CIRCUIT — why a blank cart never sees the loader

`$C2:8000` → `$C2:800E`, and after `STA $0A00` (the mode):

```
2803D  LDA $0A00 / BNE ->28064          mode != 0 -> run the menu program
28042  LDA $7F0061 / BNE ->28064        a game exists -> run it (screen $0E)
28048  JSR C2/956E                      OTHERWISE: the full new-game wipe
2804B  LDA #01 / STA $7F0061            "a new game has been started"
28051  STZ $307FE2 / STZ $307FE1
2805B  LDA $0D00 / STA $29AD
28061  JMP C2/832F  ->  C2/8345 LDX #06FF / TXS / JML $C0:0000     REBOOT
```

So on a blank cart the loader **never draws**: it wipes, sets the flag and reboots, and scene 0's
0x0014 test then takes the new-game arm. Measured exactly that way on the oracle — `f94 $0A00=0`,
`f95 $7F0061=1`, `f131 $0A00=3` (the ATB selector) — which is why a fresh cartridge goes from the
title straight to "Battle Mode" with no menu in between.

⚠ This is also why splicing an SRAM into a *late* savestate proves nothing: the C0/0057 scan has
already run. The capture has to be built on a **frame-1** state (verified: frames 1–10 all still
read `$7F0061 = 2` after the splice).

---

## 5. Screen `$0E` — the save-slot list

Handler `C2/E60B` is a sub-state table `{E613, E61B, E6AE, E705}`.

* **sub 0** `C2/E613` — `JSR C2/E7C3` (build) / `INC $68` / `JMP C2/83B2`.
* **sub 1** `C2/E61B` — the live list. `JSR C2/E984` decodes the action into `$0D1D`; only `BMI`
  (A) is handled, so **B is inert on this sub-state**.
  * `E64C LDA $54 / CMP #03 / BCC ->E652` — cursor < 3 is a SAVE SLOT: `STA $79 / STA $0414`,
    then **`E657 JSL $FF:FAA3`** = the load.
  * cursor ≥ 3 is NEW GAME: `JSR C2/956E`, `$7F0061 = 1`, `$299E = $30:7FE1`, and when
    `$30:7FE2 != 0` and `$9392 == 0x78` the New-Game+ arm (`C2/E841`, sub-state 2).
  * both fall into `C2/E694`: sound cmd `$F3` with `$2990 & 8`, `$29AD = $0D00`, `JMP C2/8320`
    → **`JML $C0:0000`**.
* **cursor settle** `C2/E743`: while `$0D49,X == 0` step and `AND #03` — the cursor **skips empty
  slots**; the step is `DEX`, or `INX INX DEX` (= +1) when `$005A` bit 3 says the press was UP.
* **default row** `C2/E7CF`: `$79 = $30:7FE0` (the last slot saved); if `$0D49|$0D4A|$0D4B == 0`,
  `$79 = 3` = New Game. Then `$54 = $79`.
* `$0D4C` — the New-Game row's own occupancy byte — is `0xFF` (`E6E4 DEC $0D4C`) except on the
  New-Game+ path (`E67A STZ $0D4C`).

### 5.1 What the screen shows (all four elements are ROM data)

Read off the oracle's own tilemaps for this screen (BG1 `$5800`, BG2 `$6000`, BG3 `$6800`, mode 1,
the shared menu shell):

| element | tile cells | source |
|---|---|---|
| "New Game" | BG3 row 1, cols 13-20 | literal |
| slot number | BG3 cols 3-4, rows 3-4 / 7-8 / 11-12 | literal |
| slot CHAPTER TITLE | BG3 cols 10-25, same rows | §5.2 |
| era name | BG3 rows 17-18, cols 4-12 | §5.3 |
| `TIME hh:mm` | BG1 row 20, cols 3-12 | summary |
| gold + `G` | BG1 row 22, cols 3-12 | summary |
| `Save # n` | BG1 row 24, cols 3-12 | summary |
| party name/LV, HP/MP | BG1 rows 17-18 / 20-21 / 23-24, cols 18-28 | save file |

The three slot buttons are BG2 tile rows 2-5 / 6-9 / 10-13 (y16/48/80, 32 px each) built from the
SAME chrome tiles as every other menu screen (`0x300-0x313` + `0x3DC-0x3EF`) — ⚠ but **not** the same
CGRAM: the buttons carry tilemap palettes 1/2/3, and rows 1-3 of the X-menu's CGRAM are the
red/cyan/marble ones. The hand cursor is OAM (measured at `(8, 53)` with the cursor on slot 0).

### 5.1b ⭐ THE CURSOR IS A NODE GRAPH, and the settle steps DOWN on UP

The movement is not a mod-4 ring written by hand: `C2/E9E5` reads `$F5` (the NEW-PRESS d-pad byte),
takes its LOWEST set bit as a direction index (Right=0x01→0, Left=0x02→1, Down=0x04→2, Up=0x08→3),
snapshots `$54` into `$81`, and jumps through `$C2:EA1F` (`45 EA 40 EA 3B EA 36 EA`) into
`LDA $938x,Y` — so index 0→node+5, 1→+4, 2→+3, 3→+2. `C2/EA27` turns the row into the node
offset (`ASL / PHA / ASL / ADC $01,S` = row*6), so the nodes are SIX bytes.

Dumped live on the load screen (`--read wram:9380+64`):

| node | hand OAM X | hand OAM Y | Up (+2) | Dn (+3) | Lf (+4) | Rt (+5) |
|---|---|---|---|---|---|---|
| 0 slot 0 | 8 | 53 | 3 | 1 | 0 | 0 |
| 1 slot 1 | 8 | 85 | 0 | 2 | 1 | 1 |
| 2 slot 2 | 8 | 117 | 1 | 3 | 2 | 2 |
| 3 New Game | 8 | 21 | 2 | 0 | 3 | 3 |
| 4/5 | 184 | 87 / 103 | — the New-Game+ prompt pair | | | |

⭐ A FOUR-NODE VERTICAL RING: **New Game is the row above slot 0**, and Up/Down wrap through it.
Left/Right are SELF on every node — inert, like the ATB selector's list. The hand's OAM position is
the node's own +0/+1.

⚠ **And `C2/E743`'s settle steps MINUS ONE ON UP.** `E74D LDA $5A / AND #08 / BNE ->E755` skips the
`INX INX`, so an UP press walks the cursor DOWN off empty rows and anything else walks it UP. Getting
that sign backwards is not cosmetic: with ONE save on the cart the valid rows are {slot 0, New Game},
and the wrong sign makes Up-from-New-Game and Down-from-slot-0 both dead ends — the cursor leaves the
save row and can never come back. Measured on the oracle, one save (`$0D49 = 01 00 00 FF`):

```
0 -Up->   3      3 -Up->   0      0 -Down-> 3      3 -Down-> 0      (a toggle, BOTH directions)
```

and with three (a spliced 3-slot cart): Up walks `0 3 2 1 0`, Down walks `0 1 2 3 0`. Both tables are
reproduced by the port and pinned.

### 5.1c ⭐ THE ROW GEOMETRY IS PER-SCANLINE SCROLL, not a flat tilemap

`ct.exe --hdma` on the load screen shows the BG vertical scrolls are HDMA-driven — the frame-boundary
`vS` that `--ppuregs` reports (503) is only the LAST band's value. Read straight out of WRAM
(`[count][lo][hi]` per entry, 0 terminates):

| table | register | lines 0..39 | lines 40..135 | lines 136..215 |
|---|---|---|---|---|
| `$7E:9200` | `$210E` **BG1**VOFS | `$FFFF` | `$01F7` | `$01F7` |
| `$7E:9080` | `$2110` **BG2**VOFS | `$00FF` | `$01E7` | `$01F7` |
| `$7E:9180` | `$2112` **BG3**VOFS | `$01F1` | `$01E7` | `$01F7` |

⚠ **`$2110` is BG2VOFS and `$2112` is BG3VOFS.** Pairing them the other way round — which the
channel ORDER in an `--hdma` dump invites, since ch1 lists before ch3 — gives BG2 a band-1 scroll of
497 instead of 255, which selects empty map rows and loses the screen's whole top row. That was the
first bake's defect.

The map line a scanline shows is `(y + vofs) & 0x1FF` over the 32x64 map (`higher=1`, second screen
at map base + 0x400 WORDS), and that is what lets the ROM draw **FOUR bars out of three button
blocks**:

```
band 1  y   0.. 39  vofs $00FF  ->  SECOND-screen rows 1-4  = the "New Game" bar   y  9.. 40
band 2  y  40..135  vofs $01E7  ->  first-screen rows 2-5 / 6-9 / 10-13            y 41.. 72
                                    = the three save slots                         y 73..104
                                                                                   y105..136
band 3  y 136..215  vofs $01F7  ->  first-screen rows 16-25 = the bottom windows   y137..216
```

Confirmed against the oracle two ways: the two rows whose BRIGHTNESS SWAPS when the cursor moves
between New Game and slot 0 are its framebuffer `y16..47` and `y48..79` — exactly `y9..40` and
`y41..72` plus the 7-line offset of the oracle's 240-line frame — and the port's own composed rows
now measure the same, as do the three bottom-window text bands (`fb 152..167 / 176..191 / 200..215`).

The text layers are scrolled too, and NOT by the same amounts, so every field's screen y is its BG
map row put through ITS layer's table (`map_line = (y + vofs) & 0x1FF`, inverted):

| field | layer / map row | band | screen y |
|---|---|---|---|
| "New Game" | BG3 row 1 | 1 (497) | 23 |
| slot *i* number + description | BG3 rows 3+4*i*, 4+4*i* | 2 (487) | 48 + 32*i*, +8 |
| era name | BG3 rows 17,18 | 3 (503) | 145, 153 |
| TIME / gold / Save # | BG1 rows 20 / 22 / 24 | 3 (503) | 169 / 185 / 201 |
| party 0/1/2 | BG1 rows 17,18 / 20,21 / 23,24 | 3 (503) | 145,153 / 169,177 / 193,201 |

⭐ And with the geometry right, the node table's own hand OAM Y is usable: the SNES renders an OBJ
one scanline BELOW its OAM Y and the blob's opaque pixels start at local +2, so a top-left of
`OAM_Y + 1` puts the ink where the oracle's is (measured: its row-0 hand inks fb `y63..74` = screen
`y56..67` = `53 + 1 + 2` ..).

### 5.1d ⭐ AND THE OTHER HDMA: the per-scanline COLOUR MATH

The scroll is only half of what this screen does per scanline. `ct.exe --hdma`'s scanline trace
prints the rest line by line, and it is the documented window-gradient law (`C2/86DD`: +8 at the top
of a window down to −7 at its bottom, 16 steps, RESET AND HEIGHT-NORMALISED PER WINDOW) over THIS
screen's partition — the four 32-line rows and the 80-line bottom pair, not the X-menu's ribbon list:

```
lines   0..  8   math=00                       no colour math at all
lines   9.. 40   fixed 8,7,6..0 then 1..7      one step every 2 lines   (the New Game row)
lines  41.. 72 / 73..104 / 105..136            the same ramp            (slots 0, 1, 2)
lines 137..216   the same ramp in 5-LINE steps                          (the two bottom windows)
```

⭐ **And the "dim" of the non-cursor rows is not a second pass.** The trace shows the CURSOR's row
(and the bottom windows) running `math=02 half=0` — plain add/subtract on BG2 — while the other
three run `math=07 half=1`: the same gradient, with the halve, and with BG1/BG3 also in the mask
(which is why the text on those rows is halved too).

⚠ **The ORDER matters and the obvious reading is wrong.** The sum is clamped AT ZERO, halved, and
only then capped at 31 — the halve is part of the same ALU pass, not something applied to an
already-clamped colour. Capping at 31 first is measurably too dark: on the top line of each dimmed
row the oracle reads `(156,156,148)` where that order gives `123`. Using the X-menu's ramp on top of
that (which is what a flat port does) leaves the bars flat and the row separations missing.

With the law right the chrome is **byte-exact on every band** — 0 mismatches over the chrome columns
of all five bands. What is left of the whole-screen difference is the two things this screen still
substitutes: the ROM's VWF text and the face icons.

Ported as `mChromeTMScroll` (the per-scanline composer) + `ctMenuLoaderBand`, and pinned by the
composed frame's own hash on the first frame each cursor row draws — with a mutation that drops the
scroll and replays the plane flat.

### 5.1e ⭐ The rest of the screen: the icons, the numerals, the VWF — and BYTE-EXACT

With the scroll and the colour math right, everything else on the screen turned out to be locatable
in the ROM rather than approximable:

* **The party FACE ICONS** (BG1 tiles `0x1E0-0x1E3` / `0x1E8-0x1EB` / `0x1F0-0x1F3` at cols 15-16)
  are stored RAW: four 4bpp tiles at **`$FF:E55C + charId * 0x80`** in reading order (tl, tr, bl, br)
  — found by searching the ROM for this screen's own VRAM tiles (Crono id 0 → `$FF:E55C`, Lucca id 2
  → `$FF:E65C`, Frog id 4 → `$FF:E75C`). Decoding all eight slots renders seven clean mugshots and
  garbage at index 7, so the table is exactly the seven playable characters.
  ⚠ **And each one carries its OWN 16-colour palette**, at **`$FF:CC84 + charId * 0x20`**: the three
  party rows' tilemap cells read CGRAM rows **4 / 5 / 6**, one per character. Resolving all seven
  through row 4 — the first thing that looks right, because Crono IS row 4 — renders the correct
  FACES in the wrong colours.
* **The big slot numerals** (BG3 tiles `001/002+011/012`, `003/004+013/014`, `005/006+015/016`) are
  NOT in the ROM raw — the ROM builds them — so they are baked from this screen's own BG3 CHR.
* **The slot descriptions and the era** are the engine's **DIALOGUE VWF**: measured, the tiles the ROM
  renders them into are the same glyphs and the same advances as `CT_ASSET_FONT` (`$FF:2060` +
  `$FF:3860`, widths at `0x0260E6`).
  ⚠ **They are NOT centred.** The pen starts at the block's LEFT EDGE and the ROM string's own
  LEADING SPACES position it: `'  The Queen is Gone'` (2 spaces × 4 px) inks at x8, `'          No
  data'` (10) at x40, `'Middle Ages'` (none) at x0 — exactly what the oracle's tiles measure. So the
  strings are baked VERBATIM. (Stripping them and centring on a tile boundary fits those same three
  points by accident, and breaks on any string whose padding is not symmetric.)
* ⚠ **Two cells do not use the code the ASCII mapping would pick**: the clock's separator is tile
  `0x2D`, not `':'` = `0xE3`, and the Save line's hash is `0xEE`, not `'#'` = `0xF3` — read straight
  off the oracle's BG1 tilemap (`0B3 0A8 0AC 0A4 000 0D4 0DC 02D 0D8 0D7` and
  `0B2 0BA 0CF 0BE 0EF 0EE 0EF ...`). Those two glyphs were the last 23 wrong pixels on the screen.
* ⚠ And the BG3 rows invert to **49 + 32i**, not 48: `(24 - 487) & 0x1FF = 49`. The modulo, not the
  subtraction — one pixel high on every slot row, and the difference between 1.94% and 0.00%.

⭐⭐ **The screen is now BYTE-EXACT.** The engine's composed 256×224 frame hashes the same FNV-32 as
the ORACLE's own framebuffer, in five independent states: a one-save cart with the cursor on the save
(`E22C51EE`) and on New Game (`0862506C`), and a three-save cart on each of its four rows — 0
mismatching pixels in every one.

### 5.1f ⚠ The exit FADES, and the script is never unparked

Choosing a row does not cut straight to the reboot. `C2/8320`'s tail runs, in order:

```
28329  JSR C2/834D                       (party record teardown)
2832C  JSR C2/83CA                       ⭐ the shell's OWN fade-out
2832F  LDA #80 / STA $2100               forced blank
28345  LDX #06FF / TXS / JML $C0:0000    the reboot
```

`C2/83CA` is `LDA #FE` — minus two — added to the brightness nibble of `$0D10` once per pass until
it reaches 0, so the load screen is still on screen, dimming, for eight frames. Measured on the
oracle after the press: brightness ~5, ~3, ~1, 0 on consecutive frames (a clean −2 per frame; the
first sample already reads 5 because the drive script's press is six frames long), then black and
staying black.

⚠ Cutting straight to the reboot instead — which the port did — shows the scene UNDERNEATH the
shell for as long as the transition takes to ramp: location 0's black map with the lead PC standing
on it. That is a sprite the ROM never draws here.

⚠ **And the interrupted script is never unparked.** In the ROM the shell's exit IS a reboot, so the
script it interrupted simply ceases to exist. The port must not clear the op-0xC8 request when the
fade ends: leaving it standing keeps the VM parked and keeps the pending-change poll gated (§2) until
the transition frees the scene. Clearing it let scene 0's staged `DD 00 00 00 00` commit in the frames
between the fade ending and the load landing, and the boot went back to location 0 instead of into
the save.

⚠ The blank-cart short circuit does NOT fade: `C2/8061 JMP C2/832F` lands PAST `C2/83CA`, on the
forced blank — correct, since that path never drew anything.

### 5.1g ⭐⭐ NEW GAME: the window after the reboot shows NOTHING -- `$0197` is the sentinel

Picking New Game runs the same tail (5.1f) plus `C2/E65D JSR C2/956E`, and the reboot re-enters at
`$0100` = 0, i.e. scene 0 at location 0, whose new-game arm (S2, 0x0034-0x00C0) is pure data stores
ending on `C8 C0` -- the ATB/naming shell. Between the two the ROM draws NOTHING AT ALL. Measured
from a state one frame before the press (`--peek ... PEEK_SAVE`, then everything below off it):

```
press+0..7    $0D10 07 05 03 01 00      the shell's own fade (5.1f)
press+9..14   CPU in bank $C0           the reboot + the location-0 load; C0/EC55 NOT reached
press+15..45  C0/EC55 visited 1x/frame  the field main loop IS running, its NMI IS writing INIDISP
                $0119 01..0F then 0F..00  (C0/284F ramps it up, C0/0CD6 -- the op-0xC8 launcher -- down)
press+46..    CPU in bank $C2           the ATB/naming shell; its own $0D10 02..0F fade at +64..71
```

⭐ And there is nothing on that screen to light. At press+22 -- with `$0119` = 0x0D and C0/EC55
writing it every frame -- the oracle reads

```
$0197 = 80        the lead-PC object offset: the "none" sentinel
$1B00.. = 00      no object alive
OAM               ZERO entries with y < 224
framebuffer       ZERO non-black pixels, every frame from press+12 to press+56
```

`$7E:2980` already reads `00 80 80 ...` (the script's 0x0054 store put Crono in slot 0), so the party
is NOT what gates it -- **no PC LOADER op ever ran**, and `$0197` keeps the 0x80 C0/0C1E leaves there.
Scene 0's script has no `57` and no `80 00` anywhere.

(⚠ The two brightness reads disagree and it does not matter here: `$0119` says 13 while `--ppuregs`
reports the PPU's own `brightness` as 0 at the frame boundary. The framebuffer is black on every frame
sampled from press+12 to press+56 either way, and the `$0197`/OAM answer does not depend on which of the
two is being displayed. Reconciling them is an open boundary, not a load-screen question.)

⚠⚠ **The port had the rule and had disabled it.** field.c's draw gate was
`f.vm && CtSceneActorCount(f.vm) > 1 && !CtScenePlayerLoaded(f.vm)` -- the house shorthand for "the VM
holds a real scene" silently turns the sentinel test OFF for a ONE-OBJECT scene, and scene 0 is one of
exactly **three in all 512** that has a single object (0, 11, 82 -- none of the three loads a PC; 11 and
82 report the same `leadPC obj=64 [sentinel]` under `--teleport`). So the New Game exit drew the lead PC
on location 0's empty black map for the ~30 frames before the naming shell opened: 210 non-black pixels
where the ROM has 0. Dropping the count term is the whole fix, and it can only change those three scenes.

Graded by `nopc:` -- the composed FIELD frame's non-black pixel count, taken before the brightness fade,
once per scene LOAD (a reload re-arms it, so the post-reboot window is graded and not just the one under
the load screen) and numbered, because a benign sibling line would otherwise satisfy the pin: the first
scene-0 visit reads 0 even with the defect present, and that is the line the first version of this check
matched -- a green check over a live bug, killed by the mutation, not by a score.

### 5.2 ⭐ The chapter title — `C2/D266`

```
2D266  LDX $7B / LDY $79
       LDA $30:0591,X / AND #07 / STA $0D79,Y      the leader char id
       LDA $30:0603,X                              ⭐ the slot's STORYLINE byte
2D27C  LDX #1A
  loop DEX / CMP $FF:D024,X / BCC loop             the 26-entry THRESHOLD table
       STX $020C                                   the chapter index
2D28D  (empty slot) LDA #1A                        index 26 = "No data"
2D2A0  LDX #D03E / STX $020D                       the 27-pointer STRING table, bank $FF
```

`$FF:D024` = `[26 ROM bytes at 0x3FD024: python tools/dis.py 0x3FD024 26]`;
`$FF:D03E` points at "The Millennial Fair" … "The Final Battle", "No data".
Verified against the reference save: storyline `0x15` → index 2 → **"The Queen is Gone"**, exactly
what the oracle draws.

### 5.3 ⭐ The era name — `C2/D3AD`

```
AD C2 98      LDA $98C2          the location id
4A            LSR A              C = loc bit0
AA            TAX
BF 96 D2 FF   LDA $FF:D296,X     ONE NIBBLE PER LOCATION, low nibble first
29 FF 01      AND #01FF
90 04         BCC +4             loc EVEN -> keep the low nibble
4A 4A 4A 4A   LSR x4             loc ODD  -> the high nibble
E2 20 / 29 0F SEP #20 / AND #0F
2D3D4  LDX #D396 / STX $020D     the 8-pointer string table
```

`$FF:D396` → `"???" / "Present" / "Middle Ages" / "Future" / "Pre-historic" / "Dark Ages" /
"Black Omen" / "End of Time"`. Verified: loc `$1F1` → nibble 2 → **"Middle Ages"**, which is what the
oracle draws for the reference save.

### 5.4 The rest of the summary — `C2/D19F` (already in `menu.h`)

leader `slot+0x0580`, char sheet `slot+0x0200 + id*0x50` (`+0x12` LEVEL, `+3` curHP, `+7` curMP),
gold 24-bit `slot+0x05E0`, Save # `slot+0x059C` (capped 999 by `C2/D21B`), location
`slot+0x05F3 & 0x1FF`, clock digits `slot+0x05E5..0x05E7`.

---

## 6. `FF/FAA3` — THE LOAD

The exact inverse of the writer `FF/F9FB` (`save_write.md`), walking **the same table**:

```
3FFAA3  PHP / REP #30
        LDA #3054 / STA $9690 ; LDA #6B30 / STA $9692     build `54 30 30 6B` at $7E:9690
                                                          = MVN <dest>,$30 / RTL
        LDX #FA7B / LDY #9890 / LDA #0027 / MVN $7E,$FF   the 8x5 record table -> $7E:9890
        JSR FF/FBD3 / TAX                                 X = the slot BASE (the MVN SOURCE)
        LDY $51                                           the record cursor (0)
  loop  LDA $9892,Y / STA $9691                           the record's DEST BANK patches the MVN
        LDA $9890,Y / STA $00                             the record's DEST ADDRESS
        LDA $9893,Y / DEC A                               len - 1
        PHY / LDY $00 / PHB / JSL $7E:9690 / PLB          copy len bytes SRAM -> WRAM
        PLA / CLC / ADC #0005 / TAY / CMP #0028 / BCC loop
        PLP / RTL
```

X runs on across the records exactly as it does in the writer, so record *k* reads from
`base + Σ preceding lengths` — the layout the writer produced. The eight records
(`$FF:FA7B`, `{addr:2, bank:1, len:2}`) are unchanged from `save_write.md`:

| # | dest | len | slot offset |
|---|---|---|---|
| 0 | `$7E:2400` | `$05B0` | `+0x0000` inventory, char sheets, party, `$299C` at `+0x059C` |
| 1 | `$7E:2C23` | `$0033` | `+0x05B0` names, gold at `+0x05E0` |
| 2 | `$7E:0400` | `$0010` | `+0x05E3` the play clock |
| 3 | `$00:0100` | `$0010` | `+0x05F3` ⭐ **the location word AND its arrival tile** |
| 4 | `$7F:0000` | `$0200` | `+0x0603` the story flags (storyline at byte 0) |
| 5 | `$00:027E` | `$0002` | `+0x0803` |
| 6 | `$00:0290` | `$0011` | `+0x0805` the overworld anchor |
| 7 | `$7E:2C7C` | `$001E` | `+0x0816` — total `0x0834` = 2100 bytes |

⚠ `FF/FAA3` neither validates (that is `FF/F9C4`, run by the caller — the cursor can only rest on an
occupied row) nor reboots (that is `C2/8320`).

### 6.1 ⭐ Where the loaded game resumes

Record 3 restores `$0100..$010F`, and the reboot's `C0/0066 LDX $0100` reads it straight back.
`$0102`/`$0103` are the **arrival tile** and `$0104` the arrival bits — the same trio a ChangeLocation
commits (`C0/0C9D-0CBE`), so a load re-enters through the ordinary location loader:

* `loc >= $1F0` → the overworld, world = `loc - $1F0`, entering at `($0102*8, $0103*8)`.
* `loc < $1F0` → a field map, placed by `C0/42B8`'s arrival law (`$0104` bit2 → X low `$00`/`$80`,
  bit3 → Y low `$7F`/`$FF`, bits 0-1 = facing).

Verified on the reference save: `$0100 = $01F1` (world 1 = 600 A.D. = "Middle Ages"),
`$0102/$0103 = 0x1D/0x2A` → `(232, 336)` — and the live oracle after its own load reads
`$0283 = 232`, `$0285 = 336`, byte for byte.

⚠ `$0283/$0285` (the live overworld position) are in **no** record. They are DERIVED at entry from
the arrival tile; a port that tries to restore them directly is restoring a variable the save does
not contain.

### 6.2 ⚠ A defect the round trip found: `$0100` on a DEBUG entry

`$0100..$0104` are written by exactly one thing — the location-change commit `C0/0C9D-0CBE`
(`CtWramCommitLocation`). Every real entry into a map goes through it, so during play they always
name the location the engine is running. A `--scene N` cold start (and `--battletest` /
`--menutest`) skips that machinery entirely and left them **zero** — and they are record 3 of a
save. A save taken in such a session recorded location 0, and loading it rebooted into scene 0.

Found by the only test that can find it: writing a save with this engine's own Save screen and then
BOOTING from it. The fix seeds what the commit would have written (the location the loader was asked
for, the spawn tile it placed the lead on, the facing) at the initial `fieldLoad`; a real transition
overwrites them again the moment one happens. Pinned by `field-menu-save-jingle-and-write`'s
`loc=008`, with a mutation that removes the seed.

---

## 7. What this pass PORTED, and what it did not

Ported (`src/menu.c`, `src/ct_vm.c`, `src/field.c`, `src/data/menu_loader.h`,
`src/title_intro.c`):

* `CtMenuSaveSlotValid` = FF/F9C4 (signature **and** checksum), `CtMenuSaveLoad` = FF/FAA3,
  `CtMenuNewGameWipeLight` = C2/95BB, `CtMenuNewGameWipe` = C2/956E, `CtMenuSaveBootScan` = C2/8D6A.
* op 0xC8's odd-operand arm (the mode-0 shell) and op 0xFF's `< $90` arm (a $C3 sequence) as parked
  requests; the changeloc poll gated while either is parked (§2 ⚠).
* Screen `$0E`: chrome plane + its own CGRAM baked from the oracle, the cursor rules (default row,
  skip-empty settle), the chapter/era/summary/party text, A = load / new game, and the
  `JML $C0:0000` reboot into `$0100`.
* `Belthasar --boot` = the cold entry: the SRAM scan, then scene 0's own script decides everything.
* The title STAGE and, since the title-intro pass, the WHOLE of what it plays (see the first
  boundary below), plus `--loadcheck <srm>`, which runs FF/FAA3 over an SRAM image and prints each
  slot's unpacked fields.

NOT ported, and deliberately labelled rather than approximated:

* ~~The title screen's OVERLAY~~ — **no longer a boundary.** This section used to say the 34 KB
  WRAM-resident program could not be reimplemented and that the port baked its settled frame
  instead. `disasm/notes/title_intro.md` derived the whole thing and `src/title_intro.c` now RUNS
  it: the `$C3` byte-code interpreter, the NMI's two hook lists, every task the sequence installs,
  the sprite-package drawers and the ROM's palette-job runner, rendered through LakeSnes' own PPU
  fetch for mode 7 and for mode 2 with offset-per-tile. Graded against the cartridge, not against
  itself: ⭐ **0 mismatching pixels of 57344 at every frame tried from the first pendulum frame to
  the settled logo**, and the machine's own cells match the oracle's WRAM for 2050 frames
  (49200 values, 0 mismatches). `tools/gen_title.py` and `src/data/title_screen.h` are deleted —
  the bake is superseded, not merely unused. The CONTROL FLOW this section pinned is unchanged: the
  sequence still blocks the script, START still leaves `$7F0060 = 1`, the exit is still the
  C0/0000 reboot.
* The attract-mode DEMO stages (`FF 60`, `FF 61`, …) — same overlay, and now the same MACHINE:
  what is missing for them is their own tasks and scripts, not an engine. `sequence_table.md` maps
  all sixteen table entries, censuses every call site in all 512 scenes, and measures the attract
  chain: it walks entries 2, 1, 0, 1, 3 and nothing else, and the gameplay footage between the
  stages is the FIELD and BATTLE engines running real locations.
* The New-Game+ arm (`$30:7FE2`, `C2/E841`, sub-state 2/3) and the erase path (`C2/E8B7`).
* (The load screen itself is no longer on this list — see §5.1e: it is byte-exact.)
* The `$7F01EC == 1` in-game load path (`C0/0283 -> C0/02CA -> C0/000E`, noted in `ct_vm.h`).

---

## 8. The RESET from a running game (2026-09-14): the attract's demo battle on a button

`C0/000E` is not only the power-on's. The attract's DEMO BATTLE aborts by resetting the machine: CF/E6D9 (the battle's
bank-CF frame service) tests `$2989` bit 5 and the auto-read pad (`LDA $004218 / ORA $004219` -- any bit), latches
`$A097`, walks `$BB00` (the battle's INIDISP shadow) down a step a frame, and at zero `LDA #80 / STA $BB80 / JML C0:FF00`
= `SEI / CLC / XCE / JML FD:C000` (hardware registers: `$4200 = 0`, `$420C = 0`, INIDISP `$80`) / `JML C0:000E`.

What C0/000E does to a RUNNING machine, measured (saves/attract_demo_battle_f1850.state, A from f20; `--hookpc`):

| step | where | oracle |
|---|---|---|
| `C0/0012-0020` quiesce | C0/0B64, C0/0B75, TCD $0100, C0/0B4E | C0/000E at f34 line 255 |
| `C0/0023-0050` clears | C0/2DF1 x3: fixed DMA from MPYL (0 x 0) over `$7E:0000` x `$0500`, `$7E:0700` x `$E900`, `$7F:5080` x `$5080` | `$0100/$0117/$0384` = 0 at f35; `$7F0062` still 2C |
| `C0/0053` `JSL C7:0000` | the driver takes `F0`/`FF`, hands the SPC to its IPL (`$BBAA`), and the whole driver goes up again | f36 line 226 -> C0:0057 f57 line 165 = 20.77 frames |
| `C0/0057` `LDA #09 / JSL C2:8004` | the SRAM scan (C2/8D6A), a blank cart -> the wipe C2/956E (`$7F0062 = 0` at C2:95D2) | C0:005D at f57 line 240 |
| `C0/005D-0066` | `LDX $0100` = 0 -> loc 0, whose script stages E0 at f75 | `$0100` = 1B1 at f91, `$0384` = 02 at f107 |

The stack page `$0500-$06FF` and `$7F:0000-507F` (story flags, script locals) survive the reset.

PORTED: battle.c `battleDemoAbortFrame`; field.c the battle NMI's brightness (CD/0900-0925) over the battle picture, and
the reset itself (the three clears, audio.c `CtAudioColdBoot` -- the upload on the loader clock with a RESET background of
no NMI and no HDMA, 7,458,170 master cycles = 20.87 frames against the cartridge's 20.77 -- the SRAM scan, and the reboot
into `$0100` one frame later with the field loop held until that load). Check `attract-battle-abort-reset`.

The port reached 1B1 48 frames after the reset where the cartridge takes 56 (BB00 = 0 at f35, commit f91): the gap was
loc 0's own `10 00` song upload (10.6 frames), which the port did not charge to the load -- closed, field_load_cost.md
7. Now 56. OPEN: the C2/8D6A scan's own 75 lines are not priced. The GAME-OVER exit (C0/02C3 `BRL ->000E`) takes the same reset now -- battle_gameover_reboot.md 6.

## 9. The list is the SHELL's (2026-09-25)

`C8 41` is C0/0C76 -> C0/0CC7's arm like the shop and the naming (C0/0CF8 `BIT #01 / BNE` -> C0/0D00 `LDA #00` -> C0/0D18
`JSL C2:8000`), so the port now runs it through the menu shell (src/field.c, `menuScreen == 0x0E`): the park, C0/2848's
ramp (C0/0C76 is a main-loop call: an op parked inside a location's fade-in waits for its end), C0/0CCE's fifteen `DEC
$19`, C0/011B, C = the JSL. Then C2/8000's mode-0 test:

* **blank cart** (`$7F0061` = 0): 28048 JSR C2/956E, 2804B `$7F0061` = 1, 28051/57 `$30:7FE2/7FE1` = 0, 28061 JMP C2/832F
  (`$2100` = 80, NMI off, `JML $C0:0000`) -- all at C, nothing drawn. The port leaves the shell before its C frame and the
  field path's consumer does C0/011B, the wipe and the reboot in that frame.
* **a save** : the shell's init, S0, C2/E613 `JSR C2/E7C3 / INC $68 / JMP C2/83B2` (P = S0 + 1), the list (C2/E61B),
  A -> C2/8320 (`#FF` kill, C2/834D, C2/83CA -2 a frame), C2/832F's JML the frame after `$0D10` reaches 0.

MEASURED (logical oracle, a blank cart and colsave3 / ct_real_save from power-on, Start at +13 and +115 from f120): `C8 41`
parks at f168 with loc 0's `$0119` climbing (0F at f182), `DEC $19` f183-197, C = f198 (`$0A00` = 00). With a save:
`$0067` = 0E at f214, `$00BE` = 01 at f216, `$0D10` 2 .. 15 over f218-225 -- the init is ONE frame longer than with no
save (C2/D156's slot scan draws the save's text through the VWF: menu_shell_timing.md 2.2, unpriced), so the port's list
(S0 = C+16 fixed) lights a frame early. `$0102-04` at C = FF 00 00 on both batteries: C0/012C `LDY $97` with `$0197` =
0080 (no lead) reads `$1881 / $1901 / $1680`, and the reboot goes to loc 000 tile (255, 0) bits 00 (the port read its
host position there; it reads those bytes now when `$0197` bit 7 is set).

Graded: tools/boot_loader_cmp.py (the settled list, 57344/57344 at the port's f260); tools/naming_cmp.py bootldr / loadlist
(park-aligned on `C8 41`) show what is left:

* the blank cart's black stretch from the loader's park to the naming's is 51 frames against the cartridge's 49 (the
  reboot's load commits the frame after the request -- the port's shared reboot pipeline -- and one more frame not yet
  located); LOCATED 2026-09-26, below;
* the list lights one frame early (C2/D156);
* the JML frame: the cartridge's picture is lit (brightness 1) only from line 141 down -- the logical oracle's hold at the
  NMI-off switch, most likely (no HDMA channel targets `$2100`); not modelled;
* the save's overworld (loc 1F0) comes up 7 frames early in the port after the reboot (44 priced black frames; the
  reboot's world entry differs from a door's -- not derived).

### 9.1 The blank cart's reboot is ONE NMI-off stretch (2026-09-26) -- the port's +2, located

Traced on the logical oracle (build/_namingcmp/boot_f120.state, the bootldr presses; `--cputrace` from f196): the
loader's C0/0B4E turns the NMI off at C0/0B5D in C's frame (f198), and it stays off through C2/8000's short circuit,
C2/832F's `JML $C0:0000` -- the SOFT entry: C0/0000 -> C0/0B86 in the same frame, no C0/0057 SPC upload, no SRAM scan --
loc 0's whole load (C0/00F4) and C0/286C's pass, to C0/0B28's wait C0/0B38. `--hookpc`: C2/8000, C2/832F, C0/0000,
C0/0B86 all at f198, C0/0B3F (NMI on) at f203.

| leg | cycles |
|---|---|
| C0/0B5D -> the JSL (C0/011B's stash, C0/0D00's arm) | 247,548 |
| C2/8000 -> C2/832F (the mode-0 test, C2/956E's wipe) | 92,670 |
| C2/832F -> C0/0000 -> C0/0B86 | 1,126 |
| C0/0B86 -> C0/286C (C0/00F4: loc 0's packets and legs) | 588,746 |
| C0/286C -> C0/0B38 (the pass: C0/0A50 941,468 without BG3, C0/B0E6 26,300 -- nothing drawn, n = 1) | 1,117,518 |
| **total** | **2,047,662 = 5.903 F -> the NMI is back at the end of C + 5 (f203)** |

The port spends C's frame (C0/011B, the wipe, the reboot request), commits the load the NEXT frame, and holds
ceil(1,841,608 / 346,888) = 6 black frames: C + 7, two late -- the naming's fade-in at the cartridge's f253 + 2. Two
separate roundings where the cartridge has one stretch, and the load's estimate is `LOADC_FIRST_PASS` (1,426,744, door
loads' measured pass) where this pass is 1,117,518. The fix is the same as the door path's: price a FRESH load
C0/0B86 -> C0/0B38 like the menu reload (src/field_cost.c: the packets, the loaders, the walked map build, the pass over
the objects the loader has placed) and time a reboot's load as the continuation of its caller's NMI-off stretch. NOT
DONE YET -- the objects-as-loaded state the pass reads has to be validated on fresh loads first (menu_shell_timing.md
section 10's last paragraph). The save's reboot (C2/8320's tail, the overworld 7 frames early) is the same class.
