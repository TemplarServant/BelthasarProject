# MENU ON THE OVERWORLD — entry / save / fade / exit (RE dossier)

Scope: the gap "the menu does not open on the OVERWORLD in our engine" (`src/field.c` gates the whole
X-menu entry behind `if (f.vm && !inWorld)`). Everything below is either a ROM address I disassembled
with `python tools/dis.py` or an oracle measurement I ran with `scratchpad/menu/ct_probe.exe`; the exact
commands and their real output are in §4. Anything I could not pin is in §6.

Companion dossiers (READ, not repeated here): `menu_entry.md` (the FIELD path C0/18D9→C2/8000 and the
menu program itself), `menu_open_items.md` §3-§4 (the Form/Save entry gates, already validated on
`saves/ow_w0.state`), `menu_phase2.md`, `menu_sfx.md`.

---

## 1. VERDICT

**The overworld does NOT run C0/18D9 at all** (`--hookpc 1920` from `saves/ow_w0.state`: `visits=0` on
every frame, CPU always in bank `$C2`). The overworld is its own module: a main loop at **C2/23CF** that
waits for vsync (`C2/046E`→`C2/0346`) and then `JMP ($23DB,X)` on **`$027C`**, while ALL per-frame game
logic runs inside the overworld NMI (`$7E:0500 = JML C2/015E`). The menu button is polled by the
**overworld player-object handler substate 1, `C2/3567`**, whose only gate is **`$027C == 1`**; it reads
the *remapped HELD* joypad word **`$00F8`**, and `BIT #$0040` (X) → **`C2/35F7 STA $027C = 5`**
(`BIT #$0001` = Y + storyline ≥ 0x49 → `$027C = 9`). On the next main-loop pass the dispatcher enters
**C2/251E**, which spawns a 1-step-per-frame **fade-out object** (script `C2/2EC1` = `28 01 52`, op 0x28 →
native handler `C2/20A2`, ramping `$001D` 0x0F→0 and then setting `$001C = 0x80` force blank), waits
**17 frames** (`LDX #0011 / JSR C2/0454`) — during which **the overworld object VM keeps running** —,
then kills NMI/DMA, calls **`C2/2E23` = six MVN block copies that stash 0x5BF7 bytes of live overworld
state into `$7F:2800-89FF`**, and finally `JSL C2/8000` with **A = 1** (`$027C==5`) or **A = 5**
(`$027C==9`) — the same menu program the field enters. On RTL it re-inits the PPU (`C2/0043`), re-installs
the overworld NMI/IRQ trampolines (`C2/0110`), **restores the six blocks byte-for-byte (`C2/2E72`)**,
clears the DMA queue (`C2/03EF`), rebuilds the overworld presentation (`C2/2C93`), spawns the fade-IN
object (`C2/2EC4` = `29 01 52`, op 0x29 → `C2/2105`), waits another 17 frames and sets `$027C = 1`
(`C2/2585`), re-entering the main loop at `C2/23CF`.
Unlike the field, the overworld entry **never writes `$0407`** — the play clock keeps ticking through the
whole menu — and because the poll is on the HELD word, **a held X re-opens the menu one frame after the
close** (measured: `$027C` 1 at f151, 5 again at f152).

**What the C engine must do:** delete the `!inWorld` condition on the menu entry, drive the menu block
from `CtWramSys(fieldWram())`/`CtWramGlb(fieldWram())` instead of `CtSceneMem7E(f.vm)`/`CtSceneMemGlb(f.vm)`
(they are the SAME image — `worldInit` does `w->wram = keepWram`), replace the field-only gates
`$011F/$0162/$0110` with the overworld gate on the world side, do **not** set `$0407` on the overworld
path, keep the world's ambient VM + camera + streamer TICKING under the 17-frame entry/exit fades while
the player mover stays inert, and make the overworld's own X poll a HELD test (the ROM's one-shot comes
from the state variable, not from an edge).

---

## 2. ROM TRACE

All disassembly below was produced with `python tools/dis.py <fileoff> <len> <m> <x>`; HiROM file offset
== `(bank-0xC0)<<16 + addr`. The overworld module runs **A 8-bit / X,Y 16-bit** for most of this code, so
the correct invocation is `... 1 0`; several blocks below were hand-decoded from the raw bytes because a
linear decoder desyncs across the `REP/SEP` pairs (noted where it matters).

### 2.1 The field handler is NOT reached — proof by execution hook

`C0/1920` (`python tools/dis.py 1920 60`) is the field's X test:

```
  1920: LDA  $00F6          ; DP=$0100 -> $00F6, the REMAPPED EDGE byte
  1923: BIT  #40            ; X
  1925: BNE  ->192F
  192F: LDA  $1F   / BNE    ; $011F != 0
  1934: LDA  $62   / BEQ    ; $0162 == 0
  1939: LDA  $10   / BEQ    ; $0110 == 0
  193E: LDA #FF / STA $0407 ; PAUSE the play clock
  1943: LDA $19 / BEQ/BMI   ; $0119 brightness
  1949: DEC  $19            ; ...synchronous fade loop
  1958: JSR  C0/0B4E        ; PPU quiesce
  195B: JSR  C0/011B        ; FIELD-STATE SAVE
```

`--hookpc 1920 --from saves/ow_w0.state` reports `visits=0` for 40 consecutive frames and the frame-boundary
PC is always `C2/0F73`, `C2/04C5`, `C2/0551`, `C2/2E3B`, … — i.e. bank `$C2`. **The overworld never enters
the field mainloop.** (Full output in §4.1.)

### 2.2 The overworld module: NMI does the work, the main loop is a `$027C` dispatcher

`$7E:0500` in `saves/ow_w0.state` reads `5C 5E 01 C2 5C 45 03 C2` = `JML C2/015E` (NMI) / `JML C2/0345`
(IRQ) — the overworld's own trampolines, installed by **C2/0110**:

```
  C2/0110: LDA #5C / STA $0500 / STA $0504
           LDX #015E / STX $0501 ; LDX #0345 / STX $0505
           LDA #C2  / STA $0503 ; LDA #C2  / STA $0507 ; RTS
```

The NMI `C2/015E` pushes state, `INC $4B` (the frame counter the main loop spins on), does the OAM/CGRAM
DMA and the whole `$2105-$2131` register upload, and **ends with the game tick**:

```
  C2/0316: SEP #20
  C2/0318: JSR  C2/0405        ; the queued VRAM/DMA transfers
  C2/031B: JSL  C2/8002        ; READ THE JOYPAD  (bank-C2 entry +2)
  C2/031F: LDA  $1C
  C2/0321: ORA  $1D            ; *** THE OVERWORLD'S INIDISP: $001C (force blank) | $001D (brightness)
  C2/0323: STA  $2100
  C2/0326: LDA  $4A / STA $420C
  C2/032B: JSR  C2/0C4D
  C2/032E: STZ  $4D
  C2/0330: JSR  C2/034D        ; OAM shadow clear
  C2/0333: JSR  C2/051D        ; the OBJECT ARRAY tick (player + ambient)
  C2/0336: JSR  C2/0CEA
  C2/0339: LDA #01 / TSB $4C
  C2/0344: RTI
```

⚠ **`$0119` is the FIELD's brightness shadow only.** The overworld's is the pair `$001C`/`$001D`
(`C2/031F-0323`). That is why a `--monitor 7E0119` across the overworld menu open reports *no change at
all* (§4.2) — a false negative that would read as "the ROM does nothing here".

The main loop (`python tools/dis.py 223A0 70 1 0` + hand decode of the table):

```
  C2/23CF: JSR  C2/046E        ; -> C2/0346: LDA $4B / CMP $4B / BEQ -3  == WAIT FOR NMI
  C2/23D2: TDC
  C2/23D3: LDA  $027C
  C2/23D6: ASL A / TAX
  C2/23D8: JMP  ($23DB,X)

  table C2/23DB (16-bit words, index = $027C):
    0 -> C2/23EF   1 -> C2/23EF   2 -> C2/24D5   3 -> C2/2402   4 -> C2/250D
    5 -> C2/251E   6 -> C2/258D   7 -> C2/23EF   8 -> C2/261D   9 -> C2/251E
```

`C2/23EF` (states 0/1/7) is the idle handler — it only drains a pending sound request and `BRA C2/23CF`:

```
  C2/23EF: LDA $1BEC / BEQ ->23CF / LDA #FF / STA $1BEC / JSL C7/0004 / STZ $1BEC / BRA ->23CF
```

`$027C = 3` is the location ENTRY (the A press, `C2/2402`), `$027C = 4` is a hard halt
(`C2/250D: SEI / NMI+DMA off / JSR C2/26F0 / JMP C2/250D`), `$027C = 6` is the Select screen
(`C2/258D`, its own inline stash, fade script `C2/2EC7`, 34-frame wait), **`$027C = 5` and `$027C = 9`
are BOTH the menu** (`C2/251E`).

### 2.3 The poll: C2/3567 (overworld player handler, substate 1)

The player object's handler head (hand-decoded from `0x23404`; a linear decode desyncs here):

```
  C2/3404: LDA $7E2980 / BPL ->340C / CLC / RTS      ; party slot 0 empty ($2980 bit7) -> nothing
  C2/340C: LDX $4E                                   ; $4E = the live object block pointer
  C2/340E: LDA $027E / CMP $027F / BEQ ->3419 / STZ $0002,X   ; mode changed -> reset substate
  C2/3419: LDA $0280 / CMP $0281 / BEQ ->3424 / STZ $0002,X
  C2/3424: TDC / LDA $027E / ASL / TAX / JMP ($342D,X)
           table C2/342D: 3439, 3479, 346D, 347B, 3489, 349B    ; index = $027E (0 = on foot)

  C2/3439: LDA $027C / CMP #01 / BEQ ->3449
  C2/3440:              CMP #00 / BEQ ->3449
  C2/3444: JSR C2/0E1D / CLC / RTS                   ; any other $027C: ANIM ONLY, no input
  C2/3449: LDA $0280 / ASL/ASL/ASL / STA $00
           LDX $4E / LDA $0002,X / ASL / ADC $00 / TAX / JMP ($345D,X)
           table C2/345D (bank = $0280*4 entries): 34F8, 3567, 36ED, 0000, ...
```

`C2/34F8` is substate 0 (INIT: `INC $0002,X`, seed `$0014/$0018,X` from `$0283/$0285`, …) and **falls
through into `C2/3567`**, substate 1 = the run handler:

```
  C2/3567: LDA  $027C
  C2/356A: CMP  #01
  C2/356C: BNE  ->35E0            ; *** THE GATE. not free-roam -> anim only, no input at all
  C2/356E: REP  #20
  C2/3570: LDX  $4E
  C2/3572: LDA  $00F8             ; *** the REMAPPED **HELD** joypad word (16-bit)
  C2/3575: BEQ  ->35B3            ; nothing held -> idle path
  C2/3577: BIT  #$0080 / BNE ->35E5   ; A   -> location entry ($027C = 3, after LDA $1B32 / BMI guard)
  C2/357C: LDA  $00F8
  C2/357F: BIT  #$0040 / BNE ->35F3   ; X   -> *** THE MENU
  C2/3584: BIT  #$0001 / BNE ->3605   ; Y   -> storyline gate, below
  C2/3589: REP  #20 / LDA $00F8
  C2/358E: BIT  #$0004 / BNE ->35FC   ; Select -> $027C = 6
  C2/3593: BIT  #$0800 / -> C2/3619   ; Up
  C2/359B: BIT  #$0400 / -> C2/3636   ; Down
  C2/35A3: BIT  #$0200 / -> C2/3653   ; Left
  C2/35AB: BIT  #$0100 / -> C2/3670   ; Right
  C2/35B3: (idle) SEP #20 / LDA $0028,X AND #7F STA $0028,X / INC $02B1 / ... idle-fidget ...
  C2/35E0: JSR C2/0E1D / CLC / RTS

  C2/35F3: SEP #20 / LDA #05 / STA $027C / BRA ->35B3      ; X   -> menu mode 1
  C2/35FC: SEP #20 / LDA #06 / STA $027C / BRA ->35B3      ; Sel -> screen 6
  C2/3605: SEP #20 / LDA $7F0000 / CMP #49 / BCC -> C2/3589 (JMP 3589)
           LDA #09 / STA $027C / BRA ->35B3                ; Y + storyline>=0x49 -> menu mode 5
```

Note the tail: after setting `$027C` the handler **branches into the IDLE path** (`C2/35B3`) — it clears
the moving bit in `$0028,X`, bumps `$02B1` and runs the anim VM — and returns; the mode switch only takes
effect at the main loop's next dispatch.

**Joypad word layout** (measured, §4.3). `$00F0`/`$00F6`/`$00FA` are EDGE words (cleared on the second
frame of a hold); `$00F2` (CT layout) and `$00F8` (config-REMAPPED layout) are HELD:

| button | LakeSnes bit | `$00F2` (CT held) | `$00F8` (remapped held) |
|---|---|---|---|
| Start | 0x0008 | 0x0001 | **0x0000 (dropped)** |
| Select | 0x0004 | 0x0002 | 0x0004 |
| Y | 0x0002 | 0x0004 | 0x0001 |
| B | 0x0001 | 0x0008 | 0x000A |
| R | 0x0800 | 0x0010 | 0x0010 |
| L | 0x0400 | 0x0020 | 0x0020 |
| **X** | 0x0200 | **0x0040** | **0x0040** |
| A | 0x0100 | 0x0080 | 0x0080 |
| Up/Down/Left/Right | 0x10/0x20/0x40/0x80 | 0x0800/0x0400/0x0200/0x0100 | same |

This is exactly the `$00F6` remap `menu_entry.md` recorded 8-bit (X→0x40 identity, Y→0x01, Sel→0x04,
B→0x0A), now pinned as the 16-bit pair with the d-pad in the high byte.

### 2.4 The menu block C2/251E (entry + exit, one straight-line routine)

Hand-decoded from `0x2251E` (bytes verified against the hexdump; a linear decode mis-reads `LDX #2EC1`):

```
  C2/251E: TDC / STA $4200                       ; NMI OFF
  C2/2522: LDA #C2 / LDX #2EC1 / JSR C2/04D9     ; spawn the FADE-OUT script object
  C2/252A: LDA #81 / STA $4200                   ; NMI ON (the fade runs in the NMI's object tick)
  C2/252F: LDX #0011 / JSR C2/0454               ; *** WAIT 17 FRAMES
  C2/2535: SEI
  C2/2536: TDC / STA $4200 / STA $420B / STA $420C   ; NMI off, HDMA off, DMA off
  C2/2540: STA $4A                               ; HDMAEN shadow = 0
  C2/2542: JSR C2/2E23                           ; *** SAVE the overworld state
  C2/2545: LDA $027C / CMP #05 / BNE ->2550 / LDA #01 / BRA ->2552
  C2/2550: LDA #05
  C2/2552: JSL C2/8000                           ; *** THE MENU  (A = 1 for X, 5 for Y)
  C2/2556: SEI
  C2/2557: JSR C2/0043                           ; PPU register re-init (DP=$2100)
  C2/255A: JSR C2/0110                           ; re-install the OW NMI/IRQ trampolines
  C2/255D: JSR C2/2E72                           ; *** RESTORE the overworld state
  C2/2560: JSR C2/03EF                           ; clear the DMA/VRAM request queue
  C2/2563: JSR C2/2C93                           ; rebuild the overworld presentation
  C2/2566: LDA #C2 / LDX #2EC4 / JSR C2/04D9     ; spawn the FADE-IN script object
  C2/256E: LDA $1B58 / BEQ ->2576 / INC $1B59
  C2/2576: LDA #03 / TSB $4C                     ; request OAM+CGRAM DMA on the next NMI
  C2/257A: LDA #81 / STA $4200                   ; NMI ON
  C2/257F: LDX #0011 / JSR C2/0454               ; *** WAIT 17 FRAMES (the fade-in)
  C2/2585: LDA #01 / STA $027C                   ; back to FREE ROAM
  C2/258A: JMP C2/23CF                           ; back into the main loop
```

`C2/0454` is the frame waiter, and it is also the only sound pump during the fades:

```
  C2/0454: PHX / JSR C2/0346 (vsync) / LDA $1BEC / BEQ ->0469 / LDA #FF / STA $1BEC
           JSL C7/0004 / STZ $1BEC / PLX / DEX / BNE ->0454 / RTS
```

`C2/04D9(A = script bank, X = script addr)` allocates a child block whose native handler is the object VM
`C2/0F63` and points it at the script:

```
  C2/04D9: STA $01 / STX $0A / LDX #0F63 / JSR C2/048A     ; C2/048A = alloc a $0C30-array block,
           REP #20 / TXA / SEC / SBC #0040 / TAX           ;   MVN 0x3A bytes of the parent into it
           LDA $0A / STA $0007,X                           ; script address
           SEP #20 / LDA $01 / STA $0009,X / STZ $000A,X   ; script bank / rate slot
```

### 2.5 The two fade scripts and their native handlers

ROM bytes at `C2/2EC1`: `28 01 52 | 29 01 52 | 00 2A 07 02 38 10 28 01 52 ...`

* **`C2/2EC1` = `28 01` `52`** — op 0x28 (rate 1), then op 0x52 (die/free the block).
* **`C2/2EC4` = `29 01` `52`** — op 0x29 (rate 1), then die.
* (`C2/2EC7` is the `$027C = 6` Select-screen fade; not this gap.)

The object-VM handler table is at **`C2/0F91 + op*2`**; `op 0x28 -> C2/158B`, `op 0x29 -> C2/1599`:

```
  C2/158B: SEP #20 / LDX #20A2 / JSR C2/048A / REP #20 / LDA #0002 / RTS   ; op 0x28: child handler C2/20A2, adv 2
  C2/1599: SEP #20 / LDX #2105 / JSR C2/048A / REP #20 / LDA #0002 / RTS   ; op 0x29: child handler C2/2105, adv 2
```

⚠ **`src/world.c` currently stubs both**: `case 0x28: adv = 2; break; // spawn NATIVE child (20A2): not
run here` and `case 0x29: adv = 2; break; // (set child draw param; visual only)`. `C2/20A2` is the
SCREEN FADE-OUT and `C2/2105` is the SCREEN FADE-IN. The comments are wrong; correcting them is part of
this gap's implementation.

**Fade-OUT, `C2/20A2`** (`python tools/dis.py 220C0 80 1 0` + the head at `0x220A2`):

```
  C2/20A2: TDC / LDA $0002,X / ASL / TAX / JMP ($20AB,X)     ; table: 0 -> 20AF, 1 -> 20DF
  C2/20AF: LDA $1BF6 / BIT #02 / BNE ->20D7                  ; another fade already running -> SEC/RTS
           LDA #02 / TSB $1BF6                               ; claim the mutex  (writer PC C2:20BB)
           LDX $4E / INC $0002,X
  C2/20C0: LDY $0007,X / STY $10 / LDA $0009,X / STA $12     ; the script long pointer
           LDY #0001 / LDA [$10],Y                           ; the RATE operand
           BNE ->20D9
           LDA #80 / STA $1C / STZ $1D / SEC / RTS           ; rate 0 = INSTANT black
  C2/20D9: STA $000A,X / STZ $0003,X
  C2/20DF: SEP #20 / LDX $4E
           LDA $0003,X / CMP $000A,X / BCC -> (CLC / RTS)    ; rate counter not reached
           STZ $0003,X
           LDA $1D / BEQ ->20FA
           DEC $1D                                           ; *** ONE BRIGHTNESS STEP (writer C2:20F6)
           BEQ ->20FA
           CLC / RTS
  C2/20FA: LDA #80 / STA $1C                                 ; *** FORCE BLANK (writer C2:20FE)
           LDA #02 / TRB $1BF6                               ; release the mutex (writer C2:2103)
           SEC / RTS
```

**Fade-IN, `C2/2105`**:

```
  C2/2105: TDC / LDA $0002,X / ASL / TAX / JMP ($210E,X)     ; table: 0->2116, 1->2152, 2->216F, 3->218D
  C2/2116: LDA $1BF6 / BNE ->218D                            ; any fade running -> abort
           LDA #01 / TSB $1BF6                               ; (writer C2:2120)
           LDX $4E / [script ptr] / LDY #0001 / LDA [$10],Y
           BNE ->2140
           STZ $1C / LDA #0F / STA $1D / LDA #03 / STA $0002,X / CLC / RTS   ; rate 0 = INSTANT full
  C2/2140: STA $000A,X / INC $0002,X
           LDA $1D / BEQ ->214D / INC $0002,X                ; already lit -> skip state 1
           STZ $0003,X / CLC / RTS
  C2/2152: LDA $1BF6 / BIT #02 / BNE ->218D
           LDA $0003,X / CMP $000A,X / BCC ->216D
           INC $0002,X / STZ $0003,X
           STZ $1C                                           ; clear FORCE BLANK
           INC $1D                                           ; 0 -> 1   (writer C2:216D)
  C2/216D: CLC / RTS
  C2/216F: LDA $1BF6 / BIT #02 / BNE ->218D
           LDA $0003,X / CMP $000A,X / BCC ->218B
           STZ $0003,X / INC $1D                             ; *** ONE STEP (writer C2:2185)
           LDA $1D / CMP #0F / BEQ ->218D
  C2/218B: CLC / RTS
  C2/218D: LDA #01 / TRB $1BF6 ...                           ; release (writer C2:2192)
```

`$1BF6` bit1 = fade-out in progress, bit0 = fade-in in progress (a mutual-exclusion mutex, both fades
abort if the other holds it).

### 2.6 The overworld state SAVE / RESTORE — C2/2E23 / C2/2E72

Six `MVN`s each way. (65816 `54 dd ss`: first operand byte = DEST bank, second = SOURCE bank; `X` = source
address, `Y` = dest address, `A` = count-1.)

| # | live region | bytes | stash |
|---|---|---|---|
| 1 | `$00:0000-00EF` (= `$7E:0000-00EF`, the DP scratch: `$1C`,`$1D`,`$4A`,`$4B`,`$4E`,`$67`,…) | 0x00F0 | `$7F:3F00-3FEF` |
| 2 | `$00:0700-1DFF` (OAM shadow `$0700-09FF`, object array `$0B30-1B2F`, OW vars `$1B32+`, mailbox `$1BE8-1BED`) | 0x1700 | `$7F:2800-3EFF` |
| 3 | `$7E:B000-B507` | 0x0508 | `$7F:4000-4507` |
| 4 | `$7E:4000-6FFF` (the world tile/map buffers) | 0x3000 | `$7F:4600-75FF` |
| 5 | `$7E:7200-7DFF` (the music-zone nibble grid) | 0x0C00 | `$7F:7600-81FF` |
| 6 | `$7E:7E00-85FF` (the OW location records `$7E7E03+`, table `$7E8480`) | 0x0800 | `$7F:8200-89FF` |

Total **0x5BF7 bytes (23.5 KB)**. `C2/2E72` is the exact inverse, same six regions, same sizes.
`$00F0-$00FF` (the joypad shadows) and `$0400-$0407` (the play clock) are deliberately **outside** every
region — the menu owns the joypad and the clock keeps running.

```
  C2/2E23: REP #20 / PHB
           LDX #0000 / LDY #3F00 / LDA #00EF / MVN dst=7F src=00
           LDX #0700 / LDY #2800 / LDA #16FF / MVN dst=7F src=00
           LDX #B000 / LDY #4000 / LDA #0507 / MVN dst=7F src=7E
           LDX #4000 / LDY #4600 / LDA #2FFF / MVN dst=7F src=7E
           LDX #7200 / LDY #7600 / LDA #0BFF / MVN dst=7F src=7E
           LDX #7E00 / LDY #8200 / LDA #07FF / MVN dst=7F src=7E
           PLB / SEP #20 / RTS
  C2/2E72: (identical, banks and X/Y swapped)
```

### 2.7 The resume sub-routines

```
  C2/0043: PHD / PEA $2100 / PLD                 ; DP = the PPU registers, then a literal re-init:
           $2101=60 $2102/03=0000 $2105=09 $2106=00 $2107=61 $2108=69 $2109=79 $210A=79 $210B=22 $210C=77 ...
           (the OVERWORLD's layer setup — the menu left mode 1 with its own map/char bases)
  C2/0110: (re-install $7E0500 = JML C2/015E, $7E0504 = JML C2/0345 — see 2.2)
  C2/03EF: STZ $5E / STZ $5F / LDX #F000 / STX $E0 / LDA #7E / STA $E2
           LDX #007F / STZ $60,X / DEX / BPL      ; clear the 0x80-byte VRAM-transfer request list
  C2/2C93: JSR C2/5775 / JSR C2/2BFB / LDA #A925 -> $7EA916 / JSR C2/2CC1 / JSR C2/295E /
           JSR C2/29BA / JSR C2/2902 / JSR C2/2930 / ($00=1) JSR C2/09C5 / ($00=2) JMP C2/09C5
           ; the overworld presentation rebuild (the counterpart of the field's C0/01A5 reload chain)
```

### 2.8 What the menu does differently when it was entered from the overworld

An exhaustive operand scan of the whole menu program (`$C2:8000-FFFF`) for `LDA/LDX $0100` and
`LDA long $00:0100` finds **exactly ONE site**:

```
  C2/9875: PHP / REP #30 / JSR C2/9988 / STZ $0D7C / STZ $0D7E
           LDA $7F00BA / BIT #0001 / BEQ ->9895 / DEC $0D7C / BIT #0002 / BNE ->9895 / DEC $0D7E
  C2/9895: STZ $0D32 / STZ $0D34 / LDA #FFFF / STA $0D36
  C2/98A1: LDA  $0100
  C2/98A4: CMP  #01F0
  C2/98A7: BCS  ->98B5           ; *** OVERWORLD -> $0D32 = $0D34 = 0
  C2/98A9: DEC  $0D32            ; field map -> $0D32 = FFFF
  C2/98AC: LDA  $7F01CE / BMI ->98B5
  C2/98B2: DEC  $0D34            ; field map with no save point -> $0D34 = FFFF
```

`$0D32`/`$0D34` are then read at only three places in the bank:

* `C2/CEDC  LDA $0D34` — the **Save screen entry gate** (already ported as `CtMenuSaveCanOpen`, and
  already validated on `saves/ow_w0.state` in `menu_open_items.md` §4.1: `$0067 = 06` **persists**).
* `C2/9969  LDA $0D34` — inside the menu-open computation chain.
* `C2/CB99  LDX $0D32` — picks the **current-song id** the menu carries: `LDA $02AE` (the OVERWORLD song
  shadow) when `$0D32 == 0`, `LDA $29AE` (the field song shadow) otherwise, into `$0F0E`
  (`C2/CB96-CBA1`), which the Config screen reads at `C2/BF5F`.

Nothing else in the menu program tests the location. **The ribbon options are not gated by location**;
Form is refused on `ow_w0` purely because the party has one free member (`menu_open_items.md` §3.2).

**The Save screen's slot TITLE is NOT a location field.** `C2/D26D-D288` reads the save file's
**storyline** byte and maps it through a threshold table:

```
  C2/D276: LDA $300603,X        ; slot+0x0603 = record 4 ($7F:0000) = STORYLINE
  C2/D27A: SEP #30 / LDX #1A
  C2/D27E: DEX / CMP $FFD024,X / BCC ->D27E     ; largest X with storyline >= FF/D024[X]
  C2/D285: STX $020C / JSR C2/D296              ; string table FF/D03E, bank FF
```
`FF/D024` = `[26 ROM bytes at 0x3FD024: python tools/dis.py 0x3FD024 26]`;
`FF/D03E[0..0x1A]` = "The Millennial Fair", "The Queen Returns", "The Queen is Gone", … , "No data".
Verified against two captures (§4.7): storyline 03 → "The Millennial Fair", storyline 0x15 → "The Queen
is Gone".

**The Save screen's ERA caption IS location-derived** (this is the one genuinely overworld-visible
difference, and it was previously unpinned). `C2/D3AD`:

```
  C2/D3AD: LDA $98C2            ; the Save screen's staged summary word = savefile+0x05F3 & 0x1FF
  C2/D3B0: LSR A / TAX          ;   (record 3 = $00:0100 -> slot+0x05F3; reader C2/D22C)
  C2/D3B2: LDA $FFD296,X        ; a 256-byte NIBBLE table covering all 512 locations
  C2/D3B6: AND #01FF / BCC ->D3BF / LSR A x4    ; odd location -> HIGH nibble
  C2/D3BF: SEP #20 / AND #0F                    ; -> era index 0..7
  C2/D3C3: CMP $0D77 / BEQ ->D3E7 / STA $0D77 / STA $020C
  C2/D3CE: LDX #5E00 / STX $0DC5
  C2/D3D4: LDX #D396 / STX $020D / LDA #FF / STA $020F / JSR C2/F90C
```
`FF/D396` (8 words, immediately after the nibble table `FF/D296-D395`):
`0 = "     ***"(blank) 1 = "Present" 2 = "Middle Ages" 3 = "Future" 4 = "Pre-historic" 5 = "Dark Ages"
6 = "Black Omen" 7 = "End of Time"`.
Table lookups: `loc 0x006 -> 1`, `loc 0x082 -> 2`, `loc 0x1F0 -> 1`, `0x1F1 -> 2`, `0x1F2 -> 3`,
`0x1F3 -> 4`, `0x1F4/5/6 -> 5`, `0x1F7+ -> 0`. Both halves validated by capture + WRAM read (§4.7).

---

## 3. RAM CONTRACT

| address | width | role | proof |
|---|---|---|---|
| `$7E:027C` | 1 | **overworld module state** = the main-loop dispatch index (`C2/23D3`). 0/1/7 idle, 1 = FREE ROAM (the only state that polls input), 3 = location entry, 4 = halt, **5 = menu mode 1**, 6 = Select screen, 8 = ?, **9 = menu mode 5** | `--monitor 7E027C:1 --writer`: `f11 7E027C=05*(C2:35FA)` — the PC after `C2/35F7 STA $027C`; `f143 7E027C=01*(C2:258A)` — the PC after `C2/2587 STA $027C` |
| `$7E:027E` / `$027F` | 1+1 | player mode / previous mode (`C2/340E`), selects the handler table `C2/342D`; 0 = on foot | `--read 7E027C+12` on `ow_w0`: `01 00 00 00 00 00 …` |
| `$7E:0280` / `$0281` | 1+1 | player sub-mode bank / previous (`C2/3419`, `C2/3449` `$0280<<3`) | same read: `$0280 = 00` |
| `$7E:0283` / `$0285` | 2+2 | overworld player X / Y (already in `MEMORY.md`) | `--peek` `owpos=(440,280)` vs `$0283 = 01B8`, `$0285 = 0118` |
| `$7E:02B1` | 1 | idle-fidget counter; INC'd only on the `C2/35B3` idle path — **frozen while `$027C != 1`** | wram diff f13 vs f29: `7E:02B1` unchanged across the entry fade |
| `$7E:001C` | 1 | **overworld force-blank byte** (`0x80` = blank) | `C2/031F LDA $1C / ORA $1D / STA $2100`; writers `C2:20FE` (blank), `C2:216D` (clear) |
| `$7E:001D` | 1 | **overworld brightness nibble 0..0x0F** | writers `C2:20F6` (`DEC $1D`), `C2:2185` / `C2:216D` (`INC $1D`) |
| `$7E:0119` | 1 | the FIELD's brightness — **NOT used by the overworld** | `--monitor 7E0119:1 --diff` over 202 frames of an overworld open: **1 row, no change** |
| `$7E:0407` | 1 | play-clock pause. Field sets `FF` (`C0/1940`); **the overworld NEVER writes it** | `--monitor 7E0407:1 --writer` on `ow_w0`: `00` on every row while `$0400` ticks (writer `C2:85BC`) every frame |
| `$7E:00F6`,`$00F7` | 2 | remapped **EDGE** joypad word (the field's X test `C0/1920`) | sweep §4.3: `0x40` on frame 1 of the hold, `0x00` on frame 2 |
| `$7E:00F8`,`$00F9` | 2 | remapped **HELD** joypad word (the overworld's X test `C2/357C`) | same sweep: `0x40` on both frames of the hold |
| `$7E:1BF6` | 1 | fade mutex: bit1 = fade-out running, bit0 = fade-in running | writers `C2:20BB` (set 2), `C2:2103` (clear 2), `C2:2120` (set 1), `C2:2192` (clear 1) |
| `$7E:1BEC` | 1 | pending-sound-request flag drained by `C2/0454` during both fades | `C2/0458-0466`; measured untouched across the entry (§4.6) |
| `$7E:1BE8-1BED` | 6 | the overworld sound mailbox — inside saved region 2, so cleared by the menu and **restored** | `--monitor 7E1BE8:6 --writer`: `f32` zeroed, `f91 …*(C2:2E8D)` restored by the `C2/2E8A` MVN |
| `$7E:004B` | 1 | overworld frame counter, INC'd by the NMI (`C2/0170`), spun on by `C2/0346` | `--monitor 7E004B:1 --writer` → `*(C2:0172)` every frame until the menu takes the NMI |
| `$7E:004A` | 1 | HDMAEN shadow, zeroed at `C2/2540` before the menu | `C2/0326 LDA $4A / STA $420C` |
| `$7E:004E` | 2 | live object-block pointer the handlers index (`LDX $4E`) | `--read 7E004E+2` = `10B0` |
| `$7E:0067` | 1 | menu screen id — inside saved region 1, so it is **restored to the overworld's 0** on exit | `--monitor` writer `(C2:2E81)` at f91 |
| `$7F:2800-89FF` | 0x5BF7 | the overworld state stash | `C2/2E23` / `C2/2E72` MVN operands (§2.6) |
| `$7E:0D32` / `$0D34` | 2+2 | menu's "on a field map" / "no save point here" flags, derived from `$0100` at `C2/98A1` | §2.8 |
| `$7E:0F0E` | 1 | menu's current-song id: `$02AE` (overworld) vs `$29AE` (field), chosen by `$0D32` at `C2/CB99` | §2.8 |
| `$7E:98C2` | 2 | Save screen staged LOCATION word for the highlighted slot (`savefile+0x05F3 & 0x1FF`) | read from the capture WRAMs: `ow_saved 01F0`, `cjs_save2 01F1` (§4.7) |
| `$7E:0D77` | 1 | the drawn era index (`C2/D3C8`) | same WRAMs: `01` / `02`, matching `FF/D296` |

Not saved / not restored by `C2/2E23`: `$7E:0100-01FF`, `$0200-06FF` (incl. the play clock `$0400-0407`),
`$1E00-1FFF` (the field mailbox page), `$2000-2FFF` (party/sheets/inventory — the menu EDITS these on
purpose), `$7F:0000-01FF` (story flags). Confirmed by the pre/post WRAM diff (§4.5).

---

## 4. ORACLE MEASUREMENTS

All commands run from the repo root with `scratchpad/menu/ct_probe.exe` (never `build/ct.exe`).

### 4.1 The field handler is never reached on the overworld

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --hookpc 1920 \
  --from saves/ow_w0.state --btnscript "0:20,200:2,0:60" --frames 40
```
```
hookpc: C0/1920 over 40 frames from saves/ow_w0.state
  f0    visits=0   end=C2/0F73  $6D:  vPos:
  f1    visits=0   end=C2/0F73
  f2    visits=0   end=C2/75D1
  f3    visits=0   end=C2/04C5
  ...
  f38   visits=0   end=C2/2E3B
  f39   visits=0   end=C2/2E53
```
40/40 frames `visits=0`, CPU in bank `$C2` at every frame boundary.

### 4.2 The FALSE NEGATIVE that `$0119` produces

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --monitor "7E0119:1,7E0407:1,7E0067:1,7E0100:2" --from saves/ow_w0.state \
  --btnscript "0:20,200:2,0:180" --frames 200 --diff --writer
```
```
  f0      7E0119=00*  7E0407=00*  7E0067=00*  7E0100=F001*
monitor: 4 region(s), 1 row(s)
```
One row over 202 frames — yet the same script *does* open the menu (§4.4). `$0119` is the wrong address
on the overworld; `$001C`/`$001D` is the right one.

### 4.3 Joypad layout sweep (which word, which bits)

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --monitor "7E00F0:12,7E027C:1" \
  --from saves/ow_w0.state --btnscript "0:6,200:6,0:6,100:4,0:4,80:4,0:6" --diff
```
```
  f7      7E00F0=400040004000400040004000*  7E027C=05*      <- X down: ALL six words = 0x0040
  f8      7E00F0=000040000000000040000000*  7E027C=05       <- frame 2 of the hold: only $F2 and $F8 keep it
  f19     7E00F0=800080008000800080008000*                  <- A = 0x0080
```
Full remap sweep (Y/Select/B/Start/L/R/Up), `--btnscript "0:4,2:3,0:3,4:3,0:3,1:3,0:3,8:3,0:3,400:3,0:3,800:3,0:3,10:3,0:3,…"`:
```
  f5   00F0=040004000400010001000100   Y      -> $00F2=0x0004  $00F8=0x0001    ($027C unchanged, storyline 03 < 0x49)
  f11  00F0=020002000200040004000400   Select -> $00F2=0x0002  $00F8=0x0004    7E027C=06*   <- Select opens screen 6
  f17  00F0=0800080008000A000A000A00   B      -> $00F2=0x0008  $00F8=0x000A
  f23  00F0=010001000100000000000000   Start  -> $00F2=0x0001  $00F8=0x0000    (Start is DROPPED by the remap)
  f29  00F0=200020002000200020002000   L      -> 0x0020
  f35  00F0=100010001000100010001000   R      -> 0x0010
  f41  00F0=000800080008000800080008   Up     -> 0x0800
```

### 4.4 The full entry timeline (writer PCs)

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --monitor "7E001C:2,7E027C:1,7E0067:1,7E1BF6:1" --from saves/ow_w0.state \
  --btnscript "0:10,200:2,0:70,1:2,0:120" --diff --writer
```
(the monitor prints the bytes in address order, so `7E001C=000F` means `$1C = 0x00`, `$1D = 0x0F`)
```
  f0      7E001C=000F*  7E027C=01*  7E0067=00*  7E1BF6=00*
  f11     7E001C=000F   7E027C=05*(C2:35FA)     7E1BF6=00     <- X (held f10-f11) -> $027C = 5
  f12     7E001C=000F   7E027C=05               7E1BF6=02*(C2:20BB)   <- C2/251E dispatched; fade obj spawned
  f13     7E001C=000E*(C2:20F6)                                        <- brightness 0F -> 0E
  ...     (one step per frame)
  f26     7E001C=0001*(C2:20F6)
  f27     7E001C=8000*(C2:20FE)   7E1BF6=00*(C2:2103)                  <- $1C = 0x80 FORCE BLANK, mutex released
  f33     7E001C=0000*                                                 <- menu program owns the machine
  f91     7E001C=8000*(C2:2E81)   7E0067=00(C2:2E81)  7E1BF6=00(C2:2E8D)  <- C2/2E72 RESTORE (MVN writers)
  f127    7E1BF6=01*(C2:2120)                                          <- fade-in object claims the mutex
  f128    7E001C=0001*(C2:216D)                                        <- $1C cleared, $1D = 1
  f129..f142  7E001C=0002..000F*(C2:2185)                              <- one step per frame
  f142    7E1BF6=00*(C2:2192)
  f143    7E027C=01*(C2:258A)                                          <- C2/2585: back to FREE ROAM
```
Entry: X→menu program = **17 frames** (f12..f28), of which the brightness visits 0F,0E,…,01 then blank.
Exit: restore at f91, fade-in 15 steps f128..f142, free roam at f143.

### 4.5 The overworld is FROZEN across the menu and restored byte-for-byte

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/ow_w0.state scratchpad/menu/ow_f29 --btnscript "0:10,200:2,0:17"
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/ow_w0.state scratchpad/menu/ow_f95 --btnscript "0:10,200:2,0:70,1:2,0:11"
```
(f29 = just after `C2/2E23`; f95 = just after `C2/2E72`) — diff of the six saved regions:
```
SAVED   7E:0000-00EF   size   240  diffs     5  ['0008','0009','0010','0011','0012']   <- the MVN/draw scratch
SAVED   7E:0700-1DFF   size  5888  diffs     0
SAVED   7E:B000-B507   size  1288  diffs     0
SAVED   7E:4000-6FFF   size 12288  diffs     0
SAVED   7E:7200-7DFF   size  3072  diffs     0
SAVED   7E:7E00-85FF   size  2048  diffs     0
total saved-region diffs: 5
UNSAVED 7E:0400-0407   diffs 2  ['0400=1A/0B','0401=05/06']    <- the play clock KEPT TICKING
UNSAVED 7E:0100-011F   diffs 0
UNSAVED 7E:2980-2982   diffs 0
UNSAVED 7E:00F0-00FF   diffs 0
```
The five `$0000-00EF` diffs are `$08/$09/$10/$11/$12`, the scratch the *later* MVNs and `C2/2C93` use
after region 1 has been restored — not overworld state.

### 4.6 …but the world DOES keep ticking under the 17-frame entry fade

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/ow_w0.state scratchpad/menu/ow_f13 --btnscript "0:10,200:2,0:1"
```
diff `ow_f13` (f13, the frame the fade starts) vs `ow_f29` (f29, the frame the menu program starts):
```
objects 7E:0B30-1B2F     diffs   105 / 4096   ['0C33=AC->BC','0C3B=D2->A6','0C3E=09->03', ...]
OAM shadow 7E:0700-091F  diffs    11 /  544
cam/scroll 7E:0020-002F  diffs     2 /   16   ['0024=CC->C8','002A=1A->1C']     <- BG3 cloud scroll moved
player 7E:0283-0286      diffs     0 /    4                                     <- the PLAYER is inert
idle 7E:02B1             diffs     0 /    1                                     <- ...and so is $02B1
```
Sound mailbox across the same span (`--monitor "7E1BE8:6,7E1E00:4" --diff --writer`):
```
  f0   7E1BE8=184580FF0000*   7E1E00=001EE80D*
  f31  7E1BE8=184580FF0000    7E1E00=0001FF0D*(C7:0196)   <- the menu's own C2/8096 prelude
  f32  7E1BE8=000000000000*                               <- menu clobbers $0700-1DFF
  f91  7E1BE8=184580FF0000*(C2:2E8D)                      <- C2/2E72 restores it
```
No overworld sound is requested by the entry fade itself.

### 4.7 What the menu shows (Save screen, both eras)

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/ow_w0.state scratchpad/menu/ow_menu1 --btnscript "0:20,200:2,0:60"
python tools/_fbpng.py scratchpad/menu/ow_menu1 scratchpad/menu/ow_menu1.png
```
→ the full main menu over the overworld: Crono LV 1, HP 70/70, MP 8/8, TIME 00:02, 600G, ribbon at
option 0, Equip preview panel — identical shape to a field open.

```
SC5A="0:20,200:2,0:70,80:2,0:6,80:2,0:6,80:2,0:6,80:2,0:6,80:2,0:6,100:2,0:80"
scratchpad/menu/ct_probe.exe ... --peek saves/ow_w0.state scratchpad/menu/ow_save --btnscript "$SC5A"
```
→ the SAVE screen opens (3 × "No data") — the `$0100 >= 0x1F0` gate passing, as `menu_open_items.md` §4.1
already pinned.

Save-then-look (the emulator's SRAM is in-memory only; `src/main.c` never persists an `.srm`):
```
# ow_w0 (loc 0x1F0, storyline 03) -> A on slot 1
... --peek saves/ow_w0.state scratchpad/menu/ow_saved \
  --btnscript "0:20,200:2,0:70,80:2,0:6,...,100:2,0:70,100:2,0:150"
# com_jogo_salvo (loc 0x1F1 = OVERWORLD world 1, storyline 0x15) -> A on slot 1, and Down+A on slot 2
```
| capture | slot title | era caption | `$7E:98C2` | `$7E:0D77` |
|---|---|---|---|---|
| `ow_saved` | "The Millennial Fair" (storyline 03) | **Present** | `01F0` | `01` |
| `cjs_save` (the ROM's own save) | "The Queen is Gone" (storyline 0x15) | **Middle Ages** | `01F1` | `02` |
| `cjs_save2` (my save into slot 2) | "The Queen is Gone" | **Middle Ages** | `01F1` | `02` |
| `ow_save` (all slots empty) | "No data" | — | `0000` | `FF` |

`cjs_save2` is the discriminator: it was written with the SAME live `$7E:2991 = 0xA0` as `ow_saved`
(`--read 7E2991+1` on both states) but a different location, and it renders a different era — so the era
keys on the LOCATION, not on the saved config byte. `FF/D296[loc>>1]` nibble = 1 for `0x1F0` and 2 for
`0x1F1`, matching `$0D77` exactly.

### 4.8 HELD, not edge — a held X re-opens the menu (overworld) but not (field)

```
scratchpad/menu/ct_probe.exe ... --monitor "7E027C:1,7E001C:2" --from saves/ow_w0.state \
  --btnscript "0:10,200:80,201:2,200:200" --diff --writer        # X held throughout, B tapped at f90
```
```
  f11   7E027C=05*(C2:35FA)          <- opened
  f151  7E027C=01*(C2:258A)          <- C2/2585 free roam
  f152  7E027C=05*(C2:35FA)          <- *** RE-OPENED one frame later, X still held
```
The field, same shape, does NOT:
```
scratchpad/menu/ct_probe.exe ... --monitor "7E0407:1,7E0119:1" --from saves/field_pre_menu.state \
  --btnscript "0:10,200:80,201:2,200:220" --diff --writer
  f11   7E0407=FF*(C0:1943)  7E0119=0E*(C0:194B)     <- opened
  ...
  f137  7E0407=00            7E0119=0F*(C0:282B)     <- resume complete
  (32 rows total; $0407 stays 00 for the remaining ~180 frames with X held — no re-open)
```
That run also pins the field/overworld clock difference: on the field `$0407 = FF` for the whole menu
(writer `C0:1943`), on the overworld it never leaves `00`.

### 4.9 Miscellaneous reads used above

```
scratchpad/menu/ct_probe.exe ... --read "7E0500+16,7E027C+8,7E00F0+12" --from saves/ow_w0.state
  7E0500+16 = 5C5E01C2 5C4503C2 0000000000000700          -> JML C2/015E (NMI), JML C2/0345 (IRQ)
scratchpad/menu/ct_probe.exe ... --read "7E027C+12,7E2980+3,7E004E+2,7E0400+8" --from saves/ow_w0.state
  7E027C+12 = 01 00 00 00 00 00 00 B8 01 18 01 00        -> $027C=1 $027E=0 $0280=0 $0283=01B8 $0285=0118
  7E2980+3  = 00 80 80                                    -> party = Crono only
  7E004E+2  = B0 10                                       -> $4E = 0x10B0
scratchpad/menu/ct_probe.exe ... --read "7E2991+1,7E0100+2,7F0000+1" --from saves/{ow_w0,p600,p300,com_jogo_salvo}.state
  ow_w0          2991=A0  0100=01F0  storyline=03
  com_jogo_salvo 2991=A0  0100=01F1  storyline=15      <- ⚠ this state is on the OVERWORLD, not the field
```

⚠ Correction to the task brief: **`saves/com_jogo_salvo.state` is an OVERWORLD state** (`$0100 = 0x1F1`
= world 1, 600 A.D.), not a field one. It is a second, independent overworld fixture with a real
ROM-written save in SRAM slot 0 — and the X-open script works on it unchanged.

---

## 5. IMPLEMENTATION SPEC (for `src/field.c` + `src/world.c`)

Facts the spec rests on, all from §2-§4. The C engine's overworld already runs on the **host** WRAM
(`worldInit`: `w->wram = CtWramSys(fieldWram())`, `src/field.c:217/225`), which is the *same* pointer
`CtSceneMem7E(f.vm)` returns on the field (`ct_vm.c:5127` `sc->mem->s`; `CtWramGlb` == `CtSceneMemGlb`,
`ct_vm.c:349/5153`). So the menu module needs no new data plumbing at all.

### S1 — the entry gate (`src/field.c`, the block that opens `if (f.vm && !inWorld)`)

> Anchor on the STRING `if (f.vm && !inWorld)`, not on a line number: while this dossier was being
> written another change moved that block (it was at ~7941 when I read it, ~7583 afterwards).

Today: `if (f.vm && !inWorld) { … if (menuPhase == 0 && … CtSceneExplore(f.vm) && s7[0x162]==0 && s7[0x110]==0 …) }`.

Required:

1. Drop `!inWorld` and drop the hard `f.vm` requirement. Hoist
   `uint8_t* s7 = CtWramSys(fieldWram());` and `uint8_t* glb = CtWramGlb(fieldWram());` above the
   branch; use those everywhere the menu block currently uses `CtSceneMem7E(f.vm)` /
   `CtSceneMemGlb(f.vm)` (the `CtMenuHost.mem`, `CtMenuSaveCanOpen`, `CtMenuFormCanOpen`,
   `CtMenuClockTick`, `fieldMenuSfxTick`, the `$2980` reads, the Item/Form writes).
2. **Two different gates, because the ROM has two different handlers.**
   * `!inWorld` → keep exactly what is there (`C0/192F`: `CtSceneExplore` = `$011F != 0`,
     `s7[0x162]==0`, `s7[0x110]==0`) and keep the EDGE test (`C0/1920` reads `$00F6`, an edge byte).
   * `inWorld` → the ROM's chain is, in order (§2.3): `$7E2980` bit7 clear · `$027E == 0` ·
     `$0280 == 0` · `$027C == 1` · `$00F8 != 0` · not A · X bit `0x40`. Of these, the only one our
     engine has a live equivalent for is the **module-state gate**. See S1a.
3. `menuPhase == 0 && !battlePhase && !gameOver && menuFb` stay as they are (they are the engine's own
   "is another modal up" conditions, and the ROM expresses the same thing through `$027C`).

**S1a — ⚠ THE `$027C` TRAP.** `src/field.c` (world block, ~line 7717) writes `wr[0x027C] = 0;
wr[0x027E] = 0;` **every frame**. A literal port of `CMP #01 / BNE` would therefore refuse the menu
forever — the same class of failure as `$1C00` in `ct-invoke-prologue-guards`. Two honest options,
in order of preference:

* **(preferred, structural)** make the world loop the ROM's `$027C` machine: free roam writes
  `$027C = 1` (it is what the ROM's own world-init script pokes via op `0x13` at `C2/12C8` — the port's
  own comment at field.c:7717 says the bake replaced that script), the menu entry writes `$027C = 5`
  from the poll, `menuPhase` runs while `$027C == 5`, and the resume writes `$027C = 1` (`C2/2585`).
  Then the gate IS `s7[0x027C] == 1` and the one-shot property is the ROM's own.
* **(minimum, and say so in the comment)** gate on `menuPhase == 0` alone, which is what `$027C == 1`
  falsifies in the ROM, and note in the code that `$027C` is baked to 0 in this engine so the canonical
  byte cannot be read. Do **not** invent a new flag.

**S1b — HELD vs EDGE.** The overworld poll is on `$00F8`, the HELD word (`C2/3572`/`C2/357C`), and the
measured consequence is that a held X re-opens the menu one frame after the close (§4.8) — the field
does not (its `C0/1920` reads the EDGE word `$00F6`). Reproduce that: on `inWorld` use `xHeld` (not
`xEdge`); on the field keep `xEdge`. If the engine keeps the edge on both for ergonomics, that is a
DOCUMENTED divergence, not a derivation.

**S1c — do NOT write `$0407` on the overworld path.** `s7[0x407] = 0xFF;` (the current line) is
`C0/1946`, field-only. The overworld never touches it and the play clock keeps ticking across the whole
menu (§4.4/§4.5). So: `if (!inWorld) s7[0x407] = 0xFF;` — and correspondingly skip the
`s7[0x407] = 0` writes at menuPhase 1→2 and 3→0 when the menu was opened from the overworld. Nothing
else changes: `CtMenuClockTick` already no-ops on `$0407 != 0` and must simply never be paused here.

### S2 — the entry/exit "save" (`C2/2E23` / `C2/2E72`) is a NO-OP for us

The ROM copies 0x5BF7 bytes because the menu program overwrites `$7E:0000-1DFF`, `$7E:3000-DFFF` and
`$7F:2000-2AFF` (`menu_entry.md` §3). **Our `CtMenu*` module writes nothing outside `$7E:2400-2FFF`
(Item/Form) and the SRAM image.** The correct port is therefore *no copy at all*, with the ROM citation
in the comment — exactly the argument the existing field exit block already makes ("in OUR engine nothing
was clobbered"). Concretely: the `mSave` block (`$0102-4` + PC positions + `$0C00` prios) is field-only;
on the overworld skip it entirely and leave `wd` (mover/camera/streamer/vram) untouched. The overworld's
resume is then `C2/0043` + `C2/0110` + `C2/03EF` + `C2/2C93` reduced to nothing, because our PPU/NMI/DMA
state was never handed to the menu.

⚠ One thing that IS load-bearing: `$7E:0700-1DFF` is inside saved region 2, so the ROM's overworld object
array, OAM shadow and sound mailbox survive the menu **unchanged**. Our engine must likewise not disturb
`wd.wram[0x0B30..0x1B2F]` while `menuPhase != 0`. The existing `if (inWorld && !menuPhase)` guard already
achieves this — keep it.

### S3 — the fades (the one place the overworld genuinely differs)

The field's fade is a synchronous `C0/EC60` loop with the field halted, which is why the engine replays a
frozen `menuFb` under a brightness ramp. **The overworld's is not.** `C2/252F` / `C2/257F` run
`LDX #0011 / JSR C2/0454` — 17 real frames in which the NMI, and therefore `C2/0333` (the object array),
keeps running: measured 105 object bytes, 11 OAM bytes and the BG3 cloud scroll all changed under the
entry fade, while the player position and `$02B1` did not (§4.6).

Required behaviour while `inWorld && (menuPhase == 1 || menuPhase == 3)`:

* run the world's ambient/camera/streamer frame as usual (`CtWorldAmbientFrame`, `CtWorldStreamFrame`,
  the camera), and re-render the world into `fb` each frame — do **not** replay `menuFb`;
* do **not** run the player mover / facing / parity / `$02B1` (`C2/356C`: `$027C != 1` → anim only);
* apply the brightness to the LIVE frame:
  - entry (`C2/20A2`, rate 1): frames 1..14 after the dispatch → brightness `0x0F - k`;
    frame 15 → force blank (0); frames 16,17 → still blank. Total **17**.
  - exit (`C2/2105`, rate 1): frame 1 → clear blank and brightness 1; frames 2..14 → 2..0x0F;
    frames 15..17 → hold 0x0F. Total **17**, then `$027C = 1`.
  - the ramp lives in `$001C` (0x80 = blank) / `$001D` (0..0x0F) if the engine wants the canonical bytes.
* the engine's field ramp is 15 steps over 15 frames; the overworld's is 15 brightness levels inside a
  **17**-frame window. Use 17 on the overworld — it is `LDX #0011`, read straight from the ROM.

Optional but canonical: implement world.c ops **0x28 / 0x29** properly (they are currently stubbed
`adv = 2` with wrong comments) so the fade is what the ROM makes it — a spawned child object with handler
`C2/20A2` / `C2/2105` and the `$1BF6` mutex — instead of a hand-rolled ramp. The scripts are
`C2/2EC1 = 28 01 52` and `C2/2EC4 = 29 01 52`.

### S4 — the menu content

Nothing in `src/menu.c` needs a location branch. The single location test in the whole menu program is
`C2/98A1` (`$0100 >= 0x1F0`), already ported as `CtMenuSaveCanOpen`, and it is fed the same `$7E` image
the overworld writes `$0100` into — so the Save option simply becomes reachable the moment S1 lands.
Two smaller, derived items the port does not have yet (both optional for this gap, both cited):

* `C2/CB99`: the menu's current-song id is `$02AE` on the overworld and `$29AE` on a field map
  (selector `$0D32`, itself from `$0100`). Consumer: the Config screen (`C2/BF5F`).
* `C2/D3AD`: the Save-slot ERA caption = `FF/D296[loc>>1]` nibble (`loc & 1` → high nibble) → index into
  the 8-entry string table `FF/D396` (`"", Present, Middle Ages, Future, Pre-historic, Dark Ages,
  Black Omen, End of Time`), where `loc` = the SAVED location (`savefile + 0x05F3 & 0x1FF`, reader
  `C2/D22C`). Our `CtMenuSaveSummary` already carries `locId`; the era caption is a pure table lookup
  on it. The slot TITLE is the storyline chapter (`FF/D024` thresholds → `FF/D03E`), also not in
  `menu.h` yet.

### S5 — regression pin (suggested, `add-regress-check`)

A `--menutest`-style check anchored on the OVERWORLD, not on a frame number: open the menu from
`saves/ow_w0.state` (X), assert `CtMenuPanelData` gives Crono LV 1 HP 70/70 MP 8/8 and gold 600, assert
`CtMenuSaveCanOpen(s7, glb) == 1` there and `== 0` on `saves/menu_open_gato2.state`, then close and
assert the world mover/camera/object array came back byte-identical to the pre-open snapshot (the
engine's analogue of §4.5's `total saved-region diffs: 5`). Anchor the fade assertion on the EVENT
("frames from the poll to the menu program == 17"), never on an absolute frame.

---

## 6. OPEN / BLOCKED

1. **`$027C` in our engine is baked to 0** (`src/field.c` world block writes `wr[0x027C] = 0` every
   frame), so the ROM's real gate `$027C == 1` cannot be read back. Making the world loop a genuine
   `$027C` state machine is the structural fix (S1a) and is a change to `src/world.c` + the world block
   in `src/field.c` that this dossier scopes but does not design. Until then any gate is an engine-level
   stand-in and must be commented as such.
2. **`$027C` states 2 and 8 are unidentified.** `2 -> C2/24D5`, `8 -> C2/261D`; not disassembled.
   `6 -> C2/258D` (the Select screen) was only skimmed: same shape as the menu block, its own inline
   stash (`$0B30` len 0x1000 → `$7F:2800`), fade script `C2/2EC7`, `LDX #0022` = 34 frames.
3. **`$027E`/`$0280` non-zero handler banks not derived.** `$027E` selects `C2/342D[{3439,3479,346D,
   347B,3489,349B}]` and `$0280` selects a 4-entry bank in `C2/345D` (`$0280 = 1` → `3772/37A1/37BB/0000`).
   Every state I own has `$027E = $0280 = 0` (on foot). These are presumably the vehicle/Epoch/boat modes.
   **Unblock:** an overworld savestate riding a vehicle (Epoch / Blackbird / the Porre ferry mid-ride).
   Whether the X poll exists at all in those modes is therefore UNKNOWN — I did not verify that the menu
   opens while riding.
4. **Menu mode 5 (`$027C = 9`, Y with storyline ≥ 0x49) is untested on the overworld.** The dispatch is
   proven (`C2/3605-3617` + `C2/2545`'s `CMP #05`), but every overworld state I have is storyline 03 or
   0x15. `--monitor` on `ow_w0` shows Y leaves `$027C = 1` (§4.3), which is the *refusal* arm, not the
   accept arm. **Unblock:** an overworld savestate at storyline ≥ 0x49. Same open question as
   `menu_entry.md`'s ("which screen is mode 5").
5. **`C2/2C93`'s sub-routines are named but not decoded** (`C2/5775`, `C2/2BFB`, `C2/2CC1`, `C2/295E`,
   `C2/29BA`, `C2/2902`, `C2/2930`, `C2/09C5`). I measured that the whole restore chain
   `C2/0043 → C2/0110 → C2/2E72 → C2/03EF → C2/2C93` takes ~36 real frames (f91..f127 in §4.4) and that
   it leaves the six saved regions byte-identical — which is all our engine needs, since it rebuilds
   nothing. If someone later ports a real VRAM rebuild, these must be opened.
6. **The `$1BEC` sound drain during the fades is proven inert on this state only.** `C2/0454` posts
   `JSL C7/0004` when `$1BEC != 0`; on `ow_w0` nothing is pending across the entry (§4.6). Whether a
   music-zone request landing in the same 17-frame window is posted (and therefore whether the overworld
   makes a sound the field's silent fade does not) is NOT measured. **Unblock:** an overworld state
   parked on a music-zone boundary, or `CT_OWSONG` driving a zone change into the fade window.
7. **The menu SFX on the overworld entry are assumed identical to the field's** (they are inside
   `C2/8000`, which both paths call). I measured `$1E00` changing at f31 with writer `C7:0196` — the
   Akao teardown of the `C2/8096` prelude — which is consistent with `menu_sfx.md`, but I did not diff
   the two command streams. Low risk, not verified.
8. **`$7E:1B58`/`$1B59`** (`C2/256E: LDA $1B58 / BEQ / INC $1B59`, run once on every menu return) are
   unlabeled. Not needed for the port, but it is a real write on the resume path.
9. **`$7E:98C2`'s writer was not caught in the act.** No `STA $98C2` exists in bank `$C2`; the word is
   deposited by the summary reader's `MVN`/`STA $0000,Y` chain (`C2/D22C`). I proved the *value* is the
   saved location by reading `$98C2` out of two capture WRAMs (`01F0` / `01F1`, §4.7), which is enough to
   pin the era mechanism, but the exact buffer offset arithmetic (`$61 = ($78 & FF00) >> 2 + $7600`)
   was not walked.
10. **Not verified: the menu opening from the overworld while an overworld EVENT is running.** All my
    captures are free-roam. The ROM's protection is the `$027C == 1` gate itself (a scripted overworld
    sequence parks `$027C` elsewhere), so I believe it is covered — but "I did not reproduce it" is the
    honest statement, per `verify-negatives-on-proper-state`.
