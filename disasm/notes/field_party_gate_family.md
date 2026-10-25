# The $C2 party / roster / heal family, and the third slice of the delegation

Seven of the ten opcodes that reach `JSL $C2:8004` had **no arm at all** in the runner that executes
the game. Recruiting did nothing, "is this character with us?" always answered yes, and an inn left
the party wounded — while `--sweepops` reported all ten byte-exact, because it grades the *other*
runner.

Third slice of the same program as `field_item_gold_family.md` (the `$C1` item family) and
`field_party_slot_table.md` (the `$0197` table). Same shape, bigger payload.

---

## 1. THE FAMILY

Ten opcodes, all with the same prologue and a `JSL $C2:8004` carrying a leaf index:

| op | ROM | what it does | live-VM arm before this |
|---|---|---|---|
| 0xCF | C0/388D | has-character (branch) | **none** |
| 0xD0 | C0/38BE | add to the RESERVE roster | **none** |
| 0xD1 | C0/38D3 | remove from it | **none** |
| 0xD7 | C0/37F9 | count an item into a local | **none** |
| 0xF8 | C0/3E5C | heal the party (both leaves) | **none** |
| 0xF9 | C0/3E6E | recompute max HP (leaf 6) | **none** |
| 0xFA | C0/3E75 | refill MP (leaf 7) | **none** |
| 0xD2 | C0/38E8 | party membership (branch) | own arm |
| 0xD3 | C0/3919 | join the active party | own arm |
| 0xD4 | C0/394E | leave it | own arm |

Site counts, from `tools/ophist.py`'s fn-pointer walk over the 504 shipped scenes (deduped by body
offset, so shared code counts once): **0xCF 44, 0xD0 66, 0xD1 4, 0xD7 12, 0xF8 25, 0xF9 1, 0xFA 1 =
153 sites the live VM used to step over.**

### Two traps this family carries, both already written down in move.c's arms

* **The carry is NOT uniform.** 0xD0/0xD1/0xD3 END the visit (carry 0 — their handlers contain no
  `SEC`/`CLC` anywhere, and the entry carry is 0 because the runner's dispatch does a SIXTEEN-bit
  `ASL` at C0/5AAD, so an opcode ≥ $80 does not set it). 0xD4's carry is the last `CMP $D9` that
  ran. The rest set it. That is why the shared body returns the carry instead of letting the caller
  assume one — the `$C1` item family could assume, this one cannot.
* **The branch polarity is the OPPOSITE of the `$C1` side's.** 0xCF and 0xD2 take the jump when the
  leaf returns **NONZERO** (the character is ABSENT), where 0xC9/0xCC take theirs on zero.

---

## 2. WHAT WAS DONE — the same delegation, mechanically

`move.c` already had all four case bodies, byte-derived with citations and graded. They were **moved
verbatim** into `CtVmPartyGateFamily(ram, rom, romLen, script, ip, CtHRet* out)`; the only edit was
the result protocol (`h.x = …; break` → `out->x = …; return 1`), applied by script and asserted (no
`h.ip`/`h.carry`/`h.ok`/`break` may survive in the moved text). `CtFieldRunnerOne`'s switch now
calls it, so **that call is what `--sweepops` micro-executes** and the shared body keeps its grade:

```
sweepops: 210 field-script opcodes ... 12600/12600 byte-exact ... (0 mismatch, 0 lost)
   ... CF:60 D0:60 D1:60 D2:60 D3:60 D4:60 D7:60 F8:60 F9:60 FA:60
```

600 micro-executed cases across the family, unchanged by the refactor.

`ct_vm.c`'s `step()` then gained a four-line arm for the seven opcodes that had none. The ROM image
comes from the live VM's existing `gRomImg`/`gRomImgLen` (`CtSceneSetRomImage`, set by both
`Belthasar` and `ct.exe`); a NULL image is not special-cased here because every ROM read inside the
gate is already bounds-checked against `romLen` in move.c.

**0xD2/0xD3/0xD4 were deliberately NOT migrated.** They have real arms in `ct_vm.c`, hand-derived
down to the carry analysis, and several pinned fair/join checks stand on them. Moving those is its
own change with its own validation, not a rider on this one.

---

## 3. HOW IT IS GRADED — and the census that decided the fixture

`field-party-gate-live-vm`: scene 71 obj19 slot1 reaches a bare `F8`, so activating it must take
`CT_ITEMDEMO`'s wounded party to full.

```
control  party: 000180 res=808080808080 0:20/70,8/8 1:25/65,12/12  partyops=0
activate party: 000180 res=808080808080 0:70/70,8/8 1:65/65,12/12  partyops=1
```

Mutation: replacing the delegation with the old "advance the IP and do nothing" turns it red.

**Why scene 71 and not Crono's bed.** The bed is the obvious fixture and it does not work: scene 2
obj11 slot1 is `C0 0E 06 / 1A 01 1E / EA 2B / F8 / …`, and the run parks forever on the op-0xC0
DECISION BOX two instructions before the heal — traced, `[run obj11] @0571 op=C0 p1` repeating to
the end of the run. Neither `TB_AUTOADV=1` nor a `--play` A-press sequence gets past it. So a census
was run for family ops with **no box (0xBB/0xC0-0xC4/0xC8) in front of them** on the path from an fn
entry: **244 (scene, object, slot) entries** qualify, and scene 71 obj19 is the cleanest heal among
them. Everything else is behind dialogue.

That census is also the honest reason this check grades ONE opcode of the seven: the rest are
reachable only through a textbox no headless driver can dismiss.

---

## 3b. ⚠ A DEFECT THIS DELEGATION SHIPPED WITH, AND WHY NOTHING CAUGHT IT

The first version of the extraction moved move.c's case bodies **verbatim**, and the mechanical
transform only rewrote the result protocol. It left five operand reads pointing at move.c's own
source:

```c
int arg  = r[0x12001 + ((ip + 1) & 0xFFFF)];   // the ROM's $7F:2001
int slot = r[0x12001 + spill];
int disp = r[0x12001 + spill];
```

Correct for `CtFieldRunnerOne`, which passes `script = r + 0x12001` so the two are the same pointer.
**Wrong for `ct_vm.c`**, whose script is host memory and whose `addr` is body-relative — the read
lands `header` bytes away (scene 2's header is 0x280, scene 21's 0x460) in a region a live
`Belthasar` run never fills. `CtVmItemFamily`, written by hand rather than extracted, does not have
this defect: it reads `script[…]` throughout.

**Neither existing grade could see it.**

* `--sweepops` is structurally blind: it micro-executes only the caller for whom the two pointers
  coincide. 12600/12600 held with the defect in place — the blindness class in
  `ct-sweepops-blindness-classes`.
* `field-party-gate-live-vm` could not see it either, and for a reason worth stating: the only arms
  a headless drive can reach without a textbox are `0xF8/0xF9/0xFA`, and **those read no operand at
  all**. The check was right about what it covered; the broken part was outside it.

It was the adversarial review of the derivation that found it, not a test.

The body now reads `script[…]` throughout (a no-op for move.c: `--sweepops` stays 12600/12600), and
`field-party-gate-operand-source` pins the thing that distinguishes them. Scene 41 obj10 slot3 is
`CF 02 05` at body `0344` — operand 02 = Lucca, displacement 05 — and since 0xCF branches when the
leaf returns NONZERO (absent), the target is `spill + disp = 0346 + 05 = 034B`:

```
fixed:  gateop: op=CF arg=02 ip 0344->034B carry=1
broken: gateop: op=CF arg=02 ip 0344->0347 carry=1      (the fall-through)
```

The **IP** is the discriminator, not the printed operand.

## 4. OPEN

* **0xD2/0xD3/0xD4 still run their own arms** in `ct_vm.c` rather than the shared body. They were
  derived carefully (including the carry) but they scan `$7E:2980` directly instead of going through
  the gate, so anything the gate does *besides* the scan is skipped for them. What that "besides" is
  — the `C2/834D` epilogue is the candidate — is NOT established here.
* **0xCF/0xD0/0xD1/0xD7 are wired but not behaviourally graded.** `partyops` proves the arm is
  reached in scene 41 (obj10/obj11 fn3 both report `partyops=1`), but no fixture discriminates their
  results, for the box reason above. A trace pin like `CT_SLOTTRACE`'s would close 0xCF's branch
  polarity specifically.
* The reserve roster `$7E:2983-88` is now printed by `CT_BAGDUMP` and stays `808080808080` in every
  fixture — nothing in the repo recruits.
