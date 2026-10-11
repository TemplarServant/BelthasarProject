# The RETURN op keeps the visit going: C0/5F74 and Lucca's first descent

Derived 2026-09-03 from the ROM + the oracle only (defect #13 of the 2026-09-02 list: "Lucca does not trigger
when leaving the castle staircase in 600AD, I need to go back up"). Every claim cites `tools/dis.py`
(bank C0 = the file offset) or an oracle measurement on the fixture below.

## 1. The fixture and the ROM's timeline

The castle-corridor cutscene is scene 120 (obj17 = Lucca); the user comes down the stairs from scene
468, whose exit at (7,39..40) lands at (37,9) facing left (`assets/maps.scene_exits`). Fixture: the
check's own flags (`saves/s120_lucca_flags.bin`: storyline $10, glb[169] = 2) grafted into
`forest_exit.state` at WRAM_OFF+$10000, then `--teleport 468 8 39 2`. Drive: Left 60, idle 120, Down 200.

`--monitor 7E0100:2,7E0129:1,7E011F:1,7E1803:1,7E1883:1,7E11A2:2,7E1C22:1`:

| frame | oracle |
|---|---|
| f9   | the exit tile (7,40) entered; f25 loc = 120 |
| f53  | the lead at (37,9); obj17 ip 0A64 (`8B 25 0B`: Lucca placed at (37,11)), f56 ip **0A68** and it stays there |
| f69  | obj0's PC-tile mirror loc[9]/loc[10] := (37,9) (op 0x22 in obj0's loop; 16 frames after the arrival) |
| f189 / f197 | row 10 / mirror 10 (8 frames later) |
| f205 | row 11 -- the player has walked INTO Lucca |
| f206 | obj17 ip 0AFD, `$1C00,X` = 2: the TOUCH invoke (C0/3154) ran her fn2, a bare RETURN |
| f207 | ip 0AF6, prio 7: the RETURN restored the idle continuation 0A68 and **ran on** through the poll loop |
| f208 / f209 | touch again / RETURN again; the mirror reads 11 at f209; frame-end ip 0AA2 |
| f210 / f211 | touch / RETURN, and `C1 55` at 0AAB posts the box: `$0129 = 1` at f211 |

Lucca's fn0 (the WRAM script image at `$7F2001 + ip`, opcode lengths from assets/script.py):

    0A64  8B 25 0B          place at (37,11)
    0A67  00                RETURN -- the init; the idle continuation is 0A68
    0A68  18 10 05          storyline >= $10 -> 0A6F
    0A6F  18 12 89          storyline >= $12 -> 0AFA
    0A72  16 A9 01 06 2C    glb[169] bit 0 clear -> 0AA2 (branch B)
    0AA2  12 0A 0B 00 50    loc[10] != 11 -> 0AF6
    0AA7  87 01 / E3 00 / C1 55 ...   the cutscene: control off, "Crono!"
    0AF6  11 8F             back to 0A68
    0AFD  00                fn2 (touch) = RETURN

So the trigger is "walk into Lucca": an idle object (`$1C00,X` = 7) is never scheduled on its own --
its ip sat on 0A68 for 150 frames -- and only the touch's RETURN lets the continuation run.

## 2. The RETURN handler, C0/5F74 (op 0x00)

```
5F74  LDX $6D / LDA $1C00,X / CMP #07 / BEQ ->5FB3        ; an idle object's RETURN: 5FB3 TYX / CLC / RTS (yield)
5F7D  ... $D9 := $1C00,X * $80 + $6D                       ; this level's saved-ip slot
5F90  LDA #0000 / STA $7F0580,X                            ; clear it
5F97  TXA / CLC / ADC #0080 / SEP / LDX $6D / INC $1C00,X   ; climb one level...
5FA3  REP / TAX / LDA $7F0580,X / BEQ ->5F97               ; ...until a level holds a saved ip
5FAC  LDX $6D / TAX / SEP / SEC / RTS                      ; X = that ip, CARRY SET
```

and the runner (C0/5AA3-5AC4): `JSR ($5D6E,X) / 5AB4 BCC ->5ABA / 5AB6 DEC $67 / BNE ->5AA3` -- carry set
means "advance", so after a RETURN the SAME visit keeps executing at the restored ip while the visit
budget `$67` lasts. That is the five-op run from 0A68 that leaves the frame-end ip at 0AF6 (f207) and
0AA2 (f209). The invoke prologues (C0/3154 touch, 5AC5 activate) store the interrupted ip into the
interrupted level's slot (`$7F0580 + level*$80 + X`), so an idle object's slot 7 holds its continuation.

## 3. The port

`ct_vm.c`'s RETURN restored the next saved level and then **yielded** (`return 1`). A prio-7 object is
not scheduled on its own in the port either, so between the touches (every other frame while the
player pushes) Lucca's poll loop never ran; her box came only when the pushing stopped (f380+ on this
drive) -- the user's "I have to go back up". The RETURN now returns 0 after restoring a level (the idle
RETURN, `$1C00 == 7`, still yields), the box posts at f210 (the CT_FIELDLOCK harness injects our input
one frame ahead of the ROM's own read: its loc change is f24 to the oracle's f25), and the `box`
projection reconverges. Check `lockstep-scene120-stairs-entry-lucca`; mutation = the old yield, killed.

The music half of #13 ("and the music stops") did not reproduce on this path: `CT_PROJECT=song` is CLEAN
over the whole entry. Open.
