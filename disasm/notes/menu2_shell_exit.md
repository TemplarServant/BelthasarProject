# The MENU SHELL: the C2/821E coroutine engine, the C2/82B2 exit, the C2/8000 open, the per-frame tick

Scope = the three recorded holes of the menu shell:
1. the "second blip ~25 frames later (C2/9C5E), after the C2/821E coroutine yield" — **we have no coroutine**;
2. the MENU-EXIT party reconcile (the bank-D1 rebuild) — we restore the frozen field verbatim;
3. `$7E0068` is "coroutine-virtualized (reads 0 on resume)" and the coroutine was never modelled.

Everything below carries either a ROM address I disassembled in this pass (`python tools/dis.py <hexoff>`)
or an oracle command I ran (pasted verbatim in §4). Where I could not derive something it is in §6.

> **Two corrections to `disasm/notes/menu_entry.md` established here, both by disassembly + measurement:**
> * the exit entry point is **C2/82B2**, not C2/82B4 (the screen table C2/8292 is 0x20 bytes and ends at
>   C2/82B1; `C2/82B2 = REP #10 / SEP #20`). There is **no** `JSR/JMP/JSL C2/82B4` anywhere in the ROM;
>   the two real callers are `C2/99D6 JMP C2/82B2` and `C2/9A95 JMP C2/82B2`.
> * **C2/8255 is not "the palette fade"** — it is `killTasks(mask)`, an 8-bit slot bitmask killer. The exit's
>   `LDA #FF / JSR C2/8255` kills all 8 coroutine slots. The fade is **C2/83CA**.
> A third, larger correction is in §2.6: the field state across the menu is preserved by a **0x14E0-byte
> block copy of `$7E:0920-1DFF` to `$7F:2000`** (C0/0918 / C0/0905), not by "reload the level + restore 3
> positions". C0/1A03's position restore is *gated behind the party-changed test* and does **not** run on a
> normal close.

---

## 1. VERDICT

The bank-C2 menu program is a **cooperative round-robin coroutine scheduler**: eight 8-byte task slots at
`$7E:00B0`, each with `[status, delay, entryPC, savedSP, saved$68]`, per-task stacks carved at
`$0A3F + slot*0x40`, and one scan of all eight slots per NMI frame (the NMI sets the tick `$7E:0D01`; the
scan clears it). A task yields with `JSR C2/821E` (**exactly 1 frame**) or `JSR C2/822B` (**A frames**);
`C2/8249` spawns; `C2/8216` self-kills; `C2/8255` kills a slot bitmask. The whole menu UI is three
long-lived tasks — slot 1 = the **screen dispatcher** (`C2/826F`: call the `($67,$68)` handler, `RTS`, yield
1 frame, repeat), slot 3 = the **OAM/cursor build** (`C2/E91B`), slot 6 = the **blink** (`C2/8AF5`) — plus
transient animation tasks. **`$7E:0068` is a task-local variable**: the scheduler zeroes it on a fresh task
start (C2/81FD) and restores it from the slot's `+6` word on every resume (C2/820D-8211), and every yield
saves it back (C2/8243-8245). Its authoritative copy for the screen machine is therefore **`$7E:00BE`**
(slot 1's `+6`), which is why a raw read of `$0068` shows 0 — you are reading whichever task last resumed.

Every "N frames later" the menu port could not explain is a **yield count**: the equip-panel entry costs
`C2/A71E`'s 24 loop yields + `C2/9C5B`'s single yield = **25 frames** between the two blips, measured
frame-for-frame on the oracle (§4.4). The menu's brightness is its own byte `$7E:0D10` (low nibble), ramped
by the coroutine fade `C2/83D0`: **+2 per yield = 7 frames in, -2 per yield = 8 frames out** (measured).

**What the C engine must do:** model the menu as a 1-frame-per-tick state machine with an explicit
*resume counter* per screen (not a coroutine — a `menuYieldLeft` countdown + a resume label is enough),
give `menuSub` a per-"task" identity so it survives the animation frames, and add the two missing shell
phases the ROM has and we do not: a **24-frame equip-entry hold with a second blip at its end**, and an
**8-frame menu fade-out before the RTL** on close, followed by the existing field fade-in. On close it must
also re-run the epilogue it already owns (`ctVmC2Epilogue` in `src/move.c`, = ROM C2/834D) and, when
`$7E:2980-82` changed inside the menu (our Form screen can do that), re-run the PC objects' init scripts
(ROM C0/1A03 → C0/597D) instead of restoring the frozen actors verbatim.

---

## 2. ROM TRACE

### 2.1 Entry: `JSL C2/8000` (`python tools/dis.py 28000 0x100`)

```
28000: BRA ->2800E      ; +0 = the MENU PROGRAM (A = mode, X = aux)
28002: BRA ->28006      ; +2 -> 28006 JSR C2/84D2 / RTL   (the joypad-read service entry)
28004: BRA ->2800A      ; +4 -> 2800A JSR C2/8C36 / RTL   (the party gate, already RE'd as op-0xD2)
2800E: SEI / CLC / XCE / PHD / PHB / PHP
28014: SEP #20 / REP #10
28018: PEA $7E7E / PLB / PLB            ; DB = $7E
2801D: STA $0A00                        ; $7E:0A00 = MODE      (measured 01 for the field X-menu)
28020: STX $0A01                        ; $7E:0A01 = aux
28023: LDA #80 / STA $002100            ; force blank
28029: LDA #01 / STA $004200            ; NMI OFF, auto-joypad ON
2802F: LDA #00 / STA $00420B / STA $00420C   ; DMA + HDMA off
28039: TSX / STX $0920                  ; ** $7E:0920 = the FIELD's stack pointer **  (measured $06F6)
2803D: LDA $0A00 / BNE ->28064          ; mode != 0 -> normal open
28042: LDA $7F0061 / BNE ->28064        ; mode 0 + first boot = the title/new-game path (C2/956E ... JMP C2/832F)
28064: LDX #09FF / TXS                  ; ** the menu's OWN stack at $7E:09FF **
28068: JSR C2/8096                      ; THE INIT CHAIN (see 2.2)
2806B: LDA $0A01 / STA $0071
28071: SEP #30
28073: STZ $0407                        ; ** the play-clock pause is released HERE ** (the field set it FF at C0/1946)
28076: LDX $0A00 / LDA $C2808F,X / STA $67   ; mode -> initial SCREEN id
2807F: LDA #01 / STA $FC / STA $FD      ; auto-repeat period + countdown seeded to 1
28085: LDA #81 / STA $004200 / CLI      ; NMI ON
2808C: JMP C2/8190                      ; the scheduler
```

`C2/808F` mode table (raw `0E 00 0C 0D 0F 07 0B`): mode 0 -> screen `$0E`, **mode 1 -> screen `$00`**
(the field X-menu), 2 -> `$0C`, 3 -> `$0D` (naming), 4 -> `$0F`, 5 -> `$07`, 6 -> `$0B`.

`C2/8292` screen -> sub-table pointer (raw, 16 words):
`00=99CA 01=9C29 02=AA34 03=B353 04=BF35 05=C621 06=CED4 07=C7A6 08/09/0A=8291(RTS) 0B=D4F0 0C=D519 0D=E1E3 0E=E60B 0F=FE0A`.

### 2.2 The INIT CHAIN `C2/8096` — every buffer the open BUILDS

```
28096: REP #30 / PEA $0000 / PLD
2809C: LDX #8406 / LDY #0500 / LDA #0007 / MVN $7E,$C2
        ; $7E:0500-0507 <- C2/8406 = "5C 0E 84 C2  5C 72 84 C2"
        ;   = JML C2/840E (the MENU NMI) and JML C2/8472 (RTI, the IRQ stub)
280A8..280EA: the five-command sound prelude (already fully derived in menu_sfx.md §2)
280EE: REP #20
280F0: JSR C2/940D        ; [A] PPU register init (DP=$2100): BGMODE $80/02, $2105=09, map/chr bases
                          ;     5A/62/6A/00/00, mode-1 layout, windows/CG-math regs cleared
280F3: $0D06/$0D0A/$0D0E := FFFF
280FF: STZ $51 / STZ $56
28103: $9B76 := FFFF; MVN fill -> $7E:9B76..9B95 = FF   (0x1E bytes)
28115: $0D5F := FFFF; MVN fill -> $7E:0D5F..0D67 = FF   (9 bytes: the roster charId list, pre-cleared)
28124: JSR C2/968D        ; [B] the GRAPHICS BUILD: two C2/9542 DMA copies from bank FF ($8E60->$7E:5000 len $0E00,
                          ;     $C9FC->$7E:5100 len $0100 = font tiles 0x30-0x3F, menu2_config_toggles.md s.11),
                          ;     a 240x16 $F0-fill of $7E:3000+, C2/9678 passes,
                          ;     MVN C2/FC6F -> $7E:0C00 (2 queue records), $0D11 := $0010, JSR C2/8663,
                          ;     MVN FF/D5E4 -> $7E:4E00 len $0600, ... (VRAM/tile/chrome image build)
28127: JSR C2/D156        ; [C] the SAVE-SLOT SCAN, at OPEN (not when screen 6 opens):
                          ;     for slot 0..2: STZ $0D49,X / JSL FF/F9C4 / valid -> INC $0D49,X + C2/D19F
                          ;     (summary) else C2/D296 ("No data"); + C2/D2C4; then queue C2/D198.
                          ;     ** $7E:0D49[0..2] = per-slot occupancy, built here **
2812A: JSR C2/984A        ; [D] OAM SHADOW CLEAR: $7E:0700 <- $E0E0 x 0x100 (512 B, Y=offscreen)
                          ;     and $7E:0900 <- $AAAA x 0x10 (32 B high table)
2812D: JSR C2/8663        ; [E] drain the VRAM queue once
28130: JSR C2/816A        ; [F] the ROSTER LISTS (see below)
28133: JSR C2/92F4        ; [G] ** THE CONFIG UNPACK ** $7E:2990/91/92 -> the 12 working bytes $0D86..$0D91
28136: JSR C2/F3CA        ; [H] 8 WINDOW/PANEL descriptor blocks at $7E:1800 + k*0x40 (k=0..6 from the
                          ;     pointer table $C2:F548, plus one from $C2:F556), each init'd by C2/F422+C2/F69D
28139: JSR C2/9875        ; [I] ** THE BIG ITEM/GATE BUILD ** (see below)
2813C..28169:             ; [J] the HP-BAR SCALE: for charId 0..6,
                          ;     $4204 <- sheet+0x3F (maxHP word); $4206 <- 10; $0D38+id*2 <- $4214 (= maxHP/10)
```

`C2/816A` [F] — the roster lists, raw:
```
2816D: SEP #30 / LDX #00 / TXY / STX $73 / STX $75
28174: LDA $2980,X / BMI skip
28179: STA $0D5F,Y      ; $7E:0D5F[] = the charIds present, packed
2817C: TXA / STA $0D68,Y; $7E:0D68[] = their $2980 slot indices
28180: INY / CPX #03 / BCS +2 / INC $73   ; $0073 = ACTIVE party count (slots 0..2)
28187: INC $75                             ; $0075 = TOTAL roster count (slots 0..8)
28189: INX / CPX #09 / BCC ->28174
```
Measured (`or_opt0.wram`, Crono+Marle): `$73=2 $75=2`, `$0D5F = 00 01 FF FF FF FF FF FF FF`,
`$0D68 = 00 01 00 ...`. `$0075 == 0` is what makes the ribbon exit the menu instantly (C2/99D2).

`C2/92F4` [G] — the config unpack, byte-exact:
```
292F7: STZ $0D86 ; MVN fill -> $0D86..$0D91 = 0
29308: LDA $2990: bit3->$0D86  bit4->$0D87  bit5->$0D88  bit6->$0D89  bit7->$0D8F  bits0-2->$0D8B
2931F: LDA $2991: bits0-2->$0D8C  bits3-5->$0D90  bit6->$0D8D  bit7->$0D8E
29341: LDA $2992: bits0-1->$0D8A
```
and the inverse **C2/934F** re-packs `$0D86..` back into `$2990/91/92`. Its ONE caller is
**C2/BF67**, i.e. *leaving the Config screen*: `C2/BF5F LDA $0F0E / ORA $0F0F / BNE ->BF52 /
C2/BF67 JSR C2/934F / C2/BF6A JMP C2/9A98`. So `$0D86..$0D91` are **derived scratch**, not persistent —
`$7E:2990-2992` is the durable config.

`C2/9875` [I] — raw (`python tools/dis.py 29875 0xB0 1 0`):
```
29878: JSR C2/9988        ; ** INVENTORY SANITISER ** for X=0..255: if $2500,X==0, or $2400,X==0,
                          ;   or id in {$5A,$7B,$94}, or id >= $F2  ->  STZ $2400,X / STZ $2500,X
2987B: STZ $0D7C / STZ $0D7E; $7F00BA bit0 -> DEC $0D7C ; bit1 clear -> DEC $0D7E
29895: STZ $0D32 / STZ $0D34 / $0D36 := FFFF
298A1: LDA $0100 / CMP #01F0 / BCS +   ; overworld skips both DECs
298A9: DEC $0D32
298AC: LDA $7F01CE / BMI + / DEC $0D34 ; ** the SAVE gate: $0D34 = FFFF => save refused **
298B5..298EB: for id = 0..$F1:  X = C2/88AD(id)
              $7E:7700+id = $CC:0000+X & $1E      ** the ITEM PROPERTY byte (bit1 = field-usable) **
              $7E:7A00+id = $CC:0003+X
              $7E:7B00+id = $CC:0004+X
              $7E:7800+id*2 = $CC:0001+X (word)
298ED..29904: for i = 0..241: $7E:7D00+i*2 = $0B5E + i*11   ** the item-NAME pointer table ($CC:0B5E stride 11) **
29906: zero-fill $7E:7ABC..7AEF (0x34) and $7E:7B94..7BEF (0x5C)
2992B..2994A: for id = 0..$BB: if ($CC:0001+X & 0x40) then $7E:7B00+id = $CC:0002+X
2994C..29967: for id = 0..$93: $7E:7C00+id = $CC:0000+X   (via C2/8881 + C2/88D8)
29969: LDA $0D34 / BEQ + / LDA #02 / TRB $77C8   ** if save is refused, item $C8 (Shelter) loses "usable" **
29973: JSL FF/F813        ; see below
29977: JSR C2/99B2        ; for i = 0..6: C2/8816 + C2/9137  = the 7 per-character DERIVED stat blocks
2997A: $9D96/$9D98 <- $2C53/$2C55   ; the GOLD display cache
```

`FF/F813` (`python tools/dis.py 3FF813 0x60`):
```
3FF816: LDX $73                      ; the char-select list length = the ACTIVE count ...
3FF818: LDA $7F0000 / CMP #49 / BCC +
3FF820: LDA $7F00BA / BIT #01 / BNE +
3FF828: LDX $75                      ; ... or the WHOLE roster once storyline >= $49 and $7F00BA bit0 clear
3FF82A: STX $85 / STZ $86            ; ** $7E:0085 = the char-select list LENGTH ** (the byte menu_sfx E3 needed)
3FF82E: LDA #80 / TSB $7709 $770C $770F $771A $771D $7721 $7724 $7727 $7729
3FF84B: JSL FF/F958                  ; the 5 combo-availability flags $04A4..$04A8
3FF863: for X = $24..0: if !($CC:2963+X & haveMask) then $7E:7F00+X = $7E:2837+X
                                     ; ** the LEARNED-TECH bitfield $7E:7F00 is derived from $7E:2837 HERE **
```
Measured `$0085 = 2` in `or_opt0.wram` with a 2-member party. `$0D38..$0D44 = 0007 0006 0007 0017 000C
0021 0041`, and the seven sheets' `+0x3F` words are `0046 0041 004B 00EB 0080 014F 028A` — quotients by 10
exactly (70/10, 65/10, 75/10, 235/10, 128/10, 335/10, 650/10). `$0D32 = $0D34 = $0D36 = FFFF` on a field
state with no save point, matching the C2/9875 gate arithmetic.

### 2.3 The SCHEDULER `C2/8190-8268` (`python tools/dis.py 28190 0x130`)

```
28190: REP #30
28192: STZ $00B0 / LDX #00B0 / LDY #00B2 / LDA #003D / MVN $7E,$7E
        ; zero $7E:00B0..$00EF = EIGHT slots x EIGHT bytes
281A1: LDA #826F / LDX #0008 / JSR C2/8249     ; slot 1 = the SCREEN DISPATCHER
281AA: LDA #3020 / STA $0058                   ; the cursor sprite's tile+attr word
281B0: LDA #E91B / LDX #0018 / JSR C2/8249     ; slot 3 = the OAM/cursor build
--- the frame loop ---
281B9: REP #10 / LDX #09FF / TXS               ; the scheduler's own stack, re-seated every frame
281BF: SEP #30 / PEA $7E7E/PLB/PLB / PEA $0000/PLD   ; DB=$7E  DP=$0000
281CA: LDA $0D01 / BEQ ->281CA                 ; ** SPIN until the NMI posts the frame tick **
281CF: LDX #00
281D1: LDY $B0,X / DEY / BMI ->281DA           ; status 0 = free -> skip
281D6: DEC $B1,X / BEQ ->281E8                 ; delay-- ; 0 -> RUN
281DA: TXA / CLC / ADC #08 / TAX / CMP #40 / BCC ->281D1
281E3: STZ $0D01 / BRA ->281B9                 ; frame consumed
--- run a slot ---
281E8: STX $6E                                 ; $7E:006E = the CURRENT slot's byte offset
281EA: LDA #03 / STA $B0,X                     ; status := 3 (RUNNING)
281EE: REP #30 / DEY / BEQ ->28206             ; original status 2 -> RESUME
        ; ---- FRESH START (status was 1) ----
281F3: TXA / ASL/ASL/ASL / ADC #0A3F / TCS     ; SP = $0A3F + slotIndex*0x40
281FB: STZ $B6,X                               ; the slot's saved-$68 := 0
281FD: STZ $68                                 ; ** $0068 := 0 **
281FF: LDA $B2,X / STA $00 / JMP ($0000)       ; enter at the entry PC
        ; ---- RESUME (status was 2) ----
28206: LDA $B4,X / TCS                         ; restore the task's SP
28209: SEP #30 / LDX $6E
2820D: LDA $B6,X / STA $68                     ; ** $0068 := the task's own copy **
28211: PLD / PLP / PLY / PLX / RTS             ; return into the yield's caller
--- primitives ---
28216 C2/8216 KILL SELF : SEP #30 / LDX $6E / STZ $B0,X / BRA ->281DA   (never returns)
2821E C2/821E YIELD 1f  : PHX/PHY/PHP/PHD / REP #20 / SEP #10 / LDA #0102 / BRA ->28236
2822B C2/822B YIELD A f : PHX/PHY/PHP/PHD / SEP #30 / XBA / LDA #02 / REP #20   ; A = (delay<<8)|02
28236: PEA $0000/PLD / LDX $6E / STA $B0,X     ; status := 2, delay := N
2823E: TSC / STA $B4,X                         ; save SP
28241: SEP #30 / LDA $68 / STA $B6,X           ; ** save $0068 **
28247: BRA ->281DA                             ; back into the slot scan
28249 C2/8249 SPAWN(A=entryPC, X=slot*8): STA $B2,X / LDA #0101 / STA $B0,X   (status 1, delay 1)
28255 C2/8255 KILL MASK(A=bitmask): for slot 0..7, if bit set -> STZ $B0,X
```
**Slot record = `[+0 status][+1 delay][+2..3 entryPC][+4..5 savedSP][+6..7 saved $0068]`.**
status: 0 free, 1 start-pending, 2 resume-pending, 3 running.
Stacks: slot *n* at `$0A3F + n*0x40` (slot0 `$0A3F` ... slot7 `$0BFF`), so `$7E:0A00-0BFF` is the task
stack arena and `$7E:0C00` (the VRAM queue) is right above it.

### 2.4 The SCREEN DISPATCHER `C2/826C-8291` (slot 1)

```
2826C: JSR C2/821E                  ; <- the loop-back point: yield ONE frame
2826F: SEP #30 / LDA #C2 / STA $18
28275: LDA $68 / ASL A / TAY        ; Y = substate*2
28279: LDA $67 / ASL A / TAX        ; X = screen*2
2827D: REP #30 / LDA $C28292,X / STA $16
28285: LDA [$16],Y / STA $00        ; the (screen,substate) handler address
28289: PER ->2826B                  ; push $826B, so the handler's RTS lands on C2/826C
2828C: SEP #20 / JMP ($0000)        ; enter the handler with A 8-bit, X/Y 16-bit
```
So the screen machine is **one handler call per frame**, and `$0068` is that task's private variable — which
is exactly why a naive read of `$7E:0068` shows whatever the *render* or *blink* task last restored (both
keep `$B6 = 0`). **Read `$7E:00BE` for the true substate.**

### 2.5 The FADE `C2/83B2 / C2/83BD / C2/83CA / C2/83D0`

```
283B2: REP #30 / LDA #83BD / LDX #0000 / JMP C2/8249    ; spawn the FADE-IN as slot 0
283BD: JSR C2/821E                    ; one idle frame
283C0: SEP #20 / LDA #02 / JSR C2/83D0
283C7: JMP C2/8216                    ; self-kill
283CA: SEP #20 / LDA #FE / BRA ->283D0 ; the FADE-OUT entry (step -2)
283D0: STA $0D20 / LDA #01 / STA $0D21 ; step, frames-per-step
283DA: LDA $0D10 / AND #0F / PHA
283E0: LDA #0F / TRB $0D10 / PLA
283E6: CLC / ADC $0D20
283EA: BEQ ->28403 / BMI ->28403       ; hit 0 or below -> done (nibble left at 0 = force blank)
283EE: CMP #0F / BCS ->283FD           ; >= $0F -> clamp
283F2: TSB $0D10 / LDA $0D21 / JSR C2/822B / BRA ->283DA
283FD: LDA #0F / TSB $0D10 / RTS
28403: JMP C2/821E                     ; one last yield, then RTS to 83CA's caller
```
`$7E:0D10` low nibble = **the menu's brightness shadow** (the NMI writes it to `$2100`, 0 -> `#80` force
blank). Fade-IN: writes 2,4,6,8,A,C,E with a yield after each (**7 frames**) then jams $0F without a yield.
Fade-OUT: writes D,B,9,7,5,3,1 (7 yields) then a final yield with the nibble cleared (**8 frames total**).
Both measured exactly (§4.1, §4.2).

### 2.6 The EXIT `C2/82B2` (`python tools/dis.py 282B4 0xC0`, entry is 2 bytes earlier)

```
282B2: REP #10 / SEP #20
282B6: LDA #FF / JSR C2/8255      ; (1) KILL ALL EIGHT TASKS (render + blink + dispatcher slots)
282BB: JSR C2/834D                ; (2) THE PARTY-GRAPHICS / TECH EPILOGUE  (2.7)
282BE: JSR C2/83CA                ; (3) FADE OUT -- 8 frames of yields on the (re-armed) current slot
282C1: LDA #80 / STA $002100      ; (4) force blank
282C7: LDA #01 / STA $004200      ;     NMI OFF, auto-joypad ON
282CD: LDA #00 / STA $00420B / STA $00420C
282D7: XBA
282D8: LDX $0920 / TXS            ; (5) ** restore the FIELD's stack pointer **
282DC: PLP / PLB / PLD / SEI / RTL ; (6) back to C0/1964
```
Note the ordering trap: the kill at (1) also clears the *current* slot, but the first `C2/822B` inside the
fade does `LDX $6E / STA $B0,X` and **re-arms it**, so the fade still runs — with every other task dead, so
the screen is a frozen image dimming for 8 frames.

`C2/8320` is the ALTERNATE hard exit (same kill/834D/83CA/blank, then `LDX #06FF / TXS / JML C0/0000`).

`C2/8255` mask census (the `LDA #imm` immediately before each `JSR C2/8255`):
`C2/82B8 = $FF` (exit), `C2/8326 = $FF` (hard exit), `C2/9A6B = $74` (ribbon tab change),
`C2/9AA4 = $74` (the shared back-to-ribbon), `C2/B1DE = $40`, `C2/B3C2 = $04`, `C2/B4BE = $40`,
`C2/C6F4 = $40`, `C2/D5F7 = $10`, `C2/D76F = $50`, `C2/E261 = $74`.
`$74` = slots 2,4,5,6 — the four transient-animation slots.

### 2.7 `C2/834D` — the exit epilogue (ALREADY PORTED, just never called on exit)

```
2834D: PHB / PHP / REP #10 / SEP #20 / PEA $7E7E / PLB / PLB
28358: LDY #21BA / LDX #0000
2835E: LDA $2980,X / BMI ->28375          ; empty slot -> skip WITHOUT advancing Y
28365: PHX / JSR C2/F5ED / MVN $7E,$D1 / PLX     ; 6 bytes of $D1:4B00 + gfxIdx*6 -> $7E:21BA + slot*0x20
2836D: REP #20 / TYA / CLC / ADC #001A / TAY
28375: INX / CPX #0003 / BCC ->2835E
2837B: JSL FF/F958                        ; the 5 combo/triple-tech availability flags $7E:04A4..04A8
2837F: JSR C2/82E1                        ; zero $7E:2857..285B, then for charId 0..6:
                                          ;   accessory (sheet+0x2A) in $AE..$B2 AND $7E:03F6+id != 0
                                          ;   -> $7E:27A9+id = $80   (i.e. $2857..$285B)
28382: PLP / PLB / RTS
```
`C2/F5ED`: `$4202 = $50 / $4203 = charId` -> `X = $4216`; `$041B = $7E:2629+X` (**the equipped WEAPON id**);
`X = $CD:6CEC + weaponId` -> gfx index; `X = $4B00 + idx*6`; returns `A = 5` (so MVN copies 6 bytes).

**This whole epilogue is already implemented in the engine** as `ctVmC2Epilogue()` /
`ctVmC2F5ED()` / `ctVmC2F958()` / `ctVmC2_82E1()` in `src/move.c` (lines ~671-745) — it was ported for the
C2/8C36 party gate. The menu exit is simply another caller.

### 2.8 The FIELD side of the exit: C0/1964 and the block copy

```
C0/1964: JSR C0/0B4E (PPU quiesce) / JSR C0/0B64 / JSR C0/0B75 (re-install the FIELD NMI/IRQ trampolines
         at $7E:0500/$7E:0504 -- the menu had overwritten them from C2/8406)
C0/196D: D := $0100
C0/1975: JSR C0/01A5     ; the reload chain
C0/1978: JSR C0/1A03     ; the PARTY RECONCILE
C0/197B: JSR C0/B192
C0/197E: JSR C0/2824     ; the field fade-IN ($0119 01..0F, 15 frames)
C0/1981: STZ $0407 / RTS ; the play clock resumes
```

`C0/0905` / `C0/0918` — **the field-state block copy** (raw bytes `A0 20 09 A2 00 20 C2 20 A9 DF 14 8B
54 00 7F AB E2 20 60` and its mirror):
```
C0/0905:  LDY #0920 / LDX #2000 / REP #20 / LDA #14DF / PHB / MVN dest=$00,src=$7F / PLB / SEP #20 / RTS
          ; ** RESTORE: 0x14E0 bytes  $7F:2000-34DF  ->  $00:0920-1DFF  (= $7E:0920-1DFF) **
C0/0918:  LDX #0920 / LDY #2000 / REP #20 / LDA #14DF / PHB / MVN dest=$7F,src=$00 / PLB / SEP #20 / RTS
          ; ** SAVE: the same 0x14E0 bytes the other way **
```
`C0/011B` (the field-state save the X-menu entry runs at C0/195B) **begins with `C0/0129 JSR C0/0918`**, and
`C0/01A5` (the reload chain) **begins with `C0/01A5 JSR C0/0905`**. So the menu preserves the entire live
field object/VM page `$7E:0920-$7E:1DFF` in `$7F:2000` — which is precisely why the compressed event script
that normally lives at `$7F:2000` has to be re-decompressed on the way back. The `$7F:1D09-1D26` block and
`$0102-0104` are saved *on top* of that (C0/012C-0197) and are the only things the reconcile re-applies.

`C0/1A03` — the party reconcile, in full (DP = `$0100`):
```
1A03: LDA $7E2980 / CMP $0194 / BNE ->1A1C
1A0B: LDA $7E2981 / CMP $0195 / BNE ->1A1C
1A13: LDA $7E2982 / CMP $0196 / BNE ->1A1C
1A1B: RTS                              ; ** party unchanged -> DO NOTHING AT ALL **
1A1C: STZ $6E
      LDA $018D / BPL: $6D := it, JSR C0/597D      ; and the same for $018E, $018F, $0191, $0190,
                                                   ;   $0192, $0193 -- IN THAT ORDER ($91 before $90)
1A5D: $0194/95/96 <- $7E:2980/81/82                ; refresh the mirror
1A6F: LDX #0000 / TDC / XBA
1A74: LDA $0197,X / BMI ->1A92 / TAY               ; the party OBJECT slot
      REP #20
      $1800,Y <- $7F1D09,X ; $1880,Y <- $7F1D0F,X ; $0C00,Y <- $7F1D15,X
      SEP #20
1A92: INX / INX / CPX #0006 / BNE ->1A74
1A99: $01AB <- $7F1D1B ; $01AC <- $7F1D1C ; $01AD <- $7F1D1D   ; the camera
1AAB: RTS
```
`C0/597D` = "run object `$6D`'s init function from the event-script image at `$7F:2001`" — the object-VM
init runner (it walks the object's fn0 opcode list via `JSR ($5D6E,X)` and finally sets `$1180,X`). That is
why the reconcile must run **after** `C0/01A5` has re-decompressed the script.

### 2.9 The per-frame MENU NMI `C2/840E` (installed via `$7E:0500`)

```
2840E: PHB/PHX/PHY/PHP / REP #30 / PEA $0000/PLB/PLB
28419: TSX / STX $1DFE / LDX #1DFD / TXS      ; swap onto the NMI's own stack
28421: PHA/PHD/PHP / SEP #20 / PEA $0D00 / PLD   ; DP = $0D00
2842A: CMP $4210                              ; ack NMI
2842D: INC $00                                ; ** $7E:0D00 = the free-running frame counter **
2842F: LDA #80 / STA $2100                    ; force blank for the transfers
28434: LDA $01 / BPL ->2843C                  ; $0D01 bit7 set = the scheduler OVERRAN -> skip the transfers
2843C: STZ $420C                              ; HDMA off
2843F: JSR C2/85D6                            ; the transfer engine (below); falls through into C2/8663
28442: LDA $13 / AND #7F / STA $420C          ; HDMA enable <- $0D13 & $7F
28449: LDA #FF / STA $01                      ; ** $7E:0D01 = the FRAME TICK the scheduler spins on **
2844D: JSR C2/8473                            ; input + clock (below)
28450: LDA #09 / STA $2105                    ; BGMODE = 9 (mode 1, BG3 priority)
28455: LDA $10 / BNE + / LDA #80              ; $0D10 -> INIDISP, 0 becomes force blank
2845B: STA $2100
2845E: LDA $0D76 / BEQ + / DEC $0D76          ; the SFX throttle drain (menu_sfx.md)
28466: PLP/PLD/PLA / LDX $1DFE / TXS / PLP/PLY/PLX/PLB / RTI
```

`C2/85D6` (the transfer engine, `dis.py 285D6 0x60 1 0`):
```
285D6: LDX #0D04 / STX $2181 / LDA #00 / STA $2183     ; WRAM read port -> $7E:0D04
285E1: LDX #210D
285E4: LDA $2180 / STA $0000,X / LDA $2180 / STA $0000,X / INX / CPX #2113 / BCC
        ; ** $7E:0D04..0D0F (12 bytes) -> the six BG scroll registers $210D-$2112, write-twice **
285F6: LDX #9680 / LDY #2123 / LDA #0F / MVN $00,$7E
        ; ** $7E:9680..968F (16 bytes) -> $2123-$2132 (window / colour-math registers) **
28604: LDX #0000 / STX $2102                            ; OAMADDR = 0
2860A: $4370 = $0400 (mode 0 -> $2104) ; $4372 = $0700 ; $4374 = $7E ; $4375 = $0220 ; $420B = $80
        ; ** OAM DMA: $7E:0700, 544 bytes, EVERY frame **
28626: LDA #80 / TRB $13 / BEQ ->28651                  ; $0D13 bit7 = "run the $7E:9690 thunk"
2862C..2864E: build a JSL trampoline at $7E:9690 ($7E54 / $6B7E) and call it with X/Y/A from
              $7E:9694/9696/9698  (the generic block-move request the equip/gear animations fill in)
28651: JSR C2/86A9                                      ; CGRAM: if $0D15 != 0 (and clear it),
                                                        ; DMA $7E:9480, 512 bytes -> $2122 from CGADD 0
28654: LDX #9E19 / LDA $7EA01E / BEQ + / LDX #9F19 / STX $4302
28663: ... falls through into C2/8663 ...
```
`C2/8663` — **the VRAM upload queue drain, run every frame**:
```
2866D: LDX $0D11 / BEQ done / STZ $0D11
28677: LDA #1801 / STA $4370                 ; DMA mode 1 -> $2118/$2119 (VMDATA)
2867D: TXA / ADC #0BF8 / TCD                 ; DP = $0C00 + tail - 8  = the LAST record
28684: $2116 <- rec+0 (VRAM word address)
28689: $4372 <- rec+2 (source address)
2868E: $4374 <- rec+4 (source bank)
28693: $4375 <- rec+5 (byte count)
28698: $420B <- $80  (channel 7)
2869B: TDC / SEC / SBC #0008 / CMP #0C00 / BCS ->28683   ; walk BACKWARD to $0C00
```
Records are pushed by **`C2/8385`** (`MVN $7E,$C2` — the 8-byte record template is read out of ROM bank
`$C2` at X) and **`C2/838E`** (`MVN $7E,$7E` — the template is already in WRAM); both do
`JSR C2/83A2` (`Y = $0C00 + $0D11`, `A = 7`), the 8-byte MVN, then `$0D11 += 8`.

`C2/8473` — the rest of the per-frame tick:
```
28476: LDA $0D18 / BEQ + / LDA #01 / STA $00FC / STA $00FD
28487: JSR C2/84EC                    ; joypad read + auto-repeat (below)
2848A: LDA $421A / STA $041F          ; joypad 2 raw
28490: JSR C2/85AE                    ; ** THE PLAY CLOCK ** (already ported: CtMenuClockTick)
28496: LDA $00F2 / AND #FFFE / BEQ ->284AB   ; nothing held (Start excluded) -> $0D19 := 1
2849E: LDA $00F4 / BEQ ->284B0 / DEC $0D19 / BPL + / STZ $0D19
284B0: SEP #30 / LDX $0D19 / LDA $C284D0,X / STA $00FC   ; ** the repeat PERIOD table: [$05, $20] **
284BB: STZ $0D18
284BD: LDA $7E2994 / ORA #01 / PHA / AND $00F2 / CMP $01,S / BNE + / STA $83
```
`C2/84EC` (the pad read): `$00F2/$00F3` = held (bit7 A, bit6 X, bit5 L, bit4 R, bit3 B, bit2 Y, bit1 Select,
bit0 Start; `$F3` bit3 Up, bit2 Down, bit1 Left, bit0 Right), `$00FE/$00FF` = the previous held,
`$00F0` = the pure edge, `$00F4` = the edge **plus synthesized repeats**:
```
28531: SEP #10 / LDX $FD                 ; the repeat countdown
28535: LDA $F2 / AND $FE / BEQ ->28540   ; not continuously held -> reload
2853B: DEX / BNE ->28542
2853E: TSB $F4                           ; countdown expired -> OR the held mask into the edge byte
28540: LDX $FC                           ; reload from the period
28542: STX $FD
```
So: **first repeat 32 frames after the press, then every 5** — table `C2/84D0 = 05 20`, measured
frame-exact in §4.6. (`C2/8545` then builds the remapped `$00F6/$00F7` from `$00F0` via the button-config
bytes `$7E:2993-299A`.)

### 2.10 The three long-lived UI tasks + the transient ones

| slot | spawn site | entry | what it is |
|---|---|---|---|
| 0 | `C2/83BA` (JMP C2/8249) | `C2/83BD` | fade-in, one-shot, `JMP C2/8216` |
| 1 | `C2/81A7` | `C2/826F` | **screen dispatcher** (`JSR C2/821E` at C2/826C every frame) |
| 2 | `C2/CBBB`/`C2/CEB1`/`C2/F9AD`/`C2/FA96` | `C2/CBBF`/`C2/CECE`/`C2/FA06`/`C2/FA9F` | per-screen helper loops |
| 3 | `C2/81B6` | `C2/E91B` | **OAM/cursor build** — `JSR C2/E92D / JSR C2/821E / BRA` |
| 4 | `C2/8BA1` | `C2/8BB8` | — |
| 5 | `C2/F652` | `C2/F657` | — |
| 6 | `C2/89EF`/`C2/8A40`/`C2/8AAD`/`C2/8ADE` | `C2/89F2`/`C2/8A45`/`C2/8AB2`/`C2/8AF5` | window animations + **the blink** |
| 7 | `C2/8FBF` | `C2/9016` | the tech-fire path (never reached in this repo) |

`C2/E92D` (slot 3's body, every frame): fill `$7E:0740..087E` with `$E0` and `$7E:0904..0917` with `$AA`
(so **OAM entries 0-15 at `$7E:0700-073F` are NOT cleared** — those are the static ribbon icons/arrows),
then `LDA #FF / TRB $0D9B / BNE skip` (an animation can suppress the cursor by INCing `$0D9B`), else
`LDA $54 / JSR C2/EA27` -> `$0770 <- $7E:9380+rec` (the cell's X,Y from the **nav graph**),
`$0772 <- $0058` (= `$3020`), and `LDA $56 / BEQ + / LDA #40 / TSB $0773` (h-flip).

`C2/8AF5` (slot 6, **the blink**): `JSR C2/8AFD / JSR C2/821E / BRA`. `C2/8AFD`:
```
28B00: LDA #3020 / STA $0776 / STA $077A / STA $077E ; three marker sprites (OAM 13,14,15)
28B0C: STA $0D9B                                     ; suppress the cursor sprite this frame
28B11: LDA #FC / TRB $0DA3 / TRB $0DA4 / TRB $0DA5   ; each counter masked to 0..3
28B1E: DEC $0DA3 / BPL + / ORA #01                   ; underflow -> toggle bit 0
28B25: DEC $0DA4 / BPL + / ORA #02
28B2C: DEC $0DA5 / BPL + / ORA #04
28B33: EOR $0DA0 / STA $0DA0                         ; ** $7E:0DA0 = the 3-bit blink mask **
28B48..28B65: for each of $73 members: if the member's blink bit is SET, skip the marker; else
              write its OAM position from the nav graph (C2/8B85: X+2, Y-1)
```
Counter cycle 3,2,1,0,-1 -> toggle: **4 frames on / 4 frames off**, seeded `$0DA5 = 2`, `$0DA0 = 2` at
`C2/8AE9-8AF2` so the three markers are out of phase. (`src/field.c`'s Item-screen
`itBlink = !((menuT >> 2) & 1)` already matches this cadence.)

### 2.11 The screen builders and their yields (the answer to "which yields do the six screens take")

Complete census of `JSR/JMP C2/821E` and `JSR C2/822B` in bank C2 (byte scan for `20 1E 82` / `4C 1E 82` /
`20 2B 82`):

```
821E JSR: 826C 83BD 89F5 8A91 8AB5 8AF8 8C30 8FC3 904C 9C5B 9E3C 9EFD A04C A0AC A0BD A511 A661 A6DC
          A785 A85F A88F A945 AAFF AB4D AD7F ADA1 AF5C AF6D B1A5 B228 B651 B705 B8F2 BE98 C337 C7D0
          C936 CA2D CA33 CBC7 CC2E CF55 D07E D0AA D7E7 D8CA E0CF E14B E906 E91E E97F F696 FAA5
821E JMP: 8403 CF5B EB98
822B JSR: 83F8 (the fade) B4F9 CECB
```
Self-looping ("`JSR C2/821E / BRA` back") = the persistent tasks: `89F2 8AB2 8AF5 A507 A6D4 B60E B712
C7CB C8F7 CBBF CC28 D7F1 D8D4 E91B E97C F657 9E06 83DA`.

The **ribbon preview builders** are dispatched by `C2/9B54` (`LDA $0411 / ASL / TAX / JSR ($9B65,X)`), table
`C2/9B65 = A051 AD50 B8DA C2FF C9E5 D065` (opt 0..5). Their yield counts, predicted from the census AND
measured on the oracle (§4.5) — they agree exactly:

| ribbon option | builder | yield sites | yields (census) | measured ($0411 change -> the C2/9A73 blip) |
|---|---|---|---|---|
| 0 Equip/Status | C2/A051 | A0AC, A0BD | 2 | **2** — the OPEN run §4.3: yields at f37 and f38, resumes f39 |
| 1 Item | C2/AD50 | AD7F, ADA1 | 2 | **2** — §4.5 f7 -> f9 |
| 2 Tech | C2/B8DA | B8F2 | 1 | **1** — §4.5 f27 -> f28 |
| 3 Config | C2/C2FF | C337 | 1 | **1** — §4.5 f47 -> f48 |
| 4 Form | C2/C9E5 | CA2D, CA33 | 2 | **2** — §4.5 f67 -> f69 |
| 5 Save | C2/D065 | D07E, D0AA | 2 | **2** — §4.5 f87 -> f89 |

The census and the measurement agree on all six. (In §4.5 the tab moves 0->1->2->3->4->5, so the row that
measures option *k*'s builder is the press that lands `$0411 = k`.)

The **back-to-ribbon builders** are `C2/9B71` -> table `C2/9B83 = A101 ADC3 B960 C397 CAE6 D0DB`.
Measured for opt 0 (`C2/A101`): **0 yields** — `$0067 -> 0` and the blip land in the same frame (§4.7).

Named animation loops, byte-exact:

* **`C2/A71E` — the EQUIP-panel entry slide. 24 yields, ALWAYS.**
  ```
  2A75C: LDA #08 / STA $4202 ; LDA #03 / SEC / SBC $0412 / STA $4203
  2A77B: LDA $4216 / STA $0D25         ; ** $0D25 = (3 - selectedPartySlot) * 8 **
  2A782: STZ $0D24
  2A785: JSR C2/821E                   ; <- the loop
  2A788: INC $0D9B                     ; hide the cursor sprite
  2A78B: LDX #FC45 / JSR C2/8385       ; queue a VRAM record
  2A791: JSR C2/A7B7                   ; $5CC2 += 6 (box A) ; $5D42 += $0DAB (box B); both MVN'd 0x16 wide
  2A794: LDA #E5 / STA $0D13
  2A799: INC $0D24
  2A79C: LDA $0D24 / CMP $0D25 / BCC ->2A785      ; phase 1
  2A7A4: LDA $0DAB / BNE + / LDA #06 / STA $0DAB  ; ** box B starts moving at rate 6 **
  2A7AE: LDA $0D24 / CMP #18 / BCC ->2A785        ; phase 2, to 24
  2A7B5: RTS
  ```
  and its caller, screen 1 substate 1 (`C2/9C39`):
  ```
  29C4C: LDA #FF / STA $84 / INC $68 / JSR C2/EAC2   ; substate := 2, BLIP #1
  29C55: JSR C2/A71E                                 ; 24 yields
  29C58: JSR C2/A321                                 ; (no yields)
  29C5B: JSR C2/821E                                 ; 1 yield
  29C5E: JSR C2/EAC2                                 ; ** BLIP #2, exactly 25 frames after BLIP #1 **
  29C61: JSR C2/8953 / JSR C2/A569 / LDX #FC1B / JMP C2/8385
  ```
* **`C2/A4AF` — the gear-list slide. 7 yields.** `$96A3` starts 0, `C2/A518` adds `$26 = 4` per pass,
  the loop `A507 JSR C2/A518 / LDA $96A3 / CMP #20 / BCS ->A516 / A511 JSR C2/821E / BRA ->A507`
  reaches $20 on the 8th pass, so 7 yields. (Unreachable in this repo — see menu_sfx.md §4.)
* **`C2/83D0`** — 7 yields in, 8 out (2.5).

### 2.12 The shared back-to-ribbon `C2/9A98`

```
29A98: SEP #20 / REP #10
29A9C: STZ $67                 ; screen := 0
29A9E: LDA #01 / STA $68       ; ** substate := 1 (the ribbon IDLE state) -- NOT 0 **
29AA2: LDA #74 / JSR C2/8255   ; kill slots 2,4,5,6
29AA7: JSR C2/9B71             ; rebuild the tab preview (table C2/9B83)
29AAA: JMP C2/EAC2             ; blip
```
Because it enters at substate **1**, `C2/99CE` (substate 0) does not re-run: no cursor reset, no fade
re-spawn. Substate 0 runs only on a fresh open (and it is what checks `$0075 == 0 -> JMP C2/82B2`).

### 2.13 The screen-build "language" `C2/ED31`

Every screen builder starts `LDX #BDxx / JSR C2/ED31`. `C2/ED31` sets `DB = $FF`, then loops: read a
command byte from `$FF:X`; bit7 set ends the list; otherwise `LDA $FF:BD18+cmd` gives a length, `MVN $7E,$FF`
copies that many parameter bytes to `$7E:005B`, and `JSR ($ED59,X)` dispatches (11 handlers:
`ED77 EF05 EDAA EF48 F332 F364 F337 F364 F378 EDB0 EDB6`). This is the declarative screen description
language: it is what writes the per-screen **nav graph at `$7E:9380`** (6 bytes/cell: `+0 X, +1 Y, +2 Up,
+3 Down, +4 Left, +5 Right` — `+0/+1` proven by `C2/E92D`'s `LDA $9380,X / STA $0770` OAM write, `+2..+5` by
menu_phase2.md). Decoding the command language is **out of scope** (§6).

---

## 3. RAM CONTRACT

| address | meaning | proof |
|---|---|---|
| `$7E:0A00` | menu MODE (1 = the field X-menu) | `C2/801D STA $0A00`; monitor `f20 7E0A00=0106*(C2:8023)` |
| `$7E:0A01` | mode aux -> `$0071` | `C2/8020 STX $0A01`, `C2/806B-806E` |
| `$7E:0920` | the FIELD's saved stack pointer | `C2/803A TSX/STX`; monitor `f20 7E0920=F606*(C2:803D)` = SP `$06F6` |
| `$7E:00B0 + n*8` | task slot n: `[status][delay][entryPC.w][savedSP.w][saved $68.w]` | `C2/8192` clear + `C2/8249/821E/822B`; monitor §4.3 shows `0101 6F82 0000 0000` -> `0300 …` -> `0201 6F82 6E0A 0000` |
| `$7E:006E` | the currently-running slot's byte offset | `C2/81E8 STX $6E`, read back by every primitive |
| `$7E:0068` | the running task's substate (**virtualized**) | zeroed `C2/81FD`, restored `C2/8211`, saved `C2/8245`; monitor `f0 7E0067=0000(C2:8211)` while `$00BE = 0100` |
| **`$7E:00BE`** | **the SCREEN machine's real substate** (slot 1 `+6`) | measured: ribbon idle `0100`; equip char-select `0100`; equip detail `0200`; item list `0200`; §4.4/§4.8 |
| `$7E:0067` | screen id | `C2/807D STA $67`, `C2/9A81`, `C2/9A9C` |
| `$7E:0D00` | free-running menu frame counter | `C2/842D INC $00` with DP `$0D00` |
| `$7E:0D01` | the frame TICK (`$FF` posted by the NMI, `STZ` by the scan) | `C2/8449 STA $01`; monitor `f36 7E0D01=FF*(C2:844D)`, `f37 =00*(C2:81E6)` |
| `$7E:0D10` | menu brightness shadow (low nibble) -> `$2100` | `C2/83F2 TSB`, `C2/8455-845B`; monitor ramps `02 04 06 08 0A 0C 0E 0F` in / `0D 0B 09 07 05 03 01 00` out |
| `$7E:0D11` | VRAM-queue tail (bytes) | `C2/83A0 STA $0D11`; monitor `f28 7E0D11=08*(C2:83A0)` … `f38 =00*(C2:8675)` |
| `$7E:0C00 + k*8` | VRAM DMA request record `[vramAddr.w][srcAddr.w][srcBank][len.w][pad]` | `C2/8663`, `C2/83A2` |
| `$7E:0D13` | HDMA-enable shadow (bit7 = "run the `$7E:9690` thunk") | `C2/8442`, `C2/8626` |
| `$7E:0D15` | CGRAM-dirty flag | `C2/86B4 TRB $0D15` -> the 512-byte CGRAM DMA |
| `$7E:0700` (544 B) | the menu's OAM shadow, DMA'd every frame | `C2/984A` build, `C2/860A-8623` DMA |
| `$7E:0740`/`$0904` | the per-frame-cleared part of the OAM shadow | `C2/E92D` |
| `$7E:0770-0773` | the cursor sprite | `C2/E95D-E977` |
| `$7E:0774/0778/077C` | the 3 blink markers | `C2/8B00-8B5A` |
| `$7E:0D9B` | "suppress the cursor sprite this frame" | `C2/8B0E STA`, `C2/A788 INC`, `C2/E958 TRB` |
| `$7E:0DA0`, `$0DA3/4/5` | blink mask + 3 phase counters (4 on / 4 off) | `C2/8AFD` |
| `$7E:0D04-0D0F` | the 6 BG scroll shadows -> `$210D-$2112` | `C2/85D6` |
| `$7E:9680-968F` | window/colour-math shadows -> `$2123-$2132` | `C2/85F9-8601` |
| `$7E:9480` (512 B) | the menu's CGRAM shadow | `C2/86BF-86D7` |
| `$7E:9380 + cell*6` | the per-screen nav graph `(x, y, up, dn, lf, rt)` | `C2/E964 LDA $9380,X` -> OAM `$0770`; menu_phase2.md R3 |
| `$7E:0073 / $0075` | active-party count / total roster count | `C2/8185/8187`; measured `2 / 2` |
| `$7E:0085` | char-select LIST LENGTH | `FF/F82A STX $85`; measured `2` |
| `$7E:0D5F[9] / $0D68[9]` | charId list / slot-index list | `C2/8179/817C`; measured `00 01 FF…` / `00 01 …` |
| `$7E:0D38 + id*2` | `maxHP/10` per charId (7) | `C2/813C-8169`; measured `0007 0006 0007 0017 000C 0021 0041` vs sheets `+0x3F` `0046 0041 004B 00EB 0080 014F 028A` |
| `$7E:0D32 / $0D34 / $0D36` | overworld flag / **save gate** / (always `FFFF` from C2/989E) | `C2/9895-98B4`; measured `FFFF FFFF FFFF` on a field state |
| `$7E:0D49[3]` | per-save-slot occupancy, built at OPEN | `C2/D16A/D17F` |
| `$7E:0D86-0D91` | the 12 **derived** config bytes | `C2/92F4` unpack from `$7E:2990-2992`; `C2/934F` repack, called only from `C2/BF67` |
| `$7E:7700+id` | item property byte (bit1 = field-usable) | `C2/98C6 STA $7700,Y`; + `FF/F82E` bit7 patches; + `C2/9970 TRB $77C8` |
| `$7E:7800/7A00/7B00/7C00/7D00` | item aux tables + the name-pointer table | `C2/98CD/98D4/98E1/995E/98F6` |
| `$7E:7F00+X` | learned-tech bitfield | `FF/F870 STA $7F00,X` from `$7E:2837+X` |
| `$7E:21BA + slot*0x20` | 3 x 6-byte weapon graphic/palette record from `$D1:4B00` | `C2/8358-8374` + `C2/F5ED` |
| `$7E:041B` | the equipped weapon id spill | `C2/F60C STA $041B`; measured `0011` |
| `$7E:04A4-04A8` | 5 combo/triple-tech availability flags | `FF/F978/F9AE` |
| `$7E:2857-285B` | accessory-derived flags | `C2/82E4` + `C2/830E STA $27A9,Y` |
| `$00:00F0/F2/F4/F6` | pad edge / held / **edge+repeat** / remapped edge | `C2/84EC`, `C2/8545` |
| `$00:00FC / $00FD` | repeat PERIOD / countdown | `C2/84B4 LDA $C284D0,X`; measured `20` idle, `05` after the first repeat |
| `$7E:0D19` | 0/1 index into the period table `[05,20]` | `C2/849E-84B2`; measured `0100` idle -> `0000` on press |
| `$7F:2000-34DF` | **the 0x14E0-byte snapshot of `$7E:0920-1DFF`** | `C0/0918` save / `C0/0905` restore; monitor `f15 7E1802=6008*(C0:0914)` |
| `$7F:1D09/0F/15/1B` | 3 PC positions + prio + camera, applied only on a party change | `C0/0149-015A`, `C0/1A7B-1AAB` |
| `$00:0194-0196` | the previous party mirror driving the reconcile | `C0/1A07/1A61` |

---

## 4. ORACLE MEASUREMENTS (verbatim)

All runs: `scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc …` from the repo root.
Button grammar is LakeSnes serial: `B=0x01 Right=0x80 A=0x100 X=0x200`.

### 4.1 The OPEN, brightness end to end

```
ct_probe.exe … --monitor "7E0067:2,7E0D10:1,7E0119:1" --from saves/field_pre_menu2.state \
  --btnscript "0:4,200:2,0:60" --diff
  f0      7E0067=0D01*  7E0D10=68*  7E0119=0F*
  f5      … 7E0119=0E*      (… 1 step/frame …)
  f19     … 7E0119=00*
  f21     … 7E0D10=00*  7E0119=00
  f22     7E0067=0000*  7E0D10=00  7E0119=00
  f41     7E0067=0000  7E0D10=02*   f42 04*  f43 06*  f44 08*  f45 0A*  f46 0C*  f47 0E*
  f48     7E0067=0000  7E0D10=0F*
```
-> field fade-out **15 frames** (`$0119` 0E..00, C0/1949), then the mode-3 shell, then the menu fade-in
**7 ramp frames + 1** (`$0D10` 02..0E then 0F).

### 4.2 The CLOSE, end to end

```
ct_probe.exe … --monitor "7E0067:2,7E0D10:1,7E0119:1,7E0100:2,7E0407:1,7E00B0:8,7E00B8:8" \
  --from saves/menu_open_gato2.state --btnscript "0:6,1:2,0:70" --diff --writer
  f0   7E0067=0000*(C2:8211) 7E0D10=0F* 7E0119=00* … 7E00B8=02016F82760A0100*(C2:8247)
  f7   7E0067=0001*(C2:8211) 7E0D10=0D*(C2:83F5) …  7E00B8=02016F82720A0100*(C2:8247)
  f8..f13  7E0D10= 0B* 09* 07* 05* 03* 01*   (all writer C2:83F5)
  f14  7E0D10=00*(C2:83E5)  … 7E00B8=02016F82740A0100*
  f15  7E0D10=68*(C0:0914)  … 7E00B8=03006F82740A0100*(C2:81EE)
  f39  7E0119=01*(C0:282B)   f40 02* … f53 0F*(C0:282B)
```
-> **B at f6; menu fade-out f7..f14 = 8 frames; f15 the RTL lands in the field (the writer flips to bank
C0); a 24-frame black reload gap f15..f38; the field fade-in f39..f53 = 15 frames.**
`$0407` stays `00` throughout — the play clock is NOT re-paused on the way out.
`$7E:00B8 = 03 00 6F 82 …` at f15 is the exit's fingerprint: slot 1 is left `status = 3` forever (the same
bytes are visible at f0 of `field_pre_menu2.state`, i.e. left over from the previous menu session).

### 4.3 The scheduler slots during the open (the whole model in one table)

```
ct_probe.exe … --monitor "7E00B0:8,7E00B8:8,7E00C0:8,7E00C8:8,7E00D0:8,7E00E0:8,7E0067:2,7E0D10:1" \
  --from saves/field_pre_menu2.state --btnscript "0:4,200:2,0:60" --diff
  f0   7E00B0=0000BD83340A0000*  7E00B8=03006F82740A0100*  7E00C8=00011BE9F60A0000*   (stale from the last menu)
  f22  ALL slots 0000000000000000*   7E0067=0000*
  f35  7E00B8=01016F8200000000*  7E00C8=01011BE900000000*                (spawn: status 1, delay 1)
  f36  7E00B8=03006F8200000000*  7E00C8=01021BE900000000*                (slot1 RUNNING)
  f37  7E00B8=02016F826E0A0000*                                          (yielded at SP $0A6E, $68 = 0)
  f38  7E00C8=02011BE9F60A0000*                                          (render task yielded, SP $0AF6)
  f39  7E00B0=0101BD8300000000*  7E00B8=02016F82760A0100*                (fade SPAWNED; $68 = 1)
  f40  7E00B0=0201BD83360A0000*                                          (fade yielded at C2/83BD)
  f41  7E00B0=0201BD83340A0000*  7E0D10=02*                              (first ramp step)
  f48  7E00B0=0000BD83340A0000*  7E0D10=0F*                              (fade task self-killed)
```
Writer attribution for the same run (`--monitor "7E0A00:2,7E0920:2,7E0067:2,7E00B0:2,7E00B8:2,7E0D11:1,7E0D01:1" --writer`):
```
  f20  7E0A00=0106*(C2:8023)  7E0920=F606*(C2:803D)
  f28/f32/f33  7E0D11=08*/20*/70*(C2:83A0)            (the init chain queueing VRAM records, NMI off)
  f35  7E0067=0000(C2:807F)  7E00B0=0000(C2:8195)  7E00B8=0101*(C2:8253)
  f36  7E0067=0000(C2:81FF)  7E00B8=0300*(C2:81EE)  7E0D11=08*(C2:83A0)  7E0D01=FF*(C2:844D)
  f37  7E00B8=0201*(C2:823E)  7E0D11=10*(C2:83A0)   7E0D01=00*(C2:81E6)
  f38  7E0D11=00*(C2:8675)
  f39  7E0067=0000(C2:8211)  7E00B0=0101*(C2:8253)
  f48  7E00B0=0000*(C2:821C)
```
Two other states, same shape and the **same constants** relative to the spawn frame:
```
--from saves/com_jogo_salvo.state : f41 7E00B8=0101*   f47 7E0D10=02*   f54 7E0D10=0F*
--from saves/ow_w0.state          : f40 7E00B8=0101*   f46 7E0D10=02*   f53 7E0D10=0F*
--from saves/field_pre_menu2.state: f35 7E00B8=0101*   f41 7E0D10=02*   f48 7E0D10=0F*
```
-> **spawn +6 = the first ramp step, spawn +13 = full brightness, on all three.** The *length of the init
chain itself* (X-press -> spawn) is cycle-emergent and varies: 31, 37, 36 frames respectively.

### 4.4 The 25-frame equip gap (hole #1), byte-exact

```
ct_probe.exe … --monitor "7E0067:1,7E00BE:2,7E0D24:1,7E0D25:1,7E0DAB:1,7E0D76:1,7E1E01:1,7E00B8:8" \
  --from saves/menu_open_gato2.state --btnscript "0:6,100:2,0:22,100:2,0:60" --diff
  f0   7E0067=00* 7E00BE=0100* …                                  (ribbon, substate 1)
  f7   7E0067=01* 7E00BE=0000* …                                  (A: screen 1, $68 reset by C2/9A83)
  f8   7E0067=01  7E00BE=0100* … 7E0D76=03* 7E1E01=00*            (C2/9C31 blip; substate 1)
  f9..f11  7E0D76=02* 01* 00*                                     (the NMI drain, 1/frame)
  f31  7E00BE=0200*  7E0D25=18*  7E0D76=03*  7E00B8=…710A0200*    (** BLIP #1 at C2/9C52; $0D25 = 24 **)
  f32..f55  7E0D24 = 01* 02* … 18*                                (** 24 slide frames **)
  f55  7E0D24=18*  7E0DAB=06*  7E00B8=…740A0200*
  f56  7E0D76=03*  7E00B8=…760A0200*                              (** BLIP #2 at C2/9C5E = f31 + 25 **)
```
The same run with the cursor moved to party slot 1 first (`--btnscript "0:6,100:2,0:16,20:2,0:16,100:2,0:45"`):
```
  f43  7E0412=01  7E00BE=0200*  7E0D25=10*  7E0D76=03*      ($0D25 = (3-1)*8 = 16)
  f44..f59  7E0D24 = 01* … 10*
  f59  7E0D24=10*  7E0DAB=06*                              (** box B starts at exactly $0D24 == $0D25 **)
  f60..f67  7E0D24 = 11* … 18*
  f68  7E0D76=03*                                          (** BLIP #2 = f43 + 25 -- STILL 25 **)
```
-> the slide is **always 24 frames**; only the phase split moves.

### 4.5 The ribbon tab change (kill mask + preview builder)

```
ct_probe.exe … --monitor "7E0411:1,7E0054:1,7E00BC:4,7E0D76:1" --from saves/menu_open_gato2.state \
  --btnscript "0:6,80:2,0:18,80:2,0:18,80:2,0:18,80:2,0:18,80:2,0:25" --diff
  f0   7E0411=00* 7E0054=00* 7E00BC=760A0100*
  f7   7E0411=01* 7E0054=01* 7E00BC=6E0A0100*   ; deep yield inside the builder
  f9   7E00BC=760A0100* 7E0D76=03*              ; the C2/9A73 blip -> 2 yields for opt 1
  f27 -> f28 (opt 2, 1 yield) ; f47 -> f48 (opt 3, 1) ; f67 -> f69 (opt 4, 2) ; f87 -> f89 (opt 5, 2)
```

### 4.6 The auto-repeat (32 then 5)

```
ct_probe.exe … --monitor "7E00FC:1,7E00FD:1,7E0D19:2,7E0411:1" --from saves/menu_open_gato2.state \
  --btnscript "0:6,80:60" --diff
  f0   7E00FC=20*  7E00FD=20*  7E0D19=0100*  7E0411=00*
  f7   7E00FC=05*  7E00FD=20   7E0D19=0000*  7E0411=01*
  f8..f38  7E00FD = 1F* 1E* … 01*
  f39  7E00FD=05*  7E0411=02*                        (** first repeat = press + 32 **)
  f40..f42 7E00FD = 04* 03* 02*                      (** then every 5 **)
```

### 4.7 Sub-screen enter / back costs

```
ct_probe.exe … --monitor "7E0067:1,7E00BE:2,7E00BC:2,7E0D76:1,7E0411:1" --from saves/menu_open_gato2.state \
  --btnscript "0:6,80:2,0:20,100:2,0:30,1:2,0:30" --diff
  f29  7E0067=02*  7E00BE=0000*             ; A -> Item, $68 cleared
  f30  7E0067=02   7E00BE=0200*  7E0D76=03* ; C2/AA40 sets $68 = 2 and blips -- ** 1 frame, no yield **
  f61  7E00BE=0100*  7E0D76=03*             ; B on the list -> substate 1 (the Use/Move label row)

ct_probe.exe … --monitor "7E0067:1,7E00BE:2,7E00BC:2,7E0D76:1" --from saves/menu_open_gato2.state \
  --btnscript "0:6,100:2,0:25,1:2,0:30" --diff
  f7   7E0067=01*  7E00BE=0000*
  f8   7E0067=01   7E00BE=0100*  7E0D76=03*
  f34  7E0067=00*  7E0D76=03*               ; B -> ribbon: screen AND blip in ONE frame => 0 yields (C2/A101)
```

### 4.8 The exit epilogue is a no-op when nothing changed; the block copy is what restores the field

```
ct_probe.exe … --monitor "7E21BA:6,7E04A4:5,7E2857:5,7E041B:2,7E0D49:3" --from saves/menu_open_gato2.state \
  --btnscript "0:6,1:2,0:40" --diff --writer
  f0   7E21BA=9D233B0EB409*  7E04A4=0000000000*  7E2857=0000000000*  7E041B=1100*  7E0D49=000000*
  (no further rows -- C2/834D recomputes the same values)

ct_probe.exe … --monitor "7E1802:2,7E1882:2,7E1804:2,7E1884:2" --from saves/menu_open_gato2.state \
  --btnscript "0:6,1:2,0:45" --diff --writer
  f0   7E1802=0100*  7E1882=0101*  7E1804=C072*  7E1884=406B*      (menu debris in the object page)
  f15  7E1802=6008*(C0:0914)  7E1882=4F0A*(C0:0914)  7E1804=9008*(C0:0914)  7E1884=4F0A*(C0:0914)
```
`C0:0914` is the `MVN` inside **C0/0905** — the 0x14E0-byte restore, not C0/1A7F. The saved block matches:
```
ct_probe.exe … --read "wram:7F1D09+24" --from saves/menu_open_gato2.state
  wram[7F1D09+24] = 60089008000A4F0A4F0A0000202020200000685900000000
ct_probe.exe … --read "wram:7E0197+6" --from saves/menu_open_gato2.state
  wram[7E0197+6] = 020004008000
```
i.e. PC0 `X=$0860 Y=$0A4F`, PC1 `X=$0890 Y=$0A4F`, prio `20 20 20 20`, camera `68 59 00`, party object
slots `$0197 = 02 / $0199 = 04 / $019B = 80`.

### 4.9 The open-built buffers, read out of an oracle menu WRAM (`scratchpad/menu/or_opt0.wram`)

```
char 0 +3F..+41 = 46 00 FF  lvl+12=1      char 4 +3F..+41 = 80 00 FF  lvl+12=5
char 1 +3F..+41 = 41 00 FF  lvl+12=1      char 5 +3F..+41 = 4F 01 FF  lvl+12=18
char 2 +3F..+41 = 4B 00 FF  lvl+12=2      char 6 +3F..+41 = 8A 02 FF  lvl+12=37
char 3 +3F..+41 = EB 00 FF  lvl+12=10
0D36..0D46: FF FF 07 00 06 00 07 00 17 00 0C 00 21 00 41 00
0D32..0D36: FF FF FF FF
0073,0075,0085: 2 2 2
0D5F..0D70: 00 01 FF FF FF FF FF FF FF 00 01 00 00 00 00 00 00
```

---

## 5. IMPLEMENTATION SPEC (for `src/field.c` + `src/menu.c`)

The engine does **not** need a real coroutine. Every yield in the six screens is either (a) a fixed-length
animation loop with a counter the ROM keeps in RAM, or (b) a 1-frame gap. Model it as an explicit
*pending-resume* record on the menu state machine.

### 5.1 New state (next to `menuPhase/menuScreen/menuStatSub` at `src/field.c:7058-7073`)

```c
int menuSubBE;        // $7E:00BE -- the DISPATCHER's substate. menuScreen/menuStatSub already carry this
                      //   per screen; keep them, but STOP treating $7E0068 as readable state.
int menuYield;        // frames still owed by the current handler (0 = the handler may run this frame)
int menuResume;       // which continuation to take when menuYield hits 0 (a small enum per screen)
int menuSlide;        // $7E:0D24  -- the equip-entry frame counter 0..24
int menuSlideSplit;   // $7E:0D25  = (3 - menuStatSlot) * 8
int menuSlideRate2;   // $7E:0DAB  -- 0 until menuSlide == menuSlideSplit, then 6
int menuBri;          // $7E:0D10 low nibble, 0..15 (the MENU's own brightness -- distinct from the field's)
int menuBriStep;      // $7E:0D20  +2 fade in / -2 fade out
int menuRepeatPeriod; // $00FC  (32 idle, 5 while repeating)
int menuRepeatCount;  // $00FD
int menuRepeatIdx;    // $0D19   0/1 -> period table {5, 32}
int menuBlinkMask;    // $0DA0, and $0DA3/4/5 phase counters
```

### 5.2 The per-frame tick, in ROM order (replaces the ad-hoc `fieldMenuSfxTick` call site)

Every menu frame, before the screen handler:
1. `menuFrame++` (`$0D00`).
2. `fieldMenuSfxTick(s7)` — the `$0D76` drain (already present; it is `C2/845E`, and it belongs here).
3. `CtMenuClockTick(s7)` — `C2/85AE` (already present).
4. **auto-repeat** (new): for each direction key,
   `if (heldNow && heldPrev) { if (--menuRepeatCount == 0) { edge |= held; menuRepeatCount = menuRepeatPeriod; } }
    else menuRepeatCount = menuRepeatPeriod;`
   then `menuRepeatIdx = (nothingHeldExceptStart) ? 1 : (edge ? 0 : menuRepeatIdx);`
   `menuRepeatPeriod = (menuRepeatIdx ? 32 : 5);`
   Order matters: the countdown uses the PREVIOUS frame's period, which is what makes the first repeat 32.
5. **blink** (new, `C2/8AFD`): three counters, each `c = (c - 1) & 3`; on the 3->…->0->wrap edge toggle that
   member's bit in `menuBlinkMask`; seed `c2 = 2`, `mask = 2`. Cadence = 4 on / 4 off.
6. brightness: if a fade is active, `menuBri += menuBriStep`; `<=0` -> 0 and the fade ends; `>=15` -> 15 and
   the fade ends **without spending a frame**. Scale the composed frame by `menuBri/15`.

### 5.3 The yield model

`CtMenuYield(n)` = "this handler owes `n` frames; do not run it again until they are spent". A screen
handler becomes a switch on `menuResume`. Only these continuations exist for the six screens (§2.11):

| screen | where | yields | continuation |
|---|---|---|---|
| ribbon open (substate 0) | `C2/9B54` -> preview builder | **2** for opt 0/1/4/5, **1** for opt 2/3 | then `menuSub = 1`, then spawn the fade-in |
| fade-in task | `C2/83BD` | 1 + 7 | brightness 0 -> 2,4,…,14 -> 15 |
| ribbon tab change | `C2/9A70 JSR C2/9B54` | same per-option counts | then the tab blip (`menuSfx(id 0)`) |
| back to ribbon | `C2/9AA7 JSR C2/9B71` | **0** for opt 0 (measured); others not measured | then the blip |
| Equip/Status enter (A on a member) | `C2/9C55 JSR C2/A71E` + `C2/9C5B` | **24 + 1** | then the SECOND blip (`menuSfx(id 0)`) |
| Equip gear list open | `C2/A4AF` | 7 | then the tail blip `C2/A45A` |
| Item / Tech / Config / Form / Save enter | `C2/AA40`, `C2/B35D`, `C2/BF3D`, `C2/C627`, `C2/CEDC` | **0** | the entry blip in the same frame |
| menu close | `C2/83CA` | **8** | then hand back to the field |

### 5.4 Equip entry (hole #1) — the concrete change

`src/field.c` currently does `menuStatSub = 1` and sets `menuEquipRise` ad hoc. Replace with the ROM's
counter:
```
on A in screen 1/3 char-select:
    menuStatSub = 1;                       // C2/9C50 INC $68 -> substate 2
    menuSfx(s7, 0x00, "equip-enter");      // C2/9C52  BLIP #1
    menuSlide = 0;
    menuSlideSplit = (3 - menuStatSlot) * 8;   // C2/A762-A77F, $0D25
    menuSlideRate2 = 0;                        // $0DAB
    menuYield = 25;                            // 24 (C2/A785 loop) + 1 (C2/9C5B)
each menu frame while menuSlide < 24:
    menuSlide++;                               // C2/A799 INC $0D24
    if (menuSlide == menuSlideSplit) menuSlideRate2 = 6;   // C2/A7A9
    boxA += 6;  boxB += menuSlideRate2;        // C2/A7BE / C2/A7DA
    // cursor sprite hidden this frame ($0D9B, C2/A788)
when menuYield reaches 0:
    menuSfx(s7, 0x00, "equip-settled");    // C2/9C5E  BLIP #2, exactly 25 frames after BLIP #1
```
`CtMenuRenderEquipSliding(..., riseOff, selSlot)` already takes the two-rate parameters; drive `riseOff`
from `menuSlide`/`menuSlideRate2` instead of the current invented ramp, and keep the settled render for
`menuSlide == 24`.

### 5.5 The CLOSE (hole #2) — what `menuPhase 2 -> 3` must become

Today: `B` -> `menuPhase = 3` -> a 15-frame field fade-in. The ROM's close is four steps; insert the first
three:

1. **kill the transient tasks** — in our terms: cancel any in-flight animation (`menuSlide`, the fade task),
   because `C2/82B6 LDA #FF / JSR C2/8255` kills everything before the exit fade.
2. **the epilogue `C2/834D`** — call the already-ported `ctVmC2Epilogue()` (`src/move.c:~737`; it is
   `static`, so expose it, e.g. `CtVmMenuExitEpilogue(uint8_t* mem7E, const uint8_t* rom, long romLen)`).
   It rewrites `$7E:21BA/21DA/21FA` (weapon gfx rows from `$D1:4B00`), `$7E:041B`, `$7E:04A4-04A8`
   (FF/F958) and `$7E:2857-285B` (C2/82E1). On every state this repo can drive it recomputes identical
   values (§4.8) — but our menu **can** change `$7E:2980-82` (`CtMenuFormSwap`), and a future equip-change
   will move `sheet+0x29`, and then it is not a no-op. Call it unconditionally, like the ROM.
3. **the 8-frame menu fade-out** (`C2/83CA`): `menuBri` 15 -> 13,11,9,7,5,3,1 (one frame each) then a final
   frame at 0. Render the LAST composed menu frame scaled by `menuBri/15` — the ROM's render task is dead,
   so the image is frozen while it dims. Only after those 8 frames do we enter the existing `menuPhase 3`.
4. **the party reconcile** (`C0/1A03`), replacing the current "restore the frozen actors verbatim":
   ```
   if (s7[0x2980] != prevParty[0] || s7[0x2981] != prevParty[1] || s7[0x2982] != prevParty[2]) {
       for (k in {0x18D,0x18E,0x18F,0x191,0x190,0x192,0x193})       // C0/1A1E-1A5A, note 0x191 BEFORE 0x190
           if (!(s7[k] & 0x80)) CtSceneRunObjectInit(f.vm, s7[k]);  // C0/597D
       prevParty[0..2] = s7[0x2980..82];                            // C0/1A5D
       for (i = 0; i < 3; i++)                                      // C0/1A6F-1A97
           if (!(s7[0x197 + i*2] & 0x80)) { restore $1800/$1880/$0C00 from mSave for that slot; }
       restore the camera from $7F1D1B-1D;                          // C0/1A99
   }
   // else: NOTHING -- the ROM RTS's at C0/1A1B; the positions are already back from the block copy
   ```
   Our `mSave` block is the exact analogue of `$7F:1D09-1D26`, and the "restore the frozen field verbatim"
   is the exact analogue of `C0/0905`'s 0x14E0-byte copy — so the existing code is the *unchanged-party*
   branch of the ROM and is correct. The missing half is the changed-party branch.
   `prevParty[]` is `$00:0194-0196`; it must persist across menus, not be recomputed at open.

   **The order is load-chain first, reconcile second** (`C0/1975` then `C0/1978`) because `C0/597D` reads
   the object's init script out of `$7F:2001`, which the load chain has just re-decompressed.

### 5.6 The OPEN (hole #3 / gap (c)) — buffers we may be skipping

At `menuPhase 1 -> 2` (where `menuShellPrelude` already runs), add, in `C2/8096` order:

| ROM | do we have it? | action |
|---|---|---|
| `C2/D156` save-slot scan -> `$0D49[3]` | we call `CtMenuSaveSlot` lazily per render | build `$7E:0D49[0..2]` ONCE at open; the Save screen's A-handler branches on it (`C2/CF0A LDA $0D49,X`) |
| `C2/816A` roster lists -> `$0073/$0075/$0D5F[]/$0D68[]` | recomputed ad hoc in several places | build them once at open; **`$0075 == 0` must close the menu immediately** (`C2/99D2`) |
| `FF/F813` -> `$0085` (char-select list length), `$7E:7F00` techs | `CtMenuTechList` recomputes `$7F00` per render | also set `$0085`; the char-select Up/Down wrap uses it (`C2/9D2A CMP $85`), and it is the roster length only once storyline >= `$49` |
| `C2/92F4` config unpack `$2990-92 -> $0D86..$0D91` | **MISSING** — `menu.h` calls `$0D86` "persistent" | unpack at open; re-pack with `C2/934F` when leaving the Config screen. Today a Config toggle would be lost/garbage. |
| `C2/9875` item tables `$7700/$7800/$7A00/$7B00/$7C00/$7D00` | `CtMenuItemUsable` reads `$7700` | build them at open from `$CC:0000+`; **and apply `C2/9970 TRB $77C8`** (Shelter loses bit1 when `$0D34 != 0`) |
| `C2/9988` inventory sanitiser | **MISSING** | at open, zero any `$2400/$2500` pair with count 0, id 0, id in {$5A,$7B,$94}, or id >= $F2 |
| `C2/99B2` 7 derived stat blocks | partly (`battle` side) | rebuild at open |
| `C2/813C` `$0D38+id*2 = maxHP/10` | **MISSING** | trivial; it is the HP-gauge scale |
| `C2/9875` `$0D32/$0D34/$0D36` | `CtMenuSaveCanOpen` recomputes | set them at open, and note `$0D36` is `FFFF` **always** from `C2/989E` (only `C2/C7AE` writes it otherwise) — so `CtMenuFormRoster`'s "reserve scanned only when `$0D36 == 0`" can never fire in the field menu |
| `C2/984A` OAM shadow / `C2/940D` PPU init / `C2/968D` VRAM build / `C2/F3CA` window objects | N/A — we compose a framebuffer | no action; recorded for completeness |

Also: the ROM releases the clock pause at **`C2/8073`**, i.e. at the END of the init chain and before the
scheduler's first tick — `field.c:8005`'s "cleared when the screen appears" is the right instant.

### 5.7 Timings the engine should match

| event | ROM frames | source |
|---|---|---|
| X press -> screen black | 15 (field `$0119` 0F->00) | §4.1, already ported |
| black -> the menu's first ramp step | init chain (cycle-emergent, 31-37 measured) + 6 | §4.3 |
| menu fade-in | 7 ramp frames, full on the 8th | §4.1 |
| ribbon tab change -> blip | 1-2 (the preview builder's yields) | §4.5 |
| A -> a sub-screen visible | 1 (Item/Tech/Config/Form/Save) | §4.7 |
| A on a party member -> the equip panel settled + 2nd blip | **25** | §4.4 |
| B -> menu black | **8** | §4.2 |
| menu black -> the field's fade-in starts | 24 (the reload chain; cycle-emergent) | §4.2 |
| field fade-in | 15 | §4.2, already ported |
| d-pad auto-repeat | first at +32, then every 5 | §4.6 |
| party-marker blink | 4 on / 4 off | §2.10 |

---

## 6. OPEN / BLOCKED

**Not derived (would need more ROM work, not a capture):**
* **The `C2/ED31` screen-build command language.** I established the interpreter (DB=`$FF`, per-command
  length table `$FF:BD18+cmd`, parameters copied to `$7E:005B`, 11 handlers at `C2/ED59 = ED77 EF05 EDAA
  EF48 F332 F364 F337 F364 F378 EDB0 EDB6`) and that the command lists live in **bank `$FF`** (`LDX #BD51`,
  `#BD74`, `#BE26`, `#BE6D`, `#BEA4`, `#BD2E`, `#BDA4`…). The opcodes themselves are NOT decoded. This is
  what writes the per-screen nav graph at `$7E:9380` and most window content.
* **Which code READS `$7E:21BA`.** The only 16-bit reference to `$21BA` in the whole 4 MB is the
  `LDY #21BA` inside `C2/834D` itself (byte scan for `BA 21`: 24 hits, one in bank C2). It must be reached
  through a base+index or a DMA table I did not find, so I cannot state the *visual* consequence of the
  rebuild — only its RAM effect, which is measured and must be reproduced.
* **Yield counts for the `C2/9B83` back-to-ribbon builders of options 1-5** (`ADC3 B960 C397 CAE6 D0DB`).
  Only opt 0 (`C2/A101`, 0 yields) was measured. The site census cannot attribute them safely because the
  address ranges interleave with the animation routines.
* **Owner attribution for ~30 of the 56 `C2/821E` sites.** The census in §2.11 is complete and exact; the
  routine each site belongs to was only established for the ones named there.
* **`C2/85D6`'s `$0D13` bit-7 thunk** (the `$7E:9690` JSL trampoline built from `$7E:9694/9696/9698`): I
  identified the mechanism but not what the animations put in those three words.
* **`$7E:0D18`, `$7E:0D9C`, `$7E:0DA6`, `$7E:0056`, `$7E:0058`'s h-flip source `$0056`** — read/written in
  the tick and the render task, semantics not pinned.
* **`C2/84D2`** (the `JSL C2/8002` service entry) — it wraps `C2/84EC` + `C2/85AE`, i.e. a "read the pad and
  tick the clock" service for a non-menu caller; the caller set was not enumerated.
* **`$0110`**, the third X-menu gate byte, is still unlabeled (carried from menu_entry.md).

**Blocked on a capture (say so, do not reason it out):**
* **The exit epilogue with a party that actually changed.** `C2/834D`'s output is identical before and
  after on every state this repo owns (§4.8), because the Form screen's gate (`$0F0B + $0F0C >= 2`) refuses
  on all of them and no state has an equippable spare weapon (menu_sfx.md §4 proved `$0FC6 == 0` across all
  104 `.state` files). **Unblocked by:** a savestate with >= 4 recruited characters *or* one whose
  `$7E:2400` inventory contains an item id in `0x01..0xBB` for the lead's weapon slot. With that, the
  decisive run is
  `--monitor "7E21BA:6,7E21DA:6,7E04A4:5,7E2857:5,7E041B:2,7E2980:3" --btnscript "<swap>,<B>" --diff --writer`
  and a `--peek` of the field one frame after the RTL to see whether the PC sprite/palette actually moved.
* **The changed-party branch of `C0/1A03` (C0/597D respawn).** Same blocker: no repo state can change
  `$7E:2980-82` inside the ROM's menu. The branch is fully disassembled (§2.8) but never executed on the
  oracle here. **Unblocked by:** the same >=4-character state, driving a Form swap and monitoring
  `"7E1800:16,7E1180:16,00194:3"` with `--writer` across the close.
* **Menu modes 0/2/4/5/6** (screens `$0E/$0C/$0F/$07/$0B`) — the mode table is read out of the ROM, but only
  mode 1 is reachable from the states we have.
* **The `$0D13` bit-7 thunk and the gear-list slide (`C2/A4AF`, 7 yields)** are disassembly-only for the
  same reason the gear list is unreachable.

**Flagged discrepancies with existing repo notes (not resolved here):**
* `menu.h` calls `$7E:0D86..$0D8F` "12 live config bytes, persistent". They are **derived at every menu
  open** from `$7E:2990-2992` (`C2/92F4`) and packed back only when Config is left (`C2/934F` @ `C2/BF67`).
* `menu.h`'s `CtMenuFormRoster` gates the reserve scan on `$0D36 == 0`; `C2/989E` sets `$0D36 = FFFF` on
  every open and the only other writer is `C2/C7AE` (screen `$07`), so in the field menu the reserve count
  is structurally 0.
* `menu_entry.md`'s "C2/8255 (palette fade)" and "C2/82B4" are wrong (see the banner); its "restore 3
  positions + camera" is the *changed-party* branch only.

## The gear panel's entry slide (C2/A71E + C2/A7B7)

Nothing is moved as a picture. On A over a member, C2/A71E builds the gear panel's left column off-screen ($FF:BE26
through C2/ED31: BG1 rows 32-63 cleared and string 7 "Weapon" at (5,42); BG2 rows 22-25 backed up to 54-57, the label
window at (2,40) 12x4, the list window at (2,44) 12x14 -- the same cells the built panel's rows 8-25 hold) and uploads
it (FBEA, FC06). BG mode 2's offset-per-tile then brings it in: screen column c (c >= 1) takes the V scroll of both BG1
and BG2 from entry c-1 of the BG3 map row its line's BG3 V scroll points at (bits 13/14 = BG1/BG2, value & 0x3FF), and
$5CC2/$5D42 are entries 1-12 (screen columns 2-13) of rows 59 and 61.

Each of the 24 steps (one per frame; FC45 uploads rows 58-61) C2/A7B7 adds 6 to $5CC2 (from 0x606F) and $0DAB to
$5D42 (0x61FF, the lines' own map line), and rewrites the channel-6 BG3-V table at $969A (C2/AA19, entries from line
16), which the NMI's thunk $9690 copies to $9303 in the same vblank ($0D13 = E5, bit 7):

```
$22 lines at 0x1E0 (row 61: the party column)     $24 lines at 0x1F0 (row 63, never written: the TIME box)
$26 lines at 0x1D0 (row 59: the new column)        16 lines at 0x1F0
```

with $22 = 0x90, $24 = 0x30, $26 = 0 at the start and, each step, $26 += 6 while $24 (then $22) gives 6 back. After
step k the new column fills lines 208 - 6k .. 207 and shows map line 320 (row 40, the label window's top) at its first
line: it rises 6 px a frame, rigid, over the TIME box and then the lower panels. After $0D25 = (3 - $0412) * 8 steps
$0DAB becomes 6 and the party column scrolls up with it, so the picked member's box ends at the top and the boxes above
it go under line 16 (lines 0-15 are mode 1, no offsets). The cursor is off for the whole loop (INC $0D9B each step, the
hand's one-shot suppress C2/E956). Step 24's frame is also C2/A321's (the built panel).

Oracle reading (tour, logical oracle): $5CC2 = 606F on the A frame, 6075 .. 60FF over the next 24; at step 9 row 59 =
0000 60A5 x12 0000.. and row 61 = 0000 61FF x12 0000.., the table 11+127 lines 0x1E0, 54 lines 0x1D0, 16 lines 0x1F0.

## The gear panel's way back (C2/A85C)

The entry slide's machine run the other way. B (or Left) on the gear select yields at once (the picture stands). The
next frame builds in the buffers only ($FF:BE5B: BG2's member box at (2,2) block-copied row by row over rows 8-25,
which C2/EF05's top-down copy turns into four copies of it; BG1 rows 9-10 cleared; C2/A1B2 writes the three member
panels; FBEA). The frame after is the setup, and it shows: OPT rows 61-62 cleared, C2/9E76 (the member boxes toned for
$0412, and its record FBFF uploads BG2 rows 2-27), the channel-6 table C2:AA06 (16 lines FFFF, 48 of 0x1E0, 144 of
0x1D0, 16 of 0x1F0), $5CC2 = 0x60FF (the gear column where it stands, BG1 and BG2 now), $5D42 = 0x6000 |
(48 * $0412 - 1) (the party column scrolled so that $0412's box is the top one), FC45, FBE3, and a 16-bit DEC $0D9A
that also sets $0D9B -- the hand's one-shot suppress, so the hand is gone on this frame already.

Then 24 steps (C2/A970): $5CC2 -= 6, $5D42 -= $0DAB (6 for $0D25 = 8 * $0412 steps, 0 for the lead), and the table
rewritten as 16 FFFF, $22 of 0x1E0, 0xC0 - $22 of 0x1D0, 16 of 0x1F0 with $22 = 0x30 + 6 per step: the gear column
sinks 6 px a frame, cut at line 207, while the party column (rows 2-25: the four boxes, BG1's panels and the member
select's TIME/gold, never cleared) opens above it. The right side keeps the gear panel's detail. The 24th step is also
C2/A2CD's rebuild.

Oracle reading (tour, logical oracle): $5CC2 = 60FF on the setup frame, 60F9 .. 606F over the next 24; OAM $0770
parked (Y E0) from the setup frame on.
