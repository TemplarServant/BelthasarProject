# Field actor movement tick (the move-step handler) — reverse-engineering map

Mapped + adversarially verified (workflow `re-field-actor-tick`, 13 agents) by static ROM decode
and the field-gameplay execution trace. This is the orchestration that ties together the already-
validated movement math (direction `C0/ABA2`, velocity `C0/ACFD`, integration `C0/AAFD`) into the
per-frame actor tick — the substrate for porting the field movement to C and retiring the OAM
priority copy-stub (the `$0C00` it stubs is produced here, at step-issue).

## The field frame — three per-actor passes (C0/00C4, verified)
Each frame, the field handler `C0/00C4` calls, in order:
1. `JSR $5A46` — the event-script **scheduler** (NOT a movement loop, NOT `$1B80`-gated).
2. `JSL $FDC2C1 ; JSL $FDFFF7` (REP/SEP around them) — DMA/transfer glue.
3. `JSR $A810` — the per-actor **movement / integration** loop.
4. `JSR $CA76` — the per-actor **animation** loop.

So within one frame: **script-tick → movement-integration → animation**. The C tick must enforce
this 3-phase order to be byte-exact (positions only update after the script tick sets up steps).

## Pass A — script scheduler C0/5A46 + runner C0/5A93 (verified)
`C0/5A46` is a **PPU-scanline-budgeted** loop (reads `$213F/$2137/$213D`, compares the scanline
against budget `$69`, bails when out of time). It walks a **linked actor list** via `$74` (cur) /
`$75` (last) into the next-link table `$1080,X` (`0x80` = end sentinel), calling `$5A93` per actor.

`C0/5A93` script runner: copies op-budget `$68→$67`; `LDA $1180,X` (16-bit script IP) → X; `STX $6D`
(**latch the actor index**); fetch opcode `LDA $7F2001,X`; `TXY`; `ASL` ×2; `JSR ($5D6E,X)` (the
256-entry dispatch table at `C0/5D6E`). After the handler: **carry CLEAR (`BCC $5ABA`) = stop
spending this visit's op budget**; **carry SET** = `DEC $67`, loop to fetch the next opcode (the
5-op budget). On exit, X (post-handler IP) is written back to `$1180[$6D]`.

> **CORRECTED 2026-07-25.** This read "carry CLEAR = yield/stop the tick (**IP left pointing at the
> same opcode** → re-enters next frame)". The parenthetical is wrong. `C0/5ABC-5ABF` (`TXA / LDX $6D
> / STA $1180,X`) writes X back **on both paths** — the carry says nothing about the IP. A handler
> that wants to re-enter the same opcode restores the entry IP itself with `TYX` before its `CLC`
> (op 0xA0 at `5130`/`516B`, op 0x96 at `4F68`/`4FA9`/`4FE8`). Handlers that **advance AND yield**
> exist and are common: SHOW/HIDE/REMOVE (`C0/4CD9`: `TYX / INX / CLC / RTS`) are the clearest case.
> Reading the carry as "did the handler advance" mis-ports every one of them.

> Note for the C port: the scanline budget is timing, not state. A fixed per-frame "tick every
> scheduled actor" is byte-exact for STATE as long as no actor is skipped on a slow frame.

## Pass B — movement loop C0/A810 + integration C0/AAFD (verified)
`C0/A810`: `JSR $0A7F` → `$BD` (actor-count limit); X=0; loop at `C0/A819`: `STX $6D`;
`LDA $1100,X` (actor **state/type**): `CMP #3 BCC`→A870 (state<3 = the integrate branch);
`CMP #8 BEQ`/`CMP #7 BEQ`→A86B (special); else (state 3..6)→A82A (`JSR $AA07`...). Common branch
`A870`: `JSR $AAFD` (integrate), `JSR $A88D` (visibility/onscreen vs camera `$1D0A`), `BCC` skip,
`JSR $C98A`, `JSR $A98A`, `JSR $AB45` (screen coords), `JSR $A9CD`; then `LDX $6D; INX; INX;
CPX $BD; BMI` loop. (The `INX/INX` steps the loop counter by 2 — because **`$6D` holds `obj*2` and
the per-actor tables are 2-BYTE-STRIDED**, see the correction below.) After the loop, `A846/A858`
handle two distinguished actors via `$99`/`$97` (player / focused obj).

`C0/AAFD` integration (already validated byte-exact, layer-5): `LDX $6D; LDA $1B80,X` (active gate);
`LDA $1A01,X; BNE` else `RTS` (counter 0 → no move); `DEC A; STA $1A01,X`; then 16-bit
`$1800 += signExt($1900)`, `$1880 += signExt($1980)`. RTS.

## The step-issue — opcode 0xA0 → C0/5129 (verified byte-exact)
`0xA0` dispatches via `$5D6E` (`$5D6E + 0xA0*2 = $5EAE`, holds ptr `$5129`). Handler `C0/5129`:
- `LDX $6D; LDA $1A01,X; BEQ $5133`. If counter **!= 0**: `TYX; CLC; RTS` → **yield** (the existing
  step keeps running; `AAFD` counts it down).
- `$5133` (counter **== 0**, time to (re)issue): read target X = `$7F2001[IP+1]` → `$F2`, target Y
  = `$7F2001[IP+2]` → `$F3`; current tile hi-bytes `$1801,X`→`$F0`, `$1881,X`→`$F1`.
- **Arrival check** `$514E`: if `$F1==$F3` (Y) and `$F0==$F2` (X) → arrived. **The arrival test is
  TILE-INDEX EQUALITY** — `$1801,X`/`$1881,X` are the HIGH bytes of the 1/16-px position, i.e.
  `px*16 >> 8` = the tile index. Not a distance, not a tile centre, not the ticket reaching zero;
  `$1A01` only decides *when* the test is evaluated (once per `$1000,X` frames).
  Then: `LDA $1C81,X; BIT #$01; BEQ $5164`.
  - **bit0 CLEAR → `$5164` = `TYX;INX;INX;INX;SEC;RTS` = advance IP by 3, MOVE COMPLETE.**
  - bit0 SET → `JSR $305D` (the arrival snap); `BCS $516A` (`TYX;CLC;RTS` = yield while the snap is
    still running), falling through to the same `$5164` complete when the snap finishes.

  > **CORRECTED 2026-07-25.** This bullet previously read "`BEQ $516A` (`TYX;CLC;RTS` = idle at
  > target, yield) else `JSR $305D`" — wrong branch target **and** inverted semantics: it made the
  > common case (`$1C81` bit0 clear, which is the DEFAULT — op 0x0E sets it and most objects never
  > do) *yield forever at the target* instead of completing. Re-disassembled: `515D: BEQ ->5164`,
  > and `5164` is the SEC/complete tail while `516A` is the CLC/yield tail. Verified against the
  > oracle on loc $082 obj32 (`$7E1CC1` = 0 all run, and its `A0` legs do complete).
- **Not arrived** `$516D` (issue/refresh a step):
  - `JSR $ABA2` — atan2 direction → heading `$EE` (+ facing `$1600`), from (target−current) delta.
  - `JSR $5B95` — 1-tile-ahead actor collision; `BCC $517C` if clear, else `LDA $EE; CLC; ADC #$30;
    STA $EE` (single +0x30 heading nudge, no retry loop). (Dormant for field NPCs.)
  - `$517C`: `LDA $1000,X; STA $1A01,X` (**reload the move counter** from the per-actor move-duration
    WRAM `$1000,X`); `STA $7F0B00,X` (mirror). `STY $C7; JSR $ACFD` (build velocity+priority);
    `LDX $C7; CLC; RTS` → **yield** (the step now runs; `AAFD` integrates it over `$1A01` frames).

So each frame the runner re-enters `0xA0`; while `$1A01>0` it yields; at `$1A01==0` it re-derives
direction+velocity toward the (static, script-given) target, reloads the counter, yields; when the
current tile reaches the target it completes (advances the IP). **Direction/velocity/priority are
recomputed once per step (every `$1000,X` frames); integration runs every frame.**

## Velocity + priority build — C0/ACFD..AE25 (verified)
`C0/ACFD` (the real callable entry; "AD02" in old notes is mid-routine): `STZ $1900,X; STZ $1980,X`;
`LDA $1A00,X` (**speed**) → `$4202`; `LDX $EE`; `LDA $F800,X` → `$4203` (HW 8×8→16 multiply);
read `$4217` (high byte), apply sign per quadrant → `$1900` (X = `table[$EE+0x40]` = cos) and
`$1980` (Y = `table[$EE]` = sin). `$C0:F800` = 256-entry signed-sin table. **This routine continues
into the priority computation** `AD61..AE25` (the `$0C00/$0C01` gate + cell, see `oam.md`), so the
step-issue sets the OBJ priority too — which is why retiring the OAM priority stub requires this tick.

- **Speed `$1A00`** is set by a script setup opcode (`LDA $7F2001,X; STA $1A00,X`, ~`C0/4B4B-4B53`),
  persisting until overwritten. (Verifier REFUTED the earlier guess that this is opcode `0x89` at
  `4B49` — `0x89` is `BIT #imm`; the real speed-set is at ~`4B4B` and does NOT run in the field-walk
  trace, i.e. it is set once at script init, not per step.)
- **Counter `$1A01`** is the per-step frame count, reloaded from `$1000,X` — distinct register,
  distinct source.

## Pass C — animation loop C0/CA76 → CAD9 → CAE2 (verified structure)

> **The drain body is now derived in full: see [`anim_drain_cad9.md`](anim_drain_cad9.md).** `CAD9`'s dispatcher, the four-slot arena `C0/E952-EA62` and class 0's frame half are PORTED and graded (`--sweepcad9`); class 0's tile half and classes 1/2 are mapped but not ported. That file also carries three corrections to what this section implies — the `CB0A` twin is a different job, not a variant; class 0 never returns SEC; and `$1201,X & 3` is the sprite's PIECE COUNT.
Same shape as the scheduler (PPU-scanline budget; scheduling indices `$76/$77`; link table
`$1081,X`, `0x80` sentinel), dispatching `JSR $CAD9` → `LDX $6D; LDA $1100,X; BPL` (state gate) →
`C0/CAE2` anim-frame handler. `$0F01` advances by a **sequence-table lookup** keyed on facing
`$1600` + walk-offset `$1480` + base `$1500` (`$FF` = end sentinel), and **commits only when the
looked-up value differs from the current `$0F01`** (`CC04 CMP $0F01,X; BNE` → `STA $0F01,X`, then
rebuild the OBJ tiles). It is **decoupled from the move counter** — animation ticks every frame.

> **CORRECTION (2026-07-30, `--sweepc98a`).** "animation ticks every frame" is right about the *timer* and wrong about the *phase*. `$76`/`$77` are not "scheduling indices" in the loose sense — they are the **head and tail of a singly-linked FIFO** whose next-pointer is `$1081,X`, and the phase `INC $1681,X` fires **only on an accepted enqueue**. See the section at the bottom of this file.

## Per-actor state the C port must maintain (invariants)
- `$6D` (1-byte actor index): set before any per-actor sub-call. **`$6D` = `obj*2`, and every
  per-actor table is 2-BYTE-STRIDED across a `0x80`-byte span** (`$1180`, `$1800`, `$1880`, `$1C00`,
  `$1A00`, `$0F00`, `$1100`, …). `0x80` is the SPAN of one table (64 objects × 2), never the stride.

  > **CORRECTED 2026-07-25.** This line, and the `A870` loop note above, both said "all per-actor
  > WRAM is `0x80`-strided keyed by `$6D`". Established by MEASUREMENT on the oracle: obj32's script
  > IP is `$7E11C0` = `$1180 + 0x40` = `$1180 + 32*2`; its priority `$7E1C40`, its position
  > `$7E1840`/`$7E18C0`, its solid flag `$7E1B41` — all `obj*2`. This resolves the byte-exactness
  > hazard flagged in `_collision_specs/14_VERDICT_C0_5B95…json`.
  >
  > **Second correction, same day, to the first correction's REASONING.** It originally argued the
  > `0x80` stride was "impossible on its face, because `$6D` is a one-byte DP cell loaded into an
  > 8-bit X". **That argument is wrong** — the index is **16-bit** inside every opcode handler. The
  > runner does `5A9E: REP #10` before `5AB1: JSR ($5D6E,X)`, so although `C0/00BF` enters the VM
  > with `SEP #10`, X is widened again before dispatch and `LDX $6D` reads `$6D/$6E` as a word.
  > The conclusion is unchanged and rests on the measured addresses above, not on the register width.
- `$1180[idx]` (16-bit script IP): persists; only advance on carry-SET (by 3 for `0xA0`); a yield
  leaves it pointing at the same opcode.
- `$1A00[idx]` speed (script-set, persists) vs `$1A01[idx]` counter (reload from `$1000,X`, −1/frame).
- `$1800/$1880[idx]` world pos (16-bit subpixel; hi-byte = tile, used for arrival).
- `$EE` heading is a **global scratch** (overwritten per actor each step-issue); `$1600` facing is
  per-actor (not touched by the `0xA0` path).
- `$1B80[idx]` active flag + `$1100[idx]` state class gate integration + the `A819` branch.
- `$0F01/$1480/$1500/$1600` animation state (advanced in pass C, after movement).

## Resolved: the two former unknowns + the real anim-advance driver
- **`$1000,X` (move-duration) — RESOLVED: init-time.** Written only by the actor/scene SETUP opcodes
  (`C0:42DE/4353/4406/4888/6277/6290`, region `42xx-62xx`); NONE of these run in the field-walk
  trace — i.e. `$1000,X` is set once when the actor's script/scene is loaded, then read at every
  step-issue. For the port: seed it from the oracle (it is already correct in `g_ram` via the copy-
  stub) and the `$1A01` reload is byte-exact. No per-frame logic to port.
- **`$1480` (walk-offset) — RESOLVED: init-time, NOT the per-frame driver.** Written only by setup
  opcodes (`C0:44FC/4604/4753`), none in the walk trace. It is a STATIC base into the anim sequence.
  The actual per-frame animation advance is a different pair (below).
- **Anim per-frame advance — RESOLVED: the `$1601`/`$1681` timer+phase pair** (driven from the
  MOVEMENT loop, not the anim loop). In `C0/C98A` (called from `A870` per moving actor): `DEC $1601,X`
  (the per-actor anim frame-TIMER); if it hits 0, take the advance path which `INC $1681,X` (the
  walk-PHASE / sequence index, at `C9C0`/`C9CB`) and RELOADS the timer `$1601,X` (at `CA45`/`CA72`,
  the anim-speed); else `RTS` (no advance this frame). The anim pass `CB3A` then forms the sequence
  pointer `$D6 = $1480*2 + $1500 + $1680*4 + $1681` and reads the frame byte `[$D6]` (`$FF` = end).
  So: timer counts down each frame, expiry bumps the phase + reloads the timer, the lookup maps
  (`$1480` base, `$1500` seq-base, `$1681` phase, facing `$1600`) → the new `$0F01`. (Confirmed in
  the trace: `C98A`×136, `C991 DEC $1601`×134, `C9CB INC $1681`×14, `CC0B $0F01-commit`×6.)

## Remaining minor unknowns / risks
- `$1601` reload VALUE (the anim speed at `CA45/CA72`): its source byte (a per-actor anim-rate or a
  table) is not pinned to the bit; the reload SITE is confirmed. Pin it when porting the anim pass.
- The `$76/$77` + `$1081,X` anim-SCHEDULE linked list that `C98A`/`CA09` walk (parallel to the
  script scheduler's `$74/$75`/`$1080`): its full maintenance is mapped only at the advance site.
- `$BD` (actor-count from `$0A7F`) + the `A819` `INX/INX` (+2) stride semantics — verify before the
  state-gating port.
- `A88D` onscreen/visibility + `C98A/AB45` post-integration calls may also touch position/priority/
  screen-coord state — relevant to the render port.
- Arrival path `JSR $305D` (only when `$1C81&1`) — purpose unknown (likely an on-arrival event).

## Incremental port plan (each step validatable byte-exact vs the oracle)
0. **Foundation**: a `--check` hook that snapshots per-actor WRAM (`$1800/$1880,$1900/$1980,$1A00,
   $1A01,$EE,$1600,$0F01,$1100,$1B80`) at the frame boundary after `C0/00EA` — pins the comparison.
1. Velocity formula (`$F800` sin, magnitude-then-sign) — **done** (`--checkvel`).
2. `$1A01` reload from `$1000,X` (+ mirror `$7F0B00,X`) — validate `$1A01` at step-issue frames.
3. The `0xA0` step-issue control flow exactly as `C0/5129` (yield/arrive/issue, the `$305D`+`$1C81&1`
   arrival gate, the `$5B95`+`+0x30` nudge branch) — validate `$EE/$1600/$1A01/$1900/$1980` per frame
   across a multi-tile move opcode.
4. Restructure the C tick into the real 3-pass frame (`C0/00C4` order): script scheduler → movement
   integration → animation — validate full per-actor `$1800/$1880` for a walking NPC.
5. The `$1100` actor-state gating (`CMP #3/#7/#8` at `A819`) + the two distinguished-actor passes
   (`$99/$97`).
6. The animation pass (`CA76→CAD9→CAE2`): the `CB3A` sequence-table lookup + the "commit only if
   different" `$0F01` update (first just `$0F01`, defer the OBJ-tile rebuild to the render path).

> Scope: this is the field actor engine — M2-scale, coupled to the field event VM (the runner + the
> move/setup opcodes + the dispatch table). The movement MATH is validated; the remaining work is the
> stateful 3-pass tick + enough of the event VM to drive it. Retiring the OAM priority copy-stub falls
> out once `$0C00` (step-issue) and the anim-decode caching run in C.

## Port status — the field NPC movement engine is ported + validated (`--portrun` 1592/1592 BYTE-EXACT)
The whole field NPC movement subsystem is ported to C and validated stateful vs the oracle:
- `CtActorMoveStep` (0xA0 step-issue) — `--checkstep` velocity/priority/counter **13/13 byte-exact**.
- the runner `CtFieldRunner` (C0/5A93 + dispatch + opcodes 0xA0/0xA6/0xB1/0xB2) + the move-family
  (`ctMoveToActor` for 0x94/0x95 and the perpetual wrappers 0xB5/0xB6) + the scheduler qualify
  predicate (skip while `$1001 > 0`, the `$1000`/`$1100` gates) + `CtActorIntegrate` (the state-3..6
  integration) — `--portrun` **1592/1592 = 100% byte-exact** owned-actor-frames.

### RESOLVED: the former 0.4% tile-crossing residual — it was the integration routine.
The old residual (a 1-frame phase offset at tile boundaries, e.g. idx 0x12 at f270: oracle pauses at
`$14FB` while the C port advanced to `$1504`) was the C port using the simple AAFD integrate for
**state-4** NPCs. The A810 loop routes state<3 -> `AAFD`, state 3..6 -> `A82A: JSR $AA07`, state 7/8 ->
`A86B`. So moving NPCs (state 4) integrate via **C0/AA07**, which AAFD-style adds the velocity but with a
**tile-boundary CLAMP** gated by `$7F0B00,X` (the move-duration mirror, set = `$1000` at each step-issue
at C0/5184): on a step's FIRST integrate frame `$7F0B00 > 0` -> zero it and cross tiles freely (AA29);
on later frames `$7F0B00 == 0` -> a move that would cross a tile instead `STZ $1A01` (stop the counter)
and does NOT cross (AA7E) — the 1-frame pause; the crossing then happens on the next step's first frame.
(`$1B80 != 0` actors take the `AAC7` simple 16-bit-add path with a `$1B81` sub-pixel accumulator instead.)
Ported as `src/move.c:CtActorIntegrate`; `--portrun` dispatches state-3..6 actors to it -> 1592/1592.
Found via `--tracenpc` (per-frame chain) + `--watchnpcpos` (per-opcode pos-write watch).
Players/focus actors (`$99`/`$97`) are excluded from the runner (they move via C0/A846/A858, the
input-driven distinguished-actor pass — now byte-exact via `--playerframe` 270/270).

## The move-to-tile — opcode 0x96 -> C0/4F61 (derived 2026-07-25, m=1 / x=0)

Structurally the twin of op 0xA0 (C0/5129) with two differences: it sets the FACING at issue, and its
arrival tail calls `C0/568B`.

```
4F61: LDX $6D / LDA $1A01,X / BEQ ->4F6B
4F68: TYX / CLC / RTS                      ; ticket busy -> yield, IP parked
4F6B: TYX / INX / LDA $7F2001,X -> $F2     ; target tile X
4F73: INX / LDA $7F2001,X -> $F3           ; target tile Y
4F7A: LDX $6D
4F7C: LDA $1801,X -> $F0                   ; CURRENT tile X (high byte of the 1/16-px position)
4F81: LDA $1881,X -> $F1                   ; CURRENT tile Y
4F86: CMP $F3 / BNE ->4FAB
4F8A: LDA $F0 / CMP $F2 / BNE ->4FAB
4F90: LDA $1C81,X / BIT #01 / BEQ ->4F9C
4F97: JSR C0/305D / BCS ->4FA5             ; snap still running -> 4FA5
4F9C: JSR C0/568B
4F9F: TYX / INX / INX / INX / SEC / RTS    ; COMPLETE, IP += 3
4FA5: JSR C0/5614 / TYX / CLC / RTS        ; snapping -> re-anim, yield
--- not arrived ---
4FAB: JSR C0/ABA2                          ; heading -> $EE
4FAE: REP #20 / LDA $EE / TAX / SEP #20
4FB5: LDA $F700,X / LDX $6D / STA $1600,X  ; FACING from the heading table, AT ISSUE
4FBD: JSR C0/5B95 / BCC ->4FD2             ; object-collision test
4FC2: LDA $EE / CLC / ADC #30 / STA $EE / TAX / LDA $F700,X / STA $1600,X   ; +0x30 sidestep, re-face
4FD2: LDX $6D / LDA $1000,X / STA $1A01,X / STA $7F0B00,X                   ; arm the ticket
4FE0: JSR C0/ACFD                          ; ISSUE THE VELOCITY -- same visit
4FE3: JSR C0/5614                          ; re-anim (walk)
4FE8: CLC / RTS                            ; yield, IP parked
```

**There is NO separate "setup" visit.** Facing, ticket and velocity are all written on the visit that
finds the object not-arrived. Arrival is TILE-INDEX EQUALITY, and `$1C81` bit0 CLEAR completes
immediately — the same law as op 0xA0.

> **Consequence for the port, recorded rather than acted on.** `src/ct_vm.c`'s op-0x96/0x97 "fresh-move
> SETUP delay" (first visit sets only the facing, `cycles = 0`, and yields) is **INVENTED** — its own
> comment justifies it from oracle timing, not from this handler. It also double-counts against the
> ROM's real SHOW/HIDE/REMOVE visit-yield (C0/4CD9 ends CLC): `vmdiff-DEMO8` moves 109 -> 69 when that
> yield is added, and back to 109 when this setup visit is removed. But deleting it turns
> `camera-reenter-scene5` and `field-battle-ambush-generic-dissolve` RED, so something else in the
> movement pipeline leans on the extra visit. Per the mandate that is a shared-MECHANISM fault: find
> the compensating error before deleting this, and ship the pair together.

## The visit budget and the carry model (derived 2026-07-25/26)

**The per-visit op budget is FIVE, with no exemptions and no second cap.** `C0/5A93` copies `$68` into
`$67`; `5AB4 BCC` ends the visit on carry-clear; `5AB6 DEC $67 / 5AB8 BNE` spends one and loops on
carry-set. `$68` is **`$0168`** (DP is `$0100` throughout the scheduler, visible in the `--trace` DP
column) and its only writer is `C0/0BCA: LDA #05 / STA $68`; it reads back as 5 on field, overworld
and menu states. **Measured: 1467 oracle visits across three states — maximum 5, NEVER 6, and 95%
ended on the first CLC.**

> **The port's old "control-flow ops are free" exemption was INVENTED**, and is refuted twice over:
> `DEC $67` is unconditional on the SEC path, and the GOTO handlers return SEC (`op 0x10 → C0/62B5`,
> `op 0x11 → C0/62CB`, shared tail `62C6 TAX / SEP #20 / SEC / RTS`). Its companion cap `0x68` was the
> direct-page **address** of the budget read as a count. The 5-op visits the oracle does perform are
> precisely the pure control-flow poll loops the exemption freed (`11 12 11 12 11`, `22 11 22 11 22`);
> ours ran those to 104 ops. `op 0x17` was also in that "control flow" set and is **set-facing**.
> The raster gate is real but lives in the OUTER scheduler `C0/5A46`, **between actors** (`5A5D CMP $69`,
> `$0169 = 0x0048`), and bailed 0 times in 1687 evaluations.

**Handlers that ADVANCE the IP and END the visit** (CLC with no SEC before the RTS), each confirmed
from its `C0/5D6E` entry:

| ops | handler |
|---|---|
| `0x7C 0x7D 0x7E 0x90 0x91` | the shared `C0/4CD9` tail |
| `0x2A` `0x2C` `0xAC` | `C0/3ED6` `C0/3EE6` `C0/2F71` |
| `0xE4` `0xE5` | `C0/3D97` `C0/3DC5` |
| `0xE8` `0xEA` `0xEC` `0xED` `0xEE` | `C0/3C11` `C0/3C2F` `C0/3C98` `C0/3CBC` `C0/3CC5` |
| `0xF0` `0xF2` `0xF3` | `C0/3CD0` `C0/3CFD` `C0/3D7A` |

### RESOLVED — the fifteen that were open (derived 2026-07-26)

All fifteen were traced from their `$5D6E + op*2` table entry to every reachable `RTS`, then
**adversarially re-derived by independent agents: 15/15 survived refutation, 0 disputed.**
**Every one is CLC.** There was no SEC hiding anywhere in the set.

| ops | handler(s) | IP delta | note |
|---|---|---|---|
| `0x0F` `0x17` `0x1B` `0x1D` | `C0/5535 5539 553D 5541` | +1 | `LDA #n / BRA 554B` (self) |
| `0xA6` `0xA7` | `C0/5545 5557` | +2 | `0xA6` falls into the tail; `0xA7` BRAs to `554B` |
| `0x1E` `0x1F` `0x25` `0x26` | `C0/556C 5579 5586 5593` | +2 | BRA to **`554D`**, skipping `LDX $6D` |
| `0x2B` `0x32` | `C0/3EDE 3EE2` | +1 | `LDA #n / BRA 3ED8` — `0x2A`'s own tail |
| `0xFE` | `C0/3F26` | +18 | single RTS at `3FAE` |
| `0xEB` | `C0/3C4C` | +3 | already had its own yield in the port |
| `0xE2` | `C0/3B9E` | **0 (PARK)** | CLC on both paths — see below |

The shared tail, and the detail that explains the whole family:

```
5545: INY / TYX / LDA $7F2001,X    <- 0xA6 enters here
554B: LDX $6D                      <- 0x0F/0x17/0x1B/0x1D/0xA7 BRA here  (X := SELF)
554D: STA $1600,X / STZ $1601,X    <- 0x1E/0x1F/0x25/0x26 BRA here (X is still the OPERAND
5553: TYX / INX / CLC / RTS           from their TAX -> the write targets ANOTHER object)
```

Entering at `554B` re-loads X from `$6D` (the runner's `5A9C: STX $6D`) so the write hits **self**;
entering one instruction later at `554D` leaves X = the operand, so it hits **another object**. That
single skipped instruction is the entire self-vs-cross-object distinction.

**`0x0F/0x17/0x1B/0x1D` are the four self-facing ops** (`LDA #0/1/2/3` → `$1600,X`) and
`0x1E/0x1F/0x25/0x26` are their cross-object twins.

> **`0xE2` is the one that must NOT go in a flat yield list.** Both its paths are CLC, but both have
> IP delta **zero**: `3BA2 STY $C7` saves the **entry** IP *before* the four `INY`s, and `3BFE LDX $C7`
> restores it. It is a PARK-and-yield — the op-`0xE0` ChangeLocation pattern, and it arms `$0117` bit7
> itself (`3BF4: ORA #80`). A flat `return 1` would fix its carry and **break its IP**. I first read
> this as advancing +4; the adversarial pass caught it. The port implements no effect for it at all.

> **RETRACTED: "the static 40-op classification is wrong about `0xFE`".** It is not. The `SEC` that
> observation found is at `3F54`, inside `3F52: REP #20 / 3F54: SEC / 3F55: SBC $F0` — **carry setup
> for a 16-bit subtraction**, not a return carry. `0xFE`'s only reachable RTS is `3FAB: LDX $C7 /
> 3FAD: CLC / 3FAE: RTS`. A static scan for "contains a SEC" cannot distinguish an arithmetic SEC
> from a verdict, which is exactly why every op here is traced to its RTS.

**`0xA6`'s hold-out is OVERTURNED.** It was held back because a fourteen-op batch had once stalled the
textbox typewriter (`tbReveal` at 0 through 400 frames) while the render hash stayed byte-identical —
the suite going green on a broken engine. Right call at the time; the ROM fact was never in dispute,
the *substitute evidence* was missing. It now exists as a **positive** test rather than an absence of
red: scene 1, `tbReveal` at f60/f120/f240/f400 = **42 / 60 / 60 / 60** with `textbox=1 idx=10` — the
box opens, the typewriter runs to the string length, and saturates. And holding one op out of a
**ten-op shared tail** is the per-op special case the mandate forbids: nothing in the ROM separates
`0xA6` from its nine siblings.

Two port-only signatures move, both predicted, neither oracle-compared:
`camera-reenter-scene5` `reveal 4 → 0` with cam, fgpx, fgskip **and `fnv32=571C8700` all identical
(a race against `--reentertest`'s synthetic frame-3 teardown)**, and
`field-battle-ambush-generic-dissolve`'s composed f362 hash, with the battle outcome byte-identical.
Every oracle-comparable measure holds: DEMO8 109, walk-touch-preempt f129, `--checkstep` 13/13,
`--checkdir` 60/60, `--checkmove`, `--porttick`.

**Found on the way, not shipped:** the shared tail also does `STZ $1601,X` — the walk-anim frame
timer `C0/C98A` decrements — so every ROM facing-set *also* resets the walk-phase clock. The port
does none of that, and implements no effect at all for `0x1E/0x1F/0x25/0x26/0xA7/0x2B/0x32/0xFE/0xE2`.
Those are missing EFFECTS, independent of the visit boundary, and are their own task.

## The lead-commit inversion in field.c — structural, and NOT yet isolated

The ROM's frame, measured (`--monitor --writer` names `C0:AB28` as the writer of the lead's `$1800`
on every frame, so `C0/881E` computes velocity and does **not** commit):

```
C0/00A7  JSR $881E                    velocity, collision, the C0/9923 touch scan
C0/00C4  JSR $5A46                    the event VM   <- reads $1801 HERE
C0/00D3  JSR $A810 -> AAFD -> AB25    the position COMMIT
```

`field.c` is inverted against that: `fieldPlayerCollide` integrates (`*px += vx/16.0`), then
`CtSceneSetActorPosF` pushes the lead into the VM (~`:6709`), and only then does `CtSceneTick` run
(~`:6809`). So our VM sees the lead's position one integrate step earlier than the ROM's does.

⚠ **That is a code reading, not a measurement, and the obvious probe does not settle it.**
`CT_LOCALDBG` (added with `CtSceneMemLoc`) dumps `$7F020C` — the script local obj0 mirrors the lead
tile into with `22 00 06 07` — next to the live lead tile, every frame. On `party3_field` walking left:

```
PORT     tile -> 0x28 at f31   $7F020C -> 0x28 at f32   (+1)
ORACLE   tile -> 0x28 at f32   $7F020C -> 0x28 at f34   (+2)
```

Two effects are superposed and neither can be subtracted out here:

1. `$7F020C` only changes on the frames **op 0x22 actually runs**, and obj0's poll loop runs it every
   ~3 frames — a cadence coarser than the one-frame effect under test.
2. The lead's own motion is already a frame out of phase between the two engines (tile changes at
   f31 vs f32) — the known `--playermove` residue, unrelated to VM ordering.

With the lead's motion itself offset by a frame, a one-frame difference in *when the VM reads* cannot
be separated from it on this scenario. **Do not change the main loop's order on the strength of the
structural reading alone**: the ~30 `Belthasar` checks can only report "nothing broke", which is not
the same as "this is now right".

What would isolate it: a scene whose script reads the lead tile **every frame** (no poll cadence), on
a scenario where the lead is script-driven rather than input-driven (removing the `--playermove`
phase residue) — or a `Belthasar`-side lockstep that replays an oracle WRAM stream, which is the same
host problem recorded for the write mask (`ct.exe` links neither `field.c` nor `battle.c`).

## `$0F01,X` — the first finding of the CT_FIELDLOCK host, triaged

The common host (`CT_FIELDLOCK`, the oracle inside `Belthasar`) reported a whole class of divergence
on its first run, across many objects from frame 0:

```
[fieldlock] f0  $7E0F01 m=FF ours=00 oracle=FF     obj0
[fieldlock] f0  $7E0F03 m=0A ours=0B oracle=01     obj1  (the lead PC)
[fieldlock] f0  $7E0F07 m=13 ours=11 oracle=02     obj3
```

**It is a SEMANTIC mismatch, not a phase one** — which is what the first data point misleadingly
suggested, so it is worth recording how that resolved.

**The ROM has exactly one writer**: `C0/CC14 STA $0F01,X` (the writer PC reports as `CC17`, the
following instruction), inside the ANIMATION pass `C0/CA76`, which the main loop calls at `C0/00D6`
— after the event VM and after `C0/A810`. It is **gated**: `CC0B STA $EE / CC0D JSR C0/E952 /
CC10 BCC ->CC09 / CC12 LDA $EE / CC14 STA $0F01,X`, so the store happens only when `C0/E952`
returns carry set.

**And it steps slowly.** Measured on `party3_field` walking left:

```
$0F07 (obj3)   02 (seed) -> ... -> 12 @f31 -> 13 @f38 -> 14 @f45 -> 15 @f52 -> 10 @f59
$0F09 (obj4)   02 (seed) -> ...    11 @f47 -> 12 @f54
```

One step per ~7 frames, and it wraps (`15 -> 10`). That is an **animation/assembly-frame cursor**,
not a priority.

**What the port writes is a different quantity.** `field.c` mirrors `actorAf[i]` — the renderer's
live assembly frame — into `$0F01 + obj*2` every frame (`~:7135` for NPCs, `~:7231` for the lead).
At frame 0 ours is already `0x11` where the ROM is still at its seed `0x02` and takes ~24 frames to
get there. So this is not our cursor running early; it is a different number entirely.

> The first measurement looked like a one-frame phase difference — obj1's `$0F03` reads `0x0B` on our
> side at f0, and the oracle writes exactly `0x0B` at f1. That coincidence is what makes the case
> worth writing down: one address agreeing with the next frame is not evidence of a phase bug when
> another address in the same class is twenty frames apart.

**obj0** is a third case again: ours `0x00`, oracle `0xFF` constant. obj0 is class 7, which the ROM
never draws, so its `$0F01` keeps the `0xFF` the scene-entry sweep wrote (`C0/B185`) and no anim step
ever overwrites it.

### The commit path, derived — and a correction to what I first wrote here

> **RETRACTED: "the `C0/E952` gate that paces it".** `C0/E952` paces nothing. It is a **4-slot
> tile-buffer ALLOCATOR**, the same shape as the OBJ palette allocator:
> ```
> E952: LDX #00
> E954: LDA $0BC0,X / BPL ->E973          ; occupied -> next slot
> E959: LDA $6D / STA $0BC0,X             ; claim: owner := obj*2
> E95E: TXA/XBA/AND #FF00/ASL A/ADC #3800 ; addr = 0x3800 + slot*0x200
> E96A: LDX $6D / STA $0D80,X             ; the buffer address, per object
> E972: SEC / RTS                          ; success
> E973: INX / CPX #04 / BMI ->E954         ; only FOUR slots
> E978: CLC / RTS                          ; none free -> FAIL
> ```
> It refuses only when all four buffers are in use. The ~7-frame stepping I measured is the
> animation sequence's own duration, resolved by `C0/CB3A` — nothing to do with `E952`.

The full commit path at `C0/CC04`:

```
CC04: CMP $0F01,X        ; A = the new frame from the sequence lookup
CC07: BNE ->CC0B         ;   different -> proceed
CC09: CLC / RTS          ;   SAME -> do nothing, and claim NO buffer
CC0B: STA $EE
CC0D: JSR C0/E952        ; allocate a tile buffer
CC10: BCC ->CC09         ;   none free -> bail, $0F01 UNCHANGED
CC12: LDA $EE / CC14: STA $0F01,X          ; commit
CC17: REP #20 / LDX $6D / LDA $0D80,X / STA $D0   ; upload target
```

So `E952` is reached **only on a frame change** — which is why four buffers suffice.

The lookup itself, `C0/CB3A`, confirms the structure this file already recorded: it is keyed on the
facing `$1600,X` and combines the walk offset `$1480,X` with the base `$1500,X`
(`CB3D BEQ` facing 0 → base only; `CB45` facing 1 → `$1480 + $1500`; `CB41`/`CB43` split facings
2 and 3). That half was already right.

### Status: NOT fixed, and what it needs

Three things are unported, and the port models **neither** `$0BC0` nor `$0D80` (zero hits in
`src/`):

1. the sequence lookup `C0/CB3A` as the source of `$0F01` (we use the renderer's own `actorAf`),
2. the **CMP guard** — commit only when the frame actually changes,
3. the `$0BC0` four-slot buffer allocator and its refusal path.

It is not cosmetic: the battle staging reads `$0F01` as its `+4` field (`C0/0557`, ported at
`field.c` `r[4]`), so a wrong value reaches a staged combatant record.

Do **not** "fix" it by re-timing the existing mirror. The quantity is wrong, not its schedule — and
the refusal path means even a correct value must not always be committed.

## `C0/C98A` — the anim timer, and the QUEUE the phase advance rides on (derived 2026-07-30)

Ported byte-exact and graded by `--sweepc98a` (116640/116640 over a synthetic cross-product of every
input the routine reads). It is **two routines welded together**, and only the second half had been
ported.

### `C98A`–`CA06` — the ENQUEUE

`$76` = head, `$77` = tail of a singly-linked FIFO of objects waiting on the sprite assembler.
`$1081,X` is the next-pointer; **any value `>= $80` is the null** (the drain writes `$80` literally,
`CABB`/`CACA`). `$1100,X` picks which end the object joins:

| `$1100,X` | path | end |
|---|---|---|
| `>= 3` | `C9A6` | tail (`$77`) |
| `0` | `C9D0` | head (`$76`) |
| `1` or `2` | `C9E2` | tail — **unless** the one-shot `$78 == 2`, which routes to `C9D0` and clears `$78` |
| bit7 set | `C99C BMI` | never enqueues |

Two rejections, and **the phase `INC $1681,X` lives on the accept paths only** (`C9C0`, `C9CB`,
`C9DD`, `CA06`):

- `C9A6 CPX $77 / BEQ` — already the tail.
- `C9AE LDA $1081,X / BPL` — already has a next-pointer, i.e. already in the list. (Checked only on
  the tail path; `C9D0`'s head push does not test it.)

The empty-list case is `C9C6`, reached from `C9B6 CPX #80 / BPL` and from `C9D6 BMI`: it sets
**both** ends to this object. A head push (`C9D8`/`C9DB`) writes `$1081,us = old $76` and leaves the
tail alone; a tail append (`C9BA`/`C9BD`) writes `$1081,oldTail = us`.

### The DRAIN — `C0/CA76`, `CAA2`–`CAD3` (read, not ported)

`CA76` is raster-budgeted (`$213D` scanline latch vs `$6B`, `CA9B BCS -> CAD6`). Each pass:
`LDA $76 / BMI -> RTS` (empty), `STA $6D`, then `JSR $CAD9`. **`BCS` loops back WITHOUT dequeuing** —
the object stays at the head until `CAD9` returns CLC, which is how a multi-part sprite build spans
frames. On CLC it pops: `$76 = $1081,head`, `$1081,head = $80`; and when `head == tail` it empties
the list by setting `$76 = $77 = $80`.

**This is a feedback loop.** An object still queued when its timer next fires does not advance. The
animation therefore advances at the rate the assembler drains, *not* at the rate the timer fires.

### The three defects `--sweepc98a` found

1. **The enqueue was a flat gate** — `st == 0 || st == 1 || st == 2 -> return, no advance`. Two
   errors at once: those states *do* advance, and state `>= 3` does *not* advance when already
   queued. Revert → 48564 mismatches (33744 anim-state, 41472 queue).
2. **The `CA23` → `CA2E` carry.** `CLC / ADC $1580,X / STA $C1 / <phase> / ADC $C1` — the second
   `ADC` has **no `CLC`** in front of it, so the 16-bit carry out of the first lands in the bank-`$E4`
   table index. Revert → 3024.
3. **The mode-2 ping-pong** (`CA4F`–`CA69`), the item this sweep was built to grade. Revert → 2856.

Why the flat gate survived: in the regime where the drain **keeps up** (queue empty at every timer
fire) it happens to be right for state `>= 3` — the only state the tracked scenarios reach. It is
wrong exactly under raster pressure and for states 0/1/2, and no scenario check can manufacture
either. `--porttick` (128/136) and `--portrun` (1592/1592) are unchanged by all three fixes.

### Scope ceiling

`CtAnimTimerTick` is called from the **harnesses only** (`--porttick`, `--sweepc98a`); `src/field.c`
does not call it and `move.c` is not linked into `Belthasar`. So this grades a ported ROM routine,
**not shipped engine behaviour** — which is also why porting the enqueue could not jam the live
engine. The drain's *body* (`C0/CAD9` → the sprite assembler) remains unported; only its bookkeeping
is derived above, from reading it. That body is still the blocker for `$0F01` proper.

## The lead's words in the event pass: op 0x22 reads last frame's commit (2026-10-18, ROADMAP F3 closed)

The ROM's frame: C0/881E (the controller: the lead's velocity, the camera's step) -> C0/00C4 the event pass -> C0/00D3
C0/A810, whose C0/AAFD adds the velocity to `$1800/$1880` (the commit). So op 0x22 / 0x21 (C0/65EB / 65B0 -> 65B2 `LDA
$1801,X` / `LDA $1881,X`) reads the lead's tile as LAST frame left it. The port's host steps the lead before the tick
(fieldPlayerCollide -> CtSceneSetActorPosF: fx/fy), and the tick mirrors fx/fy into `$1800/$1880` only at its A810 point --
so the WRAM words in the pass are the ROM's; the live op read fx/fy, a step ahead. It reads the words now (ct_vm.c, op
0x22 / 0x21's live arm; the init pass already did).

MEASURED, loc 120 below the castle stairs (saves/field_re/scene468_stairs_to_120.state, Down from f180; the port's keys a
frame after the oracle's): the lead 0AFF -> 0B0F at f205; obj0's `22 00 09 0A` (every 4 frames) stores row 0A at f205 on
the cartridge and 0B at f209; Lucca's touch-driven poll (her fn2 RETURN restoring slot 7 at 0A68) posts `C1` at f211 and
`$0129` = 01 at f211, the window at f216, the typewriter f226, state 7 f232 -- the port the same on every frame (it had
stored 0B at f205 and opened the box at f208: the "4 frames early" of F3, one of them the key phase). Check
lockstep-scene120-stairs-entry-lucca, now pinned to the cartridge's values. ct.exe's `--vmdiff` injector writes the lead's
and followers' words as well as fx/fy (it injects the previous frame's commit, which is what the pass reads).

## The idle counter $0158 and the blink roll (2026-10-18)

`C0/EC60` -- the wait every field loop ends its frame in (the main loop's C0/00BA, the fades', the return's, the styles')
-- opens with `REP #20 / LDA $0158 / INC A / AND #83FF / STA $0158` (writer C0:EC6C). C0/0C1A zeroes it at a load (it
does not count through the load's NMI-off frames), the lead's walk / run arms C0/A171 / A18C on every step. The idle chain
C0/A1E9 (A1F7 `LDA $1F / BEQ ->A211`, A1FD `LDA $58` 16-bit) rolls at 2FF and -- once A22A's `TSB #8000` set bit 15 --
at every 128th frame: `INC $F8 / LDA $FE00,X`, < 8 the blink (anim 21), else idle.

The port never counted $0158, and its free-roam lead ran C0/9E84 on the collision page without $0158 / $01F8 / $011F
(field.c, CtPlayerAnimSelect after C0/881E): no roll, no blink, the $FE00 cursor two draws short per 256 idle frames. Now
field.c counts it on every field frame that reaches the wait (not in a fight, the world or a menu, not on a frame parked
in a sound upload, not in the load's black) and hands the lead's 9E84 the scene's $0158 / $01F8 / $011F / $0162.
MEASURED (scene468_stairs_to_120, no input, both sides): rolls at f532 / f660 (the blink, $1682 = 21) / f788 / ... f1300,
$01F8 11..17 -- check field-idle-counter-and-blink-roll-vs-rule-cartridge. (The zero at the load lands a frame late in the
port -- f26 for the cartridge's f25 -- under the load's black.)

### Loc 434's decision box (ROADMAP F15): the capture's phase

Under the rule with `ct.exe` (not CT_FIELDLOCK's real-time oracle) and the port's keys a frame after, the s434 capture is
the cartridge's frame for frame through f579 ($0129, $0162, $0100). After the decision obj10's idle loop (cadence 4) posts
its `BB` at f582 in the port, f583 on the cartridge: every cadence counter of the port runs a frame ahead from f0, because
on THIS capture the cartridge's first frame after the state runs no main-loop iteration (`--monitor 7E1000` f0 = the saved
bytes, `$0153` unflipped), where the scene468 capture's does (f0 already decremented, as the port). Input-driven events
match, cadence-driven ones are a frame early -- the capture's phase, not the engine: a port start that skips its first
iteration (as the fixtures' `pad` does for the recipes) would grade it.
