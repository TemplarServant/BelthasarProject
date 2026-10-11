# The $C1 item + gold opcode family, and the first slice of the two-runner delegation

The four ITEM opcodes had **no arm at all** in the event VM that runs the game, and the three GOLD
ones had a paraphrase. This note records the derivation, the measurement that sized it, the
delegation that fixed it, and what is still open.

Companion notes: `vm_two_implementations.md` (why there are two runners), `field_treasure_chests.md`
(the other, already-ported, path into `C1/D005`).

---

## 1. THE MEASUREMENT THAT PICKED THIS

`tools/ophist.py` walks every scene's object fn-pointer tables (never linearly — a linear walk
desyncs into data and invents sites) and reports **114848 static fn-reachable op sites over 504
shipped scenes**. Against `src/move.c`'s `CtFieldRunnerOne` that is 99.9% covered. Against
`src/ct_vm.c`'s `step()` — **the runner the game actually executes** — it is not, and this family is
the largest consumer-ready hole in it:

| op | ROM handler | what it does | static sites | state before this session |
|---|---|---|---|---|
| 0xCA | C0/37B1 | ADD ITEM (id from the script) | **220** | no arm — IP advanced, nothing happened |
| 0xCB | C0/37E3 | REMOVE ITEM | **55** | no arm |
| 0xC7 | C0/37C7 | ADD ITEM (id from the scene LOCALS) | **25** | no arm |
| 0xC9 | C0/3780 | HAS ITEM + branch | **24** | no arm — and `OPLEN[0xC9]=3` meant the fall-through arm was taken every time, i.e. **every has-item test in the game answered YES** |
| 0xCC | C0/3827 | HAS GOLD + branch | 24 | paraphrase |
| 0xCD | C0/385B | ADD GOLD | 4 | paraphrase |
| 0xCE | C0/3874 | SUB GOLD | 23 | paraphrase |

**375 sites across 105 of the 504 scenes**, 324 of them with no implementation whatsoever. The
consumer was already finished and graded on the other side: `menu.c` reads the bag straight out of
`$7E:2400`/`$7E:2500`, so the engine could move gold but could never put an item into the bag.

Reproduce:

```
python tools/ophist.py
```

---

## 2. THE MECHANISM (ROM)

All seven handlers are the same eleven instructions. `C0/37B1`, the ADD-ITEM one, in full:

```
37B1 TYX / 37B2 INX / 37B3 TDC / 37B4 XBA
37B5 LDA $7F2001,X        ; the operand, from the running script
37B9 INX
37BA STX $C7              ; SIXTEEN-BIT spill of the post-operand IP
37BC TAY                  ; stage the argument in Y
37BD LDA #01              ; ...and the LEAF INDEX in A
37BF JSL $C1:8003
37C3 LDX $C7 / 37C5 SEC / 37C6 RTS
```

`C1/8003` is `JMP C1/CFC2`, which sets `DB = $7E`, `D = $0000`, doubles A and does
`JSR ($D126,X)`. The table at **`$C1:D126`**, read out of the ROM:

| leaf | routine | |
|---|---|---|
| 0 | C1/CFE9 | HasItem |
| 1 | C1/D005 | AddItem |
| 2 | C1/D056 | RemoveItem |
| 3 | C1/D086 | HasGold |
| 4 | C1/D0A2 | AddGold |
| 5 | C1/D0ED | SubGold |
| 6/7 | C1/AA7B, C1/1686 | battle, not reached from the field VM |

Because the leaf runs with `DB=$7E` and `D=0`, `$2400,X` **is** `$7E:2400` — which is why the port's
gate takes the flat 128 KB image and indexes it directly.

### The four things that are not uniform, and why one blind arm is wrong

* **Lengths.** `0xCA/0xCB/0xC7` end `LDX $C7 / SEC / RTS` with no trailing `INX` → ip+2.
  `0xCD/0xCE` → ip+3 (their operand is SIXTEEN-BIT, `382B`). `0xC9` → ip+3 **or branch**;
  `0xCC` → ip+4 **or branch**.
* **The branch target is off the SPILL, not off the op.** `379D LDA $7F2001,X` (X = the spill) reads
  the displacement, `37A6 STA $D9`, then `37A8 TXA / CLC / ADC $D9 / TAX` → target = `spill + disp`.
* **0xC7 is 0xCA with an indirection.** `37CB` reads the operand, `37D4-37D8 AND #00FF / ASL A / TAX`,
  `37DD LDA $7F0200,X` fetches the id out of the SCENE LOCALS, then `37E1 BRA ->37BC` lands in
  0xCA's own tail — so it shares 0xCA's leaf, its spill AND its length.
* **Carry is SET on every exit of all seven**, so no arm ever yields the visit.

### ROM quirks the leaves carry (already ported, kept verbatim)

* `AddItem` sets `$AEF9 = 0` at `D010` and only re-stores it when a free slot is found, so on a FULL
  inventory `D034 LDX $AEF9` reloads ZERO and **the new item clobbers slot 0**.
* `AddGold`'s clamp is TWO-STAGE and is **not** `min(gold, 9999999)`: `D0C5 CMP #98 / BCC` then
  `D0C9 LDA #98 / STA $2C55` **unconditionally**, and only then is `$2C53` tested against `#9680`.
  A pre-existing out-of-range value therefore clamps to `$98:0000` = 9,961,472, not to 9,999,999.
* `SubGold` when you cannot afford it (`D101 BCS` not taken, `$2C55 == 0`) returns 0 at `D112` and
  **never writes `$2C53`** — the purse is left untouched, not floored to zero.

The last two are exactly where the live VM's old paraphrase differed.

---

## 3. WHAT WAS DONE — ONE BODY, TWO CALLERS

The seven-opcode body was already in the tree, byte-derived and graded: `src/move.c`'s
`case 0xC7..0xCE` inside `CtFieldRunnerOne`, which `--sweepops` micro-executes against the ROM.
Transcribing it a second time into `ct_vm.c` would have been a third copy of a problem the tree
already has too much of, so instead:

* the arm body moved out of the switch into **`CtVmItemFamily(ram, script, ip, *outIp)`** (move.c,
  declared in move.h), and `CtFieldRunnerOne` now calls it — so the grade stays exactly where it was;
* `ctVmC1Gate` lost its `static`;
* `ct_vm.c`'s `step()` gained a seven-op arm that is three lines: call the shared body, take the IP,
  continue the visit.

`script` comes in as a pointer because that is the ONE thing that genuinely differs between the two
runners: `CtFieldRunnerOne` runs on the ROM's own addresses and passes `ram + 0x12001` (the script at
`$7F:2001`), while the live VM keeps the scene body in host memory and models bank `$7F` only up to
`$7F:03FF` (`ct_vm.c WM_LEN`), so it passes its own body pointer. **Everything else is a WRAM
address and is identical on both sides**, guaranteed by `ct_vm.c`'s `ct_wram_layout_assert`, which
pins `$7F:0200 == offset 0x10200` for both callers.

### A defect found while re-deriving, and not by a test

`ctVmC1Gate`'s zero-argument arm read `leaf == 3 || leaf == 4` while its own comment cited
`D0E5`/`D123` — i.e. leaves 4 and 5. Re-disassembled, the split is ITEMS vs GOLD:

```
leaf 0 CFE9 -> D002 LDA #00      leaf 3 D086 -> D09F LDA #FF
leaf 1 D005 -> D053 LDA #00      leaf 4 D0A2 -> D0E5 LDA #FF
leaf 2 D056 -> D083 LDA #00      leaf 5 D0ED -> D123 LDA #FF
```

so SubGold's zero arm was the one left out; corrected to `leaf >= 3`. **This is UNGRADED and stays
ungraded**: op 0xCE is leaf 5's only caller and its tail (`C0/387E LDX $C7 / SEC / RTS`) discards A,
so the return is invisible to `--sweepops`' whole-image compare and to the game alike.

### An invention removed

`field.c`'s `fieldLoad` used to write `$2400 = 0xD6 / $2500 = 1` whenever `storyline >= 6` and the bag
was empty — Marle's pendant, inferred from which captures carry it. That was a heuristic standing in
for op 0xCA, and it is deleted. Measured before deleting: `regress.py --quick` is **308 pass / 0 fail**
with it gone.

---

## 4. HOW IT IS GRADED

* **The body, against the ROM**: `sweepops-player-script-opcodes` micro-executes 60 cases per opcode
  — `C7:60 C9:60 CA:60 CB:60 CC:60 CD:60 CE:60`, 420 cases — and compares the WHOLE 128 KB image plus
  the returned IP and carry. Still `12600/12600`.
* **The live VM reaching that body**: `field-item-op-live-vm` (new). Scene 439 obj15 slot1 is the fair
  pendant pickup, `CA D6` at body `0486`; activating it must leave `$7E:2400=D6 / $7E:2500=01` — the
  same pair the real capture `two_member.state` carries.

  ```
  CT_BAGDUMP=4 CT_ACTIVATE="15:5" build/ct_field.exe --assets ct_assets.dat --scene 439 \
      --selftest build/_itemvm.raw --frames 60
  bag: D6:01 00:00 00:00 00:00  gold=400 famops=1
  ```

  `famops` is the anti-vacuity half: the same command without `CT_ACTIVATE` prints an empty bag and
  `famops=0`, so the pin cannot rot green by ceasing to reach the op.
* **Mutation, both sides.** Mapping op 0xCA to leaf 2 instead of 1 takes `--sweepops` to
  **12490/12600 (110 mismatch)** and empties this check's bag. Neither survives.
* **Second site, same signature**: scene 8 obj11 slot1 (`CA D6` at body `028D`) →
  `bag: D6:01 ... famops=1`.

---

## 5. OPEN QUESTIONS

* **The DP scratch words are now written by seven handlers and no others.** The shared body writes
  `$01C7/$01C8` (`STX $C7`) and, on the 0xC9/0xCC branch arm, `$01D9/$01DA` (`STA $D9`), because the
  ROM does. In `CtFieldRunnerOne` that is graded. In the LIVE VM it means those two words are correct
  only immediately after an item op: the ROM's other bank-C0 handlers spill to the same direct page
  and this VM models none of them. `--vmdiff`'s mask claims any byte that changes, so the first
  lockstep fixture that runs a family op will start comparing them and may report a divergence that
  belongs to the ~200 unported writers, not to this arm. **Measured, not assumed: of the 145 oracle
  states in the repo (root + `saves/**`), ZERO stand in any of the 105 scenes that contain a family
  site** — decoded by reading each state's `$7E:0100` against the `ophist` walk. So this is untested
  rather than clean, and closing it needs a capture in one of those 105 scenes, not a new mechanism.
* **The 0xC9 / 0xCC branch arms are only reached, not verified, in the live VM.** Scene 154 obj11
  slot1 runs `C9 DB 39` (`famops=1`, bag unchanged, branch taken because the bag is empty) — the op
  executes, but nothing headless yet asserts WHERE the script went. A check that pins the resulting
  IP would close it.
* **Gold through the live VM is unexercised end to end.** Every 0xCD site sits behind a textbox or a
  decision (scene 439 obj30 slot1 is `CD 32 00` behind a choice box; scene 14 obj8 slot1 behind
  `BB 64`), so a headless activate never reaches one. It needs a `--play` drive, not a new mechanism.
* **The rest of the delegation.** This is one family of the gap `vm_two_implementations.md` §4
  describes. The same shape applies to the movement family (0x7A 185 sites, 0x8F, 0xA1, 0x9E), to op
  0xFF's parking subs, and to the wait family — each of which already has a `--sweepops`-graded arm in
  `move.c` and no arm, or a private scalar, in `ct_vm.c`.
