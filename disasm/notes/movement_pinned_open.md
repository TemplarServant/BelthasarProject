# MOVEMENT — the 3 pinned "UNDER INVESTIGATION" shortfalls, SETTLED

Settles the one explicitly pinned open investigation in the memory file `ct-infra-regress-suite.md`:

> **⚠ OPEN (pinned as under-investigation, do NOT trust the old 270/270 claims):** `--playermove` prints
> intX 269/270, accX$2E 222/270, accY$30 134/270 (commits recorded 270/270); `--porttick` 128/136;
> `--playerframe` applied-velocity 196/270 (position 270/270). Consistent across 3+ runs — either a real
> regression since the claims or harness drift; needs its own investigation.

**VERDICT: all five shortfalls are (B) — mis-specified comparisons in the harnesses, not divergences of the
ported movement code. There is ZERO drift: every current number is bit-identical to the number the commit
that CREATED the mode printed in its own body. The "commits recorded 270/270" premise is false.**

Every claim below is anchored to a `tools/dis.py <flat hex>` disassembly (HiROM: bank `C0` addr `$XXXX` =
flat `0x0XXXX`) or to an observed `build/ct.exe` value. Writer PCs from `--findoam --watch` are the PC
*after* the store retires; writer PCs from `--watchwrite` are the PC *of* the storing opcode.

---

## 0 — The frame skeleton (the one ROM fact that explains 4 of the 5 shortfalls)

`tools/dis.py 881E 200 1 1` — the per-frame player controller, `DP=$1D00`:

```
881E: LDA #80 / STA $01EB / PHD / LDA #1D00 / TCD      ; DP = $1D00
882C: JSR  C0/88E5        ; seed:  $2E/$30 <- $2A/$2B
882F: STZ  $2C / STZ $2D  ; clear the d-pad INTENT
8833: LDA  $0162 / BNE 8840
8838: LDA  $011F / BEQ 8840
883D: JSR  C0/88EE        ; d-pad dispatch -> $2C/$2D intent + $2E/$30 accumulator
8840: LDA  $0120 / BEQ 884C
8847: JSR  C0/8A6D        ; tile collision clamp (zeroes $2E/$2F/$30/$31 into a wall)
884C: JSR  C0/9175        ; ---> VELOCITY $32/$33 = $2E/$30, screen-edge gated  <=== the port stops HERE
884F: JSR  C0/99DE        ; ---> CAMERA deadzone/edge gate: STZ $2E / STZ $30
8852: JSR  C0/91AC        ; ---> op-E7 camera seek: OVERWRITES $2E/$30
8855: JSR  C0/93E1        ; camera accumulate + commit
8858: PLD / RTS
```

**So `$1D2E/$1D30` at the end of a frame are the CAMERA's request, not the player's accumulator.**
`tools/dis.py 9175 60 1 1` shows `C0/9175` spans `9175..91AB` (`STZ $32`/`STZ $33` at 9175/9177, `STA $32`
at 9193, `STA $33` at 91A9, `RTS` at 91AB) — i.e. `$1D32/$1D33` are 9175's output and nothing after 9175
touches them, which is exactly why the *velocity* fields validate and the *accumulator* fields do not.

`tools/dis.py 99DE 70 1 1`:

```
99DE: LDA $2E / BEQ 99FF / BMI 99F1
99E4: LSR×4 / JSR C0/9A60 / BCC 99FF / 99ED: STZ $2E      ; X right: kill
99F1: EOR #FF / INC A / LSR×4 / JSR C0/9A7E / BCC 99FF / 99FD: STZ $2E   ; X left: kill
99FF: LDA $30 / BEQ 9A1E / BMI 9A10
9A05: LSR×4 / JSR C0/9A1F / BCC 9A1E / 9A0E: STZ $30      ; Y down: kill
9A10: EOR #FF / INC A / LSR×4 / JSR C0/9A3D / BCC 9A1E / 9A1C: STZ $30   ; Y up: kill
9A1E: RTS
```

`tools/dis.py 9A60 40 1 1` proves these are CAMERA tests, not player tests: `9A60` reads `$0C` (`$1D0C`,
camera right-edge coarse tile), `$1B` (`$1D1B` mask bound), `$93` (`$1D93` scroll phase) and `$0A00,X` (the
lead's ON-SCREEN foot) vs `#80` — the 128px deadzone. (This supersedes the label "TILE-collision clamp"
used in commit `838ddd9`; the routine is the camera gate, per `field_camera_unification.md`. Either
label gives the same conclusion: it runs AFTER 9175 and zeroes `$2E/$30`.)

`tools/dis.py 91AC 60 1 1`: `91B8/91BE: STA $2E`, `91D6/91DC: STA $30` — the op-E7 seek **overwrites**
`$2E/$30` whenever `$1D00 != 0`.

### Oracle confirmation of §0 (not just the disasm)

`build/ct.exe --watchwrite` (per-opcode writer PC on the player's `$1D2A-$1D33`, drive-script bedroom):

```
accX$1D2E 00->F0  @ C0/8960     x22   (88EE LEFT handler sets the accumulator)
velX$1D32 00->F0  @ C0/9193     x21   (9175 publishes the velocity)
accX$1D2E F0->00  @ C0/99FD     x21   (99DE X-left KILL zeroes the accumulator, AFTER 9175)
accY$1D30 00->10  @ C0/899E     x21
velY$1D33 00->10  @ C0/91A9     x16
accY$1D30 10->00  @ C0/9A0E     x16   (99DE Y-down KILL)
accY$1D30 10->00  @ C0/8D69     x5    (an 8A6D wall clamp)
```

`build/ct.exe --monitor '1802:2,1882:2,1902:1,1982:1,1D32:1,1D33:1,1D2E:1,1D30:1' --from fresh_ctrl.state
--btnscript '10:24,20:24,40:24,80:24,90:24,50:24,60:24,A0:24,0:24,10:24,20:24,40:6' --settle 8` over the
same 270-frame sweep shape:

```
displacement vs $1900/$1980 : 269/269 match, 0 mismatch
$1902/$1982 vs $1D32/$1D33  : 0/270 differ
post-frame $1D2E/$1D30 vs $1D32/$1D33 : 31/270 differ   <- the 99DE kills, live
```

---

## 1 — `--playermove` `accX$2E 222/270`, `accY$30 134/270` → **(B) mis-specified comparison**

`src/main.c:2206-2218` (`runPlayerMove`) snapshots the PRE-frame WRAM, runs the oracle a frame, then runs
`CtPlayerControl(snap)` and diffs **the post-frame** `$1D2E/$1D30`. `src/player.c:32-54` ports `881E` only
as far as `9175` (correct — that is the player's half). The oracle's post-frame `$1D2E/$1D30` have since
been zeroed by `99DE` (§0) or overwritten by `91AC`. The two are **different quantities**; the check is
comparing the port's player accumulator against the ROM's camera request.

Signature of every printed mismatch line (`--playermove` full output) is the `STZ`:

```
f 74..79  accX$2E: port=10 oracle=00   ($F9=01)
f 97..103 accY$30: port=F0 oracle=00   ($F9=09)
```
*port = the 9175-input accumulator; oracle = 0 = `STZ $2E`/`STZ $30`.*

**Arithmetic check.** In the 270-frame sweep (`dirs[(f/24)%9]` = U,D,L,R,U+R,U+L,D+L,D+R,none) the gate
`$011F` the harness feeds is 0 until f73 (§2/§3), so the port can only raise a request on
**125** frames for X (f73-95 R, f96-191 the four diagonals, f264-269 L) and **144** for Y
(f96-191, f216-239 U, f240-263 D). `270 − 125 = 145` frames can never mismatch on X, `270 − 144 = 126`
never on Y. Observed: `accX 222 = 145 + 77` and `accY 134 = 126 + 8` — i.e. **every single mismatch falls
on a request frame** (48 X, 136 Y), and never anywhere else. That is exactly the 99DE-kill population; a
real port divergence would have no reason to respect that boundary.

**Already known and written down.** Commit `838ddd9` ("M1: player mover STEP B") states verbatim:

> C0/91AC: RE-PURPOSES $1D2E/$1D30 for the CAMERA … KEY: the post-881E $1D2E/$1D30 is the camera's, NOT
> the player velocity — **do NOT validate them as player state**.

The `F[6]` field table in `runPlayerMove` (`src/main.c:2202-2204`) has never been edited since it was
introduced (`git log -S'{ 0x1D2C, "intX$2C" }' -- src/main.c` → only `7d4da56`), so the two accumulator
columns have been meaningless since day one.

---

## 2 — `--playermove` `intX$2C 269/270`, `velX$32 269/270` → **(B) one-frame gate mis-phase, frame 73**

`runPlayerMove` copies **this-frame (post-frame)** `$0162/$011F/$0138` into the snapshot
(`src/main.c:2213`). `881E` reads those gates at `8833/8838` — at frame START.

`build/ct.exe --tracemove` (same drive script, same `dirs`, same settle):

```
 f   h F9 | pos        dpos | vel32/33 | face int2C/2D 0162 011F 0138
 72 80 02 | 18A4,08FF  +0,+0 | 00/00   | 01   00/00     00   00   00
 73 80 01 | 18A4,08FF  +0,+0 | 00/00   | 01   00/00     00   01   00   <-- $011F flips 0->1 MID-frame
 74 80 01 | 18B4,08FF +16,+0 | 10/00   | 03   10/00     00   01   00
```

At f73 the ROM's `881E` saw `$011F = 0` → skipped `88EE` → intent 0, velocity 0, `dpos +0`. The harness
feeds the post-frame `$011F = 1`, so the port runs the dispatch and produces `$2C=$2E=$32=0x10`. **One
frame, one cause.** `runPlayerMove`'s three f73 lines (intX, accX, velX) are that one event.

This is the *same* defect commit `397223f` fixed in `--playerframe` by feeding the PREVIOUS frame's gates
(`src/main.c:2103-2118`) — which took that mode's position from 264/270 to **270/270**. `runPlayerMove`
was never given the fix. Commit `7d4da56`, which created the mode, already recorded
`intX$2C 269/270 … (the 1 miss is a transition-frame gate-timing artifact)`.

**Cross-check at the CORRECT phase:** `build/ct.exe --checkctrl` hooks the oracle at `C0/881E` **entry**
(gates + seed at their real pre-881E values), runs the port on that snapshot, steps the oracle to the
`881E` RTS and diffs `$1D32/$1D33`:

```
checkctrl: CtPlayerControl (881E: dispatch+8A6D+9175) vs oracle -> 400/400 velocity byte-exact (0 lost)
```

400/400, pinned green in `tools/regress_checks.json` (`player-controller-full`). The controller is exact.

---

## 3 — `--playerframe` `applied-velocity 196/270` → **(B) the oracle's `$1900,X` is STALE while control is locked**

### 3.1 Who writes the applied velocity

`tools/dis.py 9E29 90 1 1`:

```
9E29: JSR C0/9E84
9E2C: LDA $1D32 / BNE 9E3F
9E31: LDA $1D33 / BNE 9E42
9E36: LDX $6D
9E38: STZ $1900,X          ; both zero -> zero the applied velocity
9E3B: STZ $1980,X
9E3E: RTS
9E42: LDA $AB / INC A / AND #7F / STA $AB / TAX / LDY $6D
9E4C: LDA $1D32 / STA $7F0C00,X / 9E53: STA $1900,Y     ; applied X vel  <- $1D32
9E56: LDA $1D33 / STA $7F0C80,X / 9E5D: STA $1980,Y     ; applied Y vel  <- $1D33
```

A full-ROM byte scan for `20 29 9E` (`JSR C0/9E29`) finds **exactly one** call site: flat `0x3047` =
`C0/3047`. `tools/dis.py 3020 60 1 1`:

```
3040: LDA #01 / LDX $6D / STA $1A01,X      ; arm a 1-frame move ticket
3047: JSR C0/9E29                          ; the ONLY 9E29 call in the ROM
304A: LDX $6D / LDA $1600,X / JSR C0/5B8D / BCC 3057 / JSR C0/3154
3057: REP #10 / LDX $C7 / CLC / RTS        ; CLC = park on the op (object-VM handler)
```

`C0/3040` is an **object-VM opcode handler**. So `$1900,X/$1980,X` are refreshed only on frames where the
player object actually executes that op. `tools/dis.py AAFD 70 1 1` shows the integrator consumes them and
is gated by the very ticket `3040` arms:

```
AAFD: LDX $6D / LDA $1B80,X / BEQ AB06 / BRA AAC7    ; mode gate (AA07 path if != 0)
AB06: LDA $1A01,X / BNE AB0C / AB0B: RTS             ; NO ticket -> NO integration
AB0C: DEC A / STA $1A01,X
AB10: LDA $1900,X (sign-extend) / ADC $1800,X / STA $1800,X
AB2A: LDA $1980,X (sign-extend) / ADC $1880,X / STA $1880,X
```

Writer census over 240 gameplay frames of the drive-script bedroom, per byte
(`build/ct.exe --findoam --script --watch 7E1902 1` / `... 7E1982 1`):

```
findwrites $7E1902+$1 : 240 to the region from 2 PCs -> C0/9E3B: 75 (the 9E38 STZ), C0/9E56: 165 (the 9E53 STA)
findwrites $7E1982+$1 : 240 to the region from 2 PCs -> C0/9E3E: 75 (the 9E3B STZ), C0/9E60: 165 (the 9E5D STA)
```

One write per frame **while the player is under control** — and no other writer exists.

### 3.2 The observed staleness

`build/ct.exe --monitor '1802:2,1882:2,1902:1,1982:1,1D32:1,1D33:1,011F:1,006D:1,1A03:1' --from
bedslide.state --settle 120 --btnscript '<A-mash x45>,10:24,20:24,40:24,80:24,90:24,50:24,60:24,A0:24,0:24'`
(loc `$0002` = Crono's bedroom, `$0197 = 02`), 396 frames:

```
locked frames ($011F == 0)                     : 169
  of those, $1902/$1982 != $1D32/$1D33         : 169/169     <-- 100% STALE
  e.g. f0..f168:  $1902 = F9   $1D32 = 00   pos = 18A4,08FF (frozen)
unlocked frames                                : 227
  of those mismatching                         : 1           (the $011F flip frame, §2)
displacement vs $1900/$1980, unlocked          : 0 mismatch
$1902 vs $1D32, unlocked                       : 0 differ
held-d-pad-but-zero-velocity (wall) frames     : 9, all with $1902 = $1982 = 00 (NOT stale)
```

So the staleness condition is **"the player object is not running the `C0/3040` controller op"** (which is
what `$011F == 0` means in practice) — **not** "a move is blocked", which is what commit `397223f` guessed;
on a wall frame `9E29` still runs and takes the `9E38` STZ path, so `$1902` is correctly 0.

### 3.3 The count is exact

`runPlayerFrame` (`src/main.c:2119-2127`) synthesizes `cram[$1900+idx] = cram[$1D32]` **every** frame and
diffs it against `r[$1900+idx]`. Per `--tracemove`, `881E`'s gate `$011F` is 0 through **f73** and 1 from
f74 on. Frames f0..f73 inclusive = **74 frames** on which the oracle's `$1900/$1980` are stale while the
port writes the (correct) 0.

```
270 − 74 = 196    ==   the printed "applied-velocity 196/270"
```

Exact, to the frame. And on those 74 frames both the port and the oracle move 0 px, which is why
`position` stays **270/270** — the definitive end-to-end metric, as commit `397223f` stated:

> position now 270/270 byte-exact over the full d-pad sweep … (The applied-velocity field reports
> **196/270** only because it diffs the oracle's $1900 register, which holds a stale value…)

i.e. **196/270 is the value the commit that produced this mode recorded. It never was 270/270.**

---

## 4 — `--porttick 128/136` → **(B) zero drift; the residual is a layer the mode explicitly does not own**

Commit `e72927c` ("M1: STEP 4 — the stateful autonomous field move tick (--porttick)"), the commit that
CREATED the mode, records in its own body:

> → **128/136 owned-actor-frames byte-exact, longest unbroken run 50 frames, 9 moves tracked**.
> The divergences are at move-COMMAND TRANSITIONS (arrival → the runner advances the script IP and runs the
> next opcode within its multi-op budget = STEP 5), plus idx 10 (the non-standard actor also filtered in
> --checkstep) and one scene-event teleport.

Current output (`build/ct.exe --porttick`) is **bit-identical**, including both emergent secondary numbers:

```
porttick: stateful autonomous C move tick -> 128/136 owned-actor-frames match (9 moves tracked, longest unbroken run 50 frames)
  idx 10 f2   diverge C(1600,0D09 v5,5 c03) ora(15FB,0D09 v5,5 c00)
  idx 10 f57  diverge C(1709,0E03 v5,5 c03) ora(1709,0DFE v5,5 c00)
  idx 12 f270 diverge C(1504,0BBF v9,9 c00) ora(14FB,0BBF v9,9 c00)
  idx 12 f277 diverge C(1534,0C01 v7,10 c01) ora(1534,0BF7 v7,10 c00)
  idx 12 f302 diverge C(1604,0CD3 v9,9 c00) ora(15FB,0CD3 v9,9 c00)
  idx 12 f333 diverge C(1702,0DF2 v9,9 c01) ora(16F9,0DF2 v9,9 c00)
  idx 12 f336 diverge C(170B,0E04 v9,9 c02) ora(170B,0DFB v9,9 c00)
  idx 12 f343 diverge C(170B,0E2B v0,12 c00) ora(1880,04FF v0,12 c00)
```

Reading the lines against the ROM: in 6 of 8 the oracle's ticket `$1A01` is already **00** while the C still
holds 01/02/03, and the position gap is **exactly one velocity step** (`v5` → 5 px, `v9` → 9 px). Per
`C0/AAFD` (§3.1) ticket 0 = **no integration this frame**: the oracle's runner ended/re-issued the command,
or the scheduler did not visit the actor, while `runPortTick` (`src/main.c:1174-1183`) ticks its owned actor
**unconditionally, every frame, with no runner and no scheduler** — by construction ("'when to start' is the
scheduler, deferred"). `idx 12 f343` is the documented scene-event teleport (`ora 170B,0E2B → 1880,04FF`).

The layer that closes them is already ported and pinned GREEN: `portrun-field-runner` in
`tools/regress_checks.json` →
`portrun: runner + integration -> 1592/1592 owned-actor-frames match (6 actors tracked, longest run 359)`.

---

## 5 — Provenance: was the pin ever green, and did anything move it?

* `git log --all -S"playermove" -- tools/regress_checks.json` → **one** commit, `3d05d0c` (2026-07-01, the
  commit that created the suite). The `expect` strings for all three checks were pinned at the
  already-observed values; **they were never green and were never re-pinned**. Their own `notes` fields say
  "WARNING - PINNED BASELINE UNDER INVESTIGATION".
* The three harness functions are semantically unchanged since the commits that recorded the numbers.
  Diffing `runPlayerMove` @`838ddd9`, `runPlayerFrame` @`397223f`, `runPortTick` @`e72927c` against HEAD
  yields, in each case, exactly two changed lines: `ctDriveScript(s)` → `HarnessDriveScript(s)` (the
  `b60dfa9` honest-exit-code hardening) and `return 0;` → a real failure return. **No comparison, no
  sampling point, no field list changed.**
* Where "270/270" came from:
  * commit subject `0986f14` = "M2: field collision + player mover COMPLETE -- update RE map
    (`--playerframe 270/270`)" — that is the **position** field, and its body-commit `397223f` spells out
    "applied-velocity … 196/270".
  * `docs/DEBUG_SURFACE.md:85-86` documents the *expected* output of `--playermove` as
    `accX$2E 270/270 accY$30 270/270` and of `--playerframe` as `applied-velocity 270/270 byte-exact
    (the canonical '270/270')`. **Those two doc rows are aspirational and have never been true** — they are
    the actual source of the "harness drift / regression" belief recorded in memory.

---

## VERDICT TABLE

| metric | now | recorded when the mode was born | verdict | cause (ROM-anchored) |
|---|---|---|---|---|
| `--playermove accX$2E` | 222/270 | `838ddd9`: "do NOT validate as player state" | **B** | `C0/884F JSR 99DE` `STZ $2E` (99ED/99FD) + `91AC STA $2E` run AFTER `9175`; the port correctly stops at `9175` |
| `--playermove accY$30` | 134/270 | idem | **B** | `STZ $30` at 9A0E/9A1C |
| `--playermove intX/velX` | 269/270 | `7d4da56`: "269/270 … transition-frame gate-timing artifact" | **B** | f73: `$011F` flips 0→1 mid-frame; harness feeds the POST-frame gate (`main.c:2213`) |
| `--playerframe applied-vel` | 196/270 | `397223f`: "**196/270**" | **B** | `$1900,X` is written only by `C0/9E29`, whose only caller `C0/3040` is an object-VM op that doesn't run while control is locked → stale on exactly the 74 locked frames (270−74=196) |
| `--porttick` | 128/136 | `e72927c`: "**128/136** … run 50 … 9 moves" | **B** | mode owns the tick only; ticket-0 frames belong to the runner/scheduler (`C0/AAFD AB06`), closed green by `--portrun 1592/1592` |

**No (A). Nothing in `src/player.c`, `src/collision.c` or the mover is wrong on the evidence of these five
numbers.** The correctly-phased checks are green: `--checkctrl 400/400`, `--playerframe position 270/270`,
`--portrun 1592/1592`, `--checkstep 13/13`, `--checkmove 0/0`.

---

## RECOMMENDED FOLLOW-UP (not applied — derivation mode is read-only on `src/`)

1. `runPlayerMove` (`src/main.c:2213`): feed the **previous** frame's `$0162/$011F/$0138` exactly as
   `runPlayerFrame` already does (`main.c:2103-2118`) → `intX/velX` should become 270/270. **Do not** touch
   the `$1D2E/$1D30` rows by "fixing" the port; the correct repair is to **drop `accX$2E`/`accY$30` from the
   field table** (`main.c:2203-2204`) or to re-label them `camReqX/camReqY` and compare them against the
   camera tick's output, since `$1D2E/$1D30` post-frame ARE the camera request (`ct_vm.c:2158-2162`).
2. `runPlayerFrame` (`main.c:2127`): gate the applied-velocity tally on the frames where the oracle actually
   refreshed `$1900` (e.g. skip frames whose `881E` gate was closed), or drop the sub-metric — position is
   the end-to-end truth.
3. `docs/DEBUG_SURFACE.md:85-86`: correct the two "270/270" expected-output cells; they are the origin of
   the false memory claim.
4. `tools/regress_checks.json`: after (1)/(2), re-pin `playermove-input-velocity` and `playerframe-endtoend`
   and delete the "UNDER INVESTIGATION" warnings; `porttick-autonomous-mover` needs only a `notes` rewrite
   (128/136 is the mode's correct, scope-limited value — cite `e72927c` and `--portrun 1592/1592`).
5. Memory `ct-infra-regress-suite.md`: the OPEN paragraph should be replaced by this file's verdict.

## NOT DERIVED (honest gaps)

* **Why `C0/3040` is not executed while `$011F == 0`** is inferred from the ROM's structure (`3040` is an
  object-VM opcode handler ending in `CLC` = park-on-op) plus 169/169 observed stale frames — I did not
  disassemble the object-VM dispatch table entry that selects `3040`, nor watch the VM's IP for the player
  object across the lock boundary. *Closes it:* `build/ct.exe --followvm --obj 1 --from bedslide.state
  --input '<A-mash>' --frames 200` across the `$011F` 0→1 boundary + `tools/dis.py` on the op table entry
  that vectors to `C0/303F/3040`.
* **The exact per-frame 99DE kill counts (48 X / 136 Y)** are inferred from the mismatch totals decomposing
  exactly onto the request-frame populations (145+77=222, 126+8=134); I did not enumerate the killed frames
  individually. *Closes it:* a `--monitor` run seeded from a savestate captured at `--playerframe`'s exact
  start point, logging `$1D2E/$1D30/$1D32/$1D33` per frame over the same `dirs` sweep — which needs a
  `--savestate` variant that runs `HarnessDriveScript` (today `runSaveState` only supports `--frames/--mash`).
* **The gate state of frames f0..f67 of the sweep was not observed directly** — `--tracemove` only prints
  f68..f100, where `$011F` is 0 through f73. That the whole f0..f73 window is locked is corroborated
  *exactly* by `270 − 74 = 196` (§3.3) and by the drive script ending in a 22-round A-mash (dialogue), but
  it is an inference. *Closes it:* widen `--tracemove`'s print window to f0..f110 (one line in
  `src/main.c:2076`) — a harness edit, forbidden in derivation mode.
* `--porttick`'s `idx 10` "non-standard actor" is taken from `e72927c`'s own body; I confirmed the
  divergence signature (ticket 0 vs 3, one-step gap) but did not identify what makes actor `0x10` special.

## SIDE OBSERVATION

`two_member.state` **exists at repo root** (the memory file lists it under "Missing captures that would seed
2 SKIP checks"). The scene-8 conga `followtest` SKIP may be unblockable for free — worth a separate look.


---

## CORRECTION (critic pass, 2026-07-25) — the `$1900,X` mechanism claim was OVERBROAD

§3 and the verdict table justify `--playerframe`'s 196/270 with "`$1900,X` is written only by
`C0/9E29`". A ROM scan refutes the broad form: `$1900,X/Y` has writers at `305F 307D 3084 30B5 30F3
30FE 4378 442B 4E4C 9E38 9E53 A26F A2AD A2D2 A2FF A31A AC6B ACD7 ACE9 ACFF ADEF AF1C` (+4 outside
`$C0`), and `$1980,X/Y` similarly — `C0/305D` itself opens `LDX $6D / STZ $1900,X / STZ $1980,X`.

What survives: the NARROW claim (`C0/9E29`'s sole caller is `C0/3047`) and the 169/169 observation
that the oracle's `$1900` is stale across the locked frames. So the VERDICT (B, harness-side, not an
engine divergence) and the pinned number stand on the observation; the causal story does not.

**Consequence for the recommended follow-up #2**: do NOT implement "compare only where the oracle
refreshed `$1900,X`" as "the `881E` gate was open". That is a heuristic standing in for a mechanism —
the forbidden pattern, applied to a test harness — and it would mask real divergence on any frame the
`A2xx`/`ACxx`/`ADxx` mover block touches the player's slot. Full detail: `critic_wave_audit.md` §2.1.
