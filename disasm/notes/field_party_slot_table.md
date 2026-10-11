# A script names a PC by PARTY SLOT, and the table is $0197 — not `playerObj + i`

Second slice of the `vm_two_implementations.md` delegation. Same shape as
`field_item_gold_family.md`: `move.c`'s harness-only runner already resolves this correctly and is
graded for it, while the runner that executes the game carried a paraphrase.

---

## 1. THE MECHANISM (ROM)

The event-op jump table is at **`C0:5D6E`** (handler = `table[op*2]`), read out of the ROM:

```
op 20 -> C0/658F   op 21 -> C0/65AA   op 22 -> C0/65EB   op 24 -> C0/661F
op A8 -> C0/55A0   op A9 -> C0/5605   op 95 -> C0/5410   op B6 -> C0/354F
```

The pairs differ by exactly ONE instruction, and that instruction is the whole mechanism:

```
C0/65AA  op 0x21   INY / TYX / TDC / XBA / LDA $7F2001,X          -> A = the operand
C0/65B2            TAX / LDA $1801,X / STA $D9 / LDA $1881,X ...     (the shared body)

C0/65EB  op 0x22   INY / TYX / TDC / XBA / LDA $7F2001,X
C0/65F3            TAX / LDA $97,X                                 <-- THE INDIRECTION
C0/65F6            BRA ->65B2                                         (same shared body)
```

and identically for `0x24` (C0/661F `LDA $97,X / BRA ->6600`, whose body reads the facing array
`$1600,X`) and for `0xA9` (C0/5605 `LDA $97,X / TAX / BRL ->55A9`, sharing op 0xA8's body).

With the field engine's `D = $0100`, `$97,X` is **`$7E:0197 + X`**. `$0197/$0199/$019B` are the
three active party slots; each holds the slot's object **OFFSET** (`obj*2`, spilled there by op
0x80's arms at C0/426A), or `0x80` when the slot is empty. So:

* **0x21 / 0xA8** take a raw object offset;
* **0x22 / 0x24 / 0xA9 / 0x95 / 0xB6** take a **party slot** and fetch the offset out of `$0197`.

### ⭐ The operand is the ALREADY-DOUBLED slot index

This is the part that makes the old paraphrase wrong far more often than its own comment claimed.
Measured over the 504 shipped scenes (`tools/ctlib/script` fn-pointer walk, the same one
`tools/ophist.py` uses):

| op | sites | operand histogram |
|---|---|---|
| 0x21 (raw offset) | 52 | 4, 6, 8, 10, 12, 18, 20, 22, 24, 26, 28, 30, 38, 82 |
| 0xA8 (raw offset) | 183 | 2 … 48 |
| **0x22** (slot) | 30 | **{0: 30}** |
| **0x24** (slot) | 7 | **{0: 6, 2: 1}** |
| **0xA9** (slot) | 517 | **{0: 478, 2: 17, 4: 22}** |
| **0x95** (slot) | 70 | {0: 70} |
| **0xB6** (slot) | 6 | {0: 6} |

Never odd, never above 4 — exactly a doubled slot index into a word-strided table, which is why
`$97,X` is a byte-indexed read and still lands on `$0197`/`$0199`/`$019B`.

Consequence: `operand + playerObj` is not merely "right while the party is full", it is **wrong by a
factor of two on every non-zero operand**. With a perfectly ordinary consecutive party
(`$0197/$0199/$019B = 02/04/06`, lead = obj 1):

| operand | ROM | the paraphrase |
|---|---|---|
| 0 | obj 1 | obj 1 ✔ |
| 2 | obj **2** | obj 3 ✘ |
| 4 | obj **3** | obj 5 ✘ |

**40 sites** across 7 scenes carry a non-zero operand (25 of them in scene 21 alone).

### The shared body's own gate

`C0/55A9`, which ops 0xA8 AND 0xA9 both reach, opens with `LDA $1100,X / BMI ->5600` — the target's
MOVE-MODE bit 7 declines the op. The port had an `actors[t].hasPos` stand-in there; the ROM byte is
`$7E:1100 + offset`, which this engine already maintains (ct_vm.c mirrors `moveMode` into it).
As in the 0x95/0xB6 arm, this is **not** an emptiness test: an empty slot's `0x80` makes the ROM
read `$7E:1100 + 0x80 = $7E:1180` and decline only because that byte happens to be negative.

---

## 2. WHAT WAS DONE

Four sites in `ct_vm.c`'s `step()` — ops 0x22, 0x24, 0xA9, and the leftover inside op 0x95's JOIN
branch — now call **`ctVmPartySlotTarget`**, `move.c`'s existing resolver, exposed in `move.h`.
One body, two callers, for the same reason as `CtVmItemFamily`: `--sweepops` micro-executes ops
0x22/0x24/0xA9 against the ROM at 60 cases each, so the live VM should INHERIT that grade rather
than carry a second copy of the rule. The 0xA8/0xA9 `hasPos` stand-in is replaced by the ROM's
`$1100` bit-7 gate at the same time.

Two new debug surfaces were needed to make any of it observable:

* **`CT_CALLFN="obj:slot:prio:frame,..."`** → `CtSceneRequestFn`, which hands `reqOp` exactly the
  triple op 0x02..0x07 hands it. Without it fn slots 3..15 are unreachable headlessly
  (`--talk`/`CT_ACTIVATE` = slot 1, `CT_TOUCHAT` = slot 2), and **all 40** non-zero-operand sites
  live in slot-5/6 functions.
* **`CT_SLOTTRACE`** → one deduped line per distinct slot resolution.
* **`CT_OBJDUMP=<n>`** → per-object facing + tile out of `$1600`/`$1801`/`$1881`, plus the three
  slot bytes. Used to establish §3 below.

---

## 3. HOW IT IS GRADED — AND WHAT IS HONESTLY NOT

* **The resolver, against the ROM**: `sweepops-player-script-opcodes` covers `22:60 24:60 A9:60`.
  Mutating `ctVmPartySlotTarget`'s stride (`slot` → `slot*2`) takes it to **12251/12600 (349
  mismatch)**, so the body is genuinely graded.
* **The wiring, in the live VM**: `field-party-slot-table` (new) pins
  `slotop: op=A9 n=04 -> $019B=06 obj=3`. Reverting that one resolution to `operand + playerObj`
  makes **both** `op=A9` lines vanish from the trace.
* ⚠ **NOT graded: the behavioural consequence.** No fixture in this repo discriminates the two
  resolutions by what you can see. Measured, not assumed: the party spawns stacked (scene 21 with
  `CT_CARRYPARTY=0,1,2` puts objects 1, 2 and 3 all on tile (31,30)) and nothing headless spreads
  it — `--scene` skips the entry settle so the walkscript never moves the lead — so the facing op
  0xA9 computes is identical whichever object it picks; and the objects the paraphrase picks
  instead (obj 5, obj 7) are unplaced, which the `$1100` bit-7 gate rejects anyway. That is why the
  check pins the RESOLUTION TRACE rather than a facing.

---

## 4. OPEN

* A capture with the party actually **spread out** on a map that also runs a non-zero-operand site
  would upgrade `field-party-slot-table` from a trace pin to a behaviour pin. Nothing else is
  blocked on it.
* `op=A9 n=02` is still unreached: scene 21's obj1 fn6 contains both `A9 02` and `A9 04` sites, and
  the function parks before the `n=02` ones. A longer drive, or a different caller, would add it.
* The remaining delegation slices are unchanged: the movement family (0x7A at 185 sites, 0x8F,
  0xA1, 0x9E), op 0xFF's parking subs, and the wait family.
