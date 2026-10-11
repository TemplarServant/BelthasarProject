# The battle module's end tails (C1/83BB-845E) -- on the module's frames, not the field's ack

Status: PORTED (battle.c `CtBattleVictoryRewards`, `battleModuleTail`). Before this, the port ran all of it in
field.c `fieldBattleFinish`, i.e. at the field's ack ($0117 bit 0 clear, C0/18B9), after the whole exit tail CD/03B4.

## 1. The ROM (dis.py, X 16-bit: `python tools/dis.py 0183BB 0x8a 1 0`)

Every fight that hands back to the field leaves through one of these chains. There is NO wait anywhere in them, so under
the rule (the infinitely fast CPU) each lands on ONE frame -- the frame its caller reaches it.

| Caller | Chain |
|---|---|
| victory `C1/818B JMP C1/83BB` (after C1/8186 `#08` -> C1/356D, the celebration) | 83BB-83CF `$5E32/$5EB2/$5F32 := $B316/$B318/$B31A`; 83D2 `LDA $2989 / BIT #20 / BNE ->8423` (a demo: no reward); 83D9 `LDA $2880 / CMP #00 / BNE ->83F5`; 83E0 JSL FD/B201; 83E4 `LDY $B2A5 / JSR C1/D0A2` (gold); 83EA `$B2AF |= 40`; 83F2 JSR C1/F93E (XP, levels, TP, techs); **83F5 JSL FD/AD17 (the results messages)**; 83F9 JSR C1/EAE8; 83FC JSR C1/B442; 83FF JSR C1/B4E9; 8402-841F C1/D005 for each of the six `$B2A7` drops (`$B2AF |= 20`); BRA 8443 |
| defeat `C1/8164 JMP C1/8423`, demo `C1/83D2 -> 8423`, flee `C1/821E JMP C1/8423` | 8423-8434 the same `$B316` copy; 843A JSR C1/EA9D; 843D JSR C1/EAE8; 8440 JSR C1/B442; falls into 8443 |
| flee, before that: `C1/8212` | JSR C1/EAE8, JSR C1/B442, JSR C1/B4E9, JSR C1/B3BB, JMP C1/8423 |
| debug end C1/8357 (also the port's frame cap) | ... 83A3 JSR C1/B3BB, `LDA #06 / JSR C1/0003`, 83AB **JMP C1/8443** (skips EAE8 / B442) |
| all | 8443 `LDA $26 / STA $29AD`; 8448-8459 `$5E56/$5ED6/$5F56 := $1C48/$1C4D/$1C52`; 845A JSL FD/AAB0; 845E JMP C1/0006 -> C1/001F -> JML CF/FBE5 |

CF/FBE5: `$A46D` cleared and each PC seat's 24 bytes `$2102 + 16*$993C,X` -> `$2302`; FC08-FC71 the staging records
`$29B0..` / `$29BF..` from the live feet and `$9907` / `$990A` (CtBattleStagerRecords); FCB5-FCCC frame waits (CF/FCFB
spins on `$9E`) until `$A4 == 3`; FCCF-FCE4 `$2603,X` (CC/FC1E's list) bumped from 0 to 1; FCE9 JSL CD/0006 -> CD/03B4,
the exit tail (CD/0453, CD/039E, CD/0516, the band, CD/043E `JSL C0/000B`); then the RTL back to C0/18A7.

Also: the victory scan `C1/8183 JSR C1/EA9D` and the defeat check `C1/8137 JSR C1/EA9D` clear the PCs' status bytes
(+$1E..+$26, +$1D bit 7 kept, +2 bit 0, `$AE51`) on the check's frame -- before C1/B442 carries the blocks to the sheets.

FD/B201: `LDA $B3BD / BEQ` -- with `$B3BD` set (the count of accessory A9 among the seats, battle.c), `$B2A5 += $B28C`
(the gold reward += the XP total), `$B28C := 0`, `$B2AF = $B2AF & 7F | 40`. FD/AAB0: `$7F01EE` := the bit mask of the
eight `$AE85,X != 0`.

## 2. Measured (the rule's cartridge: CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1)

- The attract's 07D fight, `--monitor 7E29AD:1,7E5E4A:1,7E2603:2,7E29B0:5,7E0117:1,7EA013:1,7ECA24:1 --from
  saves/attract_07d_battle_f4540.state --diff --writer`: `$A013 = 0` (C1:41BD), `$29AD = 54` (C1:8448), `$5E4A` (C1:EB05),
  `$2603` (C1:B475), `$29B0 = 070A08014F` (CF:FC33) all at **f575**; `$CA24` (CD:03EB) f615; the ack (C0:18BD) f631.
- Magus's Black Hole kills GATO (`tools/battle_ab.py`'s drive `tech 6 6`, the regions `$29AD, $5E40:3, $27E3:2, $29B0:5,
  $0117, $020C, $CCEC`): the victory f1355; the block's XP +$13 `017DF4 -> 017DFE` at **f1422** with the first message
  `$020C = 0` (the box `$CCEC = 10` at f1424); the last box shut f1794; `$29AD = 33`, Magus's sheet curHP `$27E3 28A ->
  286` and the records at **f1810** (= 1794 + 16, CD/020A); the ack f1863.

The port before: XP, `$29AD`, the sheet and the records at its ack, field f1846 (36 frames after the cartridge's tail,
424 after its XP); the 07D demo's at its ack too (56 frames late). After: the same fight -- XP f1426, the tail f1813;
the 3-4 frames left are the results machine's own (the port opens the first box 4 frames after `$020C` and dismisses 3
late), not the tail's.

## 3. The port

- `CtBattleVictoryRewards` (C1/83D2-83F2): on battleResultsTick's first frame past the celebration -- the gold, XP,
  levels, TP and techs, gated as 83D2/83D9 (no demo bit 5, `$2880 == 0`) and on a victory.
- `battleModuleTail(h, path)`: 0 = the victory's 83F9 (EAE8, B442, B4E9, the drops), 1 = 8423 (EA9D, EAE8, B442: the
  defeat, the demo), 3 = the flee's 8212 + 8423, 2 = 8443 alone (the debug end); then `$29AD` and the records. Called on
  the frame the chain is reached: `victory-dismiss` (FD/AD17 returns), `defeat-end` (C1/8164), `run-off-end` (the flee's
  action loop done), `demo-end` (C1/83D2), `end-debug`.
- `battleStatusClearEA9D` (was battleFleePcClear) now also runs at the victory scan (C1/8183) and the defeat check (C1/8137).
- fieldBattleFinish keeps the field's own side (the ack, C0/0283, C1/B3BB's `$29C6` mask, the epilogue) and traces
  `battle-module-tail NOT RUN before the ack` if a path reached it without the tail.

## Open questions

- FD/B201 (the A9 accessory's XP-to-gold) and FD/AAB0 (`$7F01EE`) are not modelled.
- C1/B3BB runs on the module's frames too (victory C1/8176, defeat 813A, flee 821B, debug 83A3); the port still runs it in
  fieldBattleFinish -- nothing reads `$29C6` in between, so it is not observable, but it is not the ROM's frame.
- CF/FBE5's head (`$A46D`, `$2102 -> $2302`) is not modelled.
- (PORTED since, see section 4) CF/FCCF-FCE4: `REP #20 / LDY #000C / TYX / LDA $CCFC1E,X / TAX / LDA $2603,X /
  BNE / INC $2603,X / DEY / DEY / BPL` -- CC/FC1E = `0000 0050 00A0 00F0 0140 0190 01E0`, the seven sheets: a PC whose
  curHP word is 0 leaves the fight with 1. It runs after CF/FBE5's waits (FCB5-FCCC: CF/FCFB frames until `$A4 == 3`,
  then one more), just before `JSL CD/0006`; their frame count is not measured yet. The port leaves such a PC at 0.
- The results machine opens 4 frames late and dismisses 3 late against the cartridge (battleResultsTick), which the tail
  inherits.

## 4. CF/FBE5's waits, CF/FCCF and CD/03B4 (ported 2026-10-03)

- CF/FBE5 after the records: `FCB5 STZ $E5 / wait / INC $E5 / wait / LDA $A4 / CMP #03 / BNE ->FCB5`, then `STZ $E5 /
  $0900 = FF / wait`, then CF/FCCF-FCE4 (CC/FC1E's seven sheets: a curHP word of 0 -> 1) and FCE9 `JSL CD/0006` -> CD/03B4
  -- battle.c battleFbe5Begin / battleFbe5Step / battleFbe5HpFloor. $A4 is stepped only by the NMI's CF/E477 on $E5
  frames, so $A4 / $E5 had to be the ROM's through the victory: the copier C1/367D (`STZ $E5 / STZ $A4`), C1/369A, 1370F-
  13711 and C1/3695 write them, and the main loop's idle tick stays out of the victory (`$0027` holds on the cartridge).
- MEASURED (battle_ab Dark Matter on GATO, Marle fallen beside Magus): the tail f1777 with `$A4 = 0`, `$A4` 1 / 2 / 3 at
  f1779 / 1781 / 1783, Marle's `$2653` 0 -> 1 and CD/039E's `$1E01 = 10` at f1784, `$CE16` f1799, `$CA25` 0C f1800 / 0E
  f1813 / 08 f1814, the ack f1828. The port: f1780, 1782 / 1784 / 1786, f1787, 1802, 1803 / 1816 / 1817, the ack f1828.
- The victory now enters the run-off's CD/03B4 machine at its e == 5 (CD/039E's fifteen waits, CD/0516, the band). The
  flee / demo exits keep their measured e == 5 and get CF/FCCF there; their CF/FBE5 loop is not yet the $A4 machine.
- OPEN: the defeat's CF/FBE5 / CD/03B4 (the port's defeat-end hands back at once); the 3 frames the results machine adds.
