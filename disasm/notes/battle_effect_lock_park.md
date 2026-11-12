# The effect lock parks the scene's colour cycles

Derived 2026-11-12 from the ROM and the oracle (the whole-run picture audit: playthrough legs 117 and 121, a lit floor
whose edge was the wrong colour on most frames of a fight and of the room after it).

## 1. The routine

D1/F426, `JSL`'d by the battle's frame service every frame (CD/09BB, before the colour-slot service FD/E39C):

```
D1/F426  LDA $CFFF / BEQ ->F457               the lock: an anim script's ops 2D / 2E, a program's E6
   F431  LDA $CE12 / BNE ->RTL                 (once a lock)
   F436  INC $CE12 / JSR D1/F411               the eight modes $0520 + 12k -> $CD2F + k
   F43C  X = 0
   F43E  LDA $0520,X / BMI ->F449              bit 7: parked
   F443  AND #F0 / CMP #10 / BNE ->F44C        1x: parked
   F449  STZ $0520,X
   F44C  X += 0C / CPX #0060 / BNE ->F43E      eight records
   F457  LDA $CE12 / BEQ ->RTL / STZ $CE12 / $0520 + 12k := $CD2F + k     the lock dropped: the modes back
```

A record whose mode has bit 7 is parked like the 1x ones. Those are the scene's colour cycles (`8B 38 04 07 ..`,
`87 62 03 01 ..`: a floor's glow, a wall's lights) -- the fight keeps the field's records running (FD/E39C under its
NMI), and an effect that takes the lock stops them for its length; they go on from the count they stood on.

## 2. The case

Zeal, loc 195 (leg 117's second fight). An action whose script holds the lock runs at leg frame 7364 (the port's
trace: `anim-coverage $9877=0A atk(CD/5FF0) CD/706F .. ops: .. 2D 2E ..`). The oracle's picture of the floor (rows
208-223, a hash a frame): one cycle step every 16 frames up to f7357, the action's 15-frame fade f7364-f7378, then ONE
picture held to f7485 -- 107 frames -- and the cycle again, 16 frames a step.

The port tested `!(mode & 80) && (mode & F0) == 10`: the cycles ran through the lock, eleven frames after the fade the
floor stepped again, and from the lock's end the cycle stood 15 frames from the cartridge's for the rest of the fight
and of the room (731 px on the frames where the two steps differ). With the BMI arm the frame 75 after the lock is the
cartridge's to the pixel, and the audit's samples of legs 117 (first 12000 frames: 35 of 39 at 0 px, from 21) and 121
(first 6000: 15 of 20, from none) show it.

Check `field-picture-effect-lock-parks-the-colour-cycles-vs-cartridge`.

## 3. Open

The action's own effect sprites are not drawn by the port during the lock (a glow on the caster at f7400): the anim
script's ops 72 76 78 80 A2 of CD/706F.
