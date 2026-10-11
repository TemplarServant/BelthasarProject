# COMPLETENESS CRITIC — audit of the 8-front derivation wave (2026-07-25)

Adversarial re-check of the eight returned dossiers **against the ROM and the oracle**, not against the
summaries. Every verdict below is either a `tools/dis.py` disassembly (flat file offset; HiROM, bank
`$XX` = flat `(XX-0xC0)<<16 | addr`), a direct ROM byte read, a repo command, or an observed
`build/ct.exe` value. Nothing here is taken on the authority of the dossier that asserted it.

**Headline: the wave is NOT read-only.** `git diff --stat` at audit time:

```
src/battle_fx_ice.h | 151 +++++++-------   src/ct_vm.c |  70 ++++++   src/ct_vm.h | 16 ++++
src/field.c         |  62 ++++++++++       src/main.c  |  36 ++++--
```

plus two already-**committed** audio implementations (`45a49bf` "THE SOUND SYSTEM BOOTS", `779817c`
"ct_field GROWS AN APU") and the `--aram` oracle mode (`73ba0dc`). Three fronts (audio-runtime,
battle-ice, movement) have live uncommitted engine edits in one shared tree. That is the single largest
risk in this wave and it is not mentioned in any summary.

---

## 1 — VERIFICATION LEDGER (what I re-derived myself)

### 1.1 audio-song-selection — **strongest front in the wave. Everything I checked held.**

| claim | my check | verdict |
|---|---|---|
| song = byte +0 of the 14-byte record at `$F6:0000`, read `C0/1B79` | `dis.py 0x1B53 0x50` → `1B77 LDX $FE / 1B79 LDA $F60000,X` | ✅ |
| `X = loc*14`, built `C0/092B` | `dis.py 0x92B 0x40` → `LDA #0E / STA $4203` … `ADC #0E00` for loc≥256 / `STA $FE` | ✅ |
| bit7 set = "do not change the music" | `1B7D BMI ->1B8F` (skips **both** the `$29AE` store and the play) | ✅ |
| 87 of 512 records have bit7 set; every other ≤ `$46` | ROM histogram: `bit7 count = 87`, `max non-FF = 0x46` | ✅ exact |
| the pre-song `$81 (00,FF,FF)` | `1B5F..1B73` verbatim | ✅ |
| `$7F01ED != 0` → replay the shadow instead | `1B53 LDA $7F01ED / BEQ ->1B5F / LDA $7E29AE / BRA ->1B83` | ✅ |
| loader chain `C0/00F4` = `092B` → `1B53` → `0960` | `dis.py 0xF4 0x18` | ✅ |
| the menu-return reload skips both | **`C0/01C3 JSR C0/0960`** then the identical `6DCF/7084/7F7E/A33B/09DD` tail | ✅ (the load-bearing instruction is at `C0/01C3`, not at the routine head `C0/01A5` the dossier cites) |
| `loc >= 0x1F0` never reaches `1B53` | `dis.py 0x66 0x18 1 0` → `LDX $0100 / CPX #01F0 / BMI ->0072 / JML $C2:0000` | ✅ |
| overworld has the real same-song compare | `dis.py 0x23000` → `23009 LDA $1B9B,X / CMP $02AE / BEQ` | ✅ |
| battle start `C0/1BAB`, `$2A1F` bit6, `$01FA = $45` | `dis.py 0x1BAB` (`LDA $7E2A1F / BIT #40 / BNE ->1BD5`; else `$1E10=FF`, `LDA $FA`, cmd `$14`) + `dis.py 0xC50` (`LDA #45 / STA $FA`, and `$FC=#53`, `$FB=#65`) | ✅ |

**LIVE CROSS-CHECK (the best evidence in the whole wave).**
`build/ct.exe --read "wram:0029AE+1,000100+2,001E00+6" --from saves/base_loc6.state --frames 2` →
`$0100 = 0x0006` (loc 6), `$7E29AE = 06`, `$1E00-$1E05 = 00 06 C8 05 00 E0`.
ROM: `rom[0x360000 + 6*14] = 0x06`; `rom[$C7:241E + 6*2] = 0xC8`; `rom[$C7:241D + 6*2] & 0x0F = 0x05`.
So record → shadow → `$1E01`, and `$1E02/$1E03` = the two song-parameter tables, **all four agree**.
`rom[0x360000 + 2*14] = 0xFF` — Crono's bedroom really does decline. ✅

### 1.2 audio-runtime — protocol confirmed, one self-declared gap is now stale

* `JSL $C7:0004` call sites: ROM scan for `22 04 00 C7` → **exactly 59**. ✅ (list matches the
  dossier's bank distribution: 22 in `C0`, `C1/1B50`, `C1/40DD`, `C1/61B5…`, `C2/…`, `CD/…`, `FF/…`.)
* `$C7:0004 = JMP $0140`: `rom[0x70004] = 4C 40 01`. ✅
* `C7/061C` port order: re-disassembled with **16-bit X** (`dis.py 0x70625 0x30 1 0`) —
  `STA $2143 / $2142 / $2141`, `LDX #0400`, `STA $2140`, `CMP $2140 / BEQ / DEX / BNE`. ✅
  (The dossier's transcript prints `LDX #00` + a garbled `TSB $8D`; that is a dis.py width artifact,
  not an error in the conclusion. The retry budget is **0x400**, not the 1024-decimal wording.)
* `$1E05` mutex: `C7/0157 LDA $05 / BPL ->70192` — bit7 **clear** drops the command; `$E0` passes,
  a song id (`$45`) drops. ✅
* The six field-VM sound opcodes: dispatch table `C0/5D6E + op*2` →
  `E8→3C11, EA→3C2F, EB→3C4C, EC→3C98, ED→3CBC, EE→3CC5`. ✅ **All six end `CLC`**
  (`3C2D`, `3C4A`, `3C96`, `3CB…`, `3CC3`, `3CCE`) — the scheduler claim is real.
* `$ED`/`$EE` park-on-op: `3CBC TYX / LDA $2141 / BNE ->3CC3` (**no INX** → re-executes),
  `3CC5 TYX / LDA $2143 / AND #0F / BNE ->3CCE`. ✅
* The two refutations: `C0/2C41` is `LDA $7F3415 / BIT #02 / … ADC $341D / STA $341D` — an
  accumulator, no `$1E00` write, no `JSL $C7`. ✅ `C0/2824` is a `JSR C0/881E` loop counting `$19`
  to `#0F` — also not sound. ✅

### 1.3 audio-boot

`rom[$C7:0000-003F]` = `4c 38 00 ea | 4c 40 01 ea | 4c 26 03 ea | 4c 7b 03 ea |` then
`c3 24 | 09 43 | b4 40 | b7 42 | dd 42 | f3 42` (**6 sources**, first = `$24C3`) then
`00 02 | 00 2f | 00 47 | 00 1e | 80 1f | 00 1f` (**6 destinations**: `$0200, $2F00, $4700, $1E00,
$1F80, $1F00`). The correction of the old dossier is **confirmed byte-for-byte**, and the observed
`inPorts[2..3] = 00 02 → 00 2F → 00 47` are literally destinations 0,1,2. ✅
`22 00 00 C7` (`JSL $C7:0000`) appears **exactly once**, at flat `0x53`. ✅
`rom[$C7:0AE9] = 0x53`. ✅ The 7 words at `$C7:0028` are exactly the ones flagged. ✅

### 1.4 battle-ice-fx-anchor

* `rom[CE/116D] = 1B 09 72 03 73 00 20 10 …`, same `1B 09` opener at `118C`, `11A3`, `11BA`;
  each track carries a later `1B 00`. ✅
* `C1/561F`: `LDA $A010 / CMP #08 / BCS ->15652` → role ≥ 8 writes **`$A240,X` / `$A250,X`**
  (`X = $A2B6*2`), role < 8 writes `$1D0C,X`/`$1D23,X` via `$A014,X`. ✅
* The resolver: `dis.py 0x1764A` → `TYA / SEC / SBC #09 / TAY / LDA $A014,Y / TAX` — **mode 09 ⇒
  `$A014[0]` ⇒ role 0**, then `$1D0C,X + $9708,X` / `$1D23,X + $9713,X`. ✅ The refutation of
  "target-anchored" is sound.

### 1.5 battle-status-render

* `CC/F77F` really is a 4-byte-record table; row 2 = `01 04 04 1A`. ✅
* `C1/2F97` walks it: `LDA $CCF77F,X / BMI (terminate) / REP #21 / ADC $A2 / TAY /
  LDA $5E4A,Y / AND $CCF780,X`, with `$A2` from the per-combatant word table `$CC:F8ED`
  (`0x0000, 0x0080, 0x0100, … 0x0500` — clean 0x80 stride, 11 combatants). ✅
* The pose shift is **exactly** `$A440*4` applied to **both** pointers:
  `12E6B LDA $A440 / REP #20 / ASL / ASL / STA $82 / ADC $A1FE / STA $A1FE / ADC $A1E8 / STA $A1E8`. ✅
  So plan step 3 ("`+0x0C` becomes `+cls*4`") is genuinely structural, not a patch.
* `rom[CC/F5FF] = 0x0A` (the period-10 byte). ✅ — but see §2.5 for the cadence problem.

### 1.6 menus

* Save gate `C2/9875`: `STZ $0D32 / STZ $0D34 / LDA $0100 / CMP #01F0 / BCS ->298B5 /
  DEC $0D32 / LDA $7F01CE / BMI ->298B5 / DEC $0D34`. ✅ formula exact (0 = allowed).
* Consumer `C2/CEDC`: `LDA $0D34 / ORA $0294 / ORA $0299 / ORA $029E / AND #40 / BEQ (open)`. ✅
* Form gate `C2/C627`: `LDA $0F0B / CLC / ADC $0F0C / CMP #02 / BCS (open)`, and `C2/C649`
  `LDA $0F02,X / AND #C0 / BNE (skip)`. ✅ Both claims hold.
* Party-lock: `C2/CD55 LDA $7F01DF / AND $FFF9BB,X / BEQ (not locked)` … `DEC $0F0B / LDA #40`,
  and `rom[FF/F9BB] = 80 40 20 10 08 04 02 01`. ✅ exact.
* Item names: `C2/98ED LDA #0B5E / STA $7D00,Y / ADC #000B / CPY #01E4` → 0xF2 entries, stride 11,
  and `0x0B5E + 0xF2*11 = 0x15C4` = the tech-name base. `C2/F2E2` returns `Y = $7D00[id*2]`,
  `A = $CC0B`. ✅ The "no variable-length key-item region" conclusion is proven from code.
* Save-slot base: `C2/D1D3 LDA #0200 / ADC $7B / ADC $4216 / TAX / LDA $300012,X`. ✅ the `+0x200`
  defect is real.
* The bogus pin exists: `tools/regress_checks.json` `/checks[41]/expect[7]` =
  `menuform: screen=5 party 00,01 -> 01,00 swap PASS`. ✅

### 1.7 movement-pinned-open

* `C0/881E` order: gates read at `8833 ($0162)` / `8838 ($011F)`, `JSR 88EE`, `$0120 → JSR 8A6D`,
  then `884C JSR 9175`, `884F JSR 99DE`, `8852 JSR 91AC`. ✅
* `99DE` really clears `$1D2E` (`99ED`, `99FD`) and `91AC` really overwrites it
  (`91B8 LDA #10 / STA $2E`, `91BE LDA #F0 / STA $2E`). ✅ so post-frame `$1D2E/$1D30` are camera.
* `C0/3040`: `LDA #01 / STA $1A01,X / JSR C0/9E29 / … / CLC / RTS`. ✅ park-on-op.
* `JSR C0/9E29` ROM-wide (`20 29 9E`): **exactly one** site, `0x3047`; zero `JSL`. ✅
* `docs/DEBUG_SURFACE.md:85-86` do contain the aspirational `270/270` cells. ✅

### 1.8 structure-audit

* `src/battle.c` boundary is real (comment 394-395, `kPcBlock` at 396). ✅
* `g_owNoMarker` exists and `worldCompose`'s read is an **empty `if` body** (`field.c:534`), i.e. a
  documented no-op — relocating it into `World` is safe. ✅ (see §2.8 for the staleness problem.)

---

## 2 — CLAIMS THAT ARE UNCITED, OVERBROAD, OR REFUTED

### 2.1 ⛔ REFUTED — "`$1900,X` is written only by `C0/9E29`" (movement, dossier line 321)

ROM scan for `9D/9E/99 00 19` and `9D/9E/99 80 19`:

```
$1900,X/Y writers: 305F 307D 3084 30B5 30F3 30FE 4378 442B 4E4C 9E38 9E53
                   A26F A2AD A2D2 A2FF A31A AC6B ACD7 ACE9 ACFF ADEF AF1C  (+4 outside C0)
$1980,X/Y writers: 3062 309D 30A9 30B8 3132 3146 4D9A 4DED 4E14 4E54 9E3B 9E5D
                   A272 A2BA A2D5 A302 A327 AAEB AC6E AC8E ACA7 AD02 ADF4 AF21
```

The very next routine, `C0/305D`, opens `LDX $6D / STZ $1900,X / STZ $1980,X`. The **narrow** claim
("`9E29`'s sole caller is `3047`") is true; the **broad** one is false, and the plan's step 3 rests on
the broad one. Consequence: the proposed harness predicate "compare only where the oracle refreshed
`$1900,X`", if implemented as "the `881E` gate was open", is a *heuristic standing in for a
mechanism* — the exact forbidden pattern, applied to a test harness. It would mask real divergence on
any frame where `A2xx`/`ACxx`/`ADxx` (the mover/integrator block) touches the player's slot.

### 2.2 ⛔ STALE — audio-boot's "Nothing in the port has been built or run"

False at HEAD. `45a49bf` + `779817c` are committed, `src/audio.c`/`src/audio.h` exist, the check
`audio-c7-driver-upload` is pinned green (`13884/13884 bytes byte-exact over 6 blocks`), and plan
step 1 ("add an `--aram` dump mode") was already delivered by `73ba0dc`
(`docs/DEBUG_SURFACE.md:18`). The front's plan must be re-based before anyone executes it.

### 2.3 ⛔ STALE — audio-runtime's "No existing ct.exe mode can see the `$2140-$2143` writes"

`--aram` prints "the four APU I/O ports (`$2140-43` **in + out** = the sound-command channel)".
The CPU→APU direction *is* the written values. The front's own top-priority admitted gap
("DIRECT observation of the register writes") is closeable in one command today; it should be closed
before the protocol is trusted, not carried forward as a permanent gap.

### 2.4 ⛔ NOT REGENERABLE — the ice fixture as shipped in the worktree

`src/battle_fx_ice.h` was **translated**, not re-baked: the header still says
`Source ice_precast.state` and the new comment says `Capture anchor was (179,111)`. First sprite went
`{171,87}` → `{-8,-24}` = `171-179, 87-111`. So the shipped data is a rigid transform of a capture
that **is not in the repo** (dossier gap I1, honestly flagged) — and it therefore still carries
`ice_precast`'s victim-dependent shatter bearing (gap I2), which the dossier measured as 0/+1/+3 px
of Y error at rel 205-220. Shipping this and re-pinning two pixel canaries converts an unreproducible
capture into two green checks. That is the worst possible combination.

### 2.5 ⚠ UNSUPPORTED CONSTANT — the status icon's animation cadence

The summary states the ROM period byte is 10 (`rom[CC/F5FF] = 0x0A`, confirmed) *and* that the measured
cadence is "12 and 15 consecutive steps", *and* that the pose-tick↔render-frame multiplier is 16 and is
explicitly **NOT DERIVED**. Three mutually inconsistent numbers. Any of them hardcoded into
`battleStatusIconRender` is an eyeballed constant. The icon's *position*
(`liveFoot + (-8,-48)`, from `CC/F6D4[group + cursor*4] + $1D0C/$1D23`) is derived and safe; the
*swap rate* is not.

### 2.6 ⚠ UNCITED — menus' chrome claims

"`ctMenuChromeEnteredTM[4]/[5]` are the REFUSED (hover) captures and still contain the ribbon box,
leaving a 128x48 unpainted hole" is a statement about repo data with no cited command in the summary
(no `md5`, no cell dump, no render diff). It is plausible and probably right, but it is the basis of
plan step 3 and should carry its evidence.

### 2.7 ⚠ INFERRED, LABELLED AS DERIVED — movement's f0..f73 lock window

The dossier itself admits `--tracemove` only prints f68..f100 and that "the whole f0..f73 window is
locked is … an inference" corroborated by `270-74=196`. That is arithmetic agreement, not observation.
Fine as stated in the dossier; the **summary** presents it as fact. Keep the dossier's honesty.

### 2.8 ⚠ ALREADY STALE — every line number in structure-audit

`src/field.c` is **7465** lines now, not 7359 (the audio + ice work landed on top). `g_owNoMarker` is
at `field.c:335`, not 334. `src/*.h` is **68** files, not 67, totalling **2,256,065** bytes, not
2,092,321 (only 16 carry an `AUTO-GENERATED` banner, so the "49 generated blobs" figure uses an
unstated classification I could not reproduce). None of this falsifies the audit's *structure*, but
**every cut boundary must be re-derived at apply time**, and the plan must say so.

---

## 3 — MANDATE VIOLATIONS IN THE PLANS (and in code already in the worktree)

### V1 — `fieldLocationSong` is gated on the **presence of an audio device** (worktree `src/field.c`)

```c
static void fieldLocationSong(CtScene* vm, int loc) {
  if (!g_fieldAudio || !vm) return;      // <-- host capability gating canonical WRAM
  ...
  m[0x29AE] = song;                      // C0/1B7F  -- never runs without an APU
```

The ROM writes `$7E29AE` and stages `$1E00-$1E03` **unconditionally** (`C0/1B5F-1B8B`); only the
`JSL $C7:0004` is the sound hardware's side of the wire. As written, a `CT_NOAUDIO` / no-SDL /
no-`CT_ROM` run has different WRAM from an audio run — and `$29AE`/`$1E0x` are exactly the cells a
`--vmdiff` or a battle-return replay reads. `src/ct_vm.c` already gets this right (it stages the
mailbox in WRAM and only *posts* through the sink). `field.c` must match its sibling.

### V2 — the song is issued **after** `CtSceneCreate`, i.e. after the init scripts have run

`fieldLoad` does `f->vm = CtSceneCreate(...); fieldLocationSong(f->vm, scene);`. `CtSceneCreate` runs
the object init pass (`runFunc` at `ct_vm.c:1765`). In the ROM the order is the opposite:
`C0/00F4 = 092B → 1B53 (song) → 0960 (tileset/palette/map/script)`. Any scene whose **init** function
carries op-`0xEA` currently has its script-chosen song overwritten by the location record. The port
must issue the song at the same point in the chain, before the script exists.

### V3 — the double call site is a spot-fix

`fieldLocationSong` is called from `fieldLoad` **and** again from `main()` right after the SPC comes
up ("the scene already loaded before audio came up"). That second call exists only to paper over V1's
early-return on the first scene. Fix V1 and V2 and the second call disappears.

### V4 — audio-song plan step 2: "Gate it so it runs on a real location load only"

"A real location load" is not a ROM predicate. The ROM has **two entry points** —
`C0/00F4` (full chain, includes the song) and `C0/01C3 → C0/0960` (partial chain, no song). Mirror the
two entry points; do not add a boolean.

### V5 — audio-song plan step 6: overworld "on every walk step re-evaluate the region"

`C2/3009`'s *cadence and caller* were never derived (the dossier derives the compare, not who runs it).
"Every walk step" is a port-side scheduling decision. Also the whole step depends on `$7E1B9B`, whose
**loader does not exist in the dossier** (admitted: no indexed store to `$1B9B` anywhere in the ROM).
Implementing the table means inventing its provenance. **Defer.**

### V6 — battle-ice: `actAttacker` + clamp instead of the ROM's `$A014[0]`

```c
int atk = (h->actAttacker >= 0 && h->actAttacker < 3) ? h->actAttacker : 0;
```

The derived selector is `$A014[0]` (role 0 = the acting combatant), which is in `mem` and which the
dossier itself measured (`01 02 00 06`). Substituting a port-side "attacker index, clamped to seat 0"
is a heuristic in place of the RAM the ROM branches on — and it silently anchors on seat 0 for any
non-PC caster. Read `$A014[0]`.

### V7 — movement plan step 2 deletes coverage rather than re-phasing it

Demoting `$1D2E/$1D30` to an "informational" row removes the only check on values `CtPlayerControl`
still computes. `--checkctrl` already proves an **in-situ hook at `C0/881E`** works (400/400). The
correct fix is to move `--playermove` onto that hook (compare after `9175`, before `99DE`), not to
stop comparing. Same for step 3 (see §2.1).

### V8 — battle-status plan step 5, if it hardcodes the icon swap rate (see §2.5).

### V9 — menus: replacing the bogus `menuform` pin

Deleting `menuform: screen=5 party 00,01 -> 01,00 swap PASS` is right, but it must be **replaced by a
check that asserts the refusal** (screen 5 does not open at storyline 08, `$0F0B+$0F0C = 1`), not
silently dropped. And `ctMenuChromeEnteredTM[4]` (Form) has **no capture** — it must stay wrong-and-
documented rather than be synthesized from [5].

### Not violations (checked and cleared)

* `ct_vm.c`'s six-opcode handler is a faithful transcription (`$E8` `$1E02=0x80`/cmd `$18`; `$EA`
  shadow+cmd `$10`; `$EB` the `$81/$82/$83` triple with `$1E03=FF` on the first only — matches
  `C0/3C4C-3C96` exactly; `$EC` raw; `$ED/$EE` park-on-op). The `$1E03`-is-sticky comment is correct.
* `battleStatusScan` / `+cls*4` is genuinely general (§1.5).
* The `--playermove` gate re-phasing (carry the previous frame's post value) is *equivalent to simply
  deleting the three overwrite lines*, since `snap` is taken pre-frame. Harmless, but the added
  bookkeeping is noise — prefer the deletion.
* `vmSoundPort` returning 0 with no sink (→ `$ED/$EE` fall through) is a **decision, not a
  derivation** — the dossier says so. Acceptable, but it must be documented as a `--vmdiff` risk: the
  oracle has a real SPC that can hold `$2141` non-zero and park the VM while the port advances.

---

## 4 — CROSS-FRONT COLLISIONS AND THE SAFE ORDER

| file | fronts that touch it | collision |
|---|---|---|
| `src/field.c` | audio-song (`fieldLocationSong`, sink install), audio-runtime (same), battle-ice (`battleIceRender`), battle-status (per-seat stance loop + icon), **structure-audit S7/S8/S9/S10** (cuts 2373 lines of battle render + text + overworld + battle bridge out of it) | **severe** — S9 moves the exact region battle-ice and battle-status edit |
| `src/ct_vm.c` / `ct_vm.h` | audio-runtime (`CtSoundSink` + six ops), **structure-audit S2/S3/S4/S5** (privatise `CtScene`, cut camera / vmdiff bridge / followers) | **severe** — S2 is the prerequisite for S3-S5 and rewrites the file head where the sink now lives |
| `src/main.c` | movement (`runPlayerMove`, `runPlayerFrame`), **structure-audit S11-S16** (cut ~100 `runXxx()` bodies out) | **severe** — same functions |
| `tools/regress_checks.json` | movement (3 re-pins), battle-ice (2 pixel canaries), menus (`menuform` replace + new OW save check), audio-song (new checks) | **serialize** — one front at a time, each with its own suite run |
| `docs/DEBUG_SURFACE.md` | movement (fix the two aspirational cells at :85-86), audio (already added `--aram` at :18) | minor |
| `disasm/notes/battle_status_pipeline.md` | already dirty in the worktree | minor |

**Ordering law: every behavioral front lands BEFORE structure-audit, and structure-audit lands into a
tree with nothing else pending.** Its cut boundaries are already stale after two audio commits; letting
five more fronts land on top of a line-range plan guarantees a bad cut. Within the behavioral fronts,
the audio VM-opcode change goes first because it is the only one that can move `--vmdiff` — everything
downstream should be measured against a tree where that has already been proven.

---

## 5 — RECOMMENDED EXECUTION ORDER

See the structured `implementation_plan`. Two entries deserve emphasis here:

* **Step 0 is not optional.** No front captured a `regress.py` baseline (structure-audit says so
  explicitly), and the tree already carries five files of uncommitted engine changes from three
  fronts. Without a baseline and a per-front split, no later "the suite is still green" claim is
  attributable to anything.
* **Nothing that re-pins a canary may land in the same commit as the change that moved it** without
  the dossier's before/after fnv **and** the reason. The ice canaries in particular re-pin against a
  fixture whose source state is not in the repo (§2.4).

---

## 6 — WHAT MUST NOT BE ATTEMPTED YET

| item | why |
|---|---|
| Overworld region→song (`$7E1B9B`) | its loader does not exist in the ROM scan; the table's provenance is unknown. Implementing it = inventing the source. |
| Battle-end jingle (song `$3A`) | the `$7E2A21` bits 0/4 setter was never found (only reads + one `STZ`). Firing it unconditionally on victory would be pure invention. |
| Form (screen 5) entered layout / `ctMenuChromeEnteredTM[4]` | the ROM refuses to open the screen on **every** state in the repo; there is no capture and no way to make one without a later save. Synthesizing it from screen 6 is invention. |
| Save-slot USED layout | all three slots read empty on every state; the name/LV/time geometry in `CtMenuRenderSave` is unverified. |
| Status icon animation cadence | 10 vs 12/15 vs 16 unresolved (§2.5); the tick↔render phase law is the standing `battle_anim_engine.md` §D O7 boundary. |
| Ice shatter bearing | victim-dependent (`76 00 03` = caster→victim); no bake can be correct for two different victims. Needs the effect-object mover port (`1B`/`85`/`76`/`A8` + `CF/F640`), not another fixture. |
| Any SFX **semantics** (`$1E02=$80`, id→sound) | lives inside the 7151-byte SPC700 blob. |
| structure-audit **S18** (audio wiring) | explicitly outside that front's own behavior-preserving guarantee. |
| The `~2950-line main()` in `field.c` | structure-audit deliberately excluded it; it needs a state struct = a design change with real regression surface. |
