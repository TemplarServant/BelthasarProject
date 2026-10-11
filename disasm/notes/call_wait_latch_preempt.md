# Op 0x04 / 0x07: the caller's wait latch survives a pre-empt (the tent of horrors' freeze)

The user, 2026-10-08: "after winning the mimicry game in the tent of horrors, it got stuck" (build/ct_debug_020.state,
loc 0x1B2, scene 434, script 71).

## 1. The ROM

Op 0x04 (call obj fn and wait) is `C0/6109`:

    6109  LDX $6D / LDA $7F0980,X / BEQ ->611A      ; the CALLER's latch: 0 = fresh arm, else BRL ->61A7 (poll)
    611A  ... target = operand / 2; its $1100/$1000 bit7 -> 6114 (skip, SEC)
    6136  LDA $1C00,X / STA $D9                       ; the target's current priority
    6142  operand 2 -> $DD (fn | prio<<4), prio -> $DF
    614A  CMP $D9 / BEQ ->61A4 / BCS ->61A4           ; target busy at that priority or more urgent: park, no install
    6150  $E1 = prio * 0x80 + target*2                ; the return slot
    6168  LDA $1180,X(target) / STA $7F0580,X(slot)   ; PUSH the target's current IP (whatever byte it is parked on)
    6186  LDA fnptr / STA $1180,X ; $1C00 := prio ; STZ $1A80 ; STZ $1A01
    619C  LDA #01 / LDX $6D / STA $7F0980,X          ; ARM THE CALLER'S LATCH
    61A4  TYX / CLC / RTS                             ; park on our own byte (the IP stays on the 04)
    61A7  poll: target's $1C00 vs the called prio (61D2 CMP $D9 / BEQ / BCS ->61E6 keep waiting)
    61D8  LDA #00 / STA $7F0980,X(caller) / INX x3 / SEC   ; released: past the call, one dispatch spent

The latch is ONE byte per object and only op 0x04/0x07 reads it, on its own byte. A higher-priority call that
pre-empts a waiter pushes the waiter's $1180 -- the parked 04 itself -- and runs the new function from its first op
with the latch still 1. Nothing in the new function looks at the latch (unless it executes a 04/07 of its own, which
then goes straight to the poll arm on its own operands: a ROM quirk the port does not model). The RETURN (op 0x00, the
first slot above the priority) lands back on the 04, which polls again.

## 2. The scene

- obj11 (Bekkler) fn1, the talk: `0590: 04 00 33` -- CALL obj0 fn3 (the mimic game) at priority 3, wait. Latch = 1.
- obj0 fn3 after the win (21 rounds): `008E: 04 14 37` (obj10 fn7), `0091: 04 16 03` -- CALL obj11 fn3 at PRIORITY 0,
  wait; `0094: 04 14 35` (obj10 fn5, the prize), `0097: AD 08`, `0099: E0 05 02 2B 1E` (back to the fair).
- obj11 fn3 `062F: 89 40 / 96 08 06 / 00` -- he steps up to tile (8,6) and returns.

Measured on the rule's cartridge (saves/field_re/s1b2_mimic_won_lake.state, `--monitor`, f = after N+1 frames):

| f | obj0 | obj11 | latch11 | ret slot 1 (obj11) |
|---|---|---|---|---|
| 0 | 008A | 0590 p1 | 1 | -- |
| 79 | 0091 | 062F p0 | 1 | 0590 |
| 82-110 | 0091 | 0631 p0, tile (11,9) -> (8,6) | 1 | 0590 |
| 114 | 0091 | 0590 p1 | 1 | 0590 |
| 115 | 0094 (released) | 0590 | 1 | |
| 247 | loc 005 | | | |

## 3. The port's defect and fix

The port advances the IP past a waiting call (s->addr = next op, callAddr = the call) and kept the wait as a state
(`waitObj`) polled at the top of every visit. Pre-empted, obj11 had s->addr = 062F at priority 0 but its fn1 wait still
armed: step() polled obj0 (busy at priority 3) and returned without running fn3, while obj0 waited on obj11 at
priority 0. Both waited for good.

Fix (src/ct_vm.c `ssParked`): the wait is polled only while the object is PARKED on its call, i.e. s->addr is the byte
after callAddr -- the port's form of "the IP is on the 04". A pre-empt stashes that address in the return slot (reqOp
already did), so the new function runs, and the RETURN brings the object back to the parked address, where the poll
resumes. The $1180 mirrors (CtSceneObjIp, the --vmdiff compare, the per-visit write-back) use the same test, and the
WRAM seed rebuilds a latch whose 04 sits in a return slot (the first slot above $1C00) instead of the IP.

Graded by field-tent-mimic-won-bekkler-preempt-vs-rule-cartridge (tools/tent_mimic_cmp.py): 7/7 events on the
cartridge's frame; the mutation back to "poll every visit" leaves every event after the call 'never'.

Not modelled: the $1C00 mirror of the C-side priority (the port's $1C00 bytes keep the seed's value; the per-object
priority lives in CtSS), and the in-function 04-with-latch quirk above.
