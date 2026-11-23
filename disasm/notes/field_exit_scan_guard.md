# The map-exit scan's guard is $0117 bit7, not a frame counter

Derived 2026-09-03 from the ROM + the oracle only (defect #11 of the 2026-09-02 list, "when running on the castle
staircase"). Fixture: `saves/field_re/scene468_stairs_to_120.state` and its lakewram -- scene 468 is the
600 A.D. Guardia-castle spiral staircase, three identical stair blocks (CT_COLLDUMP rows 6/7, 24/25, 39/40)
linked by same-scene map exits, the last leading to scene 122.

## 1. The ROM

The exit is staged from INSIDE the movement commit. `C0/9C5C` computes the step's DESTINATION cell
`$66/$68 = pos + velocity $2E/$30`, fetches its props and tests walkability:

```
9C8D  JSR C0/9AC8 / JSR C0/9AD3 / JSR C0/9C37     ; destination props, shape, walkable?
9C96  BCC ->9CA1                                  ; ACCEPTED step -> the door test
9C98  REP #20 / STZ $2E / STZ $30 / SEP #20 / RTS ; blocked: velocity zeroed, NO door test
```

and `C0/9CA1`, reached only that way, has exactly one guard before it reads the door plane:

```
9CA1  LDA $0117 / BIT #80 / BEQ ->9CAB / BRL ->9D2C     ; a transfer is already pending -> skip
9CAB  ... $7E7080 at the destination cell -> the record at $E5:0000 ...
9D24  LDA $0117 / ORA #80 / STA $0117                   ; raise the pending latch
```

There is no frame counter anywhere on that path.

## 2. The port, and the measurement

`src/field.c` scanned the foot tile every frame, so it needed an anti-bounce, and the arrival set
`cooldown = 20` -- twenty frames blind to every exit. Walking, the next flight is more than twenty frames
away and nothing shows. RUNNING at 2 px/frame the lead crosses the one-tile trigger box inside the blind
window:

| | oracle (Right+B, mask 0x81) | port before | port after |
|---|---|---|---|
| warps | 3 | 1 | 3 |
| ends | scene 122 at (896,743) | scene 468 at (160,392), stuck ~380 f | scene 122 at (896,744) |

(the drive mask was measured, not assumed: `--btnscript` bit0 is B = run -- `$00F8` reads 0x0A and the
position steps -0x20 per frame instead of -0x10.)

The fix is C0/9CA1's own guard, read from the byte the ROM branches on: `!(w17[0x0117] & 0x80)` in place
of the counter. `cooldown` survives, renamed `owEnterCd`, as what it actually still is: the overworld
A-press entry debounce.

## 3. What is NOT ported, and why

The other half of the ROM's structure -- the test living on the accepted-step path and reading the step's
DESTINATION cell rather than the foot tile -- is not. Gating the loop on `fieldPlayerCollide`'s return was
tried and REVERTED: the harness's own scripted walks stop exactly on an exit tile, so `--worldtest` never
left the overworld and hung (six overworld checks red). The latch alone stops the re-trigger.

Also still open, and visible in the frame numbers above: the ROM freezes the player for ~21 frames after
each arrival (oracle: the position lands f46, the first step is f67), and the port does not -- so the port
chains the three warps at f18/f23/f28 where the oracle takes until f70 for the second.

Check `field-stairs-run-three-flights`, two mutations killed (the old blind window; the latch replaced by
the `!fade` proxy).

## The door test is the exit plane's, not the asset's spans (2026-11-23)

C0/9CAB-9CF3: the step's cell `($67,$69)` indexes the exit plane `$7E:7080` that C0/A66B builds (the record's tile word
masked by the map's `$1D1E`/`$1D1F`); bit 7 means no exit. The record ($E5, seven bytes, by the plane's index) is taken when
its x byte equals the cell's x and its y is within 3F of the cell's (9CD3, 9CE5-9CF3), or else when its y equals the cell's
y (9CDB-9CE1) -- both compares against the UNMASKED cell. The port compared the lead's foot tile with the asset's exit
spans, unmasked: on a map narrower than a record's coordinate it never matched. MEASURED, the ending's route (eot10 +
qTHE_END, leg e10): loc 134 is 16 wide and its bottom exit's record is x 27, y 2F (masked cells 07-08); the lead steps into
row 2F at f9713 and the cartridge's `$0117` rises there; the port walked on. field.c now reads the plane and the record.
A $C3 stage (the race on 10F, route leg 47b) runs instead of the field's main loop, so no door is tested while one is open.
