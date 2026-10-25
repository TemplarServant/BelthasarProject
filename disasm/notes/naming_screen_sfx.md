# Name-entry screen — SOUND (ROM mode-3 screen `$0D`)

**Status:** derived. Every row below carries either a `tools/dis.py` line / raw ROM bytes, or a
`build/ct.exe` output line. Provenance markers:

| marker | meaning |
|---|---|
| `[dis]` | reproduced by the synthesizer in this session with `tools/dis.py` or a raw-byte read of `Chrono_Trigger_USA_SNES.sfc` |
| `[orc]` | reproduced by the synthesizer in this session with `build/ct.exe` (command + output pasted) |
| `[orc*]` | oracle output reported by a measurement sub-agent and **not** re-run here |

The one claim that arrived refuted is carried as a correction, not as fact (§1a, cmd `$70`).

---

## 1. THE TABLE — every sound event of the name-entry screen

`cmd`/`id`/`pan` = the mailbox bytes `$7E:1E00 / $1E01 / $1E02` handed to `JSL $C7:0004`.
"throttled" = goes through `C2/EB03` and can be swallowed by `$0D76` (§3).
**SILENT rows are results, not omissions.**

| # | event | ROM site | cmd | id | pan | gate | throttled | confirmed by |
|---|---|---|---|---|---|---|---|---|
| 1a | screen OPENS, shell prelude 1/5 — **music RESUME/unpause, and normally a NO-OP** (see correction below) | `C0/0D09` → `JSL C2/8000` → `C2/8040 BNE` → `C2/8064` → `JSR C2/8096` → `C2/80AC` (direct store, no stager) | `70` | `00` | *not written* (`$70` handler reads only `$1E01`) | `C2/8040 LDA $0A00 / BNE ->28064`; naming = kind 3 ⇒ always taken. Then inside `C7/08E3`: `CMP $11 / BEQ ->7091F` — if the pause shadow already matches, **nothing is sent to the APU** | no | `[dis]` `[orc]` |
| 1b | prelude 2/5 — SFX id `$FF` | `C2/8096` → `C2/80B8` | `18` | `FF` | `80` | unconditional inside `C2/8096` | **no** (direct store, never touches `$0D76`) | `[dis]`, `[orc]` residue |
| 1c | prelude 3/5 | `C2/8096` → `C2/80CB` | `19` | `FF` (carried, not rewritten) | `80` (carried) | unconditional | no | `[dis]` |
| 1d | prelude 4/5 | `C2/8096` → `C2/80D4` | `82` | `01` | `FF` | unconditional | no | `[dis]`, `[orc]` residue |
| 1e | prelude 5/5 | `C2/8096` → `C2/80E7` | `83` | `01` (carried) | `FF` (carried) | unconditional | no | `[dis]` |
| 2 | **"Enter a name…" prompt DISMISSED** by *any* button | `C2/E209` → `C2/E20E` → `C2/EAC2` → `C2/EB03` | `18` | `00` | `80` | `C2/E209 LDX $00F0 / C2/E20C BEQ ->2E21D` — **X is 16-bit** at handler entry (`C2/827D REP #30`, `C2/828C SEP #20`), so the whole new-press word is tested: ANY button | yes | `[dis]` `[orc]` |
| 3 | **keyboard cursor MOVED** (d-pad, incl. wrap) | `C2/E267` → `C2/E287` → `C2/EAC2` | `18` | `00` | `80` | two gates: `C2/E279 BMI ->2E28A` (`$0D1E` bit7 = no direction) **and** `C2/E285 CPX $9380 / BEQ ->2E28A` (cursor pixel pair must actually change). On this 8×10 wrapping grid the second gate is never false | yes | `[dis]` `[orc]` |
| 4 | **d-pad HELD → auto-repeat**: every repeat blips | same as #3 | `18` | `00` | `80` | same. Repeat cadence: first move at press, second ≈ +32 f, then every 5 f — **5 f > the 3-f throttle, so no repeat is ever swallowed** | yes | `[orc*]` (cadence not re-run here) |
| 5 | **TYPE a character** (A / confirm) | `C2/E2F4` → `C2/E30A JSR C2/E316` → falls through to `C2/E30D` → `C2/EAC2` | `18` | `00` | `80` | `C2/E2F7 BIT $0D1D / BMI ->2E30A` (bit7 = A). **No further gate** — `C2/E30A` falls through to `C2/E30D` unconditionally | yes | `[dis]` `[orc]` |
| 6 | **TYPE into a FULL name** (`$0F06 == 5`) — **still blips** although nothing is stored | same as #5 | `18` | `00` | `80` | `C2/E316` clamps `LDY #04` (`C2/E337 CPY #05 / BCC / C2/E33B LDY #04`) and `C2/E343 BCS ->2E348` skips the `$0F06` store — neither branch reaches the sound site | yes | `[dis]` `[orc*]` |
| 7 | **BACKSPACE** (B) with `$0F06 > 0` | `C2/E2F4` → `C2/E300…E308` → `C2/E30D` → `C2/EAC2` | `18` | `00` | `80` | `C2/E2F9 BVC ->2E310` (bit6 = B) **and** `C2/E2FE BEQ ->2E310` (`$0F06 != 0`) | yes | `[dis]` `[orc]` |
| 8 | **BACKSPACE at position 0 — SILENT** | `C2/E2FE` branch taken to `C2/E310`, jumping *over* `C2/E30D` | — | — | — | `LDX $0F06 / BEQ ->2E310` | — | `[dis]` `[orc]` |
| 9 | **L / R shoulder — SILENT** and move nothing | no site reached | — | — | — | `$0D1E` stays `$80` (no direction) and L/R set `$0D1D` bits 0/1, not bit7/bit6 | — | `[orc*]` |
| 10 | **IDLE — SILENT** (nothing self-posts) | — | — | — | — | measured absence over 90 f | — | `[orc*]` |
| 11 | **CONFIRM (Start) with a DUPLICATE party name → the error buzz** | `C2/E21E` → `C2/E22C JSR C2/E363` → `C2/E36F JSR C2/E3E0` → `C2/E3FB` → `C2/EACC` → `C2/EB03` | `18` | **`01`** | `80` | reached only via `C2/E224 LDA $00F0 / AND #01 / BNE ->2E22C` (Start). Inside the validator the buzz fires on `C2/E3F2 BEQ ->2E3FB` (matched byte was the `0` terminator) or by falling through `C2/E3F6 CPX #0005 / BCC ->2E3E8` (all 5 bytes matched) against any of the 8 slots of `$7E:2C23`. `C2/E369 STZ $0000,X` (X = `$0F0C` = `$2C23 + charId*6`) zeroes the char's **own** slot first, so re-entering your own current name is **not** a duplicate. After the buzz `C2/E3FE LDA #FF` ⇒ `C2/E374 BNE ->2E38E` ⇒ `C2/E231 BNE ->2E22B` RTS: **the screen stays open** | yes | `[dis]` `[orc]` |
| 12 | **CONFIRM with an EMPTY name → rejected, SILENT** | `C2/E376 LDA $9890 / C2/E379 BNE ->2E37F / C2/E37B LDA #FF / C2/E37D BRA ->2E38E` | — | — | — | the empty-name reject path never touches `C2/EACx` — **only the duplicate rejects audibly** | — | `[dis]` |
| 13 | **CONFIRM with a VALID name → ACCEPT, SILENT** | `C2/E22C` → `C2/E363` returns A=0 → `C2/E233 JMP C2/82B2` | — | — | — | no `JSR C2/EAxx` anywhere on the accept path, and the whole shell region `C2/8000-8FFF` contains **only five** `STA $1E00` sites, all inside the prelude `C2/8096` (`C2/80AC, 80B8, 80CB, 80D4, 80E7`) | — | `[dis]` `[orc]` |

### 1a — correction carried (a sub-agent claim was refuted)

The prelude's first post was reported as "PAUSE MUSIC". **That is wrong; the polarity is inverted and
the post is normally inert.**

```
[dis] python tools/dis.py 708E3 0x20 0 0
  708E3: SEP #20
  708E5: LDA $01          ; DP = $1E00 (C7/0150 LDX #1E00 / PHX / PLD) -> $01 = $1E01
  708E7: CLC
  708E8: ADC #FF
  708EA: LDA #00
  708EC: ROL A            ; A = ($1E01 != 0)  == the PAUSE flag
  708ED: CMP $11          ; vs the shadow $1E11
  708EF: BEQ ->7091F      ; ALREADY IN THAT STATE -> exit, nothing written to $2140-$2143
  708F1: STA $10
  708F3: STA $11
  708F5: LDA #FE / 708F7: STA $2140 ...
```
```
[dis] the only two other cmd-$70 callers, which fix the polarity:
  1BD5: LDA #01 / 1BD7: STA $1E01 / 1BDA: LDA #70 / 1BDC: STA $1E00 / 1BDF: JSL C70004   (battle ENTER = PAUSE)
  1C7F: LDA #00 / 1C81: STA $1E01 / 1C84: LDA #70 / 1C86: STA $1E00 / 1C89: JSL C70004   (battle EXIT  = RESUME)
[dis] the prelude uses STZ $1E01 (C2/80AF) -> the C0/1C7F polarity -> RESUME.
```
```
[orc] build/ct.exe --read "wram:00BE+1,wram:0067+1,wram:0A00+2,wram:1E10+2" --from saves/luccaname.state
      wram[0000BE+1] = 01     wram[000067+1] = 0D     wram[000A00+2] = 0302     wram[001E10+2] = 0000
[orc] ... --from saves/crononame.state
      wram[0000BE+1] = 04     wram[000067+1] = 0D     wram[000A00+2] = 0300     wram[001E10+2] = 0000
```
`$1E10/$1E11 = 0000` on both naming states ⇒ the `$70 / 00` post hit `C7/08EF BEQ` and sent nothing.
It is a defensive "make sure music is not paused", **not** an audible event, and it is not part of the
missing name-entry SFX.

### 1b — the prelude is NOT name-entry-specific

`C2/8096` has exactly one caller in the ROM (`C2/8068`), inside the generic `JSL $C2:8000` mode entry.
It runs on **every** mode-3 open (main menu included), not only on naming. Port it on mode-open, never
as a naming special case.

```
[dis] python tools/dis.py 28096 0x60
  28096: REP #30 / 28098: PEA $0000 / 2809B: PLD
  2809C: LDX #8406 / 2809F: LDY #0500 / 280A2: LDA #0007 / 280A5: MVN $7E,$C2   <- installs the NMI trampoline
  280A8: SEP #20
  280AA: LDA #70 / 280AC: STA $1E00 / 280AF: STZ $1E01 / 280B2: JSL C70004
  280B6: LDA #18 / 280B8: STA $1E00 / 280BB: LDA #FF / 280BD: STA $1E01
  280C0: LDA #80 / 280C2: STA $1E02 / 280C5: JSL C70004
  280C9: LDA #19 / 280CB: STA $1E00 / 280CE: JSL C70004
  280D2: LDA #82 / 280D4: STA $1E00 / 280D7: LDA #01 / 280D9: STA $1E01
  280DC: LDA #FF / 280DE: STA $1E02 / 280E1: JSL C70004
  280E5: LDA #83 / 280E7: STA $1E00 / 280EA: JSL C70004

[dis] raw d[0x280A8:0x280EC] =
  [29 ROM bytes at 0x280A8: python tools/dis.py 0x280A8 29]
  [28 ROM bytes at 0x280C5: python tools/dis.py 0x280C5 28]
  22 04 00 c7 a9 83 8d 00 1e 22 04
```
Residue proving it ran for both namings:
```
[orc*] build/ct.exe --read "wram:1E00+6" --from saves/crononame.state -> 0001FF0500E0
[orc*] build/ct.exe --read "wram:1E00+6" --from saves/luccaname.state -> 0001FF0D00E0
       ($1E01=01 $1E02=FF = the residue of C2/80D2's `82 01 FF`; $1E05 bit7 set = driver ready)
```

### 1c — the buzz, measured first-hand

Drove Crono's naming screen to spell **Marle** (a name already in `$7E:2C23`) and pressed Start:

```
[orc] build/ct.exe --monitor "7E1E00:1,7E1E01:1,7E1E02:1,7E0D76:1,7E0F00:6,7E9890:6,7E0067:1" \
        --from saves/crononame.state \
        --input "0:4,01:2,0:8,20:2,0:8,80:2,0:8,80:2,0:8,100:2,0:8,20:2,0:8,20:2,0:8,40:2,0:8,40:2,0:8,100:2,0:8,20:2,0:8,80:2,0:8,80:2,0:8,80:2,0:8,80:2,0:8,80:2,0:8,80:2,0:8,80:2,0:8,100:2,0:8,40:2,0:8,40:2,0:8,40:2,0:8,40:2,0:8,40:2,0:8,40:2,0:8,100:2,0:8,10:2,0:8,80:2,0:8,80:2,0:8,80:2,0:8,100:2,0:8,08:2,0:8,0:30" \
        --diff --writer --frames 360
  f305  7E1E01=00(C2:EB0B)   7E1E02=80(C2:EB1A)  7E0D76=03*(C2:EB15)  7E0F00=ACBACBC5BE00*(C2:E340)  7E0067=0D
  f315  7E1E01=01*(C2:EB0B)  7E1E02=80(C2:EB1A)  7E0D76=03*(C2:EB15)  7E9890=ACBACBC5BE00*(C2:E3D5)  7E0067=0D
```
`$1E01 = 01` on the Start frame, pan `80`, `$0067` still `$0D` ⇒ **the screen did not exit**.
Party name table read live off the state (`wram:2C23+48`):
`A2CBC8C7C800 ACBACBC5BE00 ABCEBCBCBA00 B1C8BBC80000 A5CBC8C00000 A0D2C5BA0000 ACBAC0CECC00 A4C9C8BCC100`
= Crono / Marle / Lucca / Robo / Frog / Ayla / Magus / Epoch `[orc]`.

### 1d — silence, measured first-hand

```
[orc] build/ct.exe --monitor "7E1E01:1,7E1E02:1,7E0D76:1,7E0F06:1,7E0F08:1,7E0F00:6,7E0D1D:1" \
        --from saves/luccaname.state --input "0:4,01:2,0:20" --diff --writer --frames 30
  f0  7E1E01=01* 7E1E02=FF* 7E0D76=00* 7E0F06=00* 7E0F00=ABCEBCBCBA00* 7E0D1D=00*(C2:E98D)
  f5  7E1E01=01  7E1E02=FF  7E0D76=00  7E0F06=00  7E0F00=ABCEBCBCBA00  7E0D1D=40*(C2:E9AE)
  f6  7E1E01=01  7E1E02=FF  7E0D76=00  7E0F06=00  7E0F00=ABCEBCBCBA00  7E0D1D=00*(C2:E98D)
  monitor: 7 region(s), 3 row(s)
```
The B latch (`$0D1D` bit6) **is** set — the press was seen — and the mailbox never moves. Measured
silence, not a missed press.

### 1e — move / type / backspace, measured first-hand

```
[orc] build/ct.exe --monitor "7E1E01:1,7E1E02:1,7E0D76:1,7E0F06:1,7E0F08:1,7E0F09:1,7E9380:2" \
        --from saves/luccaname.state --input "0:4,80:2,0:8,100:2,0:8,01:2,0:20" --diff --writer --frames 50
  f5   7E1E01=00*(C2:EB0B) 7E1E02=80*(C2:EB1A) 7E0D76=03*(C2:EB15) 7E0F08=01*(C2:E2E3) 7E9380=3453*(C2:E2F3)  <- MOVE  right
  f6/f7/f8   7E0D76=02/01/00 (C2:8466)                                                                        <- the NMI drain
  f15  7E1E01=00(C2:EB0B)  7E1E02=80(C2:EB1A)  7E0D76=03*(C2:EB15) 7E0F06=01*(C2:E348)                        <- TYPE
  f25  7E1E01=00(C2:EB0B)  7E1E02=80(C2:EB1A)  7E0D76=03*(C2:EB15) 7E0F06=00*(C2:E308)                        <- BACKSPACE
```

### 1f — the prompt dismiss takes ANY button, and does not also type or move

```
[orc] for b in 04 10 100 200; do build/ct.exe \
        --monitor "7E1E01:1,7E1E02:1,7E0D76:1,7E00BE:1,7E0D13:1,7E0F06:1,7E0F08:1,7E0F09:1" \
        --from saves/crononame.state --input "0:4,$b:2,0:14" --diff --writer --frames 22; done
  (identical f5 row for Select 04, Up 10, A 100, X 200:)
  f5  7E1E01=00*(C2:EB0B) 7E1E02=80*(C2:EB1A) 7E0D76=03*(C2:EB15) 7E00BE=01*(C2:8247)
      7E0D13=1D*(C2:E216) 7E0F06=00 7E0F08=00 7E0F09=00
```
`$00BE` (the saved sub-state, §2) steps 4 → 1; `$0D13 := $1D`; **`$0F06/$0F08/$0F09` do not move** —
the dismissing press is consumed. `[orc*]` reports the same f5 row for all twelve buttons
(01, 02, 04, 08, 10, 20, 40, 80, 100, 200, 400, 800); four of those were re-run here.

---

## 2. Sub-state map of ROM mode-3 screen `$0D`

The mode-3 shell dispatches on two variables: `$67` = screen index, `$68` = sub-state.

```
[dis] python tools/dis.py 28270 0x40
  28275: LDA $68 / 28277: ASL A / 28278: TAY
  28279: LDA $67 / 2827B: ASL A / 2827C: TAX
  2827D: REP #30                            <- X and Y are 16-bit for the whole handler...
  2827F: LDA $C28292,X / 28283: STA $16
  28285: LDA [$16],Y   / 28287: STA $00
  28289: PER ->2826B
  2828C: SEP #20                            <- ...but m=8 (this is why C2/E209's `LDX $00F0` is 16-bit)
  2828E: JMP ($0000)

[dis] per-screen table C2/8292, decoded from raw bytes:
  screen 00 -> C2/99CA   screen 04 -> C2/BF35   screen 08 -> C2/8291   screen 0C -> C2/D519
  screen 01 -> C2/9C29   screen 05 -> C2/C621   screen 09 -> C2/8291   screen 0D -> C2/E1E3  <== NAMING
  screen 02 -> C2/AA34   screen 06 -> C2/CED4   screen 0A -> C2/8291   screen 0E -> C2/E60B
  screen 03 -> C2/B353   screen 07 -> C2/C7A6   screen 0B -> C2/D4F0   screen 0F -> C2/FE0A

[dis] raw d[0x2E1E3:0x2E1ED] = ed e1 | 1e e2 | 36 e2 | 01 e2 | 09 e2
```

| `$68` | handler | what the player sees | family |
|---|---|---|---|
| **0** | `C2/E1ED` | entry / router. `LDA $299F / ORA $71 / STA $299F / BNE ->2E201`. Zero (first naming of the game, char id 0) ⇒ `JSR C2/E576`, `$68 := 2`. Non-zero ⇒ fall into `C2/E201` with `$68` still 0, so the `INC $68` there lands on **1** and states 3/4 are skipped entirely | router |
| **1** | `C2/E21E` | **the naming keyboard** — the live screen. `JSR C2/E984` (latches) → `JSR C2/E267` (nav + type/backspace) → Start test → `JSR C2/E363` (commit) → `JMP C2/82B2` (exit) | **NAMING** |
| **2** | `C2/E236` | **the ATB Active/Wait selector** (already ported). `C2/E241 LDA $0081 / CMP $54 / BEQ ->2E24B` else `C2/E248 JSR C2/EAC2`; confirm at `C2/E24C` writes `$2990` bit6 and does `C2/E25B LDA #03 / STA $68` | **ATB** |
| **3** | `C2/E201` | naming INIT — `JSR C2/E40E`, `INC $68`. One frame, invisible, **silent** | NAMING |
| **4** | `C2/E209` | **"Enter a name, then push the Start Button."** prompt-wait. Any button → blip id `$00`, `$0D13 := $1D`, `STZ $0D9A`, `$68 := 1` | NAMING |

**Reachability of state 4 (derived, then measured):** `$68 = 3` is written at exactly one place,
`C2/E25B` inside the ATB selector's confirm — so the prompt state exists **only** in the new-game
Crono flow that ran the ATB selector first. Any later naming enters at `$68 = 1` and has **no
prompt and no prompt blip**. `[dis]`

`$68` is a per-coroutine variable: it is zeroed on yield (`C2/81FD STZ $68`) and restored from the
coroutine context (`C2/820D LDA $B6,X / C2/820F STA $68`), so between frames the live value lives in
`$00:B6+X`, **not** in `$68`. For the naming coroutine that byte is `$00BE`:

```
[orc] --read "wram:00BE+1" --from saves/crononame.state -> 04   (the prompt)
[orc] --read "wram:00BE+1" --from saves/luccaname.state -> 01   (straight into the keyboard)
[orc] --monitor "7E00B0:16" --from saves/crononame.state --input "0:4,01:2,0:20" --diff --writer
      f0 7E00B0=0201BD83340A000002016F82760A0400*(C2:8247)     <- $00BE = 04
      f5 7E00B0=0000BD83340A000002016F82760A0100*(C2:8247)     <- $00BE = 01
```
This is the cleanest live discriminator for the two flows, and it independently confirms the
"prompt is new-game-Crono-only" derivation.

### 2a — the press latches the screen reads

```
[dis] python tools/dis.py 2E984 0x80
  2E984: JSR C2/E9E2            ; builds $0D1E (d-pad direction index, $80 = none) and $0081/$54
  2E98A: STZ $0D1D
  2E98D: LDA $68 / 2E98F: CMP $0D17 / 2E992: STA $0D17
  2E995: BNE ->2E9CD            ; <== SUB-STATE CHANGED THIS FRAME -> A/B LATCHES SUPPRESSED
  2E997..2E9B3:  bit7 (A) / bit6 from the 2-byte table C2/E9D9 over $2993,Y
  2E9B5..2E9CA:  bit1 from $2999, bit0 from $299A   (L / R)
  2E9CD: LDA $0D83 / BEQ ->2E9D7 / LDA #40 / TSB $0D1D    ; bit6 (B) also from $0D83
[dis] raw d[0x2E9D9:0x2E9E2] = 00 01 00 00 00 08 06 07 ff
[dis] 2E9E5: LDA $54 / STA $81      ; the pre-move row snapshot the ATB selector compares
      2E9E9: LDA #80 / STA $0D1E    ; $0D1E = $80 == "no direction this frame"
      2E9F0: LDA $F5 / ... / 2E9FE: STX $0D1E
[dis] direction table raw d[0x2E298:0x2E2A0] = d6 e2 cc e2 aa e2 a0 e2
      -> $E2D6 right(bit0), $E2CC left(bit1), $E2AA down(bit2), $E2A0 up(bit3)
```

Consequence worth porting: on the frame the sub-state changes (prompt → keyboard), `$0D1D` carries
**no A/B bit**, so the dismissing press cannot also type. The d-pad latch `$0D1E` is built *before*
that gate (`C2/E984` calls `C2/E9E2` first) and is **not** suppressed — but §1f measured the
dismissing press leaving `$0F08/$0F09` untouched, because state 4's handler simply RTSs without
calling `C2/E267`.

### 2b — the grid handlers (why every accepted direction blips)

```
[dis] python tools/dis.py 2E2A0 0x50
  2E2A0: LDA $0F09 / DEC A / BPL ->2E2B4 / LDA #07          (UP,   wrap 0 -> 7)
  2E2AA: LDA $0F09 / INC A / CMP #08 / BCC ->2E2B4 / LDA #00 (DOWN, wrap 7 -> 0)
  2E2B4: STA $0F09 / STA $004203 / STZ $0F0A
         LDA #53 / CLC / ADC $004216 / STA $9381 / STA $0F0B / RTS
  2E2CC: LDA $0F08 / DEC A / BPL ->2E2E0 / LDA #09          (LEFT, wrap 0 -> 9)
  2E2D6: LDA $0F08 / INC A / CMP #0A / BCC ->2E2E0 / LDA #00 (RIGHT,wrap 9 -> 0)
  2E2E0: STA $0F08 / STA $004203 / NOP NOP / LDA #24 / CLC / ADC $004216 ...
```
Cursor pixel = `$9380 = 0x24 + col*16`, `$9381 = 0x53 + row*16`. Every accepted direction rewrites
it with a different value, so `C2/E285 CPX $9380 / BEQ` never suppresses on this grid.

---

## 3. The stager and the throttle, verbatim

```
[dis] python tools/dis.py 2EABA 0x50            (the C2/EAxx wrapper family)
  2EABA: PHP / SEP #30 / JSR C2/EB03 / PLP / RTS                 ; "play the id already in A"
  2EAC2: PHP / SEP #30 / LDA #00 / JSR C2/EB03 / PLP / RTS       ; THE GENERIC BLIP, id $00
  2EACC: PHP / SEP #30 / LDA #01 / JSR C2/EB03 / PLP / RTS       ; id $01 (the name-entry buzz)
  2EAD6: PHP / SEP #30 / STZ $0D76 / LDA $0DBC / AND #02 / BEQ ->2EAE7 / LDX #06 / BRA ->2EAF0
         2EAE7: LDA $0DBC / AND #01 / BEQ ->2EAF7 / LDX #07
         2EAF0: TRB $0DBC / TXA / JSR C2/EB03 / 2EAF7: PLP / RTS ; ids $06/$07 -- ZERO CALLERS in the ROM
  2EAF9: LDX #19 / STX $1E00 / STZ $0D76 / BRA ->2EB08           ; the cmd-$19 variant, UNTHROTTLED

[dis] THE STAGER
  2EB03: LDX  #18
  2EB05: STX  $1E00          ; 16-bit store: $1E00 = $18, $1E01 = $00  (X was #18 -> high byte 0)
  2EB08: STA  $1E01          ; the id, from A
  2EB0B: LDA  $0D76
  2EB0E: BNE  ->2EB1E        ; THROTTLED: operands stay STAGED, $1E02 and the JSL are SKIPPED
  2EB10: LDA  #03
  2EB12: STA  $0D76
  2EB15: LDA  #80
  2EB17: STA  $1E02          ; pan = $80, always
  2EB1A: JSL  C70004
  2EB1E: RTS
[dis] raw d[0x2EAF9:0x2EB1F] =
  a2 19 8e 00 1e 9c 76 0d 80 05 | [28 ROM bytes at 0x2EB03: python tools/dis.py 0x2EB03 28]
```

`$1E03` is **never written** by the stager, yet the driver ships it:

```
[dis] python tools/dis.py 7061C 0x30
  7061C: SEP #20 / LDA $03 -> STA $2143 / LDA $02 -> STA $2142 / LDA $01 -> STA $2141 / LDA $00 -> $2140
[dis] python tools/dis.py 70140 0x60 0 0     (the JSL C7:0004 entry)
  70150: LDX #1E00 / PHX / PLD                ; DP = $1E00
  70157: LDA $05 / BPL ->70192                ; $1E05 bit7 clear = driver busy -> COMMAND DROPPED
  7015D: LDA $00 / BEQ ->70192                ; command byte 0 = nothing
  7015F: BMI ->7017A                          ; cmd >= $80 -> C7/061C
  70165: CMP #18 / BCS ->70176
  70176: CMP #30 / BCS ->7017D / 7017A: JMP C7/061C     ; cmd $18..$2F -> C7/061C
  70184: CMP #70 / BNE ->7018B / 70188: JMP C7/08E3     ; cmd $70 -> the pause handler
  70192: SEP #20 / STZ $00                    ; the common exit CLEARS $1E00
```
There is **no CPU-side table of SFX ids and no range check**: `$1E01` goes straight to `$2141`.

### The drain

```
[dis] python tools/dis.py 28455 0x20      (inside the mode-3 NMI, C2/840E)
  2845E: LDA $0D76
  28461: BEQ ->28466
  28463: DEC $0D76
```
The NMI trampoline is installed by the same routine that runs the prelude:
`C2/8096` does `LDX #8406 / LDY #0500 / LDA #0007 / MVN $7E,$C2`, and
`d[0x28406:0x2840E] = 5c 0e 84 c2 5c 72 84 c2` = `JML $C2:840E` (NMI) + `JML $C2:8472` (=`RTI`) `[dis]`.
Live: `--read "wram:0500+8"` on **both** naming states returns `5C0E84C25C7284C2` `[orc*]`.

Measured cadence — set to 3 by `C2/EB12`, drained 3→2→1→0 by `C2/8463`, so a second post within
3 frames is swallowed:
```
[orc*] --monitor "7E0D76:1,7E1E01:1,7E1E02:1,7E1E00:1" --from saves/crononame.state \
         --input "0:6,180:2,0:8,180:2,0:20" --diff --writer --frames 40     (0x180 = A|Right)
  f7  7E0D76=03*(C2:EB15) 7E1E01=00*(C2:EB0B) 7E1E02=80*(C2:EB1A) 7E1E00=00(C7:0196)  <- ONE post: ran the JSL
  f17 7E0D76=03*(C2:EB15) 7E1E01=00(C2:EB0B)  7E1E02=80(C2:EB1A)  7E1E00=18*(C2:EB08) <- TWO in one frame:
  f18/f19/f20 7E1E00=18                                                    the 2nd staged $18 and DIED at EB0E,
                                                                           so C7/0192's STZ $00 never ran
```
The signature of a swallowed post is therefore **`$1E00` stuck at `$18`** with no new `$1E02` write.

### `$0D76` accessors — corrected

A sub-agent reported "exactly six accessors, one decrementer". **Incomplete.** Whole-ROM byte scan
for `{STA,STZ,LDA,DEC,INC,CMP,LDX,LDY,BIT,TSB,TRB} $0D76`:

```
[dis] STA $0D76 @ C2/EB12   STZ $0D76 @ C2/EAD9   STZ $0D76 @ C2/EAFE
      LDA $0D76 @ C2/845E   LDA $0D76 @ C2/EB0B   DEC $0D76 @ C2/8463
      TRB $0D76 @ D6/14AE   <- FALSE POSITIVE: d[0x1614A0:0x1614C0] =
        [32 ROM bytes at 0x1614A0: python tools/dis.py 0x1614A0 32]
        -- bank D6 event/dialogue data, not code.
```
**But** there is a seventh, non-obvious writer: the whole latch is clobbered by the field↔menu
low-WRAM swap.
```
[orc] --monitor "...7E0D76:1..." --from saves/luccaname.state --input "0:4,08:2,0:200" --diff --writer --frames 210
  f13  7E0D76=07*(C0:0914)          <- NOT a sound write
[dis] python tools/dis.py 905 0x30 0 0
  0905: LDY #0920 / 0908: LDX #2000 / 090D: LDA #14DF / 0910: PHB / 0911: MVN $00,$7F / 0914: PLB   (RESTORE)
  0918: LDX #0920 / 091B: LDY #2000 / 0920: LDA #14DF / 0923: PHB / 0924: MVN $7F,$00 / 0927: PLB   (SAVE)
```
`$00:0920 + 0x14E0 = $00:1E00`: the block covers `$0920..$1DFF`, so **`$0D76` is saved/restored with
the menu, and the mailbox `$1E00-$1E03` sits exactly one byte past the end and is not.** After the
naming screen tears down, `$0D76` is whatever the field had, not 0.

---

## 4. WHAT IS NOT DERIVED

Stated plainly. Do **not** let any of these leak into the port as an assumption.

1. **What ids `$00`, `$01` and `$FF` actually SOUND like.** The `$C7` SPC700 driver program is not
   disassembled here. The id is not range-checked and is not looked up in any CPU-side table — it
   goes straight out of `$2141` (`C7/061C`). The label "id `$FF` = stop-all-SFX" is an
   **interpretation**, not a measurement. Port the bytes `18 / FF / 80`; do not port the semantics.
2. **cmd `$19`, `$82`, `$83`** (prelude 3/4/5). Only the mailbox bytes are derived. Their meaning is
   NOT DERIVED.
3. **The prelude's launch FRAME.** "Fires once on the open frame, before any sub-state" is confirmed
   *structurally* (`C2/8068 JSR C2/8096` precedes `C2/808C JMP C2/8190`) and by post-hoc residue in
   both savestates. It was **not** captured live: `saves/` holds no state sitting *before* a naming
   screen, so the launch frame itself could not be monitored.
4. **The keyboard auto-repeat cadence** (`[orc*]`: first move at press, second ≈ +32 f, then every
   5 f). The producing code (`C2/E9F0` region, `$0D1F`/`$0D20`) was **not** disassembled here. The
   *audio* consequence is derived (5 f > 3 f throttle ⇒ no repeat is swallowed); the cadence
   constants are not.
5. **What the field posts on screen TEAR-DOWN.** `C2/82B2` contains no mailbox store, and the entire
   shell `C2/8000-8FFF` has only the five prelude `STA $1E00` sites `[dis]`; 200 frames after a valid
   accept showed no post `[orc]`. But the **field's own** music/song resume after the modal is a
   separate C0-side mechanism and is NOT DERIVED here.
6. **The trim `C2/E390`** that builds `$9890` from `$0F00` (trailing-space / `$FF` handling) was not
   disassembled. It matters for *which* names are duplicates, i.e. for *when* the buzz fires — so the
   buzz's trigger set is derived only for names typed fully.
7. **The `$0D9A` and `$0D13 := $1D`** side effects of the prompt dismiss are recorded but their
   meaning (a message/window id?) is NOT DERIVED.
8. **The naming screen's fade choreography** in the C engine (`nmPhase 1/2/4`, `nmFade`) was already
   flagged as not oracle-derived in `battle_atbmode.md`; nothing here changes that. Do not attach
   sound to a fade step on the strength of this dossier.
9. **`C2/EAD6` is dead code** — zero callers ROM-wide (`20 d6 ea` / `22 d6 ea c2`: no hits; the bare
   `d6 ea` word appears only inside data blocks in banks C3/D0/DE/E1/E2) `[orc*]`. Ids `$06`/`$07`
   reach the mailbox only through `C2/EAF9` from `C2/9047` (2 callers, `C2/9029` and `C2/9041`)
   `[orc*]`. **Neither is on any name-entry path.**

---

## 5. THE PORT

### 5.0 The defect, precisely

`grep -n "fieldMenuSfx" src/field.c` returns exactly four lines: the two definitions
(`src/field.c:1604`, `src/field.c:1618`) and the two ATB-selector call sites
(`src/field.c:8418`, `src/field.c:8455`). The name-entry arm, `src/field.c:8458-8501`, contains
**zero** sound calls. That is the whole bug: the stager exists and is correct, and nothing on the
naming path calls it.

The existing stager already matches the ROM byte-for-byte and needs no change:

```c
// src/field.c:1604
static void fieldMenuSfx(uint8_t* m, uint8_t id) {
  ...
  m[0x1E00] = 0x18;   // EB03/EB05
  m[0x1E01] = id;     // EB08
  if (m[0x0D76]) return;   // EB0B/EB0E -- throttled: staged, not posted
  m[0x0D76] = 3;      // EB10/EB12
  m[0x1E02] = 0x80;   // EB15/EB17
  fieldSoundFlush(m); // EB1A
  ...
}
// src/field.c:1618
static void fieldMenuSfxTick(uint8_t* m) {            // C2/845E-8463, once per menu frame
  if (m && m[0x0D76]) m[0x0D76]--;
}
```

### 5.1 Exactly what field.c must call, and where

All inside the `if (nameId >= 0)` arm at `src/field.c:8461-8501`, mirroring the shape the ATB arm
already uses at `src/field.c:8398-8437`.

| # | where in the arm | call | ROM authority |
|---|---|---|---|
| P1 | **`nmPhase >= 1`, unconditionally each frame** (not only phase 3) | `fieldMenuSfxTick(CtSceneMem7E(f.vm));` | `C2/845E-8463` runs in the **NMI**, i.e. every frame the mode-3 shell is up — including the prompt state and the fade |
| P2 | `nmPhase == 3 && nmMsgShow`, inside the dismiss branch (line 8484) | `fieldMenuSfx(mem, 0x00);` | `C2/E20E` |
| P3 | `nmPhase == 3` keyboard, **after** the four wrap updates, gated on "the cursor actually changed" | `fieldMenuSfx(mem, 0x00);` | `C2/E287`, gated by `C2/E285 CPX $9380 / BEQ` |
| P4 | the type branch (line 8490) — **outside** the `nmPos < 5` guard | `fieldMenuSfx(mem, 0x00);` | `C2/E30A` falls through to `C2/E30D` unconditionally |
| P5 | the backspace branch (line 8491) — **inside** the `nmPos > 0` guard | `fieldMenuSfx(mem, 0x00);` | `C2/E2FE BEQ ->2E310` jumps over `C2/E30D` |
| P6 | the confirm branch (line 8492), on a **duplicate** of another party member's name | `fieldMenuSfx(mem, 0x01);` **and do not close the screen** | `C2/E3FB JSR C2/EACC`, then `C2/E3FE LDA #FF` ⇒ `C2/E374 BNE` ⇒ `C2/E231 BNE ->2E22B` RTS |
| P7 | the confirm branch, valid name → `nmPhase = 4` | **no call** | `C2/E233 JMP C2/82B2`; no `JSR C2/EAxx` on that path |
| P8 | the confirm branch, empty name | **no call** | `C2/E37B LDA #FF / BRA ->2E38E` — the empty reject is silent |

### 5.2 Behavioral gaps the sound work exposes (each has a ROM citation; fix the mechanism)

These are not sound bugs, but the sound cannot be correct without them.

- **G1 — the prompt takes ANY button.** `src/field.c:8484` is `if (nmType || nmOk) nmMsgShow = 0;`.
  ROM: `C2/E209 LDX $00F0 / BEQ` with X **16-bit** ⇒ any of the twelve buttons (§1f). The dismiss
  must therefore also be reachable from a direction press, and that press must be **consumed** (the
  `else if` chain at 8483 already guarantees it — keep it).
- **G2 — typing into a FULL name overwrites slot 4, and blips.** `src/field.c:8490` is
  `if (nmType && nmPos < 5)`. ROM `C2/E316`:
  ```
  [dis] raw d[0x2E316:0x2E34A] =
    [26 ROM bytes at 0x2E316: python tools/dis.py 0x2E316 26]
    [26 ROM bytes at 0x2E330: python tools/dis.py 0x2E330 26]
  [dis] 2E334: LDY $0F06 / 2E337: CPY #05 / 2E339: BCC ->2E33D / 2E33B: LDY #04
        2E33D: STA $0F00,Y / 2E340: INY / 2E341: CPY #06 / 2E343: BCS ->2E348 / 2E345: STY $0F06
  ```
  i.e. write to `min(nmPos,4)`, advance only if the result is < 6. **Sound fires either way** (§1 row 6).
- **G3 — no duplicate check exists in C.** `src/field.c:8492-8496` rejects only the empty name.
  The ROM compares the trimmed name against all 8 slots of `$7E:2C23` **after zeroing the char's own
  slot** (`C2/E366 LDX $0F0C / C2/E369 STZ $0000,X`, `$0F0C = $2C23 + charId*6`), and a match anywhere
  ⇒ buzz + stay open. The C engine already seeds that table (`battleHudSeedName7E`), so the compare
  has real data to run against.
- **G4 — no keyboard auto-repeat in C.** ROM has one and every repeat blips. **NOT DERIVED** (§4.4)
  — implement the repeat only after deriving `$0D1F`/`C2/E9F0`; until then, say so in a comment.
- **G5 — the tick is currently under `atbSelPhase == 3` only** (`src/field.c:8455`). ROM ticks in the
  NMI for the whole time the shell is up. Widening it is the *structural* fix (§5.1 P1) and it
  touches the ATB path too — **measure** the two pinned checks `field-atbsel-cursor-sfx` /
  `field-atbsel-cursor-sfx-throttled` before and after; do not assume they are unaffected.

### 5.3 What must NOT be invented

- **Do not** add a "naming screen" special case to `fieldMenuSfx`. It is one stager for the whole
  mode-3 shell; the naming screen is just another caller.
- **Do not** invent an id for anything. Only two ids exist on this screen: `$00` (blip) and `$01`
  (duplicate-name buzz). Pan is always `$80` and comes from `C2/EB15`. `$1E03` is **never** written.
- **Do not** post `$70`/pause on open (§1a) — and if the prelude is ever ported, its `$70` post must
  go through a handler that keeps the `$1E10/$1E11` shadow and **drops** the command when it already
  matches, or it will send a spurious `$FE` sequence to the APU.
- **Do not** make the empty-name reject audible, and do not make the valid accept audible. Both are
  measured/derived silences.
- **Do not** guard the type blip on the buffer having room. That is exactly the eyeball-plausible
  invention the ROM contradicts.
- **Do not** re-hang the tick on a scene/phase heuristic. The ROM's condition is "the mode-3 NMI is
  installed", i.e. "the modal is up".
- **Infra note (not a ROM claim):** the drive-script parser at `src/field.c:7236-7256` only produces
  `wsDx/wsDy` and `talkPressed` (`'a'`). The naming edges `nmType/nmDel/nmOk` come **only** from SDL
  keydown (`src/field.c:6871-6873`), so nothing headless can drive the keyboard today. The checks
  below require routing the drive script into the naming arm the same way the ATB arm does
  (`int scrDir = (walkScript || playScript) ? wsDy : 0;` + an edge compare against a
  `nmPrevDir`/`nmPrevDirX`), plus two new parser tokens for **B** and **Start**. That is
  infrastructure, not engine behavior — keep it out of the mechanism.

### 5.4 Proposed regress checks, and the mutation that kills each

Format follows `tools/regress_checks.json` (`pin` on a printf prefix, `expect` on substrings).
Reuse the existing `CT_MENUSFX` / `CT_MENUSFX_COUNT` hooks (`src/field.c:1613-1615`,
`src/field.c:9822-9823`); the count printf's wording is ATB-specific ("row change(s)") and should be
generalized to "event(s)" or given a naming-side counter.

All run `build/Belthasar.exe --assets ct_assets.dat --newgame --selftest --frames N --play "…"` with
`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy CT_AUDIO_FORCE=1 CT_MENUSFX=1 CT_MENUSFX_COUNT=1
TB_AUTOADV=1` and **`CT_NAMEAS` UNSET** (setting it auto-confirms the screen and the check silently
grades nothing — the trap `field-newgame-pan`'s notes already warn about).

| check | drive | expect | mutation that kills it |
|---|---|---|---|
| `field-naming-prompt-dismiss-sfx` | one button on the prompt frame | `menusfx: cmd=18 id=00 pan=80 posted` + `1 post(s)`; and the cursor/buffer unchanged (`nmPos=0 nmRow=0 nmCol=0`) | delete the P2 call → 0 posts. Also killed by making the dismiss accept only A/Start once G1 lands and the drive uses a direction |
| `field-naming-cursor-sfx` | `wait:K; l:1; wait:6; d:1; wait:6; r:1; wait:6; u:1; wait:20` | `4 post(s) from 4 move(s)` | delete P3 → 0. Move P3 above the wrap update (so the compare sees no change) → 0. Drop the `$9380`-changed gate and add a Left/Right pair that lands on itself → count too high |
| `field-naming-cursor-sfx-throttled` | three moves on **consecutive** frames | `1 post(s) from 3 move(s)` | remove the `if (m[0x0D76]) return;` early-out → 3. Remove the P1 tick → 1 forever after (also kills the next check) |
| `field-naming-tick-drains` | one move, `wait:4`, one move | `2 post(s) from 2 move(s)` | remove the P1 tick → `1 post(s) from 2 move(s)`. This is the check that catches "one blip then the screen goes silent" |
| `field-naming-type-sfx-when-full` | seven type presses in a row (spaced ≥4 f) | `7 post(s)` while the buffer signature stays 5 chars | put the P4 call inside the `nmPos < 5` guard → `5 post(s)`. Anchored on the **event** (posts vs presses), not on a frame number |
| `field-naming-backspace-silent-at-0` | B, B, B with `nmPos == 0`, then one type + one B | `1 post(s) from 4 backspace-attempt(s)` — an invariant plus a **liveness counter**, per `[[grade-invariant-when-no-behavior]]` | move the P5 call outside the `nmPos > 0` guard → `4 post(s)`. A bare "0 posts" check would rot green the moment the naming arm stops running at all; the liveness attempt-count is what prevents that |
| `field-naming-reject-sfx` | type a name equal to another party member's (`CT_NAMEAS` unset; spell it with the drive script), then Start | `menusfx: cmd=18 id=01 pan=80 posted` **and** the screen still open at the last frame (`nmPhase == 3`) | delete P6 → no id=01. Use id `$00` → the expect string fails. Let the screen close on a duplicate → the still-open assertion fails |
| `field-naming-accept-silent` | type a unique name, Start | `0 post(s)` on the confirm frame + the name committed (`CtGetName`) | add any post to P7 → non-zero. Guarded by the same liveness pattern: the commit assertion proves the path ran |

Suite discipline: `python tools/regress.py --quick` before **and** after, and every new check must be
proved with `--mutate` (a surviving mutation is an obligation of proof, not a verdict —
`[[ct-sweepops-blindness-classes]]`). Because G5 widens the tick, re-run
`field-atbsel-cursor-sfx` and `field-atbsel-cursor-sfx-throttled` explicitly and record the result.

---

## Appendix — one-line reproductions

```
python tools/dis.py 2EB03 0x20                # the stager
python tools/dis.py 2EABA 0x50                # the C2/EAxx wrapper family
python tools/dis.py 2E1ED 0x50                # screen $0D sub-states 0,3,4 and the naming loop
python tools/dis.py 2E267 0x40                # cursor move + the CPX $9380 gate
python tools/dis.py 2E2F4 0x30                # type / backspace / the silent-at-0 branch
python tools/dis.py 2E2A0 0x50                # the four direction handlers (wrap + $9380/$9381)
python tools/dis.py 2E363 0x30 1 0            # commit + own-slot zeroing
python tools/dis.py 2E3E0 0x30 0 0            # the duplicate validator (m8/x16 in reality)
python tools/dis.py 2E984 0x80                # $0D1D/$0D1E latch builder + the sub-state-change gate
python tools/dis.py 28096 0x60                # the 5-command mode-open prelude
python tools/dis.py 28455 0x20                # the NMI $0D76 drain
python tools/dis.py 28270 0x40                # the $67/$68 dispatcher + the C2/8292 screen table
python tools/dis.py 70140 0x60 0 0            # the JSL C7:0004 command router
python tools/dis.py 7061C 0x30                # the mailbox -> $2140-$2143 shipper
python tools/dis.py 708E3 0x20 0 0            # the cmd-$70 pause handler (polarity + the BEQ no-op)
python tools/dis.py 905 0x30 0 0              # the C0 low-WRAM save/restore that clobbers $0D76

build/ct.exe --read "wram:00BE+1,wram:0067+1,wram:0A00+2,wram:1E10+2" --from saves/crononame.state
build/ct.exe --monitor "7E1E01:1,7E1E02:1,7E0D76:1,7E0F06:1,7E0F08:1,7E0F09:1,7E9380:2" \
    --from saves/luccaname.state --input "0:4,80:2,0:8,100:2,0:8,01:2,0:20" --diff --writer --frames 50
```
---

# ADDENDUM — the COMMIT, the trim and the reject buzz (second derivation pass)

This section supersedes rows 11-13 of §1 where they disagree. It was derived after the first pass,
because the id-`$01` buzz could not be ported without knowing how the ROM encodes a "blank" — and
the first pass got the empty-name verdict **wrong**. Everything here is either a `tools/dis.py`
listing / raw ROM bytes or a `build/ct.exe` measurement.

## A1. The alphabet: `$EF` and `$FF` are DIFFERENT bytes

* `$EF` = the typeable **SPACE**. It is grid cell 79 (row 7, col 9) of `$FF:C9AC` — the last key on
  the keyboard. `[dis]` raw `d[0x3FC9FB] = EF`; `$FF` appears **nowhere** in the 80-byte grid
  (the highest value is `$F3`).
* `$FF` = the **ERASED** sentinel, written only by the backspace: `C2/E300 LDA #FF / STA $0EFF,X /
  DEC $0F06` (X = `$0F06`, so it stamps `$0F00 + cursor-1`).
* `[orc]` live: 7 x Down + 9 x Right + A on `saves/luccaname.state` walks `$0F09` 01..07 and `$0F08`
  01..09 and writes **`$EF`** into `$0F00` (writer `C2:E348`).

The trim treats them asymmetrically, which is the whole point.

## A2. The trim `C2/E390`, verbatim

```
[dis] python tools/dis.py 2E390 0x50 1 0
  REP #30 / STZ $9890 / LDX #9890 / LDY #9892 / LDA #0003 / MVN $7E,$7E   ; $9890..$9895 := 0
  LDX #0F00 / LDY #98A0 / LDA #0005 / MVN $7E,$7E                        ; $98A0 := a copy of $0F00
  SEP #20 / LDX #0005
  E3B3: LDA $98A0,X / BEQ ->E3BF / CMP #EF / BNE ->E3C2 / STZ $98A0,X    ; TRAILING strip, walking DOWN
  E3BF: DEX / BPL ->E3B3
  E3C2: LDX $51 / TXY / LDA #EF / STA $00                                ; $00 = the byte to skip
  E3C9: LDA $98A0,X / BEQ ->E3DE / CMP $00 / BEQ ->E3D8 /
        STA $9890,Y / STZ $00 / INY                                      ; first stored byte -> stop skipping
  E3D8: INX / CPX #0005 / BCC ->E3C9
```
* the trailing strip zeroes **only `$EF`** and **ABORTS** (`BNE ->E3C2`) at the first byte that is
  neither 0 nor `$EF` — so a backspaced `$FF` is never stripped;
* the collapse drops **leading** `$EF` only: `STZ $00` after the first stored byte means interior
  blanks survive;
* `$51 = 0000` on this screen, measured on both states and never written across 700+ driven frames
  (`[orc]`), so both loops start at index 0. **Not proven for the whole game** — "constant 0 for the
  naming screen", not "never written".

## A3. The record and the blinding

`[orc] build/ct.exe --read "wram:2C23+48"` — identical on both naming states:

```
  id0 $2C23 A2CBC8C7C8 00 "Crono"   id4 $2C3B A5CBC8C000 00 "Frog"
  id1 $2C29 ACBACBC5BE 00 "Marle"   id5 $2C41 A0D2C5BA00 00 "Ayla"
  id2 $2C2F ABCEBCBCBA 00 "Lucca"   id6 $2C47 ACBAC0CECC 00 "Magus"
  id3 $2C35 B1C8BBC800 00 "Robo"    id7 $2C4D A4C9C8BCC1 00 "Epoch"
```
6 bytes each, **0-padded**; the 6th byte is never compared (`CPX #0005`) and can only ever be 0
(the `$9890` staging buffer is pre-zeroed and the pack writes indices 0..4 only).

`C2/E366 LDX $0F0C / C2/E369 STZ $0000,X` — an **8-bit** store (`C2/E364 SEP #20`), so it clears
**byte 0 only** of the character's own record. `[orc] --callrom` write log: the routine's very first
write is `w 7E2C2F = 00 (by C2:E36C)`, and the record then reads `00 CE BC BC BA`.

**The blinding is NOT undone on a reject.** After a buzzed attempt on Lucca's screen `$2C2F` stayed
`00 CE BC BC BA 00` for the rest of the run `[orc]`. Only an accepted name (the 6-byte MVN) restores it.

## A4. Rows 11-13 of §1, CORRECTED

| # | event | verdict | why |
|---|---|---|---|
| 11 | a name equal to ANOTHER record | **buzz id `$01`, screen STAYS OPEN** — CONFIRMED, verified live twice (records 0 and 6 typed on Lucca's screen): `$1E00-03 = 00 01 80 0D` on the Start frame, no teardown for the next 93 frames | `C2/E3E0` falls out of its 5-byte loop into `C2/E3FB JSR C2/EACC` |
| 12 | ~~empty name -> rejected, SILENT~~ | **WRONG, and it splits in two** (below) | — |
| 12a | **five typed SPACES** (`$EF` x5) | **buzz id `$01`**, screen stays open | the trim collapses them to an all-zero `$9890`, which matches the **own record blinded at `C2/E369`**: `CMP` equal -> `C2/E3F0 ORA #00` -> `C2/E3F2 BEQ ->E3FB`. `[orc]`: `$9890 = 000000000000`, `$1E01 = 01`, `$0D76 = 03` by `C2:EB15` |
| 12b | **five BACKSPACES** (`$FF` x5) | **SILENT ACCEPT**, and the record is committed as `FF FF FF FF FF 00` | `$FF` survives the trim (A2), so `$9890[0] = FF` matches no record's byte 0 -> eight `BNE`s -> `C2/E40C TDC` = accept. `C2/EB03` is never entered at all |
| 13 | valid unique name | **SILENT ACCEPT** — CONFIRMED | `[orc]`: on the commit frame the record and `$9890` are written (`C2:E38D` / `C2:E3D5`) and the mailbox `$1E00-$1E0F` is **untouched, no writer tag, no `$0D76 = 3`** |

**`C2/E376` (`LDA $9890 / BNE ->E37F / LDA #FF`), the ROM's own "empty" reject, is DEAD CODE.** It
is reached only when the validator returned 0, but an all-zero `$9890` always matches the blinded own
record first and buzzes. Verified for charId 0 (own record is index 0, the very first compare) and
charId 2 (index 2, after two mismatches). The consequence for the port: **there is no silent empty
reject to implement** — the empty name's sound is the duplicate buzz.

And the reason a character can always re-accept its **own** unchanged name is not "it is excluded":
it is blinded, and any committable name has a non-zero byte 0, so the blinded record can only ever
match an empty candidate.

## A5. What the port does with this

`src/field.c`, the `nmOk` branch, runs the ROM's four steps in the ROM's order on `nmCt` — a
**byte-exact mirror of `$0F00`** (prefilled by the 6-byte record copy of `C2/E494`, a typed key
stores the raw grid byte of `C2/E330`, a backspace stamps `$FF`). ASCII cannot carry this: `$EF` and
`$FF` both decode to `' '`, and the trim's whole behaviour hangs on telling them apart.

Pinned by `field-naming-duplicate-reject-buzz` (id `$01` + the screen still open),
`field-naming-all-spaces-buzz` (the blinding is what makes it buzz — mutating the blinding away
kills the check) and `field-naming-backspaced-empty-accepts` (the `$FF` survives the trim —
mutating the trim to treat `$FF` as a blank kills the check).

## A6. Still NOT derived, after this pass

1. What ids `$00` and `$01` actually SOUND like. Unchanged from §4.1: the id is not range-checked and
   goes straight out of `$2141`. We port the bytes, not the semantics.
2. Whether `$51` is ever non-zero outside this screen (A2).
3. The keyboard auto-repeat cadence — the ROM has one and every repeat blips; ours moves on the key
   EDGE only. The producing code (`$0D1F`/`$0D20`, `C2/E9F0`) is still undisassembled.
4. Whether the naming screen has any other exit/cancel path: only A, B, the d-pad and START were driven.
5. What the `$FF` bytes committed by case 12b render as, and whether anything later sanitizes them.

## The picture, frame by frame (2026-09-25)

`tools/naming_cmp.py type` drives the new game's Crono naming screen on both sides -- the cartridge from
saves/crononame.state (settled, the prompt up), the port from `--newgame` joining at its frame 100 (both screens are
static there) -- through the prompt's dismissal, the keyboard cursor, typing, backspace and Start: 0 of 291 frames
(regress `menu-naming-screen-vs-oracle`). Two findings:

* **The naming screen sits at the cartridge's own lines**: the port's line y is the oracle frame's line y (0 px at
  offset 0), not the other menus' 8-line head.
* **Start's close is the shell's**: C2/E233 `JMP C2/82B2` -- every task killed (the image freezes), then C2/834D and
  C2/83CA's fade-out, -2 a frame from 15 with the first step on the Start's own frame (13, 11 .. 1, then 0): MEASURED,
  the cartridge's mean brightness 47.8 -> 41.0 34.7 28.3 22.0 15.6 9.1 2.7 -> 0 over those eight frames; black after
  it until the field is back (the resume lands on the port's sixteenth frame, as before). The port had faded the
  screen over sixteen frames.

## The screen is the SHELL's (2026-09-25)

Op C8 $C0+ reaches the menu through the same launcher as the shop: C0/0C76 (a call of the MAIN LOOP, C0/00A4) ->
C0/0CC7 `BIT #40` -> C0/0CCE's `DEC $19` loop (the field frozen) -> C0/0CEA (C0/0B4E, C0/011B) -> C0/0D09 `X = operand &
$3F`, `A = (operand ROL x3) & 3` = 3 -> C0/0D18 `JSL C2:8000` -> C2/801D `$0A00 = A`, C2/8020 `$0A01 = X`, C2/806B
`$71 = $0A01` -> C2/808F[3] = screen $0D; on the way back C0/0D1C's reload, C0/0D33's handshake, C0/2824's fade-in. So
the port runs it as the shell's screen $0D (`menuScreen == 0x0D`, src/field.c), with the shell's clock (menu_shell_timing.md):

| step | ROM | port |
|---|---|---|
| the park | C0/3757's first visit ($0125, $0117 bit 6) | CtSceneNaming >= 0 |
| the fade | C0/0CCE, 15 `DEC $19`, from C0/0C76's first run -- NOT on a C0/2848 ramp frame (its loop calls C0/00BF alone) | the shop's trigger, gated `!inLoadFade` (both C8 arms) |
| C .. S0 | C2/8000's init, C2/806B `$71 = $0A01` | menuShellF 0..16, `$71` at 15 |
| S0, substate 0 | C2/E1ED `LDA $299F / ORA $71 / STA / BNE` | the router |
| $299F|$71 = 0 | C2/E576 (the selector; no yield), `$68 = 2`, C2/83B2 | P = S0 |
| otherwise | C2/E201 -> C2/E40E (CPU the logical oracle rolls into 4 frames), `INC $68` = 1, C2/83B2 | P = S0 + 4 |
| the selector's A | C2/E24C: $2990 bit 6, `$68 = 3`, C2/8255 #74 (kills slots 2, 4, 5, 6 -- NOT a sound), JMP C2/83CA | fade -2 a frame from the A frame; Z = the frame it reaches 0 |
| Z+1 .. Z+6 | the dispatcher's yield (2826C), C2/E201 -> C2/E40E, `INC $68` = 4, C2/83B2 | black; P' = Z + 6 |
| the prompt | C2/E209 `LDX $00F0` (16-bit): any press -> C2/EAC2, $0D13 = 1D, $68 = 1 | from P'+1 |
| the keyboard | C2/E21E: C2/E984 (A/B dropped on a new substate's first call), C2/E267, Start -> C2/E363 -> C2/82B2 | the shell's close |

C2/E40E decides the prompt itself: C2/E528 `LDA #1D / LDX $299F / BNE` -- zero keeps `LDA #7F` (the prompt's windows,
`DEC $0D9A`) -- then $0D13 := it, $299F := it. So the prompt is the game's FIRST naming (the selector's flow), and every
later naming finds $299F non-zero.

C2/83D0's step $0D20 is ONE variable. The fade-in task (C2/83B2 -> slot 0, `LDA #02 / JSR C2/83D0`) survives the
selector's #74 kill, so an A inside the fade-in makes both loops step -2: MEASURED (A at P+6), $0D10 8 on the A frame
(+2, -2), then 4, 0.

MEASURED on the logical oracle: from power-on with a zeroed battery (Start twice at the title), `C8 C0` parks at f217 with
loc 0's $0119 still climbing (0F at f219), the first `DEC $19` at f220, C ($0A00 = 3) f235, $0067 = 0D f250, $00BE = 02
at f251 = S0 = P, $0D10 2 .. 15 over f253-260; A at f318: $0D10 0D .. 01, 00 at f325, $00BE = 04 and $0D13 = 7F at f331,
$0D10 2 .. 15 over f333-340; the prompt's dismiss at f399 ($00BE = 01, $0D13 = 1D). Marle's (s439_marle_choice): $0125
= C1 at f572, $0119 0E .. 00 f573-587, $0A00 = 0301 f588, $0067 = 0D f603, $00BE = 01 f608 = S0 + 4, $0D10 = 02 f610.

Graded frame by frame by tools/naming_cmp.py (type 0/291, boot 0/429, bootfast 0/119, marle 0/144 over the screen's own
frames). Still OPEN: the pictures are bakes (src/data/atbsel.h, the naming's window/prompt/portraits), not the ROM's
display lists; the field's return after the close is one frame early (the shell's reload window, as after the X menu).

UNDER THE RULE (NMI-on work never costs frames, user decision 2026-09-30, CLAUDE.md; the oracle's CT_ORACLE_NMION_HOLD=1
during the migration). C2/E40E has no wait inside: ~2.0M cycles of NMI-on CPU (5.6 frames of it) from the dispatcher's
wait C2/81CA `LDA $0D01 / BEQ` back to it. The "four frames" above were the old one-frame valve running that CPU in real
time (the valve oracle's trace: four ROLLED BACK frames at C2/EExx, C2/5Exx, then a held one; stretch-end C2/81CD ->
C2/81CA C=3.46M with the rollbacks' re-runs). Under the rule the whole build ends in the frame it began (stretch-end
C2/81CD -> C2/81CE, the vblank held 1,678,102 cycles, 0 rollbacks), so C2/E201's pass spawns the fade-in: P = S0 on a
direct open and P' = Z + 2 after the selector (src/field.c nameBuildFrames(): 4 under the valve, 0 under the rule).
MEASURED, rule oracle vs valve oracle, same drive: Marle's $00BE = 01 at f596 against f600 with everything before it
identical; the boot drive's $0D10 = 00 (Z) at f358 in both, $00BE = 04 / $0D13 = 7F at f360 against f364. The new
game's boot is also two frames shorter BEFORE the naming's park (C8 C0 at f215 against f217; the loader's C8 41 at f166
against f168): loc 1B1's `EA 00` after the title's reboot (C0/3C43 `JSL $C7:0004`) meets a busy SPC in C7/0A5A's
command loop -- 18,673 polls of `LDX #0400 / CMP $2140 / BEQ / DEX / BNE` with 19 re-posts, ~1.26M cycles -- which
the valve ran as two real-time frames and the rule's wait detector does not see as a wait (X counts down, so the CPU
never returns to an identical state): held, it costs nothing; C7/0A8B's plain `CMP $2140 / BEQ` after it is seen and
costs its frame (rule oracle: $0119 01 at f139, 02 at f141; valve: 02 at f143). That leg is before both parks and
naming_cmp aligns on the park, so no naming check sees it. It is an APU handshake by the rule's own text ("the waits
the ROM makes stay"), which the detector misses -- OPEN, a question for the rule (does a counted poll of the APU port
count as a wait?). On the port's side a --selftest run has no audio (field.c: no SPC, no priced sink) and charges that
JSL nothing at all; an interactive run prices it on the loader clock (src/audio.c C7/0A71, the VM's song freeze). The
port's --boot reaches the loader's park 53 frames after the title's Start, the valve oracle 53, the rule oracle 51: the
valve's two real-time frames had been covering a port-side +2 on the reboot -> loc 1B1 -> loc 0 leg (the port ramps
loc 1B1 1..5 with no gap where the cartridge ramps 01, gap, 02..04; which of the port's reboot / load / ramp frames
make the +2 is not measured here) -- left open, not changed here.
