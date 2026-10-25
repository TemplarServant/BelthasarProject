# The ANIMATION FAMILY runs on the ROM's bytes — op 0xAB/0xB7 complete on `$7F0B01,X`

2026-09-02. The seventh delegation of a live-VM opcode family onto the body `--sweepops` grades
(after the `$C1` item family 66f2e43 and the `$C2` party gate 4353faa), and the one the project's
own triage dossier (`vmdiff_pinned_triage.md` §5) had named as "a wiring job, not an edit". This
note records what was measured, what moved, what it exposed, and what is still open.

## 1. What was wrong, measured

`src/ct_vm.c step()` kept the whole scripted-animation state in private C fields (`animMode`,
`animIl`, `animLoops`, `animBusy`, `animOpState`, `animOpAddr`) and ended a BLOCKING `0xAB`/`0xB7`
when the RENDERER said its own playback was over (`CtSceneAnimFinished`, called from field.c's
`animTickActor` and from `--vmdiff`'s `VMDIFF_ANIM=1` mimic). The ROM ends it on one byte:

```
2E9A LDX $6D
2E9C LDA $7F0B01,X          ; the ONLY arm selector
2EA0 BEQ ->2ED8             ; 0 -> ARM
2EA2 DEC A / 2EA3 BEQ ->2EB4; 1 -> COMPLETE (2EB4-2ED7: $7F0B01=0 $1601=0 $1681=0, $1780 := $1680==FF ? ($1680=0, 0) : 1, ip+2, SEC)
2EA7 ... CMP $1781,X        ; >=2, same id -> PARK (TYX/CLC, 0 writes); different id -> RE-ARM
2ED8-2F00 ARM               ; $1781=id $1681=0 $1601=0 ($1680=FF iff $1780==0) $1780=2 $7F0B01=2, TYX/CLC
```

`0xB7` (C0/2F01) is the same body with one extra INX (ip+3) and `2F63-2F6A` storing `operand3+1`
into `$7F0B01,X` instead of the literal 2. The byte is driven 2 → 1 by the frame legs the live
`CtSceneTick` already runs: `CtAnimTimerTick`'s CA4F ping-pong arm and `CtFieldFrameLookup`'s CBA4
arm (both `--sweep`-graded), and forced back to 2 for an off-camera object by A934.

Measured cost of the private copy, on the scene-8 GATE cutscene seeded at the pendant touch
(`saves/field_re/scene8_gate_cutscene.state`, obj3 mid `B7 20 06`):

| instrument | before | after |
|---|---|---|
| `--vmdiff` first divergence | **f16**, obj3 parked on B7, **ours = 0 ip-steps in 1500 frames** | f154, obj11 past `EA 18` (the song-load freeze, §4) |
| `--vmdiff` with `VMDIFF_ANIM=1` | f16 (the mimic's clock is not the ROM's) | — |
| `Belthasar` same cutscene, same A cadence | `DD 71` at **f1638**; ROM: f2447 | (see §4) |
| `vmdiff-bedslide` | pinned defect **f85** (obj1 `AB` 0434) | **CLEAN 300** |
| `vmdiff-withpos-bedslide` | f85 | **CLEAN 130** |

## 2. What moved

* **`src/move.c` `CtVmAnimFamily(r, script, ip, idx, out)`** — the seven arms (0xAA/0xAE/0xB3/0xB4,
  0xAB/0xB7, 0xAC) lifted VERBATIM out of `CtFieldRunnerOne`'s switch; the only edit is the operand
  reads, which go through `script` (the live VM's body is host memory, not `$7F:2001`) exactly as
  `CtVmItemFamily`'s do. The switch cases now call it; `--sweepops` stays 12600/12600.
* **`src/ct_vm.c step()`** — the anim arm calls the body on the flat image and then DERIVES the
  renderer's C fields from the bytes (`ctAnimSyncActor`: mode from `$1780,X`, index from `$1680,X`,
  the once-id from `$1781,X`, the held frame from `$1301,X`, the cursor from `$1681,X`, busy from
  `$7F0B01,X`). The same helper is the WRAM seed's derivation (`CtSceneSeedFromWram`) and the init
  pass's (`runFunc`), so the C fields can never disagree with the image. The carry is the handler's:
  SET for the ANIMESET family, CLEAR for `0xAC` (2F94) and for the arm/park visits of `0xAB`/`0xB7`.
  `animGen` (the renderer's restart trigger) bumps on every non-park visit.
* **`CtSceneAnimFinished`** — a no-op while the legs are on and a ROM image is present (the drain
  owns the byte); otherwise (`CT_FRAMELEGS=0`, or a dat-only run with no `$E4` tables to read) the
  renderer's end-of-playback stands in for the ticker's 2 → 1, so those hosts do not hang.
* **A934 on the flat image** — `r8/w8` stop at `$7F:03FF` (`WM_LEN`), so the existing
  `if (r8(m, 0x7F0B01+idx)) w8(..., 2)` had read 0 and dropped its store since the day it was
  written. It now indexes `R = m->s`, the buffer the legs write.

## 3. What it exposed: the live PC loader wrote no tables

With the completion on the byte, four fresh-load checks went red (scene 2, scene 120, scene 7 ×2)
while every seeded lockstep went CLEAN. `CT_DUMPWRAM` on a fresh scene 2 said why:

```
obj1 Crono  $1380/1=0000 $1300=00 $1480=0000 $1500=0000     <- the lead's anim tables: ZERO
obj8 Mom    $1380/1=EDA8 $1300=DD $1480=0018 $1500=5270     <- npcLoadMirror filled them
obj9 cat    $1380/1=27D8 $1300=DE $1480=0014 $1500=6710
```

The live PC arms (op 0x57/0x5C/0x62/0x6A/0x68/0x6C/0x6D and op 0x80's three in-party arms, all of
which `JSR C0/448D`) wrote only the C-side actor. The drain reads `$1380/$1500` to walk a PC's
sequence, so with the tables at zero a PC's `$7F0B01` never reached 1 and the bedslide's `AB`
parked forever in `Belthasar` — while `--vmdiff`, seeded from the oracle's tables, was CLEAN.
`pcLoadMirror` (ct_vm.c) now runs the same descriptor walk `loadGfxMirror` does for NPCs over the
seven PC records at `$E4:F000 + id*5` (C0/449B-4518 = C0/4595-4620 with the base changed) with the
PC tail: ROM-direct graphics pointer (`[$AF],Y` at C0/4529, no dedup/decompress — a PC's sheet is
resident), `$0C00/$0C01 = $20`, `$1680 = 0`, `$1681 = 0`, `$1600 = 1` (C0/453B-454D). It does not
write `$1B01` (three NOPs at 4498-449A); the op-0x80 arms do. Confirmed against the ROM: all seven
immediate loaders funnel through `421A STA $D9 / BRA ->426A`, and only the in-party arms reach
448D (`42B5 / 4329 / 43DC`); the not-in-party arm (428C) stores `$1100 = $80` and does not.

After the mirror: scene 2 and scene 120 came back byte-identical to their pins; scene 7's two
button-poll hashes moved (re-pinned, button-dependence re-verified: no-press A153AB27 / X 912D2B75 /
Y 401AB5FD) because the PCs' blocking anime at 0x0256 now runs the ROM's ping-pong length
(completes between f120 and f150 instead of before f89).

## 4. Still open, named -- and one claim RETRACTED

* **RETRACTED: "the mover residue at f46".** The first cut of `vmdiff-scene8-gate-cutscene` injected
  `0x100` as A. `VMDIFF_BTNSCRIPT` is the `$00F2/$00F3` layout (A = `0x80`); `0x100` there is RIGHT,
  and a D-pad bit arms the harness's LEAD SYNC (`main.c`, `if (injBtn & 0xFF00)`), which overwrote
  obj3's position with a stale oracle sample on the press frame f46. Nothing about the mover was
  measured. (`--monitor`/`--peek` scripts ARE LakeSnes serial order, A = `0x100` -- two grammars.)
* **The real next divergence, f154, is the SONG-LOAD FREEZE.** obj11's `EA 18` (play song $18) posts
  `$1E00 = $10 / $1E01 = $18` at its f154 visit and `$1E05` parks the song id (gate closed); the
  oracle's `$1196` then sits on the op for **39 frames** (f154 -> f193) because `C0/3C43 JSL $C7:0004`
  performs the upload SYNCHRONOUSLY -- the field main loop, every script, mover and box, freezes until
  it returns. Ours steps past the op at once (the audio service is asynchronous; `--vmdiff` has no
  audio at all), so from every song change on, the port runs ahead by the upload time. The duration is
  emergent from the transfer loop and the song's size; the derivation route is the same as
  `copyFreeze`'s: measure per song on the oracle (`$1E05` != `$E0` span, or the IP hold), check the
  bytes/frame rate against audio.c's own upload byte counts, model the freeze from the byte count. Measurements, the two cost models tried and the port design: `song_load_freeze.md`.
* **Cutscene pacing in `Belthasar`** is still ~840 frames ahead of the ROM on this cutscene (`DD 71`
  at f1606 vs f2447); the song-load freezes are the first named contributor.
* **The renderer still plays from the C fields** (`animTickActor`, its own frame/delay/loops model)
  and writes `$0F01,X` back over the drain's commit (field.c:11134 / :11332). The next render slice
  is to DRAW the drain's `$0F01,X` and delete the write-back.
* `animOpState` / `animOpAddr` in `CtVmActor` are dead fields (left in `ct_vm.h`, which is stored
  CRLF with a lone LF line and is not worth a 665-line diff for two fields).

## 5. Checks

* `vmdiff-bedslide` / `vmdiff-withpos-bedslide` — CLEAN + liveness; mutations: ARM store 2 → 1
  (killed), COMPLETION arm removed (killed). Both paths run on this seed (taken before the arm).
* `vmdiff-scene8-gate-cutscene` — f154 ratchet (the song-load freeze) + liveness; mutation: COMPLETION
  arm removed (killed). The ARM mutation is deliberately NOT declared here: obj3 arrives already armed
  by the oracle, so it survives (measured) — a characterised limit of the fixture, not coverage.
* `field-cat-anime-once-duration` (pre-existing, oracle-timed cat `AB 03`) stays green: the
  NPC path was already driven by the seeded tables.

## 6. The RECLAIM has to clear the BYTES (2026-09-10 — the Telepod crouch)

User report from an F5 of the Telepod: *"crono has the wrong sprite at this moment … he is
crouched and should be standing."* Their capture is loc 8, storyline `0A`, `$011F = 0`, Marle's fn1
showing message 90. Crono is in the RIGHT PLACE — obj3 at (111,159) facing up, and the oracle's own
scene-8 RAM carries the same coordinate — but he is drawn as assembly frame 71, the crouched frame of
animeset 29, where the oracle's obj3 carries `$1680 = 00`.

**The sequence.** obj3's fn8 ends

```
0130  AA 1D     the crouch
0132  AD 30     wait 48
0134  E3 01     $011F := 1     <- UNLOCK, deliberately, BEFORE the return
0136  0E 00
0138  00        RETURN
```

so the animeset reclaim is allowed to fire at all (it is gated on `$011F != 0`, which is the
soda-minigame rule: a RUNNING script's animeset holds — section 4, and `ct-anim-family-delegation`).
Marle's fn1 then re-locks (`E3 00`) and CALLs obj3's fn9, `89 20 / 96 06 09 / AD 02 / 0F / 00`, which
has no animeset op of its own.

**What was wrong.** The reclaim cleared `sc->actors[obj].animMode/animIndex` and nothing else, while
`ctAnimSyncActor` rebuilds exactly those two fields FROM WRAM on every sync (`am == 1` takes
`a->animIndex = $1680,X` — section 2). A C-side clear cannot survive one sync. Traced on the
reproduction (`CT_CALLFN="3:8:2:100,14:1:2:460"`, `CT_PDBG=1`):

```
f411  am=1 ai=29 agen=4   fn8 arms the crouch
f459  am=0 ai=0  agen=5   the RETURN reclaims it     <- the C side was right
f460  ctlLocked=0         control opens
f468  am=1 ai=29 agen=5   ...and it is BACK, with the SAME agen -- not a new arm, the byte resurfacing
```

**The fix is the ROM's own RETURN-TO-AUTO — and the first cut picked the wrong routine.** That cut
copied C0/5667's three stores (`STA $1680,X`, `STZ $1681,X`, `STZ $1601,X`) and left `$1780` alone.
It fixed the pose and broke the WALK; the user came back the same day with *"crono moved to the
current position without animation. he walked completely idle"* (translated from Portuguese). C0/5667 is the IDLE arm **inside** the
auto-selector C0/5614 and is only ever reached with `$1780 == 0` — `5616 LDA $1780,X / BEQ ->5630`
sends modes 1 and 2 straight to `561F RTS`, *the script owns the anim* — so leaving the mode at 1
meant the selector never ran and never chose WALK for the scripted step.

**Measured in the oracle**, obj3 walking to the pod from `saves/field_re/scene8_gate_cutscene.state`
(`--monitor "7E1686:1,7E1786:1,7E1906:1,7E1986:1,7E1687:1" --diff`):

```
f0   $1680=01 $1780=02
f16  $1680=00 $1780=01
f23  $1680=01 $1780=00  velY=F1   phase 01 -> 02 (f30) -> 03 (f37) -> 04 (f44)
f46  $1680=00 vel=00              phase 00     -- arrival
```

Through a scripted walk the ROM sits in **mode 0** with **animeset 1** and the phase advancing. So
what the reclaim owes is op 0xAE's handler `C0/2E8F`, falling into the shared animeset tail at
`C0/2E74` — five stores, the mode included:

```
2E93  STA $1680,X  (A = 0)      2E74  STA $1780,X  (A = 0)     2E77  STZ $1601,X
2E7C  STA $7F0B01,X (0)         2E80  STZ $1681,X
```

With the mode at 0 the already-ported selector (`C0/5614` at the 13 move-op sites, `C0/568B` at the
seven exits) picks WALK while the step runs and IDLE on arrival — both reports answered by one
change. After it: f468 `am=0 ai=1`, the cycle advancing through f504, and f566 back to `am=0 ai=0
af=3`, the standing idle-up frame section 1's original measurement named.

**Pinned** by `field-scene8-crono-stands-after-fn8` (quick tier): f411 proves the run really arms the
crouch, f459 that the RETURN still reclaims it, f468 that the step WALKS, f504 that the cycle advances,
f566 that it settles back to the standing idle. One mutation — drop `STA $1780,X` and the walk goes
idle again — killed. The soda-minigame check, the one that requires a running script's animeset to
HOLD, stays green: the `$011F` gate is untouched.

⚠ Dropping `STA $1680,X` instead SURVIVES this scenario, so that mutation was removed rather than left
standing. With the mode store in place the selector writes `$1680` itself from f468, and in the window
before it (f460..f467) control is open and the free-roam arm owns the lead's animation — there is no
frame here whose drawn anim comes from `$1680` with mode 0. The store stays because C0/2E8F writes it
and `ctAnimSyncActor`'s mode-0 arm reads it; what would grade it is a PC left STATIONARY after the
reclaim, i.e. a FOLLOWER (the lead's free-roam path stamps every frame, a follower's does not) — the
scene-439 candy-shop Marle is the named case, and building that check is open.

⚠ A capture taken BEFORE this fix still draws the crouch when reloaded: the wrong byte is inside the
savestate. This fixes the writer, not saved bytes.
