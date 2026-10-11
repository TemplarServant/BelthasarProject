# playAnim on track 4 arms every `$A018` victim, not one combatant (2026-09-19, ported 2026-09-23)

USER REPORT (translated from Portuguese) "in the demo scene, when frog uses the skill where a frog falls and damages all the enemies, only
two are doing the damage animation, it should be all of them". Loc 07D, tech $28 (Frog Squash).

## 1. The script

CD/45A6[28*7] = `28 E0 00 1C 28 02 41` → selector `$28` → CD/5EF0[28*2] = CE/2A13. Header `98 80` loads
tracks 0, 3, 4, 8 (`python` dump of the pointer table at CE/2A17):

| track | role | script | the hurt pair |
|---|---|---|---|
| 0 | actor (Frog, `$A014` = 01) | CE/2A21 | caster playback |
| 3 | primary (`$A017` = 03) | CE/2A47 | `22 0F 01 / 06 05 / 69 00 / 20 05 / 6A` … later `06 03` |
| 4 | `$A018` list | CE/2A73 | `22 0F 01 / 06 05 / 69 00 / 20 05 / 6A` … `06 03 / 00` |
| 8 | effect object | CE/2A90 | the giant frog |

`$06 05` is playAnim anim 5 (the hurt window). Op 69 already walked the list (battle_fx_sprites.md §7);
playAnim did not.

## 2. The ROM: C1/4D08 `CMP #04 / BEQ ->14D79`

`python tools/dis.py 14D04 0x120 1 0`, `14E22 0x100 1 0`, `1577D 0x80 1 0`:

```
C1/4D0D  LDA $A010 / CMP #08 / BCC / JMP C1/4DDF   effect object
         CMP #04 / BEQ ->14D79                    track 4
         LDX $A010 / LDA $A014,X                  tracks 0-3: one combatant
         … $A1E8/$A1FE window, $A124 := $8E, $A12F := 0, $A13A := $A214 := FF (C1/4D5C-4D6C) …
         LDA $8E / CMP #01 / BEQ RTS / JMP C1/4E19  (mode 2: INC $A00F, advance 2)
14D79    LDX $8C / LDA $A018,X / BMI ->14DD7
         … the same window and $A124/$A12F/$A13A/$A214 arm (C1/4DC3-4DD0) …
         INC $8C / BRA 14D79
14DD7    LDA $8E / CMP #01 / BNE C1/4E19 / RTS
```

Every playAnim op shares that body (02 = C1/4D04, 03 = C1/4E22, 04/05/06 = C1/4EA2, 26 = C1/577D) and it
never writes `$A150`. The parking wrappers latch on **`$A018[0]` only** (`LDA $A018 / TAX`), then JMP
C1/4D08, which arms the whole list with `$8E = 1`. What they do on the LATER visits differs by op:

| op | first visit (track 4) | later visits (track 4) | latch on done |
|---|---|---|---|
| 02 | `$8E = 2`, arm all, INC `$A00F`, advance 2 (free) | — | — |
| 03 | `INC $A145[$A018[0]]`, arm all mode 1, stay | walk `$A018` until every `$A124` is 0, then advance 2 (C1/4E62-4E74) | **kept** (no STZ) |
| 04/05/06 | `INC $A150[$A018[0]]` (HOLD), arm all mode 1, stay | wait `$A124` of **the first** only (C1/4EE2-4EE9) | `STZ $A150` (C1/4EEB), advance 2 |
| 26 | `$A150[$A018[0]] := nn`, arm all mode 1, stay | walk `$A018` until every `$A124` is 0 (C1/57CB-57D9), advance 3 | **kept** (no STZ) |

Tracks 0-3 (C1/4E2D / C1/4EAD / C1/5792): the same latch on `$A014,X`, one combatant armed, wait on it,
and every one clears its latch when done (C1/4E46 `STZ $A145`, C1/4EC6 / C1/57AD `STZ $A150`).

So only `$A018[0]` is HOLD; the rest of the list plays mode 1 as a full one-shot. Track 3 is a 0-3 path,
so the primary is HOLD too. (The first version of this note said 26 waits on the first only, like 04-06;
C1/57CB is the list walk.)

`tools/refscan.py A145` / `A150`: the only writers are these wrappers and the tick's read at C1/42B8.

## 3. Oracle (saves/attract_07d_battle_f4540.state)

Formation `$AF02` = `05 05 51 51 57 FF FF FF` (five enemies). `$A018 = 04 05 06 07 FF` from f80. Idle
`$9907` cmb 3..7 = `04 04 01 01 01`.

**Real time** (`--monitor 7EA124:11,7E9907:11,7EA150:11 --diff`): the effect's load makes lag frames, so
the publishes after the list arm come 4 frames apart instead of 2 and Frog's anim-1A loop stalls
f274-f303. Timing is graded on the logical oracle.

**Logical** (`CT_ORACLE_LOGICAL=1`, same monitor + `7EA145:11`):

| frame | `$A124` cmb 3..7 | `$9907` cmb 3..7 | `$A150` cmb 3/4 |
|---|---|---|---|
| f273 | **01 01 01 01 01** (track 3 + the C1/4D79 list) | 04 04 01 01 01 | 01 01 |
| f274 | 00 01 01 01 01 | **0C** 04 01 01 01 | 00 01 |
| f276 | 00 00 01 01 01 | 0C **0C** 01 01 01 | 00 00 |
| f278 / f280 / f282 | | cmb 5 **03**, cmb 6 **03**, cmb 7 **02** | |
| f297 / f309 / f312 | cmb 7, 5, 6 → 00 (anim 5's end) | | |
| f455 | **01 01 01 01 01** (`06 03`) | | 01 01 |
| f457-f499 | 3/4 hold 01; 5/6/7 walk anim 3 (00 01 02 01) | | |

The `$A012 = 2` gate (one publish every other frame) staggers the five publishes. cmb 3 and 4 (HOLD) drop
`$A124` on their first publish; cmb 5/6/7 keep mode 1 and walk the anim.

## 4. The port (src/battle.c, battleAscTick, cases 0x02 and 0x03/04/05/06/26)

`battleAscPoseArm` took `h->ascRole[t]` as a single combatant. Track 3 armed the primary (cmb 3). Track 4
armed `ascRole[4]` = `$A018[0]` (cmb 4). Tracks 5-7 never run (C1/4BBE). So only two enemies left idle
pose — the two the user saw. Roles 5/6/7 already held cmb 5/6/7 (the `$A018` tail); playAnim just never
walked them. Op 69 already did, which is why the white flash hit all five while the hurt cells did not.

Now: the arm walks `ascRole[4..]` until -1 on track 4 (the idiom op 69/6C/D9 use). The latch/HOLD stay on
`ascRole[4]`; 03/26 wait on every member and advance without clearing the latch; 04-06 wait on the first
and clear it. Op 02 no longer zeroes `$A150` before its arm (C1/4D04 does not write it; that write came
in with 06547ea and had no citation).

Graded: `tools/attract_playanim_list_cmp.py` (check `attract-07d-playanim-list-vs-logical-oracle`) ties
the port's attract to the logical oracle at the action's first arm (port f195 = oracle f122): arms 6/6
identical (frame, combatants, mode), published `$9907[0..7]` identical on all 459 frames of the action.
The HEAD build before the fix differed on 236 of them.

## 5. Open

* ~~Victim *positions* during tech $28~~ CLOSED 2026-09-23 (disasm/notes/field_op8c_place_from_vars.md): not a tech
  mover -- the field staged them wrong (op 0x8C). With them staged right, `attract-07d-frog-squash-colours-vs-oracle`
  rose to 11424 at port f6838 (the battle now stages 8 frames earlier in the port's count).
* The 03/26 track-4 behaviour (wait on every member, latch kept) is ROM-read, not oracle-graded: tech $28
  plays only 06 on track 4.
* `$A145`/`$A150` lifetime ACROSS actions: the port zeroes both at every action arm (battleAscArm); no
  clear of them was found in bank C1. Whether a latch kept by 03/26 survives into the next action on the
  cartridge is unmeasured.
* The `$A018` bytes themselves are the port's `ascRole[4..]` (CC/F156's fill); mem `$A018` is not written
  at arm. Ops that already walk `ascRole` agree with the cartridge's list on this capture.
