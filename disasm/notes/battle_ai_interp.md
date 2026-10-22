# The AI interpreter's draw micro-law — the eval/dispatch split + the resolver family

Session 10 (2026-07-23). Follow-up to battle_facing_engine.md SESSION-9 (the $26 draw ledger).
Every claim below carries a ROM address or an oracle ledger point; the HOLD ledger =
`ct_probe --monitor "26:1" --from gato_press_action.state --input "100:8,0:1900" --diff`
(f376 +1, f476 +2, f637 +0, f893 +1, f1149 +1, f1462 +1, f1739 +2) and the forest ledger =
same on forest_holdleft_pre (--input "40:1400": boot 32, evals +6, f449 +2, span-ends +1 each).

## 1. The action-op HANDLER TABLES (two of them)

- **C1/B85F** = the per-op MOVEMENT/ARM handlers (16 entries, op*2):
  op00→9810, op01/02→983A (shared), op03→98C4(RTS), op04→98C5, op05-0F→996x/997x stubs.
- **C1/B88D** = the per-op TURN-EXECUTE handlers (dispatched at C1/8D88 inside the turn
  processor 8CF9+). The two-table split IS the eval/dispatch split below.
- ⚠ SESSION-9's "25% per-entry roll inside the walker" was a MISREAD: C1/98C5 (the
  `rand%100 vs #$19` at 98D3) is just **op 0x04's handler** — none of our four decoded
  scripts use op 04, so that roll never fires for them. (The op-04 semantics: 25% →
  execute-something / 75% → skip via the FD/BA4A length step at 98E0 — not derived
  further, no user of it in scope.) **SUPERSEDED 2026-09-18, §18: derived and ported --
  one roll picks the 1st/2nd/3rd/4th entry after the 04 (quarters, cuts 19/32/4B), and
  the handler re-enters the commit on that entry. The attract's 07D uses it (id 51).**

## 2. The TARGET RESOLVER family (C1/AC14 → the B8BB table)

C1/AC14 (called by BOTH the op-00 handler and the attack/tech operand processor 983A):
clears the candidate list $AECC[0..10]+$AD8E[0..10] to FF (AC46), reads the target byte
at the AI IP ($B1D2):
- byte ≥ 0x80 → `$AECC = $AED8` (the REMEMBERED target — no draw). Not used by our
  four scripts (05/06 only) — documented boundary.
- else → `JSR ($B8BB,X)` with X=value*2 — the targeting jump table C1/B8BB:
  tgt00→A3F6 01→A3F7 02→A411 03→A42E 04→A43D **05→A452 06→A4AF** 07→A4E0 08→A508 09→A541.
- The result is copied $AECC[0..10] → $AD8E[0..10] — **the SNAPSHOT the later dispatch
  reuses** (see §4).

**tgt 05 (C1/A452, random living PC):** marks per-seat eligibility (3× JSR C1/B279, no
draws), all-dead → out with $AECB=0; else roll **`LDA #63 / JSR AF22` = rand % 0x63 =
%99 (0..98)** — ⚠ NOT %100: the divisor is the caller-loaded A (the boot-shuffle law
again), and 0..98 partitions into EXACT thirds at CMP #21/#42 (33/33/33). Dead pick →
loop back to A45A (re-mark + redraw). Success: seat → $AECC, $AECB=1.

**tgt 06 (C1/A4AF, nearest living PC):** $986F = attacker cmb ($B252+3), `LDA #05 /
JSR C1/0003` = the mode-5 DISTANCE engine (battle_counter_gating's C1/2986 family) →
$9873 = the nearest seat → $AECC, $AECB=1, also $AD8D. **ZERO RNG draws** — ledger-
confirmed (GATO f637 move-only-06 arm: +0).

**Multi-candidate pick (C1/AB20):** when a resolver returns $AECB ≥ 2 candidates,
`LDA $AECB / JSR AF22` = rand % count picks one (AB30-AB37). +1 draw for list-returning
target values (00/01-style "any/all" resolvers); our scripts' 05/06 always return
count ≤ 1 → never fires for them.

**Dead-target re-pick at dispatch (C1/8DE4):** if the SNAPSHOT target died since the
eval ($AEFF[X]==FF at execute time): `LDA #30 / JSR AF22 / LSR×4` = **rand % 0x30 >> 4
= a seat 0..2**, retried until alive → $AD8E/$AECC. A third, distinct random-seat
formula, drawn only on a stale-dead target.

## 3. The op-00 and attack/tech ARM handlers

**op-00 (C1/9810):** skip op byte → JSR AC14 (resolve the target byte → $AECC) → read
the mode byte → `$5E0D,X = mode / $5E15,X = $AECC` (X=$B252 slot). = the move-only arm.

**attack/tech operand processor (C1/983A):** skip p1 → JSR AC14 (resolve tgt) →
$AECB==0 → abort path (98B4: $B242[slot]=FF + $AF24=2 — "no eligible target");
$AECB≥2 → pick the first candidate IN THE $B23A ORDER (984D-9869 scans the boot
shuffle-2 list against the candidates) — **that is what boot shuffle-2's $B23A is
FOR: the AI's multi-candidate tie-break order**; then:
- TECH ($AEE3==02, 9899): `$AECC → $B16E,X` (the per-slot TECH-target stash) + skip
  the tech's 2 extra bytes. NO $5E0D write — a tech entry does not touch the movement
  mode (ledger: GATO's f893 aborted song eval left the mode-6 dance running).
- ATTACK: read the mode byte → the SAME tail as op-00 (C1/988E): $5E0D=mode,
  $5E15=$AECC. **The attack entry's pre-move re-arms at every eval of it.**

## 4. THE EVAL/DISPATCH SPLIT — the whole ledger reduces to one law

> ⚠ **LAW B BELOW IS SUPERSEDED (2026-09-18, §17).** Its ledger fits, but its reading does not: the "+1 at GATO
> f893 aborted" is the EVAL that follows GATO's op 00 TURN (C1/8CF9 ran the move with no action, C1:8E75 stepped
> the pointer, C1:8CA5 committed the song, C1:98A4 stored its target in $B171), and "f1149 = resolve 1" is the
> song's own C1/E4AE draw -- the turn never re-resolves a target. There is no mover abort: C1/8461-84D6 never
> reads $98A7. See §17 for the mechanism and the measurements.

The turn processor (C1/8CF9+, the B88D dispatch at 8D88) does NOT re-resolve targets:
at 8DA4-8DFB it loads **$AD8E (the snapshot the LAST EVAL wrote)** into $AECC, checks
liveness (dead → the %48>>4 re-pick), builds the bitmask (AD09). The per-slot saved
IP ($B1D4,Y / $B273 / $B26B) replays the ENTRY THE EVAL CHOSE.

**The unified law (fits every HOLD + forest ledger point):**
- **A) FULL EVAL** — at combat start AND at every own-action span end: walk the
  block/section CONDITIONS with real draws (cond-17 rolls here — the forest boot's
  3×2 and the Hetake span-ends' +1), take the entry at the persistent cursor, resolve
  its target (05 = %99 + re-roll, 06 = 0), snapshot $AD8E, arm the movement
  ($5E0D/$5E15; a move-only is CONSUMED; a tech stores to $B16E and leaves the mode).
- **B) TURN ATTEMPT** — at every ATB-0 (aborted or not): RE-RESOLVE the saved entry's
  target (the +1 at GATO f893 aborted / f1149 song) — no cond re-walk — and re-arm;
  mover mid-segment → discard + ATB reload (the retry law); else think → **DISPATCH
  the saved entry**: only the damage-pipeline draws (physical = hit roll + bonus
  when A36/3>0; tech = none) on the resolved target (dead → re-pick).

Ledger checks: f376 = A(cond0 + 05resolve) = 1 ✓; f476 = B(resolve 1 + hit 1) = 2 ✓;
f637 = A(cond0 + 06resolve) = 0 ✓; f893 = B-aborted(tech resolve) = 1 ✓; f1149 =
B(resolve 1 + tech pipeline 0) = 1 ✓; f1462 = A(cond0 + 05resolve) = 1 ✓; f1739 = 2 ✓;
forest boot evals 3×(cond17 + 05resolve) = 6 ✓; forest f449 = B(05resolve + hit) = 2 ✓;
forest span-ends = A(cond17 + 06/05resolve) = +1 each ✓.

## 5. Engine deltas (implemented this session)

1. battleAiTarget value-05: **%99** (battleRng maxv 98), exact thirds — was %100.
2. battleSpanEndArm → a FULL EVAL: condition walk WITH draws (shared with the
   combat-start eval; the Hetake span-end cond-17 draw restored).
3. The aborted-attempt dry run → RESOLVE-ONLY (no cond walk — was drawing extra
   cond-17s on Hetake aborts).
4. (Interim retained, documented): our dispatch re-resolves the target at the attempt
   (order-equivalent to B's re-resolve ✓ (same stream position), and our engine has no
   $AD8E snapshot — the attempt resolution IS the action's target; the dead-repick
   %48>>4 is unimplemented (our victim-died path just skips) — boundary.)

## Open questions

- ~~The think latency variance~~ RESOLVED session-11 (§7): the READY SCANNER's phase.
- ~~The remembered-target $AED8~~ DERIVED session-12 (§8): the last resolved target's seat (C1/AF13).
- op-04's 25% skip/execute semantics (no user in the four scripts).
- The B88D execute-handler table's non-01/02 SEMANTICS (addresses dumped in §8).
- ~~The cond-17 divisor~~ VERIFIED %100 this session (C1/95DA: `LDA #64 / JSR AF22 / CMP prob / BCS
  fail` — roll < prob passes; our model was already correct).

## 6. Validation + the two-readings note (post-implementation)

Implemented set: the %99 05-resolver; battleAiEval (the unified full eval at combat start + span ends,
conds WITH draws, cursor-aware, op-00 consume, tech no-mode-touch); the no-cond dispatch replay
(battleAiExecSection from the saved aiArm state, non-hit turns); battleAiAttemptResolve (aborted
attempts resolve-only); the ALWAYS-DRAW bonus/variance (battleRng maxv==0 draws %1 — E15E/E240/C063
`ADC #01`); the unconditional PC hit roll (DC94 draws even on the DC6A clamp); the enemy physical
SKIPS the hit roll (type-4 E276 path = formula + bonus only).

Our HOLD ledger is now count-exact per event: boot 27 → arm idx1B (%99=18→Crono, the oracle's own
creep target) → attack turns = resolve+bonus (+2 ✓ f476/f1739) → song turns +1 ✓ → attack-span-end
+0 ✓ → and the f893/f1149/f1462 window: our attribution (attempt-resolve +1 / dispatch +0-extra /
span-end-eval +N-rerolls) vs the alternative (attempt no-resolve / dispatch tech-variance +1 /
span-end +1) consume THE SAME total draws and converge on the same $26 index (0x21 post-f1462-analog)
— the available frame-granular ledger cannot distinguish them (both fit every observed point). Ours
is implemented; a future divergent-victim probe (a case where the re-roll changes the seat between
the two readings) would discriminate. Suite: quick 104/0/1, FULL 126/0/3 (re-pin wave 3: roll-value
deltas only; victims now spread per the canonical rolls — counters hit Marle when the rolls/nearest
say so).

## 7. SESSION-11 — the "think variance" IS the READY SCANNER (C1/847E) + the mode-1 ready-hold

The open "think latency 24/8/~0" reduced to a fully mechanical machine:

1. **The ATB decrement is round-robin** (C1/8240-828B: $B3C0 cycles 0..10) and reaching zero only sets
   the READY FLAG ($AFB6,X bit7) — no turn starts at the decrement.
2. **The READY SCANNER** (C1/847E): `LDX #$0C / LDA $B179,X` = the cursor at $B185, walking the
   boot-shuffled **$B163 turn order DOWNWARD, ONE entry per battle tick**, reloading to $AEC7 —
   **that is boot shuffle-1's PURPOSE** (like $B23A's tie-break role from §3). An entry < 3 branches
   to the PC path (C1/8511 — the PC enqueue is scan-timed too; our engine keeps the instant PC
   enqueue, documented boundary). The scan-phase distance IS the old "variance".
3. **The mode gate** (C1/84A3-84BB): a mode-1 (approach) enemy is gated on **$9826,X — the
   ARRIVAL/HOLD LATCH** — modes 0/4/6/0A+ pass freely. Turn start does `STZ $9826,X` (184BD) →
   `JSR C1/8CF9` (the processor; the in-turn abort lives inside) → **unconditional ATB reload**
   ($AFAB,X = $B158,X at 184DB — every turn attempt reloads, the f887 "retry" included).
4. **The READY-HOLD law** (oracle f461 + our scan-trace regression): the mode-1 handler pass on an
   ATB-READY walker sets the latch and arms NO new segment — the walk HOLDS for the turn (GATO ready
   f454 → the f461 pass latches at (136,118), 26px REMOTE, position constant = the E.3 remote attack).
   A NOT-ready walker keeps walking (the §A.6 continuous walk-in; the Imps' wander). True box-overlap
   arrival latches too.
5. **The turn-processing constant** = +3 frames (scanner hit → $9877 window): oracle f475→478 and
   forest f448→451, both EXACT. CT_BATTLE_ENEMY_THINK 24 → 3.

Latency decomposition of every datapoint: GATO t1 +24 = latch-wait (next cadence pass f461) + scan
walk (f475) + 3; t2 +8 = scan 2 ticks + 3 (mode-6, no latch); t3 ~0 = pass/scan coincided; forest H1
+3 = scan 0 + 3 (mode 0). Engine: scanCur/scanTotal + enArrive[8] in the host; the scanner replaces
the per-frame zero-scan at the dispatch site; the ready-hold in battleMoveApproach. Re-pin wave 4
(dispatch frames ~-8; the revive script's target-select Right needed +4f of tick-phase). quick
104/0/1, FULL 126/0/3.

Remaining open: the PC-side scan timing (enqueue latency — ours instant); the exact skip-vs-consume
semantics of ineligible scan entries (modeled 1 entry/tick flat); $AED8; op-04; B88D non-01/02.

## 8. SESSION-12 — the PC scan path + the last resolver boundaries

1. **The PC enqueue is scan-timed** (C1/8511, the scanner's cmb<3 branch): the cursor landing on a
   ready PC's $B163 entry runs the can't-act gate (the $AF23 status-skip family → reload, no window)
   then sets the **$93EE turn latch** (the literal pcInTurn port) + the C1/0003 mode-1 enqueue.
   IMPLEMENTED: the engine's step-E instant enqueue moved into the scanner — PC menu-open now carries
   the same scan-phase latency as enemy dispatches (~+15f typical). Re-pin wave 5 (MASHA cadences
   shift; miss/charm scenarios needed DEBUGEND 320→420; pc-approach-late shot +15, visually verified).
2. **$AED8 (the remembered target) derived**: writer C1/AF13 = `LDA $AECC / AND #0F / STA $AED8` under
   a $B1CF-gated condition — the LAST RESOLVED TARGET's seat latched after an action; target bytes
   ≥ 0x80 in scripts mean "the same target again". No user in our four scripts (documented boundary,
   unimplemented).
3. **The B88D execute-handler table dumped** (op00→99B8, 01→99BE attack-exec, 02→9A39 tech-exec,
   03-05→9B46-48, 06/07→9B8C/8D, 08/09→9C6E/6F, 0A→9CB3, 0B→9D1B, 0C→9D72, 0D→9DCE, 0E/0F→9E62/63).
   Non-01/02 semantics remain open (no user in scope).

Remaining open after session 12: the scan's exact skip-vs-consume semantics for ineligible entries
(modeled 1 entry/tick flat — every observed latency fits); op-04's 25% branch; B88D non-01/02; the
octant return-dance path (rest-equivalent only); PC bob 7/13; GATO 120f idle-swap; cursor phase-reset
on window re-copy; dissolve red-target variance. quick 104/0/1, FULL 126/0/3.

## 9. SESSION-13 — the octant return-dance FULLY IDENTIFIED (implementation spec; not yet ported)

The mode-6 return handler (C1/3A3D) decoded end-to-end; every piece of the old "octant-branch
reverse-oscillation clamped by $9791/$9872 + a secondary box test" boundary is now named:

1. **The stored-bearing latch**: `$9791,X` set -> reuse `$9786,X`, skip the recompute (C1/3A42).
2. **The ref**: fresh pass computes away = atan2(victim->self) (C1/0222), ref = victim.live +
   trig(away) x $AE where **$AE = 0x40, or 0x80 when $A2B5 == 0x10** (a double-distance arena
   variant, C1/3A71). Ref stored $A5E4/$A5EC,X.
3. **THE REVERSE-OSCILLATION** (C1/3AB5-3AF5): $84 = quadrant($DB) (C1/0118); per-quadrant sign/zero
   tests on (self - ref) decide "crossed": crossed -> **$DB += 0x80 (reverse 180)** and fall into the
   segment arm -- the machine oscillates around the ref (the oracle's first DOWN segment f668 = the
   first reversal: adjacent GATO is already past the ref on the away axis).
4. **The segment arm** (C1/3AF7): step scale $AE = 8; $DB -> $9786; trig components -> dest =
   live + (trig x 8) staged at $A03C/$A053; **facing = C0F700[$DB] -> $96DE** (the facing stamp site).
5. **The settle test** (C1/3B38 -> 2B13 family): threshold pairs $9893/$9894 per entry -- the return
   uses **0x0010 = dist^2(self, ref) <= 16 (4px)**; the same engine's other entries: 0x100, 0x400,
   and 0x900 = 2304 (the cond-1F 48px -- confirming the family). Verdict in $9872 (BMI = keep
   walking). SETTLE (C1/3B40): **INC $9829,X (the ARRIVAL LATCH -- the same byte the ready scanner
   consumes!)**, $5E0D -> $5E1D (the settled-mode shadow), victim live pos remembered at
   $9831/$9839,X, STZ $98A7 (mover off).
6. **THE BATTLE-ARENA TILE GRID** (C1/2926, called via 285A from the walk path): the step's
   destination box (positions >>4 -> tile coords) scans a **16-wide tile map at $7E7BFD** --
   `AND #40` = SOLID bit (+ a bit7 class gated on $9875) -> blocked. **This is the "tile grid not
   modelled" input: the AXIS-PURE dance segments are the grid DEFLECTING the diagonal steps.**
   To port: find the $7BFD stager (boot writes the arena's collision map -- likely from the same
   E4/CC formation data) + the blocked-deflect semantics at C1/2983+.

Port plan (next session): stage $7BFD at boot (derive the writer), port the per-pass algorithm
(latch/ref/reverse/settle-4px/facing) replacing battleMoveReturn's dot-test, wire the grid test into
the segment step (deflect), and re-pin wave 6 (every return path's pixels change to the canonical
dance; rests move <= 4px).

## 9b. SESSION-13 (cont.) — the dance PORTED + three more pieces (FULL 126/0/3)

Ported battleMoveReturn to the full derived machine and validated the trajectory against the oracle
HOLD dance leg-for-leg (down 7 -> LEFT 8 -> long UP -> rest ~8px of the oracle's). Three pieces found
during the port:

1. **The grid stager derived + ported**: CC/E45E-E4B8 = border template (rows 0-1/13-15, cols 0-1/
   14-15 = 0xC0) OR bit7($2B23,X)>>1 OR bit7($2A23,X) — **verified 256/256 vs the oracle on BOTH
   arenas**. And $2A23/$2B23 themselves = the FIELD transfer's 16x16 camera-window projections of the
   field collision planes **$7E7000 / $7E7040 (the known mover-solid plane!)** (C0/03A4-0407 MVN
   loops, writer C0:03F6) — the battle arena's collision IS the field's, projected. Engine: the
   projection staged in fieldBattleConsumer from the scene's Coll (g_fc now set at fieldLoad); the
   grid built in battleMoveInit ($2A23-plane window measured zero in both arenas -> staged 0,
   boundary).
2. **The SECOND deflector**: the step quantizes not only on the grid but on the **VICTIM-BOX overlap
   at the destination** (C1/3B8B JSR 28B0) — the oracle's down->LEFT turn at y=125 is Crono's box.
   And the quantize `(b+0x40)>>6<<6` ROTATES a blocked cardinal into the NEXT one (down->left) — the
   dance's turns.
3. **The LIVE-KNOCK discovery (open)**: the ROM's GATO attack-A strike PERMANENTLY displaces the
   victim's live position ($1D23 144->147, never restored) while the Hetake strike displaces nothing
   — a per-attack ANIM-SCRIPT op (underived; candidates in the CD/7A42 op set 69/6A/72/76/78). Also:
   the PC attack approach mutates $1D0C/$1D23 in the ROM (MASH probe: Crono walks -2px/f live) — ours
   is render-only. **The "live-position single-source" family = the next milestone**: port the PC
   attack walk + the strike knock as live mutations, then tighten the settle threshold to the ROM's
   0x10 (currently PROVISIONAL 0x40/8px because our ref sits 2-4px off without the knock — every rest
   stays within 8px of the ROM's; the right-correction leg reappears when exact).

Re-pin wave 6: the dance lengthens the return (song window f685->f960; counters/noops later; facing
legs re-timed f540 left / f2005 right, verified; songs/ice shots re-timed). quick 104/0/1, FULL 126/0/3.

## 10. SESSION-14 — the live-position SINGLE-SOURCE architecture located (derivation only)

Writer-traced the two live mutations the dance port exposed:

- The GATO-strike knock write on Crono ($1D23 144->147 at f537) writes from **CF/F1E4/F1ED** =
  `LDA $A370,X + trig -> STA $1D23,X / LDA $A35D,X + trig -> STA $1D0C,X` with `JSR CF/F9FB` —
  **THE SAME 8-phase trig mover the AI walks use, in a SECOND INSTANCE**: angle $A34A,X, base
  $A35D/$A370,X, indexed by COMBATANT. The ANIM ENGINE (bank CF, the CD-script interpreter) arms
  these movers per combatant — the strike knock IS a small mover segment on the victim (+3 = a
  short phase walk), and the PC attack approach/return (the probed -2px/f $1D0C walk) is the same
  machine driven by the attack's CD script.
- **The architecture law**: ALL battle live movement — AI walks (base $98AF/$98B7), anim-driven
  routes and knocks (base $A35D/$A370) — flows through the ONE mover (CF/F9FB) mutating the ONE
  live position ($1D0C/$1D23). Our engine's render-only pcAtkMv/fxKnock offsets are the remaining
  divergence from that law.
- Port plan (the milestone's implementation half, next session): derive the CD anim-script mover
  ops' operands (the 69/6A/72/76/78 family in CD/7A42's op set) enough to arm live mover segments
  for (a) the PC attack route and (b) the strike knock; then mvLive becomes the single source, the
  render offsets collapse into it, and the return settle tightens 0x40 -> the ROM's 0x10 (the
  right-correction leg + the exact (135,85) rest reappear).

quick 104/0/1, FULL 126/0/3 (no engine change this lap — derivation + dossier).

## 10b. SESSION-15 — the knock's full mechanism measured (op mapping = the one missing piece)

Oracle probe of the anim-mover instance through the GATO attack-A window
(--monitor "A34A:1,A35D:1,A370:1,1D23:1"):

- **f536 (rel 58): the victim's instance ARMS** — angle $A34A[0] = **0x40 (the attack direction:
  GATO above -> knock DOWN)**, base $A35D/$A370[0] = the victim's current live (0x86,0x90).
- **f537: $1D23 0x90 -> 0x93 (+3) in ONE frame** — the mover applies phase 3 immediately
  (trig(0x40)*3 = +3 Y, +0 X). The LIVE POSITION keeps the displacement.
- **f632 (span end): the instance clears** (angle/base zeroed) — the mover is released, the
  position mutation persists. The Hetake attack arms nothing (probed: no $1D0C/$1D23 change).

The attack-A script (CD/7A42 dump): `... 20 14 78 46 72 0A 06 09 20 02 69 00 20 02 6A 76 00 03 0C
A9 04 20 0A 36 72 0A 06 03 00 ...` — the arm op is one of 69/6A/76/78 (72=known, 06/03=play,
20=pause, 24=wait, 36=sync). The CF anim-op DISPATCH TABLE was not located this lap (the CF/2CBF
"F1C0 ref" is a data coincidence; CF/51B1 unchecked). ⚠ Implementing the knock without the op
mapping would need an attack-identity gate = the forbidden pattern — so it stays UNPORTED until
the dispatcher lands. Also noted: our span table maps no CD anim for the Hetake attack ($9877=0x14
-> our selector gives 0 / ptr CD/0000) — a separate small gap.

Next session's exact steps: find the CF anim-op dispatcher (candidates: follow CF/51B1's context;
or writer-trace $A34A with --writer to get the ARM instruction's PC directly and walk back to the
op fetch), map ops 69/6A/76/78, then port (a) the strike-knock live mutation and (b) the PC attack
route as anim-mover segments on mvLive; collapse pcAtkMv/fxKnock into it; settle 0x40 -> 0x10.

## 10c. SESSION-15 (cont.) — the knock OP decoded: 0x12 = MOVE(dist), victim thread

The $A34A --writer probe landed the arm instruction: **C1:6B1D** — the handler at C1/6AF0:
`INY / LDA [$E6],Y (the OPERAND from the CD script) -> $A5BD` -> role->combatant via $A010/$A014 ->
guard $A337,Y -> **operand -> $A639,Y (the DISTANCE/phase count), $A5AA,Y (the PRECOMPUTED bearing)
-> $A34A,Y (the mover angle), faceTab[angle] -> $96DB,Y (the facing stamp!), live -> base
$A35D/$A370,Y, state armed ($A337=1)**. Probes: f535 $A5AA=0x40 (the bearing computed one frame
earlier by a prelude op), f536 $A639=3 (the operand = the +3 knock!), f537 count 3->2 with the live
already +3. **The op byte = 0x12 (operand = distance)** — the `12 03` in CD/7A42's victim-side
sub-script; the attacker thread's `1E 44` = the strike/sync marker. The CD script is THREADED per
role: header `90 00 90 00 [95D3][7A4E][95D3][963E]` = 4 role-thread pointers (7A4E = the attacker
ops; 95D3 = a shared default), plus a sub-script pointer list (7A87..7B25) — the victim's hurt
sub-script carries its own 12-op. A Hetake attack simply has no victim-thread MOVE — the knock is
mechanism-gated by the script content, NOT by attack identity.

Port (next session): a minimal CD thread/sub-script parser (enough to find the victim thread's
op-0x12 + operand per attack) -> arm the live knock (operand px along the attacker->victim bearing,
facing stamped) at the strike; same machinery for the PC attack route (the attacker thread's move
ops); collapse pcAtkMv/fxKnock; settle -> 0x10.

## 10d. SESSION-16 — the anim-mover op family closed (the shared arm C1/6B11)

Correction + completion of §10c with our own parser's op table (battleAnimOpLen already knew these):
- **C1/6AF0 = op 0xA9's handler ("move-forward dd")** — the routine I disassembled; its body
  C1/6B11-6B46 is **the SHARED ARM TAIL**: operand -> $A639 (distance/phases), $A5AA (precomputed
  bearing) -> $A34A + faceTab -> $96DB facing, live -> base, $A337 armed. Op 0x12
  ("slide-to-target", handler C1/4FE3) and the A8/1B family DELEGATE into the same tail — the
  f536 arm writer C1:6B1D belongs to whichever op ran.
- **The knock instance = `12 03`** — the operand ($A639=3 probed) matches op 0x12's operand
  EXACTLY; `A9 04` (the attacker thread's lunge) has operand 4, not 3. The `12 03` lives in the
  attack's sub-script region (CD/7AA0+); the VICTIM'S SHARED TRACK = **CD/95D3** (the hurt
  library: 72 facing / 36 sync / play / `1E 49` supercommands / 50 showdmg ...), selected by the
  header's track mask (m1=0x9000, 2 tracks: 95D3 + 7A4E).
- Remaining for the port: the ROLE->COMBATANT assignment per track/sub-script ($A010 role id ->
  $A014[role] combatant — which track index maps to the victim) and the sub-script invocation
  (who runs CD/7AA0's `12 03` — a 1E-supercommand side effect or a track). With those two, the
  live-knock port is purely mechanical (arm the victim's live mover: operand px along $A5AA).

Engine unchanged this lap (green FULL 126/0/3 stands). The port = the next session's opening move.

## 10e. SESSION-17 — THE LIVE KNOCK PORTED; the dance rest is PIXEL-EXACT (FULL 126/0/3)

The last gap closed by one probe: **$A014 = [attacker cmb, FF, FF, victim cmb, ...] with $A010 = 3
(the victim role) spanning f502-553** — covering the f536 arm. The knock idiom in the dispatched
track: `76 00 03` (aim -> $A5AA = the attack direction, cardinal) + `0C` (speed) + **`A9 04`
(move-forward, operand 4)** — the shared-arm handler C1/6AF0 walks the VICTIM's live position by
**trig(bearing) * operand = +3 for operand 4** (the >>8 floor; matches the oracle's $1D23 +3
EXACTLY). op 0x78 = SFX (JSL C7). The Hetake attack's dispatched tracks carry no A9/A8 -> no knock
(mechanism gate ✓ oracle-confirmed).

PORTED: battleAnimParse detects a dispatched-track A9/A8 operand (animKnockDist); the strike
displaces the victim's mvLive by trigMul(cardinal-quantized attack bearing, operand), PERSISTENT —
and the return settle tightened to the ROM's dist² <= 0x10. Validation: the HOLD dance now ends with
the right-correction leg (128,85)->(135,85) and **RESTS AT (135,85) — THE ORACLE'S EXACT PIXEL** ✓✓
(our remaining 1px = Crono staged 145 vs oracle 144, the separately-noted PC-staging item). Re-pin
wave 7 (the knocked victim shifts every post-strike FNV by the canonical +3; song window unchanged).

Still open in this family: the PC attack route as a live mutation (the -2px/f $1D0C walk — our
pcAtkMv render offsets remain), the 1px PC staging, and the role-per-track assignment formula
(empirically: the non-ShowDamage track ran as the victim role here — the loader's bit->role law
underived; the A9/A8-content gate sidesteps it for the knock).

## 11. SESSION-18 — the PC ATTACK ROUTE ported LIVE (the bank-CE choreography + the grid pathfinder)

The §10e "PC attack route" open closed end-to-end. Oracle base: gato_press_action MASH
(`--input "100:8,0:8"×N`), monitor + writer probes, mid-run states via `--peek` + `PEEK_SAVE`.

### 11a. The oracle timeline (Crono melee, one attack)

- f435: dispatch loads role-0's track ptr = **CE/D4C4 — bank CE, not the CD/5FF0 script!** The whole
  PC-attack choreography = the CE COMMAND scripts: a base script (CE/0380 family: header m1=0x9080 →
  3 tracks CE/038E/039E/03A6) whose tracks run `1E ss` supers that dispatch per-phase SUB-BLOCKS
  (`1F` = return-to-base). The $9877-selected CD script never loads for a PC physical (it is the
  enemy-side/legacy table; our span pins are unaffected — the spans were oracle-measured).
- **The bit→role law (the §10e open, SOLVED)**: a script header's mask bit POSITION = the track's
  ROLE/slot: bit15→role 0, bit14→role 1, … (loader stride 4 at $5DBD+4·role, confirmed live: 0x9080
  → slots 0/3/8 = exactly the three loaded ptrs). Roles: 0 = attacker, 3 = victim (the $A014 map),
  8+ = the $A437 attachment/effect objects ($A240/$A250 positions; role ≥ 8 / == 4 bail out of the
  move ops' combatant path). So CD/6E0C's "mover track" (`1B 09 / 12 0C / 1B 0C`) is the WEAPON
  EFFECT's track (role 8), not Crono's — the old render-offset model was built on a mis-read.
- ENGAGE block (CE/D4C3, via the role-0 super): `07` + `02 06` (play run anim) + **`18 03` =
  WALK-TO-role-3** (f436 arm, f438-445 walk) + `72 0D` (face) + `1F`. Victim block (CE/D42A, f463+):
  `69 00` flash + hurt pose + `50` ShowDamage (PARKED ~59f = the popup float, f483→f542) + `1F`.
- STRIKE block (CE/D776, f486-516): `76 03 19` aim / `08` speed / **`A8 01`** (move-forward 1 → 0px
  net = the contact nudge; the f487 one-frame $A337 arm) / `26 0E 02` / `0C` / `D0` / **`98 02 19`**
  (f492-497) / `26 0E 01` / `20 03` / `D1` / **`1B 19`** / `06 0E` / `20 05` / `72 0D` / `06 03` / `1F`.

### 11b. The op-18 WALK machine (handlers C1/5409/541B/542D → C1/5437/5480/54C1, mover CF/EC78)

- op 0x16 walk-to-XY(x,y), 0x17 walk-to-stored, 0x18 walk-to-target(tt) — tt resolved by C1/762E.
  In progress the op returns 0 (parked); on arrival it returns $A4F0[role] (its own length).
- **The resolver C1/762E mode table (C1/79D3)**: modes 0-8 = ROLE tt's live pos ($A014→$1D0C/$1D23);
  9-0x11 = role (tt−9)'s pos + the per-cmb engage offsets $9708/$9713; 0x18 = fixed (0x80,0x80);
  **0x19-0x1B = PC seat (tt−0x19)'s staged HOME ($99AF/$99B2)**; 0x1C+ = attachment-object positions.
- ARM (first visit): `JSR C1/4A71` = **BFS FLOOD + PATH BACKTRACE** on the walk grid $7CFD:
  the grid = the ARENA GRID image ($7BFD: border 0xC0 + solid 0x40) + **0x40 stamped at every other
  combatant's cell** (f437 live grid: Marle's cell 0x6B ✓); self cell = 0x20 (cleared after), goal
  cell = 0xFF; waves 1..0x1E flood from self (frontier lists $7DFD/$7EFD); then the backtrace walks
  goal→self down the wave gradient ORing **0xE0 into each path cell** (probe order up/down/left/
  right, default right). Walker state: $A513/$A53D cell, $A517 track, $A548 no-backtrack, $A500=FF,
  $A15B=1.
- PER SEGMENT (op re-visit with $A500 < 0): reload 7; MVN the grid to the $8AFD scratch; probe the
  4 neighbors (skip $A548) for an 0xE0-class cell → dir (default right); **STOP if the chosen cell
  == 0xFF or a 2-ahead probe (cur + CC/F454/F455/F456[dir·8]) == 0xFF** (= rest ONE CLEAR CELL from
  the victim — the melee range); straight path → variant 0, else diag variants 1/3 (left, side cell
  CC/F475) / 2/4 (right, CC/F476) and the cell advances diagonally. track = dir·8+variant; $A548 =
  CC/F414[track]; **$96DB facing = CC/F434[track]**; $A53D = the chosen cell.
- PER FRAME (CF/EC78, before the controller): while $A500 ≥ 0: live($1D0C/$1D23) +=
  **CC/F494/F594[track·8 + (7−$A500)]** (2px/f straight tracks), $A500−−. Oracle: arm f436, track
  choice f437 ($A517=0 = up-straight), 8 steps f438-445 (144→128), segment-2 probe hits the FF
  2-ahead → stop at cell 0x88 = (134,128), one cell below GATO ✓.

### 11c. The return (op 0x98 → C1/6299) + the snap (op 0x1B → C1/561F)

- `98 mm tt` = trig-slide to resolver-mode-tt's point: the C1/5135-family arm (base=live, dist via
  C1/0222/65BA, step count $ABFE = dominant-dist / speed via C1/00D7, speed $A324 = 4 after the `0C`
  op) driving the CF/F113 16-bit ACCUMULATOR apply (live = base + acc-hi per frame). Cardinal speed-4
  → the byte-exact 4/4/4/4 sequence f494-497 (the accumulator carries the fraction; a rounding-half
  seed reproduces it exactly — our port seeds 0x80).
- `1B 19` = teleport-to-mode-0x19 = **SNAP EXACT to the staged home** — erases any residue AND any
  standing knock displacement (oracle: $1D23 ends 0x90 exactly; a knocked-then-attacking PC is
  restored by his own return, mode 0x19 reads the STAGED array).
- op 0x19/1A/1B = teleport-to-XY/stored/target (C1/5611/566C/567D → the C1/561F placer: live :=
  resolved point, instant). op 0x73 = attach-marker (C1/6059, $A3DF). op 0x70/0x71 = a hold/release
  flag pair (C1/5FC1/5FD3, $A1D8).

### 11d. The port (src/battle.c battlePcWalkArm/battlePcWalkTick + battlePcAtkArm/Tick, field.c)

mvLive[0] is now the SINGLE SOURCE for the whole route: the op-18 walker (BFS+backtrace+segment
machine reading the CC/F4xx tables from ROM), the crit leap (same trig sim, now writing live), the
op-98 accumulator return, the op-1B snap. field.c battleAttackRender reads the live foot directly —
the pcAtkMvOff/Goal/Step offset model is DELETED. The walker's facing stamps ride CC/F434; the
strike aim + the settle re-stamp keep their laws. Validated: our route trace (CT_BATTLE_ROUTETRACE)
reproduces the oracle shape byte-for-byte modulo the known +1px staged start: arm → track-0 choice →
8×(0,−2) → stop one cell short → 4×(0,+4) → snap home; a second attack with the victim adjacent
walks ZERO cells (the FF probe stops it immediately — the ROM's own already-adjacent idiom); the
goal tracks the LIVE victim (a wandered GATO is chased to his new cell — single-source ✓).

**The at-rest counter hold (battle_polish3 ITEM 4) is DELETED**: it was a facing cosmetic,
superseded by battleOpenAction's dispatch-time victim-aim stamp, contradicted the E.3
position-independent dispatch law, and dead-locked the reactive counter once the victim's live box
(now genuinely moving) deflected GATO's return dance into a non-converging oscillation — a geometry
the ROM's own 8px-quantized atan (C1/0254: index = (|dy|&F8)<<2 | |dx|>>3) cannot correct either;
the fight's flow, not a rest, bounds the dance. The old confirm-boundary "f966 counter-nofire" was
an artifact of that hold (the eval deferred until the dance wandered GATO out of range); the
re-pinned flow has the counter FIRE for real (~4f post-hit, attack-B, 13 dmg, in-range ✓ FIX C).
Re-pin wave: 11 checks, every shot verified visually (build/s18_*.png), battleloop victory lines
byte-identical where present; the swing shots now expose the known +1px staging delta.

Still open in this family: the 1px PC staging (145 vs oracle 144 — now RENDER-VISIBLE in swing
shots); op 0x98's arm-seed exact derivation (C1/00A7 tail — our 0x80 rounding-half reproduces the
oracle byte-exactly); the crit script CD/6E4A carries its own `98` op (parse safe-skips it — adding
len 3 to the op table would let it parse clean); the engage sub-block super table (`1E ss` → CE
address map); the CE base-script account of Marle's ranged flow.

## 12. SESSION-19 — the +1px PC STAGING closed (the sub-pixel floor in the lead sync)

The §11 "1px PC staging" open (staged Crono (134,145) vs the oracle capture's (134,144)) root-caused
and fixed — no staging/D9 mechanism was wrong; the error was a SUB-PIXEL FLOOR on our side:

- Oracle (gato_t0.state, monitor $1804/$1884 + $29B0 writer): the record lands at f76 (writer
  C0:045A = the C0/041A staging read of the maintained tile byte $1801,X + on-screen pixel $0A80,X).
  The lead (obj1) waits f0-41, then the D9 staging walk steps his WORLD Y word 0x0A4F -> +0x1F/frame
  ×6 -> 0x0B09 (164.9375 -> 176.5625): the walk stops mid-step where the tile-entry check catches it,
  and the sub-pixel fraction (0.5625) floors to screen pixel 144 -> fine 0. The record IS the pure
  arithmetic of (seeded sub-pixel start) + n×(trig step).
- Ours: CtSceneSeedFromWram already seeds fx/fy EXACT (word/16.0) — but two redundant re-seed loops
  (the --loadwram / --battletest paths) re-called CtSceneSetActorPos(wx/16, wy/16) with INTEGER
  division, and the per-frame lead<-player sync (field.c) passed (int)px/(int)py — flooring
  164.9375 -> 164.0. The floored start pushed the D9 crossing from 6 steps to 7 (landing 177.5625 ->
  fine 1 = the +1px). Marle's landing int survived the same floor by band luck (fine 15).
- FIX: the two redundant loops DELETED (the seed is authoritative); the per-frame sync uses the new
  CtSceneSetActorPosF (doubles — the ROM has ONE position storage, $1800/$1880 16-subpixel; nothing
  ever floors it). Validated: our D9 walk now reproduces the oracle's word series **byte-for-byte**
  (0A6E 0A8D 0AAC 0ACB 0AEA 0B09) and the staged seat-0 record is **BYTE-IDENTICAL to the oracle
  capture: `08 09 06 00`** (the gato-staging-handoff check now pins the oracle's own record).

Re-pin wave (21 checks): every Crono-bearing GATO-fight render canary shifts 1px toward the oracle
(lowhp/victory/flurry/crit-flash/facing/song/cyclone/ice/ambush/hud FNVs; gato-walk's tgt (134,145)->
(134,144) and the knocked return ref 148->147; confirm-boundary re-harvested same-structure).
Representative shots re-verified visually (build/s19_*.png). CT_BATTLE_STAGEDBG now also traces the
per-frame D9 walk ([d9] lines — the validation surface this derivation used).

## 13. SESSION-20 — the CE SUPER TABLE + the PC-attack dispatch chain (derivation)

- **`1E ss` (super) = a CALL into bank CE**: handler C1/56CC pushes the return ptr+bank onto the
  $A569/$A56B stack (depth $A00E/$A00F) and jumps $E6 := **CE/0000[ss*2]**, bank CE. **`1F` = the
  POP** (C1/56FB restores $E6/$E8). CD and CE scripts share the interpreter + op set; the whole
  PC-attack choreography is CE-side blocks called from a CE base script. Verified mappings:
  super 0x0A -> CE/D4C3 (the melee ENGAGE block: run-anim + `18 03` walk + `72 0D` face + `1F`),
  0x2A -> CE/D776 (the STRIKE/RETURN block: `76 03 19 / 08 / A8 01 / ... / 98 02 19 / ... / 1B 19`),
  0x00 -> CE/D428 (the victim hurt/ShowDamage block), 0x44/0x49 -> CE/DB10/DB6E (the enemy-side
  strike supers the CD attack scripts call — the GATO knock's dispatcher).
- **The PC-attack dispatch chain** (C1/4497+): command class $AE91 -> id = $9853[($AE91-3)*2]
  (the WEAPON id for the attack command) -> config record **CD/4926[id*6]** -> $9877 (anim
  selector) / $987A / $987B / $987E / $9881 (sfx/gfx ids resolved via D1/0060, CD/6A2C, CD/666E);
  $AE93 != 0 takes a variant path (144E0). The BASE script header is CE-side (three CE track-loader
  call sites: C1/4477 / C1/4662 / C1/4816 contexts — same mask-walk idiom as the CD loader with
  bank CE) — CE/0380 is the melee base observed live (roles 0/3/8; role-0: `1E 0A` engage ->
  sync -> `1E 2A` strike). Ranged/tech/item use other bases (pointer-table derivation OPEN — the
  engine's per-$9877 span pins + the ported walker/strike/return laws stand in for the block
  choreography; the supers table makes any future block-driven port mechanical).

## 13b. SESSION-20 (cont.) — bat3/forest route validation + the op-len completion

- **The PC attack route validated on the FOREST arena** (forest_holdleft_pre.state = the vmdiff pair
  of our forest_pre battletest; same staging, Crono cell 0x4A): the oracle's two MASH attacks walk
  track 0x10 (LEFT, 2 segments, X 0xA0->0x80, stop 128) and track 0x08 (DOWN, 1 segment, Y
  0x47->0x57) with the +4px/f op-98 returns — and our pcWalk reproduces BOTH series step-for-step
  (trk 16 / trk 8, same counters, same landings). The BFS pathfinder is oracle-exact on a second
  arena (multi-enemy, multi-segment).
- **The crit choreography = CD-side op-98**: CD/6E4A's attacker track decodes fully with this
  session's op lens: `72 0D | 02 00 | 0C | D9 30 | D0 | 98 00 03` (the LEAP = move-to-VICTIM,
  mode 0) `| 36 | 03 07 | 20 0F | 02 00 | 98 00 19 | 1B 19` (return + snap-home) `| D1 | DA xx |
  24 02 | 72 0D | 06 03 | 50 | 2E 01 | 00` — the crit's ShowDamage is IN the attacker track (the
  1E 44 super = CE/DB10 = just the victim flash block, no 50). Oracle crit run (bat3crit MASH):
  leap -4px/f X 0xA0->0x84, popup $A3D1 pulses f139 (58f), return +4px/f — matches.
- **Parser op-len completion** (battleAnimOpLen, all read from the handlers' own operand fetches):
  16=3, 17=1, 18=2, 19=3, 98=3, 26=3 (from the CE/D776 live pointer walk), D1=1 (STZ $A5D8 —
  D0's pair), DA=2. Effect: the crit script parses CLEAN (parsed=1 strike=1 showdmg=1) — the
  strike + popup now come from the script's own 1E + 50 instead of the parse-fail fallback; the
  fight is byte-identical (crit-x2 victory f1582 / HPs / damage stream unchanged — only the trace
  line's parse diagnostics flipped, re-pinned). Marle's 0x0F crit-ranged script still stops at
  op 0x99 (handler C1/672E underived — the remaining parse boundary).

## 14. SESSION-21 — op-99 closes the last in-scope parse boundary + the PC anim-tick machine

- **op 0x99 = move-to-STORED-point** (C1/672E: goal = $5D9E/$5D9F, no operand READ but the ROM's own
  $8E=2 advance skips one byte -> len 2; the body = JMP C1/62AA, the op-98 family). With it Marle's
  0x0F crit-ranged script CD/7164 decodes COMPLETELY: attacker `72 0D | 02 01 | 0B | 12 13 | ... |
  99 00 | 1A | 36 | 1B 03 | 02 08 | 24 02 | 98 00 19 | 1B 19 | DA | 72 0D | 06 03 | D1 | 20 0F |
  50 | 2E | 01(END)`; victim `27 | 24 01 | 20 0A | 78 45 x3 (the crossbow triple sfx) | 36 | 28 |
  06 03 | 00(RET)`. Both tracks end on legitimate terminators — NO in-scope script stops on an
  unknown op any more. quick stayed green (no check pins 0x0F parse diagnostics).
- **The PC idle-bob machine (partial — the 7/13 divider's frame source still open)**: the per-seat
  anim tick = C1/2D9F (seat loop; $9922/23/24 = per-seat priority flags choosing the ORDER, loop
  runs regardless) -> C1/2E00 per seat: gate $96F5,X; hold mode $9912==1 re-queues via INC $9922,X;
  ELSE `DEC $9689,X` (the DWELL counter) -> on zero: advance the E4 ptrB row (cursor $968C,X,
  4-row wrap), pose -> $967B -> publish C1/1C55 (`CMP $9907,X / BNE STA` — equal-skip), reload
  $9689 = ptrB byte / 5, MIN 1 (C1/2ECB div + INC). The enemy twin = C1/3400 ($9694 dwell /
  $969C cursor / $97D9 pose, cmb = seat+3). Caller = the battle frame body C1/106E (sequences
  007E/CD0009 -> $993B -> 2D9F -> 3714 -> 283D -> 007E/CD0009 -> the $993A logic-tick block ->
  3714 -> 283D); a second gate-only entry C1/10E3 exists; the 106E-vs-10E3 per-frame dispatch —
  the source of the observed 6.5f/unit lattice (dwell 1 -> 6/7f alternation, dwell 2 -> 13f;
  bat3 monitor $9907/$968C) — is NOT located (no JSR/JSL/table refs; an indirect dispatch).
  The render keeps the pinned 7/13 fixture cadence (byte-correct vs the same oracle observation).

## 15. SESSION-22 — the battle Tech/Item SUBMENU PANELS derived + rendered (user report closed)

User report (translated from Portuguese): "in battle, the Tech and Item selector menu is not being shown" — the submenu STATE
MACHINES were byte-derived ($95DB=1/2, cursors, MP-grey, staging) but the HUD render had no panel.

Derivation (the oracle strip = ONE 32x32 tilemap at VRAM word 0x7800/byte 0xF000 rendered twice —
BG1 4bpp chrome + BG3 2bpp text, text cells = tile 0x100 + CT text code; per _gen_battlehud.py):
- The GATO states are EARLY-GAME (no techs, empty inventory) -> the panels open EMPTY. To capture
  content, the field-side pre-battle state was POKED (gato_t0 + $2837|=0x80 Cyclone-learned +
  inventory $2400/$2500 = D6/BD/C6/C8 x 1/3/2/1) and the ROM'S OWN battle staging populated
  $1A80 (=01 12 ✓ the dossier's Cyclone group), $94D0 (display list), $9EE3 (resolver record
  01 12 03 02 = the old gato_cyclone probe EXACTLY) and $1580 (BD/C6 staged, D6/C8 FILTERED ✓
  the item-slice law). Harness bits: Down=0x20 / Up=0x10 (probed via $95DC).
- TECH panel ($95DB=1): hand cols 1-2 on the selected row; slot rows 2/4/6 = tech names col 5;
  "MP" mini-header (r1 col 21-22, chrome-side); the party MP column col 23+ — per present seat
  "curMP -" with the cursor tech's MP cost appended on the CHOOSER's row (oracle: "8 - 2" Crono /
  "12 -" Marle). ':'=0xE3, '-'=0xEB (the tilemap's own codes).
- ITEM panel ($95DB=2): slot rows = name col 3, ':' col 14, count col 16, "Item" label cols 21-24
  per occupied row; the ▼ scroll glyph (font 0x7F) r6 col 30 (present in BOTH captures, incl. empty).
- The fb dumps on these states are INTERLACE-DOUBLED (the strip readable via the tilemap instead);
  brightness reads 0 mid-menu; PEEK_ISOBG works (build/techiso.raw = the submenu's SINGLE-WIDE-WINDOW
  chrome — the re-bake is the documented follow-up; this pass draws the panel text over the phase-1
  command chrome, so the per-box bevels + cmd HP/MP headers remain visible = the known residual).

PORTED (field.c CtBattleHudRender): subPanel gate ($95DB 1/2 && $9609==0) suppresses the command
labels/party rows/box2/cmd-hand and draws the derived panel from the ORACLE-CANONICAL WRAM ($94D0 /
$1580 / $95DF (mirror added) / $95E5 / staged MP blocks) + ROM name tables (CtMenuTechName/ItemName,
MP cost CC/253B). Pinned: field-battle-techmenu-panel / -itemmenu-panel (the poked fixture
saves/battle_re/gato_t0_techitem.raw.wram; CT_BATTLESHOT_HUD shots f420, both verified visually).

## 16. SESSION-23 — the submenu UNIFIED-BOX chrome (user report closed)

User report (translated from Portuguese): "when selecting Tech/Item, the menu should have one unified box, with no subdivisions" — the
§15 panel drew over the 3-box command chrome (the documented residual). Closed:

- **Oracle structure** (p600 ISO-BG1 submenu captures; the submenu-open PPU runs INTERLACED — the
  dumps are line-doubled, which is why the original bake never saw this mode): the ITEM panel window
  = ONE wide box (brightness-profile dark dips at the outer frames x6-7/x248-249 ONLY); the TECH
  panel carries one inner list|MP divider whose exact column is UNRESOLVED between the two capture
  geometries (the dump profile says x88-95; the 1:1 tilemap layout contradicts both 1:1 and 2x
  readings) — omitted, documented residual (unified-no-divider is strictly closer to the oracle
  than the 3-box chrome).
- **The assembly law**: the cmd bake's body dither is EXACTLY 32px-periodic horizontally (measured
  1.000 over x110-244; the "position-dependent" doc note = within-period/vertical only). So the
  unified window = the cmd bake's own oracle pixels re-assembled: outer frame columns/rows verbatim
  (ISO-verified identical, 238-256/256), interior = the body texture cloned on the 32px period
  (base x180). ONE shared layer ctBattleHudBg1Sub (battle_hud_sub.h) — tech/item differ only in
  map-side text. No new colors by construction.
- **Header fact**: the cmd HP/MP mini-headers are MAP-side glyph cells (tiles 0x164-166, ≥0x100),
  NOT chrome — under the panel the whole baked map text pass is suppressed and the tech "MP" header
  is drawn by the panel pass (glyphs 0x65/0x66) at the oracle tilemap position (cols 21-22, row 1).

Re-pinned both panel checks (visually verified: continuous single window, no inner bevels, single
MP header). quick 107/0/1.

## 16b. SESSION-23 (cont.) — the TECH two-window structure (user pointer resolved the geometry)

The user's follow-up ("for TECH it is not unified, there is a smaller one to show the MP") + one measurement
closed the §16 residual: the submenu fb dumps are **2x-HORIZONTAL LEFT-HALF** captures (the submenu
PPU runs HIRES+interlace; proof: Cyclone's 'C' ink at dump x96-107 = real x48 = the strip tilemap's
col 6 exactly). Re-mapping the dark dips: dump x248-249 = REAL x~124 = the junction of TWO windows.

- **TECH** = the list window (real x0-127) + the SMALLER MP window (x128-255): ctBattleHudBg1SubTech
  (the junction assembled from the bake's own right-frame/left-frame columns at x118-137). The faint
  inner feature at real x44-47 (dump 88-95) remains unresolved (residual).
- **ITEM** = ONE unified window (ctBattleHudBg1SubItem) — per the user's original report; the dumps
  only show the left half, no inner divider there.
- **The map-authoritative text columns** (re-counted precisely): tech name c6+ ('Cyclone' c6-12),
  leader pads [0xEF] c13-16, "MP" header c24-25 (r1), per-seat curMP right-aligned ENDING c25
  ('8' c25 / '12' c24-25), dash c27, the cursor tech's MP cost c29 (chooser row); item name c3+
  ('Tonic' c3-7), leaders to c12, ':' = glyph 0x5F (tile 15F) at c14, count c16, "Item" label
  c22-25, the ▼ (7F) c30. The panel pass re-anchored to all of these.

Both panel checks re-pinned; verified visually (list|MP junction present; item continuous).

## 17. 2026-09-18 — ONE action pointer per enemy; an op 00 COSTS A TURN (ported)

User item "go on with the priorities" after the battle opening (battle_boot_turns.md §5 listed "the enemy's approach
walk": GATO decided at cartridge f892 and acted at f1151). There is no walk: the f892 turn runs GATO's **op 00**,
which has no action, and the song is the NEXT turn. The port consumed the op 00 inside the eval and walked past it
inside the turn, so GATO sang one ATB (~255 frames) early; the forest's "missing Hetake turn" was the same defect.

### 17.1 The state (all WRAM)

| byte | meaning | writers |
|---|---|---|
| `$B1D4,Y` (u16, bank $CC), Y = slot*2 | the entry this enemy's next TURN executes | C1/8C9A / 8CC3 (eval), C1/8E72 (turn's step) |
| `$B1E4,Y` (u16) | the entry the last EVAL committed | C1/8CA2 / 8CC6 |
| `$B26B,X` | the section the last eval chose; FF at the boot (FD:B7DB) | C1/8CBB |
| `$B2B6,X` | 1 = evaluated since the last turn | C1/8CF5 (eval), C1/8E7D (turn clears) |
| `$5E15,X` / `$B16E,X` | the target the eval stored (attack / tech) | C1/9833, C1/9894 / C1/98A1 |

FD:A989 clears `$B1D4`/`$B1E4`/`$B2B6` at the boot; `$B16E` is the boot shuffle's taken-marker (C1/80BC), all FF after.

### 17.2 The eval (C1/8C09 → C1/AFD2 → C1/8C3E)

Runs at the boot (C1/80E8) and at C1/881C -- the tail of every handler-12 pass -- for each enemy whose `$B2B6` is 0,
i.e. after that enemy's turn. For an action that is the span's END: the action is synchronous inside the turn
(C1/8E47 JSR C1/AC57), so the step and the eval come when it returns. C1/AFD2 walks block 1's conditions from its
first section every time (with draws) and the first section that holds calls C1/8C3E (from inside its condition
handler -- C1/8EA7 for cond 00 is `JSR C1/8C3E / RTS`):

```
8C6D  LDA $B263,X / CMP $B26B,X / BNE 8CB2    another section -> $B26B = it, $B1D4 = $B1E4 = its first entry
8C75  same section: X = its first entry; op 04 there -> $B1D4 = it (8C7D-8C88)
8C9D  $B1E4 = $B1D4; A = CC:$B1D4 / BPL 8CE0  the entry the pointer stands on
      (negative = the section's $FE) -> falls into 8CB2: REWOUND to the first entry -- the section wraps HERE
8CE0  STA $AEE3 / BMI RTS / JSR ($B85F,X)     the op's EVAL handler (pre-read)
8CF3  $B2B6,X = 1
```

The eval handlers never move `$B1D4` (they step the scratch pointer `$B1D2`): op 00 (C1/9810) resolves the target
(+2, C1/AC14) and writes `$5E0D` = +3, `$5E15` = the target; op 01 (C1/983A) the same; op 02 (C1/983A → 9899) stores
the target in `$B16E` and leaves the mode; 09 copies +1 to `$AEE4`; 03/05/06/08/0A/0C-0F are a bare RTS
(C1/98C4, 9960, 9961, 9966, 9978, 997D-9980), 07/0B/11-15 only clear `$AF24`; 10 counts revivable allies (none →
`$AF24` = 2, not ported); 04 is a random branch (C1/98C5, ported 2026-09-18 -- §18).

### 17.3 The turn (C1/8CF9, `$B2C0` = 0, from C1/84D6)

```
8D34  $B26B,X = FF -> 8E78                        no section ever chosen: nothing
8D6B  $B1D4,Y != $B1E4,Y -> 8E4A                  a pointer the eval did not commit: step over it, no action
8D7A  op = CC:$B1D4 -> $AEE3; BMI -> 8E78
8D88  JSR ($B88D,X)                               the op's TURN handler sets $B3B8
8D93  $B3B8 = 0 -> 8DA4 the action (target $AD8E from $5E15/$B16E; stale-dead -> 8DB5-8DF8) -> 8E47 C1/AC57
      $B3B8 = 1 -> 8E47;   else -> 8E4A
8E4A  $B1D4,Y += FD/BA4A[op]                      THE STEP -- once per turn
8E78  $B2B6,X = 0                                 so C1/881C's eval runs for him
```

Turn handlers: 00 (C1/99B8) `$B3B8 = 2` -- **the move costs the turn, with no action**; 01 (C1/99BE) the target from
`$5E15`, `$AEE4`/`$AEE5` = +1/+2, `$B3B8 = 0`; 02 (C1/9A3D) the target from `$B16E`, `$AEE4` = the tech, `$B3B8 = 0`
(C1/C1DD's `$AF23` → `$AF24 = 2`: no action and no step -- not ported); 0F (C1/9E63) the message (CD/0033) then
`$B3B8 = 2`. **No target draw at the turn** -- `--cputrace` from gato_wait f470/f1140: GATO's attack turn draws only
C1/DC94 (hit) and C1/E2CA (variance), the song only C1/E4AE.

The handler-12 gate (C1/8461-84D6) reads the mode request `$5E0A,X` and the arrival latch `$9826,X` -- and nothing of
the mover: **there is no "mid-segment abort"** (the port's `battleAiAttemptResolve` + reload was the op 00 turn seen
from outside). Block 2 is never read by this path: only the reactive C1/86CB → C1/8780 (which saves `$B1D4` in `$B3CA`
and restores it at C1/8795) walks it -- so "a hit steers the next ATB turn to block 2" (T1.C, battle_pacing_v2.md
f1588's "reload only") was also the op 00 turn.

### 17.4 MEASURED

gato_wait (`--input "100:8,0:2400" --monitor 7EA013,7EB1DA:2,7EB1EA:2,7EB2B9,7EAEE3:3,7E0026 --diff --writer`), GATO =
combatant 6 (slot 3), his block 1 = `00 00 00 00 FE | 01 00 05 01 | 00 00 06 06 | 02 10 05 00 00 00 | FE FF` at CC:A77B:

| cart | what | `$B1DA` | `$B1EA` | `AEE3` | `$26` | port (fixture +229) |
|---|---|---|---|---|---|---|
| f375 | boot eval, section 0 new → first entry (C1:8CC6) | A780 | A780 | 01 00 01 | 1C | f146 ✓ |
| f475 | attack turn (C1:9A25) | A780 | A780 | 01 00 05 | 1E (f476) | f245 (-1) |
| f635 | span end: step (C1:8E75) + eval (C1:8CA5), `$5E10` = 06 | A784 | A784 | 00 00 06 | 1E | f405 (-1) |
| f892 | **op 00 turn: step, no action**, `$B2B9` = 0 (C1:8E80) | A788 | A784 | 00 00 06 | 1E | f662 (-1) |
| f893 | eval: song committed, `$B171` = 01 (C1:98A4) | A788 | A788 | 02 00 06 | 1F | f662 (-2) |
| f1148 | song turn (C1:9A55), `$A013` f1151 | A788 | A788 | 02 10 06 | 20 (f1149) | f922 (+3) |
| f1462 | span end: step to the $FE, eval rewinds (C1:8CC6) | A780 | A780 | 01 10 01 | 21 | f1236 (+3) |
| f1738 | attack turn, `$A013` f1741 | A780 | A780 | 01 00 05 | 23 (f1739) | f1493 (-16) |

The port's GATO now sings from foot (135,85) -- the cartridge's `$1D12`/`$1D29` = 87/55 at f1141 -- where it sang from
(128,100) mid-dance before. Residuals, NOT the AI: the song turn's +3 and the next attack's -16 are the ATB count's
frame rate (102 counts = 251 frames at rest, 271 while GATO walks: the main loop iterates slower under the mover's CPU
load; the port's tick is a flat 2.5 f); the song's C1/E4AE draw lands at the port's span end, not at the turn.

forest (cartridge forest_holdleft_pre `--input "40:2600"`, `#08` f244; port forest_pre.raw.wram, `#08` f68):

| | actions (`$A013`, `#08`+) | no-action op 00 turns |
|---|---|---|
| cartridge | 207, 406, 890, 1095, 1291, 1780, 2254 | +398 slot 2, +1762 slot 0, +1971 slot 1 |
| port, before | (one turn missing from the 4th action on) | none (the "abort" reloaded instead) |
| port, now | 205, 427, 924, 1141, 1348, 1860, 2347 | +419 slot 2, +1842 slot 0, +2064 slot 1 |

Same turns, same slots, same order. The drift (+21 at the 2nd action) is the Hetake's first attack span: 177 frames on
the cartridge (f451 → f628), 202 in the port -- the action's animation length, not the turn machinery (from the span
end to slot 2's op 00 turn: 14 frames on the cartridge, 12 in the port).

### 17.5 The port (src/battle.c)

`battleAiEval` = C1/8C3E on `$B1D4`/`$B1E4`/`$B26B`/`$B2B6`; `battleAiTurn` = C1/8CF9; `battleAiAdvance` = C1/8E4A;
`battleSpanEndArm` = the step (`aiTurnAdv`, the one host-side bit: this engine animates the synchronous action over
frames) + the eval; `battleOpenAction` uses the eval's target, with C1/8DB5-8DF8's stale-dead re-pick
(`LDA #30 / JSR C1/AF22 / LSR x4`). Retired: the per-section cursors of block 1, the filler skip on the ATB turn,
`aiHit` (block 2 on an ATB turn), `aiArm*` + `battleAiAttemptResolve` (the replay and the mover abort). Block 2 keeps
its reactive path (`battleAiRunReactive`), which still walks per-section cursors -- the ROM takes the chosen section's
first entry (C1/8CD6); they agree on every block 2 the suite reaches.

## 18. 2026-09-18 -- op 04, the RANDOM BRANCH (C1/98C5), ported

Reached through the attract's 07D: its Sorcerers (id 51, script CC/9B85) open with
`17 10 00 00 | FE | 04 | 01 01 05 00 | 01 00 08 00 | 01 00 05 00 | 00 00 06 0A | FE`.

```
98C7  LDX $B1D2 / INX / STX $B1D2 / STX $B273      past the 04 byte ($B1D2 = the entry, from 8CA5/8CC9/8CD9)
98D1  TDC / TAX / LDA #64 / JSR C1/AF22 / STA $0A  rand(0..99), base 0 -- a real $26 draw
98DA  CMP #19 / BCS / BRA 9944                     < 25: the 1st entry
98E0  $B273 = $B1D2 += FD/BA4A[op]                 else step one entry (16-bit ADC), then CMP #32 (< 50: the 2nd),
                                                   again and CMP #4B (< 75: the 3rd), again (the 4th)
9944  STX $B1D2 / LDA $CC0000,X / JSR C1/ACCE      X here is STALE (AF22's PLX = the old $2C, or the TAX'd op byte),
                                                   but ACCE's TDC throws that A away and 8C90 reads $B273, not $B1D2
ACCE  $0E = $AEE5 mapped 1,2 -> 04 / 3,4 -> 0D / else itself
994E  $AEE8 = ($AEE8 & F0) | $0E
9958  LDX $B1D2 / PLY / JMP C1/8C90                the JSR's return is DROPPED: the commit restarts at
                                                   `$B1D4,Y = $B273`, `$B1E4 = $B1D2 = it`, reads ITS op (8C9D),
                                                   and dispatches ITS eval handler (8CE7); 8CF3 sets $B2B6 on return
```

So the pointer never rests on the 04: the turn executes the chosen entry, steps past it (C1/8E57), and the next eval
of the same section rewinds to the 04 (8C7D) and rolls again. MEASURED (attract_cena_inicial, `--hookpc C1AF22
--hookwatch 7E0026`, `--monitor 7EB1D4:10,7EAEE3:3,7EAEE8:1 --writer`): the 07D boot eval draws count 64 at $26 =
4E..51 (slots 0-3's cond 17; slot 3's roll 11 < 0x10 holds), count 64 at 52 (this roll, 73 -> the 3rd entry), count 63
at 53 (the chosen `01 00 05 00`'s resolver 05, C1/A488 -> seat 0); `$B1D4` slot 3 = 9B93 (the 3rd entry); `$AEE3..5
= 04 00 0A` at f4611 then `02 00 00`; `$AEE8 = 0A` (C1:9958). The port: `battleAiEval`'s commit loop. Without it the
07D handed back `$29AD` = 52 where the cartridge hands back 54, and the 087's turn order started two draws behind
(battle_boot_turns.md §7). Check: attract-battle-rng-stream-vs-oracle.

## 19. 2026-09-26 -- THE CONDITION WALK PORTED WHOLE (C1/AFD2, the C1/B80D handlers, C1/8C3E): `ct --sweepaieval`

Until now `battleAiEval` was a C loop over sections that evaluated three condition ops (00, 17, 1F) and closed every
other one. Two reasons to port it whole: 139 enemy ids use other conditions (their AI simply never took those
sections), and C3/02DD's seed after a battle reads the `$0008` the walk left (the attract's seed call 93: `$0008` = 00
from C1/8F1E, condition 02's `STA $08`, in the 0AD demo battle -- sequence_table.md 11.13). The walk is a machine over
WRAM, so it is ported as that machine (battle.c `aiWalk` / `aiCond` / `aiCommit` / `aiEvalOp`, the `BtAi` context).

### 19.1 The walk (C1/AFD2, A = the id)

```
AFD2  $B1D2 = $B1D0 = CC/8B08[id*2]; $0A/$0B = 0 (16-bit STA)
AFE6  $AF24 = 0; CC[X+4] != FE -> $B1CF = 1     a SECOND condition follows
AFF6  JSR C1/B4AA                                status override: block ($5E2D+(slot+3)*$80, C1/C90B) +$21|+$26 bit 7
                                                 -> $B1D2 = CC/8D08; (+$1E ^ +$23) & +$1E bit 2 -> CC/8D1E
AFFD  $B239 = the op; JSR ($B80D,X)              condition 1 at $B1D2
B009  $B1CF != 0 -> $B1D2 = $B1D0+4; JSR ($B80D,X)   condition 2 -- run even when condition 1 failed
B024  $B24A,X = $AF24; $AF24 = 2 -> $B2B6,X = 0; $AF24 = 0 -> B08B (end)
B03D  $B242,X = FF; $AF24 != 2 -> $B1CF = 0 and the cursor to the condition area's FE
B060  $B2C0 = 0; the cursor past the entries' FE; CC = FF -> B08B; $B1D0 = it; INC $B263,X; JMP AFE6
B08B  $B263,X = 0
```

C1/8C09 (boot C1/80E8, and C1/881C at the end of every handler-12 pass) runs it for every slot whose `$B2B6` is 0 and
that is present (`$AF02` != FF or `$AF15` bit 7), clearing `$B1CF`, `$B24A,X` and `$B263,X` for EVERY slot first.

### 19.2 The handlers (C1/B80D, 0x29 entries: 23-28 share 18's C1/95FA; 29+ run into C1/B85F)

A handler fails (`$AF24` = 1) or commits. Most commit through **C1/AEFD then C1/8C3E**: C1/AEFD is the two-condition
handshake -- on the first of two (`$B1CF` bit 0) it stores the winner's low nibble in `$AED8` and sets `$B1CF` = 80,
and C1/8C3E returns at once on 80; on the second (`$B1CF` bit 7) it clears it and the commit runs. Handlers 04-07, 0A,
11-17, 19-22 call C1/8C3E WITHOUT C1/AEFD, so on the first of two conditions they COMMIT at once -- and the walk still
runs the second. C1/AE21 is the list reduction (`$AECB` 0 -> INC `$AF24`; 1 -> the first bit-7 entry below the count,
and when entry 0 is unmarked the `TXA` leaves the COUNT itself in A, so the target becomes seat 1; 2+ -> the first
`$AECC,X & 0F` equal to `$B163,Y` whose ENTRY Y (not X) is marked).

| op | handler | reads | holds when |
|---|---|---|---|
| 00 16 19 | 8EA7 95D6 9652 | -- | always |
| 01 | 8EAB | AC14 list; block +3 HP, +5 max (16-bit; "dead" = +$1E bit 7 via a 16-bit `LDA $001D,X`) | HP <= max/2 (marked, AE21); empty list -> RTS with `$AF24` still 0 |
| 02 | 8F11 | +1 side (0 -> resolver 01, else 16, C1/AC2C direct), +2 offset, +3 mask | `$5E4A + seat*$80 + off` has all mask bits |
| 03 | 8F87 | AC14 list, +2 | `$AEFF[seat]` == operand |
| 04 | 8FDA | +2 id, +3 polarity | the id among `$AF02[0..$AEC6)` (or absent) |
| 05 | 9013 | +1 | standing enemies (`$AF02` != FF, `$AF15` = 0) <= operand |
| 06 | 9045 | +1..+3 | 24-bit `$96F1` >= operand |
| 07 | 9082 | +1, +2 polarity | `$B320,slot` >= (or <=) operand |
| 08 | 90BE | AC14 list, +2/+3 (16-bit) | HP <= operand (marked, AE21) |
| 09 / 0B | 9130 / 91F9 | AC14 list, +2 offset (`$0B` = 0), +3 | `[$0A],block` >= / <= operand: the FIRST such entry, then AE21 on the UNMARKED list |
| 0A | 918E | AC14 list (ring `$B23A` picks one), +2 offset, +3 | `[$0A],block` BIT operand |
| 0C 1F | 925D 9765 | AC14, +2 polarity; C1/2986 sub-mode 04 / 0E (C1/2B37 / 2B2B) | dist^2 from self to `$AECC` <= 0400 / 0900 (00) or above it |
| 0D 0E | 92A3 9314 | AC14 list; sub-mode 05 / (+3) + 6; +2 polarity | marks each entry (not self) with `$9872` = 0, AE21, then the LAST `$9872` against the polarity |
| 0F | 938D | C1/2986 sub-mode 08 (C1/2C02, a path test through C1/0222 / 01F9 / 2AEF and the body box) | NOT PORTED (no script uses it) |
| 10 | 93E6 | +2 / +3 pick 09-0C | self's foot Y >= 80 / < 80, X >= B0 / < 50 (C1/2CA7-2CE0) |
| 11 12 | 942A 9474 | +1 (0 -> 40, 1 -> 50), +2/+3 | `$B19E[slot*4]` high nibble (and `$B19F`) |
| 13 | 94D2 | +1 bit 0, +3 | `$B19E` bit 4 |
| 14 | 9514 | +1..+3 | bit 4 clear and: `$AF0A[$B19E & 0F]` == +1; `$AF0A[0]` != +1 (the `AND #10` left X = 0); never; always |
| 15 | 959A | +1 mask, +3 polarity | `$B1A0[slot*4]` has all mask bits |
| 17 | 95DA | +1 | rand(0..99) (C1/AF22, X = 0) < operand |
| 18 23-28 | 95FA | +1 value, AC14 at +2, +3 offset (`$0A` 8-bit: `$0B` is whatever it held) | `[$0A],block` == value; the loop keeps X = the previous entry's seat*2 |
| 1A | 9656 | +2..+4 | an enemy other than id +2 (and than self when +3) |
| 1B | 96A5 | +2 | living PCs < operand + 1 |
| 1C | 96D4 | AC14, +3/+4 | ... and `$AF24` = 1 on every path, the commit's too |
| 1D | 9728 | AC14 (no count test), +3 | `$AF02[target]` == FF (or not) |
| 1E | 975C | -- | commit, then `$AF24` = 1 |
| 20 21 | 97AB 97C0 | `$AEB3,slot` / `$AF15,slot` | nonzero |
| 22 | 97D5 | AC14, +2/+3 | `$B158[target]` < (or >=) operand; `$09` = 0 after |

### 19.3 The commit (C1/8C3E) and the eval handlers (C1/B85F)

`$B242,X = $B239`; `$B1CF` = 80 -> RTS. The cursor to the condition area's FE, +1 -> `$B273`; `$B2C0` -> 8CD6 (the
reactive arm); the rest as §17.2. The eval handlers: 00 (9810) and 01/02 (983A) step `$B1D2` and run C1/AC14 on +2;
983A with 2+ targets picks the first the ring `$B23A` names, and with none (or none named) sets `$B242,X` = FF and
**`$AF24` = 2** -- the walk then goes on to the next section and C1/AFD2 leaves `$B2B6,X` = 0, so C1/881C walks this
enemy again on every pass until a target exists; 01 writes `$0C` = the slot (STX); 02 stores the target in `$B16E` and
steps `$B1D2` by 2. 07, 0B, 11-15 clear `$AF24`; 10/16 (9981) count revivable slots into `$0C` (`$10` the index) and
set `$AF24` = 2 on none; 04 as §18 (its `STX $B1D2` at 9944 stores AF22's restored `$2C` or the last stepped op; 8C9D
replaces it).

### 19.4 The grade: `ct --sweepaieval` (pinned as sweep-ai-condition-walk)

Five battle states (the attract's two fights, bat3, GATO, the Hetake forest) seed the WRAM and the bytes the handlers
read are fuzzed; the ROM runs the same image from the same entry (C1/8C09's: DB $7E, D 0, M 8, X 16) with the NMI and
IRQs disabled (the CPU's own accesses advance the beam: without it a vblank landed inside every ~25th case). Compared:
ALL of WRAM but the stack page.

| phase | what | result |
|---|---|---|
| 1 | C1/AFD2 for every id from each state, random slot, to C1/8C53 | 7122/7122 |
| 2 | each handler alone, the cursor on every 1/40th occurrence of its byte in bank $CC, `$B1CF` 0/1/80 | 6925/6925 (all 35 ported handlers) |
| 3 | C1/8C3E + the eval handler, a real section, `$B263`/`$B26B`/`$B1D4`/`$B2C0` varied | 1208/1208 |

2026-10-11: C1/2986's simple sub-modes are ported (battle.c `aiPos2986`: the head's mirror copy, 04/0E with C1/0089's
scratch -- `$77`/`$78`, `$80`, `$82`, `$AD`-`$B0`, `$9893` --, 05-07, 09-0C), so 0C, 0D, 0E, 10 and 1F are graded: the ROM
runs C1/0003 entry 5 when its sub-mode is one of those, and the feet are fuzzed on a stream of their own. Phase 2:
0C 159, 0D 165, 0E 156, 10 200, 1F 194 cases, byte-exact. The census of CC/8B08: 0C in 66's and 6A's counters
(`0C 06 00 00`, `0C 17 01 00`), 10 in F8's (`10 00 01 01`), 1F in the counters of 59, 79, 7A, 90, 92 and AC; 0D-0F in no script.

EXCUSED (counted): a case whose walk meets an unported C1/0003 (0F, 0E's sub-modes 08 and up) or a resolver without a WRAM port (05/06 need the
live host's battleAiTarget; 07-09, 11-17 underived). The live battle closes such a condition and gives such a resolver
an empty list (the old loop's policy), with a `battletrace ai-unknown-*` line.

### 19.5 What moved in the port

* C1/8C09 whole at the boot and at every span end (`battleAiEvalAll`), not one slot: an enemy whose walk committed
  nothing is walked again, with its draws.
* The reactive counter clears `$B2B6` (C1/8780 -> C1/8CF9 -> C1/8E7D) so the span end's C1/8C09 walks the countering
  enemy again -- without it GATO's post-counter eval (and its resolver-05 draw) was skipped (the old loop ran it
  unconditionally).
* `$5E15` / `$B16E` hold the raw `$AECC` (FF included); the port's mover is still handed a seat (C1/C8F7, FF -> the
  lead PC) and the turn's own FF arm (C1/8DA4-8DF8) takes the FF as the ROM does.
* The reactive path's cond 1F reads its polarity at +2 (C1/9786 `LDA $CC0001,X` with the cursor AC14 left on +1),
  not +3.

## 20. 2026-09-26 -- THE TARGET RESOLVERS 07-17 PORTED (C1/B8BB[07..17]); every value of the table now resolves

Found through Yakra (disasm/notes/battle_yakra.md): his attack 1 on target 07 hit the port's "unknown resolver" skip.

| value | routine | what it lists |
|---|---|---|
| 07 | C1/A4E0 | C1/2986 sub-mode 1 (C1/29FB): the FARTHEST living PC; `$9873` bit 7 -> none; `$AECC`, `$AECB = 1`, `$AD8D = 1` -- no `$AEFF` re-test (06 has one) |
| 08 | C1/A508 | the living PC with the LOWEST HP -- `TDC / LDY $5E30 / CMP #00 / BNE` never branches, so seat 0's HP never enters (it is only the default); `$02/$03` keep the best with `$03` zeroed |
| 09 | C1/A541 | the 0A-0D family one byte down: `$0A = 1D`, C1/AD68 |
| 11-14 | C1/A603 / A633 / A663 / A693 | 0E-10's code (C1/AE70 x3 + C1/ADA1) with `$0A` = 21 / 21 / 20 / 01 and the mask `$AF1F` = 04 / 40 / 10 / 08 |
| 15 | C1/A6C3 | every living enemy seat 3..10 but the actor (`$0A = $B252 + 3`, `$0B = 0`) |
| 16 | C1/A6ED | every living enemy seat 3..10 |
| 17 | C1/A709 | C1/2986 sub-mode 2 (C1/2A43): the NEAREST other enemy (seats 3..10, the actor skipped before `$9870` is written); `$9873 == $986F` -> `$AECC = FF`, `$AECB = 0` |

C1/2986 is ported once (battle.c `battleAiPosScan`) for 06/07/17: the mirror `$A039/$A050` of the actor and of `$9870`
refreshed from the live feet, then C1/2B37 on the MIRROR -- `|dx|^2 + |dy|^2` summed in 16 bits (a far pair wraps),
`$9872 = FF` beyond `$9893 = 0x0400` -- and ties to the later seat in all three sub-modes; `$9873` is never reset (no
candidate: its old value). 06 moved onto it with no check moving (683/683). `ct --sweepaieval` micro-executes 08, 09 and
11-16 against the ROM: the walk 7161/7161 and the handlers 7258/7258 byte-exact over all of WRAM (were 7122 / 7038 with
39 / 220 more cases excused). 06, 07 and 17 read the host's positions and are graded by the fights (Yakra's 07: seat 1,
the cartridge's `$9873 = 01` at C1:2A37).

## 21. 2026-10-01 -- THE ENEMY'S COMMAND UNDER THE RULE: C1/ACF2's header, the decode at C1/4058's point, type 3, the element rider

Measured with `tools/battle_ab.py enemies` (every enemy id 00-FE poked into GATO's slot, `$1111`, on both sides; the rule's
oracle `CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1`; the first divergence of each enemy's first actions).

- **C1/ACF2, the action header** (`$AE90 = 0`, `$AE91 = $B18B`, `$AE92 = $AEE3`, `$AE93 = $AEE4`): written by the turn's
  C1/8DA4 `JSR C1/ACF2`, i.e. at C1/4058's point, NOT by the decode (op 01 C1/99BE / op 02 C1/9A3D write `$AEE3`/`$AEE4` and
  `$B18C` only). The port wrote `$AE93` at the decode, frames early (53 enemies' first divergence). A REACTIVE turn runs the
  same head (C1/8CF9: `$AF24`, `$B252`, `$B18B`, `$AE93-96`, `$AE99-9B`, `$AE97/98 = FF`) and the same header -- without it
  GATO's flurry read the previous attack's header (179 frames short).
- **The record header FD/AB01 (op 01) / FD/AAD2 (op 02)** at the decode (C1/9A25 / C1/9B1C), in the port's
  `battleAiRecordHeader`.
- **The decode at C1/4058's point.** C1/8DA4-8E15 (C1/ACF2, C1/8E15's staging, C1/D7C4 / C1/D8D1, the kind, C1/D2DE's hit
  roll, formula, cut, rider and extra effect) is synchronous in the turn, two frames before C1/4058's `INC $A013`: gato_wait
  under the rule writes `$AEE3 = 01 00 05` and `$26 = 1E` at f433, `$A013` at f435 (the song: `02 10 06`, `$26 = 20`, f1004 /
  f1006); enemy 63's tech `$AD89 = 10` / `$B328` at c425, `$A013` c427. The port computed the effect at the window's open,
  two frames late (a status landed on the cartridge's dispatch frame, on the port's open: 15 enemies). Now
  `battleArriveDecode` runs at both C1/4058 sites; `battleArrive` only opens.
- **Enemy type 3 = magic (C1/E203)**: base = `$AEEF` x 12 x (+0x12 + +0x39) / 15 ... the bonus `rand((MAGIC*77) >> 8)`;
  the enemies' damage ladder had only 1/2/4.
- **The extra effect `$B2CC`** (CC:2A05[$B2CC*3] through C1/FA61) runs after an enemy's C1/DCAD as after a PC's: the
  element routine 0A scales the damage by the target's resistance (`$B190` bits 80/40/20/10 -> +0x3F+k).

## 22. 2026-10-02 -- THE ENEMY'S COMMAND THROUGH THE KINDS, ON ITS VICTIM LIST

- **The kinds (C1/D9D7) are ONE table for every attacker.** C1/D7C4 / C1/D8D1 end `LDA $AEE6 / ASL / TAX / JSR ($D9D7,X)`
  (D874-D893: `$AE55 = record+2`, `$AE4F = 0`, `$B29A[0..A] = 0`); one id (D816, `$B2C7` = 0, no loop); D896-D8C1 (a
  miss of a record+0 bit-7 command skips C1/EB68), D8C4 `$B18E..$B191 = 0`. The port's `battleKindRun` (00 heal, 01
  cure, 02 status, 06 charm) is now called by the PCs' tech (C1/D6FB) and the enemy's command (`battleEnemyExecute`);
  03 is C1/D2DE (`battleD2DE`). Kinds used by enemy commands: 00 (9), 02 (12), 03 (244), 05 (10), 07 (2).
- **C1/D2DE opens `JSR C1/E9A3 / JSR C1/E9B8`** ($B1F4 / $B1F6 / $B1FD), read by the extra effect's routine 11 with no
  roll between when the attack cannot miss.
- **Extra-effect routines (C1/FA61 table)** now ported besides 02/0A/0B: 06 C1/EE9F (a random status by a 0..100 roll:
  <28 04, <41 80, <5A 02, <64 40, else the roll), 09 C1/EF14 (a 0..99 roll <= $1C -> curHP - 1, `$B1FC |= 01`), 0E
  C1/F095 (+0x19 bit 7 clear and a roll <= $1C -> curHP / 2), 0F C1/F0BD (block byte +$1C &= ~$1E), 11 C1/F10D
  ($1E: the target's / else the attacker's curHP = 0, the self-destruct). Enemy commands use 0A (62), 0E (7), 09 (4),
  11 (3), 06 (1), 0F (1).
- **The victims: C1/C1DD at op 02's decode (C1/9AEB)**, the group CC/86C9[$B18C * 2] (enemy table: 08 all PCs 119, 07
  one PC with C1/C8A8's re-draw 107, 00 11, 01 own side 6, 02 6, 0B/11/32 with an area byte 7). The head's C1/CB93 and
  the attacker gates (`$AF23 = 1` -> C1/9B3B: `$AF24 = 2`, no name, no action). Op 01 builds none (one target). The
  list rides the pending dispatch to C1/D2DE's loop (last victim first); records per victim (C1/DE6A / C1/E904).
  MEASURED (enemy 18): tech 3E `$AD8D = 03 00 01 02` (C1:C3B1), all three PCs' HP 0 at f1432 on both sides.

Census after these: 144/255 enemies identical over their first actions (from 123; none worse).

OPEN: the DRAIN kind 05 (C1/D371 -> C1/E77B: the target's HP or MP to the attacker, both slots staged) and kind 07
(C1/D3F4: heal + the attacker's own curHP staged against it) need the span end's applier as the ROM has it -- EVERY
action's span end is C1/AC57 `JSR C1/BFA4 / JSR C1/AC85` -> C1/EC7F over the `$B328` slots (bank-C1 scan: EC7F is called
from C1/8B0C and C1/AC85 only), where the port applies per victim (battleApplyDamage) and runs EC7F for status
actions only. The area pass of C1/C1DD (`$0C` != 0, C1/C586, CC/2AB0); C1/C1DD on the reactive path; AI ops 05/07/0A..
and resolvers 18-38; HP value differences (14).

## 23. 2026-10-02 -- THE TURN HANDLERS (C1/B88D, ops 00-16) AND THE RESOLVERS 18-38 (C1/B8BB) PORTED; the message box holds the turn

The two tables the port still skipped (`ai-unknown-turn` / `ai-unknown-resolver`). Every entry of both is ported as the
ROM runs it on WRAM (battle.c `aiTurnOp`, `battleAiResolveList`, `battleAiScanEnemies`, `battleAiLowestHpEnemy`,
`aiFDB438`, `aiReviveLoop`) and graded by micro-execution (`ct --sweepaieval` phases 4 and 5, below).

### 23.1 The census of bank $CC (every condition's and entry's targeting byte, every entry's op)

Resolvers asked for (ids): 18 by 19 scripts (cond 03 and entries), 19 (8B D9), 27 (16), 28 (58), 2A (CD D2 3A), 2B (8),
2C (D4), 2D (13), 2E (97 CC CD), 37 (B3 20 25 36 38), 38 (F6 20 09). 1A-26, 29, 2F-36 are asked by no script (reached
only through the condition sweep's raw bytes). Turn ops used: 07 (10 ids: 16 17 1B 2B 3E 3F 40 41 61 74), 0A (10), 0B
(20), 0C (5), 0D (18), 0F (36), 10 (4: C2 D1 D4 E7), 11 (13), 12 (20), 14 (3: 9C B5 B6), 15 (BE), 16 (EF). 03, 05, 06,
08, 09, 0E, 13 appear in no script. (build/scratch/ai_scan.py walks CC/8B08[id] by FD/BA4A.)

### 23.2 The resolvers (C1/B8BB[18..38]; X 16-bit, DB $7E)

| value | routine | what it lists |
|---|---|---|
| 18 | C1/A737 | C1/2986 sub-mode 3 (C1/2A98): the FARTHEST enemy seat 3..10, the actor NOT skipped (`$86` from 0, `LDA $AF / CMP $86 / BCC` -- ties to the later seat); `$9873 == $986F` -> `$AECC = FF`, `$AECB = 0`. Host positions (battleAiPosScan mode 3) like 06/07/17 |
| 19 / 26 | C1/A765 / AA61 | the living enemy with the LOWEST HP (26: not the actor `$B18B`); every seat 3..10 in the race (08 leaves seat 0 out); `$AECB = 1` even when none qualified; `$02` = the best's low byte, `STZ $03`; `$0E` (8-bit) ends at 0B |
| 1A-25 | C1/A7A9 .. AA25 | C1/AE70 over seats 3..10 then C1/ADA1, `$0A` / mask = 1D FF, 1D FF, 1E FF, 1E FF, 1F FF, 1F FF, 1E 02, 1E 80, 1E 04, 21 40, 1D 02, 19 01; 1B 1D 1F take the actor too, the others skip seat `$B18B`; `$0E/$0F` = 0006 then the `LDX $0E / INX / INX / STX $0E` walk |
| 1F | C1/A8C5 | ⚠ `85 0E` = `STA $0E` at A8E9 where its siblings `STX $0E`: $0E takes C1/AE70's accumulator (FF absent, the block byte, +$1D dead, +$20, or the entry ORA 80), so from the third seat the X C1/AE70 gets is that byte + 2 (the block base read from FD/A80B + X). battleAiCandidate now returns that A |
| 27-2E | C1/AAAB .. AAF8 | the fixed seat 03..0A, `$AECB = 1`, no presence test |
| 2F | C1/AB03 | every enemy SLOT 0..7 (not a seat) whose `$AF15` bit 7 is set; two or more -> `rand(0..n-1)` (C1/AF22, X = 0), the pick in `$AECC`, `$AECC[1..A] = FF`, `$AECB = 1` |
| 30-36 | C1/AB4E .. AB90 | the fixed seat 00..06 |
| 37 | C1/AB9B | the PC with the HIGHEST HP, no presence or life test, seat 0 the default, ties to the later (`CPY $02 / BCC`); `$02/$03` as 08's |
| 38 | C1/ABC9 | a RANDOM other living enemy (3..10 but `$B252 + 3`, `$0A/$0B`); two or more -> `rand(0..n-1)`, the pick, then `LDA #FF / LDX #0001 / STA $AECC,X / INX / CPX #0000 / BCC`: the compare is with ZERO, one store ($AECC[1] = FF), the rest of the list stands |

Resolvers 06/07/17/18 now also write C1/A4B9's `$986F` (the actor) and `$986E` (the sub-mode) before C1/2986.

### 23.3 The turn handlers (C1/B88D, from C1/8D88 `JSR ($B88D,X)`)

Entry: `$B1D2` = the entry (C1/8D77 STX -- the port did not write it), B = 0 (C1/8D74 TDC), `$AEC8` = the slot (C1/84D3,
the scanner's, now written by battleAiTurn), `$B18B`/`$B252` the turn head's. Verdict in WRAM: `$B3B8` 0 -> C1/8DA4 (the
C1/ACF2 header, the re-check, C1/D7C4 for `$AEE3 = 02` else C1/D8D1, C1/AC57); 1 -> C1/8E47 JSR C1/AC57 at once on the
header the handler wrote; else the step alone. `$AF24` != 0 -> C1/8D90 -> 8E78: no action, no step.

| op | handler | does |
|---|---|---|
| 00 | 99B8 | `$B3B8 = 2` |
| 01 | 99BE | `$B18C = 0`, `$B1FC |= 02` (one victim: C1/D357), `$AD8E = $AECC = $5E15,slot`, `$AD8D = $AECB = 1`, `$B3B9 = 1` on a PC, C1/AD09, `$0E = $AEE4 = +1` (1 -> `$B18C` = block +$1B), `$AEE5 = +2`, FD/AB01, C1/AC89, C1/ACCE, `$B3B8 = 0` |
| 02 | 9A3D | `$B1FC &= FD`, `$0E = $AEE4 = $B18C = +1`, the target from `$B16E`, the co-targets +3 / +4 through C1/AC14 into `$AE97/$AE98` (`$AECC` kept in `$10`), `$B2EB/$B2EC` (FF stays FF), `$B2AE`, C1/C1DD (`$AF23` -> `$AF24 = 2`), the victims into `$AECC`, `$B3B9`, C1/AD09, C1/AD35, FD/AAD2, C1/AC89, C1/ACCE, the name at +5 (CD/0033) |
| 03 04 06 08 0E | 9B46 9B47 9B8C 9C6E 9E62 | RTS -- `$B3B8` as it stands |
| 05 | 9B48 | runs the next +1 entries through the commit's tail (`JSR C1/8C63`) -- NOT PORTED, no script |
| 07 | 9B8D | TRANSFORM: `$AECC` = the id +1, `$AEE4` = +2, `$12` = +3, `$AEE3 = 02`, C1/ACF2; a seat 3..A whose `$AF0A` is that id, whose `$AEFF` is not, never swapped (or swapped with the actor) and hidden (`$AF12` bit 7) -> it appears (`$AF12 &= 7F`, `$AEFF` = the id), `$AECC` = it, C1/AD09, the actor leaves (`$AEFF = FF`, `$AF12 |= 80`) and the forwarding table `$AE6D` swaps the two; +3 != 0 -> FD/B438 (the arrival's block from its record), else it takes the actor's curHP; none -> `$AF24 = 2`. Then `$AEE3 = 05`, `$B3B8 = 1` |
| 09 | 9C6F | `$AEE4` = +1, `$AEE3 = 02`, C1/ACF2, the actor's +$1D bit 7 and `$AEFF = FF`, `$AEE3 = 07`, `$B3B8 = 1` |
| 0A | 9CB3 | C1/AC46, `$AEE4` = +1, `$B3C7` = +2, `$AEE3 = 02`, C1/ACF2, the actor's curHP 0, +$1D bit 7, `$AEFF = $AF0A = FF`, `$AECC` = the actor, `$AECB = 1`, the message (+2), C1/AD09, `$B3B8 = 1` |
| 0B | 9D1B | `$0E/$10/$12` = +1/+2/+3 (16-bit STX), `$B3C7` = +4; actor block `+$0E` = `$10` (or ORed with it when `$12`); the message; `$B3B8 = 2` |
| 0C | 9D72 | `$5E2D + $B18B * $80 + (+1)` moved by +2 (bit 7: down, the result 0 when its bit 7 is set -- `SBC / BPL`, the N flag, not a borrow; up: FF on a carry); the message (+3); `$B3B8 = 2` |
| 0D | 9DCE | the first set bit of +1 from 80: 40 `$B320,slot = 0`; 20 `$5E4A + slot * $80 &= FD` (the SLOT's block, not +3); 08 `$96F1-3 = 0`; 04 `$B320,slot++`; 02 `|= 02`; 80/10/01 nothing; the message (+2); `$B3B8 = 2` |
| 0F | 9E63 | the message (+1), `$B3B8 = 2` |
| 10 / 16 | 9E78 / A396 | `$0E` = +1 (the HP mode), `$12` = +2, the message (+3 / +B), C1/AC46, [16: FD/A990's four actor block bytes +3..+A -- whose `STX $0E` leaves the LAST offset in `$0E`, so 16's HP mode is +9], THE REVIVE LOOP (slots whose `$AF0D` is set, `$AF02 = FF`, `$AF15` bit 7 clear: the seat appended, `$AF02 = $AF0D`, `$AFAE = $B15B`, `$B26B = FF`, `$AE6D,slot = slot`, `$AE85 = 0`, FD/B438, curHP by the mode 1 /2, 2 /4, 3 = 1), `$AECB` = the count, `$AE91 = $B18B`, `$AE92 = 02`, `$AE93 = $12`, `$AE94 = 0`, C1/AD09, C1/AD35, `$B3B8 = 1` |
| 11 / 14 | 9F5A / A188 | four actor block bytes (+1,+2) .. (+7,+8), 14 through C1/A3D1 (clamped: bit 7 of the value down to 0, else up to FF); the message (+9); `$B3B8 = 2` |
| 12 / 15 | 9FD2 / A20B | the message (+F, 16-bit STX), five actor block bytes (+5,+6) .. (+D,+E) (15 clamped), then 02's tail from `$B1FC` on -- the target from `$B16E`, which the eval handler (C1/99B4) never stores; the co-targets' `$AEFF` read WITHOUT 02's FF test (`$AEFF + FF = $AFFE`); `$AEE3 = 02` after C1/C1DD |
| 13 | A14E | C1/AC14 (+1), `$0E` = +2, `$2C/$2D` = +2 & 7F; the target's `$B158` moved (bit 7: down, 1 when the result's bit 7 is set; up: FF on a carry); `$B3B8 = 2` |

FD/B438 (the enemy block from CC/4700 + id * 23): the block zeroed (FD/B4D6), +0 id, +3/+5 and +4/+6 the HP, +$12, +$1D-$1F
= 0, +$22-$26 = rec[3..7], +$20/+$21 = +$25/+$26, +$37-$42 = rec[8..19], +$19-$1B = rec[20..22]; `$00` = 3, `$01` =
(id * 8) >> 8, `$04` = id. FD/AB01 / FD/AAD2 now leave the multiply's `$28/$2A/$2C/$2E` (C1/C90B, the carry into `$2A`'s
bit 0 is the caller's: 01's `CMP #01` / the LSR of the record path; 02's `CMP #03`, then C1/AD09's and C1/AD35's).

### 23.4 What the turn does with the verdict (battleAiTurn / battleOpenAction / battleThinkTick)

* `$B3B8 = 0`: the action as before (`$AEE3 = 02` -> a tech: 02, 12, 15), the name box from the handler's CD/0033.
* `$B3B8 = 1` (07's arrival, 09, 0A, 10, 16) -- BT_AI_ACT_HDR: no C1/8DA4 (no C1/ACF2 of the turn, no re-check, no decode);
  the think (C1/4058's two waits, after 0A's message box) and the window on the header's class-2 script (`$9877` =
  `$AE93`, C1/469A); `$AEE3` keeps the handler's (05 07 02 10 16); no victim value of the port's at the close.
* A turn with no action whose handler posted a message (0B 0C 0D 0F 11 14): CD/0295 runs INSIDE the handler -- the turn
  is held its 11 frames (enMsgHold), then the step, the reload (C1/84D9), the handler's tail (C1/883D, C1/B223, C1/881C)
  and the loop's head (C1/8120), all on the frame the box's last wait returns. The box's frames 3-11 are CD/3E75s, whose
  `JSL C1/0012` = C1/10E3 is the menu pass on `$993A` (battleNameBoxFrame; the op-02 name box gets it too).
  MEASURED, the rule's oracle (gato_wait, `PEEK_POKE 1111=C0`): C0's `0F 20` turn c381 (`$CE18 = 20` CD:02AD), `$993A = 00`
  C1:10EB c384, `$CE17 = 20` c391, on c392 the step (C1:8E75), `$B3B8 = 02` (C1:9E77), the reload (`$AFB1` C1:84E1), the
  eval (C1:9884) and `$0027 = 0E` (C1:8124); the next ATB pass c394. The port ran the whole tail at once: C0's and 04's
  attacks 11 frames early (559 / 570); with the hold and no menu pass, 5 late; now 570 on both sides.
* 07 that finds no one to become (3E-41 in GATO's arena): `$AF24 = 2` -> no step, no action, the eval again. MEASURED
  (enemy 3E): `$AE90-93 = 00 06 02 7D` (C1:AD08) and `$B3B8 = 01` (C1:9C6D) at its turn c405, `$B1DA` unchanged.
* 0A (enemy 3A, `0A 93 48`): turn c425 (`$AEE3 = 02 93`, curHP 0 C1:9CE6, +$1D = 80, `$AEFF/$AF0A = FF`, `$CE18 = 48`),
  `$CE17` c435, `$AE96 = 02` (C1/AD09) and `$B3B8 = 01` c436, `$A013` c438 with `$9877 = 93` (C1:469A), the close c586.

* The enemy tech's anim selector (C1/4682-4697): `$9877 = CD/5526[$AE93 * 7]` +0 -- the record battleFxStageCfg already
  read for `$987A..`; the port mirrored `$AE93`, right below 71 and wrong for 65 records (71 -> 7D, C1 -> B1, E8 -> 36,
  E9 -> B0 ...). MEASURED (enemy 7B): tech E9, `$9877 = B0` by C1:469A at c427.

### 23.5 The grade: `ct --sweepaieval` phases 4 and 5

Phase 4: C1/AC14 with every value 00-38 (the cursor before 24 bank-$CC occurrences of the byte, each from the five
states, fuzzed, `$B18B` drawn). Phase 5: each C1/B88D handler from C1/8D88's JSR, the cursor on every script entry of its
op (both blocks of every id and the two status scripts) topped up with 24 raw occurrences spread over the bank; the ROM
side stubs CD/0033 (its A compared with the port's `msg`) and C1/C1DD (02 / 12 / 15: the victims are not this sweep's --
both sides write the same seeded `$AF23` / `$AD8D` / `$AD8E` list at its JSR and RTS, so the tail after it is graded).
07's arrival is seeded in half its cases (a hidden formation seat with the id), 0C's block byte high in half. All of WRAM
compared but the stack page; FAIL when a value or a handler is never graded.

| phase | what | result |
|---|---|---|
| 4 | C1/AC14, values 00-38 (06 07 17 18 excused: C1/2986's host positions) | 7268/7268 byte-exact; every other value graded 102-220 times, 1F's `STA $0E` path 220 |
| 5 | C1/B88D ops 00-16 but 05 | 4417/4417 byte-exact (832 past the stubbed C1/C1DD); CD/0033 reached 1957 times, its A right every time; 07's arrival 82, the revive loop 122 |

Mutations (tools/regress.py --mutate): sweep-ai-condition-walk gains five, all killed -- 1F read as `STX $0E`, 38's
fill as 2F's, 0C's subtract clamped on the borrow, 12/15 given 02's FF test, C1/AD35's carry dropped before FD/AAD2's
multiply. battle-enemy-turn-handlers-vs-oracle (tools/battle_ab.py enemies 3A,3E,C0,04,0A,7B, every line IDENTICAL):
four, all killed (the box's hold, 0A's C1/AC57, 07's `$AF24 = 2`, the CD/5526 selector). The box's C1/10E3 menu pass is
measured (c384) but moves no byte the census monitors once the loop's head runs on the box's last frame: not a mutation.

Phases 1-3 move with the new resolvers: the walk 7184/7184 (496 excused, were 519), the handlers 7742/7742 (513, were
978), the commit 2868/2868 (2252, were 2377) -- 0 mismatches throughout.

### 23.6 Census (tools/battle_ab.py enemies, all ids, the rule's oracle)

Every id 00-FE in GATO's arena (`$1111` poked on both sides), 1500 frames, every monitored change: **158 -> 184 of 255
identical, none worse** (the baseline is this branch's base, 1a979db; 181 with the handlers and resolvers alone, 184
with the CD/5526 selector).

* Now identical (26): 04 0A 3A 3E 3F 40 41 43 5D 5F 7B 7E 7F 8B 8E A0 A1 B8 BC C0 CF D2 DC DD E6 EF -- the message box
  (0F at the opening: 04, C0, ...), 07's miss (3E-41), 0A (3A, 43, 5D, 5F, 7E, 7F, A0), 0B / 11 / 12 (0A, BC, E6, EF,
  ...), the selector (7B, 8E, B8).
* First difference later (16): 07 09 20 29 42 79 7C 80 9F AC BE C1 CB D9 E3 E5. What stops them now is outside the AI:
  20's attack is a DRAIN (kind 05, C1/D371 -> C1/E77B: `enemyfx ... kind 5 NOT PORTED`; resolver 37 picks the cartridge's
  seat 2, `$B2AE = 02` c437 on both sides); D9's tech 14 on itself (resolver 19) fires its block-2 counter on the
  cartridge (the reactive walk, OPEN below); the rest part at an HP or status value at a span end (BE CB E3 E5 42 AC
  80: c0.ST; 07 09 29: an HP) or at a later turn that follows from one (79 9F C1 7C).

### 23.7 OPEN

* C1/AC57's effect for the `$B3B8 = 1` turns: C1/AC85 -> C1/EC7F applies what the `$B328` slots hold; the port applies no
  value of its own (the span end's applier is the HP session's).
* 07's arrival as a picture: a hidden formation seat appearing (its staged sprite, `$96F5`) -- no fixture reaches it.
* 05 (C1/9B48: the next entries through C1/8C63) -- no script uses it.
* The REACTIVE path (block 2, C1/86CB -> C1/8780 -> C1/8CF9 with `$B2C0` set) still runs battleAiRunReactive's per-section
  cursors over the old three-condition walk, and only its 01/02 fire: block 2's 0B / 0C / 0D / 0F / 11 / 12 / 14 (most of
  this table's users) are not reached as the ROM reaches them. The turn handlers are the same; the walk is not ported.
* The PC command's own C1/AD09 / C1/AD35 (C1/BBCE for a tech, C1/BFED for a physical) are not in the port's PC dispatch,
  which keeps no ROM `$AD8E/$AECC` list there: `$AE95/$AE96` now carries the last ENEMY turn's mask into a PC action
  (80 = Crono: the rule's cartridge, gato_wait with battle_ab's Cyclone drive, has `$AE91-$AE98 = 06 01 00 00 00 80 FF FF`
  by C1:AD31 at GATO's attack c429) where the cartridge has the PC's own (the Cyclone on GATO: `00 02 01 00 00 02 FF FF`,
  c680). Until this change nothing in the port wrote it (00). field-battle-pc-tech-sfx / -asc-off-at-script-end pin it.
* While CD/0295 holds a turn, the battle mover (C1/3714 from the idle tick) does not run in the port; the cartridge's box
  runs C1/10E3 only (the menu pass), so this matches it, but no fixture measures an enemy that walks during a box.

## 24. 2026-10-03 -- THE AREA PASS OF C1/C1DD (C1/C586-C730) AND THE BAND C1/25A3, for an enemy's tech; the groups 0A-1B / 32

The seven enemy techs with an area byte (CC/86CA[$B18C * 2]) all carry 08: 14 (group 32), 2E / 46 (11), 30 / 6E / 83 / F1
(0B). Twenty-one enemies' scripts use them (a byte scan of CC/8B08: 07 09 34 54 56 68 76 7E 7F 8B 96 99 9E A5 B1 B2 BA BE C5
D9 F6). The port took group 0B / 11 / 32 as "the decode's one" and kept the list; the cartridge narrows it to a band.

**The groups the port had lumped (C1/C477-C55D):** 09 a random pick (`LDA #64 / JSR C1/AF22` by thresholds 08 / 0C / 12 /
14 into C1/C82D or C1/C37F / C1/C43B -- no enemy table holds it, not ported); 0B-0F, 12, 1A: `JSR C1/C82D` (an enemy's
arm C1/C8A8, the same as group 07: no PC left -> C1/C72B, an absent $B2AE re-drawn at random) and `$3A = 2`; 10, 11, 13,
14, 15, 1B: `$3A = 3` (the list as the decode left it); 0A: `$3A = 1`; 32: `$AD8E = C1/C8F7($B2AE)`, `$AD8D = 1`, `$3A =
4`, `$0A = 1`. Then C1/C55F (`$3A` 0 or 2: `$AD8E = C1/C8F7($B2AE)`), `STZ $AF23`, and `$0C != 0` -> the area pass.

**The pass (C1/C586):** `$3B = CC/2AB0[$0C]`; `$14 = CC:[$3B]` entries of 3 bytes `$3D, $04, $08 | $0E << 7` from
`CC:[$3B + 1]`. C1/C74C stages (an enemy, C1/C782) `$06 = $B2AE`, `$0C = $B18B`; `$0A` = 1 (C1/C27B). Per entry, C1/C95C
[$04 & 7F] copies them into $9604-$9608 (06 = C1/C813: `$0A $0C $06 $08 $0E`); `$04` bit 7 -> C1/C736 (`$986E = $3D`,
`LDA #05 / JSR C1/0003` = C1/2986, no list) else C1/C741 (`$99CC = $3D`, `LDA #07` = C1/1FDD, whose table C1/1FEA runs
25A3 / 2701 / 2701 / 23A4 / 25A3 / 23A4 / 2332 for $99CC 0-6). The list `$99C0 + $12 * 11` (C1/C90B multiplies) goes over
`$AD8E,X` / `$AECC,X` -- an enemy whose `$AF12` bit 7 is set left out, its index untouched -- and the count of non-FF
entries into `$AECB` / `$AD8D`. Area 08 = CC/2B30 `01 / 00 06 02`: one entry, C1/C813, the band, half-width 2 * 8.

**The band C1/25A3:** `$AE = $9607 * 8`; `$DB` = C1/0222 from the target ($D5/$D6) to the attacker ($D3/$D4) -- it returns
with A = $DB on every path (C1/027C-0298) -- and C1/01F9 (C0/F900, C1/00A7's 8x16 multiply: the port's battleTrigMul) at
$DB, $DB+C0, $DB+80, $DB+40. P1 = the attacker + (s0, s1), window [$DB, $DB+40]; P2 = the attacker ($9608 = 0) or the
target + (s2, s3), window [$DB+C0, $DB] or [$DB+80, $DB+C0]. A window whose low end is >= C0 wraps ($8C / $8D). C1/27D9
FF-fills $99C0 / $A62D (12); the candidates ($9604 != 0: 0-2, else 3-A) need `$96F5`, `$9FF7` bit 7 clear, `$A0A8 = 0`,
`$A09B = 0`, not the target, and C1/0222 from P1 and from P2 inside the windows -> `$99C1 + Y`. C1/27AD puts the target in
`$99C0` when it is of that side; C1/27C5 shifts the list down when `$99C0` is FF.

**The tail (C1/C659-C72B):** `$3A != 4` and `$99C0 / $99C1` both FF -> C1/C72B (a PC attacker first posts message
7B / 7C / 7D, C1/C66F-C6A3); `$3A = 4` writes $B2AE over the first FF and `INC $AD8D`; `$3A != 5` squeezes out the absent
(`$AEFF` FF) entries, C1/C6E2-C70F (the loop does not end on two absent ids at [9] / [10]: the port stops it with a
message, no record reaches it); `$AD8D` = the index of the first FF, none -> C1/C72B.

Port: battle.c `battleEnemyAreaC586` / `battleAreaBand25A3`, called from `battleEnemyVictimsC1DD`. Only the band is
ported (every enemy tech's area is 08); another shape traces `enemyfx ... this shape is NOT PORTED` and keeps the list.

MEASURED (tools/battle_ab.py enemies, the rule's cartridge, GATO's arena, party Crono / Marle / Lucca): 09 (F1), BA (30),
9E (30), 76 (46 / 30) IDENTICAL over their first actions (were: the cartridge's band hit Crono where the port hit Lucca,
f1105 / f1281); 07 (F1) now first differs at change #15 by one frame ($AE93 at c1261 / p1262, the next action's timing),
was #6. D9 (14) is not the area's: its list is itself on both sides, and the cartridge never applies the 159 it computes --
at the span end (c530) action 76 runs at once, a self-destruct (`$B2CC` 23, C1/F10D -> C1:F131 `$6130 = 0`): the reactive
path, open. F6 acts in the port and not in the cartridge (open, section 22's group).

## 25. 2026-10-08 -- THE ENEMY CENSUS 222 -> 245 of 255: the ROM mechanisms the port had missing, reordered or shortened

`python tools/battle_ab.py enemies` (every id 00-FE in GATO's arena, the rule's oracle, 1500 frames). The 2026-10-03
count in sec. 24 was 184; the tree at the start of this pass read **222**. Each fix below names the enemy that exposed it,
the cartridge's own bytes (`--monitor --writer`, `--cputrace` from the named PC) and the port's change. ⚠ The census
writes a thumbnail per frame per side: ~130 MB per enemy, 33 GB for a full run -- run it in chunks and delete
`build/_battle_ab` between them (the disk filled once this session).

| enemy | what the cartridge does | port fix (src/battle.c) |
|---|---|---|
| 03 | class 0 (C1/3772, mode 04) tests the arrival latch `$9829` FIRST (C1/3774 `BEQ RTS`) and only then `STZ $A5FD,X` (C1/3779): an enemy that never arrived keeps its class-6 orbit capture -- after its attack mode 0A resumes the orbit at `$A615 = 90` | `battleMoveReacquire`: the store moved after the test |
| 3C, B7 | `$AE6D` (the C1/C8F7 redirect) is FD/B223's identity, called at C1/FDA5 AFTER FD/A982's clear of `$AD89..$B3F4` (`$AE6D = 00..0A` f275 by FD:B228). The stored target `$B16E` = 03 (an empty slot) redirects to 03 -> C1/8DBC: an absent ENEMY victim -> `$5E0D = 04`, `$B242 = FF`, no action, ever (the header C1/ACF2 is written first, C1/8DA4) | `battleRedirectInit` moved into `battleCombatStart` after the `$AE5D` loop (it ran before the clear: all zeros, every absent seat -> Crono); the tgt-gone arm writes the header, `$AECC`, `$B242`. Also FD/AEC4 (C1/FDA9): accessory AB -> +23 bit 2 |
| F6 | the turn scanner's C1/84C0 `LDA $B247,X / BNE ->84EC`: `$B247 + cmb` = `$B24A + slot` = the `$AF24` the last walk left (2 = no section held, C1:98C3 -> B025): the enemy keeps its 00 ATB and never takes the turn | the gate added after `STZ $9826` (C1/84BD) |
| 9A, DF | C1/350F (the battle's entrance facing, f291): per PC seat 2..0, C1/2986 sub-mode 2 (C1/2A43, the nearest ENEMY, ties to the later seat) and the facing toward `$9873`. It leaves `$9873 = 06`; resolver 17 (C1/A709) of an enemy alone on the field compares that against itself -> empty list -> cond 0B fails -> section 1 | `battlePcScanFace` (the scan + C1/0222 + C0/F700 toward `$9873`) for the entrance pass and the re-aim latch C1/2F54; `battlePcAimFacing` kept only for a seed without a staged host |
| 9F, 28 | C1/D6AE-D6BC inside C1/D523: a command that is not a tech (`$AE92` != 2) does `STZ $B18F` and `$AE93 = $AEEB - 1` (Crono's record 3A +5 = 01 -> 00, Marle's 3B -> 01; writer C1:D6BF) -- the confused Marle's attack at f670 | in `battleStageAtkHeader`; `$AE92 = 1` (C1/BFDA) set before C1/D523 on both physical paths |
| 3B, C7, 15, 19, 30, 52, 7C, 8C | C1/4093-4099 `$A0B3 = $AE94 & 40` (the second frame of C1/4058, before CC/F102) selects the script header's WORD 2 (C1/4566, C1/458F; cleared C1/474E); and C1/4844 gates the third staging upload: it runs only for `$AE93 = 37` or `$AE94 = 0` (tech 02 has `$AE94 = 40`: pre-roll 21, not 29) | `battleOpen409C` at every C1/409C site; the dispatch-time parse reads `$AE94 & 40`; `battleActStage3` returns FF when the gate closes; the item command's `STZ $AE94` (C1/BB23) |

### 25.1 Stale entries in sec. 23.7 / 24

* The REACTIVE path is ported (`battleCounterTurn`, C1/8686-87FB, a7ca570); `battleAiRunReactive` / `battleAiExecSection`
  / `aiCondEval` are dead code (no caller).
* F6 (sec. 24's last line) is closed above.

### 25.2 OPEN after this pass (10 enemies: 07 16 1B 4C 6E C1 D8 D9 E3 F4)

* **`$B188-$B18A` (C1/8570 `$B188 := $93EE`)** stays unported (the port keeps `pcInTurn`); no census case needs it: enemy 14,
  first blamed on it, was the name box -- op 02's handler posts the tech's name (C1/9B35 JSL CD/0033) and waits inside
  the turn (f972-f983); C1/8DA4's header and C1/8DCF's absent-enemy arm come after it (fixed: enGoneHold).
* **The defeat's hand-back to the field one frame late** (07, 6E, D9, E3, F4): the defeat sequence matches through f800;
  the field's own writes (C0/A689 `$AE93 = 80` f918, C0/A730 f920) land one frame later in the port -- the battle return
  ordering of ct-battle-return-order (FASE B), not the AI.
* Span lengths +-1..5 frames (16 1B 4C 81 90 D8 C1): 16 1B 4C D8 CLOSED 2026-10-09 (sec. 25.3); C1 open; 03 was one of them (the mover) and 15 19 30 52 7C 8C
  closed with the pre-roll gate, so start with the enemy's position (`CT_BATTLE_POSTRACE`) and its pre-roll.
* CLOSED 11 / 79: C1/40FB-4114 at every action's close re-bases each ENEMY role's mover ($98AF/$98B7 + slot, up to
  the role list's first negative) on its live foot before the CC/F06B wipe. An orbit segment in flight when the action
  began goes on from where the script left the enemy (79: `$98B2 / $98BA = 8E / 9F` f1205, C1:410D/4113); the port
  resumed it from the old base, jumped the enemy back beside a corpse that deflected every approach step (79's third
  action never came) or lengthened the next approach (11, +22 frames). 81 and 90 closed with it.
* CLOSED 83 (exposed by the rebase, then fixed): class 4's C1/3AAB `LDA $1D26,X / SBC $82` and class 5's C1/3C2E have no
  SEC -- the Y flag $83 inherits the X compare's borrow, so with self X < ref X it is FF already at self Y == ref Y. The port
  compared Y alone. Cartridge: self (183,147), ref (205,147), F8 -> `$9786 = 78` f794 (C1:3B02) and a clear step; the port
  kept F8, deflected off the arena edge three times. Before the rebase the port's enemy stood elsewhere and the bug hid.
* CLOSED 5F / 80: the iteration's end tests (C1/8133 defeat, C1/8167 victory) run right after the sweep C1/B223, BEFORE
  C1/8195's flags and the timer pass with the turn scanner; the port tested them after the scanner, so op 0A's self-kill
  (C1/9CB3) won the fight in the same pass and wiped the PCs' Lock at f1412. Graded too: enemy 0C killed by Crono, the
  victory f790 with Crono's ATB still 48 (reloaded f788) on both sides -- no timer pass in the victory's iteration.

### 25.3 2026-10-09 -- three of the span-length enemies: the walker, the think, the staging lift (249 / 255)

Each "span +-N" was a walk that started somewhere else; the census's monitored bytes only showed its end. Found by the
ASC cursors (`--monitor 7E5DBD:64` against CT_BATTLE_ASCIP) and the foot (`$1D0C + cmb` against CT_BATTLE_POSTRACE).

| enemy | the cartridge | port fix (src/battle.c) |
|---|---|---|
| 16, 4C | CF/F97D-F982 `LDX $C6 / LDA $98A7,X / BEQ next`: the walk stepper reads the SEGMENT's flag, never the mode. 16's no-action turn (op 00) at f677 starts a segment toward (177,90) and commits mode 00 (`$974A` f680, C1:336A); CF:F9C5/F9CE step it on to its end. Its attack's `A8 FF` (CD/7CDA) then walks up from (177,90): 21 frames | `battleMoveTick` skipped mode-00 slots: the enemy stood at (181,83), the walk took 19 frames and the span ended a frame late. Only the empty-slot bit (mode bit 7) still skips |
| 1B | the think at f684 and one more step at f685 (123 -> 124, CF:F9CE): the walkers stop when C1/4058 counts `$A0FD` up (CF/EFAF-EFBA), not at the think | the stepper held the pending enemy from its think (`mvPendAtk`); its slides `12 03` / `12 19` (CD/86B0, 86B9) ran 2 frames long each. The HANDLER pass (C1/3714) keeps its measured hold |
| D8 | CC/EC0B-EC1E after the staging: `BNE ->EC21` (the id's high byte), `CMP #D8 / BEQ / CMP #94 / BEQ / CMP #AD / BNE`, `INC $1D26,X` -- D8, 94 and AD stand one pixel lower than their record (`$1D29 = 70` by CC:EC21 for a record foot of 111) | added after the `$9853` store; D8's `43 00 F6 C0` point and its effect object's `A8 FF` (CD/E55B) met the arena's top row a frame early |

Full census after the three: **249 / 255** (245 before), no enemy regressed. Left: the defeat's hand-back one frame late (07 6E D9 E3
F4, ct-battle-return-order) and C1 (its fourth action, a PC physical CE/03B6 at f1138, ends 5 frames early; 26 vs 23 changes).
Check battle-enemy-census-movers-vs-oracle (three mutations killed).

**C1, traced -- CLOSED 2026-10-09 (below).** All three PCs are confused at f384 (+1E bit 2). The fourth action (f1138) is Crono's forced physical
(FD/AB40 CHAOS: a draw in [0, 11)): the cartridge draws 06 (the enemy; `$AD8E` by C1:D68E, `$A014 = 00 01 02 06`) and walks
up to it (`18 03` CE/D4C6, 9 frames); the port draws 2 (Lucca, adjacent: the walk ends in 1 frame) -- the span 5 frames
short and the effects differ. The cause is the RNG cursor `$26`: cartridge 23 (f750) -> 25 (f891) -> 27 (f915) -> 30
(f1136); port 23 -> 27 (f915) -> 29 (f1060) -> 31 (f1136). The cartridge's C1/881C eval runs at the close of Marle's
forced attack on the enemy (f891, `$B1DA` by C1:8C9D, two draws); the port's tail resolves there with no draw
(`aitarget f671 val=00`, then `counter-nofire`) and draws its two at the close of Lucca's forced attack (f1060), where the
cartridge evaluates nothing. Next: which condition of the enemy's script the two evals walk differently (C1/8C09 under
`--cputrace` from a PEEK_SAVE at f880).

**The defeat's hand-back one frame late (07 6E D9 E3 F4), traced (open).** Enemy 07 on the rule's cartridge: everything
through CD/0516 matches (`$CE16 = 1` f1231, `$CA25 = 08` CD:0412 f1246, `$45 = 0F` CD:0428 f1247); the exit then polls
the SPC, CD/0428 `JSR CD/044A` / CD/0432 `LDA $002141` / `BNE ->0428`, and calls C0/1BE6 (CD/043E) on the read that gives
0. `--cputrace` (CPUTRACE_BEAM) of a state saved at f1240: the four reads at line 225 h 12-22 give 02 01 01 00 and
CD:043E runs on the fourth (C0/1BE6's `11 00 FF` f1251, the ack f1261). The port (CT_BATTLE_TRACE's `exitpoll`, battle.c
CtBattleExitPoll) reads 02 02 01 01 00 from its own SPC: one read more, C0/1BE6 f1252, the ack f1262. The fade that
counts the port down is CD/039E's `81 10 00`, posted on f1216 at line 228 (CD:03AB `JSL C7/0004`) on the cartridge and
on the port's pad-latch position (LD_PAD_WAIT, line 228) -- the same. The fade steps every two frames and B6's defeat
(the same poll, 02 02 01 01 00 on the cartridge) is identical on the port, so the port's SPC is behind by a fraction of
a frame at the read, not a frame. Next: where in the host's SPC slice (field.c, the rule's CtAudioFrame per pass) the
poll's read stands against the cartridge's line 225 h ~20 after the CD/044A wait.

**C1 CLOSED: the eval after a counter that takes no action.** `--cputrace` of the cartridge at f891 (a state saved at
f885): Marle's confused attack on the enemy closes (C1:41BD); C1/C011 had set `$B2C0 = 1` (the first victim an enemy:
FD/AB30's physical runs C1/BFAA's own arm), so C1/864B -> C1/8671, the counter's turn C1/8CF9 (no draw: block 2 holds no
action here), then C1/87FE-8814 (the list's end) -> C1/881C `JSR C1/AC5E / STZ $B2C0 / LDA $24 / BNE / JSR C1/8C09`: the
eval and its two draws (C1/AF22 with A = 64, 63), and C1/8839 `STZ $B2C0` -- all on f891. The port ran the counter's turn
(battleCounterDecode's `counter-nofire`) and nothing after it; its eval came at the next action's close (f1060), the RNG
cursor a draw along from there, and the confused Crono's target (FD/AB40, a draw in [0, 11)) came out Lucca instead of
the enemy. battleReactiveCounterTick now runs C1/881C's eval when the last pending counter took no action (a counter
that acts gets it at its close, battleSpanEndArm). `$26` now matches the cartridge change by change to f1482; C1 identical.

**The defeat's poll, followed into the SPC (2026-10-09, open -- two coupled errors).** ARAM on both sides at chosen frames
(the oracle `--peek` with PEEK_ARAM, the port `CT_AUDIO_PROBE=<f>` + `CT_AUDIO_PROBE_ARAM`; battle_ab's gato_wait with enemy
07; D100+ is the echo buffer, 01F3-01FF the SPC's stack): the driver's zero page is identical through f219 (the field
song's tempo accumulator `$48/49` included), so the seed is exact. The fight's song upload then differs:
1. **The post's frame.** The cartridge reaches C0/1BAB two frames after the consumer: C0/177D f219 (its `$38` loop and a
   wait), C0/179A f220 (the transition dispatch), C0/188C f221 (C0/024C, `JSL C0/2C41`, C0/00DE, a wait), C0/18A7 `JSL
   C1:0000` -> C0/1BCE's `JSL C7:0004` f222 line 228 (`--cputrace`), back f275 line 240. The port posts on the entry's
   frame, f220 (field.c, the `entered` block) -- battleBootBandArm's `2 + apu + 3 + 1` already counts the two frames.
   Through f222-f267 the cartridge's SPC transfers (`$97/98` climbing, `$83` 00 -> 2B) while the port's stands still
   (its upload ran inside the post's service); both reach the loaded state (`5B 70 24 20 01 FF`) at f276 only when the
   post is moved to f222 -- with the port's f220 post it is f274 and the song starts f278 against f280.
2. **The upload's own price.** With the post on f222 (tried: a `bootPostPending` until the module's f2) the song starts
   on f280 on both sides but the port's tempo accumulator runs ~0.97 of a frame's increment behind (`$48/49` 2BAC
   against AC58 at f280; the ROM's 0x8304 a frame), and the JSL returns on line 252 against the cartridge's 240. With
   the post on f220 the two errors partly cancel (the port ~1 frame ahead): the census's defeats are 5 one frame late
   and B6 right; with only the post fixed 16 defeats differ (239/255) -- reverted. The CD/0432 poll reads the fade's
   count down at the driver's tick, so its frame follows the song's phase: both halves have to be fixed together, the
   second in the port's model of the C7 transfer (audio.c ldRun/ldCatch against the real C7/0004 protocol's cycles).

**The transfer model, block by block (2026-10-09).** Correcting the earlier estimate (its frame arithmetic used the
`--cputrace` frame markers, which do not sit at line 225): with the beam accumulated instruction by instruction from the
post (C0:1BCE f222 line 228), the cartridge opens the boot's blocks at 2.060 (C7:068C, `$50AB`), 9.492, 12.608, 16.718,
21.917, 27.395, 29.759, 38.302, 41.310 f, `$1E80` 43.254, `$1F40` 43.374, `$1FC0` 43.443, the `$2F00` swap C7:093A 43.516,
`$2000` C7:04E9 50.994, `$F120` C7:05CA 52.987, GO C7:0606 53.036, the RTL 53.047. The port's model (CT_AUDIO_LOADTRACE,
the post moved to f222) opens them at 2.06, 9.51, 12.63, 16.75, 21.96, 27.43, 29.80, 38.36, 41.37, 43.29, 43.42, 43.48 ..
51.04, 53.04, GO 53.10 -- the same to +0.00..+0.06 f, a slow drift (~16 lines by the GO), no single wrong term. The SPC's
own time is exact on the port (CT_AUDIO_SPCCLOCK: the total at f280 = 353,123 + 280 x 17,088 to 9 cycles; the JSL ran
907,711 cycles = its price). And the transfer's progress per frame is the cartridge's (CT_AUDIO_LDFRAMES: `$83` / `$97/98`
at each vblank the clock crosses = the cartridge's ARAM on the same frame after the post). So what moves the song's phase
by a driver tick is the GO's ~16 lines: the driver starts the song on its timer, and a GO that late misses the tick the
cartridge's catches. Matching it means the C7 transfer's CPU side cycle for cycle (its loops at FastROM/SlowROM, the
DRAM refresh, the held IRQ at line 211 and NMI waits) -- the port's cost model is within 0.1% and that is not enough here.

**The drift was the port's own listing, found segment by segment (2026-10-09).** CT_AUDIO_LOADTRACE now prints a
`[loadmark]` at every block head (master cycles since C7 began, the CPU's share, the taken polls, the handshakes); the
cartridge's side is its `--cputrace CPUTRACE_BEAM=1` at the same head PCs, each instruction priced at its own modal cost
and each branch by whether it was taken. Fixed cost = CPU - 42 x taken polls. Every segment's error had a name:
- the SAMPLE loop C7/06CD/06F9 ends `DEX DEX DEX / BNE` -- an unsigned count to zero, exactly size/3 units (every
  instrument is BRR, 9-byte blocks: 62/62 sizes a multiple of 3). The port pushed size/3 + 1 (the SIGNED count of the
  sequence's loop C7/054C/0578, `BPL`): one extra handshake per sample, 3 bytes past its end;
- the `$F120` block C7/05E0 is `LDX #000C / ... / DEX / BNE`: twelve units, not 13;
- the sequence (C7/04DB), `$F120` (059A) and the three cmd-02 blocks (03C8 / 040D / 045B) open with their OWN heads
  (literal `LDA #hi / STA $2143 / LDA #lo / STA $2142 / LDA #cmd / STA $2141`), not the sample head's C7/0656 bookkeeping
  (448, the port charged 480) and its 06A2 size word (486, charged 474);
- the directory C7/037B-03C6 (`ldRun(2200)`) is a search of 32 words per slot (the ids AND the lengths after them),
  ended by the first empty slot -- a missing id writes two zeros and goes on (the port broke out); the two parameter
  walks C7/0431/047F have their own loop (354 + the handshake), not the directory's; C7/04A9 compares LOW bytes and
  leaves `$FC` = $FFFE when nothing matches; C7/0326's scan is `LDA $20,X` with M=8 (26, not 34); C7/0344 is abs,Y with
  X=16 (40, not 34); the append loop charged C7/071C's RTS twice; C7/0367's clear loop has its `LDA #0000` (146);
- C7/09EA, the resync after a missed echo, is `LDA $84 / STA $2140` (the CURRENT token) / wait / `LDA #E0 / STA $84`.
With the listing exact the fixed cost of every segment is the cartridge's to within the taken poll an IRQ lands on
(~42 a frame, an artefact of the count), and the GO moves from +23k to +15k cycles; with the boot's post at f222
(C0/1BCE on C1/0000's frame) the driver's zero page ($44-$4B, $00-$03) is the cartridge's at f276, 280, 284, 290, 300,
350, 420 -- the song's tick caught -- and enemy 07's defeat hand-back is the cartridge's (IDENTICAL). (MEASURED with
the post moved; the move is NOT in the engine yet: posting at the module's entered frame + 2 puts the attract's 087 fight
one frame late -- the chain C0/177D .. C0/18A7 -> C1/0000 is two frames in GATO's fight and has to be measured, not
assumed, per entry.) The listing pricing alone is in (the suite re-pinned: the scene-8 gate's two uploads return on
line 71 / 222, the cartridge's 69 / 221, against the old 75 / 223). What is left of the
GO is the poll quantization (+-50 polls a segment, both signs): the CPU's 42-cycle poll against the SPC's loop, with the
port's HDMA a per-line mean.

**The exit's chain: the HDMA the tail's NMI leaves (2026-10-09).** 6E / D9 / E3 / F4 still hand back a frame late. Traced
(enemy 6E, PEEK_SAVE f898 + --cputrace): C0/1BE6's first arm posts five JSLs -- `81 00 00 FF` (225 -> 239), `11 $29AE`
(the field's song, with the `$2F00` swap back: 239 -> +7 NMIs -> 201), `81 40 FF FF` (C7/061C's post: ten $400-poll
time-outs, 201 -> +2 NMIs -> 216), `82`, `83` -> RTL line 221, four lines before the vblank. The port's song JSL is
+13.7k cycles (10 lines), so its chain ends on line 231 -- past the vblank. The segment table puts all of it in the
BACKGROUND: 182k cycles on the cartridge, 221k on the port, which prices the battle's 8-channel HDMA (17,904 a frame).
The cartridge's: `--hookpc CT_HOOK_CYCLES` hdma = 0 on the frames before the chain and its first, then ~14.1-14.3k (six
channels, the FIELD's tables 7F:1238..1442). Why: the tail's NMI D1/F4C0 clears HDMA with `STA $00420C` (long) and then,
after `JSL C0/0005` (C0/0AFF: DB = 0 inside, FD/C2C1 x2, FD/C1EE, `LDA $28` = the field's mask $5F, `PLB`), enables it with
`STA $420C` -- ABSOLUTE, through the interrupted code's data bank. While the battle's exit poll runs (DB = $7E) the write
lands in WRAM $7E:420C and HDMA stays off; once the CPU sits in C7 (DB = $00) it is the register: the field's six channels
from line 0 until the V-IRQ D1/FE97 (`TDC / PHA / PLB / STA $420C`: DB = 0, HDMA off) at line 211. So the chain's
background is: no HDMA the frame the chain starts, then the field's six channels on lines 0-210 of every frame whose
vblank the CPU met inside C7. OPEN: the loader clock prices HDMA from a per-engine mean (LD_BG); pricing it from the
channel set the NMI leaves and its tables (LakeSnes dma_doHdma: 8 + 8 a live channel + 8 a byte + the table reloads)
is the general form, and the exit chain needs it.

**The boot's post on C1/0000's frame: measured, NOT in (2026-10-09); IN since 2026-10-22, with the dispatch's price (field_load_cost.md sec. 16).** The patch (post at the consumer's frame + 2,
`bootPostPending` holding the band's arm) makes GATO's driver phase the cartridge's and enemy 07 IDENTICAL, but moves the
attract's 087 fight a frame (attract-087-ice-sword-afterimage exact at port f9586, not f9585; the demo's second title
f11006). The 087 on the rule's cartridge (PEEK_SAVE c7150, --monitor 7E0117,7E0138,7ECA1E + --hookpc): `$0117` = 21
f44, 01 f45 (C0/177D), `$0138` = 00 f53, C0/179A f54, C0/188C f55, C0/1BAB's `70 01` f56 line 228 -> RTL f69 line 251 (13
NMIs), #08 f72, `$CA1E` = FF f73 (the seed: post + apu + 4), 01 f74, 00 f86. The port with the patch: 21 f9284, 01 f9285,
`$0138` 00 f9293, the post f9296 (13 NMIs, line 29) -- the field's side at +9240 throughout -- but `$CA1E` 01 at f9313
and 00 at f9325 (+9239): the arm a frame EARLY against the field's offset while the afterimage is a frame late. OPEN:
which of the boot's module frames the post really lands on (the patch's post runs before the frame's tick), and what
in the 087's old timing the early post compensated. The diff stays out until both are read off the two sides.

**The exit's HDMA, priced from the channels (2026-10-09): 6E / D9 / E3 / F4 IDENTICAL.** Three pieces, all the ROM's:
- the TABLES. The port never ran FD/C2C1 -- at its nine `JSL` sites it only flipped `$0153` bit 0 -- so bank $7F held the
  tables of whatever state was seeded (enemy 6E's arena: 100 / 29 / 2 / 80 lines where the cartridge's tail had built
  100 / 112). FD/C2C1 (FD:C2C1-D52C, both sets, every `$0126` mode handler) is now the ROM's program translated
  (tools/gen/xlat.py over the unique traces of the 6E tail, GATO, the demo and scene 8, filtered to the family's range;
  1681 instructions, a path no trace took runs on the same core) and called where the ROM calls it (ct_vm.c
  CtHdmaTableBuild: C0/00C7, C0/00DE x2, C0/2848's C0/0B28, the menu / shop resumes, the fight's tick 1, and the exit tail's
  C0/0AFF twice with `$53` bit 7). `ct.exe --hdmabuild` against the cartridge's WRAM before/after one call: 11 calls in
  four contexts byte-exact outside the stack page.
- the CHANNELS: FD/C1EE's program read from its own immediates (DMAP 44 43 43 43 44 41 41 40, bank $7F, set A FD/C26D /
  set B FD/C297 by `$0153 & 0F`), enabled by `$0128`.
- the COST: LakeSnes's dma_initHdma / dma_doHdma per line (8 + 8 a live channel + 8 a byte + 16 / 8 a reload), checked
  against the scratch model on the cartridge's WRAM (12,416 a tail frame, 12,664 a scene-8 field frame), + LD_HDMA_SYNC 8
  a run (the two alignments: 8.03 measured on the scene-8 upload's poll frames, 8.33 on the 6E tail's).
And the ENABLE: CtAudioSetRuleHdma(prog 1, off, on at an NMI inside C7, cut at 211) around C0/1BE6's chain. With it the
6E chain's song JSL returns on line 189-201 (the cartridge's 201; the handshake's poll phase moves it by +-10 lines with
the background's last cycles), the chain on line 210-222 (221) -- before the vblank -- and 6E D9 E3 F4 hand back on the
cartridge's frame. Elsewhere the loader clock keeps LD_BG's means (the field's song loads, the battle's boot) -- pricing
those from their channels is the same work (the field: this program, the NMI C0/EA63; the battle: its own program).

**After the exit's HDMA: the census 253/255, and the two errors that hold each other (2026-10-09).** The full census on
b63de1e: 6E D9 E3 F4 IDENTICAL; 07 a frame late (the boot's post, below); B9 now a frame EARLY. B9 is a knife edge: its
cartridge chain (PEEK_SAVE f1010 + --cputrace) RTLs at line 225 h 242 -- 242 cycles past the vblank -- and the port's at
217. JSL by JSL the port is the cartridge's to 1.4k except the song's (C0/1C3A: 2,828.7k on the cartridge, 2,816.9k on the
port), and the segment table of that JSL puts the rest in the CPU/SPC phase: the command's echo wait C7/0A5A-0A79 2.6k
shorter, the `$2F00` swap 102 polls fewer, the background 2k less -- +-0.3% of a 2.8M-cycle handshake, the class a cost
model does not resolve; B9 was IDENTICAL before only on the battle mean's wrong background.
THE PREAMBLE AND THE DISPATCH, priced from the listing (disasm/notes/patches/c7_dispatch_preamble_listing.diff, NOT in): the
dispatch C7/0004-0173 by the command's path (the port's 686 is the song path's; a post >= $80 is 480, $18-$2F 552 -- the
cartridge's #08 592 = 552 + one refresh), the epilogue 304 (charged 296), C7/0734's scan and C7/0A39's record (charged
250 / 276), C7/09FD + C7/0A12's cull as loops (charged 60 + 240), the 0A83-01B3 return (124 missing). Every segment of
B9's preamble then reads the cartridge's to 0-80 cycles. But with it the 07D's boot `70 01` crosses 11 NMIs -- the
cartridge's (c4428: C7/0004 f28 line 228 -> RTL f40 line 103) -- where the committed price crosses 12, and the demo after
the 07D moves a frame EARLY against the cartridge (the Frog Squash's colours / eyes / BG3, the 087's legs and afterimage,
1F0, 1B0, entry3 -- 23 checks; the 07D's entry -> ack 658 on the cartridge, 660 -> 659 on the port; GATO's song spotlight
max 5969 -> 632 px; GATO's entry 2 -> 3 frames). The 12 was compensating the boot's other one-frame error, the post on
C1/0000's frame (disasm/notes/patches/battle_boot_post_c1_0000.diff, the measurements above): the exact price and the post
have to go in TOGETHER, with the arm's count (`2 + apu + 3 + 1`) re-read off the cartridge for the demo's three fights and
GATO's.
