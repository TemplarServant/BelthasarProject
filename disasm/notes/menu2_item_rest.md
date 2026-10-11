# menu2_item_rest — the ITEM screen (screen 2), the remaining holes

Scope handed to this lane:
 (a) the scrollbar widget C2/8BB8 in full (arrows AND thumb)
 (b) $7E0DBC — the invalid-target buzz
 (c) A on an empty slot / A on a non-usable item / B at each level
 (d) key item / weapon colour + description box vs CtMenuRenderItem
 (e) a full input-by-input differential ROM vs our engine

Every claim below carries either a ROM address that was disassembled with `tools/dis.py` (or decoded
byte-by-byte from the image, where the linear decoder desyncs) or an oracle command that was actually
run against `scratchpad/menu/ct_probe.exe`. Where neither exists it is in §6 OPEN.

---

## 1. VERDICT

**(a) The scrollbar is ONE widget of TWO OBJ sprites that MOVE.** `C2/8BB8` (task slot 4, spawned by
`C2/8B93`) is not "arrows plus a separate thumb": the up-triangle and the down-triangle **are** the
thumb. Both sit at the same X (`$0D92`), 8 px apart in Y, and the pair's Y is
`Y = $0D93 + (($0D95 * ($0D99<<8|$0D98)) >> 8)`, truncated to 8 bits — an 8.8 fixed-point ramp.
On the Item screen `C2/ADD0` writes `$0D92/$0D93 = E8/50` (X=232, Ybase=80) and `C2/EA81` derives
`$0D94 = 0xE8` (max scroll = 242 total rows − 10 visible) and `$0D98/$0D99 = 0x0072`
(= `0x6800 / 0xE8`, i.e. a 104-px track). Up arrow suppressed iff `$0D95 == 0`; down arrow suppressed
iff `$0D95 >= $0D94`. Both blink together, **8 frames on / 8 frames off** (`$0D96`, a 3-bit down
counter, toggles `$0D97` bit0). `$0D9A != 0` hides the whole widget.

**(b) `$7E0DBC` is a 2-bit VALIDITY MASK, not a flag** — bit1 = "this target's HP can still be
restored", bit0 = "its MP can still be restored". Recomputed EVERY FRAME of the target overlay by
`C2/B870` (called from `C2/B83E`, called from the overlay loop `C2/B1A5`). `A` with `$0DBC == 0`
refuses: `C2/B1B5 LDA $0DBC / BNE / C2/B1BA JSR C2/EACC` = APU command `$18` id **`$01`** (the buzz),
then `BRA` straight back into the same loop — the item is NOT consumed and the overlay does NOT close.
The SUCCESS chime is a different byte and a different command: `$0F5D` bit0/bit1 (set by the appliers
`C2/8FE9` / `C2/900F`, only when a restore actually happened) makes task-slot-7's body `C2/9016` post
command **`$19`** id **`$06`** (HP) / **`$07`** (MP) via `C2/EAF9`, which clears the throttle first so
it can never be swallowed. (`C2/EAD6` does the same thing from `$0DBC`, but nothing in the 4 MB image
references it — see §6.)

**(c) There is NO refusal on an empty slot or on a non-usable item.** `A` in the list always blips
(`C2/AA99 JSR C2/EAC2`, id 00) and always PICKS the slot, empty or not. The second `A` on the same
slot lands in `C2/AB14`, which branches three ways: `$7700[id] & 0x02` -> the USE machine `C2/B0AB`;
`id == 0` or `id >= 0xBC` -> straight back to substate 2 (a blip, nothing else); `id < 0xBC`
(EQUIPMENT) -> substate **5** and a real INFO PANEL ("Can be used by: <names>" + "Attack: N",
`C2/AE91`), dismissed by A **or** B. `B` while picked is **SILENT** (`C2/AAD0 BVS -> C2/AAFF` branches
over the `EAC2` site) — every other B in the screen blips.

**(d) The item row is `[icon]name : count CLASS`, and our row layout is already column-exact.**
The CLASS word comes from `$FF:B310 + id*2` (`WEAPON/ARMOR/HELMET/ACCESSORY/ITEM`) and our baked
`ctItemCat[]` matches it for all 242 ids. The yellow name is `$7700[id] & 0x02` setting the tilemap
attribute bit (`C2/AE8A LDA #08 / TSB $7E`). **But the description box is a STORED, DTE-compressed
string** at `$CC:2EB1 + id*2`, rendered through the ROM's own dialogue engine (`C2/F2F3` writes
`$020C/$020D/$020F`) — not generated from the effect record. That is a real, visible divergence: the
ROM shows "Power +2" for a PowerGlove and "Restores Status" for a Heal; ours shows an empty box.

**(e)** Behavioural divergences proven this pass: no target-validity test at all (a full-HP party
consumes Tonics and heals nothing — measured), no buzz, no heal chime, the wrong exit on the last
unit, no equipment info panel, no empty-slot pick, a static scrollbar with the wrong blink period.

**One-line answer to "what must the C engine do":** give the Item screen the ROM's four missing state
variables — `$0D95` (the scrollbar's scroll mirror, which drives a MOVING arrow pair), `$0D96/$0D97`
(the scrollbar's OWN 8/8 blink), `$0D9A` (the scrollbar suppress counter) and `$0DBC` (the per-frame
target validity mask, `C2/B870`) — plus substate 5, and read the description from `$CC:2EB1 + id*2`.

---

## 2. ROM TRACE

### 2.1 The screen's substate machine (`$68`, mirrored live in `$0D17`)

`C2/AA34` is the ITEM entry in the screen-handler table (menu2_shell_exit §2.10: slot-1 dispatcher
`C2/826F` -> `C2/8292[screen]` -> the per-screen sub-table). Its six substates:

| `$68` | handler | role |
|---|---|---|
| 0 | `C2/AA40` -> `JMP C2/ADD0` | INIT / builder |
| 1 | `C2/AA4A` | the LABEL row ("Use/Move" `$54=0x0A` / "Organize" `0x0B`) |
| 2 | `C2/AA86` | the LIST |
| 3 | `C2/AAC8` | PICKED (an item is lifted, `$0F00` = its absolute slot) |
| 4 | `C2/AB4A` | ORGANIZE (runs `C2/AB62`, then `$54=0x0A`, `$68=1`) |
| 5 | `C2/AC1D` | **MESSAGE WAIT** = the equipment INFO panel |

`C2/E984` is the shared per-frame input reader for every menu screen:
```
2E984  JSR C2/E9E2                ; latch the pads
2E98A  STZ $0D1D                  ; the EDGE byte for this frame
2E98D  LDA $68 / CMP $0D17 / STA $0D17
2E995  BNE ->2E9CD                ; *** the substate CHANGED this frame -> NO input at all ***
2E997  LDA #80 / STA $00 / LDX #00
2E99D  LDA $C2E9D9,X / TAY / LDA $2993,Y / AND $F0    ; button-config remap ($7E:2993+)
2E9A9  LDA $00 / TSB $0D1D / LSR $00 / INX / CPX #02 / BCC ->2E99D
2E9B5  LDA $F4 / AND $2999 -> bit1  ;  2E9C1  LDA $F4 / AND $299A -> bit0
2E9CD  LDA $0D83 / BEQ + / LDA #40 / TSB $0D1D        ; a forced auto-cancel latch
```
so **`$0D1D` bit7 = A(confirm) edge, bit6 = B(cancel) edge**, and every `$68` transition costs one
frame of deaf input. That is the 1-frame lag between `$0D17` and `$68` in every measurement below.

### 2.2 `C2/ADD0` — the Item screen builder (what seeds the scrollbar)
```
2ADD0  PHP / SEP #20
2ADD3  JSR C2/90FC
2ADD6  LDX #C0A6 / JSR C2/ED31            ; the screen's window/tilemap descriptor
2ADDC  REP #20
2ADDE  LDA #50E8 / STA $0D92              ; *** $0D92 = E8 (X=232), $0D93 = 50 (Ybase=80) ***
2ADE4  LDA #6800 / STA $004204            ; *** WRDIV dividend = 0x6800 = 104<<8 (a 104-px track) ***
2ADEB  SEP #20
2ADED  STZ $7E / STZ $0DDB                ; $0DDB = window TOP row (0)
2ADF2  LDA #0A / STA $00 / LDY #00F2      ; 10 visible rows over 242 total slots
2ADF9  JSR C2/EA81                        ; -> $0D94 (max scroll) + $0D98/$0D99 (the 8.8 step)
2ADFC  LDA $0416 / STA $0D95              ; the scrollbar's scroll mirror
2ADFF  JSR C2/8B93                        ; SPAWN the scrollbar task (slot 4, entry C2/8BB8)
2AE05  LDA $0415 / STA $54                ; restore the saved list row
2AE0A  JSR C2/F5A7
2AE0D  LDA #39 / STA $0D13                ; screen mode flags (bit2 = an overlay is up)
2AE12  INC $0D15
2AE15  LDX #FBF8 / JSR C2/8385
2AE1B  JSR C2/E923
```
`C2/EA81` (A = visible rows, Y = total rows):
```
2EA84  LDA $00 / STA $0DD8                ; $0DD8 = 0x0A visible rows
2EA89  TYA / SEC / SBC $00                ; 0xF2 - 0x0A = 0xE8
2EA8D  STA $004206                        ; WRDIVB = 0xE8
2EA91  STA $01
2EA99  LDA $00 / BEQ + / DEC A
2EA9E  CLC / ADC $0DDB / STA $0DDC        ; $0DDC = window BOTTOM row = 9
2EAA5  LDA $01 / STA $57 / STA $0D94 / STA $0DDA     ; *** $0D94 = 0xE8 = MAX SCROLL ***
2EAAF  REP #20 / LDA $004214 / STA $0D98  ; *** $0D98/$0D99 = 0x6800/0xE8 = 0x0072 ***
```
Note the window is **fixed at 242 slots regardless of how many items are held**, so `$0D94` is always
0xE8 on this screen and the scrollbar is always present.

### 2.3 `C2/8B93` — spawn, and `C2/8BB8` — THE SCROLLBAR TASK (slot 4), in full
```
2B93   PHP / SEP #20 / STZ $0D9A          ; clear the suppress counter
2B99   REP #30 / LDA #8BB8 / LDX #0020 / JSR C2/8249   ; spawn into task slot 4 (0x20/8)

28BB8  SEP #20
28BBA  LDX #E000 / TXY                    ; X = Y = 0xE000  (X=00, Y=E0 = OFF-SCREEN)
28BBE  LDA #F8 / TRB $0D96                ; $0D96 &= 0x07
28BC3  DEC $0D96
28BC6  BPL ->28BD0
28BC8  LDA $0D97 / EOR #01 / STA $0D97    ; underflow -> flip the blink phase
28BD0  LDA $0D97 / LSR A
28BD4  BCC ->28C24                        ; phase 0 -> leave X/Y at E000 = both arrows HIDDEN
28BD6  LDA $0D9A / BNE ->28C30            ; *** suppressed -> draw NOTHING at all ***
28BDB  LDA $0D95 / STA $4202              ; WRMPYA = scroll
28BE2  LDA $0D98 / STA $4203              ;   * step.lo
28BE9  LDA $0D92 / STA $00 / STA $02      ; both arrows share X = $0D92
28BF0  LDA $4217 / PHA                    ; hi(scroll * step.lo)
28BF5  LDA $0D99 / STA $4203              ;   scroll * step.hi
28BFC  PLA / CLC / ADC $0D93 / ADC $4216  ; + Ybase + lo(scroll * step.hi)
28C05  STA $01                            ; UP arrow Y
28C07  ADC #08 / STA $03                  ; DOWN arrow Y = UP + 8
28C0B  LDA $0D94 / BEQ ->28C30            ; max scroll 0 -> no bar at all
28C10  LDA $0D95 / BEQ ->28C1A            ; scroll == 0 -> no UP arrow
28C15  LDX $00 / STX $0778                ; OAM entry 30 <- (X, Yup)
28C1A  CMP $0D94 / BCS ->28C24            ; scroll >= max -> no DOWN arrow
28C1F  LDY $02 / STY $077C                ; OAM entry 31 <- (X, Yup+8)
28C24  LDX #3022 / STX $077A              ; tile 0x22 attr 0x30 (prio 3, pal 0)
28C2A  LDX #B022 / STX $077E              ; tile 0x22 attr 0xB0 (prio 3, V-FLIPPED)
28C30  JSR C2/821E                        ; yield 1 frame
28C33  JMP C2/8BB8
```
The OAM shadow is re-blanked to `$E0` every frame by slot 3's `C2/E92D` (`$7E:0740..087E`), so
"not written this frame" == "not drawn this frame". `$0778`/`$077C` are OAM entries **30** and **31**
(`($778-$700)/4`); the high table `$0904..0917 = $AA` makes them **size bit 1 = 16x16** (OBSEL
objSize=0, measured `--ppuregs`).

**The closed form for the Item screen:**
```
  X          = $0D92 = 232
  Yup(s)     = $0D93 + ((s * 0x0072) >> 8)   truncated to 8 bits  = 80 + ((s*114) >> 8)
  Ydown(s)   = Yup(s) + 8
  up   drawn iff  s != 0
  down drawn iff  s <  $0D94  (= 232)
  blink: 8 frames drawn, 8 frames blank  ($0D96 = FF,6,5,4,3,2,1,0 then flip $0D97 bit0)
  the whole widget is hidden while $0D9A != 0
```
At s=0, Yup=80; at s=232, Yup=80+103=183 — a 103-px travel.

The sprite graphic (tile 0x22 in the 16x16 cell at objTileAdr1 word 0x4000, decoded from
`cr_list.raw.vram`) is an UP triangle occupying tile rows 1..12 and columns 2..12:
```
  ................
  .......F........
  ......FEF.......
  ......FEF.......
  .....FEDEF......
  .....FEDEF......
  ....FEDDDEF.....
  ....FEDDDEF.....
  ...FEDDDDDEF....
  ...FEDDDDDEF....
  ..FEDDDDDDDEF...
  ..FEEEEEEEEEF...
  ..FFFFFFFFFFF...
  ................  (rows 13-15 blank)
```
So the composite widget's INK, in the engine's own 256x224 space, is
`x 234..244`, `y (Yup+1)..(Yup+12)` for the up arrow and `y (Yup+11)..(Yup+22)` for the (V-flipped)
down arrow — they overlap by two rows and read as one diamond-shaped thumb.

### 2.4 `$0D9A` — who hides the scrollbar
`STZ` (show): `C2/8B96` (spawn), `C2/AA6C` (A on "Use/Move" -> list), `C2/AC36` (dismiss the info
panel), `C2/B1D6` (leave target-select). `DEC` (hide): `C2/AAB7` (B from the list -> the labels),
`C2/AB2F` (A on equipment -> the info panel). `STA` (hide): `C2/B195` (`LDA #0C` -> target-select).
Returning to the ribbon kills the task outright (`C2/9A98 LDA #74 / JSR C2/8255` kills slots 2,4,5,6).

### 2.5 `$7E0DBC` — the target validity mask (`C2/B870`, run every frame from `C2/B83E`)
```
2B83E  PHP / SEP #20
2B841  LDA $0DBD / AND #08 / BNE ->2B866        ; all-party item -> no cursor clamp
2B848  LDA $54 / SEC / SBC $0DBB / CMP $73      ; index = cursor - base(0x0C), vs party size $73
2B850  BCC ->2B866
2B852  LDA $5A / BIT #08 / BNE ->2B85D
2B858  LDA $0DBB / BRA ->2B864                  ; out of range -> CLAMP to the FIRST member
2B85D  LDA $73 / DEC A / CLC / ADC $0DBB        ;              -> or to the LAST member
2B864  STA $54
2B866  JSR C2/B870
2B869  LDA #02 / STA $0D75

2B870  STZ $0DBC                                ; <- the ONLY unconditional write
2B873  LDA $54 / SEC / SBC $0DBB / STA $71      ; $71 = member index
2B87B  CMP $0DBE / STA $0DBE
2B881  BEQ ->2B88D
2B883  LDA $0DBD / AND #08 / BNE ->2B88D
2B88A  JSR C2/EAC2                              ; the target CURSOR-MOVE blip (id 00)
2B88D  LDA #01 / STA $00                        ; single target: 1 candidate
2B891  LDA $0DBD / BIT #08 / BEQ ->2B89E
2B898  LDA $73 / STA $00 / STZ $71              ; all-party: scan every member from 0
2B89E  JSR C2/8820                              ; $6F = $2600 + $0D5F[$71]*0x50 (the char sheet)
2B8A1  LDX $6F
2B8A3  LDA $0DBD / BIT #80 / BEQ ->2B8B9        ; item restores HP?
2B8AA  REP #20 / LDA $0003,X / CMP $003F,X      ; curHP  vs  the WORD at sheet+0x3F
2B8B2  SEP #20 / BCS ->2B8B9
2B8B6  INC $0DBC
2B8B9  ASL $0DBC                                ; the HP answer becomes bit1
2B8BC  LDA $0DBD / BIT #40 / BEQ ->2B8CE        ; item restores MP?
2B8C3  LDA $0007,X / CMP $0009,X                ; curMP vs maxMP  (8-bit compare)
2B8C9  BCS ->2B8CE
2B8CB  INC $0DBC                                ;   -> bit0
2B8CE  LDA $0DBC / BNE ->2B8D9                  ; found a valid one -> done
2B8D3  INC $71 / DEC $00 / BNE ->2B89E          ; all-party: try the next member
2B8D9  RTS
```
`C2/8820` = `LDX $71 / CPX $73 / BCC + / DEC $78` then `LDA $0D5F,X` -> `C2/8839`
(`STA $4202 / LDA #50 / STA $4203 / REP #31 / LDA #2600 / ADC $4216`), i.e. `$6F = 0x2600 + charId*0x50`.

`$0DBD` is the item's effect/scope byte, built by `C2/B0AB`:
```
2B0AE  LDA $04C9 / JSR C2/8881 / LDX $04CE
2B0B7  LDA $CC1BB5,X / INC A / AND #03 / ASL A / ASL A / STA $0DBD    ; the scope nibble
2B0C3  JSR C2/88D8 / LDA $CC0001,X / ORA $0DBD / STA $0DBD           ; | record byte +1
2B0D0  LDA $CC0002,X / AND #0F / STA $0DC0 / CMP #0F / BNE + / LDA #40 / STA $0DC0
2B0E2  LDA $CC0000,X / JSR C2/B0FA / JSR ($B0EE,X)                   ; effect-kind dispatch
```
bit7 = restores HP, bit6 = restores MP, bit3 = ALL-PARTY. Measured for the Tonic (0xBD):
`$0DBD = 0x84`, `$0DC0 = 0x01`.

### 2.6 The two target-select loops and the BUZZ
```
2B17F  LDA #B2 / BIT $0DBD / BMI + / LDA #8A          ; $0DBF = B2 (HP item) or 8A
2B188  STA $0DBF
2B18B  LDA #0C / STA $54 / STA $0D9C / STA $0DBB / STA $0D9A  ; cursor+base = 0x0C; SCROLLBAR OFF
2B198  JSR C2/AF42                                     ; build the overlay (2 yields; sets $0D13 bit2)
2B19B  LDA $0DBD / AND #08 / BEQ + / JSR C2/8AD5       ; all-party -> the multi-highlight task
2B1A5  JSR C2/821E                                     ; ---- THE LOOP ----
2B1A8  JSR C2/E984
2B1AB  JSR C2/B83E                                     ; clamp the cursor + recompute $0DBC
2B1AE  BIT $0D1D
2B1B1  BVS ->2B1C7                                     ; B -> cancel
2B1B3  BPL ->2B1A5                                     ; no A -> loop
2B1B5  LDA $0DBC
2B1B8  BNE ->2B1BF
2B1BA  JSR C2/EACC                                     ; *** THE BUZZ: cmd $18 id $01 ***
2B1BD  BRA ->2B1A5                                     ; ...and STAY in the overlay
2B1BF  JSR C2/8F55                                     ; apply the effect
2B1C2  JSR C2/B31C                                     ; consume + redraw
2B1C5  BRA ->2B1A5                                     ; *** ALSO stays in the overlay ***
2B1C7  BVC ->2B1CC / JSR C2/EAC2                       ; the B blip
2B1CC  LDA $0415 / STA $54                             ; restore the list row
2B1D1  LDA #04 / TRB $0D13                             ; clear the overlay mode bit
2B1D6  STZ $0D9A                                       ; scrollbar back ON
2B1D9  JSR C2/F566 / LDA #40 / JSR C2/8255 ...         ; kill task slot 6
```
`C2/AF42` (the overlay build) does `LDX #C0E4 / JSR C2/ED31 / LDA #FF STA $19C0 / JSR C2/F566 /
JSR C2/AFB5 / LDX #FC37 JSR C2/8385 / JSR C2/821E / JSR C2/AFED / LDA #01 STA $0D75 / JSR C2/AF85 /
JSR C2/F643 / JSR C2/821E / LDA #02 STA $0D75 / JSR C2/AF9D / STZ $0DBE / STZ $0DBC /
LDA #04 TSB $0D13` — two yields, hence the measured 2-frame gap between `$54 = 0x0C` and `$0D13 = 0x3D`.

The TAB items use the twin loop at `C2/B228` with validator `C2/B24A -> C2/B275` (same shape; its test
is `LDA $7E:9A9B,Y` vs `$FF:CF32,X` with `$FF:CF2B,X` selecting the stat). Its refusal is
`C2/B23D JSR C2/EACC` — the same id-`$01` buzz. A third `JMP C2/EACC` sits at `C2/B407`. Those three
are the only buzz sites in the whole Item region.

`C2/B31C` (after a successful use):
```
2B31C  LDA #01 / STA $04CB
2B321  LDX $0F00 / LDA $2400,X / STA $04C9
2B32A  CMP #C9 / BNE + / JSR C2/B10D
2B331  JSR C2/87D5                     ; CONSUME 1 (zeroes the id in place at count 0)
2B334  JSR C2/AE20                     ; redraw the one row
2B337  LDX $0F00 / LDA $2500,X / BIT #FF / BNE +
2B341  STZ $0DBD                       ; *** last unit gone -> $0DBD = 0 -> $0DBC is 0 forever ***
2B344  LDA $2400,X / JSR C2/F2F3       ; refresh the description (now empty)
2B34A  JSR C2/AFED / LDA #01 / STA $0D75
```
So using the LAST unit does **not** close the overlay; it leaves it open with everything invalid, and
every further A buzzes until B.

### 2.7 The SUCCESS chime is `$0F5D`, not `$0DBC`
The appliers set a "something actually changed" flag:
```
28FE6  LDA #0001 / STA $0F5D                  ; an HP restore happened   (C2/8FCB tail)
29009  LDA $0F5D / ORA #0002 / STA $0F5D      ; an MP restore happened (STA at C2/900F, C2/8FEF tail)
```
and task slot 7's body posts the chime:
```
29016  SEP #30
29018  LSR $0F5D / BCC ->2902C
2901D  STZ $0F60 / LDA #03 / STA $0F61 / STZ $16
29027  LDA #06 / JSR C2/9047          ; *** HP chime ***
2902C  LSR $0F5D / BCC ->29044
29031  LDA #2A / STA $0F60 / LDA #01 / STA $0F61 / LDA #06 / STA $16
2903F  LDA #07 / JSR C2/9047          ; *** MP chime ***
29044  JMP C2/8216                    ; self-kill
29047  JSR C2/EAF9                    ; LDX #19 / STX $1E00 / STZ $0D76 -> cmd $19, NEVER throttled
```
Measured: the heal frame leaves `$1E01 = 06`; the buzz frame leaves `$1E01 = 01`; every blip leaves 00.

### 2.8 The three `A` outcomes on a picked slot (`C2/AB14`) and the INFO PANEL
```
2AB14  TDC / LDX $0F00 / LDA $2400,X / STA $04C9 / TAY
2AB1F  LDA $7700,Y / AND #02
2AB24  BNE ->2AB3E                    ; field-usable -> JSR C2/B0AB (the USE machine)
2AB26  LDA $04C9 / BEQ ->2AB41        ; EMPTY slot   -> nothing
2AB2B  CMP #BC / BCS ->2AB41          ; id >= 0xBC (consumable/key, not usable) -> nothing
2AB2F  DEC $0D9A                      ; EQUIPMENT -> hide the scrollbar
2AB32  LDA #05 / STA $68              ;             substate 5
2AB36  JSR C2/AE91                    ;             build the "Can be used by / Attack" panel
2AB39  LDA #40 / JMP C2/8255          ;             kill task slot 6 (the pinned hand)
2AB3E  JSR C2/B0AB
2AB41  LDA #02 / STA $68 / LDA #40 / JMP C2/8255
```
`C2/AE91` = `JSR C2/AFB5 / JSR C2/AEB6 / JSR C2/AF0B / LDX #C0D5 JSR C2/ED31 / LDA #04 TSB $0D13 /
LDX #FBEA JSR C2/8385 / LDX #FC37 JSR C2/8385`. `C2/AEB6` clears `$7E:9890..98B9`, then reads
`LDA $7A00,X` with X = `$04C9` — a per-item "who can equip this" table at `$7E:7A00 + id` — to build
the name list. Dismissal (`C2/AC1D`, substate 5):
```
2AC1D  INC $0D1F / JSR C2/E984
2AC23  LDA $0D1D / AND #C0 / BEQ ->2AC39      ; *** A **or** B dismisses ***
2AC2A  JSR C2/EAC2                            ; one blip
2AC2D  LDA #04 / TRB $0D13
2AC32  LDA #02 / STA $68                      ; -> the LIST, not back to PICKED
2AC36  STZ $0D9A                              ; scrollbar back ON
```

### 2.9 The list / labels / picked handlers (every sound site)
```
2AA86 (LIST)    LDX #FFFF / STX $0F00
                BMI ->2AA99 (A) ; BVS ->2AAAD (B) ; else JMP C2/AC3A (nav)
      A   2AA99 JSR C2/EAC2                        ; blip -- UNCONDITIONAL, before any test
            2AA9C LDX $0D26 / STX $0F00            ; *** picks the slot even if EMPTY ***
            2AAA2 INC $68                          ; -> substate 3
            2AAA4 LDX $0415 / STX $0D9C / JMP C2/89E4   ; spawn the blinking pinned hand (slot 6)
      B   2AAAD JSR C2/EAC2                        ; blip
            2AAB0 DEC $0D78                        ; force a description refresh
            2AAB3 LDA #01 / STA $68                ; -> the LABEL row
            2AAB7 DEC $0D9A                        ; *** scrollbar OFF ***
            2AABA LDA #0A / STA $54                ; cursor -> "Use/Move"
            2AABE LDA #E0 / STA $0701              ; blank OAM entry 0 (the list hand)
            2AAC3 LDA #00 / JMP C2/F2F3            ; description := item 0 = EMPTY

2AAC8 (PICKED)  BMI ->2AAD5 (A) ; BVS ->2AAFF (B) ; else JMP C2/AC3A
      A   2AAD5 JSR C2/EAC2 ; LDX $0F00 / CPX $0D26 ; BEQ ->2AB14 (use/info) else the MOVE swap
            2AAE0 LDY $0D26 ; swap $2400[X]<->$2400[Y] and $2500[X]<->$2500[Y]
      B   2AAFF   -- NO C2/EAC2 ON THIS PATH: cancelling a pick is SILENT --
            2AAFF JSR C2/821E / JSR C2/AE43 / LDX #FBE3 JSR C2/8385
            2AB0B LDA #02 / STA $68 / LDA #40 / JMP C2/8255   ; kill the pinned-hand task

2AA4A (LABELS)  BMI ->2AA5E (A) ; BVS ->2AA83 (B)
            2AA54 LDA $81 / CMP $54 / BEQ + / 2AA5A JSR C2/EAC2   ; the L/R move blip
      A   2AA5E JSR C2/EAC2
            2AA61 CMP #0A BEQ ->2AA6C  |  CMP #0B BEQ ->2AA7E
            2AA6C STZ $0D9A / LDA $0415 STA $54 / LDA #FF STA $0D78 / LDA #02 STA $68
            2AA7E LDA #04 / STA $68                ; ORGANIZE
      B   2AA83 JMP C2/9A98            ; STZ $67 / LDA #01 STA $68 / LDA #74 JSR C2/8255
                                       ; (kills tasks 2,4,5,6 -- INCLUDING the scrollbar)
                                       ; JSR C2/9B71 / JMP C2/EAC2   <- the B-to-ribbon blip
```
`C2/8255(mask)` walks bits 0..7 and `STZ $B0,X` for each set bit — it kills task slots. `LDA #40`
kills slot 6 only; `LDA #74` kills 2,4,5,6.

`C2/AB4A` (Organize) = `JSR C2/AB62` (the sort) then `JSR C2/821E / JSR C2/AE43 / LDX #FBE3
JSR C2/8385 / LDA #0A STA $54 / LDA #01 STA $68`. **It does not touch `$0415` or `$0416`** — a byte
scan of `C2/AB62..ABE8` finds no store to either.

### 2.10 The scroll gate (`C2/AC6E`) and the description refresh (`C2/AD2E`)
```
2AC54  JSR C2/EA53 / BMI ->2AC6E       ; the nav wanted to leave the 10-row window
2AC59  JSR C2/AD2E                     ; (in-window move) refresh $0D26/$04C9/$04CA + description
2AC5C  LDA $54 / STA $0415 / CMP $81 / BEQ + / JSR C2/EAC2
2AC6E  LDA $54 / CLC / ADC $0416 / DEC A
2AC75  CMP #F0 / BCS ->2AC59           ; *** (row + scroll - 1) must stay < 240 ***
2AC8D  LDA $5A / BIT #04 / BNE ->2AC9E
2AC93  DEC $0416   ...   2AC9E  INC $0416
2ACA7  LDA $0416 / STA $0D95           ; *** re-arm the scrollbar's scroll mirror ***
2ACAD  LDX #344A / JSR C2/AE65 / LDX #FBE3 JSR C2/8385
2ACB9  JSR C2/EAC2                     ; the scroll blip

2AD2E  REP #21 / LDA $0416 / ADC $54 / AND #00FF / STA $0D26 / TAX
2AD3D  SEP #20 / LDA $2500,X / STA $04CA / LDA $2400,X / STA $04C9
2AD4B  JSR C2/F2F3                     ; -> the DESCRIPTION
```
`row=9, scroll=231` gives 239 (allowed -> 232); `row=9, scroll=232` gives 240 (refused), so max
scroll = 232 = `$0D94` and the maximum absolute slot is 241. `row=0, scroll=0` gives 0xFF (refused).

### 2.11 The row builder — icon, name, `:`, count, CLASS
```
2AE43  REP #30 / PEI $54 / STZ $54 / LDX #2F4A
2AE4D  PHX / JSR C2/AE65 / PLA / CLC ADC #0080 / TAX / INC $54 / LDA $54 / CMP #000A / BCC ->2AE4D
2AE65  SEP #20 / TDC / LDA $0416 / CLC ADC $54 / TAY / JSR C2/AE75
2AE75  TDC / LDA $2500,Y / STA $0D4E          ; count
2AE7C  LDA $2400,Y / STA $0D4D / TAY          ; id
2AE83  LDA $7700,Y / AND #02 / BEQ ->2AE8E
2AE8A  LDA #08 / TSB $7E                      ; *** field-usable -> attr bit 11 = the YELLOW palette
2AE8E  JMP C2/B04B

2B04B  LDA $0D4D / BEQ ->2B09F                ; empty slot
2B053  REP #20 / AND #00FF / ASL A / TAY
2B05A  LDA $7D00,Y / TAY                      ; $7E:7D00 + id*2 -> the name pointer (built at menu open)
2B05E  LDA #CC0B / JSR C2/EF65                ; VWF: 11 chars from bank $CC (= $CC:0B5E + id*11)
2B064  LDA $7D / AND #FF00 / ORA #002D / STA $0000,X / INX INX     ; the ':' glyph, current attr
2B071  LDY #0D4E / LDA #7E11 / JSR C2/F114 / INX INX               ; the COUNT
2B07C  PHX / STZ $0000,X / TXY / INY INY / LDA #000F / MVN $7E,$7E ; pad
2B089  LDA $0D4D / AND #00FF / ASL A / TAX
2B091  LDA $FFB310,X / TAY                    ; *** the CLASS-string pointer table ***
2B097  LDA #FF09 / JSR C2/EF65                ; 9 chars from bank $FF
2B09F  LDY #9B76 / LDA #7E / XBA / LDA #18 / PLP / JMP C2/EF65     ; empty row = 24 blanks
```
`$FF:B310 + id*2` -> `$FF:B4F5 "WEAPON"` / `$B4FC "HELMET"` / `$B503 "ARMOR"` / `$B509 "ACCESSORY"` /
`$B513 "ITEM"`, NUL-terminated, CT mapped encoding. 242 entries, boundaries
**00-59 WEAPON, 5A-7A ARMOR, 7B-93 HELMET, 94-BB ACCESSORY, BC-F1 ITEM**.
(The `ct-field-item-menu-use` memory note and `src/menu.c`'s Organize comment both label `5A-7A`
"helmets" and `7B-93` "armor" — the ROM's own class table says the opposite. Behaviourally harmless:
Organize's emit order comes from the id ranges at `$CC:AC12`, not from the labels. Worth correcting.)

### 2.12 The DESCRIPTION is a STORED string (`C2/F2F3`)
```
2F2F3  SEP #20 / REP #10
2F2F8  BIT $0D78 / BMI ->2F302            ; $0D78 bit7 = "force a refresh"
2F2FD  CMP $0D77 / BEQ ->2F330            ; same item as last time -> nothing to do
2F302  STA $0D77 / STZ $0D78
2F308  STA $020C                          ; *** the string INDEX = the item id ***
2F30B  LDX #4E84 / STX $0DC5
2F311  LDX #6840 / STX $0DCC
2F317  LDX #2EB1 / STX $020D              ; *** the pointer table = $CC:2EB1 ***
2F31D  LDA #CC   / STA $020F              ; *** its bank ***
2F322  LDA #01 / STA $0DC9
2F327  LDX #0080 / STX $0DD0
2F32D  JSR C2/FA49                        ; the ROM's own text/dialogue renderer
```
i.e. `desc(id)` = the DTE-compressed string at `$CC:0000 + rom16($CC:2EB1 + id*2)`, decoded by the
ordinary dialogue decoder (`assets/text.py load_dict` + the 0x21..0x9F dictionary). Decoded, vs what
our `mItemDesc()` produces:

| id | ROM description | `mItemDesc()` |
|---|---|---|
| BC | *(empty)* | *(empty)* OK |
| BD Tonic | `Restores 50 HP` | `Restores 50 HP` OK |
| BE / BF | `Restores 200 HP` / `Restores 500 HP` | same OK |
| C0 / C1 / C2 | `Restores 10/30/60 MP` | same OK |
| C3 Elixir | `Restores HP/MP` | `Restores all HP/MP` **wrong** |
| C4 HyperEther | `Restores MP` | `Restores all MP` **wrong** |
| C5 MegaElixir | `Restores HP/MP for all` | `Restores all HP/MP` **wrong** |
| C6 Heal | `Restores Status` | *(empty)* **missing** |
| C7 Revive | `Revives an ally` | *(empty)* **missing** |
| C8 Shelter | `Recovers HP/MP at Save Pts.` | *(empty)* **missing** |
| C9 PowerMeal | `Recovers from "?"` | `Restores all HP/MP` **wrong** |
| CA Lapis | `Restores 200 HP for all` | `Restores 200 HP` **wrong** |
| CB / CC | `Cuts magic hits by 1/3` / `Cuts physical hits by 1/3` | *(empty)* **missing** |
| CD / CE / CF | `"Power" +1` / `"Magic" +1` / `"Speed" +1` | `Power +1` (no quotes) **wrong** |
| D6 Pendant | *(empty)* | *(empty)* OK |
| 03 SteelSaber, 5D BronzeMail | *(empty)* | *(empty)* OK |
| 8F Vigil Hat | `Protects status` | *(empty)* **missing** |
| 97 PowerGlove | `Power +2` | *(empty)* **missing** |

The "Power +2" case is not a decode artefact: it is on screen — see §4.5.

---

## 3. RAM CONTRACT

| addr | meaning | proof |
|---|---|---|
| `$7E0054` | the ONE unified cursor: list rows 0x00-0x09, `Use/Move`=0x0A, `Organize`=0x0B, target members 0x0C+ | `C2/AABA/AA6C/B18B`; monitored through every ladder in §4 |
| `$7E0067` | screen id (2 = Item) | `--monitor 7E0067:2` -> 02 at the entry frame |
| `$7E0068` | substate; **`$7E0D17` is the live mirror** and a change deafens input for one frame | `C2/E98D-E995`; the 1-frame lag in every §4 trace |
| `$7E0073` | active party size | `C2/B848 CMP $73`; forest_exit reads 01 |
| `$7E0415` | the saved list ROW | `C2/AE05`, `C2/AC5E`, `C2/B1CC` |
| `$7E0416` | the scroll (top slot), 0..232 | `C2/ADFC`, `C2/AC93/AC9E`; measured 0->0x0B over 20 Downs |
| `$7E04C9` | the highlighted/picked item id | `C2/AD48`, `C2/AB1B`; 0xD6 on the Pendant row, 0x00 on an empty row, 0x03 on SteelSaber |
| `$7E04CA` | its count | `C2/AD42` |
| `$7E04CB` | the consume amount (1) | `C2/B31C` |
| `$7E0D13` | screen mode flags; **bit2 = an overlay is up** (target-select or the info panel) | 0x39 -> 0x3D measured at the target-select frame |
| `$7E0D17` | live substate mirror | see above |
| `$7E0D26` | absolute slot = `$0416 + $54` | `C2/AD39` |
| `$7E0D4D/$0D4E` | the row builder's id / count scratch | `C2/AE75` |
| `$7E0D5F+` | party slot -> charId table | `C2/882F` |
| `$7E0D76` | the SFX throttle, 3 frames, `DEC` at `C2/8463` | measured 3->2->1->0 after each blip |
| `$7E0D77/$0D78` | the description cache (last id / force-refresh flag) | `C2/F2F8-F305`, `C2/AAB0`, `C2/AA74` |
| `$7E0D92/$0D93` | scrollbar X / Ybase = **E8 / 50** | measured `7E0D92=E850…` the frame after entry |
| `$7E0D94` | scrollbar MAX SCROLL = **E8** | `C2/EAA9`; measured E8 |
| `$7E0D95` | scrollbar SCROLL mirror of `$0416` | `C2/ADFF`, `C2/ACAA`; measured to track `$0416` exactly |
| `$7E0D96/$0D97` | scrollbar blink counter / phase | measured FF,6,5,4,3,2,1,0 then a phase flip — **8 frames** |
| `$7E0D98/$0D99` | scrollbar 8.8 step = **0x0072** | `C2/EAB5`; measured 72 00 |
| `$7E0D9A` | scrollbar SUPPRESS (non-zero = hidden) | measured FF when B goes to the labels / on the equipment panel; 0x0C in target-select |
| `$7E0DBB` | target cursor BASE = 0x0C | `C2/B192`; measured 0C |
| `$7E0DBC` | **target validity mask**: bit1 HP, bit0 MP; 0 = refuse | measured 02 (damaged) and 00 (`--callrom`, full HP) |
| `$7E0DBD` | item effect/scope byte: b7 HP, b6 MP, b3 all-party | `C2/B0C0-B0CD`; measured 0x84 for a Tonic, 0x00 after the last unit |
| `$7E0DBE` | last target index (blip edge) | `C2/B87E` |
| `$7E0DC0` | the amount nibble (0x0F -> 0x40 = FULL) | `C2/B0D6-B0DF`; measured 01 for a Tonic |
| `$7E0DD8/$0DDA/$0DDB/$0DDC` | window height 0x0A / max scroll / top row 0 / bottom row 9 | `C2/EA86-EAAC` |
| `$7E0F00` | picked absolute slot, FFFF = none | measured FFFF -> 0000 -> FFFF over pick/cancel |
| `$7E0F5D` | b0 = an HP restore happened, b1 = an MP restore happened -> the chime | `C2/8FE9`, `C2/900F`, `C2/9018` |
| `$7E1E00/01/02` | the APU mailbox: cmd / id / pan | measured id 00 blip, 01 buzz, 06 heal |
| `$7E2400+n / $7E2500+n` | inventory ids / counts (242 slots, holes kept) | measured BD/01 -> 00/00 after the last use |
| `$7E2600+id*0x50` | char sheet; `+3` curHP, `+5` maxHP, `+7/+9` MP, **`+0x3F` the HP compare operand** | `C2/B8AC/B8AF`; Crono +0x3F = 0x46 = 70, Marle 0x41 = 65 |
| `$7E7700+id` | item props; bit1 = field-usable (yellow + usable), bit3 = key item | `C2/AE83`, `C2/AB1F` |
| `$7E7A00+id` | "who can equip this" mask, read by the info panel | `C2/AECD` |
| `$7E7D00+id*2` | the item NAME pointer table built at menu open | `C2/B05A` |
| OAM 30 / 31 (`$7E0778` / `$7E077C`) | the two scrollbar triangles | dumped: `idx30 232,82 tile 022 attr 30`, `idx31 232,90 tile 022 attr B0` |
| OAM 28 (`$7E0770`) | the list hand cursor, tile 0x20, `Y = 87 + 12*row` | dumped: `(24,87)` at row 0, `(24,195)` at row 9 |
| ROM `$FF:B310 + id*2` | class-string pointer | 242 entries, matched byte-for-byte against our `ctItemCat[]` |
| ROM `$CC:2EB1 + id*2` | description-string pointer | decoded, see §2.12 |
| ROM `$CC:0B5E + id*11` | item name (already ported) | — |

---

## 4. ORACLE MEASUREMENTS

All runs use `scratchpad/menu/ct_probe.exe` and `Chrono_Trigger_USA_SNES.sfc`. Button grammar:
`A=0x100 B=0x01 X=0x200 Up=0x10 Down=0x20 Left=0x40 Right=0x80`.

### 4.1 The scrollbar's variables at entry, and the 8-frame blink
```
ct_probe.exe ROM --monitor "7E0067:2,7E0054:2,7E0416:1,7E0D92:9,7E0DBC:1" \
  --from saves/menu_open_gato2.state --btnscript "0:10,80:2,0:8,100:2,0:40" --diff
```
```
  f21     7E0067=0200*  ...                                   <- the Item screen is entered
  f22     7E0054=0000*  7E0416=00  7E0D92=E850E800FF01720000*
  f23                              7E0D92=E850E8000601720000*
  f24..f29                         ...0501.. 0401.. 0301.. 0201.. 0101.. 0001..
  f30                              7E0D92=E850E800FF00720000*   <- $0D97 flips 01 -> 00
  f31..f37                         ...0600.. through ...0000..
  f38                              7E0D92=E850E800FF01720000*   <- flips back
```
`$0D92=E8 $0D93=50 $0D94=E8 $0D95=00 $0D96=cycle $0D97=phase $0D98=72 $0D99=00 $0D9A=00`, and the
phase flips every **8** frames.

### 4.2 The arrow pair MOVES with the scroll (20 Down presses)
```
ct_probe.exe ROM --monitor "7E0054:1,7E0416:1,7E0D94:2,7E0778:8" \
  --from saves/menu_open_gato2.state \
  --btnscript "0:10,80:2,0:8,100:2,0:20,20:2,0:6,  ...x20...  ,0:20" --diff
```
```
  f22   7E0416=00  7E0D94=E800  7E0778=00E02230E85822B0   <- up hidden (E000), down (232,0x58=88)
  f30                            7E0778=00E0223000E022B0   <- blink OFF: both hidden
  f107  7E0054=09                                          <- 9 Downs walk rows 0..9
  f115  7E0416=01  7E0D94=E801
  f118                           7E0778=E8502230E85822B0   <- up (232,0x50=80) down (232,88)
  f134  7E0416=03                7E0778=E8512230E85922B0   <- 81 / 89
  f150  7E0416=05                7E0778=E8522230E85A22B0   <- 82 / 90
  f166  7E0416=07                7E0778=E8532230E85B22B0   <- 83 / 91
  f182  7E0416=09                7E0778=E8542230E85C22B0   <- 84 / 92
  f198  7E0416=0B                7E0778=E8542230E85C22B0   <- 84 / 92
```
Against `Yup = 80 + ((s*114)>>8)`: s=1->80, 3->81, 5->82, 7->83, 9->84, 11->84. **Exact, every row.**

### 4.3 The sprite record, the tile and the on-screen ink
```
ct_probe.exe ROM --peek saves/menu_open_gato2.state scratchpad/menu/sb5.raw \
   --btnscript "0:10,80:2,0:8,100:2,0:20, (20:2,0:6) x14"
python tools/ct.py oam scratchpad/menu/sb5.raw.oam
```
```
* 28   24  195  020   30   3    0   1   0  0      <- the list hand at row 9  (87 + 12*9 = 195)
* 30  232   82  022   30   3    0   1   0  0      <- UP   arrow, scroll 5
* 31  232   90  022   B0   3    0   1   1  0      <- DOWN arrow (V-flipped)
```
Framebuffer ink for that frame (decoded 256x240, `decoded_y = engine_y + 8`): the up triangle at
`x 234..244, y 91..102`; the down triangle continues to `y ~112`. That is
`engine_y = OAM_Y + tilerow` with the tile ink in rows 1..12 — see the tile dump in §2.3.

### 4.4 The whole `A` / `B` ladder (sound + state), key item and empty slot
```
ct_probe.exe ROM --monitor "7E0054:1,7E0D17:1,7E0F00:2,7E04C9:1,7E0D9A:1,7E0D13:1,7E1E01:1,7E0D76:1" \
  --from saves/menu_open_gato2.state --btnscript "0:10,80:2,0:8,100:2,0:40,100:2,0:40,100:2,0:60" --diff
```
row 0 = Pendant (0xD6, a key item):
```
  f22  $0D13 39   $0D76=03      <- entering the screen (blip)
  f23  $0D17=02  $0F00=FFFF  $04C9=D6
  f63  $0F00=0000  $0D76=03     <- A: PICKED, blip
  f64  $0D17=03
  f105 $0D76=03                 <- A again: blip
  f106 $0D17=02  $0F00=FFFF     <- straight back to the LIST. No panel, no buzz, no message.
```
row 1 = an EMPTY slot (same command with `20:2,0:30` inserted):
```
  f63  $0054=01  $04C9=00  $0D76=03   <- Down: the description CLEARS ($04C9 = 0)
  f95  $0F00=0100  $0D76=03           <- A: the EMPTY slot IS PICKED ($0F00 = slot 1)
  f96  $0D17=03
  f137 $0D76=03                        <- A again: blip
  f138 $0D17=02  $0F00=FFFF
```
The B ladder (from `saves/forest_exit.state`, `…,1:2,0:40` x4):
```
  f147/148  A         -> $0D17 03, $0F00=0000, $0D76=03     (blip)
  f191      B (1st)   -> $0D17 02, $0F00=FFFF, $0D76 STAYS 0    *** SILENT ***
  f231/232  B (2nd)   -> $0054=0A, $0D9A=FF, $0D17 01, $0D76=03 (blip; SCROLLBAR HIDDEN)
  f273      B (3rd)   -> $0067=00 (ribbon), $0054=01, $0D76=03  (blip)
  f323      B (4th)   -> the menu CLOSES, $0D76=04
```

### 4.5 The equipment INFO PANEL (substate 5) and the description box
```
ct_probe.exe ROM --monitor "7E0067:2,7E0054:1,7E0D17:1,7E0D13:1,7E0F00:2,7E04C9:1,7E0D9A:1" \
  --from scratchpad/menu/cr.state \
  --btnscript "0:10,200:2,0:60,80:2,0:20,100:2,0:40,100:2,0:30,100:2,0:80" --diff
```
```
  f95   $0067=02                                  <- Item screen
  f97   $0D17=02  $0F00=FFFF  $04C9=03            <- row 0 = SteelSaber
  f137  $0F00=0000 ; f138 $0D17=03                <- A: picked
  f169  $0D13 39 -> 3D   $0D9A=FF                 <- A: overlay bit set, SCROLLBAR HIDDEN
  f170  $0D17=05                                  <- *** substate 5 ***
```
`--peek` of that frame (`scratchpad/menu/cr_eq.png`) shows the list box narrowed and a right-hand
panel reading **"Can be used by: / Crono"** and **"Attack:  15"**.

The plain list of the same state (`scratchpad/menu/cr_list.png`) reads
`SteelSaber: 1 WEAPON / Lode Sword: 1 WEAPON / Bolt Sword: 1 WEAPON / BronzeMail: 1 ARMOR /
PowerGlove: 1 ACCESSORY / … / Vigil Hat: 1 HELMET`, with the ▼ at the right edge and an EMPTY
description box (0x03's description string is empty).

Four Downs later (`scratchpad/menu/cr_pg.png`), with the cursor on the **PowerGlove**, the
description box reads **"Power +2"** — an EQUIPMENT description, which is impossible under the
"generated from the effect record" model and exactly what `$CC:2EB1[0x97]` decodes to.

`saves/menu_open_gato2.state` (`gt_list.png`): `Pendant   : 1 ITEM`, white name, empty description.
`saves/forest_exit.state` (`fx_list.png`): `Tonic     : 1 ITEM` with **Tonic in YELLOW** and the
description box reading **"Restores 50 HP"**.

### 4.6 Our engine's row geometry vs the ROM's — column-exact
```
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy CT_ITEMDEMO=1 \
  CT_MENUSHOT=scratchpad/menu/eng_list.raw CT_MENUSHOT_AT=40 \
  build/ct_field.exe --assets ct_assets.dat --scene 8 --selftest scratchpad/menu/_x.raw \
  --frames 160 --play "wait:20; m:1; wait:20; r:1; w:6; a:1; wait:60"
```
Bright-pixel column runs across the Tonic row:
```
 ROM  (fx_list.raw, y95..108) : (17,18)(20,20)(25,25)(27,36) (49,54)(56,61)(64,69)(75,76)(80,85)
                                (131,132) (145,148) (162,165)(169,174)(176,182)(184,190)
 OURS (eng_list.raw, y86..100): (17,18)(20,20)(25,25)(27,36) (49,54)(56,61)(64,69)(75,76)(80,85)
                                (131,132) (144,150) (162,165)(169,174)(176,182)(184,190)
```
identical except the count glyph (ROM "1" vs our "3" from `CT_ITEMDEMO`). Row baselines:
ROM rows at decoded y 96,108,120,…,204 (pitch 12); ours at 88,100,112,… — i.e. the SAME rows once
the peek's +8 overscan is removed. **The list row layout needs no work.**

### 4.7 The USE path, the heal chime, and the BUZZ (`saves/forest_exit.state`)
`forest_exit`: FIELD loc 19, party = Crono only, Crono 56/70, inventory = Tonic x1.
```
ct_probe.exe ROM --monitor "7E0054:1,7E0D17:1,7E0DBC:1,7E0DBD:1,7E2400:2,7E2500:2,7E2603:2,7E1E00:3,7E0D76:1" \
  --from saves/forest_exit.state \
  --btnscript "0:10,200:2,0:70,80:2,0:20,100:2,0:40,100:2,0:30,100:2,0:40,100:2,0:90,100:2,0:40,100:2,0:40" --diff
```
```
  f107  $0D17=02                                     <- list, Tonic highlighted
  f148  $0D17=03                                     <- A: picked
  f179  $0054=0C  $0DBD=84                           <- A: target overlay (2-frame build)
  f182  $0DBC=02                                     <- HP is restorable -> VALID
  f221  $2603 = 3800 -> 4600   $1E01 = 06            <- A: 56 -> 70 HP, *** the HP chime, id 06 ***
  f263  $2400=0000 $2500=0000  $0DBD -> 00           <- the last unit consumed, id zeroed IN PLACE
  f264  $0DBC -> 00                                  <- the overlay is STILL OPEN, now invalid
  f313  $1E01 = 01  $0D76=03                         <- A: *** THE BUZZ, id 01 ***
  f355  $1E01 = 01  $0D76=03                         <- A: it buzzes again, and again
```
This is the first time this repo has driven the `$0DBC` refusal (menu_sfx.md I14 was marked
"not driven"). The throttle writer is `C2:EB15` on every one of those frames, i.e. all three sounds
go through `C2/EB03`.

### 4.8 The FULL-HP refusal, isolated with `--callrom`
A savestate captured mid-overlay one frame after the heal (Tonic still in the slot, Crono now 70/70):
```
PEEK_SAVE=scratchpad/menu/tgt_full.state ct_probe.exe ROM --peek saves/forest_exit.state \
  scratchpad/menu/tgt_full.raw --btnscript "…,100:2,0:19"
ct_probe.exe ROM --read "wram:7E0DBB+4,wram:7E2603+8,wram:7E2400+2,wram:7E2500+2,wram:7E0073+1,wram:7E006F+2" \
  --from scratchpad/menu/tgt_full.state
    7E0DBB+4 = 0C 02 84 00      ; base 0x0C, stale $0DBC=02, $0DBD=84 (HP item)
    7E2603+8 = 4600 4600 0800 0800   ; Crono 70/70 8/8   -- FULL
    7E2400+2 = BD00 ; 7E2500+2 = 0100 ; $73 = 01 ; $6F = 2600
ct_probe.exe ROM --callrom C2/B870 --from scratchpad/menu/tgt_full.state \
  --regdb 7E --regdp 0000 --m8 --x16 --writes
    callrom: ret ops=160 a=0000 x=2600 ... C=0 Z=1
    w 7E0DBC = 00  (by C2:B873)      <- the STZ only; NEITHER INC fired
    w 7E0DBE = 00  (by C2:B881)
```
So with an HP item and a target already at `[sheet+0x3F]`, `C2/B870` returns `$0DBC = 0` = REFUSE.

### 4.9 Our engine eats the whole stack on a full-HP party
```
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy CT_ITEMDEMO=1 CT_MENUSFX=1 \
  build/ct_field.exe --assets ct_assets.dat --scene 8 --selftest scratchpad/menu/_x.raw --frames 400 \
  --play "wait:20; m:1; wait:20; r:1; w:6; a:1; wait:30; a:1; wait:20; a:1; wait:40; a:1; wait:40; a:1; wait:40; a:1; wait:40; a:1; wait:40"
```
`CT_ITEMDEMO` seeds Crono 20/70 + 3 Tonics. Sounds posted: `ribbon-tab`, `screen-open`, `item-a` x3 —
and **nothing at all on any of the four target-select A presses**. The final menu shot
(`scratchpad/menu/eng_use3.png`) shows the Tonic row GONE: all three Tonics were consumed, the last
two healing an already-full Crono. The ROM would have healed once (chime id 06) and then buzzed twice
with two Tonics still in the bag.

---

## 5. IMPLEMENTATION SPEC (in the engine's own terms)

### 5.1 `field.c` — new item-screen state
Add, next to `menuItemRow/Scroll/Label/Picked/Tgt`:
```c
int menuItemBlink = 0;      /* $0D96: 3-bit down-counter, ticks once per menu frame            */
int menuItemBlinkOn = 1;    /* $0D97 bit0                                                       */
int menuItemBarHide = 0;    /* $0D9A: non-zero = the scrollbar draws nothing                    */
int menuItemValid = 0;      /* $0DBC: bit1 HP-restorable, bit0 MP-restorable                    */
int menuItemFx = 0;         /* $0DBD: bit7 HP, bit6 MP, bit3 all-party                          */
int menuItemMsg = 0;        /* $68 == 5: the equipment info panel is up                         */
```
Tick the blink once per menu frame exactly as `C2/8BB8` does (`v = (v & 7) - 1; if (v < 0)
{ on ^= 1; }`, storing `v` back as `0xFF` when it underflows) — do NOT reuse the pinned-hand's
4-on/4-off phase (`$0DA3`/`$0DA0`), which is a different counter with a different period.

### 5.2 `field.c` — the missing branches
1. **Pick an empty slot.** `C2/AA9C` has no test: delete the `if (s7[0x2400 + absSlot])` guard so
   `menuItemPicked = absSlot` always. (This is what makes "pick the hole, then A on the item" a legal
   Move in the ROM.)
2. **A on the same slot** (`C2/AB14`), in this exact order:
   - `$7700[id] & 0x02` -> target-select (as today);
   - `id == 0 || id >= 0xBC` -> unpick, back to the list, nothing else;
   - else (`id < 0xBC`, equipment) -> `menuItemMsg = 1`, `menuItemBarHide++`, and render the info
     panel. **A or B** dismisses it: one blip, `menuItemMsg = 0`, `menuItemBarHide = 0`, back to the
     LIST (not to PICKED).
3. **B from the list** (`C2/AAAD`) must also do `menuItemBarHide++` and clear the description
   (`C2/AAC3 LDA #00 / JMP C2/F2F3`); **A on "Use/Move"** (`C2/AA6C`) clears it again.
4. **Target-select** (`C2/B1A5`), replacing the current unconditional `CtMenuItemUse`:
   ```c
   menuItemValid = CtMenuItemTargetMask(s7, menuItemPicked, menuItemTgt);  /* C2/B870 */
   if (aEdge) {
     if (!menuItemValid) menuSfx(s7, 0x01, "target-invalid");   /* C2/B1BA -> EACC, cmd 18 id 01 */
     else {
       int fx = CtMenuItemUse(...);                             /* C2/8F55 + C2/B31C            */
       if (fx & 1) menuSfxCmd19(s7, 0x06, "heal-hp");           /* C2/9027 -> EAF9, cmd 19 id 06 */
       if (fx & 2) menuSfxCmd19(s7, 0x07, "heal-mp");           /* C2/903F -> EAF9, cmd 19 id 07 */
     }
     /* NO exit here: C2/B1C5 BRA -> C2/B1A5 in BOTH cases */
   }
   ```
   and **delete** the `if (…s7[0x2400+menuItemPicked] == 0) { menuItemTgt = -1; … }` early exit —
   `C2/B31C` only does `STZ $0DBD`, which makes the mask 0 from then on. The overlay leaves only on B.
   `menuSfxCmd19` must post `$1E00 = 0x19` and **clear** `$0D76` first (`C2/EAF9 STZ $0D76`), so it is
   never throttled.
5. **Target nav.** The ROM CLAMPS, it does not wrap: `C2/B848-B864` puts an out-of-range index back on
   the first or the last member depending on `$5A` bit3. Our `% partyN` wrap is unproven — see §6.

### 5.3 `menu.c` — `CtMenuItemTargetMask` (new, byte-exact `C2/B870`)
```c
/* C2/B870. mask bit1 = HP restorable, bit0 = MP restorable; 0 = the A press must BUZZ. */
int CtMenuItemTargetMask(const uint8_t* mem, int absSlot, int tgtSlot) {
  int id = mem[0x2400 + absSlot];
  int fx = ctItemFxByte[id];                 /* $0DBD, baked from $CC[rec+1] | ((type+1)&3)<<2 */
  int n = (fx & 0x08) ? partySize(mem) : 1;  /* C2/B891-B898                                   */
  int i = (fx & 0x08) ? 0 : tgtSlot;
  for (; n--; i++) {
    const uint8_t* sh = mem + 0x2600 + (mem[0x0D5F + i] & 0x7F) * 0x50;   /* C2/8820 */
    int m = 0;
    if ((fx & 0x80) && rd16(sh + 3) <  rd16(sh + 0x3F)) m  = 1;  /* C2/B8AC-B8B6 (16-bit!) */
    m <<= 1;                                                     /* C2/B8B9 ASL            */
    if ((fx & 0x40) && sh[7]        <  sh[9])           m |= 1;  /* C2/B8C3-B8CB (8-bit!)  */
    if (m) return m;                                             /* C2/B8CE                */
  }
  return 0;
}
```
Note the asymmetry, which is the ROM's: the HP test is a **16-bit** compare against **`sheet+0x3F`**,
the MP test is an **8-bit** compare of `+7` against `+9`.
(The `mem[0x0D5F + i] & 0x7F` above is a SKETCH: `C2/8839` branches on `BMI ->C2/8880` for a slot byte
with bit7 set and that branch was not decoded — see §6.)

### 5.4 `menu.c` — `CtMenuRenderItem`, the scrollbar
Replace the two fixed blits with the ROM's moving pair:
```c
/* C2/8BB8 with the Item screen's seeds (C2/ADD0 + C2/EA81) */
if (!barHide && blinkOn) {
  int yUp = 80 + ((scroll * 0x0072) >> 8);            /* $0D93 + hi(s*step), 8-bit truncated */
  if (scroll > 0)   blitArrowUp  (fb, 232, yUp);      /* tile 0x22 attr 0x30, 16x16 cell     */
  if (scroll < 232) blitArrowDown(fb, 232, yUp + 8);  /* tile 0x22 attr 0xB0 = V-flipped     */
}
```
where the 16x16 cell's top-left is `(232, y)` and the triangle ink lands on rows 1..12 of it (§2.3).
`barHide` must be set by: the LABEL row, the equipment info panel, and target-select (today only
target-select suppresses it, via `if (!targetMode)`).

### 5.5 `menu.c` — the description
Replace `mItemDesc()`'s generator with a lookup of the ROM string
`$CC:0000 + rom16($CC:2EB1 + id*2)`, decoded with the same dictionary decoder the dialogue text
already uses (`assets/text.py load_dict` produces the 128 substrings; bake them, or bake the 242
decoded strings straight into `src/data/menu_romdata.h` beside `ctItemCat[]`). Every id 0x00-0xF1 gets
an entry; an empty string means an empty box. Keep showing it for the highlighted row (`C2/AD2E` ->
`C2/F2F3`) and for the picked item while the overlay is up.

### 5.6 What is already right — do not touch
The row layout (icon / name x48 / `:` x128 / count / class x160), the 12-px pitch, the first row at
engine-y 88, the yellow rule (`$7700 & 0x02`), `ctItemCat[]` (242/242 against `$FF:B310`), the
scroll clamp (`0..232`), the pick/Move/Organize logic, the blip-on-every-A rule, and the silent
pick-cancel are all confirmed correct against the ROM this pass.

*(Provenance note: `build/ct_field.exe` used for §4.6/§4.9 was built before a parallel lane's
Config/Tech edits landed in `src/menu.c` / `src/field.c`. `git diff` shows those hunks start at the
old `field.c:8365` (the `menuScreen == 4` branch) and inside `CtMenuRenderConfig`/`CtMenuRenderTech` —
the Item block, `field.c:8265-8364` and `menu.c:470-631`, is untouched, so the differential holds.)*

---

## 6. OPEN / BLOCKED

1. **`C2/EAD6` has no caller.** It reads `$0DBC` bits 1/0, posts ids 6/7 through `C2/EB03` and TRBs
   the bit — i.e. it is a `$0DBC`-driven twin of the `$0F5D` chime at `C2/9016`. A byte scan of the
   whole 4 MB image finds no `JSR`/`JMP`/`JSL`, no 16- or 24-bit pointer, and no relative branch that
   targets `$EAD6`. Recorded as apparently DEAD; the live chime is `C2/9016` (cmd `$19`), which is
   what the oracle actually posted. If a computed/RAM-built vector reaches it, that is unfound.
2. **`sheet+0x3F` vs `sheet+0x05`.** `C2/B8AF` compares against `+0x3F`; in every sheet this repo can
   read (`menu_open_gato2` Crono 70 / Marle 65, `com_jogo_salvo` Crono 96 / Marle 76) `+0x3F` equals
   the `+0x05` maxHP, so the two are **not separable here**. The existing memory note calls `+0x3F`
   the equipment-adjusted effective max (and `C2/8E82`'s write cap uses it), which is consistent, but
   this pass did not prove it. *Unblocked by:* a capture with an HP-boosting accessory equipped.
3. **The multi-member target cursor (clamp vs wrap) was NOT driven.** `C2/B848-B864` reads as an edge
   CLAMP (to `$0DBB` or `$0DBB + $73 - 1` per `$5A` bit3), while our engine wraps modulo `partyN`.
   Every FIELD state in this repo that holds a field-usable item (`saves/forest_exit.state`) has a
   ONE-member party; every 2-3 member field state holds only Pendant/Heal/Revive, none field-usable.
   *Unblocked by:* **a field savestate with 2+ active party members and at least one Tonic/Ether in
   `$7E2400`** — e.g. the Truce/forest area right after buying Tonics, converted with
   `tools/s9x_to_lake.py`. The same capture would also drive the all-party (`$0DBD` bit3) scan in
   `C2/B870`, the `C2/8AD5` multi-highlight task, and the `C2/B228`/`C2/B275` TAB loop is still
   undriven for want of a state holding a Power/Magic/SpeedTab.
4. **"Target at full HP with the item still in hand" was measured by `--callrom`, not in a live
   frame.** The live buzz (§4.7) was reached through the other road to `$0DBC == 0` (`C2/B31C`'s
   `STZ $0DBD` after the last unit). Both are the same `LDA $0DBC / BNE` test, but the live full-HP
   path wants a state with 2+ of a consumable. Same capture as (3) unblocks it.
5. **`C2/AEB6`'s exact panel content.** The "Can be used by:" list and the "Attack: 15" number are on
   screen (§4.5) and the table is `$7E:7A00 + id`, but the routine's second half (from `2AECD`) was
   read only as raw bytes — the linear decoder desyncs there and the exact stat row (Attack vs
   Defense vs both, and where the number comes from) was not disassembled. The panel's window
   geometry (`LDX #C0D5 / JSR C2/ED31`) was likewise not decoded.
6. **`$5A`**, the nav's per-move status byte (bit2 = the scroll direction at `C2/AC8F`, bit3 = the
   clamp direction at `C2/B854`), is used but not derived; it is produced by the shared nav-graph
   mover `C2/E9E5`, which belongs to the shell lane.
7. **The description is rendered by `C2/FA49`**, the ROM's dialogue engine, with
   `$0DC5 = 0x4E84`, `$0DCC = 0x6840`, `$0DC9 = 1`, `$0DD0 = 0x0080` as its window parameters. Those
   four constants were read but not interpreted, so the description box's exact pixel geometry (as
   opposed to its TEXT, which is settled) is still approximate in our renderer.
8. **`$0F5D`'s clear.** The chime task self-kills (`C2/9016 -> JMP C2/8216`) after consuming both
   bits with `LSR`, so the flag is drained by the shifts. Whether anything else re-arms it between
   two uses in the same overlay session was not traced; both `LSR`s run every spawn.

---

# ADVERSARIAL VERIFICATION (second agent, independent pass)

Method: every load-bearing address in §2/§3 was **re-disassembled** with `tools/dis.py` (and hand-decoded
from raw bytes where the linear decoder desyncs), every §4 measurement was **re-run from scratch with a
script I wrote myself** (never copied where the dossier elided it), and the two claims that rest on
exhaustive scans (`C2/EAD6` dead; `C2/AB62` touches no `$0415/$0416`) were re-scanned with my own
scanner. Verdict: **SOUND_WITH_FIXES.** The mechanism is right and the implementation spec is safe to
build on after seven corrections, one of which (V-2) would mis-implement `C2/8820`.

## AV.0 — the seven defects, ranked

| # | § | claim | verdict |
|---|---|---|---|
| **V-1** | 2.6 | "A third `JMP C2/EACC` sits at `C2/B407`. **Those three are the only buzz sites in the whole Item region**" | **REFUTED** — AV.1 |
| **V-2** | 5.3 | `mem[0x0D5F + i] & 0x7F` ("a SKETCH … see §6") | **WRONG, and the §6 pointer is dangling** — AV.2 |
| **V-3** | 2.4 | the `$0D9A` writer census | **INCOMPLETE** — omits `C2/B222 STA $0D9A` — AV.3 |
| **V-4** | 1 / 2.5 | `$0DBC` "Recomputed EVERY FRAME of the target overlay" | **OVERSTATED** — per loop ITERATION; measured 42-frame stall — AV.4 |
| **V-5** | 1 | "`C2/EAD6` does the same thing" (as the `$19` chime) | **IMPRECISE** — `EAD6` goes through `C2/EB03` = command **`$18`** — AV.5 |
| **V-6** | 2.9 | `2AABE LDA #E0 / STA $0701` annotated "(the list hand)" | **UNPROVEN + self-inconsistent** — §3 puts the list hand at OAM **28** (`$0770`) — AV.6 |
| **V-7** | 2.3 | the OAM shadow blank range "`$7E:0740..087E`" | **off by one** — it is `$0740..$087F` — AV.7 |

Everything else in §1–§5 that I could reach reproduced **exactly**, including two measurements the
dossier could not make (AV.8, AV.9) and one it made only from disassembly (AV.10).

## AV.1 — V-1: the buzz-site census is wrong, and the Item screen has exactly TWO

My own operand scan of the whole of bank `$C2` for absolute-mode instructions whose operand is `$EACC`
(`scratchpad/menu/_scan_ops.py` — all 50 abs / abs-indexed opcodes, not just `20`/`4C`):

```
C2/9C8D JMP   C2/B1BA JSR   C2/B23D JSR   C2/B38F JMP   C2/B3EF JMP
C2/B406 JMP   C2/B437 JMP   C2/B4E3 JMP   C2/C63A JMP   C2/CEF2 JMP
C2/D5D6 JMP   C2/D686 JMP   C2/D738 JMP   C2/E3FB JSR
```

Two errors in that sentence:

1. **The address is `C2/B406`, not `C2/B407`.** `B407` is the *operand* of the `4C` at `B406`.
2. **`C2/B406` is not in the Item screen at all — it is in the TECH screen.** Measured from the ROM's
   own dispatch tables (`C2/826F`: `LDA $C28292,X` with `X = $67*2`, then `LDA [$16],Y` with `Y = $68*2`):

```
C2/8292 screen table = 99CA 9C29 AA34 B353 BF35 C621 CED4 C7A6
                        ^0    ^1  ^2=ITEM ^3=TECH ^4   ^5   ^6
C2/AA34 item sub-table = AA40 AA4A AA86 AAC8 AB4A AC1D    (6 entries, AA34..AA3F; AA40 = the 1st handler)
C2/B353 tech sub-table = B35D B365 B36D B3AE B48E         (5 entries, B353..B35C; B35D = the 1st handler)
```
   So the Item screen's code is `C2/AA34..B352` and `B353+` is Tech. `B38F`, `B3EF`, `B406`, `B437`,
   `B4E3` are **five** TECH refusals (`B406`'s guard is `LDA $7700,X / BMI` on a *tech* record, not on
   `$0DBC`) — the dossier saw one of them and mis-filed it. The item sub-table's own length settles the
   boundary: 6 entries occupy `AA34..AA3F` and the first handler starts immediately at `AA40`.
3. **The corrected claim is stronger, and I verified it closes.** The item-use dispatch is
   `C2/B0E9 JSR ($B0EE,X)`; the vector table reads
   `C2/B0EE = B17F B0ED B0ED B107 B1F8 B200` — **every vector is `< $B353`**, i.e. inside the Item
   region (`B107` falls through to `JMP C2/B17F`; `B1F8`/`B200` are the TAB path into `B215/B228`).
   No item effect handler can reach a Tech buzz.

   > **Corrected text for §2.6:** *the Item screen (`C2/AA34..B352`) contains exactly TWO `C2/EACC`
   > sites — `C2/B1BA` (the `$0DBC` refusal in the single/all-target loop) and `C2/B23D` (the same
   > refusal in the TAB loop). There is no third.*

This does **not** disturb §5.2: the spec was already "buzz iff `$0DBC == 0`".

## AV.2 — V-2: `C2/8820`/`C2/8839` does NOT mask bit7; it returns without touching `$6F`

§5.3 sketches the party-slot→sheet resolution as `mem[0x0D5F + i] & 0x7F` and points at §6 — but §6 has
no such entry, so the sketch would be copied into C as-is. Hand-decoded from the raw bytes at `0x28839`
(`tools/dis.py` desyncs here; bytes pasted so this is checkable):

```
28839  8F 02 42 00   STA $004202          ; A = $0D5F,X (the party-slot byte)
2883D  09 00         ORA #00              ; set N from A
2883F  30 3F         BMI  ->28880         ; *** slot byte NEGATIVE -> straight to the RTS ***
28841  A9 50         LDA #50
28843  8F 03 42 00   STA $004203          ; * 0x50
2884A  A9 00 26      LDA #2600
2884D  6F 16 42 00   ADC $004216
28851  85 6F         STA $6F              ; $6F = 0x2600 + charId*0x50   <- SKIPPED when bit7 is set
...
2887F  1A            INC A
28880  60            RTS                  ; <- the BMI target
```

So for a slot byte with bit7 set (an absent member — `C2/8820` already flags that case with
`CPX $73 / BCC + / DEC $78`) the routine **returns leaving `$6F` at its previous value**, and
`C2/B870`'s very next instruction `2B8A1 LDX $6F` therefore re-reads the **previous member's** sheet.
It does not read `sheet[slotByte & 0x7F]`. A port must reproduce *"if the slot byte is negative, keep
the previous `$6F`"*, not a mask. (This also closes the dangling `see §6`.)

## AV.3 — V-3: one missing `$0D9A` writer

Same exhaustive operand scan, restricted to the Item region `C2/AA34..B352`:

```
C2/AA6C STZ   C2/AAB7 DEC   C2/AB2F DEC   C2/AC36 STZ
C2/B195 STA   C2/B1D6 STZ   C2/B222 STA   (+ C2/8B96 STZ at spawn; C2/8BD6 LDA = the task's own read)
```
`C2/B222 STA $0D9A` (`LDA #0C` at `B218` — the TAB overlay's prologue, the twin of `C2/B195`) is absent
from §2.4's list. Harmless for the spec (same suppress, same value `0x0C`), but the census was presented
as complete. Every other hit the scan found (`A36F A565 A6D1 A76C A90A D5CC D63B D67C D6B7 E216 E52F`)
is outside `AA34..B352` — so §2.4 is otherwise exactly right.

## AV.4 — V-4: `$0DBC` is recomputed per LOOP ITERATION, and the loop stalls inside the apply

Re-running §4.7 (my script, AV.11 §6) shows `$0DBC` **frozen at `02` from f222 to f263** — 42 frames
during which Crono was already `70/70` (`$2603 = 4600`) with `$0DBD = 84` still armed. A literally
per-frame `C2/B870` would have produced `00` at f222. The cause is structural, not a contradiction:
`C2/B1BF JSR C2/8F55` (the apply + its count-up + the `C2/9016` chime task) yields for those frames, so
`C2/B1A5`'s `JSR C2/B83E` does not run. §1's "Recomputed EVERY FRAME of the target overlay" should read
**"once per iteration of the `C2/B1A5` loop"**. Behaviourally inert (the only consumer is the `A` test
at `B1B5`, inside the same iteration), but it changes what a `$0DBC` trace looks like — a regression
check pinned on `$0DBC` must not assume a per-frame value.

## AV.5 — V-5: `C2/EAD6` posts command `$18`, not `$19`

```
2EAD6  PHP / SEP #30 / STZ $0D76
2EADC  LDA $0DBC / AND #02 / BEQ ->2EAE7 / LDX #06 / BRA ->2EAF0
2EAE7  LDA $0DBC / AND #01 / BEQ ->2EAF7 / LDX #07
2EAF0  TRB $0DBC / TXA / JSR C2/EB03        ; *** EB03 = LDX #18 / STX $1E00 ***
```
§6.1 states this correctly ("posts ids 6/7 through `C2/EB03`"); §1's shorthand "does the same thing"
does not — the live chime is `C2/EAF9` (`LDX #19`), the dead twin is `C2/EB03` (`LDX #18`). The ids
coincide, the command byte does not.

**Dead-code verdict re-run independently** (`scratchpad/menu/_scan_ead6.py`, whole 4 MB image): no
`20/4C/22/5C/FC/7C/DC/6C` with operand `EAD6`; no `D6 EA C2` 24-bit pointer anywhere; no relative branch
(`10/30/50/70/90/B0/D0/F0/82`) in bank `$C2` landing on `$2EAD6`; the twelve `D6 EA` byte pairs in the
image sit at `03E8DC 03E988 03E9F0 03EA0C 03EA50 03EA94 10D452 1EE368 213EBF 21E42D 21E51D 228FDB` —
**all outside bank `$C2`**. CONFIRMED as far as a static scan reaches.

## AV.6 — V-6: `$0701` is OAM entry 0, and nothing proves it is the list hand

Exhaustive scan: in the whole of bank `$C2` the only abs-mode write to `$0701` is `C2/AAC0`, and the
only abs-mode write to `$0770` is `C2/E967` (`LDA $9380,X / STA $0770`, inside the per-frame redraw).
`$0770` is OAM entry **28** — which is what §3 says and what the OAM dump shows carrying the hand.
`$0701` is OAM entry **0**, inside the `$0700..$073F` block that `C2/E92D` does **not** blank. §2.9's
"(the list hand)" is an uncited annotation that contradicts §3; the safe statement is "`C2/AAC0` parks
OAM entry 0 off-screen".

## AV.7 — V-7: the blank range, and `C2/E92D` really is slot 3

```
2E92D  PHP / REP #30
2E930  LDA #E000 / STA $0740
2E936  LDX #0740 / LDY #0742 / LDA #013D / MVN $7E,$7E     ; 0x13E bytes -> last byte = $087F
2E942  LDA #AAAA / STA $0904
2E948  LDX #0904 / LDY #0906 / LDA #0011 / MVN $7E,$7E     ; $0904..$0917 = AA  (sprites 16..95)
```
Range is `$0740..$087F` (80 OAM entries, 16..95), not `..$087E`. The **slot-3** attribution, which §2.3
asserts without a citation, is CONFIRMED — hand-decoded from `0x281AA`:
`A9 20 30 / 8D 58 00 / A9 1B E9 (LDA #E91B) / A2 18 00 (LDX #0018 -> slot 3) / 20 49 82 (JSR C2/8249)`,
and `C2/E91B` is `JSR C2/E92D / JSR C2/821E / BRA` — a task body. (The same site spawns
`LDA #826F / LDX #0008` = the screen dispatcher into slot 1, matching menu2_shell_exit.)

---

## AV.8 — NEW MEASUREMENT: command `$19` id `$06` proven at the APU PORT

The dossier's "cmd `$19`" was disassembly-only, and it **cannot** be seen with `--monitor`: the sound
driver clears `$1E00` before the frame boundary, which is why every §4 trace reads `$1E00 = 00`.
`--callrom` sees it:

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --callrom C2/EACC \
  --from saves/forest_exit.state --regdb 7E --regdp 0000 --m8 --x8 --writes
    callrom: ret ops=12 a=0004 x=0018 ... C=0 Z=0
    w 7E1E00 = 18  (by C2:EB08)        <- THE BUZZ: command $18
    w 7E1E01 = 01  (by C2:EB0B)        <-           id $01

scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --callrom C2/EAF9 --rega 0006 \
  --from saves/forest_exit.state --regdb 7E --regdp 0000 --m8 --x8 --writes
    ... w 002143 = FF (by C7:0623)
        w 002142 = 80 (by C7:0628)
        w 002141 = 06 (by C7:062D)     <- id $06
        w 002140 = 19 (by C7:0635)     <- *** COMMAND $19, on the APU port ***
        w 001E00 = 00 (by C7:0196)     <- the driver CLEARS the mailbox (why --monitor shows 00)
```
§1/§2.7's chime spec is now measured end-to-end. Corollary for `add-regress-check`: a check must **not**
pin `$1E00` at a frame boundary — only `$1E01`/`$1E02` survive, or use `--callrom`.

## AV.9 — NEW MEASUREMENT: A **and** B both dismiss the equipment panel

§2.8's `LDA $0D1D / AND #C0` was disassembly-only. Both drives:

```
scratchpad/menu/ct_probe.exe ROM --monitor "7E0D17:1,7E0D13:1,7E0F00:2,7E0D9A:1,7E0D76:1" \
  --from scratchpad/menu/cr.state \
  --btnscript "0:10,200:2,0:60,80:2,0:20,100:2,0:40,100:2,0:30,100:2,0:60,<X>:2,0:40" --diff
```
with `<X>` = `100` (A) and `<X>` = `1` (B) — **byte-identical traces**:
```
  f170  $0D17=05  $0D13=3D  $0F00=0000  $0D9A=FF
  f231  $0D13 3D -> 39      $0D9A FF -> 00   $0D76=03      (one blip)
  f232  $0D17 05 -> 02      $0F00 -> FFFF                  (back to the LIST, not to PICKED)
```

## AV.10 — NEW MEASUREMENT: the scrollbar tile and its on-screen ink, measured differentially

§2.3's tile art is CONFIRMED byte-for-byte, decoded from my own VRAM dump (`--ppuregs` on the same
frame: `objSize=0 objTileAdr1=4000`, so tile `0x22` is the top-left of a 16x16 cell = tiles
`22/23/32/33` at VRAM byte `0x8440`):
```
 0 ................       8 ...FEDDDDDEF....
 1 .......F........       9 ...FEDDDDDEF....
 2 ......FEF.......      10 ..FEDDDDDDDEF...
 3 ......FEF.......      11 ..FEEEEEEEEEF...
 4 .....FEDEF......      12 ..FFFFFFFFFFF...
 5 .....FEDEF......      13-15 blank
 6 ....FEDDDEF.....
 7 ....FEDDDEF.....
```
And the composite ink, measured as a **framebuffer difference between the blink-ON frame and the
blink-OFF frame 8 frames later** (so no colour-threshold guessing), for OAM `(232,82)` + `(232,90)`:
```
 y= 91 x239..239   y= 95 x237..241   y= 99 x235..243   y=103 x234..244   y=107 x236..242   y=111 x238..240
 y= 92 x238..240   y= 96 x236..242   y=100 x234..244   y=104 x235..243   y=108 x237..241   y=112 x239..239
 y= 93 x238..240   y= 97 x236..242   y=101 x234..244   y=105 x235..243   y=109 x237..241
 y= 94 x237..241   y= 98 x235..243   y=102 x234..244   y=106 x236..242   y=110 x238..240
```
Exactly the dossier's model: up arrow decoded `y 91..102`, down arrow `y 101..112`, two rows of overlap,
`x 234..244`, apex a single pixel at `x = 239 = 232 + 7`. The relation that falls out is
**`decoded_y = OAM_Y + tileRow + 8`** for both sprites — i.e. under the `+8` overscan convention there
is **no** extra `+1` here. That is a property of the peek decode, not of the ROM, so §5.4 (which places
the 16x16 cell at the ROM's `(232, yUp)`) is unaffected; the `+/-1` ambiguity the dossier flagged remains
exactly where it was.

---

## AV.11 — everything that reproduced EXACTLY

**Disassembly re-derived at the cited address.** (⚠ `tools/dis.py` needs `x=0` at `C2/8BB8`, `C2/ADD6`,
`C2/AA86`, `C2/AB4A`, `C2/AF42`, `C2/B0AB`, `C2/F2F3` or it desyncs — the dossier's listings are the
*correct* ones.) Verified: `C2/8B93`, `C2/8BB8` (all of it, incl. `LDX #E000 / TXY`), `C2/ADD0`,
`C2/EA81`, `C2/E92D`, `C2/E984`, `C2/AA4A`, `C2/AA86`, `C2/AAC8`, `C2/AB14`, `C2/AB4A`, `C2/AC1D`,
`C2/AC3A..ACB9`, `C2/AD2E`, `C2/AE43/AE65/AE75`, `C2/AE91`, `C2/AEB6`, `C2/AF42`, `C2/B04B`, `C2/B0AB`,
`C2/B17F..B1E1`, `C2/B24A`, `C2/B275`, `C2/B83E`, `C2/B870`, `C2/8255`, `C2/89E4` (`LDX #0030` = slot 6),
`C2/8FE6` (`LDA #0001 / STA $0F5D`), `C2/900A` (`ORA #0002`), `C2/9016`, `C2/9047`,
`C2/EAC2 / EACC / EAD6 / EAF9 / EB03`, `C2/8463` (`DEC $0D76`), `C2/9A98`.

Two refinements, not defects:
* `C2/AEB6` reads `LDX $04C9 / LDA $7A00,X / **AND $29AF**` — the equip-mask is ANDed with a party mask
  before the name list is built (§3 cites `$7A00` correctly but omits the mask).
* the TAB validator `C2/B275` sets `$0DBC` with `LDA #02 / STA`, not `INC` — consistent with "bit1",
  and its cursor-move blip is `C2/B288` (the item loop's is `C2/B88A`; `src/field.c:8301` cites `B288`).

**Oracle measurements re-run** (all `scratchpad/menu/ct_probe.exe` + `Chrono_Trigger_USA_SNES.sfc`;
button grammar checked each time: `A = 0x100`, `X = 0x200`, `Right = 0x80`, `Down = 0x20`, `B = 0x01`):

1. **§4.1 verbatim** → identical: `$0D92=E8 $0D93=50 $0D94=E8 $0D98=72 $0D99=00 $0D9A=00`, `$0D96`
   cycling `FF,06,05,04,03,02,01,00`, `$0D97` flipping every **8** frames (f22 → f30 → f38 → f46 → f54).
2. **§4.2 with my own 20-Down script** (`0:10,80:2,0:8,100:2,0:20` + `(20:2,0:6)x20` + `0:20`) →
   identical rows: `s=1 -> Yup 0x50`, `3 -> 0x51`, `5 -> 0x52`, `7 -> 0x53`, `9 -> 0x54`, `0B -> 0x54`;
   at `s=0` the up arrow is blanked (`00E0`) and the down arrow sits at `(232,0x58)`. Against
   `Yup = 80 + ((s*114)>>8)`: **exact, every sampled row.**
3. **§4.3** `--peek` + `python tools/ct.py oam` → `* 28  24 195 020 30 …`, `* 30 232 82 022 30 …`,
   `* 31 232 90 022 B0 … vf=1`. `--ppuregs` on the same frame: `objSize=0 objTileAdr1=4000`.
4. **§4.4 both ladders** → identical. Key item (`$04C9=D6`): f63 pick / f64 `$0D17=03` / f105 blip /
   f106 straight back to `$0D17=02` with `$0F00=FFFF` — no panel, no buzz. Empty slot: f95
   **`$0F00=0100` while `$04C9=00`** — the ROM picks the hole. B-ladder on `forest_exit`: f191 B →
   `$0D17 02`, `$0F00 FFFF`, **`$0D76` stays 0 (SILENT)**; f231 B → `$54=0A`, `$0D9A=FF`, blip;
   f273 B → `$67=00`; f323 the menu closes (`$0D76=04`).
5. **§4.5** on `scratchpad/menu/cr.state` → f169 `$0D13 39 -> 3D` + `$0D9A=FF`, f170 `$0D17=05`.
6. **§4.7 verbatim** → identical frame for frame: f179 `$54=0C $0DBD=84`, f182 `$0DBC=02`,
   **f221 `$2603 = 3800 -> 4600` with `$1E01 = 06`**, f263 `$2400/$2500 -> 0000` and `$0DBD -> 00`,
   f264 `$0DBC -> 00`, **f313 and f355 `$1E01 = 01` (the buzz), the Tonic already gone.**
7. **§4.8 on a state I captured myself** — `PEEK_SAVE=scratchpad/menu/_av_tgtfull.state` over the same
   drive truncated to `…,100:2,0:19`; then `--read` gives `$0DBB+4 = 0C 02 84 00`,
   `$2603+8 = 4600 4600 0800 0800`, `$263F = 4600`, `$2400=BD $2500=01 $73=01 $6F=2600`; then
   ```
   --callrom C2/B870 --regdb 7E --regdp 0000 --m8 --x16 --writes
     callrom: ret ops=160 a=0000 x=2600 … C=0 Z=1
     w 7E0DBC = 00  (by C2:B873)     <- the STZ only; NEITHER INC fired
     w 7E0DBE = 00  (by C2:B881)
   ```
   byte-identical to the dossier's numbers, on an independently produced state.

**ROM data re-decoded from the image:**
* `$FF:B310 + id*2`, 242 entries, decoded with `assets/text.py`'s mapped font and diffed against
  `src/data/menu_romdata.h`'s `ctItemCat[]`: **0 mismatches over `0x00..0xF1`**. Runs:
  `00-59 -> $B4F5 WEAPON`, `5A-7A -> $B503 ARMOR`, `7B-93 -> $B4FC HELMET`, `94-BB -> $B509 ACCESSORY`,
  `BC-F1 -> $B513 ITEM`. **The label swap §2.11 reports is real and still in the tree**:
  `src/menu.c:785` reads "weapons(00-59) -> helmets(5A-7A) -> armor(7B-93)".
* `$CC:2EB1 + id*2` decoded with `load_dict` — **all 25 rows of §2.12's table reproduce character for
  character**, including `C9 'Recovers from "?"'`, `CD/CE/CF '"Power"/"Magic"/"Speed" +1'`,
  `C8 'Recovers HP/MP at Save Pts.'`, and the empty entries for `BC / D6 / 03 / 5D`.
* `$7E:7700` read out of a live `--peek .wram`: bit1 set for `BD BE BF C0 C1 C2 C3 C4 C5 C9 CA CD CE CF`
  and **clear for `C6 Heal`, `C7 Revive`, `C8 Shelter`, `CB`, `CC`**; `$7700[D6] = 0x1C` (bit3 = key
  item). Confirms both the yellow rule and §6.3's premise.

**Pixel confirmations I produced myself** (not inherited):
* `forest_exit` item list: the Tonic name is pure **`(255,255,0)`** while the class label `ITEM` is
  **`(255,255,255)`**; the description box reads **"Restores 50 HP"** (cropped and read).
* `cr.state` with the cursor 4 rows down onto the **PowerGlove** (`id 0x97`, EQUIPMENT): the description
  box reads **"Power +2"** (cropped and read), and a whole-frame diff against the SteelSaber row confines
  the change to `y 68..76, x 24..80`. **An equipment item has a description**, so `mItemDesc()`'s
  "generate it from the effect record" model is refuted on screen, exactly as §1(d) claims.

**§6.3 is a real blocker — re-verified exhaustively, not spot-checked.** I read `$2400+32`, `$2980+3`
and `$0100+2` out of **every** `.state` in `saves/` plus every candidate in `scratchpad/menu/`. The only
states with a 3-member active party (`$2980 = 00 02 04`) are `com_jogo_salvo`, `grupo_3_mapa`,
`pre_derrota`, `status_aplicado`, `g3_bare/duals/full/hurt` — and their entire inventories are
`{D6 Pendant, C6 Heal, C7 Revive}`, all with `$7700` bit1 **clear**. Every state that holds a
field-usable item (`forest_exit`: `BD`) has `$2980 = 00 80 80` = one member. `cr.state`'s 20-item
inventory is all equipment (`03 04 0F 5D 97 69 60 AD A3 8F …`, every id `< 0xBC`) with one member.
**No capture in this repo can drive `C2/B848-B864` (clamp vs wrap), the all-party scan, `C2/8AD5`, or
the live full-HP refusal.** The capture §6.3 asks for is genuinely required.

**Engine-side differential re-verified from SOURCE, not from the stale binary.** The provenance caveat is
now moot in the other direction: `src/field.c` is clean in the working tree today, and the uncommitted
`src/menu.c` hunks are at `@@ -26,0 +27`, `@@ -774,0 +776` and `@@ -1228,0 +1393` (plus
`src/menu.h @@ -151,0 +152`) — **none inside the Item block**. Reading the current tree confirms every
divergence §1(e)/§4.9 lists:
* `src/field.c:8302-8305` — an explicit comment *"NOT PORTED: the ROM's INVALID-target refusal … gated
  on `$0DBC == 0`"*: no validity mask, no buzz, no chime.
* `src/field.c:8306-8311` — leaves target-select when the count hits 0 (the ROM's `C2/B31C` only does
  `STZ $0DBD` and stays).
* `src/field.c:8299-8300` — `% partyN` **wrap**; the ROM `C2/B858-B864` **clamps**.
* `src/field.c:8362` — `if (s7[0x2400 + absSlot]) menuItemPicked = absSlot;` refuses the empty slot.
* `src/field.c:8367-8368` — no substate 5: a non-usable pick just deselects; no equipment info panel.
* `src/menu.c:593-595` — the scrollbar is two FIXED blits at `MI_LY+6` / `MH-16` gated on
  `pinnedBlinkOn` (the pinned hand's **4-on/4-off** phase), inside `if (!targetMode)` — no `$0D95` ramp,
  no `$0D96/$0D97` 8/8 blink, no `$0D9A`.
* `src/menu.c:506-517` — `mItemDesc()` is a generator capped at `id <= 0xCF`, so every equipment
  description is empty and `C3/C4/C5/C9/CA/CD` differ in wording (`ctItemFxPool[13] = 3` confirms the
  `C9 PowerMeal -> "Restores all HP/MP"` row of §2.12's table).

## AV.12 — what I did NOT verify

* **§4.6's column-run comparison.** Reproducing it means running `build/ct_field.exe`, which this pass
  was instructed not to do (and which would take the build lock). I verified the *source* claims
  instead; the row-geometry claim is untested by me and remains the dossier's own.
* **§6.2 — I found the only ROM writer of `sheet+0x3F` and it cuts both ways.** `C1/CF69` is an
  equipment-bonus walker: `LDA $CC0005,X` (a per-equipment stat-bit mask), and for each set bit, 8-bit
  `LDA $003F,X / CLC / ADC $0A / STA $003F,X`, then `+0x40`, then `+0x41`, … (`STA $003F,X` occurs at
  exactly one place in the whole 4 MB image: `C1/CF87`; `CMP $003F,X` at exactly one: `C2/B8AF`). That is
  real evidence the byte is **equipment-adjusted** — and it **sharpens the doubt**: `+0x3F` and `+0x40`
  are written as *separate 8-bit stats*, yet `C2/B8AF` reads them as one 16-bit word. In every state
  readable here `+0x40 = 0`, so the two models are still not separable. The §6.2 capture (an HP-boosting
  accessory equipped) remains required, and would now also settle whether the 16-bit read is deliberate
  or a quirk that only works because the neighbour is zero.
* **§6.5 / §6.7** (panel content, description-box geometry) and **§6.6** (`$5A`) — untouched, still open.
* **`scratchpad/menu/cr.state`'s provenance is unknown to me.** Its 20-item all-equipment inventory with
  a 1-member party does not obviously correspond to a reachable point in a normal playthrough. The ROM
  *behaviour* it drives (substate 5, `$0D13` bit2, `$0D9A`) is screen-general and I reproduced it, but if
  that state was built by poking `$2400` rather than by playing, it should be re-derived before anything
  numeric is pinned from it.

*(Files written by this pass, all under `scratchpad/menu/`: `_scan_ops.py`, `_scan_ead6.py`,
`_verif_tables.py`, `_verif_desc.py`, `_verif_cat.py`, `_av_tgtfull.state`, `_av_sb5*.raw`,
`_av_cr0/_av_cr4.raw`, `_av_fx.raw`, `_av_desc.png`, `_av_fx_crop.png`. Nothing under `src/`, `tools/`
or `build/` was touched; the build was never run.)*

## 7. PORTED 2026-09-24: the screen's own frames (tools/menu_audit_cmp.py tour f371-f720 = 0 frames differ)

MEASURED on the logical oracle with `--monitor 0054,0D17,0415,0D77,0D9A,0F00` over the tour (party3_field: the bag
holds Pendant, an empty slot, Heal): build f372 (`$54` = `$0415`, the scrollbar task's first tick), the list's first
frame f373 (`$0D17` = 2, C2/F2F3 on the Pendant = an empty line), Down f411 / f436 (the empty row, then Heal typed
from f436), Up f461, B f491 (labels, `$0D9A` = FF), R f521, A f551 -> C2/AB62's sort f552 -> C2/AE43's rows and the
hand on Use/Move f553, A f591 (`$54` = `$0415` = 1, `$0D78` = FF, `$0D9A` = 0), Down f631, B f661, B f691.

* **The scrollbar task is not the Item screen's.** C2/8BB8 lives in task slot 4 from any C2/8B93 (the Equip panel's
  C2/A45D / A463 -- f168 on the tour --, the Item build C2/AE02, the Tech build C2/B9DA, C2/A170 when `$0FC4`, the Form
  build when `$0F0C` >= 5) until a C2/8255 mask with 0x10 (the back-to-ribbon and the tab change `$74`, the list's way
  back C2/9CF7 `#10`, the open/close `FF`). `$0D96/$0D97` are only zeroed by the open's fill, so the Item screen's blink
  phase carries the 113 frames the Equip panel ran it (f168-f280). The port ticks it on that life now (menuSbTask).
* **C2/E984's deaf frame** drops A and B (the edge is lost, not deferred) on the first call after `$68` changes; the
  directions still move (C2/E9E2 latches them first). C2/AB4A, the organize, makes no C2/E984 call, so returning from
  it to the labels is NOT deaf (`$0D17` stays 01 through f551-f553).
* **The organize is three frames**: the A (`$68` := 4, the hand still on Organize), C2/AB62's sort + C2/821E (the bag
  sorted, the old rows still on screen), C2/AE43 + FBE3 with `$54` := 0A and `$68` := 1. It never touches
  `$0415/$0416`, so Use/Move goes back to the saved row -- the port used to reset both to 0.
* **`$0415/$0416` are not in C2/940D's fill** ($0C00-1DFF): the saved row and the scroll outlive the screen and the
  menu; C2/AE05 restores `$54` from `$0415` at the build.
* **The description types** (C2/F2F3 -> C2/FA49's string task, one decoded character a frame from the F2F3 frame) in
  the ROM's VWF (tone 231, lines 60-67) -- the port drew the whole line at once, in the 8x8 menu font, a tile row high.
  `$0D77/$0D78` are kept in WRAM: the ribbon's Item tab builder C2/AD50 sets both to FF (writer C2:AD61 at f341), the B
  to the labels DECs `$0D78` and F2F3s item 0, the A on Use/Move stores FF.
* **`$0D9A`**: DEC at the B to the labels (C2/AAB7), STZ at the A on Use/Move (C2/AA6C) and when target-select ends
  (C2/B1D6), 0x0C at target-select (C2/B195); non-zero draws no scrollbar (C2/8BD6).
* **The label hand** is the nav graph's own record (cell 0x0A = (40,31), 0x0B = (136,31)), as the list rows' are.

Open here: the picked-slot paths (C2/AAD5's swap, C2/AAFF's cancel with its C2/821E yield) and the info panel
(substate 5) still act in one frame; C2/FA49's task is not killed by `$74` (slot 8) -- what it types after a B to the
ribbon mid-line is unmeasured.

## 8. PORTED 2026-09-24: a Tonic used, end to end (tools/menu_audit_cmp.py itemuse f20-f345 = 0 frames differ)

MEASURED with `--monitor` (Tonics poked into slot 1, Crono 25/96): the use A f151, C2/AF42 f151-f153, the A on Crono
f171 (`$2603` 25 -> 75 at once, cmd 19 id 06), C2/B31C at f213 (`$2501` 3 -> 2), the B f271 (the list).

* **The target overlay is offset-per-tile.** C2/AFB5 writes BG3 buffer rows 0-1 from column 15 (0x6090 / 0x60AF);
  FC37 puts them in map rows 62-63, the rows BG3 V 496 (channel 5, FF/B8F4) fetches on the lines channel 2 (FF/BAD8,
  enabled by `$0D13` bit 2 on AF42's third frame) makes mode 2: 80-207. Screen columns 16-28 there show BG1/BG2 at
  (x + 144, y + 176): the window `$FF:C0A6` built at (2,32) 14 x 16 (its cells: src/data/menu_itemoverlay.h, captured
  by tools/_gen_item_overlay.py) and C2/AFED's three panels (BG1 from row 30, five rows apart: text x 136, name rows
  88 + 40k), all bright. The sprites stand at C2:B045's words (centre E4, feet 78/A0/C8). The port's old overlay was a
  hand-placed layout with the two other members dimmed.
* **C2/AF42 takes three frames**: the A (C0E4 clears BG1 rows 32-47, C2/AFB5, FC37 -- the list still on screen, the
  hand already on cell 0C's record), C2/AFED + C2/F643, then `$0D13 |= 4` -- the overlay shows.
* **The use is split.** C2/8F55: C2/8E2D changes the sheet at once, FF/F7D9 lays the digits (FF/9390's 2bpp font as
  4bpp, plane 2 = plane 0 | plane 1; FC5A -> OBJ tile 0x174), and C2/9016 floats each member's number up FF/CEBA's
  curve (41 entries: 8, 16, 22 .. 31 .. 15), tile 0x174 + digit, attr 31, the colours FF/CB9C loads into OBJ palette
  0's 4-7, the first drawn digit at the centre + C2:90F8[position]. The number is the item's amount (capped at 999)
  or the max -- C2/8E82's `$0F63`, not the HP gained. HP first (chime 06), then MP from index 2A (chime 07) in the
  frame the HP run ends. C2/8F55 waits on `$E8` (no input); the frame after the task ends, C2/B31C consumes the unit
  and C2/AFED redraws the panels -- the healed HP shows only then.
* **After the overlay's B**, C2/B0AB returns into C2/AB41: `$68` := 2 (the LIST -- the pick is gone) and task slot 6
  (the pinned hand) is killed. The port used to keep the item picked.
* **The pinned hand's blink** is C2/8B6E on `$0DA3`/`$0DA0` (4 on, 4 off), stepped only while task slot 6 lives (from
  the pick's C2/89E4), and never reset but by the open's fill. During the overlay the task keeps ticking and draws
  nothing (`$0D9C` = 0C puts its row out of the list).
* **C2/AAFF** (the Move swap and the pick's cancel) yields once: the bag is swapped at once, the rows and `$68` := 2
  come the frame after.
* **The reload after the menu recomputes the camera** (C0/7506 from `$0102/$0103`): `$1D0A` 4E before the menu, 4E
  again after C0/0905's restore (f334), 00 at f350, 44 at f367. The port kept the camera the menu found.

Open: the reload's length (menu_shell_timing.md "W is short", one frame here). (The info panel and the TAB loop: section 7.)

## 7. PORTED (2026-09-25): the equipment info panel, the list's icons, the TABS, the closing quote, the capped stars

Graded by `tools/menu_audit_cmp.py` (every frame against the logical oracle, P+2) on party3_field with the bag poked:
`iteminfo` (an Iron Blade), `itemtab` (a Power Tab on Crono), `itemtab2` (two Magic Tabs, the second target -- Lucca in party3),
`itemtab3` (a Speed Tab on a Crono whose Speed is poked to 0x10) -- all four 0 frames, and the sheets and the bag
identical at the end (checks `menu-item-info-panel-vs-oracle`, `menu-item-power-tab-vs-oracle`,
`menu-item-magic-tab-second-target-vs-oracle`, `menu-item-tab-at-cap-buzz-vs-oracle`).

* **The info panel (substate 5)** -- open item 5 above, now read in full. `C2/AE91` = `C2/AFB5` (the target overlay's own
  offset-per-tile window) + `C2/AEB6` + `C2/AF0B` + the list `$FF:C0D5` (base $3600, clear 0x400 bytes, string $22 of
  `$FF:C457` at (4, 2) with `$88` = 80). String $22 (FF:C880): "Can be used" / "by" + tile 2D; `goto` +0x188 (row 6,
  col 4) and the six-byte names `C2/AEB6` copied to $9890 -- two to a row, up to seven; `goto` +0x318 (row 12, col 12)
  `03 4D 0D 21 7E` = `$0D4D` in three digits; `goto` +0x308 (row 12, col 4) the label `C2/AF0B` copied from
  `C2:AF3A[type]` (`C2/8881`, thresholds `C2:88A8` = 5A / 7B / 94 / BC): "Attack" + 2D, "Defense" + 2D twice, eleven
  blanks for an accessory. `C2/AEB6`'s names: `$7A00[id] & $29AF` (bit 7 = character 0), each from `$2C23 + 6 c`.
  `C2/AF0B`'s value: `$7C00[id]` (0 for ids from 0x94). The text shows at x = 8 col - 144, y = 8 row - 176 (mod 256).
  A or B dismisses (`C2/AC1D`), one blip, back to the LIST, `$0D9A` = 0. Port: menu.c `CtMenuRenderItemInfo` +
  `mItemOverlayWindow` (shared with the target overlay), field.c `menuItemInfo`.
* **The list's type icon.** `C2/B05A-B061` copies the WHOLE 11-byte name record (`$CC:0B5E + 11 id`) from the row's
  first column, x 40, in the row's attr: byte 0 is the icon tile (0x20 sword .. 0x29), FF on a consumable (a blank
  tile in `$FF:8C60`). The port drew bytes 1-10 from x 48 and so had never shown a weapon's icon in the bag.
* **The TABS** (open item 3's "`C2/B228`/`C2/B275` TAB loop is still undriven" -- driven now, by poking the bag).
  `C2/B0E9`'s sixth entry `C2/B200`: `$0DBD` = 1 for CD (Power), 6 for CE (Magic), 2 for the rest (CF, Speed); then
  C2/AF42's overlay and `C2/B228`'s OWN loop. Its validity `C2/B275` (every frame): `C2/8820` copies the target's
  sheet to $9A90, `$0DBC` = 2 while `$9A9B + FF:CF2B[X]` (= sheet + 0x0B + `FF:CF2B[X]`; CF2B = 04 00 02 01 04 05 03) is
  below `FF:CF32[X]` (CF32 = 06 63 10 63 63 63 63: Power 99, Speed 16, Magic 99), else 0 -- the A then buzzes
  (`C2/B23D -> C2/EACC`). The A: `C2/B2A7` MVNs `FF/CBAC`'s four colours into the pop-up's palette ($9588; a restore's
  `C2/8F55` uses `FF/CB9C`), sets `$0F63..$0F6E` = FFFF and the target's HP word = 1, and `JSR C2/8F6C` -- the same
  `C2/9016` task as a restore: chime 06, a "1" up FF/CEBA over the target's sprite, `C2/8F6C` returning only when the
  task is over. NO sheet change on the A (no `C2/8E2D`). Then `C2/B2E0`: with M = 16, `INC $000B,X` -- the WORD at
  sheet + 0x0B + `FF:CF2B[X]` --, the byte at sheet + 0x2F + `C2:B315[X]` (60 00 02 01 03 04 05) + 1 unless already
  0x63, then `C2/8820` + `C2/9137` (the derived block and the clamped HP; no `C2/813C`); then `C2/B31C` in the same
  frame. MEASURED (itemtab): chime at f211 (the A at 210), the "1" for 41 frames, `$260B` 07 -> 08, `$262F` + 1, +0x36
  and Attack re-derived and the count 01 -> 00 all at f253. Port: menu.c `CtMenuItemTabIndex` / `CtMenuItemTabMask` /
  `CtMenuItemTabApply`, field.c `menuItemTabX` + `menuItemPopPal`. (The old port bumped the base byte only, at the A,
  with no pop-up; its offsets were an agent's guesses.)
* **The closing quote** is its own glyph, 0xE2 (the opening one is 0xE1): '"Power" +1' is `E1 .. E2 EF EC D5`. 32 of
  the 242 descriptions close a quote; the bake (tools/_gen_menudesc.py) flattened both to '"', so the VWF drew the
  opening glyph twice -- itemtab f117-f252 differed by its shape and advance. The bake now keeps 0xE2 raw.
* **The capped stars.** Menu string code 0C [cap, addr16] (`C2/F0D1`) prints the byte in two digits, or tile 2F TWICE
  once it has reached `FF:CF33[cap]` (63 10 63 63 63 63 63 63). The strings that use it: 01 (`LV`, cap 7), 04 (the
  Status detail: LEVEL cap 7 and the seven stats), 05 (the Equip stats), 09 (the Equip preview's new stats), 17 (the
  Save rows' LV). The stats' caps by field: Power +0x36 cap 0, Stamina +0x37 cap 2, Speed +0x38 cap 1, Magic +0x39
  cap 5, Hit +0x3A cap 3, Evade +0x3B cap 4, M.Def +0x3C cap 6 -- so Speed shows two stars at 16, everything else at
  99. MEASURED (itemtab3, the ribbon's Status preview at f40-f52): two stars where the port printed "16". Port: menu.c
  `mStatCap` at every site those strings draw.

* **An EMPTY slot is picked** (section 4.4 measured it; the port had refused): C2/AA99 stores the cursor in $0F00 with
  no test, so A on a hole picks it and a later A on an item swaps the item into the hole (C2/AAD5). MEASURED (the
  itemempty drive): $2400 D6 00 C6 00 -> D6 C6 00 00, every frame 0 differ (check menu-item-pick-empty-slot-vs-oracle).

* **An ALL-PARTY item's hands** (open item 3's "C2/8AD5 multi-highlight task"; driven by poking a Lapis): C2/B19B
  `LDA $0DBD / AND #08 / BEQ / JSR C2/8AD5` spawns task slot 0x30 (it replaces the pinned hand's C2/89F2) with $0DA3 /
  $0DA4 = 03 / 01, $0DA5 = 02, $0DA0 = 02. C2/8AFD each frame: TRB #FC the three counters, DEC each, a wrap toggles bit
  k of $0DA0; a hand (tile 20, attr 30) at each member whose bit is clear, at C2/8B85's (x + 2, y) -- A is 16-bit there,
  so its `DEC A / XBA` is overwritten by the `LDA $9380,X` after it -- and `STA $0D9B`. C2/E92D (every frame) hides all
  OAM and `TRB $0D9B / BNE` skips the nav hand: it is up on the task's first frame only, and after the B (the task killed
  in the handler, before it runs) the list's hand is back one frame after. MEASURED (itemall: OAM 28-31, a write watch
  on $0770): 78 63 + 7A 63 + 7A B3 on the first frame, then members 0+1+2, 0+1, 1, 1, none, 2, 0+2 ...; every member
  healed. Check menu-item-all-party-hands-vs-oracle.

Still open: string 09 also draws the preview's max HP (`$9B25`, fmt 22) in `$0D57`'s palette at `goto` 0x114 -- the
Equip preview's HP row (menu2_equip_change.md, "the max-HP preview") -- not yet drawn by the port.

## 9. PORTED (2026-09-25): the list's one-row slide, its L/R page, the blank names

The Item list is BG1, and its V scroll is HDMA channel 3 (`$210E`, table `$9180`): 40 lines at 1F7, 44 at 0A7, then ten
bands of 12 lines from line 84 at V = 1CF + 4k (the 16-px slots of the list's buffer shown 12 lines each), then 1 at 0FF
(MEASURED, `ct.exe --hdma` with HDMA_TRACE_BG=1 on tools/menu_audit_cmp.py's itemscroll). A Down / Up past the window's
edge (`C2/AC6E`, refused when `$54 + $0416 - 1 >= F0`) runs the Equip list's machinery (menu2_equip_change.md section
11) with `$9696` = 9186, `$0DAA` = 0B (ten bands + the incoming one): C2/EB9B, `INC/DEC $0416`, `$0D9E` = +-12,
`$0D95 = $0416`, the incoming row drawn at `$344A` (C2/AE65), the blip, then `C2/ACC3`'s loop -- `$0DAB += $0D22`,
`$0D9E -= $0D22`, C2/EB1F, a yield -- three frames at 3 / 6 / 9 px (MEASURED: 9 lines at 1D2, 12-line bands, 3 lines at
1FA on the first), and on the fourth `STZ $0D9E`, `STZ $0D11`, `JSR C2/AC59` (C2/AD2E's description from that frame,
`$0415`, no blip). `$0D9E` is the PICKED item's hand offset (the Move pin rides the strip); the cursor's hand stays on its
nav record. The list shows from line 88 only: channel 4's TM (`$212C`) is 12 (BG2 + OBJ) on lines 72-87 and 13 on
88-207, so the slide's first rows are cut there.

The L/R page: `C2/AC3A` runs `C2/93BF` on `$0DD9 = $0416` first (10 rows a page, `$0DDB` 0, `$0DDC` 9, range E8); a
moved page stores `$0416`, redraws the rows (C2/AE43) and branches to `C2/AC5C` -- `$0415`, a blip for `$81 = FF` --
over C2/EA53 and C2/AD2E: the page frame types no description; a clamp alone only moves the cursor to row 0 / 9.

The names: the Item and Equip rows print the ROM's record even when it is blank (ids 1B-1D); the port's "Item %02X"
fallback (for a ROM that is no longer needed: the names are baked) printed text the cartridge does not draw.

`tools/menu_audit_cmp.py itemscroll` (weapons 02-1D with a Tonic, an Ether and a Heal where the pages leave the cursor)
0 of 641 frames, `itemscrollpick` (a pick carried through two slides each way) 0 of 591 (regress
`menu-item-list-slide-and-page-vs-oracle`, `menu-item-list-slide-with-a-pick-vs-oracle`).
