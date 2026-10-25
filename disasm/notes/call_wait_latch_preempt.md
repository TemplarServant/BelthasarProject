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
priority lives in CtSS). The in-function 04-with-latch quirk is modelled since 2026-10-19 (sec. 4).

## 4. The 04 met with the latch set (the Telepod's double raise)

The user, 2026-10-19 (translated from Portuguese): "on climbing the pod in scene 8, picking up the pendant to go after
Marle, sometimes Crono does the arms-raising animation, lowers them and raises them again". Their guess -- the port fires
the event on both the touch and the A button where the cartridge only on the touch -- is not it: obj11's fn1 (A) and fn2
(touch) are the SAME code (`ct.py script 8 --obj 11`: `1/2 @0x028A`), and the cartridge runs it from either.

### The ROM

`C0/6109 LDX $6D / LDA $7F0980,X / BEQ ->611A / BRL ->61A7`: with the latch set the op skips the install and goes to the
poll, which takes the target (61AB) and the priority (61C6-61D0) from THE OP IN HAND. 0x07 the same (C0/6214 `BNE ->6232`,
6236 `LDA $7F2001,X / TAX / LDA $97,X` with no empty-slot test, `BRL 61AF`). Poll: the target's $1100 / $1000 bit 7 ->
released; else wait (61E6 `TYX / CLC`) while the priority >= the target's $1C00; released = `STA $7F0980,X` 00 (61D8-61DC),
`INX x3 / SEC` (61E0). The pushes (C0/6005 / 60CC / 6168, the invokes C0/5AC5 / C0/3154, C0/5B41, C0/32E8) store $1180,X,
which for a parked waiter is the 04's own byte; none of them touches $7F0980.

### The scene

obj11 fn1 = fn2 (`0x028A`): `EB 30 00 / CA D6 / 04 06 36` (Crono's fn6 at priority 3: `E3 00 / B7 20 06`, the raised
arms, anim 32 held while $7F0B07 counts 5 -> 1, ~21 frames) `/ 91 / 04 06 47 / ...`. Measured on the rule's cartridge
(saves/scene8_duas_animacoes.state, spliced from the user's snes9x capture over saves/field_re/scene8_gate_cutscene.state;
`--monitor 7E1196:2,7E1186:2,7E1786:2,7E1686:2 --input "10:21,110:1,10:3,0:120"`, Up held, A on the 22nd frame):

| f | obj11 ($1196) | obj3 ($1186) | |
|---|---|---|---|
| 16 | 062A | 0497 | the touch installs fn2 (priority 2) |
| 21 | 062F (on `04 06 36`) | 04AA | the call installed, latch 1 |
| 22 | 062D | 04AC | A: fn1 (priority 1) pre-empts, ret[2] = 062F; fn1 runs `EB` |
| 26 | 062F | 04AC | fn1's own `04 06 36`: latch set -> POLL (obj3 at priority 3: wait) |
| 43 | 062F | 0497 | Crono's fn6 returns |
| 46 | 0633 | 0497 | released: latch 0, past the call -- ONE raise |

The port installed fn1's call afresh: it waited on the equal priority until f43 and then ran Crono's fn6 again, f47-f68.

### The fix (src/ct_vm.c)

- The op 0x02-0x07 arm: a 04/07 with `s->waitObj >= 0` (the port's latch) polls on its own operands (ssCallBusy, the same
  verdict as the parked poll), parked on itself; released -> latch cleared, IP past the op, the visit goes on.
- ssPushAddr: every pre-empt pushes the ROM's $1180 -- for a parked waiter the 04's byte (callAddr), not the op after it --
  so the RETURN lands on the call and the op decides on the latch (poll, or a fresh install if the pre-empting function's
  own 04 released it). The WRAM seed keeps a pre-empted waiter's return slot on the 04 for the same reason.
- CtSceneBattleReturn (C0/5B1F) and the D9/DA release (C0/32C8) no longer clear the latch: neither touches $7F0980.

Validated: 106 pad variants from the capture on both sides (Up/A timings, held A, diagonals, side steps; the obj3/obj11
IPs and Crono's anim bytes): the three A-on-f22 variants went from Crono's fn6 run twice on the port (once on the cartridge)
to the cartridge's sequence; the other 103 are unchanged.

## 5. The install drops the lead's step (a lateral press on the Telepod touch)

Found 2026-10-19 by a 106-pad sweep of the same capture: Left or Right held around the touch made the port's Crono end
fn7's MOVETO (`96 04 05`) a step late and obj11 leave its `04 06 47` 4 frames late (f77 against f73).

### The ROM

The lead's step is computed before the event pass (C0/00A7 `JSR C0/881E`: d-pad, collision C0/8A6D -> `$1D32/$1D33`) and
moved after it (C0/00D3 `JSR C0/A810` -> AAFD: `AB06 LDA $1A01,X / BEQ ->AB0B RTS`, else `AB10-AB3F $1800 += $1900`). In
between, the lead's own op B0 ARMS it: C0/3040 `LDA #01 / STA $1A01,X / JSR C0/9E29` (9E53/9E5D copy the velocity into
`$1900/$1980`). Every install on an object ends `STZ $1A80,X / STZ $1A01,X` (sec. 1: C0/6034 / 60FD / 6199; op D9 32FE;
C0/3526, C0/5B5C, the invokes C0/5AC5 / 3154). So an object visited AFTER the lead in the same pass that installs a
function on him drops the step: A810 does not move him that frame, and the velocity stays in `$1900` unused.

MEASURED on the rule's cartridge (`--monitor 7E1A07:1,7E1906:1,7E1806:2 --writer`, pad `10:20,40:5,10:20`): f18-f20
`$1A07` by C0:AB10, `$1806` by C0:AB28 (the walk up); f21 -- the touch -- `$1906 = F0` by C0:9E56, `$1A07 = 00` by
C0:619C (obj11's fn2, `04 06 36`), `$1806` stays 3704 with no AB28. The F0 sits in `$1906` until fn7's MOVETO.

### The port's defect and fix

The host (field.c, fieldPlayerCollide with applyMove) moves the lead BEFORE CtSceneTick and wrote the position into the
VM, so the step always landed: `$1806 = 2704` from f21. Fix: ct_vm.c `vmTicketStop` is the invokes' `STZ $1A80/$1A01` on
another object (reqOp, op D9, the regroup calls, the activate and touch invokes) and drops the lead's ticket the B0 lead
arm records (`g_leadTk`, the 3040 store); the host hands the step's origin over (`CtSceneLeadStepFrom`), and the tick puts
the lead back there right before the integrate leg (after the party trail, whose push C0/9E3F runs inside B0 and so sees
the step) -- C0/A810's `BEQ`. field.c re-reads the lead's words after the tick (`CtSceneLeadStepDropped`). The camera
request is C0/881E's and stands, as on the cartridge.

Graded by field-telepod-lateral-touch-drops-the-lead-step-vs-rule-cartridge (tools/telepod_step_drop_cmp.py): Left and
Right, four events each on the cartridge's frame and Crono's `$1806/$1886` identical on 120/120 frames (5/10 before).

## 6. The lead walks only on his own B0's ticket (the general rule)

Sec. 5 is one case of a rule: C0/A810 moves the free lead only on the `$1A01,X` his op B0 arms in this pass, and nothing
else arms it for him. Simulated 2026-10-19 on seeds of the same capture (PEEK_POKE on both sides, Left held):

- **B2** (`$1186` = 0473): C0/3543 `TYX / CLC / RTS`, no arm -- the cartridge never moves him; the port walked.
- **Period 4** (`$1006` = 04): the queue build C0/59F0 enqueues him one frame in four -- the cartridge steps on those
  frames only; the port stepped every frame.
- **Song upload mid-walk** (obj4 on obj11's `EA 18`): the posting frame's B0 arms (`$1A07 = 01` by C0:3047, no AB28),
  the JSL parks the pass, and A810 integrates on the return frame (f44, AB10/AB28, one step). The port already matched:
  the host does not move a parked lead (CtSceneMainLoopParked) and the posting frame's step commits on the resume.
- **Install before his B0**: impossible -- the queue PREPENDS the lead (C0/5A01 `LDA $1100,X / BEQ ->5A31`, 5A3B/5A3D),
  so he is visited first in every pass he is queued for.
- **`$0138` != 0 on a main-loop frame**: not reachable -- C0/1770 is its only armer and its loop C0/1781-178E runs until
  it is 0 (the port seeds a nonzero `$0138` as an ALIGN frame and clears it; the cartridge, poked into the main loop,
  keeps it and the lead stands).

Fix: ct_vm.c `vmLeadStepAtA810` takes the host's step back when no ticket stands -- BEFORE the party trail when B0 did
not run (no 9E29, so no ring push), AFTER it when an invoke dropped the ticket (9E29 pushed). A resumed pass keeps the
ticket its first half armed. Graded by field-lead-walks-only-on-his-b0-ticket-vs-rule-cartridge
(tools/lead_step_ticket_cmp.py): 3/3 (1/3 before). Harness note: the port's first tick from a seed runs an event pass
the cartridge's first `--monitor` frame does not (the state was saved after its pass; a poked `$1001` reads the same at
f0), so a poked countdown is one higher on the port's side.
