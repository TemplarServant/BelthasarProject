# THE FIELD OBJECT'S OAM PRIORITY PAIR — `$0C00/$0C01,X` is stored state

2026-09-27, the user's list item #4: *"in the scene in the epoch, there is a priority problem, crono should be behind"* (translated from Portuguese) —
located by the attract-divergence agent at loc 1B0 (the vortex, the Epoch's cockpit), not 1F0.

## 1. What the OAM builder reads

Each object's two halves take their OBJ priority from `$0C00,X` (top) and `$0C01,X` (bottom), X = obj × 2 (C0/CE84 /
CE8F; the port's byte-exact `src/oam.c` reads exactly these). $30 = priority 3 (in front of mode 1's BG priority-1
tiles), $20 = 2 (behind them), lower values lower still. The pair is **written** by the ROM at specific moments and
otherwise keeps its value — it is not a function of the frame.

## 2. Every writer (bank C0, `STA $0C00/$0C01,X|Y`, each aligned by disassembly)

| site | when | the pair |
|---|---|---|
| C0/0462, 04A4, 04E6 | battle staging | `($2989 & 10) \| 20`, both halves |
| C0/453E, C0/4574 | the object loaders (op 0x81/0x82/0x83) | `2020` |
| C0/4B98-4BBC (op 0x8B), 4BFC-4C20 (0x8C), 4C42-4C6C (0x8D) | a placement, only with `$0F80` bit 7 (AUTO) | the new tile: `$0C00` ← `$7E7040`, `$0C01` ← `$7E7000` (`(p & 40) >> 2 \| 20`) |
| C0/4C83-4CA8 / 4CB7-4CC8 (op 0x8E) | the priority op | AUTO: the tile as above; else the byte's NIBBLES — `$0C00` = `(b << 4) & 30`, `$0C01` = `b & 30` |
| C0/9D54-9D67 | the lead's step commit (`$6C` = 0) | `$6E/$6F`, the tile the step lands on (C0/9AD3's decode); also `$0155` |
| C0/9DC3-9E1D | the load, BRL from C0/5926 (end of the object pass) | the ARRIVAL tile `$0102/$0103`: the lead `$6E/$6F`, the other two PCs `$6F` in both halves; also `$0155` |
| C0/A2BD-A2C8, A32A-A335 | the followers' ring restore | `$7F0E00/0E80` (the lead's saved pair) |
| C0/ADB6-ADE8, AE06-AE25, AEE3-AF15 | C0/ACFD, the mover's step issue | AUTO: the TENTATIVE cell — `$0C01` ← `$7E7040`, `$0C00` ← `$7E7000` (the other way from the placements); else the nibbles |
| C0/1A8D | a party restore | `$7F1D15` |

## 3. What the port had

The field renderer re-derived the pair every frame: the foot tile for the lead (`footPrio`) and, in a cutscene, the
lead's `$0F80` nibbles **swapped** (top from bits 4-5; C0/4CB7 has the top from bits 0-1); for NPCs the foot tile
(AUTO) or the nibbles. That equals the stored pair only between writes. At 1B0 op 0x8E leaves the three PCs at 30 20 /
30 30 / 30 30 (cartridge c8324, C0:4CC9) and C0/9DC3 rewrites all three to 20 20 at c8325 (C0:9E0E / 9E17 / 9E20); the
PCs never step there, so the cartridge draws them behind the console for the whole leg — the port drew Crono's legs
and two arms over it (cockpit (96,64)-(160,128): 3896 / 4096).

In WRAM the port kept the loaders', op 0x8E's, the restores' and battle staging's writes, but not: C0/9DC3; the step
commit (it writes the collision engine's own image, `g_collRam`, lead at a fixed slot); the three placement ops; and
C0/ACFD's (the live mover in ct_vm.c never called move.c's `CtBuildVelocityPriority`) — whose per-frame stand-in was the
renderer's re-derivation. ACFD also needs the DP masks `$1D1E/$1D1F`, which the port never staged: C0/A36A stages the
map header's layer sizes (`$0BCB/$0BCD/$0BCF/$0BD1/$0BD3/$0BD5`, from `$7EB500/B501`) and C0/7399 (JSR at C0/A4FD) turns
each into a mask (`$40`/`$30` → 3F, `$20` → 1F, else 0F). The cartridge's scene 5: `$0BCB/$0BCD` = 30 40, `$1D1E/$1D1F`
= 3F 3F; the port's: 00 00 / 00 00.

## 4. Now

* `CtSceneArrivalPriorities` (ct_vm.c) = C0/9DC3's WRAM half, from `fieldArrivalTileMode`'s tile half.
* `g_collPairCommit` (collision.c) flags the commit's branch; `fieldPlayerCollide` copies `$0155` and the pair into the
  scene WRAM at the lead's `$0197`.
* `vmPlacePrio` (ct_vm.c) after ops 0x8B/0x8C/0x8D, at both dispatch sites.
* `mvIssuePrio` (ct_vm.c) in `mvIssueRegs`: ACFD's block on the published velocity (zero → no write).
* `fieldMapHeaderStage`: C0/A36A's size bytes and C0/7399's six masks.
* The renderer reads `$0C00/$0C01` for the lead and every NPC.

MEASURED after: the 1B0 cockpit 4096 / 4096 and the whole frame 57344 / 57344 (port f10375 / cartridge c8402);
attract-1b0-vortex-bg2 57144 → 57344. Scene 5's obj 8 (AUTO, a fair racer) follows its tile again (the cartridge:
C0:ADD5 rewrites it at every tile change, 2020 → 3020 → 2020 → 3030 ...; without the mover's write the port kept its
placement's 20 30, and loc 01B's fade frame lost 96 px). The lead at scene 5's bell: 30 20, the cartridge's
(`saves/field_re/s5_bell_marle.state`).

## 5. Verified on the cartridge, and graded

* **The step commit** (`saves/field_re/s5_bell_marle.state`, the lead obj 1 at (05,25), pair 30 20, `--monitor 7E0C02:2
  --writer`): Down → 20 20 at f17 as y reaches 27 (C0:9D6A); Up → 30 30 at y 24; Left / Right → 20 20. The port from the
  same seed (its lakewram), `d:40`: 3020 to f15, 2020 from f16 (the key's one-frame alignment). Check
  `field-lead-step-commit-writes-priority-pair-vs-cartridge` (f20).
* **The placements** (`CT_PLACEPRIO_TRACE=1` over the attract, then the cartridge's `--monitor 7E0C1A:2,7E0C1C:2
  --writer`): loc 01B's obj 13 / obj 14 get 20 30 by C0:4BBF at c1125 / c1126 and keep it; 0AD's obj 13 30 30 at c1433.
  The port's obj 13 at f3300: 20 30. Check `attract-01b-placement-priority-pair-vs-cartridge`.
* **The mover**: scene 5's racer obj 8 (C0:ADD5 at every tile change on the cartridge); lockstep: the party3 write-mask
  runs model 18 more addresses with their divergences unchanged, the Manoria nuns' OAM mismatches 1407 in 299 frames →
  179 in 15. Loc 01B's fade frame loses 96 px without it (its mutation, in `attract-fade-rate-01B-vs-oracle`).
* **C0/9DC3 and the renderer**: `attract-1b0-cockpit-pcs-behind-console-vs-oracle` (the whole frame, 57344 / 57344), with
  mutations for C0/9DC3's stores, the NPCs' re-derivation and the lead's old swapped cutscene decode;
  `attract-1b0-vortex-bg2-vs-oracle` 57144 → 57344.
* **The selftest line's `topF/botF`** now prints the lead's stored pair (what the render draws): scene 2 → 0/0 (the
  oracle's 20 20 through the bed intro), scene 113 after the gate warp → 1/1 (the script's `8E 33`).

## 6. OPEN

* The ring restore and C0/1A8D were already ported; C0/0462's battle staging too. Not audited here: whether every
  port path that MOVES an object without an op issue (the C-hosted followers, the cat's chase mover) also reaches a
  pair writer as the ROM's does.
* `$1D1A-$1D1D` (C0/7477-74A0) stay the camera's own; the collision image keeps its measured masks.
