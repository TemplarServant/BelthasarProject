# The wait ops count on the object's own counter

Derived 2026-11-12 from the ROM and the oracle (playthrough leg 78, Reptite Lair's holes: the room's exit came 24
frames early in the port; found while following the whole-run sound audit).

## 1. The routine

Op AD (`AD nn`) is C0/2F96; the four fixed lengths B9 / BA / BC / BD (4, 8, $10, $20) are C0/2FC6-2FF9, the same body
with the length in A (`romdis.py 002F96`, `002FC6`):

```
2F96  TYX / INX / LDA $7F2001,X           N, the op's byte
2F9C  LDX $6D
2F9E  CMP $7F0A01,X / BEQ ->2FAF          the count IS N   : zero it, X past the op, SEC   (the visit goes on)
2FA4  BCS ->2FBA                          N above the count: INC it, `TYX / CLC`          (the visit ends on the op)
2FA6  LDA #00 / STA $7F0A01,X / TYX / SEC the count ABOVE N: zero it, X = the op, SEC     (the op again, same visit)
```

The counter `$7F0A01,X` belongs to the OBJECT, not to the op. A wait cut short -- a call takes the object to another
function -- leaves its count behind, and the next wait the object meets starts from it:

- a count below the new N: the wait is that much shorter;
- a count equal to N: it ends on its first visit;
- a count above N: the third arm -- the counter is zeroed and the wait runs whole.

The port's `s->wait >= N` ended the wait in the third case.

## 2. The case

Loc 11C, object 13 (the holes' controller, period 4): its idle loop sits on `AD 20`. A hole's touch calls it at leg
frame 667 and it runs `06 00 13 / AD 06 / 06 02 13 / AD 06 / 06 04 13 / AD 06` -- the three characters' fall
(function 3 of each, at priority 1), a wait after each -- then `E0`, the room's exit. The oracle's event PC of object
13 (`$119A`): 052C at f667, 053B (the first wait) f671-f698, 0540 f699-f722, 0545 f723-f746, 0569 at f747; the fade
starts at f748. Twenty-eight frames, then twenty-four twice: the first wait took the third arm (the idle wait's count
was past 6) and counted six visits like the others.

The port: 053B for four frames (one visit), so 0540 at f672 and the exit 24 frames early; with the arm, f696 / f720 /
f744 against the cartridge's f699 / f723 / f747 -- what is left is the three frames of the controller's cadence phase,
which the room's load leaves one frame apart (the load clock's class).

Check `leg-window-wait-starts-over-above-its-count-vs-cartridge`.

## 3. The same ops under C0/595C

The controller's function 1 at a load (C0/56C8 -> C0/595C, `obj0_slot1_runner.md`) is dispatched op after op with the
carry ignored, so a wait there never yields: it is dispatched again until its count is N. See that note's sec. 7.
