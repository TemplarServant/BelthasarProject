# Yakra's fight (user defect 2026-09-26 #19, "Yakra did not attack properly")

Graded by `tools/yakra_cmp.py` (check `battle-yakra-fight-vs-logical-oracle`) against the logical oracle.

## 1. The fixture: a Yakra fight staged the ROM's way

No capture holds a Yakra fight. The battle's enemy ids come from the FIELD objects that stand in the formation:
C0/04ED clears the eight records, and C0/0557 fills one per `$9D..$A2` entry, `LDA $1101,Y / STA $7E29C4,X` (record
+5 = the id; writewatch on gato_wait: `$29E8 = 92 by C0/05AC`). GATO is field object 8 (`$1101 + $10 = 92`), so
poking that byte to Yakra's 0x90 on both sides makes the battle load Yakra from the ROM: its stats, its AI (CC:A6FF),
its walk record (E4/F600 + 0x90 * 10), its sheet. The cartridge: `gato_wait.state`, `PEEK_POKE=7E1111=90`,
`--input 100:8,0:3000`; the port: `gato_t0.raw.wram` with the same byte. Port frame + 227 = cartridge frame.

Yakra's AI (CC:A6FF): attack 0 on target 05 twice, an op 00 move, tech 0E (NeedleSpin, all PCs), attack 1 on 05 and
07, a move, tech 4D.

## 2. What was wrong, in the order it showed

1. **Tech 0E did not animate.** Its script CD/9CEB (CD/61F0[0E]) uses op **A2** on its effect tracks; the port had no
   length for it, so the static parse failed and the action fell back to a stored 202-frame span (cartridge 382).
2. **Op 80 stopped two more parses** (attack 1 = `$9877` 30, tech 4D): the parse called the fixed-length table where
   the op is variable-length (C1/621B, `1 + (operand & 0F)`); the live interpreter already used the site form.
3. **The needles slid nowhere.** `12 1E` / `12 21` slide an effect object to ANOTHER effect object's point: C1/762E
   modes 1C-23 -> C1/77EC, `$A2B0 = $A260[k] + $A240[k]`, `$A2B2 = $A270[k] + $A250[k]` (16-bit, read 8-bit at
   C1/50CD / C1/50D3), k = mode - 1C, and object k is track 8 + k (C1/4C14-4C9A set `$A2B6 = k` before the track
   runs). The port answered every 1C-23 with the asking track's own position.
4. **Yakra walked on GATO's clock.** The mode machine's pass counter `$9897` is per enemy (CC/EBE3-EBFA):
   `$989F = CC/F544[((period >> 1) << 2) + (step >> 1)]` from the walk record byte E4/F609[id] (period = >> 4,
   step = & 7) and `$9897 = $989F + slot`; the pass reloads `$989F` (C1/3729). GATO/Hetake 0x41 -> 0x20, Yakra 0x21
   -> 0x10 (cartridge: `$9897 = 20 20 20 13`, CC:EBFD, the first pass f391). The port had 0x20 + slot and 0x20. And
   C1/371F `LDA $974A,X / BEQ` skips the DEC entirely while the mode is 0 -- the forest's mode-00 Hetakes hold 16/17.
5. **The enemy physical staged no effect config.** C1/4497: X = the word `$9853[($AE91 - 3) * 2]` (the id, CC/EC03:
   `$8C` zero-extended) * 6; CD/4926 (`$AE93` = 0) or CD/4F26: `$9877 $987A $987B $987E $9881 $9884`; then
   `$987F = D1:0060[$987E*2]`, `$9882 = CD:6A2C[$9881*2]`, `$9885 = CD:666E[$9884*2]`. Without `$9885` the effect
   load's C1/48EC walked the animation set at CE/0000: the Hetake's object 7 took record offsets (-44, +57) where the
   cartridge's frame-3 record has (0, 0) -- which the old 1C-23 shortcut had hidden.

After 1-5: every edge of the fight up to tech 0E's turn is the cartridge's (f412 569 753 755 922 1106 1290), tech
0E's sixteen track cursors are the cartridge's from its rise, its span 382 on both, attack 1's 205 on both.

## 3. Op A2 (C1/6751) -- walk forward until the arena stops it

No operand. A combatant track: if `$A337` is clear, arm like A9 (`$A34A = $A5AA`, `$96DB = C0:F700[..]`, the start
latched, `$A2EB = 1`, `$A86D = 2`, `$A311 = $A19D = 0`) but with `$A639 = FF` (C1/678F-6797). The visit (C1/679A):
finished (`$A19D`) -> stop; the tile 16 px ahead on `$7BFD` has bits C0 -> stop; else `$A337 = 1` and park (C1/6911
`LDA #00`). Stop = `STZ $A337` + C1/6916 `LDA #01`. No body test, no countdown in the visit (A9 has both). Track 4
(C1/67E2) arms every `$A018` member; tracks >= 8 (C1/6883) the effect object `$A2B6` on `$A342 / $A355 / $A644 /
$A1E0`, its point `$A240 / $A250`. Track 4 is named, not ported (no script this port reaches runs it there).

## 4. The enemy tech's NAME BOX (CD/0033 -> CD/0295) -- ported but for C2/0009's render time

C1/9A3D (the op-02 turn) ends `LDX $B1D2 / LDA $CC0001,X / BEQ / JSL CD/0033` (C1/9B2A); op 0F (C1/9E63) the same.
`$B1D2` walks the record: +1 the tech, +2, then +1 for each zero second-target byte (C1/9A9C, C1/9AC2), so
`02 0E 05 00 00 06` names message 06. The attack turn (C1/99BE) posts none. CD/0295: `INC $CD1D`, `$020D/F = CC:CBC9`,
`CMP $CE17 / BEQ RTL` (the same name still up: nothing more), `$CE18 = id`; CD/025E (`$020C`, `STZ $CCED/$02A1/$0214`,
`$0210 = 7E:B400`, CD/0239 clears `$B400-$B7FF`, `JSL C2/0003`); CD/027F (`$0213 = 40`, `JSL C2/0009`, two CD/3E7D
waits, `$CCEC = 10`, `INC $CA24`); `LDA #08 / JSR CD/3E3B` (eight CD/3E75 waits); `$CCEB` = the message speed's dwell
(CD/01A5); `$CE17 = $CE18`; `INC $CCED`; one CD/3E75; RTL. D1/E7EC counts `$CCEB` down in the NMI and closes (`$CD1D`
set -> `$CE17 = FF`; `STZ $CD1D / $CCEC / $CCED`; `INC $CA24`). Table CC:CBC9: 06 "NeedleSpin / Yakra's needle
attack.", 07 "Naga-ette's......... / Slow spell!", 09 "Drrrrrooooo...".

Measured (the decision frame D): `$CCEC = 10` at D + C + 2, `$CCEB / $CE17` at D + C + 10, C1/4058 at D + C + 11,
`$A013` at D + C + 13, C = the frames C2/0009's render spans: **3 for message 06, 0 for 09**.

**C is CPU time.** On the logical oracle the render is one call that runs until done (`$0213 = 40`); message 06 is
1,002,430 main-thread cycles (+ 341,844 of interrupts: three NMIs, ~114k each) and message 09 308,570 -- the valve
(oracle_logical_frames.md §2) lets a held frame finish work under 357,368 cycles and rolls back otherwise, so C = the
failed holds: 1,002,430 - n * (F - I) < 357,368 gives 3 with I = 114k, and 309k finishes in its first hold (0). A
price needs W (the VWF walk -- src/menu_cost.c prices C2/58B2 -> C2/5DC4 already, but not the dictionary codes, which
switch to `$30 = 1` at C2/58D4-58FA and run C2/5BF5..5C3C) AND I (the battle NMI's cost in each spanned frame).

**Ported (2026-09-26 b):** battle.c `battleNameBoxPost` (C1/9B2A's read in `battleAiTurn`, CD/0295's head and CD/025E's bytes, posted at the think arm) and `battleNameBoxFrame` (the 11 waits: the band on the 2nd, the dwell on the 10th), D1/E7EC's `$CE17 = FF`, the boot's `$CE16/$CE17 = FF` (D1/F28A-F28D); field.c draws CC:CBC9[`$020C`] (`fieldCtDecodeAt`, the `$DE:FA00` dictionary) in the message window while `$CCEC = 10`. Graded: post/band/dwell/close 0/2/10/170 from the turn against the cartridge's 0/5/13/173 -- C = 3 unpriced. Not ported: op 0F's message-only turn (C1/9E63 -> no action, the box still holds the turn), a name after a non-zero second-target byte (C1/AC14), the reactive counter's techs.
