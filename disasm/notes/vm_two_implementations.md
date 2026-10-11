# THE TREE HAS TWO FIELD-VM OPCODE IMPLEMENTATIONS, AND THE GAME RUNS THE UNGRADED ONE

Surfaced by the scene-254 A/B campaign. This is an architectural fact, not a defect report — but it
changes how every future opcode fix should be scoped, so it is written down before it is acted on.

---

## 1. The two runners

| | `src/ct_vm.c` `step()` | `src/move.c` `CtFieldRunnerOne` |
|---|---|---|
| called by | `CtSceneTick` — **the game** | `src/main.c:2423`, `src/main.c:5931`, `src/move.c:4729` — **harness only** |
| shape | one long `if (op == …)` chain over a `CtScene` with private C scalars (`sc->textbox`, `sc->irisState`, `sc->portalCtr`, `s->wait`, `s->addr`) | `switch (op)` returning the ROM's own `(ip, carry, ok)` and writing the ROM's own WRAM addresses in a flat 128 KB image |
| graded by | the regression suite's scene-level pins (fnv, trace lines, lockstep) | **`--sweepops`: 210 opcodes, 12600/12600 byte-exact over the WHOLE 128 KB image plus the returned IP and carry, 0 mismatch, 0 lost** |

Verified in this session:

```
$ grep -rn "CtFieldRunnerOne" src/
src/main.c:2423   src/main.c:5931   src/move.c:4729   src/move.h:123   src/move.c:1860 (the definition)
$ build/ct.exe --sweepops | head -1
sweepops: 210 field-script opcodes vs micro-execution -> 12600/12600 byte-exact over the WHOLE 128KB
image plus the returned IP and carry (0 mismatch, 0 lost)
```

`--sweepops` also reports what it does **not** grade — 251 "poison" writes (an address written without
changing value), 388 transient addresses (written then overwritten inside the handler), and `C3/0002`'s LZ
workspace — which is the honest bound on that 12600/12600 (`ct-sweepops-blindness-classes`: twenty green
blind spots were once found there, all by MUTATION, none by a score).

## 2. Why this matters

The difference is **fidelity, not coverage** — both cover nearly every opcode (`ct-vm-opcode-coverage`:
99.9%, 217 ops). But only one of them is graded against ROM micro-execution, and it is not the one that
runs. Every divergence this campaign found in `ct_vm.c` is a place where `move.c` already has the derived
answer:

| campaign finding | already in `move.c` |
|---|---|
| op 0xFF sub ≥ 0x90 must END the visit (`C0/400C CLC`) — fixed this session | `move.c:3363-3455`, every arm ends `h.carry = 0` |
| op 0xFF's **parking** subs `0x92`-`0x95`/`0x9E` (hand-shake `$0139`/`$0350`, IP unchanged) — **still missing in `ct_vm.c`**, which advances past every 0xFF | `move.c:3418-3450` |
| op 0xFF's per-sub IP deltas (5 for `0x90`/`0x97`, 2 otherwise) | `move.c:3388-3409` |
| the textbox family parking on `$0129` — fixed this session | `ctVmTextRequest` |
| the decision box writing `result` on the COMPLETION arm — fixed this session | `move.c:2492-2521` |

## 3. What was NOT confirmed, and why a straight swap is not on the table

* **`step()` is not a pure function of RAM.** Its state lives in C scalars on `CtScene`/its per-object
  struct, so a direct op-by-op differential against `CtFieldRunnerOne` (which is RAM-pure) cannot be built
  without refactoring the thing being measured. The "how many ops disagree" number therefore does not
  exist yet, and nothing in this note should be read as claiming it does.
* **`CtFieldRunner` (the scene-level wrapper in `move.c`) is not a scene VM.** It runs one object's visit
  budget over a flat RAM image; the harnesses that call it copy the oracle's RAM in every frame. Wiring it
  under `CtSceneTick` means porting the scheduler, the actor table, the draw list and the host callbacks
  onto the ROM's addresses — a project, not a patch.

## 4. The honest next step

Delegate **op by op**, not wholesale, each with its own lockstep evidence:

1. Give `CtFieldRunnerOne`'s arms a callable form that takes the VM's flat `$7E`/`$7F` image (it already
   does — `uint8_t* r` with `$7F` at `0x10000`, which is exactly `CtWram`'s layout).
2. Route ONE opcode family at a time from `ct_vm.c` to it, starting with **op 0xFF** (whose parking subs
   are simply absent today) and the **wait family** (`0xAD`/`0xB9`/`0xBA`/`0xBC`/`0xBD`, whose counter the
   ROM keeps at `$7F0A01,X` while we keep a private scalar).
3. Grade each one on the scene-254 lockstep *and* on `--sweepops`, and keep the private scalar only where
   a host callback genuinely needs it (the textbox, the naming screen, the iris).

## 5. Where the scene-254 lockstep stands, and what f467 actually is

`VMDIFF_BTNSCRIPT="80:4,0:86" × N` on `saves/field_re/scene254_tutorial.state` now tracks the tutorial to
**f467**, where obj7 and obj8 are one op apart on op `0xAD`.

That op is **not** the cause. obj7's script is a blink loop —

```
04A1: 12 0A 01 00 09   IF loc[10] ?1 cmp0 else +9
04A6: 91              HIDE
04A7: AD 01           WAIT 1
04A9: 90              SHOW
04AA: AD 01           WAIT 1
04AC: 11 0C           JUMPBACK -12 -> 04A1
```

— and the ROM's handler `C0/2F96` has three arms, all verified against ours:

```
2F98 LDA $7F2001,X / LDX $6D / CMP $7F0A01,X
2FA2 BEQ ->2FAF      ; operand == counter -> counter = 0, ip += 2, SEC  (advance, CONTINUE the visit)
2FA4 BCS ->2FBA      ; operand >  counter -> counter++,        CLC      (park, END the visit)
2FA6                 ; operand <  counter -> counter = 0, ip unchanged, SEC
```

With operand 1 that costs exactly two visits, which is what our handler costs too. **The divergence is the
VISIT CADENCE**: our phase lasts 8 frames (2 visits × 4) and the ROM's second phase lasts **10**, which is
not a multiple of the object's 4-frame cadence at all. That points at the scheduler's own raster yield
(`ct-field-scheduler-re`: the C0/59D9 queue yields on the raster, visit cap `$0169 = 0x48` — measured live
on this fixture as `$0167/$0168 = 05 05`, `$0169 = 48`), not at the opcode.

⚠ One real, currently unreachable difference in the wait family: our handler collapses `==` and `<` into
`>=` and advances in both, where the ROM's `<` arm resets the counter and does **not** advance. It cannot
fire while the counter is only ever touched by this op — but the ROM keeps that counter at `$7F0A01,X`,
where a script or another op could disturb it, and we keep it in a private scalar.
