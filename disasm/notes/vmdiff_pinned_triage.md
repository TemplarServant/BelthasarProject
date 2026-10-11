# TRIAGE — the two pinned KNOWN-DEFECT baselines

`vmdiff-withpos-bedslide` and `vmdiff-wmask-party3` are green checks that pin *defects*, not passes.
This is what they actually are, measured. **Neither is what its own note claimed**, and one of them was
not a defect at all.

---

## 0. HEADLINE

| check | what the note claimed | what it is |
|---|---|---|
| `vmdiff-withpos-bedslide` | "ONE frame of the live VM's **float integration** is not [exact]" | **NOT a float or rounding problem.** The oracle holds obj14/15 completely still; the port walks them ~1 px/frame diagonally. The port's **op-0xB6 chase starts a step the ROM's does not**, and `C0/5410` is reached in both. |
| `vmdiff-wmask-party3` | "6 untriaged divergences" | **The mask is CLEAN** — `[wmask] no masked divergence over 130 frames`. The 6 lived only in the note's prose. The real residuals on that scenario are three things the mask *structurally cannot see*. |

Both then share one root: **the port keeps actor position and draw state in C struct fields, outside
the WRAM image**, so the byte compare cannot grade them and they drift independently of it.

---

## 1. `vmdiff-withpos-bedslide` — the port runs a chase the ROM declines

Reproduce:

```
ct --vmdiff bedslide.state --frames 130 --withpos
  >>> FIRST divergence at frame 4: 4 field(s)
    obj14 posX ours=13EC oracle=13DC     obj14 posY ours=0C08 oracle=0BF7
    obj15 posX ours=17EB oracle=17DC     obj15 posY ours=0908 oracle=08F7
```

The pinned numbers (+16 / +17 sub-pixels = one pixel and change) make it look like rounding. It is not.
`VMDIFF_TRACK=14` shows the shape:

```
[trk f1 obj14] ours ip=FFFF pos(5072,3056) fa=3 | oracle ip=0846(op B6) pos(5084,3063) fa=3
[trk f4 obj14] ours ip=FFFF pos(5088,3072) fa=1 | oracle ip=0846(op B6) pos(5084,3063) fa=3
[trk f8 obj14] ours ip=FFFF pos(5120,3120) fa=1 | oracle ip=0846(op B6) pos(5084,3063) fa=3
```

**The oracle never moves obj14 at all** — position, IP and facing are frozen for the whole window. The
port walks it down-right at ~1 px/frame and flips its facing 3→1. By f8 the error is far larger than the
pinned 16-17; frame 4 is merely where the comparator first crossed its threshold. So the pinned number
is a *symptom at one frame*, not the size of the defect.

### The state that discriminates it

Measured on `bedslide.state --settle 1` (`--vmdiff` settles one frame before seeding):

| byte | obj14 / obj15 | obj1 / obj8 (which the oracle *does* move) |
|---|---|---|
| `$1A00` speed | `30` | `10` |
| `$1A01` **ticket** | **`00`** | `10` / `01` |
| `$1900`/`$1980` velocity | `17` / `FE` (stale) | `F9`/`00`, `05`/`05` |
| `$7F0B00` lifecycle | `00` | `11` / `00` |
| `$1100` move-mode | `04` | `00` / `04` |
| `$1A81` script-visible | **`00`** (HIDDEN) | `01` |
| `$0F00` activation latch | **`00`** | `80` |

`--monitor "191C:1,199C:1,1A1D:1,10B1C:1,181C:2,189C:2" --from bedslide.state --frames 10 --diff` prints
**only frame 0**: not one of those bytes changes. The oracle holds a *nonzero velocity with a zero
ticket* and never integrates it — with `$1A01 = 0` there is no step in flight, so the stale velocity is
inert.

### The IP is parked on a chase, and the port resumes it

The oracle's `$1180` for obj14 is `0x0846`. Reading the live script image there
(`--read 7F2847+10`) gives `B6 02 00 8B 17 08 82 AF 91 00` — **op `0xB6` (chase) with operand 2,
followed immediately by `00` (RETURN)**. obj15 is the same shape at `0x0857`: `B6 04 00 ...`.

That explains the port's trace exactly: it resumes at `B6 02`, the chase starts a step down-right, then
`00` RETURNs and the slot goes idle — which is why `ours ip=FFFF` from f1 onward while the oracle's IP
stays on the op.

### ⚠ TWO GATES TESTED AND REFUTED — do not re-try these

Both looked obvious and both are wrong:

- **"prio 7 means not scheduled."** The ROM's queue-build `C0/59D9` (D = `$1000`) gates on
  `$1100,X`, not on prio:
  ```
  59F0: LDA $01,X / BEQ ->5A01 / DEC $01,X / BEQ ->5A01   ; the cadence counter $1001,X
  5A01: LDA $1100,X
  5A04: BEQ ->5A31          ; == 0 -> the lead-PC path
  5A06: BMI ->59F8          ; ★ bit7 SET -> SKIP, not enqueued
  5A0D: LDA $80,X / BPL ->59F8   ; $1080,X already linked -> skip
  ```
  obj14/15 have `$1100 = 0x04` — **bit 7 clear** — so the ROM enqueues them. And `$1080,X` is `0x80`
  for *every* object in this state, so the link gate excludes nobody.
- **"the activation latch stops the run."** It does not stop the *script*. `--hookpc 5410` proves the
  ROM reaches op 0xB6's handler for these very objects:
  ```
  f5  visits=15  $6D: 18 18 18 18 18 1C 1C 1C 1C 1C 1E 1E 1E 1E 1E
  ```
  `$6D` is the object index ×2, so `1C` = obj14 and `1E` = obj15 — **five visits each in one frame**
  (the runner's 5-op budget), and the object still does not move.

### ✅ FIXED — the target lookup was a PARAPHRASE

`op 0xB6` is `C0/354F`, **not** `C0/5410` (`ct-field-cat-mover` records the body, not the entry):

```
3546: STY $E3 / JSR C0/5215 / LDX $E3 / CLC / RTS        ; op 0xB5 -- forces CLC
354F: STY $E3 / JSR C0/5410 / LDX $E3 /       RTS        ; op 0xB6 -- keeps 5410's carry
```

Both wrappers restore X from `$E3`, which is why these two ops are **perpetual**: whatever IP advance
the body makes is discarded.

```
5410: LDX $6D / LDA $1A01,X / BEQ ->541A     ; ticket != 0 -> TYX / CLC / RTS, a step is in flight
541A: TYX / INX / TDC / XBA
541E: LDA $7F2001,X / TAX                    ; the operand
5423: LDA $97,X / TAX                        ; ★ $0197 + operand = the PARTY-SLOT table
5426: BRL ->5228

5215: ... same ticket gate ...
5223: LDA $7F2001,X / TAX                    ; op 0x94/0xB5: the operand IS the object offset
5228: LDA $1100,X                            ; ← both flavours converge here
522B: BMI ->5273                             ; ★ the target's move-mode bit 7 -> DECLINE
5273: JSR C0/568B / TYX / INX / INX / SEC / RTS
```

**The party-slot table decides it.** On `bedslide.state` `$0197+8` reads `02 00 80 00 80 00` — Crono
alone, so slots 2 and 3 hold **`0x80` = EMPTY**. obj14's `B6 02` looks up `$0199` = `0x80`, and
`C0/5228`'s `LDA $1100,X` with X = `0x80` therefore reads **`$7E:1180` = `0x8D`** — negative — and the
move is declined. `ct_vm.c` instead computed `operand + playerObj`, chased objects 3 and 5, and walked
two hidden objects the oracle holds still.

⚠ **THE GATE IS NOT AN "IS THE SLOT EMPTY" TEST.** It declines through *aliasing*: an empty slot's
`0x80` makes the mode read land on object 0's IP low byte, and it happens to be negative. A tidied
`if (slot == 0x80) skip` would diverge wherever `$1180` reads `< 0x80`. The port reproduces the
indexing as written.

⚠ **`move.c` HAD IT RIGHT ALL ALONG** — `ctVmPartySlotTarget` plus the `$1100` bit-7 gate, ROM cited,
graded by `--sweepops`. Only `ct_vm.c`'s field-VM mirror paraphrased it. Same class as the
`$71`-vs-`$0171` heap cursor: *the graded runner is the reference; when the mirror disagrees, the mirror
is wrong.*

Result: `--vmdiff bedslide.state --withpos` moves from **frame 4 → frame 85**, and obj14/obj15 leave the
report entirely. f85 is the **pre-existing** divergence the non-withpos `vmdiff-bedslide` check already
pinned (verified by re-running at the previous HEAD), so nothing new was introduced: it is an IP lag on
obj1, two bytes behind on op `0xAB` (ANIME once) — the same visit-yield shape as §2's f129 and as
`vmdiff-walk-touch-preempt`. Those three want one audit, not three.

### ⚠ THE CHECK CANNOT GRADE THE PIECES — a characterised weakness, not coverage

All three mutations of the fix **survive** on this scenario, every one reporting the unchanged f85:

| mutation | bedslide |
|---|---|
| back to `operand + playerObj` | f85 (survives) |
| drop the `$1100` bit-7 gate | f85 (survives) |
| index the slot table by `operand*2` | f85 (survives) |
| control | f85 |

The fix is **over-determined here**: the table lookup, the ROM gate and the port's own bounds guard
(`t >= nobj`) each independently stop the motion, so removing any one is masked by the other two. What
licenses the fix is therefore *not* this check — it is the ROM citation plus `move.c`'s graded body, and
the measurement that the port now declines for the ROM's own reason (`off = 0x80` → `$1100+0x80` =
`$1180` = `0x8D`, bit 7 set).

A discriminating scenario needs a **3-member party** running op `0x95`/`0xB6` on a slot whose object
index differs from `playerObj + slot` — on `party3_field.state` the table reads `02 00 06 00 08 00`, so
slot 4 is object 4 while the paraphrase would say 5. **No such op site was found** in the states
available, and a raw byte scan for one is a superset that lies (it reported dozens of "op 0x95" hits
inside the fn-pointer table). Finding or building that state is the outstanding work on this fix.

### The old framing, for the record

`C0/5410` **runs in both** and produces a step in the port and none in the ROM. The mechanism is the
chase's own decline condition, and the discriminating state is on the table above — most likely
`$7F0B00 = 0` (the C0/AA07 lifecycle is in its clamp phase, not its issue phase) and/or the object being
hidden, but **that is not derived and must not be guessed**: `C0/5410` has never been disassembled in
this project. Fixing it is a `re-derive` pass on `C0/5410`, not an edit.

⚠ The note on the check also claimed the defect "BLOCKS wiring the animation frame legs into the live
VM" because position lives in doubles. That may still be true as a *separate* concern, but it is not the
cause here — the cause is a step that should never have been issued, and no amount of mirroring
precision fixes an extra step.

---

## 2. `vmdiff-wmask-party3` — the mask is clean; the residuals are invisible to it

```
CT_BTN=200 VMDIFF_WMASK=1 ct --vmdiff saves/battle_re/party3_field.state --frames 130
  seed fidelity (frame 0): 0 divergence(s) (clean)
  >>> FIRST divergence at frame 129: 1 field(s)
    obj33 ip   ours=0B00(op E8) oracle=0B02(op 96)
  [wmask] no masked divergence over 130 frames
  [wmask] mask 42 addr / excluded 8 / oracle-changed 493 / unmodelled 482 / dropped 0
```

**There are no masked divergences.** Every byte the port claims to own matches for 130 frames. The
note's "6 untriaged divergences" and its "mask 46 / oracle-changed 494" are stale prose from an earlier
build; the pinned expectation has always been the clean line plus the four counters.

`VMDIFF_TRACK=33` shows what *is* wrong, and it is three separate things:

```
[trk f121 obj33] ours ip=FFFF     pr7 pos(6784,9200) shown=0 | oracle ip=0AF3(op B2) pr7 pos(6784,9215) shown=0
[trk f126 obj33] ours ip=0B00(E8) pr3 pos(6784,9200) shown=1 | oracle ip=0B00(E8)    pr3 pos(6784,9215) shown=0  <-- DRAW-DIFF
[trk f129 obj33] ours ip=0B00(E8) pr3 pos(6784,9200) shown=1 | oracle ip=0B02(op 96) pr3 pos(6784,9215) shown=0  <-- IP-DIFF
[trk f130 obj33] ours ip=0B02(96) pr3 pos(6784,9200) shown=1 | oracle ip=0B02(op 96) pr3 pos(6784,9215) shown=0
```

1. **A one-frame IP lag at f129, which self-corrects at f130.** The port is one op behind executing
   `0xE8`. Same shape as the divergence `vmdiff-walk-touch-preempt` pins, where the cause was op 0x90's
   missing visit-yield (`C0/4CD9`) — so a visit-yield audit of op `0xE8` is the lead. This is the only
   one of the three the curated comparator reports.
2. **A persistent DRAW divergence from f126:** `ours shown=1` where the oracle has `shown=0`. The port
   draws an object the ROM does not. Visible, and unrelated to the IP lag.
3. **`posY` off by 15 sub-pixels from before f121**, i.e. already wrong when obj33 is still parked at
   op `0xB2`.

None of the three is in the byte mask because **the port keeps `draw` and position in
`sc->actors[]`/`sc->fx[]`, not in the WRAM image** — `$1800/$1880` are mirrored only for PC-mode actors
(`ct_vm.c` ~2926). That is the same structural fact the bedslide note names, seen from the other side:
the byte compare is honest about what it grades, and these bytes are simply not in it.

---

## 3. WHAT TO DO WITH THEM

- **Do not re-pin either check.** The behaviour has not changed; only the prose was wrong, and the
  prose is fixed in `regress_checks.json`.
- `vmdiff-withpos-bedslide` needs `C0/5410` derived. Until then it is correctly pinned as a defect —
  but the *description* now names the real mechanism instead of blaming float rounding.
- `vmdiff-wmask-party3` should stop being described as a defect pin at all. It pins a **clean byte
  compare plus its coverage counters**. The three residuals above belong to their own checks (a draw
  compare and a position compare), which do not exist yet — that absence, not the mask, is the gap.

Related: `movement_pinned_open.md`, `ct-field-mover-reload-ticket`, `ct-field-cat-mover` (op 0xB6),
`ct-field-scheduler-re` (the C0/59D9 queue), `ct-field-touch-preempt` (the visit-yield class).

---

## 4. THE VISIT-YIELD AUDIT — the shared cause does not exist

§1 and §2 both ended by guessing that the residual one-op lags were "the same visit-yield shape, so
they want one audit, not three". **That guess is refuted.** The audit found no missing visit-yield, and
no third divergence.

### 4.1 All three ops end the visit in the ROM, and the port already does too

Carries **measured**, not read off a listing — `--callrom` runs the handler and reports the exit flag:

| op | handler | `--callrom` verdict | IP delta (x − y) |
|---|---|---|---|
| `0x90` SHOW | `C0/4CD5` | **CLC** (ends the visit) | +1 |
| `0xAB` ANIME-once | `C0/2E9A` | **CLC** (the arm arm) | 0 |
| `0xE8` sound | `C0/3C11` | **CLC** | +2 |

The listings agree and add the arm the call did not reach: op 0xAB's *finish* path is
`2ED3: TYX / INX / INX / SEC / RTS` — **SEC**, +2.

And the port already matches all of it. `ct_vm.c:2156` is an explicit visit-yield list, ROM addresses
cited, and **`0x90`, `0x91` and `0xE8` are all on it**; op 0xAB returns 1 (yield) on both its arm and
its wait arm and 0 (continue) on finish, which is exactly CLC / CLC / SEC. **Nothing is missing.**

### 4.2 There are TWO divergences, not three — one is pinned twice

`vmdiff-walk-touch-preempt` runs the **identical command** to `vmdiff-wmask-party3` —
`saves/battle_re/party3_field.state --frames 130` with `CT_BTN=200` — and pins the **identical** line
(`obj33 ip ours=0B00(op E8) oracle=0B02(op 96)` at f129). The two differ only in `VMDIFF_WMASK`. Its
description's "f126 / op 0x90's missing visit-yield" refers to the **draw** divergence at f126 (§2 item
2), which the curated comparator does not report at all — so that historical finding is not what the
check now measures.

### 4.3 Cause A — bedslide f85 is op 0xAB's COMPLETION TIMING

`$7F0B01,X` is the ROM byte the handler branches on (`2E9C LDA $7F0B01,X`, `BEQ` → arm, `DEC A / BEQ` →
finish). Measured on the oracle for obj1:

```
--monitor "10B03:1,1783:1,1683:1" --from bedslide.state --frames 92 --settle 1 --diff
  f67  $7F0B03 = 02   ($1783 = 21, $1683 = 01)    armed
  f77  $7F0B03 = 01                                "complete on next"
  f84  $7F0B03 = 00                                finished
```

So the ROM finishes the op around f84 and advances +2; the divergence is reported at f85 with the
oracle past the op and the port still on it. The port drives the finish off a **host-side `animBusy`
flag** rather than off `$7F0B01,X`, and lands one frame later. `CtVmActor` already documents
`animOpState` as "the op's per-actor visit state (ROM `$7F:0B01,X`): 0 idle, 2 waiting" — the model is
there; what is missing is the ROM's `2 → 1 → 0` transition being driven by the anim ticker
(`C0/CA4B`; the port's `CtAnimTimerTick` = `C0/C98A` is already graded by `--sweepc98a`). **Keying the
op's completion on that byte is the fix**, and it is a derivation on the 2→1 transition, not an edit.

### 4.4 Cause B — party3 f129 is a CADENCE PHASE, not an op

Op `0xE8` never parks: it stages the mailbox, advances +2 and yields. Yet **both** sides sit on it from
f126 to f128 — which can only mean the object is not being *visited*. The oracle visits obj33 at f129
and the port at f130. That is a one-frame cadence-phase difference on obj33, in the same family as
`ct-field-scheduler-re`'s cadence notes, and it has nothing to do with either op's carry.

**RESOLVED.** The cause is op 0x87's SECOND store, `C0/488B STA $1001,X`: the queue build at `00C1`
reloads the countdown from the OLD period before the runner at `00C4` executes obj33's `87 02`, and the
ROM's store re-arms it with the NEW one. Gap 3, not 4 -- obj33 now runs `0xE8` at f129. The countdown
is now the WRAM byte `$1001,X` and `party3`'s wmask check went 20 masked divergences -> **0**. Full
derivation, the `DP = $1000` reason every xref missed it, and the `5A06` `$1100` gate it uncovered:
`frame_wiring.md`, "The one-frame cadence lag".

### 4.5 What the audit changes

Nothing in the engine. It removes a wrong shared hypothesis, halves the count of open divergences,
names the two real causes and points each at its own derivation. The claim "three scenarios, one
audit" is deleted rather than left to be re-read as fact.

---

## 5. FIXING CAUSE A IS A WIRING JOB, NOT AN EDIT — and every piece is already graded

§4.3 named the fix: key op 0xAB's completion on `$7F0B01,X`, the byte the ROM handler actually
branches on, instead of the port's host-side `animBusy` flag. Tracing what that requires turned up an
answer worth writing down, because it is not what the CMake and dossier notes said.

### 5.1 `move.c` already has ALL of it, graded

| piece | where | graded by | called by the live engine? |
|---|---|---|---|
| op 0xAB / 0xB7, four arms keyed on `$7F0B01,X` | `move.c:2261` | `--sweepops` | **no** |
| the `C0/CA4B` leg that drives `$7F0B01` **2 → 1** | `move.c:892` | `--sweepcb3a` / `--sweepc98a` | **no** |
| the `C0/CAD9` anim tree / `CA76` FIFO / `B701` release | `move.c` | `sweepcad9-drain-visit` 165888/165888, `sweepdrain-b701-ca76` 2088/2088 + 144/144 | **no** |
| the object loader `C0/4590` (`CtObjLoad`) | `move.c:5470` | `sweeploader-obj-descriptor` **720/720** | **no** — only move.c's own dispatch |

`ct_vm.c` includes `move.h` for exactly three symbols (`CtObjSceneInit`, `CtObjSlotAlloc`,
`CtObjListLink`) and runs none of the chain. So the ROM's completion condition is ported, byte-exact,
and **never evaluated** in the engine that needs it.

⚠ **THE CMakeLists NOTE WAS WRONG AND IS CORRECTED IN THIS PASS.** It said the tables the anim tree
consumes "are populated by the ROM's object LOADER (C0/448D+4557 / C0/4590 / C0/46DF), **which is not
ported**". The loader *is* ported and graded 720/720. Only the second half held: nothing in `field.c`
calls either piece. Anyone starting this wiring would have begun by re-porting a routine that was
already done.

### 5.2 Why a PARTIAL fix is refused

The tempting half-measure is to have `ct_vm.c` read `$7F0B01,X` while the host keeps deciding when the
anim ends — the byte becomes the single source of truth without the drain being wired. That is refused
for a reason the project has already paid for once: putting the state in WRAM **claims it in the write
mask**, and *claiming a byte is a promise to keep it correct every frame*. The same law reverted
`C0/A88D`'s rising edge, and the bedslide note cites it as why mirroring position was not done either.
A byte the port cannot yet keep right is worse in the mask than outside it.

Adjusting the host's completion by one frame to match f84 is also refused: that is eyeballing a
constant, and the ROM's condition is not a frame count at all.

### 5.3 So the fix is the wiring, and it is now unblocked

Every part exists and is graded; what is missing is that `field.c` calls none of it. That is a real,
bounded integration — loader → tables → drain → `$7F0B01` → op 0xAB — and it will move baselines
(populating `$1300/$1380/$1700/$0D00/$0A00/$0A80/$1480/$1500` in the live engine changes what the
write mask sees), so it wants its own pass with its own validation budget, not a tail-end edit.

**What this pass delivers instead:** the boundary named exactly, the stale "loader is not ported" claim
killed at its source, and the four graded pieces tabulated so the wiring starts from what exists rather
than from a re-port.

