# BATTLE — an action's state is WIPED at both ends, and a bake only owns the slots it covers

Two user reports, one session, two different leaks of *previous* state into a *later* frame:

1. *"after using cyclone the animation apparently repeated on the next turn, at the wrong moment"* — the
   Cyclone's presentation replays on a later turn.
2. *"in the first battle with roly, GATO appeared and then vanished"* — GATO shows up in a fight he is not in
   and then disappears. ("Roly" = enemy **id 0x10**, `$CC:6500` record `B1 C8 C5 D2 EF…`; the fights that
   stage it are **scene 112** (Truce Canyon 600 AD) and **scene 119** (Guardia Forest 600 AD) — the two
   places a first playthrough meets one.) ⚠ **The Roly link is at the FORMATION level, not the enemy
   level**: in those formations slot 3 is filled by a **Green Imp `0x0D`** (both scenes) or a **Blue
   Eaglet `0x15`** (scene 112 obj14), while the Rolys themselves land in slot 6 / slots 0-1. What the
   report needs is only "a formation that fills slot 3", which is what a Roly party happens to be.

---

## A. The ROM wipes an action's state at BOTH ends — `CC/F06B`

The action machine is `C1/4058 … C1/41BD`:

```
14058  INC $A0FD
1405B  JSL CC/F06B              <- STATE WIPE at the action's OPEN
140A6  INC $A013                <- the frozen span opens
140A9  JSL CC/F156              <- roles $A014 / $99AF / $99B2
140AD  JSR C1/4310              <- selector + script LOAD  (its head STZs $9877..$9886)
140B0  (frame body) … JSR C1/423A (pose engine) … JSR C1/4BBE (track runner)
140CE  LDA $9876 / BEQ ->140B0  <- THE ONLY loop-exit test
140D3  LDA #FF / STA $1E01 / LDA #18 / STA $1E00 / JSL C7/0004   ; the lifecycle post
14119  JSL CC/F06B              <- STATE WIPE at the action's CLOSE
141BA  STZ $A013 / RTS
```

`CC/F06B` has **exactly two call sites in the whole 4 MB image** (`22 6B F0 CC` at C1/405B and C1/4119) and
it zeroes:

| region | what it is |
|---|---|
| `$5D80..$5E0C` | the ENTIRE script state — `$5D9D` rendezvous, `$5D9E/$5D9F` stored point, `$5DAC..` track-live, `$5DBD..` the 16 track cursors, `$5DFD..` per-track pause |
| `$A240..$A27F` | the effect-object array |
| `$A124..$A1A7` | the POSE ENGINE's run state (mode/dwell/cursor/latch) |
| `$A014` (FF-fill) | the role bindings |
| `$9876`, `$A21F`, `$A012`, `$A3D1`, … | the action/loop flags |

Two opcode facts decide the shape of the end: `op 0x00` = `C1/4CF0 LDX $A010 / STZ $5DAC,X` kills **one
track**; `op 0x01` = `C1/4CF7 → C1/4D00 INC $9876` ends the **action** and never clears `$5DAC`, so the
exit frame always leaves track 0 flagged live, parked on its `01` byte — only `CC/F06B` retires it.

**MEASURED** (`gato_cyclone.state`, `--monitor "7E5DAC:16,7EA010:1,7EA014:8" --diff --writer`):
`f438 5DAC=0100 0001 0100…(C1:4653)` (tracks 0,3,4,8 armed) · `f639`/`f726` two tracks run their `00`
(writer C1:4CF6) · **`f749 $9876=01 (C1:4D03)`** = op 0x01 · `f751` everything zeroed by **CC:F071/CC:F08E**
· `f755` the next action arms. Also measured as surviving the boundary: `$9877/$9878`, `$AE93`, `$9907`
(the published poses), `$967B`, `$96DB`, `$1D0C/$1D23`.

⚠ `battle_loop.md` misfiles this routine as *"battle-init memory-clear … not the interpreter"*. It is the
**per-action** teardown.

## B. Leak 1 — the interpreter stayed latched past its own script

Our spans are PINNED (`battlePcTechSpan` / `battlePcItemSpan`, the documented boundary) rather than
script-driven, and `h->ascOn` was cleared only on a frame with **no action live** — which an action that
dispatches on the same frame the previous one ends never provides. Measured on `gato_cyclone`: the Cyclone
ends f563 and the enemy's script arms at f584, so for 21 frames the renderers still published the
Cyclone's last pose (`battleStanceRender` takes `h->ascPmPose[seat]` whenever `ascOn` is set); after a PC
PHYSICAL — which arms no script at all — it lasted the whole next action.

**Fixed** by following the tracks, as the ROM's own loop test does: when every track has ended, `ascOn = 0`
and the published poses are cleared (`CC/F06B`'s `$A124..$A1A7` wipe). Cyclone now goes off at rel 240 of
its 313-frame span; the enemy's at rel 150 of 224. Check `field-battle-asc-off-at-script-end`.

## C. Leak 2 — `battleArrive` opened an action without clearing the tech id

`battleArrive` (the enemy walk-in / think-drain dispatch) set `actActive/actAttacker/actTarget` but never
cleared `h->actTechId` / `h->actItemId` / `h->techActive`; its twin `battleOpenAction` clears exactly those
three, one line, with the reason spelled out. So when an ENEMY TECH dispatched (GATO's song, `$9877 =
0x10`), `battleActionSpan` set `actIsTech = 1` while `actTechId` still held the Cyclone's `1`, and three
consumers re-fired on the enemy's turn:

* `battleWhirlRender` — `!h->actIsTech || h->actTechId != 1`,
* the Cyclone screen-darken + victim white-blink block (`fxTechDarkK` / `fxTechFlashMask`),
* `battleTechRouteTick`, gated only on `techActive`.

**MEASURED** on `gato_cyclone` with the cast delayed so every later dispatch goes through `battleArrive`
(`CT_BATTLE_SCRIPT="n:1000,d:2,n:10,a:2,n:10,a:2,n:10,a:2,n:2600"`): a Cyclone at f1286, then GATO's song
at f2871 — **15 `techfx` lines where the cast alone prints 6**, and a pixel scan for `ctBattleWhirlPal`
entries in x 90..180 / y 95..180 found **124 opaque whirl pixels at f2931** (the song's rel 60 =
`CT_WHIRL_REL0`) and 0 one frame earlier. After the fix: **6 techfx lines, 0 whirl pixels** at f2929 /
f2931 / f2960. Check `field-battle-cyclone-fx-not-replayed` (removed 2026-10-24: the replay path is gone, docs/MUTATION_AUDIT.md).

## D. Leak 3 — a per-slot BAKE standing in for the id-driven chain

`battleStanceRender`'s pre-combat baked layer draws `ctBattleStanceObj`, a fixture of ONE combatant —
GATO — and its only enemy-side gates were `gatoDead` (**enemy slot 3 EMPTY**, `$AF05 == 0xFF`) and
`combatLive` (`h->frames >= CT_BATTLE_COMBAT_LIVE_F` = 82). Emptiness, not identity. So **any** formation
that stages an enemy into slot 3 drew GATO's baked cluster at his Millennial-Fair coordinates for the
first 82 module frames (~1.4 s) and then lost him the instant `battleGatoRender` — which *does* check
`$5FAD + 3*0x80 + 0 == 0x92` — took over.

Nothing in the ROM keys a sprite on "slot 3 occupied": appearance is entirely record/id driven
(`CC/EAAB LDA $29C7,X / ASL A / STA $9888,Y` for the palette; `E4:F600 + id*0x10` → the E3 record → the
CT-LZ sheet → `E4:0000 + rec[+2]*24` for the cells). And the staging is not late on our side either —
`stageAll` writes `$5FAD + slot*0x80 + 0` for all 8 slots (the ROM does the same at FD/B43C-B45D, writer
FD:B460) and `CtBattleBegin` runs before `battlePhase = 1`, i.e. before the first composed frame.

**Fixed** with the gate `battleGatoRender` already uses. **REPRODUCED and pinned** on `bat9` (Crono/Lucca/
Frog vs three Diablos, staged slot 3 = id 0x48): `CT_BATTLE_RENDERFRAME=40` drew GATO in the middle of the
arena (fnv `3E7EB59C`), `=400` did not (`209CB2A8`). Checks `field-battle-no-gato-in-other-fights` +
`field-battle-gato-bake-precombat` (the second one's mutation is the INVERTED gate — removing the gate
cannot change GATO's own fight, so it would survive by construction).

### D.2 — the same class, found by the same run: a slot-3 enemy drawn by NOBODY

`battleEnemyGenRender` skipped ids 0x92 and 0x5E outright ("dedicated renderers own these"), but
`battleGatoRender` draws only slot 3 and `battleHetakeRender` only slots `< CT_HETAKE_NSLOT` (3). A Hetake
staged into slot 3+ was therefore drawn by nobody. The slot is DATA: the ROM's op-0x83 loader stores
`(op2 & 0x7F) + 1` into `$0D00,X` (C0/47A3-47C6) and the staging consumer turns it into **`slot = op2 − 3`**
(C0/0557-056E), so the same id lands in different slots in different formations — scene 112's Roly fills
slot 3, scene 19's Hetakes fill 0/1/2. Fixed by skipping only the slots the dedicated renderer covers;
pinned by `field-battle-slot3-enemy-renders` on a one-byte variant of the forest formation
(`$7E0D2E: 0x60 → 0x68`, the slot code scene-19 obj17 already carries) which now shows **three** Hetakes.

## OPEN

- The ROM's wipe is **regional**; ours is per-field. `CC/F06B`'s `$5D80..$5E0C` / `$A240..$A27F` /
  `$A124..$A1A7` have no single equivalent in the port, so a future field added to the interpreter can
  leak again. The two checks above only grade the two paths measured here.
- `battleHetakeRender` and the GATO bake are still per-slot fixtures; the id-driven generic renderer now
  covers the slots they do not, but the fixtures themselves remain (the documented sprite-streamer
  boundary).
- `$AE93` surviving an action boundary is the ROM's own behaviour (measured: `01` for the whole of GATO's
  following action) — it is only a leak when a consumer reads it without its own gate.
