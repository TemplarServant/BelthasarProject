# BATTLE ATB MODE — the new-game "Battle Mode" (Active/Wait) selector + the Wait freeze gate

Derives the two things the user asked for ("When starting the game, it is possible to choose the ATB mode, active or
wait"): **(1)** the selector screen at new-game start — where it sits, its layout, its navigation, and the
persistent flag byte it writes; **(2)** what that flag does in battle — the exact consumer PC and the ATB-freeze
rule, proven in BOTH modes.

Every claim cites a ROM disasm address (`tools/dis.py <fileoffset>`, fileoffset = (bank−0xC0)<<16 | addr) or an
oracle observation. Writer PCs from `--monitor --writer` are the PC of the instruction AFTER the store retires.

Source state: `aux crono decompile/active_time_battle_selector.state` (snes9x). Decoded: **loc $0100 = 0, story
$7F0000 = 0, party $7E2980 = 00 80 80 (Crono only), Crono name buffer $2C00.. = all-zero (not yet named), CPU in
bank C2 at C2/81CD (the menu mainloop frame-wait)** — i.e. the earliest point of a new game.

---

## PART 1 — DERIVATION

### 1.1  The selector IS menu screen 0x0D (menu "mode 3"), the new-game setup screen

The battle-mode selector is not a field scene — it is the C2 **menu** system in a dedicated mode. In the source
state `$0A00 = 3` (menu-mode word) and `$0067 = 0x0D` (screen id).

- Mode→screen map `C0/8076` (`C2/8076`): `LDX $0A00 / LDA $C2808F,X / STA $67`. Table `C2/808F` (raw bytes)
  `0E 00 0C 0D 0F 07 0B` ⇒ **mode 3 → screen `0x0D`** (byte-exact: table[3]=0x0D matches the observed `$0067`).
- Screen dispatch `C2/8279`: `LDA $67 / ASL / TAX / LDA $C28292,X / … / JMP ($00)`. Handler word-table
  `C2/8292`: entry `0x0D` (offset 0x1A) = **`C2/E1E3`**. (Same table pins the field screens: 0=99CA ribbon,
  1=9C29 Equip, 2=AA34 Item, 3=B353 Tech, 4=BF35 Config, 5=C621 Form, 6=CED4 Save — matches `menu_phase2.md`.)

**Flow position — selector THEN Crono naming, both inside this mode-3 screen 0x0D.** `saves/crononame.state`
(the Crono naming keyboard, rendered to confirm — portrait + "Crono" field + "Enter a name, then push the Start
Button.") is ALSO `$0A00=3 / $0067=0x0D`, but at a later sub-phase (`$0D13=0x7F` vs the selector's `$0D13=5`) and
with the battle mode **already confirmed** (`$2990=0xC4`, bit6 set) whereas the selector is pre-confirm
(`$2990=0x84`, bit6 clear). So the new-game "mode 3" screen sequences **ATB Battle-Mode select → Crono naming**,
and the ATB select is FIRST (its bit6 is written before naming runs). Relative to our engine's `--newgame`
(scene0 naming → pan → wake-up, `ct-field-newgame-boot`): the selector goes **immediately before the naming step**.
Launcher: `C0/0D10 JSR C0/25A5 / ROL×3 / AND #03 / JSL C2/8000` (`C0/0D18`) opens the menu with the computed mode
(the generic special-menu launch+resume wrapper, `C0/0D00-0D3B` = `0B4E/0B64/0B75 / C0/01A5` reload chain).

### 1.2  Layout / provenance (render)

Rendered byte-correct by splicing the s9x state's own VRAM into a lake state (offset method, §Artifacts):
`atb_vram.png`. The screen shows:

- **Title** "Battle Mode"; **subtitle** "Active Time / Battle ver.2".
- **Two vertically-stacked options** with a hand cursor: **`☞ ACTIVE`** (row 0, cursor default) and **`WAIT`**
  (row 1).
- **Description box** (bottom): for ACTIVE = **"Enemies will attack even if you're not ready!"** — i.e. the box
  literally states the Active semantics (enemies act while you are in a menu = no freeze).

The chrome/text are the screen-0x0D widgets drawn by handler `C2/E1E3` into the C2 menu BG (menu font/CHR, same
system as `menu_phase2.md`); the option/description strings are the screen's own data (not derived to a ROM
string address this pass — a boundary, not needed for the flag).

### 1.3  Navigation (empirical, `--monitor` on the spliced state)

- Cursor cell = **`$0054`** (working index; `$0055` = pre-move snapshot, matches `menu_phase2.md`).
- **Up/Down toggle the row**: UP `$0054` 0→1 (writer C2:EA4A), DOWN 1→0 — a 2-item vertical list (ACTIVE row0 /
  WAIT row1). Nav via the `$7E9380` neighbor graph + `C2/E267`→`($E298,X)` dispatch (the screen's own mover).
- **Left/Right are INERT** here: they only latch the transient direction cell `$005A` (Right→1, Left→2, writer
  C2:EA4A) which is cleared next frame (C2:E9F4); `$0054`/`$2990` do not change.
- **A confirms** (writes the flag, §1.4, then substate `$0068`→3 + cursor SFX #74); B is the menu cancel.

### 1.4  ★ THE FLAG WRITE — A-confirm writes `$2990` bit6 (C2/E24C)

Handler `C2/E236` (A/direction dispatch for screen 0x0D): `JSR C2/E984` (action decode → `$0D1D` bit7=A/bit6=B)
… `BIT $0D1D / BMI ->E24C`. The A-confirm body `C2/E24C` (disasm, byte-exact):

```
C2/E24C  LDA $54        ; cursor row (0=ACTIVE, 1=WAIT)
C2/E24E  LSR A          ; carry = cursor bit0
C2/E24F  LDA #40        ; 0x40 = bit6 mask
C2/E251  BCS ->E258     ; cursor odd (row1) → clear
C2/E253  TSB $2990      ; cursor 0 (ACTIVE) → SET   $2990 bit6 = 1
C2/E256  BRA ->E25B
C2/E258  TRB $2990      ; cursor 1 (WAIT)   → CLEAR $2990 bit6 = 0
C2/E25B  LDA #03 / STA $68 / LDA #74 / JSR C2/8255 / JMP C2/83CA
```

**⇒ `$2990` bit6 is the persistent battle-mode bit: ACTIVE (row0) → bit6=1; WAIT (row1) → bit6=0.**
Empirically confirmed: A on the default (row0) drove `$2990` **0x84 → 0xC4** (writer **C2:E256** = the instruction
after `TSB $2990` @E253), i.e. bit6 set = ACTIVE. `$0068`→3 pulsed the same frame.

`$2990` is the packed menu-config low byte (`$2990-92`; also editable later via the Config screen 4 — unpacker
`C2/92F4` maps `$2990` bit6 → editable cell `$0D89`, and re-packs `$0D89`→`$2990` bit6, `C2/9308`/`C2/9379`;
battle-speed = `$2990 & 7`, `battle_loop.md §B`). So the selector and the Config "Battle:" setting write the SAME
bit.

### 1.5  Corroboration + ruling out `$7F00BA`

Scan of all 51 states (`aux crono decompile/*.state`): **`$2990` bit6 = 1 (ACTIVE) in every in-game save; = 0
only in the pre-confirm selector state.** So the persistent battle mode lives in `$2990` bit6.

`$7F00BA` (the `battle_menu_synthesis.md §1.12` "$A0A6 = $7F00BA & 1" candidate) = **0 in ALL 51 states** and is
**NOT** the Active/Wait flag. Boot `CF/FBB8`: `LDA $7F00BA / AND #01 / STA $A0A6 / BEQ ->FFBE1` — when 0 it SKIPS
the per-PC block `LDA $7F00B4,X / STA $A09B,Y` (indexed by `$2980,Y & 7`) and the `$A0A7 = ($7F00BA&2)^2` write.
This is a **per-character control setting** ("Custom"/per-PC), gated OFF for this player; unrelated to the global
Active/Wait. No C1 (battle-loop) code reads `$2990` directly (ROM scan: `$2990` referenced only in C2 config, CC
boot-unpack, FD speed) — the loop reads the unpacked battle flag `$99E1` instead (§1.6).

### 1.6  ★ Battle boot unpack — `$99E1 = ($2990 bit6) inverted` (CC/E387)

Battle-init routine (bank CC, reached inside the `JSL CD/0000`→C1 boot; runs SEP #20) at `CC/E387` (byte-exact):

```
CC/E387  LDA $2990 / AND #$40 / EOR #$40 / STA $99E1   ; $99E1 = ($2990 & 0x40) ^ 0x40
CC/E391  LDA $2992 / TAX / LDA $CCFBC5,X / STA $9F20    ; (message-speed table)
CC/E39C  LDA $2991 / AND #$40 / STA $A0D4               ; (other config bits …)
         LDA $2991 / AND #$80 / STA $A0D5
         LDA $2990 / AND #$80 / STA $A0DB
```

**⇒ `$99E1` is the in-battle mode flag: `$2990` bit6=1 (ACTIVE) → `$99E1 = 0x00`; bit6=0 (WAIT) → `$99E1 = 0x40`.**
Empirically confirmed at boot (writer **CC:E391**): `gato_press_action` (`$2990=0xC4`) → **`$99E1=0`**;
`gato_wait` (`$2990=0x84`) → **`$99E1=0x40`**.

### 1.7  ★ THE FREEZE GATE — `$99E0 != 0` skips the ATB decrement (C1/81CA)

Scheduler dispatch `C1/1153` (per logic-tick). The mode is copied into the live gate `$99E0` depending on the
PC's command sub-state `$95DB`:

```
C1/1167  JSR C1/1115 / LDA $95DB
C1/116D  BEQ ->11184                 ; $95DB==0 (top Att/Tech/Item ring, idle-await) ↓
C1/1170  DEC A / BNE ->1117B
C1/1172  LDA $99E1 / STA $99E0 / JMP C1/1320   ; $95DB==1 (target/tech select) → $99E0 = mode
C1/117B  LDA $99E1 / STA $99E0 / JMP C1/143D   ; $95DB==2 (item/list select)   → $99E0 = mode
C1/11184 STZ $A09A / STZ $99E0                 ; top ring → $99E0 = 0
```

Consumer `C1/81CA` (in the ATB turn-processing block, adjacent to the countdown decrement):

```
C1/81CA  LDA $99E0
C1/81CD  BEQ ->81EC        ; $99E0==0 → run the ATB tick (81EC→18221→C1/823C..:
                           ;            LDA $AF27,X / DEC A / STA $AF27,X  = the per-actor countdown −1)
         ; $99E0 != 0 :  loop C1/B3F9 ×3 slots then  JMP C1/8345  — SKIPS the whole decrement (FREEZE)
```

Secondary consumer `C1/3714` (`LDA $99E0 / BNE`) gates a per-slot status/animation tick loop
(`$974A,X`/`$9897,X`) the same way. `$99E0` is also STZ'd on the no-current-actor path (`C1/1748`) and enqueue
(`C1/1C08`).

**NET RULE:**
- **ACTIVE** (`$99E1=0`): `$99E0` is always 0 ⇒ the ATB **never freezes** — enemy and other-PC countdowns keep
  running in every menu. (This is the engine's current model.)
- **WAIT** (`$99E1=0x40`): `$99E0` = 0x40 **only while a PC has a sub-menu open** (`$95DB ∈ {1,2}` = Tech list /
  Item list / target-select) ⇒ **the ATB freezes for ALL combatants**. The **top Att/Tech/Item command ring
  (`$95DB=0`) does NOT freeze** even in Wait.

### 1.8  ★ Dual-mode empirical proof (identical input, opens Item submenu at f447)

`ct_probe --monitor "99E0:1,95DB:1,95DC:1,AFAB:8" --from <state> --input
"100:5,0:5,100:5,0:400,20:3,0:12,20:3,0:12,100:3,0:150" --diff` (`AFAB` region: byte1 `$AFAC` & byte6 `$AFB1`
are the live ticking countdowns):

**WAIT (`gato_wait`, `$99E1=0x40`):**
```
f434  99E0=00 95DB=00 95DC=02 AFAB=0008…08FF   (top ring, timers ticking freely)
f447  99E0=00 95DB=02 …           AFAB=0003…03FF   (A → Item submenu opens, $95DB=2)
f449  99E0=40 95DB=02 …           AFAB=0003…03FF   ← $99E0 = mode; timers FROZE at 0x03
      (no further change through 150 idle frames)
```
**ACTIVE (`gato_press_action`, `$99E1=0`):** identical input, Item submenu opens at f447, but
```
f447  99E0=00 95DB=02  AFAB=0003…03FF
f449  99E0=00 95DB=02  AFAB=0002…02FF     ← keeps ticking
f452  99E0=00 95DB=02  AFAB=0001…01FF
f454  99E0=00 95DB=02  AFAB=0000…00FF
```
Also note (both modes): while navigating the top ring `$95DB=0` (f381→f447, `$95DC` 0→1→2), the timers ticked
freely — confirming the top ring never freezes.

---

## PART 2 — IMPLEMENTATION SPEC

Two pieces: **(A)** the selector in `--newgame`; **(B)** the Wait gate in `CtBattleTick` (`src/battle.c`).

### (A) Battle-Mode selector (new-game, before naming)

- **Where:** front of the `--newgame` sequence, immediately before the Crono naming step (both are the ROM's
  mode-3 screen 0x0D; the selector's bit is written before naming runs).
- **Layout:** title "Battle Mode" / "Active Time Battle ver.2"; two vertical options **ACTIVE** (row0, default
  cursor) / **WAIT** (row1) with a hand cursor; a description box. (Chrome = C2 menu system; render reference
  `atb_vram.png`.)
- **Nav:** Up/Down toggle the row (2 items); A confirms; Left/Right inert; B = cancel.
- **Flag:** on A-confirm, set the persistent battle-mode bit = **`$2990` bit6** — **ACTIVE(row0)→bit6=1**,
  **WAIT(row1)→bit6=0** (ROM `C2/E24C` TSB/TRB `$2990`). Store it however our config is baked; the battle module
  must be able to read bit6 (or an equivalent `battleModeWait` bool). Default outcome (confirm on row0) = ACTIVE.
- The Config screen (screen 4, later milestone) edits the SAME bit (cell `$0D89`, pack/unpack `C2/92F4`).

### (B) Wait freeze gate in `CtBattleTick`

- **Derive the mode flag at battle boot:** `battleMode99E1 = (cfg2990 & 0x40) ? 0x00 : 0x40` (ROM `CC/E387`).
  Equivalently `waitMode = ((cfg2990 & 0x40) == 0)`.
- **Per-tick gate (mirror `C1/1153`→`C1/81CA`):** compute `freeze = waitMode && aPcSubmenuOpen`, where
  `aPcSubmenuOpen` = our equivalent of `$95DB ∈ {1,2}` — a PC is in a **Tech list, Item list, or target-select**
  sub-menu (NOT the top Att/Tech/Item command ring, and NOT idle/no-actor).
- **When `freeze`:** SKIP the per-tick ATB countdown decrement for **all** combatants (the `timer[c] -= 1` step,
  ROM `C1/823C`), i.e. hold every `$AF27`/ATB gauge this tick. Also skip the per-slot status/anim tick
  (`C1/3714`) if we model it. Do NOT freeze the intro delay, the command/nav input, or the HUD.
- **When not `freeze` (ACTIVE always, or WAIT at the top ring):** run the ATB normally (current behavior).
- This is purely a per-tick GATE on the existing scheduler; it does not change the damage/turn-order/outcome
  logic. Our current model already implements the ACTIVE case (nothing freezes) — Wait only ADDS the skip.

**Addresses to hardcode / cite:** selector flag `$2990` bit6 (write `C2/E24C`); boot unpack `$99E1 =
($2990&0x40)^0x40` (`CC/E387`); scheduler `$99E0` source (`C1/1172`/`C1/117B` submenu = mode, `C1/1187` ring/idle
= 0); freeze gate `C1/81CA` (`$99E0!=0` ⇒ skip decrement `C1/823C`, jump `C1/8345`); secondary `C1/3714`.

---

## Scratchpad artifacts produced (paths for the caller; NOT committed)

Dir: `%TEMP%/claude/C--Users-Icaro-Ferris-Desktop-Crono-Trigger-Decompile/e5608df4-34bf-4aa1-b49d-5255ac6f07ac/scratchpad/`
- **`gato_wait.state`** — GATO pre-battle state patched to **WAIT** (`$2990` bit6 cleared, file offset
  0x45+0x2990=0x29D5: 0xC4→0x84). Reusable Wait-mode battle repro. Compare vs repo-root `gato_press_action.state`
  (ACTIVE).
- `atb_sel.state` / `atb_menu.state` — s9x WRAM+CPU spliced onto candy / menu bases (WRAM-logic runnable; render
  is the base's).
- **`atb_vram.state`** + **`atb_vram.png`** — selector RENDER (s9x VRAM spliced at lake offset 131537; offset
  verified by chunk-search). **`crononame.png`** — the naming step (same screen 0x0D).
- Helpers: `s9xread.py` (dump WRAM bytes from an s9x state), `scanall.py` (config/flag scan over all 51 states).
- `ct_probe.exe` (oracle copy).

Repro command lines:
- Flag write: `ct_probe --monitor "0067:1,2990:3,0068:1" --from atb_sel.state --input "0:6,100:2,0:15" --diff --writer` → f7 `$2990` 84→C4 (C2:E256).
- Boot mode: `ct_probe --monitor "99E1:1" --from {gato_press_action|gato_wait}.state --input "$(A-mash)" --diff --writer` → 0 vs 40 (CC:E391).
- Freeze proof: the §1.8 command on both states.

## Open questions / boundaries

- The screen-0x0D sub-phase machine (`$0D13`=5 battle-mode vs 0x7F naming; `$0068` substates) is only
  partially decoded — enough to place the selector and prove the flag, not a full re-implementation of the
  new-game menu state machine. The exact ROM-string addresses of "ACTIVE"/"WAIT"/description are un-pinned
  (captured by render, not traced to a bank — a boundary; not needed for the flag).
- `$95DB` sub-mode taxonomy: 1→`C1/1320`, 2→`C1/143D` both freeze; the precise "which submenu = 1 vs 2"
  (tech/item/target permutations) was not exhaustively enumerated — Item = `$95DB=2` observed. The freeze rule
  (`$95DB≠0` in Wait) is mode-agnostic to the exact value.
- `$7F00BA`/`$A0A6`/`$A0A7` (per-PC "Custom" controls) are ruled OUT as the Active/Wait flag but their own
  battle behavior (the per-PC `$A09B` load) is out of scope here.

---

## ADDENDUM — implementation (PART A: the selector) + 3 new oracle facts

Implements PART 2(A) in `src/data/atbsel.h` (baked) + `src/field.c` (flow) + `src/ct_vm.c`/`.h` (the flag write
+ its carry across scene transitions). Three facts left open above are now pinned, all fresh `--monitor`
probes on `saves/battle_re/atb_vram.state` (the same source state PART 1 used):

**1. Up/Down both TOGGLE the row (resolves the odd S1.3 phrasing).** Isolated presses from the fresh
(row0) state: `--input "0:6,10:2,0:10"` (UP alone) → `$0054` 0→1, writer **C2:EA4A**. `--input
"0:6,20:2,0:10"` (DOWN alone) → **also** `$0054` 0→1, same writer. From row1, DOWN→0 and UP→0 (both
directions land on the other row). This is the expected property of a 2-item **circular** neighbor graph
(`$7E9380`/`C2/E267`, S1.3): a single step in EITHER direction around a 2-node cycle lands on the other
node, so UP and DOWN are behaviorally identical here — not a bug, not asymmetric. `src/field.c` implements
this as a plain toggle (`atbSelRow = 1 - atbSelRow`) on either edge, byte-exact to both observed directions.

**2. B is INERT (was "if unprobed, mark OPEN" — now probed).** `--monitor
"0054:1,0067:1,0A00:1,2990:1,0068:1,0D13:1" --input "0:6,1:2,0:20" --diff` (B = bit 0x01): **zero** rows
printed after f0 — all six watched cells (cursor, screen id, menu mode, the config flag, the substate byte,
the sub-phase byte) unchanged over 26 frames. `src/field.c` wires no key for B on this screen (a documented
no-op, matching the ROM exactly — implementing "B does nothing" as no code IS the byte-exact implementation).
Left/Right re-confirmed inert the same way (`$0054`/`$2990` unchanged under `40:2`/`80:2`).

**3. The WAIT row's description text: "Take your time to choose items".** `atb_vram.state` is WRAM+CPU
logic-runnable (§1.3 already established this for the cursor); pressing Down and re-peeking renders row1
for real. The description box is a VWF (variable-width font) reveal that is still animating at f20 (534 px
differ between f20 and f30) but fully settled by f30 (0-diff f30 vs f40+). Both rows now byte-exact-baked:

| row | option | description (oracle-rendered, VWF-settled) |
|---|---|---|
| 0 (default) | ACTIVE | "Enemies will attack even if you're not ready!" |
| 1 | WAIT | "Take your time to choose items" |

**4. A-confirm re-confirmed both directions.** Row0 A: `$2990` 0x84→0xC4, writer **C2:E256** (the
instruction after `TSB $2990`) — matches PART 1 exactly. Row1 A: no `$2990` *value* change logged (the
source state's `$2990=0x84` already has bit6 clear, so `TRB` is a value-preserving no-op there) — the ROM
disassembly quoted in §1.4 (`C2/E258 TRB $2990`) is authoritative for the clear direction; `gato_wait.state`
(bit6 forced clear via file patch, §Artifacts) is the WAIT-mode battle repro either way.

### The bake (`src/data/atbsel.h`, `tools/_gen_atbsel.py`)

Unlike the naming screen (dynamic typed content → font/tile composite, `gen_namewin.py` + `drawNameScreen`),
this screen has exactly **two static states** (cursor row 0 or 1), so it bakes as a literal indexed-color
**full-frame capture** — guaranteed byte-exact because it IS the oracle's own composited framebuffer, not a
re-derivation. Measured: only **76 distinct RGB colours** across both frames, and only a **168×81 rect**
(x40–207, y93–173 — the description box + hand cursor) differs between row0 and row1. Bake = a shared
76-colour palette + ROW0 as a full 256×224 indexed frame + ROW1 as that same rect's real pixels (an
"override patch"); reconstructing ROW1 = ROW0+patch is verified **0/57344 mismatch** against the real ROW1
capture (`tools/_gen_atbsel.py --verify`). Captures: ROW0 `--btnscript "0:8"` (0-diff vs 20f — stable
immediately); ROW1 `--btnscript "0:6,20:2,0:40"` (settles f30, +margin per fact 3 above).

### The flow (`src/field.c`)

There is no separate field-VM op for this screen (open question above) — our engine's naming screen is
ALREADY the field-VM's own modal-overlay pattern (triggered by `CtSceneNaming`, rendered as a full-frame
takeover). The selector is implemented the same way, gated to appear strictly BEFORE naming ever acts:
`atbGate = newGame && !atbSelDone && CtSceneNaming(f.vm) >= 0` — while true, naming's own trigger is held
back (`nameId` forced -1) and every OTHER place that assumed "`nameId<0` means input is free" (talk/interact,
the camera d-pad read, player movement, the idle-blink timer — 4 sites) got an added `&& !atbGate` guard so
a press used to navigate/confirm the selector can't ALSO fire the field's normal talk/move handling in the
same frame. Once A confirms, `atbSelDone` latches (the selector never shows again this process) and naming
proceeds exactly as before. Interactive mode mirrors the naming screen's own fade choreography (field fades
out → selector fades in → live input → fades out → field fades back in, reusing the same `fade`/`fadeBri`
machinery and `FADE_STEP` — the transition TIMING itself isn't oracle-derived, only the settled frame is, so
this reuses an already-established value rather than inventing a new one). Headless (`--selftest`) skips
straight to "live" and auto-resolves the same frame it would otherwise wait for input (see CT_ATBSEL below).

### The flag write + its carry (`src/ct_vm.c`/`.h`)

`CtSceneSetAtbMode(sc, active)` — the C2/E24C TSB/TRB semantics, byte-exact (only bit6 touched). Persistence
across a scene transition needed its OWN carry mechanism, `CtSceneSetCarryConfig`/`g_carryConfig2990`,
mirroring the existing `CtSceneSetCarryParty`/`g_carryParty` (party persists across doors) — **but with one
difference**: party carry only re-captures at a field→field door (`pendKind==1||3`); it does NOT capture at
a scene→overworld hop (`pendKind==2`), because the overworld engine carries no `$7E` at all and (empirically)
nothing needed party to survive that specific leg before now. `$2990` DOES need to survive it (the new-game
flow is scene0 → **overworld** (scripted 1000 AD pan) → loc271 → loc2), so field.c captures the byte at
BOTH transition points (immediately before each `fieldFree`), chaining scene0 → overworld → loc271 → loc2
unbroken. Verified via `CT_DUMPWRAM_AT` at the settled bedroom scene (f2000, post-naming): `$2990=0x00`
(WAIT) / `0x40` (ACTIVE) — the original selector choice, four scene-loads later.

### Test hooks + regression (`docs/TESTING.md`, `tools/regress_checks.json`)

`CT_ATBSEL=0|1` — headless force (mirrors `CT_NAMEAS`, but with one difference: unset also resolves, to
row0/ACTIVE, since — unlike a name — this screen has a well-defined ROM default (fact 4 / S1.4), so every
PRE-EXISTING `--newgame` invocation that never heard of `CT_ATBSEL` is unaffected). `CT_ATBSELSHOT=<path>`
+ optional `CT_ATBSELSHOT_AT` — holds the modal open (skips auto-confirm) and dumps the composed frame +
its fnv32, mirroring `CT_MENUSHOT`/`CT_BATTLESHOT`. Four regression checks: `field-atbsel-shot-active`/
`-wait` (render, byte-exact vs the oracle per fact-set above) and `field-atbsel-flag-active`/`-wait` (the
live WRAM byte read back post-write). Existing checks unaffected — `field-newgame-pan` never sets
`CT_ATBSEL`, always takes the ACTIVE/default path, and its pinned signatures (`ngintro: pan complete`,
`selftest: scene 2`) are unchanged; `field-battlewait-gato` (the Part B freeze-gate check, pre-existing,
73dfa42) is likewise unaffected — it drives `$2990` directly via `CT_BATTLE_WAITMODE`, not through this
selector.

---

## ADDENDUM 2 — CGRAM CORRECTION (selector palette was wrong: dark title + tan hand)

User defect: the baked selector rendered the "Battle Mode" title too DARK and the `☞` hand cursor TAN
instead of WHITE. Root cause: **the oracle capture `saves/battle_re/atb_vram.state` itself carried
STALE CGRAM.** The PART-2A gen spliced the s9x WRAM+CPU+VRAM into a lake base but NOT the CGRAM, so the
palette came from the base state. The entries the idle menu loop does NOT re-upload each frame kept the
base's stale values — exactly the title-text gradient and the hand-cursor OBJ palette.

FIX (zero invention — every colour from the user's ORIGINAL s9x): parsed the TRUE CGRAM out of `aux
crono decompile/active_time_battle_selector.state` (the PPU block, `tools/parse_ppu_state.cgdata`, 256
BGR555 words) and spliced it into the lake CGRAM region. **Lake CGRAM offset = 197073** (= VRAM_OFF
131537 + 0x10000; snes_handleState layout header 69 + WRAM 0x20000 + CPU 29 + DMA 171 + PPU-scalar 196
→ VRAM @131537, then CGRAM = 256 LE words). VRAM already matched the s9x byte-exact (0 diff); only CGRAM
was wrong.

CGRAM diff (wrong→right, the two UI elements the user cited):
- **Title text** — CGRAM `0x40..0x4F` (16-step anti-aliased font gradient). Wrong = dark irregular greys
  ending `0x4F=0421` (8,8,8 ≈ black). Right = smooth blue-white gradient ending **`0x4F=7FDE`
  (247,247,255)**, `0x4E=6F39` (206,206,222), `0x4D=66D6` (181,181,206) … → title LIGHT.
- **Hand cursor** (`☞`, an OBJ sprite → OBJ palette `0x80+`). Wrong `0x8D=367C` (231,156,107 tan),
  `0x8E=4F5F` (255,214,156), `0x8F=67FF` (255,255,206) → tan/yellow hand. Right **`0x8D=7FFF`
  (255,255,255)**, `0x81=0x82=7FFF`, `0x8E=318D` (107,99,99), `0x8F=14A5` (41,41,41) → WHITE hand, dark
  outline.
- (73 of 256 CGRAM entries differed in total; the rest are palette slots not referenced by any visible
  pixel — the render changed ONLY in the title + cursor region, bbox x82..172 y32..104, geometry identical.)

Re-baked `src/data/atbsel.h` (`tools/_gen_atbsel.py`): palette **76→82** colours, patch bbox UNCHANGED
(168×81 @x40..207,y93..173 → only palette-driven pixels moved), row1 = row0+patch reconstruction
**0/57344**. Runtime composite re-verified **0/57344** vs the corrected `--peek` captures. Re-pinned the
render fnvs: ACTIVE **E2924E14→2412D18F**, WAIT **A72A002F→004CA91C** (`field-atbsel-shot-active`/
`-wait`). The flag checks (`field-atbsel-flag-*`) are palette-independent and untouched. Corrected
`saves/battle_re/atb_vram.state` overwritten on disk (gitignored/local). Cross-checked against the s9x
state's own embedded screenshot (the `SHO` block, 512×478 RGB = the live screen the user saw): light
title, white hand. Repro: overwrite the state via `parse_ppu_state.cgdata`→CGRAM@197073, then `python
tools/_gen_atbsel.py --verify` (0/57344).

## ADDENDUM 3 -- the flow is the ROM's now (2026-09-25)

* §1.3's "cursor SFX #74" is WRONG: C2/E25F `LDA #74 / JSR C2/8255` is the task KILL by bitmask (C2/8255: bit n ->
  `STZ $B0 + 8n`), slots 2, 4, 5, 6 -- the description typewriter (C2/FA49 spawns it at X = #0010, slot 2) stops on the
  A frame. The confirm is silent; the fade-in task (slot 0) survives it.
* The gate is not "new game": C2/E1ED routes to the selector when `$299F | $71` is zero, and C2/E40E sets $299F to
  $0D13 (7F / 1D) on every naming. The port's `newGame && !atbSelDone` gate, its 16-frame fades and the selftest
  auto-resolve are gone: the screen runs in the menu shell (disasm/notes/naming_screen_sfx.md, "The screen is the
  SHELL's"), graded frame by frame from a blank cartridge's power-on (tools/naming_cmp.py boot / bootfast).
* The typewriter's empty-box bake is applied inside the description window only ($FF:C429 op 04 at tile (2,16), 28 x
  10; BG1/BG2 V scroll 1023): it used to cover the whole patch rect and put the WAIT hand on ACTIVE for the pen's first
  steps.
