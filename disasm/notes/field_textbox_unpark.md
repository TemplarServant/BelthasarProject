# FIELD — a textbox unparks the event VM at the END of its close ramp, not on the press

Found by an A/B (lockstep) run of **scene 254, CT's combat tutorial** — a scene built almost entirely out
of op-0xC2 boxes and scripted movement, which is exactly what makes it a good differential target. Two
things had to be fixed before the A/B could say anything at all, and then it said something large.

---

## 0. The A/B could not run: `CT_BTN` is ONE HELD WORD

`--vmdiff` injected a single held button for the whole run. A held A dismisses nothing — the ROM's box
machine advances on an EDGE — so the scene froze on its first box and every remaining frame compared two
identical *stopped* machines. MEASURED:

```
CT_BTN=0080 --vmdiff s254 --frames 1200     ->  liveness: oracle ip-steps=12   CLEAN over 1200 frames
```

Twelve scheduler steps in twelve hundred frames, reported as CLEAN. That is the failure mode this repo's
own rule names (`grade-invariant-when-no-behavior`): a green line that grades nothing.

Two harness additions:

* **`VMDIFF_BTNSCRIPT`** — the same `hexbtn:frames,...` grammar as `--btnscript`, in the `$00F2` layout
  `CT_BTN` already uses, fed through the shared `ctBtnNext` cursor. A mash is `80:4,0:86,...`, and both
  machines get the same edges on the same frames.
* **`liveness: oracle ip-steps=N ours=M over F frames`** — printed unconditionally, with an explicit
  `<-- NOTHING RAN: this comparison is vacuous` when the oracle's count is 0.

With a real mash the same run reads `oracle ip-steps=286 ours=12` and diverges at the first `C2`.

## 1. The ROM: op 0xC2 is a two-visit blocking op gated on `$0129`

`C0/35FC` (DP = `$0100`), verbatim:

```
35FC LDA $2D / BNE ->3606
3606 LDA $29 / BNE ->3640            ; $0129 != 0  -> TYX / CLC / RTS   = the IP does NOT advance
360A LDX $6D / LDA $7F0A00,X / BNE ->3635    ; the per-object "I opened it" latch
3612 LDA #01 / STA $7F0A00,X                 ; first visit: set the latch and OPEN the box
3618 TYX / INX / LDA $7F2001,X / STA $2A     ;   $012A = the string-id operand
3620 LDA $6D / STA $2E / STZ $32 / LDA #01 / STA $29 / LDA #02 / STA $30 / LDA #20 / TRB $54
3632 TYX / CLC / RTS                         ;   CLC = park
3635 LDA #00 / STA $7F0A00,X / TYX / INX / INX / SEC / RTS   ; later visit: clear the latch, ADVANCE
```

So the script resumes **on the first scheduler visit after `$0129` returns to 0** — and `$0129` is cleared
by the box machine `C0/1F87` at `C0/204F`, at the end of its close ramp. Op `0xC0`/`0xC3`/`0xC4` (the
decision box, `C0/3643`) open with the identical `LDA $29 / BNE` head.

## 2. Measured: the byte clears at press + 11, and the resume is phase-dependent

`--monitor "7E0129:1,7E0127:1,7E119A:2,7E1C1A:1,7E00F6:1" --from <scene-254 state> --btnscript
"0:90,100:4,0:60" --diff --writer` (obj13 is the narrator; `$119A` = `$1180 + 13*2` is its IP):

| frame | what | writer |
|---|---|---|
| f91 | `$00F6 = 80` (the A press); `$0129` 07 → **0D** (the CLOSE state) | `C0:2085` |
| f92..f101 | `$0127` ramps `28 24 20 1C 18 14 10 0C 08 04 00` — **4 per frame, 10 frames** | `C0:2044` |
| f102 | `$0129` → 00, `$0126` → 00 | `C0:204F` / `C0:2051` |
| **f103** | obj13's IP moves `06BA` → `06B4`, its prio → 7 | `C0:5AC2` / `C0:5FA3` |

**`$0129` clears at press + 11**, and the script resumes on the object's next scheduler visit after
that — which for this press and this object is f103.

⚠ **The `+12` this dossier first claimed was a LAW is one fixture's PHASE.** The close ramp `C0/2032`
is `A5 27 F0 17 | C6 27 F0 13 | C6 27 F0 0F | C6 27 F0 0B | C6 27 | 6B` — **four decrements but only
four tests**: the last DEC is untested, so with `$0127 = 0x28` the counter reaches 0 on ramp frame 10
and `C0/204F STZ $29` only fires on frame **11**. The RESUME is then the first scheduler visit with
`$0129 == 0`, which depends on where the press lands in that object's visit lattice: sweeping the
press phase on scene 254 (obj13 visits every 4 frames) and on `intro_box.state` (a 5-frame lattice)
measures the resume at **+11, +12, +13, +14 and +15**, with `$0129` clearing at +11 in 14/14 runs.
A fixed count reproduces exactly one of those.

`ct_field_textbox.md` recorded this as OPEN — *"⚠ SCOPE: this is the VISUAL ramp only. Whether the event
VM resumes on the press (f31) or at the ramp's end (f42) was NOT measured"*. It is measured now, and the
answer is the ramp's end.

## 3. What the port did, and what it costs

`CtSceneAdvance` cleared `sc->textbox` immediately, so **every dialogue advance in the game ran eleven
to fifteen frames early**, and every event a script sequences after a box inherited the error — a scene's clock
drifts by that much per box it has already dismissed.

Now the port keeps **the ROM's own bytes**, and there is no constant left:

* the textbox op writes `$0129 = 1` when it opens the box (`C0/3626`);
* `CtSceneAdvance` (the host's dismiss) writes `$0129 = 0x0D` (`C0/2081`) and re-arms `$0127 = 0x28`
  (`C0/1FF8`'s own immediate) — and sets one flag, because **`C0/2081` sets the state and RETURNS**:
  the `0x0D` arm is only dispatched on the box machine's *next* frame;
* `CtSceneTick` runs `C0/2032`'s ramp, four DECs with four tests, and clears `$0129`/`$0126`
  (`C0/204F`/`C0/2051`) when it drains;
* the op's later visits take `C0/3606`'s park arm while `$0129 != 0` and `C0/3635`'s advance arm when
  it is 0 — so the resume is phase-dependent all by itself, exactly as measured;
* `CtSceneChoose` no longer writes the decision's `result` at the press: `C0/365D` writes it on the
  **completion visit** (`LDA $66 / STA $7F0A80,X`, the same arm that advances the IP), so the port
  holds it in `tbPendChoice` and applies it there. `C0/1FDD` reads `$7F0A80,X` while the box is still
  alive, so writing it eleven frames early is not obviously harmless.

`CtSceneTextboxUnparking()` still lets the host draw the window ramping out from the press frame
(`$0129 == 0x0D`), which is what `C0/2032` does.

After the fix the same lockstep run reproduces the ROM's frames exactly:

```
f103  ours ip=FFFF pr7 | oracle ip=06B4(op B2) pr7        <- both unpark on the same frame
f107  ours ip=06C1(op C2) pr1 | oracle ip=06C1(op C2) pr1 <- both re-enter the next box together
```

## 4. The harness's dismiss must respect the ROM's own accept window

The second box in the tutorial swallows its press entirely: the ROM's typewriter is still running, so `A`
completes the reveal and does **not** advance the script. The reveal rate is the FIELD layer's model,
which `--vmdiff` does not run — so the harness does not model it at all. It watches the **oracle's own
`$0129` 7 → 0x0D transition**, which *is* the ROM accepting the dismiss, and fires `CtSceneAdvance`
there. Same class of injection as this mode's LEAD SYNC, and it isolates the event VM from our
typewriter's timing.

⚠ **It has to be that edge and not our own button.** The word injected for frame `f` reaches the ROM's
logic on frame `f+1` (the joypad auto-read latches at vblank), so a dismiss keyed on our press fires
one frame early — MEASURED with `VMDIFF_BOXTRACE`: our dismiss f91, the oracle's `$0129` 7 → 0D at
f92, its clear at f103 against our f102. With obj13 visited every 4 frames that error is invisible at
some press phases and a 4-frame divergence at others: **sweeping the drive script by one frame flipped
4 of 8 phases red**, while the `liveness:` line stayed byte-identical in all eight. A lockstep whose
verdict depends on the phase of its own input is not a lockstep. All eight phases are clean now, and
`vmdiff-scene254-textbox-phase1` pins the shifted one.

## 5. Where the A/B stands

With `VMDIFF_BTNSCRIPT="80:4,0:86" × N` on `saves/field_re/scene254_tutorial.state`:

| state | first divergence | oracle liveness |
|---|---|---|
| before anything | **f91** (`obj13 prio ours=7 oracle=1`) | 286 steps / 1200f (held A: **12**) |
| after the first unpark model | f195 | — |
| after gating the harness dismiss | f435 (`obj13 ip ours=06CF(C2) oracle=06B4(B2)`) | 1297 / 5400f |
| after `$0129` as STATE + the arm delay + the oracle-edge dismiss | f435, and all 8 press phases clean | — |
| **after op 0xFF ends the visit** | **f467** (`obj7/obj8 ip ours=04A7/04E1 oracle=04AA/04E4`, op AD) | 1297 / 5400f |

`vmdiff-scene254-textbox-unpark` pins the first 400 frames CLEAN **with the liveness line**, so it cannot
go green on a stopped machine. Both mutations are killed: `CT_TB_UNPARK_F 12 -> 1` (the old behaviour) and
removing the harness's `$0129 == 7` gate.

## 6. What op 0xFF had to do with it

The f435 divergence was **not** a textbox problem at all: op `0xFF`'s portable half is
`4000 … 4009 JSR ($400E,X) / 400C CLC / 400D RTS`, and that `CLC` is **unconditional** — the visit
runner's `C0/5AB4 BCC` closes the object's visit right there, whatever the sub-handler did. Our
handler returned "keep going", so obj0's autorun spent the rest of its five-op budget and ran two
ops ahead of the ROM for the rest of the scene, firing its next `CALL` **four frames early**. One
`return 1` moved the first divergence to f467.

⚠ **There are two field-VM opcode implementations in this tree** and only one runs the game:
`src/ct_vm.c`'s `step()` (what `CtSceneTick` calls) and `src/move.c`'s `CtFieldRunnerOne`, a
149-opcode runner derived instruction-by-instruction from the ROM, graded by `--sweepops` at
12600/12600 byte-exact including the returned IP and carry — and called only from harness code.
`move.c:3363-3455` already has the whole op-0xFF ladder: the five-byte `0x90`/`0x97` arms, the
two-byte `0x91`/`0x98`-`0x9D` arms, and the PARKING handshake arms `0x92`-`0x95`/`0x9E` that leave
the IP alone. Delegating `ct_vm.c`'s ops to it is the real fix, and it is OPEN.

## 7. OPEN — the next divergences in this scene

* **f467 is DIAGNOSED and it is not the opcode.** obj7's script is a blink loop (`91 HIDE / AD 01 /
  90 SHOW / AD 01 / 11 0C`), and the ROM's wait handler `C0/2F96` has three arms — `operand ==
  counter` advance+SEC, `>` counter++ +CLC, `<` reset without advancing — which cost exactly two
  visits for operand 1, the same as ours. The difference is the **VISIT CADENCE**: our phase lasts 8
  frames (2 visits x 4) and the ROM's second phase lasts **10**, which is not a multiple of the
  object's 4-frame cadence. That is the scheduler's raster yield (`ct-field-scheduler-re`; measured
  live on this fixture: `$0167/$0168 = 05 05`, `$0169 = 0x48`), not the wait op. See
  [vm_two_implementations.md](vm_two_implementations.md) §5.
* **op 0xB2's duration.** At the first unpark the oracle spends 4 frames on `06B4 (op B2)` before its
  RETURN; ours returns in one visit. Costless today, visible only now that the timing lines up.
* **The parking sub-ops of 0xFF** (`0x92`-`0x95`, `0x9E`) are still unported here — they hand-shake
  `$0139`/`$0350` and leave the IP where it is. `move.c` has them.
* **The tree has TWO field-VM opcode implementations and the game runs the ungraded one.** Written up
  in [vm_two_implementations.md](vm_two_implementations.md), with the op-by-op delegation plan.
* The `$012D` early-out at `C0/35FC`/`C0/3643` (`LDA $2D / BNE`) is unported — measured non-zero in every
  state loaded, so the `BRL ->2E1E` arm has never been taken here.
* Scene 254 has **no `D8` battle**: it *simulates* combat with field objects (op-0x81 loads + scripted
  movers + `C2` narration), which is why it exercises the VM so densely without entering the battle module.

---

## 8. THE SECOND HALF OF THE SAME SPLIT: the WINDOW belongs to the box machine, not to the op

USER REPORT (2026-08-28): *"in scene 2, after mom says 'Wake up, Crono!', a dialogue box
is reappearing briefly"* (translated from Portuguese) -- sharpened by the user himself one message later: *"after closing ANY
dialogue box, it comes back briefly and vanishes"*. It was the shared mechanism, and it is
the direct consequence of getting S1-S3 right: once the VM legitimately stays parked for eleven to
fifteen frames after the press, anything that reads that park as "a box is on screen" is wrong for
the tail of it.

### The ROM

C0/1FCD (box-machine state 5) is the ONLY place a window is ever created:

```
1FCD LDA $26 / BNE ->1FF8          ; a window already exists -> run the OPEN ramp instead
1FD1 ... $30 / $0A80,X ...         ; else pick the side: 1 = top, 2 = bottom  (C0/1FCD-1FE8)
1FEA STA $26 / STZ $27 / STZ $31 / STZ $33 / STZ $36 / JSR C0/20F2 / RTL
```

so **$0126 IS the window** (its position; 0 = there is none) and `$0127` is its height, 0..$28. The
close ramp's tail kills both in the same breath:

```
2032 LDA $27 / BEQ ->204D | DEC $27 / BEQ ->204D | DEC $27 / BEQ ->204D | DEC $27 / BEQ ->204D | DEC $27 / RTL
204D STZ $29        ; the state
204F STZ $26        ; ...and the window, together
2051 LDA #20 / TRB $54 / RTL
```

And the textbox op's COMPLETION arm creates nothing -- C0/3635, verbatim:

```
3635 LDA #00 / STA $7F0A00,X / TYX / INX / INX / SEC / RTS
```

There is no `STA $29` on that arm (the OPEN arm at C0/3626 is the only one that has it), so **a box
that has been dismissed can never come back**. MEASURED:

```
build/ct.exe --monitor "7E0126:1,7E0127:1,7E0129:1,7E0154:1" --from saves/intro_box.state \
             --btnscript "0:40,100:4,0:60" --diff --writer

  f0    $0126=02  $0127=28  $0129=07
  f41   $0126=02  $0127=28  $0129=0D*(C0:2085)      <- the press
  f42.. $0127 ramps 24 20 1C 18 14 10 0C 08 04 00   (C0:2044, 4/frame, 10 frames)
  f52   $0126=00*(C0:2051)  $0129=00*(C0:204F)      <- window AND state die together
  (no further rows to the end of the run)
```

### The defect

The host derived "a box is on screen" from the **VM's own park latch** (`sc->textbox`) with a single
exception for `$0129 == 0x0D`. But S2 above is exactly the point: the VM stays parked from the ramp
draining (`$0129 = 0`, press + 11) until the op's next SCHEDULER VISIT (press + 11..15). For those
0..4 frames the host read `textbox && $0129 != 0x0D` = **a box is open** with `$0129` already 0, so it
re-opened the window it had just finished closing, re-laid the string out (`tbLastIdx` had been reset
to -1 while the box was down, so the re-assert looked like a NEW box) and then ran a SECOND close
ramp when the op finally advanced. Measured with `CT_TBTRACE` on scene 2:

```
tbtrace: f96   vmTb=1 $0129=0D $0127=28 | tbActive=0 tbAnim=36 tbClose=1   <- the dismiss, ramping out
tbtrace: f105  vmTb=1 $0129=0D $0127=04 | tbActive=0 tbAnim=0  tbClose=0   <- window gone
tbtrace: f107  vmTb=1 $0129=00 $0127=00 | tbActive=1 tbAnim=40 tbClose=0   <- BACK, full size
tbtrace: f110  vmTb=0 $0129=00 $0127=00 | tbActive=0 tbAnim=36 tbClose=1   <- and a second close ramp
```

### The fix

One predicate. `CtSceneTextboxUnparking` now answers for **both** post-dismiss states of the box
machine -- `0x0D` (dismissed, ramping out) and `0` (gone) -- so the host's window follows the ROM's
own byte instead of the op's latch. No count, no scene gate, no new state.

```c
int CtSceneTextboxUnparking(const CtScene* sc) {
  if (!sc || !sc->textbox) return 0;
  uint8_t st = (uint8_t)r8(sc->mem, 0x7E0129);
  return st == 0x0D || st == 0;                  // C0/2081's close state, or C0/204D's cleared one
}
```

### Graded

`CT_TBTRACE` prints a census -- `tbtrace-sum: boxes=N windows=M reopen=K` -- counting the script's box
openings (the VM latch's 0->1 edges) against the windows the host actually raises (`tbActive`'s 0->1
edges), with `reopen` counting a window raised for a box that was **already up**, which C0/3635 makes
impossible. `boxes == windows && reopen == 0` is the law; `boxes >= 1` is the liveness half, so
neither check can go green on a run where no box ever opened.

| check | fixed | mutation `st == 0x0D || st == 0` -> `st == 0x0D` |
|---|---|---|
| `field-textbox-one-window-per-box` (scene 254, 1500f) | boxes=12 windows=12 reopen=0 | **boxes=12 windows=24 reopen=12** |
| `field-textbox-no-reopen-scene2` (scene 2, 1500f) | boxes=4 windows=4 reopen=0 | **boxes=4 windows=7 reopen=3** |

Both mutations KILLED. Scene 2 flashing 3 of its 4 boxes and not the fourth is the phase-dependence of
S2 showing through: that box's press landed on a phase whose gap was zero frames -- which is exactly
why a single-box fixture would have been a coin flip, and why the dense scene is pinned alongside it.

### OPEN

* **The host's `tbAnim` and the ROM's `$0127` are two counters for one quantity** (both 0..40, both
  4/frame). They currently run one frame apart on the close (`tbAnim` hits 0 at ramp frame 10, `$0127`
  at frame 11), because the port never runs C0/1FF8's OPEN ramp on `$0127` -- the op only writes
  `$0129 = 1` and `$0127` stays 0 until `CtSceneAdvance` arms it with `0x28`. Porting C0/1FCD's window
  creation (`$0126` = the side) and C0/1FF8's open ramp would collapse the two into the ROM's one, and
  would make `$0126` -- not our `tbTop` -- the position. NOT derived here, NOT implemented.
* **`--demotrace` (src/main.c:181) re-fires `CtSceneAdvance` every frame while a box is up**, which
  re-writes `$0129 = 0x0D` and re-arms `tbCloseArmed` before the ramp can run, so on that path the
  ramp never drains. Spotted while auditing the callers of `CtSceneTextbox`; not exercised by any
  check, not touched here.
