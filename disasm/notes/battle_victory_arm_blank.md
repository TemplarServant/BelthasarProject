# The celebration arm draws the PCs: $9907 is never cleared

Derived 2026-09-03 from the ROM + the oracle only (defect #4 of the 2026-09-02 list: "before the battle
celebration animation, crono vanishes briefly for 1 or more frames").

## 1. The ROM

The victory router `C1/8167` reaches `C1/8186 LDA #08 / JSR C1/0003` -> the dispatch table `C1/0051[8]` ->
the stager **C1/356D**:

```
1356D  JSL CD0009 / INC $A0FE / LDX #0007 / STZ $98A7,X / DEX / BPL     ; clear 8 bytes
1357D  LDX #0002 / STX $94 / JSR C1/30B6 / DEC $94 / BPL                ; three seats
13589  STZ $A1 / JSR C1/1BAA / JSR C1/007E                              ; seat 0 queue removal + A WHOLE FRAME
13591  INC $A1 / JSR C1/1BAA / JSR C1/007E                              ; seat 1
13599  INC $A1 / JSR C1/1BAA / JSR C1/007E                              ; seat 2
135A1  LDA #FF / STA $95D5 / JSR C1/0C2D                                ; ...THEN no current actor + repaint
135A9  LDA $2989 / AND #01 / BEQ ->135B1 / RTS                          ; the "no celebration" early-out (0 in every fixture)
135C8  LDA #28 / STA $5DF1,Y                                            ; the +$28 VICTORY window, 3 seats
```

and the shared copier `C1/35DD` writes only `$922D` (dwells), `$9245` (poses) and, at 13665,
`$5DBD/BE/BF = 1/2/3`, `$5DC1/C2/C3 = FF`, `STZ $E5`. **Neither writes `$9907`.** A ROM-wide scan for every
addressing mode of `$9907` finds two writers (`C1/1C66`, `CC/E363`) and one reader, `CF/FC2D`
(`LDA $9907,X / STA $29B4,Y`) -- the OAM stager, which copies the seat's pose with no victory or presence
test. And the driver tail cannot publish immediately:

```
1367D  STZ $E5 / LDA #40 / STA $5DC9
13686  JSR C1/007E / JSL CD0009 / JSR C1/369A / DEC $5DC9 / BNE ->13686
1369A  LDA $E5 / BNE ->136A1 / INC $E5 / RTS      ; the FIRST stepper call only increments
```

so the first publish is two rendered frames after the arm, staggered a further `1+seat` by the dwell.
In between, the ROM keeps drawing whatever `$9907,X` already held.

**Measured** (`saves/gato_t0.state`, A-mash, `--monitor --writer`): the arm is f1985 (`$5DF1` = 28 00 28 00
28 00, writer C1:35D2) and `$9907` = `C6 D9 FF` there and at f1986; the first victory publish is f1987
(`$9907` -> `4F D9 FF`, writer C1:1C69) and seat 1 at f1989. The isolated OBJ layer (`PEEK_ISOBG=4`) draws
520 px of Crono and 419 of Marle on every one of those frames.

## 2. The port

`battleStanceRender` (src/field.c) carried

```c
    int ladderPose = CtBattlePcLadderPose(h, seat);
    if (ladderPose < 0 && (victory || (mem[0x5E2D + seat * 0x80 + 0x1D] & 0x80)))
      continue;                                            // nothing published yet -> draw nothing
```

with `victory = h->victoryHold`. There is no such branch in the ROM. `CtBattlePcLadderArm` sets
`pcLadderPose[seat] = -1` ("the ladder is not the drawing owner", which also gates the hand-offs below it),
so for 2 frames (seat 0), 4 (seat 1) and 6 (seat 2) the seat was skipped by every compose path --
`battleEndPoseRender` is retired, and the hand-off targets all close during `victoryHold`. Both PCs
vanished right before the celebration.

The fix is the deletion of the `victory ||` term: the seat then falls through to its idle stance, which is
the CONTINUITY the ROM gets for free from the unchanged byte. (It is not a model of `$9907` -- the port
has two machines where the ROM has one, and that stays true.)

## 3. What was tried and refuted

Forcing `$95D5 = FF` at the arm -- on the reading that C1/35A1-35A3 is the stager's first instruction --
is WRONG: as the listing above shows, 35A1 comes after the three per-seat removals, each of which runs a
whole rendered frame. Doing it early changes what `C1/1BAA` does with the ready queue (its head came out 1
instead of 255 at the teardown, caught immediately by `field-battle-end-uiteardown`). `battleEndTeardown`'s
existing write, after the loop, is the canonical placement.

## 4. Graded

`battle-victory-arm-draws-the-pcs`: the render with the celebration armed
(`CT_BATTLE_RENDER_VICTORYHOLD=1`) must be BYTE-IDENTICAL to the same render with it unset -- both
`fnv=805D1C93`. That equality IS the ROM statement "the arm changes no drawn byte"; it read `DBAAE213`
(the PC-absent frame) before the fix. Its sibling `battle-victory-hold2-publishes` pins the frame where the
ladder HAS published (`9B62835D`), so the pair cannot be satisfied by a ladder that never publishes.
Three mutations, all killed.
