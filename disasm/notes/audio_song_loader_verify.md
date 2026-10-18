# AUDIO — ADVERSARIAL VERIFICATION of the three song-loader dossiers

Target: `audio_song_loader.md` (front 1, disassembly), `audio_song_data.md` (front 2, ROM data),
`audio_song_trace.md` (front 3, live capture). **Nothing was re-derived on trust.** Every claim below
was re-executed here: `python tools/dis.py` for ROM, `build/ct.exe --read/--aram/--trace/--teleport/
--savestate` for the oracle. Default verdict when a check could not be made: *refuted*.

Read-only session. Nothing under `src/` was edited, nothing was built.

---

## 0. VERDICT

**Porting `$C7:01A1` is SAFE TO ATTEMPT NOW.** The hot path is fully derived and I reproduced it
byte-for-byte on **three** song ids and on a fourth transition that exercises two paths both dossiers
listed as never-observed. Two named gaps are now **CLOSED** (§4). One front-1 "harness caveat" is
**REFUTED** (§3.1) and one front-1 semantic label is **WRONG** (§3.2, front 2 has it right). Front 2's
port spec has a **fatal omission** (§3.4) and front 3 carries a **false gap** (§3.5). None of these
block the port; all of them would waste a day if the implementer trusted the wrong sentence.

The one thing that must be *decided* (not derived) before code lands is **frame accounting** (§5).

---

## 1. WHAT I RE-DERIVED AND CONFIRMED

### 1.1 The song-id → ROM-address arithmetic (the highest-risk claim class) — HOLDS

Independently verified for **three** ids by matching ROM bytes against the oracle's ARAM
(`--aram --from S --out f`, then a Python compare):

| id | state (how made) | sequence | samples | DIR `$1E80` | `$1F40`/`$1FC0` | `$F120` |
|---|---|---|---|---|---|---|
| `$06` | `candy_shop_lake.state` | `$CA:9EC3` len 3045 → **0/3045** | 10 blocks → **0 mismatches** | 10/10 exact | exact + fill | **exact** |
| `$1E` | `--teleport 12 8 8 0 --from candy_shop_lake.state --frames 240` | `$CB:0B32` len 1472 → **0/1472** | 4 blocks → **0** | 4/4 | exact + fill | exact (all-zero) |
| `$08` | `--teleport 19 8 8 0 --from <the $1E state> --frames 300` | `$CA:ABF4` len 1468 → **0/1468** | 9 blocks → **0** | 9/9 | — | — |

All eight table bases tile exactly (`$0AEA`+62·3=`$0BA4`+62·2=`$0C20`+62·2=`$0C9C`+62·2=`$0D18`+83·3=
`$0E11`+83·32=`$1871`+83·36=`$241D`+83·2=`$24C3`), song count `rom[$C7:0AE9] = $53`.
**The arithmetic holds.** No id, stride or base is off by anything.

Independent corroboration nobody used: **`$C7:0028-$0037` literally names six of the eight tables**
(`0D18, 0AEA, 0BA4, 0C9C, 0C20, 0E11, 0AE9`). Front 2 §A.1 and front 3 §E.1 both spotted this; I
confirmed the seven words by ROM dump. Two independent ROM sources for the same directory.

### 1.2 Contiguity / "nothing is compressed" — HOLDS

My own walk over all **152** length-prefixed blocks (5 boot + 2 SFX sets + 62 samples + 83
sequences), sorted by flat offset: **gaps = 0, overlaps = 0**, span `0x0724C3 … 0x0BF164` =
**314 529 bytes**. BRR structural check on all 62 samples: `len%9==0`, END bit set on exactly the last
9-byte block — **0/62 deviate**.
⚠ *Trap for the next reader:* the sequences are **not** in song-id order (`$0F` precedes `$0E`), so a
walk in id order breaks at index 14 with a 582-byte "gap". Front 2 states this; front 1 does not.

### 1.3 It is one blocking, straight-line busy-wait — HOLDS, strengthened

* **Operand scan I ran myself** over flat `0x701A1-0x70726`: **zero** `$42xx` operands; zero `WAI`
  (`CB`), `STP`, `CLI`, `SEI`, `MVN`, `MVP`, `JSL` opcodes. Only `$2140`×4, `$2141`×24, `$2142`×19,
  `$2143`×16. (Front 1 §14a says 25/17 for two of those — a scan-method difference, not a conflict.)
* **Frame-boundary PC probe**, corrected for the `--trace --from` 30-settle trap:

```
build/ct.exe --savestate pN.state --frames N ; build/ct.exe --trace 1 --from pN.state     (PC at N+30)
  abs f96 : C3 085C     f98 : C7 09E3    f100: C7 06FE    f105: C7 09E0    f110: C7 09E3
  f115: C7 09E3   f120: C7 09E3   f125: C7 09E9   f130: C7 0701   f131: C7 09E0
  f132: C7 057D   <- the SEQUENCE fast loop, i.e. the last sub-transfer
  f133: 7E 5AAD   <- out
```
  The CPU is inside bank `$C7` at **every** frame boundary of the 35-frame window and leaves at f133.
* **`--trace 300000 --from p70.state`** (= abs f100): **300000 / 300000 instructions in bank `$C7`**,
  `00:0500` NMI entries = **0**, `C7/09DA` handshakes = 5324, `C7/06F9` 3-byte streams = 5321,
  PC range `0342..09E9`. `C7/054C`, `C7/06CD` (bank-crossing paths), `C7/09EA`, `C7/0A85` = 0.

### 1.4 The window, the release and the acceptance signature — HOLD verbatim

My own per-frame sweep (`--savestate --frames N` + `--read "wram:1E00+8" --settle 0`, default settle
is 0, `src/main.c:4520`):

```
f98 .. f132   $1E00..$1E07 = 10 30 C0 06 00 30 00 00      <- cmd $10, song $30, $1E05 = BUSY
f133          $1E00..$1E07 = 00 30 C0 06 00 E0 00 00      <- $1E00 cleared, $1E05 = $E0 READY
```
`rom[$C7:241D + $30*2] = 06 C0` ⇒ `$1E03 = 6`, `$1E02 = $C0` ✔.
`build/ct.exe --aram --frames 139` prints, verbatim:
`evol=20/20 voicesWithGain=1 / v1 srcn=25 pitch=0BFC gain=07F7 adsrState=2 decodeOff=$B606 <-- SOUNDING`.

### 1.5 The sequence padding law — HOLDS, and the mechanism is `BPL` not a padded count

`python tools/dis.py 0x70539 0x70 1 0`: `$0539 LDX $0C` (X = the **raw** size word) … `$0571/$0591
DEX/DEX/DEX` … **`BPL`**. `python tools/dis.py 0x706E0 0x50 1 0`: the sample loop is the same shape
but ends in **`BNE`**. So the sequence sends `floor(len/3)+1` units, samples send exactly `len/3`.
I confirmed the overshoot bytes land in ARAM and equal the **next ROM block's head**:

| song | len | padded | ROM tail | ARAM tail |
|---|---|---|---|---|
| `$06` | 3045 | 3048 | `48 01 d0` | `48 01 d0` ✔ |
| `$1E` | 1472 | 1473 | `fc` | `fc` ✔ |
| `$08` | 1468 | 1470 | `6e 03` | `6e 03` ✔ |

### 1.6 History-dependent allocation + the srcn↔slot subtlety — HOLDS

Reproduced front 2 §D.4 to the byte. From the song-`$1E` state (`$1E20 = 2C 18 17 38`,
`$1E60 = 4901 6626 6BCF 77C3 8219`), `--teleport 19` (song `$08`) gives

```
$1F00 = 17 2C 3A 19 18 13 02 0C 04     (the SONG's list, ROM order)
$1F20 = 3A 19 13 02 0C 04              (the DIFF: only the six not already resident)
$1E20 = 2C 18 17 3A 19 13 02 0C 04     ($2C/$18/$17 KEPT IN PLACE at slots 0-2)
$1E60 = 4901 6626 6BCF 77C3 9338 9AC7 AAC6 B267 C0F5 D0D9   ($1EF6 = D0D9)
```
and **all 9 samples byte-exact at those history-dependent addresses**.
The subtlety a port will get wrong: **`srcn $20+k` is keyed to `$1F00[k]` (the song's list order), but
its ARAM base comes from wherever that instrument sits in `$1E20`.** Verified 9/9:
`srcn $20`→instr `$17`@`$6BCF`, `srcn $21`→instr `$2C`@`$4901`, …

### 1.7 Boot-time constants — HOLD, and one is *computed*, not a literal

`python tools/dis.py 0x700F0 0x28 0 0`:
```
700FF LDX #0100 / 70102 STA $1DFF,X / DEX / BNE      ; clears $7E:1E00-$1EFF ONLY (256 bytes)
7010A LDA $C740B4 (=$0201) / CLC / ADC #4700         ; = $4901
70112 STA $F8 / 70114 STA $60 / 70116 STA $F6        ; $1EF8 = $1E60 = $1EF6 = $4901
70128 LDA #05 / STA $F0 ... ADC #F1 / STA $F2        ; $1EF0 = 5, $1EF2 = $C9
7013B LDA #E0 / STA $05                              ; $1E05 = READY
```
**`$4901` is a ROM computation, not a constant.** `$7E:1F00-$1FFF` is **never** cleared by this boot;
⟦OBS⟧ at f20 `$7E:1F80+64` is all zero and `$1E60`/`$1EF0` are still 0 (the driver init runs ~f21-23).

---

## 2. NEW OBSERVATIONS — two named gaps CLOSED

### 2.1 ⭐ The DEFRAGMENTER (`$C7:025A`, driver command `$07`) **DOES** run in ordinary play

Front 1 §17.5 and front 2 §J.4 both say "derived, never executed in any trace". It executes, and here
is a two-command repro:

```
build/ct.exe --teleport 19 8 8 0 --from <a settled song-$1E state> --frames 300 --out A.state
build/ct.exe --teleport  5 8 8 0 --from A.state                    --frames 300 --out B.state
build/ct.exe --read "wram:1E20+32,wram:1E40+32,wram:1E60+34,wram:1F00+32,wram:1F20+32" --from B.state
build/ct.exe --aram --from B.state --out aram_B.bin
```
`A.state` = song `$08`, residents `2C 18 17 3A 19 13 02 0C 04`, `$1E60 = 4901 6626 6BCF 77C3 9338 9AC7
AAC6 B267 C0F5 D0D9`, `$1EF0/$1EF2 = 04/D1`.
`B.state` = song `$06` (`$1EF0/$1EF2 = 05/C9`):
```
$1E20 = 02 32 1D 1C 1A 03 04 1F 01 1B
$1E40 = 07A1 0ADD 0E6A 142E 0C21 022E 0FE4 0AF8 0EA0 0F5D
$1E60 = 4901 50A2 5B7F 69E9 7E17 8A38 8C66 9C4A A742 B5E2 C53F
$1F20 = 32 1D 1C 1A 03 04 1F 01 1B         <- 9 to load; only $02 survived
```
**Instrument `$02`'s 1953-byte sample physically MOVED from ARAM `$AAC6` to `$4901`** — byte-identical
to `rom[$C7:81AF+2]` at its new address, and `$AAC6` now holds instrument `$1F`. All 10 samples,
10/10 DIR rows and the 3045-byte sequence: **0 mismatches**. Only driver command `$07` can do that
(`$025A` is the sole writer of `$1E20,Y = $1E20,X` with a `$2141=07` preamble).
⇒ front 1 §17.5 / front 2 §J.4 downgrade from "unconfirmed" to **outcome-confirmed**. The byte order
of the three parameter units (src, dst, len) as read by front 1 §5a produces the right ARAM, so it is
at least outcome-correct.

### 2.2 ⭐ The echo-buffer CULL (`$C7:0A29`) also fires — on an ordinary field walk

Front 1 §12 reports "`$0A29` (the cull body) **zero** times". In the same `A → B` transition it fires:
`$1EF2` goes `$D1 → $C9`; slot 8 (instrument `$04`) ends at `$D0D9`, high byte `$D0 ≥ $C9`, so
`$0A12/$0A29` clears it **before** the diff runs. That is exactly why `$04` shows up in the to-load
list `$1F20` even though the previous song had it resident — the diff never evicts a *wanted*
instrument, so only the cull can explain it. **The cull is not a corner case; it fires when walking
between two ordinary field locations.**

### 2.3 The echo-delay reading is empirically settled

40-frame ARAM diffs (`--aram --from S --frames 60/100 --out`) with `ESA = $F1 - EDL*8`:

| state | EDL | predicted buffer | changed inside | changed outside, ≥ `$1000` |
|---|---|---|---|---|
| song `$06` | 5 | `[$C900,$F100)` = 10240 B | **10107 (98.7 %)** | 19 bytes (`$1DCC`, `$F2xx-$F5xx` driver scratch) |
| song `$1E` | 13 | `[$8900,$F100)` = 26624 B | **25528 (95.9 %)** | 2 bytes (`$1DCE-$1DCF`) |

`$F100 − ESA·$100 == EDL·2048` exactly in both cases — the canonical SNES echo-buffer geometry.
Also ⟦OBS⟧ `aram[$004D] = $1E02` and `aram[$0081] = $1E03` on both states (front 2 §B.2 ✔).

---

## 3. DEFECTS FOUND IN THE THREE DOSSIERS

### 3.1 ❌ REFUTED — front 1 §17.9 "mid-load savestates are not a reliable resume point"

Both halves are wrong.

*(a) The evidence is an artifact of a harness trap front 3 documented in the same session.*
`--trace N --from STATE` runs **30 settle frames before tracing** (`src/main.c:5222`,
`for (int sf = 0; sf < 30; sf++) … CtOracleRunFrame(s);`). So `--trace 1 --from b120.state` prints the
PC at **frame 150**, not 120. That is the entire source of "the restored CPU PC is outside bank `$C7`
(`C3:06DA`, `7E:3026`)". Corrected for the offset, the PC is inside `$C7` at every frame f98…f132
(§1.3).

*(b) Resuming DOES finish the load — bit-identically.*
```
build/ct.exe --aram --frames 133                 -> fnv32=F2DF1C42 nonzero=41598  spc->cpu = E0 00 00 00
build/ct.exe --aram --from cb110.state --frames 23 -> fnv32=F2DF1C42 nonzero=41598  spc->cpu = E0 00 00 00
build/ct.exe --aram --from cb100.state --frames 33 -> fnv32=F2DF1C42 nonzero=41598
--read "wram:1E00+8" --from cb100.state|cb110.state --settle 60  ->  00 30 C0 06 00 E0 00 00 (both)
```
This matters: the caveat discourages exactly the mid-load capture technique the port's validation
needs. **Delete it.** (Front 3's *different* warning — that `--teleport` out of an in-flight load
wedges `$1E05` — is real: `build/tp12.state` on disk today carries `$1E05 = $06`, `$1E00 = 00`,
`voicesWithGain = 0`, i.e. a permanently muted oracle.)

### 3.2 ❌ WRONG LABEL — front 1 calls `$1E03`/`$1EF0`/`$1EF2` an "SFX bank"

Front 1 headline #2 / §12 keep `audio_commands.md`'s label. Front 2 §B.1 corrects it and **front 2 is
right** (§2.3 above). The *arithmetic* is identical, so a port built from either is numerically
correct — but front 1 §12 adds an **uncited rationale**: "the top of ARAM is reserved for the SFX
bank's own samples". There is no citation for that sentence and it is false; the reserved region is
the DSP echo buffer. Front 1 §12's derived closing sentence ("`$FC` is 'set SFX bank'") inherits the
same error — `$FC` sets the **echo delay**.

### 3.3 ⚠ UNSUPPORTED — front 1 §2: "`$1E03` survives because SFX callers never write it"

`python tools/dis.py 0x1C08 0x20 1 0` → `C0/1C20 LDA #FF / STA $1E03` immediately before
`$1E00 = $81`. A caller does write it. Not load-bearing (the loader always refills `$1E02/$1E03` from
`$0A45/$0A4B`), but the sentence is an inference, not a derivation.

### 3.4 ❌ FATAL OMISSIONS in front 2's port spec (§I)

1. **Wrong order.** §I step 2 does the port write **then** the EDL switch. The ROM does
   `$0A53 CMP $F0 / BEQ / JSR $09FD` (switch + cull) **before** the `$0A5A` port block.
2. **The command block is NOT `$C7:061C`.** §I calls it "`C7/061C`'s port write — already ported".
   It is the *inline* block at `$0A5A-$0A79`, which has its own **unbounded resend loop**
   (`$0A79 BRA ->0A5A` on a 1024-iteration timeout). `$061C` has no such loop.
3. **The token seed is missing.** `$0A7B INC A / AND #7F / STA $84` is absent from §I. Without it
   `$1E84` is stale and **every** subsequent handshake writes the wrong token → the SPC never echoes
   → the port hangs in `$09DA`'s spin. This is the single most likely way a first port dies.
4. **The abort is missing.** `$0A80 LDA $2141 / BPL` → `$0A85` unwinds and exits without setting
   `$1E05` and without uploading anything.
5. **The GO block is missing.** §I step 11 jumps straight to `$1E05 = $E0`; the ROM first does
   `$0601: A=$E0; X=$400; STA $2141; STA $2140; spin-with-timeout`.
Front 1 §18 has all five. **Build the port from front 1 §18's shape and front 2 §A/§D's data.**

### 3.5 ❌ FALSE GAP — front 3 §K.2

§C.2 quotes the **sample** loop (`C7/06F9 … DEX×3 ; BNE`) and §E.3 then attributes the sequence
padding to "a `DEX/DEX/DEX ; BNE` loop preloaded with that padded count". The sequence uses a
different loop (`$054C`/`$0578`) ending in **`BPL`**, with X preloaded with the **raw** size at
`$0539 LDX $0C`. So §K.2's "the instruction that loads X with `3*(size/3+1)` was not located" is
looking for an instruction that does not exist — the padding is emergent. Front 1 §4 and front 2 §C.4
both have it right. **Delete gap K.2.**

### 3.6 ⚠ front 1 §18's port spec never shows the heap-base init

`$4901` appears only as prose in §15. A port that types `0x4901` is inventing a constant the ROM
computes (`rom16(0x740B4) + 0x4700`, §1.7). Front 2 §I's "constants a port must not invent" list is
the correct discipline; adopt it.

---

## 4. MANDATE AUDIT OF THE THREE PLANS

| item | verdict |
|---|---|
| ARAM dests `$1E80 $1F40 $1FC0 $2000 $F120 $2F00` hardcoded | **OK** — they are ROM immediates (`LDA #20 / STA $2143` …). |
| `$4901` hardcoded | **VIOLATION if literal.** Compute `rom16(0x740B4)+0x4700` (`$C7:010A`). |
| `62` / `83` hardcoded | **Read them**: `rom[$C7:0AE9]`; `(0x70BA4-0x70AEA)/3`. |
| `$0205` / `$C21F` fill hardcoded | **VIOLATION if literal.** Reproduce the `X=$FFFE` long,X bank wrap → `rom[$C8:0C1E]`, `rom[$C8:0C9A]`. |
| `$0F00`, `$F1`, `·8`, `$0400` timeouts | **OK** — ROM immediates. Keep the timeouts; they change behaviour when the SPC is slow. |
| "`$0AD8` list is inert" folded into `if (cmd >= 0x14)` | **Marginal.** It is an observation about `rom[$C7:0AD8] == $FF`. Implement the 3-line scan — bit 2 of the command byte feeds the token seed, so a wrong result hangs the port. |
| "one frame is enough" | **Nobody claims it.** But see §5 — the *inverse* assumption is the risk. |
| port-side scheduling stated as ROM fact | Front 3 headline #2 **is** a ROM fact (verified). Its PLAN bullet 1 is a port decision; label it as such. |

---

## 5. THE ONE DECISION THAT MUST PRECEDE THE CODE — frame accounting

Front 1 §14 is right that **the ARAM outcome is invariant** to how the port schedules the APU: every
byte is a two-way rendezvous. But the **game-visible** outcome is not invariant. The ROM stalls the
65816 for 35 frames with NMI masked; the field VM does not advance. If `CtAudioService` performs the
load "instantly" inside one `Belthasar` frame, **every location change desyncs the port from the oracle
by ~35 frames** — and `--vmdiff` is a lockstep comparator.

And the stall length is **not derivable from the ROM**: it is APU-timing dependent. Measured here, the
cold-boot load takes ~427 000 instructions live (35 frames × ~12 200) but only ~70 000 when resumed
from a savestate, because the spin counts depend on how far ahead the APU is. **The port must drive
the real vendored SPC and let the spins take as long as they take. It must never bake in "35 frames".**

---

## 6. ORDERED EXECUTION LIST

See the structured plan. Summary of the ordering rationale: the two primitives (`$09DA` handshake and
the `$0A39` command block **with its seed and abort**) must exist and be provably correct before any
data moves, because a wrong token is an infinite spin with no diagnostic. Then the pure-data pushes
(cheapest to validate: DIR / `$1F40` / `$1FC0` / sequence / `$F120`), then the stateful allocator
(diff → evict → defrag → append), which is the only part with history.

---

## 7. REPRO — every command used in this verification

```
python tools/dis.py 0x70A39 0x60 1 0     # record lookup, port block, token seed, ABORT
python tools/dis.py 0x709DA 0x30 1 0     # $09DA token / $09EA resync
python tools/dis.py 0x709FD 0x40 1 0     # $09FD EDL->ESA / $0A12 cull (byte-stride clear at $0A29)
python tools/dis.py 0x70150 0x60 1 0     # $0157 BUSY gate, dispatch, $0192 exit
python tools/dis.py 0x701A1 0x30 1 0     # head, range check, $01B3 STA $05
python tools/dis.py 0x701C4 0x60 0 0     # 16-bit A: the $1F00/$1F20 diff builder
python tools/dis.py 0x70431 0x40 1 0     # $1F40 push, the LDA $C70C20,X long,X wrap
python tools/dis.py 0x704A9 0x90 1 0     # $1EFC, mode, $2F00, sequence head
python tools/dis.py 0x70539 0x70 1 0     # $0F00 test + the sequence DEX/DEX/DEX / BPL
python tools/dis.py 0x706E0 0x50 1 0     # the sample DEX/DEX/DEX / BNE
python tools/dis.py 0x70601 0x40 1 0     # the GO block + $061C
python tools/dis.py 0x70734 0x30 1 0     # $0734 cmd|=4 + the $0AD8 scan
python tools/dis.py 0x700F0 0x28 0 0     # boot: $4901 = rom16($C7:40B4)+$4700
python tools/dis.py 0x1BA0 0x40 1 0      # C0/1BAB..1BC1 $1E10=$FF ; 0x1C08 -> C0/1C11 $1E10=0

build/ct.exe --aram --from candy_shop_lake.state --out aram_candy.bin
build/ct.exe --teleport 12 8 8 0 --from candy_shop_lake.state --frames 240 --out vtp12.state
build/ct.exe --teleport 19 8 8 0 --from vtp12.state            --frames 300 --out vtp19.state
build/ct.exe --teleport  5 8 8 0 --from vtp19.state            --frames 300 --out vdefrag.state   # DEFRAG
build/ct.exe --read "wram:1E00+8,wram:1E20+32,wram:1E40+32,wram:1E60+34,wram:1EF0+8,wram:1F00+32,wram:1F20+32,wram:1F80+64,wram:1E84+1" --from <state>
for N in 90 95 96 98 100 105 110 120 130 132 133 140; do
  build/ct.exe --savestate cb$N.state --frames $N
  build/ct.exe --read "wram:1E00+8" --from cb$N.state          # default --settle 0
done
build/ct.exe --savestate p70.state --frames 70
build/ct.exe --trace 300000 --from p70.state                   # NB +30 settle -> abs f100
build/ct.exe --aram --frames 133   /  --aram --frames 139
build/ct.exe --aram --from cb110.state --frames 23             # == --aram --frames 133, bit-identical
```
