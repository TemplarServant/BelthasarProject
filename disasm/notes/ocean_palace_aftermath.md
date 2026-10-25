# The Ocean Palace's aftermath: Lavos rising to the party's new leader (2026-10-14)

The user's own snes9x capture before the Mammon Machine scene, spliced into a lake state:
`saves/field_re/pre_scene_19f.state` (+ `.lakewram`), loc 19F, storyline CB. Repro, both sides:

    CT_ORACLE_LOGICAL=1 build/ct.exe Chrono_Trigger_USA_SNES.sfc --monitor 7E0100:2,7F0000:1 \
        --from saves/field_re/pre_scene_19f.state --input "0:6000,100:2,0:38,..." --diff
    CT_NOPACE=1 CT_AUDIO_LOCKSTEP=1 CT_AUDIO_SEEDSTATE=saves/field_re/pre_scene_19f.state CT_LOCTRACE=1 \
        build/Belthasar.exe --assets ct_assets.dat --scene 415 --loadwram saves/field_re/pre_scene_19f.lakewram \
        --walkscript "n:6000,a:2,n:38,..."

The rule's cartridge, no input to f6000 then A every 40 frames: `$0100` 19F, 1F4 f508, 1F5 f869, 1EE f1580, 1F5 f1978,
165 f2447, 1F4 f2660, 182 f3598, 1ED f4462, 10F f5908, 1A9 f6277; storyline CC f507, CD f9395. Without input it stops at
10F's box (state 7, `$0134` = FFFF), as the port does. Pinned: `ocean-palace-aftermath-chain-vs-oracle`.

## 1. The world VM's op 03 -- C2/10BF, class $1CF5

`SEP #20 / LDX #1CF5 / JSR C2/048A / REP #20 / LDA #000A / RTS`: a native object, ten bytes stepped. The port had no case
(the anim sub-VM's own op 03 is a jump, C2/0E91 -- a different dispatcher) and parked: 1F4 stayed black.

Class $1CF5, `JMP ($1D02,X)` on +02 (1D06, 1D4C):

    1D06 INC +02 / $10 = +07 (the op's address, the parent's pc copied by C2/048A's MVN), $12 = +09
         +0D = [op+3] (bank) / +0B = [op+1] (source) / +0E = [op+4] (VRAM word) / +12 = [op+6] / +14 = [op+8]
         +12 == 0 -> +12 = +14                                         ...and on into 1D4C the same frame
    1D4C $08 = min(+12, +14) / an NMI queue entry at $60 + $5F (INC $5E around it): $62 = +0D, $67 = 80, $60 = +0B,
         $63 = +0E, $65 = $0A = $08 << 5 / $5F += 8 / STZ $5E / +14 -= $08: 0 -> SEC (freed), else +0B += $0A,
         +0E += $0A >> 1, CLC

1F4's script: `4E 28 E0 C6` (the picture) then `03 00 90 7F 00 00 00 00 00 01` ($100 tiles to VRAM word 0).

## 2. The C6 far calls (op 4E)

A byte-pattern scan of bank C6 finds the whole family -- four picture loaders and the song hold:

    LDX #dst / STX $0303 / LDA #7F / STA $0305 / LDX #x / REP #20 / LDA tbl,X / STA $0300 / SEP #20 /
    LDA tbl+2,X / STA $0302 / JSL C3/0002 / RTL
      C6/E028  $7F:9000  C6:FDF0 + 06 -> C5:B798      C6/E04D  $7F:8C00  C6:FEA0 + 03 -> C3:CEDA
      C6/E64A  $7F:9000  C6:FDF0 + 27 -> C6:0E9A      C6/E66F  $7F:8C00  C6:FEA0 + 06 -> C3:CF6C
    C6/E63D  LDA #01 / STA $7F01ED / RTL            C6/E644  TDC / STA $7F01ED / RTL

E04D is the palette: the three op-04 rows after it read $7F:8C00/8C20/8C40 (Lavos red, not the map's blue).
### 2.1 World 1F5's wave and light (2026-10-18, ported: world.c CtWorldObjOp4E)

(Not 1F4's: the cartridge from saves/field_re/pre_scene_19f.state reaches 1F5 at f869 and runs these there, f993-1580 and,
after 1EE, f2019-2447.)

* **C6/E595**, the wave's install: the indirect table `$7E:8A4F = F0 .. .. F0 .. .. 00` (two 112-line repeat runs; their
  pointers are the NMI's copies of `$8A29 / $8A2B`, C2/02B2-02DE), HDMA channel 3 = DMAP 42 over `$210E` (BG1VOFS),
  `TSB $4A` #08, the object's +$22 := 0. **C6/E5C5**, a call a frame: the 224-line table into `$9A36` or `$9796` by +$24's
  bit 0, `$8A29` = it, `$8A2B` = it + $E0, +$24 up; line k's word = `$26` + the signed sine `C0:F900[(phase + 8k) & 3FF] * 4
  >> 8` (the second half negated: `EOR #FFFF / INC / XBA / ORA #FF00`), then +$22 += 8. BG1 is drawn through it entirely
  (its VOFS is the camera's `$26` plus the wave).
* **C6/E448 / E470**, the light's windows: `$8A35` (`$8A42`) = `F0 .. .. F0 .. .. 00`, channel 1 (2) = DMAP 41 -- two
  registers, indirect -- over `$2126` (`$2128`): WH0 + WH1 (WH2 + WH3) a line; `TSB $4A` #02 (#04).
* **C6/E498 / E4BE**, a call a frame: the buffer `$8B56 / $8DF6` (`$9176 / $9416`) by +$26's parity, the pointers
  `$8A21 / $8A23` (`$8A25 / $8A27`) = it and it + $E0, +$26 up; C3/0008 (C3/0077's circle) around (+$14, +$18) of radius
  +$22; then E4FF stretches it +$24 lines -- radius 0: the word +$14 at line +$18 repeated down (MVN one byte on); else the
  circle's top +$22 rows moved +$24 lines down and the rows between filled with the centre's (MVN a word on). Every ADC
  there follows an ASL with no CLC: at centre line 0 (`DEC A / ASL A` of 0) the carry makes the start the buffer - 1, and
  the beam's last rows come from the odd address (measured at cartridge f1038: without it the bottom rows were one off).
* **The colour window**: CGWSEL `$45` = 10 (prevent 1: math only inside), WOBJSEL `$3A` names window 1 / 2 for it,
  WOBJLOG `$40` bits 2-3 join them; CGADSUB 11 / 13 adds the fixed colour there. With the window empty (`1..0`) the clouds
  are grey -- the port drew them yellow before it modelled the window (field.c owColorWin).
* **The C2/2BFB clear**: the world's loader (C2/2C7A) and the rebuild after a screen of its own (C2/2C96) zero
  `$7E:8621-8E20` through WMDATA (`LDX #8621 / STX $2181`, 512 x four `STA $2180`): every effect's pointers, tables and
  window buffers. The port kept the field's bytes there (`$8DF6` = 80s until the light wrote it).
* **The line index**: a repeat-mode HDMA table's unit y is on screen row y (field.c OW_HDMA_LINE = 0). MEASURED: the
  cartridge's `--hdma` trace of peek f1120 against the port's buffers -- scanline s takes unit s - 1 of `$9A36` written at
  the port's f1125, 0 lines differ, every other buffer and shift 191 or more. Both this wave and the Black Omen's move a
  line a frame, so a one-line index error looks like a one-frame lag: the Black Omen's check had graded y + 1 at port
  f + 360, when the world's P + 1 is f + 361 (its WRAM runs 362 frames behind: the buffer's tail at cartridge f698 = port
  f1060). Mode 7's matrix keeps y + 1 (OW_M7_LINE), measured separately.

Graded by tools/ow_wave_cmp.py (check ow-1f5-wave-and-light-vs-oracle): seven pictures 0 px -- the light's first beam,
both beams, the wave alone and after the way back -- the wave's buffers ten frames after each side's install, and the
windows' buffers and pointers on all 75 frames the beams draw. The pictures are at d = +5 until 1EE and +9 after: the
world's WRAM is 6 frames behind the cartridge, then 10 -- the way back from 1EE to 1F5 costs the port 4 more frames than
the cartridge (the world entry's load clock, the same class as 1F1's +2 after the gauge), picture P + 1 on that clock.

## 3. The rest of the world VM

- C2/7BD1 (op 34's native): `LDA $00 / STA $7E29AE / RTS`, M = 8 -- the held song; 1F4 sets $00 with `13 00 00 0D`.
- Op 0C, C2/1230: block[op1]-- . Ops 46 / 47, C2/1ACA / 1AE1: block[op1] += / -= op2. Three bytes.
- Op 4A, C2/18F9: op 3D's body after its `$7F01ED` gate -- the song plays whatever the hold says.
- Op 51, C2/16FA: op 32's pan for ONE layer, `51 <layer> <frames>` -- the timer is operand 2, the whole pixels go to
  C2/0568 / 066C with `$00` = operand 1 (no BG3 arm, no gates); 1754 TDC / CLC / RTS yields.

## 4. A world's exit into another world

Main-loop state 2, C2/24D5: `SEI / NMITIMEN 0 / JSR C2/26F0 / $1E00 = 82, 83 / JML C0:0000` -- it does not look at the
destination, and the reset's C0/006B `CPX #01F0 / BMI` sends `$0100 >= 1F0` to the overworld loader. The port's host only
knew exits into a location (`dl < 0x1F0`); 1F4's `05 F5 01 08 08` was dropped. Now: the flag copy back, then the reboot's
own world arm (`worldInit` at the commit).

## 5. Last Village (182): Crono stays dead

obj0 fn0 `16 6A 01 06 08 / 4A 11 01 00 01 / E3 00`, obj1's loop `16 6A 01 06 04 / 8B FF FF / B0`. The cartridge: `$011F`
= 00 at f3638, obj1 at $1803/$1883 = FF from f3647. Two port defects:

- the fn0 pass's E3 set only the host flag -- C0/3C05 is `LDA $7F2001,X / STA $1F`; the byte is the op's effect;
- with `$011F` = 0 and `$0120` on, C0/8A6D runs on the lead's own words; the host ran it on its copy (px, py), stale since the
  script's `8B FF FF`, and wrote the arrival back every frame. The copy is read back first now.

## 6. The dialogue's code 03 -- the timed pause and the self-closing end

C2/5945 (the text engine): `LDA [$31] / STA $16 / INC $31`, `$15 = 03` -- ONE argument byte (the pointer steps 2 over
`03 08`, measured: $0231 F8:8930 -> 8932). The typewriter C0/2176 -> 21A8: `$0216` != 0 -> `$34 = $0216 * 15`, `$29 = 7`,
`$36 = 1`; `$0216` = 0 -> 21D8 (`$29 = 7`, `$36 = 81`, `$34` as it stands). State 7, with `$62` = 0: 20A5 `LDX $34 / BEQ
->2058 / CPX #FFFF / BEQ RTL / DEX / STX $34`; 2058 on code 03 -> 2096 `LDA $0216 / BEQ ->2081 (0D)` else `$29 = 6` (RTL:
the typewriter is the next frame's). The A handler C0/1AB4 spends `$34` only when it is FFFF.

"A tidal wave!" is `... DE 03 08 03 00`: measured (from last_village_182_f3605.state) `$0134 = 0078` at f517 by C0:21C0, one
less a frame by C0:20B1, `$0129` 06 f638, 07 f639, 0D f640 -- no press. 373 uses in the dialogue (83 `03 00`); the packer
read a second "tick byte" and so dropped the byte after every pause, and every self-closing box waited for A.

Now: assets/text.py emits `03 n` as `\x03` + two letters and `03 00` as `\x04` (the end); field.c's tbStripPauses keeps
where each stood and the typewriter spends an iteration on each (CtSceneTextboxPause); the box machine runs the count and
2096 (tbMachineTick), with the one-frame latch tbResumeArmed.

## 7. Op D3 (JOIN) through C2/8C79 whole

`D3 01` at f9395: `$2985 = 81` (C2:8D01, leaf 5's `| 80` on the joiner's reserve entry) and `$2982 = 01` (C2:8C9E). The live
VM's own arm took the first empty active slot and stopped; the exchange screen `C8 00` then listed Marle twice. Routed to
move.c's CtVmPartyGateFamily (C0/3919: leaf 1, `$78`, the object into `$0199`/`$019B`), as D4 already was.

The exchange screen that follows: B closes it on both sides (back on 1A9 with Lucca leading). Its picture still lacks the
prompt ("Who'll you replace?") and the hooded figure -- [menu2_form_screen.md](menu2_form_screen.md) 2.14.
