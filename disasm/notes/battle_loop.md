# BATTLE LOOP — the minimal canonical GATO-fight spine (ATB → command → attack → AI → death → victory)

Derives the MODULE-internal battle spine that `battle_menu_synthesis.md` / `battle_boot.md` left OPEN
(ATB tick, command-open, damage pipeline, enemy AI, death→end). Every claim is anchored to a ROM
disassembly address (`tools/dis.py <fileoffset>`, fileoffset = (bank−0xC0)<<16 | addr) or an oracle
monitor/peek observation from `gato_press_action.state` (pre-battle; A-press boots battle ≈ f222).
Writer PCs from `--monitor --writer` are the PC *after* the store retires.

> ⚠ **METHOD GOTCHA (new, important):** `--peek` RENDERS each frame; `--monitor` does NOT. The battle
> RNG / turn cadence depends on frame-parity work done in the render/IRQ path, so **`--peek` and
> `--monitor` produce DIFFERENT battle timelines from the identical input** (e.g. the countdown timer
> reads 0x011F/0x1B00 under peek but 0x1E under monitor). All *tracing* below (writer PCs, value
> sequences, HP) uses **`--monitor` exclusively** and is self-consistent. Sparse `--peek` WRAM diffs
> were only used for structure discovery and are cross-checked against monitor before any claim.
> Battle code runs 16-bit A/X (D=0 proven, `battle_menu_synthesis.md §1.8`); linear dis.py desyncs on
> M/X across unfollowed branches in this bank — cited instruction bytes were re-read where load-bearing.

Repro base for everything: `ct_probe.exe --monitor "<regs>" --from gato_press_action.state --input "<mash>" --diff --writer`
where mash = A-pulses in the LakeSnes grammar (A=0x100), e.g. `python -c "print(','.join(['100:20,0:20']*N))"`.

---

## S1 — ATB (turn readiness) : per-actor COUNTDOWN timer `$AF27,X`, −1 per logic-tick

**CT does NOT use a smooth per-frame gauge accumulator.** It is a discrete-event turn scheduler. Findings:

- **Logic cadence** — the whole battle tick `C1/10E3` (reached via `JSL C1/0012`) is gated `LDA $993A / BEQ`
  and `STZ $993A` at entry. `$993A` (byte `$993B`) is incremented by the render/HUD side `CF/E748` (~1/real-frame)
  and consumed by the tick (`C1:10A9/10CE/1102`). Post-intro it stays 0–3 (tick keeps up ⇒ ~1 logic-tick/frame);
  during the intro it accumulated to 0x14 (tick paused). So a "logic-tick" ≈ one real frame once combat is live.
- **Intro delay** — `$993B` (a separate byte, monitored as `$993A`+1 hi) ramps **1→0x14 (20)** over f301→f377
  (+1 per ~4 frames), a fixed pre-combat delay. At **f378** combat begins.
- **The countdown** — `$AF27,X` is a per-actor down-counter array. Routine `C1/8246+`:
  `LDA $AFB6,X` (per-actor active flag) then `LDA $AF27,X / DEC A / STA $AF27,X` at **C1/8267-8268**
  decrements the acting-order timer by 1 each pass. The slot watched as `$AFAB` (= `$AF27`+0x84) shows,
  under `--monitor "AFAB:2" --writer`:
  - f290 init `0x011F` (287, writer `FD:B935`); f374 turn order `$B163` = `05 0A 04 00 09 06 07 08` (writer `C1:808B`,
    a speed-sorted permutation — `battle_boot.md` f374);
  - **f378 reset to `0x1E` (30)** (writer `C1:826B`); then **−1 each logic-tick** (writer `C1:826B` every step)
    → **0 at f454** → the next actor (Marle) becomes current at f459 (`$95D5`=1, writer `C1:1B37`) and is
    enqueued (queue tail `$A6DE` 0001→0002).
  - `$99DD/$99DE/$99DF` are a **mirror** of `$AFAB/$AFAC/$AFAD` — copied every pass at `C1/8290-829F`
    (`LDA $AFAB/STA $99DD; LDA $AFAC/STA $99DE; ...`). (Do not treat `$99DD` as the source.)
- **Actor-of-turn / round-robin** — `$95D5` = CURRENT combatant index (byte; 0=Crono, 1=Marle, 0xFF/0xFFFF=none/AI);
  `C1/1153` gates on it (`LDA $95D5 / BPL` — negative ⇒ no current actor ⇒ `JMP C1/179C`). `$A6E0` is a mod-3
  slot cursor cycling 0,1,2 each tick (inc `C1:05BF`, wrap `C1:05C9`). PC ready-**queue** = `$A6D9,X`
  (head `$A6DD`, tail `$A6DE`, sentinel `$A6DF`; enqueue path `C1/1C3A`, `C1/1BA0/1BA9`).
- **First actor acts immediately** (pre-charged): Crono is current from f381 (his timer already at/near 0 at
  combat start); Marle at f459 (timer 30→0); enemy GATO first acts ≈ f635 (idle) on his own timer.

**Reload rate / speed-scaling: PARTIALLY OPEN.** The −1/tick decrement and the "act at 0" trigger are byte-pinned.
The reload VALUE is speed-derived (initial stagger 30; post-action recharge is much larger — Crono re-attacked
~450 frames after acting) but the exact Speed→reload equation was not fully traced (the reload writer path is
in the same branch-heavy `C1/824x` block that dis.py can't linearly decode). For the minimal loop a fixed or
speed-scaled reload with −1/logic-tick decrement reproduces the observed turn cadence.

Logs: `SP/gauge2.txt` (clean `$AFAB`→`$99DD` −1/tick, C1:826B), `SP/afab.txt` (init/reset/reload writers),
`SP/queue.txt` (`$A6D9` queue + `$95D5`), `SP/statemachine.txt` (intro-timer + first-turn timeline).

## S2 — Command open

When an actor's `$AF27,X` reaches 0 it becomes the **current actor** `$95D5`. For a **PC**, the command window
opens and waits for input; the state during "menu open, awaiting input" (observed f381→635 idle, Crono active) is:

    $95D5 = PC index (0/1)   AND   $95DB = 0   AND   $9609 = 0

(`$95DB`/`$9609` are the action sub-mode selectors read at `C1/1153`: nonzero ⇒ an action/animation is executing,
so 0/0 = idle-awaiting-input.) The **hand cursor** marks the active PC's HUD box (it is font tiles 0x160-0x163
at box0 of the active PC — `battle_hud_render.md §Q5`; box0 = the current-actor's window). The Att/Tech/Item cursor
row is `$95DC` (`battle_hud_render.md §2`). Frame relation to ATB fill: menu opens the tick after the timer hits 0
(Marle: timer 0 at f454 → current at f459, ≈5 frames of enqueue/turn-setup latency).

## S3 — Attack execution & the damage pipeline (byte-pinned; base ATK/DEF coefficients = boundary)

**Confirmed damage lands** (`--monitor "6130"` = GATO **curHP** at block `$612D+3`; the prior dossiers mistakenly
watched `$6132` = **maxHP**, constant 76 — that is why "GATO never took damage" was an open question):

| frame | event | value | writer |
|---|---|---|---|
| f451 | Crono attack computed | `$AD89`=0x11 (17) | `C1:DCED` |
| f579 | applied to GATO curHP `$6130` 76→59 | −17 | `C1:ED1A` |
| f884 | Crono **critical** computed | `$AD89`=0x28 (40) | `C1:EDB0` |
| f1030| applied to GATO 59→19 | −40 | `C1:ED1A` |
| f580 | GATO counter computed (target Crono) | `$AD89`=0x0E (14) | `C1:DCED` |
| f805 | applied to Crono `$5E30` 70→56 | −14 | `C1:ED1A` |

**Pipeline (attack → HP):**
1. **Compute** — the physical-damage routine writes the final damage to **`$AD89`** at **C1/DCED** (normal) or
   **C1/EDB0** (critical ≈ 2× via an extra `C1/C90B` multiply, `C1/EDA8+`). Same routine serves PC *and* enemy
   physicals (GATO→Crono damage also `C1:DCED`). Target index in `$B1F6` (=3 GATO / 0 Crono; writer `C1:E9D4`).
2. **Variance + RNG** (`C1/DCB9-DCEA`): `dmg = C90B/C92A( base × (256 − rand[0..99]) / 256 ) + (bonus $B2BE & 0xFF)`.
   `C1/C90B`=16-bit multiply ($28×$2A→$2C), `C1/C92A`=divide (/$2A). **RNG = `C1/AF22`** (via `JSL C1/FDCB`;
   `LDA #$64 / JSR C1/AF22` = random 0..99). The pre-variance **base `$16`** (the ATK-vs-DEF core) is computed
   upstream.
3. **Stage** (`C1/EC00-EC38`): `LDA $AD89 / STA $B328,Y` at **C1/EC29** (Y = target*4 + `$2C`, target index from
   `JSR C1/C90B`); the hit-type flag `$B203 → $B32B,Y` (`C1/EC32`). `$B328/$B354/$B380` = **hit-1/2/3** damage
   slots; a single physical uses only `$B328,Y`.
4. **Apply** (`C1/EC3C`+, per target Y / block-offset X): `curHP $5E30,X −= $B328,Y` (`C1/EC99`); optional
   `−= $B354,Y` and `−= $B380,Y` each gated by `BIT #$4000` on `$B356,Y` / `$B382,Y`.
5. **Clamp + death** (`C1/ECC7-ED6B`): `LDA $5E30,X`; if ≤0 → death path (`C1/ED04` `LDA $5E4A,X / ORA #$80 /
   STA` sets the **dead flag block+0x1D bit7**; enemy → `JSL FD/ABA2`; `C1/ED0F` writes curHP=0); else clamp to
   maxHP `$5E32,X` and `STA $5E30,X` at `C1/ED17` (writer `C1:ED1A`).

**Combatant block layout** (0x80 stride; PC `$5E2D + seat*0x80`, enemy `$5FAD + slot*0x80`, GATO slot 3 = `$612D`):
`+0 id · +3 curHP u16 · +5 maxHP u16 · +7 curMP · +9 maxMP · +0x1D dead-flag(bit7) · +0x37.. stat bytes`.
GATO `$612D`: id 92, curHP/maxHP 76, `+0x37` stats `01 08 08 64 06 32 07 7F 04 04 04 04` (= ROM `CC/541E`).

**BOUNDARY (base ATK/DEF equation):** the pre-variance base `$16` = attacker-Attack vs target-Defense core is
computed in the same `C1/DBxx-DCxx` block whose branch density defeats linear dis.py (repeated M/X desync). The
FULL wrapper (variance formula, RNG, crit ≈2×, bonus, staging, apply, clamp, death) IS byte-pinned above, and
the **oracle-observed GATO-fight damage** is documented (Crono→GATO 16–17 normal / 40 crit; GATO→Crono 9–16).
A minimal GATO loop can reproduce the fight from these observed numbers; the exact base coefficients are marked
open rather than invented (mandate). Logs: `SP/dmg_samples.txt`, `SP/ad89_writer.txt`, `SP/mashA_curhp.txt`.

## S4 — Enemy action (GATO AI)

**Observed behaviour (multiple full/partial fights):** on his own ATB timer (first action ≈ f635 idle; then
~every 200–450 frames), **GATO does a physical attack on the LEAD PC (Crono) only** — Marle's HP `$5EB0` never
dropped across every run; no status/heal/other effect was ever written. His attack uses the same damage routine
(`C1:DCED`, target `$B1F6`=0=Crono), damage 9–16 (variance). So for the mash-A GATO fight the enemy AI reduces to:
**"on ready, physical-attack the lead PC via the shared formula."**

**Full AI-script interpreter (bank CC): DOCUMENTED BOUNDARY.** The region I reached (`CC/F06B+`) is a
memory clear (`STZ $5D80,X` etc.), not the interpreter; the per-enemy AI-script
pointer table + bytecode is a large separate subsystem not required by — and never exercised beyond a physical
attack in — the observed GATO fight. GATO's canonical "song"/taunt special did not trigger in any capture and is
dropped from the minimal loop as provably-inert for this fight (state clearly if ever implementing the full AI).
Logs: `SP/death.txt`, `SP/victory.txt` (Marle HP flat; only Crono hit).

> **CORRECTION.** `CC/F06B` is **not** "battle-init" anything: it is the PER-ACTION teardown. It has
> exactly two call sites in the whole 4 MB image (`22 6B F0 CC` at `C1/405B` and `C1/4119`), i.e. the
> action machine calls it at an action's OPEN *and* again at its CLOSE, and it wipes `$5D80..$5E0C`
> (all script state), `$A240..$A27F` (effect objects), `$A124..$A1A7` (the pose engine), FF-fills the
> roles `$A014` and clears `$9876/$A21F/$A012/$A3D1`. Measured firing at the Cyclone's own span end
> (f751 on `gato_cyclone`). Calling it an init clear is what let two actions' state bleed together --
> see [battle_action_state_leak.md](battle_action_state_leak.md).

## S5 — Death → victory → `$7F01EC`

Captured end-to-end from a checkpoint `SP/gato_lowhp.state` (GATO 19 HP), mashing A:

- **f950 killing blow**: GATO curHP `$6130` → 0 (`C1:ED12`); **dead flag `$614A` = 0x80** (`C1:ED0C`, the
  `ORA #$80` store — block `$612D`+0x1D).
- **f951 alive-list clear**: `$AF02[slot3]` / `$AF05` `92 → FF` (writer **`C1:B2C3`**), i.e. GATO removed from the
  enemy alive-list (the enemy-death handler reached via `JSL FD/ABA2` from the clamp death path). `$AF02` now
  all-FF.
- **Victory scan `C1/817A`** (`battle_menu_synthesis.md §1.8`, exhaustive-scan): scans `$AF02,X`==FF (skip
  `$AF15,X` bit6 hidden); all-FF ⇒ `TDC / STA $7F01EC` = **victory = 0** (D=0 proven ⇒ TDC stores 0). `$7F01EC`
  stayed **0** throughout (victory), and control returned to the field ≈ f1156 (first bank-C0 write `C0:A730`).
- The full mash-A win end-to-end is corroborated by `saves/battle_re/mon_full.txt`: battle completes f3070
  (op-D8 ack `C0:18C1`), field victory script (op-0x58/0x5B, Silver Points) fires f3670 — `$7F01EC` never left 0.

Other outcome writers (from `battle_menu_synthesis.md §1.8`, re-confirmed by scan): **defeat** = `C1/813F` writes
**1** (gated dp `$24` party-wiped); **flee/debug-end** = `C1/81F9` / `C1/8357` write **0**; **no writer of 2 exists**.
Log: `SP/kill.txt`, `SP/victory.txt`.

## S6 — PC damage / defeat side (secondary)

PC HP writes go through the SAME apply+clamp routine (`C1/ED1A` store, block-indexed by X): GATO→Crono
70→56→42→… (`$5E30`, `C1:ED1A`). Death path for a PC = same `C1/ED04` dead-flag (block+0x1D bit7). A full party
wipe sets dp `$24` → `C1/813F` writes `$7F01EC`=1 (defeat). The dp `$24` maintenance and the `$2A1F` bit5
loss-continue branch are disasm-derived only (`battle_menu_synthesis.md §1.9`); no deliberate-loss capture was
taken (GATO would need ~15 counters). Marked as in the prior dossier's open list.

---

# PART 2 — IMPLEMENTATION-READY SPEC (minimal GATO loop, `src/battle.c CtBattleTick`)

Interface in = the staged blocks from the field handoff (`battle_menu_synthesis.md §(b/c)`): PC blocks
`$5E2D+seat*0x80`, enemy blocks `$5FAD+slot*0x80` (GATO slot3=`$612D`), alive-list `$AF02+slot`, id-list
`$AF0D+slot`, hidden `$AF15+slot`, party `$2980`. Out = `$7F01EC` (0 victory / 1 defeat) for `C0/0283`.

**Per-logic-tick (once per frame once combat is live; gate on a frame-ready flag like `$993A`):**

1. **Intro delay**: run a fixed pre-combat countdown (~20 counts, ~1 per 4 frames) before step 2 (`$993B` ramp to
   0x14 f301→377). Combat starts after.
2. **ATB**: for each alive combatant, `timer[c] -= 1` (the `$AF27,X` `DEC`, `C1/8267`). Turn order among
   simultaneously-ready = the speed-sorted permutation `$B163` (built `C1:808B`). First actor is pre-charged
   (acts at combat start); subsequent actors have `timer` reload (initial stagger ≈30). Post-action recharge =
   **`$B158[actor] = 105 − speed×6 + CC2E31[(speed−1)+(battleSpeed&7)×16]`** (speed=block+0x0D, ≤16;
   battleSpeed=`$2990&7`; `FD/B850`), applied `$AFAB,X=$B158,X` at `C1:BCEB` — **CLOSED, see ADDENDUM §B**.
3. **On `timer[c]` ≤ 0 → set current actor `$95D5 = c`.**
   - **PC** → open command window awaiting input (`$95D5`=index, `$95DB=0`, `$9609=0`); hand cursor = font tiles
     0x160-0x163 at that PC's HUD box; cursor row `$95DC`.
   - **Enemy (GATO)** → run AI = **physical-attack the lead PC** (target index 0), then go to step 5.
4. **Input A on the command window** (mash-A path = Att. on default target):
   Att. → target = the enemy (GATO) → confirm → execute physical attack → step 5.
5. **Damage** (both PC→enemy and enemy→PC use ONE routine):
   `base = $AEEF × (A[+0x36]×12 + A[+0x43]×5) / 18` (PC physical, `C1/E0F5`; enemy `C1/E27B` = `$AEEF×A[+0x3D]×2/18`)
   → `dmg = base × (256 − T[+0x3E])/256 + bonus`  *(`$18`=target DEFENSE, NOT rand — **CLOSED, see ADDENDUM §A**;
   `bonus=rand(0..A[+0x36]/3)`)* → stage `$B328,Y = dmg`
   → **`curHP $5E30,X (block+3) -= dmg`** → **clamp [0, maxHP block+5]**; if `curHP ≤ 0` → set **dead flag
   block+0x1D bit7**, and for an **enemy** clear `$AF02[slot] = 0xFF` (`C1:B2C3` / `FD/ABA2`).
6. **Reload the actor's `timer`** and clear `$95D5` (back to idle/AI) so the loop continues.
7. **End check** each tick:
   - all `$AF02[0..7] == 0xFF` (skip `$AF15` bit6 hidden) → **`$7F01EC = 0` (victory)** (`C1/817A`);
   - party all dead (dp `$24`) → **`$7F01EC = 1` (defeat)** (`C1/813F`).
   Then the field's `C0/0283` dispatch resumes (`battle_menu_synthesis.md §1.9`).

**Constants / addresses to hardcode:**
- Block: `+0 id, +3 curHP16, +5 maxHP16, +7 curMP16, +9 maxMP16, +0x1D deadflag(bit7), +0x37 stats`.
- Enemy lists: `$AF02+slot` alive (FF=dead), `$AF0D+slot` id, `$AF15+slot` hidden(bit6/7). GATO slot 3, id 0x92.
- Damage: staged `$B328/$B354/$B380` (hit1/2/3) + flag words `$B32A/$B356/$B382` (apply gate `BIT #$4000`);
  final value `$AD89`; target index `$B1F6`. RNG `C1/AF22`. Compute `C1/DCED` (normal) / `C1/EDB0` (crit).
- Apply `C1/EC3C`; clamp+death `C1/ECC7`; enemy-death alive-clear `C1/B2C3` (via `FD/ABA2`).
- ATB timer `$AF27,X` (dec `C1/8267`); turn order `$B163`; current actor `$95D5`; PC queue `$A6D9`(head `$A6DD`/
  tail `$A6DE`); logic gate `$993A`.
- Outcome `$7F01EC`: 0=victory(`C1/817A`)/flee/debug, 1=defeat(`C1/813F`), no writer of 2.

**NOT derivable / omitted (documented boundaries):**
- ~~Base ATK/DEF damage coefficients~~ — **CLOSED**: `base = $AEEF×(A[+0x36]×12+A[+0x43]×5)/18`, then
  `×(256−T[+0x3E])/256 + bonus` (`$18`=target DEFENSE, not rand). Disasm+empirical: **ADDENDUM §A**.
- ~~Speed→ATB-reload equation~~ — **CLOSED**: `reload $B158[a] = 105 − speed×6 + CC2E31[(speed−1)+(bs&7)×16]`
  (speed=block+0x0D). Disasm+empirical: **ADDENDUM §B**.
- **Full enemy-AI script interpreter (bank CC)** — GATO only physical-attacks in every observation; his song/
  special never fired and is dropped as inert for this fight.
- **Defeat path live behaviour** (`C1/813F`, dp `$24`, `$2A1F` bit5 continue) — disasm-derived, not captured.
- **Animations, sprite movement, techs, items, multi-enemy layouts, target-select navigation** — out of the
  minimal spine (rendering/menu subsystems; see `battle_hud_render.md`).

---

## Scratchpad artifacts produced (paths for the caller; not committed)

Dir: `%TEMP%/claude/C--Users-Icaro-Ferris-Desktop-Crono-Trigger-Decompile/e5608df4-34bf-4aa1-b49d-5255ac6f07ac/scratchpad/`
- `gato_lowhp.state` — **in-battle checkpoint, GATO 19 HP** (Crono 56, Marle 65) — one crit from death; reusable.
- Monitor logs: `gauge2.txt` (ATB −1/tick), `afab.txt`, `queue.txt`, `statemachine.txt`, `mashA_curhp.txt`,
  `dmg_samples.txt`, `ad89_writer.txt`, `dmg_writer.txt`, `kill.txt`, `victory.txt`, `death.txt`, `atb_cand.txt`.
- `ct_probe.exe` (oracle copy). WRAM captures: `w3*/e2*/c*/d*.raw.wram` (peek-timeline, structure-only).

---

# ADDENDUM — the two closed equations (S1 & S3 boundaries now CLOSED)

Both boundaries the spine left open are now derived byte-exact (disasm) **and** confirmed empirically
(oracle `--monitor` on stat-patched states). Method: the WRAM base offset inside a LakeSnes `.state` file =
**0x45** (both the GATO `92 00 00 13 00 4C` and Crono `00 86 80 38 00 46` block signatures land at `0x45+addr`),
so `state[0x45+addr]` hex-patches any stat byte. Experiments re-run the fight from a patched `gato_lowhp`
checkpoint (`surv*.state`, all HP=3000 so it never ends) and read the damage/reload back. Helpers
`scratch/patchstate.py` + `scratch/measure.py`. Method caveat: two PCs both hit GATO, so *attacker* experiments
zero Marle's stats and read GATO-HP deltas; *defense/enemy* experiments use the clean single-attacker
GATO→Crono channel; C90B (16×16→32 mult, `$2C=$28×$2A`) **exits 8-bit A**, C92A (divide) preserves width — that
width flip is what desynced linear dis.py in this region, and re-segmenting on it is what unblocked the trace.

## A. Damage base = f(attacker stats, target defense)  [closes S3]

Physical pipeline for attacker A vs target T. The per-attack **data record** (12 bytes → `$AEE6+Y`, Y=0..11,
copied from ROM `CC/213F`+`$B2C8`×12 for PCs / `CC/89C6` for enemies; writer `C1:D5F4`(PC)/`C1:D945`(enemy)) gives:
`$AEEB`=**attack TYPE**, `$AEEC`=**defensive-stat SELECTOR** (`0x3E`=physical), `$AEEF`=**attack POWER** (Crono
basic=**8**, GATO=**18**). `C1/D1A4` seeds `$16=$AEEB`, `$18=$AEEC`.

**Base power** (dispatch on `$16`, each finalizes via `JMP C1/E575`):
- **type 1 — PC single physical** `C1/E0F5` (E106-E15C):  **`P = $AEEF × (A[+0x36]×12 + A[+0x43]×5) / 18`**
  (`$5E63,X`=A+0x36, `$5E70,X`=A+0x43, X=`$B1F4`=attacker block index; 2× `C1/C90B` + `/18` via `C1/C92A`).
- **type 5 — enemy physical** `C1/E27B` (E27E-E2B8):  **`P = $AEEF × (A[+0x3D]×2) / 18`**  (`$5E6A,X`=A+0x3D).
- **type 2 — PC variant** `C1/E184`:  `P = $AEEF × (A[+0x3A]+A[+0x43])×8 / 18`.
- **bonus** `$B2BE = rand(0 .. A[+0x36]/3)` (E15E-E17A; RNG `C1/AF22`; writer `C1:E17D`).

**Status mult** `C1/E575` (attacker status `$5E4B/4E/53`): ×3/2, ×5/4, ×4/5, ×1/4; **no-op unafflicted** →
`$16 = P` (E634 `STX $16`).

**Defense resolve** `C1/E65F` (E661-E676): `$18` still holds selector `0x3E` ⇒ `$18 = $5E2D + $18 + $B1F6 =
T[+0x3E]` (E671 `LDA $5E2D,X` with X=0x3E+target = **target physical defense**; writer `C1:E676`). (`0x3C`=magic def.)

**Variance wrapper** `C1/DCB9→DCED`:  **`dmg = $16 × (256 − $18)/256 + ($B2BE & 0xFF) = P × (256 − T[+0x3E])/256
+ bonus`**  (`C1/C90B`× , `C1/C92A` /256). CRIT = `C1/EDB0` doubles the base (extra `C1/C90B` ×2) first.

⚠ **Correction to S3.2 (the "$18 = rand[0..99]" wording):** `$18` at DCB9 is **the target's DEFENSE byte
(T+0x3E)**, not a 0-99 RNG — proven directly (patched GATO def 127 vs 0 → `$18`=127 vs 0 at the Crono-attack
frame, writer C1:E676; dmg = 33·129/256=16 vs 33) and by scan (**DCBB is the ONLY `LDA #$0100` (256−x)/256 step
in all of `C1/D000-F000`**). PC physical damage is therefore near-deterministic (jitter = the small `+bonus`
only: A[+0x36]=40 gave 222/221, ±1). The `LDA #$64/JSR AF22` at `C1/E9F0` is the **hit-roll** (`rand<hit%`;
miss ⇒ `$16=0xFE`), a *different* RNG use — not the damage variance.

**Confirmation (oracle `--monitor`, patched states — one line per experiment):**

| experiment | predicted (disasm eqn) | measured |
|---|---|---|
| Crono base A[+36]=5,+43=3,$AEEF=8,def0 | 8·(5·12+3·5)/18 = **33** | live `$30`=33, `$AD89`=33 ✓ |
| A[+36]=10 / 20 / 40 | **60 / 113 / 220** | 61 / 114–115 / 221–222 ✓ |
| A[+43]=20 / 40 | **71 / 115** | 71–72 / 115–116 ✓ |
| GATO def(+3E)=127, Crono base 33 | 33·(256−127)/256 = **16** | `$18`=127(C1:E676), `$AD89`=16 ✓ |
| GATO def(+3E)=0 | 33·1 = **33** | `$18`=0, `$AD89`=33 ✓ |
| Crono def sweep, GATO→Crono (base 16) | 16·(256−def)/256, def 0/40/80/160/255 → 16/13/11/6/~0 | 16/13/11/7/2 ✓ (+bonus/min floor) |
| enemy GATO base, $AEEF=18, A[+3D]=7 | 18·(7·2)/18 = **14** | GATO→Crono base ≈14–16 ✓ |

Block stat offsets (0x80 stride): **+0x36 & +0x43** = PC attack terms, **+0x3D** = enemy attack term, **+0x3E** =
physical DEFENSE (PC & enemy). +0x0D = SPEED (see B). [The old `+0x37` note = the first stat byte, not ATK/DEF.]

## B. Reload = f(speed, battle-speed config)  [closes S1]

Countdown `$AFAB,X` (Crono X=0 → `$AFAB`; Marle X=1 → `$AFAC`), −1 per logic-tick (`C1:826B`, the `STA $AF27,X`
at `C1/8268`; a logic-tick ≈ 2.5 real frames at this `$993A` cadence). On action-completion the countdown is
**RELOADED**: `$AFAB,X = $B158,X`  (`C1/BCE5-BCE8` `LDA $B158,X / STA $AFAB,X`, writer `C1:BCEB`; the SAME
`$B158,X` also seeds the initial charge at `C1/E655`).

`$B158,X` = per-actor reload table, built at battle-init from SPEED (`FD/B807-B850`):

**`$B158[actor] = 105 − speed×6 + CC2E31[(speed−1) + (battleSpeed&7)×16]`**

- `speed = min(block[+0x0D], 16)` (clamp `FD/B80A`); `× 6` via `JSL C1/FDBF`(=`C1/C90B`); const `0x69`=105.
- `battleSpeed = $2990 & 7`; enemies keep the plain default 105 (`FD/B4F3`).
- Haste/Slow later add ±delta to `$B158`, clamped [1,255] (`C1/A150-A187`, writer `C1:A17F`).

Net observed = **reload = K − 6·speed** (K = 105 + config-table term, per-actor ≈144–150 at this fight's
`$2990`=0xC4 ⇒ battleSpeed 4):

| actor | speed (block+0x0D) | K−6·speed | measured `$B158` |
|---|---|---|---|
| Crono | 12 (native) | 144−72 = 72 | **72** (0x48) ✓ |
| Crono | 1 / 3 / 6 | 138 / 126 / 108 | **138 / 126 / 108** ✓ |
| Crono | 16 | (K=150 here) 54 | **54** ✓ |
| Marle | 8 (native) | 150−48 = 102 | **102** (0x66) ✓ |
| Marle | 12 / 2 | 78 / 138 | **78 / 138** ✓ |
| enemy GATO | — (default) | 105 | **105** (0x69) ✓ |

SPEED byte pinned by patch: char-sheet/block **+0x0D** is the only offset whose patch moves `$B158` (slope
−6/speed exact over 5 points each for Crono & Marle).

**Initial stagger** (combat-start fill, `FD:B935` @f290 — distinct from the reload): the sentinel `$AFAB`=0x011F
is replaced by a small first-round value; the first-to-act is **pre-charged** (Crono `$AFAB`≈0-1 → acts f381),
later actors get a fixed **~30-31** stagger (Marle 0x1F=31 → 0 at f454 → acts f459). This first-round fill is a
small fixed stagger, **not** speed-scaled (Marle's 31 is independent of her 102 reload); the speed reload only
governs 2nd-and-later turns.

Logs (scratch): `base_trace.txt`, `aeef_trace.txt`, `atb_reload.txt`, `countdown.txt`; states
`surv*.state`/`base0.state`/`survnm.state`; helpers `patchstate.py`, `measure.py`.

---

# ADDENDUM — entry STANCE (combatant battle sprites): the field object arrays are CLEARED at boot; the stance is the battle module's OWN OBJ engine (BOUNDARY)

Target: the user report "Crono is not keeping the animation (attack position)" — no battle stance on
entry. Derived by comparing pre-battle vs in-battle OAM + field object arrays (peek `pre_battle.raw`
`--btnscript "0:60"` vs `idle_f460`, decoder `bstate.py`) and a boot-window `--monitor --writer`.
**Every stance-candidate the caller flagged is RESOLVED below — two rejected, one identified as the boundary.**

**(1) The field object arrays do NOT drive the in-battle stance — candidate `$1600/$1680/$1601` REJECTED.**
In-battle (`idle_f460.wram`) ALL of `$1600,X` (facing), `$1680,X` (anim), `$1801,X` (tile), `$1100,X`
(move-mode) read **0 for every object 0-11** (vs the live field values pre-battle). Writer: at boot **~f288 the
battle module `C1:CE04` zeroes them** (monitored: obj `$1604` 02→00, `$1690` 01→00, writer C1:CE04) — the
battle-init memclear (this dossier S4, `STZ $5D80,X` family). The module WIPES the field arrays, so they cannot
carry a stance.

**(2) The `$0C00,X = ($2989&0x10)|0x20` override is field-side PRE-battle staging, not in-battle prio —
candidate REJECTED.** That write (our `field.c:1711`, ROM C0/045A, part of the C0/038F handoff) runs BEFORE
`JSL C1/0000`; the oracle's in-battle `$0C00` reads F4/FC/FD/F5… (OBJ Y-sort / draw-order bytes, `$0F00`=2900
for all), never 0x30. The `$29B0+5k` PC record (`field.c:1700`) is only the screen-POSITION handoff
(relTileX/Y, fineX/Y, prio) the module consumes to PLACE each combatant — it holds no stance/anim id.

**(3) The stance IS produced by the battle module's own OBJ sprite-animation engine (bank C1/CC) — the real
mechanism, and the BOUNDARY.** The module builds the OAM shadow `$7E0700` from the combatant blocks
(`$5E2D + seat*0x80`, loaded by `FD:AE83` at f288) + battle sprite/anim state. OAM proof (`bstate.py oam`):
- **pre-battle** each PC = a neat vertical 4-tile column = the STANDING field sprite (Crono pal5 @x126 tiles
  0x08/0A/0C/0E; Marle pal6 @x130 tiles 0x20/22/24/26);
- **in-battle** the SAME VRAM tile slots are re-assembled (different per-tile offsets) into a spread **battle
  stance** at battle positions (Crono left ~x109-126, Marle right ~x164-180), and it **ANIMATES**: the
  active-PC (Crono, command open) stance oscillates ±1-3 px/tile across `idle_f460/480/520/560` (an idle-ready
  loop); the waiting PC (Marle) is static. GATO likewise re-poses (tiles 0x006×4 mirrored + 0x068-0x08E, pal1).

So the stance is an **animated idle-ready loop**, not a static frame, emitted by the C1/CC combatant OBJ
builder + each character's battle-animation scripts.

**Our-engine gap:** `src/battle.c` (355 lines) implements ATB/damage/outcome + the HUD strip ONLY; it renders
**no combatant sprites**, so no stance appears — exactly the user's report. This is the documented boundary
"Animations, sprite movement, techs…" (PART 2, "NOT derivable / omitted").

**Minimal boot-time rule (mandate-clean):** there is NO field-array stance to set — the field arrays are
cleared (C1:CE04) and unused in battle; do NOT try to drive the stance via `$1600/$1680` or the `$0C00`
override. To show the stance our engine must port the **battle OBJ builder**: for each combatant, load its
battle sprite set, place it at its staged battle position (`$29B0+5k` PCs / `$29BF+12·slot` enemies), and run
its **idle-ready battle animation**. The full per-action animation set (walk-in, attack-swing, hurt, tech/item,
victory) + the C1/CC OBJ-assembly/animation engine = **BOUNDARY** (a large subsystem; not reducible to a
compact write). Cite this addendum before attempting any battle-sprite work.

Scratch: `pre_battle.raw*` (field standing), `idle_f460/480/520/560.raw*` (in-battle stance loop); boot writer
monitor `--monitor "1604:1,1690:1,5E30:2,2989:1" --btnscript "100:8,0:290" --diff --writer` (f288 C1:CE04 clear
+ FD:AE83 block load; f218 `$2989`→0x92 writer C0:3A99).

---

# ADDENDUM T2/T3 — enemy INITIAL ATB load (not $B158) + command-menu OWNER display (both pinned)

Probe `ct_probe --monitor "AFAB:10,9F22:10" --from gato_press_action.state --input "100:8,0:800" --diff --writer`.

**T2 — the enemy's first-turn timer is the FIXED stagger, NOT its $B158 reload (corrects the S1 §B/PART-2
"enemy initial = $B158" stopgap).** At combat-start fill (f290, `FD:B935`) GATO (idx6) loads **`$AFAB`=`0x1F`
(31) — the SAME fixed first-round stagger as Marle (idx1=`0x1F`)** — and his **`$9F22`=`0`** (enemies have NO
ATB bar; `FD/B935` `LDA $AFAB/STA $9F22` mirrors the **PC** slots `$AFAB/AC/AD` only, dis @`3DB93B`). His timer
`-1`/tick (`C1:826B`) reaches **0 at f454** (same as Marle) but he does **NOT act until f635**: a ~72-logic-tick
**bank-CC AI FIRST-TURN delay** while the timer sits at 0 (the full AI-script interpreter = the S4 BOUNDARY).
At f635 the writer is the reload `LDA $B158,X / STA $AFAB,X` (`C1/84DB-84E1`, dis-confirmed) → `0x66`=102 — i.e.
**`$B158` is the POST-action recharge, not the initial load.** So the enemy's net first-action ≈ **31 stagger +
72 AI-wait = ~103 ticks**, and `$B158`=102 equals it only by coincidence (30+72=102 for GATO's speed).

Implemented (`battle.c battleCombatStart`, `CT_BATTLE_AI_FIRSTDELAY 0x48`): first turn-order actor pre-charged;
every other present actor (PC *or* enemy) = `0x1E` stagger; an enemy additionally folds the AI first-turn delay
→ `0x1E + 0x48 = 0x66` = 102 (same VALUE as the old stopgap, so `field-battleloop-gato` is unchanged — MASHA
victory f227/7 attacks/Crono 57 pinned intact), and enemy `$9F22 = 0` (byte-matches the oracle f378). ⚠ The
absolute-frame gap **f182 (ours) vs f635 (oracle)** is NOT this bug: it is the **global cadence** — our loop runs
1 logic-tick/frame, the oracle's `$993A` gate runs ~2.5 real-frames/tick (see ADDENDUM 2 §A) — so GATO's
first-action **TICK count (102) already matches**; only the frame axis is compressed 2.5×. Reproducing f635 in
absolute frames needs the `$993A` frame-gate (a global change, out of scope for T2 / breaks every battle pin).

**T3 — the command-menu OWNER + A-target key on the QUEUE HEAD, and `$95D5` == the head in the settled state
(our head-based display is correct).** `saves/battle_re/p600.raw.wram` (COMMAND settled): `$95D5`=**0** (Crono),
`$95DB`=`$9609`=0 (idle-awaiting-input), ready-queue head `$A6DD`=0 → `$A6D9[0]`=**0** (Crono), tail `$A6DE`=2
(queue = [Crono, Marle]). So `$95D5` and the queue head BOTH = Crono; the tilemap hand sits at box0. The S1 f459
"`$95D5`=1" transient does NOT survive into the settled capture. Our engine sets `$95D5 = queue head` every tick
and the HUD keys the hand on `$95D5` (`field.c` `activePc`), so display + A-target never diverge from the head —
byte-matches p600. No change needed.

**T1 (engine-side app input, NOT ROM) — the battle command edge.** `field.c` reads A/Up/Down as SDL LEVEL state
(`SDL_GetKeyboardState`), so SDL key-repeat cannot inject extra edges; the rising-edge detect is gated by a
**release-after-battle-entry** flag (`battleAReleased`, reset on entry) so the A that CONFIRMED the field trigger
can't leak into Crono's first attack (mirrors the ROM's `$00F6` fresh-press edge-detect). The `CT_BATTLE_SCRIPT`
'a' is likewise EDGE-triggered now, so a held `a:60` fires ONE command (was: one per frame → auto-attacked every
PC as it readied = the user's "cursor jumps to Marle / battle self-resolves"). `CT_BATTLE_MASHA` stays an explicit
per-ready-PC auto-mash (drives the pinned victory).

---

# ADDENDUM — the GLOBAL LOGIC-TICK CADENCE (the "battle too fast" fix) + PC-death scheduling

Closes the T2 note above ("our loop runs 1 logic-tick/frame vs the oracle's ~2.5 frames/tick") and the
user reports "battle ends far too fast / Crono disappears / weird HP". Method = **`--monitor` (never
`--peek`; the peek/monitor timeline gotcha) of the per-actor ATB countdown `$AFAB` EVERY frame** over a
long idle run, plus a no-attack run to a PC death. Base = `gato_press_action.state` + A-boot. Scratch:
`cad/idle_diff.txt`, `cad/death.txt`.

## §C — the cadence is a REGULAR 2/3-frame alternation, avg 2.5 real frames per logic tick

`ct_probe --monitor "AFAB:10,993A:2" --from gato_press_action.state --input "100:8,0:900" --diff`
(`cad/idle_diff.txt`). Post-combat, **Marle's timer `$AFAC` decrements at frames
f378(reset),382,384,387,389,392,394,397,399,… → inter-tick gaps `[4,2,3,2,3,2,3,…]`** — a clean,
deterministic **2/3 alternation** (histogram over her 30-count stagger: {gap2:15, gap3:14, lead4:1}).
Absolute anchors (all measured, this run): Marle 30 ticks span **f378→f454 = 76 frames**; GATO's 102-tick
first action spans **f378→f635 = 257 frames** (30 stagger ticks to 0 at f454, then a 72-tick bank-CC AI
first-turn delay, ADDENDUM T2). Both = **2.53 real frames per logic tick**. `$993B` (the gate residual,
display byte1) stays **0–3** the whole post-combat run — exactly the dossier-S1 "0-3" observation, i.e.
the produce/consume of the `$993A` frame-gate.

**Mechanism (grounded, not a bare constant):** the render/HUD side `CF/E748` **produces ~+2 per real
frame** into `$993A`; the battle tick `C1/10E3` (`LDA $993A / BEQ`) **consumes 5 per logic step** → a tick
fires every 2.5 frames, leaving the 0-3 residual. That produce/consume is reproduced DIRECTLY as a
per-frame accumulator:

    each render frame:  tickAcc += 2 (TICKADD);  if tickAcc >= 5 (TICKTHR):  fire ONE logic tick, tickAcc -= 5

(ADD < THR ⇒ ≤1 tick/frame.) **Seed `tickAcc = -2` at combat-start** (ACC0): a Python sim of this
accumulator vs `idle_diff.txt` reproduces the observed gaps **`[4,2,3,2,3,…]` EXACTLY**, with Marle's
30th tick at **+76f** (== oracle f454) and GATO's 102nd at **+256f** (== oracle f635, −1f within the
AI-delay jitter). The leading gap 4 is combat-start-specific (the intro→combat `$993A` reset realigns the
phase); a later reload (GATO post-action) resumes with no leading-4 (`cad/idle_diff.txt` gap after his
f635 reload = 2), which the ACC0 seed does not re-apply (it only fires once, at battleCombatStart).

**Clock separation (which counter uses which clock — stated in `src/battle.c`):**
- `h->frames` = **RENDER frames**, 1/frame. Drives the **intro delay** (`CT_BATTLE_INTRO=80`) and the
  **HUD settle** (`field.c frames>=82`) — both REAL-frame per battle_hud Q6/ADDENDUM 2 (settle f304−boot
  f222 = 82 real frames), UNCHANGED. Also the debug-end / cap / trace / `CT_BATTLE_SCRIPT` timing.
- **logic ticks** (the accumulator) = the 2.5-f/tick clock. Gate ONLY the post-intro scheduler: the ATB
  `−1` decrement (§S1), the enemy AI first-turn delay (a TICK count = 0x1E+0x48, so it auto-slows), the PC
  enqueue, the command/nav (`CT_BATTLE_NAV_DELAY=6`/`REPEAT=2` ticks now, ≈15/5 real frames per §B), and
  the end-check. On a non-tick frame `CtBattleTick` returns early (the oracle's `LDA $993A/BEQ` skip); the
  HUD holds the prior state. **Input edges are LATCHED** across skipped frames (`h->pendEdges`, joypad-latch
  parallel) so a command pressed on a skipped frame is consumed by the next tick — never dropped.

**Validation (our engine, `CT_BATTLE_TRACE`):** GATO first action **f336** (= combat-start f80 + 256,
matches the derived +256f); MASHA deterministic victory **f227 → f449** (×2 byte-identical), with the SAME
7 PC attacks / same 33→16 damage / Crono 57 / Marle 65 — only the frame axis stretched 2.5×. Pin
`field-battleloop-gato` re-pinned to f449; `field-battletest-gato` (debug-end@f60, pre-intro) and
`field-battlehud-gato` (settle real-frame) are UNCHANGED (both stay green).

## §D — PC death leaves the scheduling (Target 2a) + the "ends as if I won" / "weird HP" triage

**PC-death cleanup (EXTRAPOLATED — no clean oracle death-frame was capturable).** A no-attack idle
monitor `"95D5:1,A6DD:3,5E4A:1,5E30:2"` over 2200f (`cad/death.txt`) proved the CONFIRMED semantics:
`$95D5` is **STICKY at the ready-queue HEAD** (Crono=0 from f381), the queue = **[Crono@f381, Marle@f462]**
(head `$A6DD`=0, tail `$A6DE`=2). But GATO's **idle bank-CC AI-script (the S4 BOUNDARY) does NOT
physical-spam the lead to death** — after one hit (f635, 70→61) it slow-drains (Marle −1s), so Crono never
died in-window. So the death rule is derived from the confirmed semantics (`$95D5`==head, queue=$A6D9
ring, death via alive-list/dead-flag S5) and implemented minimally (`battle.c battleRemoveDead`): **on a
combatant's death (block+0x1D bit7), purge his seat from `$A6D9[head..tail)` preserving order; if he was
the head/`$95D5`, the compaction advances to the next living PC (or empties → 0xFF); his timer already
stops (the alive-gate halts the −1 decrement AND the re-enqueue).** VERIFIED (`CT_BATTLE_TRACE`, do-nothing
wipe): Crono dies f1611 → next tick `$95D5` 0→1 (Marle), queue `q[0..2]→q[0..1]` (Crono removed); a
post-death A acts **only Marle (cur=1)**, dead Crono NEVER acts (0 dead-PC actions); party wipe → defeat.

**"ends as if I won" — NOT a plumbing bug (verified correct).** Headless party-wipe (no A / partial A):
outcome `$7F01EC=1`, `$2A1F` bit5 set (staging 18/18 incl. `$2A1F=0x20`) → the C0/0298 scripted-loss
CONTINUE (no GAME OVER, field resumes) → op-0x16 **==1 DEFEAT branch → `$7F0052` stays 0** (points=$00),
vs the victory path's points=$0F. The victory end-check requires ALL enemies dead (`C1/817A`) checked
before defeat, and the step-order (enemy acts D → removeDead → PC acts F → end-check G) cannot let a wipe
tick also kill the last enemy (no living PC is left to act in F). "Crono disappears" = his dead-flag
correctly suppressing the stance (no death pose exists — a known boundary), not a wrong outcome.

**"weird HP" — the display pipeline is byte-correct for the GATO fight (0–99 HP).** `CT_BATTLE_HPDBG`
(new inert stderr hook in `CtBattleHudRender`) confirms: normal HP reads `$5E2D+3`/`$5EAD+3` → "57"/"65";
HP=0 → tens BLANK + ones 0 = " 0" (leading-zero blank, correct). The `$B328,Y` damage staging is
COSMETIC (the apply uses the local `dmg`, never re-reads the stage → no cross-contamination; PC y=0/4/8,
GATO y=24 land in the stage area, clear of the `$5E2D`/`$5FAD` blocks). The C0/0715 defeat-pose staging
is field-side + post-battlePhase (writes `$1780`/`$1301`, not the HP blocks; the HUD isn't drawn after the
battle) → can't touch the HP cell. **LATENT (unfixed, boundary):** the 2-digit HUD clamps HP>99 to "99"
(`field.c:1565`) — UNOBSERVED in the GATO fight (max 70) and the 3-digit HUD column layout is un-RE'd; a
capture of a >99-HP party in battle is needed before implementing it (do NOT invent the layout).

---

# ADDENDUM — BATTLE-END HP/STAT WRITEBACK (block → char sheet): `C1/B442`, the reverse of the boot copy

The boot copy (`battle_boot.md` FD/AE52, `LDA $2600,X / STA block,Y`) stages the char sheet `$7E2600+id*0x50`
INTO each PC battle block `$5E2D+seat*0x80`. At battle END the module does the **exact reverse** so post-fight
HP/MP persists into the field + menu (the sheet `$2600` is the canonical live-HP store the X-menu reads).

**Routine `C1/B442`** (fresh disasm; the store retires as monitor writer **`C1:B475`**): for each party seat
`X = 0..2`, `LDA $2980,X` (char id; `CMP #FF / BEQ` skip empty), then a `CPY #$50` loop copying the **whole
0x50-byte** block `$5E2D+seat*0x80` → sheet `$2600+id*0x50` (`LDA $5E2D,X / STA $2600,X`, dest index = id*0x50,
src index = seat*0x80). **Ungated** at the routine entry (no outcome test) — the caller runs it once at battle
end, BEFORE the field handoff `C0/A730` (`battle_end.md` T2).

Oracle (`gato_lowhp.state` + mash-A win, `--monitor "2603:2,2653:2,2607:2,263D:1,5E30:2" --writer`): the sheet
stays constant the WHOLE fight (block `$5E30` alone tracks damage 70→…), then at **f1110** `C1:B475` writes, in
one pass: Crono `$2603` **70→19** (= block `$5E30`), Marle `$2653` 65→65, curMP `$2607` 8→8, and — proving the
copy is the full 0x50 — **attack `$263D` 8→7** (the boot-derived `+0x3C..+0x45` block value, `kBattleStatDerive`).
Fires once (f1110, after the kill f950, before the field handoff `$95D5`→0x80 `C0:A730` f1170).

**Ported** (`field.c fieldBattleFinish`, the battle-finish convergence of both the interactive and headless
paths): the same per-seat whole-block copy `$2600+id*0x50 ← $5E2D+seat*0x80`, id from `$2980`, id==FF skipped.
Without it our engine kept the pre-battle sheet HP, so the post-battle X-menu showed stale HP that disagreed
with the battle — the user's "battle HP ≠ menu HP" report (`--scene 6 --itemdemo`). Paired with a seed-ONCE char
sheet carry (`field.c g_sheetCarry`, mirroring `g_carryParty`) so a scene transition no longer re-seeds the sheet
from `ctCharBase` and wipe the written-back HP. ⚠ **Defeat-path writeback UNVERIFIED** (no deliberate-loss capture;
GATO can't be lost) — the port runs the writeback on every battle-end (matching the ungated ROM routine), but a
party-wipe → HP=0 → sheet=0 interaction with the `$2A1F` bit5 scripted-loss continue is not oracle-confirmed.

---

# ADDENDUM — the ACTION-FREEZE (`$A013`) + the full pacing ledger ("the battle still seems too fast")

Closes the biggest remaining pacing gap. User: *"the battle still seems too fast"* — our mash fight
resolves in ~900 render frames vs the oracle's ~1950–2850. **~1000–1900 of those oracle frames are ATB
FREEZE around each action, and it happens in ACTIVE mode** — a mechanism the `battle_atbmode.md` dossier
missed (it pinned only the Wait-mode `$99E0` submenu freeze and stated ACTIVE "never freezes"). It also
directly resolves the `battle_hud_render.md` ADDENDUM 2 §A OPEN note ("post-attack sweep f560-760 fully
frozen" on the DEFAULT=ACTIVE state) and corrects `battle_actionfx.md`'s "the f558→f725 gap is GATO recharge,
NOT a freeze" (it IS a freeze — the ATB `$AFAB` is byte-frozen across it).

Method = `--monitor` (never `--peek`; the peek/monitor gotcha) of the per-actor ATB countdown `$AFAB` + the
candidate gates, EVERY frame, over A-mash fights on `gato_press_action.state` (ACTIVE) and `gato_wait.state`
(WAIT), + `tools/dis.py`. Writer PCs = the PC after the store retires. Scratch: `ledger_full.txt`,
`ledger_active.txt`.

## §E — THE FREEZE RULE: `$A013` = the "action-executing" flag (set C1/40A6, clear C1/41BA)

The candidates the `$99E0` Wait path suggests are all RULED OUT for ACTIVE: across the whole ACTIVE fight
**`$99E0`=0, `$95DB`=0, `$9609`=0, `$AF25`=0, `$AFB6`=00** — none move during the freeze
(`ct_probe --monitor "AF25:1,AFB6:8,95D5:1,99E0:1,9609:1" --from gato_press_action.state --input <mash> --diff`).
The freeze is driven by a DIFFERENT byte:

**`$A013` (byte) = 1 while any combatant's action animates, 0 otherwise.** Writers (ROM scan + `--writer`):
- **`INC $A013` at C1/40A6** (monitor writer `C1:40A9`) — inside the **action-execution routine `C1/4090`**,
  which right after it drives the swing/claw via `JSL CC/F156/F278/F1E7/F110` (bank-CC OBJ anim) and advances
  frames with `JSR C1/007E`.
- **`STZ $A013` at C1/41BA** (monitor writer `C1:41BD`) — at **action-completion `C1/41xx`**, right after two
  `JSR C1/007E` frame-waits, then `RTS`.
- (also `DEC $A013` C9/A816 and `TSB $A013` D6/EFAD — other subsystems, not this loop.)

**Why `$A013`=1 freezes EVERYTHING** (byte-exact oracle proof, `ledger_active.txt`, ACTIVE, first action):
| f | event | writer | `$AFAB` (Crono/Marle/GATO) | `$993A` |
|---|---|---|---|---|
| f417 | Crono confirms target → dispatch | `$95D5`→FF `C1:1745`, `$9609`→0 `C1:1629` | Marle 0E ticking (`C1:826B`) | osc 0-3 |
| f419 | last ATB tick before freeze | `C1:826B` | Marle **0E** | osc |
| **f423** | **`$A013` 0→1** | **`C1:40A9`** | Marle **0E (FROZEN)** | **frozen 0x01** |
| f556 | action done → Crono timer reload | `$A013`→0 `C1:41BD`; `$AFAB`+= `C1:BCEB` | Crono 00→**48**, Marle still **0E** | still frozen (next span) |
| f560 | next action (GATO→Crono) starts | `$A013` 0→1 `C1:40A9` | Marle **0E (FROZEN)** | frozen |
| f786 | that action done | `$A013`→0 `C1:41BD` | Marle **0E** | — |
| f788 | ATB RESUMES | `C1:826B` | Marle 0E→**0D**, Crono 48→47, GATO 0E→0D | osc resumes |

So `$AFAB` (all slots) and `$993A` (the logic-tick clock) are **byte-frozen for the entire `$A013`=1 span**
(Marle stuck at 0x0E for 137+226 frames), with **`$99E0`=0 throughout** (NOT the Wait gate).

**Mechanism (disasm-cited).** The per-actor ATB decrement is `$AF27,X --` at `C1/8267` (writer `C1:826B`),
reached only from the battle-logic tick `C1/10E3`/`C1/10FA` — both gated `LDA $993A / BEQ` (skip if 0) and
themselves reached from the main battle frame. The sole producer of `$993A` is **`INC $993A` at CF/E745**
(gated `INC $9931 / AND #03 / BNE`, the HUD per-frame chain). During `$A013`=1 the action routine `C1/4090`
**owns the frame**: it advances via `JSR C1/007E` = `INC $9E / JSL CD/0036 / LDA $9E / BNE` (spin one frame
through the bank-CD kernel + OBJ anim) — a path that renders the animation but **does not run `C1/10E3` nor
the CF `$993A` producer**. So `$993A` stops being produced/consumed (freezes at its residual 0x01) and the
`C1:826B` decrement never fires → the whole ATB freezes. Corroborating readers that gate on `$A013`:
**`CD/04BF` `LDA $A013 / BNE ->skip`** (skips per-frame scheduler work `CD/0453` when an action runs), plus
the HUD/render path `CF/E706, E74B, E769, E3F8, E909, EDC0, EDF7, EFAF` and `CD/09A0` — `$A013` is a GLOBAL
"action mode" switch. `$9931` keeps counting during the freeze (the action loop re-uses it, resetting it at
action start) but the real `INC $993A` at CF/E745 is not executed — verified: `$993A` stays 0x01 even as
`$9931 & 3 == 0` recurs (`ct_probe --monitor "9931:2,993A:2,A013:1,AFAB:2" ...`).

**Enemy actions freeze identically.** GATO's attacks/counters raise `$A013`=1 the same way (the counter span
f560-786 above; GATO's `$AFB1` timer also frozen at 0x0E). The `$A013`-freeze is NOT PC-only.

## §F — Active-vs-Wait delta (the freeze taxonomy)

Two INDEPENDENT freeze mechanisms, now both pinned:
- **`$A013` action-freeze — SHARED (Active AND Wait), the dominant one.** Verified identical on
  `gato_wait.state`: the `$A013`=1 spans land at the SAME frames (f478-635, f643-768, f772+) and freeze the
  ATB the same, with `$99E0`=0 (no submenu open) — so it is mode-INDEPENDENT
  (`ct_probe --monitor "A013:1,99E0:1,95DB:1,95D5:1,AFAB:2" --from gato_wait.state ...`).
- **`$99E0` submenu-freeze — WAIT-ONLY, ADDITIVE (`battle_atbmode.md` §1.7).** In Wait, `$99E0`=0x40 while a
  PC has a Tech/Item/target sub-menu open (`$95DB ∈ {1,2}`), freezing the ATB via `C1/81CA`. It STAYS 0 in
  Active. It stacks ON TOP of the `$A013` freeze; it is not the freeze the user is seeing in ACTIVE.

So `battle_atbmode.md`'s "ACTIVE never freezes" was wrong at the whole-fight level: ACTIVE never freezes *for
a submenu*, but ALWAYS freezes *for an action animation*.

## §G — THE FULL PACING LEDGER (one complete ACTIVE mash fight, byte-verified, sums exactly)

`ct_probe --monitor "A013:1,95D5:1,9609:1,6130:2,5E30:2,5EB0:2,0117:2" --from gato_press_action.state
--input <('100:20,0:40')×70> --frames 2250 --diff --writer` (`ledger_full.txt`). Boot f219 → field-ack f2169
(`$0117`→0001 `C0:18C1`, `$7F01EC`=0 victory throughout). Every span classified; frozen = `$A013`=1 span:

| span (frames) | len | class | what / damage (writer) |
|---|---|---|---|
| f219→f378 | **159** | INTRO | module boot (`$0117` 21→01) + HUD slide + intro-delay ($993B ramp); ATB not yet counting |
| f378→f478 | 100 | TICKING | ATB reset 0x1E @f378, Marle/GATO charge; Crono pre-charged ready f381; his menu-nav+target |
| **f478→f635** | **157** | **FROZEN** | GATO action → Crono `$5E30` 70→61 (−9, `C1:ED1A`) |
| f635→f643 | 8 | TICKING | enqueue latency |
| **f643→f768** | **125** | **FROZEN** | PC physical → GATO `$6130` 76→60 (−16, `C1:ED1A`) |
| f768→f772 | 4 | TICKING | enqueue latency |
| **f772→f998** | **226** | **FROZEN** | GATO action → Crono 61→47 (−14) |
| f998→f1005 | 7 | TICKING | enqueue latency |
| **f1005→f1151** | **146** | **FROZEN** | PC physical (crit) → GATO 60→26 (−34) |
| f1151→f1155 | 4 | TICKING | enqueue latency |
| **f1155→f1377** | **222** | **FROZEN** | GATO action → Crono 47→32 (−15) |
| f1377→f1627 | 250 | TICKING | Marle recharges to ready (post-reload countdown) + her menu-nav+target |
| **f1627→f1769** | **142** | **FROZEN** | Marle physical → GATO 26→10 (−16) |
| f1769→f1791 | 22 | TICKING | enqueue latency |
| **f1791→f1938** | **147** | **FROZEN** | Marle physical → GATO 10→0 KILL (`C1:ED12`); no counter (GATO dead) |
| f1938→f2169 | **231** | VICTORY-HOLD/END | death → victory pose/fanfare/XP → field ack `C0:18C1` (`$A013`=0 — NOT an action-freeze) |

**SUMS (byte-exact, add to the observed total):**

| class | frames | note |
|---|---|---|
| INTRO | 159 | fixed pre-combat |
| TICKING (ATB advancing) | **395** | ← ≈ what OUR engine's whole combat currently is |
| **FROZEN (action animations, `$A013`=1)** | **1165** | **7 spans; 60% of the fight — the missing time** |
| VICTORY-HOLD/END | 231 | fixed post-combat |
| **TOTAL** | **1950** | = boot f219 → ack f2169 (1950) ✓ |

**Reconciliation to `mon_full.txt` (2851f).** Same structure, MORE rounds: this fight got two crits (−34, and
fast kills) so GATO died in **4 PC hits / 7 action-spans**; a no-crit fight (mon_full) needs ~5-6 PC hits +
more GATO turns ≈ **11-13 action-spans**. Fixed parts (intro 159 + victory 231 + ticking ~400) ≈ 790f; the
remaining 2851−790 ≈ **2060f = ~12 freeze spans** at the observed 125-226f each. So the freeze fraction
(and the ~1900f our engine skips) scales with round count in BOTH fights. The ledger of SPANS — not the total
— is the implementation contract (the total falls out of the damage/round count our engine already computes).

**Observed span-length law** (for the engine): **PC physical ≈ 125-157 real frames; enemy physical ≈ 157-226
real frames** (enemy longer: GATO's rear-up+claw is a longer animation, `battle_actionfx.md` A3.1 ~49f hold +
return, plausibly + bank-CC AI think-time — the exact split is a boundary). These are REAL frames, not logic
ticks: `$993A` is frozen during an action so the animation is real-time-paced. The "~100f between strike
(compute) and HP-apply" (`battle_actionfx.md`; e.g. compute f451, apply f556) is subsumed INSIDE the frozen
span — compute mid-animation, apply at span-end — not a separate pause. The damage popup (56f lifetime,
`battle_actionfx.md` P1.5) rises and clears inside the span too.

## §H — T3: inter-turn pauses (there is no hidden one)

The only non-action, non-frozen time is: INTRO (159f), the ATB-CHARGE spans (100f, 250f — a readying PC's
countdown draining at the 2.5f/tick cadence, §S1/§C, plus input-paced menu-nav in ACTIVE where the ATB keeps
ticking), the **~4-8f enqueue latency** between a timer hitting 0 and the next action (already known, §S2),
and the VICTORY hold (231f). No separate "pause when a PC becomes ready / menu opens" beyond the enqueue
latency. (Note: a stalled synthetic mash — `100:16,0:16` on the fresh state — can wedge `$A013`=1 forever if
an A-edge lands exactly on an action boundary; that is an input-timing artifact of headless mashing, not a
canonical hang — use a slower A-cadence like `100:20,0:40` for a clean full capture.)

---

# PART 2 (freeze) — IMPLEMENTATION-READY SPEC (the pacing fix)

Our engine (`src/battle.c`) already applies the 2.5f/tick cadence to the ATB (ADDENDUM 2 §C) but keeps the
ATB TICKING through action windows — it only blocks dispatch. The fix ADDS the `$A013` action-freeze:

1. **Mirror `$A013` with an engine flag `battleActionFreeze`** (or reuse an existing "action animating" state):
   set it the frame an action is DISPATCHED (PC physical confirmed, or enemy AI action chosen — our
   `C1/40A6` equivalent), clear it when the action's animation-through-apply COMPLETES (our `C1/41BA`
   equivalent).
2. **While `battleActionFreeze`:** do NOT advance the logic-tick accumulator (`tickAcc`, ADDENDUM 2 §C) and do
   NOT decrement ANY combatant's ATB countdown (`C1/8267` equiv). Hold `$95D5`, the ready-queue, and the HUD
   bar. This freezes ALL timers (acting + waiting + enemy) for the action's duration — mode-INDEPENDENT (do it
   in Active AND Wait; it is separate from, and additive to, the existing Wait `$99E0` submenu gate,
   `field-battlewait-gato`).
3. **Freeze DURATION = the action's full animation-to-apply length in REAL frames** (the `$993A` clock is
   frozen, so it is real-frame-paced, NOT 2.5f/tick): **PC physical ≈ 125-157 f, enemy physical ≈ 157-226 f**
   (observed §G). Our engine already bakes the animation trajectories (`src/data/battle_anim.h` `ctCronoAttackTraj`
   f440-530 ≈ 91f, GATO rear-up) + the popup lifetime (`CT_FX_LIFETIME` 56) — the freeze should span
   dispatch → (swing + popup rise/hold + HP-apply + return), i.e. hold until the whole action sequence the FX
   layer already plays has finished. Pin the observed span constants if the animation layer isn't wired.
4. **Do NOT freeze:** the intro delay (`CT_BATTLE_INTRO`), the victory hold (231f end-sequence), input edge
   latching, or the HUD render — those keep their real-frame clock (§S1/§C unchanged).
5. **Net effect:** restores the ~60%-of-fight frozen time (1165f of 1950f here). Our combat's currently-~900f
   ticking becomes ~900f ticking + ~1165f×(rounds/7) frozen → matches the oracle's per-span pacing. Re-pin
   `field-battleloop-gato` to the new (longer) victory frame after implementing; the DAMAGE/turn-order/outcome
   logic is unchanged (this is a pure per-frame pacing gate).

**Addresses to cite:** action-flag `$A013` (set `C1/40A6`, clear `C1/41BA`; readers `CD/04BF`, `CF/E706/E74B/
E769/E3F8/E909/EDC0/EDF7/EFAF`, `CD/09A0`); action-execution routine `C1/4090` (frame-advance `C1/007E` =
`JSL CD/0036`); ATB decrement `$AF27,X--` `C1/8267` (writer `C1:826B`) via tick `C1/10E3`/`C1/10FA` (gated
`$993A`); `$993A` sole producer `INC` `CF/E745` (gated `$9931 & 3`); Wait-only additive gate `$99E0`
(`C1/81CA`, `battle_atbmode.md`).

## OPEN (freeze) — residue, flagged with size
- **Enemy action span length composition** (157-226f vs PC 125-157f): the extra ~60-80f is GATO's longer
  rear-up+claw animation and/or bank-CC AI think-time — the split is not decomposed (the bank-CC AI
  interpreter is the standing S4 BOUNDARY). Span LENGTHS are observed/pinned; their internal makeup is open.
- **`mon_full.txt`'s exact 2851f decomposition** is RECONSTRUCTED (same structure, ~12 spans), not directly
  captured — its input is unrecorded and a fast headless mash on the fresh state can wedge (§H). The fully
  captured, byte-exact ledger is the 1950f fight (`ledger_full.txt`); the 2851 reconciliation is an estimate
  (±~a span's worth). Residual uncertainty ≈ one 150-220f span.
- **`$993A` micro-bookkeeping during a freeze** (why it holds at 0x01 vs 0x00): the action routine calls
  `C1/10FA` a couple times at action-start; the exact consume/hold of the residual is not decomposed. Not
  load-bearing — the ATB-freeze is proven directly on `$AFAB` (`C1:826B` never fires in-span).
- **Multi-enemy / big-party fights:** only 1-enemy + 2-PC captured; the freeze is per-action so it should
  generalize, but simultaneous-action layouts are unobserved.

---

# ADDENDUM — SPAN COMPOSITION (T1) + INTRO 159 (T2) + VICTORY 231 (T3): the three pacing residues closed

Closes the "OPEN (freeze)" residue above (enemy-span makeup) and the caller deltas *INTRO 80 vs 159 / enemy
157 vs 157·226·222 / victory-hold parity* — user *"the battle still seems too fast"* (our 1705f vs the
oracle 1950-class). Method = `--monitor` (never `--peek`, the timeline gotcha) of `$A013`/`$9876`/`$9877` +
`--writer` + `tools/dis.py`, base `gato_press_action.state` (ACTIVE). Ledger-mash = `('100:20,0:40')×70`; HOLD
(isolated-GATO) = `100:8,0:2600`. Writer PCs = the PC after the store. Scratch: `t1_ledger9876.txt`,
`t1_9876writer.txt`, `t1_bytes.txt`, `t1_hold.txt`, `t3_victory.txt`, `t3_noA.txt`, `t3_phases.txt`.

## §I — T1: the frozen span is a `$9876`-gated animation LOOP; its length is the AI-chosen attack's anim (emergent)

**The span mechanism (disasm `C1/4090`, the action-execution routine `$A013` set at `C1/40A6`).** The routine
is a **per-frame loop** `C1/40B0→40D1` that advances exactly **one real frame per iteration** (`JSR C1/007E` =
`INC $9E / JSL CD/0036 / spin`) while driving the bank-CC OBJ anim (`JSL CC/F156·F278·F1E7·F110`), and **loops
while `$9876 == 0`** (`C1/40CE LDA $9876 / C1/40D1 BEQ ->40B0`). So:

> **Frozen-span length (real frames) = number of loop passes until the anim engine sets `$9876 != 0`.**

Confirmed byte-exact (`t1_9876writer.txt`, `t1_bytes.txt`): **`$9876` (byte) = 0 for the whole span, then
pulses to `0x01` 1-3 frames before span-end (writer `C1:4D03`), which drops out of the loop; it is re-zeroed
by `CC:F0EC`** right after. `$A013` clears at `C1:41BD` (`C1/41BA STZ $A013`) that same frame.

**What actually varies the length = the ATTACK the AI picks, held in `$9877`.** `$9877` (byte, NOT `$9876`) is
the **animation/attack selector**, set once at span-dispatch and constant through the span. Its writer + the
2-way selector (`dis 144AF`):

```
C1/44AF  LDA $AE93            ; the attack-table switch
C1/44B2  BNE ->44E0           ;   $AE93 != 0 → table B
C1/44B4  LDA $CD4926,X        ; $AE93 == 0 : record from ROM table CD/4926 (X = attackIdx*6)
C1/44B8  STA $9877            ;   → $9877 = 0x31  (GATO attack A, the SHORT anim)
C1/44E0  LDA $CD4F26,X        ; $AE93 != 0 : record from ROM table CD/4F26
C1/44E4  STA $9877            ;   → $9877 = 0x34  (GATO attack B, the LONG anim)
```

`$9877` then indexes anim-length table `CD/5FF0` → `$9878` (`C1/4535-453C`). Each ROM attack record is **6
bytes** (`X = Y*6`, `C1/44A3-AB`), setting `$9877/$987A/$987B/$987E/$9881/$9884` = anim-id + targeting/damage-
type params. A THIRD path `C1/469A` (writer, table region `CD/…`) supplies GATO's special (`$9877=0x10`).

**Per-attack span lengths — OBSERVED (the implementable table; emergent-timing → the copyFreeze precedent):**

| actor / action | `$9877` | writer | span (real frames) | source |
|---|---|---|---|---|
| PC physical (Att) | **0x00** | `C1:43FF` | **125** (Crono); ~142-147 (Marle) | ledger f643→768; §G |
| PC **critical** | **0x0F** | `C1:4392` | **146** | ledger f1005→1151 |
| GATO attack A (1st turn) | **0x31** | `C1:44BB` (`$AE93==0`) | **157 / 158** | ledger f478→635; HOLD f1741→1899 |
| GATO attack B (later turns) | **0x34** | `C1:44E7` (`$AE93!=0`) | **222 / 226** (~224) | ledger f772→998, f1155→1377 |
| GATO **special** (0x10) | **0x10** | `C1:469A` | **311** | HOLD f1151→1462 |

**⇒ THE SPAN LAW.** The enemy-span "growth 157 → 226 → 222" the OLD OPEN note flagged is **NOT** think-time and
**NOT** monotonic growth: it is **attack-variant selection** — GATO's AI runs attack **A** (`$9877=0x31`, 157f)
on its first action and attack **B** (`$9877=0x34`, ~224f) on subsequent ones, a **deterministic** table switch
on `$AE93` (toggled between turns by the AI interpreter, writer **`C1:AD08`** — `$AE93`: 0→0x10→0 across turns,
`t1_hold.txt`). The "+60-80f" is exactly attack B's longer animation vs attack A's (**both are pure animation**,
see below). In the isolated HOLD fight GATO instead **alternates A(0x31)/special(0x10)** (157/311f) — i.e. the
attack picked (hence the span length) depends on combat state, which is the **bank-CC AI-script interpreter =
the standing S4 BOUNDARY.** So the span length is **emergent from script execution**, per-attack-pinned above.

**"What fills the frames before/after the anim" (the exact caller question) — answered:**
- **Before the span** = the 4-8f TICKING enqueue gap, inside which the **damage is pre-computed** (`C1:DCED`
  normal / `C1:EDB0` crit; e.g. enemy span2 computes f769, dispatches f772). Small, already known (§S2).
- **Inside the span** = **100% animation**. The anim script runs frame-by-frame; the **HP write lands at the
  LAST span frame** (`C1:ED1A`, e.g. f998 for the f772→998 span). There is **NO pre-anim think-pause and NO
  post-anim recovery counter inside the frozen span** — the longer attack (0x34) is simply a longer animation
  (more rear-up hold + follow-through, `battle_actionfx.md` A3/A4). The attack "decision" is the instantaneous
  `$9877` write at dispatch, not a timed wait.
- The **72-tick AI first-turn delay** (ADDENDUM T2 above) is a *TICKING-phase* delay BEFORE GATO's first
  dispatch (`$A013=0`), **not** part of any frozen span — do not double-count it.

**Engine contract (unchanged mandate stance).** Since the split is the AI interpreter (boundary), pin the
per-attack table above; for an engine that does not model GATO's attack selection, use the **oracle-anchored
enemy-span average of the ledger fight = (157+226+222)/3 ≈ 202 real frames** (copyFreeze precedent), with PC
physical 125 / crit 146. Our engine's single pinned enemy span (157) is the *attack-A* value only — it under-
counts every *attack-B* turn by ~67f, and every *special* by ~154f. Raising the enemy span to ~202 (or the
per-attack table) recovers the missing enemy-side freeze time. (Damage/turn-order/outcome logic unchanged —
pure per-span duration.)

## §J — T2: the intro is 159 real frames = ~82 boot/HUD + ~77 the `$993A` ramp (byte corrected: it is `$993A`, not `$993B`)

Full boot→combat decomposition (`t1_ledger9876.txt`, ledger-mash), boot f219 → combat f378 = **159f**:

| sub-phase | frames | len | what (evidence) |
|---|---|---|---|
| module boot | f219→~282 | ~63 | `$5D9B` D5→00 f223 (`CF:FB9A`); the CD/0000 open window / HUD slide begins |
| combatant load | f282→f290 | 8 | blocks staged: GATO `$6130`=0x4C(76) f287 (`FD:B472`), Crono `$5E30`=70 f288 (`FD:AE83`), Marle `$5EB0`=65; `$95D5` 00→FF f290 (`CC:E33F`) |
| pre-ramp settle | f290→f301 | 11 | HUD finishing its slide (settles ~f304, `battle_hud` Q6) |
| **the `$993A` ramp** | **f301→f377** | **~76** | **`$993A` accumulates 1→0x14 (20), +1 per 4 frames** (writers per HUD chain) — the fixed pre-combat delay |
| combat start | f378 | — | `$993A` reset to 0; first actor (Crono) pre-charged ready f381 |

**★ CORRECTION to §S1/§E (the ramp byte).** §S1/§E label the ramp "`$993B`". **It is `$993A`.** Separate-byte
monitor (`ct_probe --monitor "993A:1,993B:1" …input "100:20,0:420"`, clean): **`$993A` steps 01,02,…,0x14 at
f301,f305,…,f377 while `$993B` stays `00` the entire ramp**, then `$993A`→00 at f378 (`t1_bytes`/ramp run).
This is self-consistent with §S1's own prose ("`$993A` … during the intro it accumulated to 0x14, tick
paused") — the ramp IS the paused-tick accumulation of `$993A`, not a distinct `$993B` counter. (`$993B` is the
combat-phase logic-tick residual, 0-3, §S1/§C — it only wakes AFTER f378.)

**Corrected intro model.** Pre-combat = **159f**, of which our engine already covers the first **~82f** (the
`CT_BATTLE_INTRO=80` / HUD-settle-`frames>=82` window = boot+slide, real-frame per ADDENDUM 2 §C) but **omits
the ~77f `$993A` ramp** (f301→378) — a static pause AFTER the HUD has settled, BEFORE the ATB starts counting.
That ~77f is the whole of the caller's "INTRO 80 vs 159" delta. Fix = extend the pre-combat hold to ~159 real
frames (e.g. `CT_BATTLE_INTRO ≈ 157`, or keep the 82f HUD slide and add a ~77f settled-HUD ramp before the
first ATB tick). Purely a per-frame gate; the ATB does not count during it (§S1).

## §K — T3: the victory hold (231/214f) = a FIXED celebration sequence + an A-skippable results hold + ~11f handoff

Captured from `gato_lowhp.state` (GATO 19 HP) mashing to the kill (`t3_victory.txt`, `t3_noA.txt`,
`t3_phases.txt`). Death→field-return spine:

- **kill**: GATO `$6130`→0 (`C1:ED12`); dead-flag `$614A`=0x80 (`C1:ED0C`, block+0x1D bit7); f951 alive-list
  `$AF05` 92→FF (`C1:B2C3`). `$A013`=0 throughout — the victory is **NOT** an action-freeze (its own frame loop).
- **field-ack**: `$0117` 01→**0001** (`C0:18C1`) — the module signals done.
- **handoff**: `$95D5`→0x80 (`C0:A730`) then `$0117`→0000 (`C0:3AB1`), field resumes.

**★ The 231 is INPUT-DEPENDENT (this is the parity fact).** Two runs, identical up to the kill (kill f950):

| run | input after kill | kill→field-ack | total |
|---|---|---|---|
| **mash-A** (continuous) | A every 16f | f950 → **f1164** | **214f** (≈ ledger 231f) |
| **stop-A** (release after kill) | none | f950 → **f1466** | **516f** |

So an A-press **SKIPS ~302f** of the victory: there is an **A-dismissable results/pose hold** (the
`victory = static pose until A` the battle memory notes). Sub-structure (`t3_phases.txt`): the module runs its
own frame loop (`$9E` frame-advance toggling) with a **writer phase-change at ~f1111** (`C1:0080` → `CF:FCFD`)
= dissolve/pose → results boundary. **Post-dismiss handoff is a fixed ~11f** (ack f1164 → `$95D5`→0x80 f1170 →
field-resume f1175).

**Honest parity decomposition of the 231:**
- **FIXED, non-skippable** (death DISSOLVE + fallen pose + victory pose/fanfare + results-window populate):
  the mash-A minimum **≈ 214f minus the ~11f handoff ≈ ~203 real frames**. **Our engine plays ~0 of this**
  (it waits for A immediately, then ends "instantly") — this is the missing victory time.
- **INPUT-DEPENDENT** (results/pose held until A): **0f with mash-A, ~+302f without**. Our engine's
  "hold-until-A" already models THIS half (directionally correct).
- **HANDOFF**: fixed **~11f** (`C0:18C1`→`C0:A730`→`C0:3AB1`).
- So for honest interactive parity: play a **~200f fixed celebration** first, THEN the A-wait, THEN ~11f. The
  internal dissolve/pose/fanfare/results split = the victory-RENDER subsystem (`battle memory`: generic
  DISSOLVE + fallen-dead + `static pose until A`) = a boundary; the fixed ~200f + the A-skip + 11f handoff are
  the pinned, implementable envelope.

## §L — T4 SCOUT: `wait_then_battle.state` = FOREST scenario-2 ambush, **ACTIVE mode** (not Wait), auto-triggers on wait

User save `aux crono decompile/wait_then_battle.state` (s9x, gzipped). Decoded (direct WRAM):

- **loc `$0100` = 0x13 = 19 (the FOREST, scenario 2)**, storyline `$7F0000` = 0x08, party `$2980` = **00 01 80**
  (Crono + Marle + empty), `$027C` = 3, player = obj2.
- **`$2990` = 0xC4 → bit6 = 1 = ACTIVE mode** (per `battle_atbmode.md` §1.4: bit6=1 ACTIVE / bit6=0 WAIT;
  cross-checked: `gato_press_action`=0xC4 Active, `gato_wait`=0x84 Wait). **So this is an ACTIVE-mode save — the
  "wait" in the filename is a FIELD-TRIGGER recipe, not the ATB mode.** (Consistent with §1.5: every in-game
  save is Active unless WAIT was chosen at the new-game selector.)
- **Trigger recipe:** load it, **wait (no input) ~280 frames → a battle auto-starts** (`$0117` 21→01 f279-280;
  party HP `$5E30`=70/`$5EB0`=65 reload f337). The clean `base_loc19` (teleport entry) does **not** auto-battle,
  so the trigger is the s9x state's own captured encounter moment, not the scene. **Enemies spawned = Beetle
  ×2 (id `0x8D`) + Avian Chaos (id `0x19`)** (from `$AF0D` f336 = `FF 8D 8D FF FF 19 …`, names decoded from ROM
  `CC/6500`). The state's STALE `$AF0D` carries 3× **Hetake** (id `0x5E`) from the prior forest fight.
- **NOT a Wait-mode state** → no Wait-mode battle sequence captured from it (the Wait-parity repro remains
  `saves/battle_re/gato_wait.state`, `$2990`=0x84). This IS a useful **ACTIVE multi-enemy** forest repro —
  complements the single-enemy GATO ledger and directly exercises the freeze-ledger's OPEN "Multi-enemy /
  big-party" residue (simultaneous-action freeze layouts).
- **Converted** (s9x → lake, spliced onto `base_loc19.state` loc19/story8; `s9x_to_lake.py`) → saved
  **`saves/battle_re/forest_wait_ambush_active.state`** (verified loc=19, story=8, `$2990`=0xC4; field render =
  the forest clearing with Crono+Marle). Repro: `ct_probe --from saves/battle_re/forest_wait_ambush_active.state
  --input "0:400"` (battle boots ~f280) — or `--peek … --btnscript "0:60"` for the field render.

## §M — OPEN (this addendum)
- **GATO attack-table decode:** `$9877` values pinned (0x31/0x34/0x10) + the `$AE93` switch + writer PCs;
  the ROM records at `CD/4926`/`CD/4F26` + the `C1/46xx` special path are NOT byte-decoded (the anim-script
  contents / why each length) — the bank-CC AI+anim interpreter, the standing S4 boundary. Span LENGTHS are
  observed/pinned; their internal makeup is the anim engine.
- **`$AE93` per-turn logic:** toggled by `C1:AD08` (an AI-interp instruction); the exact rule (turn-parity vs
  hit-reactive) is un-decoded — mash gives A,B,B; HOLD gives A,special,A,special. Boundary.
- **Victory internal split** (dissolve vs fallen-pose vs fanfare vs results-populate within the ~200f fixed
  part): the phase boundary at ~f1111 is pinned; the finer frame split is the victory-render subsystem (boundary).
- **T4 battle full roster/positions** (Beetle×2 + Avian Chaos): enemy IDs pinned; the exact spawn script /
  is-it-scripted-vs-random and per-enemy stats not pursued (out of the pacing scope).

---

# ADDENDUM (T3) — the "3px lead teleport": staging reads the MAINTAINED on-screen-Y byte `$0A80,X`, our port re-derives it from the float `fy` (no trigger snap; the fix is field-side)

Closes the 10751d7 "our staged seat0 fineY=03, lead ~179 vs oracle 176" residue (user: *"crono teleports
slightly"*). Disasm of the ROM staging + oracle monitor; connects to `battle_stance.md` ADDENDUM 8 (T1) which
proved **battle screenAnchor = 16·rel + fine**, so a 3px error in the staged `fine` shifts the battle anchor 3px.

## §T3.1 — the ROM staging math (C0/041A-044D, `dis.py 0400`): rel from the TILE byte, fine from the SCREEN byte
```
041A LDA $1D0A / LSR / STA $DB      ; oTX = arena origin ($1D0A>>1, the settled cam scroll in tiles)
0420 LDA $1D0E / LSR / STA $DD      ; oTY
0428 LDA $97 / TAX                   ; X = lead-PC field-obj index ($97; =0 in gato_press_action)
042D LDA $1801,X / SBC $DB / STA $29B0   ; relTileX = $1801,X (obj TILE-X byte) - oTX
0437 LDA $0A00,X / AND #0F / STA $29B2   ; fineX    = $0A00,X (obj ON-SCREEN-X pixel) & 0x0F
0440 LDA $1881,X / SBC $DD / STA $29B1   ; relTileY = $1881,X (obj TILE-Y byte) - oTY
044A LDA $0A80,X / AND #0F                ; fineY    = $0A80,X (obj ON-SCREEN-Y pixel) & 0x0F
```
So `rel` comes from the object's **maintained TILE coordinate** (`$1801`/`$1881`) and `fine` from its
**maintained ON-SCREEN pixel** (`$0A00`/`$0A80`). Together `16·rel + fine = (objTile − camTile)·16 +
(screenPixel & 15) = the object's on-screen pixel position` — exactly T1's `16·rel+fine = screenAnchor`. The
staging **reads the position bytes AS-IS — there is NO tile-snap / no `AND #$FFF0`** anywhere in the path. So a
**trigger-time snap is RULED OUT** (hypothesis b: no).

## §T3.2 — oracle: staging fires f221 with fineY=0, WHILE the camera is still panning
`ct_probe --monitor "29B0:5,97:1,0A80:6,1881:6" --from gato_press_action.state --input "100:8,0:300" --diff
--writer`: `$29B0` is written **`08 09 06 00 03`** at **f221** (writer **C0/045A**, the C0/038F→C0/0430 chain) →
relTileY=9, **fineY=0** (byte+3). Crucially the on-screen-Y byte `$0A80` **changes every frame f183→f221**
(writer C0/AB9F — the actor→screen transform) as the arena CAMERA settles into the battle framing; the staged
`fine` is the low nibble of that settling `$0A80` **at the trigger frame**. Oracle fineY here = 0 (the GATO lead's
on-screen Y lands tile-aligned at f221); the forest lead is NOT always 0 (staged seat0 fineY=7, `battle_stance.md`
A8.1) — `fine` is the true sub-tile screen position, not a constant.

## §T3.3 — OUR port re-derives fine/rel from the float `fy` (field.c:2508-2517) — that's where the 3px enters
```
r[0]=(ax>>4)-oTX;  r[1]=(ay>>4)-oTY;              // relTile from the FLOAT world pos ay = sc->fy[obj]
r[2]=(ax-camPx)&15; r[3]=(ay-camPy)&15;           // fine    from (float world pos - camera)
```
`ay = sc->fy[obj]` is our mover's **float** world Y (CtSceneActor `*y=(int)sc->fy[obj]`, ct_vm.c:1959); the ROM
reads the **integer** on-screen byte `$0A80,X` the sprite pipeline already computed. These disagree by ~3px: our
`(ay − camPy)` at the trigger frame ≠ the oracle's maintained `$0A80,X`. Because the camera is mid-settle at the
trigger (§T3.2), the residue is a **relative lead-screen-Y offset** — it originates in either our lead mover `fy`
or our camera `camPy` **at the exact staging frame**, not in the battle code.

## §T3.4 — VERDICT + where the fix belongs (NOT implemented here)
- **Mechanism:** the 3px is a **field-sim position residue**, surfaced because our staging **recomputes** the
  screen fine from the float `fy`+`camPy` instead of reading the **maintained integer on-screen coordinate** the
  ROM uses (`$0A00/$0A80` = our `CtActorScreenCoords` output) and the maintained tile coordinate the ROM uses
  (`$1801/$1881`). **NO trigger snap exists** (§T3.1 disasm). Not a walk-step-phase for the GATO fight either —
  it triggers from a STANDING lead (`gato_press_action`, A-press), so the residue is the lead's resting
  screen-Y in our field-sim vs the oracle's (the camera-settle-at-trigger is the prime suspect, §T3.2: `$0A80`
  is still moving at f221; `ct-field-camera-scroll-model` flags cam-seek/settle as an open field pin).
- **Fix location = the FIELD SIM, one of:**
  (i) **stage fine/rel from the same maintained coords the renderer/ROM use** — `fineX/Y = CtActorScreenCoords(obj)
  & 0x0F` (the integer on-screen pixel), `relTile` from the tile coordinate — so the staged position matches
  what is drawn, dropping the float residue; **or**
  (ii) make the field mover + **camera settle** byte-match the oracle at the trigger frame (the deeper field-sim
  pin the task defers to "the field-sim pins separately").
- **NOT the fix:** the battle staging FORMULA (a faithful port) and the battle ANCHOR (correctly consumes
  `16·rel+fine`, T1). The 3px must be corrected upstream in the field position/camera, not by nudging the anchor.

## 2026-10-19 -- no iteration head after the victory or the defeat

C1/8133's defeat (C1/8143) and C1/817A's victory (C1/8186) leave the loop: no C1/8120 (`INC $0027`) follows. The port's tick wrapper
ran the next head on the victory's frame (CtBattleTick, `!r` after the victory's return 0). MEASURED, the rule's cartridge (the
playthrough's leg 20, the first forest ambush): `$0027` stops at DE on f3005; the port's went on to DF, and since `$0027` is never
reset between fights, the second ambush's turn scanner (C1/82A2 / C1/8313, odd iterations) ran two frames off the cartridge's --
Crono's command window 4 frames late, the enemies' due turns ahead of it.
