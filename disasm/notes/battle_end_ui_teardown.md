# BATTLE-END UI TEARDOWN — the target cursor + the command menu when the LAST enemy dies

User report (translated from Portuguese): *"when killing the last opponent with an attack already in the queue, while another character
is selecting whom to attack, the pointer is not vanishing (nor the action selection menu)"* — a
queued attack kills the LAST enemy while ANOTHER PC sits in target-select; the target hand stays on screen and
so does the Att./Tech/Item menu.

Companion to `battle_queue_death.md` (which pinned the NON-last case: the cursor RE-RESOLVES to the next living
enemy) and `battle_end.md` (the celebration/regroup that follows). Nothing here was implemented; `src/` and git
are untouched by this pass.

---

## HEADLINE — the canonical mechanism (2 distinct moments, 2 distinct ROM routines)

1. **The target HAND closes on its own, ~3 frames after the last enemy leaves `$AF02`** — and NOT because the
   battle ended. The per-frame target-select handler **`C1/1561`** re-runs the cursor/eligibility builder
   **`C1/1F79`** every logic tick; `C1/1F79` rebuilds the compacted living-target list `$99C0`/`$A62D` and, if
   **every `$A62D[0..10]` entry is `0xFF`**, sets the "no eligible target" flag **`$9613 = 0xFF`** (`C1/1FD9`).
   `C1/1561` then branches at `11567` into the same tail a **B-cancel** uses: `STZ $9614` / `STZ $960E` /
   **`DEC $9609`** (`C1/1569`, `11577`-`1157D`) — target-select CLOSES. Fully general: it is an *empty target
   set* rule, not a victory rule.
2. **The command MENU, the ready-queue and the current actor are torn down by the VICTORY handler `C1/356D`**,
   ~45 real frames later (after the dying enemy's dissolve drains). The victory scan `C1/8167` (all `$AF02`
   == FF) ends with `LDA #08 / JSR C1/0003` (`C1/8186`) → dispatch table `C1/0051`[8] → **`C1/356D`**, which
   calls **`C1/1BAA` once per party seat 0/1/2** (`C1/358B`, `C1/3593`, `C1/359B`, one frame apart via
   `JSR C1/007E`) and then forces **`$95D5 = 0xFF`** (`C1/35A1-35A3`) + `JSR C1/0C2D`.
   `C1/1BAA` on the seat that is the current chooser writes: `STZ $9609`, `STZ $960E`, `STZ $9614`,
   `STZ $95DB`, `STZ $99E0`, `$A6DF = 0xFE`, `$A6D9[seat] = 0xFF`, `DEC $A6DE`, `$A6DD`/`$95D5` = next queued
   seat or `0xFF`, `JSR C1/1C3A`, `STZ $A862`.
3. **The ROM gates the STATE, never the render.** The hand is an OBJ the builder simply stops emitting once
   `$9609 == 0` (proven: its OAM slot is parked at y=240); the command strip is a BG tilemap that **`C1/1C3A`**
   restores verbatim from ROM **`D1/5800`** (0x180 bytes → `$0B40`) whenever `C1/0C2D` sees `$95D5 < 0`
   (`C1/0C71-0C76`) or the queue head changed (`C1/0C4E-0C59`). There is **no `$AF02`/"all dead" test anywhere
   in the render path** — clear the state and both renders follow.
4. **Our engine performs NONE of these writes.** `CtBattleTick` step G (`src/battle.c:4370-4382`) sets
   `glb[0x1EC]=0` + `h->victoryHold=1` and returns; `$9609`, `$9614`, `$95D5`, `$95DB`, `$A6D9/$A6DD/$A6DE`
   are left exactly as the chooser left them, so `battleTargetCursorRender` (`src/field.c:3853`, gate
   `mem[0x9609]!=1`) and the command strip (`bhPhase = (mem[0x95D5] < 3)`, `src/field.c:7108`) keep drawing
   for the whole celebration + results + handoff.

---

## 1. ROM ROUTINES + RAM MAP

### 1.1 The end-check and its two branches (`dis.py 18120 0x60 1 0`, x=0 for the 16-bit `CPX`)

```
18127  LDA #03 / 18129 JSR C1/0003        ; dispatch A=3 -> C1/106E (flee housekeeping + ONE frame wait)
1812C  JSL FD/ACFD
18130  JSR C1/B223
18133  LDA $24                            ; dp $24 = party-wiped
18135  BEQ ->18167                        ;   0 -> the VICTORY scan
;--- DEFEAT (dp $24 != 0) --------------------------------------------------------------
18137  JSR C1/EA9D / JSR C1/B3BB
1813D  LDA #01 / STA $7F01EC              ; outcome 1 = defeat
18143  JSR C1/B7F2 / $A10E=1 / (8x: LDA #03 / JSR C1/0003) / STZ $A10E
1815F  LDA #09 / JSR C1/0003              ; dispatch A=9 -> C1/354D   (**no C1/1BAA, no $95D5 write**)
18164  JMP C1/8423
;--- VICTORY ---------------------------------------------------------------------------
18167  TDC / TAX
18169  LDA $AF02,X / CMP #FF / BNE ->1818E ; a live slot?  ($AF15,X BIT #40 = hidden -> treat as dead, 1818E-18193)
18170  INX / CPX #0008 / BCC ->18169
18175  JSR C1/B3BB                         ; write surviving-enemy flags back to the field staging $29C6+12k
18178  TDC / STA $7F01EC                   ; outcome 0 = VICTORY (D=0 proven, battle_menu_synthesis §1.8)
1817E  LDA #FF / STA $A5A9                 ; (writer C1:8183 -- the frame marker used in the timeline below)
18183  JSR C1/EA9D                         ; clear the PCs' transient status bytes $5E4B..$5E53 (keeps +0x1D bit7)
18186  LDA #08 / JSR C1/0003               ; *** dispatch A=8 -> C1/356D = THE UI TEARDOWN ***
1818B  JMP C1/83BB
```

Raw bytes verified: `C1/8130 = 20 23 B2 A5 24 F0 30 20 9D EA 20 BB B3 A9 01 8F EC 01 7F …`,
`C1/8167 = [33 ROM bytes at 0x18167: python tools/dis.py 0x18167 33]
20 03 00 4C BB 83`.

`C1/0003` → `JMP C1/0045` → `PHA/PHX/PHY / ASL A / TAX / JSR ($0051,X)`. Table `C1/0051` decoded
(`23 00 19 1B AA 1B 6E 10 58 40 86 29 6D 00 DD 1F 6D 35 4D 35 …`): **A=8 → `C1/356D`**, A=9 → `C1/354D`,
A=3 → `C1/106E`.

**Reachability (exhaustive ROM scan):** the byte pattern `A9 08 20 03 00` occurs **exactly once in the whole
ROM, at `C1/8186`**, and there is **no** `JSR/JMP/JSL C1/356D` anywhere. So the full teardown is
**VICTORY-ONLY** — see §5.

### 1.2 `C1/356D` — the victory UI teardown (raw `[38 ROM bytes at 0x1356D: python tools/dis.py 0x1356D 38] …`)

```
1356D  JSL CD/0009
13571  INC $A0FE                        ; (writer C1:3574 -- the frame marker used below)
13574  LDX #0007 / 13577 STZ $98A7,X / DEX / BPL      ; clear $98A7..$98AE   (writer C1:357A)
1357D  LDX #0002 / STX $94
13582  JSR C1/30B6 / DEC $94 / BPL ->13582            ; 3x (per-PC, $94 = 2,1,0)
13589  STZ $A1                          ; --- seat 0
1358B  JSR C1/1BAA                      ;     queue-remove + (if current) UI TEARDOWN
1358E  JSR C1/007E                      ;     WAIT ONE FRAME (INC $9E / spin JSL CD/0036 until $9E==0)
13591  INC $A1 / 13593 JSR C1/1BAA / 13596 JSR C1/007E   ; --- seat 1
13599  INC $A1 / 1359B JSR C1/1BAA / 1359E JSR C1/007E   ; --- seat 2
135A1  LDA #FF / STA $95D5              ; *** NO CURRENT ACTOR ***      (writer C1:35A6)
135A6  JSR C1/0C2D                      ; HUD/menu refresh -> C1/1C3A restores the default strip
135A9  LDA $2989 / AND #01 / BEQ …      ; (celebration continues: $5DF1/$5DF2 per PC, $5DDD/$5DDE, …)
```

### 1.3 `C1/1BAA` — remove one seat from the ready queue, and if it is the CURRENT chooser tear the menu down

```
11BAA  TDC / TAX / LDA $A1 / STA $80 / TAX
11BB1  LDA $A6D9,X / BPL ->11BD6        ; seat ENQUEUED? (FF = not) -> 11BD6
11BB6  … (not enqueued) compact the seat out of the pending list $95D6..$95D8, DEC $95DA, RTS
11BD6  DEC $A6DE                        ; queue depth--                          (writer C1:1BD9)
11BD9  CMP $95D5 / BEQ ->11BED          ; is this seat the CURRENT chooser?
11BDE  (not current) $A6D9[seat]=FF, $A6DF=FE, BRA ->11C33
11BED  LDA $A86B / BEQ ->11BFC          ; a Tech/Item submenu was stashed by the target-select entry?
11BF2  DEC A / STA $95DB / STZ $A86B / STZ $A09A       ;   restore it (it is re-zeroed 9 bytes later)
11BFC  STZ $9609                        ; *** TARGET-SELECT CLOSED ***           (writer C1:1BFF)
11BFF  STZ $960E                        ; ***                                    (writer C1:1C02)
11C02  STZ $9614                        ; *** cursor slot = 0 ***                (writer C1:1C05)
11C05  STZ $95DB                        ; *** command sub-mode = 0 ***           (writer C1:1C08)
11C08  STZ $99E0                        ; *** ATB-freeze gate cleared ***        (writer C1:1C0B)
11C0B  LDA #FE / STA $A6DF              ; strip "dirty" sentinel                 (writer C1:1C10)
11C10  LDA $A1 / TAX / LDA #FF / STA $A6D9,X            ; *** DEQUEUE this seat *** (writer C1:1C18)
11C18  TDC / TAX / 11C1A LDA $A6D9,X / STA $A6DD / STA $95D5 / BPL ->11C33   ; next queued seat
11C23  INX / CPX #0003 / BNE ->11C1A
11C2B  LDA #FF / STA $A6DD / STA $95D5  ; *** none left -> current actor = 0xFF *** (writer C1:1C33)
11C33  JSR C1/1C3A                      ; restore the default HUD strip (below)
11C36  STZ $A862 / RTS                                                          (writer C1:1C39)
```

### 1.4 The two RENDER consumers (state-gated, no all-dead test)

* **Command strip / boxes.** `C1/1C3A` = `TDC/TAX / LDA D1/5800,X / STA $0B40,X / INX / CPX #0180 / BNE` —
  a verbatim 0x180-byte ROM→WRAM copy of the DEFAULT (enemy-name) strip tilemap. Callers: `C1/0C59` and
  `C1/0C76` (both inside `C1/0C2D`), `C1/138F`, `C1/14DB`, `C1/1C33` (the teardown), `C1/1FB9`.
  `C1/0C2D` (called every logic tick from `C1/10E3`) does:
  ```
  10C4E  LDA $A6DD / CMP $A6DF / BEQ ->10C61 ; queue head changed?
  10C56  STA $A6DF / JSR C1/1C3A / JSR C1/0299   ;   -> repaint the strip
  10C71  LDA $95D5 / BPL ->10C79 / JSR C1/1C3A   ; *** no current actor -> DEFAULT STRIP ***
  ```
* **Command ☞ hand (BG3 tiles 0x60-0x63).** Written only when `$9609 == 0`:
  `C1/08CB LDA $9609 / BNE ->108E4` then `STA $0CC0,X`/`$0CC2,X`/`$0D00,X`/`$0D02,X`; likewise
  `C1/0E66 LDA $9609 / BNE ->10E8D` writing `$A6E1/$A6E3/$A721/$A723 + Y`.
* **The whole PC menu subsystem is gated on `$95D5 >= 0`:** `C1/1153` (input, from the tick `C1/10E3`)
  `LDA $95D5 / BPL ->1115F / JMP C1/179C`, and `C1/17DD` (the other tick member) `LDA $95D5 / BPL ->117E5 /
  JMP C1/1AFE`. Inside both, `LDA $9609 / BEQ` selects command-menu vs target-select. So **one write
  (`$95D5=0xFF`) disables the entire command/target machine**.
* **Target HAND OBJ** — 16×16 tile 0x000 pal1 prio3 built by `C1/1F79` on target-select entry
  (`battle_actionfx.md` P5.2); it is emitted only while target-select is live (OAM proof §3.2).

### 1.5 The "no eligible target" flag `$9613` (`C1/1F79`, `dis.py 11F79 0x80 1 0`)

```
11F79  LDA $95D5 / TAX / STX $960F
11F80  STZ $9613 / STZ $960C / STZ $960A / STZ $A64F / STZ $A6D8
11F8F  LDX #000B / LDA #FF / 11F94 STA $99C0,X / STA $A62D,X / DEX / BPL   ; FF-fill both lists
11F9D  LDA $960D / BPL ->11FBC   ; (bit7 = "hide the command strip while choosing": stash $95DB->$A86B, STZ
                                 ;  $95DB, $A6DF=FE, INC $A09A, JSR C1/1C3A)
11FBC  LDA $960D / AND #7F / CMP #21 / BCC / LDA #20 / ASL A / TAX
11FC9  JSR ($1FF8,X)             ; the per-target-class builder; $960D=07 -> C1/20A9:
                                 ;   LDX #000B -> $80 (limit) ; LDX #0003 (first ENEMY combatant) ; INC $960A
                                 ;   -> the shared loop C1/2045-2099: skip if $96F5,X==0 / $9FF7,X bit7 /
                                 ;      $A0A8,X / $A09B,X ; else compact X into $99C1,Y ; JSR C1/27C5 copies
                                 ;      $99C1,X -> $99C0,X (11 entries, writer C1:27D2) ;
                                 ;      $A62D = $99C0[$9614] (C1/2085, writer C1:2088)
11FCC  TDC / TAX
11FCE  LDA $A62D,X / BPL ->11FDC       ; ANY non-FF entry -> leave $9613 = 0
11FD3  INX / CPX #000B / BNE ->11FCE
11FD9  STA $9613                       ; *** all 11 entries FF -> $9613 = 0xFF ***   (writer C1:1FDC)
11FDC  RTS
```

### 1.6 The target-select frame handler `C1/1561` (entered from the tick: `C1/1153` `LDA $9609 / BEQ / JMP C1/1561`)

```
11561  JSR C1/1F79                 ; REBUILD every tick (§1.5)
11564  LDA $9613
11567  BPL ->1156E                 ; a target exists -> normal nav/confirm/cancel
11569  STZ $9614                   ; *** NO TARGET: fall straight into the cancel tail ***
1156C  BRA ->11577
1156E  LDA $EE / AND #08 / BEQ ->115E8   ; (B pressed? else the normal path)
11574  JSR C1/1B55
11577  STZ $9614                   ;                                        (writer C1:157A)
1157A  STZ $960E                   ;                                        (writer C1:157D)
1157D  DEC $9609                   ; *** TARGET-SELECT CLOSED (1 -> 0) ***  (writer C1:1580)
11580  LDA $A86B / BEQ ->115C8     ; came from a Tech/Item submenu? restore $95DB and, for Tech,
11595  … LDA D1/5BD0,X / STA $0B40,X (0x180) / $9920=FF / INC $99E2        ;   repaint the list strip
```

### 1.7 RAM map (battle UI)

| addr | meaning | cleared by | value after victory |
|---|---|---|---|
| `$9609` | target-select open (0/1) | `C1/157D` (no-target) **and** `C1/1BFC` | 0 |
| `$960E` | target-select aux | `C1/157A`, `C1/1BFF` | 0 |
| `$9613` | 0 = a target exists / **0xFF = eligible set EMPTY** | rebuilt each tick `C1/1F79` | 0xFF |
| `$9614` | cursor POSITION in the compacted list `$99C0` | `C1/11569/11577`, `C1/1C02` | 0 |
| `$99C0[0..10]` | compacted LIVING-target combatant idx, FF-padded | rebuilt `C1/27C5` (writer C1:27D2) | all FF |
| `$A62D[0..10]` | resolved target(s) = `$99C0[$9614]` | rebuilt `C1/2085` (writer C1:2088) | all FF |
| `$960A / $960C / $960D` | target-class scratch / all-targets flag / target CLASS (07 = enemies) | rebuilt while open | 01 / 00 / 07 (stale, inert) |
| `$95D5` | current chooser seat (0xFF = none) | `C1/1C30` then `C1/35A3` | 0xFF |
| `$95DB` | command sub-mode (0 cmd / 1 Tech / 2 Item) | `C1/1C05` | 0 |
| `$95DC` | command row cursor | **not touched** | unchanged |
| `$A6D9[0..2]` | ready queue (seat idx or FF) | `C1/1C15` per seat | FF FF FF |
| `$A6DD` | queue head | `C1/1C1D`/`C1/1C2D` | 0xFF |
| `$A6DE` | queue depth | `C1/1BD6` (DEC per seat) | 0 |
| `$A6DF` | last-rendered head (strip dirty sentinel) | `C1/1C0B` = 0xFE → `C1/0C56` = 0xFF | 0xFF |
| `$99E0` | ATB Wait-freeze gate | `C1/1C08` | 0 |
| `$A862` | (menu scratch) | `C1/1C36` | 0 |
| `$A5A9` | victory marker `#FF` | `C1/8180` | 0xFF |
| `$A0FE` | victory-handler entered counter | `C1/3571` INC | 1 |
| `$99D4[0..3]` / `$99D8` | pending-action actor stack / depth | **NOT touched by the teardown** | unchanged |
| `$95D6..$95D9` / `$95DA` | pending-to-enqueue list / count | only via the `C1/1BB6` compaction | unchanged |

---

## 2. ORACLE FRAME TIMELINE — the user's exact scenario, built from scratch

**State:** `saves/battle_re/forest_battle_3hetake.state` (3 Hetakes, 14 HP each, slots 0/1/2;
`$AF02 = 5E 5E 5E FF`, Crono current). **No state patching was needed.**

**Script (real frames, A = 0x100):**
```
0:20,100:6,0:10,100:6,0:10,100:6,0:10,100:6,0:586,100:6,0:12,100:6,0:20,100:6,0:700
```
Round 1 kills Het0 + Het1 with the two PCs' first turns; then both ATBs refill (Crono ready f575, Marle f643);
the A at f660 opens Crono's target-select, the A at f678 CONFIRMS (his attack is queued, `$95D5`→Marle), and
the A at f704 opens **Marle's** target-select on the LAST living Hetake. Crono's queued attack then kills it.

```
ct_probe --monitor "9609:1,9613:1,9614:1,960A:1,960C:1,960D:1,960E:1,95D5:1,95DB:1,95DC:1,99D4:4,99D8:1,\
95D6:4,95DA:1,A6D9:3,A6DD:1,A6DE:1,A6DF:1,A862:1,99E0:1,AF02:4" \
  --from saves/battle_re/forest_battle_3hetake.state \
  --btnscript "0:20,100:6,0:10,100:6,0:10,100:6,0:10,100:6,0:586,100:6,0:12,100:6,0:20,100:6,0:700" \
  --diff --writer
```

| frame | event | evidence (value + writer PC) |
|---|---|---|
| f660 | Crono target-select | `$9609`=1 (C1:12B9), `$A62D`=05, `$99C0`=`05 FF FF FF` |
| f678 | Crono CONFIRMS the last Hetake | `$9609`→0 (C1:1629), `$95D5`→01 Marle (C1:1735), `$A6D9`=`FF 01 FF` (C1:172A) |
| f683 | his committed attack ANIMATES | `$A013`→1 (C1:40A9) — the whole scheduler freezes |
| **f705** | **Marle opens TARGET-SELECT during the freeze** | `$9609`=1 (**C1:12B9**), `$A62D`=05 (C1:2088), `$99C0`=`05 FF FF FF` (C1:27D2), `$9613`=00, `$95D5`=01 |
| **f816** | **the LAST enemy's curHP hits 0** | `$60B0`→`0000` (**C1:ED12**), `$A013`→0 (C1:41BD) |
| **f817** | **delist** | `$AF02`→`FF FF FF FF` (**C1:B2C3**). `$9609` still 1, `$9614`=0, `$A62D`=05 — 1 frame stale |
| **f820** | **THE HAND CLOSES** (next logic tick, `$993A` consumed C1:10EB) | `$99C0`→`FF FF FF FF` (C1:27D2), `$A62D`→FF (C1:2088), **`$9613`→FF (C1:1FDC)**, `$9614`=0 (C1:157A), `$960E`=0 (C1:157D), **`$9609`→0 (C1:1580)**. `$95D5` STILL 01 — the command menu is still up |
| f862 | **victory scan fires** (45 frames after the delist) | `$A5A9`→FF (**C1:8183**), `$A0FE`→1 (**C1:3574**), `$98A7`(C1:357A) |
| **f863** | **`C1/1BAA` teardown** (seat1 = the current chooser) | `$9609`=0 (C1:1BFF), `$960E`=0 (C1:1C02), `$9614`=0 (C1:1C05), `$95DB`=0 (C1:1C08), `$99E0`=0 (C1:1C0B), `$A6DF`→FE (C1:1C10), `$A6D9`→`FF FF FF` (C1:1C18), `$A6DD`→FF (C1:1C30), **`$95D5`→FF (C1:1C33)**, `$A6DE`→00 (C1:1BD9), `$A862`=0 (C1:1C39) |
| f865 | the explicit force + strip repaint | `$95D5`=FF (**C1:35A6** = `C1/35A1 LDA #FF / STA $95D5`), `$A6DF`→FF (**C1:0C59** = `C1/0C56 STA $A6DF` then `JSR C1/1C3A`) |
| f864+ | **the battle scheduler is DEAD** | `$993A` free-runs 01,02,03… (writer CF:E748) and is **never consumed by `C1/10EB` again** |
| f866→f868 | the repainted strip reaches VRAM | menu still visible at f866, **gone at f868** (peeks below) |

**The 45-frame delist→teardown gap is reproducible.** Second, independent run (pure mash to victory,
`--btnscript "$(python -c \"print(','.join(['100:6,0:10']*40)+',0:500')\")"`): delist **f720** (`$AF02`→all FF,
C1:B2C3) → victory scan **f765** (`$A5A9`→FF, C1:8183, `$A0FE`→1, C1:3574) = **45 frames again**.
Cross-check: `battle_dissolve.md` S2 measures the enemy death-dissolve at **death→gone ≈ 43 peek frames**
(pal-swap ~+2, hold 9, dither 31, remove ~4) — i.e. the end-check runs after the dying enemy has finished
dissolving. (The exact wait instruction inside the turn tail is **OPEN**, §6.)

---

## 3. THE OAM / FRAMEBUFFER PROOF

Three `--peek`s on the SAME script (the peek timeline matches the monitor exactly — verified by reading
`$9609/$9613/$95D5/$AF02` out of each `.wram` sidecar):

```
PRE="0:20,100:6,0:10,100:6,0:10,100:6,0:10,100:6,0:586,100:6,0:12,100:6,0:20,100:6"
ct_probe --peek saves/battle_re/forest_battle_3hetake.state lc_812.raw --btnscript "$PRE,0:102"
ct_probe --peek saves/battle_re/forest_battle_3hetake.state lc_822.raw --btnscript "$PRE,0:112"
ct_probe --peek saves/battle_re/forest_battle_3hetake.state lc_870.raw --btnscript "$PRE,0:160"
```

| | `$9609` | `$9613` | `$95D5` | `$A62D` | `$99C0` | `$AF02` | `$A6D9` |
|---|---|---|---|---|---|---|---|
| f812 | **01** | 00 | 01 | 05 | `05 FF FF FF` | `FF FF 5E FF` | `FF 01 FF` |
| f822 | 00 | **FF** | 01 | FF | `FF FF FF FF` | `FF FF FF FF` | `FF 01 FF` |
| f870 | 00 | FF | **FF** | FF | `FF FF FF FF` | `FF FF FF FF` | `FF FF FF` |

### 3.1 OAM diff (`.oam` sidecar: 512 low + 32 high + 1024 CGRAM; x bit8/size from the high table)

```
f812  #0 x=152 y=119 tile=000 pal=1 prio=3 size=1     <-- THE TARGET HAND, on screen
      #1 x=-133 y=108 / #2 x=-141 y=132 / #3 x=-133 y=132  (tile 000 pal1, parked off-screen — P5.2)
f822  #0 x=-104 y=240 / #1 x=-133 y=240 / #2 x=-141 y=240 / #3 x=-133 y=240
      ^^^ all four tile-000 pal1 slots at y=240 = OFF-SCREEN (SNES OBJ y>=224 does not render)
f870  no tile-000 pal1 sprite at all
```
So the hand OBJ is **gone the frame after `$9609` clears** — the builder simply stops emitting it; nothing in
the OBJ path tests `$AF02`.

### 3.2 Framebuffer (`tools/ct.py fb2png`)

| frame | HUD strip |
|---|---|
| f812 | **command strip**: `Hetak` box + **Att. / Tech / Item** + `Crono 70: 8` / `Marle 65:12` + bars |
| f822 | **still the command strip** (`$95D5`=1) — only the arena hand is gone |
| f862 / f866 | still the command strip (state cleared at f863; the VRAM upload lags) |
| **f868 / f870** | **the ENEMY-NAME strip**: `Hetake` box + `Crono 70: 8` / `Marle 65:12` + bars, **no Att./Tech/Item, no ☞ hand**, and the dead enemy sprite is gone |

That post-victory strip is exactly the layout our `CtBattleHudRender(phase=0)` (`ctBattleHudMapEnemy`) already
bakes — i.e. **no new art is needed**, only the `$95D5` write.

---

## 4. WHAT OUR ENGINE DOES INSTEAD (and why the defect is visible)

### 4.1 The victory branch writes no UI state — `src/battle.c:4370-4382`

```c
if (!anyEnemy) {                          // VICTORY (C1/817A -> $7F01EC=0): enter the hold
  h->glb[0x1EC] = 0; h->endReason = 1;
  h->victoryHold = 1;
  battleBuildResults(h);
  h->actActive = 0; h->actAttacker = -1; h->actTarget = -1; s[0xA013] = 0;
  h->fxAtkPal = -1; h->fxAtkPose = -1; h->fxFlashPal = -1; h->flashTarget = -1; h->flashLeft = 0;
  h->fxKnockTarget = -1; h->fxKnockDX = 0; h->fxKnockDY = 0; h->fxEnemyRearup = 0;
  battleTrace(h, "victory"); return 0;
}
```
No `$9609`, `$960E`, `$9614`, `$95DB`, `$99E0`, `$95D5`, `$A6D9/$A6DD/$A6DE/$A6DF` write — the ROM's
`C1/1BAA` + `C1/35A3` block is entirely absent.

`battleRemoveDead` (`src/battle.c:1046`) only purges **dead PC seats** from the queue/FIFO, and the enemy
delist (`src/battle.c:1765`, `s[0xAF02 + (tgt-3)] = 0xFF`) touches nothing UI-side (correctly — the ROM's
death path doesn't either, `battle_queue_death.md` A.2). And `$9614` is never re-validated while target-select
is merely HELD (`battle_queue_death.md` A.5) — with the last enemy dead there is no living slot to move to
anyway, so the ROM's answer is *close*, not *re-resolve*.

### 4.2 The renders faithfully mirror the ROM — so they keep drawing the stale state

* `battleTargetCursorRender` — `src/field.c:3852-3853`: `if (!mem || mem[0x9609] != 1) return;` then the hand
  over `mem[0x9614]`. Called unconditionally at `src/field.c:7090` (interactive) and `4102` (shot path), with
  **no `victoryHold` gate**.
* `battleBracketRender` — `src/field.c:3797-3802`: returns if `$95D5 >= 3` or `$9609 != 0`. With `$95D5`
  left at the chooser's seat and `$9609`=1 it correctly draws nothing here — but it WOULD wrongly draw the
  bracket for the sibling case (chooser at the plain command menu when the last enemy dies).
* Command strip — `src/field.c:7108` `int bhPhase = (bs2[0x95D5] < 3) ? 1 : 0;` (same at `4108`/`7204`).
  This IS the ROM's `C1/0C71` rule; with `$95D5` left at 1 the strip stays in COMMAND phase forever.

### 4.3 Live repro in our engine (headless, `build/ct_field.exe`)

```
CT_BATTLE_SCRIPT="a:1,n:8,a:1,n:200,a:1,n:8,a:1,n:600,a:1,n:20,a:1,n:25,n:400,a:1,n:60,a:1,n:60,a:1,n:1500" \
CT_BATTLE_TRACE=1 build/ct_field.exe --assets ct_assets.dat --scene 19 \
    --battletest saves/battle_re/forest_pre.raw.wram
```
```
f1500 pc-act        cur=1 A013=1 9609=0 …  AF02=FFFF5EFF     (Crono's committed attack animates)
f1549 target-select cur=1 A013=1 9609=1 …  AF02=FFFF5EFF     (Marle opens target-select DURING the freeze)
f1625 act-end       cur=1 A013=0 9609=1 …  AF02=FFFFFFFF     (the LAST enemy delisted)
f1628 victory       cur=1 A013=0 9609=1 …  AF02=FFFFFFFF     <-- $95D5 AND $9609 UNCHANGED
f4000 end-debug     cur=1 A013=0 9609=1 …  AF02=FFFFFFFF     <-- still, ~2400 frames later
```
Compare the oracle: `$9609`→0 at delist+3, `$95D5`→0xFF at delist+46.

Render proof (`CT_BATTLESHOT=… CT_BATTLESHOT_AT=1700 CT_BATTLESHOT_HUD=1`, i.e. 72 frames into the
celebration, `battleshot: f1700 fnv=7E3BEA50`): the frame shows **the Att./Tech/Item command strip** and **the
white target hand floating over the dead slot-2 position** — precisely "the pointer is not vanishing
(nor the action selection menu)".

---

## 5. GENERALITY — which half is "battle over" and which is not

| teardown | trigger | scope |
|---|---|---|
| `$9609`/`$960E`/`$9614` close (`C1/1561` @`11567`) | **`$9613 == 0xFF` = the eligible-target list is EMPTY** (`C1/1F79` @`11FCC-11FD9`) | **GENERAL** — not a battle-end condition at all. It is the same tail a B-cancel takes. It fires for *any* reason the target set empties (last enemy killed by a queued ally attack, by a counter, by poison, by an enemy's own friendly-fire, …). NOT scene- or enemy-gated: `$960D`=07 → `C1/20A9` scans combatants 3..10 generically. |
| queue + `$95DB`/`$99E0` + `$95D5`=0xFF + strip repaint (`C1/1BAA` ×3 + `C1/35A3` + `C1/0C2D`) | reached **only** from `C1/8186 LDA #08 / JSR C1/0003` | **VICTORY-ONLY.** Exhaustive ROM scan: `A9 08 20 03 00` occurs exactly once (`C1/8186`); there is no other `JSR/JMP/JSL C1/356D`. The **DEFEAT** branch (`C1/8133 LDA $24 / BNE`) dispatches A=3 ×8 then **A=9 → `C1/354D`**, which does *not* call `C1/1BAA` and does *not* write `$95D5`. The **FLEE** branch (`C1/81F8-8218`) uses `LDA #08 / JSL CD/0021` — a different (graphics) dispatcher — and also never reaches `C1/356D`. |

Practical consequence for the port: implement the cursor close as a **general empty-target rule**, and the
queue/menu teardown on the **victory** path only. Defeat/flee end the module immediately (`JMP C1/8423` /
`C1/83BB`) and the field re-entry then overwrites `$95D5`/`$95DB` with `0x80` at `C0/A730`
(`battle_end.md` T2), so leaving them alone there is canonical.

**The ROM gates STATE, not RENDER** (asked explicitly): the OBJ builder stops emitting the hand because
`$9609` is 0 (OAM parked at y=240, §3.1); the strip is repainted from `D1/5800` because `$95D5 < 0`
(`C1/0C71-0C76`). No `$AF02`/"all enemies dead" test exists anywhere in either render path. **Do not add an
"if all dead then hide" render hack.**

---

## 6. IMPLEMENTATION SPEC (mandate-safe; every constant cited)

Two independent changes, both in `src/battle.c`. **No render change is required** — `battleTargetCursorRender`
(`field.c:3853`, `$9609`), `battleBracketRender` (`field.c:3800/3802`, `$95D5`/`$9609`) and the HUD phase
(`field.c:7108`, `$95D5 < 3`) already mirror the ROM's own gates exactly.

### 6.1 CHANGE A — close target-select when the eligible target set empties (mirror `C1/1F79` + `C1/1561`)

Add one helper and call it **per render frame, immediately after `battleAdvanceAction(h)`** in `CtBattleTick`
(that is where a span-end kill lands, `src/battle.c:4101`) — i.e. BEFORE the `victoryHold` early-return
(`src/battle.c:4168`) and BEFORE the `wasFrozen` return (`src/battle.c:4141`), so it also runs on the frozen
frames when the pointing PC is mid-freeze (the oracle case: Marle's cursor is open during `$A013`=1).

```
battleTargetSetRevalidate(h):                      // == C1/1F79's tail + C1/1561 @11564-1157D
  if (s[0x9609] != 1) return;                      //   only while target-select is HELD
  first = battleFirstEnemySlot(s);                 //   the compacted living list's head ($99C0[0], C1/27C5)
  if (first < 0) {                                 //   *** $9613 = 0xFF: eligible set EMPTY (C1/1FD9) ***
      s[0x9609] = 0;                               //     DEC $9609        (C1/157D)
      s[0x960E] = 0;                               //     STZ $960E        (C1/157A)
      s[0x9614] = 0;                               //     STZ $9614        (C1/11569 + 11577)
      return;
  }
  if (s[0x9614] >= 8 || s[0xAF02 + s[0x9614]] == 0xFF)   //   dead under the cursor but others live ->
      s[0x9614] = (uint8_t)first;                        //   RE-RESOLVE (battle_queue_death.md C.1, A.3)
```
Notes / boundaries:
* The `first < 0` branch is **the Lane-C fix**; the re-resolve branch is `battle_queue_death.md` C.1 and can
  land in the same helper (one rule, no duplication). Neither is victory- or scene-gated.
* Our `$9614` is a PHYSICAL enemy slot, the ROM's is a POSITION in the compacted `$99C0` list; the ROM's
  `$9614` is likewise zeroed on this path (`C1/11569`/`11577`), so writing 0 is byte-faithful either way.
* Do **not** touch `$960A/$960C/$960D`: the ROM leaves them stale (oracle f820-f870: `01 / 00 / 07`).
* Timing acceptance: the ROM closes on the first logic tick after the delist — oracle **delist f817 →
  `$9609`=0 f820** (3 real frames = one `$993A` tick). Running it per render frame lands within that window.

### 6.2 CHANGE B — the VICTORY queue/menu teardown (mirror `C1/356D` → `C1/1BAA` ×3 → `$95D5=0xFF`)

The ROM runs it **45 real frames after the `$AF02` delist** (measured twice: f817→f862 and f720→f765), i.e.
after the dying enemy's dissolve has drained (`battle_dissolve.md` S2 death→gone ≈ 43f; our
`CT_DISSOLVE_HOLD 9` + `CT_DISSOLVE_STEPS 32` model the same curve). Our step G enters `victoryHold=1`
**3 render frames after the delist** (engine trace: act-end/delist f1625 → victory f1628 — the same 3-frame
lag as the ROM's own tick), so the faithful gate is:

```
#define CT_BATTLE_END_TEARDOWN 43   /* ROM: $AF02 delist -> C1/356D = 45 real frames (oracle f817->f862 and
                                       f720->f765); our victoryHold starts at delist+3 => 45-3+1 = 43 */
```
In the `victoryHold` block (`src/battle.c:4168`, before the results/dismiss logic), fire **once** when
`h->victoryHold == CT_BATTLE_END_TEARDOWN` (a `h->endTornDown` latch, or the `==` itself since `victoryHold`
increments by 1 per real frame at `src/battle.c:4120`):

```
battleEndTeardown(h):                              // == C1/356D @13589-135A6
  for (seat = 0; seat < 3; seat++)                 //   $A1 = 0,1,2 ; JSR C1/1BAA (C1/358B,3593,359B)
      battleQueueRemoveTeardown(h, seat);
  s[0x95D5] = 0xFF;                                //   LDA #FF / STA $95D5      (C1/35A1-35A3)
  /* C1/35A6 JSR C1/0C2D = the HUD repaint; ours redraws every frame from $95D5, so nothing to do. */

battleQueueRemoveTeardown(h, seat):                // == C1/1BAA
  if (s[0xA6D9 + seat] == 0xFF) return;            //   11BB1 BPL: not enqueued -> (the $95D6 compaction,
                                                   //   11BB6-11BD5; our engine has no $95D6 pending list)
  if (s[0xA6DE] > 0) s[0xA6DE]--;                  //   DEC $A6DE                (C1/1BD6)
  if (seat != (int)s[0x95D5]) {                    //   11BD9 CMP $95D5 / BNE
      s[0xA6D9 + seat] = 0xFF; s[0xA6DF] = 0xFE;   //   11BDE-11BEB
  } else {
      s[0x9609] = 0;                               //   STZ $9609                (C1/1BFC)
      s[0x960E] = 0;                               //   STZ $960E                (C1/1BFF)
      s[0x9614] = 0;                               //   STZ $9614                (C1/1C02)
      s[0x95DB] = 0;                               //   STZ $95DB                (C1/1C05)
      s[0x99E0] = 0;                               //   STZ $99E0                (C1/1C08)
      s[0xA6DF] = 0xFE;                            //   LDA #FE / STA $A6DF      (C1/1C0B)
      s[0xA6D9 + seat] = 0xFF;                     //   DEQUEUE                  (C1/1C15)
      nh = battleQueueHead(s);                     //   11C18-11C2B scan $A6D9[0..2]
      s[0xA6DD] = s[0x95D5] = (nh >= 0) ? nh : 0xFF;   //  STA $A6DD / STA $95D5 (C1/1C1D/1C20, C1/1C2D/1C30)
      s[0xA862] = 0;                               //   STZ $A862                (C1/1C36)
  }
  /* also clear our host-side submenu latches so the Tech/Item panel cannot survive:
     h->techOpen = h->itemOpen = 0;  (the ROM's equivalent is $A86B/$A09A at C1/11BED-11BF9 + STZ $95DB) */
```

Ordering / hygiene notes:
* `$95D5 = 0xFF` must be written **after** the per-seat loop (the ROM's `C1/1BAA` may re-point `$95D5` at
  another queued seat; `C1/35A3` then forces FF). Doing it in one shot instead of the ROM's
  one-frame-per-seat spread (`JSR C1/007E` at `C1/358E/3596/359E` = `INC $9E` + spin) is user-visibly
  identical — the oracle's own seat writes all land in a single frame (f863) because only one seat is queued.
* Do **not** clear `$95DC` (the ROM does not) and do **not** clear `$99D4/$99D8` (our `schedAtk` FIFO's
  analog) — `C1/356D` leaves them alone; the FIFO is already inert because `victoryHold` freezes
  `battleDispatchScheduled` (`src/battle.c:3342`).
* Do **not** add a `victoryHold` gate to `battleTargetCursorRender` / `battleBracketRender` / `bhPhase`:
  §5 proves the ROM gates the STATE. Once A + B write the state, all three renders switch by themselves
  (the hand vanishes, the bracket stays absent, `bhPhase` falls to 0 = the enemy-name strip that the ROM
  repaints from `D1/5800`).

### 6.3 Acceptance checks (headless, no new capture needed)

1. **The Lane-C repro must flip.** With the exact script in §4.3 the trace must read
   `f1625 act-end … 9609=1` → `f1628 victory … cur=1 9609=0` (Change A: the hand closes on the victory frame,
   within the ROM's 3-frame window) and, ~43 frames later, `cur=-1` for every subsequent line
   (Change B). Today both stay `cur=1 9609=1` to f4000.
2. **`CT_BATTLESHOT_AT=1700` + `CT_BATTLESHOT_HUD=1`** on the same script must lose the arena hand AND show
   the enemy-name strip (compare against oracle `lc_870.png`; today `fnv=7E3BEA50` shows both).
3. **The pinned MASHA fights must stay byte-identical.** In `field-battleloop-gato` / `field-battlewait-gato`
   the masher has always confirmed before the kill (`$95D5`=0xFF, `$9609`=0 at victory — verified in the
   forest MASHA trace, `f996 victory cur=-1 9609=0`), so Change A is a no-op and Change B writes values that
   are already FF/0. `python tools/regress.py` must stay green with no `--pin`.
4. **`battle_queue_death.md` A.3 must still hold**: with 3 Hetakes and only Het0 killed under the cursor,
   Change A's re-resolve branch must move `$9614` to the next living slot and `$9609` must stay 1.

---

## 7. OPEN

* **The 45-frame delist→`C1/356D` gap is MEASURED (twice), not decoded.** The end-check block
  `C1/8123-8135` is reached from the turn processor after the action executor's tail; which instruction
  waits out the dissolve was not isolated. Next step: `--monitor` the executor's own state (`$A013`, `$B18B`,
  `$99D4/$99D8`, `$AE5A`) with `--writer` across f816-f862, or disassemble backwards from `C1/8123` to the
  routine entry (the region needs manual M/X re-segmentation — linear `dis.py` desyncs at `C1/8150-8166`).
  Until then `CT_BATTLE_END_TEARDOWN 43` is an oracle-measured interval, corroborated by
  `battle_dissolve.md` S2's independent 43-frame death→gone curve — **not** an eyeballed constant, but its
  MECHANISM is unproven, so it may differ for a different enemy's dissolve length.
* **Only the forest (Hetake ×3, 2 PCs) fight was captured.** GATO (single enemy) was not cross-checked
  because no in-battle low-HP GATO state exists in the repo (`gato_lowhp.state` lives in a previous
  session's scratch). The mechanism is enemy-agnostic (`C1/8167`/`C1/356D`/`C1/1BAA` contain zero enemy-id
  branching), and a 3-PC party would exercise the `C1/1BAA` "not the current seat" branch (`11BDE`) which
  the 2-PC capture only reached for seat 0 as a no-op.
* **Defeat / flee were not captured live.** The disasm proves `C1/354D` (A=9) and the flee tail
  (`C1/81F8-8218`) never call `C1/1BAA` and never write `$95D5`; whether the command menu is visibly up
  during the game-over fade is UNVERIFIED (it is immediately overwritten by the field re-entry `C0/A730`).
* **Tech/Item submenu open when the last enemy dies.** `C1/1BAA` handles the `$A86B` stash
  (`11BED-11BF9`) and `C1/1561`'s no-target tail restores/repaints the submenu strip from `D1/5BD0`
  (`11595-115A2`), but no capture was taken with `$95DB` = 1/2 at the moment of the kill. The spec above
  clears `$95DB` + our `techOpen/itemOpen` unconditionally, which matches the end state
  (`C1/1C05 STZ $95DB`) but not necessarily the 1-frame intermediate.
* **`$96F5,X` (the per-combatant eligibility byte read at `C1/2047`)** — the only writer found by a ROM scan
  is `STZ $96F5,X` at `CF/FE7B`; its per-frame maintainer (presumably a differently-based indexed store) is
  unidentified. Not load-bearing: the oracle proves the compaction result (`$99C0`→all FF at f820).
* **`C1/30B6`** (called 3× from `C1/356D` with `$94` = 2/1/0, before the seat loop) and the
  `STZ $98A7,X` (0x08 bytes) clear were not decoded — they are part of the celebration setup, not the UI
  teardown, and no UI byte in §1.7 changed at f862.

---

## 8. Reproduction commands (repo root; probe = a COPY of `build/ct.exe`)

```sh
# --- the user's exact scenario: last enemy dies while PC B is in target-select ---
ct_probe --monitor "9609:1,9613:1,9614:1,960E:1,95D5:1,95DB:1,99E0:1,A6D9:3,A6DD:1,A6DE:1,A6DF:1,A862:1,\
A62D:1,99C0:4,AF02:4,60B0:2,A013:1" \
  --from saves/battle_re/forest_battle_3hetake.state \
  --btnscript "0:20,100:6,0:10,100:6,0:10,100:6,0:10,100:6,0:586,100:6,0:12,100:6,0:20,100:6,0:700" \
  --diff --writer
#   f705 Marle target-select ($9609=1) ; f817 delist ; f820 $9613=FF -> $9609=0 (C1:1580)
#   f862 victory scan ($A5A9=FF C1:8183, $A0FE=1 C1:3574) ; f863 C1/1BAA teardown ; f865 $95D5=FF (C1:35A6)

# --- the 45-frame delist -> teardown interval, second independent run ---
SCRIPT=$(python -c "print(','.join(['100:6,0:10']*40)+',0:500')")
ct_probe --monitor "A5A9:1,A0FE:1,AF02:4,9609:1,95D5:1,A6D9:3,95DB:1" \
  --from saves/battle_re/forest_battle_3hetake.state --btnscript "$SCRIPT" --diff --writer
#   f720 delist -> f765 $A5A9=FF/$A0FE=1  == 45 frames

# --- OAM / framebuffer proof ---
PRE="0:20,100:6,0:10,100:6,0:10,100:6,0:10,100:6,0:586,100:6,0:12,100:6,0:20,100:6"
ct_probe --peek saves/battle_re/forest_battle_3hetake.state lc_812.raw --btnscript "$PRE,0:102"   # hand UP
ct_probe --peek saves/battle_re/forest_battle_3hetake.state lc_822.raw --btnscript "$PRE,0:112"   # hand GONE
ct_probe --peek saves/battle_re/forest_battle_3hetake.state lc_870.raw --btnscript "$PRE,0:160"   # menu GONE
python tools/ct.py fb2png lc_870.raw lc_870.png

# --- our engine's defect, live ---
CT_BATTLE_SCRIPT="a:1,n:8,a:1,n:200,a:1,n:8,a:1,n:600,a:1,n:20,a:1,n:25,n:400,a:1,n:60,a:1,n:60,a:1,n:1500" \
CT_BATTLE_TRACE=1 build/ct_field.exe --assets ct_assets.dat --scene 19 \
    --battletest saves/battle_re/forest_pre.raw.wram | grep battletrace | tail
#   f1628 victory cur=1 9609=1  ... f4000 end-debug cur=1 9609=1   (ROM: 9609=0 at delist+3, 95D5=FF at +46)

# --- ROM sites ---
python tools/dis.py 18130 0x60 1 1    # end-check: defeat (dp $24) vs victory scan C1/8167; A=8 @C1/8186
python tools/dis.py 1356D 0x40 1 0    # C1/356D victory handler: 1BAA x3 + $95D5=FF (35A1) + JSR C1/0C2D
python tools/dis.py 11BAA 0x90 1 0    # C1/1BAA queue-remove + the $9609/$960E/$9614/$95DB/$99E0 teardown
python tools/dis.py 11561 0x50 1 0    # C1/1561 target-select tick: $9613<0 -> STZ $9614/$960E / DEC $9609
python tools/dis.py 11F79 0x80 1 0    # C1/1F79 rebuild: $99C0/$A62D FF-fill + $9613=FF when all FF (11FD9)
python tools/dis.py 12040 0x60 1 0    # the compaction loop + $A62D = $99C0[$9614] (C1/2085)
python tools/dis.py 11C3A 0x20 1 0    # C1/1C3A: D1/5800 -> $0B40 (0x180) = restore the DEFAULT HUD strip
python tools/dis.py 10C2D 0x60 1 0    # C1/0C2D: repaint when $A6DD != $A6DF, and when $95D5 < 0 (10C71)
python tools/dis.py 11153 0x20 1 0    # the tick's input machine, gated $95D5>=0 then $9609
python tools/dis.py 117DD 0x20 1 0    # the tick's second member, same gates
python tools/dis.py 1354D 0x20 1 0    # C1/354D (A=9, DEFEAT): no C1/1BAA, no $95D5 write
```

---

## VERIFICATION (adversarial pass)

Independent re-run of every ROM/oracle citation. Probe = a COPY of `build/ct.exe` (`ct_probe.exe`).
Verdict: **SOUND WITH CORRECTIONS** — the two-moment mechanism reproduces byte-for-byte, but the
**generality table in §5 is wrong** and **`CT_BATTLE_END_TEARDOWN 43` is fitted to Hetake**.

### V-A. CONFIRMED (re-derived / re-measured independently)

* **Dispatch table** `C1/0051` raw-dumped from the ROM file: `23 00 19 1B AA 1B 6E 10 58 40 86 29 6D 00
  DD 1F 6D 35 4D 35 …` → `[2]=C1/1BAA [3]=C1/106E [8]=C1/356D [9]=C1/354D`. ✔
* **`C1/356D`, `C1/1BAA`, `C1/1561`, `C1/1F79`, `C1/1C3A`, `C1/0C2D`, `C1/1153`, `C1/17DD`, `C1/007E`,
  `C1/354D`, `C1/08CB`, `C1/20A9`, `C1/27C5`, `C1/2045-2099`** re-disassembled: every instruction and every
  writer PC in §1.2-§1.6 is exactly as quoted. `JSR C1/1C3A` callers scanned = exactly
  `{0C59, 0C76, 138F, 14DB, 1C33, 1FB9}` — the §1.4 list is byte-exact.
* **`C1/356D` reachability**: `A9 08 20 03 00` occurs once (C1/8186); no `20/4C/22 6D 35` anywhere; all 27
  `JSR $0003` sites in bank C1 checked for their A-load (only C1/8186 loads #08; C1/802D loads 0, C1/93C0
  loads 5) and both `JSR C1/0045` sites (C1/2F60, C1/351E) load #05. **`C1/356D` is victory-only.** ✔
* **Oracle timeline** (`--monitor … --diff --writer`, same script): f705 `$9609`=1 (C1:12B9) · f816 `$60B0`→0
  (C1:ED12) · f817 `$AF02`→FFFFFFFF (C1:B2C3) · f820 `$9613`→FF (C1:1FDC), `$9614`(C1:157A),
  `$960E`(C1:157D), `$9609`→0 (C1:1580), `$95D5` still 01 · f862 `$A5A9`→FF (C1:8183)+`$A0FE`→1 (C1:3574)
  (second monitor set) · f863 the full C1/1BAA writer set · f865 `$95D5`=FF (C1:35A6). **Identical.** ✔
* **OAM**, decoded with an independent decoder: f812 `#0 x=152 y=119 tile=000 pal=1 prio=3 sz=1` (+ #1..#3
  parked at x=-133/-141); f822 `#0..#3 all y=240`; f870 no tile-000 pal1 sprite. **Identical.** ✔
* `$960A/$960C/$960D` = `01/00/07` and `$95DC`=00 at f812/f822/f870; `$A6DD/$A6DE/$A6DF` = FF/00/FF at f870. ✔
* **Our engine**: the §4.3 trace reproduces verbatim (f1625 act-end 9609=1 AF02=FFFFFFFF → f1628 victory
  cur=1 9609=1 vhold=1 → f4000 cur=1 9609=1 vhold=2372) and `CT_BATTLESHOT_AT=1700`+`_HUD=1` gives
  `fnv=7E3BEA50`. MASHA no-op premise verified BOTH ways: forest `f996 victory cur=-1 9609=0`, GATO
  `f2171 victory cur=-1 9609=0`. Every cited `src/` line checked (only `battle.c:4141` is really **4139**). ✔
* **NEW — the ROM DOES paint the PC bracket in the gap.** Peeks at f828/f832/f836/f840 show the white
  corner bracket around Marle (absent f822/f824 = the pulse's dark phase). So the §6.2 note *"the bracket
  stays absent"* is wrong, but the **spec is right**: our unchanged `battleBracketRender`
  (`$95D5<3 && $9609==0`) reproduces the ROM. Do not "fix" the bracket when it appears.
* **NEW — mechanism pointer for the §7 OPEN.** `--monitor "A0A8:11,96F5:11,…" --writer` across the gap:
  `$A0A8[5]` 0→1 at **f817** (writer **CF:FE30**) = the dying-combatant exclusion the target-set builder
  reads at `C1/2051`; `$96F5[5]` 1→0 at **f860** (writer **CF:FE7E**, the `STZ $96F5,X` at `CF/FE7B`), i.e.
  **2 frames before** the victory scan. The gap is the bank-CF death routine draining — not a fixed wait.

### V-B. REFUTED

**R1 — §5's "VICTORY-ONLY" is FALSE. Defeat and flee run the SAME `C1/1BAA` teardown.**
Table entry `[2] = C1/1BAA`, and `python tools/dis.py 1B7F2 0x40 1 0`:
```
1B7F2 TDC / STA $A1 / LDA #02 / JSR C1/0003      ; seat 0 -> C1/1BAA
1B7FA LDA #01 / STA $A1 / LDA #02 / JSR C1/0003  ; seat 1
1B803 LDA #02 / STA $A1 / LDA #02 / JSR C1/0003  ; seat 2
1B80C RTS
```
`C1/B7F2` is called at **`C1/8143`** (the DEFEAT branch, right after `$7F01EC`=1) and at **`C1/820A`**
(the FLEE tail). ORACLE PROOF (flee forced with `L+R` = btn `C00`, target-select OPEN, `$95D5`=00):
```
ct_probe --monitor "7F01EC:1,95D5:1,9609:1,95DB:1,9614:1,960E:1,99E0:1,A6D9:3,A6DE:1,A6DF:1,A862:1,99CD:1" \
  --from saves/battle_re/forest_battle_3hetake.state \
  --btnscript "0:20,100:6,0:10,100:6,0:10,100:6,0:10,100:6,0:586,100:6,0:20,C00:600" --diff --writer
f1091  95D5=FF(C1:1C33) 9609=00(C1:1BFF) 95DB=00(C1:1C08) 9614=00(C1:1C05) 960E=00(C1:1C02)
       99E0=00(C1:1C0B) A6D9=FFFFFF(C1:1C18) A6DE=00(C1:1BD9) A6DF=FE(C1:1C10) A862=00(C1:1C39)
```
— the **identical** writer set as the victory f863 row, with no 45-frame delay.
Victory-exclusive is only `C1/356D`'s *wrapper*: the one-frame spacing (`C1/007E`), the redundant
`$95D5=0xFF` force (`C1/35A3`) and the `JSR C1/0C2D` repaint. **The spec line "Defeat/flee get NO teardown"
is invention-by-omission.**

**R2 — "45 real frames" is NOT invariant; `CT_BATTLE_END_TEARDOWN 43` is Hetake-fitted.**
delist (`$AF02`→all FF, C1:B2C3) → victory scan (`$A5A9`→FF, C1:8183), mash scripts, `--diff --writer`:

| state | enemy(s) | party | delist → scan |
|---|---|---|---|
| forest_battle_3hetake (6 runs, 4 cadences) | 0x5E ×3 | 2 PC | **45** |
| bat3_lake | 0x5E ×3 | 2 PC | **45** |
| bat7_lake | 0x15 ×3 | 1 PC | **45** |
| bat2_lake | 0x8D ×2 + 0x19 | 2 PC | **45** |
| bat4_lake (3 runs, 3 cadences) | 0x0C ×3 | 1 PC | **46** |
| bat5_lake | 0x0C ×2 | 1 PC | **46** |
| **bat1_lake (2 runs, 2 cadences)** | **0x92 GATO** | 1 PC | **2** |

Party size does not explain it (1-PC bat7 = 45 vs 1-PC bat4/bat5 = 46). A single baked constant is wrong by
**43 frames** for GATO — the very enemy the pinned checks use. Consistent with V-A's `$96F5` finding: the
gate is the per-enemy death routine finishing, not a fixed wait.

**R3 — `h->victoryHold == CT_BATTLE_END_TEARDOWN` can silently never fire.**
`victoryHold++` is at `battle.c:4120` (every real frame), but the `if (h->victoryHold)` block is at **4168**,
*below* the `wasFrozen` return (4139) and *below* the logic-tick gate (4146-4148: `tickAcc += 2`, fire at 5)
— so the block is entered on only ~2 of every 5 real frames. Roughly 3 of 5 integer values of `victoryHold`
are never observed there; whether 43 is one of them depends on the accumulator phase. **Use `>=` + a latch,
or fire from the real-frame section next to 4120.**

**R4 (minor) — `nh = battleQueueHead(s); // scan $A6D9[0..2]` misreads our own helper.**
`battleQueueHead` (`battle.c:998`) returns `$A6D9[$A6DD]` (or -1) — it dereferences the CURSOR, it does not
scan from index 0 like `C1/1C18-1C2B`. End state is the same in the 3-seat loop (all FF), but the comment is
wrong and the helper is not a drop-in for the ROM scan anywhere else.

**R5 (minor) — `battleFirstEnemySlot` is not the ROM's eligible set.** It tests only `$AF02!=FF`; the ROM
skips on `$96F5,X==0 / $9FF7,X bit7 / $A0A8,X / $A09B,X` (C1/2047-2062) — and `$A0A8` is what actually
excludes the dying enemy (V-A). It also ignores the `$AF15` bit6 hidden test that our own step-G victory
check applies, so the two disagree when a hidden enemy is present. Fine as a documented approximation;
not "== `$9613`".

**R6 (cosmetic)** — §1.1's `18175/18178` are really `18176/18179`; the 8× `A=3` dispatch is a loop
(`1814F-1815A`), not unrolled; §1.5's FF-fill covers 12 entries (`LDX #000B`→0) while the emptiness SCAN
covers 11; `s[0xA862]=0` writes a byte no code in `src/` reads.


---

# IMPLEMENTATION ADDENDUM (session 24) — SHIPPED

Both halves landed in `src/battle.c`; **no render change** (the dossier's §5 finding that the ROM gates the
STATE, not the render, holds — `battleTargetCursorRender` / `battleBracketRender` / the HUD phase are the
ROM's own gates already).

* **`battleTargetSetRevalidate`** (== the C1/1F79 rebuild + the C1/1561 tail) runs every RENDER frame, ahead
  of both the `$A013`-freeze return and the logic-tick gate — because the killing blow lands at a span end
  INSIDE the acting PC's freeze while the OTHER PC holds the cursor. Empty eligible set → `STZ $9614` /
  `STZ $960E` / `DEC $9609`; non-empty → the `battle_queue_death.md` C.1 re-resolve. One helper, one rule.
  Its eligibility test mirrors step G's own scan (`$AF02` + the `$AF15` bit6 hidden skip) so the two can
  never disagree; the ROM's extra per-actor gates (`$96F5`/`$9FF7`/`$A0A8`/`$A09B`) stay unmodelled (OPEN).
* **`battleQueueRemoveTeardown`** (== C1/1BAA) + **`battleEndTeardown`** (== C1/356D, `$95D5=0xFF` forced
  AFTER the per-seat loop). Per the adversarial pass it is **NOT victory-gated**: the defeat path calls it
  too (the ROM reaches C1/1BAA from C1/8143 `JSR C1/B7F2` as well as C1/356D).
* **The delay is the MECHANISM, not a constant.** The verifier refuted the proposed `CT_BATTLE_END_TEARDOWN
  43` (measured 45/46 for the dissolving ids but **2** for GATO) and supplied the reason: the victory scan
  waits for the bank-CF death routine to clear `$96F5,X` (writer CF:FE7E). So the gate here is "fire once no
  corpse is still dissolving" — which self-scales per enemy exactly as the ROM does. Residual: our dissolve
  is HOLD 9 + STEPS 32 = 41 render frames vs the ROM's 45; that rides the `battle_dissolve.md` fixture.

**Validated** by the §4.3 repro: `f1625 act-end … 9609=1 AF02=FFFFFFFF` → **`f1625 target-empty … 9609=0`**
(same frame — inside the ROM's 3-frame window) → `f1628 victory` → **`f1665 end-teardown … cur=-1
q[255..0]`**. Pinned as `field-battle-end-uiteardown`. The whole suite stayed green with ZERO re-pins, as the
dossier predicted (every MASHA fight confirms before the kill, so both changes write already-cleared values).

---

## 9. THE VICTORY'S FOUR FRAMES (2026-10-13, roadmap B5) — SHIPPED

The addendum above ran C1/356D's three `C1/1BAA` removals together, one frame after the victory, and armed the pose
ladder in the victory's own frame. `dis.py 1356D 0x48 1 0` says otherwise: every removal is followed by `JSR C1/007E`
(the one-frame wait), and the arm comes after the third wait:

```
1356D JSL CD/0009 / INC $A0FE / STZ $98A7,X x8 / JSR C1/30B6 x3      ; frame V
13589 STZ $A1 / JSR C1/1BAA / JSR C1/007E                             ; seat 0 in V
13591 INC $A1 / JSR C1/1BAA / JSR C1/007E                             ; seat 1 in V+1
13599 INC $A1 / JSR C1/1BAA / JSR C1/007E                             ; seat 2 in V+2
135A1 LDA #FF / STA $95D5 / JSR C1/0C2D                               ; V+3: no chooser, the strip repainted
135A9 LDA $2989 / AND #01 / BEQ ; 135B1-135DD the windows +28, the copier C1/35DD -> C1/367D's 64-frame loop
```

The defeat and flee teardown `C1/B7F2` (`dis.py 1B7F2 0x1B 1 0`) is three `LDA #02 / JSR C1/0003` with `$A1` = 0/1/2 and
no wait: those stay immediate.

**Measured**, the rule's cartridge (CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1) on GATO's fight made into a Dark Matter
kill (tools/battle_ab.py's pokes on saves/battle_re/gato_wait.state; Magus's eighth tech at the first menu):

| party | V (`$A0FE` 00->01) | `$A1` 00 / 01 / 02 | queue | `$A6DF` FE->FF, `$5DF1` = 28 00 x3, `$5DC9` = 40 | first `$9907` publish |
|---|---|---|---|---|---|
| Magus + Crono + Marle | c1397 | V / V+1 / V+2 | V: `$A6D9` 00 FF 02 -> FF FF 02, `$A6DD` 02 01 FE, `$95D5` 02; V+2: `$A6D9` FF FF FF, `$95D5` FF | V+3 | V+5 |
| Magus solo | c1322 | V / V+1 / V+2 | (empty) | V+3 | V+5 |

The port made the three removals in V+1 and published the first victory pose in V+2 — three frames early.
`battleVictoryTeardownFrame` (src/battle.c) now runs one call per frame from V: the removal of seat `victoryHold - 1`
with `$A1`, and in V+3 `$95D5 = FF`, `battleMenuPassHead` (C1/0C2D) and `CtBattlePcLadderArm(0x28)`. The ladder's stepper
runs ahead of it in the frame, so its loop begins in V+4 as C1/367D's does. `CT_BATTLE_VICTORY_CELEBRATE` counts from V, not
from the arm, so the results are unchanged: the cartridge's `$5DC9` runs 40 (c1325 = V+3) to 00 (c1389), and the results
box's `$CCEC = 10` lands at c1391 = V+69, the glyphs two frames later at victoryHold 72. The `end-teardown` trace is printed at
C1/35A1 (V+3); the checks anchored on it moved by two frames, with the same pictures.

Pinned by `battle-victory-teardown-frames-vs-oracle` (tools/battle_victory_teardown_cmp.py): both parties, every byte of
the queue, the menu, `$E5`, `$A1` (from V) and `$9907` from V-2 to V+40 equal to the cartridge's (the old engine: 95 and
114 differing frame-fields).

**Open:** the ladder's own bytes (`$5DF1/$5DF2` windows, `$5DDD/$5DDE`, `$5DBD` dwells, `$5DC1` cursors, `$5DC9` loop) live
in the host (`pcLadder*`), not the WRAM; the cartridge writes them at V+3. Before V, the port's `$A1` reads 00 where the
cartridge's reads 02 (another writer of the direct page's `$A1`, not traced). After the results the port's exit runs three frames
behind the cartridge's (CF/FBE5's `$A4` 00 -> 01: cartridge V+457, port V+460; the song post `$1E00 = 10` V+462 / V+465),
unchanged by this section's work -- not derived yet.
