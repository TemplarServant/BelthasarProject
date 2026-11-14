# The run-away counter, the can't-flee box, and the flee end (`$99CF/$99D0`, `$99CD`, window 0x2B)

Status 2026-09-10: the COUNTER, the WALK's run-away window, the CAN'T-FLEE consumer with its message box, and the
FLEE TRIGGER are ported and graded (§1-§4, §6). The flee END SEQUENCE (§5) is derived to the frame and not yet
ported -- it is the next step of this item.

Correction to the item's own wording: "the 0x2B window setter `C3/3D1C`" was a mis-decode (bank C3 data). The
setter of `$99CF/$99D0` is the bank-CF frame service, and `C1/1076` is the can't-flee branch of the main loop's
iteration head.

## 1. The counter -- `CF/E706-E73A`, in the frame service (the same linear pass as the `$A4` advance `CF/E470`)

```
FE706  LDA $A013 / BNE ->FE73B                     nothing inside an action span (neither the INC nor the clear)
FE70B  LDA $004218 / AND #30 / CMP #30 / BNE ->FE732   L and R both held: the AUTO-READ joypad, low byte bits 5/4
FE715  REP #20 / INC $99CF                          16-bit count $99CF/$99D0, +1 per frame
FE71A  LDA $99CF / CMP #0078 / BCC ->FE72D          under 120: done
FE722  LDA $2989 / AND #0080 / BNE ->FE72D          bit7 = this fight cannot be fled
FE72A  INC $99CD                                    16-bit: the FLEE TRIGGER $99CD/$99CE, +1 every frame past 120
FE72D  TDC / SEP #20 / BRA ->FE73B
FE732  STZ $99D1 / STZ $99CF / STZ $99D0            not held: the count AND the can't-flee latch
```

`$4218` is the auto-read register: the NMI that opens frame N latches the pad as it stood during frame N-1. Oracle
gato_t0, `--input "00:240,C00:60"` (mask C00 = L+R): the first INC is f241 (CF:E71A), the release at f300 clears at
f301 (CF:E738). The `$2989 & #0080` is the 16-bit AND of the byte pair `$2989/$298A` -- only bit7 of `$2989`
matters. All fixtures: GATO `$2989 = $92` (flee-blocked, written by the field's battle-call op, ct_vm.c),
forest/ambush `$02`.

## 2. The walk's window -- `C1/2E31-2E49`

`JSR C1/308C / LDA $A440 / CMP #03 / BNE / LDA $99CF / ORA $99D0 / BEQ / LDA #2B / STA $A440 / LDA #FF / STA
$968C,X`: a PC whose status class is 3 (normal) and a non-zero count takes window 0x2B with its cursor reset. The
poses are the run-away sprites -- entry 0 with the seat's facing: gato_t0 Crono 07, Marle 19; forest Crono 15,
Marle 13 -- and the OAM is their E3 record at the live foot, exactly:

```
07 (Crono, foot 134,144): (123,96)(126,112)(142,128)(126,128)      oracle peek f262 pal5
19 (Marle, foot 176,111): (167,79)(168,63)(169,95)(153,95)         oracle peek f262 pal6
```

Only the entry INTO 0x2B resets the cursor; the return to window 3 after the release continues from the idle
cursor. The port already had this test in `battlePcWalk`; nothing set the bytes until now.

## 3. The can't-flee consumer -- `C1/106E-1099`, the main loop's iteration head

```
1106E  LDA $A10E / BNE ->1109D                     the defeat teardown flag (C1/8148 sets it; this port ends there)
11073  STZ $A0FD                                   (the service's "an action opened" flag -- not modelled)
11076  LDA $99CF / ORA $99D0 / BEQ ->1109D
1107E  LDA $2989 / BPL ->1109D                     fleeable: the count stands, the service ends it
11083  STZ $99CF / STZ $99D0                       can't flee: cleared every iteration
11089  LDA $99D1 / BNE ->1109D / INC $99D1         once per hold (the service zeroes $99D1 on release)
11091  LDA #FF / JSL CD/0027                       reset the message dedup
11097  LDA #75 / JSL CD/0027                       post message $75 = "Can't run away"
1109D  JSR C1/007E / JSL CD/0009 / ... / JSR C1/2D9F   the frame wait, the service, the walk dispatch
```

WHERE IT RUNS: the head executes in the same frame as the previous iteration's tail -- after that frame's service
(the INC) and its walk dispatch -- and an iteration spans 2 or 3 frames (the 2-per-5 cadence the walk dispatch
rides). So the count reads 1,2,0,1,0,1,2,0,... on gato_t0 (INC every frame by CF:E71A; cleared f242, f245, f247,
f250, f252, f255 ... by C1:1086/1089), and every walk visit in between sees a non-zero count: Marle 19 at her
f251 visit (`$A103 = 03 2B`), Crono 07 at f258. The port runs the consumer right after the walk dispatch on the
same `schedWalk` frames.

## 4. The message box -- `CD/0027 -> CD/0310` over `CC:3A09`, the C2/0003 window

```
D0310  LDX $CD1D / BEQ / RTL                       a box teardown pending: refused
D0316  CMP $CD06 / BEQ ->D033F / STA $CD06         the dedup latch (same id twice = one box)
D031E  CMP #FF / BEQ ->D033F                       FF only resets the latch
D0322  LDX #3A09 / STX $020D / LDA #CC / STA $020F   the text table: 16-bit X, $020D/$020E = $3A09, bank CC
D032F  JSR CD/025E                                 STA $020C (the id) / STZ $CCED / STZ $02A1 / $0212 / STZ $0214 /
                                                   JSR CD/0239 (clears the box's tile buffer) / JSL C2/0003 (renders)
D0332  LDA #10 / STA $CCEC / INC $CA24             the WINDOW band preset (D1/FE5E indexes CE/EE6E with $CA25+$CA22+$CCEC)
D033A  LDA #01 / STA $CD05                         the box tick armed
```

`CC:3A09` is the tech-DESCRIPTION pointer table (menu.h) doubling as a system-message table: id $75 -> CC:4386 =
"Can't run away" (assets/text.py's dictionary decoder, the same one `tools/_gen_menutech.py` uses; $74 = "Phys.
Att. w/ NewMasamune", $76 = "Single Tech"). Oracle gato_t0 f242: `$020C..F = 75 09 3A CC` (CD:0261), `$CCEC = 10`
(CD:0337), `$CD05 = 01` (CD:033F), `$99D1 = 01` (C1:1091).

The box's life, two services:

* `CD/04DF-04FC` (the battle frame service): while `$CD05 != 0`, `LDA #01 / STA $0213 / JSL C2/0009 / LDA $0215 /
  STA $CD05` -- the C2 renderer answers `$10` while it draws the window in, and `0` when done; on that frame
  `JSR CD/01A5 / STA $CCEB / INC $CCED` arms the dwell from the message-speed table (`$CD019D[$2991 >> 3 & 7]`,
  `$A0` = 160 at the default). Measured: `$CD05 = 10` f243-f260 (18 frames), the dwell armed f261.
* `D1/E7EC-E80C` (the HDMA band service): `LDA $CCED / BEQ / DEC $CCEB / BNE / LDA $CD1D / BEQ / LDA #FF / STA
  $CE17 / STZ $CD1D / STZ $CCEC / STZ $CCED / INC $CA24` -- counts the dwell out and drops the window. Measured:
  `$CCEC := 0` at f421 = f261 + 160 (D1:E806). The window stays up through the release (f300) -- nothing but the
  dwell closes it.

The port keeps every one of those bytes in WRAM (`$020C..F`, `$CCEB/CCEC/CCED`, `$CD05/CD06/CD1D`, `$CA24`), models
the C2 renderer's 18-frame opening with a host counter, and draws the text through the results-window renderer
(the same C2/0003 window, placed by the same strip-side preset) whenever `$CCEC = $10` and `$020D..F` name
`CC:3A09` -- so a mid-battle seed carrying an open box renders it, and a results-box seed (`CE:EF11`) does not take
that branch. The module init `D1/F210-F29E` (oracle f78, the transition) zeroes `$CCEA..$CCED`, `$CD02`, `$CD05`,
`$CD1D` and sets `$CD06 = FF`; pre-battle seeds carry field leftovers there (`$CD05 = $37`, `$CD06 = $AF` on
gato_t0.raw.wram -- a box tick fired at boot until those bytes were staged).

## 5. The flee END -- `C1/81EC-821E`, derived and PORTED (stage 2, 2026-09-10)

At the turn/end check (after C1/813F defeat and C1/817A victory):

```
181EC  LDA $2989 / BIT #20 / BNE ->18221           bit5: no flee check (the CF/E6D9 timeout fights)
181F3  LDA $99CD / BEQ ->18221                     the trigger
181F8  TDC / STA $7F01EC                           the field's battle result := 0 (as a victory)
181FD  JSL FD/AA98                                 every enemy block's +$1D := $80 (LDA #80 / STA $5FCA,X, X += $80)
18201  LDA #08 / JSL CD/0021                       -> CD/01B3: MESSAGE 8 of CE:EF11 = "Ran away!" through C2/0003
                                                   (LDX #EF11 / STX $020D / LDA #CE / STA $020F / ... JSR CD/025E /
                                                   CD/027F / CD/020A (16 frames) / CD/01A5), then for id 8 a FIXED
                                                   `LDA #3C / JSR CD/3E3B` = 60 frames, then STZ $CCEC
18207  JSR C1/EA9D                                 (per-PC block clears, C1/E9A3 loop)
1820A  JSR C1/B7F2                                 the defeat/flee teardown (= battleEndTeardown)
1820D  LDA #79 / JSR C1/895B                       $AE93 = $79, $AE91 = 0, $AE92 = 2, $AE94/95 = 0, $AE96 = $80,
                                                   JSR C1/AC57 -> dispatch[4] + C1/EC7F: ACTION $79 = the run-off
                                                   choreography (CD/5526 + $79*7 = 79 91 00 46 43 43 42)
18212  JSR C1/EAE8 / JSR C1/B442 / JSR C1/B4E9 / JSR C1/B3BB    the writebacks + the mode-6 survivor teardown
1821E  JMP C1/8423                                 the module exit
```

Oracle forest_holdleft_pre (`--input "40:120,00:230,C00:420,00:500"`): the count climbs from f351, freezes at 0x65
for the Hetake's action (f451-f628), resumes f629, reaches 0x78 at f647 -> `$99CD = 1` (CF:E72D) and the SAME
frame `$7F01EC := 0` (C1:81FD), `$5FCA := 80` (FD:AA9F), `$020D..F = 11 EF CE` (CD:01C1); the window preset
`$CCEC = 10` at f650, `= 0` at f726 (CD:0200, after the 60-frame wait); `$AE93 = 79` at f742 (C1:895E) with
`$A013 = 1` from f744 to f951 (C1:40A9 / C1:41BD) -- the 207-frame run-off action; the module-done flag `$0117 =
0001` at f1039 (C0:18C1) and the field resumes at f1057 (C0:3AB1). `$99CD` keeps counting while the pad is held
(0x62 at the release) -- nothing reads it after the trigger.

What porting it needs: the results-window machine (CD/01B3) driven with a message id rather than a reward line
(text "Ran away!", the fixed 60-frame wait for id 8, no A-cut), the action-$79 script through the interpreter with
the three PCs as actors, and a third `endReason` for `fieldBattleFinish` (no rewards, the enemy blocks marked
dead by FD/AA98 before B3BB's survivor rule).

### 5.1 What the run-off action `$79` is (2026-09-10, corrected)

An earlier draft of this section named the script at CD:D07F and five tracks with ops 2A/73/07 -- that was the
ENEMY-table entry for $79, the wrong table. The action goes through the PC-TECH path (`$AE92 = 2` -> C1/7A63[2] ->
C1/45BB), so the script is `CD/5EF0[$79]` = CE:81D0: header `80 00 80 00`, ONE track (role 0 = the actor) at CE:81D8:

    34 1B          INC $5D80[$1B] = $5D9B          (C1/5931, free)
    22 1B 00       wait until $5D9B == 00           (C1/572C: A = 0 / no INC $A00F while unequal -> parked;
                                                    `INC $A00F / LDA #03` when equal -> free)
    2E             STZ $CFFF                        (C1/58E4, costs a frame)
    01             end the action                   (C1/4CF7 -> INC $9876; C1/40CE's loop test sees it)

Nothing in it moves a combatant. The choreography is a BACKGROUND PROGRAM: the effect load for `$987C = $86`
(CD/45A6[$79] = `79 61 00 01 00 00 86`) stages `D1:6240[$86] = 8F FF 2F 2F` into `$CA93..$CA96` (CD/0F4F-0F5C,
`STX $CCA2` first), and CD/0D53 arms program `$8F` in the first of the two program slots through CD/15D5 --
`$CA35 = D1:646C[$8F] + 1 = D1:6796`, `$CA37 = D1`, `$CA38 = D1:6795 & 0F = 1` (the per-frame divider), `INC $CA39`,
`INC $CA32` -- measured at open+10 (f754 for the f744 open; the block $CA32..$CA92 is zeroed by the same load).

**The runner, CD/1609**, in the bank-CD frame service (CD/09AF), every frame while `$CCEA == 0 && $5D9B != 0`:
`$CA92 = CD/0E9D` (0 for `$CCA2` in {86, 7B..7F}, else 1); per slot `DEC $CA39,X / BNE next / reload from $CA38,X /
JSR CD/1654`; when both slots are idle (`CPY #02`) `STZ $5D9B / INC $CCEA`. CD/1654 runs ops from `[$40]` until one
costs the frame: a POSITIVE byte selects an HDMA table (`+ $CA3A,X`, `<< 9`, `+ D000` -> `$CA18`, `INC $CA17`),
`7F` idles, `FF` puts the slot to sleep (`STZ $CA32,X`); a NEGATIVE op is dispatched through CD/16A6 -> the CD/16B5
table with `$40` advanced past the opcode before the handler and once more after it (so every handler's operand
count is one less than the bytes it spans). The program at D1:6796, op by op, with the oracle's frames (open f744):

    6796  DC 0D                     CD/1923: $CD24 = 0D, $CD25 = 0080, INC $CD23 -> CD/3B5F..3B71: cmd 18 id 0D
                                    pan 80. First runner frame = open+23 (f767), the frame after the script's
                                    `34 1B` (open+22, f766): the service runs before the main loop in a frame.
    6798  83 07  90 71  7F x4  84   CD/2EA2: $CA74 = 07 + 1, $CA72 = the count byte's address. Body: CD/2EF7
                                    op 90 -- $BE9A's HIGH nibble += 1 (saturating at F), LOW nibble := 7; four
                                    idles. CD/2EB5: DEC $CA74, back to the count byte (the post-INY steps over
                                    it) or DEY on zero. $BE9A: 00 -> 17 (f767), 27, ... 87 (f795), 4 frames apart.
    67A1  E6 01                     CD/18C2: $CFFF = 1 (f799 = open+55).
    67A3  8E 50 10 70 FF 00 08 02   CD/29E3, an OBJSLOT SPAWN (7 operands; $40 += 6, then the post-INY):
                                    operand 5 & 7 = the slot k -> record $0550 + CD/2A42[k] (k=0 -> $05A4);
                                    +0 = op0 & F0 (mode 5), +6 = op0 & 0F, +1 dst 10, +2 count 70 (CGRAM $10..$7F
                                    = BG palettes 1-7), +7 acc FF, +8 limit 00, +3 = op5 & F8 = step 08,
                                    +5 = op6 & 0F = period 2, +4 = op6 >> 4 = the tick's start 0.
    67AB  8E 50 90 30 FF 00 09 12   the same for k=1 -> $0598: CGRAM $90..$BF (OBJ palettes 1-3), tick start 1.
    67B3  83 07  90 71  7F x4  84   loop B: $BE9A 97 (f799) .. F7 (f823), then held at F7 by the saturation.
    67BC  83 40  7F  84             loop C: 65 idle frames (f831-f895). Meanwhile the two records reach black
                                    (below) at f861/f862.
    67C0  80 1B                     CD/2A4A -> ($2A51,X)[1B] = CD/2A8B: INC $9FFA..$A001 -- the eight ENEMY HIDE
                                    FLAGS ($9FF7 + 3 + slot). CF/FE28 `LDA $9FF7,X / BNE skip` builds no OAM for
                                    them: the OAM shows the twelve pal-1 sprites gone at f899 (f896 = open+152).
    67C2  8E 50 10 70 00 FF 08 02   k=0 again: BG palettes 1-7 from 00 back UP by 8 every 2 frames.
    67CA  83 0F  90 F1  7F x3  84   loop D, 16x: $BE9A high nibble -= 1 (op 90 with bit 7: SBC, floored at 0),
                                    low nibble := F -- EF (f896), DF, ... 0F (f938), then 0F.
    67D2  E6 00                     $CFFF = 0 (f944).
    67D4  FF                        the slot sleeps (f944); the next runner frame finds both idle: `$5D9B = 0`
                                    and `INC $CCEA` at f945.

**The objslot service, FD/E39C** (`JSL FD/FFF7` = the bank's vector, from CD/09BF right after `JSL D1/F426`): the
twelve 12-byte records $0520..$05AF, every frame: `INC +4 / CMP +5 / BNE` (the tick against the period), on the
wrap modes 1/8 step a sub-counter in +3, then the mode's handler (+0 >> 4: 1 FD/E437, 2 E532, 3 E4E8, 4 E82C,
**5 FD/E5A4**, 6 E807, 7 E57C, else E708). FD/E5A4: `+5` bit 7 -> the byte itself is stored as the acc; otherwise
`acc >= limit`: `SBC step`, a borrow or a result below the limit clears the mode and lands ON the limit; `acc <
limit`: `ADC step`, a CARRY clears the mode and copies the base page back (`$7E2000 -> $7E2200` for the record's
words, no acc store), a result at/over the limit clears the mode and lands on it. Then, for sub-mode 0, FD/E64F
rewrites the live page word by word: `STA $4202` = acc, `STA $4203` = the component, `LDA $4217` = the product's
high byte -- B and G are shifted into the byte first and masked back, so every 5-bit component becomes
`(acc * c) >> 8` (FF gives c - 1, 00 gives black). Measured: $0598 F7 at f799 (its tick started at 1), $05A4 F7 at
f800, both -8 every 2 frames, mode off at 00 at f861/f862; the third spawn 08 at f897, F8 at f958, the carry at
f959/f960 (the base page back).

**The effect lock, D1/F426** (`JSL` from CD/09BB): while `$CFFF` is set, once (`$CE12`), the eight mode bytes are
saved to `$CD2F..` (D1/F411) and every mode-1x record is parked (`STZ +0`, D1/F43C-F453); when it drops, they are
restored (D1/F457). The forest's record 0 (`13`) is the one parked (f799) and restored (f944).

**The MOSAIC.** `$BE9A` has no `STA $2106` anywhere in the image because the register is fed by HDMA: CD/0A91-0A9B
sets channel 6 every NMI (`$4360 = 41` mode 1 indirect, `$4361 = 05` -> $2105/$2106, table `$7E:C975`), the table
CE:EEF8 builds at the boot (and CD/054D rewrites per band step) is `[50][BE99] [50][BE99] [30][C14F] ...` -- so
the arena's scanlines take BGMODE from `$BE99` and MOSAIC from `$BE9A`, and the band's lines from `$C14F/$C150`.
Oracle `--ppuregs` (the probe now prints `mosaic size=... bg=...`): f796 `size=9 bg=1110` for `$BE9A = 87`, f830
`size=16`, f920 `size=8 bg=1111` for 6F. The shadow itself is staged at the boot by CD/0D0E-0D11 `LDX $2C6E / STX
$BE99` (oracle f173 `$BE99 = 0900`), which is what keeps a pre-battle seed's leftover in that byte (gato_t0: C5)
from ever reaching the PPU; the native LakeSnes states carry 09 00.

**The end.** `$5D9B = 0` at f945 (the runner) -> op 22 passes (free) -> 2E costs f945 -> 01 at f946: `$9876 = 1`
(C1:4D03), C1/40CE falls through, C1/40D3 posts #FF, then FIVE C1/007E waits: C1/40E1 (the `$5D9B` re-test),
C1/4116 (then CC/F06B: `$A014.. = FF`, `$5D9B = 0`, `$9876 = 0` at f948 -- CC:F08E/F071/F0EC -- and the C1/411D
scan-all), C1/414B, C1/41B4, C1/41B7 -> `STZ $A013` f951 (C1:41BD). Then C1/8212's EAE8 / B442 / B4E9 / B3BB and
C1/8423 (EA9D / EAE8 / B442 again, `$29AD = $26`, FD/AAB0) -> C1/0006 -> CF/FBE5 -> CD/03B4..03C9: `STZ $CD23 /
LDA $2A1F / AND #40 / BNE / JSR CD/039E` = cmd 81 id 10 pan 00 at f956 (+5) **and then `LDA #0F / JMP CD/3E3B`,
15 waits inside CD/039E** (the victory path arms its HUD close on the same frame as the sound in this port; the ROM
has these 15 waits there too -- a residual now on record, not touched here); CD/0516 = `$CE16 = 1`, wait, `$CA25 =
0C / INC $CA24` (f972), wait, `$CA1F = 17 / $CA1E = 80` (f973); CD/03DA-03E1 polls CD/0534 while the band animator
steps (f974-f984, CD/05EB) to 0 (f985, CD/05FE); `$CA25 = 0E / INC $CA24` (f985), wait, `$CA25 = 08 / INC $CA24`
(f986), then the CD/044A loop on the APU port `$2141` (the battle song's fade-out) and `JSL C0/000B` -- the field's
module-done `$0118 = 1` at f1039 (C0:18C1) and its resume f1057. C1/B4E9, on both end chains (C1/83FF too), is the
QUEUED-ITEM REFUND (C1/B6D1: `$93EF + seat*7` bit 6 -> cleared, the `$1580` record for `$93F4 + seat*7` gets its
count back).

### 5.2 The port (stage 2)

* `battle.c`: at the trigger (the end check) `$7F01EC = 0`, FD/AA98's dead marks, the CD/01B3 post bytes (`$020D/E/F
  = 11 EF CE`, `$020C = 8`, `$0213 = 40`, D1:EE99[8] = FF so no sound); then `battleFleeStageTick` runs INSTEAD of the
  turn machinery every frame (the ROM's main loop is inside C1/81EC): T+3 the preset `$CCEC = 10`, T+79 the close,
  T+95 `battleFleePcClear` (C1/EA9D) + `battleEndTeardown` (C1/B7F2) + C1/895B's bytes + the CC/F06B wipe, T+96 the
  CC/F102 clobber, T+97 `battleFleeOpenAction` (C1/40A6, CC/F156's roles from the actor + CC/FBC8's co-actors + the
  `$AE95` bit walk, C1/45BB's config bytes and table words; the host record with `actEndByScript`), open+10
  `battleFleeProgramArm` (CD/0F2C..CD/15D5); `battleAdvanceAction` runs the action (the asc interpreter arms at
  `CT_FLEE_ACT_PREROLL` = 22 = 13 + U = 9, measured like the 21/33 pre-rolls -- 21 under the rule, sec. 8; ops 22/2D/2E/34 added; op 01 records
  `actEndRel` and `INC $9876`; the #FF post on that frame; the wipe at +2; the close at +`CT_ACT_END_TAIL` = 5 with
  no HP apply, no settle re-aim, no second #FF); the exit stage: +5 the sound 10, +20/21/22 CD/0516's three steps,
  the band poll, `$CA25 = 0E`, `08`, `endReason = 3`, return 1 (the APU handshake is the audio clock's -- residual).
* The frame services, in the CD/09AF-09BF order at the top of the tick after the icon builder: `battleFleeProgramFrame`
  (CD/1609 + CD/1654 + the seven handlers + the CD/3B5F sound post), `battleObjSlotPark` (D1/F426), `battleObjSlotFrame`
  (FD/E39C with mode 5; the other modes' records tick, their handlers stay unported). The live CGRAM page is
  `h->palK[16]` (one factor per 16-word palette, -1 = the base page) written by the mode-5 handler.
* The `$5D80` store file moved from a host array into WRAM (`h->mem[0x5D80 + x]`), so the runner, the interpreter and
  the CC/F06B wipe share the ROM's byte; the wipe (`CtBattlePcLadderWipe`) now also zeroes `$5D80..$5E0C` and `$9876`
  and sets the eleven roles `$A014..` to FF, as CC/F06B does. `CtBattleBegin` stages D1/F295 `$CFFF`, D1/F277 `$CE12`,
  D1/F2BB `$CA32`, D1/F253 `$CA92` and CD/0D02's shadow pairs (`$BE99/$BE9A` from `$2C6E`).
* `field.c`: `battleHiddenObj` (CF/FE28) drops a flagged combatant from the draw list (live fights only; traced once
  per drop); `battleFleeObjPal` scales an enemy's OBJ palette by its palette's factor (the Hetake/GATO/generic
  blits); `battleFleeFxRender`, before the sprites, scales the arena composite by BG palette 1's factor and applies
  the MOSAIC from `$BE9A` (size = high nibble + 1, when any BG bit is set) in frame-anchored blocks;
  `battleResultsRender` draws CE:EF11[8] "Ran away!" for the `$020F = CE / $020C = 8` post; `fieldBattleFinish` skips
  the reward grant on `endReason 3` and refunds a queued item (C1/B4E9). The headless `--battletest` surface has no
  arena, so the BG fade and the mosaic are graded through the shadow bytes in the traces.
* Residuals, on record: the port's arena composite has no per-palette split (BG palette 1's factor scales the whole
  arena; the ROM leaves palette 0) and its HUD strip is drawn after the mosaic (the ROM's BG1 band lines carry
  `$C150`'s own mosaic byte); the OAM lag (hidden at the write frame vs the oracle's OAM three frames later); the
  port's close does the scan-all/roles clear on its last frame where the ROM does them at E+2 (the icon dossier's
  known residual); the module hands back at the `$CA25 = 08` frame (E+35) where the ROM waits ~50 more frames on the
  APU port; the D1/F411 save CD/0F2C's path also performs at the load (f754) is not modelled (its bytes equal the
  f799 save's); `$0212`, `$A0FD`'s meaning and the `$CA95/$CA96` HDMA bases are written, not consumed.

## 6. The port and its checks

* `battleFleeCounterFrame` (top of the frame, after the ladder tick, every frame, `!$A013`): §1 with the auto-read
  lag (`held` = last frame's L+R level). Input: `CT_BIN_LRH` from both Shift keys held (the field's run key idles in
  battle), or the `CT_HOLDBTN` mask (L = 0x20, R = 0x10, the `$00F2` layout) read inside the tick in BATTLE frames.
* `battleFleeConsumer` (after the walk dispatch, `schedWalk` frames): §3, posting through `battleSysMsgPost` (§4).
* `battleSysMsgFrame` (top of the frame): the D1 dwell countdown, then the CD/04DF box tick.
* `battleResultsRender`: the `CC:3A09` branch (§4).
* The trigger at the end check (after defeat): the ROM's trigger-frame writes, then the stage machine (§5.2).
* Checks: `field-battle-cantflee-box` (gato_t0, L+R f200-f260: post f202, box f221-f381, Crono 07 / Marle 19 with
  the oracle's cells, the shot with the window; 4 mutations killed: the once-per-hold latch, the 18-frame opening,
  the message-speed dwell, the walk's 0x2B test) and `field-battle-flee-counter-forest` (forest_pre, L+R f250-f1150:
  the count through two Hetake actions, threshold + trigger f774, 348/229 at the release; 3 killed: the 0x78
  threshold, the `$A013` freeze, the auto-read lag).
* Stage 2 (2026-09-10): `field-battle-flee-counter-forest` re-pinned (the flee lines after the trigger are the
  sequence's; the pcwalk f904 line is gone because the main loop is blocked -- the ROM's own freeze);
  `field-battle-flee-end-forest` (forest_pre, the whole sequence f774 -> f1113: every program op line with `$BE9A` /
  `$CFFF` / `$9FFA`, every mode-5 record step, the sounds 0D / FF / 10, the hidden-object drops, the module hand-back;
  the shot at open+60 = the enemies through the live page at k = 0x77; mutations: op 22 never parks, the hide op
  skipped, the spawn's tick nibble, op 90's saturation, the 60-frame box wait, the runner's `CPY #02`, the five-wait
  tail, the pre-roll (21 under the rule, sec. 8), the renderer's hide gate) and `field-battle-flee-ranaway-box` (the box shot at f800;
  mutations: the empty line, a late preset) and `field-battle-flee-objfade` (the shot at open+60, open+59 under the rule -- sec. 8 -- = the Hetakes
  through the live page at k = 0x77; mutations: the sprite draw ignoring the page, half the FD/E5C6 step).
* This port's GATO acts at f251 where the oracle's acts at f332 (the enemy first-turn pacing residue, unchanged):
  with the hold from f240 Marle's deferred visit never lands before the freeze, which is why the check holds from
  f200.

## 7. Under the rule (2026-10-27): the counter reads $4218, the box opens at the post + 2

Re-measured on the rule's cartridge (the infinitely fast CPU, CLAUDE.md): saves/battle_re/forest_holdleft_pre with
`--input "40:120,00:230,C00:900,00:500"` -- the post (`$020D` := 11 EF CE, C1/8201) at f849, `$CCEC` = 10 at f851, = 00
at f927, `$AE93` = 79 at f943; the playthrough's 48_lab32_road (L+R from f588) the same offsets from its post at f708
(710 / 786 / 802, `$A013` = 01 at f804). Section 5's f647 -> f650 was this oracle before the rule flip: the box's
C2/0009 render is NMI-on CPU work, free under the rule, so the open is the two CD/3E7D waits alone. battle.c's
battleFleeStageTick takes + 2 / + 78 / + 94 / + 96 (was + 3 / + 79 / + 95 / + 97).

The counter itself (section 1) reads the auto-read register: CF/E70B `LDA $004218`. battle.c counted the frame's own L+R
level with a frame of history, which on a route drive (the port's pad a frame after the cartridge's, field.c
CT_WS_BATTLE_LEAD) made the first INC a frame late (f590 for the cartridge's f589) and every run-away with it. It reads
h->joyAutoRead now, as the demo button CF/E6E0 beside it does.

## 8. The run-off under the rule (2026-11-10): the pre-roll is 21, the load has no wait

Sections 5.1 and 5.2 carry the run-off's frames as the cartridge gave them BEFORE the rule flip (open f744: the arm at
open+10, `34 1B` at open+22, the first runner frame at open+23). One of those frames was a lag frame of the load.

The listing. The action prologue is C1/4836 `JSL CD/0015` (5 frames), C1/483D `JSL CD/002A` (3), C1/4841 `JSR C1/007E`
(1), then C1/4857 `JSL CD/0018` -> CD/0D28 `STZ $5D9B / TAX / JSR CD/0EBD / JSR CD/0D33 / RTL`. In CD/0EBD:
`0EF8 JSR CD/0E9D / 0EFB BCC ->0F42`, and CD/0E9D answers carry CLEAR for the ids 86 and 7B..7F (`TDC / CLC / RTS`).
CD/0F42-0F8E copies the four ids from `$D16240,X` to `$CA93,Y`, calls CD/11B8 (a table copy) and returns at 0F8E
(`0F89 JSR CD/0E9D / BCS / RTS`): no CD/3E75, no CD/3E3B on the way. CD/0D33 then arms the slots (CD/15D5: `INC $CA32`)
in the frame C1/4841's wait returned in. So the arm is open+9, and the twelve C1/007E waits after it put the script's
first op at open+21: a no-effect action's 13 + 8.

The rule's cartridge, two scenes. saves/battle_re/forest_holdleft_pre with `--input "40:120,00:230,C00:900,00:500"`
(`CT_ORACLE_LOGICAL=1`), the open (`$A013` = 1) at f945, offsets from it:

    +9    $CA32 = 01                         (the arm; 00 at +199)
    +21   $5D9B = 01                         (the script's `34 1B`; 00 at +200)
    +22   $BE9A = 17, then +10 every 4 frames to F7 at +78
    +54   $CFFF = 01; record $0598 mode 50 (to 00 at +116), record $05A4 mode 50 (00 at +117)
    +151  $9FFA = 01 (the hide); record $05A4 mode 50 again (00 at +214); $BE9A = EF, then 3 frames apart to 0F at +193
    +199  $CFFF = 00
    +206  $A013 = 00

The fade records' acc cells: `$059F` F7 at +54, EF +56 ... 07 +114, 00 +116 (32 steps); `$05AB` F7 at +55, EF +57 ...
F8 at +212 (63 steps). Playthrough leg 107's run-off (loc 0E3): open f11727, `$CA32` f11736, `$5D9B` f11748, `$A013` = 0
f11933 -- the same +9 / +21 / +206.

The port (battle.c: `CT_FLEE_ACT_PREROLL` 21; battleActPreroll adds no wait for the ids CD/0E9D clears) on forest_pre,
open f829: arm f838, the program's first frame f851, the spawns and `$CFFF` f883, the hide f980, `$CFFF` = 0 f1028,
run-off-end f1035. Every program op, mosaic step and fade-record step equals the cartridge's by open-relative frame
(the two acc cells: 32 of 32 and 63 of 63). The 7B..7F ids (enemy techs A0-A4) take the same branch by the listing; no
cartridge case of them was measured.

Checks: `field-battle-flee-end-forest` (251 of its 263 lines one frame earlier; the shot at open+170 unchanged),
`field-battle-flee-counter-forest` (the five lines after the open), `field-battle-flee-objfade` (the same picture at
open+59, hash unchanged; the window f883-902). Leg 107's alignment loses the warp f11933+1 with it (16 warps left, all loads, fight exits and one first window; the fled fight's exit is f12008+2).

## 9. The menu under the run-off (2026-11-13)

The run-off is an action like any other, and an action's loops call the menu pass themselves: C1/10FA (`LDA $993A /
BEQ ->1110 / STZ $993A / STZ $E5 / JSR C1/1B67 / JSR C1/0C2D / JSR C1/1153 / JSR C1/17DD`, then `JSL CD/0009`) at
C1/40A3, after the pre-roll's waits (C1/485E, 4867, 4890, 4946) and in the script loop's C1/40B6 and C1/40E4. C1/820A's
teardown (C1/B7F2) has left no chooser by then, so the first pass repaints the strip without the command menu.

Measured on the rule's cartridge, playthrough leg 95 (loc 1BF, the Vamps, run away from with L + R held):

    f1062   `$95D5` = FF (the teardown, the stage's t 94)
    f1064   `$A013` = 1 (C1/40A6, t 96)
    f1066   `$A6DF` FE -> FF: C1/0C2D's repaint, the request the frame service raised (`$993A`, every fourth frame)
            consumed on its frame -- `$993A` reads 00 on every row of the run-off
    f1068   the picture: the strip's left panel is the enemy's name alone ("Vamp"), the hand and the bracket gone, to
            the exit at f1283

The port's flee stage ran the pass on `$993A` only up to the dispatch (`t < 94`, CD/01B3's CD/3E75 waits): under the
run-off the requests piled up (`$993A` 01, 02, 03 ... on f1066, f1070, f1074) and the command menu, the hand and the
bracket stood for the whole run-off -- 1007 px of leg 95's f1200, the same at every lag. battleFleeStageTick now makes
the action loop's pass from t 96 on (not from C1/4116 on: menuPassHold, as every action).

Also staged, the boot's CC/E31B `STA $95D2` and CC/E32D `STA $A6DC` (A = FF, the run that fills `$A6D9-$A6DB`, `$A6DD`,
`$A6DF`, `$A6E0`): the cartridge writes both at f659 and the port left 00.

Open: the strip's close after the run-off -- the cartridge's left panel shows two pictures on f1286 / f1287 that the
port's f1285 / f1286 do not (the frames before and after are the same, a frame apart).

Check: `field-picture-run-off-closes-the-command-menu-vs-cartridge`.

## 10. The walkers under the box (2026-11-13)

The walk stepper is the frame service's, not the main loop's: CF/EFAF-EFBA (`LDA $A013 / ORA $A0FD / BNE skip`, then
`JMP CF/F978`) runs on every NMI whatever the main thread waits in, and CF/F978's loop (`LDA $98A7,X / BEQ next / DEC
$9819,X / BNE next / ... / STA $1D26,X` CF/F9C2, `STA $1D0F,X` CF/F9CB) moves every segment in flight. C1/81EC's
blocking chain stops the main loop -- no C1/3714, so no new segment starts -- but not the NMI: the segments in flight at
the flee's trigger walk on to their ends under the "Ran away" box, until C1/4058 counts `$A0FD` up.

Measured on the rule's cartridge, the forest recipe's twin (saves/battle_re/forest_holdleft_pre.state, 120 frames left
into the Hetakes, L + R held from f350; the post at f849):

    f849    `$1D0F/10` = 70 5A (the post's frame; writer CF:F9CE)
    f850    70 5B, `$1D27` 5E -> 5D
    f853    6F 5B      f854  6F 5C      ... one step every fourth frame on each axis ...
    f866    6C 5F      f869  6B 5F      f873  6A 5F      f877  69 5F
    f881    68 5F (the last segment's end; nothing moves after)

The port's flee stage returned before the turn machinery's stepper call (battle.c: `if (h->fleeStage) return
battleFleeStageTick(h)`), so its enemies froze on the post's frame: the two left Hetakes stood up to 9 px from the
cartridge's for the rest of the fight (1257 px of every frame from f900 on, the same at every lag). battleFleeStageTick
now runs the stepper and the part link (CF/E75C -> CF/F9D3, the same service) at its top, on the stepper's own gate;
the feet are the cartridge's on every row f849-f881 and the pictures between two steps are 0 px whole.

Not measured: a multi-part enemy (CF/F9D3's link) in a fled fight.

## 11. The mosaic by the line (2026-11-13)

Section 5's "The MOSAIC" has the feed: `$2106` is written by HDMA channel 6 alone, from the shadow `$BE9A` through the
table $7E:C975. What the picture needs beyond the shadow's value:

**The table's runs.** Bottom-HUD layout (the forest, cartridge f1000): `50 [BE99] / 50 [BE99] / 30 [C14F] / 04 [BE99] /
00` -- runs on lines 0, 80, 160 (the band's own shadow `$C150` = 00: no mosaic on the HUD) and 208 (the four arena
lines under the band). Top-HUD layout (leg 95, loc 1BF): `08 [BE99] / 30 [C14F] / 54 [BE99] / 48 [BE99] / 00` -- runs on
lines 0, 8 (the band), 56 and 140. A run on line L sets the register for the lines after it (the line is drawn before
its H-blank's run), i.e. picture rows L and down.

**The line cut.** The program's op 90 (CD/2EF7) writes `$BE9A` inside the NMI's service, which under the rule lands on
line 64 (CD/09AA's wait; CLAUDE.md "The battle NMI's line wait"). The runs above line 64 were made before it and read
the size the frame began with; the runs below read the new one. Forest, `$BE9A` 87 -> 97 on monitor row f999: cartridge
picture f1000 has 9-line blocks down to row 79 and 10-line blocks from row 80 (the block rows change at 8, 17, ... 71,
80, 89, 99, 109 ...); picture f1001 is 10-line throughout. Leg 95, 27 -> 37: rows 0-139 keep the old size (the runs of
lines 0 and 56), rows 140-211 take the new one.

**The anchor.** The oracle's PPU anchors a BG's mosaic rows on the line of the last `$2106` write (snes/ppu.c:953
`mosaicStartLine = vPos`; :425 `ly -= (ly - mosaicStartLine) % mosaicSize`, :424 `lx -= lx % mosaicSize`). So every run
starts its own grid: the run of line 0 anchors on PPU line 0, the BG line ABOVE the picture, and the first row of blocks
is that line's pixels, one line short on the screen (size 9: rows 0-7, then 8-16 ...); the run of line 80 starts a new
grid at row 80 whatever the size above it. (ppu.c carries a `TODO: mosaic line reset specifics` on that write. The
port matches the oracle, as everywhere; whether the S-PPU restarts the row counter on a mid-frame write is not something
this project can measure.)

**The layers.** Every value the run-off's program writes enables BG1, BG2 and BG3 together (17 ... F7 on the way up,
EF ... 0F on the way down), and the PPU applies the mosaic per BG before priority (ppu.c:423). With all three under one
size the blocks are the arena composite's -- priority tiles included: a canopy is a block like the ground, and it
covers the sprites wherever its block's first pixel is a canopy's. The sprites are never in a block.

**The port.** `CtBattleMosaicRows` (battle.c) walks channel 6's table as the effect layer's compose walks the others,
the line cuts' copies above each cut, and gives each picture row its block size and the row its BG pixels come from
(-1 = the line above the picture, which field.c draws from the canvas at the top row's scroll under the frame's
brightness). `battleMosaicApply` (field.c) replaces each BG pixel by its block's first on the anchored row, before the
effect layer and the sprites; the compose's foreground pass (the priority tiles over the sprites) reads the tile and the
colour of the same block. Before: one size for the whole frame from the live shadow, anchored on the top row, and the
canopies redrawn sharp over it -- 32,000 px of every frame of the forest's rise.

Measured, the forest twin: every whole frame f949-f999 is 0 px at port -1 (the rise 17 -> 97, each size-change frame's
two sizes included). From f1000 the frames on which the enemies' palette does not step are 0 px and the others differ in
the Hetakes' pixels alone: the two fade records are byte-equal on both sides (`$0598` stepping on odd rows, `$05A4` on
even ones), but the cartridge shows the OBJ palettes' step two pictures after its row and the port one -- a frame ahead
of the arena's mosaic within the port's own picture. That is the open item on a fight's lags (sprites and their palette
against what HDMA and the NMI's uploads feed), not the mosaic's.

Open:
* a block whose rows have different scrolls (a shake under the mosaic): the PPU takes the block's first pixel at the
  CURRENT line's scroll; the port takes the anchored row as it was drawn. Not met in a measured frame.
* the line above the picture in a room that draws BG3 planes or a colour window over its canvas (leg 95's mist): the
  port's row is the canvas alone. Leg 95's rise shows 13-90 px a frame at port -1 on rows 121-129, where a sprite is
  seen through the mist -- the sprite's sub-screen mix reads the mist's un-mosaiced pixels (field.c g_objSub*).
* leg 95's size-change frames differ by 1700-3800 px at every lag: the mist's own scroll leads the cartridge's by two
  pictures and the mosaic by one (the same lag item).
* the widescreen's sides (a mod) take the top row for the line above.

Checks: `field-picture-run-off-mosaic-by-the-line-vs-cartridge` (five mutations: the cut, the anchor, the line above,
the priority tiles, the stepper of sec. 10); the forest's frame canaries `field-battle-flee-ranaway-box`,
`field-battle-flee-objfade` and `field-battle-flee-end-forest` re-pinned with these three changes.

## 12. The block a sprite is under (2026-11-14)

Sec. 11's "the sprites are never in a block" is the sprites' own pixels. The BG pixel ABOVE one is a block's like any
other, and in a room seen through BG3 that decides what the sprite is.

Playthrough leg 65, Denadoro (loc 092): the arena's lines are TM 17, TS 13, CGWSEL 02, CGADSUB 64 -- BG3, the mist, on
the main screen with the half add, OBJ on both screens -- and BGMODE 09 puts BG3's priority pixels above every sprite.
Under one of them a sprite shows as the sub pixel: (mist + sprite) / 2. The party runs from a fight (`$A013` 1 on row
f28035, the run-off's tick 281):

    (110,56), a pixel of Marle's back      f28040-f28090  16, 3, 0    her palette's word 0070, no mist over her
                                           f28120          9, 3, 1
                                           f28150-f28180   8, 1, 0    half: the mist's block, faded to black
    (108,72)                               f28090  9,17,15   f28120  6,10, 9   f28150  4, 8, 7

The mist has no pixel at her own place; the block's first column on the anchored row has, and under the mosaic that
pixel -- its layer, its priority, its colour -- is the whole block's (ppu.c:423-425, per BG before priority). The port
built the sub-pixel mask (field.c `g_objSubMask` / `g_objSubTop` / `g_objSubRgb`) from each pixel's own place and drew
her whole: 43 px on f28060 rising to 400 on f28105, 101 on the standing frames to f28229.

field.c, the fight's compose: after `battleMosaicApply` the mask of every row with a block size is read at the
block's anchor, as the foreground pass reads its priority tiles. f28050-f28230, every tenth picture: 0 px.

Check: `field-picture-mosaic-block-makes-the-sprite-its-sub-pixel-vs-cartridge` (one mutation: the mask at the pixel's
own place).
